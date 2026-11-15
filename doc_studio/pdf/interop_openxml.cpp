#include "interop_openxml.hpp"
#include "../core/zip_container.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace DocStudio {

namespace {

std::string XmlEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 16);
    for (unsigned char ch : in) {
        switch (ch) {
        case '&':  out += "&amp;"; break;
        case '<':  out += "&lt;"; break;
        case '>':  out += "&gt;"; break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default:
            // Strip illegal XML 1.0 control chars except \t \n \r
            if (ch >= 0x20 || ch == '\t' || ch == '\n' || ch == '\r') {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    return out;
}

std::string XmlUnescape(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size();) {
        if (in[i] == '&') {
            if (in.compare(i, 5, "&amp;") == 0) { out.push_back('&'); i += 5; continue; }
            if (in.compare(i, 4, "&lt;") == 0) { out.push_back('<'); i += 4; continue; }
            if (in.compare(i, 4, "&gt;") == 0) { out.push_back('>'); i += 4; continue; }
            if (in.compare(i, 6, "&quot;") == 0) { out.push_back('"'); i += 6; continue; }
            if (in.compare(i, 6, "&apos;") == 0) { out.push_back('\''); i += 6; continue; }
        }
        out.push_back(in[i++]);
    }
    return out;
}

std::vector<std::string> SplitParagraphs(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream iss(text);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out.push_back(std::move(line));
    }
    return out;
}

std::string ExtractXmlTagTextParagraphs(const std::string& xml, const std::string& paraCloseTag, const std::string& textTagName) {
    // Extracts text inside <textTagName ...>...</textTagName>, inserting '\n' when paraCloseTag appears
    const std::string openPrefix = "<" + textTagName;
    const std::string closeTag = "</" + textTagName + ">";
    std::string result;
    std::size_t pos = 0;
    while (pos < xml.size()) {
        const std::size_t tPos = xml.find(openPrefix, pos);
        const std::size_t pEndPos = paraCloseTag.empty() ? std::string::npos : xml.find(paraCloseTag, pos);
        if (tPos == std::string::npos) break;
        if (pEndPos != std::string::npos && pEndPos < tPos) {
            if (!result.empty() && result.back() != '\n') result.push_back('\n');
            pos = pEndPos + paraCloseTag.size();
            continue;
        }
        const std::size_t afterName = tPos + openPrefix.size();
        if (afterName >= xml.size() || (xml[afterName] != '>' && xml[afterName] != ' ')) {
            pos = afterName;
            continue;
        }
        const std::size_t gtPos = xml.find('>', afterName);
        if (gtPos == std::string::npos) break;
        if (gtPos > 0 && xml[gtPos - 1] == '/') {
            pos = gtPos + 1;
            continue;
        }
        const std::size_t endPos = xml.find(closeTag, gtPos + 1);
        if (endPos == std::string::npos) break;
        result += XmlUnescape(xml.substr(gtPos + 1, endPos - (gtPos + 1)));
        pos = endPos + closeTag.size();
    }
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    return result;
}

bool WriteAllFile(const std::wstring& path, const std::vector<std::uint8_t>& bytes, std::string* outErr) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (outErr) *outErr = "No se pudo crear archivo de exportaci\xC3\xB3n OpenXML.";
        return false;
    }
    std::size_t off = 0;
    while (off < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>((std::min<std::size_t>)(bytes.size() - off, 4u * 1024u * 1024u));
        DWORD wr = 0;
        if (!WriteFile(hFile, bytes.data() + off, chunk, &wr, nullptr) || wr != chunk) {
            CloseHandle(hFile);
            if (outErr) *outErr = "Error escribiendo archivo OpenXML.";
            return false;
        }
        off += wr;
    }
    FlushFileBuffers(hFile);
    CloseHandle(hFile);
    return true;
}

