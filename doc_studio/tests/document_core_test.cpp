#include "../core/camera.hpp"
#include "../core/command_stack.hpp"
#include "../core/cudoc_storage.hpp"
#include "../core/doc_types.hpp"
#include "../core/document_model.hpp"
#include "../core/lru_byte_cache.hpp"
#include "../core/page_layout.hpp"
#include "../core/spatial_index.hpp"
#include "../core/utf_conv.hpp"
#include "../core/zip_container.hpp"
#include "../pdf/interop_openxml.hpp"
#include "../pdf/pdf_parser.hpp"
#include "../pdf/pdf_signer.hpp"
#include "../pdf/pdf_writer.hpp"
#include "../pdf/winrt_adapters.hpp"
#include "../render/d2d_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace DocStudio;

namespace {

int gFailures = 0;

void Check(bool cond, const char* expr, int line) {
    if (!cond) {
        std::cerr << "[FAIL] line " << line << ": " << expr << "\n";
        ++gFailures;
    }
}

#define CHECK(expr) Check(static_cast<bool>(expr), #expr, __LINE__)

struct ScopedTempDir {
    fs::path dir;
    bool keep = false;
    explicit ScopedTempDir(const fs::path& overrideDir = {}) {
        if (!overrideDir.empty()) {
            dir = overrideDir;
            keep = true;
        } else {
            dir = fs::temp_directory_path() / ("docstudio_test_" + std::to_string(GetCurrentProcessId()));
        }
        fs::create_directories(dir);
    }
    ~ScopedTempDir() {
        if (keep) return;
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

std::vector<std::uint8_t> MakeSolidPng(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    RasterImageBgra32 img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<std::size_t>(w) * h * 4u);
    for (int i = 0; i < w * h; ++i) {
        img.pixels[i * 4 + 0] = b;
        img.pixels[i * 4 + 1] = g;
        img.pixels[i * 4 + 2] = r;
        img.pixels[i * 4 + 3] = 255;
    }
    std::vector<std::uint8_t> png;
    WinRtPdfRenderer::EncodeBgra32ToPngBytes(img, png);
    return png;
}

void TestF1CudocAndCommandStack(const fs::path& tmpDir) {
    std::cout << "-- Test F1: .cudoc Round-trip, Corruption Rejection, Transactional Save, CommandStack --\n";
    DocumentModel doc;
    CommandStack stack;
    doc.SetTitle("Cuaderno C\xC3\xA1lculo CASIO");

    AnnotationObject ink;
    ink.kind = ObjectKind::InkStroke;
    ink.strokeColor = DocColor::RGBA(26, 115, 232, 255);
    ink.strokeWidth = 3.2;
    ink.inkPoints = { {50.0, 60.0, 0.25f}, {95.0, 120.0, 0.85f}, {160.0, 140.0, 0.55f} };
    CHECK(stack.Execute(doc, std::make_unique<AddObjectCommand>(ObjectTarget{ false, 0 }, ink)));

    AnnotationObject tb;
    tb.kind = ObjectKind::TextBox;
    tb.bounds = { 80.0, 200.0, 240.0, 48.0 };
    tb.textUtf8 = "Ecuaci\xC3\xB3n cuadr\xC3\xA1tica x\xC2\xB2 - 5x + 6 = 0";
    tb.fontSizePt = 13.0;
    CHECK(stack.Execute(doc, std::make_unique<AddObjectCommand>(ObjectTarget{ false, 0 }, tb)));

    AnnotationObject imgObj;
    imgObj.kind = ObjectKind::Image;
    imgObj.bounds = { 100.0, 280.0, 64.0, 48.0 };
    imgObj.pngBytes = MakeSolidPng(32, 24, 24, 180, 90);
    CHECK(!imgObj.pngBytes.empty());
    CHECK(stack.Execute(doc, std::make_unique<AddObjectCommand>(ObjectTarget{ false, 0 }, imgObj)));

    PageModel page2;
    page2.background = PaperPattern::Grid;
    CHECK(stack.Execute(doc, std::make_unique<InsertPageCommand>(1, page2)));
    CHECK(doc.Pages().size() == 2);

    // Undo / Redo check
    CHECK(stack.Undo(doc));
    CHECK(doc.Pages().size() == 1);
    CHECK(stack.Redo(doc));
    CHECK(doc.Pages().size() == 2);
    CHECK(stack.Execute(doc, std::make_unique<RotatePageCommand>(1, 90)));
    CHECK(doc.Pages()[1].rotationDeg == 90);
    CHECK(stack.Execute(doc, std::make_unique<CropPageCommand>(1, NormalizedCropRect{ 0.1, 0.1, 0.9, 0.8 })));

    // Guard deleting down to 0 pages must fail
    CHECK(stack.Execute(doc, std::make_unique<DeletePageCommand>(1)));
    CHECK(doc.Pages().size() == 1);
    CHECK(!stack.Execute(doc, std::make_unique<DeletePageCommand>(0)));
    CHECK(stack.Undo(doc)); // restores page 1
    CHECK(doc.Pages().size() == 2);

    // Atomic transactional save + round-trip load
    const fs::path cudocPath = tmpDir / L"proyecto_prueba.cudoc";
    std::string err;
    CHECK(CudocStorage::SaveToFile(cudocPath.wstring(), doc, &err));
    CHECK(!doc.IsDirty());

    // Simulate failed overwrite mid-transaction: original must survive intact!
    doc.SetTitle("TituloNoGuardado");
    CHECK(!CudocStorage::SaveToFile(cudocPath.wstring(), doc, &err, true));

    DocumentModel loaded;
    CHECK(CudocStorage::LoadFromFile(cudocPath.wstring(), loaded, &err));
    CHECK(loaded.Title() == "Cuaderno C\xC3\xA1lculo CASIO");
    CHECK(loaded.Pages().size() == 2);
    CHECK(loaded.Pages()[0].objects.size() == 3);
    CHECK(loaded.Pages()[0].objects[0].inkPoints.size() == 3);
    CHECK(std::abs(loaded.Pages()[0].objects[0].inkPoints[1].pressure - 0.85f) < 1e-4f);
    CHECK(loaded.Pages()[0].objects[1].textUtf8.find("cuadr\xC3\xA1tica") != std::string::npos);
    CHECK(loaded.Pages()[0].objects[2].pngBytes == imgObj.pngBytes);
    CHECK(loaded.Pages()[1].rotationDeg == 90);
    CHECK(std::abs(loaded.Pages()[1].crop.left - 0.1) < 1e-4);

    // Corrupt .cudoc rejection without mutating valid target
    DocumentModel guardDoc;
    guardDoc.SetTitle("NoTocar");
    const std::vector<std::uint8_t> junk = { 'N', 'O', 'T', 'A', 'Z', 'I', 'P' };
    CHECK(!CudocStorage::DeserializeFromBytes(junk.data(), junk.size(), guardDoc, &err));
    CHECK(guardDoc.Title() == "NoTocar");

    // Corrupt primary file + .bak recovery check
    {
        DocumentModel v2;
        CHECK(CudocStorage::LoadFromFile(cudocPath.wstring(), v2, &err));
        v2.SetTitle("SegundaVersion");
        CHECK(CudocStorage::SaveToFile(cudocPath.wstring(), v2, &err)); // creates .bak with loaded v1
        HANDLE hf = CreateFileW(cudocPath.wstring().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(hf != INVALID_HANDLE_VALUE);
        DWORD wr = 0;
        WriteFile(hf, junk.data(), static_cast<DWORD>(junk.size()), &wr, nullptr);
        CloseHandle(hf);

        bool usedBak = false;
        DocumentModel recovered;
        CHECK(CudocStorage::LoadFromFileWithRecovery(cudocPath.wstring(), recovered, &usedBak, &err));
        CHECK(usedBak);
        CHECK(recovered.Title() == "Cuaderno C\xC3\xA1lculo CASIO");
    }
}

void TestF2LayoutCameraAndLruCache() {
    std::cout << "-- Test F2: Virtualized 1,200-Page Layout, Zoom-at-Anchor, 256MiB LRU Cache --\n";
    std::vector<PageModel> many(1200);
    for (std::size_t i = 0; i < many.size(); ++i) {
        many[i].pageId = i + 1;
        many[i].widthPt = (i % 5 == 0) ? 842.0 : 595.0;
        many[i].heightPt = (i % 5 == 0) ? 595.0 : 842.0;
    }
    PageLayout layout;
    layout.Rebuild(many);
    CHECK(layout.PageCount() == 1200);
    CHECK(layout.TotalHeight() > 900000.0);

    std::size_t first = 0, last = 0;
    const auto* midPl = layout.PlacementAt(650);
    CHECK(midPl != nullptr);
    CHECK(layout.ComputeVisibleRange(midPl->topWorld + 10.0, midPl->topWorld + 900.0, first, last, 1));
    CHECK(first <= 650 && last >= 650 && (last - first) <= 6);

    std::size_t hitIdx = 0;
    double lx = 0, ly = 0;
    CHECK(layout.HitTestWorldPoint(0.0, midPl->topWorld + 123.5, hitIdx, lx, ly));
    CHECK(hitIdx == 650);
    CHECK(std::abs(ly - 123.5) < 1e-6);

    // Zoom-at-anchor invariant
    Camera2D cam;
    cam.panX = -120.0;
    cam.panY = 3450.0;
    cam.zoom = 1.25;
    const DocPoint beforeW = cam.ScreenToWorld(640.0, 410.0);
    cam.ZoomAtScreenAnchor(640.0, 410.0, 2.85);
    const DocPoint afterW = cam.ScreenToWorld(640.0, 410.0);
    CHECK(std::abs(beforeW.x - afterW.x) < 1e-6);
    CHECK(std::abs(beforeW.y - afterW.y) < 1e-6);

    // LRU Byte-budget enforcement
    LruByteCache<int, std::vector<std::uint8_t>> lru(1024u * 1024u);
    CHECK(lru.Put(1, std::vector<std::uint8_t>(400u * 1024u, 1), 400u * 1024u));
    CHECK(lru.Put(2, std::vector<std::uint8_t>(400u * 1024u, 2), 400u * 1024u));
    std::vector<std::uint8_t> tmp;
    CHECK(lru.TryGet(1, tmp)); // touch key 1 so key 2 becomes LRU
    CHECK(lru.Put(3, std::vector<std::uint8_t>(400u * 1024u, 3), 400u * 1024u));
    CHECK(lru.BytesUsed() <= 1024u * 1024u);
    CHECK(lru.EvictionCount() == 1);
    CHECK(lru.TryGet(1, tmp));
    CHECK(!lru.TryGet(2, tmp));
    CHECK(!lru.Put(99, std::vector<std::uint8_t>(2u * 1024u * 1024u, 9), 2u * 1024u * 1024u)); // oversize rejected cleanly
}

void TestF3D2DRenderAndMultiPressureInk() {
    std::cout << "-- Test F3: Direct2D Offscreen Render (Ink, Highlighter, Shape, Grid, TextBox) --\n";
    DocumentModel doc;
    doc.MutablePages()[0].background = PaperPattern::Grid;

    AnnotationObject pen;
    pen.kind = ObjectKind::InkStroke;
    pen.strokeColor = DocColor::RGBA(26, 115, 232, 255);
    pen.strokeWidth = 6.0;
    pen.inkPoints = { {60.0, 120.0, 0.2f}, {220.0, 120.0, 0.9f}, {420.0, 180.0, 0.7f} };
    doc.AddPageObject(0, std::move(pen));

    AnnotationObject textBox;
    textBox.kind = ObjectKind::TextBox;
    textBox.bounds = { 80.0, 220.0, 420.0, 80.0 };
    textBox.textUtf8 = "CASIO 2026 DOCUMENT STUDIO";
    textBox.fontSizePt = 28.0;
    textBox.bold = true;
    textBox.strokeColor = DocColor::RGBA(10, 10, 10, 255);
    doc.AddPageObject(0, std::move(textBox));

    D2DRenderer renderer;
    RasterImageBgra32 img;
    CHECK(renderer.RenderPageOffscreen(doc, 0, 800, nullptr, img));
    CHECK(img.Valid());

    std::size_t bluePixels = 0;
    std::size_t darkTextPixels = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(img.width) * img.height; ++i) {
        const std::uint8_t b = img.pixels[i * 4 + 0];
        const std::uint8_t g = img.pixels[i * 4 + 1];
        const std::uint8_t r = img.pixels[i * 4 + 2];
        if (b > 170 && r < 80) ++bluePixels;
        if (r < 60 && g < 60 && b < 60) ++darkTextPixels;
    }
    std::cout << "  D2D Offscreen pixels -> blue_ink=" << bluePixels << ", dark_text=" << darkTextPixels << "\n";
    CHECK(bluePixels > 500);
    CHECK(darkTextPixels > 400);
}

void TestF4RealCasioPdfEditMergeSplitFlatten(const fs::path& manualsDir, const fs::path& tmpDir) {
    std::cout << "-- Test F4: Real CASIO PDF Split, Rotate, Crop, Flatten Ink+Image+Stamp, Merge --\n";
    const fs::path srcPdf = manualsDir / L"fx-570CW_991CW_ES.pdf";
    CHECK(fs::exists(srcPdf));
    if (!fs::exists(srcPdf)) return;

    PdfDocument parser;
    std::string err;
    CHECK(parser.LoadFromFile(srcPdf.wstring(), &err));
    std::cout << "  fx-570CW_991CW_ES.pdf version=" << parser.PdfVersion() << " pages=" << parser.PageCount() << "\n";
    CHECK(parser.PageCount() >= 5);
    if (parser.PageCount() < 5) return;

    const fs::path splitPath = tmpDir / L"cw_split2.pdf";
    CHECK(PdfWriter::SplitPdfFile(srcPdf.wstring(), { 0, 2 }, splitPath.wstring(), &err));

    PdfDocument splitParsed;
    CHECK(splitParsed.LoadFromFile(splitPath.wstring(), &err));
    CHECK(splitParsed.PageCount() == 2);
    if (splitParsed.PageCount() != 2) return;

    DocumentModel editDoc;
    CHECK(splitParsed.PopulateDocumentModel(editDoc, WideToUtf8(splitPath.wstring()), &err));

    // Rotate page 0 clockwise 90 deg, crop page 1, add bright red ink & PNG image onto page 0
    CommandStack stack;
    CHECK(stack.Execute(editDoc, std::make_unique<RotatePageCommand>(0, 90)));
    CHECK(stack.Execute(editDoc, std::make_unique<CropPageCommand>(1, NormalizedCropRect{ 0.1, 0.1, 0.9, 0.9 })));

    AnnotationObject redInk;
    redInk.kind = ObjectKind::InkStroke;
    redInk.strokeColor = DocColor::RGBA(230, 25, 25, 255);
    redInk.strokeWidth = 10.0;
    redInk.inkPoints = { {60.0, 60.0, 0.9f}, {260.0, 60.0, 0.9f}, {260.0, 140.0, 0.9f} };
    CHECK(stack.Execute(editDoc, std::make_unique<AddObjectCommand>(ObjectTarget{ false, 0 }, redInk)));

    AnnotationObject imgAnnot;
    imgAnnot.kind = ObjectKind::Image;
    imgAnnot.bounds = { 300.0, 60.0, 80.0, 60.0 };
    imgAnnot.pngBytes = MakeSolidPng(24, 24, 15, 190, 40); // green patch
    CHECK(stack.Execute(editDoc, std::make_unique<AddObjectCommand>(ObjectTarget{ false, 0 }, imgAnnot)));

    const fs::path flattenedPdf = tmpDir / L"cw_flattened.pdf";
    CHECK(PdfWriter::ExportDocumentToFile(editDoc, flattenedPdf.wstring(), &err));

    // Independent verify: load exported PDF via both PdfDocument AND WinRt Windows.Data.Pdf
    PdfDocument reParsed;
    CHECK(reParsed.LoadFromFile(flattenedPdf.wstring(), &err));
    CHECK(reParsed.PageCount() == 2);
    if (reParsed.PageCount() >= 2) {
        CHECK(((reParsed.PageAt(0).rotationDeg % 360) + 360) % 360 == 90);
        const auto& p1Box = reParsed.PageAt(1).hasCropBox ? reParsed.PageAt(1).cropBox : reParsed.PageAt(1).mediaBox;
        CHECK(p1Box.w < reParsed.PageAt(1).mediaBox.w * 0.86);
    }

    RasterImageBgra32 winrtRaster;
    CHECK(WinRtPdfRenderer::RenderPageToBgra32(flattenedPdf.wstring(), 0, 1000, 0, NormalizedCropRect{}, winrtRaster, nullptr, &err));
    CHECK(winrtRaster.Valid());
    CHECK(winrtRaster.width > winrtRaster.height); // page 0 rotated 90 deg portrait -> landscape!

    std::size_t redInkPixels = 0;
    std::size_t greenImgPixels = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(winrtRaster.width) * winrtRaster.height; ++i) {
        const std::uint8_t b = winrtRaster.pixels[i * 4 + 0];
        const std::uint8_t g = winrtRaster.pixels[i * 4 + 1];
        const std::uint8_t r = winrtRaster.pixels[i * 4 + 2];
        if (r > 180 && g < 80 && b < 80) ++redInkPixels;
        if (g > 150 && r < 60 && b < 80) ++greenImgPixels;
    }
    std::cout << "  Windows.Data.Pdf flattened check -> rotated=" << winrtRaster.width << "x" << winrtRaster.height
              << ", red_ink_px=" << redInkPixels << ", green_xobj_px=" << greenImgPixels << "\n";
    CHECK(redInkPixels > 1200);
    CHECK(greenImgPixels > 800);

