#include "page_layout.hpp"

#include <algorithm>

namespace DocStudio {

void PageLayout::Rebuild(
    const std::vector<PageModel>& pages,
    double pageGapPt,
    double topMarginPt,
    const std::vector<bool>* collapsedMask,
    bool doublePageSpread)
{
    gapPt_ = (std::max)(0.0, pageGapPt);
    topMarginPt_ = (std::max)(0.0, topMarginPt);
    doublePageSpread_ = doublePageSpread;
    placements_.clear();
    placements_.reserve(pages.size());

    auto isCollapsedAt = [&](std::size_t idx) -> bool {
        return collapsedMask && idx < collapsedMask->size() && (*collapsedMask)[idx];
    };

    double curY = topMarginPt_;
    maxWidth_ = 0.0;
    constexpr double kCollapsedBannerH = 34.0;
    constexpr double kSpreadGapX = 16.0;

    std::size_t i = 0;
    while (i < pages.size()) {
        if (isCollapsedAt(i)) {
            std::size_t endIdx = i;
            while (endIdx + 1 < pages.size() && isCollapsedAt(endIdx + 1)) {
                ++endIdx;
            }
            const double w = (std::max)(320.0, pages[i].EffectiveWidthPt());
            // First page in collapsed run holds the visible clickable accordion banner;
            // subsequent pages in the run have height 0 so total vertical height truly collapses!
            PagePlacement hdr{};
            hdr.pageIndex = i;
            hdr.leftWorld = -w * 0.5;
            hdr.topWorld = curY;
            hdr.widthWorld = w;
            hdr.heightWorld = kCollapsedBannerH;
            hdr.collapsed = true;
            hdr.collapsedHeader = true;
            hdr.collapsedEndPage0 = static_cast<int>(endIdx);
            placements_.push_back(hdr);
            if (w > maxWidth_) maxWidth_ = w;
            for (std::size_t k = i + 1; k <= endIdx; ++k) {
                PagePlacement hidden{};
                hidden.pageIndex = k;
                hidden.leftWorld = -w * 0.5;
                hidden.topWorld = curY + kCollapsedBannerH;
                hidden.widthWorld = 0.0;
                hidden.heightWorld = 0.0;
                hidden.collapsed = true;
                hidden.collapsedHeader = false;
                hidden.collapsedEndPage0 = static_cast<int>(endIdx);
                placements_.push_back(hidden);
            }
            curY += kCollapsedBannerH + gapPt_;
            i = endIdx + 1;
            continue;
        }

        if (doublePageSpread && (i + 1 < pages.size()) && !isCollapsedAt(i + 1)) {
            const double w0 = pages[i].EffectiveWidthPt();
            const double h0 = pages[i].EffectiveHeightPt();
            const double w1 = pages[i + 1].EffectiveWidthPt();
            const double h1 = pages[i + 1].EffectiveHeightPt();
            const double totalW = w0 + kSpreadGapX + w1;
            const double rowH = (std::max)(h0, h1);
            const double x0 = -totalW * 0.5;

            PagePlacement p0{};
            p0.pageIndex = i;
            p0.leftWorld = x0;
            p0.topWorld = curY;
            p0.widthWorld = w0;
            p0.heightWorld = h0;
            placements_.push_back(p0);

            PagePlacement p1{};
            p1.pageIndex = i + 1;
            p1.leftWorld = x0 + w0 + kSpreadGapX;
            p1.topWorld = curY;
            p1.widthWorld = w1;
            p1.heightWorld = h1;
            placements_.push_back(p1);

            if (totalW > maxWidth_) maxWidth_ = totalW;
            curY += rowH + gapPt_;
            i += 2;
            continue;
        }

        const double w = pages[i].EffectiveWidthPt();
        const double h = pages[i].EffectiveHeightPt();
        PagePlacement pl{};
        pl.pageIndex = i;
        pl.leftWorld = -w * 0.5;
        pl.topWorld = curY;
        pl.widthWorld = w;
        pl.heightWorld = h;
        placements_.push_back(pl);
        if (w > maxWidth_) maxWidth_ = w;
        curY += h + gapPt_;
        ++i;
    }
    totalHeight_ = pages.empty() ? 0.0 : (curY - gapPt_ + topMarginPt_);
}

const PagePlacement* PageLayout::PlacementAt(std::size_t pageIndex) const noexcept
{
    if (pageIndex >= placements_.size()) return nullptr;
    return &placements_[pageIndex];
}

bool PageLayout::ComputeVisibleRange(
    double viewTopWorld,
    double viewBottomWorld,
    std::size_t& outFirst,
    std::size_t& outLast,
    std::size_t prefetchMargin) const noexcept
{
    if (placements_.empty()) return false;
    if (viewBottomWorld < viewTopWorld) std::swap(viewTopWorld, viewBottomWorld);

    // Binary search first placement whose bottom >= viewTopWorld
    auto itFirst = std::lower_bound(
        placements_.begin(), placements_.end(), viewTopWorld,
        [](const PagePlacement& p, double y) {
            return (p.topWorld + p.heightWorld) < y;
        });

    std::size_t first = (itFirst == placements_.end())
        ? (placements_.size() - 1)
        : static_cast<std::size_t>(itFirst - placements_.begin());

    // Binary search first placement whose top > viewBottomWorld
    auto itPast = std::upper_bound(
        placements_.begin() + static_cast<std::ptrdiff_t>(first), placements_.end(), viewBottomWorld,
        [](double y, const PagePlacement& p) {
            return y < p.topWorld;
        });

    std::size_t last = (itPast == placements_.begin())
        ? 0
        : static_cast<std::size_t>((itPast - placements_.begin()) - 1);
    if (last < first) last = first;

    outFirst = (first > prefetchMargin) ? (first - prefetchMargin) : 0;
    outLast = (std::min)(placements_.size() - 1, last + prefetchMargin);
    return true;
}

bool PageLayout::HitTestWorldPoint(
    double wx,
    double wy,
    std::size_t& outPageIndex,
    double& outLocalX,
    double& outLocalY) const noexcept
{
    if (placements_.empty()) return false;
    auto it = std::lower_bound(
        placements_.begin(), placements_.end(), wy,
        [](const PagePlacement& p, double y) {
            return (p.topWorld + p.heightWorld) < y;
        });
    if (it == placements_.end()) return false;
    // Check both pages in a potential double-page row starting around it
    std::size_t startIdx = static_cast<std::size_t>(it - placements_.begin());
    if (startIdx > 0) --startIdx;
    const std::size_t endIdx = (std::min)(placements_.size(), startIdx + 4);
    for (std::size_t idx = startIdx; idx < endIdx; ++idx) {
        const auto& pl = placements_[idx];
        if (pl.widthWorld <= 0.0 || pl.heightWorld <= 0.0) continue;
        const DocRect r = pl.WorldRect();
        if (r.Contains(wx, wy, 0.0)) {
            outPageIndex = pl.pageIndex;
            outLocalX = wx - r.x;
            outLocalY = wy - r.y;
            return true;
        }
    }
    return false;
}

} // namespace DocStudio
