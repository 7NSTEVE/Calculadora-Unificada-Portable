#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <roapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "../core/camera.hpp"
#include "../core/command_stack.hpp"
#include "../core/cudoc_storage.hpp"
#include "../core/doc_types.hpp"
#include "../core/document_model.hpp"
#include "../core/utf_conv.hpp"
#include "../pdf/interop_openxml.hpp"
#include "../pdf/pdf_parser.hpp"
#include "../pdf/pdf_signer.hpp"
#include "../pdf/pdf_writer.hpp"
#include "../pdf/winrt_adapters.hpp"
#include "../render/d2d_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

using namespace DocStudio;

constexpr const wchar_t* kMainClassName = L"CalculadoraUnificadaDocStudioWnd";
constexpr const wchar_t* kViewportClassName = L"CalculadoraUnificadaDocStudioViewport";
constexpr const wchar_t* kSidebarClassName = L"CalculadoraUnificadaDocStudioSidebar";
constexpr const wchar_t* kPromptClassName = L"CalculadoraUnificadaDocStudioPrompt";

constexpr UINT WM_APP_ASYNC_PAGE_READY = WM_APP + 101;
constexpr UINT WM_APP_TAKE_SCREENSHOT = WM_APP + 102;

enum class ToolKind {
    Hand = 0,
    Pen,
    Highlighter,
    Eraser,
    ShapeRect,
    ShapeArrow,
    TextBox,
    Crop
};

enum CmdId : WORD {
    CMD_OPEN = 1001,
    CMD_SAVE_CUDOC,
    CMD_EXPORT_PDF,
    CMD_MERGE_PDF,
    CMD_SPLIT_PDF,
    CMD_SIGN_PDF,
    CMD_VERIFY_SIG,
    CMD_RUN_OCR,
    CMD_INTEROP_MENU,
    CMD_PRINT,
    CMD_MODE_PDF,
    CMD_MODE_CANVAS,
    CMD_TOOL_HAND,
    CMD_TOOL_PEN,
    CMD_TOOL_HL,
    CMD_TOOL_ERASER,
    CMD_TOOL_RECT,
    CMD_TOOL_ARROW,
    CMD_TOOL_TEXT,
    CMD_TOOL_CROP,
    CMD_INSERT_IMAGE,
    CMD_COL_BLACK,
    CMD_COL_BLUE,
    CMD_COL_RED,
    CMD_COL_GREEN,
    CMD_COL_YELLOW,
    CMD_PAPER_NEXT,
    CMD_ADD_PAGE,
    CMD_ROTATE_PAGE,
    CMD_DEL_PAGE,
    CMD_PAGE_UP,
    CMD_PAGE_DOWN,
    CMD_UNDO,
    CMD_REDO,
    CMD_ZOOM_OUT,
    CMD_ZOOM_RESET,
    CMD_ZOOM_IN,
    CMD_SEARCH_EDIT,
    CMD_SEARCH_NEXT,
    CMD_REPLACE_TEXT,
    CMD_SIDEBAR_SCROLL,
    // Menu IDs
    CMD_EXPORT_DOCX = 2001,
    CMD_EXPORT_PPTX,
    CMD_IMPORT_DOCX,
    CMD_IMPORT_PPTX,
    CMD_IMPORT_PDF_TO_CANVAS
};

struct ToolbarBtn {
    WORD id = 0;
    const wchar_t* label = nullptr;
    int width = 72;
    int row = 0;
    bool accent = false;
    bool activeCheck = false;
    COLORREF swatchColor = 0;
    bool isSwatch = false;
};

struct StudioAppState {
    HWND hwndMain = nullptr;
    HWND hwndViewport = nullptr;
    HWND hwndSidebar = nullptr;
    HWND hwndSearchEdit = nullptr;
    HFONT hUiFont = nullptr;
    HFONT hUiBoldFont = nullptr;

    DocumentModel doc;
    CommandStack cmdStack;
    Camera2D camera;
    PageRenderCache pageCache{ 256ull * 1024ull * 1024ull };
    D2DRenderer renderer;
    std::uint64_t asyncGen = 1;

    std::wstring currentDocPath;
    std::string activePdfSourcePathUtf8;
    std::string statusBannerUtf8 = "Document Studio listo";

    ToolKind activeTool = ToolKind::Pen;
    DocColor penColor = DocColor::RGBA(26, 115, 232, 255);
    double penWidth = 2.6;

    // Interactive gesture state
    bool panning = false;
    POINT lastPanPt{};
    bool drawing = false;
    bool draftIsCanvas = false;
    std::size_t draftPageIdx = 0;
    AnnotationObject draftObj{};
    DocPoint draftAnchorWorld{};
    bool hasDraftCrop = false;
    DocRect draftCropPageLocal{};

    std::uint64_t selectedObjectId = 0;
    std::vector<SearchMatch> searchMatches;
    std::size_t activeSearchIdx = 0;
    std::size_t activePageIndex = 0;
    int sidebarScrollPos = 0;

    std::vector<RECT> buttonRects;
    std::vector<ToolbarBtn> buttons;
    int hoveredButton = -1;

    // CLI automation state
    bool smokeTestMode = false;
    std::wstring screenshotOutPath;
    int initialPage1Based = 1;
};

// ---------------- Small Win32 Helpers ----------------

std::wstring GetFileExtLower(const std::wstring& path) {
    const auto dot = path.rfind(L'.');
    if (dot == std::wstring::npos) return {};
    std::wstring ext = path.substr(dot);
    for (wchar_t& c : ext) c = static_cast<wchar_t>(towlower(c));
    return ext;
}

bool ShowOpenDialog(HWND owner, const wchar_t* title, const COMDLG_FILTERSPEC* specs, UINT specCount, std::vector<std::wstring>& outPaths, bool multiSelect) {
    outPaths.clear();
    Microsoft::WRL::ComPtr<IFileOpenDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) || !dlg) return false;
    DWORD opts = FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST;
    if (multiSelect) opts |= FOS_ALLOWMULTISELECT;
    dlg->SetOptions(opts);
    if (title) dlg->SetTitle(title);
    if (specs && specCount > 0) dlg->SetFileTypes(specCount, specs);
    if (FAILED(dlg->Show(owner))) return false;

    Microsoft::WRL::ComPtr<IShellItemArray> arr;
    if (SUCCEEDED(dlg->GetResults(&arr)) && arr) {
        DWORD cnt = 0;
        arr->GetCount(&cnt);
        for (DWORD i = 0; i < cnt; ++i) {
            Microsoft::WRL::ComPtr<IShellItem> item;
            if (SUCCEEDED(arr->GetItemAt(i, &item)) && item) {
                PWSTR p = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                    outPaths.emplace_back(p);
                    CoTaskMemFree(p);
                }
            }
        }
    }
    return !outPaths.empty();
}

bool ShowSaveDialog(HWND owner, const wchar_t* title, const COMDLG_FILTERSPEC* specs, UINT specCount, const wchar_t* defExt, const wchar_t* defName, std::wstring& outPath) {
    outPath.clear();
    Microsoft::WRL::ComPtr<IFileSaveDialog> dlg;
    if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) || !dlg) return false;
    dlg->SetOptions(FOS_FORCEFILESYSTEM | FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST);
    if (title) dlg->SetTitle(title);
    if (specs && specCount > 0) dlg->SetFileTypes(specCount, specs);
    if (defExt) dlg->SetDefaultExtension(defExt);
    if (defName) dlg->SetFileName(defName);
    if (FAILED(dlg->Show(owner))) return false;
    Microsoft::WRL::ComPtr<IShellItem> item;
    if (SUCCEEDED(dlg->GetResult(&item)) && item) {
        PWSTR p = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
            outPath = p;
            CoTaskMemFree(p);
        }
    }
    return !outPath.empty();
}

// Dynamic modal text prompt (no .rc dependency)
struct PromptState {
    std::wstring label1;
    std::wstring val1;
    std::wstring label2;
    std::wstring val2;
    bool twoFields = false;
    bool accepted = false;
    HWND hwndEdit1 = nullptr;
    HWND hwndEdit2 = nullptr;
};

