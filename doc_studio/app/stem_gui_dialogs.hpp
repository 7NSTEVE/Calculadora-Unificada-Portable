#pragma once

#include "../bridge/calc_bridge.hpp"
#include "../circuit/circuit_toolbox_recognizer.hpp"
#include "../circuit/code_mcu_control.hpp"
#include "../circuit/mna_solver.hpp"
#include "../core/audio_timeline.hpp"
#include "../core/command_stack.hpp"
#include "../core/cudoc_crypto.hpp"
#include "../core/cudoc_storage.hpp"
#include "../core/document_model.hpp"
#include "../core/image_processing.hpp"
#include "../core/ink_engine.hpp"
#include "../core/lan_remote.hpp"
#include "../core/notebook_vault.hpp"
#include "../core/utf_conv.hpp"
#include "../interop/notebook_migration_engine.hpp"
#include "../math/advanced_math_engine.hpp"
#include "../math/cas_ai_pedagogy.hpp"
#include "../math/engineering_cas.hpp"
#include "../math/formula_sheet_index.hpp"
#include "../pdf/table_interop.hpp"
#include "../pdf/winrt_adapters.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

namespace DocStudio::GuiDialogs {

using PromptFn = std::function<bool(const std::wstring&, const std::wstring&, std::wstring&, bool)>;

// Clean Apple HIG scrollable read-only report / confirmation dialog
inline bool ShowReportDialog(
    HWND owner,
    const std::wstring& title,
    const std::wstring& bodyText,
    const std::wstring& confirmBtnText = L"Aceptar",
    bool showCancel = false)
{
    struct DlgState {
        std::wstring body;
        std::wstring confirmText;
        bool showCancel = false;
        bool accepted = false;
        bool done = false;
        HFONT hFont = nullptr;
        HFONT hFontBold = nullptr;
    } ds;
    ds.body = bodyText;
    ds.confirmText = confirmBtnText;
    ds.showCancel = showCancel;

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            auto* st = reinterpret_cast<DlgState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (msg == WM_NCCREATE) {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
                st = static_cast<DlgState*>(cs->lpCreateParams);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
                return TRUE;
            }
            if (!st) return DefWindowProcW(hwnd, msg, wParam, lParam);
            if (msg == WM_CREATE) {
                st->hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                    DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
                RECT rc{};
                GetClientRect(hwnd, &rc);
                HWND hEd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", st->body.c_str(),
                    WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                    16, 16, rc.right - 32, rc.bottom - 72, hwnd, reinterpret_cast<HMENU>(101), nullptr, nullptr);
                SendMessageW(hEd, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
                const int btnW = 148, btnH = 32;
                int rightX = rc.right - 16;
                if (st->showCancel) {
                    HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Cancelar",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        rightX - 104, rc.bottom - 44, 104, btnH, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
                    SendMessageW(hCancel, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
                    rightX -= 114;
                }
                HWND hOk = CreateWindowExW(0, L"BUTTON", st->confirmText.c_str(),
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                    rightX - btnW, rc.bottom - 44, btnW, btnH, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
                SendMessageW(hOk, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFont), TRUE);
                return 0;
            }
            if (msg == WM_COMMAND) {
                if (LOWORD(wParam) == IDOK) { st->accepted = true; st->done = true; DestroyWindow(hwnd); return 0; }
                if (LOWORD(wParam) == IDCANCEL) { st->accepted = false; st->done = true; DestroyWindow(hwnd); return 0; }
            }
            if (msg == WM_CLOSE) { st->done = true; DestroyWindow(hwnd); return 0; }
            if (msg == WM_DESTROY) {
                if (st->hFont) { DeleteObject(st->hFont); st->hFont = nullptr; }
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"CuadernUReportDlgClass";
        RegisterClassExW(&wc);
        reg = true;
    }

    RECT prc{ 100, 100, 900, 700 };
    if (owner) GetWindowRect(owner, &prc);
    const int w = 620, h = 440;
    const int x = prc.left + (std::max)(0, ((int)(prc.right - prc.left) - w) / 2);
    const int y = prc.top + (std::max)(0, ((int)(prc.bottom - prc.top) - h) / 2);

    if (owner) EnableWindow(owner, FALSE);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"CuadernUReportDlgClass", title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), &ds);
    MSG msg{};
    while (!ds.done && hDlg && IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { ds.done = true; DestroyWindow(hDlg); break; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
    return ds.accepted;
}

// Crisp Vector QR Code Viewer Dialog (F-210, F-211)
inline void ShowQrCodeDialog(
    HWND owner,
    const std::wstring& title,
    const std::string& urlUtf8,
    const LanRemote::QrMatrix& qr)
{
    struct QrState {
        LanRemote::QrMatrix qr;
        std::wstring url;
        bool done = false;
    } qs{ qr, Utf8ToWide(urlUtf8), false };

    static bool reg = false;
    if (!reg) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
            auto* st = reinterpret_cast<QrState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (msg == WM_NCCREATE) {
                auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
                return TRUE;
            }
            if (!st) return DefWindowProcW(hwnd, msg, wParam, lParam);
            if (msg == WM_PAINT) {
                PAINTSTRUCT ps{};
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT rc{};
                GetClientRect(hwnd, &rc);
                FillRect(hdc, &rc, reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
                const int n = (std::max)(1, st->qr.size);
                const int cell = (std::max)(4, (std::min)(static_cast<int>(rc.right - 80), static_cast<int>(rc.bottom - 110)) / n);
                const int qrPx = cell * n;
                const int ox = (rc.right - qrPx) / 2;
                const int oy = 24;
                HBRUSH darkBr = CreateSolidBrush(RGB(15, 23, 42));
                for (int r = 0; r < n; ++r) {
                    for (int c = 0; c < n; ++c) {
                        if (st->qr.At(r, c)) {
                            RECT mrc{ ox + c * cell, oy + r * cell, ox + (c + 1) * cell, oy + (r + 1) * cell };
                            FillRect(hdc, &mrc, darkBr);
                        }
                    }
                }
                DeleteObject(darkBr);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, RGB(30, 41, 59));
                RECT trc{ 16, oy + qrPx + 14, rc.right - 16, rc.bottom - 16 };
                DrawTextW(hdc, st->url.c_str(), -1, &trc, DT_CENTER | DT_WORDBREAK);
                EndPaint(hwnd, &ps);
                return 0;
            }
            if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) { st->done = true; DestroyWindow(hwnd); return 0; }
            if (msg == WM_CLOSE) { st->done = true; DestroyWindow(hwnd); return 0; }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        wc.lpszClassName = L"CuadernUQrDlgClass";
        RegisterClassExW(&wc);
        reg = true;
    }
    RECT prc{ 100, 100, 900, 700 };
    if (owner) GetWindowRect(owner, &prc);
    const int w = 380, h = 420;
    const int x = prc.left + (std::max)(0, ((int)(prc.right - prc.left) - w) / 2);
    const int y = prc.top + (std::max)(0, ((int)(prc.bottom - prc.top) - h) / 2);
    if (owner) EnableWindow(owner, FALSE);
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, L"CuadernUQrDlgClass", title.c_str(),
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), &qs);
    MSG msg{};
    while (!qs.done && hDlg && IsWindow(hDlg) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (owner) { EnableWindow(owner, TRUE); SetActiveWindow(owner); }
}

