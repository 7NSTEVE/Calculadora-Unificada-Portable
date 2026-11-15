#include "command_stack.hpp"

#include <algorithm>
#include <unordered_set>

namespace DocStudio {

namespace {

int NormalizeDegrees(int deg) noexcept {
    int r = deg % 360;
    if (r < 0) r += 360;
    return r;
}

bool AddToTarget(DocumentModel& doc, const ObjectTarget& target, AnnotationObject obj) {
    if (target.isCanvas) {
        doc.AddCanvasObject(std::move(obj));
        return true;
    }
    return doc.AddPageObject(target.pageIndex, std::move(obj));
}

bool RemoveFromTarget(DocumentModel& doc, const ObjectTarget& target, std::uint64_t id, AnnotationObject* out) {
    if (target.isCanvas) {
        return doc.RemoveCanvasObject(id, out);
    }
    return doc.RemovePageObject(target.pageIndex, id, out);
}

bool ReplaceInTarget(DocumentModel& doc, const ObjectTarget& target, const AnnotationObject& state) {
    if (target.isCanvas) {
        AnnotationObject* existing = doc.FindCanvasObjectMutable(state.id);
        if (!existing) return false;
        doc.AddCanvasObject(state);
        return true;
    }
    AnnotationObject* existing = doc.FindPageObjectMutable(target.pageIndex, state.id);
    if (!existing) return false;
    *existing = state;
    existing->RecomputeBounds();
    doc.BumpRevision();
    return true;
}

} // namespace

bool CommandStack::Execute(DocumentModel& doc, std::unique_ptr<ICommand> cmd) {
    if (!cmd) return false;
    if (!cmd->Execute(doc)) return false;

    if (cursor_ < history_.size()) {
        if (!cleanInvalidated_ && cleanCursor_ > cursor_) {
            cleanInvalidated_ = true;
        }
        history_.erase(history_.begin() + static_cast<std::ptrdiff_t>(cursor_), history_.end());
    }

    history_.push_back(std::move(cmd));
    cursor_ = history_.size();

    if (maxDepth_ > 0 && history_.size() > maxDepth_) {
        const std::size_t dropCount = history_.size() - maxDepth_;
        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(dropCount));
        cursor_ -= dropCount;
        if (cleanCursor_ < dropCount) {
            cleanInvalidated_ = true;
            cleanCursor_ = 0;
        } else {
            cleanCursor_ -= dropCount;
        }
    }

    UpdateDirtyFlag(doc);
    return true;
}

bool CommandStack::Undo(DocumentModel& doc) {
    if (!CanUndo()) return false;
    auto& cmd = history_[cursor_ - 1];
    if (!cmd->Undo(doc)) return false;
    --cursor_;
    UpdateDirtyFlag(doc);
    return true;
}

bool CommandStack::Redo(DocumentModel& doc) {
    if (!CanRedo()) return false;
    auto& cmd = history_[cursor_];
    if (!cmd->Execute(doc)) return false;
    ++cursor_;
    UpdateDirtyFlag(doc);
    return true;
}

std::string CommandStack::UndoDescription() const {
    if (!CanUndo()) return {};
    return history_[cursor_ - 1]->Description();
}

std::string CommandStack::RedoDescription() const {
    if (!CanRedo()) return {};
    return history_[cursor_]->Description();
}

void CommandStack::MarkClean(DocumentModel& doc) noexcept {
    cleanCursor_ = cursor_;
    cleanInvalidated_ = false;
    doc.SetDirty(false);
}

void CommandStack::Clear() noexcept {
    history_.clear();
    cursor_ = 0;
    cleanCursor_ = 0;
    cleanInvalidated_ = false;
}

void CommandStack::UpdateDirtyFlag(DocumentModel& doc) const noexcept {
    if (cleanInvalidated_) {
        doc.SetDirty(true);
    } else {
        doc.SetDirty(cursor_ != cleanCursor_);
    }
}

// AddObjectCommand
AddObjectCommand::AddObjectCommand(ObjectTarget target, AnnotationObject obj)
    : target_(target), obj_(std::move(obj)) {}

std::string AddObjectCommand::Description() const {
    return "Agregar objeto";
}

bool AddObjectCommand::Execute(DocumentModel& doc) {
    if (obj_.id == 0) obj_.id = doc.AllocateObjectId();
    if (obj_.zOrder == 0) obj_.zOrder = doc.AllocateZOrder();
    obj_.RecomputeBounds();
    return AddToTarget(doc, target_, obj_);
}

bool AddObjectCommand::Undo(DocumentModel& doc) {
    return RemoveFromTarget(doc, target_, obj_.id, nullptr);
}