    // Merge split + flattened -> 4 pages
    const fs::path mergedPdf = tmpDir / L"cw_merged.pdf";
    CHECK(PdfWriter::MergePdfFiles({ splitPath.wstring(), flattenedPdf.wstring() }, mergedPdf.wstring(), &err));
    PdfDocument mergedParsed;
    CHECK(mergedParsed.LoadFromFile(mergedPdf.wstring(), &err));
    CHECK(mergedParsed.PageCount() == 4);

    // Verify ALL real CASIO PDFs in manualsDir load cleanly (including Standard R=2/3/4 encrypted manuals)
    int manualPdfCount = 0;
    if (fs::exists(manualsDir)) {
        for (const auto& entry : fs::directory_iterator(manualsDir)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != L".pdf" && entry.path().extension() != L".PDF") continue;
            ++manualPdfCount;
            PdfDocument mDoc;
            std::string mErr;
            const bool ok = mDoc.LoadFromFile(entry.path().wstring(), &mErr);
            std::cout << "  Manual PDF: " << WideToUtf8(entry.path().filename().wstring())
                      << " ok=" << ok << " pages=" << mDoc.PageCount();
            if (!ok) std::cout << " err=" << mErr;
            std::cout << "\n";
            CHECK(ok);
            CHECK(mDoc.PageCount() > 0);
        }
    }
    CHECK(manualPdfCount >= 16);

