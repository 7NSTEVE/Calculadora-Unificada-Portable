#include "page_layout.hpp"

#include <algorithm>

namespace DocStudio {

void PageLayout::Rebuild(const std::vector<PageModel>& pages, double pageGapPt, double topMarginPt)
{
    gapPt_ = (std::max)(0.0, pageGapPt);
    topMarginPt_ = (std::max)(0.0, topMarginPt);
    placements_.clear();
    placements_.reserve(pages.size());

    double curY = topMarginPt_;
    maxWidth_ = 0.0;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const double w = pages[i].EffectiveWidthPt();
        const double h = pages[i].EffectiveHeightPt();
        placements_.push_back({ i, curY, w, h });
        if (w > maxWidth_) maxWidth_ = w;
        curY += h + gapPt_;
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
    const DocRect r = it->WorldRect();
    if (!r.Contains(wx, wy, 0.0)) return false;
    outPageIndex = it->pageIndex;
    outLocalX = wx - r.x;
    outLocalY = wy - r.y;
    return true;
}

} // namespace DocStudio
