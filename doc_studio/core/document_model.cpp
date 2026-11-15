#include "document_model.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace DocStudio {

namespace {

std::string AsciiLower(const std::string& s) {
    std::string r = s;
    for (char& c : r) {
        if (static_cast<unsigned char>(c) < 128) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    return r;
}

void SearchInText(
    std::size_t pageIdx,
    const std::string& haystack,
    const std::string& needle,
    bool caseSensitive,
    std::vector<SearchMatch>& out)
{
    if (haystack.empty() || needle.empty()) return;
    const std::string h = caseSensitive ? haystack : AsciiLower(haystack);
    const std::string n = caseSensitive ? needle : AsciiLower(needle);
    std::size_t pos = 0;
    while ((pos = h.find(n, pos)) != std::string::npos) {
        const std::size_t snipStart = (pos > 28) ? (pos - 28) : 0;
        const std::size_t snipEnd = (std::min)(haystack.size(), pos + needle.size() + 36);
        SearchMatch m{};
        m.pageIndex = pageIdx;
        m.charOffset = pos;
        m.charLength = needle.size();
        m.snippetUtf8 = haystack.substr(snipStart, snipEnd - snipStart);
        out.push_back(std::move(m));
        pos += (std::max<std::size_t>)(1u, n.size());
    }
}

} // namespace

DocumentModel::DocumentModel()
    : canvasIndex_({ -1e6, -1e6, 2e6, 2e6 }, 10, 24)
{
    ResetToBlank(DocumentMode::PaginatedPdf);
}

void DocumentModel::ResetToBlank(DocumentMode mode) {
    mode_ = mode;
    titleUtf8_ = "Documento nuevo";
    dirty_ = false;
    nextObjectId_ = 0;
    nextPageId_ = 0;
    nextZOrder_ = 0;
    nextAssetSeq_ = 0;
    pages_.clear();
    canvasObjects_.clear();
    canvasIdToPos_.clear();
    canvasIndex_.Clear();
    assets_.clear();

    PageModel p;
    p.pageId = AllocatePageId();
    p.widthPt = 595.0;
    p.heightPt = 842.0;
    p.background = PaperPattern::Plain;
    pages_.push_back(std::move(p));
    RebuildLayout();
    BumpRevision();
}

void DocumentModel::RebuildLayout() {
    layout_.Rebuild(pages_);
    BumpRevision();
}

void DocumentModel::RestoreCounters(
    std::uint64_t nextObj,
    std::uint64_t nextPage,
    std::uint64_t nextZ,
    std::uint64_t nextAsset) noexcept
{
    if (nextObj > nextObjectId_) nextObjectId_ = nextObj;
    if (nextPage > nextPageId_) nextPageId_ = nextPage;
    if (nextZ > nextZOrder_) nextZOrder_ = nextZ;
    if (nextAsset > nextAssetSeq_) nextAssetSeq_ = nextAsset;
}

std::string DocumentModel::RegisterAsset(std::vector<std::uint8_t> bytes) {
    const std::string key = "asset_" + std::to_string(++nextAssetSeq_);
    assets_[key] = std::move(bytes);
    return key;
}

void DocumentModel::SetNamedAsset(const std::string& key, std::vector<std::uint8_t> bytes) {
    assets_[key] = std::move(bytes);
}

const std::vector<std::uint8_t>* DocumentModel::FindAsset(const std::string& key) const {
    auto it = assets_.find(key);
    return (it == assets_.end()) ? nullptr : &it->second;
}

void DocumentModel::AddCanvasObject(AnnotationObject obj) {
    if (obj.id == 0) obj.id = AllocateObjectId();
    if (obj.zOrder == 0) obj.zOrder = AllocateZOrder();
    obj.RecomputeBounds();

    auto existing = canvasIdToPos_.find(obj.id);
    if (existing != canvasIdToPos_.end()) {
        canvasObjects_[existing->second] = obj;
        canvasIndex_.Update(obj.id, obj.bounds);
    } else {
        const std::size_t idx = canvasObjects_.size();
        canvasIdToPos_[obj.id] = idx;
        canvasObjects_.push_back(obj);
        canvasIndex_.Insert(obj.id, obj.bounds);
    }
    if (obj.id > nextObjectId_) nextObjectId_ = obj.id;
    if (obj.zOrder > nextZOrder_) nextZOrder_ = obj.zOrder;
    dirty_ = true;
    BumpRevision();
}

bool DocumentModel::RemoveCanvasObject(std::uint64_t id, AnnotationObject* outRemoved) {
    auto it = canvasIdToPos_.find(id);
    if (it == canvasIdToPos_.end()) return false;
    const std::size_t idx = it->second;
    if (outRemoved) *outRemoved = canvasObjects_[idx];

    canvasIndex_.Remove(id);
    const std::size_t lastIdx = canvasObjects_.size() - 1;
    if (idx != lastIdx) {
        canvasObjects_[idx] = std::move(canvasObjects_[lastIdx]);
        canvasIdToPos_[canvasObjects_[idx].id] = idx;
    }
    canvasObjects_.pop_back();
    canvasIdToPos_.erase(it);
    dirty_ = true;
    BumpRevision();
    return true;
}

AnnotationObject* DocumentModel::FindCanvasObjectMutable(std::uint64_t id) {
    auto it = canvasIdToPos_.find(id);
    return (it == canvasIdToPos_.end()) ? nullptr : &canvasObjects_[it->second];
}

const AnnotationObject* DocumentModel::FindCanvasObject(std::uint64_t id) const {
    auto it = canvasIdToPos_.find(id);
    return (it == canvasIdToPos_.end()) ? nullptr : &canvasObjects_[it->second];
}

std::vector<std::uint64_t> DocumentModel::QueryCanvasRect(const DocRect& worldRect) const {
    return canvasIndex_.QueryRect(worldRect);
}

void DocumentModel::RebuildCanvasIndex() {
    canvasIndex_.Clear();
    canvasIdToPos_.clear();
    for (std::size_t i = 0; i < canvasObjects_.size(); ++i) {
        canvasObjects_[i].RecomputeBounds();
        canvasIdToPos_[canvasObjects_[i].id] = i;
        canvasIndex_.Insert(canvasObjects_[i].id, canvasObjects_[i].bounds);
        if (canvasObjects_[i].id > nextObjectId_) nextObjectId_ = canvasObjects_[i].id;
        if (canvasObjects_[i].zOrder > nextZOrder_) nextZOrder_ = canvasObjects_[i].zOrder;
    }
}

bool DocumentModel::AddPageObject(std::size_t pageIdx, AnnotationObject obj) {
    if (pageIdx >= pages_.size()) return false;
    if (obj.id == 0) obj.id = AllocateObjectId();
    if (obj.zOrder == 0) obj.zOrder = AllocateZOrder();
    obj.RecomputeBounds();
    if (obj.id > nextObjectId_) nextObjectId_ = obj.id;
    if (obj.zOrder > nextZOrder_) nextZOrder_ = obj.zOrder;
    pages_[pageIdx].objects.push_back(std::move(obj));
    dirty_ = true;
    BumpRevision();
    return true;
}

bool DocumentModel::RemovePageObject(std::size_t pageIdx, std::uint64_t id, AnnotationObject* outRemoved) {
    if (pageIdx >= pages_.size()) return false;
    auto& objs = pages_[pageIdx].objects;
    for (std::size_t i = 0; i < objs.size(); ++i) {
        if (objs[i].id == id) {
            if (outRemoved) *outRemoved = objs[i];
            objs.erase(objs.begin() + static_cast<std::ptrdiff_t>(i));
            dirty_ = true;
            BumpRevision();
            return true;
        }
    }
    return false;
}

AnnotationObject* DocumentModel::FindPageObjectMutable(std::size_t pageIdx, std::uint64_t id) {
    if (pageIdx >= pages_.size()) return nullptr;
    for (auto& o : pages_[pageIdx].objects) {
        if (o.id == id) return &o;
    }
    return nullptr;
}

std::vector<SearchMatch> DocumentModel::SearchText(const std::string& queryUtf8, bool caseSensitive) const {
    std::vector<SearchMatch> out;
    if (queryUtf8.empty()) return out;

    for (std::size_t p = 0; p < pages_.size(); ++p) {
        const auto& pg = pages_[p];
        SearchInText(p, pg.extractedTextUtf8, queryUtf8, caseSensitive, out);
        SearchInText(p, pg.ocrTextUtf8, queryUtf8, caseSensitive, out);
        for (const auto& obj : pg.objects) {
            if (obj.kind == ObjectKind::TextBox && !obj.textUtf8.empty()) {
                const std::string h = caseSensitive ? obj.textUtf8 : AsciiLower(obj.textUtf8);
                const std::string n = caseSensitive ? queryUtf8 : AsciiLower(queryUtf8);
                if (h.find(n) != std::string::npos) {
                    SearchMatch m{};
                    m.pageIndex = p;
                    m.charOffset = 0;
                    m.charLength = queryUtf8.size();
                    m.snippetUtf8 = obj.textUtf8;
                    m.hasBox = true;
                    const double pw = (std::max)(1.0, pg.EffectiveWidthPt());
                    const double ph = (std::max)(1.0, pg.EffectiveHeightPt());
                    m.boxNormalized = {
                        obj.bounds.x / pw,
                        obj.bounds.y / ph,
                        obj.bounds.w / pw,
                        obj.bounds.h / ph
                    };
                    out.push_back(std::move(m));
                }
            }
        }
    }
    return out;
}

} // namespace DocStudio