bool ReadAllFile(const std::wstring& path, std::vector<std::uint8_t>& out, std::string* outErr) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        if (outErr) *outErr = "No se pudo abrir archivo OpenXML.";
        return false;
    }
    LARGE_INTEGER li{};
    if (!GetFileSizeEx(hFile, &li) || li.QuadPart < 0 || li.QuadPart > 256ll * 1024ll * 1024ll) {
        CloseHandle(hFile);
        return false;
    }
    out.resize(static_cast<std::size_t>(li.QuadPart));
    std::size_t off = 0;
    while (off < out.size()) {
        const DWORD chunk = static_cast<DWORD>((std::min<std::size_t>)(out.size() - off, 4u * 1024u * 1024u));
        DWORD rd = 0;
        if (!ReadFile(hFile, out.data() + off, chunk, &rd, nullptr) || rd == 0) {
            CloseHandle(hFile);
            return false;
        }
        off += rd;
    }
    CloseHandle(hFile);
    return true;
}

std::string MakeDocxDrawingXml(const std::string& relId, std::uint64_t picId, double wPt, double hPt) {
    const std::uint64_t cx = static_cast<std::uint64_t>((std::max)(72.0, (std::min)(468.0, wPt)) * 12700.0);
    const std::uint64_t cy = static_cast<std::uint64_t>((std::max)(48.0, (std::min)(640.0, hPt)) * 12700.0);
    std::ostringstream s;
    s << "<w:p><w:r><w:drawing>"
      << "<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
      << "<wp:extent cx=\"" << cx << "\" cy=\"" << cy << "\"/>"
      << "<wp:docPr id=\"" << picId << "\" name=\"Imagen " << picId << "\"/>"
      << "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
      << "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
      << "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
      << "<pic:nvPicPr><pic:cNvPr id=\"" << picId << "\" name=\"img" << picId << ".png\"/><pic:cNvPicPr/></pic:nvPicPr>"
      << "<pic:blipFill><a:blip r:embed=\"" << relId << "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
      << "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" << cx << "\" cy=\"" << cy << "\"/></a:xfrm>"
      << "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
      << "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>\n";
    return s.str();
}

} // namespace