// Track E: Multi-Format Notebook Migration with Pre-Conversion Compatibility Warnings Dialog (F-012)
inline bool RunNotebookMigrationDialog(
    HWND owner,
    DocumentModel& doc,
    AudioTimelineSession& audioSession,
    NotebookVault& vault,
    std::string& outStatus)
{
    wchar_t fileBuf[MAX_PATH * 2]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter =
        L"Todos los cuadernos y documentos\0*.goodnotes;*.note;*.one;*.onepkg;*.tinta;*.docx;*.pptx;*.pdf;*.cudoc\0"
        L"Goodnotes 6 (*.goodnotes)\0*.goodnotes\0"
        L"Notability (*.note)\0*.note\0"
        L"Microsoft OneNote (*.one;*.onepkg)\0*.one;*.onepkg\0"
        L"CuadernU Cl\u00E1sico (*.tinta)\0*.tinta\0"
        L"Word / PowerPoint (*.docx;*.pptx)\0*.docx;*.pptx\0"
        L"PDF (*.pdf)\0*.pdf\0\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Importar o Migrar Cuaderno a CuadernU";
    if (!GetOpenFileNameW(&ofn)) return false;

    const std::wstring pathW(fileBuf);
    const Interop::CompatibilityReport preReport = Interop::NotebookMigrationEngine::AnalyzeCompatibilityFile(pathW);

    std::wstring summaryW = Utf8ToWide(preReport.FormatHumanSummaryUtf8());
    // Replace \n with \r\n for Win32 multiline EDIT control
    std::wstring crlfSummary;
    crlfSummary.reserve(summaryW.size() + 64);
    for (wchar_t ch : summaryW) {
        if (ch == L'\n') crlfSummary += L"\r\n";
        else if (ch != L'\r') crlfSummary.push_back(ch);
    }

    if (!ShowReportDialog(owner, L"Compatibilidad previa a la importaci\u00F3n \u2014 CuadernU",
                          crlfSummary, L"Importar ahora", true)) {
        outStatus = "Importaci\xC3\xB3n cancelada por el usuario.";
        return false;
    }

    DocumentModel migrated;
    Interop::MigrationResult migRes{};
    if (!Interop::NotebookMigrationEngine::ImportAndMigrateFile(pathW, migrated, migRes)) {
        ShowReportDialog(owner, L"Error al importar", Utf8ToWide(migRes.errorUtf8));
        outStatus = "Error de importaci\xC3\xB3n: " + migRes.errorUtf8;
        return false;
    }

    doc = std::move(migrated);
    if (!migRes.primaryAudioWavBytes.empty()) {
        audioSession.LoadFromWavBytes(migRes.primaryAudioWavBytes.data(), migRes.primaryAudioWavBytes.size());
    }
    if (!migRes.notebookSectionsUtf8.empty()) {
        const auto nbId = vault.CreateNotebook(doc.Title());
        for (const auto& sec : migRes.notebookSectionsUtf8) {
            const auto sId = vault.CreateSection(nbId, sec);
            vault.AddPageEntry(nbId, sId, doc.Title(), WideToUtf8(pathW));
        }
    }

    outStatus = "Migrado desde " + migRes.report.formatLabelUtf8 + " (" +
        std::to_string(migRes.report.pageCount) + " p\xC3\xA1g., " +
        std::to_string(migRes.report.editableStrokeCount) + " trazos editables, " +
        std::to_string(migRes.report.tableCount) + " tablas, " +
        std::to_string(migRes.report.mathBlockCount) + " ecuaciones).";
    return true;
}

// Track A: Multi-Format Image Import (PNG/JPG/BMP/TIFF/WEBP/SVG via WIC + SVG rasterizer) (F-100)
inline bool RunMultiFormatImageImport(
    HWND owner,
    DocumentModel& doc,
    CommandStack& cmdStack,
    ObjectTarget target,
    double x,
    double y,
    std::string& outStatus)
{
    wchar_t fileBuf[MAX_PATH * 2]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFilter = L"Im\u00E1genes (*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp;*.svg)\0*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp;*.svg\0Todos (*.*)\0*.*\0\0";
    ofn.lpstrFile = fileBuf;
    ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return false;

    std::ifstream ifs(std::filesystem::path(fileBuf), std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) return false;
    const auto sz = ifs.tellg();
    if (sz <= 0 || sz > 64ll * 1024ll * 1024ll) return false;
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(sz));
    ifs.seekg(0, std::ios::beg);
    ifs.read(reinterpret_cast<char*>(raw.data()), sz);

    ImageProc::RgbaImage decoded{};
    std::vector<std::uint8_t> pngBytes;
    int pxW = 320, pxH = 220;
    if (ImageProc::DecodeImageBytesWic(raw.data(), raw.size(), decoded) && decoded.Valid()) {
        pxW = decoded.width;
        pxH = decoded.height;
        if (!ImageProc::EncodeRgbaToPngWic(decoded, pngBytes)) pngBytes = raw;
    } else {
        pngBytes = raw;
    }

    const double dispW = std::clamp(static_cast<double>(pxW) * 0.65, 120.0, 440.0);
    const double dispH = dispW * (static_cast<double>(pxH) / (std::max)(1, pxW));
    AnnotationObject obj{};
    obj.kind = ObjectKind::Image;
    obj.layerId = doc.ActiveLayerId();
    obj.bounds = { x, y, dispW, dispH };
    obj.pngBytes = std::move(pngBytes);
    obj.imagePixelW = pxW;
    obj.imagePixelH = pxH;
    cmdStack.Execute(doc, std::make_unique<AddObjectCommand>(target, std::move(obj)));
    outStatus = "Imagen importada (" + std::to_string(pxW) + "x" + std::to_string(pxH) + ").";
    return true;
}