LRESULT CALLBACK PromptWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PromptState* ps = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ps = reinterpret_cast<PromptState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ps));
        HFONT hf = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

        HWND l1 = CreateWindowW(L"STATIC", ps->label1.c_str(), WS_CHILD | WS_VISIBLE, 14, 12, 390, 20, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(l1, WM_SETFONT, reinterpret_cast<WPARAM>(hf), TRUE);
        ps->hwndEdit1 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", ps->val1.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 14, 34, 390, 24, hwnd, reinterpret_cast<HMENU>(101), nullptr, nullptr);
        SendMessageW(ps->hwndEdit1, WM_SETFONT, reinterpret_cast<WPARAM>(hf), TRUE);

        int btnY = 72;
        if (ps->twoFields) {
            HWND l2 = CreateWindowW(L"STATIC", ps->label2.c_str(), WS_CHILD | WS_VISIBLE, 14, 68, 390, 20, hwnd, nullptr, nullptr, nullptr);
            SendMessageW(l2, WM_SETFONT, reinterpret_cast<WPARAM>(hf), TRUE);
            ps->hwndEdit2 = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", ps->val2.c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP, 14, 90, 390, 24, hwnd, reinterpret_cast<HMENU>(102), nullptr, nullptr);
            SendMessageW(ps->hwndEdit2, WM_SETFONT, reinterpret_cast<WPARAM>(hf), TRUE);
            btnY = 128;
        }

        HWND bOk = CreateWindowW(L"BUTTON", L"Aceptar", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            234, btnY, 82, 28, hwnd, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        HWND bCancel = CreateWindowW(L"BUTTON", L"Cancelar", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            322, btnY, 82, 28, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
        SendMessageW(bOk, WM_SETFONT, reinterpret_cast<WPARAM>(hf), TRUE);
        SendMessageW(bCancel, WM_SETFONT, reinterpret_cast<WPARAM>(hf), TRUE);
        SetFocus(ps->hwndEdit1);
        return 0;
    }
    case WM_COMMAND:
        if (!ps) break;
        if (LOWORD(wp) == IDOK) {
            wchar_t buf[2048]{};
            GetWindowTextW(ps->hwndEdit1, buf, 2048);
            ps->val1 = buf;
            if (ps->twoFields && ps->hwndEdit2) {
                GetWindowTextW(ps->hwndEdit2, buf, 2048);
                ps->val2 = buf;
            }
            ps->accepted = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) {
            ps->accepted = false;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (ps) ps->accepted = false;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool ShowTextPrompt(HWND owner, const wchar_t* title, const wchar_t* label1, std::wstring& val1, const wchar_t* label2 = nullptr, std::wstring* val2 = nullptr) {
    PromptState ps;
    ps.label1 = label1 ? label1 : L"";
    ps.val1 = val1;
    ps.twoFields = (label2 != nullptr && val2 != nullptr);
    if (ps.twoFields) {
        ps.label2 = label2;
        ps.val2 = *val2;
    }

    RECT ownerRc{};
    GetWindowRect(owner, &ownerRc);
    const int w = 434;
    const int h = ps.twoFields ? 204 : 150;
    const int x = (ownerRc.left + ownerRc.right - w) / 2;
    const int y = (ownerRc.top + ownerRc.bottom - h) / 2;

    EnableWindow(owner, FALSE);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kPromptClassName, title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), &ps);

    MSG msg{};
    while (IsWindow(dlg) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessageW(dlg, WM_COMMAND, IDOK, 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessageW(dlg, WM_COMMAND, IDCANCEL, 0);
            continue;
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (ps.accepted) {
        val1 = ps.val1;
        if (ps.twoFields && val2) *val2 = ps.val2;
    }
    return ps.accepted;
}

bool CaptureWindowToPng(HWND hwnd, const std::wstring& outPath) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int w = (std::max)(1, static_cast<int>(rc.right - rc.left));
    const int h = (std::max)(1, static_cast<int>(rc.bottom - rc.top));

    HDC hdcWin = GetDC(hwnd);
    HDC hdcMem = CreateCompatibleDC(hdcWin);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcWin, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp || !bits) {
        if (hBmp) DeleteObject(hBmp);
        DeleteDC(hdcMem);
        ReleaseDC(hwnd, hdcWin);
        return false;
    }

    HGDIOBJ old = SelectObject(hdcMem, hBmp);
    if (!PrintWindow(hwnd, hdcMem, PW_CLIENTONLY | PW_RENDERFULLCONTENT)) {
        BitBlt(hdcMem, 0, 0, w, h, hdcWin, 0, 0, SRCCOPY);
    }

    RasterImageBgra32 img;
    img.width = w;
    img.height = h;
    img.pixels.resize(static_cast<std::size_t>(w) * h * 4u);
    std::memcpy(img.pixels.data(), bits, img.pixels.size());
    // Ensure opaque alpha on GDI chrome pixels
    for (std::size_t i = 3; i < img.pixels.size(); i += 4) img.pixels[i] = 255;

    SelectObject(hdcMem, old);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(hwnd, hdcWin);

    std::vector<std::uint8_t> png;
    if (!WinRtPdfRenderer::EncodeBgra32ToPngBytes(img, png)) return false;

    HANDLE hf = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    const BOOL ok = WriteFile(hf, png.data(), static_cast<DWORD>(png.size()), &wr, nullptr);
    CloseHandle(hf);
    return ok && wr == png.size();
}

// Composite Direct2D viewport onto GDI capture if Direct2D Present isn't copied by PrintWindow on some GPUs
bool CaptureStudioCombinedToPng(StudioAppState* st, const std::wstring& outPath) {
    RECT mainRc{}, vpRc{};
    GetClientRect(st->hwndMain, &mainRc);
    GetWindowRect(st->hwndViewport, &vpRc);
    MapWindowPoints(HWND_DESKTOP, st->hwndMain, reinterpret_cast<LPPOINT>(&vpRc), 2);

    const int mw = (std::max)(1, static_cast<int>(mainRc.right - mainRc.left));
    const int mh = (std::max)(1, static_cast<int>(mainRc.bottom - mainRc.top));
    HDC hdcWin = GetDC(st->hwndMain);
    HDC hdcMem = CreateCompatibleDC(hdcWin);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = mw;
    bmi.bmiHeader.biHeight = -mh;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdcWin, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hBmp || !bits) {
        if (hBmp) DeleteObject(hBmp);
        DeleteDC(hdcMem);
        ReleaseDC(st->hwndMain, hdcWin);
        return false;
    }

    HGDIOBJ old = SelectObject(hdcMem, hBmp);
    PrintWindow(st->hwndMain, hdcMem, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
    SelectObject(hdcMem, old);

    RasterImageBgra32 composite;
    composite.width = mw;
    composite.height = mh;
    composite.pixels.resize(static_cast<std::size_t>(mw) * mh * 4u);
    std::memcpy(composite.pixels.data(), bits, composite.pixels.size());
    for (std::size_t i = 3; i < composite.pixels.size(); i += 4) composite.pixels[i] = 255;

    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(st->hwndMain, hdcWin);

    // Blit crisp synchronous offscreen Direct2D render straight into viewport region so screenshot is 100% deterministic
    const int vw = (std::max)(1, static_cast<int>(vpRc.right - vpRc.left));
    const int vh = (std::max)(1, static_cast<int>(vpRc.bottom - vpRc.top));
    RasterImageBgra32 vpImg;
    ViewportOverlayState ov{};
    ov.selectedObjectId = st->selectedObjectId;
    ov.searchMatches = st->searchMatches.empty() ? nullptr : &st->searchMatches;
    ov.activeSearchMatchIndex = st->activeSearchIdx;
    if (st->renderer.RenderHwndViewportToOffscreen(st->doc, st->camera, vw, vh, &st->pageCache, vpImg, &ov)) {
        for (int y = 0; y < vh; ++y) {
            const int dy = vpRc.top + y;
            if (dy < 0 || dy >= mh) continue;
            for (int x = 0; x < vw; ++x) {
                const int dx = vpRc.left + x;
                if (dx < 0 || dx >= mw) continue;
                const std::uint8_t* s = &vpImg.pixels[(static_cast<std::size_t>(y) * vw + x) * 4u];
                std::uint8_t* d = &composite.pixels[(static_cast<std::size_t>(dy) * mw + dx) * 4u];
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = 255;
            }
        }
    }

    std::vector<std::uint8_t> png;
    if (!WinRtPdfRenderer::EncodeBgra32ToPngBytes(composite, png)) return false;
    HANDLE hf = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    const BOOL ok = WriteFile(hf, png.data(), static_cast<DWORD>(png.size()), &wr, nullptr);
    CloseHandle(hf);
    return ok && wr == png.size();
}

// ---------------- App Helpers ----------------

void RefreshActivePageFromCamera(StudioAppState* st) {
    if (st->doc.Mode() != DocumentMode::PaginatedPdf || st->doc.Pages().empty()) return;
    RECT rc{};
    GetClientRect(st->hwndViewport, &rc);
    const double midY = st->camera.ScreenToWorld(0.0, (rc.bottom - rc.top) * 0.4).y;
    std::size_t f = 0, l = 0;
    if (st->doc.Layout().ComputeVisibleRange(midY, midY + 10.0, f, l, 0)) {
        st->activePageIndex = f;
    }
}

void CenterCameraOnPage(StudioAppState* st, std::size_t pageIdx) {
    if (st->doc.Pages().empty()) return;
    const std::size_t idx = (std::min)(pageIdx, st->doc.Pages().size() - 1);
    const auto* pl = st->doc.Layout().PlacementAt(idx);
    if (!pl) return;
    RECT rc{};
    GetClientRect(st->hwndViewport, &rc);
    const double vw = (std::max)(200.0, static_cast<double>(rc.right - rc.left));
    st->camera.zoom = std::clamp((vw - 64.0) / (std::max)(120.0, pl->widthWorld), 0.25, 2.2);
    st->camera.panX = -vw / (2.0 * st->camera.zoom);
    st->camera.panY = pl->topWorld - 16.0;
    st->activePageIndex = idx;
}

void UpdateWindowTitle(StudioAppState* st) {
    const std::wstring dirtyMark = st->doc.IsDirty() ? L"*" : L"";
    const std::wstring name = st->currentDocPath.empty()
        ? Utf8ToWide(st->doc.Title())
        : std::filesystem::path(st->currentDocPath).filename().wstring();
    const std::wstring title = L"Document Studio \u2014 " + name + dirtyMark;
    SetWindowTextW(st->hwndMain, title.c_str());
}

void RebuildToolbarButtons(StudioAppState* st) {
    st->buttons = {
        // Row 0: Document & Formats
        { CMD_OPEN,         L"Abrir",      56, 0, false },
        { CMD_SAVE_CUDOC,   L"Guardar",    64, 0, true },
        { CMD_EXPORT_PDF,   L"Export PDF", 78, 0, false },
        { CMD_MERGE_PDF,    L"Combinar",   68, 0, false },
        { CMD_SPLIT_PDF,    L"Dividir",    58, 0, false },
        { CMD_SIGN_PDF,     L"Firmar",     58, 0, false },
        { CMD_VERIFY_SIG,   L"Verificar",  66, 0, false },
        { CMD_RUN_OCR,      L"OCR",        46, 0, false },
        { CMD_INTEROP_MENU, L"Office \u25BE", 70, 0, false },
        { CMD_PRINT,        L"Imprimir",   64, 0, false },
        // Row 1: Modes, Tools, Palette, Page Ops
        { CMD_MODE_PDF,     L"PDF P\u00E1g.",  68, 1, false },
        { CMD_MODE_CANVAS,  L"Lienzo \u221E",  72, 1, false },
        { CMD_TOOL_HAND,    L"Mano",           50, 1, false },
        { CMD_TOOL_PEN,     L"Pluma",          52, 1, false },
        { CMD_TOOL_HL,      L"Resalt.",        56, 1, false },
        { CMD_TOOL_ERASER,  L"Borrar",         52, 1, false },
        { CMD_TOOL_RECT,    L"Rect",           44, 1, false },
        { CMD_TOOL_ARROW,   L"Flecha",         52, 1, false },
        { CMD_TOOL_TEXT,    L"Texto",          48, 1, false },
        { CMD_TOOL_CROP,    L"Recorte",        58, 1, false },
        { CMD_INSERT_IMAGE, L"+Img",           44, 1, false },
        { CMD_COL_BLACK,    L"",               22, 1, false, false, RGB(17, 24, 39), true },
        { CMD_COL_BLUE,     L"",               22, 1, false, false, RGB(26, 115, 232), true },
        { CMD_COL_RED,      L"",               22, 1, false, false, RGB(220, 38, 38), true },
        { CMD_COL_GREEN,    L"",               22, 1, false, false, RGB(21, 128, 61), true },
        { CMD_COL_YELLOW,   L"",               22, 1, false, false, RGB(250, 204, 21), true },
        { CMD_PAPER_NEXT,   L"Papel",          52, 1, false },
        { CMD_ADD_PAGE,     L"+P\u00E1g",      46, 1, false },
        { CMD_ROTATE_PAGE,  L"Rot 90\u00B0",   58, 1, false },
        { CMD_DEL_PAGE,     L"-P\u00E1g",      46, 1, false },
        { CMD_UNDO,         L"Desh.",          48, 1, false },
        { CMD_REDO,         L"Reh.",           44, 1, false },
        { CMD_ZOOM_OUT,     L"-",              24, 1, false },
        { CMD_ZOOM_RESET,   L"100%",           44, 1, false },
        { CMD_ZOOM_IN,      L"+",              24, 1, false }
    };
}

bool IsToolbarButtonChecked(const StudioAppState* st, WORD id) {
    switch (id) {
    case CMD_MODE_PDF:    return st->doc.Mode() == DocumentMode::PaginatedPdf;
    case CMD_MODE_CANVAS: return st->doc.Mode() == DocumentMode::InfiniteCanvas;
    case CMD_TOOL_HAND:   return st->activeTool == ToolKind::Hand;
    case CMD_TOOL_PEN:    return st->activeTool == ToolKind::Pen;
    case CMD_TOOL_HL:     return st->activeTool == ToolKind::Highlighter;
    case CMD_TOOL_ERASER: return st->activeTool == ToolKind::Eraser;
    case CMD_TOOL_RECT:   return st->activeTool == ToolKind::ShapeRect;
    case CMD_TOOL_ARROW:  return st->activeTool == ToolKind::ShapeArrow;
    case CMD_TOOL_TEXT:   return st->activeTool == ToolKind::TextBox;
    case CMD_TOOL_CROP:   return st->activeTool == ToolKind::Crop;
    case CMD_COL_BLACK:   return st->penColor.r == 17 && st->penColor.g == 24;
    case CMD_COL_BLUE:    return st->penColor.r == 26 && st->penColor.g == 115;
    case CMD_COL_RED:     return st->penColor.r == 220 && st->penColor.g == 38;
    case CMD_COL_GREEN:   return st->penColor.r == 21 && st->penColor.g == 128;
    case CMD_COL_YELLOW:  return st->penColor.r == 250 && st->penColor.g == 204;
    default: return false;
    }
}

void LayoutMainChildren(StudioAppState* st) {
    if (!st || !st->hwndMain) return;
    RECT rc{};
    GetClientRect(st->hwndMain, &rc);
    const int w = (std::max)(320, static_cast<int>(rc.right - rc.left));
    const int h = (std::max)(240, static_cast<int>(rc.bottom - rc.top));
    constexpr int kRibbonH = 74;
    constexpr int kStatusH = 24;
    const int sideW = (st->doc.Mode() == DocumentMode::PaginatedPdf) ? 196 : 0;

    st->buttonRects.clear();
    int xRow0 = 10;
    int xRow1 = 10;
    for (const auto& b : st->buttons) {
        if (b.row == 0) {
            st->buttonRects.push_back({ xRow0, 6, xRow0 + b.width, 32 });
            xRow0 += b.width + 4;
        } else {
            st->buttonRects.push_back({ xRow1, 39, xRow1 + b.width, 67 });
            xRow1 += b.width + 3;
        }
    }

    // Place search bar at right side of Row 0 (if fits)
    if (st->hwndSearchEdit) {
        const int searchX = (std::max)(xRow0 + 8, w - 265);
        const int searchW = (std::max)(90, w - searchX - 96);
        MoveWindow(st->hwndSearchEdit, searchX, 8, searchW, 22, TRUE);
    }

    if (st->hwndSidebar) {
        if (sideW > 0) {
            ShowWindow(st->hwndSidebar, SW_SHOW);
            MoveWindow(st->hwndSidebar, 0, kRibbonH, sideW, (std::max)(60, h - kRibbonH - kStatusH), TRUE);
        } else {
            ShowWindow(st->hwndSidebar, SW_HIDE);
        }
    }
    if (st->hwndViewport) {
        const int vw = (std::max)(120, w - sideW);
        const int vh = (std::max)(100, h - kRibbonH - kStatusH);
        MoveWindow(st->hwndViewport, sideW, kRibbonH, vw, vh, TRUE);
        st->renderer.Resize(vw, vh);
    }
}

void InvalidateViews(StudioAppState* st) {
    if (st->hwndViewport) InvalidateRect(st->hwndViewport, nullptr, FALSE);
    if (st->hwndSidebar && IsWindowVisible(st->hwndSidebar)) InvalidateRect(st->hwndSidebar, nullptr, FALSE);
    if (st->hwndMain) {
        RECT statusRc{};
        GetClientRect(st->hwndMain, &statusRc);
        statusRc.top = statusRc.bottom - 24;
        InvalidateRect(st->hwndMain, &statusRc, FALSE);
        RECT ribRc{ 0, 0, statusRc.right, 74 };
        InvalidateRect(st->hwndMain, &ribRc, FALSE);
    }
}

bool OpenDocumentFile(StudioAppState* st, const std::wstring& path, std::string* outErr = nullptr) {
    const std::wstring ext = GetFileExtLower(path);
    const std::string pathUtf8 = WideToUtf8(path);

    if (ext == L".pdf") {
        PdfDocument pdf;
        std::string err;
        if (!pdf.LoadFromFile(path, &err)) {
            if (outErr) *outErr = err;
            st->statusBannerUtf8 = "Error PDF: " + err;
            return false;
        }
        if (!pdf.PopulateDocumentModel(st->doc, pathUtf8, &err)) {
            if (outErr) *outErr = err;
            return false;
        }
        st->cmdStack.Clear();
        st->pageCache.InvalidateAll(++st->asyncGen);
        st->currentDocPath.clear();
        st->activePdfSourcePathUtf8 = pathUtf8;
        st->doc.SetTitle(WideToUtf8(std::filesystem::path(path).stem().wstring()));
        CenterCameraOnPage(st, static_cast<std::size_t>((std::max)(0, st->initialPage1Based - 1)));
        st->initialPage1Based = 1;
        st->statusBannerUtf8 = "PDF abierto: " + std::to_string(st->doc.Pages().size()) + " p\xC3\xA1ginas";
    } else if (ext == L".cudoc") {
        bool usedBak = false;
        std::string err;
        if (!CudocStorage::LoadFromFileWithRecovery(path, st->doc, &usedBak, &err)) {
            if (outErr) *outErr = err;
            st->statusBannerUtf8 = "Error .cudoc: " + err;
            return false;
        }
        st->cmdStack.Clear();
        st->pageCache.InvalidateAll(++st->asyncGen);
        st->currentDocPath = path;
        CenterCameraOnPage(st, 0);
        st->statusBannerUtf8 = usedBak ? "Recuperado desde respaldo .bak" : ".cudoc cargado";
    } else if (ext == L".docx") {
        std::string err;
        if (!OpenXmlInterop::ImportFromDocxFile(path, st->doc, &err)) {
            if (outErr) *outErr = err;
            st->statusBannerUtf8 = "Error .docx: " + err;
            return false;
        }
        st->cmdStack.Clear();
        st->pageCache.InvalidateAll(++st->asyncGen);
        st->currentDocPath.clear();
        CenterCameraOnPage(st, 0);
        st->statusBannerUtf8 = "Word .docx importado";
    } else if (ext == L".pptx") {
        std::string err;
        if (!OpenXmlInterop::ImportFromPptxFile(path, st->doc, &err)) {
            if (outErr) *outErr = err;
            st->statusBannerUtf8 = "Error .pptx: " + err;
            return false;
        }
        st->cmdStack.Clear();
        st->pageCache.InvalidateAll(++st->asyncGen);
        st->currentDocPath.clear();
        CenterCameraOnPage(st, 0);
        st->statusBannerUtf8 = "PowerPoint .pptx importado";
    } else if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp") {
        HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li{};
        GetFileSizeEx(hf, &li);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>((std::min<LONGLONG>)(li.QuadPart, 64ll * 1024ll * 1024ll)));
        DWORD rd = 0;
        ReadFile(hf, bytes.data(), static_cast<DWORD>(bytes.size()), &rd, nullptr);
        CloseHandle(hf);
        bytes.resize(rd);

        AnnotationObject img;
        img.kind = ObjectKind::Image;
        img.pngBytes = std::move(bytes);
        img.bounds = { 48.0, 48.0, 480.0, 320.0 };
        if (st->doc.Mode() == DocumentMode::InfiniteCanvas) {
            st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(ObjectTarget{ true, 0 }, std::move(img)));
        } else {
            st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(ObjectTarget{ false, st->activePageIndex }, std::move(img)));
        }
        st->statusBannerUtf8 = "Imagen insertada en estudio";
    } else {
        if (outErr) *outErr = "Extensi\xC3\xB3n de archivo no soportada.";
        return false;
    }

    LayoutMainChildren(st);
    UpdateWindowTitle(st);
    InvalidateViews(st);
    return true;
}