    for (int p = 0; p < 6; ++p) {
        RasterImageBgra32 pgImg;
        std::string pgErr;
        const bool pOk = WinRtPdfRenderer::RenderPageToBgra32(srcPdf.wstring(), p, 640, 0, NormalizedCropRect{}, pgImg, nullptr, &pgErr);
        std::size_t nonWhite = 0;
        if (pOk && pgImg.Valid()) {
            for (std::size_t k = 0; k < static_cast<std::size_t>(pgImg.width) * pgImg.height; ++k) {
                if (pgImg.pixels[k * 4 + 0] < 245 || pgImg.pixels[k * 4 + 1] < 245 || pgImg.pixels[k * 4 + 2] < 245) ++nonWhite;
            }
        }
        std::cout << "  WinRT Render fx-570CW p=" << p << " ok=" << pOk << " valid=" << pgImg.Valid()
                  << " size=" << pgImg.width << "x" << pgImg.height << " nonWhite=" << nonWhite
                  << " err=\"" << pgErr << "\"\n";
        CHECK(pOk && pgImg.Valid());
        CHECK(nonWhite > 100);
    }
}

void TestF5TextExtractionReplacementAndOcr(const fs::path& manualsDir, const fs::path& tmpDir) {
    std::cout << "-- Test F5: PDF Text Extraction, Ligatures, Honest Replacement Check, Offline OCR --\n";
    CHECK(NormalizeLigaturesUtf8("Con\xEF\xAC\x81gura \xEF\xAC\x82ujo") == "Configura flujo");

    // Generate clean vector PDF with known Helvetica text string
    DocumentModel txtDoc;
    AnnotationObject tb;
    tb.kind = ObjectKind::TextBox;
    tb.bounds = { 60.0, 80.0, 420.0, 40.0 };
    tb.fontSizePt = 18.0;
    tb.textUtf8 = "Casio FX-991CW Manual";
    txtDoc.AddPageObject(0, tb);

    std::vector<std::uint8_t> genBytes;
    std::string err;
    CHECK(PdfWriter::ExportDocumentToBytes(txtDoc, genBytes, &err));

    PdfDocument parsedGen;
    CHECK(parsedGen.LoadFromBytes(genBytes, &err));
    CHECK(parsedGen.PageCount() == 1);
    std::cout << "  Extracted generated PDF text: \"" << parsedGen.PageAt(0).extractedTextUtf8 << "\"\n";
    CHECK(parsedGen.PageAt(0).extractedTextUtf8.find("Casio FX-991CW Manual") != std::string::npos);

    const auto capDirect = parsedGen.InspectTextReplacement(0, "Casio FX-991CW Manual");
    CHECK(capDirect.supportedDirectStream);

    std::vector<std::uint8_t> replacedBytes;
    CHECK(PdfWriter::TryDirectReplaceTextInPdfBytes(genBytes, 0, "Casio FX-991CW Manual", "Casio FX-CG50 Guia", replacedBytes, &err));
    PdfDocument reParsed;
    CHECK(reParsed.LoadFromBytes(replacedBytes, &err));
    CHECK(reParsed.PageAt(0).extractedTextUtf8.find("Casio FX-CG50 Guia") != std::string::npos);

    // Check real CASIO manual extraction / honest unsupported stream check
    const fs::path casioPdf = manualsDir / L"fx-570CW_991CW_ES.pdf";
    PdfDocument casioDoc;
    if (casioDoc.LoadFromFile(casioPdf.wstring(), &err)) {
        std::size_t nonEmptyTextPages = 0;
        for (std::size_t i = 0; i < (std::min<std::size_t>)(casioDoc.PageCount(), 8u); ++i) {
            if (!casioDoc.PageAt(i).extractedTextUtf8.empty()) ++nonEmptyTextPages;
        }
        std::cout << "  fx-570CW_991CW_ES.pdf pages with extracted text in first 8 pages: " << nonEmptyTextPages << "\n";
        const auto honestCap = casioDoc.InspectTextReplacement(0, "ImpossibleNonMatchingSubsetGlyphXYZ");
        CHECK(!honestCap.supportedDirectStream);
        CHECK(honestCap.supportedVisualRedaction);
    }

    // Offline Windows.Media.Ocr test on crisp high-contrast DirectWrite render
    const auto langs = WinRtOcrAdapter::ListAvailableLanguageTags();
    std::cout << "  Windows.Media.Ocr installed languages: " << langs.size() << "\n";
    if (!langs.empty()) {
        DocumentModel ocrDoc;
        AnnotationObject headline;
        headline.kind = ObjectKind::TextBox;
        headline.bounds = { 60.0, 120.0, 480.0, 80.0 };
        headline.textUtf8 = "CALCULADORA\nCASIO 2026";
        headline.fontSizePt = 36.0;
        headline.bold = true;
        headline.strokeColor = DocColor::RGBA(0, 0, 0, 255);
        ocrDoc.AddPageObject(0, headline);

        D2DRenderer d2d;
        RasterImageBgra32 img;
        CHECK(d2d.RenderPageOffscreen(ocrDoc, 0, 1200, nullptr, img));
        const OcrOutput ocr = WinRtOcrAdapter::RecognizeBgra32(img.width, img.height, img.pixels.data());
        std::cout << "  OCR output (" << ocr.languageTagUtf8 << "): \"" << ocr.fullTextUtf8 << "\"\n";
        CHECK(ocr.engineAvailable);
        CHECK(ocr.fullTextUtf8.find("CASIO") != std::string::npos || ocr.fullTextUtf8.find("CALCULADORA") != std::string::npos);
    }
    (void)tmpDir;
}