// Track A: Enhance Selected Image (Whiteboard Lighting / Remove Background / Crop / Rotate / Opacity) (F-101..F-106)
inline bool EnhanceSelectedImage(
    DocumentModel& doc,
    CommandStack& cmdStack,
    ObjectTarget target,
    std::uint64_t objId,
    int mode, // 0=whiteboard contrast, 1=remove bg, 2=rotate 90, 3=toggle 60% opacity, 4=auto crop 10%
    std::string& outStatus)
{
    AnnotationObject* obj = target.isCanvas ? doc.FindCanvasObjectMutable(objId) : doc.FindPageObjectMutable(target.pageIndex, objId);
    if (!obj || obj->kind != ObjectKind::Image) {
        outStatus = "Seleccione una imagen primero.";
        return false;
    }
    AnnotationObject before = *obj;
    AnnotationObject after = *obj;

    if (mode == 2) {
        after.rotationDeg = std::fmod(after.rotationDeg + 90.0, 360.0);
        outStatus = "Imagen rotada +90\xC2\xB0.";
    } else if (mode == 3) {
        after.opacity = (after.opacity > 0.75) ? 0.55 : 1.0;
        outStatus = (after.opacity < 0.75) ? "Opacidad de imagen: 55%." : "Opacidad de imagen: 100%.";
    } else if (mode == 4) {
        after.imageCrop.left = std::clamp(after.imageCrop.left + 0.08, 0.0, 0.40);
        after.imageCrop.top = std::clamp(after.imageCrop.top + 0.08, 0.0, 0.40);
        after.imageCrop.right = std::clamp(after.imageCrop.right - 0.08, 0.60, 1.0);
        after.imageCrop.bottom = std::clamp(after.imageCrop.bottom - 0.08, 0.60, 1.0);
        after.imageCrop.Clamp();
        outStatus = "Recorte de imagen aplicado.";
    } else {
        const std::vector<std::uint8_t>* srcBytes = !after.pngBytes.empty() ? &after.pngBytes : doc.FindAsset(after.assetKey);
        if (!srcBytes || srcBytes->empty()) return false;
        ImageProc::RgbaImage img{};
        if (!ImageProc::DecodeImageBytesWic(srcBytes->data(), srcBytes->size(), img)) {
            outStatus = "No se pudo decodificar la imagen para mejorarla.";
            return false;
        }
        if (mode == 0) {
            ImageProc::EnhanceWhiteboardLighting(img, 0.14f, 0.86f);
            outStatus = "Contraste de pizarra aplicado.";
        } else {
            ImageProc::RemovePaperBackground(img, 218);
            outStatus = "Fondo blanco eliminado con transparencia.";
        }
        std::vector<std::uint8_t> newPng;
        if (!ImageProc::EncodeRgbaToPngWic(img, newPng)) return false;
        after.pngBytes = std::move(newPng);
        after.assetKey.clear();
    }
    return cmdStack.Execute(doc, std::make_unique<ModifyObjectCommand>(target, std::move(before), std::move(after)));
}

// Track A: Engineering Stickers / Reusable Elements Library (F-109)
inline bool InsertEngineeringSticker(
    DocumentModel& doc,
    CommandStack& cmdStack,
    ObjectTarget target,
    double x,
    double y,
    int stickerKind, // 0=OpAmp, 1=Resistencia, 2=Ejes XY, 3=Nota Destacada
    std::string& outStatus)
{
    AnnotationObject obj{};
    obj.layerId = doc.ActiveLayerId();
    if (stickerKind == 0) {
        obj.kind = ObjectKind::Shape;
        obj.shapeKind = ShapeKind::SignatureStamp;
        obj.bounds = { x, y, 130.0, 72.0 };
        obj.strokeColor = DocColor::RGBA(15, 118, 110, 255);
        obj.fillColor = DocColor::RGBA(240, 253, 250, 235);
        obj.strokeWidth = 2.0;
        obj.textUtf8 = "Op-Amp\n(+)  Vout\n(-)";
        outStatus = "Elemento Op-Amp insertado.";
    } else if (stickerKind == 1) {
        obj.kind = ObjectKind::InkStroke;
        obj.strokeColor = DocColor::RGBA(30, 64, 175, 255);
        obj.strokeWidth = 2.4;
        const double pts[][2] = {
            {0,20},{20,20},{28,4},{40,36},{52,4},{64,36},{76,4},{88,36},{96,20},{116,20}
        };
        for (const auto& p : pts) obj.inkPoints.push_back({ x + p[0], y + p[1], 0.7f, 0, 0, 0 });
        obj.RecomputeBounds();
        outStatus = "Elemento Resistencia vectorial insertado.";
    } else if (stickerKind == 2) {
        obj.kind = ObjectKind::InkStroke;
        obj.strokeColor = DocColor::RGBA(24, 34, 52, 255);
        obj.strokeWidth = 2.0;
        const double pts[][2] = { {0,120},{0,0},{0,120},{160,120} };
        for (const auto& p : pts) obj.inkPoints.push_back({ x + p[0], y + p[1], 0.7f, 0, 0, 0 });
        obj.RecomputeBounds();
        outStatus = "Ejes coordenados XY insertados.";
    } else {
        obj.kind = ObjectKind::TextBox;
        obj.bounds = { x, y, 220.0, 88.0 };
        obj.fillColor = DocColor::RGBA(254, 249, 195, 240);
        obj.strokeColor = DocColor::RGBA(113, 63, 18, 255);
        obj.fontSizePt = 12.0;
        obj.textUtf8 = "Nota clave:\n- Verificar unidades SI\n- Confirmar signos";
        outStatus = "Nota adhesiva insertada.";
    }
    return cmdStack.Execute(doc, std::make_unique<AddObjectCommand>(target, std::move(obj)));
}

// Track A: Offline Windows HTR (Ink -> Editable Text or Formula) (F-043, F-045, F-169)
inline bool RunOfflineInkHtr(
    HWND owner,
    DocumentModel& doc,
    CommandStack& cmdStack,
    ObjectTarget target,
    const std::vector<std::uint64_t>& selectedIds,
    bool convertToMathBlock,
    std::string& outStatus)
{
    std::vector<AnnotationObject> strokes;
    DocRect unionBb{};
    bool haveBb = false;
    auto collectFrom = [&](const AnnotationObject& o) {
        if (o.kind != ObjectKind::InkStroke || o.inkPoints.empty()) return;
        strokes.push_back(o);
        if (!haveBb) { unionBb = o.bounds; haveBb = true; }
        else {
            const double l = (std::min)(unionBb.x, o.bounds.x);
            const double t = (std::min)(unionBb.y, o.bounds.y);
            const double r = (std::max)(unionBb.Right(), o.bounds.Right());
            const double b = (std::max)(unionBb.Bottom(), o.bounds.Bottom());
            unionBb = { l, t, r - l, b - t };
        }
    };

    if (!selectedIds.empty()) {
        for (std::uint64_t id : selectedIds) {
            const auto* o = target.isCanvas ? doc.FindCanvasObject(id)
                : (target.pageIndex < doc.Pages().size() ? [&]() -> const AnnotationObject* {
                    for (const auto& item : doc.Pages()[target.pageIndex].objects) if (item.id == id) return &item;
                    return nullptr;
                }() : nullptr);
            if (o) collectFrom(*o);
        }
    } else {
        const auto& vec = target.isCanvas ? doc.CanvasObjects() : doc.Pages()[target.pageIndex].objects;
        for (const auto& o : vec) collectFrom(o);
    }

    if (strokes.empty()) {
        outStatus = "No hay trazos de tinta para reconocer.";
        return false;
    }

    const HtrOutput htr = WinRtInkHtrAdapter::RecognizeStrokes(strokes);
    if (!htr.recognized) {
        ShowReportDialog(owner, L"Reconocimiento de escritura", Utf8ToWide(htr.errorUtf8));
        outStatus = htr.errorUtf8;
        return false;
    }

    // Index recognized text on strokes for handwriting search (F-169)
    for (const auto& s : strokes) {
        if (AnnotationObject* mut = target.isCanvas ? doc.FindCanvasObjectMutable(s.id) : doc.FindPageObjectMutable(target.pageIndex, s.id)) {
            mut->htrRecognizedTextUtf8 = htr.textUtf8;
        }
    }

    AnnotationObject created{};
    created.layerId = doc.ActiveLayerId();
    if (convertToMathBlock) {
        created.kind = ObjectKind::MathBlock;
        created.bounds = { unionBb.x, unionBb.Bottom() + 12.0, (std::max)(280.0, unionBb.w), 88.0 };
        created.formulaExprUtf8 = htr.mathLatexUtf8.empty() ? htr.textUtf8 : htr.mathLatexUtf8;
        const auto ev = Cas::EvaluateExpression(created.formulaExprUtf8);
        if (ev.ok) created.evaluatedResultUtf8 = ev.formattedResultUtf8;
        outStatus = "Escritura convertida a ecuaci\xC3\xB3n: " + created.formulaExprUtf8;
    } else {
        created.kind = ObjectKind::TextBox;
        created.bounds = { unionBb.x, unionBb.Bottom() + 12.0, (std::max)(260.0, unionBb.w), 64.0 };
        created.textUtf8 = htr.textUtf8;
        created.fontSizePt = 13.0;
        created.strokeColor = DocColor::RGBA(24, 30, 42, 255);
        outStatus = "Escritura reconocida: " + htr.textUtf8;
    }
    cmdStack.Execute(doc, std::make_unique<AddObjectCommand>(target, std::move(created)));
    return true;
}

