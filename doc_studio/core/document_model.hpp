#pragma once

#include "doc_types.hpp"
#include "page_layout.hpp"
#include "spatial_index.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace DocStudio {

// Pure C++17 document state: paginated PDF pages + Infinite Canvas + Quadtree + Asset registry.
class DocumentModel {
public:
    DocumentModel();

    void ResetToBlank(DocumentMode mode = DocumentMode::PaginatedPdf);

    DocumentMode Mode() const noexcept { return mode_; }
    void SetMode(DocumentMode m) noexcept { mode_ = m; ++revision_; }

    const std::string& Title() const noexcept { return titleUtf8_; }
    void SetTitle(std::string t) { titleUtf8_ = std::move(t); ++revision_; }

    std::uint64_t Revision() const noexcept { return revision_; }
    void BumpRevision() noexcept { ++revision_; }
    bool IsDirty() const noexcept { return dirty_; }
    void SetDirty(bool d) noexcept { dirty_ = d; }

    std::uint64_t AllocateObjectId() noexcept { return ++nextObjectId_; }
    std::uint64_t AllocatePageId() noexcept { return ++nextPageId_; }
    std::uint64_t AllocateZOrder() noexcept { return ++nextZOrder_; }

    std::uint64_t NextObjectId() const noexcept { return nextObjectId_; }
    std::uint64_t NextPageId() const noexcept { return nextPageId_; }
    std::uint64_t NextZOrder() const noexcept { return nextZOrder_; }
    std::uint64_t NextAssetSeq() const noexcept { return nextAssetSeq_; }
    void RestoreCounters(
        std::uint64_t nextObj,
        std::uint64_t nextPage,
        std::uint64_t nextZ,
        std::uint64_t nextAsset) noexcept;

    // Assets (embedded PNG/binary blobs shared cleanly across undo/redo and save/load)
    std::string RegisterAsset(std::vector<std::uint8_t> bytes);
    void SetNamedAsset(const std::string& key, std::vector<std::uint8_t> bytes);
    const std::vector<std::uint8_t>* FindAsset(const std::string& key) const;
    const std::unordered_map<std::string, std::vector<std::uint8_t>>& Assets() const noexcept { return assets_; }

    // Paginated Document API
    const std::vector<PageModel>& Pages() const noexcept { return pages_; }
    std::vector<PageModel>& MutablePages() noexcept { return pages_; }
    PageLayout& Layout() noexcept { return layout_; }
    const PageLayout& Layout() const noexcept { return layout_; }
    void RebuildLayout();

    // Infinite Canvas API
    PaperPattern CanvasBackground() const noexcept { return canvasBg_; }
    void SetCanvasBackground(PaperPattern bg) noexcept { canvasBg_ = bg; ++revision_; }
    const std::vector<AnnotationObject>& CanvasObjects() const noexcept { return canvasObjects_; }
    void AddCanvasObject(AnnotationObject obj);
    bool RemoveCanvasObject(std::uint64_t id, AnnotationObject* outRemoved = nullptr);
    AnnotationObject* FindCanvasObjectMutable(std::uint64_t id);
    const AnnotationObject* FindCanvasObject(std::uint64_t id) const;
    std::vector<std::uint64_t> QueryCanvasRect(const DocRect& worldRect) const;
    std::size_t CanvasQuadtreeSize() const noexcept { return canvasIndex_.Size(); }
    void RebuildCanvasIndex();

    // Text Search across pages (extracted PDF text, OCR text, and TextBox annotations)
    std::vector<SearchMatch> SearchText(const std::string& queryUtf8, bool caseSensitive = false) const;

    // Direct page object helpers
    bool AddPageObject(std::size_t pageIdx, AnnotationObject obj);
    bool RemovePageObject(std::size_t pageIdx, std::uint64_t id, AnnotationObject* outRemoved = nullptr);
    AnnotationObject* FindPageObjectMutable(std::size_t pageIdx, std::uint64_t id);

private:
    DocumentMode mode_ = DocumentMode::PaginatedPdf;
    std::string titleUtf8_ = "Sin t\xC3\xADtulo";
    std::uint64_t revision_ = 1;
    bool dirty_ = false;

    std::uint64_t nextObjectId_ = 0;
    std::uint64_t nextPageId_ = 0;
    std::uint64_t nextZOrder_ = 0;
    std::uint64_t nextAssetSeq_ = 0;

    std::vector<PageModel> pages_;
    PageLayout layout_;

    PaperPattern canvasBg_ = PaperPattern::Grid;
    std::vector<AnnotationObject> canvasObjects_;
    std::unordered_map<std::uint64_t, std::size_t> canvasIdToPos_;
    QuadtreeIndex canvasIndex_;

    std::unordered_map<std::string, std::vector<std::uint8_t>> assets_;
};

} // namespace DocStudio