bool OpenXmlInterop::ExportToDocxBytes(
    const DocumentModel& doc,
    const std::vector<std::vector<std::uint8_t>>& optionalPageSnapshotsPng,
    std::vector<std::uint8_t>& outDocxBytes,
    std::string* outErr)
{
    ZipArchive zip;
    zip.SetTextEntry("[Content_Types].xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Default Extension=\"png\" ContentType=\"image/png\"/>"
        "<Override PartName=\"/word/document.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
        "</Types>");

    zip.SetTextEntry("_rels/.rels",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"word/document.xml\"/>"
        "</Relationships>");

    std::ostringstream rels;
    rels << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
         << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";

    std::ostringstream body;
    body << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
         << "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
         << "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
         << "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\">\n<w:body>\n";

    body << "<w:p><w:r><w:rPr><w:b/><w:sz w:val=\"32\"/></w:rPr><w:t xml:space=\"preserve\">"
         << XmlEscape(doc.Title()) << "</w:t></w:r></w:p>\n";

    std::uint64_t nextImgIdx = 0;
    auto addImage = [&](const std::vector<std::uint8_t>& png, double wPt, double hPt) {
        if (png.empty()) return;
        const std::uint64_t id = ++nextImgIdx;
        const std::string rid = "rId" + std::to_string(id);
        const std::string mediaPath = "media/image" + std::to_string(id) + ".png";
        zip.SetEntry("word/" + mediaPath, png);
        rels << "<Relationship Id=\"" << rid
             << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\""
             << mediaPath << "\"/>\n";
        body << MakeDocxDrawingXml(rid, id, wPt, hPt);
    };

    if (doc.Mode() == DocumentMode::InfiniteCanvas) {
        if (optionalPageSnapshotsPng.size() > 0 && !optionalPageSnapshotsPng[0].empty()) {
            addImage(optionalPageSnapshotsPng[0], 468.0, 320.0);
        }
        for (const auto& obj : doc.CanvasObjects()) {
            if (obj.kind == ObjectKind::TextBox && !obj.textUtf8.empty()) {
                for (const auto& para : SplitParagraphs(obj.textUtf8)) {
                    body << "<w:p><w:r><w:t xml:space=\"preserve\">" << XmlEscape(para) << "</w:t></w:r></w:p>\n";
                }
            } else if (obj.kind == ObjectKind::Image) {
                const auto* b = !obj.pngBytes.empty() ? &obj.pngBytes : doc.FindAsset(obj.assetKey);
                if (b) addImage(*b, obj.bounds.w, obj.bounds.h);
            }
        }
    } else {
        for (std::size_t p = 0; p < doc.Pages().size(); ++p) {
            if (p > 0) {
                body << "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>\n";
            }
            const auto& pg = doc.Pages()[p];
            if (p < optionalPageSnapshotsPng.size() && !optionalPageSnapshotsPng[p].empty()) {
                addImage(optionalPageSnapshotsPng[p], 440.0, 440.0 * (pg.EffectiveHeightPt() / (std::max)(1.0, pg.EffectiveWidthPt())));
            }
            const std::string textBlock = !pg.extractedTextUtf8.empty() ? pg.extractedTextUtf8 : pg.ocrTextUtf8;
            for (const auto& para : SplitParagraphs(textBlock)) {
                body << "<w:p><w:r><w:t xml:space=\"preserve\">" << XmlEscape(para) << "</w:t></w:r></w:p>\n";
            }
            for (const auto& obj : pg.objects) {
                if (obj.kind == ObjectKind::TextBox && !obj.textUtf8.empty()) {
                    for (const auto& para : SplitParagraphs(obj.textUtf8)) {
                        body << "<w:p><w:r><w:t xml:space=\"preserve\">" << XmlEscape(para) << "</w:t></w:r></w:p>\n";
                    }
                } else if (obj.kind == ObjectKind::Image) {
                    const auto* b = !obj.pngBytes.empty() ? &obj.pngBytes : doc.FindAsset(obj.assetKey);
                    if (b) addImage(*b, obj.bounds.w, obj.bounds.h);
                }
            }
        }
    }

    body << "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/></w:sectPr>\n</w:body>\n</w:document>";
    rels << "</Relationships>";

    zip.SetTextEntry("word/_rels/document.xml.rels", rels.str());
    zip.SetTextEntry("word/document.xml", body.str());
    return zip.Serialize(outDocxBytes, true, outErr);
}

bool OpenXmlInterop::ExportToDocxFile(
    const DocumentModel& doc,
    const std::wstring& outPath,
    const std::vector<std::vector<std::uint8_t>>& optionalPageSnapshotsPng,
    std::string* outErr)
{
    std::vector<std::uint8_t> bytes;
    if (!ExportToDocxBytes(doc, optionalPageSnapshotsPng, bytes, outErr)) return false;
    return WriteAllFile(outPath, bytes, outErr);
}