void ExecuteToolbarCommand(StudioAppState* st, WORD id) {
    if (!st) return;
    switch (id) {
    case CMD_OPEN: {
        static constexpr COMDLG_FILTERSPEC kFilters[] = {
            { L"Documentos compatibles (*.pdf;*.cudoc;*.docx;*.pptx;*.png)", L"*.pdf;*.cudoc;*.docx;*.pptx;*.png;*.jpg" },
            { L"Documentos PDF (*.pdf)", L"*.pdf" },
            { L"Document Studio (*.cudoc)", L"*.cudoc" }
        };
        std::vector<std::wstring> paths;
        if (ShowOpenDialog(st->hwndMain, L"Abrir documento", kFilters, 3, paths, false)) {
            std::string err;
            if (!OpenDocumentFile(st, paths[0], &err)) {
                MessageBoxW(st->hwndMain, Utf8ToWide(err).c_str(), L"Error al abrir", MB_ICONERROR);
            }
        }
        break;
    }
    case CMD_SAVE_CUDOC: {
        std::wstring savePath = st->currentDocPath;
        if (savePath.empty()) {
            static constexpr COMDLG_FILTERSPEC kFilters[] = { { L"Document Studio (*.cudoc)", L"*.cudoc" } };
            if (!ShowSaveDialog(st->hwndMain, L"Guardar proyecto .cudoc", kFilters, 1, L"cudoc", L"documento.cudoc", savePath)) break;
        }
        std::string err;
        if (CudocStorage::SaveToFile(savePath, st->doc, &err)) {
            st->currentDocPath = savePath;
            st->cmdStack.MarkClean(st->doc);
            st->statusBannerUtf8 = "Guardado at\xC3\xB3micamente (.cudoc + .bak)";
            UpdateWindowTitle(st);
        } else {
            MessageBoxW(st->hwndMain, Utf8ToWide(err).c_str(), L"Error al guardar", MB_ICONERROR);
        }
        break;
    }
    case CMD_EXPORT_PDF: {
        static constexpr COMDLG_FILTERSPEC kFilters[] = { { L"Archivo PDF (*.pdf)", L"*.pdf" } };
        std::wstring outPath;
        if (!ShowSaveDialog(st->hwndMain, L"Exportar PDF", kFilters, 1, L"pdf", L"exportado.pdf", outPath)) break;
        std::string err;
        bool ok = false;
        if (st->doc.Mode() == DocumentMode::InfiniteCanvas) {
            CanvasPdfExportOptions opts;
            opts.paginated = true;
            ok = PdfWriter::ExportInfiniteCanvasToFile(st->doc, opts, outPath, &err);
        } else {
            ok = PdfWriter::ExportDocumentToFile(st->doc, outPath, &err);
        }
        st->statusBannerUtf8 = ok ? "PDF exportado correctamente" : ("Error PDF: " + err);
        if (!ok) MessageBoxW(st->hwndMain, Utf8ToWide(err).c_str(), L"Error exportando PDF", MB_ICONERROR);
        break;
    }
    case CMD_MERGE_PDF: {
        static constexpr COMDLG_FILTERSPEC kFilters[] = { { L"Archivos PDF (*.pdf)", L"*.pdf" } };
        std::vector<std::wstring> inputs;
        if (!ShowOpenDialog(st->hwndMain, L"Seleccionar PDFs para combinar", kFilters, 1, inputs, true) || inputs.size() < 2) break;
        std::wstring outPath;
        if (!ShowSaveDialog(st->hwndMain, L"Guardar PDF combinado", kFilters, 1, L"pdf", L"combinado.pdf", outPath)) break;
        std::string err;
        if (PdfWriter::MergePdfFiles(inputs, outPath, &err)) {
            OpenDocumentFile(st, outPath);
            st->statusBannerUtf8 = "PDFs combinados (" + std::to_string(inputs.size()) + " archivos)";
        } else {
            MessageBoxW(st->hwndMain, Utf8ToWide(err).c_str(), L"Error combinando PDF", MB_ICONERROR);
        }
        break;
    }
    case CMD_SPLIT_PDF: {
        std::wstring rangeStr = L"1-" + std::to_wstring((std::max<std::size_t>)(1u, st->doc.Pages().size()));
        if (!ShowTextPrompt(st->hwndMain, L"Dividir / Extraer P\u00E1ginas", L"P\u00E1ginas (ej. 1-3, 5):", rangeStr)) break;
        std::vector<std::size_t> picked;
        std::wstringstream wss(rangeStr);
        std::wstring part;
        while (std::getline(wss, part, L',')) {
            const auto dash = part.find(L'-');
            if (dash != std::wstring::npos) {
                const int a = _wtoi(part.substr(0, dash).c_str());
                const int b = _wtoi(part.substr(dash + 1).c_str());
                for (int p = (std::max)(1, a); p <= b && p <= static_cast<int>(st->doc.Pages().size()); ++p) {
                    picked.push_back(static_cast<std::size_t>(p - 1));
                }
            } else {
                const int p = _wtoi(part.c_str());
                if (p >= 1 && p <= static_cast<int>(st->doc.Pages().size())) picked.push_back(static_cast<std::size_t>(p - 1));
            }
        }
        if (picked.empty()) break;
        static constexpr COMDLG_FILTERSPEC kFilters[] = { { L"Archivo PDF (*.pdf)", L"*.pdf" } };
        std::wstring outPath;
        if (!ShowSaveDialog(st->hwndMain, L"Guardar PDF dividido", kFilters, 1, L"pdf", L"extraccion.pdf", outPath)) break;
        DocumentModel subset;
        subset.ResetToBlank(DocumentMode::PaginatedPdf);
        subset.MutablePages().clear();
        for (std::size_t idx : picked) subset.MutablePages().push_back(st->doc.Pages()[idx]);
        subset.RebuildLayout();
        std::string err;
        if (PdfWriter::ExportDocumentToFile(subset, outPath, &err)) {
            st->statusBannerUtf8 = "Extra\xC3\xAD" "das " + std::to_string(picked.size()) + " p\xC3\xA1ginas";
        } else {
            MessageBoxW(st->hwndMain, Utf8ToWide(err).c_str(), L"Error dividiendo PDF", MB_ICONERROR);
        }
        break;
    }
    case CMD_SIGN_PDF: {
        std::wstring signer = L"Usuario Calculadora Unificada";
        std::wstring reason = L"Revisado y aprobado";
        if (!ShowTextPrompt(st->hwndMain, L"Firma Digital PKCS#7 (SHA-256)", L"Firmante:", signer, L"Motivo:", &reason)) break;
        static constexpr COMDLG_FILTERSPEC kFilters[] = { { L"PDF Firmado (*.pdf)", L"*.pdf" } };
        std::wstring outPath;
        if (!ShowSaveDialog(st->hwndMain, L"Guardar PDF firmado", kFilters, 1, L"pdf", L"firmado.pdf", outPath)) break;
        PdfSignatureOptions sopts;
        sopts.signerNameUtf8 = WideToUtf8(signer);
        sopts.reasonUtf8 = WideToUtf8(reason);
        sopts.addVisualStamp = true;
        sopts.visualPageIndex0 = st->activePageIndex;
        std::string err;
        if (PdfSigner::ExportAndSignDocument(st->doc, sopts, outPath, L"", L"", &err)) {
            st->statusBannerUtf8 = "PDF firmado criptogr\xC3\xA1" "ficamente (PKCS#7 SHA-256)";
            MessageBoxW(st->hwndMain, L"Documento firmado con PKCS#7 detached SHA-256.\nPuede verificarlo con el bot\u00F3n 'Verificar'.", L"Firma exitosa", MB_ICONINFORMATION);
        } else {
            MessageBoxW(st->hwndMain, Utf8ToWide(err).c_str(), L"Error firmando PDF", MB_ICONERROR);
        }
        break;
    }
    case CMD_VERIFY_SIG: {
        static constexpr COMDLG_FILTERSPEC kFilters[] = { { L"Archivo PDF (*.pdf)", L"*.pdf" } };
        std::vector<std::wstring> paths;
        if (!ShowOpenDialog(st->hwndMain, L"Seleccionar PDF a verificar", kFilters, 1, paths, false)) break;
        const auto vres = PdfSigner::VerifyPdfFile(paths[0]);
        const UINT icon = (vres.hasSignature && vres.byteRangeValid && vres.cryptographicValid) ? MB_ICONINFORMATION : MB_ICONWARNING;
        std::wstring msg = L"Firmante: " + Utf8ToWide(vres.signerSubjectUtf8.empty() ? "-" : vres.signerSubjectUtf8) +
            L"\nByteRange \u00EDntegro: " + (vres.byteRangeValid ? L"S\u00CD" : L"NO") +
            L"\nFirma criptogr\u00E1fica: " + (vres.cryptographicValid ? L"V\u00C1LIDA" : L"INV\u00C1LIDA") +
            L"\nCadena de sistema: " + (vres.trustedBySystemRoot ? L"De confianza" : L"Autofirmada / no ra\u00EDz") +
            L"\n\n" + Utf8ToWide(vres.diagnosticUtf8);
        MessageBoxW(st->hwndMain, msg.c_str(), L"Verificaci\u00F3n PKCS#7", icon);
        break;
    }
    case CMD_RUN_OCR: {
        if (st->doc.Pages().empty()) break;
        const std::size_t idx = (std::min)(st->activePageIndex, st->doc.Pages().size() - 1);
        RasterImageBgra32 img;
        if (!st->renderer.RenderPageOffscreen(st->doc, idx, 1400, &st->pageCache, img)) {
            st->statusBannerUtf8 = "No se pudo rasterizar p\xC3\xA1gina para OCR";
            break;
        }
        const OcrOutput ocr = WinRtOcrAdapter::RecognizeBgra32(img.width, img.height, img.pixels.data());
        if (ocr.engineAvailable && !ocr.fullTextUtf8.empty()) {
            st->doc.MutablePages()[idx].ocrTextUtf8 = ocr.fullTextUtf8;
            st->doc.BumpRevision();
            st->statusBannerUtf8 = "OCR completado (" + ocr.languageTagUtf8 + "): " + std::to_string(ocr.words.size()) + " palabras";
            MessageBoxW(st->hwndMain, Utf8ToWide(ocr.fullTextUtf8.substr(0, 1200)).c_str(), L"Texto reconocido (OCR offline)", MB_ICONINFORMATION);
        } else {
            const std::string msg = ocr.errorUtf8.empty() ? "Sin texto detectado en esta p\xC3\xA1gina." : ocr.errorUtf8;
            st->statusBannerUtf8 = msg;
            MessageBoxW(st->hwndMain, Utf8ToWide(msg).c_str(), L"Resultado OCR", MB_ICONINFORMATION);
        }
        break;
    }
    case CMD_INTEROP_MENU: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, CMD_EXPORT_DOCX, L"Exportar a Word (.docx)...");
        AppendMenuW(menu, MF_STRING, CMD_EXPORT_PPTX, L"Exportar a PowerPoint (.pptx)...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CMD_IMPORT_DOCX, L"Importar Word (.docx)...");
        AppendMenuW(menu, MF_STRING, CMD_IMPORT_PPTX, L"Importar PowerPoint (.pptx)...");
        POINT pt{ 540, 34 };
        for (std::size_t i = 0; i < st->buttons.size(); ++i) {
            if (st->buttons[i].id == CMD_INTEROP_MENU) {
                pt = { st->buttonRects[i].left, st->buttonRects[i].bottom };
                break;
            }
        }
        ClientToScreen(st->hwndMain, &pt);
        const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, st->hwndMain, nullptr);
        DestroyMenu(menu);
        if (cmd != 0) ExecuteToolbarCommand(st, static_cast<WORD>(cmd));
        break;
    }
    case CMD_EXPORT_DOCX:
    case CMD_EXPORT_PPTX: {
        const bool isDocx = (id == CMD_EXPORT_DOCX);
        const COMDLG_FILTERSPEC spec = isDocx
            ? COMDLG_FILTERSPEC{ L"Word (*.docx)", L"*.docx" }
            : COMDLG_FILTERSPEC{ L"PowerPoint (*.pptx)", L"*.pptx" };
        std::wstring outPath;
        if (!ShowSaveDialog(st->hwndMain, isDocx ? L"Exportar Word .docx" : L"Exportar PowerPoint .pptx", &spec, 1,
            isDocx ? L"docx" : L"pptx", isDocx ? L"documento.docx" : L"presentacion.pptx", outPath)) break;

        std::vector<std::vector<std::uint8_t>> snaps;
        const std::size_t maxSnapPages = (std::min<std::size_t>)(st->doc.Pages().size(), 12u);
        for (std::size_t p = 0; p < maxSnapPages; ++p) {
            RasterImageBgra32 img;
            std::vector<std::uint8_t> png;
            if (st->renderer.RenderPageOffscreen(st->doc, p, 900, &st->pageCache, img) &&
                WinRtPdfRenderer::EncodeBgra32ToPngBytes(img, png)) {
                snaps.push_back(std::move(png));
            } else {
                snaps.push_back({});
            }
        }
        std::string err;
        const bool ok = isDocx
            ? OpenXmlInterop::ExportToDocxFile(st->doc, outPath, snaps, &err)
            : OpenXmlInterop::ExportToPptxFile(st->doc, outPath, snaps, &err);
        st->statusBannerUtf8 = ok ? "Exportaci\xC3\xB3n OpenXML completada" : err;
        if (!ok) MessageBoxW(st->hwndMain, Utf8ToWide(err).c_str(), L"Error OpenXML", MB_ICONERROR);
        break;
    }
    case CMD_IMPORT_DOCX:
    case CMD_IMPORT_PPTX: {
        const bool isDocx = (id == CMD_IMPORT_DOCX);
        const COMDLG_FILTERSPEC spec = isDocx
            ? COMDLG_FILTERSPEC{ L"Word (*.docx)", L"*.docx" }
            : COMDLG_FILTERSPEC{ L"PowerPoint (*.pptx)", L"*.pptx" };
        std::vector<std::wstring> paths;
        if (ShowOpenDialog(st->hwndMain, L"Importar Office", &spec, 1, paths, false)) {
            OpenDocumentFile(st, paths[0]);
        }
        break;
    }
    case CMD_PRINT: {
        PRINTDLGW pd{};
        pd.lStructSize = sizeof(pd);
        pd.hwndOwner = st->hwndMain;
        pd.Flags = PD_RETURNDC | PD_USEDEVMODECOPIESANDCOLLATE | PD_NOSELECTION;
        pd.nMinPage = 1;
        pd.nMaxPage = static_cast<WORD>((std::max<std::size_t>)(1u, st->doc.Pages().size()));
        pd.nFromPage = 1;
        pd.nToPage = pd.nMaxPage;
        if (!PrintDlgW(&pd) || !pd.hDC) break;

        DOCINFOW di{};
        di.cbSize = sizeof(di);
        di.lpszDocName = L"Document Studio";
        if (StartDocW(pd.hDC, &di) > 0) {
            const int dpiW = GetDeviceCaps(pd.hDC, HORZRES);
            const int dpiH = GetDeviceCaps(pd.hDC, VERTRES);
            const std::size_t pStart = (pd.Flags & PD_PAGENUMS) ? (pd.nFromPage - 1) : 0;
            const std::size_t pEnd = (pd.Flags & PD_PAGENUMS) ? (std::min<std::size_t>)(pd.nToPage, st->doc.Pages().size()) : st->doc.Pages().size();
            for (std::size_t p = pStart; p < pEnd; ++p) {
                RasterImageBgra32 img;
                if (!st->renderer.RenderPageOffscreen(st->doc, p, 1600, &st->pageCache, img)) continue;
                if (StartPage(pd.hDC) > 0) {
                    BITMAPINFO bmi{};
                    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                    bmi.bmiHeader.biWidth = img.width;
                    bmi.bmiHeader.biHeight = -img.height;
                    bmi.bmiHeader.biPlanes = 1;
                    bmi.bmiHeader.biBitCount = 32;
                    bmi.bmiHeader.biCompression = BI_RGB;
                    const double scale = (std::min)(static_cast<double>(dpiW) / img.width, static_cast<double>(dpiH) / img.height);
                    const int dstW = static_cast<int>(img.width * scale);
                    const int dstH = static_cast<int>(img.height * scale);
                    const int dstX = (dpiW - dstW) / 2;
                    const int dstY = (dpiH - dstH) / 2;
                    StretchDIBits(pd.hDC, dstX, dstY, dstW, dstH, 0, 0, img.width, img.height,
                        img.pixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
                    EndPage(pd.hDC);
                }
            }
            EndDoc(pd.hDC);
        }
        DeleteDC(pd.hDC);
        st->statusBannerUtf8 = "Trabajo de impresi\xC3\xB3n enviado";
        break;
    }
    case CMD_MODE_PDF:
        st->doc.SetMode(DocumentMode::PaginatedPdf);
        st->doc.RebuildLayout();
        CenterCameraOnPage(st, st->activePageIndex);
        LayoutMainChildren(st);
        break;
    case CMD_MODE_CANVAS:
        st->doc.SetMode(DocumentMode::InfiniteCanvas);
        st->camera.panX = -50.0;
        st->camera.panY = -50.0;
        st->camera.zoom = 1.0;
        LayoutMainChildren(st);
        break;
    case CMD_TOOL_HAND:   st->activeTool = ToolKind::Hand; break;
    case CMD_TOOL_PEN:    st->activeTool = ToolKind::Pen; break;
    case CMD_TOOL_HL:     st->activeTool = ToolKind::Highlighter; break;
    case CMD_TOOL_ERASER: st->activeTool = ToolKind::Eraser; break;
    case CMD_TOOL_RECT:   st->activeTool = ToolKind::ShapeRect; break;
    case CMD_TOOL_ARROW:  st->activeTool = ToolKind::ShapeArrow; break;
    case CMD_TOOL_TEXT:   st->activeTool = ToolKind::TextBox; break;
    case CMD_TOOL_CROP:   st->activeTool = ToolKind::Crop; break;
    case CMD_INSERT_IMAGE: {
        static constexpr COMDLG_FILTERSPEC kFilters[] = { { L"Im\u00E1genes (*.png;*.jpg;*.bmp)", L"*.png;*.jpg;*.jpeg;*.bmp" } };
        std::vector<std::wstring> paths;
        if (ShowOpenDialog(st->hwndMain, L"Insertar imagen", kFilters, 1, paths, false)) {
            OpenDocumentFile(st, paths[0]);
        }
        break;
    }
    case CMD_COL_BLACK:  st->penColor = DocColor::RGBA(17, 24, 39, 255); break;
    case CMD_COL_BLUE:   st->penColor = DocColor::RGBA(26, 115, 232, 255); break;
    case CMD_COL_RED:    st->penColor = DocColor::RGBA(220, 38, 38, 255); break;
    case CMD_COL_GREEN:  st->penColor = DocColor::RGBA(21, 128, 61, 255); break;
    case CMD_COL_YELLOW: st->penColor = DocColor::RGBA(250, 204, 21, 255); st->activeTool = ToolKind::Highlighter; break;
    case CMD_PAPER_NEXT: {
        const PaperPattern cur = (st->doc.Mode() == DocumentMode::InfiniteCanvas)
            ? st->doc.CanvasBackground()
            : (st->doc.Pages().empty() ? PaperPattern::Plain : st->doc.Pages()[st->activePageIndex].background);
        const PaperPattern next = static_cast<PaperPattern>((static_cast<int>(cur) + 1) % 5);
        st->cmdStack.Execute(st->doc, std::make_unique<SetBackgroundCommand>(
            ObjectTarget{ st->doc.Mode() == DocumentMode::InfiniteCanvas, st->activePageIndex }, next));
        break;
    }
    case CMD_ADD_PAGE: {
        PageModel blank;
        blank.background = PaperPattern::Grid;
        st->cmdStack.Execute(st->doc, std::make_unique<InsertPageCommand>(st->activePageIndex + 1, std::move(blank)));
        CenterCameraOnPage(st, st->activePageIndex + 1);
        break;
    }
    case CMD_ROTATE_PAGE:
        st->cmdStack.Execute(st->doc, std::make_unique<RotatePageCommand>(st->activePageIndex, 90));
        break;
    case CMD_DEL_PAGE:
        if (!st->cmdStack.Execute(st->doc, std::make_unique<DeletePageCommand>(st->activePageIndex))) {
            st->statusBannerUtf8 = "Debe permanecer al menos 1 p\xC3\xA1gina";
        } else if (st->activePageIndex >= st->doc.Pages().size()) {
            st->activePageIndex = st->doc.Pages().size() - 1;
        }
        break;
    case CMD_PAGE_UP:
        if (st->activePageIndex > 0) {
            if (st->cmdStack.Execute(st->doc, std::make_unique<ReorderPageCommand>(st->activePageIndex, st->activePageIndex - 1))) {
                --st->activePageIndex;
            }
        }
        break;
    case CMD_PAGE_DOWN:
        if (st->activePageIndex + 1 < st->doc.Pages().size()) {
            if (st->cmdStack.Execute(st->doc, std::make_unique<ReorderPageCommand>(st->activePageIndex, st->activePageIndex + 1))) {
                ++st->activePageIndex;
            }
        }
        break;
    case CMD_UNDO: st->cmdStack.Undo(st->doc); break;
    case CMD_REDO: st->cmdStack.Redo(st->doc); break;
    case CMD_ZOOM_OUT: {
        RECT rc{};
        GetClientRect(st->hwndViewport, &rc);
        st->camera.ZoomByFactorAtAnchor((rc.right - rc.left) * 0.5, (rc.bottom - rc.top) * 0.5, 0.85);
        break;
    }
    case CMD_ZOOM_IN: {
        RECT rc{};
        GetClientRect(st->hwndViewport, &rc);
        st->camera.ZoomByFactorAtAnchor((rc.right - rc.left) * 0.5, (rc.bottom - rc.top) * 0.5, 1.18);
        break;
    }
    case CMD_ZOOM_RESET:
        CenterCameraOnPage(st, st->activePageIndex);
        break;
    case CMD_SEARCH_NEXT: {
        wchar_t qbuf[256]{};
        if (st->hwndSearchEdit) GetWindowTextW(st->hwndSearchEdit, qbuf, 256);
        const std::string qUtf8 = WideToUtf8(qbuf);
        if (qUtf8.empty()) {
            st->searchMatches.clear();
            break;
        }
        st->searchMatches = st->doc.SearchText(qUtf8, false);
        if (st->searchMatches.empty()) {
            st->statusBannerUtf8 = "Sin resultados para: " + qUtf8;
        } else {
            st->activeSearchIdx = (st->activeSearchIdx + 1) % st->searchMatches.size();
            CenterCameraOnPage(st, st->searchMatches[st->activeSearchIdx].pageIndex);
            st->statusBannerUtf8 = "Coincidencia " + std::to_string(st->activeSearchIdx + 1) + "/" + std::to_string(st->searchMatches.size());
        }
        break;
    }
    case CMD_REPLACE_TEXT: {
        wchar_t qbuf[256]{};
        if (st->hwndSearchEdit) GetWindowTextW(st->hwndSearchEdit, qbuf, 256);
        std::wstring oldW = qbuf;
        std::wstring newW;
        if (!ShowTextPrompt(st->hwndMain, L"Reemplazar / Redactar texto", L"Texto actual:", oldW, L"Nuevo texto:", &newW)) break;
        const std::string oldUtf8 = WideToUtf8(oldW);
        const std::string newUtf8 = WideToUtf8(newW);
        std::string report = "Modo cobertura/redactado + TextBox preparado.";
        if (!st->activePdfSourcePathUtf8.empty()) {
            PdfDocument pdf;
            if (pdf.LoadFromFile(Utf8ToWide(st->activePdfSourcePathUtf8))) {
                const auto cap = pdf.InspectTextReplacement(st->activePageIndex, oldUtf8);
                report = cap.diagnosticUtf8;
            }
        }
        // Always place clean opaque white background TextBox at active page center as visible redaction overlay
        AnnotationObject redactBox;
        redactBox.kind = ObjectKind::TextBox;
        redactBox.bounds = { 80.0, 120.0, 320.0, 34.0 };
        redactBox.fillColor = DocColor::RGBA(255, 255, 255, 255);
        redactBox.strokeColor = DocColor::RGBA(17, 24, 39, 255);
        redactBox.fontSizePt = 13.0;
        redactBox.textUtf8 = newUtf8;
        st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(ObjectTarget{ false, st->activePageIndex }, std::move(redactBox)));
        st->statusBannerUtf8 = report;
        MessageBoxW(st->hwndMain, Utf8ToWide(report).c_str(), L"Reemplazo sem\u00E1ntico PDF", MB_ICONINFORMATION);
        break;
    }
    }
    UpdateWindowTitle(st);
    InvalidateViews(st);
}