void TestF6InfiniteCanvas100kQuadtree(const fs::path& tmpDir) {
    std::cout << "-- Test F6: Infinite Canvas Quadtree 100,000 Objects Benchmark & Export --\n";
    DocumentModel canvasDoc;
    canvasDoc.ResetToBlank(DocumentMode::InfiniteCanvas);

    // 100,000 objects arranged over [-50,000 .. +50,000]
    for (int i = 0; i < 100000; ++i) {
        AnnotationObject obj;
        obj.id = static_cast<std::uint64_t>(i + 1);
        obj.zOrder = obj.id;
        obj.kind = ObjectKind::Shape;
        obj.shapeKind = ShapeKind::Rectangle;
        const int gx = i % 316;
        const int gy = i / 316;
        obj.bounds = { -45000.0 + gx * 280.0, -45000.0 + gy * 280.0, 40.0, 30.0 };
        obj.strokeColor = DocColor::RGBA(37, 99, 235, 255);
        canvasDoc.AddCanvasObject(std::move(obj));
    }
    CHECK(canvasDoc.CanvasQuadtreeSize() == 100000);

    const DocRect query{ -500.0, -500.0, 1400.0, 1400.0 };
    std::vector<std::uint64_t> brute;
    for (const auto& o : canvasDoc.CanvasObjects()) {
        if (o.bounds.Intersects(query)) brute.push_back(o.id);
    }
    std::sort(brute.begin(), brute.end());

    const auto t0 = std::chrono::steady_clock::now();
    std::vector<std::uint64_t> quadRes;
    for (int iter = 0; iter < 100; ++iter) {
        quadRes = canvasDoc.QueryCanvasRect(query);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double usPerQuery = std::chrono::duration<double, std::micro>(t1 - t0).count() / 100.0;
    std::sort(quadRes.begin(), quadRes.end());
    std::cout << "  100k Quadtree query returned " << quadRes.size() << " matches (brute=" << brute.size()
              << "), avg=" << usPerQuery << " us/query\n";
    CHECK(quadRes == brute);

    // Region export to PDF
    const fs::path canvasPdf = tmpDir / L"canvas_region.pdf";
    CanvasPdfExportOptions opts;
    opts.paginated = false;
    opts.hasCustomRegion = true;
    opts.customRegionWorld = query;
    std::string err;
    CHECK(PdfWriter::ExportInfiniteCanvasToFile(canvasDoc, opts, canvasPdf.wstring(), &err));

    RasterImageBgra32 img;
    CHECK(WinRtPdfRenderer::RenderPageToBgra32(canvasPdf.wstring(), 0, 800, 0, NormalizedCropRect{}, img, nullptr, &err));
    CHECK(img.Valid());
}

void TestF7Pkcs7SignatureAndOpenXml(const fs::path& tmpDir) {
    std::cout << "-- Test F7: PKCS#7 SHA-256 PDF Signature + Tamper Rejection + DOCX/PPTX Round-trip --\n";
    DocumentModel doc;
    doc.SetTitle("Documento Oficial CASIO \xC3\x91");
    AnnotationObject tb;
    tb.kind = ObjectKind::TextBox;
    tb.bounds = { 60.0, 60.0, 360.0, 80.0 };
    tb.textUtf8 = "An\xC3\xA1lisis num\xC3\xA9rico con calculadora fx-991CW";
    doc.AddPageObject(0, tb);

    AnnotationObject img;
    img.kind = ObjectKind::Image;
    img.bounds = { 60.0, 160.0, 120.0, 80.0 };
    img.pngBytes = MakeSolidPng(64, 48, 37, 99, 235);
    doc.AddPageObject(0, img);

    const fs::path signedPdf = tmpDir / L"signed_test.pdf";
    PdfSignatureOptions sopts;
    sopts.signerNameUtf8 = "Ing. Prueba";
    sopts.reasonUtf8 = "Validacion Cryptografica";
    sopts.addVisualStamp = true;
    std::string err;
    CHECK(PdfSigner::ExportAndSignDocument(doc, sopts, signedPdf.wstring(), L"", L"", &err));

    const auto vGood = PdfSigner::VerifyPdfFile(signedPdf.wstring());
    std::cout << "  Signature Verify -> hasSig=" << vGood.hasSignature << " byteRangeOk=" << vGood.byteRangeValid
              << " cryptoValid=" << vGood.cryptographicValid << " subj=\"" << vGood.signerSubjectUtf8 << "\"\n";
    CHECK(vGood.hasSignature);
    CHECK(vGood.byteRangeValid);
    CHECK(vGood.cryptographicValid);

    // Tamper test: modify 1 byte near header after PDF signature sealed
    std::vector<std::uint8_t> tampered;
    {
        HANDLE hf = CreateFileW(signedPdf.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        CHECK(hf != INVALID_HANDLE_VALUE);
        LARGE_INTEGER li{};
        GetFileSizeEx(hf, &li);
        tampered.resize(static_cast<std::size_t>(li.QuadPart));
        DWORD rd = 0;
        ReadFile(hf, tampered.data(), static_cast<DWORD>(tampered.size()), &rd, nullptr);
        CloseHandle(hf);
    }
    const std::string asStr(reinterpret_cast<const char*>(tampered.data()), tampered.size());
    const std::size_t objPos = asStr.find("endobj");
    CHECK(objPos != std::string::npos && objPos > 10);
    tampered[objPos - 4] ^= 0x01u;
    const auto vTampered = PdfSigner::VerifyPdfBytes(tampered);
    CHECK(vTampered.hasSignature);
    CHECK(!vTampered.cryptographicValid); // MUST reject tampered PDF!

    // Bidirectional OpenXML .docx & .pptx round-trip
    const fs::path docxPath = tmpDir / L"exportado.docx";
    const fs::path pptxPath = tmpDir / L"exportado.pptx";
    std::vector<std::vector<std::uint8_t>> snaps = { MakeSolidPng(120, 160, 200, 220, 240) };
    CHECK(OpenXmlInterop::ExportToDocxFile(doc, docxPath.wstring(), snaps, &err));
    CHECK(OpenXmlInterop::ExportToPptxFile(doc, pptxPath.wstring(), snaps, &err));

    DocumentModel fromDocx, fromPptx;
    CHECK(OpenXmlInterop::ImportFromDocxFile(docxPath.wstring(), fromDocx, &err));
    CHECK(OpenXmlInterop::ImportFromPptxFile(pptxPath.wstring(), fromPptx, &err));
    CHECK(!fromDocx.Pages().empty());
    CHECK(fromDocx.Pages()[0].extractedTextUtf8.find("fx-991CW") != std::string::npos);
    CHECK(!fromPptx.Pages().empty());
    CHECK(fromPptx.Pages()[0].extractedTextUtf8.find("fx-991CW") != std::string::npos);
}

} // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    const fs::path manualsDir = (argc >= 2) ? fs::path(argv[1]) : fs::path("manuales");
    const fs::path keepDir = (argc >= 3) ? fs::path(argv[2]) : fs::path{};
    ScopedTempDir tmp(keepDir);

    TestF1CudocAndCommandStack(tmp.dir);
    TestF2LayoutCameraAndLruCache();
    TestF3D2DRenderAndMultiPressureInk();
    TestF4RealCasioPdfEditMergeSplitFlatten(manualsDir, tmp.dir);
    TestF5TextExtractionReplacementAndOcr(manualsDir, tmp.dir);
    TestF6InfiniteCanvas100kQuadtree(tmp.dir);
    TestF7Pkcs7SignatureAndOpenXml(tmp.dir);

    WinRtPdfRenderer::ClearOpenDocumentCache();

    if (gFailures == 0) {
        std::cout << "\nALL DocumentCoreTest CHECKS PASSED (F1-F7)\n";
        return 0;
    }
    std::cerr << "\nDocumentCoreTest FAILED with " << gFailures << " failure(s)\n";
    return 1;
}