bool OpenXmlInterop::ImportFromDocxBytes(
    const std::uint8_t* bytes,
    std::size_t size,
    DocumentModel& outDoc,
    std::string* outErr)
{
    ZipArchive zip;
    if (!zip.Deserialize(bytes, size, outErr)) return false;
    const auto* docEntry = zip.FindEntry("word/document.xml");
    if (!docEntry) {
        if (outErr) *outErr = "El archivo .docx no contiene word/document.xml.";
        return false;
    }

    const std::string xml(reinterpret_cast<const char*>(docEntry->data()), docEntry->size());
    const std::string text = ExtractXmlTagTextParagraphs(xml, "</w:p>", "w:t");

    DocumentModel temp;
    temp.ResetToBlank(DocumentMode::PaginatedPdf);
    temp.SetTitle("Importado de Word");
    if (!temp.MutablePages().empty()) {
        temp.MutablePages()[0].extractedTextUtf8 = text;
        if (!text.empty()) {
            AnnotationObject tb;
            tb.kind = ObjectKind::TextBox;
            tb.bounds = { 48.0, 48.0, 499.0, 520.0 };
            tb.textUtf8 = text;
            tb.strokeColor = DocColor::RGBA(24, 28, 36, 255);
            tb.fontSizePt = 11.5;
            temp.AddPageObject(0, std::move(tb));
        }

        double yOff = 60.0;
        for (const std::string& ePath : zip.ListEntries()) {
            if (ePath.rfind("word/media/", 0) == 0) {
                if (const auto* media = zip.FindEntry(ePath)) {
                    AnnotationObject img;
                    img.kind = ObjectKind::Image;
                    img.bounds = { 72.0, yOff, 300.0, 180.0 };
                    img.pngBytes = *media;
                    temp.AddPageObject(0, std::move(img));
                    yOff += 200.0;
                }
            }
        }
    }
    temp.RebuildLayout();
    temp.SetDirty(false);
    outDoc = std::move(temp);
    return true;
}

bool OpenXmlInterop::ImportFromDocxFile(
    const std::wstring& path,
    DocumentModel& outDoc,
    std::string* outErr)
{
    std::vector<std::uint8_t> bytes;
    if (!ReadAllFile(path, bytes, outErr)) return false;
    return ImportFromDocxBytes(bytes.data(), bytes.size(), outDoc, outErr);
}

