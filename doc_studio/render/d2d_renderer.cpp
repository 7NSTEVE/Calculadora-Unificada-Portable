#include "d2d_renderer.hpp"
#include "card_render_common.hpp"
#include "math_box_typesetter.hpp"
#include "../core/ink_engine.hpp"
#include "../core/utf_conv.hpp"

#include <d2d1.h>
#include <dwrite.h>
#include <objbase.h>
#include <roapi.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace DocStudio {

namespace {

using Microsoft::WRL::ComPtr;

int QuantizeBucketPx(int px) noexcept {
    if (px <= 160) return 160;
    if (px <= 320) return 320;
    if (px <= 640) return 640;
    if (px <= 960) return 960;
    if (px <= 1400) return 1400;
    if (px <= 2000) return 2000;
    return 2600;
}

D2D1_COLOR_F ToD2DColor(DocColor c) noexcept {
    return D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

D2D1_RECT_F ToD2DRect(const DocRect& r) noexcept {
    return D2D1::RectF(
        static_cast<FLOAT>(r.x),
        static_cast<FLOAT>(r.y),
        static_cast<FLOAT>(r.Right()),
        static_cast<FLOAT>(r.Bottom()));
}

std::uint64_t HashBytesFnv1a64(const std::vector<std::uint8_t>& bytes) noexcept {
    std::uint64_t h = 14695981039346656037ull;
    const std::size_t n = bytes.size();
    const std::size_t step = (n > 8192u) ? (n / 4096u) : 1u;
    for (std::size_t i = 0; i < n; i += step) {
        h ^= static_cast<std::uint64_t>(bytes[i]);
        h *= 1099511628211ull;
    }
    h ^= static_cast<std::uint64_t>(n);
    h *= 1099511628211ull;
    return h;
}

// Materializes embedded PDF asset bytes (from migrated .goodnotes / .note / portable .cudoc)
// into a deterministic %TEMP%\CuadernU\pdf_cache\<key>.pdf file so WinRT PDF rendering works
// even when sourcePdfPathUtf8 is empty or originated on another device.
std::string ResolvePdfSourcePathForRender(
    const DocumentModel& doc,
    const std::string& sourcePdfPathUtf8,
    const std::string& sourcePdfAssetKey)
{
    if (!sourcePdfPathUtf8.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(std::filesystem::path(Utf8ToWide(sourcePdfPathUtf8)), ec)) {
            return sourcePdfPathUtf8;
        }
    }
    if (sourcePdfAssetKey.empty()) return sourcePdfPathUtf8;
    const auto* bytes = doc.FindAsset(sourcePdfAssetKey);
    if (!bytes || bytes->empty()) return sourcePdfPathUtf8;

    static std::mutex sCacheMtx;
    static std::unordered_map<std::string, std::string> sMaterialized;
    const std::string cacheKey = sourcePdfAssetKey + "#" + std::to_string(bytes->size()) + "#" + std::to_string(HashBytesFnv1a64(*bytes));
    {
        std::lock_guard<std::mutex> lk(sCacheMtx);
        auto it = sMaterialized.find(cacheKey);
        if (it != sMaterialized.end()) {
            std::error_code ec;
            if (std::filesystem::exists(std::filesystem::path(Utf8ToWide(it->second)), ec)) {
                return it->second;
            }
        }
    }

    wchar_t tmpRoot[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmpRoot);
    const std::filesystem::path dir = std::filesystem::path(tmpRoot) / L"CuadernU" / L"pdf_cache";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    char hexName[64]{};
    std::snprintf(hexName, sizeof(hexName), "pdf_%016llx_%zu.pdf",
        static_cast<unsigned long long>(HashBytesFnv1a64(*bytes)), bytes->size());
    const std::filesystem::path outPath = dir / hexName;
    if (!std::filesystem::exists(outPath, ec) || std::filesystem::file_size(outPath, ec) != bytes->size()) {
        std::ofstream ofs(outPath, std::ios::binary | std::ios::trunc);
        if (ofs.is_open()) {
            ofs.write(reinterpret_cast<const char*>(bytes->data()), static_cast<std::streamsize>(bytes->size()));
        }
    }
    const std::string utf8Out = WideToUtf8(outPath.wstring());
    {
        std::lock_guard<std::mutex> lk(sCacheMtx);
        sMaterialized[cacheKey] = utf8Out;
    }
    return utf8Out;
}

std::string ResolvePdfSourcePathForRender(const DocumentModel& doc, const PageModel& pg) {
    return ResolvePdfSourcePathForRender(doc, pg.sourcePdfPathUtf8, pg.sourcePdfAssetKey);
}

class D2DMathBackend final : public MathTypeset::IMathDrawBackend {
public:
    D2DMathBackend(ID2D1RenderTarget* rt, ID2D1SolidColorBrush* brush, IDWriteFactory* dw)
        : rt_(rt), brush_(brush), dw_(dw) {}

    MathTypeset::TextMetrics2D MeasureText(
        const std::string& utf8,
        double fontSizePx,
        MathTypeset::FontStyleRole role) override
    {
        MathTypeset::TextMetrics2D out{};
        if (!dw_ || utf8.empty()) {
            out.ascent = fontSizePx * 0.78;
            out.descent = fontSizePx * 0.22;
            return out;
        }
        ComPtr<IDWriteTextLayout> layout = CreateLayout(utf8, fontSizePx, role);
        if (!layout) {
            out.width = fontSizePx * 0.52 * static_cast<double>(utf8.size());
            out.ascent = fontSizePx * 0.78;
            out.descent = fontSizePx * 0.22;
            return out;
        }
        DWRITE_TEXT_METRICS tm{};
        if (SUCCEEDED(layout->GetMetrics(&tm))) {
            out.width = static_cast<double>(tm.widthIncludingTrailingWhitespace);
        }
        DWRITE_LINE_METRICS lm{};
        UINT32 actualLines = 0;
        if (SUCCEEDED(layout->GetLineMetrics(&lm, 1, &actualLines)) && actualLines >= 1) {
            out.ascent = static_cast<double>(lm.baseline);
            out.descent = (std::max)(fontSizePx * 0.18, static_cast<double>(lm.height - lm.baseline));
        } else {
            out.ascent = fontSizePx * 0.78;
            out.descent = fontSizePx * 0.22;
        }
        return out;
    }

    void DrawTextRun(
        double x,
        double baselineY,
        const std::string& utf8,
        double fontSizePx,
        MathTypeset::FontStyleRole role,
        DocColor color) override
    {
        if (!rt_ || !brush_ || !dw_ || utf8.empty()) return;
        ComPtr<IDWriteTextLayout> layout = CreateLayout(utf8, fontSizePx, role);
        if (!layout) return;
        double baseline = fontSizePx * 0.78;
        DWRITE_LINE_METRICS lm{};
        UINT32 actualLines = 0;
        if (SUCCEEDED(layout->GetLineMetrics(&lm, 1, &actualLines)) && actualLines >= 1) {
            baseline = static_cast<double>(lm.baseline);
        }
        brush_->SetColor(ToD2DColor(color));
        rt_->DrawTextLayout(
            D2D1::Point2F(static_cast<FLOAT>(x), static_cast<FLOAT>(baselineY - baseline)),
            layout.Get(), brush_, D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
    }

    void DrawLine(
        double x0, double y0,
        double x1, double y1,
        double thickness,
        DocColor color) override
    {
        if (!rt_ || !brush_) return;
        brush_->SetColor(ToD2DColor(color));
        rt_->DrawLine(
            D2D1::Point2F(static_cast<FLOAT>(x0), static_cast<FLOAT>(y0)),
            D2D1::Point2F(static_cast<FLOAT>(x1), static_cast<FLOAT>(y1)),
            brush_, static_cast<FLOAT>((std::max)(0.6, thickness)));
    }

    void DrawRoundedPill(
        double x, double y, double w, double h,
        double radius,
        DocColor fill,
        DocColor stroke,
        double strokeWidth) override
    {
        if (!rt_ || !brush_) return;
        const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(
            D2D1::RectF(static_cast<FLOAT>(x), static_cast<FLOAT>(y),
                        static_cast<FLOAT>(x + w), static_cast<FLOAT>(y + h)),
            static_cast<FLOAT>(radius), static_cast<FLOAT>(radius));
        if (fill.a > 0) {
            brush_->SetColor(ToD2DColor(fill));
            rt_->FillRoundedRectangle(rr, brush_);
        }
        if (stroke.a > 0 && strokeWidth > 0.0) {
            brush_->SetColor(ToD2DColor(stroke));
            rt_->DrawRoundedRectangle(rr, brush_, static_cast<FLOAT>(strokeWidth));
        }
    }

private:
    ComPtr<IDWriteTextLayout> CreateLayout(
        const std::string& utf8,
        double fontSizePx,
        MathTypeset::FontStyleRole role)
    {
        const wchar_t* family = L"Segoe UI";
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
        DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
        switch (role) {
        case MathTypeset::FontStyleRole::UiRegular:
            family = L"Segoe UI";
            weight = DWRITE_FONT_WEIGHT_NORMAL;
            break;
        case MathTypeset::FontStyleRole::UiSemiBold:
            family = L"Segoe UI";
            weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            break;
        case MathTypeset::FontStyleRole::MathItalic:
            family = L"Cambria Math";
            style = DWRITE_FONT_STYLE_ITALIC;
            break;
        case MathTypeset::FontStyleRole::MathRoman:
            family = L"Cambria Math";
            break;
        case MathTypeset::FontStyleRole::MathBold:
            family = L"Cambria Math";
            weight = DWRITE_FONT_WEIGHT_BOLD;
            break;
        case MathTypeset::FontStyleRole::MathSymbol:
            family = L"Cambria Math";
            break;
        case MathTypeset::FontStyleRole::KeycapLabel:
            family = L"Segoe UI";
            weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
            break;
        }
        ComPtr<IDWriteTextFormat> fmt;
        if (FAILED(dw_->CreateTextFormat(
                family, nullptr, weight, style, DWRITE_FONT_STRETCH_NORMAL,
                static_cast<FLOAT>((std::max)(5.5, fontSizePx)), L"es-ES", &fmt)) || !fmt) {
            return nullptr;
        }
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        const std::wstring wtxt = Utf8ToWide(utf8);
        ComPtr<IDWriteTextLayout> layout;
        if (FAILED(dw_->CreateTextLayout(
                wtxt.c_str(), static_cast<UINT32>(wtxt.size()),
                fmt.Get(), 4096.0f, 512.0f, &layout))) {
            return nullptr;
        }
        return layout;
    }

    ID2D1RenderTarget* rt_ = nullptr;
    ID2D1SolidColorBrush* brush_ = nullptr;
    IDWriteFactory* dw_ = nullptr;
};

} // namespace

// ---------------- PageRenderCache ----------------

PageRenderCache::PageRenderCache(std::size_t maxBytes)
    : cache_(maxBytes)
{
    worker_ = std::thread(&PageRenderCache::WorkerLoop, this);
}

PageRenderCache::~PageRenderCache() {
    Shutdown();
}

void PageRenderCache::Shutdown() {
    {
        std::lock_guard<std::mutex> lk(queueMtx_);
        if (stop_.load()) return;
        stop_.store(true);
        queue_.clear();
        inFlight_.clear();
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

PageCacheKey PageRenderCache::MakeKey(const PageModel& pg, int targetWidthPx) {
    PageCacheKey k;
    k.pageId = pg.pageId;
    k.sourcePdfPath = pg.sourcePdfPathUtf8;
    k.sourcePageIndex0 = pg.sourcePageIndex0;
    k.widthBucketPx = QuantizeBucketPx(targetWidthPx);
    k.rotationDeg = ((pg.rotationDeg % 360) + 360) % 360;
    k.cropL = static_cast<int>(std::lround(pg.crop.left * 1000.0));
    k.cropT = static_cast<int>(std::lround(pg.crop.top * 1000.0));
    k.cropR = static_cast<int>(std::lround(pg.crop.right * 1000.0));
    k.cropB = static_cast<int>(std::lround(pg.crop.bottom * 1000.0));
    return k;
}

void PageRenderCache::SetMaxBytes(std::size_t maxBytes) { cache_.SetMaxBytes(maxBytes); }
std::size_t PageRenderCache::MaxBytes() const { return cache_.MaxBytes(); }
std::size_t PageRenderCache::BytesUsed() const { return cache_.BytesUsed(); }
std::size_t PageRenderCache::EntryCount() const { return cache_.EntryCount(); }
std::uint64_t PageRenderCache::EvictionCount() const { return cache_.EvictionCount(); }

bool PageRenderCache::TryGet(const PageCacheKey& key, RasterImageBgra32& outImg) {
    return cache_.TryGet(key, outImg);
}

void PageRenderCache::PutDirect(const PageCacheKey& key, RasterImageBgra32 img) {
    const std::size_t bytes = img.ByteSize();
    cache_.Put(key, std::move(img), bytes);
}

void PageRenderCache::RequestAsync(const PageCacheKey& key, std::uint64_t generation, std::function<void()> onReady) {
    if (key.sourcePdfPath.empty()) return;
    {
        std::lock_guard<std::mutex> lk(queueMtx_);
        if (stop_.load()) return;
        auto it = inFlight_.find(key);
        if (it != inFlight_.end() && it->second >= generation) return;
        inFlight_[key] = generation;
        while (queue_.size() >= 64) {
            const auto dropped = std::move(queue_.front());
            queue_.pop_front();
            auto dit = inFlight_.find(dropped.key);
            if (dit != inFlight_.end() && dit->second <= dropped.generation) {
                inFlight_.erase(dit);
            }
        }
        queue_.push_back({ key, generation, std::move(onReady) });
    }
    cv_.notify_one();
}

void PageRenderCache::InvalidateAll(std::uint64_t newGeneration) {
    activeGen_.store(newGeneration, std::memory_order_release);
    cache_.Clear();
    std::lock_guard<std::mutex> lk(queueMtx_);
    queue_.clear();
    inFlight_.clear();
}

void PageRenderCache::WorkerLoop() {
    struct ScopedRoMta {
        HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
        ~ScopedRoMta() { if (SUCCEEDED(hr)) RoUninitialize(); }
    } roMta;
    while (true) {
        PendingJob job;
        {
            std::unique_lock<std::mutex> lk(queueMtx_);
            cv_.wait(lk, [&]() { return stop_.load() || !queue_.empty(); });
            if (stop_.load()) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        if (job.generation < activeGen_.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lk(queueMtx_);
            inFlight_.erase(job.key);
            continue;
        }

        RasterImageBgra32 existing;
        if (cache_.TryGet(job.key, existing)) {
            std::lock_guard<std::mutex> lk(queueMtx_);
            inFlight_.erase(job.key);
            continue;
        }

        NormalizedCropRect crop{
            job.key.cropL / 1000.0,
            job.key.cropT / 1000.0,
            job.key.cropR / 1000.0,
            job.key.cropB / 1000.0
        };
        RasterImageBgra32 rendered;
        const std::wstring wpath = Utf8ToWide(job.key.sourcePdfPath);
        const bool ok = WinRtPdfRenderer::RenderPageToBgra32(
            wpath,
            job.key.sourcePageIndex0,
            job.key.widthBucketPx,
            job.key.rotationDeg,
            crop,
            rendered,
            &stop_);

        {
            std::lock_guard<std::mutex> lk(queueMtx_);
            inFlight_.erase(job.key);
        }

        if (ok && job.generation >= activeGen_.load(std::memory_order_acquire)) {
            const std::size_t bytes = rendered.ByteSize();
            cache_.Put(job.key, std::move(rendered), bytes);
            if (job.onReady) job.onReady();
        }
    }
}

// ---------------- D2DRenderer ----------------

struct D2DRenderer::Impl {
    ComPtr<ID2D1Factory> d2dFactory;
    ComPtr<IDWriteFactory> dwFactory;
    ComPtr<IWICImagingFactory> wicFactory;

    HWND hwnd = nullptr;
    ComPtr<ID2D1HwndRenderTarget> hwndRt;
    ComPtr<ID2D1StrokeStyle> roundStroke;
    ComPtr<ID2D1StrokeStyle> dashStroke;

    // Lightweight per-render-target GPU bitmap cache (cleared cleanly on DiscardDeviceResources)
    struct CachedGpuPage {
        PageCacheKey key;
        ComPtr<ID2D1Bitmap> bmp;
    };
    std::vector<CachedGpuPage> pageGpuBitmaps;

    struct CachedGpuAsset {
        std::string key;
        ComPtr<ID2D1Bitmap> bmp;
    };
    std::vector<CachedGpuAsset> assetGpuBitmaps;

    PageRenderCache* activePageCache = nullptr;
    std::uint64_t activeAsyncGen = 0;
    std::function<void()> activeOnAsyncReady;
    bool activeSyncOffscreen = false;

    bool factoriesReady = false;
    bool didCoInit = false;

    bool EnsureStrokeStyles() {
        if (!d2dFactory) return false;
        if (!roundStroke) {
            D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
                D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f);
            d2dFactory->CreateStrokeStyle(props, nullptr, 0, &roundStroke);
        }
        if (!dashStroke) {
            D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
                D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_DASH, 0.0f);
            d2dFactory->CreateStrokeStyle(props, nullptr, 0, &dashStroke);
        }
        return true;
    }

    ID2D1Bitmap* UploadBgra32(ID2D1RenderTarget* rt, const RasterImageBgra32& img) {
        if (!rt || !img.Valid()) return nullptr;
        D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComPtr<ID2D1Bitmap> bmp;
        const HRESULT hr = rt->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(img.width), static_cast<UINT32>(img.height)),
            img.pixels.data(),
            static_cast<UINT32>(img.width * 4),
            props,
            &bmp);
        if (FAILED(hr) || !bmp) return nullptr;
        return bmp.Detach();
    }