// ---------------- Hit-testing for Eraser & Selection ----------------

bool PointHitsAnnotation(const AnnotationObject& obj, double lx, double ly, double tolWorld) {
    if (!obj.bounds.Contains(lx, ly, tolWorld)) return false;
    if (obj.kind == ObjectKind::InkStroke && !obj.inkPoints.empty()) {
        for (const auto& pt : obj.inkPoints) {
            if (std::hypot(pt.x - lx, pt.y - ly) <= (tolWorld + obj.strokeWidth)) return true;
        }
        return false;
    }
    return true;
}

std::vector<std::uint64_t> CollectHits(StudioAppState* st, bool isCanvas, std::size_t pageIdx, double lx, double ly, double tol) {
    std::vector<std::uint64_t> hits;
    if (isCanvas) {
        const auto candidates = st->doc.QueryCanvasRect({ lx - tol, ly - tol, tol * 2.0, tol * 2.0 });
        for (std::uint64_t id : candidates) {
            if (const auto* o = st->doc.FindCanvasObject(id)) {
                if (PointHitsAnnotation(*o, lx, ly, tol)) hits.push_back(id);
            }
        }
    } else if (pageIdx < st->doc.Pages().size()) {
        for (const auto& o : st->doc.Pages()[pageIdx].objects) {
            if (PointHitsAnnotation(o, lx, ly, tol)) hits.push_back(o.id);
        }
    }
    return hits;
}