bool OpenXmlInterop::ExportToPptxBytes(
    const DocumentModel& doc,
    const std::vector<std::vector<std::uint8_t>>& optionalPageSnapshotsPng,
    std::vector<std::uint8_t>& outPptxBytes,
    std::string* outErr)
{
    const std::size_t slideCount = (doc.Mode() == DocumentMode::InfiniteCanvas)
        ? 1u
        : (std::max<std::size_t>)(1u, doc.Pages().size());

    ZipArchive zip;
    std::ostringstream ct;
    ct << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
       << "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
       << "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
       << "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
       << "<Default Extension=\"png\" ContentType=\"image/png\"/>"
       << "<Override PartName=\"/ppt/presentation.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml\"/>"
       << "<Override PartName=\"/ppt/slideMasters/slideMaster1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml\"/>"
       << "<Override PartName=\"/ppt/slideLayouts/slideLayout1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml\"/>"
       << "<Override PartName=\"/ppt/theme/theme1.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.theme+xml\"/>";
    for (std::size_t i = 0; i < slideCount; ++i) {
        ct << "<Override PartName=\"/ppt/slides/slide" << (i + 1)
           << ".xml\" ContentType=\"application/vnd.openxmlformats-officedocument.presentationml.slide+xml\"/>";
    }
    ct << "</Types>";
    zip.SetTextEntry("[Content_Types].xml", ct.str());

    zip.SetTextEntry("_rels/.rels",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"ppt/presentation.xml\"/>"
        "</Relationships>");

    zip.SetTextEntry("ppt/theme/theme1.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<a:theme xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" name=\"DocStudio\">"
        "<a:themeElements>"
        "<a:clrScheme name=\"Default\"><a:dk1><a:srgbClr val=\"111827\"/></a:dk1><a:lt1><a:srgbClr val=\"FFFFFF\"/></a:lt1>"
        "<a:dk2><a:srgbClr val=\"334155\"/></a:dk2><a:lt2><a:srgbClr val=\"F1F5F9\"/></a:lt2>"
        "<a:accent1><a:srgbClr val=\"1D4ED8\"/></a:accent1><a:accent2><a:srgbClr val=\"0EA5E9\"/></a:accent2>"
        "<a:accent3><a:srgbClr val=\"10B981\"/></a:accent3><a:accent4><a:srgbClr val=\"F59E0B\"/></a:accent4>"
        "<a:accent5><a:srgbClr val=\"8B5CF6\"/></a:accent5><a:accent6><a:srgbClr val=\"EC4899\"/></a:accent6>"
        "<a:hlink><a:srgbClr val=\"2563EB\"/></a:hlink><a:folHlink><a:srgbClr val=\"7C3AED\"/></a:folHlink></a:clrScheme>"
        "<a:fontScheme name=\"Default\"><a:majorFont><a:latin typeface=\"Segoe UI\"/></a:majorFont><a:minorFont><a:latin typeface=\"Segoe UI\"/></a:minorFont></a:fontScheme>"
        "<a:fmtScheme name=\"Default\"><a:fillStyleLst><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:fillStyleLst>"
        "<a:lnStyleLst><a:ln w=\"9525\"><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:ln></a:lnStyleLst>"
        "<a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst>"
        "<a:bgFillStyleLst><a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill></a:bgFillStyleLst></a:fmtScheme>"
        "</a:themeElements></a:theme>");

    zip.SetTextEntry("ppt/slideMasters/_rels/slideMaster1.xml.rels",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>"
        "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/theme\" Target=\"../theme/theme1.xml\"/>"
        "</Relationships>");

    zip.SetTextEntry("ppt/slideMasters/slideMaster1.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<p:sldMaster xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">"
        "<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/></p:spTree></p:cSld>"
        "<p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" hlink=\"hlink\" folHlink=\"folHlink\"/>"
        "<p:sldLayoutIdLst><p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/></p:sldLayoutIdLst></p:sldMaster>");

    zip.SetTextEntry("ppt/slideLayouts/_rels/slideLayout1.xml.rels",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster\" Target=\"../slideMasters/slideMaster1.xml\"/>"
        "</Relationships>");

    zip.SetTextEntry("ppt/slideLayouts/slideLayout1.xml",
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<p:sldLayout xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\" type=\"blank\">"
        "<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/></p:spTree></p:cSld></p:sldLayout>");

    std::ostringstream presRels;
    presRels << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
             << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
             << "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideMaster\" Target=\"slideMasters/slideMaster1.xml\"/>\n";

    std::ostringstream presXml;
    presXml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<p:presentation xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">\n"
            << "<p:sldMasterIdLst><p:sldMasterId id=\"2147483648\" r:id=\"rId1\"/></p:sldMasterIdLst>\n"
            << "<p:sldIdLst>\n";

    std::size_t globalMediaIdx = 0;

    for (std::size_t i = 0; i < slideCount; ++i) {
        const std::string presRelId = "rId" + std::to_string(i + 2);
        presRels << "<Relationship Id=\"" << presRelId
                 << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" Target=\"slides/slide"
                 << (i + 1) << ".xml\"/>\n";
        presXml << "<p:sldId id=\"" << (256 + i) << "\" r:id=\"" << presRelId << "\"/>\n";

        std::ostringstream sRels;
        sRels << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
              << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
              << "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>\n";

        std::ostringstream slideXml;
        slideXml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                 << "<p:sld xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" xmlns:p=\"http://schemas.openxmlformats.org/presentationml/2006/main\">\n"
                 << "<p:cSld><p:spTree>\n"
                 << "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>\n";

        std::size_t shapeId = 2;
        std::size_t sRelCounter = 1;

        auto appendTextShape = [&](const std::string& text, std::int64_t x, std::int64_t y, std::int64_t cx, std::int64_t cy) {
            if (text.empty()) return;
            slideXml << "<p:sp><p:nvSpPr><p:cNvPr id=\"" << (++shapeId) << "\" name=\"Texto\"/><p:cNvSpPr txBox=\"1\"/><p:nvPr/></p:nvSpPr>"
                     << "<p:spPr><a:xfrm><a:off x=\"" << x << "\" y=\"" << y << "\"/><a:ext cx=\"" << cx << "\" cy=\"" << cy << "\"/></a:xfrm>"
                     << "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom><a:noFill/></p:spPr><p:txBody><a:bodyPr wrap=\"square\"/><a:lstStyle/>";
            for (const auto& line : SplitParagraphs(text)) {
                slideXml << "<a:p><a:r><a:rPr lang=\"es-ES\" sz=\"1400\"/><a:t>" << XmlEscape(line) << "</a:t></a:r></a:p>";
            }
            slideXml << "</p:txBody></p:sp>\n";
        };

        if (i < optionalPageSnapshotsPng.size() && !optionalPageSnapshotsPng[i].empty()) {
            const std::string mediaName = "slideImg" + std::to_string(++globalMediaIdx) + ".png";
            const std::string rid = "rId" + std::to_string(++sRelCounter);
            zip.SetEntry("ppt/media/" + mediaName, optionalPageSnapshotsPng[i]);
            sRels << "<Relationship Id=\"" << rid
                  << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" Target=\"../media/"
                  << mediaName << "\"/>\n";
            slideXml << "<p:pic><p:nvPicPr><p:cNvPr id=\"" << (++shapeId) << "\" name=\"" << mediaName << "\"/><p:cNvPicPr/><p:nvPr/></p:nvPicPr>"
                     << "<p:blipFill><a:blip r:embed=\"" << rid << "\"/><a:stretch><a:fillRect/></a:stretch></p:blipFill>"
                     << "<p:spPr><a:xfrm><a:off x=\"457200\" y=\"365760\"/><a:ext cx=\"4572000\" cy=\"5800000\"/></a:xfrm>"
                     << "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr></p:pic>\n";
        }

        if (doc.Mode() == DocumentMode::InfiniteCanvas) {
            std::string combined;
            for (const auto& obj : doc.CanvasObjects()) {
                if (obj.kind == ObjectKind::TextBox && !obj.textUtf8.empty()) {
                    if (!combined.empty()) combined += '\n';
                    combined += obj.textUtf8;
                }
            }
            appendTextShape(doc.Title() + (combined.empty() ? "" : ("\n" + combined)), 5200000, 457200, 6500000, 5800000);
        } else if (i < doc.Pages().size()) {
            const auto& pg = doc.Pages()[i];
            std::string slideText = !pg.extractedTextUtf8.empty() ? pg.extractedTextUtf8 : pg.ocrTextUtf8;
            for (const auto& obj : pg.objects) {
                if (obj.kind == ObjectKind::TextBox && !obj.textUtf8.empty()) {
                    if (!slideText.empty()) slideText += '\n';
                    slideText += obj.textUtf8;
                }
            }
            const std::int64_t tx = (i < optionalPageSnapshotsPng.size() && !optionalPageSnapshotsPng[i].empty()) ? 5200000 : 457200;
            const std::int64_t tw = (tx > 1000000) ? 6500000 : 11000000;
            appendTextShape(slideText.empty() ? (doc.Title() + " - P\xC3\xA1gina " + std::to_string(i + 1)) : slideText,
                            tx, 457200, tw, 5800000);
        }

        slideXml << "</p:spTree></p:cSld></p:sld>";
        sRels << "</Relationships>";
        zip.SetTextEntry("ppt/slides/_rels/slide" + std::to_string(i + 1) + ".xml.rels", sRels.str());
        zip.SetTextEntry("ppt/slides/slide" + std::to_string(i + 1) + ".xml", slideXml.str());
    }

    presXml << "</p:sldIdLst>\n<p:sldSz cx=\"12192000\" cy=\"6858000\"/><p:notesSz cx=\"6858000\" cy=\"9144000\"/></p:presentation>";
    presRels << "</Relationships>";
    zip.SetTextEntry("ppt/_rels/presentation.xml.rels", presRels.str());
    zip.SetTextEntry("ppt/presentation.xml", presXml.str());
    return zip.Serialize(outPptxBytes, true, outErr);
}