    ComPtr<ID2D1Bitmap> AcquireGpuBitmapForEncodedAsset(
        ID2D1RenderTarget* rt,
        const std::string& cacheTag,
        const std::vector<std::uint8_t>& bytes,
        bool cacheResult)
    {
        if (!rt || !wicFactory || bytes.empty()) return nullptr;
        if (cacheResult) {
            for (const auto& entry : assetGpuBitmaps) {
                if (entry.key == cacheTag) return entry.bmp;
            }
        }
        ComPtr<IWICStream> stream;
        if (FAILED(wicFactory->CreateStream(&stream))) return nullptr;
        if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()), static_cast<DWORD>(bytes.size())))) return nullptr;
        ComPtr<IWICBitmapDecoder> dec;
        if (FAILED(wicFactory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &dec))) return nullptr;
        ComPtr<IWICBitmapFrameDecode> frame;
        if (FAILED(dec->GetFrame(0, &frame))) return nullptr;
        ComPtr<IWICFormatConverter> conv;
        if (FAILED(wicFactory->CreateFormatConverter(&conv))) return nullptr;
        if (FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom))) return nullptr;

        ComPtr<ID2D1Bitmap> bmp;
        if (FAILED(rt->CreateBitmapFromWicBitmap(conv.Get(), nullptr, &bmp)) || !bmp) return nullptr;

        if (cacheResult) {
            if (assetGpuBitmaps.size() > 64) assetGpuBitmaps.erase(assetGpuBitmaps.begin());
            assetGpuBitmaps.push_back({ cacheTag, bmp });
        }
        return bmp;
    }

    void DrawPaperPattern(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush,
        const DocRect& worldRegion,
        PaperPattern pattern,
        const Camera2D& cam,
        bool darkPaper = false)
    {
        if (pattern == PaperPattern::Plain) return;

        auto drawGridLines = [&](double stepWorld, DocColor col, float pxThickness) {
            const double screenStep = stepWorld * cam.zoom;
            if (screenStep < 4.0) return; // avoid subpixel moire when zoomed very far out
            brush->SetColor(ToD2DColor(col));
            const double startX = std::floor(worldRegion.x / stepWorld) * stepWorld;
            const double endX = worldRegion.Right();
            for (double x = startX; x <= endX; x += stepWorld) {
                const DocPoint p0 = cam.WorldToScreen(x, worldRegion.y);
                const DocPoint p1 = cam.WorldToScreen(x, worldRegion.Bottom());
                rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(p0.x), static_cast<FLOAT>(p0.y)),
                             D2D1::Point2F(static_cast<FLOAT>(p1.x), static_cast<FLOAT>(p1.y)),
                             brush, pxThickness);
            }
            const double startY = std::floor(worldRegion.y / stepWorld) * stepWorld;
            const double endY = worldRegion.Bottom();
            for (double y = startY; y <= endY; y += stepWorld) {
                const DocPoint p0 = cam.WorldToScreen(worldRegion.x, y);
                const DocPoint p1 = cam.WorldToScreen(worldRegion.Right(), y);
                rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(p0.x), static_cast<FLOAT>(p0.y)),
                             D2D1::Point2F(static_cast<FLOAT>(p1.x), static_cast<FLOAT>(p1.y)),
                             brush, pxThickness);
            }
        };

        if (pattern == PaperPattern::Ruled) {
            const double step = 24.0;
            if (step * cam.zoom >= 4.0) {
                brush->SetColor(ToD2DColor(darkPaper ? DocColor::RGBA(44, 52, 66, 255) : DocColor::RGBA(205, 221, 244, 255)));
                const double startY = std::floor(worldRegion.y / step) * step;
                for (double y = startY; y <= worldRegion.Bottom(); y += step) {
                    const DocPoint p0 = cam.WorldToScreen(worldRegion.x, y);
                    const DocPoint p1 = cam.WorldToScreen(worldRegion.Right(), y);
                    rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(p0.x), static_cast<FLOAT>(p0.y)),
                                 D2D1::Point2F(static_cast<FLOAT>(p1.x), static_cast<FLOAT>(p1.y)),
                                 brush, 1.0f);
                }
                const double marginX = worldRegion.x + 56.0;
                const DocPoint m0 = cam.WorldToScreen(marginX, worldRegion.y);
                const DocPoint m1 = cam.WorldToScreen(marginX, worldRegion.Bottom());
                brush->SetColor(ToD2DColor(darkPaper ? DocColor::RGBA(120, 52, 60, 220) : DocColor::RGBA(240, 150, 155, 255)));
                rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(m0.x), static_cast<FLOAT>(m0.y)),
                             D2D1::Point2F(static_cast<FLOAT>(m1.x), static_cast<FLOAT>(m1.y)),
                             brush, 1.2f);
            }
        } else if (pattern == PaperPattern::Grid) {
            drawGridLines(20.0, darkPaper ? DocColor::RGBA(40, 46, 58, 255) : DocColor::RGBA(218, 228, 240, 255), 1.0f);
        } else if (pattern == PaperPattern::Dots) {
            const double step = 20.0;
            if (step * cam.zoom >= 6.0) {
                brush->SetColor(ToD2DColor(darkPaper ? DocColor::RGBA(62, 70, 86, 255) : DocColor::RGBA(175, 186, 204, 255)));
                const float r = (std::max)(1.0f, static_cast<float>(1.2 * cam.zoom));
                const double startX = std::floor(worldRegion.x / step) * step;
                const double startY = std::floor(worldRegion.y / step) * step;
                for (double y = startY; y <= worldRegion.Bottom(); y += step) {
                    for (double x = startX; x <= worldRegion.Right(); x += step) {
                        const DocPoint sp = cam.WorldToScreen(x, y);
                        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<FLOAT>(sp.x), static_cast<FLOAT>(sp.y)), r, r), brush);
                    }
                }
            }
        } else if (pattern == PaperPattern::Millimeter) {
            drawGridLines(8.0, darkPaper ? DocColor::RGBA(36, 44, 44, 255) : DocColor::RGBA(228, 240, 232, 255), 0.7f);
            drawGridLines(40.0, darkPaper ? DocColor::RGBA(52, 68, 62, 255) : DocColor::RGBA(178, 214, 190, 255), 1.1f);
        } else if (pattern == PaperPattern::Isometric) {
            const double step = 24.0;
            if (step * cam.zoom >= 5.0) {
                const double dy = step * 0.8660254037844386;
                brush->SetColor(ToD2DColor(darkPaper ? DocColor::RGBA(44, 52, 66, 255) : DocColor::RGBA(206, 219, 236, 255)));
                // Vertical lines
                const double startX = std::floor(worldRegion.x / (step * 0.5)) * (step * 0.5);
                for (double x = startX; x <= worldRegion.Right(); x += step * 0.5) {
                    const DocPoint p0 = cam.WorldToScreen(x, worldRegion.y);
                    const DocPoint p1 = cam.WorldToScreen(x, worldRegion.Bottom());
                    rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(p0.x), static_cast<FLOAT>(p0.y)),
                                 D2D1::Point2F(static_cast<FLOAT>(p1.x), static_cast<FLOAT>(p1.y)), brush, 0.8f);
                }
                // +30 deg and -30 deg diagonals (slope = +/- 1/sqrt(3))
                const double slope = 0.5773502691896257;
                const double spanX = worldRegion.w;
                const double extraY = spanX * slope;
                const double startY = std::floor((worldRegion.y - extraY) / dy) * dy;
                for (double y = startY; y <= worldRegion.Bottom() + extraY; y += dy) {
                    const DocPoint a0 = cam.WorldToScreen(worldRegion.x, y);
                    const DocPoint a1 = cam.WorldToScreen(worldRegion.Right(), y + spanX * slope);
                    rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(a0.x), static_cast<FLOAT>(a0.y)),
                                 D2D1::Point2F(static_cast<FLOAT>(a1.x), static_cast<FLOAT>(a1.y)), brush, 0.8f);
                    const DocPoint b0 = cam.WorldToScreen(worldRegion.x, y);
                    const DocPoint b1 = cam.WorldToScreen(worldRegion.Right(), y - spanX * slope);
                    rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(b0.x), static_cast<FLOAT>(b0.y)),
                                 D2D1::Point2F(static_cast<FLOAT>(b1.x), static_cast<FLOAT>(b1.y)), brush, 0.8f);
                }
            }
        } else if (pattern == PaperPattern::LogLog) {
            const double decade = 140.0;
            if (decade * cam.zoom >= 18.0) {
                const double startX = std::floor(worldRegion.x / decade) * decade;
                const double startY = std::floor(worldRegion.y / decade) * decade;
                for (double dx0 = startX; dx0 <= worldRegion.Right(); dx0 += decade) {
                    for (int m = 1; m <= 9; ++m) {
                        const double x = dx0 + std::log10(static_cast<double>(m)) * decade;
                        brush->SetColor(ToD2DColor(m == 1
                            ? (darkPaper ? DocColor::RGBA(68, 92, 118, 255) : DocColor::RGBA(156, 188, 224, 255))
                            : (darkPaper ? DocColor::RGBA(40, 50, 66, 255) : DocColor::RGBA(218, 230, 244, 255))));
                        const DocPoint p0 = cam.WorldToScreen(x, worldRegion.y);
                        const DocPoint p1 = cam.WorldToScreen(x, worldRegion.Bottom());
                        rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(p0.x), static_cast<FLOAT>(p0.y)),
                                     D2D1::Point2F(static_cast<FLOAT>(p1.x), static_cast<FLOAT>(p1.y)), brush, m == 1 ? 1.2f : 0.7f);
                    }
                }
                for (double dy0 = startY; dy0 <= worldRegion.Bottom(); dy0 += decade) {
                    for (int m = 1; m <= 9; ++m) {
                        const double y = dy0 + (1.0 - std::log10(static_cast<double>(m))) * decade;
                        brush->SetColor(ToD2DColor(m == 1
                            ? (darkPaper ? DocColor::RGBA(68, 92, 118, 255) : DocColor::RGBA(156, 188, 224, 255))
                            : (darkPaper ? DocColor::RGBA(40, 50, 66, 255) : DocColor::RGBA(218, 230, 244, 255))));
                        const DocPoint p0 = cam.WorldToScreen(worldRegion.x, y);
                        const DocPoint p1 = cam.WorldToScreen(worldRegion.Right(), y);
                        rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(p0.x), static_cast<FLOAT>(p0.y)),
                                     D2D1::Point2F(static_cast<FLOAT>(p1.x), static_cast<FLOAT>(p1.y)), brush, m == 1 ? 1.2f : 0.7f);
                    }
                }
            }
        } else if (pattern == PaperPattern::Polar) {
            const double cx = worldRegion.x + worldRegion.w * 0.5;
            const double cy = worldRegion.y + worldRegion.h * 0.5;
            const double maxR = (std::min)(worldRegion.w, worldRegion.h) * 0.44;
            const DocPoint cScr = cam.WorldToScreen(cx, cy);
            brush->SetColor(ToD2DColor(darkPaper ? DocColor::RGBA(56, 72, 96, 255) : DocColor::RGBA(190, 210, 232, 255)));
            for (int ring = 1; ring <= 8; ++ring) {
                const float rPx = static_cast<float>((maxR * ring / 8.0) * cam.zoom);
                rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<FLOAT>(cScr.x), static_cast<FLOAT>(cScr.y)), rPx, rPx),
                    brush, (ring % 2 == 0) ? 1.1f : 0.7f);
            }
            for (int deg = 0; deg < 360; deg += 30) {
                const double rad = deg * (3.14159265358979323846 / 180.0);
                const DocPoint ep = cam.WorldToScreen(cx + maxR * std::cos(rad), cy + maxR * std::sin(rad));
                rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(cScr.x), static_cast<FLOAT>(cScr.y)),
                             D2D1::Point2F(static_cast<FLOAT>(ep.x), static_cast<FLOAT>(ep.y)), brush, (deg % 90 == 0) ? 1.2f : 0.7f);
            }
        } else if (pattern == PaperPattern::SmithChart) {
            const double cx = worldRegion.x + worldRegion.w * 0.5;
            const double cy = worldRegion.y + worldRegion.h * 0.5;
            const double R = (std::min)(worldRegion.w, worldRegion.h) * 0.42;
            const DocPoint cScr = cam.WorldToScreen(cx, cy);
            const float rPx = static_cast<float>(R * cam.zoom);
            brush->SetColor(ToD2DColor(darkPaper ? DocColor::RGBA(72, 118, 128, 240) : DocColor::RGBA(146, 186, 198, 255)));
            rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<FLOAT>(cScr.x), static_cast<FLOAT>(cScr.y)), rPx, rPx), brush, 1.4f);
            // Horizontal real axis
            rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(cScr.x) - rPx, static_cast<FLOAT>(cScr.y)),
                         D2D1::Point2F(static_cast<FLOAT>(cScr.x) + rPx, static_cast<FLOAT>(cScr.y)), brush, 1.1f);
            // Constant-resistance circles: center (r/(1+r), 0), radius 1/(1+r)
            for (double rv : { 0.2, 0.5, 1.0, 2.0, 5.0 }) {
                const double rcX = cx + (rv / (1.0 + rv)) * R;
                const float radPx = static_cast<float>((1.0 / (1.0 + rv)) * R * cam.zoom);
                const DocPoint sc = cam.WorldToScreen(rcX, cy);
                rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<FLOAT>(sc.x), static_cast<FLOAT>(sc.y)), radPx, radPx),
                    brush, (std::abs(rv - 1.0) < 0.01) ? 1.2f : 0.8f);
            }
            // Constant-reactance arcs clipped inside unit circle: center (1, +/-1/x), radius 1/|x|
            for (double xv : { -2.0, -1.0, -0.5, 0.5, 1.0, 2.0 }) {
                const double xc = 1.0;
                const double yc = 1.0 / xv;
                const double radN = std::abs(1.0 / xv);
                DocPoint prev{};
                bool havePrev = false;
                for (int s = 0; s <= 48; ++s) {
                    const double ang = static_cast<double>(s) * (6.28318530717958647692 / 48.0);
                    const double gx = xc + radN * std::cos(ang);
                    const double gy = yc + radN * std::sin(ang);
                    if (gx * gx + gy * gy <= 1.005) {
                        const DocPoint cur = cam.WorldToScreen(cx + gx * R, cy - gy * R);
                        if (havePrev) {
                            rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(prev.x), static_cast<FLOAT>(prev.y)),
                                         D2D1::Point2F(static_cast<FLOAT>(cur.x), static_cast<FLOAT>(cur.y)), brush, 0.75f);
                        }
                        prev = cur;
                        havePrev = true;
                    } else {
                        havePrev = false;
                    }
                }
            }
        } else if (pattern == PaperPattern::Cornell) {
            const double headerY = worldRegion.y + 72.0;
            const double summaryY = worldRegion.Bottom() - 108.0;
            const double cueX = worldRegion.x + worldRegion.w * 0.28;
            // Ruled main body
            brush->SetColor(ToD2DColor(darkPaper ? DocColor::RGBA(44, 52, 66, 255) : DocColor::RGBA(214, 226, 244, 255)));
            for (double y = headerY + 24.0; y < summaryY; y += 24.0) {
                const DocPoint p0 = cam.WorldToScreen(cueX, y);
                const DocPoint p1 = cam.WorldToScreen(worldRegion.Right() - 24.0, y);
                rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(p0.x), static_cast<FLOAT>(p0.y)),
                             D2D1::Point2F(static_cast<FLOAT>(p1.x), static_cast<FLOAT>(p1.y)), brush, 0.9f);
            }
            // Cornell dividers
            brush->SetColor(ToD2DColor(darkPaper ? DocColor::RGBA(156, 76, 86, 220) : DocColor::RGBA(218, 124, 134, 255)));
            const DocPoint h0 = cam.WorldToScreen(worldRegion.x + 18.0, headerY);
            const DocPoint h1 = cam.WorldToScreen(worldRegion.Right() - 18.0, headerY);
            rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(h0.x), static_cast<FLOAT>(h0.y)),
                         D2D1::Point2F(static_cast<FLOAT>(h1.x), static_cast<FLOAT>(h1.y)), brush, 1.4f);
            const DocPoint s0 = cam.WorldToScreen(worldRegion.x + 18.0, summaryY);
            const DocPoint s1 = cam.WorldToScreen(worldRegion.Right() - 18.0, summaryY);
            rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(s0.x), static_cast<FLOAT>(s0.y)),
                         D2D1::Point2F(static_cast<FLOAT>(s1.x), static_cast<FLOAT>(s1.y)), brush, 1.4f);
            const DocPoint c0 = cam.WorldToScreen(cueX, headerY);
            const DocPoint c1 = cam.WorldToScreen(cueX, summaryY);
            rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(c0.x), static_cast<FLOAT>(c0.y)),
                         D2D1::Point2F(static_cast<FLOAT>(c1.x), static_cast<FLOAT>(c1.y)), brush, 1.4f);
        } else if (pattern == PaperPattern::MusicStaff) {
            const double staffGap = 72.0;
            const double lineStep = 7.5;
            if (lineStep * cam.zoom >= 2.5) {
                brush->SetColor(ToD2DColor(darkPaper ? DocColor::RGBA(72, 82, 100, 255) : DocColor::RGBA(168, 180, 198, 255)));
                for (double base = worldRegion.y + 52.0; base + 4.0 * lineStep < worldRegion.Bottom() - 28.0; base += staffGap) {
                    for (int ln = 0; ln < 5; ++ln) {
                        const double y = base + ln * lineStep;
                        const DocPoint p0 = cam.WorldToScreen(worldRegion.x + 36.0, y);
                        const DocPoint p1 = cam.WorldToScreen(worldRegion.Right() - 36.0, y);
                        rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(p0.x), static_cast<FLOAT>(p0.y)),
                                     D2D1::Point2F(static_cast<FLOAT>(p1.x), static_cast<FLOAT>(p1.y)), brush, 0.9f);
                    }
                }
            }
        }
    }

    void DrawAnnotationObject(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush,
        const DocumentModel& doc,
        const AnnotationObject& obj,
        const Camera2D& cam,
        double originWorldX,
        double originWorldY,
        bool isSelected,
        bool cacheGpuAssets,
        const ViewportOverlayState* overlay = nullptr)
    {
        double layerOpacity = 1.0;
        if (!RenderCommon::ShouldDrawObjectOnLayer(doc, obj, layerOpacity, overlay)) return;
        layerOpacity *= std::clamp(obj.opacity, 0.05, 1.0);

        auto toScreen = [&](double lx, double ly) -> D2D1_POINT_2F {
            const DocPoint sp = cam.WorldToScreen(originWorldX + lx, originWorldY + ly);
            return D2D1::Point2F(static_cast<FLOAT>(sp.x), static_cast<FLOAT>(sp.y));
        };
        auto rectToScreen = [&](const DocRect& lr) -> D2D1_RECT_F {
            const DocRect wr{ originWorldX + lr.x, originWorldY + lr.y, lr.w, lr.h };
            return ToD2DRect(cam.WorldRectToScreen(wr));
        };

        // Optional object rotation transform (F-107, F-131)
        D2D1::Matrix3x2F savedTransform = D2D1::Matrix3x2F::Identity();
        const bool applyRotation = (std::abs(obj.rotationDeg) > 0.15);
        if (applyRotation) {
            rt->GetTransform(&savedTransform);
            const D2D1_RECT_F rotBox = rectToScreen(obj.bounds);
            const D2D1_POINT_2F center = D2D1::Point2F((rotBox.left + rotBox.right) * 0.5f, (rotBox.top + rotBox.bottom) * 0.5f);
            rt->SetTransform(D2D1::Matrix3x2F::Rotation(static_cast<FLOAT>(obj.rotationDeg), center) * savedTransform);
        }

        if (obj.kind == ObjectKind::InkStroke) {
            const std::vector<InkPoint> filteredPts = (overlay && overlay->replayActive)
                ? InkEngine::FilterInkPointsAtReplayTime(obj, overlay->replayCursorMs)
                : obj.inkPoints;
            if (filteredPts.empty()) return;

            DocColor baseCol = RenderCommon::ApplyLayerOpacity(obj.strokeColor, layerOpacity);
            if ((obj.isHighlighter || obj.penStyle == InkPenStyle::Highlighter) && baseCol.a > 140) {
                baseCol.a = 98;
            }
            if (filteredPts.size() == 1) {
                brush->SetColor(ToD2DColor(baseCol));
                const float r = (std::max)(1.0f, static_cast<float>(obj.strokeWidth * 0.5 * cam.zoom));
                rt->FillEllipse(D2D1::Ellipse(toScreen(filteredPts[0].x, filteredPts[0].y), r, r), brush);
            } else {
                for (std::size_t i = 1; i < filteredPts.size(); ++i) {
                    const auto& p0 = filteredPts[i - 1];
                    const auto& p1 = filteredPts[i];
                    const double segDist = std::hypot(p1.x - p0.x, p1.y - p0.y);
                    const double dtMs = (p1.timestampMs > p0.timestampMs)
                        ? static_cast<double>(p1.timestampMs - p0.timestampMs) : 16.0;
                    const double speed = (segDist * 1000.0) / (std::max)(1.0, dtMs);
                    const InkPoint mid{
                        0.5 * (p0.x + p1.x),
                        0.5 * (p0.y + p1.y),
                        0.5f * (p0.pressure + p1.pressure),
                        0.5f * (p0.tiltXDeg + p1.tiltXDeg),
                        0.5f * (p0.tiltYDeg + p1.tiltYDeg),
                        p1.timestampMs
                    };
                    const double widthWorld = InkEngine::ComputeEffectiveWidth(mid, obj, speed);
                    const double tiltAlpha = InkEngine::ComputeTiltAlphaMultiplier(mid, obj.penStyle);
                    DocColor segCol = baseCol;
                    segCol.a = static_cast<std::uint8_t>(std::clamp(std::lround(baseCol.a * tiltAlpha), 1L, 255L));
                    brush->SetColor(ToD2DColor(segCol));
                    const float wPx = (std::max)(0.8f, static_cast<float>(widthWorld * cam.zoom));
                    rt->DrawLine(toScreen(p0.x, p0.y), toScreen(p1.x, p1.y), brush, wPx, roundStroke.Get());
                }
            }
        } else if (obj.kind == ObjectKind::Shape) {
            const float wPx = (std::max)(1.0f, static_cast<float>(obj.strokeWidth * cam.zoom));
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            if (obj.shapeKind == ShapeKind::Rectangle || obj.shapeKind == ShapeKind::SignatureStamp) {
                if (obj.isRedaction) {
                    brush->SetColor(ToD2DColor(DocColor::RGBA(10, 12, 16, 255)));
                    rt->FillRectangle(sr, brush);
                } else {
                    if (obj.fillColor.a > 0) {
                        brush->SetColor(ToD2DColor(RenderCommon::ApplyLayerOpacity(obj.fillColor, layerOpacity)));
                        rt->FillRectangle(sr, brush);
                    }
                    brush->SetColor(ToD2DColor(RenderCommon::ApplyLayerOpacity(obj.strokeColor, layerOpacity)));
                    rt->DrawRectangle(sr, brush, wPx);
                }
                if (obj.shapeKind == ShapeKind::SignatureStamp && !obj.textUtf8.empty() && dwFactory) {
                    const D2D1_RECT_F inner = D2D1::RectF(sr.left + 3.0f, sr.top + 3.0f, sr.right - 3.0f, sr.bottom - 3.0f);
                    rt->DrawRectangle(inner, brush, 0.8f);
                    const float fontPx = (std::max)(8.0f, static_cast<float>((obj.fontSizePt > 0 ? obj.fontSizePt : 10.5) * cam.zoom));
                    ComPtr<IDWriteTextFormat> fmt;
                    if (SUCCEEDED(dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontPx, L"es-ES", &fmt)) && fmt) {
                        const std::wstring wtxt = Utf8ToWide(obj.textUtf8);
                        rt->DrawTextW(wtxt.c_str(), static_cast<UINT32>(wtxt.size()), fmt.Get(),
                            D2D1::RectF(inner.left + 4.0f, inner.top + 4.0f, inner.right - 4.0f, inner.bottom - 4.0f), brush);
                    }
                }
            } else if (obj.shapeKind == ShapeKind::Ellipse) {
                const D2D1_ELLIPSE ell = D2D1::Ellipse(
                    D2D1::Point2F((sr.left + sr.right) * 0.5f, (sr.top + sr.bottom) * 0.5f),
                    (sr.right - sr.left) * 0.5f,
                    (sr.bottom - sr.top) * 0.5f);
                if (obj.fillColor.a > 0) {
                    brush->SetColor(ToD2DColor(obj.fillColor));
                    rt->FillEllipse(ell, brush);
                }
                brush->SetColor(ToD2DColor(obj.strokeColor));
                rt->DrawEllipse(ell, brush, wPx);
            } else {
                const D2D1_POINT_2F p0 = toScreen(obj.shapeStart.x, obj.shapeStart.y);
                const D2D1_POINT_2F p1 = toScreen(obj.shapeEnd.x, obj.shapeEnd.y);
                brush->SetColor(ToD2DColor(obj.strokeColor));
                rt->DrawLine(p0, p1, brush, wPx, roundStroke.Get());
                if (obj.shapeKind == ShapeKind::Arrow || obj.shapeKind == ShapeKind::Connector) {
                    const double dx = p1.x - p0.x;
                    const double dy = p1.y - p0.y;
                    const double len = std::hypot(dx, dy);
                    if (len > 2.0) {
                        const double ux = dx / len, uy = dy / len;
                        const double hs = (std::max)(8.0, obj.strokeWidth * 3.2 * cam.zoom);
                        const double px = -uy * (hs * 0.45), py = ux * (hs * 0.45);
                        const D2D1_POINT_2F a = D2D1::Point2F(static_cast<FLOAT>(p1.x - ux * hs + px), static_cast<FLOAT>(p1.y - uy * hs + py));
                        const D2D1_POINT_2F b = D2D1::Point2F(static_cast<FLOAT>(p1.x - ux * hs - px), static_cast<FLOAT>(p1.y - uy * hs - py));
                        rt->DrawLine(a, p1, brush, wPx, roundStroke.Get());
                        rt->DrawLine(b, p1, brush, wPx, roundStroke.Get());
                    }
                    if (obj.shapeKind == ShapeKind::Connector) {
                        rt->FillEllipse(D2D1::Ellipse(p0, 3.5f, 3.5f), brush);
                        rt->FillEllipse(D2D1::Ellipse(p1, 3.5f, 3.5f), brush);
                    }
                }
            }
        } else if (obj.kind == ObjectKind::TextBox) {
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            if (obj.fillColor.a > 0) {
                brush->SetColor(ToD2DColor(obj.fillColor));
                rt->FillRectangle(sr, brush);
            }
            if (dwFactory && !obj.textUtf8.empty()) {
                const float fontPx = (std::max)(6.0f, static_cast<float>(obj.fontSizePt * cam.zoom));
                if (MathTypeset::TextContainsMathMarkup(obj.textUtf8)) {
                    rt->PushAxisAlignedClip(sr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    D2DMathBackend mbk(rt, brush, dwFactory.Get());
                    std::istringstream iss(obj.textUtf8);
                    std::string line;
                    double curY = sr.top + 2.0;
                    const double maxW = (std::max)(12.0, static_cast<double>(sr.right - sr.left) - 4.0);
                    while (std::getline(iss, line) && curY < sr.bottom) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        curY += MathTypeset::DrawRichMathLine(mbk, line, sr.left + 2.0, curY, maxW, fontPx, obj.strokeColor, false);
                    }
                    rt->PopAxisAlignedClip();
                } else {
                    brush->SetColor(ToD2DColor(obj.strokeColor));
                    const std::wstring wfam = Utf8ToWide(obj.fontFamilyUtf8.empty() ? "Segoe UI" : obj.fontFamilyUtf8);
                    ComPtr<IDWriteTextFormat> fmt;
                    if (SUCCEEDED(dwFactory->CreateTextFormat(
                            wfam.c_str(), nullptr,
                            obj.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                            obj.italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL,
                            DWRITE_FONT_STRETCH_NORMAL, fontPx, L"es-ES", &fmt)) && fmt) {
                        const std::wstring wtxt = Utf8ToWide(obj.textUtf8);
                        if (obj.underline) {
                            ComPtr<IDWriteTextLayout> layout;
                            if (SUCCEEDED(dwFactory->CreateTextLayout(
                                    wtxt.c_str(), static_cast<UINT32>(wtxt.size()), fmt.Get(),
                                    (std::max)(4.0f, sr.right - sr.left), (std::max)(4.0f, sr.bottom - sr.top), &layout)) && layout) {
                                layout->SetUnderline(TRUE, DWRITE_TEXT_RANGE{ 0, static_cast<UINT32>(wtxt.size()) });
                                rt->DrawTextLayout(D2D1::Point2F(sr.left, sr.top), layout.Get(), brush);
                            } else {
                                rt->DrawTextW(wtxt.c_str(), static_cast<UINT32>(wtxt.size()), fmt.Get(), sr, brush);
                            }
                        } else {
                            rt->DrawTextW(wtxt.c_str(), static_cast<UINT32>(wtxt.size()), fmt.Get(), sr, brush);
                        }
                    }
                }
            }
        } else if (obj.kind == ObjectKind::Image) {
            const std::vector<std::uint8_t>* bytesPtr = !obj.pngBytes.empty() ? &obj.pngBytes : doc.FindAsset(obj.assetKey);
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            if (bytesPtr && !bytesPtr->empty()) {
                const std::string baseTag = !obj.assetKey.empty() ? obj.assetKey : ("inline_" + std::to_string(obj.id));
                const std::string tag = baseTag + "#" + std::to_string(bytesPtr->size()) + "#" + std::to_string(HashBytesFnv1a64(*bytesPtr));
                ComPtr<ID2D1Bitmap> bmp = AcquireGpuBitmapForEncodedAsset(rt, tag, *bytesPtr, cacheGpuAssets);
                if (bmp) {
                    const D2D1_SIZE_F bsz = bmp->GetSize();
                    NormalizedCropRect ic = obj.imageCrop;
                    ic.Clamp();
                    const D2D1_RECT_F srcR = D2D1::RectF(
                        static_cast<FLOAT>(ic.left * bsz.width),
                        static_cast<FLOAT>(ic.top * bsz.height),
                        static_cast<FLOAT>(ic.right * bsz.width),
                        static_cast<FLOAT>(ic.bottom * bsz.height));
                    rt->DrawBitmap(bmp.Get(), sr, static_cast<FLOAT>(layerOpacity), D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &srcR);
                }
            } else {
                brush->SetColor(ToD2DColor(DocColor::RGBA(200, 210, 224, 255)));
                rt->FillRectangle(sr, brush);
            }
        } else if (obj.kind == ObjectKind::PdfCard) {
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            const bool isDarkCard = (overlay && overlay->darkTheme);
            const bool isHcCard = (overlay && overlay->highContrast);
            // Subtle Apple HIG sheet elevation shadow
            brush->SetColor(ToD2DColor(RenderCommon::ApplyLayerOpacity(
                isDarkCard ? DocColor::RGBA(0, 0, 0, 76) : DocColor::RGBA(15, 23, 42, 24), layerOpacity)));
            rt->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(sr.left + 1.0f, sr.top + 2.5f, sr.right + 1.5f, sr.bottom + 3.0f), 3.0f, 3.0f), brush);

            brush->SetColor(ToD2DColor(RenderCommon::ApplyLayerOpacity(DocColor::RGBA(255, 255, 255, 255), layerOpacity)));
            rt->FillRectangle(sr, brush);
            bool drewCardRaster = false;
            const std::vector<std::uint8_t>* bytesPtr = !obj.pngBytes.empty() ? &obj.pngBytes : doc.FindAsset(obj.assetKey);
            if (bytesPtr && !bytesPtr->empty()) {
                const std::string tag = !obj.assetKey.empty() ? obj.assetKey : ("pdfcard_" + std::to_string(obj.id));
                if (ComPtr<ID2D1Bitmap> bmp = AcquireGpuBitmapForEncodedAsset(rt, tag, *bytesPtr, cacheGpuAssets)) {
                    rt->DrawBitmap(bmp.Get(), sr, static_cast<FLOAT>(layerOpacity));
                    drewCardRaster = true;
                }
            } else if (!obj.pdfSourcePathUtf8.empty() || !obj.pdfSourceAssetKey.empty()) {
                const std::string resolvedPdfPath = ResolvePdfSourcePathForRender(doc, obj.pdfSourcePathUtf8, obj.pdfSourceAssetKey);
                if (!resolvedPdfPath.empty()) {
                    const int targetW = static_cast<int>(std::lround((std::max)(80.0, static_cast<double>(sr.right - sr.left))));
                    PageCacheKey key{};
                    key.pageId = 0x8000000000000000ull | obj.id;
                    key.sourcePdfPath = resolvedPdfPath;
                    key.sourcePageIndex0 = obj.pdfSourcePageIndex0;
                    key.widthBucketPx = QuantizeBucketPx(targetW);
                    key.rotationDeg = 0;
                    key.cropL = static_cast<int>(std::lround(obj.imageCrop.left * 1000.0));
                    key.cropT = static_cast<int>(std::lround(obj.imageCrop.top * 1000.0));
                    key.cropR = static_cast<int>(std::lround(obj.imageCrop.right * 1000.0));
                    key.cropB = static_cast<int>(std::lround(obj.imageCrop.bottom * 1000.0));

                    RasterImageBgra32 img;
                    bool haveRaster = activePageCache && activePageCache->TryGet(key, img);
                    if (!haveRaster && activeSyncOffscreen) {
                        NormalizedCropRect c = obj.imageCrop;
                        c.Clamp();
                        haveRaster = WinRtPdfRenderer::RenderPageToBgra32(
                            Utf8ToWide(resolvedPdfPath), obj.pdfSourcePageIndex0,
                            key.widthBucketPx, key.rotationDeg, c, img);
                        if (haveRaster && activePageCache) activePageCache->PutDirect(key, img);
                    }
                    if (haveRaster && img.Valid()) {
                        ComPtr<ID2D1Bitmap> gpuBmp;
                        if (!activeSyncOffscreen) {
                            for (const auto& entry : pageGpuBitmaps) {
                                if (entry.key == key) { gpuBmp = entry.bmp; break; }
                            }
                        }
                        if (!gpuBmp) {
                            gpuBmp.Attach(UploadBgra32(rt, img));
                            if (gpuBmp && !activeSyncOffscreen) {
                                if (pageGpuBitmaps.size() > 64) pageGpuBitmaps.erase(pageGpuBitmaps.begin());
                                pageGpuBitmaps.push_back({ key, gpuBmp });
                            }
                        }
                        if (gpuBmp) {
                            rt->DrawBitmap(gpuBmp.Get(), sr, static_cast<FLOAT>(layerOpacity));
                            drewCardRaster = true;
                        }
                    } else if (activePageCache && !activeSyncOffscreen) {
                        activePageCache->RequestAsync(key, activeAsyncGen, activeOnAsyncReady);
                    }
                }
            }
            // Neutral 1px paper border (high-contrast aware)
            brush->SetColor(ToD2DColor(RenderCommon::ApplyLayerOpacity(
                isHcCard ? DocColor::RGBA(25, 32, 44, 235) : DocColor::RGBA(198, 208, 222, 185), layerOpacity)));
            rt->DrawRectangle(sr, brush, isHcCard ? 1.35f : 1.0f);

            // Vector padlock indicator in top-right corner when pinned and selected
            if (isSelected && obj.lockedInPlace) {
                const float bx = sr.right - 22.0f;
                const float by = sr.top + 6.0f;
                brush->SetColor(ToD2DColor(DocColor::RGBA(15, 23, 42, 185)));
                rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bx, by, bx + 16.0f, by + 16.0f), 4.0f, 4.0f), brush);
                brush->SetColor(ToD2DColor(DocColor::RGBA(255, 255, 255, 245)));
                rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bx + 4.0f, by + 7.5f, bx + 12.0f, by + 13.5f), 1.5f, 1.5f), brush);
                rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(bx + 8.0f, by + 6.5f), 2.6f, 2.8f), brush, 1.3f);
            }
            if (!drewCardRaster && dwFactory) {
                ComPtr<IDWriteTextFormat> fmt;
                if (SUCCEEDED(dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, (std::max)(8.0f, static_cast<float>(12.0 * cam.zoom)), L"es-ES", &fmt)) && fmt) {
                    brush->SetColor(ToD2DColor(DocColor::RGBA(30, 41, 59, 255)));
                    const std::wstring title = Utf8ToWide(obj.captionUtf8.empty()
                        ? ("P\xC3\xA1g. " + std::to_string(obj.pdfSourcePageIndex0 + 1))
                        : obj.captionUtf8);
                    rt->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), fmt.Get(),
                        D2D1::RectF(sr.left + 8.0f, sr.top + 8.0f, sr.right - 8.0f, sr.bottom - 8.0f), brush);
                }
            }
        } else if (obj.kind == ObjectKind::StudyTape ||
                   obj.kind == ObjectKind::MathBlock ||
                   obj.kind == ObjectKind::CircuitBlock ||
                   obj.kind == ObjectKind::CalcSessionCard ||
                   obj.kind == ObjectKind::PlotBlock) {
            const bool isDarkCard = (overlay && overlay->darkTheme);
            const bool isHcCard = (overlay && overlay->highContrast);
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            const auto card = RenderCommon::BuildStemCardLayout(obj, isDarkCard, isHcCard);
            const float cardRad = (std::max)(4.0f, static_cast<float>(10.0 * cam.zoom));
            const D2D1_ROUNDED_RECT cardRR = D2D1::RoundedRect(sr, cardRad, cardRad);
            // Subtle elevation shadow for content card (standard material, never Liquid Glass)
            brush->SetColor(ToD2DColor(RenderCommon::ApplyLayerOpacity(
                isDarkCard ? DocColor::RGBA(0, 0, 0, 72) : DocColor::RGBA(15, 23, 42, 22), layerOpacity)));
            rt->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(sr.left + 1.0f, sr.top + 2.5f, sr.right + 1.0f, sr.bottom + 2.5f), cardRad, cardRad), brush);
            brush->SetColor(ToD2DColor(RenderCommon::ApplyLayerOpacity(card.bg, layerOpacity)));
            rt->FillRoundedRectangle(cardRR, brush);
            brush->SetColor(ToD2DColor(RenderCommon::ApplyLayerOpacity(card.border, layerOpacity)));
            rt->DrawRoundedRectangle(cardRR, brush, (std::max)(1.0f, static_cast<float>((isHcCard ? 1.55 : 1.25) * cam.zoom)));

            const float pad = static_cast<float>(6.0 * cam.zoom);
            if (dwFactory) {
                rt->PushAxisAlignedClip(sr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                D2DMathBackend mbk(rt, brush, dwFactory.Get());
                const DocRect cardBox{
                    static_cast<double>(sr.left), static_cast<double>(sr.top),
                    static_cast<double>(sr.right - sr.left), static_cast<double>(sr.bottom - sr.top)
                };
                MathTypeset::DrawStemCardContent(
                    mbk, obj, cardBox, cam.zoom,
                    RenderCommon::ApplyLayerOpacity(card.titleColor, layerOpacity),
                    RenderCommon::ApplyLayerOpacity(card.bodyColor, layerOpacity),
                    card.headerUtf8, card.bodyUtf8, isDarkCard);
                rt->PopAxisAlignedClip();
            }

            if (obj.kind == ObjectKind::CalcSessionCard && !obj.pngBytes.empty()) {
                const float imgW = (std::min)(static_cast<float>((sr.right - sr.left) * 0.42f), static_cast<float>(130.0 * cam.zoom));
                const float imgH = (std::min)(static_cast<float>((sr.bottom - sr.top) * 0.60f), imgW * 0.55f);
                if (imgW > 18.0f && imgH > 12.0f) {
                    const D2D1_RECT_F lcdR = D2D1::RectF(sr.right - imgW - pad, sr.top + pad * 3.5f, sr.right - pad, sr.top + pad * 3.5f + imgH);
                    if (ComPtr<ID2D1Bitmap> bmp = AcquireGpuBitmapForEncodedAsset(rt, "calc_lcd_" + std::to_string(obj.id), obj.pngBytes, cacheGpuAssets)) {
                        rt->DrawBitmap(bmp.Get(), lcdR);
                    }
                }
            } else if (obj.kind == ObjectKind::PlotBlock) {
                const auto plot = RenderCommon::BuildPlotRenderData(obj);
                const float px0 = sr.left + pad * 1.2f;
                const float py0 = sr.top + pad * 4.2f;
                const float px1 = (std::max)(px0 + 16.0f, sr.right - pad * 1.2f);
                const float py1 = (std::max)(py0 + 16.0f, sr.bottom - pad * 3.0f);
                brush->SetColor(ToD2DColor(isDarkCard ? DocColor::RGBA(19, 23, 33, 245) : DocColor::RGBA(255, 255, 255, 240)));
                rt->FillRectangle(D2D1::RectF(px0, py0, px1, py1), brush);
                brush->SetColor(ToD2DColor(isDarkCard ? DocColor::RGBA(71, 85, 105, 235)
                                         : (isHcCard ? DocColor::RGBA(30, 41, 59, 255) : DocColor::RGBA(148, 163, 184, 255))));
                rt->DrawRectangle(D2D1::RectF(px0, py0, px1, py1), brush, 1.0f);

                auto mapPt = [&](double wx, double wy) -> D2D1_POINT_2F {
                    const double nx = (wx - plot.xMin) / (std::max)(1e-9, plot.xMax - plot.xMin);
                    const double ny = (wy - plot.yMin) / (std::max)(1e-9, plot.yMax - plot.yMin);
                    return D2D1::Point2F(
                        static_cast<FLOAT>(px0 + std::clamp(nx, 0.0, 1.0) * (px1 - px0)),
                        static_cast<FLOAT>(py1 - std::clamp(ny, 0.0, 1.0) * (py1 - py0)));
                };
                if (plot.is3D && !plot.surface3DSegments.empty()) {
                    const float segW = (std::max)(1.0f, static_cast<float>(1.35 * cam.zoom));
                    for (const auto& seg : plot.surface3DSegments) {
                        brush->SetColor(ToD2DColor(seg.color));
                        rt->DrawLine(mapPt(seg.a.x, seg.a.y), mapPt(seg.b.x, seg.b.y), brush, segW);
                    }
                } else {
                    if (plot.xMin <= 0.0 && plot.xMax >= 0.0) {
                        const auto a = mapPt(0.0, plot.yMin), b = mapPt(0.0, plot.yMax);
                        rt->DrawLine(a, b, brush, 0.9f);
                    }
                    if (plot.yMin <= 0.0 && plot.yMax >= 0.0) {
                        const auto a = mapPt(plot.xMin, 0.0), b = mapPt(plot.xMax, 0.0);
                        rt->DrawLine(a, b, brush, 0.9f);
                    }
                    brush->SetColor(ToD2DColor(isDarkCard ? DocColor::RGBA(129, 140, 248, 255)
                                             : (isHcCard ? DocColor::RGBA(0, 64, 221, 255) : DocColor::RGBA(79, 70, 229, 255))));
                    for (std::size_t i = 1; i < plot.curvePoints.size(); ++i) {
                        rt->DrawLine(mapPt(plot.curvePoints[i - 1].x, plot.curvePoints[i - 1].y),
                                     mapPt(plot.curvePoints[i].x, plot.curvePoints[i].y), brush, (std::max)(1.3f, static_cast<float>(1.8 * cam.zoom)));
                    }
                    brush->SetColor(ToD2DColor(isDarkCard ? DocColor::RGBA(248, 113, 113, 255) : DocColor::RGBA(220, 38, 38, 255)));
                    for (const auto& sp : plot.scatterPoints) {
                        rt->FillEllipse(D2D1::Ellipse(mapPt(sp.x, sp.y), 3.0f, 3.0f), brush);
                    }
                }
            }
        } else if (obj.kind == ObjectKind::TableBlock) {
            const bool isDarkCard = (overlay && overlay->darkTheme);
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            const float rad = (std::max)(4.0f, static_cast<float>(8.0 * cam.zoom));
            brush->SetColor(ToD2DColor(RenderCommon::ApplyLayerOpacity(
                isDarkCard ? DocColor::RGBA(28, 34, 46, 246) : DocColor::RGBA(255, 255, 255, 250), layerOpacity)));
            rt->FillRoundedRectangle(D2D1::RoundedRect(sr, rad, rad), brush);
            brush->SetColor(ToD2DColor(RenderCommon::ApplyLayerOpacity(
                isDarkCard ? DocColor::RGBA(72, 86, 108, 235) : DocColor::RGBA(198, 208, 224, 255), layerOpacity)));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(sr, rad, rad), brush, 1.1f);

            std::vector<std::vector<std::string>> rows;
            if (!obj.tableJsonUtf8.empty()) {
                try {
                    const auto tj = nlohmann::json::parse(obj.tableJsonUtf8);
                    if (tj.contains("rows") && tj["rows"].is_array()) {
                        for (const auto& rj : tj["rows"]) {
                            if (!rj.is_array()) continue;
                            std::vector<std::string> rv;
                            for (const auto& cj : rj) rv.push_back(cj.is_string() ? cj.get<std::string>() : cj.dump());
                            rows.push_back(std::move(rv));
                        }
                    }
                } catch (...) {}
            }
            if (rows.empty() && !obj.textUtf8.empty()) {
                std::istringstream iss(obj.textUtf8);
                std::string ln;
                while (std::getline(iss, ln)) {
                    std::vector<std::string> rv;
                    std::istringstream ls(ln);
                    std::string cell;
                    while (std::getline(ls, cell, '|')) rv.push_back(cell);
                    if (!rv.empty()) rows.push_back(std::move(rv));
                }
            }
            if (!rows.empty()) {
                std::size_t maxCols = 1;
                for (const auto& r : rows) maxCols = (std::max)(maxCols, r.size());
                const float cellW = (sr.right - sr.left) / static_cast<float>(maxCols);
                const float cellH = (sr.bottom - sr.top) / static_cast<float>(rows.size());
                // Header band tint
                brush->SetColor(ToD2DColor(isDarkCard ? DocColor::RGBA(40, 50, 68, 230) : DocColor::RGBA(241, 245, 250, 245)));
                rt->FillRectangle(D2D1::RectF(sr.left + 1.0f, sr.top + 1.0f, sr.right - 1.0f, sr.top + cellH), brush);
                brush->SetColor(ToD2DColor(isDarkCard ? DocColor::RGBA(64, 76, 96, 200) : DocColor::RGBA(214, 222, 234, 255)));
                for (std::size_t r = 1; r < rows.size(); ++r) {
                    const float y = sr.top + static_cast<float>(r) * cellH;
                    rt->DrawLine(D2D1::Point2F(sr.left, y), D2D1::Point2F(sr.right, y), brush, 0.8f);
                }
                for (std::size_t c = 1; c < maxCols; ++c) {
                    const float x = sr.left + static_cast<float>(c) * cellW;
                    rt->DrawLine(D2D1::Point2F(x, sr.top), D2D1::Point2F(x, sr.bottom), brush, 0.8f);
                }
                if (dwFactory) {
                    const float fpx = std::clamp(static_cast<float>(10.5 * cam.zoom), 7.5f, 18.0f);
                    ComPtr<IDWriteTextFormat> fmtN, fmtB;
                    dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fpx, L"es-ES", &fmtN);
                    dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fpx, L"es-ES", &fmtB);
                    brush->SetColor(ToD2DColor(isDarkCard ? DocColor::RGBA(238, 242, 250, 255) : DocColor::RGBA(24, 30, 42, 255)));
                    rt->PushAxisAlignedClip(sr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    for (std::size_t r = 0; r < rows.size(); ++r) {
                        for (std::size_t c = 0; c < rows[r].size(); ++c) {
                            const D2D1_RECT_F cr = D2D1::RectF(
                                sr.left + static_cast<float>(c) * cellW + 4.0f,
                                sr.top + static_cast<float>(r) * cellH + 2.0f,
                                sr.left + static_cast<float>(c + 1) * cellW - 4.0f,
                                sr.top + static_cast<float>(r + 1) * cellH - 2.0f);
                            const std::wstring wcell = Utf8ToWide(rows[r][c]);
                            IDWriteTextFormat* f = (r == 0 && fmtB) ? fmtB.Get() : fmtN.Get();
                            if (f) rt->DrawTextW(wcell.c_str(), static_cast<UINT32>(wcell.size()), f, cr, brush);
                        }
                    }
                    rt->PopAxisAlignedClip();
                }
            }
        } else if (obj.kind == ObjectKind::CodeBlock) {
            const bool isDarkCard = (overlay && overlay->darkTheme);
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            const float rad = (std::max)(4.0f, static_cast<float>(8.0 * cam.zoom));
            brush->SetColor(ToD2DColor(isDarkCard ? DocColor::RGBA(22, 26, 36, 248) : DocColor::RGBA(246, 248, 252, 250)));
            rt->FillRoundedRectangle(D2D1::RoundedRect(sr, rad, rad), brush);
            brush->SetColor(ToD2DColor(isDarkCard ? DocColor::RGBA(72, 84, 108, 235) : DocColor::RGBA(190, 202, 220, 255)));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(sr, rad, rad), brush, 1.1f);
            if (dwFactory) {
                const float fpx = std::clamp(static_cast<float>(10.0 * cam.zoom), 7.5f, 16.0f);
                ComPtr<IDWriteTextFormat> fmt;
                if (SUCCEEDED(dwFactory->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fpx, L"es-ES", &fmt)) && fmt) {
                    brush->SetColor(ToD2DColor(isDarkCard ? DocColor::RGBA(220, 234, 252, 255) : DocColor::RGBA(28, 36, 52, 255)));
                    std::string body = (obj.captionUtf8.empty() ? ("[" + obj.codeLanguageUtf8 + "]") : obj.captionUtf8) + "\n" + obj.textUtf8;
                    if (!obj.evaluatedResultUtf8.empty()) body += "\n// => " + obj.evaluatedResultUtf8;
                    const std::wstring wtxt = Utf8ToWide(body);
                    rt->PushAxisAlignedClip(sr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    rt->DrawTextW(wtxt.c_str(), static_cast<UINT32>(wtxt.size()), fmt.Get(),
                        D2D1::RectF(sr.left + 8.0f, sr.top + 6.0f, sr.right - 8.0f, sr.bottom - 6.0f), brush);
                    rt->PopAxisAlignedClip();
                }
            }
        } else if (obj.kind == ObjectKind::AudioClip) {
            const bool isDarkCard = (overlay && overlay->darkTheme);
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            const float rad = (std::min)(14.0f, (sr.bottom - sr.top) * 0.45f);
            brush->SetColor(ToD2DColor(isDarkCard ? DocColor::RGBA(32, 46, 68, 240) : DocColor::RGBA(239, 246, 255, 248)));
            rt->FillRoundedRectangle(D2D1::RoundedRect(sr, rad, rad), brush);
            brush->SetColor(ToD2DColor(DocColor::RGBA(37, 99, 235, 220)));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(sr, rad, rad), brush, 1.2f);
            // Vector play triangle + waveform bars
            const float cy = (sr.top + sr.bottom) * 0.5f;
            const float px = sr.left + 14.0f;
            brush->SetColor(ToD2DColor(DocColor::RGBA(37, 99, 235, 255)));
            rt->DrawLine(D2D1::Point2F(px - 3.0f, cy - 5.0f), D2D1::Point2F(px + 5.0f, cy), brush, 2.0f, roundStroke.Get());
            rt->DrawLine(D2D1::Point2F(px + 5.0f, cy), D2D1::Point2F(px - 3.0f, cy + 5.0f), brush, 2.0f, roundStroke.Get());
            rt->DrawLine(D2D1::Point2F(px - 3.0f, cy - 5.0f), D2D1::Point2F(px - 3.0f, cy + 5.0f), brush, 2.0f, roundStroke.Get());
            if (dwFactory) {
                ComPtr<IDWriteTextFormat> fmt;
                if (SUCCEEDED(dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, std::clamp(static_cast<float>(10.5 * cam.zoom), 8.0f, 15.0f), L"es-ES", &fmt)) && fmt) {
                    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    brush->SetColor(ToD2DColor(isDarkCard ? DocColor::RGBA(219, 234, 254, 255) : DocColor::RGBA(30, 58, 138, 255)));
                    const std::wstring wcap = Utf8ToWide(obj.captionUtf8.empty() ? "Nota de voz" : obj.captionUtf8);
                    rt->DrawTextW(wcap.c_str(), static_cast<UINT32>(wcap.size()), fmt.Get(),
                        D2D1::RectF(sr.left + 26.0f, sr.top, sr.right - 8.0f, sr.bottom), brush);
                }
            }
        }

        if (applyRotation) {
            rt->SetTransform(savedTransform);
        }

        if (isSelected && !obj.lockedInPlace) {
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            brush->SetColor(ToD2DColor(DocColor::RGBA(0, 122, 255, 255)));
            rt->DrawRectangle(D2D1::RectF(sr.left - 3.0f, sr.top - 3.0f, sr.right + 3.0f, sr.bottom + 3.0f),
                brush, 1.5f, dashStroke.Get());
            // 8 resize handles + top rotation stalk handle (F-103, F-107, F-131)
            if (!overlay || overlay->showSelectionHandles) {
                const float mx = (sr.left + sr.right) * 0.5f;
                const float my = (sr.top + sr.bottom) * 0.5f;
                const D2D1_POINT_2F handles[8] = {
                    { sr.left - 3.0f, sr.top - 3.0f }, { mx, sr.top - 3.0f }, { sr.right + 3.0f, sr.top - 3.0f },
                    { sr.right + 3.0f, my }, { sr.right + 3.0f, sr.bottom + 3.0f }, { mx, sr.bottom + 3.0f },
                    { sr.left - 3.0f, sr.bottom + 3.0f }, { sr.left - 3.0f, my }
                };
                // Rotation stalk
                const D2D1_POINT_2F rotTop = D2D1::Point2F(mx, sr.top - 22.0f);
                rt->DrawLine(D2D1::Point2F(mx, sr.top - 3.0f), rotTop, brush, 1.4f);
                brush->SetColor(ToD2DColor(DocColor::RGBA(255, 255, 255, 255)));
                rt->FillEllipse(D2D1::Ellipse(rotTop, 4.5f, 4.5f), brush);
                brush->SetColor(ToD2DColor(DocColor::RGBA(0, 122, 255, 255)));
                rt->DrawEllipse(D2D1::Ellipse(rotTop, 4.5f, 4.5f), brush, 1.5f);
                for (const auto& h : handles) {
                    const D2D1_RECT_F hr = D2D1::RectF(h.x - 4.0f, h.y - 4.0f, h.x + 4.0f, h.y + 4.0f);
                    brush->SetColor(ToD2DColor(DocColor::RGBA(255, 255, 255, 255)));
                    rt->FillRoundedRectangle(D2D1::RoundedRect(hr, 2.0f, 2.0f), brush);
                    brush->SetColor(ToD2DColor(DocColor::RGBA(0, 122, 255, 255)));
                    rt->DrawRoundedRectangle(D2D1::RoundedRect(hr, 2.0f, 2.0f), brush, 1.4f);
                }
            }
        }
    }

    void RenderSceneToTarget(
        ID2D1RenderTarget* rt,
        const DocumentModel& doc,
        const Camera2D& cam,
        int widthPx,
        int heightPx,
        PageRenderCache* pageCache,
        std::uint64_t asyncGen,
        std::function<void()> onAsyncPageReady,
        const ViewportOverlayState* overlay,
        bool synchronousOffscreen,
        bool forceCanvasMode = false)
    {
        if (!rt) return;
        EnsureStrokeStyles();

        activePageCache = pageCache;
        activeAsyncGen = asyncGen;
        activeOnAsyncReady = onAsyncPageReady;
        activeSyncOffscreen = synchronousOffscreen;

        ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &brush)) || !brush) return;

        const bool isDark = (overlay && overlay->darkTheme);
        const bool isHc = (overlay && overlay->highContrast);

        if (forceCanvasMode || doc.Mode() == DocumentMode::InfiniteCanvas) {
            if (isDark) {
                rt->Clear(ToD2DColor(DocColor::RGBA(25, 28, 36, 255)));
            } else if (isHc) {
                rt->Clear(ToD2DColor(DocColor::RGBA(255, 255, 255, 255)));
            } else {
                rt->Clear(ToD2DColor(DocColor::RGBA(250, 252, 255, 255)));
            }
            const DocRect viewWorld = cam.ScreenRectToWorld({ 0.0, 0.0, static_cast<double>(widthPx), static_cast<double>(heightPx) });
            DrawPaperPattern(rt, brush.Get(), viewWorld, doc.CanvasBackground(), cam, isDark);

            const std::vector<std::uint64_t> visibleIds = doc.QueryCanvasRect(viewWorld);
            std::vector<const AnnotationObject*> drawList;
            drawList.reserve(visibleIds.size());
            for (std::uint64_t id : visibleIds) {
                if (const auto* o = doc.FindCanvasObject(id)) drawList.push_back(o);
            }
            std::sort(drawList.begin(), drawList.end(), [](const AnnotationObject* a, const AnnotationObject* b) {
                if (a->lockedInPlace != b->lockedInPlace) return a->lockedInPlace;
                return a->zOrder < b->zOrder;
            });

            for (const auto* o : drawList) {
                const bool sel = RenderCommon::IsObjectSelectedInOverlay(overlay, o->id);
                DrawAnnotationObject(rt, brush.Get(), doc, *o, cam, 0.0, 0.0, sel, !synchronousOffscreen, overlay);
            }
            if (overlay && overlay->draftObject) {
                DrawAnnotationObject(rt, brush.Get(), doc, *overlay->draftObject, cam, 0.0, 0.0, false, !synchronousOffscreen, overlay);
            }
            // Search match highlights on Infinite Canvas (including inside pinned PdfCard printouts)
            if (overlay && overlay->searchMatches) {
                for (std::size_t mi = 0; mi < overlay->searchMatches->size(); ++mi) {
                    const auto& m = (*overlay->searchMatches)[mi];
                    if (!m.onCanvas || !m.hasBox || m.worldBox.Empty()) continue;
                    const bool isActive = (mi == overlay->activeSearchMatchIndex);
                    const D2D1_RECT_F sr = ToD2DRect(cam.WorldRectToScreen(m.worldBox));
                    const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(sr, 3.0f, 3.0f);
                    brush->SetColor(ToD2DColor(isActive ? DocColor::RGBA(251, 146, 60, 125) : DocColor::RGBA(250, 204, 21, 95)));
                    rt->FillRoundedRectangle(rr, brush.Get());
                    if (isActive) {
                        brush->SetColor(ToD2DColor(DocColor::RGBA(234, 88, 12, 235)));
                        rt->DrawRoundedRectangle(rr, brush.Get(), 1.6f);
                    }
                }
            }
            if (overlay && overlay->laserPointer.active) {
                const auto& lp = overlay->laserPointer;
                if (lp.trail.size() >= 2) {
                    for (std::size_t i = 1; i < lp.trail.size(); ++i) {
                        const double frac = static_cast<double>(i) / static_cast<double>(lp.trail.size());
                        brush->SetColor(ToD2DColor(DocColor::RGBA(239, 68, 68, static_cast<std::uint8_t>(40 + 180 * frac))));
                        const DocPoint a = cam.WorldToScreen(lp.trail[i - 1].x, lp.trail[i - 1].y);
                        const DocPoint b = cam.WorldToScreen(lp.trail[i].x, lp.trail[i].y);
                        rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(a.x), static_cast<FLOAT>(a.y)),
                                     D2D1::Point2F(static_cast<FLOAT>(b.x), static_cast<FLOAT>(b.y)),
                                     brush.Get(), static_cast<FLOAT>(2.0 + 3.0 * frac), roundStroke.Get());
                    }
                }
                const DocPoint tip = cam.WorldToScreen(lp.worldX, lp.worldY);
                brush->SetColor(ToD2DColor(DocColor::RGBA(239, 68, 68, 95)));
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<FLOAT>(tip.x), static_cast<FLOAT>(tip.y)), 11.0f, 11.0f), brush.Get());
                brush->SetColor(ToD2DColor(DocColor::RGBA(255, 255, 255, 245)));
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<FLOAT>(tip.x), static_cast<FLOAT>(tip.y)), 4.5f, 4.5f), brush.Get());
            }
            DrawHudOverlays(rt, brush.Get(), doc, cam, widthPx, heightPx, overlay);
            return;
        }

        // Paginated PDF / Notebook Mode (Apple HIG Light/Dark/HighContrast desk surface)
        if (isDark) {
            rt->Clear(ToD2DColor(DocColor::RGBA(20, 23, 30, 255)));
        } else if (isHc) {
            rt->Clear(ToD2DColor(DocColor::RGBA(242, 245, 250, 255)));
        } else {
            rt->Clear(ToD2DColor(DocColor::RGBA(232, 237, 245, 255)));
        }
        const DocRect viewWorld = cam.ScreenRectToWorld({ 0.0, 0.0, static_cast<double>(widthPx), static_cast<double>(heightPx) });
        std::size_t firstPg = 0, lastPg = 0;
        if (!doc.Layout().ComputeVisibleRange(viewWorld.y, viewWorld.Bottom(), firstPg, lastPg, 1)) {
            return;
        }

        const auto& pages = doc.Pages();
        for (std::size_t p = firstPg; p <= lastPg && p < pages.size(); ++p) {
            const auto* place = doc.Layout().PlacementAt(p);
            if (!place) continue;
            if (place->collapsed && !place->collapsedHeader) continue;

            const DocRect pageWorld = place->WorldRect();
            const D2D1_RECT_F pageScreen = ToD2DRect(cam.WorldRectToScreen(pageWorld));

            // Collapsed page range pill banner (F-055: clickable to expand)
            if (place->collapsedHeader) {
                const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(pageScreen, 10.0f, 10.0f);
                brush->SetColor(ToD2DColor(isDark ? DocColor::RGBA(36, 44, 60, 235) : DocColor::RGBA(241, 245, 252, 248)));
                rt->FillRoundedRectangle(rr, brush.Get());
                brush->SetColor(ToD2DColor(DocColor::RGBA(0, 122, 255, 200)));
                rt->DrawRoundedRectangle(rr, brush.Get(), 1.2f);
                if (dwFactory) {
                    ComPtr<IDWriteTextFormat> fmt;
                    if (SUCCEEDED(dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"es-ES", &fmt)) && fmt) {
                        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                        brush->SetColor(ToD2DColor(isDark ? DocColor::RGBA(191, 219, 254, 255) : DocColor::RGBA(29, 78, 216, 255)));
                        const std::wstring lbl = L"P\u00E1ginas " + std::to_wstring(p + 1) + L"\u2013" +
                            std::to_wstring(place->collapsedEndPage0 + 1) + L" contra\u00EDdas \u2014 Clic para expandir";
                        rt->DrawTextW(lbl.c_str(), static_cast<UINT32>(lbl.size()), fmt.Get(), pageScreen, brush.Get());
                    }
                }
                continue;
            }

            // Multi-layered soft Apple HIG page shadow + crisp white sheet (content layer: NEVER glass)
            for (int sh = 3; sh >= 1; --sh) {
                const float pad = static_cast<float>(sh) * 2.5f;
                const float dy = static_cast<float>(sh) * 2.2f;
                const std::uint8_t a = isDark
                    ? static_cast<std::uint8_t>(42 - sh * 10)
                    : static_cast<std::uint8_t>(20 - sh * 5);
                brush->SetColor(ToD2DColor(DocColor::RGBA(15, 23, 42, a)));
                rt->FillRoundedRectangle(
                    D2D1::RoundedRect(D2D1::RectF(pageScreen.left - pad * 0.4f, pageScreen.top + dy - 1.0f,
                        pageScreen.right + pad * 0.4f, pageScreen.bottom + dy + pad * 0.4f), 4.0f, 4.0f),
                    brush.Get());
            }
            brush->SetColor(ToD2DColor(DocColor::RGBA(255, 255, 255, 255)));
            rt->FillRectangle(pageScreen, brush.Get());
            brush->SetColor(ToD2DColor(isHc ? DocColor::RGBA(25, 32, 44, 220) : DocColor::RGBA(198, 208, 222, 160)));
            rt->DrawRectangle(pageScreen, brush.Get(), isHc ? 1.25f : 0.8f);

            const auto& pg = pages[p];
            if (pg.hasPdfSource && (!pg.sourcePdfPathUtf8.empty() || !pg.sourcePdfAssetKey.empty())) {
                const std::string resolvedPdfPath = ResolvePdfSourcePathForRender(doc, pg);
                if (!resolvedPdfPath.empty()) {
                const int targetW = static_cast<int>(std::lround((std::max)(80.0, static_cast<double>(pageScreen.right - pageScreen.left))));
                PageCacheKey key = PageRenderCache::MakeKey(pg, targetW);
                key.sourcePdfPath = resolvedPdfPath;

                RasterImageBgra32 img;
                bool haveRaster = pageCache && pageCache->TryGet(key, img);
                if (!haveRaster && synchronousOffscreen) {
                    NormalizedCropRect c = pg.crop;
                    c.Clamp();
                    haveRaster = WinRtPdfRenderer::RenderPageToBgra32(
                        Utf8ToWide(resolvedPdfPath), pg.sourcePageIndex0,
                        key.widthBucketPx, key.rotationDeg, c, img);
                    if (haveRaster && pageCache) pageCache->PutDirect(key, img);
                }

                if (haveRaster && img.Valid()) {
                    ComPtr<ID2D1Bitmap> gpuBmp;
                    if (!synchronousOffscreen) {
                        for (const auto& entry : pageGpuBitmaps) {
                            if (entry.key == key) { gpuBmp = entry.bmp; break; }
                        }
                    }
                    if (!gpuBmp) {
                        gpuBmp.Attach(UploadBgra32(rt, img));
                        if (gpuBmp && !synchronousOffscreen) {
                            if (pageGpuBitmaps.size() > 36) pageGpuBitmaps.erase(pageGpuBitmaps.begin());
                            pageGpuBitmaps.push_back({ key, gpuBmp });
                        }
                    }
                    if (gpuBmp) {
                        rt->DrawBitmap(gpuBmp.Get(), pageScreen);
                    }
                } else {
                    if (pageCache && !synchronousOffscreen) {
                        pageCache->RequestAsync(key, asyncGen, onAsyncPageReady);
                    }
                    if (dwFactory) {
                        ComPtr<IDWriteTextFormat> fmt;
                        if (SUCCEEDED(dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"es-ES", &fmt)) && fmt) {
                            brush->SetColor(ToD2DColor(DocColor::RGBA(120, 132, 148, 255)));
                            const std::wstring msg = L"Renderizando p\u00E1gina " + std::to_wstring(p + 1) + L"...";
                            rt->DrawTextW(msg.c_str(), static_cast<UINT32>(msg.size()), fmt.Get(),
                                D2D1::RectF(pageScreen.left + 16.0f, pageScreen.top + 16.0f, pageScreen.right - 16.0f, pageScreen.bottom - 16.0f), brush.Get());
                        }
                    }
                }
                }
            }

            // Paper pattern overlay for blank/notebook or lined PDFs
            rt->PushAxisAlignedClip(pageScreen, D2D1_ANTIALIAS_MODE_ALIASED);
            DrawPaperPattern(rt, brush.Get(), pageWorld, pg.background, cam);

            // Annotations
            for (const auto& obj : pg.objects) {
                const bool sel = RenderCommon::IsObjectSelectedInOverlay(overlay, obj.id);
                DrawAnnotationObject(rt, brush.Get(), doc, obj, cam, pageWorld.x, pageWorld.y, sel, !synchronousOffscreen, overlay);
            }
            if (overlay && overlay->draftObject && p == overlay->draftPageIndex) {
                DrawAnnotationObject(rt, brush.Get(), doc, *overlay->draftObject, cam, pageWorld.x, pageWorld.y, false, !synchronousOffscreen, overlay);
            }

            // Search match highlights on this page
            if (overlay && overlay->searchMatches) {
                for (std::size_t mi = 0; mi < overlay->searchMatches->size(); ++mi) {
                    const auto& m = (*overlay->searchMatches)[mi];
                    if (m.onCanvas || m.pageIndex != p) continue;
                    const bool isActive = (mi == overlay->activeSearchMatchIndex);
                    brush->SetColor(ToD2DColor(isActive ? DocColor::RGBA(251, 146, 60, 120) : DocColor::RGBA(250, 204, 21, 90)));
                    if (m.hasBox) {
                        const DocRect mr{
                            pageWorld.x + m.boxNormalized.x * pageWorld.w,
                            pageWorld.y + m.boxNormalized.y * pageWorld.h,
                            m.boxNormalized.w * pageWorld.w,
                            m.boxNormalized.h * pageWorld.h
                        };
                        rt->FillRectangle(ToD2DRect(cam.WorldRectToScreen(mr)), brush.Get());
                    } else {
                        // Highlight page header badge cleanly when offset-only match found
                        const D2D1_RECT_F badge = D2D1::RectF(pageScreen.left + 6.0f, pageScreen.top + 6.0f, pageScreen.left + 96.0f, pageScreen.top + 26.0f);
                        rt->FillRectangle(badge, brush.Get());
                    }
                }
            }

            // Clickable InkLink badges on this page (F-132)
            for (const auto& lk : doc.InkLinks()) {
                if (lk.sourcePageIndex0 != static_cast<int>(p)) continue;
                const DocRect lr{
                    pageWorld.x + lk.sourceRectNorm.left * pageWorld.w,
                    pageWorld.y + lk.sourceRectNorm.top * pageWorld.h,
                    (std::max)(80.0, (lk.sourceRectNorm.right - lk.sourceRectNorm.left) * pageWorld.w),
                    (std::max)(20.0, (lk.sourceRectNorm.bottom - lk.sourceRectNorm.top) * pageWorld.h)
                };
                const D2D1_RECT_F sLr = ToD2DRect(cam.WorldRectToScreen(lr));
                const D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(sLr, 6.0f, 6.0f);
                brush->SetColor(ToD2DColor(DocColor::RGBA(0, 122, 255, 36)));
                rt->FillRoundedRectangle(rr, brush.Get());
                brush->SetColor(ToD2DColor(DocColor::RGBA(0, 122, 255, 220)));
                rt->DrawRoundedRectangle(rr, brush.Get(), 1.2f);
                if (dwFactory && !lk.labelUtf8.empty()) {
                    ComPtr<IDWriteTextFormat> fmt;
                    if (SUCCEEDED(dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, std::clamp(static_cast<float>(9.5 * cam.zoom), 7.5f, 13.0f), L"es-ES", &fmt)) && fmt) {
                        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                        const std::wstring wl = Utf8ToWide(lk.labelUtf8);
                        rt->DrawTextW(wl.c_str(), static_cast<UINT32>(wl.size()), fmt.Get(),
                            D2D1::RectF(sLr.left + 6.0f, sLr.top, sLr.right - 4.0f, sLr.bottom), brush.Get());
                    }
                }
            }

            // PDF Hyperlink subtle indicator (F-082) & AcroForm field overlay (F-084)
            for (const auto& pl : pg.pdfLinks) {
                const DocRect lr{
                    pageWorld.x + pl.rectNorm.x * pageWorld.w,
                    pageWorld.y + pl.rectNorm.y * pageWorld.h,
                    pl.rectNorm.w * pageWorld.w,
                    pl.rectNorm.h * pageWorld.h
                };
                const D2D1_RECT_F sLr = ToD2DRect(cam.WorldRectToScreen(lr));
                brush->SetColor(ToD2DColor(DocColor::RGBA(37, 99, 235, 110)));
                rt->DrawLine(D2D1::Point2F(sLr.left, sLr.bottom), D2D1::Point2F(sLr.right, sLr.bottom), brush.Get(), 1.0f, dashStroke.Get());
            }
            for (const auto& ff : pg.formFields) {
                const DocRect fr{
                    pageWorld.x + ff.rectNorm.x * pageWorld.w,
                    pageWorld.y + ff.rectNorm.y * pageWorld.h,
                    ff.rectNorm.w * pageWorld.w,
                    ff.rectNorm.h * pageWorld.h
                };
                const D2D1_RECT_F sFr = ToD2DRect(cam.WorldRectToScreen(fr));
                brush->SetColor(ToD2DColor(DocColor::RGBA(219, 234, 254, 96)));
                rt->FillRectangle(sFr, brush.Get());
                brush->SetColor(ToD2DColor(DocColor::RGBA(59, 130, 246, 180)));
                rt->DrawRectangle(sFr, brush.Get(), 0.9f);
                if (dwFactory) {
                    const std::string disp = (ff.fieldTypeUtf8 == "Btn") ? (ff.checked ? "X" : "") : ff.valueUtf8;
                    if (!disp.empty()) {
                        ComPtr<IDWriteTextFormat> fmt;
                        if (SUCCEEDED(dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, std::clamp(static_cast<float>(10.0 * cam.zoom), 7.5f, 15.0f), L"es-ES", &fmt)) && fmt) {
                            brush->SetColor(ToD2DColor(DocColor::RGBA(15, 23, 42, 255)));
                            const std::wstring wd = Utf8ToWide(disp);
                            rt->DrawTextW(wd.c_str(), static_cast<UINT32>(wd.size()), fmt.Get(), sFr, brush.Get());
                        }
                    }
                }
            }

            // PDF drag-to-select live selection highlight (F-075, F-076)
            if (overlay && overlay->hasPdfTextSel && overlay->pdfTextSelPage == p) {
                const DocRect selW{
                    pageWorld.x + overlay->pdfTextSelRectLocal.x,
                    pageWorld.y + overlay->pdfTextSelRectLocal.y,
                    overlay->pdfTextSelRectLocal.w,
                    overlay->pdfTextSelRectLocal.h
                };
                brush->SetColor(ToD2DColor(DocColor::RGBA(0, 122, 255, 68)));
                rt->FillRectangle(ToD2DRect(cam.WorldRectToScreen(selW)), brush.Get());
                brush->SetColor(ToD2DColor(DocColor::RGBA(0, 122, 255, 210)));
                rt->DrawRectangle(ToD2DRect(cam.WorldRectToScreen(selW)), brush.Get(), 1.1f);
            }

            // Crop preview box
            if (overlay && overlay->hasCropPreview && overlay->cropPageIndex == p) {
                const DocRect cr{
                    pageWorld.x + overlay->cropPreviewPageLocal.x,
                    pageWorld.y + overlay->cropPreviewPageLocal.y,
                    overlay->cropPreviewPageLocal.w,
                    overlay->cropPreviewPageLocal.h
                };
                brush->SetColor(ToD2DColor(DocColor::RGBA(37, 99, 235, 255)));
                rt->DrawRectangle(ToD2DRect(cam.WorldRectToScreen(cr)), brush.Get(), 2.0f, dashStroke.Get());
            }

            rt->PopAxisAlignedClip();
        }
        DrawHudOverlays(rt, brush.Get(), doc, cam, widthPx, heightPx, overlay);
    }

    void DrawHudOverlays(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush,
        const DocumentModel& doc,
        const Camera2D& cam,
        int widthPx,
        int heightPx,
        const ViewportOverlayState* overlay)
    {
        if (!overlay) return;

        const bool isDark = overlay->darkTheme;
        const bool isHc = overlay->highContrast;

        if (overlay->rulerState.visible && d2dFactory) {
            if (overlay->rulerState.mode == RulerMode::Protractor) {
                const DocPoint cScr = cam.WorldToScreen(overlay->rulerState.centerX, overlay->rulerState.centerY);
                const float rPx = static_cast<float>((std::max)(40.0, overlay->rulerState.lengthPt * 0.42) * cam.zoom);
                const double baseRad = overlay->rulerState.angleDeg * (3.14159265358979323846 / 180.0);
                // Semicircle translucent arc + radial degree ticks (F-039)
                brush->SetColor(ToD2DColor(isDark ? DocColor::RGBA(56, 189, 248, 220) : DocColor::RGBA(2, 132, 199, 235)));
                DocPoint prev{};
                for (int deg = 0; deg <= 180; deg += 3) {
                    const double a = baseRad - deg * (3.14159265358979323846 / 180.0);
                    const DocPoint cur{ cScr.x + rPx * std::cos(a), cScr.y + rPx * std::sin(a) };
                    if (deg > 0) {
                        rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(prev.x), static_cast<FLOAT>(prev.y)),
                                     D2D1::Point2F(static_cast<FLOAT>(cur.x), static_cast<FLOAT>(cur.y)), brush, 2.0f);
                    }
                    if (deg % 15 == 0) {
                        const float inner = (deg % 45 == 0) ? (rPx * 0.82f) : (rPx * 0.90f);
                        rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(cScr.x + inner * std::cos(a)), static_cast<FLOAT>(cScr.y + inner * std::sin(a))),
                                     D2D1::Point2F(static_cast<FLOAT>(cur.x), static_cast<FLOAT>(cur.y)), brush, (deg % 45 == 0) ? 1.5f : 0.9f);
                    }
                    prev = cur;
                }
                rt->DrawLine(
                    D2D1::Point2F(static_cast<FLOAT>(cScr.x - rPx * std::cos(baseRad)), static_cast<FLOAT>(cScr.y - rPx * std::sin(baseRad))),
                    D2D1::Point2F(static_cast<FLOAT>(cScr.x + rPx * std::cos(baseRad)), static_cast<FLOAT>(cScr.y + rPx * std::sin(baseRad))),
                    brush, 2.0f);
                rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<FLOAT>(cScr.x), static_cast<FLOAT>(cScr.y)), 4.0f, 4.0f), brush);
            } else if (overlay->rulerState.mode == RulerMode::Compass) {
                const DocPoint cScr = cam.WorldToScreen(overlay->rulerState.centerX, overlay->rulerState.centerY);
                const float rPx = static_cast<float>((std::max)(18.0, overlay->rulerState.compassRadiusPt) * cam.zoom);
                brush->SetColor(ToD2DColor(isDark ? DocColor::RGBA(56, 189, 248, 220) : DocColor::RGBA(2, 132, 199, 235)));
                rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<FLOAT>(cScr.x), static_cast<FLOAT>(cScr.y)), rPx, rPx),
                    brush, 1.8f, dashStroke.Get());
                rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(cScr.x) - 8.0f, static_cast<FLOAT>(cScr.y)),
                             D2D1::Point2F(static_cast<FLOAT>(cScr.x) + 8.0f, static_cast<FLOAT>(cScr.y)), brush, 1.5f);
                rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(cScr.x), static_cast<FLOAT>(cScr.y) - 8.0f),
                             D2D1::Point2F(static_cast<FLOAT>(cScr.x), static_cast<FLOAT>(cScr.y) + 8.0f), brush, 1.5f);
                rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(cScr.x), static_cast<FLOAT>(cScr.y)),
                             D2D1::Point2F(static_cast<FLOAT>(cScr.x) + rPx, static_cast<FLOAT>(cScr.y)), brush, 1.2f);
            } else {
            const auto rg = RenderCommon::BuildRulerOverlay(overlay->rulerState, cam);
            ComPtr<ID2D1PathGeometry> poly;
            if (SUCCEEDED(d2dFactory->CreatePathGeometry(&poly)) && poly) {
                ComPtr<ID2D1GeometrySink> sink;
                if (SUCCEEDED(poly->Open(&sink)) && sink) {
                    sink->BeginFigure(
                        D2D1::Point2F(static_cast<FLOAT>(rg.corners[0].x), static_cast<FLOAT>(rg.corners[0].y)),
                        D2D1_FIGURE_BEGIN_FILLED);
                    for (int k = 1; k < 4; ++k) {
                        sink->AddLine(D2D1::Point2F(static_cast<FLOAT>(rg.corners[k].x), static_cast<FLOAT>(rg.corners[k].y)));
                    }
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    sink->Close();
                }
                brush->SetColor(ToD2DColor(isDark ? DocColor::RGBA(30, 58, 86, 195)
                                                  : DocColor::RGBA(224, 242, 254, 185)));
                rt->FillGeometry(poly.Get(), brush);
                brush->SetColor(ToD2DColor(isDark ? DocColor::RGBA(56, 189, 248, 235)
                                                  : DocColor::RGBA(2, 132, 199, 240)));
                rt->DrawGeometry(poly.Get(), brush, 1.5f);
            }
            brush->SetColor(ToD2DColor(isDark ? DocColor::RGBA(125, 211, 252, 255)
                                              : DocColor::RGBA(3, 105, 161, 255)));
            rt->DrawLine(
                D2D1::Point2F(static_cast<FLOAT>(rg.activeEdge0.x), static_cast<FLOAT>(rg.activeEdge0.y)),
                D2D1::Point2F(static_cast<FLOAT>(rg.activeEdge1.x), static_cast<FLOAT>(rg.activeEdge1.y)),
                brush, 2.2f);
            for (const auto& tk : rg.ticks) {
                rt->DrawLine(
                    D2D1::Point2F(static_cast<FLOAT>(tk.p0.x), static_cast<FLOAT>(tk.p0.y)),
                    D2D1::Point2F(static_cast<FLOAT>(tk.p1.x), static_cast<FLOAT>(tk.p1.y)),
                    brush, tk.major ? 1.4f : 0.9f);
            }
            if (dwFactory && !rg.badgeUtf8.empty()) {
                ComPtr<IDWriteTextFormat> fmt;
                if (SUCCEEDED(dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"es-ES", &fmt)) && fmt) {
                    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    const std::wstring wb = Utf8ToWide(rg.badgeUtf8);
                    const float pillW = 74.0f;
                    const float pillH = 20.0f;
                    const D2D1_RECT_F pillR = D2D1::RectF(
                        static_cast<FLOAT>(rg.badgePos.x) - pillW * 0.5f,
                        static_cast<FLOAT>(rg.badgePos.y) - pillH * 0.5f,
                        static_cast<FLOAT>(rg.badgePos.x) + pillW * 0.5f,
                        static_cast<FLOAT>(rg.badgePos.y) + pillH * 0.5f);
                    const D2D1_ROUNDED_RECT pillRR = D2D1::RoundedRect(pillR, 10.0f, 10.0f);
                    brush->SetColor(ToD2DColor(isDark ? DocColor::RGBA(15, 23, 42, 235)
                                                      : DocColor::RGBA(255, 255, 255, 238)));
                    rt->FillRoundedRectangle(pillRR, brush);
                    brush->SetColor(ToD2DColor(isDark ? DocColor::RGBA(56, 189, 248, 220)
                                                      : DocColor::RGBA(2, 132, 199, 210)));
                    rt->DrawRoundedRectangle(pillRR, brush, 1.0f);
                    brush->SetColor(ToD2DColor(isDark ? DocColor::RGBA(224, 242, 254, 255)
                                                      : DocColor::RGBA(12, 74, 110, 255)));
                    rt->DrawTextW(wb.c_str(), static_cast<UINT32>(wb.size()), fmt.Get(), pillR, brush);
                }
            }
            }
        }

        if (overlay->minimapVisible && widthPx >= 640 && heightPx >= 420) {
            const auto mm = RenderCommon::BuildMinimapOverlay(doc, cam, widthPx, heightPx);
            const D2D1_RECT_F pr = ToD2DRect(mm.panelScreen);
            const D2D1_ROUNDED_RECT mmRR = D2D1::RoundedRect(pr, 10.0f, 10.0f);
            // Soft HIG elevation shadow
            brush->SetColor(ToD2DColor(isDark ? DocColor::RGBA(0, 0, 0, 88) : DocColor::RGBA(15, 23, 42, 28)));
            rt->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(pr.left, pr.top + 2.5f, pr.right, pr.bottom + 2.5f), 10.0f, 10.0f), brush);
            // Frosted Apple HIG Light / Dark / HighContrast minimap card
            brush->SetColor(ToD2DColor(isDark ? DocColor::RGBA(28, 32, 42, 228)
                                     : (isHc ? DocColor::RGBA(255, 255, 255, 248) : DocColor::RGBA(248, 250, 253, 228))));
            rt->FillRoundedRectangle(mmRR, brush);
            brush->SetColor(ToD2DColor(isHc ? DocColor::RGBA(24, 30, 42, 255)
                                     : (isDark ? DocColor::RGBA(94, 106, 128, 210) : DocColor::RGBA(186, 198, 216, 220))));
            rt->DrawRoundedRectangle(mmRR, brush, isHc ? 1.4f : 1.0f);
            for (const auto& it : mm.items) {
                brush->SetColor(ToD2DColor(it.color));
                rt->FillRoundedRectangle(D2D1::RoundedRect(ToD2DRect(it.rectScreen), 2.0f, 2.0f), brush);
            }
            brush->SetColor(ToD2DColor(isDark ? DocColor::RGBA(56, 189, 248, 240) : DocColor::RGBA(0, 122, 255, 235)));
            rt->DrawRoundedRectangle(D2D1::RoundedRect(ToD2DRect(mm.viewportScreen), 3.0f, 3.0f), brush, 1.5f);
        }

        // Composite live Liquid Glass 2.0 floating overlay (e.g. Appearance Popover) inside Direct2D
        if (overlay->floatingOverlayBgra && overlay->floatingOverlayBgra->Valid() &&
            overlay->floatingOverlayScreenRect.w > 0.0 && overlay->floatingOverlayScreenRect.h > 0.0) {
            ComPtr<ID2D1Bitmap> ovBmp;
            ovBmp.Attach(UploadBgra32(rt, *overlay->floatingOverlayBgra));
            if (ovBmp) {
                rt->DrawBitmap(ovBmp.Get(), ToD2DRect(overlay->floatingOverlayScreenRect));
            }
        }
    }
};

