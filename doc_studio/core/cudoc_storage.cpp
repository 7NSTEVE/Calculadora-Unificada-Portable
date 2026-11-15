#include "cudoc_storage.hpp"
#include "utf_conv.hpp"
#include "zip_container.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>

namespace DocStudio {

namespace {

using json = nlohmann::json;

const char* ModeToString(DocumentMode m) noexcept {
    return (m == DocumentMode::InfiniteCanvas) ? "infinite_canvas" : "paginated_pdf";
}

bool StringToMode(const std::string& s, DocumentMode& out) noexcept {
    if (s == "paginated_pdf") { out = DocumentMode::PaginatedPdf; return true; }
    if (s == "infinite_canvas") { out = DocumentMode::InfiniteCanvas; return true; }
    return false;
}

const char* PatternToString(PaperPattern p) noexcept {
    switch (p) {
    case PaperPattern::Plain:      return "plain";
    case PaperPattern::Ruled:      return "ruled";
    case PaperPattern::Grid:       return "grid";
    case PaperPattern::Dots:       return "dots";
    case PaperPattern::Millimeter: return "millimeter";
    default:                       return "plain";
    }
}

PaperPattern StringToPattern(const std::string& s) noexcept {
    if (s == "ruled") return PaperPattern::Ruled;
    if (s == "grid") return PaperPattern::Grid;
    if (s == "dots") return PaperPattern::Dots;
    if (s == "millimeter") return PaperPattern::Millimeter;
    return PaperPattern::Plain;
}

const char* ObjectKindToString(ObjectKind k) noexcept {
    switch (k) {
    case ObjectKind::InkStroke: return "ink";
    case ObjectKind::Shape:     return "shape";
    case ObjectKind::TextBox:   return "textbox";
    case ObjectKind::Image:     return "image";
    case ObjectKind::PdfCard:   return "pdfcard";
    default:                    return "ink";
    }
}

bool StringToObjectKind(const std::string& s, ObjectKind& out) noexcept {
    if (s == "ink") { out = ObjectKind::InkStroke; return true; }
    if (s == "shape") { out = ObjectKind::Shape; return true; }
    if (s == "textbox") { out = ObjectKind::TextBox; return true; }
    if (s == "image") { out = ObjectKind::Image; return true; }
    if (s == "pdfcard") { out = ObjectKind::PdfCard; return true; }
    return false;
}

const char* ShapeKindToString(ShapeKind s) noexcept {
    switch (s) {
    case ShapeKind::Line:           return "line";
    case ShapeKind::Arrow:          return "arrow";
    case ShapeKind::Rectangle:      return "rectangle";
    case ShapeKind::Ellipse:        return "ellipse";
    case ShapeKind::SignatureStamp: return "signature_stamp";
    default:                        return "rectangle";
    }
}

ShapeKind StringToShapeKind(const std::string& s) noexcept {
    if (s == "line") return ShapeKind::Line;
    if (s == "arrow") return ShapeKind::Arrow;
    if (s == "ellipse") return ShapeKind::Ellipse;
    if (s == "signature_stamp") return ShapeKind::SignatureStamp;
    return ShapeKind::Rectangle;
}

json RectToJson(const DocRect& r) {
    return json{ {"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h} };
}

DocRect JsonToRect(const json& j) {
    DocRect r{};
    if (j.is_object()) {
        r.x = j.value("x", 0.0);
        r.y = j.value("y", 0.0);
        r.w = j.value("w", 0.0);
        r.h = j.value("h", 0.0);
    }
    return r;
}

json ObjectToJson(
    const AnnotationObject& obj,
    const std::string& scopeTag,
    std::size_t idx,
    ZipArchive& zip)
{
    json j;
    j["id"] = obj.id;
    j["zOrder"] = obj.zOrder;
    j["kind"] = ObjectKindToString(obj.kind);
    j["bounds"] = RectToJson(obj.bounds);
    j["strokeColor"] = obj.strokeColor.PackRgba();
    j["fillColor"] = obj.fillColor.PackRgba();
    j["strokeWidth"] = obj.strokeWidth;
    j["isHighlighter"] = obj.isHighlighter;

    if (obj.kind == ObjectKind::InkStroke) {
        json pts = json::array();
        for (const auto& pt : obj.inkPoints) {
            pts.push_back({ pt.x, pt.y, pt.pressure });
        }
        j["inkPoints"] = std::move(pts);
    } else if (obj.kind == ObjectKind::Shape) {
        j["shapeKind"] = ShapeKindToString(obj.shapeKind);
        j["shapeStart"] = { obj.shapeStart.x, obj.shapeStart.y };
        j["shapeEnd"] = { obj.shapeEnd.x, obj.shapeEnd.y };
        j["text"] = obj.textUtf8;
    } else if (obj.kind == ObjectKind::TextBox) {
        j["text"] = obj.textUtf8;
        j["fontFamily"] = obj.fontFamilyUtf8;
        j["fontSizePt"] = obj.fontSizePt;
        j["bold"] = obj.bold;
        j["italic"] = obj.italic;
    } else if (obj.kind == ObjectKind::Image) {
        j["assetKey"] = obj.assetKey;
        j["imagePixelW"] = obj.imagePixelW;
        j["imagePixelH"] = obj.imagePixelH;
        j["caption"] = obj.captionUtf8;
        if (!obj.pngBytes.empty()) {
            const std::string inlinePath = "assets/_inline/" + scopeTag + "_" + std::to_string(idx) + ".bin";
            zip.SetEntry(inlinePath, obj.pngBytes);
            j["inlineAssetPath"] = inlinePath;
        }
    } else if (obj.kind == ObjectKind::PdfCard) {
        j["pdfSourcePath"] = obj.pdfSourcePathUtf8;
        j["pdfSourcePageIndex0"] = obj.pdfSourcePageIndex0;
        j["caption"] = obj.captionUtf8;
    }
    return j;
}

bool JsonToObject(const json& j, const ZipArchive& zip, AnnotationObject& out, std::string* outErr) {
    if (!j.is_object()) {
        if (outErr) *outErr = "Objeto con formato JSON no v\xC3\xA1lido.";
        return false;
    }
    const std::string kindStr = j.value("kind", "");
    if (!StringToObjectKind(kindStr, out.kind)) {
        if (outErr) *outErr = "Tipo de objeto desconocido en .cudoc: " + kindStr;
        return false;
    }
    out.id = j.value("id", std::uint64_t{ 0 });
    out.zOrder = j.value("zOrder", std::uint64_t{ 0 });
    out.bounds = JsonToRect(j.value("bounds", json::object()));
    out.strokeColor = DocColor::UnpackRgba(j.value("strokeColor", std::uint32_t{ 0x1A73E8FFu }));
    out.fillColor = DocColor::UnpackRgba(j.value("fillColor", std::uint32_t{ 0 }));
    out.strokeWidth = j.value("strokeWidth", 2.5);
    out.isHighlighter = j.value("isHighlighter", false);

    if (out.kind == ObjectKind::InkStroke && j.contains("inkPoints") && j["inkPoints"].is_array()) {
        for (const auto& pt : j["inkPoints"]) {
            if (pt.is_array() && pt.size() >= 2) {
                InkPoint ip{};
                ip.x = pt[0].get<double>();
                ip.y = pt[1].get<double>();
                ip.pressure = (pt.size() >= 3) ? pt[2].get<float>() : 0.5f;
                out.inkPoints.push_back(ip);
            }
        }
    } else if (out.kind == ObjectKind::Shape) {
        out.shapeKind = StringToShapeKind(j.value("shapeKind", "rectangle"));
        if (j.contains("shapeStart") && j["shapeStart"].is_array() && j["shapeStart"].size() >= 2) {
            out.shapeStart.x = j["shapeStart"][0].get<double>();
            out.shapeStart.y = j["shapeStart"][1].get<double>();
        }
        if (j.contains("shapeEnd") && j["shapeEnd"].is_array() && j["shapeEnd"].size() >= 2) {
            out.shapeEnd.x = j["shapeEnd"][0].get<double>();
            out.shapeEnd.y = j["shapeEnd"][1].get<double>();
        }
        out.textUtf8 = j.value("text", "");
    } else if (out.kind == ObjectKind::TextBox) {
        out.textUtf8 = j.value("text", "");
        out.fontFamilyUtf8 = j.value("fontFamily", "Segoe UI");
        out.fontSizePt = j.value("fontSizePt", 14.0);
        out.bold = j.value("bold", false);
        out.italic = j.value("italic", false);
    } else if (out.kind == ObjectKind::Image) {
        out.assetKey = j.value("assetKey", "");
        out.imagePixelW = j.value("imagePixelW", 0);
        out.imagePixelH = j.value("imagePixelH", 0);
        out.captionUtf8 = j.value("caption", "");
        const std::string inlinePath = j.value("inlineAssetPath", "");
        if (!inlinePath.empty()) {
            const auto* entry = zip.FindEntry(inlinePath);
            if (entry) out.pngBytes = *entry;
        }
    } else if (out.kind == ObjectKind::PdfCard) {
        out.pdfSourcePathUtf8 = j.value("pdfSourcePath", "");
        out.pdfSourcePageIndex0 = j.value("pdfSourcePageIndex0", 0);
        out.captionUtf8 = j.value("caption", "");
    }

    out.RecomputeBounds();
    return true;
}

bool ReadWholeFileBytes(const std::wstring& path, std::vector<std::uint8_t>& out, std::string* outErr) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (outErr) *outErr = "No se pudo abrir archivo para lectura (WinError " + std::to_string(GetLastError()) + ").";
        return false;
    }
    LARGE_INTEGER li{};
    if (!GetFileSizeEx(hFile, &li) || li.QuadPart < 0 || li.QuadPart > 512ll * 1024ll * 1024ll) {
        CloseHandle(hFile);
        if (outErr) *outErr = "Tama\xC3\xB1o de archivo inv\xC3\xA1lido.";
        return false;
    }
    const std::size_t total = static_cast<std::size_t>(li.QuadPart);
    out.resize(total);
    std::size_t offset = 0;
    while (offset < total) {
        const DWORD chunk = static_cast<DWORD>((std::min<std::size_t>)(total - offset, 4u * 1024u * 1024u));
        DWORD readNow = 0;
        if (!ReadFile(hFile, out.data() + offset, chunk, &readNow, nullptr) || readNow == 0) {
            CloseHandle(hFile);
            if (outErr) *outErr = "Fallo leyendo archivo (WinError " + std::to_string(GetLastError()) + ").";
            return false;
        }
        offset += readNow;
    }
    CloseHandle(hFile);
    return true;
}

bool WriteBytesAndFlush(const std::wstring& path, const std::vector<std::uint8_t>& data, std::string* outErr) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (outErr) *outErr = "No se pudo crear archivo temporal (WinError " + std::to_string(GetLastError()) + ").";
        return false;
    }
    std::size_t offset = 0;
    while (offset < data.size()) {
        const DWORD chunk = static_cast<DWORD>((std::min<std::size_t>)(data.size() - offset, 4u * 1024u * 1024u));
        DWORD written = 0;
        if (!WriteFile(hFile, data.data() + offset, chunk, &written, nullptr) || written != chunk) {
            CloseHandle(hFile);
            DeleteFileW(path.c_str());
            if (outErr) *outErr = "Error escribiendo bytes en disco (WinError " + std::to_string(GetLastError()) + ").";
            return false;
        }
        offset += written;
    }
    if (!FlushFileBuffers(hFile)) {
        CloseHandle(hFile);
        DeleteFileW(path.c_str());
        if (outErr) *outErr = "Error en FlushFileBuffers (WinError " + std::to_string(GetLastError()) + ").";
        return false;
    }
    CloseHandle(hFile);
    return true;
}

} // namespace