// RemoveObjectCommand
RemoveObjectCommand::RemoveObjectCommand(ObjectTarget target, std::uint64_t objectId)
    : target_(target), objectId_(objectId) {}

std::string RemoveObjectCommand::Description() const {
    return "Eliminar objeto";
}

bool RemoveObjectCommand::Execute(DocumentModel& doc) {
    AnnotationObject removed;
    if (!RemoveFromTarget(doc, target_, objectId_, &removed)) return false;
    captured_ = std::move(removed);
    return true;
}

bool RemoveObjectCommand::Undo(DocumentModel& doc) {
    if (!captured_.has_value()) return false;
    return AddToTarget(doc, target_, *captured_);
}

// BatchEraseObjectsCommand
BatchEraseObjectsCommand::BatchEraseObjectsCommand(ObjectTarget target, std::vector<std::uint64_t> objectIds)
    : target_(target), objectIds_(std::move(objectIds)) {}

std::string BatchEraseObjectsCommand::Description() const {
    return "Borrador de trazos";
}

bool BatchEraseObjectsCommand::Execute(DocumentModel& doc) {
    captured_.clear();
    std::unordered_set<std::uint64_t> seen;
    for (std::uint64_t id : objectIds_) {
        if (!seen.insert(id).second) continue;
        AnnotationObject removed;
        if (RemoveFromTarget(doc, target_, id, &removed)) {
            captured_.push_back(std::move(removed));
        }
    }
    return !captured_.empty();
}

bool BatchEraseObjectsCommand::Undo(DocumentModel& doc) {
    if (captured_.empty()) return false;
    bool allOk = true;
    for (const auto& obj : captured_) {
        allOk = AddToTarget(doc, target_, obj) && allOk;
    }
    return allOk;
}

// ModifyObjectCommand
ModifyObjectCommand::ModifyObjectCommand(ObjectTarget target, AnnotationObject beforeState, AnnotationObject afterState)
    : target_(target), before_(std::move(beforeState)), after_(std::move(afterState)) {}

std::string ModifyObjectCommand::Description() const {
    return "Modificar objeto";
}

bool ModifyObjectCommand::Execute(DocumentModel& doc) {
    return ReplaceInTarget(doc, target_, after_);
}

bool ModifyObjectCommand::Undo(DocumentModel& doc) {
    return ReplaceInTarget(doc, target_, before_);
}

// SetBackgroundCommand
SetBackgroundCommand::SetBackgroundCommand(ObjectTarget target, PaperPattern newBg)
    : target_(target), newBg_(newBg) {}

std::string SetBackgroundCommand::Description() const {
    return "Cambiar fondo de papel";
}

bool SetBackgroundCommand::Execute(DocumentModel& doc) {
    if (target_.isCanvas) {
        if (!capturedOld_) {
            oldBg_ = doc.CanvasBackground();
            capturedOld_ = true;
        }
        doc.SetCanvasBackground(newBg_);
        return true;
    }
    auto& pages = doc.MutablePages();
    if (target_.pageIndex >= pages.size()) return false;
    if (!capturedOld_) {
        oldBg_ = pages[target_.pageIndex].background;
        capturedOld_ = true;
    }
    pages[target_.pageIndex].background = newBg_;
    doc.BumpRevision();
    return true;
}

bool SetBackgroundCommand::Undo(DocumentModel& doc) {
    if (!capturedOld_) return false;
    if (target_.isCanvas) {
        doc.SetCanvasBackground(oldBg_);
        return true;
    }
    auto& pages = doc.MutablePages();
    if (target_.pageIndex >= pages.size()) return false;
    pages[target_.pageIndex].background = oldBg_;
    doc.BumpRevision();
    return true;
}

// InsertPageCommand
InsertPageCommand::InsertPageCommand(std::size_t insertAtIndex, PageModel page)
    : insertAtIndex_(insertAtIndex), page_(std::move(page)) {}

std::string InsertPageCommand::Description() const {
    return "Insertar p\xC3\xA1gina";
}

bool InsertPageCommand::Execute(DocumentModel& doc) {
    auto& pages = doc.MutablePages();
    if (page_.pageId == 0) page_.pageId = doc.AllocatePageId();
    const std::size_t pos = (std::min)(insertAtIndex_, pages.size());
    insertAtIndex_ = pos;
    pages.insert(pages.begin() + static_cast<std::ptrdiff_t>(pos), page_);
    doc.RebuildLayout();
    return true;
}

bool InsertPageCommand::Undo(DocumentModel& doc) {
    auto& pages = doc.MutablePages();
    if (pages.size() <= 1 || insertAtIndex_ >= pages.size()) return false;
    pages.erase(pages.begin() + static_cast<std::ptrdiff_t>(insertAtIndex_));
    doc.RebuildLayout();
    return true;
}

