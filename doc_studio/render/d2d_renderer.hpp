#pragma once

#include "../core/camera.hpp"
#include "../core/doc_types.hpp"
#include "../core/document_model.hpp"
#include "../core/lru_byte_cache.hpp"
#include "../pdf/winrt_adapters.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace DocStudio {

struct PageCacheKey {
    std::uint64_t pageId = 0;
    std::string sourcePdfPath;
    int sourcePageIndex0 = 0;
    int widthBucketPx = 800;
    int rotationDeg = 0;
    int cropL = 0;
    int cropT = 0;
    int cropR = 1000;
    int cropB = 1000;

    bool operator==(const PageCacheKey& o) const noexcept {
        return pageId == o.pageId &&
               sourcePageIndex0 == o.sourcePageIndex0 &&
               widthBucketPx == o.widthBucketPx &&
               rotationDeg == o.rotationDeg &&
               cropL == o.cropL && cropT == o.cropT &&
               cropR == o.cropR && cropB == o.cropB &&
               sourcePdfPath == o.sourcePdfPath;
    }
};

struct PageCacheKeyHash {
    std::size_t operator()(const PageCacheKey& k) const noexcept {
        std::size_t h = std::hash<std::uint64_t>{}(k.pageId);
        auto mix = [&](std::size_t v) {
            h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
        };
        mix(std::hash<std::string>{}(k.sourcePdfPath));
        mix(static_cast<std::size_t>(k.sourcePageIndex0));
        mix(static_cast<std::size_t>(k.widthBucketPx));
        mix(static_cast<std::size_t>(k.rotationDeg));
        mix(static_cast<std::size_t>((k.cropL * 131) ^ (k.cropT * 997) ^ (k.cropR * 31) ^ k.cropB));
        return h;
    }
};

// Async byte-budgeted LRU page bitmap cache (256 MiB default)
class PageRenderCache {
public:
    explicit PageRenderCache(std::size_t maxBytes = 256ull * 1024ull * 1024ull);
    ~PageRenderCache();

    PageRenderCache(const PageRenderCache&) = delete;
    PageRenderCache& operator=(const PageRenderCache&) = delete;

    static PageCacheKey MakeKey(const PageModel& pg, int targetWidthPx);

    void SetMaxBytes(std::size_t maxBytes);
    std::size_t MaxBytes() const;
    std::size_t BytesUsed() const;
    std::size_t EntryCount() const;
    std::uint64_t EvictionCount() const;

    bool TryGet(const PageCacheKey& key, RasterImageBgra32& outImg);
    void PutDirect(const PageCacheKey& key, RasterImageBgra32 img);
    void RequestAsync(const PageCacheKey& key, std::uint64_t generation, std::function<void()> onReady);
    void InvalidateAll(std::uint64_t newGeneration);
    void Shutdown();

private:
    struct PendingJob {
        PageCacheKey key;
        std::uint64_t generation = 0;
        std::function<void()> onReady;
    };

    LruByteCache<PageCacheKey, RasterImageBgra32, PageCacheKeyHash> cache_;
    std::mutex queueMtx_;
    std::condition_variable cv_;
    std::deque<PendingJob> queue_;
    std::unordered_map<PageCacheKey, std::uint64_t, PageCacheKeyHash> inFlight_;
    std::atomic<std::uint64_t> activeGen_{ 1 };
    std::atomic<bool> stop_{ false };
    std::thread worker_;

    void WorkerLoop();
};

struct ViewportOverlayState {
    const AnnotationObject* draftObject = nullptr;
    std::size_t draftPageIndex = 0;
    std::uint64_t selectedObjectId = 0;
    std::vector<std::uint64_t> lassoSelectedIds;
    bool hasCropPreview = false;
    std::size_t cropPageIndex = 0;
    DocRect cropPreviewPageLocal{};
    const std::vector<SearchMatch>* searchMatches = nullptr;
    std::size_t activeSearchMatchIndex = 0;
    bool replayActive = false;
    std::uint64_t replayCursorMs = 0;
    LaserPointerState laserPointer{};
    bool presenterMode = false;
    bool minimapVisible = false;
    RulerState rulerState{};
    bool focusMode = false;
    bool darkTheme = false;
    bool highContrast = false;
    const RasterImageBgra32* floatingOverlayBgra = nullptr;
    DocRect floatingOverlayScreenRect{};
    bool hasPdfTextSel = false;
    std::size_t pdfTextSelPage = 0;
    DocRect pdfTextSelRectLocal{};
    bool showSelectionHandles = true;
};

class D2DRenderer {
public:
    D2DRenderer();
    ~D2DRenderer();

    D2DRenderer(const D2DRenderer&) = delete;
    D2DRenderer& operator=(const D2DRenderer&) = delete;

    bool InitializeFactories();
#ifdef _WIN32
    void AttachHwnd(HWND hwnd, int widthPx, int heightPx);
#else
    void RenderToCairo(
        void* cairoContext,
        const DocumentModel& doc,
        const Camera2D& camera,
        int widthPx,
        int heightPx,
        PageRenderCache* pageCache,
        std::uint64_t asyncGen,
        std::function<void()> onAsyncPageReady,
        const ViewportOverlayState* overlay = nullptr);
#endif
    void Resize(int widthPx, int heightPx);
    void DiscardDeviceResources();

    void RenderViewport(
        const DocumentModel& doc,
        const Camera2D& camera,
        int widthPx,
        int heightPx,
        PageRenderCache* pageCache,
        std::uint64_t asyncGen,
        std::function<void()> onAsyncPageReady,
        const ViewportOverlayState* overlay = nullptr);

    bool RenderPageOffscreen(
        const DocumentModel& doc,
        std::size_t pageIndex0,
        int widthPx,
        PageRenderCache* pageCache,
        RasterImageBgra32& outBgra);

    bool RenderInfiniteCanvasOffscreen(
        const DocumentModel& doc,
        const DocRect& worldRegion,
        int widthPx,
        int heightPx,
        RasterImageBgra32& outBgra);

    bool RenderHwndViewportToOffscreen(
        const DocumentModel& doc,
        const Camera2D& camera,
        int widthPx,
        int heightPx,
        PageRenderCache* pageCache,
        RasterImageBgra32& outBgra,
        const ViewportOverlayState* overlay = nullptr);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace DocStudio