// ---------------- Viewport WndProc ----------------

LRESULT CALLBACK ViewportWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StudioAppState* st = reinterpret_cast<StudioAppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    auto extractPointerWorld = [&](LPARAM lParam, float& outPressure, DocPoint& outScreen) {
        outPressure = 0.55f;
        outScreen = { static_cast<double>(GET_X_LPARAM(lParam)), static_cast<double>(GET_Y_LPARAM(lParam)) };
    };

    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<StudioAppState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (st) st->renderer.Resize(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (st) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            ViewportOverlayState ov{};
            ov.draftObject = st->drawing ? &st->draftObj : nullptr;
            ov.draftPageIndex = st->draftPageIdx;
            ov.selectedObjectId = st->selectedObjectId;
            ov.hasCropPreview = st->hasDraftCrop;
            ov.cropPageIndex = st->draftPageIdx;
            ov.cropPreviewPageLocal = st->draftCropPageLocal;
            ov.searchMatches = st->searchMatches.empty() ? nullptr : &st->searchMatches;
            ov.activeSearchMatchIndex = st->activeSearchIdx;

            HWND mainH = st->hwndMain;
            st->renderer.RenderViewport(
                st->doc, st->camera,
                (std::max)(1, static_cast<int>(rc.right - rc.left)),
                (std::max)(1, static_cast<int>(rc.bottom - rc.top)),
                &st->pageCache,
                st->asyncGen,
                [mainH]() { if (IsWindow(mainH)) PostMessageW(mainH, WM_APP_ASYNC_PAGE_READY, 0, 0); },
                &ov);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!st) break;
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0 || st->doc.Mode() == DocumentMode::InfiniteCanvas;
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &pt);
        if (ctrl) {
            const double factor = (delta > 0) ? 1.12 : 0.89;
            st->camera.ZoomByFactorAtAnchor(pt.x, pt.y, factor);
        } else {
            st->camera.panY -= (static_cast<double>(delta) / WHEEL_DELTA) * (72.0 / (std::max)(0.2, st->camera.zoom));
            RefreshActivePageFromCamera(st);
        }
        InvalidateViews(st);
        return 0;
    }
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        if (!st) break;
        st->panning = true;
        st->lastPanPt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        SetCapture(hwnd);
        return 0;
    }
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        if (st && st->panning) {
            st->panning = false;
            ReleaseCapture();
        }
        return 0;
    }
    case WM_POINTERDOWN:
    case WM_LBUTTONDOWN: {
        if (!st) break;
        SetFocus(hwnd);
        float pressure = 0.55f;
        DocPoint sp{};
        if (msg == WM_POINTERDOWN) {
            const UINT32 pid = GET_POINTERID_WPARAM(wp);
            POINTER_INFO pi{};
            if (GetPointerInfo(pid, &pi)) {
                POINT pt = pi.ptPixelLocation;
                ScreenToClient(hwnd, &pt);
                sp = { static_cast<double>(pt.x), static_cast<double>(pt.y) };
                POINTER_PEN_INFO pen{};
                if (pi.pointerType == PT_PEN && GetPointerPenInfo(pid, &pen)) {
                    pressure = std::clamp(static_cast<float>(pen.pressure) / 1024.0f, 0.1f, 1.0f);
                }
            }
        } else {
            extractPointerWorld(lp, pressure, sp);
        }

        if (st->activeTool == ToolKind::Hand || (GetKeyState(VK_SPACE) & 0x8000)) {
            st->panning = true;
            st->lastPanPt = { static_cast<LONG>(sp.x), static_cast<LONG>(sp.y) };
            SetCapture(hwnd);
            return 0;
        }

        const DocPoint wpWorld = st->camera.ScreenToWorld(sp.x, sp.y);
        bool isCanvas = (st->doc.Mode() == DocumentMode::InfiniteCanvas);
        std::size_t pgIdx = st->activePageIndex;
        double lx = wpWorld.x, ly = wpWorld.y;
        if (!isCanvas) {
            if (!st->doc.Layout().HitTestWorldPoint(wpWorld.x, wpWorld.y, pgIdx, lx, ly)) {
                return 0;
            }
            st->activePageIndex = pgIdx;
        }

        if (st->activeTool == ToolKind::Eraser) {
            const auto hits = CollectHits(st, isCanvas, pgIdx, lx, ly, 10.0);
            if (!hits.empty()) {
                st->cmdStack.Execute(st->doc, std::make_unique<BatchEraseObjectsCommand>(ObjectTarget{ isCanvas, pgIdx }, hits));
                UpdateWindowTitle(st);
                InvalidateViews(st);
            }
            return 0;
        }

        if (st->activeTool == ToolKind::TextBox) {
            std::wstring text = L"";
            if (ShowTextPrompt(st->hwndMain, L"Insertar Cuadro de Texto", L"Contenido:", text) && !text.empty()) {
                AnnotationObject tb;
                tb.kind = ObjectKind::TextBox;
                tb.bounds = { lx, ly, 240.0, 80.0 };
                tb.textUtf8 = WideToUtf8(text);
                tb.strokeColor = st->penColor;
                tb.fontSizePt = 14.0;
                st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(ObjectTarget{ isCanvas, pgIdx }, std::move(tb)));
                UpdateWindowTitle(st);
                InvalidateViews(st);
            }
            return 0;
        }

        st->drawing = true;
        st->draftIsCanvas = isCanvas;
        st->draftPageIdx = pgIdx;
        st->draftAnchorWorld = { lx, ly };
        st->hasDraftCrop = false;
        st->draftObj = AnnotationObject{};
        st->draftObj.strokeColor = st->penColor;
        st->draftObj.strokeWidth = (st->activeTool == ToolKind::Highlighter) ? 14.0 : st->penWidth;
        st->draftObj.isHighlighter = (st->activeTool == ToolKind::Highlighter);

        if (st->activeTool == ToolKind::Pen || st->activeTool == ToolKind::Highlighter) {
            st->draftObj.kind = ObjectKind::InkStroke;
            st->draftObj.inkPoints.push_back({ lx, ly, pressure });
        } else if (st->activeTool == ToolKind::ShapeRect) {
            st->draftObj.kind = ObjectKind::Shape;
            st->draftObj.shapeKind = ShapeKind::Rectangle;
            st->draftObj.bounds = { lx, ly, 1.0, 1.0 };
        } else if (st->activeTool == ToolKind::ShapeArrow) {
            st->draftObj.kind = ObjectKind::Shape;
            st->draftObj.shapeKind = ShapeKind::Arrow;
            st->draftObj.shapeStart = { lx, ly };
            st->draftObj.shapeEnd = { lx, ly };
        } else if (st->activeTool == ToolKind::Crop && !isCanvas) {
            st->hasDraftCrop = true;
            st->draftCropPageLocal = { lx, ly, 1.0, 1.0 };
        }

        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_POINTERUPDATE:
    case WM_MOUSEMOVE: {
        if (!st) break;
        float pressure = 0.55f;
        DocPoint sp{};
        if (msg == WM_POINTERUPDATE) {
            const UINT32 pid = GET_POINTERID_WPARAM(wp);
            POINTER_INFO pi{};
            if (GetPointerInfo(pid, &pi)) {
                POINT pt = pi.ptPixelLocation;
                ScreenToClient(hwnd, &pt);
                sp = { static_cast<double>(pt.x), static_cast<double>(pt.y) };
                POINTER_PEN_INFO pen{};
                if (pi.pointerType == PT_PEN && GetPointerPenInfo(pid, &pen)) {
                    pressure = std::clamp(static_cast<float>(pen.pressure) / 1024.0f, 0.1f, 1.0f);
                }
            }
        } else {
            extractPointerWorld(lp, pressure, sp);
        }

        if (st->panning) {
            const double dx = sp.x - st->lastPanPt.x;
            const double dy = sp.y - st->lastPanPt.y;
            st->camera.panX -= dx / st->camera.zoom;
            st->camera.panY -= dy / st->camera.zoom;
            st->lastPanPt = { static_cast<LONG>(sp.x), static_cast<LONG>(sp.y) };
            RefreshActivePageFromCamera(st);
            InvalidateViews(st);
            return 0;
        }

        if (!st->drawing) return 0;

        const DocPoint wpWorld = st->camera.ScreenToWorld(sp.x, sp.y);
        double lx = wpWorld.x, ly = wpWorld.y;
        if (!st->draftIsCanvas) {
            if (const auto* pl = st->doc.Layout().PlacementAt(st->draftPageIdx)) {
                const DocRect wr = pl->WorldRect();
                lx = wpWorld.x - wr.x;
                ly = wpWorld.y - wr.y;
            }
        }

        if (st->draftObj.kind == ObjectKind::InkStroke) {
            if (!st->draftObj.inkPoints.empty()) {
                const auto& prev = st->draftObj.inkPoints.back();
                if (std::hypot(lx - prev.x, ly - prev.y) >= 1.2) {
                    st->draftObj.inkPoints.push_back({ lx, ly, pressure });
                }
            }
        } else if (st->hasDraftCrop) {
            st->draftCropPageLocal = DocRect::FromMinMax(
                (std::min)(st->draftAnchorWorld.x, lx), (std::min)(st->draftAnchorWorld.y, ly),
                (std::max)(st->draftAnchorWorld.x, lx), (std::max)(st->draftAnchorWorld.y, ly));
        } else if (st->draftObj.shapeKind == ShapeKind::Rectangle) {
            st->draftObj.bounds = DocRect::FromMinMax(
                (std::min)(st->draftAnchorWorld.x, lx), (std::min)(st->draftAnchorWorld.y, ly),
                (std::max)(st->draftAnchorWorld.x, lx), (std::max)(st->draftAnchorWorld.y, ly));
        } else if (st->draftObj.shapeKind == ShapeKind::Arrow) {
            st->draftObj.shapeEnd = { lx, ly };
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_POINTERUP:
    case WM_LBUTTONUP: {
        if (!st) break;
        if (st->panning) {
            st->panning = false;
            ReleaseCapture();
            return 0;
        }
        if (!st->drawing) return 0;
        st->drawing = false;
        ReleaseCapture();

        if (st->hasDraftCrop && !st->draftIsCanvas && st->draftPageIdx < st->doc.Pages().size()) {
            const auto& pg = st->doc.Pages()[st->draftPageIdx];
            const double pw = (std::max)(1.0, pg.EffectiveWidthPt());
            const double ph = (std::max)(1.0, pg.EffectiveHeightPt());
            if (st->draftCropPageLocal.w >= 24.0 && st->draftCropPageLocal.h >= 24.0) {
                NormalizedCropRect nc{
                    pg.crop.left + (st->draftCropPageLocal.x / pw) * (pg.crop.right - pg.crop.left),
                    pg.crop.top + (st->draftCropPageLocal.y / ph) * (pg.crop.bottom - pg.crop.top),
                    pg.crop.left + (st->draftCropPageLocal.Right() / pw) * (pg.crop.right - pg.crop.left),
                    pg.crop.top + (st->draftCropPageLocal.Bottom() / ph) * (pg.crop.bottom - pg.crop.top)
                };
                nc.Clamp();
                st->cmdStack.Execute(st->doc, std::make_unique<CropPageCommand>(st->draftPageIdx, nc));
            }
            st->hasDraftCrop = false;
        } else {
            st->draftObj.RecomputeBounds();
            if (!st->draftObj.bounds.Empty()) {
                st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(
                    ObjectTarget{ st->draftIsCanvas, st->draftPageIdx }, std::move(st->draftObj)));
            }
        }
        UpdateWindowTitle(st);
        InvalidateViews(st);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------- Virtualized Sidebar WndProc ----------------

LRESULT CALLBACK SidebarWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StudioAppState* st = reinterpret_cast<StudioAppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    constexpr int kItemH = 112;
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<StudioAppState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEWHEEL: {
        if (!st) break;
        const int delta = GET_WHEEL_DELTA_WPARAM(wp);
        const int maxScroll = (std::max)(0, static_cast<int>(st->doc.Pages().size()) * kItemH - 300);
        st->sidebarScrollPos = std::clamp(st->sidebarScrollPos - (delta / 3), 0, maxScroll);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (!st) break;
        const int y = GET_Y_LPARAM(lp) + st->sidebarScrollPos;
        const std::size_t idx = static_cast<std::size_t>((std::max)(0, y) / kItemH);
        if (idx < st->doc.Pages().size()) {
            const int x = GET_X_LPARAM(lp);
            const int localY = y - static_cast<int>(idx) * kItemH;
            st->activePageIndex = idx;
            // Mini reorder buttons at bottom-right of selected card
            if (x >= 145 && localY >= kItemH - 26 && localY < kItemH - 4) {
                ExecuteToolbarCommand(st, (x < 168) ? CMD_PAGE_UP : CMD_PAGE_DOWN);
                return 0;
            }
            CenterCameraOnPage(st, idx);
            InvalidateViews(st);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (st) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);

            HBRUSH bgBr = CreateSolidBrush(RGB(241, 245, 249));
            FillRect(mem, &rc, bgBr);
            DeleteObject(bgBr);

            SetBkMode(mem, TRANSPARENT);
            SelectObject(mem, st->hUiFont);

            const std::size_t count = st->doc.Pages().size();
            const std::size_t first = static_cast<std::size_t>((std::max)(0, st->sidebarScrollPos / kItemH));
            const std::size_t last = (std::min)(count, first + static_cast<std::size_t>(rc.bottom / kItemH + 2));

            for (std::size_t i = first; i < last; ++i) {
                const int top = static_cast<int>(i) * kItemH - st->sidebarScrollPos + 6;
                RECT card{ 10, top, rc.right - 10, top + kItemH - 12 };
                const bool active = (i == st->activePageIndex);
                HBRUSH cardBr = CreateSolidBrush(active ? RGB(219, 234, 254) : RGB(255, 255, 255));
                HPEN cardPen = CreatePen(PS_SOLID, active ? 2 : 1, active ? RGB(37, 99, 235) : RGB(203, 213, 225));
                SelectObject(mem, cardBr);
                SelectObject(mem, cardPen);
                RoundRect(mem, card.left, card.top, card.right, card.bottom, 8, 8);
                DeleteObject(cardBr);
                DeleteObject(cardPen);

                // Thumbnail miniature preview if already in cache (non-blocking)
                const auto& pg = st->doc.Pages()[i];
                RECT thumb{ card.left + 10, card.top + 8, card.left + 68, card.bottom - 8 };
                Rectangle(mem, thumb.left, thumb.top, thumb.right, thumb.bottom);
                if (pg.hasPdfSource) {
                    const PageCacheKey tkey = PageRenderCache::MakeKey(pg, 160);
                    RasterImageBgra32 img;
                    if (st->pageCache.TryGet(tkey, img) && img.Valid()) {
                        BITMAPINFO bmi{};
                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bmi.bmiHeader.biWidth = img.width;
                        bmi.bmiHeader.biHeight = -img.height;
                        bmi.bmiHeader.biPlanes = 1;
                        bmi.bmiHeader.biBitCount = 32;
                        bmi.bmiHeader.biCompression = BI_RGB;
                        SetStretchBltMode(mem, HALFTONE);
                        StretchDIBits(mem, thumb.left + 1, thumb.top + 1, (thumb.right - thumb.left) - 2, (thumb.bottom - thumb.top) - 2,
                            0, 0, img.width, img.height, img.pixels.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);
                    } else {
                        HWND mainH = st->hwndMain;
                        st->pageCache.RequestAsync(tkey, st->asyncGen, [mainH]() {
                            if (IsWindow(mainH)) PostMessageW(mainH, WM_APP_ASYNC_PAGE_READY, 0, 0);
                        });
                    }
                }

                SetTextColor(mem, RGB(15, 23, 42));
                const std::wstring lbl = L"P\u00E1g. " + std::to_wstring(i + 1) + L"\n" +
                    std::to_wstring(static_cast<int>(pg.EffectiveWidthPt())) + L"x" +
                    std::to_wstring(static_cast<int>(pg.EffectiveHeightPt())) + L" pt";
                RECT txtRc{ card.left + 76, card.top + 10, card.right - 8, card.bottom - 26 };
                DrawTextW(mem, lbl.c_str(), static_cast<int>(lbl.size()), &txtRc, DT_LEFT | DT_WORDBREAK);

                SetTextColor(mem, RGB(71, 85, 105));
                RECT arrowRc{ card.right - 46, card.bottom - 22, card.right - 6, card.bottom - 4 };
                DrawTextW(mem, L"\u25B2 \u25BC", 3, &arrowRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------- Main Studio WndProc ----------------

void PaintRibbonAndStatus(StudioAppState* st, HDC hdc) {
    RECT rc{};
    GetClientRect(st->hwndMain, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);

    // Top Ribbon background
    RECT rib{ 0, 0, rc.right, 74 };
    HBRUSH ribBr = CreateSolidBrush(RGB(248, 250, 252));
    FillRect(mem, &rib, ribBr);
    DeleteObject(ribBr);
    HPEN sepPen = CreatePen(PS_SOLID, 1, RGB(203, 213, 225));
    SelectObject(mem, sepPen);
    MoveToEx(mem, 0, 36, nullptr); LineTo(mem, rc.right, 36);
    MoveToEx(mem, 0, 73, nullptr); LineTo(mem, rc.right, 73);
    DeleteObject(sepPen);

    SetBkMode(mem, TRANSPARENT);
    SelectObject(mem, st->hUiFont);

    for (std::size_t i = 0; i < st->buttons.size() && i < st->buttonRects.size(); ++i) {
        const auto& b = st->buttons[i];
        const RECT& br = st->buttonRects[i];
        const bool checked = IsToolbarButtonChecked(st, b.id);
        const bool hovered = (static_cast<int>(i) == st->hoveredButton);

        if (b.isSwatch) {
            HBRUSH sw = CreateSolidBrush(b.swatchColor);
            HPEN sp = CreatePen(PS_SOLID, checked ? 2 : 1, checked ? RGB(37, 99, 235) : RGB(100, 116, 139));
            SelectObject(mem, sw);
            SelectObject(mem, sp);
            Ellipse(mem, br.left + 2, br.top + 3, br.right - 2, br.bottom - 3);
            DeleteObject(sw);
            DeleteObject(sp);
            continue;
        }

        COLORREF fill = RGB(255, 255, 255);
        COLORREF border = RGB(203, 213, 225);
        COLORREF txtCol = RGB(15, 23, 42);
        if (b.accent) {
            fill = hovered ? RGB(29, 78, 216) : RGB(37, 99, 235);
            border = RGB(30, 64, 175);
            txtCol = RGB(255, 255, 255);
        } else if (checked) {
            fill = RGB(219, 234, 254);
            border = RGB(37, 99, 235);
            txtCol = RGB(30, 64, 175);
        } else if (hovered) {
            fill = RGB(241, 245, 249);
            border = RGB(148, 163, 184);
        }

        HBRUSH fBr = CreateSolidBrush(fill);
        HPEN fPen = CreatePen(PS_SOLID, 1, border);
        SelectObject(mem, fBr);
        SelectObject(mem, fPen);
        RoundRect(mem, br.left, br.top, br.right, br.bottom, 6, 6);
        DeleteObject(fBr);
        DeleteObject(fPen);

        SetTextColor(mem, txtCol);
        RECT tr = br;
        DrawTextW(mem, b.label, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Search action labels to the right of Search Edit
    if (st->hwndSearchEdit) {
        RECT seRc{};
        GetWindowRect(st->hwndSearchEdit, &seRc);
        MapWindowPoints(HWND_DESKTOP, st->hwndMain, reinterpret_cast<LPPOINT>(&seRc), 2);
        // Draw tiny status hint or search/replace badges via toolbar? Keep CMD_SEARCH_NEXT / CMD_REPLACE_TEXT clickable!
        RECT bFind{ seRc.right + 4, 8, seRc.right + 46, 30 };
        RECT bRepl{ seRc.right + 50, 8, seRc.right + 90, 30 };
        HBRUSH fBr = CreateSolidBrush(RGB(226, 232, 240));
        FillRect(mem, &bFind, fBr);
        FillRect(mem, &bRepl, fBr);
        DeleteObject(fBr);
        SetTextColor(mem, RGB(15, 23, 42));
        DrawTextW(mem, L"Ir", -1, &bFind, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(mem, L"Reem.", -1, &bRepl, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Bottom Status Bar
    RECT stBar{ 0, rc.bottom - 24, rc.right, rc.bottom };
    HBRUSH stBr = CreateSolidBrush(RGB(241, 245, 249));
    FillRect(mem, &stBar, stBr);
    DeleteObject(stBr);

    const double usedMiB = static_cast<double>(st->pageCache.BytesUsed()) / (1024.0 * 1024.0);
    const double maxMiB = static_cast<double>(st->pageCache.MaxBytes()) / (1024.0 * 1024.0);
    std::wostringstream status;
    status << (st->doc.Mode() == DocumentMode::InfiniteCanvas ? L"Lienzo Infinito" : L"PDF Paginado")
           << L" | P\u00E1g. " << (st->activePageIndex + 1) << L"/" << st->doc.Pages().size()
           << L" | Zoom: " << static_cast<int>(std::lround(st->camera.zoom * 100.0)) << L"%"
           << L" | Cach\u00E9: " << static_cast<int>(std::lround(usedMiB)) << L"/" << static_cast<int>(std::lround(maxMiB)) << L" MiB";
    if (st->doc.Mode() == DocumentMode::InfiniteCanvas) {
        status << L" | Quadtree: " << st->doc.CanvasQuadtreeSize() << L" objs";
    }
    status << L" | " << Utf8ToWide(st->statusBannerUtf8);

    SetTextColor(mem, RGB(51, 65, 85));
    RECT stTxt{ 10, rc.bottom - 22, rc.right - 10, rc.bottom - 2 };
    const std::wstring sStr = status.str();
    DrawTextW(mem, sStr.c_str(), static_cast<int>(sStr.size()), &stTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    BitBlt(hdc, 0, 0, rc.right, 74, mem, 0, 0, SRCCOPY);
    BitBlt(hdc, 0, rc.bottom - 24, rc.right, 24, mem, 0, rc.bottom - 24, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StudioAppState* st = reinterpret_cast<StudioAppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<StudioAppState*>(cs->lpCreateParams);
        st->hwndMain = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        st->hUiFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        RebuildToolbarButtons(st);

        st->hwndSearchEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 740, 8, 120, 22, hwnd,
            reinterpret_cast<HMENU>(CMD_SEARCH_EDIT), cs->hInstance, nullptr);
        SendMessageW(st->hwndSearchEdit, WM_SETFONT, reinterpret_cast<WPARAM>(st->hUiFont), TRUE);
        SendMessageW(st->hwndSearchEdit, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"Buscar texto... (Ctrl+F)"));

        st->hwndSidebar = CreateWindowW(kSidebarClassName, L"", WS_CHILD | WS_VISIBLE,
            0, 74, 196, 500, hwnd, nullptr, cs->hInstance, st);
        st->hwndViewport = CreateWindowW(kViewportClassName, L"", WS_CHILD | WS_VISIBLE,
            196, 74, 800, 500, hwnd, nullptr, cs->hInstance, st);

        st->renderer.AttachHwnd(st->hwndViewport, 800, 500);
        DragAcceptFiles(hwnd, TRUE);
        LayoutMainChildren(st);
        CenterCameraOnPage(st, 0);
        UpdateWindowTitle(st);
        return 0;
    }
    case WM_SIZE:
        LayoutMainChildren(st);
        return 0;
    case WM_DROPFILES: {
        if (!st) break;
        HDROP hDrop = reinterpret_cast<HDROP>(wp);
        wchar_t path[4096]{};
        if (DragQueryFileW(hDrop, 0, path, 4096) > 0) {
            OpenDocumentFile(st, path);
        }
        DragFinish(hDrop);
        return 0;
    }
    case WM_MOUSEMOVE: {
        if (!st) break;
        const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hit = -1;
        for (std::size_t i = 0; i < st->buttonRects.size(); ++i) {
            if (PtInRect(&st->buttonRects[i], pt)) { hit = static_cast<int>(i); break; }
        }
        if (hit != st->hoveredButton) {
            st->hoveredButton = hit;
            RECT ribRc{ 0, 0, 4000, 74 };
            InvalidateRect(hwnd, &ribRc, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (!st) break;
        const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        for (std::size_t i = 0; i < st->buttonRects.size(); ++i) {
            if (PtInRect(&st->buttonRects[i], pt)) {
                ExecuteToolbarCommand(st, st->buttons[i].id);
                return 0;
            }
        }
        if (st->hwndSearchEdit) {
            RECT seRc{};
            GetWindowRect(st->hwndSearchEdit, &seRc);
            MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<LPPOINT>(&seRc), 2);
            RECT bFind{ seRc.right + 4, 8, seRc.right + 46, 30 };
            RECT bRepl{ seRc.right + 50, 8, seRc.right + 90, 30 };
            if (PtInRect(&bFind, pt)) { ExecuteToolbarCommand(st, CMD_SEARCH_NEXT); return 0; }
            if (PtInRect(&bRepl, pt)) { ExecuteToolbarCommand(st, CMD_REPLACE_TEXT); return 0; }
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (!st) break;
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        if (ctrl && wp == 'S') { ExecuteToolbarCommand(st, CMD_SAVE_CUDOC); return 0; }
        if (ctrl && wp == 'O') { ExecuteToolbarCommand(st, CMD_OPEN); return 0; }
        if (ctrl && wp == 'Z') { ExecuteToolbarCommand(st, CMD_UNDO); return 0; }
        if (ctrl && wp == 'Y') { ExecuteToolbarCommand(st, CMD_REDO); return 0; }
        if (ctrl && wp == 'F' && st->hwndSearchEdit) { SetFocus(st->hwndSearchEdit); return 0; }
        if (ctrl && wp == 'P') { ExecuteToolbarCommand(st, CMD_PRINT); return 0; }
        if (wp == VK_F3) { ExecuteToolbarCommand(st, CMD_SEARCH_NEXT); return 0; }
        break;
    }
    case WM_APP_ASYNC_PAGE_READY:
        if (st) InvalidateViews(st);
        return 0;
    case WM_APP_TAKE_SCREENSHOT: {
        if (st && !st->screenshotOutPath.empty()) {
            auto ensureThumb = [&](std::size_t p) {
                if (p >= st->doc.Pages().size()) return;
                const auto& pg = st->doc.Pages()[p];
                if (!pg.hasPdfSource || pg.sourcePdfPathUtf8.empty()) return;
                const PageCacheKey tkey = PageRenderCache::MakeKey(pg, 160);
                RasterImageBgra32 img;
                if (st->pageCache.TryGet(tkey, img)) return;
                NormalizedCropRect c = pg.crop;
                c.Clamp();
                if (WinRtPdfRenderer::RenderPageToBgra32(Utf8ToWide(pg.sourcePdfPathUtf8), pg.sourcePageIndex0,
                        tkey.widthBucketPx, tkey.rotationDeg, c, img, nullptr, nullptr)) {
                    st->pageCache.PutDirect(tkey, img);
                }
            };
            const std::size_t topCount = (std::min<std::size_t>)(st->doc.Pages().size(), 7u);
            for (std::size_t p = 0; p < topCount; ++p) ensureThumb(p);
            const std::size_t firstThumb = st->activePageIndex > 2 ? st->activePageIndex - 2 : 0;
            const std::size_t endThumb = (std::min)(st->doc.Pages().size(), firstThumb + 6);
            for (std::size_t p = firstThumb; p < endThumb; ++p) ensureThumb(p);
            if (st->hwndSidebar) RedrawWindow(st->hwndSidebar, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            CaptureStudioCombinedToPng(st, st->screenshotOutPath);
            if (st->smokeTestMode) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        } else if (st && st->smokeTestMode) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (st) PaintRibbonAndStatus(st, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        if (st) {
            st->pageCache.Shutdown();
            WinRtPdfRenderer::ClearOpenDocumentCache();
            st->renderer.DiscardDeviceResources();
            if (st->hUiFont) { DeleteObject(st->hUiFont); st->hUiFont = nullptr; }
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ParseCliArgs(StudioAppState* st, std::wstring& outOpenPath, std::string& outMode) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--open" && i + 1 < argc) {
            outOpenPath = argv[++i];
        } else if (arg == L"--page" && i + 1 < argc) {
            st->initialPage1Based = (std::max)(1, _wtoi(argv[++i]));
        } else if (arg == L"--mode" && i + 1 < argc) {
            const std::wstring m = argv[++i];
            if (m == L"canvas") outMode = "canvas";
            else if (m == L"pdf") outMode = "pdf";
        } else if (arg == L"--screenshot-out" && i + 1 < argc) {
            st->screenshotOutPath = argv[++i];
        } else if (arg == L"--smoke-test") {
            st->smokeTestMode = true;
        }
    }
    LocalFree(argv);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDPIAware();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    RoInitialize(RO_INIT_SINGLETHREADED);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    auto registerCls = [&](const wchar_t* name, WNDPROC proc, HCURSOR cur) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = proc;
        wc.hInstance = hInst;
        wc.hCursor = cur;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = name;
        RegisterClassExW(&wc);
    };
    registerCls(kMainClassName, MainWndProc, LoadCursorW(nullptr, IDC_ARROW));
    registerCls(kViewportClassName, ViewportWndProc, LoadCursorW(nullptr, IDC_CROSS));
    registerCls(kSidebarClassName, SidebarWndProc, LoadCursorW(nullptr, IDC_ARROW));
    registerCls(kPromptClassName, PromptWndProc, LoadCursorW(nullptr, IDC_ARROW));

    {
        auto st = std::make_unique<StudioAppState>();
        std::wstring initialOpen;
        std::string initialMode;
        ParseCliArgs(st.get(), initialOpen, initialMode);

        if (initialMode == "canvas") {
            st->doc.ResetToBlank(DocumentMode::InfiniteCanvas);
            st->doc.SetCanvasBackground(PaperPattern::Grid);
            st->doc.SetTitle("Lienzo Infinito");
        }

        HWND hwnd = CreateWindowExW(0, kMainClassName, L"Document Studio \u2014 Calculadora Unificada",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 820,
            nullptr, nullptr, hInst, st.get());
        if (!hwnd) {
            st.reset();
            RoUninitialize();
            CoUninitialize();
            return 1;
        }

        if (!initialOpen.empty()) {
            OpenDocumentFile(st.get(), initialOpen);
        }
        if (initialMode == "canvas") {
            if (st->doc.Mode() != DocumentMode::InfiniteCanvas) {
                ExecuteToolbarCommand(st.get(), CMD_MODE_CANVAS);
            }
            st->camera.zoom = 1.0;
            st->camera.panX = -48.0;
            st->camera.panY = -48.0;
        }

        if (st->doc.Mode() == DocumentMode::InfiniteCanvas && st->doc.CanvasObjects().empty() && initialOpen.empty()) {
            // Seed crisp sample ink & geometry in Infinite Canvas smoke/launch when empty
            AnnotationObject box;
            box.kind = ObjectKind::Shape;
            box.shapeKind = ShapeKind::Rectangle;
            box.bounds = { 80.0, 80.0, 260.0, 120.0 };
            box.strokeColor = DocColor::RGBA(37, 99, 235, 255);
            box.fillColor = DocColor::RGBA(239, 246, 255, 255);
            st->doc.AddCanvasObject(std::move(box));

            AnnotationObject tb;
            tb.kind = ObjectKind::TextBox;
            tb.bounds = { 96.0, 102.0, 228.0, 80.0 };
            tb.textUtf8 = "Lienzo Infinito listo\nZoom y pan ilimitados";
            tb.strokeColor = DocColor::RGBA(15, 23, 42, 255);
            tb.fontSizePt = 15.0;
            st->doc.AddCanvasObject(std::move(tb));
        }

        ShowWindow(hwnd, st->smokeTestMode ? SW_SHOWNOACTIVATE : nCmdShow);
        UpdateWindow(hwnd);

        if (!st->screenshotOutPath.empty() || st->smokeTestMode) {
            PostMessageW(hwnd, WM_APP_TAKE_SCREENSHOT, 0, 0);
        }

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        st.reset();
    }

    RoUninitialize();
    CoUninitialize();
    return 0;
}