bool OpenXmlInterop::ExportToPptxFile(
    const DocumentModel& doc,
    const std::wstring& outPath,
    const std::vector<std::vector<std::uint8_t>>& optionalPageSnapshotsPng,
    std::string* outErr)
{
    std::vector<std::uint8_t> bytes;
    if (!ExportToPptxBytes(doc, optionalPageSnapshotsPng, bytes, outErr)) return false;
    return WriteAllFile(outPath, bytes, outErr);
}

bool OpenXmlInterop::ImportFromPptxBytes(
    const std::uint8_t* bytes,
    std::size_t size,
    DocumentModel& outDoc,
    std::string* outErr)
{
    ZipArchive zip;
    if (!zip.Deserialize(bytes, size, outErr)) return false;

    std::vector<std::pair<int, std::string>> slidePaths;
    for (const std::string& p : zip.ListEntries()) {
        constexpr const char* kPrefix = "ppt/slides/slide";
        if (p.rfind(kPrefix, 0) == 0 && p.size() > 20 && p.compare(p.size() - 4, 4, ".xml") == 0) {
            const int num = std::atoi(p.substr(16, p.size() - 20).c_str());
            if (num > 0) slidePaths.push_back({ num, p });
        }
    }
    if (slidePaths.empty()) {
        if (outErr) *outErr = "El archivo .pptx no contiene diapositivas.";
        return false;
    }
    std::sort(slidePaths.begin(), slidePaths.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    DocumentModel temp;
    temp.ResetToBlank(DocumentMode::PaginatedPdf);
    temp.SetTitle("Importado de PowerPoint");
    temp.MutablePages().clear();

    for (const auto& sp : slidePaths) {
        const auto* entry = zip.FindEntry(sp.second);
        if (!entry) continue;
        const std::string xml(reinterpret_cast<const char*>(entry->data()), entry->size());
        const std::string txt = ExtractXmlTagTextParagraphs(xml, "</a:p>", "a:t");

        PageModel pm;
        pm.pageId = temp.AllocatePageId();
        pm.widthPt = 960.0;
        pm.heightPt = 540.0;
        pm.extractedTextUtf8 = txt;
        if (!txt.empty()) {
            AnnotationObject tb;
            tb.id = temp.AllocateObjectId();
            tb.zOrder = temp.AllocateZOrder();
            tb.kind = ObjectKind::TextBox;
            tb.bounds = { 48.0, 48.0, 864.0, 420.0 };
            tb.textUtf8 = txt;
            tb.fontSizePt = 16.0;
            tb.strokeColor = DocColor::RGBA(20, 24, 34, 255);
            pm.objects.push_back(std::move(tb));
        }
        temp.MutablePages().push_back(std::move(pm));
    }

    if (temp.Pages().empty()) {
        PageModel pm;
        pm.pageId = temp.AllocatePageId();
        temp.MutablePages().push_back(std::move(pm));
    }

    // Import first media assets onto slide 0 if present
    double xOff = 48.0;
    for (const std::string& p : zip.ListEntries()) {
        if (p.rfind("ppt/media/", 0) == 0) {
            if (const auto* m = zip.FindEntry(p)) {
                AnnotationObject img;
                img.kind = ObjectKind::Image;
                img.bounds = { xOff, 180.0, 280.0, 200.0 };
                img.pngBytes = *m;
                temp.AddPageObject(0, std::move(img));
                xOff += 300.0;
                if (xOff > 700.0) break;
            }
        }
    }

    temp.RebuildLayout();
    temp.SetDirty(false);
    outDoc = std::move(temp);
    return true;
}

bool OpenXmlInterop::ImportFromPptxFile(
    const std::wstring& path,
    DocumentModel& outDoc,
    std::string* outErr)
{
    std::vector<std::uint8_t> bytes;
    if (!ReadAllFile(path, bytes, outErr)) return false;
    return ImportFromPptxBytes(bytes.data(), bytes.size(), outDoc, outErr);
}

} // namespace DocStudio