D2DRenderer::D2DRenderer() : impl_(new Impl()) {}

D2DRenderer::~D2DRenderer() {
    const bool uninit = impl_ && impl_->didCoInit;
    delete impl_;
    impl_ = nullptr;
    if (uninit) CoUninitialize();
}

bool D2DRenderer::InitializeFactories() {
    if (impl_->factoriesReady) return true;
    const HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hrCo)) impl_->didCoInit = true;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, impl_->d2dFactory.ReleaseAndGetAddressOf());
    if (FAILED(hr) || !impl_->d2dFactory) return false;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(impl_->dwFactory.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) return false;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&impl_->wicFactory));
    if (FAILED(hr) || !impl_->wicFactory) return false;
    impl_->factoriesReady = true;
    return true;
}

void D2DRenderer::AttachHwnd(HWND hwnd, int widthPx, int heightPx) {
    impl_->hwnd = hwnd;
    if (!InitializeFactories() || !hwnd) return;
    if (impl_->hwndRt) return;
    const D2D1_SIZE_U sz = D2D1::SizeU(
        static_cast<UINT32>((std::max)(1, widthPx)),
        static_cast<UINT32>((std::max)(1, heightPx)));
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    rtProps.dpiX = 96.0f;
    rtProps.dpiY = 96.0f;
    impl_->d2dFactory->CreateHwndRenderTarget(
        rtProps,
        D2D1::HwndRenderTargetProperties(hwnd, sz),
        &impl_->hwndRt);
}