bool CudocStorage::SerializeToBytes(
    const DocumentModel& doc,
    std::vector<std::uint8_t>& outZipBytes,
    std::string* outErr)
{
    try {
        ZipArchive zip;

        for (const auto& kv : doc.Assets()) {
            zip.SetEntry("assets/" + kv.first, kv.second);
        }

        json pagesJson = json::array();
        for (std::size_t p = 0; p < doc.Pages().size(); ++p) {
            const auto& pg = doc.Pages()[p];
            json pj;
            pj["pageId"] = pg.pageId;
            pj["widthPt"] = pg.widthPt;
            pj["heightPt"] = pg.heightPt;
            pj["rotationDeg"] = pg.rotationDeg;
            pj["crop"] = {
                {"left", pg.crop.left},
                {"top", pg.crop.top},
                {"right", pg.crop.right},
                {"bottom", pg.crop.bottom}
            };
            pj["background"] = PatternToString(pg.background);
            pj["hasPdfSource"] = pg.hasPdfSource;
            pj["sourcePdfPath"] = pg.sourcePdfPathUtf8;
            pj["sourcePageIndex0"] = pg.sourcePageIndex0;
            pj["extractedText"] = pg.extractedTextUtf8;
            pj["ocrText"] = pg.ocrTextUtf8;

            json objs = json::array();
            for (std::size_t i = 0; i < pg.objects.size(); ++i) {
                objs.push_back(ObjectToJson(pg.objects[i], "p" + std::to_string(p), i, zip));
            }
            pj["objects"] = std::move(objs);
            pagesJson.push_back(std::move(pj));
        }

        json canvasObjsJson = json::array();
        for (std::size_t i = 0; i < doc.CanvasObjects().size(); ++i) {
            canvasObjsJson.push_back(ObjectToJson(doc.CanvasObjects()[i], "canvas", i, zip));
        }
        json objectsRoot;
        objectsRoot["canvasObjects"] = std::move(canvasObjsJson);

        json manifest;
        manifest["format"] = kFormatMagic;
        manifest["formatVersion"] = kCurrentFormatVersion;
        manifest["title"] = doc.Title();
        manifest["mode"] = ModeToString(doc.Mode());
        manifest["canvasBackground"] = PatternToString(doc.CanvasBackground());
        manifest["pageCount"] = doc.Pages().size();
        manifest["canvasObjectCount"] = doc.CanvasObjects().size();
        manifest["nextObjectId"] = doc.NextObjectId();
        manifest["nextPageId"] = doc.NextPageId();
        manifest["nextZOrder"] = doc.NextZOrder();
        manifest["nextAssetSeq"] = doc.NextAssetSeq();

        zip.SetTextEntry("manifest.json", manifest.dump(2));
        zip.SetTextEntry("pages.json", pagesJson.dump(2));
        zip.SetTextEntry("objects.json", objectsRoot.dump(2));

        return zip.Serialize(outZipBytes, true, outErr);
    } catch (const std::exception& ex) {
        if (outErr) *outErr = ex.what();
        return false;
    }
}

