#include "notebook_vault.hpp"

#include "cudoc_storage.hpp"
#include "utf_conv.hpp"

#include <nlohmann/json.hpp>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#ifdef _WIN32
#include <io.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace DocStudio {

namespace {

std::vector<std::string> TokenizeNormalizedUtf8(const std::string& rawUtf8) {
    const std::string folded = FoldUtf8ForSearch(rawUtf8, false, true);
    std::vector<std::string> tokens;
    std::string cur;
    for (unsigned char ch : folded) {
        const bool isSep = (ch <= 0x20 || std::ispunct(ch) != 0) && ch < 0x80;
        if (isSep) {
            if (!cur.empty()) {
                tokens.push_back(std::move(cur));
                cur.clear();
            }
        } else {
            cur.push_back(static_cast<char>(ch));
        }
    }
    if (!cur.empty()) tokens.push_back(std::move(cur));
    return tokens;
}

std::uint32_t ComputeCrc32Bytes(const void* data, std::size_t len) noexcept {
    if (!data || len == 0) return 0;
    return static_cast<std::uint32_t>(
        ::crc32(0uL, reinterpret_cast<const Bytef*>(data), static_cast<uInt>(len)));
}

fs::path ToFsPath(const std::wstring& wpath) {
    return fs::path(WideToUtf8(wpath));
}

bool WriteTextFileUtf8(const std::wstring& wpath, const std::string& text, std::string* outErr) {
    try {
        const fs::path p = ToFsPath(wpath);
        if (!p.parent_path().empty()) fs::create_directories(p.parent_path());
        std::ofstream ofs(p, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            if (outErr) *outErr = "No se pudo abrir archivo para escritura.";
            return false;
        }
        ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
        ofs.flush();
        return static_cast<bool>(ofs);
    } catch (const std::exception& ex) {
        if (outErr) *outErr = ex.what();
        return false;
    }
}

bool ReadTextFileUtf8(const std::wstring& wpath, std::string& outText, std::string* outErr) {
    try {
        const fs::path p = ToFsPath(wpath);
        std::ifstream ifs(p, std::ios::binary | std::ios::ate);
        if (!ifs) {
            if (outErr) *outErr = "No se pudo leer archivo.";
            return false;
        }
        const auto sz = ifs.tellg();
        if (sz < 0) return false;
        outText.resize(static_cast<std::size_t>(sz));
        ifs.seekg(0, std::ios::beg);
        if (!outText.empty()) {
            ifs.read(outText.data(), static_cast<std::streamsize>(outText.size()));
        }
        return static_cast<bool>(ifs);
    } catch (const std::exception& ex) {
        if (outErr) *outErr = ex.what();
        return false;
    }
}

void WriteU32LE(std::uint8_t* dst, std::uint32_t v) noexcept {
    dst[0] = static_cast<std::uint8_t>(v & 0xFFu);
    dst[1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    dst[2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
    dst[3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
}

std::uint32_t ReadU32LE(const std::uint8_t* src) noexcept {
    return static_cast<std::uint32_t>(src[0]) |
           (static_cast<std::uint32_t>(src[1]) << 8) |
           (static_cast<std::uint32_t>(src[2]) << 16) |
           (static_cast<std::uint32_t>(src[3]) << 24);
}

} // namespace

// ---------------- NotebookVault ----------------

std::uint64_t NotebookVault::CreateNotebook(const std::string& titleUtf8, std::uint32_t coverRgba) {
    VaultNotebook nb;
    nb.id = nextId_++;
    nb.titleUtf8 = titleUtf8.empty() ? ("Cuaderno " + std::to_string(nb.id)) : titleUtf8;
    nb.coverColorRgba = coverRgba;
    notebooks_.push_back(std::move(nb));
    return notebooks_.back().id;
}

std::uint64_t NotebookVault::CreateSection(std::uint64_t notebookId, const std::string& titleUtf8, std::uint32_t colorRgba) {
    for (auto& nb : notebooks_) {
        if (nb.id == notebookId) {
            VaultSection sec;
            sec.id = nextId_++;
            sec.titleUtf8 = titleUtf8.empty() ? ("Secci\xC3\xB3n " + std::to_string(sec.id)) : titleUtf8;
            sec.colorRgba = colorRgba;
            nb.sections.push_back(std::move(sec));
            return nb.sections.back().id;
        }
    }
    return 0;
}

std::uint64_t NotebookVault::AddPageEntry(
    std::uint64_t notebookId,
    std::uint64_t sectionId,
    const std::string& titleUtf8,
    const std::string& cudocRelPathUtf8)
{
    for (auto& nb : notebooks_) {
        if (nb.id != notebookId) continue;
        for (auto& sec : nb.sections) {
            if (sec.id != sectionId) continue;
            VaultPageEntry pe;
            pe.id = nextId_++;
            pe.titleUtf8 = titleUtf8.empty() ? ("Nota " + std::to_string(pe.id)) : titleUtf8;
            pe.cudocRelPathUtf8 = cudocRelPathUtf8;
            pe.updatedEpochSec = static_cast<std::uint64_t>(std::time(nullptr));
            sec.pages.push_back(std::move(pe));
            return sec.pages.back().id;
        }
    }
    return 0;
}

bool NotebookVault::MovePageBetweenSections(
    std::uint64_t pageEntryId,
    std::uint64_t dstNotebookId,
    std::uint64_t dstSectionId)
{
    VaultPageEntry extracted{};
    bool found = false;
    for (auto& nb : notebooks_) {
        for (auto& sec : nb.sections) {
            for (std::size_t i = 0; i < sec.pages.size(); ++i) {
                if (sec.pages[i].id == pageEntryId) {
                    extracted = std::move(sec.pages[i]);
                    sec.pages.erase(sec.pages.begin() + static_cast<std::ptrdiff_t>(i));
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (found) break;
    }
    if (!found) return false;

    for (auto& nb : notebooks_) {
        if (nb.id != dstNotebookId) continue;
        for (auto& sec : nb.sections) {
            if (sec.id == dstSectionId) {
                sec.pages.push_back(std::move(extracted));
                return true;
            }
        }
    }
    return false;
}

bool NotebookVault::SaveToFile(const std::wstring& vaultJsonPath, std::string* outErr) const {
    try {
        json root;
        root["version"] = 1;
        root["nextId"] = nextId_;
        json nbs = json::array();
        for (const auto& nb : notebooks_) {
            json nj;
            nj["id"] = nb.id;
            nj["title"] = nb.titleUtf8;
            nj["coverColorRgba"] = nb.coverColorRgba;
            json secs = json::array();
            for (const auto& sec : nb.sections) {
                json sj;
                sj["id"] = sec.id;
                sj["title"] = sec.titleUtf8;
                sj["colorRgba"] = sec.colorRgba;
                json pgs = json::array();
                for (const auto& p : sec.pages) {
                    pgs.push_back({
                        {"id", p.id},
                        {"title", p.titleUtf8},
                        {"cudocPath", p.cudocRelPathUtf8},
                        {"updatedEpochSec", p.updatedEpochSec}
                    });
                }
                sj["pages"] = std::move(pgs);
                secs.push_back(std::move(sj));
            }
            nj["sections"] = std::move(secs);
            nbs.push_back(std::move(nj));
        }
        root["notebooks"] = std::move(nbs);
        return WriteTextFileUtf8(vaultJsonPath, root.dump(2), outErr);
    } catch (const std::exception& ex) {
        if (outErr) *outErr = ex.what();
        return false;
    }
}

bool NotebookVault::LoadFromFile(const std::wstring& vaultJsonPath, std::string* outErr) {
    std::string raw;
    if (!ReadTextFileUtf8(vaultJsonPath, raw, outErr)) return false;
    try {
        const json root = json::parse(raw);
        if (!root.is_object()) return false;
        notebooks_.clear();
        nextId_ = root.value("nextId", std::uint64_t{ 1 });
        std::uint64_t maxSeen = nextId_;
        if (root.contains("notebooks") && root["notebooks"].is_array()) {
            for (const auto& nj : root["notebooks"]) {
                VaultNotebook nb;
                nb.id = nj.value("id", std::uint64_t{ 0 });
                nb.titleUtf8 = nj.value("title", "");
                nb.coverColorRgba = nj.value("coverColorRgba", 0x1D4ED8FFu);
                if (nb.id >= maxSeen) maxSeen = nb.id + 1;
                if (nj.contains("sections") && nj["sections"].is_array()) {
                    for (const auto& sj : nj["sections"]) {
                        VaultSection sec;
                        sec.id = sj.value("id", std::uint64_t{ 0 });
                        sec.titleUtf8 = sj.value("title", "");
                        sec.colorRgba = sj.value("colorRgba", 0x2563EBFFu);
                        if (sec.id >= maxSeen) maxSeen = sec.id + 1;
                        if (sj.contains("pages") && sj["pages"].is_array()) {
                            for (const auto& pj : sj["pages"]) {
                                VaultPageEntry pe;
                                pe.id = pj.value("id", std::uint64_t{ 0 });
                                pe.titleUtf8 = pj.value("title", "");
                                pe.cudocRelPathUtf8 = pj.value("cudocPath", "");
                                pe.updatedEpochSec = pj.value("updatedEpochSec", std::uint64_t{ 0 });
                                if (pe.id >= maxSeen) maxSeen = pe.id + 1;
                                sec.pages.push_back(std::move(pe));
                            }
                        }
                        nb.sections.push_back(std::move(sec));
                    }
                }
                notebooks_.push_back(std::move(nb));
            }
        }
        nextId_ = maxSeen;
        return true;
    } catch (const std::exception& ex) {
        if (outErr) *outErr = ex.what();
        return false;
    }
}

// ---------------- CudocWal ----------------

bool CudocWal::AppendRecord(const std::wstring& walPath, const WalRecord& rec, std::string* outErr) {
    try {
        const fs::path p = ToFsPath(walPath);
        if (!p.parent_path().empty()) fs::create_directories(p.parent_path());

        json j;
        j["op"] = rec.opTag;
        j["payload"] = rec.payloadJson;
        const std::string body = j.dump();

        std::uint8_t hdr[12];
        std::memcpy(hdr, "CWAL", 4);
        WriteU32LE(hdr + 4, static_cast<std::uint32_t>(body.size()));
        WriteU32LE(hdr + 8, ComputeCrc32Bytes(body.data(), body.size()));

        std::ofstream ofs(p, std::ios::binary | std::ios::app);
        if (!ofs) {
            if (outErr) *outErr = "No se pudo abrir .cudoc-wal.";
            return false;
        }
        ofs.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
        ofs.write(body.data(), static_cast<std::streamsize>(body.size()));
        ofs.flush();
        return static_cast<bool>(ofs);
    } catch (const std::exception& ex) {
        if (outErr) *outErr = ex.what();
        return false;
    }
}

bool CudocWal::ReadValidRecords(const std::wstring& walPath, std::vector<WalRecord>& outRecords, std::string* outErr) {
    outRecords.clear();
    std::string raw;
    if (!ReadTextFileUtf8(walPath, raw, outErr)) return false;

    std::size_t off = 0;
    while (off + 12 <= raw.size()) {
        if (std::memcmp(raw.data() + off, "CWAL", 4) != 0) break;
        const std::uint32_t len = ReadU32LE(reinterpret_cast<const std::uint8_t*>(raw.data() + off + 4));
        const std::uint32_t crc = ReadU32LE(reinterpret_cast<const std::uint8_t*>(raw.data() + off + 8));
        if (len > 32u * 1024u * 1024u || off + 12 + len > raw.size()) {
            break; // Torn tail write ignored safely
        }
        const std::uint32_t actualCrc = ComputeCrc32Bytes(raw.data() + off + 12, len);
        if (actualCrc != crc) {
            break; // Corrupt tail record stopped safely
        }
        try {
            const json j = json::parse(raw.data() + off + 12, raw.data() + off + 12 + len);
            if (j.is_object()) {
                WalRecord rec;
                rec.opTag = j.value("op", "");
                rec.payloadJson = j.value("payload", "");
                outRecords.push_back(std::move(rec));
            }
        } catch (...) {
            break;
        }
        off += 12 + len;
    }
    return true;
}

bool CudocWal::Truncate(const std::wstring& walPath) {
    std::error_code ec;
    fs::remove(ToFsPath(walPath), ec);
    return true;
}

bool CudocWal::RecoverDocument(
    const std::wstring& cudocPath,
    const std::wstring& walPath,
    DocumentModel& outDoc,
    std::size_t* outReplayedCount,
    std::string* outErr)
{
    if (outReplayedCount) *outReplayedCount = 0;
    bool usedBak = false;
    if (!CudocStorage::LoadFromFileWithRecovery(cudocPath, outDoc, &usedBak, outErr)) {
        return false;
    }

    std::vector<WalRecord> recs;
    if (!fs::exists(ToFsPath(walPath))) return true;
    if (!ReadValidRecords(walPath, recs, nullptr)) return true;

    std::size_t replayed = 0;
    for (const auto& rec : recs) {
        try {
            const json pj = json::parse(rec.payloadJson);
            if (rec.opTag == "set_title") {
                outDoc.SetTitle(pj.value("title", outDoc.Title()));
                ++replayed;
            } else if (rec.opTag == "add_obj") {
                const bool isCanvas = pj.value("isCanvas", false);
                const std::size_t pageIdx = pj.value("pageIndex", std::size_t{ 0 });
                AnnotationObject obj;
                obj.id = pj.value("id", std::uint64_t{ 0 });
                obj.kind = static_cast<ObjectKind>(pj.value("kind", 2));
                obj.layerId = pj.value("layerId", std::uint64_t{ 1 });
                obj.textUtf8 = pj.value("text", "");
                obj.formulaExprUtf8 = pj.value("formulaExpr", "");
                obj.evaluatedResultUtf8 = pj.value("evaluatedResult", "");
                obj.bounds = {
                    pj.value("x", 0.0),
                    pj.value("y", 0.0),
                    pj.value("w", 120.0),
                    pj.value("h", 40.0)
                };
                if (pj.contains("inkPoints") && pj["inkPoints"].is_array()) {
                    for (const auto& ipj : pj["inkPoints"]) {
                        if (ipj.is_array() && ipj.size() >= 2) {
                            InkPoint ip;
                            ip.x = ipj[0].get<double>();
                            ip.y = ipj[1].get<double>();
                            ip.pressure = (ipj.size() >= 3) ? ipj[2].get<float>() : 0.5f;
                            obj.inkPoints.push_back(ip);
                        }
                    }
                }
                if (isCanvas) {
                    outDoc.AddCanvasObject(std::move(obj));
                    ++replayed;
                } else if (outDoc.AddPageObject(pageIdx, std::move(obj))) {
                    ++replayed;
                }
            } else if (rec.opTag == "erase_obj") {
                const bool isCanvas = pj.value("isCanvas", false);
                const std::size_t pageIdx = pj.value("pageIndex", std::size_t{ 0 });
                const std::uint64_t oid = pj.value("id", std::uint64_t{ 0 });
                if (isCanvas) {
                    if (outDoc.RemoveCanvasObject(oid)) ++replayed;
                } else {
                    if (outDoc.RemovePageObject(pageIdx, oid)) ++replayed;
                }
            }
        } catch (...) {
            // Skip malformed single delta gracefully
        }
    }
    if (outReplayedCount) *outReplayedCount = replayed;
    return true;
}

// ---------------- VaultFullTextIndex ----------------

void VaultFullTextIndex::RemoveDocument(const std::string& docKeyUtf8) {
    indexedDocs_.erase(docKeyUtf8);
    for (auto it = postings_.begin(); it != postings_.end();) {
        auto& vec = it->second;
        vec.erase(std::remove_if(vec.begin(), vec.end(), [&](const FtsPosting& p) {
            return p.docKeyUtf8 == docKeyUtf8;
        }), vec.end());
        if (vec.empty()) {
            it = postings_.erase(it);
        } else {
            ++it;
        }
    }
}

void VaultFullTextIndex::IndexDocument(const std::string& docKeyUtf8, const DocumentModel& doc) {
    RemoveDocument(docKeyUtf8);
    indexedDocs_[docKeyUtf8] = doc.Pages().size();

    auto indexString = [&](std::size_t pageIdx, std::uint64_t objId, const std::string& kind, const std::string& text) {
        if (text.empty()) return;
        const std::vector<std::string> tokens = TokenizeNormalizedUtf8(text);
        std::unordered_set<std::string> seenInItem;
        const std::string snippet = (text.size() <= 140) ? text : (text.substr(0, 140) + "...");
        for (const auto& tok : tokens) {
            if (tok.empty() || !seenInItem.insert(tok).second) continue;
            postings_[tok].push_back(FtsPosting{ docKeyUtf8, pageIdx, objId, kind, snippet });
        }
    };

    const auto& pages = doc.Pages();
    for (std::size_t p = 0; p < pages.size(); ++p) {
        const auto& pg = pages[p];
        indexString(p, 0, "pdf", pg.extractedTextUtf8);
        indexString(p, 0, "ocr", pg.ocrTextUtf8);
        for (const auto& obj : pg.objects) {
            if (!obj.textUtf8.empty()) indexString(p, obj.id, "textbox", obj.textUtf8);
            if (!obj.formulaExprUtf8.empty() || !obj.evaluatedResultUtf8.empty()) {
                indexString(p, obj.id, "math", obj.formulaExprUtf8 + " = " + obj.evaluatedResultUtf8);
            }
            if (!obj.calcKeySeqUtf8.empty() || !obj.calcModelIdUtf8.empty()) {
                indexString(p, obj.id, "calc", obj.calcModelIdUtf8 + " " + obj.calcKeySeqUtf8 + " " + obj.evaluatedResultUtf8);
            }
        }
    }

    for (const auto& obj : doc.CanvasObjects()) {
        if (!obj.textUtf8.empty()) indexString(0, obj.id, "textbox", obj.textUtf8);
        if (!obj.formulaExprUtf8.empty() || !obj.evaluatedResultUtf8.empty()) {
            indexString(0, obj.id, "math", obj.formulaExprUtf8 + " = " + obj.evaluatedResultUtf8);
        }
        if (!obj.calcKeySeqUtf8.empty() || !obj.calcModelIdUtf8.empty()) {
            indexString(0, obj.id, "calc", obj.calcModelIdUtf8 + " " + obj.calcKeySeqUtf8 + " " + obj.evaluatedResultUtf8);
        }
    }
}

std::vector<FtsHit> VaultFullTextIndex::Query(const std::string& queryUtf8, std::size_t maxResults) const {
    std::vector<FtsHit> hits;
    const std::vector<std::string> qTokens = TokenizeNormalizedUtf8(queryUtf8);
    if (qTokens.empty()) return hits;

    struct AggKey {
        std::string key; // docKey + page + objId
        FtsHit hit;
        int matchedTerms = 0;
    };
    std::unordered_map<std::string, AggKey> agg;

    for (const auto& qt : qTokens) {
        for (const auto& kv : postings_) {
            const bool exact = (kv.first == qt);
            const bool prefix = (!exact && qt.size() >= 3 && kv.first.find(qt) == 0);
            if (!exact && !prefix) continue;

            for (const auto& post : kv.second) {
                const std::string k = post.docKeyUtf8 + "#" + std::to_string(post.pageIndex) + "#" + std::to_string(post.objectId);
                auto& entry = agg[k];
                if (entry.matchedTerms == 0) {
                    entry.key = k;
                    entry.hit = FtsHit{ post.docKeyUtf8, post.pageIndex, post.objectId, post.sourceKindUtf8, post.snippetUtf8, 0 };
                }
                entry.matchedTerms += 1;
                entry.hit.score += exact ? 10 : 4;
            }
        }
    }

    hits.reserve(agg.size());
    for (auto& kv : agg) {
        if (kv.second.matchedTerms >= static_cast<int>(qTokens.size())) {
            hits.push_back(std::move(kv.second.hit));
        }
    }
    std::sort(hits.begin(), hits.end(), [](const FtsHit& a, const FtsHit& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.docKeyUtf8 != b.docKeyUtf8) return a.docKeyUtf8 < b.docKeyUtf8;
        return a.pageIndex < b.pageIndex;
    });
    if (hits.size() > maxResults) hits.resize(maxResults);
    return hits;
}

bool VaultFullTextIndex::SaveToFile(const std::wstring& indexPath, std::string* outErr) const {
    try {
        json root;
        root["docs"] = indexedDocs_;
        json postJson = json::object();
        for (const auto& kv : postings_) {
            json arr = json::array();
            for (const auto& p : kv.second) {
                arr.push_back({
                    {"d", p.docKeyUtf8},
                    {"p", p.pageIndex},
                    {"o", p.objectId},
                    {"k", p.sourceKindUtf8},
                    {"s", p.snippetUtf8}
                });
            }
            postJson[kv.first] = std::move(arr);
        }
        root["postings"] = std::move(postJson);
        return WriteTextFileUtf8(indexPath, root.dump(), outErr);
    } catch (const std::exception& ex) {
        if (outErr) *outErr = ex.what();
        return false;
    }
}

bool VaultFullTextIndex::LoadFromFile(const std::wstring& indexPath, std::string* outErr) {
    std::string raw;
    if (!ReadTextFileUtf8(indexPath, raw, outErr)) return false;
    try {
        const json root = json::parse(raw);
        if (!root.is_object()) return false;
        indexedDocs_.clear();
        postings_.clear();
        if (root.contains("docs") && root["docs"].is_object()) {
            for (auto it = root["docs"].begin(); it != root["docs"].end(); ++it) {
                indexedDocs_[it.key()] = it.value().get<std::size_t>();
            }
        }
        if (root.contains("postings") && root["postings"].is_object()) {
            for (auto it = root["postings"].begin(); it != root["postings"].end(); ++it) {
                auto& vec = postings_[it.key()];
                for (const auto& pj : it.value()) {
                    vec.push_back(FtsPosting{
                        pj.value("d", ""),
                        pj.value("p", std::size_t{ 0 }),
                        pj.value("o", std::uint64_t{ 0 }),
                        pj.value("k", ""),
                        pj.value("s", "")
                    });
                }
            }
        }
        return true;
    } catch (const std::exception& ex) {
        if (outErr) *outErr = ex.what();
        return false;
    }
}

// ---------------- StudySetManager (SM-2) ----------------

void StudySetManager::ReviewCardSm2(FlashcardItem& card, int quality0to5, std::uint64_t nowEpochSec) {
    const int q = std::clamp(quality0to5, 0, 5);
    if (q < 3) {
        card.repetitions = 0;
        card.intervalDays = 1;
    } else {
        if (card.repetitions == 0) {
            card.intervalDays = 1;
        } else if (card.repetitions == 1) {
            card.intervalDays = 6;
        } else {
            card.intervalDays = (std::max)(1, static_cast<int>(std::lround(static_cast<double>(card.intervalDays) * card.easeFactor)));
        }
        card.repetitions += 1;
    }

    const double dq = static_cast<double>(5 - q);
    card.easeFactor = card.easeFactor + (0.1 - dq * (0.08 + dq * 0.02));
    if (card.easeFactor < 1.3) card.easeFactor = 1.3;
    card.dueEpochSec = nowEpochSec + static_cast<std::uint64_t>(card.intervalDays) * 86400ull;
}

StudyDeck StudySetManager::BuildDeckFromDocument(const DocumentModel& doc, const std::string& deckTitle) {
    StudyDeck deck;
    deck.titleUtf8 = deckTitle.empty() ? ("Study Set - " + doc.Title()) : deckTitle;
    std::uint64_t nextCid = 1;

    auto harvestObj = [&](std::size_t pgIdx, const AnnotationObject& obj) {
        if (obj.kind == ObjectKind::StudyTape) {
            FlashcardItem item;
            item.id = nextCid++;
            item.questionUtf8 = obj.captionUtf8.empty() ? "Concepto oculto en Study Tape" : obj.captionUtf8;
            item.answerUtf8 = obj.textUtf8.empty() ? "(Respuesta visual en p\xC3\xA1gina)" : obj.textUtf8;
            item.sourcePageIndex = pgIdx;
            item.sourceRect = obj.bounds;
            deck.cards.push_back(std::move(item));
        } else if (obj.kind == ObjectKind::MathBlock && !obj.formulaExprUtf8.empty()) {
            FlashcardItem item;
            item.id = nextCid++;
            item.questionUtf8 = "Evaluar f\xC3\xB3rmula: " + obj.formulaExprUtf8;
            item.answerUtf8 = obj.evaluatedResultUtf8.empty() ? obj.textUtf8 : obj.evaluatedResultUtf8;
            item.sourcePageIndex = pgIdx;
            item.sourceRect = obj.bounds;
            deck.cards.push_back(std::move(item));
        } else if (obj.kind == ObjectKind::CalcSessionCard && !obj.calcKeySeqUtf8.empty()) {
            FlashcardItem item;
            item.id = nextCid++;
            item.questionUtf8 = "[" + obj.calcModelIdUtf8 + "] Secuencia: " + obj.calcKeySeqUtf8;
            item.answerUtf8 = !obj.evaluatedResultUtf8.empty() ? obj.evaluatedResultUtf8 : obj.calcLatexUtf8;
            item.sourcePageIndex = pgIdx;
            item.sourceRect = obj.bounds;
            deck.cards.push_back(std::move(item));
        }
    };

    for (std::size_t p = 0; p < doc.Pages().size(); ++p) {
        for (const auto& obj : doc.Pages()[p].objects) {
            harvestObj(p, obj);
        }
    }
    for (const auto& obj : doc.CanvasObjects()) {
        harvestObj(0, obj);
    }
    return deck;
}

std::size_t StudySetManager::CountDueCards(const StudyDeck& deck, std::uint64_t nowEpochSec) {
    std::size_t n = 0;
    for (const auto& c : deck.cards) {
        if (c.dueEpochSec <= nowEpochSec) ++n;
    }
    return n;
}

// ---------------- Tags, Version Snapshots & Conflict Merge ----------------

std::vector<TaggedItemHit> CollectTaggedItems(const DocumentModel& doc, const std::string& tagFilterUtf8) {
    std::vector<TaggedItemHit> hits;
    const std::string foldedFilter = FoldUtf8ForSearch(tagFilterUtf8, false, true);
    auto inspectObj = [&](bool isCanvas, std::size_t pgIdx, const AnnotationObject& obj) {
        for (const auto& t : obj.tags) {
            if (!foldedFilter.empty() && FoldUtf8ForSearch(t, false, true).find(foldedFilter) == std::string::npos) {
                continue;
            }
            TaggedItemHit h{};
            h.isCanvas = isCanvas;
            h.pageIndex = pgIdx;
            h.objectId = obj.id;
            h.tagUtf8 = t;
            if (!obj.formulaExprUtf8.empty()) h.summaryUtf8 = obj.formulaExprUtf8;
            else if (!obj.textUtf8.empty()) h.summaryUtf8 = obj.textUtf8.substr(0, 60);
            else if (!obj.captionUtf8.empty()) h.summaryUtf8 = obj.captionUtf8;
            else h.summaryUtf8 = "Objeto #" + std::to_string(obj.id);
            hits.push_back(std::move(h));
        }
    };
    for (std::size_t p = 0; p < doc.Pages().size(); ++p) {
        for (const auto& obj : doc.Pages()[p].objects) inspectObj(false, p, obj);
    }
    for (const auto& obj : doc.CanvasObjects()) inspectObj(true, 0, obj);
    return hits;
}

std::uint64_t PageVersionHistory::CaptureSnapshot(
    const DocumentModel& doc,
    std::size_t pageIndex,
    std::uint64_t timestampSec,
    const std::string& labelUtf8)
{
    if (pageIndex >= doc.Pages().size()) return 0;
    PageVersionSnapshot snap{};
    snap.versionId = nextVerId_++;
    snap.pageIndex = pageIndex;
    snap.timestampSec = timestampSec;
    snap.authorOrLabelUtf8 = labelUtf8;
    snap.snapshotPage = doc.Pages()[pageIndex];
    snapshots_.push_back(std::move(snap));
    return snapshots_.back().versionId;
}

std::vector<PageVersionSnapshot> PageVersionHistory::SnapshotsForPage(std::size_t pageIndex) const {
    std::vector<PageVersionSnapshot> out;
    for (const auto& s : snapshots_) {
        if (s.pageIndex == pageIndex) out.push_back(s);
    }
    return out;
}

bool PageVersionHistory::RestoreSnapshot(DocumentModel& doc, std::uint64_t versionId) const {
    for (const auto& s : snapshots_) {
        if (s.versionId == versionId && s.pageIndex < doc.Pages().size()) {
            doc.MutablePages()[s.pageIndex] = s.snapshotPage;
            doc.RebuildLayout();
            doc.SetDirty(true);
            doc.BumpRevision();
            return true;
        }
    }
    return false;
}

namespace {
std::string PageSignature(const PageModel& pg) {
    std::ostringstream oss;
    oss << pg.widthPt << "x" << pg.heightPt << "|rot=" << pg.rotationDeg
        << "|bg=" << static_cast<int>(pg.background) << "|n=" << pg.objects.size();
    for (const auto& o : pg.objects) {
        oss << "|" << o.id << ":" << static_cast<int>(o.kind) << ":" << o.bounds.x << "," << o.bounds.y
            << ":" << o.textUtf8 << ":" << o.formulaExprUtf8 << ":" << o.evaluatedResultUtf8;
    }
    return oss.str();
}
} // namespace

ConflictMergeResult MergeConcurrentDocuments(
    const DocumentModel& baseDoc,
    const DocumentModel& localDoc,
    const DocumentModel& remoteDoc,
    DocumentModel& outMerged)
{
    ConflictMergeResult res{};
    std::vector<std::uint8_t> tmpBytes;
    if (!CudocStorage::SerializeToBytes(localDoc, tmpBytes) ||
        !CudocStorage::DeserializeFromBytes(tmpBytes.data(), tmpBytes.size(), outMerged)) {
        outMerged.ResetToBlank(localDoc.Mode());
        outMerged.SetTitle(localDoc.Title());
    }
    outMerged.RestoreCounters(
        (std::max)(localDoc.NextObjectId(), remoteDoc.NextObjectId()),
        (std::max)(localDoc.NextPageId(), remoteDoc.NextPageId()),
        (std::max)(localDoc.NextZOrder(), remoteDoc.NextZOrder()),
        (std::max)(localDoc.NextAssetSeq(), remoteDoc.NextAssetSeq()));
    outMerged.MutablePages().clear();

    const std::size_t maxPages = (std::max)(baseDoc.Pages().size(),
        (std::max)(localDoc.Pages().size(), remoteDoc.Pages().size()));

    for (std::size_t p = 0; p < maxPages; ++p) {
        const bool hasBase = p < baseDoc.Pages().size();
        const bool hasLoc = p < localDoc.Pages().size();
        const bool hasRem = p < remoteDoc.Pages().size();

        if (hasLoc && !hasRem) {
            outMerged.MutablePages().push_back(localDoc.Pages()[p]);
            ++res.mergedCleanPages;
            continue;
        }
        if (!hasLoc && hasRem) {
            outMerged.MutablePages().push_back(remoteDoc.Pages()[p]);
            ++res.mergedCleanPages;
            continue;
        }
        if (!hasLoc && !hasRem) continue;

        const std::string sigLoc = PageSignature(localDoc.Pages()[p]);
        const std::string sigRem = PageSignature(remoteDoc.Pages()[p]);
        if (sigLoc == sigRem) {
            outMerged.MutablePages().push_back(localDoc.Pages()[p]);
            ++res.mergedCleanPages;
            continue;
        }
        const std::string sigBase = hasBase ? PageSignature(baseDoc.Pages()[p]) : "";
        if (hasBase && sigLoc == sigBase) {
            // Only remote modified
            outMerged.MutablePages().push_back(remoteDoc.Pages()[p]);
            ++res.mergedCleanPages;
            continue;
        }
        if (hasBase && sigRem == sigBase) {
            // Only local modified
            outMerged.MutablePages().push_back(localDoc.Pages()[p]);
            ++res.mergedCleanPages;
            continue;
        }

        // True concurrent offline edit conflict -> preserve local page AND insert side-by-side Conflict Page!
        outMerged.MutablePages().push_back(localDoc.Pages()[p]);
        PageModel conflictPg = remoteDoc.Pages()[p];
        conflictPg.pageId = outMerged.AllocatePageId();
        AnnotationObject banner;
        banner.id = outMerged.AllocateObjectId();
        banner.zOrder = outMerged.AllocateZOrder();
        banner.kind = ObjectKind::TextBox;
        banner.bounds = { 24.0, 12.0, 520.0, 32.0 };
        banner.fillColor = DocColor::RGBA(254, 242, 242, 250);
        banner.strokeColor = DocColor::RGBA(185, 28, 28, 255);
        banner.textUtf8 = "[PAGINA DE CONFLICTO] Edicion concurrente preservada de Pagina " + std::to_string(p + 1);
        conflictPg.objects.insert(conflictPg.objects.begin(), std::move(banner));
        outMerged.MutablePages().push_back(std::move(conflictPg));

        ++res.conflictPagesCreated;
        res.conflictDescriptions.push_back("Conflicto en pagina " + std::to_string(p + 1) + ": copia remota preservada lado a lado");
    }

    if (outMerged.Pages().empty()) {
        PageModel blank;
        blank.pageId = outMerged.AllocatePageId();
        outMerged.MutablePages().push_back(std::move(blank));
    }
    outMerged.RebuildLayout();
    outMerged.BumpRevision();
    return res;
}

} // namespace DocStudio