void D2DRenderer::Resize(int widthPx, int heightPx) {
    if (impl_->hwndRt) {
        impl_->hwndRt->Resize(D2D1::SizeU(
            static_cast<UINT32>((std::max)(1, widthPx)),
            static_cast<UINT32>((std::max)(1, heightPx))));
    }
}

void D2DRenderer::DiscardDeviceResources() {
    impl_->pageGpuBitmaps.clear();
    impl_->assetGpuBitmaps.clear();
    impl_->hwndRt.Reset();
}

void D2DRenderer::RenderViewport(
    const DocumentModel& doc,
    const Camera2D& camera,
    int widthPx,
    int heightPx,
    PageRenderCache* pageCache,
    std::uint64_t asyncGen,
    std::function<void()> onAsyncPageReady,
    const ViewportOverlayState* overlay)
{
    if (!InitializeFactories()) return;
    if (!impl_->hwndRt && impl_->hwnd) AttachHwnd(impl_->hwnd, widthPx, heightPx);
    if (!impl_->hwndRt) return;

    impl_->hwndRt->BeginDraw();
    impl_->RenderSceneToTarget(impl_->hwndRt.Get(), doc, camera, widthPx, heightPx,
        pageCache, asyncGen, std::move(onAsyncPageReady), overlay, false);
    const HRESULT hr = impl_->hwndRt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
    }
}