bool CudocStorage::DeserializeFromBytes(
    const std::uint8_t* bytes,
    std::size_t size,
    DocumentModel& outDoc,
    std::string* outErr)
{
    ZipArchive zip;
    if (!zip.Deserialize(bytes, size, outErr)) {
        return false;
    }

    const auto* manifestEntry = zip.FindEntry("manifest.json");
    const auto* pagesEntry = zip.FindEntry("pages.json");
    const auto* objectsEntry = zip.FindEntry("objects.json");
    if (!manifestEntry || !pagesEntry || !objectsEntry) {
        if (outErr) *outErr = "Archivo .cudoc incompleto: falta manifest.json, pages.json u objects.json.";
        return false;
    }

    try {
        const json manifest = json::parse(manifestEntry->begin(), manifestEntry->end());
        if (!manifest.is_object()) {
            if (outErr) *outErr = "manifest.json no es un objeto JSON.";
            return false;
        }
        if (manifest.value("format", "") != kFormatMagic) {
            if (outErr) *outErr = "Firma de formato .cudoc inv\xC3\xA1lida.";
            return false;
        }
        const int version = manifest.value("formatVersion", 0);
        if (version < 1 || version > kCurrentFormatVersion) {
            if (outErr) *outErr = "Versi\xC3\xB3n de formato .cudoc no soportada: " + std::to_string(version);
            return false;
        }

        DocumentMode mode = DocumentMode::PaginatedPdf;
        if (!StringToMode(manifest.value("mode", "paginated_pdf"), mode)) {
            if (outErr) *outErr = "Modo de documento desconocido en manifest.json.";
            return false;
        }

        const json pagesJson = json::parse(pagesEntry->begin(), pagesEntry->end());
        const json objectsRoot = json::parse(objectsEntry->begin(), objectsEntry->end());
        if (!pagesJson.is_array() || !objectsRoot.is_object()) {
            if (outErr) *outErr = "Estructura JSON inv\xC3\xA1lida en pages.json u objects.json.";
            return false;
        }

        DocumentModel tempDoc;
        tempDoc.ResetToBlank(mode);
        tempDoc.SetTitle(manifest.value("title", "Sin t\xC3\xADtulo"));
        tempDoc.SetCanvasBackground(StringToPattern(manifest.value("canvasBackground", "grid")));
        tempDoc.MutablePages().clear();

        // Restore named assets from assets/* (excluding assets/_inline/*)
        constexpr const char* kAssetPrefix = "assets/";
        constexpr const char* kInlinePrefix = "assets/_inline/";
        for (const std::string& path : zip.ListEntries()) {
            if (path.rfind(kAssetPrefix, 0) == 0 && path.rfind(kInlinePrefix, 0) != 0) {
                const std::string key = path.substr(7);
                if (!key.empty()) {
                    const auto* data = zip.FindEntry(path);
                    if (data) tempDoc.SetNamedAsset(key, *data);
                }
            }
        }

        std::uint64_t maxObjId = manifest.value("nextObjectId", std::uint64_t{ 0 });
        std::uint64_t maxPageId = manifest.value("nextPageId", std::uint64_t{ 0 });
        std::uint64_t maxZ = manifest.value("nextZOrder", std::uint64_t{ 0 });
        const std::uint64_t maxAsset = manifest.value("nextAssetSeq", std::uint64_t{ 0 });

        for (const auto& pj : pagesJson) {
            if (!pj.is_object()) {
                if (outErr) *outErr = "Elemento de p\xC3\xA1gina inv\xC3\xA1lido.";
                return false;
            }
            PageModel pg;
            pg.pageId = pj.value("pageId", std::uint64_t{ 0 });
            pg.widthPt = (std::max)(36.0, pj.value("widthPt", 595.0));
            pg.heightPt = (std::max)(36.0, pj.value("heightPt", 842.0));
            pg.rotationDeg = pj.value("rotationDeg", 0);
            if (pj.contains("crop") && pj["crop"].is_object()) {
                const auto& cj = pj["crop"];
                pg.crop.left = cj.value("left", 0.0);
                pg.crop.top = cj.value("top", 0.0);
                pg.crop.right = cj.value("right", 1.0);
                pg.crop.bottom = cj.value("bottom", 1.0);
                pg.crop.Clamp();
            }
            pg.background = StringToPattern(pj.value("background", "plain"));
            pg.hasPdfSource = pj.value("hasPdfSource", false);
            pg.sourcePdfPathUtf8 = pj.value("sourcePdfPath", "");
            pg.sourcePageIndex0 = pj.value("sourcePageIndex0", 0);
            pg.extractedTextUtf8 = pj.value("extractedText", "");
            pg.ocrTextUtf8 = pj.value("ocrText", "");
            if (pg.pageId > maxPageId) maxPageId = pg.pageId;

            if (pj.contains("objects") && pj["objects"].is_array()) {
                for (const auto& oj : pj["objects"]) {
                    AnnotationObject obj;
                    if (!JsonToObject(oj, zip, obj, outErr)) return false;
                    if (obj.id > maxObjId) maxObjId = obj.id;
                    if (obj.zOrder > maxZ) maxZ = obj.zOrder;
                    pg.objects.push_back(std::move(obj));
                }
            }
            tempDoc.MutablePages().push_back(std::move(pg));
        }

        if (tempDoc.Pages().empty()) {
            PageModel blank;
            blank.pageId = ++maxPageId;
            tempDoc.MutablePages().push_back(std::move(blank));
        }

        if (objectsRoot.contains("canvasObjects") && objectsRoot["canvasObjects"].is_array()) {
            for (const auto& oj : objectsRoot["canvasObjects"]) {
                AnnotationObject obj;
                if (!JsonToObject(oj, zip, obj, outErr)) return false;
                if (obj.id > maxObjId) maxObjId = obj.id;
                if (obj.zOrder > maxZ) maxZ = obj.zOrder;
                tempDoc.AddCanvasObject(std::move(obj));
            }
        }

        tempDoc.RestoreCounters(maxObjId, maxPageId, maxZ, maxAsset);
        tempDoc.RebuildLayout();
        tempDoc.SetDirty(false);
        outDoc = std::move(tempDoc);
        return true;
    } catch (const std::exception& ex) {
        if (outErr) *outErr = ex.what();
        return false;
    }
}

