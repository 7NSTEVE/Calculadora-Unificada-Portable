#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <objidl.h>
#include <gdiplus.h>
#include <roapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "../../ui_glass/liquid_glass_tokens.hpp"
#include "../../ui_glass/liquid_glass_math.hpp"
#include "../../ui_glass/vector_icons.hpp"
#include "../../ui_glass/gdi_liquid_glass.hpp"
#include "studio_actions.hpp"
#include "studio_cli_dispatch.hpp"
#include "../core/audio_timeline.hpp"
#include "../core/camera.hpp"
#include "../core/command_stack.hpp"
#include "../core/cudoc_storage.hpp"
#include "../core/doc_types.hpp"
#include "../core/document_model.hpp"
#include "../core/notebook_vault.hpp"
#include "../core/utf_conv.hpp"
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
#include <cstring>
#include <filesystem>
#include <fstream>
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
constexpr const wchar_t* kInspectorClassName = L"CalculadoraUnificadaDocStudioInspector";
constexpr const wchar_t* kPromptClassName = L"CalculadoraUnificadaDocStudioPrompt";

constexpr int kTopChromeH = 58;
constexpr int kBottomStatusH = 30;
constexpr int kSidebarWidth = 220;
constexpr int kInspectorWidth = 284;

constexpr UINT WM_APP_ASYNC_PAGE_READY = WM_APP + 101;
constexpr UINT WM_APP_TAKE_SCREENSHOT = WM_APP + 102;

enum class ToolKind {
    Hand = 0,
    Pen,
    Highlighter,
    Eraser,
    Lasso,
    Laser,
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
    CMD_STEM_MENU,
    CMD_STUDY_MENU,
    CMD_PRINT,
    CMD_MODE_PDF,
    CMD_MODE_CANVAS,
    CMD_TOOL_HAND,
    CMD_TOOL_PEN,
    CMD_TOOL_HL,
    CMD_TOOL_ERASER,
    CMD_TOOL_LASSO,
    CMD_TOOL_LASER,
    CMD_TOOL_RECT,
    CMD_TOOL_ARROW,
    CMD_TOOL_TEXT,
    CMD_TOOL_CROP,
    CMD_INSERT_IMAGE,
    CMD_TOGGLE_SNAP,
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
    CMD_IMPORT_PDF_TO_CANVAS,
    // STEM & Study Menu IDs
    CMD_ADD_FORMULA = 2101,
    CMD_CHECK_STEP,
    CMD_ADD_CIRCUIT,
    CMD_ADD_KMAP,
    CMD_ADD_PLOT,
    CMD_ADD_CALC_CARD,
    CMD_EXPORT_CIRCUIT,
    CMD_TOGGLE_STAB,
    CMD_LAYER_ADD,
    CMD_LAYER_NEXT,
    CMD_LAYER_TOGGLE_VIS,
    CMD_LAYER_TOGGLE_LOCK,
    CMD_ADD_TAPE,
    CMD_ANCHOR_INK,
    CMD_ADD_INKLINK,
    CMD_TOGGLE_COLLAPSE,
    CMD_STUDY_DECK,
    CMD_FTS_SEARCH,
    CMD_REPLAY_TOGGLE,
    CMD_AUDIO_REC,
    CMD_PRESENTER_MODE,
    // Extended 4-Document STEM & Goodnotes/OneNote/Calcpad Compliance IDs
    CMD_ADD_PLOT_3D,
    CMD_ADD_MATRIX_SEL,
    CMD_ADD_TRANSIENT_LAPLACE,
    CMD_CYCLE_MATH_SUBMODE,
    CMD_PARAM_SLIDER,
    CMD_OCR_CONFIRM_FORMULA,
    CMD_EXPORT_LAB_HTML,
    CMD_TOGGLE_MINIMAP,
    CMD_TOGGLE_RULER,
    CMD_CYCLE_ERASER_FILTER,
    CMD_CLEAR_HIGHLIGHTS,
    CMD_TAG_OBJECT_BOARD,
    CMD_FOCUS_MODE,
    CMD_CASIO_REVERSE_KEYS,
    CMD_ADD_PHASOR_DIAG,
    CMD_ADD_BODE_PLOT,
    CMD_VAR_INSPECTOR,
    CMD_TEACHER_PRIVATE_LAYER,
    // Apple HIG & Liquid Glass 2.0 Navigation & Academic Hub IDs
    CMD_FILE_MENU = 2301,
    CMD_TOGGLE_SIDEBAR,
    CMD_TOGGLE_INSPECTOR,
    CMD_TOGGLE_ACADEMIC,
    CMD_TOGGLE_SEARCH,
    CMD_TOGGLE_APPEARANCE,
    CMD_MORE_OVERFLOW,
    CMD_ACADEMIC_SYNC_TEC,
    CMD_ACADEMIC_SOLVE_SCHEDULE,
    CMD_ACADEMIC_SEARCH_MATERIALS
};

enum class ToolbarZone {
    Leading = 0,
    CenterTools,
    CenterInk,
    CenterHub,
    TrailingUtility,
    TrailingProminent,
    StatusZoom
};

struct ToolbarBtn {
    WORD id = 0;
    const wchar_t* label = nullptr;
    int width = 32;
    ToolbarZone zone = ToolbarZone::CenterTools;
    UiGlass::IconId icon = UiGlass::IconId::Document;
    bool hasIcon = true;
    bool hasLabel = false;
    bool accent = false;
    bool isSwatch = false;
    COLORREF swatchColor = 0;
    bool visible = true;
};

struct GlassCapsuleGroup {
    RECT rc{};
    bool prominent = false;
    bool visible = false;
};

struct InspectorHitBtn {
    RECT rc{};
    WORD cmdId = 0;
    int tabIdx = -1; // >= 0 switches inspector tab
};

struct StudioAppState {
    HWND hwndMain = nullptr;
    HWND hwndViewport = nullptr;
    HWND hwndSidebar = nullptr;
    HWND hwndInspector = nullptr;
    HWND hwndSearchEdit = nullptr;
    HFONT hUiFont = nullptr;
    HFONT hUiBoldFont = nullptr;
    HFONT hUiSmallFont = nullptr;
    HFONT hUiTitleFont = nullptr;
    HBRUSH hSearchBgBrush = nullptr;

    // Liquid Glass 2.0 & Apple HIG appearance state
    UiGlass::GlassSettings glassSettings{};
    bool sidebarVisible = true;
    bool inspectorVisible = false;
    int inspectorTab = 0; // 0 = Académico, 1 = STEM, 2 = Capas/Audio
    bool searchVisible = false;
    bool appearancePopoverOpen = false;
    bool draggingOpacitySlider = false;
    RECT appearancePopoverRc{};
    RECT opacityTrackRc{};
    std::vector<GlassCapsuleGroup> toolbarCapsules;
    std::vector<InspectorHitBtn> inspectorHits;
    std::vector<RECT> sidebarNavRects;
    std::string academicStatusUtf8 = "Solo lectura · Offline-First listo";
    std::string scheduleSummaryUtf8 = "Sin choques · Listo para optimizar";

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
    POINT panStartPt{};
    bool panMoved = false;
    bool drawing = false;
    bool draftIsCanvas = false;
    std::size_t draftPageIdx = 0;
    AnnotationObject draftObj{};
    DocPoint draftAnchorWorld{};
    bool hasDraftCrop = false;
    DocRect draftCropPageLocal{};

    std::uint64_t selectedObjectId = 0;
    bool selectedIsCanvas = false;
    std::size_t selectedPageIndex = 0;
    std::vector<std::uint64_t> lassoSelectedIds;
    std::vector<SearchMatch> searchMatches;
    std::size_t activeSearchIdx = 0;
    std::size_t activePageIndex = 0;
    int sidebarScrollPos = 0;

    // P1-P3 Ink Physics, Layers, Study & STEM interactive state
    bool snapShapes = true;
    double penStabilization = 0.65;
    bool presenterMode = false;
    bool replayActive = false;
    std::uint64_t replayCursorMs = 0;
    std::uint64_t replayMaxMs = 1;
    LaserPointerState laserPointer{};
    std::uint64_t strokeOriginEpochMs = 0;
    AudioTimelineSession audioSession;
    VaultFullTextIndex ftsIndex;
    StudyDeck activeDeck;
    std::size_t activeDeckCursor = 0;

    // Extended Goodnotes 6 / OneNote / Calcpad state
    bool minimapVisible = false;
    bool focusMode = false;
    bool hasPredictedTip = false;
    RulerState rulerState{};
    EraserFilterMode eraserFilterMode = EraserFilterMode::All;
    bool showcaseStemCli = false;

    std::vector<RECT> buttonRects;
    std::vector<ToolbarBtn> buttons;
    int hoveredButton = -1;

    // CLI automation state
    bool smokeTestMode = false;
    std::wstring screenshotOutPath;
    int initialPage1Based = 1;
    std::string cliCalcCardSpec;
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

void PaintAppearancePopoverOnHdc(StudioAppState* st, HDC hdcTarget, int clientW, int clientH);

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
    GdiFlush();

    RasterImageBgra32 composite;
    composite.width = mw;
    composite.height = mh;
    composite.pixels.resize(static_cast<std::size_t>(mw) * mh * 4u);
    std::memcpy(composite.pixels.data(), bits, composite.pixels.size());
    for (std::size_t i = 3; i < composite.pixels.size(); i += 4) composite.pixels[i] = 255;

    // Blit crisp synchronous offscreen Direct2D render straight into viewport region so screenshot is 100% deterministic
    const int vw = (std::max)(1, static_cast<int>(vpRc.right - vpRc.left));
    const int vh = (std::max)(1, static_cast<int>(vpRc.bottom - vpRc.top));
    RasterImageBgra32 vpImg;
    ViewportOverlayState ov{};
    ov.selectedObjectId = st->selectedObjectId;
    ov.lassoSelectedIds = st->lassoSelectedIds;
    ov.searchMatches = st->searchMatches.empty() ? nullptr : &st->searchMatches;
    ov.activeSearchMatchIndex = st->activeSearchIdx;
    ov.replayActive = st->replayActive;
    ov.replayCursorMs = st->replayCursorMs;
    ov.laserPointer = st->laserPointer;
    ov.presenterMode = st->presenterMode;
    ov.minimapVisible = st->minimapVisible;
    ov.rulerState = st->rulerState;
    ov.focusMode = st->focusMode;
    ov.darkTheme = st->glassSettings.IsDark();
    ov.highContrast = st->glassSettings.IsHighContrast();
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

    // Composite floating Liquid Glass 2.0 Appearance Popover over live D2D content when open
    if (st->appearancePopoverOpen) {
        std::memcpy(bits, composite.pixels.data(), composite.pixels.size());
        PaintAppearancePopoverOnHdc(st, hdcMem, mw, mh);
        GdiFlush();
        std::memcpy(composite.pixels.data(), bits, composite.pixels.size());
        for (std::size_t i = 3; i < composite.pixels.size(); i += 4) composite.pixels[i] = 255;
    }

