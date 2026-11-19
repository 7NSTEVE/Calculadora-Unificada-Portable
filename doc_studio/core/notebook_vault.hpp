#pragma once

#include "doc_types.hpp"
#include "document_model.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace DocStudio {

// -------- Hierarchical Notebook Vault (Workspace -> Notebook -> Section -> Page) --------

struct VaultPageEntry {
    std::uint64_t id = 0;
    std::string titleUtf8;
    std::string cudocRelPathUtf8;
    std::uint64_t updatedEpochSec = 0;
};

struct VaultSection {
    std::uint64_t id = 0;
    std::string titleUtf8;
    std::uint32_t colorRgba = 0x3B82F6FFu;
    std::vector<VaultPageEntry> pages;
};

struct VaultNotebook {
    std::uint64_t id = 0;
    std::string titleUtf8;
    std::uint32_t coverColorRgba = 0x1E40AFu;
    std::vector<VaultSection> sections;
};

class NotebookVault {
public:
    std::uint64_t CreateNotebook(const std::string& titleUtf8, std::uint32_t coverRgba = 0x1D4ED8FFu);
    std::uint64_t CreateSection(std::uint64_t notebookId, const std::string& titleUtf8, std::uint32_t colorRgba = 0x2563EBFFu);
    std::uint64_t AddPageEntry(std::uint64_t notebookId, std::uint64_t sectionId, const std::string& titleUtf8, const std::string& cudocRelPathUtf8);

    const std::vector<VaultNotebook>& Notebooks() const noexcept { return notebooks_; }
    bool MovePageBetweenSections(std::uint64_t pageEntryId, std::uint64_t dstNotebookId, std::uint64_t dstSectionId);

    bool SaveToFile(const std::wstring& vaultJsonPath, std::string* outErr = nullptr) const;
    bool LoadFromFile(const std::wstring& vaultJsonPath, std::string* outErr = nullptr);

private:
    std::uint64_t nextId_ = 1;
    std::vector<VaultNotebook> notebooks_;
};

// -------- Crash-Safe Write-Ahead Log (.cudoc-wal) --------

struct WalRecord {
    std::string opTag;       // "add_obj", "erase_obj", "modify_obj", "set_title"
    std::string payloadJson; // compact JSON delta
};

class CudocWal {
public:
    static bool AppendRecord(const std::wstring& walPath, const WalRecord& rec, std::string* outErr = nullptr);
    static bool ReadValidRecords(const std::wstring& walPath, std::vector<WalRecord>& outRecords, std::string* outErr = nullptr);
    static bool Truncate(const std::wstring& walPath);

    // Loads base .cudoc (with .bak fallback) and replays any uncommitted WAL frames atomically
    static bool RecoverDocument(
        const std::wstring& cudocPath,
        const std::wstring& walPath,
        DocumentModel& outDoc,
        std::size_t* outReplayedCount = nullptr,
        std::string* outErr = nullptr);
};

// -------- Pure C++17 Unicode Inverted Full-Text Search Index --------

struct FtsPosting {
    std::string docKeyUtf8;
    std::size_t pageIndex = 0;
    std::uint64_t objectId = 0; // 0 = page/OCR text
    std::string sourceKindUtf8; // "pdf", "ocr", "textbox", "math", "calc"
    std::string snippetUtf8;
};

struct FtsHit {
    std::string docKeyUtf8;
    std::size_t pageIndex = 0;
    std::uint64_t objectId = 0;
    std::string sourceKindUtf8;
    std::string snippetUtf8;
    int score = 0;
};

class VaultFullTextIndex {
public:
    void RemoveDocument(const std::string& docKeyUtf8);
    void IndexDocument(const std::string& docKeyUtf8, const DocumentModel& doc);
    std::vector<FtsHit> Query(const std::string& queryUtf8, std::size_t maxResults = 64) const;

    std::size_t IndexedDocumentCount() const noexcept { return indexedDocs_.size(); }
    std::size_t VocabularySize() const noexcept { return postings_.size(); }

    bool SaveToFile(const std::wstring& indexPath, std::string* outErr = nullptr) const;
    bool LoadFromFile(const std::wstring& indexPath, std::string* outErr = nullptr);

private:
    std::unordered_map<std::string, std::vector<FtsPosting>> postings_;
    std::unordered_map<std::string, std::size_t> indexedDocs_;
};

// -------- Flashcards / Study Sets (SM-2 Spaced Repetition) --------

struct FlashcardItem {
    std::uint64_t id = 0;
    std::string questionUtf8;
    std::string answerUtf8;
    std::size_t sourcePageIndex = 0;
    DocRect sourceRect{};
    int repetitions = 0;
    int intervalDays = 0;
    double easeFactor = 2.5;
    std::uint64_t dueEpochSec = 0;
};

struct StudyDeck {
    std::string titleUtf8 = "Study Set CASIO";
    std::vector<FlashcardItem> cards;
};

class StudySetManager {
public:
    // SM-2 spaced repetition grade q in [0..5]
    static void ReviewCardSm2(FlashcardItem& card, int quality0to5, std::uint64_t nowEpochSec);

    // Harvests StudyTape cards, MathBlock derivations, CalcSessionCard steps, and Q:/A: text boxes
    static StudyDeck BuildDeckFromDocument(const DocumentModel& doc, const std::string& deckTitle = "");

    static std::size_t CountDueCards(const StudyDeck& deck, std::uint64_t nowEpochSec);
};

// -------- Tag Summary Board, Page Version Snapshots & Conflict Page Merge --------

struct TaggedItemHit {
    bool isCanvas = false;
    std::size_t pageIndex = 0;
    std::uint64_t objectId = 0;
    std::string tagUtf8;
    std::string summaryUtf8;
};

std::vector<TaggedItemHit> CollectTaggedItems(const DocumentModel& doc, const std::string& tagFilterUtf8 = "");

struct PageVersionSnapshot {
    std::uint64_t versionId = 0;
    std::size_t pageIndex = 0;
    std::uint64_t timestampSec = 0;
    std::string authorOrLabelUtf8;
    PageModel snapshotPage;
};

class PageVersionHistory {
public:
    std::uint64_t CaptureSnapshot(const DocumentModel& doc, std::size_t pageIndex, std::uint64_t timestampSec, const std::string& labelUtf8 = "Auto");
    std::vector<PageVersionSnapshot> SnapshotsForPage(std::size_t pageIndex) const;
    bool RestoreSnapshot(DocumentModel& doc, std::uint64_t versionId) const;

private:
    std::uint64_t nextVerId_ = 1;
    std::vector<PageVersionSnapshot> snapshots_;
};

struct ConflictMergeResult {
    bool ok = true;
    int mergedCleanPages = 0;
    int conflictPagesCreated = 0;
    std::vector<std::string> conflictDescriptions;
};

// OneNote-style side-by-side Conflict Page merge (preserves all divergent edits without overwriting)
ConflictMergeResult MergeConcurrentDocuments(
    const DocumentModel& baseDoc,
    const DocumentModel& localDoc,
    const DocumentModel& remoteDoc,
    DocumentModel& outMerged);

} // namespace DocStudio