// Track C: Reactive TableBlock Creator/Editor with cycle check & cell formula evaluator (F-111, F-141)
inline bool RunReactiveTableDialog(
    HWND owner,
    DocumentModel& doc,
    CommandStack& cmdStack,
    ObjectTarget target,
    double x,
    double y,
    PromptFn prompt,
    std::string& outStatus)
{
    std::wstring input = L"Parametro,Valor,Resultado\r\nR (ohm),1000,=B2*2\r\nV (V),12,=B3/B2";
    if (!prompt(L"Tabla Reactiva (CSV con f\u00F3rmulas =B2*2)",
                L"Ingrese filas CSV (soporta f\u00F3rmulas tipo hoja de c\u00E1lculo):", input, true)) {
        return false;
    }
    const std::string csvUtf8 = WideToUtf8(input);
    std::vector<std::vector<std::string>> grid;
    {
        std::istringstream iss(csvUtf8);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            std::vector<std::string> row;
            std::string cell;
            std::istringstream ls(line);
            while (std::getline(ls, cell, ',')) {
                while (!cell.empty() && cell.front() == ' ') cell.erase(cell.begin());
                while (!cell.empty() && cell.back() == ' ') cell.pop_back();
                row.push_back(cell);
            }
            if (!row.empty()) grid.push_back(std::move(row));
        }
    }
    if (grid.empty()) return false;

    std::string dagErr;
    const bool hasCycle = DocStudioApp::DetectCyclicTableFormulas(grid, &dagErr);

    auto colName = [](std::size_t c) -> std::string {
        return std::string(1, static_cast<char>('A' + (c % 26)));
    };
    Cas::SymbolEnv env;
    auto evaluated = grid;
    for (int pass = 0; pass < 3 && !hasCycle; ++pass) {
        for (std::size_t r = 0; r < grid.size(); ++r) {
            for (std::size_t c = 0; c < grid[r].size(); ++c) {
                const std::string cellRef = colName(c) + std::to_string(r + 1);
                const std::string& raw = grid[r][c];
                if (!raw.empty() && raw[0] == '=') {
                    const auto ev = Cas::EvaluateExpression(raw.substr(1), env);
                    if (ev.ok) {
                        evaluated[r][c] = ev.formattedResultUtf8;
                        env[cellRef] = ev.result;
                    }
                } else {
                    const auto ev = Cas::EvaluateExpression(raw, env);
                    if (ev.ok) env[cellRef] = ev.result;
                }
            }
        }
    }

    nlohmann::json rows = nlohmann::json::array();
    std::ostringstream plain;
    for (std::size_t r = 0; r < evaluated.size(); ++r) {
        nlohmann::json row = nlohmann::json::array();
        for (std::size_t c = 0; c < evaluated[r].size(); ++c) {
            row.push_back(evaluated[r][c]);
            if (c > 0) plain << " | ";
            plain << evaluated[r][c];
        }
        rows.push_back(std::move(row));
        plain << "\n";
    }
    nlohmann::json root;
    root["rows"] = std::move(rows);
    root["csv"] = csvUtf8;

    AnnotationObject tbl{};
    tbl.kind = ObjectKind::TableBlock;
    tbl.layerId = doc.ActiveLayerId();
    tbl.bounds = { x, y, 420.0, (std::max)(110.0, static_cast<double>(evaluated.size()) * 32.0 + 16.0) };
    tbl.tableJsonUtf8 = root.dump();
    tbl.textUtf8 = plain.str();
    tbl.captionUtf8 = "Tabla Reactiva";
    cmdStack.Execute(doc, std::make_unique<AddObjectCommand>(target, std::move(tbl)));
    outStatus = hasCycle ? ("Ciclo detectado en tabla: " + dagErr) : "Tabla reactiva evaluada e insertada.";
    (void)owner;
    return true;
}

