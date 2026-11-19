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
#include <cstring>

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

        auto toScreen = [&](double lx, double ly) -> D2D1_POINT_2F {
            const DocPoint sp = cam.WorldToScreen(originWorldX + lx, originWorldY + ly);
            return D2D1::Point2F(static_cast<FLOAT>(sp.x), static_cast<FLOAT>(sp.y));
        };
        auto rectToScreen = [&](const DocRect& lr) -> D2D1_RECT_F {
            const DocRect wr{ originWorldX + lr.x, originWorldY + lr.y, lr.w, lr.h };
            return ToD2DRect(cam.WorldRectToScreen(wr));
        };

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
                if (obj.fillColor.a > 0) {
                    brush->SetColor(ToD2DColor(obj.fillColor));
                    rt->FillRectangle(sr, brush);
                }
                brush->SetColor(ToD2DColor(obj.strokeColor));
                rt->DrawRectangle(sr, brush, wPx);
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
                if (obj.shapeKind == ShapeKind::Arrow) {
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
                        rt->DrawTextW(wtxt.c_str(), static_cast<UINT32>(wtxt.size()), fmt.Get(), sr, brush);
                    }
                }
            }
        } else if (obj.kind == ObjectKind::Image) {
            const std::vector<std::uint8_t>* bytesPtr = !obj.pngBytes.empty() ? &obj.pngBytes : doc.FindAsset(obj.assetKey);
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            if (bytesPtr && !bytesPtr->empty()) {
                const std::string tag = !obj.assetKey.empty() ? obj.assetKey : ("inline_" + std::to_string(obj.id));
                ComPtr<ID2D1Bitmap> bmp = AcquireGpuBitmapForEncodedAsset(rt, tag, *bytesPtr, cacheGpuAssets);
                if (bmp) {
                    rt->DrawBitmap(bmp.Get(), sr);
                }
            } else {
                brush->SetColor(ToD2DColor(DocColor::RGBA(200, 210, 224, 255)));
                rt->FillRectangle(sr, brush);
            }
        } else if (obj.kind == ObjectKind::PdfCard) {
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            brush->SetColor(ToD2DColor(DocColor::RGBA(255, 255, 255, 255)));
            rt->FillRectangle(sr, brush);
            const std::vector<std::uint8_t>* bytesPtr = !obj.pngBytes.empty() ? &obj.pngBytes : doc.FindAsset(obj.assetKey);
            if (bytesPtr && !bytesPtr->empty()) {
                const std::string tag = !obj.assetKey.empty() ? obj.assetKey : ("pdfcard_" + std::to_string(obj.id));
                if (ComPtr<ID2D1Bitmap> bmp = AcquireGpuBitmapForEncodedAsset(rt, tag, *bytesPtr, cacheGpuAssets)) {
                    rt->DrawBitmap(bmp.Get(), sr);
                }
            }
            brush->SetColor(ToD2DColor(DocColor::RGBA(59, 130, 246, 255)));
            rt->DrawRectangle(sr, brush, 1.5f);
            if (!bytesPtr && dwFactory) {
                ComPtr<IDWriteTextFormat> fmt;
                if (SUCCEEDED(dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, (std::max)(8.0f, static_cast<float>(12.0 * cam.zoom)), L"es-ES", &fmt)) && fmt) {
                    brush->SetColor(ToD2DColor(DocColor::RGBA(30, 41, 59, 255)));
                    const std::wstring title = L"PDF p." + std::to_wstring(obj.pdfSourcePageIndex0 + 1) + L"\n" + Utf8ToWide(obj.captionUtf8);
                    rt->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), fmt.Get(),
                        D2D1::RectF(sr.left + 8.0f, sr.top + 8.0f, sr.right - 8.0f, sr.bottom - 8.0f), brush);
                }
            }
        } else if (obj.kind == ObjectKind::StudyTape ||
                   obj.kind == ObjectKind::MathBlock ||
                   obj.kind == ObjectKind::CircuitBlock ||
                   obj.kind == ObjectKind::CalcSessionCard ||
                   obj.kind == ObjectKind::PlotBlock) {
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            const auto card = RenderCommon::BuildStemCardLayout(obj);
            const float cardRad = (std::max)(4.0f, static_cast<float>(10.0 * cam.zoom));
            const D2D1_ROUNDED_RECT cardRR = D2D1::RoundedRect(sr, cardRad, cardRad);
            // Subtle elevation shadow for content card (standard material, never Liquid Glass)
            brush->SetColor(ToD2DColor(RenderCommon::ApplyLayerOpacity(DocColor::RGBA(15, 23, 42, 22), layerOpacity)));
            rt->FillRoundedRectangle(D2D1::RoundedRect(
                D2D1::RectF(sr.left + 1.0f, sr.top + 2.5f, sr.right + 1.0f, sr.bottom + 2.5f), cardRad, cardRad), brush);
            brush->SetColor(ToD2DColor(RenderCommon::ApplyLayerOpacity(card.bg, layerOpacity)));
            rt->FillRoundedRectangle(cardRR, brush);
            brush->SetColor(ToD2DColor(RenderCommon::ApplyLayerOpacity(card.border, layerOpacity)));
            rt->DrawRoundedRectangle(cardRR, brush, (std::max)(1.0f, static_cast<float>(1.25 * cam.zoom)));

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
                    card.headerUtf8, card.bodyUtf8);
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
                brush->SetColor(ToD2DColor(DocColor::RGBA(255, 255, 255, 235)));
                rt->FillRectangle(D2D1::RectF(px0, py0, px1, py1), brush);
                brush->SetColor(ToD2DColor(DocColor::RGBA(148, 163, 184, 255)));
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
                    brush->SetColor(ToD2DColor(DocColor::RGBA(79, 70, 229, 255)));
                    for (std::size_t i = 1; i < plot.curvePoints.size(); ++i) {
                        rt->DrawLine(mapPt(plot.curvePoints[i - 1].x, plot.curvePoints[i - 1].y),
                                     mapPt(plot.curvePoints[i].x, plot.curvePoints[i].y), brush, (std::max)(1.3f, static_cast<float>(1.8 * cam.zoom)));
                    }
                    brush->SetColor(ToD2DColor(DocColor::RGBA(220, 38, 38, 255)));
                    for (const auto& sp : plot.scatterPoints) {
                        rt->FillEllipse(D2D1::Ellipse(mapPt(sp.x, sp.y), 3.0f, 3.0f), brush);
                    }
                }
            }
        }

        if (isSelected) {
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            brush->SetColor(ToD2DColor(DocColor::RGBA(37, 99, 235, 255)));
            rt->DrawRectangle(D2D1::RectF(sr.left - 3.0f, sr.top - 3.0f, sr.right + 3.0f, sr.bottom + 3.0f),
                brush, 1.6f, dashStroke.Get());
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
                return a->zOrder < b->zOrder;
            });

            for (const auto* o : drawList) {
                const bool sel = RenderCommon::IsObjectSelectedInOverlay(overlay, o->id);
                DrawAnnotationObject(rt, brush.Get(), doc, *o, cam, 0.0, 0.0, sel, !synchronousOffscreen, overlay);
            }
            if (overlay && overlay->draftObject) {
                DrawAnnotationObject(rt, brush.Get(), doc, *overlay->draftObject, cam, 0.0, 0.0, false, !synchronousOffscreen, overlay);
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
            const DocRect pageWorld = place->WorldRect();
            const D2D1_RECT_F pageScreen = ToD2DRect(cam.WorldRectToScreen(pageWorld));

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
            if (pg.hasPdfSource && !pg.sourcePdfPathUtf8.empty()) {
                const int targetW = static_cast<int>(std::lround((std::max)(80.0, static_cast<double>(pageScreen.right - pageScreen.left))));
                const PageCacheKey key = PageRenderCache::MakeKey(pg, targetW);

                RasterImageBgra32 img;
                bool haveRaster = pageCache && pageCache->TryGet(key, img);
                if (!haveRaster && synchronousOffscreen) {
                    NormalizedCropRect c = pg.crop;
                    c.Clamp();
                    haveRaster = WinRtPdfRenderer::RenderPageToBgra32(
                        Utf8ToWide(pg.sourcePdfPathUtf8), pg.sourcePageIndex0,
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
                    if (m.pageIndex != p) continue;
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

        if (overlay->rulerState.visible && d2dFactory) {
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
                brush->SetColor(ToD2DColor(DocColor::RGBA(224, 242, 254, 175)));
                rt->FillGeometry(poly.Get(), brush);
                brush->SetColor(ToD2DColor(DocColor::RGBA(2, 132, 199, 240)));
                rt->DrawGeometry(poly.Get(), brush, 1.5f);
            }
            brush->SetColor(ToD2DColor(DocColor::RGBA(3, 105, 161, 255)));
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
                if (SUCCEEDED(dwFactory->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_BOLD,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"es-ES", &fmt)) && fmt) {
                    const std::wstring wb = Utf8ToWide(rg.badgeUtf8);
                    brush->SetColor(ToD2DColor(DocColor::RGBA(12, 74, 110, 255)));
                    rt->DrawTextW(wb.c_str(), static_cast<UINT32>(wb.size()), fmt.Get(),
                        D2D1::RectF(static_cast<FLOAT>(rg.badgePos.x), static_cast<FLOAT>(rg.badgePos.y),
                                    static_cast<FLOAT>(rg.badgePos.x + 110.0), static_cast<FLOAT>(rg.badgePos.y + 22.0)), brush);
                }
            }
        }

        if (overlay->minimapVisible) {
            const auto mm = RenderCommon::BuildMinimapOverlay(doc, cam, widthPx, heightPx);
            const D2D1_RECT_F pr = ToD2DRect(mm.panelScreen);
            brush->SetColor(ToD2DColor(DocColor::RGBA(15, 23, 42, 215)));
            rt->FillRectangle(pr, brush);
            brush->SetColor(ToD2DColor(DocColor::RGBA(100, 116, 139, 240)));
            rt->DrawRectangle(pr, brush, 1.2f);
            for (const auto& it : mm.items) {
                brush->SetColor(ToD2DColor(it.color));
                rt->FillRectangle(ToD2DRect(it.rectScreen), brush);
            }
            brush->SetColor(ToD2DColor(DocColor::RGBA(250, 204, 21, 250)));
            rt->DrawRectangle(ToD2DRect(mm.viewportScreen), brush, 1.6f);
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