bool D2DRenderer::RenderPageOffscreen(
    const DocumentModel& doc,
    std::size_t pageIndex0,
    int widthPx,
    PageRenderCache* pageCache,
    RasterImageBgra32& outBgra)
{
    if (!InitializeFactories() || !impl_->wicFactory || pageIndex0 >= doc.Pages().size()) return false;
    const auto* place = doc.Layout().PlacementAt(pageIndex0);
    if (!place || place->widthWorld <= 0.0) return false;

    const int w = std::clamp(widthPx, 64, 3000);
    const double zoom = static_cast<double>(w) / place->widthWorld;
    const int h = (std::max)(64, static_cast<int>(std::lround(place->heightWorld * zoom)));

    Camera2D cam;
    cam.panX = -place->widthWorld * 0.5;
    cam.panY = place->topWorld;
    cam.zoom = zoom;

    return RenderHwndViewportToOffscreen(doc, cam, w, h, pageCache, outBgra, nullptr);
}

bool D2DRenderer::RenderInfiniteCanvasOffscreen(
    const DocumentModel& doc,
    const DocRect& worldRegion,
    int widthPx,
    int heightPx,
    RasterImageBgra32& outBgra)
{
    if (worldRegion.w <= 0.0 || worldRegion.h <= 0.0) return false;
    if (!InitializeFactories() || !impl_->wicFactory) return false;
    const int w = (std::max)(1, widthPx);

    Camera2D cam;
    cam.panX = worldRegion.x;
    cam.panY = worldRegion.y;
    cam.zoom = static_cast<double>(w) / worldRegion.w;

    ComPtr<IWICBitmap> wicBmp;
    if (FAILED(impl_->wicFactory->CreateBitmap(
            static_cast<UINT>(w), static_cast<UINT>((std::max)(1, heightPx)),
            GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &wicBmp)) || !wicBmp) {
        return false;
    }

    const int h = (std::max)(1, heightPx);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    ComPtr<ID2D1RenderTarget> wicRt;
    if (FAILED(impl_->d2dFactory->CreateWicBitmapRenderTarget(wicBmp.Get(), props, &wicRt)) || !wicRt) {
        return false;
    }

    wicRt->BeginDraw();
    impl_->RenderSceneToTarget(wicRt.Get(), doc, cam, w, h, nullptr, 1, nullptr, nullptr, true, true);
    if (FAILED(wicRt->EndDraw())) return false;

    outBgra.width = w;
    outBgra.height = h;
    outBgra.pixels.resize(static_cast<std::size_t>(w) * h * 4u);
    WICRect lockRect{ 0, 0, w, h };
    return SUCCEEDED(wicBmp->CopyPixels(&lockRect, static_cast<UINT>(w * 4), static_cast<UINT>(outBgra.pixels.size()), outBgra.pixels.data()));
}