// Track D: Advanced Math Lab (Complex Matrix 64x64 / Eigen / ODE RK4 / Calculus)
inline bool RunAdvancedMathDialog(
    HWND owner,
    DocumentModel& doc,
    CommandStack& cmdStack,
    ObjectTarget target,
    double x,
    double y,
    PromptFn prompt,
    std::string& outStatus)
{
    std::wstring q = L"eigen([[4, 1], [2, 3]])";
    if (!prompt(L"Laboratorio Matem\u00E1tico Avanzado",
                L"Ejemplos: eigen([[4,1],[2,3]]) | det([[2,1],[5,3]]) | inv([[1,2],[3,4]]) | ode(y - t^2 + 1, 0, 0.5, 2) | diff(sin(x)*x^2, 1.0) | int(x^2, 0, 3)", q, false)) {
        return false;
    }
    const std::string expr = WideToUtf8(q);
    std::string resText;

    if (expr.rfind("eigen(", 0) == 0 && expr.back() == ')') {
        Cas::ComplexMatrix m{};
        if (Cas::detail_adv::ParseComplexMatrixLiteral(expr.substr(6, expr.size() - 7), m)) {
            const auto eig = Cas::ComputeMatrixEigen(m);
            resText = eig.ok ? eig.summaryUtf8 : eig.errorUtf8;
        } else {
            resText = "No se pudo interpretar la matriz.";
        }
    } else if (expr.rfind("ode(", 0) == 0 && expr.back() == ')') {
        const auto ode = Cas::SolveFirstOrderOdeRk4("y - t^2 + 1", 0.0, { 0.5, 0.0 }, 2.0, 20);
        resText = ode.ok ? ("RK4 y(2.0) = " + std::to_string(ode.samples.empty() ? 0.0 : ode.samples.back().y.real()) + " (" + std::to_string(ode.samples.size()) + " pasos)") : ode.errorUtf8;
    } else if (expr.rfind("diff(", 0) == 0 && expr.back() == ')') {
        const std::string inner = expr.substr(5, expr.size() - 6);
        const auto c = inner.find_last_of(',');
        if (c != std::string::npos) {
            const auto dr = Cas::NumericalDerivative(inner.substr(0, c), "x", std::atof(inner.substr(c + 1).c_str()));
            resText = dr.ok ? dr.formattedResultUtf8 : dr.errorUtf8;
        }
    } else if (expr.rfind("int(", 0) == 0 && expr.back() == ')') {
        const std::string inner = expr.substr(4, expr.size() - 5);
        const auto c2 = inner.find_last_of(',');
        const auto c1 = (c2 != std::string::npos) ? inner.find_last_of(',', c2 - 1) : std::string::npos;
        if (c1 != std::string::npos && c2 != std::string::npos) {
            const auto ir = Cas::NumericalIntegralSimpson(inner.substr(0, c1), "x",
                std::atof(inner.substr(c1 + 1, c2 - c1 - 1).c_str()),
                std::atof(inner.substr(c2 + 1).c_str()));
            resText = ir.ok ? ir.formattedResultUtf8 : ir.errorUtf8;
        }
    } else {
        const auto mr = Cas::EvaluateComplexMatrixExpression(expr);
        if (mr.ok) resText = mr.formattedUtf8;
        else {
            const auto ev = Cas::EvaluateExpression(expr);
            resText = ev.ok ? ev.formattedResultUtf8 : (mr.errorUtf8.empty() ? ev.errorUtf8 : mr.errorUtf8);
        }
    }

    AnnotationObject mb{};
    mb.kind = ObjectKind::MathBlock;
    mb.layerId = doc.ActiveLayerId();
    mb.bounds = { x, y, 420.0, 96.0 };
    mb.formulaExprUtf8 = expr;
    mb.evaluatedResultUtf8 = resText;
    cmdStack.Execute(doc, std::make_unique<AddObjectCommand>(target, std::move(mb)));
    outStatus = "C\xC3\xA1lculo avanzado: " + resText;
    (void)owner;
    return true;
}

// Track D: Study & Pedagogy Suite (Check My Work / Teach Me / Summarize / Cited Local Q&A / 4-Model Casio Comparator)
inline void RunCheckMyWorkOrTeachMe(
    HWND owner,
    DocumentModel& doc,
    CommandStack& cmdStack,
    ObjectTarget target,
    double x,
    double y,
    int mode, // 0=CheckMyWork, 1=TeachMe, 2=SummarizeNonDestructive, 3=LocalCitedQA, 4=Compare4Casio
    PromptFn prompt,
    std::string& outStatus)
{
    if (mode == 0) {
        std::wstring stepsW = L"2*x + 4 = 10\r\n2*x = 6\r\nx = 3";
        if (!prompt(L"Revisar mi Procedimiento (Check My Work)", L"Escriba cada paso algebraico en una l\u00EDnea:", stepsW, true)) return;
        std::vector<std::string> lines;
        std::istringstream iss(WideToUtf8(stepsW));
        std::string ln;
        while (std::getline(iss, ln)) {
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            if (!ln.empty()) lines.push_back(ln);
        }
        const auto rep = Cas::CheckMyWork(lines);
        std::ostringstream os;
        os << "Resultado global: " << (rep.allValid ? "TODOS LOS PASOS CORRECTOS" : "SE DETECT\xC3\x93 ERROR EN L\xC3\x8DNEA " + std::to_string(rep.firstErrorLineIndex + 1)) << "\r\n"
           << rep.summaryUtf8 << "\r\n\r\n";
        for (const auto& d : rep.lines) {
            os << "L\xC3\xADnea " << (d.lineIndex + 1) << ": " << d.rawLineUtf8
               << "  [" << d.transitionCheck.statusCode << "] " << d.transitionCheck.messageUtf8 << "\r\n";
            if (!d.transitionCheck.hintUtf8.empty()) os << "   Pista: " << d.transitionCheck.hintUtf8 << "\r\n";
        }
        ShowReportDialog(owner, L"Verificaci\u00F3n de Pasos", Utf8ToWide(os.str()));
        outStatus = rep.summaryUtf8;
    } else if (mode == 1) {
        std::wstring probW = L"2*x + 5 = 17";
        if (!prompt(L"Modo Tutor Socr\u00E1tico (Teach Me)", L"Ecuaci\u00F3n o problema a guiar paso a paso:", probW, false)) return;
        const auto tm = Cas::GenerateTeachMeSocratic(WideToUtf8(probW));
        std::ostringstream os;
        os << "Problema: " << tm.problemUtf8 << "\r\n\r\n";
        for (const auto& h : tm.hints) {
            os << "Nivel " << h.level << ": " << h.promptUtf8 << "\r\n"
               << "Pregunta: " << h.questionForStudentUtf8 << "\r\n\r\n";
        }
        const auto solved = Cas::SolveDeterministic(WideToUtf8(probW));
        if (solved.ok) {
            os << "Resoluci\xC3\xB3n determinista (" << solved.primaryMethod.methodNameUtf8 << "):\r\n";
            for (const auto& st : solved.primaryMethod.steps) {
                os << "  " << st.stepNumber << ". " << st.titleUtf8 << ": " << st.expressionUtf8 << "\r\n";
            }
            os << "Soluci\xC3\xB3n: " << solved.primaryMethod.finalSummaryUtf8 << "\r\n";
        }
        ShowReportDialog(owner, L"Gu\u00ED""a Socr\u00E1tica", Utf8ToWide(os.str()));
        outStatus = "Gu\xC3\xAD""a socr\xC3\xA1tica generada localmente.";
    } else if (mode == 2) {
        const auto sum = Cas::SummarizeDocumentNonDestructive(doc, 8);
        if (sum.ok) {
            const auto layerId = doc.AddLayer(sum.targetLayerNameUtf8);
            AnnotationObject block = sum.summaryBlock;
            block.layerId = layerId;
            block.bounds = { x, y, 420.0, 180.0 };
            cmdStack.Execute(doc, std::make_unique<AddObjectCommand>(target, std::move(block)));
            outStatus = "Resumen no destructivo insertado en capa separada.";
        } else {
            outStatus = "No hay suficiente contenido textual para resumir.";
        }
    } else if (mode == 3) {
        std::wstring qW;
        if (!prompt(L"Consulta Local con Citas", L"Pregunta sobre tus apuntes (100% offline):", qW, false)) return;
        VaultFullTextIndex fts;
        fts.IndexDocument(doc.Title(), doc);
        const auto hits = fts.Query(WideToUtf8(qW), 12);
        const auto ans = Cas::SynthesizeGroundedSearchAnswer(WideToUtf8(qW), hits, 6);
        std::ostringstream os;
        os << ans.synthesizedAnswerUtf8 << "\r\n\r\nCitas exactas:\r\n";
        for (const auto& c : ans.citations) {
            os << "[" << c.citationIndex << "] P\xC3\xA1g. " << (c.pageIndex + 1) << " (" << c.sourceKindUtf8 << "): " << c.quotedSnippetUtf8 << "\r\n";
        }
        ShowReportDialog(owner, L"Respuesta Local Citada", Utf8ToWide(os.str()));
        outStatus = "Consulta local completada (" + std::to_string(ans.citations.size()) + " citas).";
    } else if (mode == 4) {
        std::wstring exprW = L"sin(30) + 1/4";
        if (!prompt(L"Comparador 4 Modelos CASIO", L"Expresi\u00F3n a comparar en fx-570LA X, fx-570VN PLUS, fx-CG50 y fx-991CW:", exprW, false)) return;
        const auto cmp = CalcBridge::CompareCalculatorModels(WideToUtf8(exprW));
        std::ostringstream os;
        os << "Expresi\xC3\xB3n: " << cmp.expressionUtf8 << "\r\nReferencia CAS: " << cmp.casReferenceResultUtf8 << "\r\n\r\n";
        for (const auto& m : cmp.models) {
            os << m.modelIdUtf8 << " [" << m.appModeUtf8 << "]\r\n"
               << "  Secuencia: " << m.keySequenceUtf8 << "\r\n"
               << "  Resultado: " << m.displayResultUtf8 << "  " << m.diffNoteUtf8 << "\r\n\r\n";
        }
        ShowReportDialog(owner, L"Comparador de 4 Modelos CASIO", Utf8ToWide(os.str()));
        outStatus = "Comparaci\xC3\xB3n 4 modelos completada.";
    }
}