// DeletePageCommand
DeletePageCommand::DeletePageCommand(std::size_t pageIndex)
    : pageIndex_(pageIndex) {}

std::string DeletePageCommand::Description() const {
    return "Eliminar p\xC3\xA1gina";
}

bool DeletePageCommand::Execute(DocumentModel& doc) {
    auto& pages = doc.MutablePages();
    if (pages.size() <= 1 || pageIndex_ >= pages.size()) return false;
    capturedPage_ = pages[pageIndex_];
    pages.erase(pages.begin() + static_cast<std::ptrdiff_t>(pageIndex_));
    doc.RebuildLayout();
    return true;
}

bool DeletePageCommand::Undo(DocumentModel& doc) {
    if (!capturedPage_.has_value()) return false;
    auto& pages = doc.MutablePages();
    const std::size_t pos = (std::min)(pageIndex_, pages.size());
    pages.insert(pages.begin() + static_cast<std::ptrdiff_t>(pos), *capturedPage_);
    doc.RebuildLayout();
    return true;
}

// ReorderPageCommand
ReorderPageCommand::ReorderPageCommand(std::size_t fromIndex, std::size_t toIndex)
    : fromIndex_(fromIndex), toIndex_(toIndex) {}

std::string ReorderPageCommand::Description() const {
    return "Reordenar p\xC3\xA1gina";
}

bool ReorderPageCommand::Execute(DocumentModel& doc) {
    auto& pages = doc.MutablePages();
    if (fromIndex_ >= pages.size() || toIndex_ >= pages.size() || fromIndex_ == toIndex_) return false;
    PageModel moved = std::move(pages[fromIndex_]);
    pages.erase(pages.begin() + static_cast<std::ptrdiff_t>(fromIndex_));
    pages.insert(pages.begin() + static_cast<std::ptrdiff_t>(toIndex_), std::move(moved));
    doc.RebuildLayout();
    return true;
}

bool ReorderPageCommand::Undo(DocumentModel& doc) {
    auto& pages = doc.MutablePages();
    if (toIndex_ >= pages.size() || fromIndex_ >= pages.size() || fromIndex_ == toIndex_) return false;
    PageModel moved = std::move(pages[toIndex_]);
    pages.erase(pages.begin() + static_cast<std::ptrdiff_t>(toIndex_));
    pages.insert(pages.begin() + static_cast<std::ptrdiff_t>(fromIndex_), std::move(moved));
    doc.RebuildLayout();
    return true;
}

// RotatePageCommand
RotatePageCommand::RotatePageCommand(std::size_t pageIndex, int deltaDegrees)
    : pageIndex_(pageIndex), deltaDeg_(deltaDegrees) {}

std::string RotatePageCommand::Description() const {
    return "Rotar p\xC3\xA1gina";
}

bool RotatePageCommand::Execute(DocumentModel& doc) {
    auto& pages = doc.MutablePages();
    if (pageIndex_ >= pages.size()) return false;
    if (!capturedOld_) {
        oldDeg_ = pages[pageIndex_].rotationDeg;
        capturedOld_ = true;
    }
    pages[pageIndex_].rotationDeg = NormalizeDegrees(oldDeg_ + deltaDeg_);
    doc.RebuildLayout();
    return true;
}

bool RotatePageCommand::Undo(DocumentModel& doc) {
    if (!capturedOld_) return false;
    auto& pages = doc.MutablePages();
    if (pageIndex_ >= pages.size()) return false;
    pages[pageIndex_].rotationDeg = oldDeg_;
    doc.RebuildLayout();
    return true;
}

// CropPageCommand
CropPageCommand::CropPageCommand(std::size_t pageIndex, NormalizedCropRect newCrop)
    : pageIndex_(pageIndex), newCrop_(newCrop) {}

std::string CropPageCommand::Description() const {
    return "Recortar p\xC3\xA1gina";
}

bool CropPageCommand::Execute(DocumentModel& doc) {
    auto& pages = doc.MutablePages();
    if (pageIndex_ >= pages.size()) return false;
    if (!capturedOld_) {
        oldCrop_ = pages[pageIndex_].crop;
        capturedOld_ = true;
    }
    NormalizedCropRect c = newCrop_;
    c.Clamp();
    pages[pageIndex_].crop = c;
    doc.RebuildLayout();
    return true;
}

bool CropPageCommand::Undo(DocumentModel& doc) {
    if (!capturedOld_) return false;
    auto& pages = doc.MutablePages();
    if (pageIndex_ >= pages.size()) return false;
    pages[pageIndex_].crop = oldCrop_;
    doc.RebuildLayout();
    return true;
}

} // namespace DocStudio
