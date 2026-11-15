#include "spatial_index.hpp"

#include <algorithm>
#include <unordered_set>

namespace DocStudio {

QuadtreeIndex::QuadtreeIndex(DocRect worldBounds, int maxDepth, std::size_t splitThreshold)
    : rootBounds_(worldBounds),
      maxDepth_(maxDepth),
      splitThreshold_(splitThreshold)
{
    Clear();
}

void QuadtreeIndex::Clear()
{
    entries_.clear();
    root_ = std::make_unique<Node>();
    root_->bounds = rootBounds_;
    root_->depth = 0;
    root_->isLeaf = true;
}

void QuadtreeIndex::Insert(std::uint64_t objectId, const DocRect& bounds)
{
    auto it = entries_.find(objectId);
    if (it != entries_.end()) {
        RemoveFromNode(root_.get(), objectId, it->second);
    }
    entries_[objectId] = bounds;
    InsertIntoNode(root_.get(), objectId, bounds);
}

bool QuadtreeIndex::Remove(std::uint64_t objectId)
{
    auto it = entries_.find(objectId);
    if (it == entries_.end()) return false;
    const DocRect oldBounds = it->second;
    entries_.erase(it);
    RemoveFromNode(root_.get(), objectId, oldBounds);
    return true;
}

void QuadtreeIndex::Update(std::uint64_t objectId, const DocRect& newBounds)
{
    Insert(objectId, newBounds);
}

void QuadtreeIndex::Subdivide(Node* node)
{
    if (!node || !node->isLeaf) return;
    const double hw = node->bounds.w * 0.5;
    const double hh = node->bounds.h * 0.5;
    const double x = node->bounds.x;
    const double y = node->bounds.y;

    const DocRect qRects[4] = {
        { x,      y,      hw, hh },
        { x + hw, y,      hw, hh },
        { x,      y + hh, hw, hh },
        { x + hw, y + hh, hw, hh }
    };
    for (int i = 0; i < 4; ++i) {
        node->children[i] = std::make_unique<Node>();
        node->children[i]->bounds = qRects[i];
        node->children[i]->depth = node->depth + 1;
        node->children[i]->isLeaf = true;
    }
    node->isLeaf = false;

    std::vector<std::uint64_t> stayHere;
    stayHere.reserve(node->ids.size());
    for (std::uint64_t id : node->ids) {
        auto it = entries_.find(id);
        if (it == entries_.end()) continue;
        const DocRect& b = it->second;
        int matchChild = -1;
        int overlapCount = 0;
        for (int c = 0; c < 4; ++c) {
            if (node->children[c]->bounds.Intersects(b)) {
                matchChild = c;
                ++overlapCount;
            }
        }
        if (overlapCount == 1 && matchChild >= 0) {
            InsertIntoNode(node->children[matchChild].get(), id, b);
        } else {
            stayHere.push_back(id);
        }
    }
    node->ids.swap(stayHere);
}

void QuadtreeIndex::InsertIntoNode(Node* node, std::uint64_t id, const DocRect& r)
{
    if (!node) return;
    if (!node->isLeaf) {
        int matchChild = -1;
        int overlapCount = 0;
        for (int c = 0; c < 4; ++c) {
            if (node->children[c]->bounds.Intersects(r)) {
                matchChild = c;
                ++overlapCount;
            }
        }
        if (overlapCount == 1 && matchChild >= 0) {
            InsertIntoNode(node->children[matchChild].get(), id, r);
            return;
        }
    }
    node->ids.push_back(id);
    if (node->isLeaf && node->ids.size() > splitThreshold_ && node->depth < maxDepth_) {
        Subdivide(node);
    }
}

bool QuadtreeIndex::RemoveFromNode(Node* node, std::uint64_t id, const DocRect& r)
{
    if (!node) return false;
    auto& vec = node->ids;
    for (std::size_t i = 0; i < vec.size(); ++i) {
        if (vec[i] == id) {
            vec[i] = vec.back();
            vec.pop_back();
            return true;
        }
    }
    if (!node->isLeaf) {
        for (int c = 0; c < 4; ++c) {
            if (node->children[c] && node->children[c]->bounds.Intersects(r)) {
                if (RemoveFromNode(node->children[c].get(), id, r)) return true;
            }
        }
    }
    return false;
}

void QuadtreeIndex::QueryNode(
    const Node* node,
    const DocRect& viewRect,
    std::vector<std::uint64_t>& out) const
{
    if (!node || !node->bounds.Intersects(viewRect)) return;
    for (std::uint64_t id : node->ids) {
        auto it = entries_.find(id);
        if (it != entries_.end() && it->second.Intersects(viewRect)) {
            out.push_back(id);
        }
    }
    if (!node->isLeaf) {
        for (int c = 0; c < 4; ++c) {
            if (node->children[c]) {
                QueryNode(node->children[c].get(), viewRect, out);
            }
        }
    }
}

std::vector<std::uint64_t> QuadtreeIndex::QueryRect(const DocRect& viewRect) const
{
    std::vector<std::uint64_t> out;
    QueryNode(root_.get(), viewRect, out);
    return out;
}

} // namespace DocStudio