bool D2DRenderer::RenderHwndViewportToOffscreen(
    const DocumentModel& doc,
    const Camera2D& camera,
    int widthPx,
    int heightPx,
    PageRenderCache* pageCache,
    RasterImageBgra32& outBgra,
    const ViewportOverlayState* overlay)
{
    if (!InitializeFactories() || !impl_->wicFactory) return false;
    const int w = (std::max)(1, widthPx);
    const int h = (std::max)(1, heightPx);

    ComPtr<IWICBitmap> wicBmp;
    if (FAILED(impl_->wicFactory->CreateBitmap(
            static_cast<UINT>(w), static_cast<UINT>(h),
            GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &wicBmp)) || !wicBmp) {
        return false;
    }

    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    ComPtr<ID2D1RenderTarget> wicRt;
    if (FAILED(impl_->d2dFactory->CreateWicBitmapRenderTarget(wicBmp.Get(), props, &wicRt)) || !wicRt) {
        return false;
    }

    wicRt->BeginDraw();
    impl_->RenderSceneToTarget(wicRt.Get(), doc, camera, w, h, pageCache, 1, nullptr, overlay, true);
    if (FAILED(wicRt->EndDraw())) return false;

    outBgra.width = w;
    outBgra.height = h;
    outBgra.pixels.resize(static_cast<std::size_t>(w) * h * 4u);
    WICRect lockRect{ 0, 0, w, h };
    return SUCCEEDED(wicBmp->CopyPixels(&lockRect, static_cast<UINT>(w * 4), static_cast<UINT>(outBgra.pixels.size()), outBgra.pixels.data()));
}

} // namespace DocStudio