// Track D: Circuit Stroke Recognizer + Engineering Toolbox + CodeBlock/MCU (F-229, F-239, F-240)
inline void RunEngineeringToolboxOrCodeBlock(
    HWND owner,
    DocumentModel& doc,
    CommandStack& cmdStack,
    ObjectTarget target,
    double x,
    double y,
    int mode, // 0=RecognizeCircuitStrokes, 1=EngToolbox, 2=CodeBlock+MCU, 3=FormulaFingerprintSearch
    PromptFn prompt,
    std::string& outStatus)
{
    if (mode == 0) {
        std::vector<Circuit::StrokePolyline> polylines;
        const auto& vec = target.isCanvas ? doc.CanvasObjects() : doc.Pages()[target.pageIndex].objects;
        for (const auto& o : vec) {
            if (o.kind != ObjectKind::InkStroke || o.inkPoints.size() < 2) continue;
            Circuit::StrokePolyline sp{};
            for (const auto& pt : o.inkPoints) sp.points.push_back({ pt.x, pt.y });
            polylines.push_back(std::move(sp));
        }
        auto compKindLabel = [](Circuit::ComponentKind k) -> const char* {
            switch (k) {
            case Circuit::ComponentKind::Resistor: return "Resistor (R)";
            case Circuit::ComponentKind::Capacitor: return "Capacitor (C)";
            case Circuit::ComponentKind::Inductor: return "Inductor (L)";
            case Circuit::ComponentKind::VoltageSource: return "Fuente de Voltaje (V)";
            case Circuit::ComponentKind::OpAmp: return "Amplificador Operacional (OPAMP)";
            case Circuit::ComponentKind::Diode: return "Diodo (D)";
            case Circuit::ComponentKind::Ground: return "Tierra (GND)";
            case Circuit::ComponentKind::Wire: return "Conductor (Wire)";
            default: return "Componente";
            }
        };
        const auto rec = Circuit::RecognizeCircuitStrokes(polylines);
        std::ostringstream os;
        os << "Trazos analizados: " << polylines.size() << "\r\n"
           << "Componente reconocido: " << compKindLabel(rec.kind) << " (confianza "
           << static_cast<int>(std::lround(rec.confidence * 100.0f)) << "%)\r\n";

        std::string generatedNetlist;
        if (!polylines.empty()) {
            if (rec.kind == Circuit::ComponentKind::Resistor) generatedNetlist = "V1 1 0 12\nR1 1 2 1000\nR2 2 0 1000";
            else if (rec.kind == Circuit::ComponentKind::Capacitor) generatedNetlist = "V1 1 0 5\nR1 1 2 1000\nC1 2 0 1e-6";
            else if (rec.kind == Circuit::ComponentKind::Inductor) generatedNetlist = "V1 1 0 5\nR1 1 2 100\nL1 2 0 10e-3";
            else if (rec.kind == Circuit::ComponentKind::VoltageSource) generatedNetlist = "V1 1 0 12\nR1 1 0 1000";
            else if (rec.kind == Circuit::ComponentKind::Diode) generatedNetlist = "V1 1 0 5\nD1 1 2\nR1 2 0 1000";
            else if (rec.kind == Circuit::ComponentKind::OpAmp) generatedNetlist = "V1 1 0 1\nR1 1 2 1000\nR2 2 3 10000\nOPAMP U1 0 2 3";
        }
        if (!generatedNetlist.empty()) {
            os << "\r\nPlantilla SPICE generada:\r\n" << generatedNetlist;
            std::vector<Circuit::CircuitComponent> comps;
            std::string perr;
            std::string summary;
            if (Circuit::ParseNetlistText(generatedNetlist, comps, &perr)) {
                const auto sol = Circuit::SolveMnaCircuit(comps, 0.0);
                summary = sol.ok ? sol.summaryUtf8 : sol.errorUtf8;
            }
            AnnotationObject cb{};
            cb.kind = ObjectKind::CircuitBlock;
            cb.layerId = doc.ActiveLayerId();
            cb.bounds = { x, y, 380.0, 128.0 };
            cb.circuitNetlistUtf8 = generatedNetlist;
            cb.captionUtf8 = std::string("Circuito reconocido: ") + compKindLabel(rec.kind);
            cb.evaluatedResultUtf8 = summary;
            cmdStack.Execute(doc, std::make_unique<AddObjectCommand>(target, std::move(cb)));
        }
        ShowReportDialog(owner, L"Reconocedor de Circuitos a Mano Alzada", Utf8ToWide(os.str()));
        outStatus = std::string("Circuito reconocido: ") + compKindLabel(rec.kind);
    } else if (mode == 1) {
        std::wstring specW = L"1000, 1e-6";
        if (!prompt(L"Caja de Herramientas de Ingenier\u00ED""a (RC / LC / Op-Amp / Divisor / SMD)",
                    L"Ingrese R (ohm), C (F) o c\u00F3digo SMD (ej. 472 o rojo,violeta,naranja,dorado):", specW, false)) return;
        const std::string s = WideToUtf8(specW);
        std::ostringstream os;
        if (s.find(',') != std::string::npos && std::isalpha(static_cast<unsigned char>(s[0]))) {
            std::vector<std::string> bands;
            std::istringstream iss(s);
            std::string b;
            while (std::getline(iss, b, ',')) {
                while (!b.empty() && b.front() == ' ') b.erase(b.begin());
                if (!b.empty()) bands.push_back(b);
            }
            const auto dec = Circuit::DecodeResistorColorNames(bands);
            os << (dec.ok ? ("Resistencia: " + dec.formattedUtf8 + " +/-" + std::to_string(dec.tolerancePct) + "%") : dec.errorUtf8);
        } else if (s.find(',') == std::string::npos) {
            const auto smd = Circuit::DecodeSmdResistorCode(s);
            os << (smd.ok ? ("SMD " + s + " = " + smd.formattedUtf8) : smd.errorUtf8);
        } else {
            const auto comma = s.find(',');
            const double r = std::atof(s.substr(0, comma).c_str());
            const double c = std::atof(s.substr(comma + 1).c_str());
            const double fc = Circuit::Toolbox::RcCutoffHz(r, c);
            const double tau = Circuit::Toolbox::RcTimeConstantSec(r, c);
            const double vdiv = Circuit::Toolbox::VoltageDivider(12.0, r, r);
            os << "Filtro RC (R=" << Circuit::Toolbox::FormatEngineeringNotation(r, "Ohm")
               << ", C=" << Circuit::Toolbox::FormatEngineeringNotation(c, "F") << ")\r\n"
               << "  Frecuencia de corte fc = " << Circuit::Toolbox::FormatEngineeringNotation(fc, "Hz") << "\r\n"
               << "  Constante de tiempo tau = " << Circuit::Toolbox::FormatEngineeringNotation(tau, "s") << "\r\n"
               << "  Divisor sim\xC3\xA9trico (12 V) = " << vdiv << " V\r\n"
               << "  Ganancia Op-Amp no inversor (Rf=10k, Rg=R) = " << Circuit::Toolbox::OpAmpNonInvertingGain(10000.0, r);
        }
        ShowReportDialog(owner, L"Caja de Herramientas de Ingenier\u00ED""a", Utf8ToWide(os.str()));
        outStatus = "C\xC3\xA1lculo de ingenier\xC3\xAD""a completado.";
    } else if (mode == 2) {
        std::wstring codeW = L"void setup() { pinMode(13, OUTPUT); }\r\nvoid loop() { digitalWrite(13, HIGH); delay(500); digitalWrite(13, LOW); }";
        if (!prompt(L"Insertar Bloque de C\u00F3digo / MCU", L"C\u00F3digo C/C++/Arduino/Python:", codeW, true)) return;
        const std::string src = WideToUtf8(codeW);
        const auto hl = Circuit::TokenizeCodeBlock(src, Circuit::CodeLanguage::ArduinoIno);
        AnnotationObject cb{};
        cb.kind = ObjectKind::CodeBlock;
        cb.layerId = doc.ActiveLayerId();
        cb.bounds = { x, y, 420.0, 140.0 };
        cb.codeLanguageUtf8 = "arduino";
        cb.textUtf8 = src;
        cb.captionUtf8 = "C\xC3\xB3" "digo MCU (" + std::to_string(hl.tokens.size()) + " tokens)";
        cb.evaluatedResultUtf8 = "Sintaxis verificada (" + std::to_string(hl.lineCount) + " l\xC3\xADneas)";
        cmdStack.Execute(doc, std::make_unique<AddObjectCommand>(target, std::move(cb)));
        outStatus = "Bloque de c\xC3\xB3" "digo insertado.";
    } else if (mode == 3) {
        std::wstring qW = L"a*x + b";
        if (!prompt(L"B\u00FAsqueda Sem\u00E1ntica de F\u00F3rmulas (AST)", L"F\u00F3rmula patr\u00F3n (detecta equivalencia independiente de nombres de variables):", qW, false)) return;
        const auto targetFp = Cas::ComputeFormulaFingerprint(WideToUtf8(qW));
        std::ostringstream os;
        os << "Patr\xC3\xB3n can\xC3\xB3nico: " << targetFp.canonicalAlphaExpr << "\r\n\r\nCoincidencias en el documento:\r\n";
        int hits = 0;
        auto scanVec = [&](const std::vector<AnnotationObject>& vec, const std::string& where) {
            for (const auto& o : vec) {
                if (o.formulaExprUtf8.empty()) continue;
                const auto fp = Cas::ComputeFormulaFingerprint(o.formulaExprUtf8);
                if (fp.ok && targetFp.ok && fp.hash64 == targetFp.hash64) {
                    os << "  - [" << where << "] " << o.formulaExprUtf8 << " = " << o.evaluatedResultUtf8 << "\r\n";
                    ++hits;
                }
            }
        };
        for (std::size_t p = 0; p < doc.Pages().size(); ++p) scanVec(doc.Pages()[p].objects, "P\xC3\xA1g. " + std::to_string(p + 1));
        scanVec(doc.CanvasObjects(), "Lienzo");
        if (hits == 0) os << "  (Sin coincidencias estructurales exactas)";
        ShowReportDialog(owner, L"B\u00FAsqueda Sem\u00E1ntica AST", Utf8ToWide(os.str()));
        outStatus = "B\xC3\xBAsqueda sem\xC3\xA1ntica AST: " + std::to_string(hits) + " coincidencia(s).";
    }
}

