#include "d2d_renderer.hpp"
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
        if (queue_.size() > 64) queue_.pop_front();
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

    ID2D1Bitmap* AcquireGpuBitmapForEncodedAsset(
        ID2D1RenderTarget* rt,
        const std::string& cacheTag,
        const std::vector<std::uint8_t>& bytes,
        bool cacheResult)
    {
        if (!rt || !wicFactory || bytes.empty()) return nullptr;
        if (cacheResult) {
            for (const auto& entry : assetGpuBitmaps) {
                if (entry.key == cacheTag) return entry.bmp.Get();
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
        return bmp.Get();
    }

    void DrawPaperPattern(
        ID2D1RenderTarget* rt,
        ID2D1SolidColorBrush* brush,
        const DocRect& worldRegion,
        PaperPattern pattern,
        const Camera2D& cam)
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
                brush->SetColor(ToD2DColor(DocColor::RGBA(205, 221, 244, 255)));
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
                brush->SetColor(ToD2DColor(DocColor::RGBA(240, 150, 155, 255)));
                rt->DrawLine(D2D1::Point2F(static_cast<FLOAT>(m0.x), static_cast<FLOAT>(m0.y)),
                             D2D1::Point2F(static_cast<FLOAT>(m1.x), static_cast<FLOAT>(m1.y)),
                             brush, 1.2f);
            }
        } else if (pattern == PaperPattern::Grid) {
            drawGridLines(20.0, DocColor::RGBA(218, 228, 240, 255), 1.0f);
        } else if (pattern == PaperPattern::Dots) {
            const double step = 20.0;
            if (step * cam.zoom >= 6.0) {
                brush->SetColor(ToD2DColor(DocColor::RGBA(175, 186, 204, 255)));
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
            drawGridLines(8.0, DocColor::RGBA(228, 240, 232, 255), 0.7f);
            drawGridLines(40.0, DocColor::RGBA(178, 214, 190, 255), 1.1f);
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
        bool cacheGpuAssets)
    {
        auto toScreen = [&](double lx, double ly) -> D2D1_POINT_2F {
            const DocPoint sp = cam.WorldToScreen(originWorldX + lx, originWorldY + ly);
            return D2D1::Point2F(static_cast<FLOAT>(sp.x), static_cast<FLOAT>(sp.y));
        };
        auto rectToScreen = [&](const DocRect& lr) -> D2D1_RECT_F {
            const DocRect wr{ originWorldX + lr.x, originWorldY + lr.y, lr.w, lr.h };
            return ToD2DRect(cam.WorldRectToScreen(wr));
        };

        if (obj.kind == ObjectKind::InkStroke) {
            DocColor col = obj.strokeColor;
            if (obj.isHighlighter && col.a > 140) col.a = 98;
            brush->SetColor(ToD2DColor(col));
            if (obj.inkPoints.size() == 1) {
                const float r = (std::max)(1.0f, static_cast<float>(obj.strokeWidth * 0.5 * cam.zoom));
                rt->FillEllipse(D2D1::Ellipse(toScreen(obj.inkPoints[0].x, obj.inkPoints[0].y), r, r), brush);
            } else {
                for (std::size_t i = 1; i < obj.inkPoints.size(); ++i) {
                    const auto& p0 = obj.inkPoints[i - 1];
                    const auto& p1 = obj.inkPoints[i];
                    const double pAvg = 0.5 * (p0.pressure + p1.pressure);
                    const double widthWorld = obj.isHighlighter
                        ? (std::max)(4.0, obj.strokeWidth)
                        : (std::max)(0.6, obj.strokeWidth * (0.45 + 1.1 * pAvg));
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
                brush->SetColor(ToD2DColor(obj.strokeColor));
                const float fontPx = (std::max)(6.0f, static_cast<float>(obj.fontSizePt * cam.zoom));
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
        } else if (obj.kind == ObjectKind::Image) {
            const std::vector<std::uint8_t>* bytesPtr = !obj.pngBytes.empty() ? &obj.pngBytes : doc.FindAsset(obj.assetKey);
            const D2D1_RECT_F sr = rectToScreen(obj.bounds);
            if (bytesPtr && !bytesPtr->empty()) {
                const std::string tag = !obj.assetKey.empty() ? obj.assetKey : ("inline_" + std::to_string(obj.id));
                ID2D1Bitmap* bmp = AcquireGpuBitmapForEncodedAsset(rt, tag, *bytesPtr, cacheGpuAssets);
                if (bmp) {
                    rt->DrawBitmap(bmp, sr);
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
                if (ID2D1Bitmap* bmp = AcquireGpuBitmapForEncodedAsset(rt, tag, *bytesPtr, cacheGpuAssets)) {
                    rt->DrawBitmap(bmp, sr);
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

        if (forceCanvasMode || doc.Mode() == DocumentMode::InfiniteCanvas) {
            rt->Clear(ToD2DColor(DocColor::RGBA(252, 253, 255, 255)));
            const DocRect viewWorld = cam.ScreenRectToWorld({ 0.0, 0.0, static_cast<double>(widthPx), static_cast<double>(heightPx) });
            DrawPaperPattern(rt, brush.Get(), viewWorld, doc.CanvasBackground(), cam);

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
                const bool sel = overlay && overlay->selectedObjectId == o->id;
                DrawAnnotationObject(rt, brush.Get(), doc, *o, cam, 0.0, 0.0, sel, !synchronousOffscreen);
            }
            if (overlay && overlay->draftObject) {
                DrawAnnotationObject(rt, brush.Get(), doc, *overlay->draftObject, cam, 0.0, 0.0, false, !synchronousOffscreen);
            }
            return;
        }

        // Paginated PDF / Notebook Mode
        rt->Clear(ToD2DColor(DocColor::RGBA(36, 42, 52, 255)));
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

            // Shadow + crisp white page card
            brush->SetColor(ToD2DColor(DocColor::RGBA(15, 19, 25, 130)));
            rt->FillRectangle(D2D1::RectF(pageScreen.left + 4.0f, pageScreen.top + 5.0f, pageScreen.right + 4.0f, pageScreen.bottom + 5.0f), brush.Get());
            brush->SetColor(ToD2DColor(DocColor::RGBA(255, 255, 255, 255)));
            rt->FillRectangle(pageScreen, brush.Get());

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
                const bool sel = overlay && overlay->selectedObjectId == obj.id;
                DrawAnnotationObject(rt, brush.Get(), doc, obj, cam, pageWorld.x, pageWorld.y, sel, !synchronousOffscreen);
            }
            if (overlay && overlay->draftObject && p == overlay->draftPageIndex) {
                DrawAnnotationObject(rt, brush.Get(), doc, *overlay->draftObject, cam, pageWorld.x, pageWorld.y, false, !synchronousOffscreen);
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