    SelectObject(hdcMem, old);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(st->hwndMain, hdcWin);

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

ObjectTarget CurrentTarget(const StudioAppState* st) noexcept {
    if (!st) return { true, 0 };
    const bool isCanvas = (st->doc.Mode() == DocumentMode::InfiniteCanvas);
    const std::size_t pg = st->doc.Pages().empty() ? 0 : (std::min)(st->activePageIndex, st->doc.Pages().size() - 1);
    return { isCanvas, isCanvas ? 0u : pg };
}

void NextCardPlacement(const StudioAppState* st, double& outX, double& outY) noexcept {
    const ObjectTarget t = CurrentTarget(st);
    const std::size_t count = t.isCanvas
        ? st->doc.CanvasObjects().size()
        : (t.pageIndex < st->doc.Pages().size() ? st->doc.Pages()[t.pageIndex].objects.size() : 0);
    const std::size_t col = count / 4;
    const std::size_t row = count % 4;
    outX = 80.0 + static_cast<double>(col) * 390.0;
    outY = 80.0 + static_cast<double>(row) * 145.0;
}

std::uint64_t CurrentStrokeRelMs(StudioAppState* st) noexcept {
    const auto nowMs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    if (st->strokeOriginEpochMs == 0) st->strokeOriginEpochMs = nowMs;
    return (nowMs >= st->strokeOriginEpochMs) ? (nowMs - st->strokeOriginEpochMs) : 0ull;
}

void PopulateViewportOverlay(const StudioAppState* st, ViewportOverlayState& ov) {
    ov.draftObject = st->drawing ? &st->draftObj : nullptr;
    ov.draftPageIndex = st->draftPageIdx;
    ov.selectedObjectId = st->selectedObjectId;
    ov.lassoSelectedIds = st->lassoSelectedIds;
    ov.hasCropPreview = st->hasDraftCrop;
    ov.cropPageIndex = st->draftPageIdx;
    ov.cropPreviewPageLocal = st->draftCropPageLocal;
    ov.searchMatches = st->searchMatches.empty() ? nullptr : &st->searchMatches;
    ov.activeSearchMatchIndex = st->activeSearchIdx;
    ov.replayActive = st->replayActive;
    ov.replayCursorMs = st->replayCursorMs;
    ov.laserPointer = st->laserPointer;
    ov.presenterMode = st->presenterMode;
    ov.minimapVisible = st->minimapVisible;
    ov.rulerState = st->rulerState;
    ov.focusMode = st->focusMode;
    ov.darkTheme = st->glassSettings.IsDark();
    ov.highContrast = st->glassSettings.IsHighContrast();
}

void ApplyCliCalcCardSpec(StudioAppState* st) {
    if (!st || st->cliCalcCardSpec.empty()) return;
    CalcBridge::CalcStepRecord rec{};
    const auto b1 = st->cliCalcCardSpec.find('|');
    if (b1 == std::string::npos) {
        rec.modelIdUtf8 = "fx-991CW";
        rec.expressionUtf8 = st->cliCalcCardSpec;
    } else {
        rec.modelIdUtf8 = b1 > 0 ? st->cliCalcCardSpec.substr(0, b1) : "fx-991CW";
        const auto b2 = st->cliCalcCardSpec.find('|', b1 + 1);
        if (b2 == std::string::npos) {
            rec.keySeqUtf8 = st->cliCalcCardSpec.substr(b1 + 1);
            rec.expressionUtf8 = rec.keySeqUtf8;
        } else {
            rec.keySeqUtf8 = st->cliCalcCardSpec.substr(b1 + 1, b2 - (b1 + 1));
            rec.expressionUtf8 = st->cliCalcCardSpec.substr(b2 + 1);
        }
    }
    double x = 80.0, y = 80.0;
    NextCardPlacement(st, x, y);
    const ObjectTarget tgt = CurrentTarget(st);
    const std::uint64_t newObjId = AppActions::AddCalcCardAction(st->doc, st->cmdStack, tgt, x, y, rec, st->statusBannerUtf8);
    if (newObjId != 0) {
        st->selectedObjectId = newObjId;
        st->selectedIsCanvas = tgt.isCanvas;
        st->selectedPageIndex = tgt.pageIndex;
    }
}

void RebuildToolbarButtons(StudioAppState* st) {
    st->buttons = {
        // Leading Capsule: Sidebar + File menu + Mode switcher (PDF | Lienzo)
        { CMD_TOGGLE_SIDEBAR,  L"",              30, ToolbarZone::Leading,           UiGlass::IconId::SidebarLeft,     true,  false, false, false, 0, true },
        { CMD_FILE_MENU,       L"Archivo",       80, ToolbarZone::Leading,           UiGlass::IconId::FolderOpen,      true,  true,  false, false, 0, true },
        { CMD_MODE_PDF,        L"PDF",           60, ToolbarZone::Leading,           UiGlass::IconId::PdfPages,        true,  true,  false, false, 0, true },
        { CMD_MODE_CANVAS,     L"Lienzo",        78, ToolbarZone::Leading,           UiGlass::IconId::InfiniteCanvas,  true,  true,  false, false, 0, true },

        // Center Capsule 1: Icon-only vector tool dock (strict HIG: never mix icons + verbose text)
        { CMD_TOOL_HAND,       L"",              27, ToolbarZone::CenterTools,       UiGlass::IconId::HandPan,         true,  false, false, false, 0, true },
        { CMD_TOOL_PEN,        L"",              27, ToolbarZone::CenterTools,       UiGlass::IconId::PenTool,         true,  false, false, false, 0, true },
        { CMD_TOOL_HL,         L"",              27, ToolbarZone::CenterTools,       UiGlass::IconId::HighlighterTool, true,  false, false, false, 0, true },
        { CMD_TOOL_ERASER,     L"",              27, ToolbarZone::CenterTools,       UiGlass::IconId::EraserTool,      true,  false, false, false, 0, true },
        { CMD_TOOL_LASSO,      L"",              27, ToolbarZone::CenterTools,       UiGlass::IconId::LassoTool,       true,  false, false, false, 0, true },
        { CMD_TOOL_RECT,       L"",              27, ToolbarZone::CenterTools,       UiGlass::IconId::ShapeRect,       true,  false, false, false, 0, true },
        { CMD_TOOL_ARROW,      L"",              27, ToolbarZone::CenterTools,       UiGlass::IconId::ShapeArrow,      true,  false, false, false, 0, true },
        { CMD_TOOL_TEXT,       L"",              27, ToolbarZone::CenterTools,       UiGlass::IconId::TextMath,        true,  false, false, false, 0, true },
        { CMD_TOOL_LASER,      L"",              27, ToolbarZone::CenterTools,       UiGlass::IconId::LaserTool,       true,  false, false, false, 0, true },
        { CMD_TOOL_CROP,       L"",              27, ToolbarZone::CenterTools,       UiGlass::IconId::CropTool,        true,  false, false, false, 0, true },

        // Center Capsule 2: Aqua 2.0 Ink Wells + Snap + Undo/Redo
        { CMD_COL_BLACK,       L"",              20, ToolbarZone::CenterInk,         UiGlass::IconId::PenTool,         false, false, false, true,  RGB(22, 27, 38),   true },
        { CMD_COL_BLUE,        L"",              20, ToolbarZone::CenterInk,         UiGlass::IconId::PenTool,         false, false, false, true,  RGB(0, 122, 255),  true },
        { CMD_COL_RED,         L"",              20, ToolbarZone::CenterInk,         UiGlass::IconId::PenTool,         false, false, false, true,  RGB(255, 59, 48),  true },
        { CMD_COL_GREEN,       L"",              20, ToolbarZone::CenterInk,         UiGlass::IconId::PenTool,         false, false, false, true,  RGB(40, 167, 69),  true },
        { CMD_COL_YELLOW,      L"",              20, ToolbarZone::CenterInk,         UiGlass::IconId::PenTool,         false, false, false, true,  RGB(245, 184, 10), true },
        { CMD_TOGGLE_SNAP,     L"",              25, ToolbarZone::CenterInk,         UiGlass::IconId::SnapMagnet,      true,  false, false, false, 0, true },
        { CMD_UNDO,            L"",              25, ToolbarZone::CenterInk,         UiGlass::IconId::Undo,            true,  false, false, false, 0, true },
        { CMD_REDO,            L"",              25, ToolbarZone::CenterInk,         UiGlass::IconId::Redo,            true,  false, false, false, 0, true },

        // Center Capsule 3: Engineering STEM, Study SM-2, Academic TEC Digital / HorariosTEC
        { CMD_STEM_MENU,       L"STEM",          68, ToolbarZone::CenterHub,         UiGlass::IconId::StemAtom,        true,  true,  false, false, 0, true },
        { CMD_STUDY_MENU,      L"Estudio",       76, ToolbarZone::CenterHub,         UiGlass::IconId::StudySparkles,   true,  true,  false, false, 0, true },
        { CMD_TOGGLE_ACADEMIC, L"Acad\u00E9mico",104, ToolbarZone::CenterHub,        UiGlass::IconId::AcademicCap,     true,  true,  false, false, 0, true },

        // Trailing Utility Capsule: Search + Liquid Glass Appearance + Inspector + Overflow
        { CMD_TOGGLE_SEARCH,     L"",            28, ToolbarZone::TrailingUtility,   UiGlass::IconId::Search,          true,  false, false, false, 0, true },
        { CMD_TOGGLE_APPEARANCE, L"",            28, ToolbarZone::TrailingUtility,   UiGlass::IconId::SlidersGlass,    true,  false, false, false, 0, true },
        { CMD_TOGGLE_INSPECTOR,  L"",            28, ToolbarZone::TrailingUtility,   UiGlass::IconId::InspectorRight,  true,  false, false, false, 0, true },
        { CMD_MORE_OVERFLOW,     L"",            28, ToolbarZone::TrailingUtility,   UiGlass::IconId::MoreEllipsis,    true,  false, false, false, 0, true },

        // Trailing Prominent CTA Capsule (HIG: single prominent tinted Liquid Glass action)
        { CMD_EXPORT_PDF,      L"Exportar",      92, ToolbarZone::TrailingProminent, UiGlass::IconId::ShareExport,     true,  true,  true,  false, 0, true },

        // Bottom Status Zoom Controls
        { CMD_ZOOM_OUT,        L"-",             24, ToolbarZone::StatusZoom,        UiGlass::IconId::ZoomOut,         true,  false, false, false, 0, true },
        { CMD_ZOOM_RESET,      L"100%",          46, ToolbarZone::StatusZoom,        UiGlass::IconId::Search,          false, true,  false, false, 0, true },
        { CMD_ZOOM_IN,         L"+",             24, ToolbarZone::StatusZoom,        UiGlass::IconId::ZoomIn,          true,  false, false, false, 0, true }
    };
}

bool IsToolbarButtonChecked(const StudioAppState* st, WORD id) {
    switch (id) {
    case CMD_TOGGLE_SIDEBAR:    return st->sidebarVisible && !st->focusMode;
    case CMD_TOGGLE_INSPECTOR:  return st->inspectorVisible && !st->focusMode;
    case CMD_TOGGLE_ACADEMIC:   return st->inspectorVisible && st->inspectorTab == 0 && !st->focusMode;
    case CMD_TOGGLE_SEARCH:     return st->searchVisible;
    case CMD_TOGGLE_APPEARANCE: return st->appearancePopoverOpen;
    case CMD_MODE_PDF:          return st->doc.Mode() == DocumentMode::PaginatedPdf;
    case CMD_MODE_CANVAS:       return st->doc.Mode() == DocumentMode::InfiniteCanvas;
    case CMD_TOOL_HAND:         return st->activeTool == ToolKind::Hand;
    case CMD_TOOL_PEN:          return st->activeTool == ToolKind::Pen;
    case CMD_TOOL_HL:           return st->activeTool == ToolKind::Highlighter;
    case CMD_TOOL_ERASER:       return st->activeTool == ToolKind::Eraser;
    case CMD_TOOL_LASSO:        return st->activeTool == ToolKind::Lasso;
    case CMD_TOOL_LASER:        return st->activeTool == ToolKind::Laser;
    case CMD_TOOL_RECT:         return st->activeTool == ToolKind::ShapeRect;
    case CMD_TOOL_ARROW:        return st->activeTool == ToolKind::ShapeArrow;
    case CMD_TOOL_TEXT:         return st->activeTool == ToolKind::TextBox;
    case CMD_TOOL_CROP:         return st->activeTool == ToolKind::Crop;
    case CMD_TOGGLE_SNAP:       return st->snapShapes;
    case CMD_COL_BLACK:         return st->penColor.r <= 30 && st->penColor.g <= 35;
    case CMD_COL_BLUE:          return st->penColor.b >= 220 && st->penColor.r <= 40;
    case CMD_COL_RED:           return st->penColor.r >= 210 && st->penColor.g <= 70;
    case CMD_COL_GREEN:         return st->penColor.g >= 120 && st->penColor.r <= 60;
    case CMD_COL_YELLOW:        return st->penColor.r >= 230 && st->penColor.g >= 170;
    default: return false;
    }
}

void LayoutMainChildren(StudioAppState* st) {
    if (!st || !st->hwndMain) return;
    RECT rc{};
    GetClientRect(st->hwndMain, &rc);
    const int w = (std::max)(360, static_cast<int>(rc.right - rc.left));
    const int h = (std::max)(260, static_cast<int>(rc.bottom - rc.top));

    // Responsive breakpoints: guarantee ZERO clipping even on 800x600 windows
    const bool showInkCapsule = (w >= 1180);
    const bool showHubCapsule = (w >= 960);
    const bool compactTools   = (w < 840);
    const bool allowSidebars  = (w >= 860) && !st->focusMode;

    const int sideW = (allowSidebars && st->sidebarVisible) ? kSidebarWidth : 0;
    const int inspW = (allowSidebars && st->inspectorVisible) ? kInspectorWidth : 0;

    st->buttonRects.assign(st->buttons.size(), RECT{ 0, 0, 0, 0 });
    st->toolbarCapsules.clear();

    constexpr int kCapTop = 10;
    constexpr int kCapH = 38;
    constexpr int kPadX = 5;
    constexpr int kGapBtn = 2;
    constexpr int kItemY0 = kCapTop + 4;
    constexpr int kItemY1 = kCapTop + kCapH - 4;

    auto layoutZoneGroup = [&](ToolbarZone z, int startX, bool visible, bool prominent, int& outEndX) {
        if (!visible) {
            for (auto& b : st->buttons) {
                if (b.zone == z) b.visible = false;
            }
            outEndX = startX;
            return;
        }
        int x = startX + kPadX;
        int placed = 0;
        for (std::size_t i = 0; i < st->buttons.size(); ++i) {
            auto& b = st->buttons[i];
            if (b.zone != z) continue;
            if (compactTools && z == ToolbarZone::CenterTools &&
                (b.id == CMD_TOOL_LASSO || b.id == CMD_TOOL_ARROW || b.id == CMD_TOOL_LASER || b.id == CMD_TOOL_CROP)) {
                b.visible = false;
                continue;
            }
            b.visible = true;
            const int bw = b.width;
            st->buttonRects[i] = { x, kItemY0, x + bw, kItemY1 };
            x += bw + kGapBtn;
            placed++;
        }
        if (placed > 0) {
            x = x - kGapBtn + kPadX;
            st->toolbarCapsules.push_back({ RECT{ startX, kCapTop, x, kCapTop + kCapH }, prominent, true });
            outEndX = x;
        } else {
            outEndX = startX;
        }
    };

    auto measureZoneWidth = [&](ToolbarZone z, bool visible) -> int {
        if (!visible) return 0;
        int sum = kPadX * 2;
        int cnt = 0;
        for (const auto& b : st->buttons) {
            if (b.zone != z) continue;
            if (compactTools && z == ToolbarZone::CenterTools &&
                (b.id == CMD_TOOL_LASSO || b.id == CMD_TOOL_ARROW || b.id == CMD_TOOL_LASER || b.id == CMD_TOOL_CROP)) {
                continue;
            }
            sum += b.width + kGapBtn;
            cnt++;
        }
        return cnt > 0 ? (sum - kGapBtn) : 0;
    };

    // 1. Leading Zone (left margin 12px)
    int leadEnd = 12;
    layoutZoneGroup(ToolbarZone::Leading, 12, true, false, leadEnd);

    // 2. Trailing Zone (right-aligned: Utility glass capsule + single Prominent CTA capsule)
    const int promW = measureZoneWidth(ToolbarZone::TrailingProminent, true);
    const int utilW = measureZoneWidth(ToolbarZone::TrailingUtility, true);
    const int promX = w - 12 - promW;
    const int utilX = promX - 7 - utilW;
    int dummyEnd = 0;
    layoutZoneGroup(ToolbarZone::TrailingUtility, utilX, true, false, dummyEnd);
    layoutZoneGroup(ToolbarZone::TrailingProminent, promX, true, true, dummyEnd);

    // 3. Center Zone (centered harmoniously between leading & trailing with >= 8px breathing room)
    const int toolsW = measureZoneWidth(ToolbarZone::CenterTools, true);
    const int inkW   = measureZoneWidth(ToolbarZone::CenterInk, showInkCapsule);
    const int hubW   = measureZoneWidth(ToolbarZone::CenterHub, showHubCapsule);
    int centerTotal = toolsW + (inkW > 0 ? (6 + inkW) : 0) + (hubW > 0 ? (6 + hubW) : 0);
    const int availLeft = leadEnd + 8;
    const int availRight = utilX - 8;
    const int availCenterW = (std::max)(0, availRight - availLeft);

    bool effInk = showInkCapsule;
    bool effHub = showHubCapsule;
    if (centerTotal > availCenterW && effInk) {
        effInk = false;
        centerTotal = toolsW + (effHub ? (6 + hubW) : 0);
    }
    if (centerTotal > availCenterW && effHub) {
        effHub = false;
        centerTotal = toolsW;
    }

    int cx = availLeft + (std::max)(0, (availCenterW - centerTotal) / 2);
    int cEnd = cx;
    layoutZoneGroup(ToolbarZone::CenterTools, cx, true, false, cEnd);
    if (effInk) {
        cx = cEnd + 6;
        layoutZoneGroup(ToolbarZone::CenterInk, cx, true, false, cEnd);
    } else {
        layoutZoneGroup(ToolbarZone::CenterInk, 0, false, false, dummyEnd);
    }
    if (effHub) {
        cx = cEnd + 6;
        layoutZoneGroup(ToolbarZone::CenterHub, cx, true, false, cEnd);
    } else {
        layoutZoneGroup(ToolbarZone::CenterHub, 0, false, false, dummyEnd);
    }

    // 4. Bottom Status Zoom Stepper (right side of bottom bar)
    int zx = w - 110;
    const int zy0 = h - kBottomStatusH + 4;
    const int zy1 = h - 4;
    for (std::size_t i = 0; i < st->buttons.size(); ++i) {
        if (st->buttons[i].zone != ToolbarZone::StatusZoom) continue;
        st->buttons[i].visible = true;
        st->buttonRects[i] = { zx, zy0, zx + st->buttons[i].width, zy1 };
        zx += st->buttons[i].width + 2;
    }

    // 5. Optional floating search edit bar below top chrome when toggled on
    const int searchBandH = st->searchVisible ? 38 : 0;
    if (st->hwndSearchEdit) {
        if (st->searchVisible) {
            const int sx = (std::max)(sideW + 22, w - inspW - 340);
            const int sw = (std::max)(140, (w - inspW - 122) - sx);
            MoveWindow(st->hwndSearchEdit, sx, kTopChromeH + 10, sw, 18, TRUE);
            ShowWindow(st->hwndSearchEdit, SW_SHOW);
        } else {
            ShowWindow(st->hwndSearchEdit, SW_HIDE);
        }
    }

    const int contentTop = kTopChromeH + searchBandH;
    const int contentH = (std::max)(100, h - contentTop - kBottomStatusH);

    if (st->hwndSidebar) {
        if (sideW > 0) {
            ShowWindow(st->hwndSidebar, SW_SHOW);
            MoveWindow(st->hwndSidebar, 0, kTopChromeH, sideW, (std::max)(100, h - kTopChromeH - kBottomStatusH), TRUE);
        } else {
            ShowWindow(st->hwndSidebar, SW_HIDE);
        }
    }
    if (st->hwndInspector) {
        if (inspW > 0) {
            ShowWindow(st->hwndInspector, SW_SHOW);
            MoveWindow(st->hwndInspector, w - inspW, kTopChromeH, inspW, (std::max)(100, h - kTopChromeH - kBottomStatusH), TRUE);
        } else {
            ShowWindow(st->hwndInspector, SW_HIDE);
        }
    }
    if (st->hwndViewport) {
        const int vw = (std::max)(120, w - sideW - inspW);
        MoveWindow(st->hwndViewport, sideW, contentTop, vw, contentH, TRUE);
        st->renderer.Resize(vw, contentH);
    }

    // Floating Appearance Popover geometry (anchored top-right INSIDE visible viewport,
    // never clipped behind the right Inspector or Search bar)
    const int popW = 296;
    const int popH = 228;
    const int vpRight = w - inspW;
    const int popX = (std::max)(sideW + 12, vpRight - popW - 16);
    const int popY = contentTop + 8;
    st->appearancePopoverRc = { popX, popY, popX + popW, popY + popH };
    st->opacityTrackRc = { popX + 20, popY + 124, popX + popW - 20, popY + 144 };
}

void InvalidateViews(StudioAppState* st) {
    if (!st) return;
    if (st->hwndViewport) InvalidateRect(st->hwndViewport, nullptr, FALSE);
    if (st->hwndSidebar && IsWindowVisible(st->hwndSidebar)) InvalidateRect(st->hwndSidebar, nullptr, FALSE);
    if (st->hwndInspector && IsWindowVisible(st->hwndInspector)) InvalidateRect(st->hwndInspector, nullptr, FALSE);
    if (st->hwndMain) {
        RECT rc{};
        GetClientRect(st->hwndMain, &rc);
        RECT topRc{ 0, 0, rc.right, kTopChromeH + (st->searchVisible ? 40 : 0) };
        InvalidateRect(st->hwndMain, &topRc, FALSE);
        RECT botRc{ 0, rc.bottom - kBottomStatusH, rc.right, rc.bottom };
        InvalidateRect(st->hwndMain, &botRc, FALSE);
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
    case CMD_TOOL_LASSO:  st->activeTool = ToolKind::Lasso; st->statusBannerUtf8 = "Lazo activo: dibuje un bucle para seleccionar objetos"; break;
    case CMD_TOOL_LASER:  st->activeTool = ToolKind::Laser; st->statusBannerUtf8 = "Puntero l\xC3\xA1ser activo (arrastre para se\xC3\xB1" "alar en vivo)"; break;
    case CMD_TOOL_RECT:   st->activeTool = ToolKind::ShapeRect; break;
    case CMD_TOOL_ARROW:  st->activeTool = ToolKind::ShapeArrow; break;
    case CMD_TOOL_TEXT:   st->activeTool = ToolKind::TextBox; break;
    case CMD_TOOL_CROP:   st->activeTool = ToolKind::Crop; break;
    case CMD_TOGGLE_SNAP:
        st->snapShapes = !st->snapShapes;
        st->statusBannerUtf8 = st->snapShapes
            ? "Hold-to-Snap activado (trazos rectos/flechas/figuras se ajustan)"
            : "Hold-to-Snap desactivado (trazo libre)";
        break;
    case CMD_STEM_MENU: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, CMD_ADD_FORMULA,           L"Insertar F\u00F3rmula #varsub / Unidades...");
        AppendMenuW(menu, MF_STRING, CMD_CYCLE_MATH_SUBMODE,    L"Alternar Modo Calcpad (#varsub / #nosub / #novar)");
        AppendMenuW(menu, MF_STRING, CMD_PARAM_SLIDER,          L"Slider de Par\u00E1metro Reactivo...");
        AppendMenuW(menu, MF_STRING, CMD_CHECK_STEP,            L"Verificar Paso Algebraico...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_MATRIX_SEL,        L"Matriz / Sistema de Ecuaciones Lineales (SEL)...");
        AppendMenuW(menu, MF_STRING, CMD_OCR_CONFIRM_FORMULA,   L"OCR -> F\u00F3rmula con Tarjeta de Confirmaci\u00F3n...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CMD_ADD_CIRCUIT,           L"Resolver Circuito MNA (DC / AC / Diodos)...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_TRANSIENT_LAPLACE, L"Simulaci\u00F3n Transitoria Osciloscopio + Laplace (s)...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_KMAP,              L"Simplificar Mapa de Karnaugh...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_PLOT,              L"Gr\u00E1fico 2D / Regresi\u00F3n CSV...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_PLOT_3D,           L"Superficie 3D z = f(x,y) [Paleta CVD Accesible]...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_PHASOR_DIAG,       L"Diagrama Fasorial Vectorial (Rect / Polar / Exp)...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_BODE_PLOT,         L"Diagrama de Bode Semilogar\u00EDtmico |H(jw)| dB...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_CALC_CARD,         L"Insertar Tarjeta CASIO Interactiva...");
        AppendMenuW(menu, MF_STRING, CMD_CASIO_REVERSE_KEYS,    L"Traducir Ecuaci\u00F3n/Matriz/Fasor -> Teclas CASIO...");
        AppendMenuW(menu, MF_STRING, CMD_VAR_INSPECTOR,         L"Inspector de Variables y Dimensiones F\u00EDsicas...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CMD_EXPORT_CIRCUIT,        L"Exportar Circuito (SVG / DXF / KiCad)...");
        AppendMenuW(menu, MF_STRING, CMD_EXPORT_LAB_HTML,       L"Exportar Memoria de C\u00E1lculo / Informe Lab (.html)...");
        POINT pt{ 600, 34 };
        for (std::size_t i = 0; i < st->buttons.size() && i < st->buttonRects.size(); ++i) {
            if (st->buttons[i].id == CMD_STEM_MENU) {
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
    case CMD_STUDY_MENU: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, CMD_LAYER_ADD,           L"Capas: Nueva capa...");
        AppendMenuW(menu, MF_STRING, CMD_LAYER_NEXT,          L"Capas: Cambiar capa activa");
        AppendMenuW(menu, MF_STRING, CMD_LAYER_TOGGLE_VIS,    L"Capas: Mostrar / Ocultar capa activa");
        AppendMenuW(menu, MF_STRING, CMD_LAYER_TOGGLE_LOCK,   L"Capas: Bloquear / Desbloquear capa activa");
        AppendMenuW(menu, MF_STRING, CMD_TEACHER_PRIVATE_LAYER, L"Capas: Crear Capa Privada Docente (Oculta en Proyecci\u00F3n/PDF)");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CMD_TOGGLE_MINIMAP,      L"Minimapa de Lienzo Infinito (Mostrar / Ocultar)");
        AppendMenuW(menu, MF_STRING, CMD_TOGGLE_RULER,        L"Regla Graduada 0\u00B0-360\u00B0 (Mostrar / Rotar +15\u00B0 / Ocultar)");
        AppendMenuW(menu, MF_STRING, CMD_CYCLE_ERASER_FILTER, L"Borrador Selectivo: Todo / Solo Resaltador / Solo Tinta");
        AppendMenuW(menu, MF_STRING, CMD_CLEAR_HIGHLIGHTS,    L"Limpiar Todos los Resaltados de la Vista");
        AppendMenuW(menu, MF_STRING, CMD_TAG_OBJECT_BOARD,    L"Etiquetar Objeto (#examen / #duda) y Ver Tablero...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CMD_ADD_TAPE,            L"Insertar Study Tape (Ocultar/Revelar)...");
        AppendMenuW(menu, MF_STRING, CMD_ANCHOR_INK,          L"Anclar tinta seleccionada a objeto");
        AppendMenuW(menu, MF_STRING, CMD_ADD_INKLINK,         L"Vincular p\u00E1gina PDF \u2194 Lienzo (InkLink)");
        AppendMenuW(menu, MF_STRING, CMD_TOGGLE_COLLAPSE,     L"Colapsar / Expandir rango PDF actual");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CMD_STUDY_DECK,          L"Repasar Flashcards (SM-2)...");
        AppendMenuW(menu, MF_STRING, CMD_FTS_SEARCH,          L"Buscar en \u00CDndice Unicode (FTS Vault)...");
        AppendMenuW(menu, MF_STRING, CMD_REPLAY_TOGGLE,       L"Ink Replay + L\u00EDnea Temporal (Play/Stop)");
        AppendMenuW(menu, MF_STRING, CMD_AUDIO_REC,           L"Grabar / Exportar Audio WAV (16-bit PCM)...");
        AppendMenuW(menu, MF_STRING, CMD_TOGGLE_STAB,         L"Estabilizaci\u00F3n de Tinta (Bajo/Medio/Alto)");
        AppendMenuW(menu, MF_STRING, CMD_PRESENTER_MODE,      L"Modo Presentador + Puntero L\u00E1ser");
        AppendMenuW(menu, MF_STRING, CMD_FOCUS_MODE,          L"Modo Concentraci\u00F3n (Foco Sin Distracciones)");
        POINT pt{ 670, 34 };
        for (std::size_t i = 0; i < st->buttons.size() && i < st->buttonRects.size(); ++i) {
            if (st->buttons[i].id == CMD_STUDY_MENU) {
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
    case CMD_ADD_FORMULA: {
        std::wstring expr = L"V_out = 4.7 kOhm * 2 mA | V";
        if (!ShowTextPrompt(st->hwndMain, L"F\u00F3rmula #varsub / Unidades", L"Expresi\u00F3n (soporta unidades, |, fasores r\u2220\u03B8\u00B0):", expr) || expr.empty()) break;
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddFormulaNodeAction(st->doc, st->cmdStack, tgt, x, y, WideToUtf8(expr), "", st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_CHECK_STEP: {
        std::wstring prevW = L"(x + 2)^2";
        std::wstring nextW = L"x^2 + 4*x + 4";
        if (!ShowTextPrompt(st->hwndMain, L"Verificar Paso Algebraico", L"Paso anterior:", prevW, L"Paso actual:", &nextW)) break;
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddFormulaNodeAction(st->doc, st->cmdStack, tgt, x, y, WideToUtf8(nextW), WideToUtf8(prevW), st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_ADD_CIRCUIT: {
        std::wstring netW = L"V1 1 0 12; R1 1 2 1000; R2 2 0 2000";
        std::wstring freqW = L"0";
        if (!ShowTextPrompt(st->hwndMain, L"Resolver Circuito MNA", L"Netlist (separar l\u00EDneas con ';', R C L V I OPAMP):", netW, L"Frecuencia Hz (0 = DC):", &freqW)) break;
        std::string netUtf8 = WideToUtf8(netW);
        for (char& ch : netUtf8) { if (ch == ';') ch = '\n'; }
        const double freqHz = _wtof(freqW.c_str());
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddCircuitNodeAction(st->doc, st->cmdStack, tgt, x, y, netUtf8, freqHz, st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_ADD_KMAP: {
        std::wstring varsW = L"4";
        std::wstring minsW = L"0,1,4,5,12,13";
        if (!ShowTextPrompt(st->hwndMain, L"Mapa de Karnaugh (Quine-McCluskey)", L"N\u00FAmero de variables (2..4):", varsW, L"Minit\u00E9rminos (ej. 0,1,4,5):", &minsW)) break;
        const int nv = std::clamp(_wtoi(varsW.c_str()), 2, 4);
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddKMapNodeAction(st->doc, st->cmdStack, tgt, x, y, nv, WideToUtf8(minsW), st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_ADD_PLOT: {
        std::wstring exprW = L"sin(x) + 0.25*x";
        std::wstring rngW = L"-6.28, 6.28";
        if (!ShowTextPrompt(st->hwndMain, L"Gr\u00E1fico 2D / Regresi\u00F3n CSV", L"Expresi\u00F3n f(x) o puntos x,y separados por ';':", exprW, L"Rango xMin, xMax:", &rngW)) break;
        std::string exprUtf8 = WideToUtf8(exprW);
        for (char& ch : exprUtf8) { if (ch == ';') ch = '\n'; }
        double xMin = -6.28, xMax = 6.28;
        const std::string rngUtf8 = WideToUtf8(rngW);
        if (const auto comma = rngUtf8.find(','); comma != std::string::npos) {
            try {
                xMin = std::stod(rngUtf8.substr(0, comma));
                xMax = std::stod(rngUtf8.substr(comma + 1));
            } catch (...) {}
        }
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddPlotNodeAction(st->doc, st->cmdStack, tgt, x, y, exprUtf8, xMin, xMax, st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_ADD_CALC_CARD: {
        std::wstring modelW = L"fx-991CW";
        std::wstring exprW = L"4.7 kOhm * 2 mA | V";
        if (!ShowTextPrompt(st->hwndMain, L"Tarjeta CASIO Interactiva", L"Modelo CASIO:", modelW, L"Teclas|Expresi\u00F3n o C\u00E1lculo:", &exprW)) break;
        CalcBridge::CalcStepRecord rec{};
        rec.modelIdUtf8 = WideToUtf8(modelW);
        const std::string raw = WideToUtf8(exprW);
        if (const auto bar = raw.find('|'); bar != std::string::npos && raw.find('=') == std::string::npos && raw.find(" | ") == std::string::npos) {
            rec.keySeqUtf8 = raw.substr(0, bar);
            rec.expressionUtf8 = raw.substr(bar + 1);
        } else {
            rec.keySeqUtf8 = "[CALC]";
            rec.expressionUtf8 = raw;
        }
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddCalcCardAction(st->doc, st->cmdStack, tgt, x, y, rec, st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_EXPORT_CIRCUIT: {
        const ObjectTarget tgt = CurrentTarget(st);
        const AnnotationObject* cObj = nullptr;
        if (st->selectedObjectId != 0) {
            cObj = tgt.isCanvas ? st->doc.FindCanvasObject(st->selectedObjectId) : st->doc.FindPageObjectMutable(tgt.pageIndex, st->selectedObjectId);
            if (cObj && cObj->kind != ObjectKind::CircuitBlock) cObj = nullptr;
        }
        if (!cObj && (tgt.isCanvas || tgt.pageIndex < st->doc.Pages().size())) {
            const auto& vec = tgt.isCanvas ? st->doc.CanvasObjects() : st->doc.Pages()[tgt.pageIndex].objects;
            for (auto it = vec.rbegin(); it != vec.rend(); ++it) {
                if (it->kind == ObjectKind::CircuitBlock) { cObj = &(*it); break; }
            }
        }
        if (!cObj) {
            st->statusBannerUtf8 = "Inserte o seleccione primero un Circuito MNA.";
            break;
        }
        std::vector<Circuit::CircuitComponent> comps;
        std::string parseErr;
        if (!Circuit::ParseNetlistText(cObj->circuitNetlistUtf8, comps, &parseErr)) {
            st->statusBannerUtf8 = parseErr;
            break;
        }
        const auto sol = Circuit::SolveMnaCircuit(comps, 0.0);
        static constexpr COMDLG_FILTERSPEC kFilters[] = {
            { L"Esquema SVG (*.svg)", L"*.svg" },
            { L"CAD DXF (*.dxf)", L"*.dxf" },
            { L"Netlist KiCad (*.net)", L"*.net" }
        };
        std::wstring outPath;
        if (!ShowSaveDialog(st->hwndMain, L"Exportar Circuito CAD", kFilters, 3, L"svg", L"circuito.svg", outPath)) break;
        const std::wstring ext = GetFileExtLower(outPath);
        std::string payload;
        if (ext == L".dxf") payload = Circuit::ExportCircuitToDxf(comps, sol.ok ? &sol : nullptr);
        else if (ext == L".net" || ext == L".cir") payload = Circuit::ExportCircuitToKiCadNetlist(comps, st->doc.Title());
        else payload = Circuit::ExportCircuitToSvg(comps, sol.ok ? &sol : nullptr);
        std::ofstream ofs(std::filesystem::path(outPath), std::ios::binary | std::ios::trunc);
        if (ofs) {
            ofs.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            st->statusBannerUtf8 = "Circuito exportado (" + std::to_string(payload.size()) + " bytes)";
        } else {
            st->statusBannerUtf8 = "Error escribiendo archivo de circuito";
        }
        break;
    }
    case CMD_LAYER_ADD: {
        std::wstring nameW = L"Anotaciones Docente";
        if (!ShowTextPrompt(st->hwndMain, L"Nueva Capa", L"Nombre de la capa:", nameW) || nameW.empty()) break;
        const std::uint64_t lid = st->doc.AddLayer(WideToUtf8(nameW));
        st->doc.SetActiveLayerId(lid);
        st->statusBannerUtf8 = "Capa activa: " + WideToUtf8(nameW);
        break;
    }
    case CMD_LAYER_NEXT: {
        const auto& layers = st->doc.Layers();
        if (layers.empty()) break;
        std::size_t idx = 0;
        for (std::size_t i = 0; i < layers.size(); ++i) {
            if (layers[i].id == st->doc.ActiveLayerId()) { idx = (i + 1) % layers.size(); break; }
        }
        st->doc.SetActiveLayerId(layers[idx].id);
        st->statusBannerUtf8 = "Capa activa: " + layers[idx].nameUtf8 +
            (layers[idx].visible ? " [Vis]" : " [Oculta]") + (layers[idx].locked ? " [Bloq]" : "");
        break;
    }
    case CMD_LAYER_TOGGLE_VIS: {
        const LayerModel* cur = st->doc.FindLayer(st->doc.ActiveLayerId());
        if (!cur) break;
        const bool nextVis = !cur->visible;
        st->doc.SetLayerProperties(cur->id, nextVis, cur->locked, cur->includeInExport, cur->opacity);
        st->statusBannerUtf8 = "Capa '" + cur->nameUtf8 + "' ahora " + (nextVis ? "visible" : "oculta");
        break;
    }
    case CMD_LAYER_TOGGLE_LOCK: {
        const LayerModel* cur = st->doc.FindLayer(st->doc.ActiveLayerId());
        if (!cur) break;
        const bool nextLock = !cur->locked;
        st->doc.SetLayerProperties(cur->id, cur->visible, nextLock, cur->includeInExport, cur->opacity);
        st->statusBannerUtf8 = "Capa '" + cur->nameUtf8 + "' ahora " + (nextLock ? "bloqueada" : "desbloqueada");
        break;
    }
    case CMD_ADD_TAPE: {
        std::wstring capW = L"F\u00F3rmula Impedancia RLC";
        std::wstring ansW = L"Z = R + j*(w*L - 1/(w*C))";
        if (!ShowTextPrompt(st->hwndMain, L"Insertar Study Tape", L"Etiqueta visible:", capW, L"Contenido oculto:", &ansW)) break;
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddStudyTapeAction(st->doc, st->cmdStack, tgt, x, y, WideToUtf8(capW), WideToUtf8(ansW), st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_ANCHOR_INK: {
        const ObjectTarget tgt = CurrentTarget(st);
        if (!tgt.isCanvas && tgt.pageIndex >= st->doc.Pages().size()) break;
        const auto& vec = tgt.isCanvas ? st->doc.CanvasObjects() : st->doc.Pages()[tgt.pageIndex].objects;
        std::uint64_t parentId = 0;
        for (const auto& o : vec) {
            if (o.kind != ObjectKind::InkStroke && o.id != st->selectedObjectId) parentId = o.id;
        }
        if (parentId == 0) {
            st->statusBannerUtf8 = "Se requiere un objeto destino (tarjeta/imagen/PDF) para anclar tinta";
            break;
        }
        int anchoredCount = 0;
        std::vector<std::uint64_t> ids = st->lassoSelectedIds;
        if (ids.empty() && st->selectedObjectId != 0) ids.push_back(st->selectedObjectId);
        for (std::uint64_t oid : ids) {
            if (oid == parentId) continue;
            AnnotationObject* obj = tgt.isCanvas ? st->doc.FindCanvasObjectMutable(oid) : st->doc.FindPageObjectMutable(tgt.pageIndex, oid);
            if (!obj) continue;
            AnnotationObject before = *obj;
            AnnotationObject after = before;
            after.anchorObjectId = parentId;
            if (st->cmdStack.Execute(st->doc, std::make_unique<ModifyObjectCommand>(tgt, std::move(before), std::move(after)))) {
                ++anchoredCount;
            }
        }
        st->statusBannerUtf8 = anchoredCount > 0
            ? (std::to_string(anchoredCount) + " trazo(s) anclado(s) al objeto #" + std::to_string(parentId))
            : "Seleccione o encierre con Lazo un trazo para anclar";
        break;
    }
    case CMD_ADD_INKLINK: {
        InkLink link{};
        link.sourceDocPathUtf8 = st->activePdfSourcePathUtf8;
        link.sourcePageIndex0 = static_cast<int>(st->activePageIndex);
        link.sourceRectNorm = { 0.1, 0.1, 0.9, 0.4 };
        link.targetIsCanvas = true;
        link.targetPageIndex0 = 0;
        link.targetObjectId = st->selectedObjectId;
        link.labelUtf8 = "PDF P\xC3\xA1g " + std::to_string(st->activePageIndex + 1) + " <-> Lienzo";
        const std::uint64_t lid = st->doc.AddInkLink(std::move(link));
        st->statusBannerUtf8 = "InkLink #" + std::to_string(lid) + " creado (" + std::to_string(st->doc.InkLinks().size()) + " enlaces)";
        break;
    }
    case CMD_TOGGLE_COLLAPSE: {
        if (st->doc.Pages().size() < 2) {
            st->statusBannerUtf8 = "Colapso de rango requiere al menos 2 p\xC3\xA1ginas";
            break;
        }
        const int s0 = static_cast<int>(st->activePageIndex);
        const int e0 = (std::min)(static_cast<int>(st->doc.Pages().size()) - 1, s0 + 1);
        st->doc.ToggleCollapsedRange(s0, e0);
        st->statusBannerUtf8 = st->doc.IsPageCollapsed(s0)
            ? ("Rango P\xC3\xA1g " + std::to_string(s0 + 1) + "-" + std::to_string(e0 + 1) + " colapsado")
            : ("Rango P\xC3\xA1g " + std::to_string(s0 + 1) + "-" + std::to_string(e0 + 1) + " expandido");
        break;
    }
    case CMD_STUDY_DECK: {
        st->activeDeck = StudySetManager::BuildDeckFromDocument(st->doc, st->doc.Title());
        if (st->activeDeck.cards.empty()) {
            st->statusBannerUtf8 = "No hay tarjetas StudyTape/Math/CASIO en este documento";
            MessageBoxW(st->hwndMain, L"Agregue una Study Tape, F\u00F3rmula #varsub o Tarjeta CASIO para generar Flashcards SM-2.", L"Study Set SM-2", MB_ICONINFORMATION);
            break;
        }
        if (st->activeDeckCursor >= st->activeDeck.cards.size()) st->activeDeckCursor = 0;
        auto& card = st->activeDeck.cards[st->activeDeckCursor];
        std::wstring qScore = L"5";
        const std::wstring promptTxt = Utf8ToWide(
            "Pregunta: " + card.questionUtf8 + "\nRespuesta: " + card.answerUtf8 + "\nCalificaci\xC3\xB3n SM-2 (0=Olvid\xC3\xB3 .. 5=Perfecto):");
        if (ShowTextPrompt(st->hwndMain, L"Flashcards SM-2", promptTxt.c_str(), qScore)) {
            const int q = std::clamp(_wtoi(qScore.c_str()), 0, 5);
            StudySetManager::ReviewCardSm2(card, q, 1700000000ull);
            st->statusBannerUtf8 = "SM-2 tarjeta " + std::to_string(st->activeDeckCursor + 1) + "/" +
                std::to_string(st->activeDeck.cards.size()) + ": pr\xC3\xB3ximo repaso en " +
                std::to_string(card.intervalDays) + " d\xC3\xAD" "a(s)";
            st->activeDeckCursor = (st->activeDeckCursor + 1) % st->activeDeck.cards.size();
        }
        break;
    }
    case CMD_FTS_SEARCH: {
        st->ftsIndex.IndexDocument("active", st->doc);
        std::wstring qW = L"ohm";
        if (!ShowTextPrompt(st->hwndMain, L"B\u00FAsqueda Unicode Vault FTS", L"T\u00E9rmino a buscar:", qW) || qW.empty()) break;
        const auto hits = st->ftsIndex.Query(WideToUtf8(qW), 24);
        if (hits.empty()) {
            st->statusBannerUtf8 = "FTS (" + std::to_string(st->ftsIndex.VocabularySize()) + " t\xC3\xA9rminos): 0 coincidencias";
        } else {
            if (hits.front().objectId != 0) st->selectedObjectId = hits.front().objectId;
            if (st->doc.Mode() == DocumentMode::PaginatedPdf && hits.front().pageIndex < st->doc.Pages().size()) {
                CenterCameraOnPage(st, hits.front().pageIndex);
            }
            st->statusBannerUtf8 = "FTS " + std::to_string(hits.size()) + " resultado(s): " + hits.front().snippetUtf8;
        }
        break;
    }
    case CMD_REPLAY_TOGGLE: {
        if (st->replayActive) {
            st->replayActive = false;
            KillTimer(st->hwndMain, 701);
            st->audioSession.SetPlaying(false);
            st->statusBannerUtf8 = "Ink Replay detenido";
        } else {
            const auto range = InkEngine::ComputeReplayTimelineRange(st->doc, CurrentTarget(st));
            const std::uint64_t audioDur = st->audioSession.DurationMs();
            st->replayMaxMs = (std::max)(range.second, audioDur);
            if (st->replayMaxMs < 100) st->replayMaxMs = 1500;
            st->replayCursorMs = range.first;
            st->replayActive = true;
            st->audioSession.SeekMs(st->replayCursorMs);
            st->audioSession.SetPlaying(true);
            SetTimer(st->hwndMain, 701, 45, nullptr);
            st->statusBannerUtf8 = "Ink Replay reproduciendo (0 / " + std::to_string(st->replayMaxMs) + " ms)";
        }
        break;
    }
    case CMD_AUDIO_REC: {
        if (!st->audioSession.IsRecording()) {
        const auto micRep = st->audioSession.CaptureFromMicrophone(1200, 16000, false);
        std::vector<std::uint8_t> wav;
        if (st->audioSession.ExportWavBytes(wav)) {
            static constexpr COMDLG_FILTERSPEC kWavFilter[] = { { L"Audio PCM WAV (*.wav)", L"*.wav" } };
            std::wstring outWav;
            if (ShowSaveDialog(st->hwndMain, L"Guardar Nota de Audio WAV (16-bit PCM)", kWavFilter, 1, L"wav", L"nota_estudio.wav", outWav)) {
                std::ofstream ofs(std::filesystem::path(outWav), std::ios::binary | std::ios::trunc);
                if (ofs) ofs.write(reinterpret_cast<const char*>(wav.data()), static_cast<std::streamsize>(wav.size()));
            }
            st->statusBannerUtf8 = micRep.statusNoteUtf8 + " (" + std::to_string(st->audioSession.DurationMs()) + " ms)";
        }
        }
        break;
    }
    case CMD_TOGGLE_STAB:
        if (st->penStabilization < 0.25) st->penStabilization = 0.45;
        else if (st->penStabilization < 0.70) st->penStabilization = 0.85;
        else st->penStabilization = 0.15;
        st->statusBannerUtf8 = "Estabilizaci\xC3\xB3n de tinta: " + std::to_string(static_cast<int>(std::lround(st->penStabilization * 100.0))) + "%";
        break;
    case CMD_PRESENTER_MODE:
        st->presenterMode = !st->presenterMode;
        if (st->presenterMode) st->activeTool = ToolKind::Laser;
        st->statusBannerUtf8 = st->presenterMode
            ? "Modo Presentador + Puntero L\xC3\xA1ser ACTIVADO"
            : "Modo Presentador desactivado";
        break;
    case CMD_ADD_PLOT_3D: {
        std::wstring exprW = L"sin(x^2 + y^2)";
        std::wstring domW = L"-2.5, 2.5";
        if (!ShowTextPrompt(st->hwndMain, L"Superficie 3D z = f(x, y) [CVD]", L"Expresi\u00F3n z = f(x, y):", exprW, L"Dominio min, max (ej. -2.5, 2.5):", &domW) || exprW.empty()) break;
        double dMin = -2.5, dMax = 2.5;
        const std::string domUtf8 = WideToUtf8(domW);
        if (const auto comma = domUtf8.find(','); comma != std::string::npos) {
            try {
                dMin = std::stod(domUtf8.substr(0, comma));
                dMax = std::stod(domUtf8.substr(comma + 1));
            } catch (...) {}
        }
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddSurface3DPlotNodeAction(
            st->doc, st->cmdStack, tgt, x, y, WideToUtf8(exprW), dMin, dMax, dMin, dMax, st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_ADD_MATRIX_SEL: {
        std::wstring inW = L"2*x + y = 7; x - y = 2";
        if (!ShowTextPrompt(st->hwndMain, L"Matriz o Sistema Lineal (SEL)", L"Ej: 2*x + y = 7; x - y = 2  \u00F3  det([[2,1],[1,3]]):", inW) || inW.empty()) break;
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddMatrixOrLinearSystemAction(
            st->doc, st->cmdStack, tgt, x, y, WideToUtf8(inW), st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_ADD_TRANSIENT_LAPLACE: {
        std::wstring netW = L"V1 1 0 10; D1 1 2; R1 2 0 1k; C1 2 0 2.2u";
        std::wstring cfgW = L"35, 60, 2";
        if (!ShowTextPrompt(st->hwndMain, L"Osciloscopio MNA Transitorio + Laplace (s)",
            L"Netlist (R, C, L, V, I, OPAMP, D):", netW, L"Duraci\u00F3n_ms, Frecuencia_Hz, Nodo_sonda:", &cfgW) || netW.empty()) break;
        std::string netUtf8 = WideToUtf8(netW);
        for (char& ch : netUtf8) { if (ch == ';') ch = '\n'; }
        double durMs = 35.0, fHz = 60.0;
        int probeN = 2;
        {
            std::string cUtf8 = WideToUtf8(cfgW);
            for (char& ch : cUtf8) { if (ch == ',') ch = ' '; }
            std::istringstream iss(cUtf8);
            iss >> durMs >> fHz >> probeN;
        }
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddTransientLaplaceCircuitAction(
            st->doc, st->cmdStack, tgt, x, y, netUtf8, durMs, fHz, probeN, st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_CYCLE_MATH_SUBMODE:
        AppActions::CycleSelectedMathSubModeAction(
            st->doc, st->cmdStack, CurrentTarget(st), st->selectedObjectId, st->statusBannerUtf8);
        break;
    case CMD_PARAM_SLIDER: {
        std::wstring varW = L"k";
        std::wstring valW = L"2.0, 0.2, 5.0";
        if (!ShowTextPrompt(st->hwndMain, L"Slider de Par\u00E1metro Reactivo",
            L"Variable (ej. k, R_load, F):", varW, L"ValorActual, M\u00EDn, M\u00E1x [Unidad]:", &valW) || varW.empty()) break;
        double curV = 2.0, minV = 0.2, maxV = 5.0;
        std::string unitSuf;
        {
            std::string s = WideToUtf8(valW);
            for (char& ch : s) { if (ch == ',') ch = ' '; }
            std::istringstream iss(s);
            iss >> curV >> minV >> maxV >> unitSuf;
        }
        AppActions::UpdateParameterSliderAction(
            st->doc, st->cmdStack, CurrentTarget(st), WideToUtf8(varW), curV, minV, maxV, unitSuf, st->statusBannerUtf8);
        break;
    }
    case CMD_OCR_CONFIRM_FORMULA: {
        // Confirmation Card workflow before converting OCR/handwriting to executable formula (Superando p.12 / F-046)
        std::string seedCandidate = "P_tot = 3 bar + 100 psi";
        if (st->doc.Mode() == DocumentMode::PaginatedPdf && st->activePageIndex < st->doc.Pages().size()) {
            const auto& ocr = st->doc.Pages()[st->activePageIndex].ocrTextUtf8;
            if (!ocr.empty()) {
                const auto nl = ocr.find('\n');
                seedCandidate = ocr.substr(0, nl == std::string::npos ? 80 : (std::min<std::size_t>)(80, nl));
            }
        }
        std::wstring candW = Utf8ToWide(seedCandidate);
        if (!ShowTextPrompt(st->hwndMain, L"Tarjeta de Confirmaci\u00F3n OCR -> F\u00F3rmula",
            L"Verifique o edite la expresi\u00F3n reconocida antes de ejecutar:", candW) || candW.empty()) {
            st->statusBannerUtf8 = "Conversi\xC3\xB3n OCR cancelada (trazo original intacto)";
            break;
        }
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddFormulaNodeAction(
            st->doc, st->cmdStack, tgt, x, y, WideToUtf8(candW), "", st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_EXPORT_LAB_HTML: {
        static constexpr COMDLG_FILTERSPEC kFilters[] = {
            { L"Memoria de C\u00E1lculo HTML5 (*.html)", L"*.html" },
            { L"Informe Markdown (*.md)", L"*.md" }
        };
        std::wstring outPath;
        if (!ShowSaveDialog(st->hwndMain, L"Exportar Memoria de C\u00E1lculo / Informe de Laboratorio",
            kFilters, 2, L"html", L"memoria_calculo_stem.html", outPath)) break;
        const std::wstring ext = GetFileExtLower(outPath);
        const std::string payload = (ext == L".md")
            ? Bridge::ExportCalculationReportMarkdown(st->doc)
            : Bridge::ExportCalculationReportHtml(st->doc);
        std::ofstream ofs(std::filesystem::path(outPath), std::ios::binary | std::ios::trunc);
        if (ofs) {
            ofs.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            st->statusBannerUtf8 = "Memoria de c\xC3\xA1" "lculo exportada (" + std::to_string(payload.size()) + " bytes)";
        } else {
            st->statusBannerUtf8 = "No se pudo guardar el informe";
        }
        break;
    }
    case CMD_TOGGLE_MINIMAP:
        st->minimapVisible = !st->minimapVisible;
        st->statusBannerUtf8 = st->minimapVisible ? "Minimapa activado" : "Minimapa oculto";
        break;
    case CMD_TOGGLE_RULER:
        if (!st->rulerState.visible) {
            RECT rc{};
            GetClientRect(st->hwndViewport, &rc);
            const DocPoint cWorld = st->camera.ScreenToWorld((rc.right - rc.left) * 0.5, (rc.bottom - rc.top) * 0.55);
            st->rulerState.centerX = cWorld.x;
            st->rulerState.centerY = cWorld.y;
            st->rulerState.angleDeg = 0.0;
            st->rulerState.visible = true;
        } else if (st->rulerState.angleDeg < 165.0) {
            st->rulerState.angleDeg += 15.0;
        } else {
            st->rulerState.visible = false;
            st->rulerState.angleDeg = 0.0;
        }
        st->statusBannerUtf8 = st->rulerState.visible
            ? ("Regla graduada activa (" + std::to_string(static_cast<int>(st->rulerState.angleDeg)) + "\xC2\xB0)")
            : "Regla graduada oculta";
        break;
    case CMD_CYCLE_ERASER_FILTER:
        if (st->eraserFilterMode == EraserFilterMode::All) {
            st->eraserFilterMode = EraserFilterMode::HighlighterOnly;
            st->statusBannerUtf8 = "Borrador Selectivo: SOLO RESALTADOR (protege tinta y f\xC3\xB3rmulas)";
        } else if (st->eraserFilterMode == EraserFilterMode::HighlighterOnly) {
            st->eraserFilterMode = EraserFilterMode::InkOnly;
            st->statusBannerUtf8 = "Borrador Selectivo: SOLO TINTA";
        } else {
            st->eraserFilterMode = EraserFilterMode::All;
            st->statusBannerUtf8 = "Borrador Selectivo: TODO";
        }
        st->activeTool = ToolKind::Eraser;
        break;
    case CMD_CLEAR_HIGHLIGHTS:
        AppActions::ClearItemsByFilterAction(
            st->doc, st->cmdStack, CurrentTarget(st), EraserFilterMode::HighlighterOnly, st->statusBannerUtf8);
        break;
    case CMD_TAG_OBJECT_BOARD: {
        if (st->selectedObjectId != 0) {
            std::wstring tagW = L"examen";
            if (ShowTextPrompt(st->hwndMain, L"Etiquetar Objeto (#tag)", L"Etiqueta (ej. examen, importante, duda):", tagW) && !tagW.empty()) {
                AppActions::AssignTagToObjectAction(
                    st->doc, st->cmdStack, CurrentTarget(st), st->selectedObjectId, WideToUtf8(tagW), st->statusBannerUtf8);
            }
        }
        const std::string board = AppActions::FormatTagSummaryBoardUtf8(st->doc);
        MessageBoxW(st->hwndMain, Utf8ToWide(board).c_str(), L"Tablero Resumen de Etiquetas (#tags)", MB_ICONINFORMATION);
        break;
    }
    case CMD_FOCUS_MODE:
        st->focusMode = !st->focusMode;
        LayoutMainChildren(st);
        st->statusBannerUtf8 = st->focusMode
            ? "Modo Concentraci\xC3\xB3n activado (panel lateral oculto)"
            : "Modo Concentraci\xC3\xB3n desactivado";
        break;
    case CMD_CASIO_REVERSE_KEYS: {
        std::wstring exprW = L"2*x + y = 7; x - y = 2";
        std::wstring modW = L"fx-991CW";
        if (!ShowTextPrompt(st->hwndMain, L"Puente Lienzo -> Teclas CASIO",
            L"Ecuaci\u00F3n, Matriz [[...]], SEL o Fasor:", exprW, L"Modelo (fx-991CW / fx-991LAX):", &modW) || exprW.empty()) break;
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddCasioReverseBridgeAction(
            st->doc, st->cmdStack, tgt, x, y, WideToUtf8(exprW), WideToUtf8(modW), st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_ADD_PHASOR_DIAG: {
        std::wstring phW = L"V_s = 120 @ 0 V; I_s = 40 @ -36.87 A; V_L = 96 @ 53.13 V";
        if (!ShowTextPrompt(st->hwndMain, L"Generador de Diagrama Fasorial",
            L"Fasores separados por ';' (ej. V = 120 @ 0 V; I = 40 @ -36.87 A):", phW) || phW.empty()) break;
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddPhasorDiagramNodeAction(
            st->doc, st->cmdStack, tgt, x, y, WideToUtf8(phW), st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_ADD_BODE_PLOT: {
        std::wstring netW = L"V1 1 0 1; R1 1 2 1k; C1 2 0 1u";
        std::wstring rngW = L"2, 10, 100000";
        if (!ShowTextPrompt(st->hwndMain, L"Diagrama de Bode Semilogar\u00EDtmico",
            L"Netlist AC (separado por ';'):", netW, L"NodoSalida, fMin_Hz, fMax_Hz:", &rngW) || netW.empty()) break;
        std::string netUtf8 = WideToUtf8(netW);
        for (char& ch : netUtf8) { if (ch == ';') ch = '\n'; }
        int outN = 2;
        double f0 = 10.0, f1 = 100000.0;
        {
            std::string r = WideToUtf8(rngW);
            for (char& ch : r) { if (ch == ',') ch = ' '; }
            std::istringstream iss(r);
            iss >> outN >> f0 >> f1;
        }
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const ObjectTarget tgt = CurrentTarget(st);
        const std::uint64_t newObjId = AppActions::AddBodePlotNodeAction(
            st->doc, st->cmdStack, tgt, x, y, netUtf8, outN, f0, f1, st->statusBannerUtf8);
        if (newObjId != 0) { st->selectedObjectId = newObjId; st->selectedIsCanvas = tgt.isCanvas; st->selectedPageIndex = tgt.pageIndex; }
        break;
    }
    case CMD_VAR_INSPECTOR: {
        const std::string rep = AppActions::FormatVariableInspectorUtf8(st->doc);
        MessageBoxW(st->hwndMain, Utf8ToWide(rep).c_str(), L"Inspector F\u00EDsico de Variables y Unidades SI", MB_ICONINFORMATION);
        break;
    }
    case CMD_TEACHER_PRIVATE_LAYER:
        AppActions::AddPrivateTeacherLayerAction(st->doc, st->statusBannerUtf8);
        break;
    case CMD_FILE_MENU: {
        HMENU hm = CreatePopupMenu();
        AppendMenuW(hm, MF_STRING, CMD_OPEN,         L"Abrir documento... (Ctrl+O)");
        AppendMenuW(hm, MF_STRING, CMD_SAVE_CUDOC,   L"Guardar proyecto .cudoc (Ctrl+S)");
        AppendMenuW(hm, MF_STRING, CMD_EXPORT_PDF,   L"Exportar PDF...");
        AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hm, MF_STRING, CMD_MERGE_PDF,    L"Combinar PDFs...");
        AppendMenuW(hm, MF_STRING, CMD_SPLIT_PDF,    L"Dividir / Extraer p\u00E1ginas...");
        AppendMenuW(hm, MF_STRING, CMD_SIGN_PDF,     L"Firmar PDF (PKCS#7)...");
        AppendMenuW(hm, MF_STRING, CMD_VERIFY_SIG,   L"Verificar firma digital...");
        AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hm, MF_STRING, CMD_RUN_OCR,      L"Reconocer texto (OCR)");
        AppendMenuW(hm, MF_STRING, CMD_EXPORT_DOCX,  L"Exportar Word (.docx)...");
        AppendMenuW(hm, MF_STRING, CMD_EXPORT_PPTX,  L"Exportar PowerPoint (.pptx)...");
        AppendMenuW(hm, MF_STRING, CMD_IMPORT_DOCX,  L"Importar Word (.docx)...");
        AppendMenuW(hm, MF_STRING, CMD_IMPORT_PPTX,  L"Importar PowerPoint (.pptx)...");
        AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hm, MF_STRING, CMD_INSERT_IMAGE, L"Insertar imagen...");
        AppendMenuW(hm, MF_STRING, CMD_PRINT,        L"Imprimir (Ctrl+P)");
        POINT pt{ 54, kTopChromeH };
        ClientToScreen(st->hwndMain, &pt);
        const UINT chosen = TrackPopupMenu(hm, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, st->hwndMain, nullptr);
        DestroyMenu(hm);
        if (chosen != 0) { ExecuteToolbarCommand(st, static_cast<WORD>(chosen)); return; }
        break;
    }
    case CMD_TOGGLE_SIDEBAR:
        st->focusMode = false;
        st->sidebarVisible = !st->sidebarVisible;
        LayoutMainChildren(st);
        break;
    case CMD_TOGGLE_INSPECTOR:
        st->focusMode = false;
        st->inspectorVisible = !st->inspectorVisible;
        LayoutMainChildren(st);
        break;
    case CMD_TOGGLE_ACADEMIC:
        st->focusMode = false;
        if (st->inspectorVisible && st->inspectorTab == 0) {
            st->inspectorVisible = false;
        } else {
            st->inspectorTab = 0;
            st->inspectorVisible = true;
        }
        LayoutMainChildren(st);
        break;
    case CMD_TOGGLE_SEARCH:
        st->searchVisible = !st->searchVisible;
        LayoutMainChildren(st);
        if (st->searchVisible && st->hwndSearchEdit) SetFocus(st->hwndSearchEdit);
        break;
    case CMD_TOGGLE_APPEARANCE:
        st->appearancePopoverOpen = !st->appearancePopoverOpen;
        break;
    case CMD_MORE_OVERFLOW: {
        HMENU hm = CreatePopupMenu();
        AppendMenuW(hm, MF_STRING, CMD_STEM_MENU,       L"Ingenier\u00EDa STEM (+F\u00F3rmula / Circuito / Gr\u00E1fica)...");
        AppendMenuW(hm, MF_STRING, CMD_STUDY_MENU,      L"Estudio SM-2 / Capas / Audio...");
        AppendMenuW(hm, MF_STRING, CMD_TOGGLE_ACADEMIC, L"Panel Acad\u00E9mico (TEC Digital y HorariosTEC)");
        AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hm, MF_STRING, CMD_ADD_PAGE,        L"A\u00F1adir p\u00E1gina");
        AppendMenuW(hm, MF_STRING, CMD_ROTATE_PAGE,     L"Rotar p\u00E1gina 90\u00B0");
        AppendMenuW(hm, MF_STRING, CMD_DEL_PAGE,        L"Eliminar p\u00E1gina actual");
        AppendMenuW(hm, MF_STRING, CMD_PAPER_NEXT,      L"Cambiar patr\u00F3n de papel");
        AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hm, MF_STRING, CMD_UNDO,            L"Deshacer (Ctrl+Z)");
        AppendMenuW(hm, MF_STRING, CMD_REDO,            L"Rehacer (Ctrl+Y)");
        AppendMenuW(hm, MF_STRING, CMD_TOGGLE_RULER,    L"Regla graduada");
        AppendMenuW(hm, MF_STRING, CMD_TOGGLE_MINIMAP,  L"Minimapa de lienzo");
        AppendMenuW(hm, MF_STRING, CMD_FOCUS_MODE,      L"Modo Concentraci\u00F3n");
        RECT rc{};
        GetClientRect(st->hwndMain, &rc);
        POINT pt{ rc.right - 120, kTopChromeH };
        ClientToScreen(st->hwndMain, &pt);
        const UINT chosen = TrackPopupMenu(hm, TPM_RETURNCMD | TPM_RIGHTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, st->hwndMain, nullptr);
        DestroyMenu(hm);
        if (chosen != 0) { ExecuteToolbarCommand(st, static_cast<WORD>(chosen)); return; }
        break;
    }
    case CMD_ACADEMIC_SYNC_TEC: {
        st->academicStatusUtf8 = "TEC Digital: Pol\xC3\xADtica Solo Lectura activa (0 mutaciones) \xC2\xB7 B\xC3\xB3veda DPAPI lista";
        st->statusBannerUtf8 = st->academicStatusUtf8;
        break;
    }
    case CMD_ACADEMIC_SOLVE_SCHEDULE: {
        std::string sum = "HorariosTEC: 0 combinaciones v\xC3\xA1lidas";
#if defined(DOC_STUDIO_HAS_ACADEMIC)
        academic::horarios_tec::CourseCatalog cat{};
        cat.titleUtf8 = "Plan Semestral TEC";
        academic::horarios_tec::Course c1{};
        c1.codeUtf8 = "MA1103"; c1.nameUtf8 = "C\xC3\xA1lculo y \xC3\x81lgebra Lineal"; c1.credits = 4;
        academic::horarios_tec::Group g1{};
        g1.idUtf8 = "Gr 50"; g1.modalityUtf8 = "Regular"; g1.seatsAvailable = 15;
        g1.sessions.push_back({ academic::horarios_tec::DayOfWeek::Monday, 585, 690, "B3-04", "B3", "Cartago" });
        g1.sessions.push_back({ academic::horarios_tec::DayOfWeek::Wednesday, 585, 690, "B3-04", "B3", "Cartago" });
        c1.groups.push_back(g1);
        academic::horarios_tec::Course c2{};
        c2.codeUtf8 = "FI1102"; c2.nameUtf8 = "F\xC3\xADsica General II"; c2.credits = 4;
        academic::horarios_tec::Group g2{};
        g2.idUtf8 = "Gr 51"; g2.modalityUtf8 = "Semipresencial"; g2.seatsAvailable = 8;
        g2.sessions.push_back({ academic::horarios_tec::DayOfWeek::Tuesday, 780, 890, "D2-10", "D2", "Cartago" });
        g2.sessions.push_back({ academic::horarios_tec::DayOfWeek::Thursday, 780, 890, "D2-10", "D2", "Cartago" });
        c2.groups.push_back(g2);
        cat.courses.push_back(std::move(c1));
        cat.courses.push_back(std::move(c2));

        academic::horarios_tec::SolverPreferences prefs{};
        prefs.maxResults = 10;
        const auto res = academic::horarios_tec::HorariosSolver::Solve(cat, {}, prefs);
        if (res.HasSolutions()) {
            const auto& best = res.schedules.front();
            sum = "HorariosTEC: " + std::to_string(res.schedules.size()) + " opci\xC3\xB3n \xC3\xB3ptima (0 choques, " +
                  std::to_string(best.metrics.totalCredits) + " cr\xC3\xA9" "ditos, " +
                  std::to_string(best.metrics.activeDaysCount) + " d\xC3\xAD" "as)";
        }
#endif
        st->scheduleSummaryUtf8 = sum;
        st->statusBannerUtf8 = sum;
        break;
    }
    case CMD_ACADEMIC_SEARCH_MATERIALS: {
        st->ftsIndex.IndexDocument("active", st->doc);
        std::wstring qW = L"";
        if (ShowTextPrompt(st->hwndMain, L"Buscar en Materiales Offline y B\u00F3veda", L"Consulta FTS:", qW) && !qW.empty()) {
            const auto hits = st->ftsIndex.Query(WideToUtf8(qW));
            st->academicStatusUtf8 = "B\xC3\xBA" "squeda offline: " + std::to_string(hits.size()) + " coincidencias";
            st->statusBannerUtf8 = st->academicStatusUtf8;
        }
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

void UpdateAppearanceFromPoint(StudioAppState* st, POINT pt, bool isDrag);

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
        HDC hdcVp = BeginPaint(hwnd, &ps);
        if (st) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const int vw = (std::max)(1, static_cast<int>(rc.right - rc.left));
            const int vh = (std::max)(1, static_cast<int>(rc.bottom - rc.top));
            ViewportOverlayState ov{};
            PopulateViewportOverlay(st, ov);

            // Live Liquid Glass 2.0 floating Appearance Popover composited INSIDE Direct2D
            // (refracts real D2D scene pixels with zero GDI-over-swapchain flicker!)
            RasterImageBgra32 popPatch;
            if (st->appearancePopoverOpen && hdcVp) {
                RECT vpInMain{};
                GetWindowRect(hwnd, &vpInMain);
                MapWindowPoints(HWND_DESKTOP, st->hwndMain, reinterpret_cast<LPPOINT>(&vpInMain), 2);
                RECT popLocal = st->appearancePopoverRc;
                OffsetRect(&popLocal, -vpInMain.left, -vpInMain.top);
                RECT trkLocal = st->opacityTrackRc;
                OffsetRect(&trkLocal, -vpInMain.left, -vpInMain.top);

                const int padX = 12;
                const int padY = 14;
                const int capL = (std::max)(0, static_cast<int>(popLocal.left) - padX);
                const int capT = (std::max)(0, static_cast<int>(popLocal.top) - padY);
                const int capR = (std::min)(vw, static_cast<int>(popLocal.right) + padX);
                const int capB = (std::min)(vh, static_cast<int>(popLocal.bottom) + padY);
                const int pw = capR - capL;
                const int ph = capB - capT;
                if (pw > 16 && ph > 16) {
                    RasterImageBgra32 sceneBg;
                    if (st->renderer.RenderHwndViewportToOffscreen(st->doc, st->camera, vw, vh, &st->pageCache, sceneBg, &ov) &&
                        sceneBg.Valid()) {
                        BITMAPINFO bmi{};
                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bmi.bmiHeader.biWidth = pw;
                        bmi.bmiHeader.biHeight = -ph;
                        bmi.bmiHeader.biPlanes = 1;
                        bmi.bmiHeader.biBitCount = 32;
                        bmi.bmiHeader.biCompression = BI_RGB;
                        void* bits = nullptr;
                        HDC memDc = CreateCompatibleDC(hdcVp);
                        HBITMAP hDib = memDc ? CreateDIBSection(hdcVp, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
                        if (memDc && hDib && bits) {
                            HGDIOBJ oldB = SelectObject(memDc, hDib);
                            auto* dst = static_cast<std::uint8_t*>(bits);
                            for (int y = 0; y < ph; ++y) {
                                const std::uint8_t* sRow = sceneBg.pixels.data() +
                                    (static_cast<std::size_t>(capT + y) * static_cast<std::size_t>(vw) + static_cast<std::size_t>(capL)) * 4u;
                                std::memcpy(dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(pw) * 4u, sRow, static_cast<std::size_t>(pw) * 4u);
                            }
                            const RECT savedPop = st->appearancePopoverRc;
                            const RECT savedTrk = st->opacityTrackRc;
                            st->appearancePopoverRc = { popLocal.left - capL, popLocal.top - capT, popLocal.right - capL, popLocal.bottom - capT };
                            st->opacityTrackRc      = { trkLocal.left - capL, trkLocal.top - capT, trkLocal.right - capL, trkLocal.bottom - capT };
                            PaintAppearancePopoverOnHdc(st, memDc, pw, ph);
                            GdiFlush();
                            st->appearancePopoverRc = savedPop;
                            st->opacityTrackRc = savedTrk;

                            popPatch.width = pw;
                            popPatch.height = ph;
                            popPatch.pixels.resize(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph) * 4u);
                            std::memcpy(popPatch.pixels.data(), bits, popPatch.pixels.size());
                            for (std::size_t k = 3; k < popPatch.pixels.size(); k += 4) popPatch.pixels[k] = 255;

                            ov.floatingOverlayBgra = &popPatch;
                            ov.floatingOverlayScreenRect = {
                                static_cast<double>(capL), static_cast<double>(capT),
                                static_cast<double>(pw), static_cast<double>(ph)
                            };
                            SelectObject(memDc, oldB);
                        }
                        if (hDib) DeleteObject(hDib);
                        if (memDc) DeleteDC(memDc);
                    }
                }
            }

            HWND mainH = st->hwndMain;
            st->renderer.RenderViewport(
                st->doc, st->camera,
                vw, vh,
                &st->pageCache,
                st->asyncGen,
                [mainH]() { if (IsWindow(mainH)) PostMessageW(mainH, WM_APP_ASYNC_PAGE_READY, 0, 0); },
                &ov);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CAPTURECHANGED: {
        if (st) {
            st->draggingOpacitySlider = false;
            st->panning = false;
            st->drawing = false;
            st->laserPointer.active = false;
        }
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
        if (msg == WM_LBUTTONDOWN && st->appearancePopoverOpen) {
            POINT ptMain{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            MapWindowPoints(hwnd, st->hwndMain, &ptMain, 1);
            if (PtInRect(&st->appearancePopoverRc, ptMain)) {
                if (ptMain.x >= st->opacityTrackRc.left - 8 && ptMain.x <= st->opacityTrackRc.right + 8 &&
                    ptMain.y >= st->opacityTrackRc.top - 6 && ptMain.y <= st->opacityTrackRc.bottom + 6) {
                    st->draggingOpacitySlider = true;
                    SetCapture(hwnd);
                }
                UpdateAppearanceFromPoint(st, ptMain, false);
                return 0;
            }
            st->appearancePopoverOpen = false;
            InvalidateViews(st);
        }
        SetFocus(hwnd);
        float pressure = 0.55f;
        float tiltX = 0.0f;
        float tiltY = 0.0f;
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
                    tiltX = static_cast<float>(pen.tiltX);
                    tiltY = static_cast<float>(pen.tiltY);
                }
            }
        } else {
            extractPointerWorld(lp, pressure, sp);
        }

        if (st->activeTool == ToolKind::Hand || (GetKeyState(VK_SPACE) & 0x8000)) {
            st->panning = true;
            st->panMoved = false;
            st->panStartPt = { static_cast<LONG>(sp.x), static_cast<LONG>(sp.y) };
            st->lastPanPt = st->panStartPt;
            SetCapture(hwnd);
            return 0;
        }

        const DocPoint wpWorld = st->camera.ScreenToWorld(sp.x, sp.y);
        if (st->activeTool == ToolKind::Laser) {
            st->drawing = true;
            st->laserPointer.active = true;
            st->laserPointer.worldX = wpWorld.x;
            st->laserPointer.worldY = wpWorld.y;
            st->laserPointer.trail.clear();
            st->laserPointer.trail.push_back(wpWorld);
            SetCapture(hwnd);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

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
            const ObjectTarget erTarget{ isCanvas, pgIdx };
            const auto rawHits = CollectHits(st, isCanvas, pgIdx, lx, ly, 10.0);
            const auto hits = InkEngine::FilterHitsByEraserMode(st->doc, erTarget, rawHits, st->eraserFilterMode, true);
            if (!hits.empty()) {
                st->cmdStack.Execute(st->doc, std::make_unique<BatchEraseObjectsCommand>(erTarget, hits));
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
                tb.layerId = st->doc.ActiveLayerId();
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
        st->draftObj.layerId = st->doc.ActiveLayerId();
        st->draftObj.strokeColor = st->penColor;
        st->draftObj.strokeWidth = (st->activeTool == ToolKind::Highlighter) ? 14.0 : st->penWidth;
        st->draftObj.isHighlighter = (st->activeTool == ToolKind::Highlighter);

        if (st->activeTool == ToolKind::Pen || st->activeTool == ToolKind::Highlighter || st->activeTool == ToolKind::Lasso) {
            st->draftObj.kind = ObjectKind::InkStroke;
            st->hasPredictedTip = false;
            if (st->activeTool == ToolKind::Lasso) {
                st->draftObj.strokeColor = DocColor::RGBA(37, 99, 235, 220);
                st->draftObj.strokeWidth = 2.0;
            } else if (st->rulerState.visible) {
                const DocPoint snapped = InkEngine::ProjectPointOntoRuler({ lx, ly }, st->rulerState);
                lx = snapped.x;
                ly = snapped.y;
            }
            const std::uint64_t relMs = CurrentStrokeRelMs(st);
            st->draftObj.inkStartTimeMs = relMs;
            st->draftObj.inkEndTimeMs = relMs;
            st->draftObj.inkPoints.push_back({ lx, ly, pressure, tiltX, tiltY, relMs });
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
        if (msg == WM_MOUSEMOVE && st->draggingOpacitySlider && st->appearancePopoverOpen) {
            POINT ptMain{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            MapWindowPoints(hwnd, st->hwndMain, &ptMain, 1);
            UpdateAppearanceFromPoint(st, ptMain, true);
            return 0;
        }
        float pressure = 0.55f;
        float tiltX = 0.0f;
        float tiltY = 0.0f;
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
                    tiltX = static_cast<float>(pen.tiltX);
                    tiltY = static_cast<float>(pen.tiltY);
                }
            }
        } else {
            extractPointerWorld(lp, pressure, sp);
        }

        if (st->panning) {
            if (std::hypot(sp.x - st->panStartPt.x, sp.y - st->panStartPt.y) > 3.5) {
                st->panMoved = true;
            }
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
        if (st->activeTool == ToolKind::Laser) {
            st->laserPointer.active = true;
            st->laserPointer.worldX = wpWorld.x;
            st->laserPointer.worldY = wpWorld.y;
            st->laserPointer.trail.push_back(wpWorld);
            if (st->laserPointer.trail.size() > 28) {
                st->laserPointer.trail.erase(st->laserPointer.trail.begin());
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        double lx = wpWorld.x, ly = wpWorld.y;
        if (!st->draftIsCanvas) {
            if (const auto* pl = st->doc.Layout().PlacementAt(st->draftPageIdx)) {
                const DocRect wr = pl->WorldRect();
                lx = wpWorld.x - wr.x;
                ly = wpWorld.y - wr.y;
            }
        }

        if (st->draftObj.kind == ObjectKind::InkStroke) {
            if (st->hasPredictedTip && st->draftObj.inkPoints.size() >= 2) {
                st->draftObj.inkPoints.pop_back();
                st->hasPredictedTip = false;
            }

            auto appendLocalSample = [&](double sxLocal, double syLocal, float pr, float tx, float ty) {
                if (st->activeTool != ToolKind::Lasso && st->rulerState.visible) {
                    const DocPoint snapped = InkEngine::ProjectPointOntoRuler({ sxLocal, syLocal }, st->rulerState);
                    sxLocal = snapped.x;
                    syLocal = snapped.y;
                }
                if (!st->draftObj.inkPoints.empty()) {
                    const auto& prev = st->draftObj.inkPoints.back();
                    if (std::hypot(sxLocal - prev.x, syLocal - prev.y) >= 1.1) {
                        const std::uint64_t relMs = CurrentStrokeRelMs(st);
                        st->draftObj.inkEndTimeMs = relMs;
                        st->draftObj.inkPoints.push_back({ sxLocal, syLocal, pr, tx, ty, relMs });
                    }
                }
            };

            bool ingestedCoalesced = false;
            if (msg == WM_POINTERUPDATE) {
                const UINT32 pid = GET_POINTERID_WPARAM(wp);
                POINTER_INFO pi{};
                if (GetPointerInfo(pid, &pi) && pi.historyCount > 1) {
                    UINT32 count = (std::min<UINT32>)(pi.historyCount, 64u);
                    if (pi.pointerType == PT_PEN) {
                        std::vector<POINTER_PEN_INFO> penHist(count);
                        if (GetPointerPenInfoHistory(pid, &count, penHist.data()) && count > 0) {
                            // Win32 returns coalesced history newest-first -> iterate oldest-to-newest
                            for (int hi = static_cast<int>(count) - 1; hi >= 0; --hi) {
                                POINT hpt = penHist[static_cast<std::size_t>(hi)].pointerInfo.ptPixelLocation;
                                ScreenToClient(hwnd, &hpt);
                                const DocPoint hw = st->camera.ScreenToWorld(hpt.x, hpt.y);
                                double hlx = hw.x, hly = hw.y;
                                if (!st->draftIsCanvas) {
                                    if (const auto* pl = st->doc.Layout().PlacementAt(st->draftPageIdx)) {
                                        const DocRect wr = pl->WorldRect();
                                        hlx -= wr.x; hly -= wr.y;
                                    }
                                }
                                const float hPr = std::clamp(static_cast<float>(penHist[static_cast<std::size_t>(hi)].pressure) / 1024.0f, 0.1f, 1.0f);
                                appendLocalSample(hlx, hly, hPr,
                                    static_cast<float>(penHist[static_cast<std::size_t>(hi)].tiltX),
                                    static_cast<float>(penHist[static_cast<std::size_t>(hi)].tiltY));
                            }
                            ingestedCoalesced = true;
                        }
                    } else {
                        std::vector<POINTER_INFO> ptHist(count);
                        if (GetPointerInfoHistory(pid, &count, ptHist.data()) && count > 0) {
                            for (int hi = static_cast<int>(count) - 1; hi >= 0; --hi) {
                                POINT hpt = ptHist[static_cast<std::size_t>(hi)].ptPixelLocation;
                                ScreenToClient(hwnd, &hpt);
                                const DocPoint hw = st->camera.ScreenToWorld(hpt.x, hpt.y);
                                double hlx = hw.x, hly = hw.y;
                                if (!st->draftIsCanvas) {
                                    if (const auto* pl = st->doc.Layout().PlacementAt(st->draftPageIdx)) {
                                        const DocRect wr = pl->WorldRect();
                                        hlx -= wr.x; hly -= wr.y;
                                    }
                                }
                                appendLocalSample(hlx, hly, pressure, 0.0f, 0.0f);
                            }
                            ingestedCoalesced = true;
                        }
                    }
                }
            }
            if (!ingestedCoalesced) {
                appendLocalSample(lx, ly, pressure, tiltX, tiltY);
            }

            if (st->activeTool != ToolKind::Lasso && !st->rulerState.visible && st->draftObj.inkPoints.size() >= 3) {
                InkPoint pred{};
                if (InkEngine::PredictNextInkPoint(st->draftObj.inkPoints, pred, 12.0) &&
                    std::hypot(pred.x - st->draftObj.inkPoints.back().x, pred.y - st->draftObj.inkPoints.back().y) >= 0.4) {
                    st->draftObj.inkPoints.push_back(pred);
                    st->hasPredictedTip = true;
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
        if (msg == WM_LBUTTONUP && st->draggingOpacitySlider) {
            st->draggingOpacitySlider = false;
            ReleaseCapture();
            return 0;
        }
        if (st->panning) {
            st->panning = false;
            ReleaseCapture();
            if (!st->panMoved && st->activeTool == ToolKind::Hand) {
                const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                const DocPoint wpWorld = st->camera.ScreenToWorld(pt.x, pt.y);
                bool isCanvas = (st->doc.Mode() == DocumentMode::InfiniteCanvas);
                std::size_t pgIdx = st->activePageIndex;
                double lx = wpWorld.x, ly = wpWorld.y;
                if (isCanvas || st->doc.Layout().HitTestWorldPoint(wpWorld.x, wpWorld.y, pgIdx, lx, ly)) {
                    const double tol = std::clamp(8.0 / (std::max)(0.25, st->camera.zoom), 3.0, 18.0);
                    const auto hits = CollectHits(st, isCanvas, pgIdx, lx, ly, tol);
                    if (!hits.empty()) {
                        st->selectedObjectId = hits.back();
                        st->selectedIsCanvas = isCanvas;
                        st->selectedPageIndex = pgIdx;
                        AppActions::ToggleStudyTapeIfHit(
                            st->doc, st->cmdStack, ObjectTarget{ isCanvas, pgIdx },
                            st->selectedObjectId, st->statusBannerUtf8);
                    } else {
                        st->selectedObjectId = 0;
                        st->lassoSelectedIds.clear();
                    }
                    UpdateWindowTitle(st);
                    InvalidateViews(st);
                }
            }
            return 0;
        }
        if (!st->drawing) return 0;
        st->drawing = false;
        ReleaseCapture();

        if (st->activeTool == ToolKind::Laser) {
            st->laserPointer.active = false;
            InvalidateViews(st);
            return 0;
        }

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
        } else if (st->draftObj.kind == ObjectKind::InkStroke) {
            if (st->hasPredictedTip && st->draftObj.inkPoints.size() >= 2) {
                st->draftObj.inkPoints.pop_back();
                st->hasPredictedTip = false;
            }
            AppActions::HandleStrokeCompletedAction(
                st->doc,
                st->cmdStack,
                ObjectTarget{ st->draftIsCanvas, st->draftPageIdx },
                st->draftObj,
                (st->activeTool == ToolKind::Lasso),
                st->snapShapes,
                st->penStabilization,
                st->lassoSelectedIds,
                st->selectedObjectId,
                st->statusBannerUtf8);
            st->selectedIsCanvas = st->draftIsCanvas;
            st->selectedPageIndex = st->draftPageIdx;
        } else {
            st->draftObj.RecomputeBounds();
            if (!st->draftObj.bounds.Empty()) {
                st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(
                    ObjectTarget{ st->draftIsCanvas, st->draftPageIdx }, std::move(st->draftObj)));
                st->selectedIsCanvas = st->draftIsCanvas;
                st->selectedPageIndex = st->draftPageIdx;
                st->selectedObjectId = AppActions::LastInsertedId(st->doc, ObjectTarget{ st->draftIsCanvas, st->draftPageIdx });
            }
        }
        UpdateWindowTitle(st);
        InvalidateViews(st);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------- Appearance Popover Overlay (Liquid Glass 2.0) ----------------

void UpdateAppearanceFromPoint(StudioAppState* st, POINT pt, bool isDrag) {
    if (!st || !st->appearancePopoverOpen) return;
    const RECT& tr = st->opacityTrackRc;
    if (isDrag || (pt.x >= tr.left - 8 && pt.x <= tr.right + 8 && pt.y >= tr.top - 6 && pt.y <= tr.bottom + 6)) {
        const float frac = std::clamp(
            static_cast<float>(pt.x - tr.left) / static_cast<float>((std::max)(1, static_cast<int>(tr.right - tr.left))),
            0.0f, 1.0f);
        st->glassSettings.userOpacitySlider = frac;
        st->glassSettings.reduceTransparency = false;
        UiGlass::SaveGlassSettings(st->glassSettings);
        InvalidateViews(st);
        return;
    }
    const RECT& pr = st->appearancePopoverRc;
    // Theme segmented buttons: Claro | Oscuro | Contraste
    const int segY0 = pr.top + 46;
    const int segY1 = pr.top + 76;
    if (pt.y >= segY0 && pt.y <= segY1 && pt.x >= pr.left + 16 && pt.x <= pr.right - 16) {
        const int segW = (pr.right - pr.left - 32) / 3;
        const int idx = std::clamp(static_cast<int>((pt.x - (pr.left + 16)) / (std::max)(1, segW)), 0, 2);
        st->glassSettings.mode = static_cast<UiGlass::AppearanceMode>(idx);
        const bool isHcMode = (st->glassSettings.mode == UiGlass::AppearanceMode::HighContrast);
        st->glassSettings.increaseContrast = isHcMode;
        st->glassSettings.reduceTransparency = isHcMode;
        UiGlass::ApplyGoldenGateWindowFrame(st->hwndMain, st->glassSettings.IsDark());
        UiGlass::SaveGlassSettings(st->glassSettings);
        InvalidateViews(st);
        return;
    }
    // Accessibility toggles
    const int t1Y0 = pr.top + 164;
    const int t1Y1 = pr.top + 188;
    const int t2Y0 = pr.top + 194;
    const int t2Y1 = pr.top + 218;
    if (pt.x >= pr.left + 16 && pt.x <= pr.right - 16) {
        if (pt.y >= t1Y0 && pt.y <= t1Y1) {
            st->glassSettings.reduceTransparency = !st->glassSettings.reduceTransparency;
            UiGlass::SaveGlassSettings(st->glassSettings);
            InvalidateViews(st);
            return;
        }
        if (pt.y >= t2Y0 && pt.y <= t2Y1) {
            st->glassSettings.increaseContrast = !st->glassSettings.increaseContrast;
            UiGlass::SaveGlassSettings(st->glassSettings);
            InvalidateViews(st);
            return;
        }
    }
}

void PaintAppearancePopoverOnHdc(StudioAppState* st, HDC hdcTarget, int clientW, int clientH) {
    if (!st || !st->appearancePopoverOpen) return;
    (void)clientW;
    (void)clientH;
    const RECT pr = st->appearancePopoverRc;
    const UiGlass::GlassPalette pal = UiGlass::GetPalette(st->glassSettings);

    // True optical Liquid Glass 2.0 panel refracting live scene pixels behind the popover
    UiGlass::DrawLiquidGlassCapsuleOnHdc(hdcTarget, pr, st->glassSettings, 16.0f);

    Gdiplus::Graphics g(hdcTarget);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    SetBkMode(hdcTarget, TRANSPARENT);

    // Header
    HGDIOBJ oldF = SelectObject(hdcTarget, st->hUiBoldFont ? st->hUiBoldFont : st->hUiFont);
    SetTextColor(hdcTarget, pal.labelPrimary.ToColorRef());
    RECT titleRc{ pr.left + 18, pr.top + 14, pr.right - 18, pr.top + 36 };
    DrawTextW(hdcTarget, L"Apariencia", -1, &titleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    // Theme Segmented Control (concentric radius = max(4, 16 - 6) = 10)
    RECT segOuter{ pr.left + 16, pr.top + 46, pr.right - 16, pr.top + 76 };
    Gdiplus::GraphicsPath segTrack;
    UiGlass::AddRoundPath(segTrack, static_cast<float>(segOuter.left), static_cast<float>(segOuter.top),
        static_cast<float>(segOuter.right - segOuter.left), static_cast<float>(segOuter.bottom - segOuter.top), 10.0f);
    Gdiplus::SolidBrush trBr(st->glassSettings.IsDark() ? Gdiplus::Color(220, 18, 22, 30) : Gdiplus::Color(175, 224, 231, 242));
    g.FillPath(&trBr, &segTrack);

    const wchar_t* themeNames[3] = { L"Claro", L"Oscuro", L"Contraste" };
    const int sw = (segOuter.right - segOuter.left - 6) / 3;
    SelectObject(hdcTarget, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
    for (int i = 0; i < 3; ++i) {
        const bool sel = (static_cast<int>(st->glassSettings.mode) == i);
        RECT sr{ segOuter.left + 3 + i * sw, segOuter.top + 3, segOuter.left + 3 + (i + 1) * sw, segOuter.bottom - 3 };
        if (sel) {
            Gdiplus::GraphicsPath sp;
            UiGlass::AddRoundPath(sp, static_cast<float>(sr.left), static_cast<float>(sr.top),
                static_cast<float>(sr.right - sr.left), static_cast<float>(sr.bottom - sr.top), 7.5f);
            Gdiplus::SolidBrush sbr(UiGlass::ToGdip(pal.glassPillSelected));
            g.FillPath(&sbr, &sp);
            Gdiplus::Pen spen(UiGlass::ToGdip(pal.accentBlue, 200), 1.0f);
            g.DrawPath(&spen, &sp);
        }
        const COLORREF inactiveFg = st->glassSettings.IsDark() ? RGB(214, 222, 236) : pal.labelSecondary.ToColorRef();
        const COLORREF activeFg = st->glassSettings.IsDark() ? RGB(122, 198, 255) : pal.accentBlue.ToColorRef();
        SetTextColor(hdcTarget, sel ? activeFg : inactiveFg);
        DrawTextW(hdcTarget, themeNames[i], -1, &sr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // macOS 27 Golden Gate continuous UserOpacitySlider (0% Ultra-Clear .. 100% Frosted Opaque)
    const int pct = static_cast<int>(std::lround(st->glassSettings.EffectiveOpacity() * 100.0f));
    const wchar_t* modeTag = (pct <= 20) ? L"Claro" : (pct >= 75 ? L"Esmerilado" : L"Equilibrado");
    std::wstring opLbl = L"Opacidad del cristal: " + std::to_wstring(pct) + L"% (" + modeTag + L")";
    SetTextColor(hdcTarget, pal.labelPrimary.ToColorRef());
    RECT opTxtRc{ pr.left + 18, pr.top + 92, pr.right - 18, pr.top + 114 };
    DrawTextW(hdcTarget, opLbl.c_str(), -1, &opTxtRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const RECT tr = st->opacityTrackRc;
    const float ty = static_cast<float>(tr.top + tr.bottom) * 0.5f;
    const float tw = static_cast<float>(tr.right - tr.left);
    Gdiplus::GraphicsPath trackP;
    UiGlass::AddRoundPath(trackP, static_cast<float>(tr.left), ty - 3.0f, tw, 6.0f, 3.0f);
    Gdiplus::SolidBrush trackBg(st->glassSettings.IsDark() ? Gdiplus::Color(180, 62, 70, 86) : Gdiplus::Color(180, 206, 214, 228));
    g.FillPath(&trackBg, &trackP);

    const float fillW = (std::max)(6.0f, tw * st->glassSettings.EffectiveOpacity());
    Gdiplus::GraphicsPath fillP;
    UiGlass::AddRoundPath(fillP, static_cast<float>(tr.left), ty - 3.0f, fillW, 6.0f, 3.0f);
    Gdiplus::SolidBrush fillBr(UiGlass::ToGdip(pal.accentBlue));
    g.FillPath(&fillBr, &fillP);

    const float thumbX = static_cast<float>(tr.left) + tw * st->glassSettings.EffectiveOpacity();
    UiGlass::DrawAquaVitreousOrb(g, thumbX, ty, 8.5f, Gdiplus::Color(255, 255, 255, 255), true, st->glassSettings.IsDark());

    // Accessibility Toggle Rows (crisp labelPrimary for guaranteed WCAG AA/AAA contrast over refracted glass)
    auto drawToggleRow = [&](int y0, const wchar_t* text, bool on) {
        RECT txt{ pr.left + 18, y0, pr.right - 64, y0 + 22 };
        SetTextColor(hdcTarget, pal.labelPrimary.ToColorRef());
        DrawTextW(hdcTarget, text, -1, &txt, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const float tx = static_cast<float>(pr.right - 54);
        const float ty0 = static_cast<float>(y0 + 2);
        Gdiplus::GraphicsPath tp;
        UiGlass::AddRoundPath(tp, tx, ty0, 36.0f, 18.0f, 9.0f);
        Gdiplus::SolidBrush tb(on ? UiGlass::ToGdip(pal.accentBlue) : Gdiplus::Color(190, 168, 178, 194));
        g.FillPath(&tb, &tp);
        const float kx = on ? (tx + 20.0f) : (tx + 2.0f);
        Gdiplus::SolidBrush kb(Gdiplus::Color(255, 255, 255, 255));
        g.FillEllipse(&kb, kx, ty0 + 2.0f, 14.0f, 14.0f);
    };
    drawToggleRow(pr.top + 164, L"Reducir transparencia", st->glassSettings.reduceTransparency);
    drawToggleRow(pr.top + 194, L"Aumentar contraste", st->glassSettings.increaseContrast);

    if (oldF) SelectObject(hdcTarget, oldF);
}

// ---------------- Edge-to-Edge Golden Gate Sidebar WndProc ----------------

LRESULT CALLBACK SidebarWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StudioAppState* st = reinterpret_cast<StudioAppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    constexpr int kNavAreaH = 196;
    constexpr int kItemH = 108;
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
        const int maxScroll = (std::max)(0, static_cast<int>(st->doc.Pages().size()) * kItemH - 220);
        st->sidebarScrollPos = std::clamp(st->sidebarScrollPos - (delta / 3), 0, maxScroll);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (!st) break;
        const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        // Check top colored-icon navigation items
        for (std::size_t i = 0; i < st->sidebarNavRects.size(); ++i) {
            if (PtInRect(&st->sidebarNavRects[i], pt)) {
                if (i == 0) ExecuteToolbarCommand(st, CMD_MODE_PDF);
                else if (i == 1) ExecuteToolbarCommand(st, CMD_MODE_CANVAS);
                else if (i == 2) { st->inspectorTab = 1; st->inspectorVisible = true; LayoutMainChildren(st); InvalidateViews(st); }
                else if (i == 3) ExecuteToolbarCommand(st, CMD_STUDY_MENU);
                else if (i == 4) { st->inspectorTab = 0; st->inspectorVisible = true; LayoutMainChildren(st); InvalidateViews(st); }
                return 0;
            }
        }
        if (pt.y < kNavAreaH) return 0;
        if (st->doc.Mode() == DocumentMode::PaginatedPdf) {
            const int y = (pt.y - kNavAreaH) + st->sidebarScrollPos;
            const std::size_t idx = static_cast<std::size_t>((std::max)(0, y) / kItemH);
            if (idx < st->doc.Pages().size()) {
                const int localY = y - static_cast<int>(idx) * kItemH;
                st->activePageIndex = idx;
                if (pt.x >= kSidebarWidth - 62 && localY >= kItemH - 28 && localY < kItemH - 4) {
                    ExecuteToolbarCommand(st, (pt.x < kSidebarWidth - 36) ? CMD_PAGE_UP : CMD_PAGE_DOWN);
                    return 0;
                }
                CenterCameraOnPage(st, idx);
                InvalidateViews(st);
            }
        } else {
            // Canvas mode quick actions list below nav
            const int relY = pt.y - kNavAreaH - 28;
            if (relY >= 0) {
                const int row = relY / 36;
                if (row == 0) ExecuteToolbarCommand(st, CMD_PAPER_NEXT);
                else if (row == 1) ExecuteToolbarCommand(st, CMD_TOGGLE_RULER);
                else if (row == 2) ExecuteToolbarCommand(st, CMD_TOGGLE_MINIMAP);
                else if (row == 3) ExecuteToolbarCommand(st, CMD_AUDIO_REC);
                else if (row == 4) ExecuteToolbarCommand(st, CMD_FOCUS_MODE);
            }
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (st) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const int w = (std::max)(1, static_cast<int>(rc.right - rc.left));
            const int h = (std::max)(1, static_cast<int>(rc.bottom - rc.top));
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ old = SelectObject(mem, bmp);

            const UiGlass::GlassPalette pal = UiGlass::GetPalette(st->glassSettings);
            Gdiplus::Graphics g(mem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

            // Edge-to-edge macOS 27 Golden Gate sidebar fill + subtle BackgroundExtensionEffect edge glow
            Gdiplus::LinearGradientBrush bgGrad(
                Gdiplus::PointF(0.0f, 0.0f), Gdiplus::PointF(static_cast<float>(w), 0.0f),
                UiGlass::ToGdip(pal.sidebarBg), UiGlass::ToGdip(pal.windowCanvasTop));
            g.FillRectangle(&bgGrad, 0, 0, w, h);

            SetBkMode(mem, TRANSPARENT);
            SelectObject(mem, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
            SetTextColor(mem, pal.labelTertiary.ToColorRef());
            RECT secLbl{ 16, 10, w - 12, 26 };
            DrawTextW(mem, L"ESPACIO DE TRABAJO", -1, &secLbl, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

            struct NavEntry {
                const wchar_t* title;
                UiGlass::IconId icon;
                UiGlass::Rgba8 accent;
                bool active;
            };
            const NavEntry navs[5] = {
                { L"P\u00E1ginas PDF",          UiGlass::IconId::PdfPages,       pal.accentBlue,   st->doc.Mode() == DocumentMode::PaginatedPdf },
                { L"Lienzo Infinito",           UiGlass::IconId::InfiniteCanvas, pal.accentIndigo, st->doc.Mode() == DocumentMode::InfiniteCanvas },
                { L"Ingenier\u00EDa STEM",      UiGlass::IconId::StemAtom,       pal.accentGreen,  st->inspectorVisible && st->inspectorTab == 1 },
                { L"Estudio SM-2",              UiGlass::IconId::StudySparkles,  pal.accentPurple, false },
                { L"TEC Digital y Horarios",    UiGlass::IconId::AcademicCap,    pal.accentOrange, st->inspectorVisible && st->inspectorTab == 0 }
            };
            st->sidebarNavRects.clear();
            SelectObject(mem, st->hUiFont);
            for (int i = 0; i < 5; ++i) {
                RECT nr{ 10, 30 + i * 31, w - 10, 30 + i * 31 + 28 };
                st->sidebarNavRects.push_back(nr);
                if (navs[i].active) {
                    Gdiplus::GraphicsPath np;
                    UiGlass::AddRoundPath(np, static_cast<float>(nr.left), static_cast<float>(nr.top),
                        static_cast<float>(nr.right - nr.left), static_cast<float>(nr.bottom - nr.top), 8.0f);
                    Gdiplus::SolidBrush nb(UiGlass::ToGdip(pal.sidebarSelected));
                    g.FillPath(&nb, &np);
                }
                // Colored SF-Symbol-style icon (macOS 27 Golden Gate signature)
                UiGlass::DrawVectorIconGdiPlus(g, navs[i].icon,
                    static_cast<float>(nr.left + 15), static_cast<float>(nr.top + 14),
                    15.0f, UiGlass::ToGdip(navs[i].accent), 1.55f);
                SelectObject(mem, navs[i].active && st->hUiBoldFont ? st->hUiBoldFont : st->hUiFont);
                SetTextColor(mem, navs[i].active ? pal.labelPrimary.ToColorRef() : pal.labelSecondary.ToColorRef());
                RECT tr{ nr.left + 30, nr.top, nr.right - 8, nr.bottom };
                DrawTextW(mem, navs[i].title, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }

            // Hairline divider between Navigation and Contextual Pages/Tools
            Gdiplus::Pen sepPen(UiGlass::ToGdip(pal.separator), 1.0f);
            g.DrawLine(&sepPen, 14.0f, static_cast<float>(kNavAreaH - 4), static_cast<float>(w - 14), static_cast<float>(kNavAreaH - 4));

            if (st->doc.Mode() == DocumentMode::PaginatedPdf) {
                SelectObject(mem, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
                SetTextColor(mem, pal.labelTertiary.ToColorRef());
                std::wstring pgHead = L"P\u00C1GINAS (" + std::to_wstring(st->doc.Pages().size()) + L")";
                RECT phRc{ 16, kNavAreaH + 2, w - 12, kNavAreaH + 20 };
                DrawTextW(mem, pgHead.c_str(), -1, &phRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                // Clip page list below header
                HRGN clipRgn = CreateRectRgn(0, kNavAreaH + 22, w - 1, h);
                SelectClipRgn(mem, clipRgn);
                g.SetClip(Gdiplus::Rect(0, kNavAreaH + 22, w - 1, (std::max)(1, h - kNavAreaH - 22)));

                const std::size_t count = st->doc.Pages().size();
                const std::size_t first = static_cast<std::size_t>((std::max)(0, st->sidebarScrollPos / kItemH));
                const std::size_t last = (std::min)(count, first + static_cast<std::size_t>((h - kNavAreaH) / kItemH + 3));
                for (std::size_t i = first; i < last; ++i) {
                    const int top = kNavAreaH + 24 + static_cast<int>(i) * kItemH - st->sidebarScrollPos;
                    RECT card{ 12, top, w - 12, top + kItemH - 10 };
                    const bool active = (i == st->activePageIndex);
                    Gdiplus::GraphicsPath cp;
                    UiGlass::AddRoundPath(cp, static_cast<float>(card.left), static_cast<float>(card.top),
                        static_cast<float>(card.right - card.left), static_cast<float>(card.bottom - card.top), 11.0f);
                    Gdiplus::SolidBrush cb(UiGlass::ToGdip(active ? pal.sidebarSelected : pal.contentCard));
                    g.FillPath(&cb, &cp);
                    Gdiplus::Pen cpen(UiGlass::ToGdip(active ? pal.accentBlue : pal.borderSubtle), active ? 1.6f : 1.0f);
                    g.DrawPath(&cpen, &cp);

                    // Concentric inner thumbnail (R_inner = max(4, 11 - 5) = 6)
                    const auto& pg = st->doc.Pages()[i];
                    RECT thumb{ card.left + 8, card.top + 8, card.left + 64, card.bottom - 8 };
                    Gdiplus::SolidBrush tBg(Gdiplus::Color(255, 255, 255, 255));
                    g.FillRectangle(&tBg, thumb.left, thumb.top, thumb.right - thumb.left, thumb.bottom - thumb.top);
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
                    Gdiplus::Pen tPen(UiGlass::ToGdip(pal.borderSubtle), 1.0f);
                    g.DrawRectangle(&tPen, thumb.left, thumb.top, thumb.right - thumb.left, thumb.bottom - thumb.top);

                    SelectObject(mem, st->hUiBoldFont ? st->hUiBoldFont : st->hUiFont);
                    SetTextColor(mem, pal.labelPrimary.ToColorRef());
                    const std::wstring lbl = L"P\u00E1g. " + std::to_wstring(i + 1);
                    RECT txtRc{ card.left + 72, card.top + 10, card.right - 8, card.top + 32 };
                    DrawTextW(mem, lbl.c_str(), -1, &txtRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                    SelectObject(mem, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
                    SetTextColor(mem, pal.labelSecondary.ToColorRef());
                    const std::wstring dimLbl = std::to_wstring(static_cast<int>(pg.EffectiveWidthPt())) + L" \u00D7 " +
                        std::to_wstring(static_cast<int>(pg.EffectiveHeightPt())) + L" pt";
                    RECT dimRc{ card.left + 72, card.top + 32, card.right - 8, card.top + 52 };
                    DrawTextW(mem, dimLbl.c_str(), -1, &dimRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                    SetTextColor(mem, pal.labelTertiary.ToColorRef());
                    RECT arrowRc{ card.right - 50, card.bottom - 24, card.right - 8, card.bottom - 6 };
                    DrawTextW(mem, L"\u25B2  \u25BC", -1, &arrowRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
                SelectClipRgn(mem, nullptr);
                DeleteObject(clipRgn);
                g.ResetClip();
            } else {
                // Infinite Canvas Quick Controls (clean HIG list, standard material cards)
                SelectObject(mem, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
                SetTextColor(mem, pal.labelTertiary.ToColorRef());
                RECT chRc{ 16, kNavAreaH + 4, w - 12, kNavAreaH + 22 };
                DrawTextW(mem, L"HERRAMIENTAS DE LIENZO", -1, &chRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                struct QuickAction { const wchar_t* label; UiGlass::IconId icon; UiGlass::Rgba8 col; bool on; };
                const QuickAction qa[5] = {
                    { L"Patr\u00F3n de papel",     UiGlass::IconId::LayersStack,  pal.accentBlue,   false },
                    { L"Regla graduada",           UiGlass::IconId::CropTool,     pal.accentTeal,   st->rulerState.visible },
                    { L"Minimapa espacial",        UiGlass::IconId::CompareGrid,  pal.accentIndigo, st->minimapVisible },
                    { st->audioSession.IsRecording() ? L"Detener audio (Mic)" : L"Grabar nota de audio",
                                                   UiGlass::IconId::MicAudio,     pal.accentRed,    st->audioSession.IsRecording() },
                    { L"Modo Concentraci\u00F3n",  UiGlass::IconId::ContrastCircle, pal.accentPurple, st->focusMode }
                };
                SelectObject(mem, st->hUiFont);
                for (int i = 0; i < 5; ++i) {
                    RECT ar{ 12, kNavAreaH + 28 + i * 36, w - 12, kNavAreaH + 28 + i * 36 + 30 };
                    Gdiplus::GraphicsPath ap;
                    UiGlass::AddRoundPath(ap, static_cast<float>(ar.left), static_cast<float>(ar.top),
                        static_cast<float>(ar.right - ar.left), static_cast<float>(ar.bottom - ar.top), 8.0f);
                    Gdiplus::SolidBrush ab(UiGlass::ToGdip(qa[i].on ? pal.sidebarSelected : pal.contentCard));
                    g.FillPath(&ab, &ap);
                    Gdiplus::Pen apen(UiGlass::ToGdip(qa[i].on ? pal.accentBlue : pal.borderSubtle), 1.0f);
                    g.DrawPath(&apen, &ap);
                    UiGlass::DrawVectorIconGdiPlus(g, qa[i].icon, static_cast<float>(ar.left + 16),
                        static_cast<float>(ar.top + 15), 14.0f, UiGlass::ToGdip(qa[i].col), 1.5f);
                    SetTextColor(mem, pal.labelPrimary.ToColorRef());
                    RECT tr{ ar.left + 32, ar.top, ar.right - 8, ar.bottom };
                    DrawTextW(mem, qa[i].label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                }
            }

            // Crisp right border hairline
            g.DrawLine(&sepPen, static_cast<float>(w - 1), 0.0f, static_cast<float>(w - 1), static_cast<float>(h));

            BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
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

// ---------------- Right Collapsible Inspector (Academic Hub / STEM / Layers) ----------------

LRESULT CALLBACK InspectorWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    StudioAppState* st = reinterpret_cast<StudioAppState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        st = reinterpret_cast<StudioAppState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN: {
        if (!st) break;
        const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        for (const auto& hit : st->inspectorHits) {
            if (PtInRect(&hit.rc, pt)) {
                if (hit.tabIdx >= 0) {
                    st->inspectorTab = hit.tabIdx;
                    InvalidateViews(st);
                } else if (hit.cmdId != 0) {
                    ExecuteToolbarCommand(st, hit.cmdId);
                }
                return 0;
            }
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (st) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            const int w = (std::max)(1, static_cast<int>(rc.right - rc.left));
            const int h = (std::max)(1, static_cast<int>(rc.bottom - rc.top));
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ old = SelectObject(mem, bmp);

            const UiGlass::GlassPalette pal = UiGlass::GetPalette(st->glassSettings);
            Gdiplus::Graphics g(mem);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            st->inspectorHits.clear();

            Gdiplus::SolidBrush bgBr(UiGlass::ToGdip(pal.inspectorBg));
            g.FillRectangle(&bgBr, 0, 0, w, h);
            Gdiplus::Pen sepPen(UiGlass::ToGdip(pal.separator), 1.0f);
            g.DrawLine(&sepPen, 0.0f, 0.0f, 0.0f, static_cast<float>(h));

            SetBkMode(mem, TRANSPARENT);

            // Top 3-Segment HIG Tab Control: Académico | STEM | Capas
            RECT segOuter{ 12, 12, w - 12, 42 };
            Gdiplus::GraphicsPath segP;
            UiGlass::AddRoundPath(segP, static_cast<float>(segOuter.left), static_cast<float>(segOuter.top),
                static_cast<float>(segOuter.right - segOuter.left), static_cast<float>(segOuter.bottom - segOuter.top), 9.0f);
            Gdiplus::SolidBrush segTr(st->glassSettings.IsDark() ? Gdiplus::Color(255, 34, 39, 50) : Gdiplus::Color(255, 229, 234, 243));
            g.FillPath(&segTr, &segP);

            const wchar_t* tabLabels[3] = { L"Acad\u00E9mico", L"STEM", L"Capas" };
            const int tw = (segOuter.right - segOuter.left - 6) / 3;
            for (int i = 0; i < 3; ++i) {
                RECT tr{ segOuter.left + 3 + i * tw, segOuter.top + 3, segOuter.left + 3 + (i + 1) * tw, segOuter.bottom - 3 };
                st->inspectorHits.push_back({ tr, 0, i });
                const bool sel = (st->inspectorTab == i);
                if (sel) {
                    Gdiplus::GraphicsPath tp;
                    UiGlass::AddRoundPath(tp, static_cast<float>(tr.left), static_cast<float>(tr.top),
                        static_cast<float>(tr.right - tr.left), static_cast<float>(tr.bottom - tr.top), 6.5f);
                    Gdiplus::SolidBrush tb(UiGlass::ToGdip(pal.contentCard));
                    g.FillPath(&tb, &tp);
                }
                SelectObject(mem, sel && st->hUiBoldFont ? st->hUiBoldFont : (st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont));
                SetTextColor(mem, sel ? pal.accentBlue.ToColorRef() : pal.labelSecondary.ToColorRef());
                DrawTextW(mem, tabLabels[i], -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            auto drawCard = [&](int y0, int cardH) -> RECT {
                RECT cr{ 12, y0, w - 12, y0 + cardH };
                Gdiplus::GraphicsPath cp;
                UiGlass::AddRoundPath(cp, static_cast<float>(cr.left), static_cast<float>(cr.top),
                    static_cast<float>(cr.right - cr.left), static_cast<float>(cr.bottom - cr.top), 12.0f);
                Gdiplus::SolidBrush cb(UiGlass::ToGdip(pal.contentCard));
                g.FillPath(&cb, &cp);
                Gdiplus::Pen cpen(UiGlass::ToGdip(pal.borderSubtle), 1.0f);
                g.DrawPath(&cpen, &cp);
                return cr;
            };
            auto drawActionBtn = [&](const RECT& br, const wchar_t* lbl, WORD cmd, bool primary) {
                st->inspectorHits.push_back({ br, cmd, -1 });
                Gdiplus::GraphicsPath bp;
                UiGlass::AddRoundPath(bp, static_cast<float>(br.left), static_cast<float>(br.top),
                    static_cast<float>(br.right - br.left), static_cast<float>(br.bottom - br.top), 8.0f);
                Gdiplus::SolidBrush bb(primary ? UiGlass::ToGdip(pal.accentBlue) : UiGlass::ToGdip(pal.contentCardElevated));
                g.FillPath(&bb, &bp);
                if (!primary) {
                    Gdiplus::Pen bpen(UiGlass::ToGdip(pal.borderSubtle), 1.0f);
                    g.DrawPath(&bpen, &bp);
                }
                SelectObject(mem, primary && st->hUiBoldFont ? st->hUiBoldFont : (st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont));
                SetTextColor(mem, primary ? RGB(255, 255, 255) : pal.labelPrimary.ToColorRef());
                RECT tx = br;
                DrawTextW(mem, lbl, -1, &tx, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            };

            if (st->inspectorTab == 0) {
                // Tab 0: Academic Hub (TEC Digital + HorariosTEC)
                RECT c1 = drawCard(56, 198);
                UiGlass::DrawVectorIconGdiPlus(g, UiGlass::IconId::AcademicCap,
                    static_cast<float>(c1.left + 20), static_cast<float>(c1.top + 22), 16.0f, UiGlass::ToGdip(pal.accentOrange), 1.6f);
                SelectObject(mem, st->hUiBoldFont ? st->hUiBoldFont : st->hUiFont);
                SetTextColor(mem, pal.labelPrimary.ToColorRef());
                RECT h1{ c1.left + 36, c1.top + 11, c1.right - 12, c1.top + 33 };
                DrawTextW(mem, L"TEC Digital \u00B7 CuadernU", -1, &h1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                // Read-Only shield badge
                RECT bdg{ c1.left + 12, c1.top + 38, c1.right - 12, c1.top + 58 };
                Gdiplus::GraphicsPath bp;
                UiGlass::AddRoundPath(bp, static_cast<float>(bdg.left), static_cast<float>(bdg.top),
                    static_cast<float>(bdg.right - bdg.left), static_cast<float>(bdg.bottom - bdg.top), 9.0f);
                Gdiplus::SolidBrush bbr(UiGlass::ToGdip(pal.accentGreen, st->glassSettings.IsDark() ? 60 : 38));
                g.FillPath(&bbr, &bp);
                SelectObject(mem, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
                SetTextColor(mem, pal.accentGreen.ToColorRef());
                DrawTextW(mem, L"\u25CF Solo Lectura \u00B7 0 mutaciones", -1, &bdg, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

                SetTextColor(mem, pal.labelSecondary.ToColorRef());
                RECT st1{ c1.left + 12, c1.top + 64, c1.right - 12, c1.top + 108 };
                const std::wstring acW = Utf8ToWide(st->academicStatusUtf8);
                DrawTextW(mem, acW.c_str(), -1, &st1, DT_LEFT | DT_WORDBREAK);

                drawActionBtn({ c1.left + 12, c1.top + 116, c1.right - 12, c1.top + 146 },
                    L"Verificar Sesi\u00F3n y Pol\u00EDtica TEC", CMD_ACADEMIC_SYNC_TEC, true);
                drawActionBtn({ c1.left + 12, c1.top + 154, c1.right - 12, c1.top + 184 },
                    L"Buscar Materiales Offline (FTS)", CMD_ACADEMIC_SEARCH_MATERIALS, false);

                RECT c2 = drawCard(268, 188);
                UiGlass::DrawVectorIconGdiPlus(g, UiGlass::IconId::CalendarGrid,
                    static_cast<float>(c2.left + 20), static_cast<float>(c2.top + 22), 16.0f, UiGlass::ToGdip(pal.accentTeal), 1.6f);
                SelectObject(mem, st->hUiBoldFont ? st->hUiBoldFont : st->hUiFont);
                SetTextColor(mem, pal.labelPrimary.ToColorRef());
                RECT h2{ c2.left + 36, c2.top + 11, c2.right - 12, c2.top + 33 };
                DrawTextW(mem, L"HorariosTEC \u00B7 Optimizador", -1, &h2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                SelectObject(mem, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
                SetTextColor(mem, pal.labelSecondary.ToColorRef());
                RECT st2{ c2.left + 12, c2.top + 42, c2.right - 12, c2.top + 96 };
                const std::wstring schW = Utf8ToWide(st->scheduleSummaryUtf8);
                DrawTextW(mem, schW.c_str(), -1, &st2, DT_LEFT | DT_WORDBREAK);

                drawActionBtn({ c2.left + 12, c2.top + 104, c2.right - 12, c2.top + 134 },
                    L"Generar Horario Sin Choques", CMD_ACADEMIC_SOLVE_SCHEDULE, true);
                drawActionBtn({ c2.left + 12, c2.top + 142, c2.right - 12, c2.top + 172 },
                    L"Exportar Reporte STEM HTML", CMD_EXPORT_LAB_HTML, false);
            } else if (st->inspectorTab == 1) {
                // Tab 1: STEM & CAS + Graphical Casio Keycaps Bridge
                RECT c1 = drawCard(56, 210);
                UiGlass::DrawVectorIconGdiPlus(g, UiGlass::IconId::StemAtom,
                    static_cast<float>(c1.left + 20), static_cast<float>(c1.top + 22), 16.0f, UiGlass::ToGdip(pal.accentGreen), 1.6f);
                SelectObject(mem, st->hUiBoldFont ? st->hUiBoldFont : st->hUiFont);
                SetTextColor(mem, pal.labelPrimary.ToColorRef());
                RECT h1{ c1.left + 36, c1.top + 11, c1.right - 12, c1.top + 33 };
                DrawTextW(mem, L"Motor STEM 2D y CAS", -1, &h1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                drawActionBtn({ c1.left + 12, c1.top + 42, c1.right - 12, c1.top + 70 },  L"+ F\u00F3rmula Natural 2D / CAS", CMD_ADD_FORMULA, true);
                drawActionBtn({ c1.left + 12, c1.top + 76, c1.right - 12, c1.top + 104 }, L"+ Circuito SPICE / MNA",        CMD_ADD_CIRCUIT, false);
                drawActionBtn({ c1.left + 12, c1.top + 110, c1.right - 12, c1.top + 138 },L"+ Gr\u00E1fica 2D / Bode",      CMD_ADD_BODE_PLOT, false);
                drawActionBtn({ c1.left + 12, c1.top + 144, c1.right - 12, c1.top + 172 },L"Inspector de Variables SI",     CMD_VAR_INSPECTOR, false);
                drawActionBtn({ c1.left + 12, c1.top + 176, c1.right - 12, c1.top + 202 },L"Puente Teclas CASIO",           CMD_CASIO_REVERSE_KEYS, false);

                RECT c2 = drawCard(280, 146);
                SelectObject(mem, st->hUiBoldFont ? st->hUiBoldFont : st->hUiFont);
                SetTextColor(mem, pal.labelPrimary.ToColorRef());
                RECT h2{ c2.left + 14, c2.top + 10, c2.right - 12, c2.top + 30 };
                DrawTextW(mem, L"Teclas CASIO Gr\u00E1ficas", -1, &h2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                const wchar_t* sampleKeys[4] = { L"SHIFT", L"ALPHA", L"OPTN", L"EXE" };
                const int kw = (c2.right - c2.left - 36) / 4;
                for (int k = 0; k < 4; ++k) {
                    RECT kr{ c2.left + 12 + k * (kw + 4), c2.top + 40, c2.left + 12 + k * (kw + 4) + kw, c2.top + 66 };
                    UiGlass::DrawCasioKeycapPillGdi(mem, kr, sampleKeys[k], st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont, st->glassSettings.IsDark());
                }
                SelectObject(mem, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
                SetTextColor(mem, pal.labelSecondary.ToColorRef());
                RECT noteRc{ c2.left + 12, c2.top + 76, c2.right - 12, c2.top + 134 };
                DrawTextW(mem, L"Notaci\u00F3n matem\u00E1tica 2D nativa, letras griegas y teclas gr\u00E1ficas sincronizadas con Calculadora Unificada.", -1, &noteRc, DT_LEFT | DT_WORDBREAK);
            } else {
                // Tab 2: Layers & Audio Timeline
                RECT c1 = drawCard(56, 178);
                UiGlass::DrawVectorIconGdiPlus(g, UiGlass::IconId::LayersStack,
                    static_cast<float>(c1.left + 20), static_cast<float>(c1.top + 22), 16.0f, UiGlass::ToGdip(pal.accentIndigo), 1.6f);
                SelectObject(mem, st->hUiBoldFont ? st->hUiBoldFont : st->hUiFont);
                SetTextColor(mem, pal.labelPrimary.ToColorRef());
                RECT h1{ c1.left + 36, c1.top + 11, c1.right - 12, c1.top + 33 };
                DrawTextW(mem, L"Capas del Documento", -1, &h1, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(mem, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
                SetTextColor(mem, pal.labelSecondary.ToColorRef());
                std::wstring lInfo = L"Capas activas: " + std::to_wstring(st->doc.Layers().size()) +
                    L"\nCapa actual ID: " + std::to_wstring(st->doc.ActiveLayerId());
                RECT lr{ c1.left + 14, c1.top + 40, c1.right - 14, c1.top + 86 };
                DrawTextW(mem, lInfo.c_str(), -1, &lr, DT_LEFT | DT_WORDBREAK);
                drawActionBtn({ c1.left + 12, c1.top + 96, c1.right - 12, c1.top + 126 }, L"+ Nueva Capa", CMD_LAYER_ADD, true);
                drawActionBtn({ c1.left + 12, c1.top + 134, c1.right - 12, c1.top + 164 }, L"+ Capa Privada Docente", CMD_TEACHER_PRIVATE_LAYER, false);

                RECT c2 = drawCard(248, 166);
                UiGlass::DrawVectorIconGdiPlus(g, UiGlass::IconId::MicAudio,
                    static_cast<float>(c2.left + 20), static_cast<float>(c2.top + 22), 16.0f, UiGlass::ToGdip(pal.accentRed), 1.6f);
                SelectObject(mem, st->hUiBoldFont ? st->hUiBoldFont : st->hUiFont);
                SetTextColor(mem, pal.labelPrimary.ToColorRef());
                RECT h2{ c2.left + 36, c2.top + 11, c2.right - 12, c2.top + 33 };
                DrawTextW(mem, L"Audio Sincronizado", -1, &h2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                SelectObject(mem, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
                SetTextColor(mem, pal.labelSecondary.ToColorRef());
                RECT ar{ c2.left + 14, c2.top + 40, c2.right - 14, c2.top + 82 };
                DrawTextW(mem, st->audioSession.IsRecording() ? L"Grabando desde micr\u00F3fono (Win32 waveIn)..." : L"Listo para grabar trazos + voz offline.", -1, &ar, DT_LEFT | DT_WORDBREAK);
                drawActionBtn({ c2.left + 12, c2.top + 88, c2.right - 12, c2.top + 118 },
                    st->audioSession.IsRecording() ? L"Detener Grabaci\u00F3n" : L"Iniciar Grabaci\u00F3n (Mic)", CMD_AUDIO_REC, true);
                drawActionBtn({ c2.left + 12, c2.top + 124, c2.right - 12, c2.top + 154 }, L"Reproducir Ink Replay", CMD_REPLAY_TOGGLE, false);
            }

            BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
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

// ---------------- Main Studio Liquid Glass 2.0 Chrome Renderer ----------------

void PaintRibbonAndStatus(StudioAppState* st, HDC hdc) {
    RECT rc{};
    GetClientRect(st->hwndMain, &rc);
    const int w = (std::max)(1, static_cast<int>(rc.right - rc.left));
    const int h = (std::max)(1, static_cast<int>(rc.bottom - rc.top));
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ old = SelectObject(mem, bmp);

    const UiGlass::GlassPalette pal = UiGlass::GetPalette(st->glassSettings);
    const int topTotalH = kTopChromeH + (st->searchVisible ? 38 : 0);

    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        // Airy Apple HIG atmospheric backdrop behind floating Liquid Glass 2.0 toolbar capsules
        Gdiplus::LinearGradientBrush topBg(
            Gdiplus::PointF(0.0f, 0.0f), Gdiplus::PointF(0.0f, static_cast<float>(topTotalH)),
            UiGlass::ToGdip(pal.windowCanvasTop), UiGlass::ToGdip(pal.windowCanvasBottom));
        g.FillRectangle(&topBg, 0, 0, w, topTotalH);

        // Subtle atmospheric tonal wave behind toolbar so optical Snell bevel refraction is visibly rich
        if (!st->glassSettings.IsHighContrast()) {
            Gdiplus::LinearGradientBrush accentWave(
                Gdiplus::PointF(0.0f, 0.0f), Gdiplus::PointF(static_cast<float>(w), static_cast<float>(kTopChromeH)),
                UiGlass::ToGdip(pal.accentBlue, st->glassSettings.IsDark() ? 28 : 22),
                UiGlass::ToGdip(pal.accentIndigo, st->glassSettings.IsDark() ? 22 : 16));
            g.FillRectangle(&accentWave, 0, 0, w, kTopChromeH);
        }

        // Apple HIG ScrollEdgeEffectStyle.soft transition at bottom of top chrome
        UiGlass::DrawScrollEdgeSoftFade(g, RECT{ 0, topTotalH - 12, w, topTotalH }, st->glassSettings, false);
        Gdiplus::Pen sepPen(UiGlass::ToGdip(pal.separator), 1.0f);
        g.DrawLine(&sepPen, 0.0f, static_cast<float>(topTotalH - 1), static_cast<float>(w), static_cast<float>(topTotalH - 1));
    }

    // Render every floating Liquid Glass 2.0 toolbar capsule with real optical refraction,
    // Dual Kawase blur, Schlick Fresnel, opposing @1/@2 specular rims & UserOpacitySlider!
    for (const auto& cap : st->toolbarCapsules) {
        if (!cap.visible) continue;
        if (cap.prominent) {
            UiGlass::DrawLiquidGlassCapsuleOnHdc(mem, cap.rc, st->glassSettings, -1.0f, pal.accentBlue, 0.66f);
        } else {
            UiGlass::DrawLiquidGlassCapsuleOnHdc(mem, cap.rc, st->glassSettings, -1.0f);
        }
    }

    // Paint crisp 100% vector SF-Symbol-style icons, Aqua 2.0 vitreous ink orbs, and labels inside capsules
    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        SetBkMode(mem, TRANSPARENT);

        for (std::size_t i = 0; i < st->buttons.size() && i < st->buttonRects.size(); ++i) {
            const auto& b = st->buttons[i];
            if (!b.visible || b.zone == ToolbarZone::StatusZoom) continue;
            const RECT& br = st->buttonRects[i];
            if (br.right <= br.left || br.bottom <= br.top) continue;
            const bool checked = IsToolbarButtonChecked(st, b.id);
            const bool hovered = (static_cast<int>(i) == st->hoveredButton);

            if (b.isSwatch) {
                const float cx = static_cast<float>(br.left + br.right) * 0.5f;
                const float cy = static_cast<float>(br.top + br.bottom) * 0.5f;
                UiGlass::DrawAquaVitreousOrb(g, cx, cy, 7.2f,
                    Gdiplus::Color(255, GetRValue(b.swatchColor), GetGValue(b.swatchColor), GetBValue(b.swatchColor)),
                    checked, st->glassSettings.IsDark());
                continue;
            }

            // Concentric inner selection/hover pill inside outer glass capsule (R_inner = 19 - 4 = 15)
            if (!b.accent && (checked || hovered)) {
                Gdiplus::GraphicsPath ip;
                UiGlass::AddRoundPath(ip, static_cast<float>(br.left), static_cast<float>(br.top),
                    static_cast<float>(br.right - br.left), static_cast<float>(br.bottom - br.top), 14.0f);
                Gdiplus::SolidBrush ib(UiGlass::ToGdip(checked ? pal.glassPillSelected : pal.glassPillHover));
                g.FillPath(&ib, &ip);
                if (checked) {
                    Gdiplus::Pen ipen(UiGlass::ToGdip(pal.accentBlue, st->glassSettings.IsHighContrast() ? 240 : 120), 1.0f);
                    g.DrawPath(&ipen, &ip);
                }
            }

            const UiGlass::Rgba8 fgRgba = b.accent
                ? UiGlass::Rgba8{ 255, 255, 255, 255 }
                : (checked ? pal.accentBlue : (hovered ? pal.labelPrimary : pal.labelSecondary));

            const float cy = static_cast<float>(br.top + br.bottom) * 0.5f;
            if (b.hasIcon && !b.hasLabel) {
                const float cx = static_cast<float>(br.left + br.right) * 0.5f;
                UiGlass::DrawVectorIconGdiPlus(g, b.icon, cx, cy, 15.0f, UiGlass::ToGdip(fgRgba), 1.6f);
            } else if (b.hasIcon && b.hasLabel) {
                UiGlass::DrawVectorIconGdiPlus(g, b.icon, static_cast<float>(br.left + 14), cy, 14.0f, UiGlass::ToGdip(fgRgba), 1.55f);
                SelectObject(mem, (b.accent || checked) && st->hUiBoldFont ? st->hUiBoldFont : (st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont));
                SetTextColor(mem, fgRgba.ToColorRef());
                RECT tr{ br.left + 24, br.top, br.right - 6, br.bottom };
                DrawTextW(mem, b.label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            } else if (b.hasLabel) {
                SelectObject(mem, (b.accent || checked) && st->hUiBoldFont ? st->hUiBoldFont : st->hUiFont);
                SetTextColor(mem, fgRgba.ToColorRef());
                RECT tr = br;
                DrawTextW(mem, b.label, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
        }

        // Optional Search Action Buttons + HIG rounded input chrome when search bar is visible
        if (st->searchVisible && st->hwndSearchEdit) {
            RECT seRc{};
            GetWindowRect(st->hwndSearchEdit, &seRc);
            MapWindowPoints(HWND_DESKTOP, st->hwndMain, reinterpret_cast<LPPOINT>(&seRc), 2);
            RECT frameRc{ seRc.left - 6, seRc.top - 3, seRc.right + 6, seRc.bottom + 3 };
            {
                Gdiplus::GraphicsPath sp;
                UiGlass::AddRoundPath(sp, static_cast<float>(frameRc.left), static_cast<float>(frameRc.top),
                    static_cast<float>(frameRc.right - frameRc.left), static_cast<float>(frameRc.bottom - frameRc.top), 7.0f);
                Gdiplus::SolidBrush sbr(UiGlass::ToGdip(pal.contentCard));
                g.FillPath(&sbr, &sp);
                Gdiplus::Pen spn(UiGlass::ToGdip(pal.borderStrong), 1.0f);
                g.DrawPath(&spn, &sp);
            }
            RECT bFind{ seRc.right + 10, kTopChromeH + 7, seRc.right + 52, kTopChromeH + 31 };
            RECT bRepl{ seRc.right + 58, kTopChromeH + 7, seRc.right + 108, kTopChromeH + 31 };
            auto drawMini = [&](const RECT& r, const wchar_t* t) {
                Gdiplus::GraphicsPath p;
                UiGlass::AddRoundPath(p, static_cast<float>(r.left), static_cast<float>(r.top),
                    static_cast<float>(r.right - r.left), static_cast<float>(r.bottom - r.top), 7.0f);
                Gdiplus::SolidBrush br(UiGlass::ToGdip(pal.contentCard));
                g.FillPath(&br, &p);
                Gdiplus::Pen pn(UiGlass::ToGdip(pal.borderSubtle), 1.0f);
                g.DrawPath(&pn, &p);
                SelectObject(mem, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
                SetTextColor(mem, pal.labelPrimary.ToColorRef());
                RECT tx = r;
                DrawTextW(mem, t, -1, &tx, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            };
            drawMini(bFind, L"Ir");
            drawMini(bRepl, L"Reemp.");
        }

        // Bottom Minimal Status Strip
        RECT stBar{ 0, h - kBottomStatusH, w, h };
        Gdiplus::SolidBrush stBr(UiGlass::ToGdip(pal.sidebarBg));
        g.FillRectangle(&stBr, stBar.left, stBar.top, w, kBottomStatusH);
        Gdiplus::Pen sepPen(UiGlass::ToGdip(pal.separator), 1.0f);
        g.DrawLine(&sepPen, 0.0f, static_cast<float>(h - kBottomStatusH), static_cast<float>(w), static_cast<float>(h - kBottomStatusH));

        std::wostringstream status;
        status << (st->doc.Mode() == DocumentMode::InfiniteCanvas ? L"Lienzo Infinito" : L"PDF Paginado");
        if (st->doc.Mode() == DocumentMode::InfiniteCanvas) {
            status << L"  \u00B7  " << st->doc.CanvasQuadtreeSize() << L" objetos";
        } else {
            status << L"  \u00B7  P\u00E1g. " << (st->activePageIndex + 1) << L"/" << (std::max<std::size_t>)(1u, st->doc.Pages().size());
        }
        status << L"  \u00B7  " << Utf8ToWide(st->statusBannerUtf8);

        SelectObject(mem, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
        SetTextColor(mem, pal.labelSecondary.ToColorRef());
        RECT stTxt{ 14, h - kBottomStatusH + 2, w - 120, h - 2 };
        const std::wstring sStr = status.str();
        DrawTextW(mem, sStr.c_str(), static_cast<int>(sStr.size()), &stTxt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Bottom-right zoom stepper pill (- 100% +)
        const std::wstring zStr = std::to_wstring(static_cast<int>(std::lround(st->camera.zoom * 100.0))) + L"%";
        for (std::size_t i = 0; i < st->buttons.size() && i < st->buttonRects.size(); ++i) {
            const auto& b = st->buttons[i];
            if (b.zone != ToolbarZone::StatusZoom) continue;
            const RECT& br = st->buttonRects[i];
            const bool hov = (static_cast<int>(i) == st->hoveredButton);
            Gdiplus::GraphicsPath zp;
            UiGlass::AddRoundPath(zp, static_cast<float>(br.left), static_cast<float>(br.top),
                static_cast<float>(br.right - br.left), static_cast<float>(br.bottom - br.top), 7.0f);
            Gdiplus::SolidBrush zb(UiGlass::ToGdip(hov ? pal.glassPillHover : pal.contentCard));
            g.FillPath(&zb, &zp);
            Gdiplus::Pen zpen(UiGlass::ToGdip(pal.borderSubtle), 1.0f);
            g.DrawPath(&zpen, &zp);
            if (b.id == CMD_ZOOM_RESET) {
                SetTextColor(mem, pal.labelPrimary.ToColorRef());
                RECT tr = br;
                DrawTextW(mem, zStr.c_str(), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            } else {
                UiGlass::DrawVectorIconGdiPlus(g, b.icon,
                    static_cast<float>(br.left + br.right) * 0.5f,
                    static_cast<float>(br.top + br.bottom) * 0.5f,
                    11.0f, UiGlass::ToGdip(pal.labelPrimary), 1.5f);
            }
        }
    }

    BitBlt(hdc, 0, 0, w, topTotalH, mem, 0, 0, SRCCOPY);
    BitBlt(hdc, 0, h - kBottomStatusH, w, kBottomStatusH, mem, 0, h - kBottomStatusH, SRCCOPY);
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
        st->hUiFont      = UiGlass::CreateHigUiFont(13, FW_NORMAL);
        st->hUiBoldFont  = UiGlass::CreateHigUiFont(13, FW_SEMIBOLD);
        st->hUiSmallFont = UiGlass::CreateHigUiFont(12, FW_NORMAL);
        st->hUiTitleFont = UiGlass::CreateHigUiFont(15, FW_SEMIBOLD);
        UiGlass::ApplyGoldenGateWindowFrame(hwnd, st->glassSettings.IsDark());
        RebuildToolbarButtons(st);

        st->hwndSearchEdit = CreateWindowExW(0, L"EDIT", L"",
            WS_CHILD | ES_AUTOHSCROLL, 740, kTopChromeH + 10, 180, 18, hwnd,
            reinterpret_cast<HMENU>(CMD_SEARCH_EDIT), cs->hInstance, nullptr);
        SendMessageW(st->hwndSearchEdit, WM_SETFONT, reinterpret_cast<WPARAM>(st->hUiFont), TRUE);
        SendMessageW(st->hwndSearchEdit, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(L"Buscar texto... (Ctrl+F)"));

        st->hwndSidebar = CreateWindowW(kSidebarClassName, L"", WS_CHILD | WS_VISIBLE,
            0, kTopChromeH, kSidebarWidth, 500, hwnd, nullptr, cs->hInstance, st);
        st->hwndInspector = CreateWindowW(kInspectorClassName, L"", WS_CHILD,
            900, kTopChromeH, kInspectorWidth, 500, hwnd, nullptr, cs->hInstance, st);
        st->hwndViewport = CreateWindowW(kViewportClassName, L"", WS_CHILD | WS_VISIBLE,
            kSidebarWidth, kTopChromeH, 800, 500, hwnd, nullptr, cs->hInstance, st);

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
    case WM_DPICHANGED: {
        if (!st) break;
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        if (suggested) {
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        const UINT newDpi = HIWORD(wp);
        if (newDpi >= 96) {
            const int scaleDen = (newDpi <= 120) ? static_cast<int>(newDpi) : 120;
            auto recreateFont = [&](HFONT& f, int basePx, int wt) {
                if (f) DeleteObject(f);
                f = UiGlass::CreateHigUiFont((std::min)(basePx + 5, MulDiv(basePx, static_cast<int>(newDpi), scaleDen)), wt);
            };
            recreateFont(st->hUiFont, 13, FW_NORMAL);
            recreateFont(st->hUiBoldFont, 13, FW_SEMIBOLD);
            recreateFont(st->hUiSmallFont, 12, FW_NORMAL);
            recreateFont(st->hUiTitleFont, 15, FW_SEMIBOLD);
            if (st->hwndSearchEdit && st->hUiFont) {
                SendMessageW(st->hwndSearchEdit, WM_SETFONT, reinterpret_cast<WPARAM>(st->hUiFont), TRUE);
            }
        }
        LayoutMainChildren(st);
        InvalidateViews(st);
        return 0;
    }
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
        if (st->draggingOpacitySlider && st->appearancePopoverOpen) {
            UpdateAppearanceFromPoint(st, pt, true);
            return 0;
        }
        int hit = -1;
        for (std::size_t i = 0; i < st->buttonRects.size() && i < st->buttons.size(); ++i) {
            if (st->buttons[i].visible && PtInRect(&st->buttonRects[i], pt)) { hit = static_cast<int>(i); break; }
        }
        if (hit != st->hoveredButton) {
            st->hoveredButton = hit;
            InvalidateViews(st);
        }
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        if (st && reinterpret_cast<HWND>(lp) == st->hwndSearchEdit) {
            const UiGlass::GlassPalette pal = UiGlass::GetPalette(st->glassSettings);
            HDC hdcE = reinterpret_cast<HDC>(wp);
            SetTextColor(hdcE, pal.labelPrimary.ToColorRef());
            SetBkColor(hdcE, pal.contentCard.ToColorRef());
            if (st->hSearchBgBrush) DeleteObject(st->hSearchBgBrush);
            st->hSearchBgBrush = CreateSolidBrush(pal.contentCard.ToColorRef());
            return reinterpret_cast<LRESULT>(st->hSearchBgBrush);
        }
        break;
    }
    case WM_CAPTURECHANGED: {
        if (st) {
            st->draggingOpacitySlider = false;
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (st && st->draggingOpacitySlider) {
            st->draggingOpacitySlider = false;
            ReleaseCapture();
            return 0;
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        if (!st) break;
        const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (st->appearancePopoverOpen && PtInRect(&st->appearancePopoverRc, pt)) {
            if (pt.x >= st->opacityTrackRc.left - 8 && pt.x <= st->opacityTrackRc.right + 8 &&
                pt.y >= st->opacityTrackRc.top - 6 && pt.y <= st->opacityTrackRc.bottom + 6) {
                st->draggingOpacitySlider = true;
                SetCapture(hwnd);
            }
            UpdateAppearanceFromPoint(st, pt, false);
            return 0;
        }
        for (std::size_t i = 0; i < st->buttonRects.size() && i < st->buttons.size(); ++i) {
            if (st->buttons[i].visible && PtInRect(&st->buttonRects[i], pt)) {
                if (st->buttons[i].id != CMD_TOGGLE_APPEARANCE) st->appearancePopoverOpen = false;
                ExecuteToolbarCommand(st, st->buttons[i].id);
                return 0;
            }
        }
        if (st->appearancePopoverOpen) {
            st->appearancePopoverOpen = false;
            InvalidateViews(st);
        }
        if (st->searchVisible && st->hwndSearchEdit) {
            RECT seRc{};
            GetWindowRect(st->hwndSearchEdit, &seRc);
            MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<LPPOINT>(&seRc), 2);
            RECT bFind{ seRc.right + 10, kTopChromeH + 7, seRc.right + 52, kTopChromeH + 31 };
            RECT bRepl{ seRc.right + 58, kTopChromeH + 7, seRc.right + 108, kTopChromeH + 31 };
            if (PtInRect(&bFind, pt)) { ExecuteToolbarCommand(st, CMD_SEARCH_NEXT); return 0; }
            if (PtInRect(&bRepl, pt)) { ExecuteToolbarCommand(st, CMD_REPLACE_TEXT); return 0; }
        }
        return 0;
    }
    case WM_KEYDOWN: {
        if (!st) break;
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool inSearchEdit = (st->hwndSearchEdit && GetFocus() == st->hwndSearchEdit);
        if (wp == VK_ESCAPE && st->appearancePopoverOpen) {
            st->appearancePopoverOpen = false;
            InvalidateViews(st);
            return 0;
        }
        if (ctrl && wp == 'S') { ExecuteToolbarCommand(st, CMD_SAVE_CUDOC); return 0; }
        if (ctrl && wp == 'O') { ExecuteToolbarCommand(st, CMD_OPEN); return 0; }
        if (ctrl && !inSearchEdit && wp == 'Z') { ExecuteToolbarCommand(st, CMD_UNDO); return 0; }
        if (ctrl && !inSearchEdit && wp == 'Y') { ExecuteToolbarCommand(st, CMD_REDO); return 0; }
        if (ctrl && wp == 'F') {
            if (!st->searchVisible) ExecuteToolbarCommand(st, CMD_TOGGLE_SEARCH);
            else if (st->hwndSearchEdit) SetFocus(st->hwndSearchEdit);
            return 0;
        }
        if (ctrl && wp == 'P') { ExecuteToolbarCommand(st, CMD_PRINT); return 0; }
        if (wp == VK_F3) { ExecuteToolbarCommand(st, CMD_SEARCH_NEXT); return 0; }
        if (!inSearchEdit && (wp == VK_DELETE || wp == VK_BACK)) {
            if (!st->lassoSelectedIds.empty()) {
                st->cmdStack.Execute(st->doc, std::make_unique<BatchEraseObjectsCommand>(
                    ObjectTarget{ st->selectedIsCanvas, st->selectedPageIndex }, st->lassoSelectedIds));
                st->lassoSelectedIds.clear();
                st->selectedObjectId = 0;
                st->statusBannerUtf8 = "Selecci\xC3\xB3n Lazo eliminada";
                UpdateWindowTitle(st);
                InvalidateViews(st);
                return 0;
            }
            if (st->selectedObjectId != 0) {
                st->cmdStack.Execute(st->doc, std::make_unique<RemoveObjectCommand>(
                    ObjectTarget{ st->selectedIsCanvas, st->selectedPageIndex }, st->selectedObjectId));
                st->selectedObjectId = 0;
                st->statusBannerUtf8 = "Objeto eliminado";
                UpdateWindowTitle(st);
                InvalidateViews(st);
                return 0;
            }
        }
        break;
    }
    case WM_TIMER: {
        if (st && wp == 701 && st->replayActive) {
            st->replayCursorMs += 45;
            st->audioSession.AdvancePlaybackMs(45);
            if (st->replayCursorMs >= st->replayMaxMs) {
                st->replayActive = false;
                KillTimer(hwnd, 701);
                st->audioSession.SetPlaying(false);
                st->statusBannerUtf8 = "Ink Replay completado";
            } else {
                st->statusBannerUtf8 = "Ink Replay: " + std::to_string(st->replayCursorMs) + " / " + std::to_string(st->replayMaxMs) + " ms";
            }
            InvalidateViews(st);
            return 0;
        }
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
            if (st->hwndSidebar && IsWindowVisible(st->hwndSidebar)) RedrawWindow(st->hwndSidebar, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            if (st->hwndInspector && IsWindowVisible(st->hwndInspector)) RedrawWindow(st->hwndInspector, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            CaptureStudioCombinedToPng(st, st->screenshotOutPath);
            if (st->smokeTestMode || !st->screenshotOutPath.empty()) PostMessageW(hwnd, WM_CLOSE, 0, 0);
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
        KillTimer(hwnd, 701);
        if (st) {
            st->pageCache.Shutdown();
            WinRtPdfRenderer::ClearOpenDocumentCache();
            st->renderer.DiscardDeviceResources();
            if (st->hUiFont)      { DeleteObject(st->hUiFont);      st->hUiFont = nullptr; }
            if (st->hUiBoldFont)  { DeleteObject(st->hUiBoldFont);  st->hUiBoldFont = nullptr; }
            if (st->hUiSmallFont) { DeleteObject(st->hUiSmallFont); st->hUiSmallFont = nullptr; }
            if (st->hUiTitleFont) { DeleteObject(st->hUiTitleFont); st->hUiTitleFont = nullptr; }
            if (st->hSearchBgBrush) { DeleteObject(st->hSearchBgBrush); st->hSearchBgBrush = nullptr; }
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
        } else if (arg == L"--calc-card" && i + 1 < argc) {
            st->cliCalcCardSpec = WideToUtf8(argv[++i]);
        } else if (arg == L"--showcase-stem") {
            st->showcaseStemCli = true;
            outMode = "canvas";
        } else if (arg == L"--screenshot-out" && i + 1 < argc) {
            st->screenshotOutPath = argv[++i];
        } else if (arg == L"--smoke-test") {
            st->smokeTestMode = true;
        } else if (arg == L"--theme" && i + 1 < argc) {
            const std::wstring t = argv[++i];
            if (t == L"dark") {
                st->glassSettings.mode = UiGlass::AppearanceMode::Dark;
                st->glassSettings.increaseContrast = false;
                st->glassSettings.reduceTransparency = false;
            } else if (t == L"high-contrast" || t == L"hc") {
                st->glassSettings.mode = UiGlass::AppearanceMode::HighContrast;
                st->glassSettings.increaseContrast = true;
                st->glassSettings.reduceTransparency = true;
            } else {
                st->glassSettings.mode = UiGlass::AppearanceMode::Light;
                st->glassSettings.increaseContrast = false;
                st->glassSettings.reduceTransparency = false;
            }
        } else if (arg == L"--glass-opacity" && i + 1 < argc) {
            st->glassSettings.userOpacitySlider = std::clamp(static_cast<float>(_wtof(argv[++i])), 0.0f, 1.0f);
            st->glassSettings.reduceTransparency = false;
        } else if (arg == L"--inspector" && i + 1 < argc) {
            const std::wstring tab = argv[++i];
            if (tab == L"academic") { st->inspectorVisible = true; st->inspectorTab = 0; }
            else if (tab == L"stem") { st->inspectorVisible = true; st->inspectorTab = 1; }
            else if (tab == L"layers") { st->inspectorVisible = true; st->inspectorTab = 2; }
            else if (tab == L"off") { st->inspectorVisible = false; }
        } else if (arg == L"--appearance-popover") {
            st->appearancePopoverOpen = true;
        } else if (arg == L"--search-open") {
            st->searchVisible = true;
        }
    }
    LocalFree(argv);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    if (HMODULE hUser32 = GetModuleHandleW(L"user32.dll")) {
        using SetDpiCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto pSetDpiCtx = reinterpret_cast<SetDpiCtxFn>(GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
        if (!pSetDpiCtx || !pSetDpiCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
            SetProcessDPIAware();
        }
    } else {
        SetProcessDPIAware();
    }
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    RoInitialize(RO_INIT_SINGLETHREADED);

    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartupInput gdiplusIn{};
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusIn, nullptr);

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
    registerCls(kInspectorClassName, InspectorWndProc, LoadCursorW(nullptr, IDC_ARROW));
    registerCls(kPromptClassName, PromptWndProc, LoadCursorW(nullptr, IDC_ARROW));

    {
        auto st = std::make_unique<StudioAppState>();
        UiGlass::LoadGlassSettings(st->glassSettings);

        // Collect UTF-8 CLI args for unified cross-platform workflow dispatch
        std::vector<std::string> cliArgsUtf8;
        {
            int argcW = 0;
            LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argcW);
            if (argvW) {
                for (int i = 1; i < argcW; ++i) {
                    cliArgsUtf8.push_back(WideToUtf8(argvW[i]));
                }
                LocalFree(argvW);
            }
        }
        const CliDispatch::StudioCliOptions cliOpts = CliDispatch::ParseArgsUtf8(cliArgsUtf8);

        std::wstring initialOpen;
        std::string initialMode;
        ParseCliArgs(st.get(), initialOpen, initialMode);

        // Execute extended batch/workflow/academic/audio/crypto/LAN CLI commands first
        if (cliOpts.HasAnyExtendedWorkflow() ||
            (!cliOpts.openPathUtf8.empty() && !cliOpts.decryptPassUtf8.empty())) {
            const auto disp = CliDispatch::ExecuteWorkflow(cliOpts, st->doc, st->audioSession);
            if (!disp.statusBannerUtf8.empty()) {
                st->statusBannerUtf8 = disp.statusBannerUtf8;
            }
            if (disp.exitImmediately) {
                st.reset();
                if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
                RoUninitialize();
                CoUninitialize();
                return disp.exitCode;
            }
        }

        if (initialMode == "canvas" && st->doc.Mode() != DocumentMode::InfiniteCanvas) {
            st->doc.ResetToBlank(DocumentMode::InfiniteCanvas);
            st->doc.SetCanvasBackground(PaperPattern::Grid);
            st->doc.SetTitle("Lienzo Infinito");
        }

        const int winW = cliOpts.windowW > 0 ? cliOpts.windowW : 1320;
        const int winH = cliOpts.windowH > 0 ? cliOpts.windowH : 840;
        HWND hwnd = CreateWindowExW(0, kMainClassName, L"Document Studio \u2014 Calculadora Unificada",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, winW, winH,
            nullptr, nullptr, hInst, st.get());
        if (!hwnd) {
            st.reset();
            if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
            RoUninitialize();
            CoUninitialize();
            return 1;
        }

        if (cliOpts.simulateGpuReset) {
            st->renderer.DiscardDeviceResources();
            st->renderer.AttachHwnd(st->hwndViewport, (std::max)(200, winW - kSidebarWidth), (std::max)(200, winH - kTopChromeH - kBottomStatusH));
        }

        if (!initialOpen.empty()) {
            const std::wstring openExt = GetFileExtLower(initialOpen);
            if (openExt != L".png" && openExt != L".jpg" && openExt != L".jpeg" && cliOpts.decryptPassUtf8.empty()) {
                OpenDocumentFile(st.get(), initialOpen);
            }
        }
        if (initialMode == "canvas") {
            if (st->doc.Mode() != DocumentMode::InfiniteCanvas) {
                ExecuteToolbarCommand(st.get(), CMD_MODE_CANVAS);
            }
            st->camera.zoom = 1.0;
            st->camera.panX = -48.0;
            st->camera.panY = -48.0;
        }

        if (st->showcaseStemCli) {
            AppActions::PopulateStemShowcaseScene(st->doc, st->cmdStack);
            st->minimapVisible = true;
            st->rulerState.visible = true;
            st->rulerState.centerX = 610.0;
            st->rulerState.centerY = 294.0;
            st->rulerState.angleDeg = -8.0;
            st->rulerState.lengthPt = 250.0;
            st->camera.zoom = 1.0;
            st->camera.panX = -18.0;
            st->camera.panY = -12.0;
        } else if (!st->cliCalcCardSpec.empty() && !cliOpts.calcBridgeSync) {
            ApplyCliCalcCardSpec(st.get());
        } else if (st->doc.Mode() == DocumentMode::InfiniteCanvas && st->doc.CanvasObjects().empty() && initialOpen.empty()) {
            // Seed crisp sample ink & geometry in Infinite Canvas smoke/launch when empty
            AnnotationObject box;
            box.kind = ObjectKind::Shape;
            box.shapeKind = ShapeKind::Rectangle;
            box.bounds = { 80.0, 80.0, 260.0, 120.0 };
            box.strokeColor = DocColor::RGBA(0, 122, 255, 255);
            box.fillColor = DocColor::RGBA(232, 242, 255, 255);
            st->doc.AddCanvasObject(std::move(box));

            AnnotationObject tb;
            tb.kind = ObjectKind::TextBox;
            tb.bounds = { 96.0, 102.0, 228.0, 80.0 };
            tb.textUtf8 = "Lienzo Infinito listo\nZoom y pan ilimitados";
            tb.strokeColor = DocColor::RGBA(20, 26, 36, 255);
            tb.fontSizePt = 15.0;
            st->doc.AddCanvasObject(std::move(tb));
        }

        LayoutMainChildren(st.get());
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

    if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
    RoUninitialize();
    CoUninitialize();
    return 0;
}