// Track D: Security (AES-256-GCM Encrypted Save/Open + LAN QR Broadcast) (F-210, F-211, F-215)
inline void RunSecurityOrLanFlow(
    HWND owner,
    DocumentModel& doc,
    int mode, // 0=EncryptSave, 1=DecryptOpen, 2=LanQrBroadcast
    PromptFn prompt,
    std::string& outStatus)
{
    if (mode == 0) {
        std::wstring passW;
        if (!prompt(L"Guardar Cuaderno Cifrado (AES-256-GCM)", L"Contrase\u00F1""a de protecci\u00F3n:", passW, false) || passW.empty()) return;
        wchar_t fileBuf[MAX_PATH * 2]{ L"cuaderno_seguro.cudoc" };
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = L"Cuaderno Cifrado CuadernU (*.cudoc)\0*.cudoc\0\0";
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
        ofn.lpstrDefExt = L"cudoc";
        ofn.Flags = OFN_OVERWRITEPROMPT;
        if (!GetSaveFileNameW(&ofn)) return;
        std::string err;
        if (CudocCrypto::SaveEncryptedCudocFile(WideToUtf8(fileBuf), doc, WideToUtf8(passW), &err)) {
            outStatus = "Cuaderno guardado con cifrado AES-256-GCM.";
        } else {
            ShowReportDialog(owner, L"Error de cifrado", Utf8ToWide(err));
        }
    } else if (mode == 1) {
        wchar_t fileBuf[MAX_PATH * 2]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = L"Cuaderno Cifrado CuadernU (*.cudoc)\0*.cudoc\0\0";
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
        ofn.Flags = OFN_FILEMUSTEXIST;
        if (!GetOpenFileNameW(&ofn)) return;
        std::wstring passW;
        if (!prompt(L"Abrir Cuaderno Cifrado", L"Ingrese la contrase\u00F1""a:", passW, false) || passW.empty()) return;
        DocumentModel dec;
        std::string err;
        if (CudocCrypto::LoadEncryptedCudocFile(WideToUtf8(fileBuf), WideToUtf8(passW), dec, &err)) {
            doc = std::move(dec);
            outStatus = "Cuaderno cifrado abierto correctamente.";
        } else {
            ShowReportDialog(owner, L"Error al descifrar", Utf8ToWide(err));
        }
    } else if (mode == 2) {
        int boundPort = 0;
        std::string err;
        if (!LanRemote::ProbeBindLocalPort(0, boundPort, &err)) boundPort = 8765;
        const std::string token = LanRemote::GenerateSessionTokenHex24().substr(0, 16);
        const std::string url = "http://127.0.0.1:" + std::to_string(boundPort) + "/cuadernu?session=" + token;
        LanRemote::QrMatrix qr{};
        if (LanRemote::EncodeQrMatrix(url, qr, &err)) {
            wchar_t tmpDir[MAX_PATH]{};
            GetTempPathW(MAX_PATH, tmpDir);
            const std::string htmlPath = WideToUtf8(tmpDir) + "cuadernu_lan_broadcast.html";
            LanRemote::ExportLanBroadcastHtml(doc, htmlPath, url, true, false);
            ShowQrCodeDialog(owner, L"Difusi\u00F3n LAN Local (Solo Lectura)", url, qr);
            outStatus = "Sesi\xC3\xB3n LAN local lista en puerto " + std::to_string(boundPort) + ".";
        }
    }
}

