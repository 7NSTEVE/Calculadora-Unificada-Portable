#pragma once

#include "doc_types.hpp"
#include <cstddef>
#include <vector>

namespace DocStudio {

struct PagePlacement {
    std::size_t pageIndex = 0;
    double topWorld = 0.0;    // top in world coordinates (at 1.0 scale, centered horizontally around x=0)
    double widthWorld = 595.0;
    double heightWorld = 842.0;

    DocRect WorldRect() const noexcept {
        return { -widthWorld * 0.5, topWorld, widthWorld, heightWorld };
    }
};

// Virtualized multi-page vertical continuous layout using prefix sums & O(log N) binary search.
class PageLayout {
public:
    void Rebuild(const std::vector<PageModel>& pages, double pageGapPt = 18.0, double topMarginPt = 16.0);

    std::size_t PageCount() const noexcept { return placements_.size(); }
    double TotalHeight() const noexcept { return totalHeight_; }
    double MaxWidth() const noexcept { return maxWidth_; }

    const PagePlacement* PlacementAt(std::size_t pageIndex) const noexcept;

    // Finds inclusive [firstPage, lastPage] visible inside [viewTopWorld, viewBottomWorld].
    bool ComputeVisibleRange(
        double viewTopWorld,
        double viewBottomWorld,
        std::size_t& outFirst,
        std::size_t& outLast,
        std::size_t prefetchMargin = 1) const noexcept;

    // Hit-tests a world point against page rects. Returns true if inside a page.
    bool HitTestWorldPoint(
        double wx,
        double wy,
        std::size_t& outPageIndex,
        double& outLocalX,
        double& outLocalY) const noexcept;

private:
    std::vector<PagePlacement> placements_;
    double gapPt_ = 18.0;
    double topMarginPt_ = 16.0;
    double totalHeight_ = 0.0;
    double maxWidth_ = 595.0;
};

} // namespace DocStudio