bool CudocStorage::SaveToFile(
    const std::wstring& path,
    DocumentModel& doc,
    std::string* outErr,
    bool simulateFailBeforeRename)
{
    std::vector<std::uint8_t> bytes;
    if (!SerializeToBytes(doc, bytes, outErr)) return false;

    const std::wstring tmpPath = path + L".tmp";
    const std::wstring bakPath = path + L".bak";

    if (!WriteBytesAndFlush(tmpPath, bytes, outErr)) {
        return false;
    }

    if (simulateFailBeforeRename) {
        DeleteFileW(tmpPath.c_str());
        if (outErr) *outErr = "Simulaci\xC3\xB3n de fallo antes del reemplazo at\xC3\xB3mico.";
        return false;
    }

    const DWORD attrs = GetFileAttributesW(path.c_str());
    const bool destExists = (attrs != INVALID_FILE_ATTRIBUTES) && ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0);
    if (destExists) {
        if (!ReplaceFileW(path.c_str(), tmpPath.c_str(), bakPath.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            if (!MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                DeleteFileW(tmpPath.c_str());
                if (outErr) *outErr = "Fallo en ReplaceFileW/MoveFileExW (WinError " + std::to_string(GetLastError()) + ").";
                return false;
            }
        }
    } else {
        if (!MoveFileExW(tmpPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(tmpPath.c_str());
            if (outErr) *outErr = "Fallo en MoveFileExW (WinError " + std::to_string(GetLastError()) + ").";
            return false;
        }
    }

    doc.SetDirty(false);
    return true;
}

bool CudocStorage::LoadFromFile(
    const std::wstring& path,
    DocumentModel& outDoc,
    std::string* outErr)
{
    std::vector<std::uint8_t> bytes;
    if (!ReadWholeFileBytes(path, bytes, outErr)) return false;
    return DeserializeFromBytes(bytes.data(), bytes.size(), outDoc, outErr);
}

bool CudocStorage::LoadFromFileWithRecovery(
    const std::wstring& path,
    DocumentModel& outDoc,
    bool* outUsedBackup,
    std::string* outErr)
{
    if (outUsedBackup) *outUsedBackup = false;
    std::string primaryErr;
    if (LoadFromFile(path, outDoc, &primaryErr)) {
        return true;
    }
    const std::wstring bakPath = path + L".bak";
    const DWORD bakAttrs = GetFileAttributesW(bakPath.c_str());
    if (bakAttrs != INVALID_FILE_ATTRIBUTES && (bakAttrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        std::string bakErr;
        if (LoadFromFile(bakPath, outDoc, &bakErr)) {
            if (outUsedBackup) *outUsedBackup = true;
            return true;
        }
        if (outErr) *outErr = "Principal: " + primaryErr + " | Respaldo .bak: " + bakErr;
        return false;
    }
    if (outErr) *outErr = primaryErr;
    return false;
}

} // namespace DocStudio