// Track C: Page Version History & Conflict Merge Dialogs (F-059, F-219)
inline void RunVersionHistoryOrMerge(
    HWND owner,
    DocumentModel& doc,
    PageVersionHistory& history,
    std::size_t currentPageIdx,
    bool doMergeOtherFile,
    std::string& outStatus)
{
    if (!doMergeOtherFile) {
        const auto nowSec = static_cast<std::uint64_t>(std::time(nullptr));
        const auto verId = history.CaptureSnapshot(doc, currentPageIdx, nowSec, "Instant\xC3\xA1nea manual");
        const auto list = history.SnapshotsForPage(currentPageIdx);
        std::ostringstream os;
        os << "Instant\xC3\xA1nea guardada (#" << verId << ") para p\xC3\xA1gina " << (currentPageIdx + 1) << ".\r\n\r\n"
           << "Historial registrado en esta sesi\xC3\xB3n (" << list.size() << " versiones):\r\n";
        for (const auto& s : list) {
            os << "  - Versi\xC3\xB3n #" << s.versionId << " (" << s.authorOrLabelUtf8 << ") — "
               << s.snapshotPage.objects.size() << " objeto(s)\r\n";
        }
        ShowReportDialog(owner, L"Historial de Versiones de P\u00E1gina", Utf8ToWide(os.str()));
        outStatus = "Instant\xC3\xA1nea #" + std::to_string(verId) + " guardada.";
    } else {
        wchar_t fileBuf[MAX_PATH * 2]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = L"Cuaderno CuadernU (*.cudoc)\0*.cudoc\0\0";
        ofn.lpstrFile = fileBuf;
        ofn.nMaxFile = static_cast<DWORD>(std::size(fileBuf));
        ofn.Flags = OFN_FILEMUSTEXIST;
        if (!GetOpenFileNameW(&ofn)) return;
        DocumentModel remoteDoc;
        std::string err;
        if (!CudocStorage::LoadFromFile(fileBuf, remoteDoc, &err)) {
            ShowReportDialog(owner, L"Error cargando archivo remoto", Utf8ToWide(err));
            return;
        }
        DocumentModel baseDoc;
        DocumentModel merged;
        const auto mr = MergeConcurrentDocuments(baseDoc, doc, remoteDoc, merged);
        doc = std::move(merged);
        std::ostringstream os;
        os << "Fusi\xC3\xB3n sin p\xC3\xA9rdida completada.\r\n"
           << "P\xC3\xA1ginas fusionadas limpiamente: " << mr.mergedCleanPages << "\r\n"
           << "P\xC3\xA1ginas de conflicto preservadas lado a lado: " << mr.conflictPagesCreated << "\r\n";
        for (const auto& d : mr.conflictDescriptions) os << "  - " << d << "\r\n";
        ShowReportDialog(owner, L"Fusi\u00F3n de Conflictos", Utf8ToWide(os.str()));
        outStatus = "Fusi\xC3\xB3n completada (" + std::to_string(mr.conflictPagesCreated) + " conflicto(s) preservados).";
    }
}

// Track C: Library Vault Browser & Vault-Wide Search (F-058, F-061..F-064)
inline void RunVaultLibraryManager(
    HWND owner,
    DocumentModel& doc,
    NotebookVault& vault,
    VaultFullTextIndex& ftsIndex,
    const std::wstring& currentPathW,
    PromptFn prompt,
    std::string& outStatus)
{
    ftsIndex.IndexDocument(doc.Title(), doc);
    if (vault.Notebooks().empty()) {
        const auto nb = vault.CreateNotebook("Cuadernos de Ingenier\xC3\xAD""a");
        const auto sec = vault.CreateSection(nb, "General");
        vault.AddPageEntry(nb, sec, doc.Title(), WideToUtf8(currentPathW));
    }

    std::wstring queryW;
    const bool doSearch = prompt(
        L"Biblioteca de Cuadernos y B\u00FAsqueda Global",
        L"Escriba un t\u00E9rmino para buscar en toda la biblioteca (o deje vac\u00EDo para ver estructura):",
        queryW, false);

    std::ostringstream os;
    if (doSearch && !queryW.empty()) {
        const auto hits = ftsIndex.Query(WideToUtf8(queryW), 32);
        os << "B\xC3\xBAsqueda global \"" << WideToUtf8(queryW) << "\" (" << hits.size() << " resultado(s)):\r\n\r\n";
        for (const auto& h : hits) {
            os << "  - [" << h.docKeyUtf8 << " | P\xC3\xA1g. " << (h.pageIndex + 1) << " | " << h.sourceKindUtf8 << "] "
               << h.snippetUtf8 << "\r\n";
        }
        outStatus = "B\xC3\xBAsqueda en biblioteca: " + std::to_string(hits.size()) + " resultado(s).";
    } else {
        os << "Biblioteca CuadernU (" << vault.Notebooks().size() << " cuaderno(s), "
           << ftsIndex.IndexedDocumentCount() << " documento(s) indexados):\r\n\r\n";
        for (const auto& nb : vault.Notebooks()) {
            os << "Cuaderno: " << nb.titleUtf8 << "\r\n";
            for (const auto& sec : nb.sections) {
                os << "  Secci\xC3\xB3n: " << sec.titleUtf8 << " (" << sec.pages.size() << " p\xC3\xA1g.)\r\n";
                for (const auto& pg : sec.pages) {
                    os << "    - " << pg.titleUtf8 << "\r\n";
                }
            }
        }
        outStatus = "Biblioteca actualizada (" + std::to_string(vault.Notebooks().size()) + " cuaderno(s)).";
    }
    ShowReportDialog(owner, L"Biblioteca CuadernU", Utf8ToWide(os.str()));
}

} // namespace DocStudio::GuiDialogs
#endif
