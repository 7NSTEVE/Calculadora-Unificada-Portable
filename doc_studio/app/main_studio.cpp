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
#include "canvas_manipulation.hpp"
#include "projector_window.hpp"
#include "stem_gui_dialogs.hpp"
#include "studio_actions.hpp"
#include "studio_cli_dispatch.hpp"
#include "../core/audio_timeline.hpp"
#include "../core/camera.hpp"
#include "../core/command_stack.hpp"
#include "../core/cudoc_storage.hpp"
#include "../core/doc_types.hpp"
#include "../core/document_model.hpp"
#include "../core/image_processing.hpp"
#include "../core/notebook_vault.hpp"
#include "../core/utf_conv.hpp"
#include "../interop/notebook_migration_engine.hpp"
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
    CMD_ACADEMIC_SEARCH_MATERIALS,
    // Track A-E Unified CuadernU & Multi-Format Interoperability IDs
    CMD_MIGRATE_NOTEBOOK = 2401,
    CMD_OPEN_VAULT_LIBRARY,
    CMD_SAVE_ENCRYPTED,
    CMD_OPEN_ENCRYPTED,
    CMD_VERSION_HISTORY,
    CMD_CONFLICT_MERGE,
    CMD_VIEW_ATTACHMENTS,
    CMD_QUICK_NOTE,
    CMD_NEW_TAB,
    CMD_NEXT_TAB,
    CMD_TOGGLE_SPREAD,
    CMD_TOGGLE_SPLIT_VIEW,
    CMD_OPEN_PROJECTOR,
    CMD_FREEZE_PROJECTOR,
    CMD_TOGGLE_PARTIAL_ERASER,
    CMD_CYCLE_PEN_STYLE,
    CMD_CUSTOM_INK_COLOR,
    CMD_TOGGLE_SNAP_GRID,
    CMD_CYCLE_GEOMETRY_TOOL,
    CMD_COPY_STROKE_STYLE,
    CMD_RESTYLE_INK,
    CMD_HTR_TO_TEXT,
    CMD_HTR_TO_MATH,
    CMD_GROUP_OBJECTS,
    CMD_UNGROUP_OBJECTS,
    CMD_DUPLICATE_OBJECTS,
    CMD_CLIPBOARD_PASTE,
    CMD_ADD_REDACTION,
    CMD_ADD_CONNECTOR,
    CMD_IMPORT_IMAGE_MULTI,
    CMD_IMG_ENHANCE_WHITEBOARD,
    CMD_IMG_REMOVE_BG,
    CMD_IMG_ROTATE_90,
    CMD_IMG_TOGGLE_OPACITY,
    CMD_IMG_CROP,
    CMD_STICKER_OPAMP,
    CMD_STICKER_RESISTOR,
    CMD_STICKER_AXES,
    CMD_STICKER_NOTE,
    CMD_PAPER_ISOMETRIC,
    CMD_PAPER_LOGLOG,
    CMD_PAPER_POLAR,
    CMD_PAPER_SMITH,
    CMD_PAPER_CORNELL,
    CMD_PAPER_MUSIC,
    CMD_AUDIO_BOOKMARK,
    CMD_AUDIO_IMPORT,
    CMD_AUDIO_TRANSCRIBE,
    CMD_AUDIO_EXPORT_HTML5,
    CMD_ADD_TABLE,
    CMD_ADD_CODE_BLOCK,
    CMD_ADV_MATH,
    CMD_CHECK_MY_WORK,
    CMD_TEACH_ME,
    CMD_SUMMARIZE_NOTES,
    CMD_LOCAL_QA,
    CMD_COMPARE_4_CASIO,
    CMD_RECOGNIZE_CIRCUIT,
    CMD_ENG_TOOLBOX,
    CMD_FORMULA_AST_SEARCH,
    CMD_LAN_QR,
    CMD_EXAM_PIN_LOCK,
    CMD_TEXT_BOLD,
    CMD_TEXT_ITALIC,
    CMD_TEXT_UNDERLINE,
    CMD_INLINE_EDIT_CTRL = 2599
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
    std::string statusBannerUtf8 = "CuadernU listo";

    ToolKind activeTool = ToolKind::Pen;
    DocColor penColor = DocColor::RGBA(26, 115, 232, 255);
    double penWidth = 2.6;
    InkPenStyle penStyle = InkPenStyle::Fountain;
    EraserActionMode eraserActionMode = EraserActionMode::WholeStroke;
    bool snapToGrid = false;
    double penOpacity = 1.0;

    // Interactive gesture & direct canvas manipulation state
    bool panning = false;
    POINT lastPanPt{};
    POINT panStartPt{};
    bool panMoved = false;
    bool rbuttonMoved = false;
    POINT rbuttonDownPt{};
    bool drawing = false;
    bool draftIsCanvas = false;
    std::size_t draftPageIdx = 0;
    AnnotationObject draftObj{};
    DocPoint draftAnchorWorld{};
    bool hasDraftCrop = false;
    DocRect draftCropPageLocal{};

    CanvasManip::ActiveTransformDrag transformDrag{};
    bool pdfSelectingText = false;
    std::size_t pdfSelPage = 0;
    DocPoint pdfSelAnchorLocal{};
    DocRect pdfSelRectLocal{};
    std::string lastSelectedPdfTextUtf8;

    // Stylus hardware features (F-036 palm rejection, F-037 inverted eraser, F-038 barrel button)
    ULONGLONG lastPenInputTickMs = 0;
    bool barrelTempLasso = false;
    bool invertedTempEraser = false;
    ToolKind savedToolBeforeStylusMod = ToolKind::Pen;

    // Inline TextBox editing overlay
    HWND hwndInlineEdit = nullptr;
    std::uint64_t inlineEditObjId = 0;
    ObjectTarget inlineEditTarget{};

    // Multi-tab, split view, vault, versions, projector, exam lock
    struct OpenDocTab {
        std::wstring path;
        std::vector<std::uint8_t> serializedCudoc;
        Camera2D cam{};
        std::string title;
    };
    std::vector<OpenDocTab> tabs;
    std::size_t activeTabIdx = 0;
    bool splitViewActive = false;
    Camera2D splitCamera{};
    NotebookVault vault;
    PageVersionHistory versionHistory;
    SecondaryProjectorWindow projectorWnd;
    bool examModeLocked = false;
    std::string examPinUtf8;
    bool aiOfflineOnly = true;

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
    bool testDpiCycleCli = false;
    UINT currentDpi = 96;
    int dpiCycleTransitions = 0;
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

// Dynamic modal text prompt (no .rc dependency, Apple HIG + Liquid Glass 2.0 synchronized)
struct PromptState {
    std::wstring label1;
    std::wstring val1;
    std::wstring label2;
    std::wstring val2;
    bool twoFields = false;
    bool accepted = false;
    UiGlass::GlassSettings glassSettings{};
    HWND hwndEdit1 = nullptr;
    HWND hwndEdit2 = nullptr;
    HFONT hFont = nullptr;
    HFONT hBoldFont = nullptr;
    HBRUSH hBgBrush = nullptr;
    HBRUSH hCardBrush = nullptr;
};

LRESULT CALLBACK PromptWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PromptState* ps = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_CREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ps = reinterpret_cast<PromptState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ps));
        UiGlass::ApplyGoldenGateWindowFrame(hwnd, ps->glassSettings.IsDark());
        ps->hFont = UiGlass::CreateHigUiFont(13, FW_NORMAL);
        ps->hBoldFont = UiGlass::CreateHigUiFont(13, FW_SEMIBOLD);
        const UiGlass::GlassPalette pal = UiGlass::GetPalette(ps->glassSettings);
        ps->hBgBrush = CreateSolidBrush(pal.sidebarBg.ToColorRef());
        ps->hCardBrush = CreateSolidBrush(pal.contentCard.ToColorRef());
        HFONT hf = ps->hFont ? ps->hFont : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

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
        SendMessageW(bOk, WM_SETFONT, reinterpret_cast<WPARAM>(ps->hBoldFont ? ps->hBoldFont : hf), TRUE);
        SendMessageW(bCancel, WM_SETFONT, reinterpret_cast<WPARAM>(hf), TRUE);
        SetFocus(ps->hwndEdit1);
        return 0;
    }
    case WM_ERASEBKGND: {
        if (ps && ps->hBgBrush) {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, ps->hBgBrush);
            return 1;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        if (ps && ps->hBgBrush) {
            const UiGlass::GlassPalette pal = UiGlass::GetPalette(ps->glassSettings);
            HDC hdcS = reinterpret_cast<HDC>(wp);
            SetTextColor(hdcS, pal.labelPrimary.ToColorRef());
            SetBkColor(hdcS, pal.sidebarBg.ToColorRef());
            return reinterpret_cast<LRESULT>(ps->hBgBrush);
        }
        break;
    }
    case WM_CTLCOLOREDIT: {
        if (ps && ps->hCardBrush) {
            const UiGlass::GlassPalette pal = UiGlass::GetPalette(ps->glassSettings);
            HDC hdcE = reinterpret_cast<HDC>(wp);
            SetTextColor(hdcE, pal.labelPrimary.ToColorRef());
            SetBkColor(hdcE, pal.contentCard.ToColorRef());
            return reinterpret_cast<LRESULT>(ps->hCardBrush);
        }
        break;
    }
    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        if (suggested) {
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
        InvalidateRect(hwnd, nullptr, TRUE);
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
    case WM_DESTROY:
        if (ps) {
            if (ps->hFont) { DeleteObject(ps->hFont); ps->hFont = nullptr; }
            if (ps->hBoldFont) { DeleteObject(ps->hBoldFont); ps->hBoldFont = nullptr; }
            if (ps->hBgBrush) { DeleteObject(ps->hBgBrush); ps->hBgBrush = nullptr; }
            if (ps->hCardBrush) { DeleteObject(ps->hCardBrush); ps->hCardBrush = nullptr; }
        }
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
    if (owner) {
        if (const auto* ownerSt = reinterpret_cast<const StudioAppState*>(GetWindowLongPtrW(owner, GWLP_USERDATA))) {
            ps.glassSettings = ownerSt->glassSettings;
        }
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
    std::wstring extra;
    if (st->tabs.size() > 1) {
        extra += L"  [" + std::to_wstring(st->activeTabIdx + 1) + L"/" + std::to_wstring(st->tabs.size()) + L"]";
    }
    if (st->splitViewActive) extra += L"  [Vista Dividida]";
    if (st->examModeLocked) extra += L"  [Modo Examen]";
    const std::wstring title = L"CuadernU \u2014 " + name + extra + dirtyMark;
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
    ov.hasPdfTextSel = st->pdfSelectingText && !st->pdfSelRectLocal.Empty();
    ov.pdfTextSelPage = st->pdfSelPage;
    ov.pdfTextSelRectLocal = st->pdfSelRectLocal;
    ov.showSelectionHandles = !st->presenterMode;
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
    if (st->projectorWnd.IsOpen()) {
        ViewportOverlayState pv{};
        PopulateViewportOverlay(st, pv);
        st->projectorWnd.SyncFromMain(&st->doc, &st->camera, &st->pageCache, pv);
    }
    if (st->hwndMain) {
        RECT rc{};
        GetClientRect(st->hwndMain, &rc);
        RECT topRc{ 0, 0, rc.right, kTopChromeH + (st->searchVisible ? 40 : 0) };
        InvalidateRect(st->hwndMain, &topRc, FALSE);
        RECT botRc{ 0, rc.bottom - kBottomStatusH, rc.right, rc.bottom };
        InvalidateRect(st->hwndMain, &botRc, FALSE);
    }
}

std::wstring GetCuadernUDataDir() {
    wchar_t localApp[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", localApp, MAX_PATH);
    std::wstring base = (n > 0 && n < MAX_PATH) ? std::wstring(localApp) : L".";
    std::filesystem::path dir = std::filesystem::path(base) / L"CuadernU";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir.wstring();
}

std::wstring GetPersistentUndoPath(const StudioAppState* st) {
    std::filesystem::path dir = std::filesystem::path(GetCuadernUDataDir()) / L"history";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::string uuid = st ? st->doc.DocumentUuid() : "default";
    for (char& ch : uuid) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') ch = '_';
    }
    return (dir / (Utf8ToWide(uuid) + L".undo.json")).wstring();
}

void SavePersistentUndoAndTruncateWal(StudioAppState* st) {
    if (!st) return;
    if (!st->currentDocPath.empty()) {
        CudocWal::Truncate(st->currentDocPath + L"-wal");
    }
    st->cmdStack.SaveSessionHistoryToFile(GetPersistentUndoPath(st), st->doc);
}

void TryLoadPersistentUndo(StudioAppState* st) {
    if (!st) return;
    if (st->cmdStack.LoadSessionHistoryFromFile(GetPersistentUndoPath(st), st->doc)) {
        st->statusBannerUtf8 += " (historial Undo restaurado)";
    }
}

void AppendWalMutation(StudioAppState* st, const std::string& opTag) {
    if (!st || st->currentDocPath.empty()) return;
    WalRecord rec{};
    rec.opTag = opTag;
    rec.payloadJson = "{\"rev\":" + std::to_string(st->doc.Revision()) + "}";
    CudocWal::AppendRecord(st->currentDocPath + L"-wal", rec);
}

const char* PaperPatternLabelUtf8(PaperPattern pat) noexcept {
    switch (pat) {
    case PaperPattern::Plain:      return "Blanco";
    case PaperPattern::Ruled:      return "Rayado";
    case PaperPattern::Grid:       return "Cuadr\xC3\xAD" "cula";
    case PaperPattern::Dots:       return "Puntos";
    case PaperPattern::Millimeter: return "Milimetrado";
    case PaperPattern::Isometric:  return "Isom\xC3\xA9trico 30\xC2\xB0";
    case PaperPattern::LogLog:     return "Log-Log";
    case PaperPattern::Polar:      return "Polar";
    case PaperPattern::SmithChart: return "Carta de Smith";
    case PaperPattern::Cornell:    return "Cornell";
    case PaperPattern::MusicStaff: return "Pentagrama";
    }
    return "Blanco";
}

GuiDialogs::PromptFn MakePromptAdapter(StudioAppState* st) {
    return [st](const std::wstring& title, const std::wstring& label, std::wstring& value, bool) -> bool {
        return ShowTextPrompt(st->hwndMain, title.c_str(), label.c_str(), value);
    };
}

void CommitInlineTextEdit(StudioAppState* st, bool save) {
    if (!st || !st->hwndInlineEdit) return;
    HWND hEdit = st->hwndInlineEdit;
    const std::uint64_t objId = st->inlineEditObjId;
    const ObjectTarget tgt = st->inlineEditTarget;
    st->hwndInlineEdit = nullptr;
    st->inlineEditObjId = 0;

    if (save && objId != 0) {
        const int len = GetWindowTextLengthW(hEdit);
        std::wstring buf(static_cast<std::size_t>((std::max)(0, len)) + 1u, L'\0');
        GetWindowTextW(hEdit, buf.data(), static_cast<int>(buf.size()));
        buf.resize(static_cast<std::size_t>((std::max)(0, len)));
        if (AnnotationObject* obj = CanvasManip::FindObjectMut(st->doc, tgt, objId)) {
            AnnotationObject before = *obj;
            AnnotationObject after = *obj;
            after.textUtf8 = WideToUtf8(buf);
            if (after.textUtf8 != before.textUtf8) {
                st->cmdStack.Execute(st->doc, std::make_unique<ModifyObjectCommand>(tgt, std::move(before), std::move(after)));
                AppendWalMutation(st, "edit_textbox");
                st->statusBannerUtf8 = "Texto actualizado";
            }
        }
    }
    if (IsWindow(hEdit)) DestroyWindow(hEdit);
    UpdateWindowTitle(st);
    InvalidateViews(st);
}

void BeginInlineTextEdit(StudioAppState* st, ObjectTarget tgt, const AnnotationObject& obj) {
    if (!st || !st->hwndViewport) return;
    CommitInlineTextEdit(st, true);
    double ox = 0.0, oy = 0.0;
    if (!tgt.isCanvas) {
        if (const auto* pl = st->doc.Layout().PlacementAt(tgt.pageIndex)) {
            const DocRect wr = pl->WorldRect();
            ox = wr.x; oy = wr.y;
        }
    }
    const DocRect scr = st->camera.WorldRectToScreen({ ox + obj.bounds.x, oy + obj.bounds.y, obj.bounds.w, obj.bounds.h });
    const int x = static_cast<int>(std::lround(scr.x));
    const int y = static_cast<int>(std::lround(scr.y));
    const int w = (std::max)(160, static_cast<int>(std::lround(scr.w)));
    const int h = (std::max)(44, static_cast<int>(std::lround(scr.h)));

    st->hwndInlineEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", Utf8ToWide(obj.textUtf8).c_str(),
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        x, y, w, h, st->hwndViewport, reinterpret_cast<HMENU>(CMD_INLINE_EDIT_CTRL),
        GetModuleHandleW(nullptr), nullptr);
    if (st->hwndInlineEdit) {
        st->inlineEditObjId = obj.id;
        st->inlineEditTarget = tgt;
        SendMessageW(st->hwndInlineEdit, WM_SETFONT, reinterpret_cast<WPARAM>(st->hUiFont), TRUE);
        SetFocus(st->hwndInlineEdit);
        SendMessageW(st->hwndInlineEdit, EM_SETSEL, 0, -1);
    }
}

void SaveActiveTabSnapshot(StudioAppState* st) {
    if (!st) return;
    if (st->tabs.empty()) {
        st->tabs.push_back({});
        st->activeTabIdx = 0;
    }
    if (st->activeTabIdx >= st->tabs.size()) st->activeTabIdx = st->tabs.size() - 1;
    auto& t = st->tabs[st->activeTabIdx];
    t.path = st->currentDocPath;
    t.cam = st->camera;
    t.title = st->doc.Title();
    t.serializedCudoc.clear();
    CudocStorage::SerializeToBytes(st->doc, t.serializedCudoc);
}

void SwitchToDocTab(StudioAppState* st, std::size_t newIdx) {
    if (!st || st->tabs.empty()) return;
    SaveActiveTabSnapshot(st);
    newIdx %= st->tabs.size();
    st->activeTabIdx = newIdx;
    auto& t = st->tabs[newIdx];
    if (!t.serializedCudoc.empty()) {
        DocumentModel loaded;
        if (CudocStorage::DeserializeFromBytes(t.serializedCudoc.data(), t.serializedCudoc.size(), loaded)) {
            st->doc = std::move(loaded);
        }
    }
    st->currentDocPath = t.path;
    st->camera = t.cam;
    st->selectedObjectId = 0;
    st->lassoSelectedIds.clear();
    st->cmdStack.Clear();
    st->pageCache.InvalidateAll(++st->asyncGen);
    st->statusBannerUtf8 = "Pesta\xC3\xB1""a " + std::to_string(newIdx + 1) + "/" + std::to_string(st->tabs.size()) + ": " + st->doc.Title();
    LayoutMainChildren(st);
    UpdateWindowTitle(st);
    InvalidateViews(st);
}

void AddNewDocTab(StudioAppState* st, bool quickNote) {
    if (!st) return;
    SaveActiveTabSnapshot(st);
    StudioAppState::OpenDocTab nt{};
    DocumentModel fresh;
    fresh.ResetToBlank(quickNote ? DocumentMode::InfiniteCanvas : DocumentMode::PaginatedPdf);
    if (quickNote) {
        fresh.SetCanvasBackground(PaperPattern::Dots);
        fresh.SetTitle("Nota R\xC3\xA1pida");
    } else {
        fresh.SetTitle("Cuaderno " + std::to_string(st->tabs.size() + 1));
    }
    CudocStorage::SerializeToBytes(fresh, nt.serializedCudoc);
    nt.title = fresh.Title();
    nt.cam.zoom = 1.0;
    nt.cam.panX = -48.0;
    nt.cam.panY = -48.0;
    st->tabs.push_back(std::move(nt));
    SwitchToDocTab(st, st->tabs.size() - 1);
}

void PollCasioIpcInbox(StudioAppState* st) {
    if (!st) return;
    std::filesystem::path inbox = std::filesystem::path(GetCuadernUDataDir()) / L"ipc_inbox";
    std::error_code ec;
    if (!std::filesystem::exists(inbox, ec)) return;
    bool anyAdded = false;
    for (const auto& ent : std::filesystem::directory_iterator(inbox, ec)) {
        if (!ent.is_regular_file()) continue;
        if (ent.path().extension() != L".json") continue;
        std::ifstream ifs(ent.path(), std::ios::binary);
        if (!ifs.is_open()) continue;
        std::string raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();
        try {
            auto j = nlohmann::json::parse(raw);
            CalcBridge::CalcStepRecord rec{};
            rec.modelIdUtf8 = j.value("model", "fx-991CW");
            rec.keySeqUtf8 = j.value("keySeq", "[CALC]");
            rec.expressionUtf8 = j.value("expression", "");
            if (!rec.expressionUtf8.empty() || !rec.keySeqUtf8.empty()) {
                double x = 80.0, y = 80.0;
                NextCardPlacement(st, x, y);
                const ObjectTarget tgt = CurrentTarget(st);
                const auto newId = AppActions::AddCalcCardAction(st->doc, st->cmdStack, tgt, x, y, rec, st->statusBannerUtf8);
                if (newId != 0) {
                    st->selectedObjectId = newId;
                    st->selectedIsCanvas = tgt.isCanvas;
                    st->selectedPageIndex = tgt.pageIndex;
                    st->statusBannerUtf8 = "Tarjeta CASIO recibida en vivo (" + rec.modelIdUtf8 + ")";
                    anyAdded = true;
                }
            }
        } catch (...) {}
        std::filesystem::remove(ent.path(), ec);
    }
    if (anyAdded) {
        UpdateWindowTitle(st);
        InvalidateViews(st);
    }
}

bool OpenDocumentFile(StudioAppState* st, const std::wstring& path, std::string* outErr = nullptr) {
    const std::wstring ext = GetFileExtLower(path);
    const std::string pathUtf8 = WideToUtf8(path);

    if (ext == L".goodnotes" || ext == L".note" || ext == L".one" || ext == L".onepkg" || ext == L".tinta") {
        DocumentModel migrated;
        Interop::MigrationResult migRes{};
        if (!Interop::NotebookMigrationEngine::ImportAndMigrateFile(path, migrated, migRes)) {
            if (outErr) *outErr = migRes.errorUtf8;
            st->statusBannerUtf8 = "Error de migraci\xC3\xB3n: " + migRes.errorUtf8;
            return false;
        }
        st->doc = std::move(migrated);
        if (!migRes.primaryAudioWavBytes.empty()) {
            st->audioSession.LoadFromWavBytes(migRes.primaryAudioWavBytes.data(), migRes.primaryAudioWavBytes.size());
        }
        st->cmdStack.Clear();
        st->pageCache.InvalidateAll(++st->asyncGen);
        st->currentDocPath.clear();
        CenterCameraOnPage(st, 0);
        st->statusBannerUtf8 = "Migrado desde " + migRes.report.formatLabelUtf8 + " (" +
            std::to_string(migRes.report.pageCount) + " p\xC3\xA1g., " +
            std::to_string(migRes.report.editableStrokeCount) + " trazos editables)";
    } else if (ext == L".pdf") {
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
        std::size_t replayed = 0;
        const std::wstring walPath = path + L"-wal";
        bool loadedOk = false;
        if (std::filesystem::exists(walPath)) {
            loadedOk = CudocWal::RecoverDocument(path, walPath, st->doc, &replayed, &err);
        }
        if (!loadedOk) {
            loadedOk = CudocStorage::LoadFromFileWithRecovery(path, st->doc, &usedBak, &err);
        }
        if (!loadedOk) {
            if (outErr) *outErr = err;
            st->statusBannerUtf8 = "Error .cudoc: " + err;
            return false;
        }
        st->cmdStack.Clear();
        st->pageCache.InvalidateAll(++st->asyncGen);
        st->currentDocPath = path;
        CenterCameraOnPage(st, 0);
        st->statusBannerUtf8 = (replayed > 0)
            ? ("Recuperado con WAL (" + std::to_string(replayed) + " deltas)")
            : (usedBak ? "Recuperado desde respaldo .bak" : ".cudoc cargado");
        TryLoadPersistentUndo(st);
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
    } else if (ext == L".png" || ext == L".jpg" || ext == L".jpeg" || ext == L".bmp" ||
               ext == L".tif" || ext == L".tiff" || ext == L".webp" || ext == L".svg" || ext == L".gif") {
        HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hf == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li{};
        GetFileSizeEx(hf, &li);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>((std::min<LONGLONG>)(li.QuadPart, 64ll * 1024ll * 1024ll)));
        DWORD rd = 0;
        ReadFile(hf, bytes.data(), static_cast<DWORD>(bytes.size()), &rd, nullptr);
        CloseHandle(hf);
        bytes.resize(rd);

        ImageProc::RgbaImage dec{};
        std::vector<std::uint8_t> pngOut;
        int pxW = 480, pxH = 320;
        if (ImageProc::DecodeImageBytesWic(bytes.data(), bytes.size(), dec) && dec.Valid()) {
            pxW = dec.width;
            pxH = dec.height;
            if (!ImageProc::EncodeRgbaToPngWic(dec, pngOut)) pngOut = std::move(bytes);
        } else {
            pngOut = std::move(bytes);
        }

        AnnotationObject img;
        img.kind = ObjectKind::Image;
        img.layerId = st->doc.ActiveLayerId();
        img.pngBytes = std::move(pngOut);
        img.imagePixelW = pxW;
        img.imagePixelH = pxH;
        const double dispW = std::clamp(static_cast<double>(pxW) * 0.65, 120.0, 480.0);
        const double dispH = dispW * (static_cast<double>(pxH) / (std::max)(1, pxW));
        img.bounds = { 48.0, 48.0, dispW, dispH };
        if (st->doc.Mode() == DocumentMode::InfiniteCanvas) {
            st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(ObjectTarget{ true, 0 }, std::move(img)));
        } else {
            st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(ObjectTarget{ false, st->activePageIndex }, std::move(img)));
        }
        st->statusBannerUtf8 = "Imagen insertada (" + std::to_string(pxW) + "x" + std::to_string(pxH) + ")";
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
            { L"Todos compatibles (*.cudoc;*.pdf;*.goodnotes;*.note;*.one;*.onepkg;*.tinta;*.docx;*.pptx;*.png;*.svg)",
              L"*.cudoc;*.pdf;*.goodnotes;*.note;*.one;*.onepkg;*.tinta;*.docx;*.pptx;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff;*.webp;*.svg" },
            { L"Cuadernos CuadernU (*.cudoc;*.tinta)", L"*.cudoc;*.tinta" },
            { L"Goodnotes / Notability / OneNote (*.goodnotes;*.note;*.one;*.onepkg)", L"*.goodnotes;*.note;*.one;*.onepkg" },
            { L"Documentos PDF / Office (*.pdf;*.docx;*.pptx)", L"*.pdf;*.docx;*.pptx" }
        };
        std::vector<std::wstring> paths;
        if (ShowOpenDialog(st->hwndMain, L"Abrir en CuadernU", kFilters, 4, paths, false)) {
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
            static constexpr COMDLG_FILTERSPEC kFilters[] = { { L"Cuaderno CuadernU (*.cudoc)", L"*.cudoc" } };
            if (!ShowSaveDialog(st->hwndMain, L"Guardar cuaderno CuadernU (.cudoc)", kFilters, 1, L"cudoc", L"cuaderno.cudoc", savePath)) break;
        }
        std::string err;
        if (CudocStorage::SaveToFile(savePath, st->doc, &err)) {
            st->currentDocPath = savePath;
            st->cmdStack.MarkClean(st->doc);
            SavePersistentUndoAndTruncateWal(st);
            st->statusBannerUtf8 = "Guardado at\xC3\xB3micamente (.cudoc + .bak + historial)";
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
        di.lpszDocName = L"CuadernU";
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
        AppendMenuW(menu, MF_STRING, CMD_ADV_MATH,              L"Laboratorio Matem\u00E1tico Avanzado (Eigen / EDO / C\u00E1lculo)...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_TABLE,             L"Insertar Tabla Reactiva (CSV con F\u00F3rmulas)...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_CODE_BLOCK,        L"Insertar Bloque de C\u00F3digo / MCU...");
        AppendMenuW(menu, MF_STRING, CMD_CYCLE_MATH_SUBMODE,    L"Alternar Modo de Sustituci\u00F3n (#varsub / #nosub / #novar)");
        AppendMenuW(menu, MF_STRING, CMD_PARAM_SLIDER,          L"Slider de Par\u00E1metro Reactivo...");
        AppendMenuW(menu, MF_STRING, CMD_CHECK_STEP,            L"Verificar Paso Algebraico...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_MATRIX_SEL,        L"Matriz / Sistema de Ecuaciones Lineales (SEL)...");
        AppendMenuW(menu, MF_STRING, CMD_OCR_CONFIRM_FORMULA,   L"OCR -> F\u00F3rmula con Tarjeta de Confirmaci\u00F3n...");
        AppendMenuW(menu, MF_STRING, CMD_FORMULA_AST_SEARCH,    L"B\u00FAsqueda Sem\u00E1ntica de F\u00F3rmulas (AST)...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CMD_ADD_CIRCUIT,           L"Resolver Circuito MNA (DC / AC / Diodos)...");
        AppendMenuW(menu, MF_STRING, CMD_RECOGNIZE_CIRCUIT,     L"Reconocer Circuito a Mano Alzada...");
        AppendMenuW(menu, MF_STRING, CMD_ENG_TOOLBOX,           L"Caja de Herramientas de Ingenier\u00ED""a (RC / Op-Amp / SMD)...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_TRANSIENT_LAPLACE, L"Simulaci\u00F3n Transitoria Osciloscopio + Laplace (s)...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_KMAP,              L"Simplificar Mapa de Karnaugh...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_PLOT,              L"Gr\u00E1fico 2D / Regresi\u00F3n CSV...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_PLOT_3D,           L"Superficie 3D z = f(x,y) [Rotable con Rat\u00F3n]...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_PHASOR_DIAG,       L"Diagrama Fasorial Vectorial (Rect / Polar / Exp)...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_BODE_PLOT,         L"Diagrama de Bode Semilogar\u00EDtmico |H(jw)| dB...");
        AppendMenuW(menu, MF_STRING, CMD_ADD_CALC_CARD,         L"Insertar Tarjeta CASIO Interactiva...");
        AppendMenuW(menu, MF_STRING, CMD_CASIO_REVERSE_KEYS,    L"Traducir Ecuaci\u00F3n/Matriz/Fasor -> Teclas CASIO...");
        AppendMenuW(menu, MF_STRING, CMD_COMPARE_4_CASIO,       L"Comparador de 4 Modelos CASIO...");
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
        AppendMenuW(menu, MF_STRING, CMD_CHECK_MY_WORK,       L"Revisar mi Procedimiento (Check My Work)...");
        AppendMenuW(menu, MF_STRING, CMD_TEACH_ME,            L"Gu\u00ED""a Socr\u00E1tica Paso a Paso (Teach Me)...");
        AppendMenuW(menu, MF_STRING, CMD_SUMMARIZE_NOTES,     L"Generar Resumen No Destructivo (Capa Separada)");
        AppendMenuW(menu, MF_STRING, CMD_LOCAL_QA,            L"Consulta Local con Citas sobre Apuntes...");
        AppendMenuW(menu, MF_STRING, CMD_STUDY_DECK,          L"Repasar Flashcards (SM-2)...");
        AppendMenuW(menu, MF_STRING, CMD_FTS_SEARCH,          L"Buscar en \u00CDndice Unicode (FTS Vault)...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        HMENU hAudioSub = CreatePopupMenu();
        AppendMenuW(hAudioSub, MF_STRING, CMD_AUDIO_REC,          st->audioSession.IsContinuousRecording() ? L"Detener Grabaci\u00F3n Continua (Mic)" : L"Grabar Audio Continuo en Segundo Plano (Mic)");
        AppendMenuW(hAudioSub, MF_STRING, CMD_REPLAY_TOGGLE,      st->replayActive ? L"Detener Ink Replay + Audio" : L"Reproducir Ink Replay + Altavoces");
        AppendMenuW(hAudioSub, MF_STRING, CMD_AUDIO_BOOKMARK,     L"Insertar Marcador en L\u00EDnea de Audio...");
        AppendMenuW(hAudioSub, MF_STRING, CMD_AUDIO_IMPORT,       L"Importar Archivo de Audio (.wav)...");
        AppendMenuW(hAudioSub, MF_STRING, CMD_AUDIO_TRANSCRIBE,   L"Transcribir / Etiquetar Segmentos de Audio");
        AppendMenuW(hAudioSub, MF_STRING, CMD_AUDIO_EXPORT_HTML5, L"Exportar Tutorial Sincronizado HTML5...");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hAudioSub), L"Audio Sincronizado e Ink Replay");
        HMENU hLayerSub = CreatePopupMenu();
        AppendMenuW(hLayerSub, MF_STRING, CMD_LAYER_ADD,             L"Nueva capa...");
        AppendMenuW(hLayerSub, MF_STRING, CMD_LAYER_NEXT,            L"Cambiar capa activa");
        AppendMenuW(hLayerSub, MF_STRING, CMD_LAYER_TOGGLE_VIS,      L"Mostrar / Ocultar capa activa");
        AppendMenuW(hLayerSub, MF_STRING, CMD_LAYER_TOGGLE_LOCK,     L"Bloquear / Desbloquear capa activa");
        AppendMenuW(hLayerSub, MF_STRING, CMD_TEACHER_PRIVATE_LAYER, L"Crear Capa Privada Docente (Oculta en Proyecci\u00F3n/PDF)");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(hLayerSub), L"Capas del Documento");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CMD_ADD_TAPE,            L"Insertar Study Tape (Ocultar/Revelar)...");
        AppendMenuW(menu, MF_STRING, CMD_ANCHOR_INK,          L"Anclar tinta seleccionada a objeto");
        AppendMenuW(menu, MF_STRING, CMD_ADD_INKLINK,         L"Vincular p\u00E1gina PDF \u2194 Lienzo (InkLink)");
        AppendMenuW(menu, MF_STRING, CMD_TOGGLE_COLLAPSE,     L"Colapsar / Expandir rango PDF actual");
        AppendMenuW(menu, MF_STRING, CMD_TAG_OBJECT_BOARD,    L"Etiquetar Objeto (#examen / #duda) y Ver Tablero...");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CMD_OPEN_PROJECTOR,      L"Abrir Ventana de Proyector (Segundo Monitor)");
        AppendMenuW(menu, MF_STRING, CMD_FREEZE_PROJECTOR,    st->projectorWnd.IsFrozen() ? L"Descongelar Proyector" : L"Congelar Proyector");
        AppendMenuW(menu, MF_STRING, CMD_PRESENTER_MODE,      L"Modo Presentador + Puntero L\u00E1ser");
        AppendMenuW(menu, MF_STRING, CMD_LAN_QR,              L"Difusi\u00F3n LAN Local con C\u00F3digo QR...");
        AppendMenuW(menu, MF_STRING, CMD_EXAM_PIN_LOCK,       st->examModeLocked ? L"Desbloquear Modo Examen (PIN)..." : L"Activar Modo Examen con PIN...");
        AppendMenuW(menu, MF_STRING, CMD_FOCUS_MODE,          L"Modo Concentraci\u00F3n (Sin Distracciones)");
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
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        GuiDialogs::RunMultiFormatImageImport(st->hwndMain, st->doc, st->cmdStack, CurrentTarget(st), x, y, st->statusBannerUtf8);
        AppendWalMutation(st, "add_image");
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
        const PaperPattern next = static_cast<PaperPattern>((static_cast<int>(cur) + 1) % 11);
        st->cmdStack.Execute(st->doc, std::make_unique<SetBackgroundCommand>(
            ObjectTarget{ st->doc.Mode() == DocumentMode::InfiniteCanvas, st->activePageIndex }, next));
        st->statusBannerUtf8 = std::string("Plantilla de papel: ") + PaperPatternLabelUtf8(next);
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
            st->audioSession.StopAudioPlayback();
            st->statusBannerUtf8 = "Ink Replay + Audio detenido";
        } else {
            const auto range = InkEngine::ComputeReplayTimelineRange(st->doc, CurrentTarget(st));
            const std::uint64_t audioDur = st->audioSession.DurationMs();
            st->replayMaxMs = (std::max)(range.second, audioDur);
            if (st->replayMaxMs < 100) st->replayMaxMs = 1500;
            st->replayCursorMs = range.first;
            st->replayActive = true;
            st->audioSession.SeekMs(st->replayCursorMs);
            if (audioDur > 0) {
                std::string playErr;
                st->audioSession.StartAudioPlayback(st->replayCursorMs, &playErr);
            } else {
                st->audioSession.SetPlaying(true);
            }
            SetTimer(st->hwndMain, 701, 45, nullptr);
            st->statusBannerUtf8 = "Ink Replay reproduciendo (0 / " + std::to_string(st->replayMaxMs) + " ms)";
        }
        break;
    }
    case CMD_AUDIO_REC: {
        if (st->audioSession.IsContinuousRecording()) {
            const auto rep = st->audioSession.StopContinuousMicRecording();
            std::vector<std::uint8_t> wav;
            if (st->audioSession.ExportWavBytes(wav) && !wav.empty()) {
                // Embed playable AudioClip widget on current canvas/page
                double x = 80.0, y = 80.0;
                NextCardPlacement(st, x, y);
                AnnotationObject clip{};
                clip.kind = ObjectKind::AudioClip;
                clip.layerId = st->doc.ActiveLayerId();
                clip.bounds = { x, y, 270.0, 56.0 };
                clip.captionUtf8 = "Audio (" + std::to_string(st->audioSession.DurationMs() / 1000) + " s)";
                clip.textUtf8 = rep.statusNoteUtf8;
                clip.inkStartTimeMs = 0;
                clip.inkEndTimeMs = st->audioSession.DurationMs();
                clip.assetKey = st->doc.RegisterAsset(wav);
                st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(CurrentTarget(st), std::move(clip)));
                st->statusBannerUtf8 = rep.statusNoteUtf8 + " — clic en tarjeta o Replay para reproducir";
            } else {
                st->statusBannerUtf8 = rep.statusNoteUtf8;
            }
        } else {
            st->strokeOriginEpochMs = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            std::string recErr;
            if (st->audioSession.StartContinuousMicRecording(16000, &recErr)) {
                st->statusBannerUtf8 = "Grabando audio continuo (Win32 waveIn) — escribe mientras hablas; pulsa de nuevo para detener";
            } else {
                st->statusBannerUtf8 = "No se pudo iniciar micr\xC3\xB3" "fono: " + recErr;
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
        AppendMenuW(hm, MF_STRING, CMD_OPEN,                 L"Abrir documento... (Ctrl+O)");
        AppendMenuW(hm, MF_STRING, CMD_MIGRATE_NOTEBOOK,     L"Importar / Migrar Cuaderno (Goodnotes / Notability / OneNote / Office / .tinta)...");
        AppendMenuW(hm, MF_STRING, CMD_OPEN_VAULT_LIBRARY,   L"Biblioteca de Cuadernos y B\u00FAsqueda Global...");
        AppendMenuW(hm, MF_STRING, CMD_SAVE_CUDOC,           L"Guardar cuaderno .cudoc (Ctrl+S)");
        AppendMenuW(hm, MF_STRING, CMD_EXPORT_PDF,           L"Exportar PDF...");
        AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);
        HMENU hTabSub = CreatePopupMenu();
        AppendMenuW(hTabSub, MF_STRING, CMD_NEW_TAB,          L"Nueva pesta\u00F1""a de cuaderno (Ctrl+T)");
        AppendMenuW(hTabSub, MF_STRING, CMD_QUICK_NOTE,       L"Nota r\u00E1pida instant\u00E1nea (Ctrl+Shift+N)");
        AppendMenuW(hTabSub, MF_STRING, CMD_NEXT_TAB,         L"Cambiar a siguiente pesta\u00F1""a (Ctrl+Tab)");
        AppendMenuW(hTabSub, MF_STRING, CMD_TOGGLE_SPLIT_VIEW, st->splitViewActive ? L"Cerrar Vista Dividida" : L"Vista Dividida Lado a Lado");
        AppendMenuW(hTabSub, MF_STRING, CMD_TOGGLE_SPREAD,    st->doc.DoublePageSpread() ? L"Vista de P\u00E1gina \u00DAnica" : L"Vista Doble P\u00E1gina (Libro)");
        AppendMenuW(hm, MF_POPUP, reinterpret_cast<UINT_PTR>(hTabSub), L"Pesta\u00F1""as y Vista");
        HMENU hPdfSub = CreatePopupMenu();
        AppendMenuW(hPdfSub, MF_STRING, CMD_MERGE_PDF,            L"Combinar PDFs...");
        AppendMenuW(hPdfSub, MF_STRING, CMD_SPLIT_PDF,            L"Dividir / Extraer p\u00E1ginas...");
        AppendMenuW(hPdfSub, MF_STRING, CMD_IMPORT_PDF_TO_CANVAS, L"Importar p\u00E1ginas de PDF al Lienzo...");
        AppendMenuW(hPdfSub, MF_STRING, CMD_VIEW_ATTACHMENTS,     L"Ver adjuntos incrustados en PDF...");
        AppendMenuW(hPdfSub, MF_STRING, CMD_SIGN_PDF,             L"Firmar PDF (PKCS#7)...");
        AppendMenuW(hPdfSub, MF_STRING, CMD_VERIFY_SIG,           L"Verificar firma digital...");
        AppendMenuW(hPdfSub, MF_STRING, CMD_RUN_OCR,              L"Reconocer texto (OCR offline)");
        AppendMenuW(hm, MF_POPUP, reinterpret_cast<UINT_PTR>(hPdfSub), L"Herramientas PDF");
        HMENU hOffSub = CreatePopupMenu();
        AppendMenuW(hOffSub, MF_STRING, CMD_EXPORT_DOCX, L"Exportar Word (.docx)...");
        AppendMenuW(hOffSub, MF_STRING, CMD_EXPORT_PPTX, L"Exportar PowerPoint (.pptx)...");
        AppendMenuW(hOffSub, MF_STRING, CMD_IMPORT_DOCX, L"Importar Word (.docx)...");
        AppendMenuW(hOffSub, MF_STRING, CMD_IMPORT_PPTX, L"Importar PowerPoint (.pptx)...");
        AppendMenuW(hm, MF_POPUP, reinterpret_cast<UINT_PTR>(hOffSub), L"Office (.docx / .pptx)");
        HMENU hSecSub = CreatePopupMenu();
        AppendMenuW(hSecSub, MF_STRING, CMD_SAVE_ENCRYPTED,   L"Guardar Cuaderno Cifrado (AES-256-GCM)...");
        AppendMenuW(hSecSub, MF_STRING, CMD_OPEN_ENCRYPTED,   L"Abrir Cuaderno Cifrado...");
        AppendMenuW(hSecSub, MF_STRING, CMD_VERSION_HISTORY,  L"Guardar / Ver Historial de Versiones...");
        AppendMenuW(hSecSub, MF_STRING, CMD_CONFLICT_MERGE,   L"Fusionar Cuaderno Concurrente sin P\u00E9rdida...");
        AppendMenuW(hm, MF_POPUP, reinterpret_cast<UINT_PTR>(hSecSub), L"Versiones y Cifrado");
        AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hm, MF_STRING, CMD_INSERT_IMAGE, L"Insertar imagen (PNG/JPG/SVG/WEBP/TIFF)...");
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
        HMENU hInkSub = CreatePopupMenu();
        AppendMenuW(hInkSub, MF_STRING, CMD_CYCLE_PEN_STYLE,       L"Cambiar estilo de l\u00E1piz (Bol\u00EDgrafo / Pluma / L\u00E1piz / Pincel)");
        AppendMenuW(hInkSub, MF_STRING, CMD_CUSTOM_INK_COLOR,      L"Color personalizado de tinta...");
        AppendMenuW(hInkSub, MF_STRING, CMD_TOGGLE_PARTIAL_ERASER, st->eraserActionMode == EraserActionMode::PartialPixel ? L"Borrador: Cambiar a Trazo Completo" : L"Borrador: Cambiar a Precisi\u00F3n / P\u00EDxel");
        AppendMenuW(hInkSub, MF_STRING, CMD_CYCLE_ERASER_FILTER,   L"Filtro de borrador (Todo / Solo Resaltador / Solo Tinta)");
        AppendMenuW(hInkSub, MF_STRING, CMD_CLEAR_HIGHLIGHTS,      L"Limpiar todos los resaltados");
        AppendMenuW(hInkSub, MF_STRING, CMD_TOGGLE_STAB,           L"Estabilizaci\u00F3n de trazo");
        AppendMenuW(hInkSub, MF_STRING, CMD_TOGGLE_SNAP_GRID,      st->snapToGrid ? L"Desactivar ajuste a cuadr\u00EDcula" : L"Activar ajuste a cuadr\u00EDcula");
        AppendMenuW(hInkSub, MF_STRING, CMD_CYCLE_GEOMETRY_TOOL,   L"Herramienta de geometr\u00ED""a (Regla / Transportador / Comp\u00E1s)");
        AppendMenuW(hInkSub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hInkSub, MF_STRING, CMD_COPY_STROKE_STYLE,     L"Copiar estilo del trazo seleccionado");
        AppendMenuW(hInkSub, MF_STRING, CMD_RESTYLE_INK,           L"Enderezar y estilizar tinta seleccionada");
        AppendMenuW(hInkSub, MF_STRING, CMD_HTR_TO_TEXT,           L"Convertir escritura a texto editable (HTR)");
        AppendMenuW(hInkSub, MF_STRING, CMD_HTR_TO_MATH,           L"Convertir escritura a ecuaci\u00F3n 2D (HTR)");
        AppendMenuW(hm, MF_POPUP, reinterpret_cast<UINT_PTR>(hInkSub), L"Tinta y Escritura");

        HMENU hObjSub = CreatePopupMenu();
        AppendMenuW(hObjSub, MF_STRING, CMD_CLIPBOARD_PASTE,        L"Pegar imagen o texto (Ctrl+V)");
        AppendMenuW(hObjSub, MF_STRING, CMD_GROUP_OBJECTS,          L"Agrupar selecci\u00F3n (Ctrl+G)");
        AppendMenuW(hObjSub, MF_STRING, CMD_UNGROUP_OBJECTS,        L"Desagrupar selecci\u00F3n (Ctrl+Shift+G)");
        AppendMenuW(hObjSub, MF_STRING, CMD_DUPLICATE_OBJECTS,      L"Duplicar selecci\u00F3n (Ctrl+D)");
        AppendMenuW(hObjSub, MF_STRING, CMD_ADD_CONNECTOR,          L"Insertar conector magn\u00E9tico");
        AppendMenuW(hObjSub, MF_STRING, CMD_ADD_REDACTION,          L"Insertar censura PDF permanente");
        AppendMenuW(hObjSub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hObjSub, MF_STRING, CMD_TEXT_BOLD,              L"Texto: Negrita (Ctrl+B)");
        AppendMenuW(hObjSub, MF_STRING, CMD_TEXT_ITALIC,            L"Texto: Cursiva (Ctrl+I)");
        AppendMenuW(hObjSub, MF_STRING, CMD_TEXT_UNDERLINE,         L"Texto: Subrayado (Ctrl+U)");
        AppendMenuW(hObjSub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hObjSub, MF_STRING, CMD_IMG_ENHANCE_WHITEBOARD, L"Imagen: Mejorar contraste de pizarra");
        AppendMenuW(hObjSub, MF_STRING, CMD_IMG_REMOVE_BG,          L"Imagen: Quitar fondo blanco");
        AppendMenuW(hObjSub, MF_STRING, CMD_IMG_ROTATE_90,          L"Imagen: Rotar +90\u00B0");
        AppendMenuW(hObjSub, MF_STRING, CMD_IMG_TOGGLE_OPACITY,     L"Imagen: Alternar opacidad 55% / 100%");
        AppendMenuW(hObjSub, MF_STRING, CMD_IMG_CROP,               L"Imagen: Recortar m\u00E1rgenes");
        AppendMenuW(hObjSub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hObjSub, MF_STRING, CMD_STICKER_OPAMP,          L"Elemento: Amplificador Op-Amp");
        AppendMenuW(hObjSub, MF_STRING, CMD_STICKER_RESISTOR,       L"Elemento: Resistencia vectorial");
        AppendMenuW(hObjSub, MF_STRING, CMD_STICKER_AXES,           L"Elemento: Ejes XY");
        AppendMenuW(hObjSub, MF_STRING, CMD_STICKER_NOTE,           L"Elemento: Nota adhesiva");
        AppendMenuW(hm, MF_POPUP, reinterpret_cast<UINT_PTR>(hObjSub), L"Objetos, Texto e Im\u00E1genes");

        HMENU hPaperSub = CreatePopupMenu();
        AppendMenuW(hPaperSub, MF_STRING, CMD_PAPER_NEXT,       L"Siguiente plantilla de papel");
        AppendMenuW(hPaperSub, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hPaperSub, MF_STRING, CMD_PAPER_ISOMETRIC,  L"Papel Isom\u00E9trico 30\u00B0");
        AppendMenuW(hPaperSub, MF_STRING, CMD_PAPER_LOGLOG,     L"Papel Log-Log");
        AppendMenuW(hPaperSub, MF_STRING, CMD_PAPER_POLAR,      L"Papel Polar");
        AppendMenuW(hPaperSub, MF_STRING, CMD_PAPER_SMITH,      L"Carta de Smith");
        AppendMenuW(hPaperSub, MF_STRING, CMD_PAPER_CORNELL,    L"Plantilla Cornell");
        AppendMenuW(hPaperSub, MF_STRING, CMD_PAPER_MUSIC,      L"Pentagrama musical");
        AppendMenuW(hm, MF_POPUP, reinterpret_cast<UINT_PTR>(hPaperSub), L"Plantillas de Papel");

        AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hm, MF_STRING, CMD_STEM_MENU,       L"Ingenier\u00EDa STEM (+F\u00F3rmula / Circuito / Gr\u00E1fica)...");
        AppendMenuW(hm, MF_STRING, CMD_STUDY_MENU,      L"Estudio SM-2 / Capas / Audio...");
        AppendMenuW(hm, MF_STRING, CMD_TOGGLE_ACADEMIC, L"Panel Acad\u00E9mico (TEC Digital y HorariosTEC)");
        AppendMenuW(hm, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hm, MF_STRING, CMD_ADD_PAGE,        L"A\u00F1adir p\u00E1gina");
        AppendMenuW(hm, MF_STRING, CMD_ROTATE_PAGE,     L"Rotar p\u00E1gina 90\u00B0");
        AppendMenuW(hm, MF_STRING, CMD_DEL_PAGE,        L"Eliminar p\u00E1gina actual");
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
    case CMD_MIGRATE_NOTEBOOK: {
        if (GuiDialogs::RunNotebookMigrationDialog(st->hwndMain, st->doc, st->audioSession, st->vault, st->statusBannerUtf8)) {
            st->cmdStack.Clear();
            st->pageCache.InvalidateAll(++st->asyncGen);
            st->currentDocPath.clear();
            CenterCameraOnPage(st, 0);
            LayoutMainChildren(st);
        }
        break;
    }
    case CMD_OPEN_VAULT_LIBRARY:
        GuiDialogs::RunVaultLibraryManager(st->hwndMain, st->doc, st->vault, st->ftsIndex, st->currentDocPath, MakePromptAdapter(st), st->statusBannerUtf8);
        break;
    case CMD_SAVE_ENCRYPTED:
        GuiDialogs::RunSecurityOrLanFlow(st->hwndMain, st->doc, 0, MakePromptAdapter(st), st->statusBannerUtf8);
        break;
    case CMD_OPEN_ENCRYPTED:
        GuiDialogs::RunSecurityOrLanFlow(st->hwndMain, st->doc, 1, MakePromptAdapter(st), st->statusBannerUtf8);
        st->cmdStack.Clear();
        st->pageCache.InvalidateAll(++st->asyncGen);
        CenterCameraOnPage(st, 0);
        LayoutMainChildren(st);
        break;
    case CMD_VERSION_HISTORY:
        GuiDialogs::RunVersionHistoryOrMerge(st->hwndMain, st->doc, st->versionHistory, st->activePageIndex, false, st->statusBannerUtf8);
        break;
    case CMD_CONFLICT_MERGE:
        GuiDialogs::RunVersionHistoryOrMerge(st->hwndMain, st->doc, st->versionHistory, st->activePageIndex, true, st->statusBannerUtf8);
        st->pageCache.InvalidateAll(++st->asyncGen);
        break;
    case CMD_VIEW_ATTACHMENTS: {
        std::ostringstream os;
        os << "Adjuntos incrustados en el documento (" << st->doc.Attachments().size() << "):\r\n\r\n";
        for (const auto& a : st->doc.Attachments()) {
            os << "  - " << a.filenameUtf8 << " (" << a.mimeTypeUtf8 << ", " << a.sizeBytes << " bytes)\r\n";
        }
        if (st->doc.Attachments().empty()) os << "  (Este documento no contiene archivos adjuntos incrustados.)";
        GuiDialogs::ShowReportDialog(st->hwndMain, L"Adjuntos del Documento", Utf8ToWide(os.str()));
        break;
    }
    case CMD_QUICK_NOTE:
        AddNewDocTab(st, true);
        break;
    case CMD_NEW_TAB:
        AddNewDocTab(st, false);
        break;
    case CMD_NEXT_TAB:
        if (st->tabs.size() > 1) SwitchToDocTab(st, st->activeTabIdx + 1);
        else st->statusBannerUtf8 = "Abra otra pesta\xC3\xB1""a con Ctrl+T o Nota R\xC3\xA1pida";
        break;
    case CMD_TOGGLE_SPREAD:
        st->doc.SetDoublePageSpread(!st->doc.DoublePageSpread());
        st->statusBannerUtf8 = st->doc.DoublePageSpread() ? "Vista Doble P\xC3\xA1gina (Libro) activa" : "Vista P\xC3\xA1gina \xC3\x9Anica activa";
        break;
    case CMD_TOGGLE_SPLIT_VIEW:
        st->splitViewActive = !st->splitViewActive;
        if (st->splitViewActive) {
            st->splitCamera = st->camera;
            st->splitCamera.panY += 420.0;
        }
        st->statusBannerUtf8 = st->splitViewActive ? "Vista Dividida lado a lado activada" : "Vista Dividida cerrada";
        break;
    case CMD_OPEN_PROJECTOR: {
        ViewportOverlayState ov{};
        PopulateViewportOverlay(st, ov);
        if (st->projectorWnd.Open(GetModuleHandleW(nullptr), st->hwndMain, &st->doc, &st->camera, &st->pageCache, &ov)) {
            st->statusBannerUtf8 = "Ventana de Proyector activa (capas privadas ocultas; tecla F para congelar)";
        }
        break;
    }
    case CMD_FREEZE_PROJECTOR:
        if (st->projectorWnd.IsOpen()) {
            st->projectorWnd.SetFrozen(!st->projectorWnd.IsFrozen());
            st->statusBannerUtf8 = st->projectorWnd.IsFrozen() ? "Proyector CONGELADO (puedes editar en privado)" : "Proyector descongelado en vivo";
        } else {
            st->statusBannerUtf8 = "Abra primero la Ventana de Proyector";
        }
        break;
    case CMD_IMPORT_PDF_TO_CANVAS: {
        static constexpr COMDLG_FILTERSPEC kPdfF[] = { { L"Archivos PDF (*.pdf)", L"*.pdf" } };
        std::vector<std::wstring> paths;
        if (!ShowOpenDialog(st->hwndMain, L"Importar p\u00E1ginas de PDF al Lienzo", kPdfF, 1, paths, false)) break;
        PdfDocument pdf;
        std::string err;
        if (!pdf.LoadFromFile(paths[0], &err)) {
            st->statusBannerUtf8 = "Error PDF: " + err;
            break;
        }
        if (st->doc.Mode() != DocumentMode::InfiniteCanvas) {
            st->doc.SetMode(DocumentMode::InfiniteCanvas);
            LayoutMainChildren(st);
        }
        const std::size_t nImport = (std::min<std::size_t>)(pdf.PageCount(), 6u);
        double cx = 60.0, cy = 60.0;
        for (std::size_t p = 0; p < nImport; ++p) {
            RasterImageBgra32 img{};
            std::vector<std::uint8_t> pngBytes;
            if (WinRtPdfRenderer::RenderPageToBgra32(paths[0], static_cast<int>(p), 720, 0, {}, img, nullptr, nullptr) &&
                WinRtPdfRenderer::EncodeBgra32ToPngBytes(img, pngBytes)) {
                AnnotationObject pic{};
                pic.kind = ObjectKind::Image;
                pic.layerId = st->doc.ActiveLayerId();
                pic.bounds = { cx, cy, 420.0, 420.0 * (static_cast<double>(img.height) / (std::max)(1, img.width)) };
                pic.pngBytes = std::move(pngBytes);
                pic.captionUtf8 = "PDF P\xC3\xA1g. " + std::to_string(p + 1);
                st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(ObjectTarget{ true, 0 }, std::move(pic)));
                cx += 456.0;
            }
        }
        st->statusBannerUtf8 = "Importadas " + std::to_string(nImport) + " p\xC3\xA1gina(s) del PDF al Lienzo Infinito";
        break;
    }
    case CMD_TOGGLE_PARTIAL_ERASER:
        st->eraserActionMode = (st->eraserActionMode == EraserActionMode::WholeStroke)
            ? EraserActionMode::PartialPixel : EraserActionMode::WholeStroke;
        st->activeTool = ToolKind::Eraser;
        st->statusBannerUtf8 = (st->eraserActionMode == EraserActionMode::PartialPixel)
            ? "Borrador de Precisi\xC3\xB3n / P\xC3\xADxel activo (divide trazos)"
            : "Borrador de Trazo Completo activo";
        break;
    case CMD_CYCLE_PEN_STYLE: {
        const int next = (static_cast<int>(st->penStyle) + 1) % 4;
        st->penStyle = static_cast<InkPenStyle>(next);
        static const char* kStyleNames[4] = { "Bol\xC3\xADgrafo", "Pluma Fuente", "L\xC3\xA1piz Grafito", "Pincel Caligr\xC3\xA1" "fico" };
        st->activeTool = ToolKind::Pen;
        st->statusBannerUtf8 = std::string("Estilo de l\xC3\xA1piz: ") + kStyleNames[next];
        break;
    }
    case CMD_CUSTOM_INK_COLOR: {
        static COLORREF customPalette[16] = {
            RGB(26,115,232), RGB(220,38,38), RGB(21,128,61), RGB(245,158,11),
            RGB(124,58,237), RGB(14,116,144), RGB(190,24,93), RGB(17,24,39),
            RGB(71,85,105), RGB(234,88,12), RGB(37,99,235), RGB(5,150,105),
            RGB(168,85,247), RGB(217,119,6), RGB(225,29,72), RGB(15,23,42)
        };
        CHOOSECOLORW cc{};
        cc.lStructSize = sizeof(cc);
        cc.hwndOwner = st->hwndMain;
        cc.rgbResult = RGB(st->penColor.r, st->penColor.g, st->penColor.b);
        cc.lpCustColors = customPalette;
        cc.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (ChooseColorW(&cc)) {
            st->penColor = DocColor::RGBA(GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult), 255);
            st->statusBannerUtf8 = "Color de tinta personalizado aplicado";
        }
        break;
    }
    case CMD_TOGGLE_SNAP_GRID:
        st->snapToGrid = !st->snapToGrid;
        st->statusBannerUtf8 = st->snapToGrid ? "Ajuste a cuadr\xC3\xAD" "cula ACTIVADO" : "Ajuste a cuadr\xC3\xAD" "cula desactivado";
        break;
    case CMD_CYCLE_GEOMETRY_TOOL: {
        if (!st->rulerState.visible) {
            RECT rc{};
            GetClientRect(st->hwndViewport, &rc);
            const DocPoint cW = st->camera.ScreenToWorld((rc.right - rc.left) * 0.5, (rc.bottom - rc.top) * 0.55);
            st->rulerState.centerX = cW.x;
            st->rulerState.centerY = cW.y;
            st->rulerState.mode = RulerMode::Straightedge;
            st->rulerState.visible = true;
            st->statusBannerUtf8 = "Regla Recta activa";
        } else if (st->rulerState.mode == RulerMode::Straightedge) {
            st->rulerState.mode = RulerMode::Protractor;
            st->statusBannerUtf8 = "Transportador 180\xC2\xB0 activo (traza arcos precisos)";
        } else if (st->rulerState.mode == RulerMode::Protractor) {
            st->rulerState.mode = RulerMode::Compass;
            st->rulerState.compassRadiusPt = 110.0;
            st->statusBannerUtf8 = "Comp\xC3\xA1s geom\xC3\xA9trico activo (radio 110 pt)";
        } else {
            st->rulerState.visible = false;
            st->rulerState.mode = RulerMode::Straightedge;
            st->statusBannerUtf8 = "Herramienta geom\xC3\xA9trica oculta";
        }
        break;
    }
    case CMD_COPY_STROKE_STYLE: {
        if (const auto* o = CanvasManip::FindObjectConst(st->doc, CurrentTarget(st), st->selectedObjectId)) {
            st->penColor = o->strokeColor;
            st->penWidth = std::clamp(o->strokeWidth, 0.8, 24.0);
            st->penStyle = o->penStyle;
            st->statusBannerUtf8 = "Estilo del objeto copiado al l\xC3\xA1piz activo";
        } else {
            st->statusBannerUtf8 = "Seleccione un trazo u objeto para copiar su estilo";
        }
        break;
    }
    case CMD_RESTYLE_INK: {
        const ObjectTarget tgt = CurrentTarget(st);
        std::vector<std::uint64_t> ids = st->lassoSelectedIds;
        if (ids.empty() && st->selectedObjectId != 0) ids.push_back(st->selectedObjectId);
        const bool ok = InkEngine::NeatenAndRestyleStrokes(st->doc, tgt, ids);
        st->statusBannerUtf8 = ok
            ? ("Estilizados y enderezados " + std::to_string(ids.size()) + " trazo(s)")
            : "Seleccione o encierre con Lazo los trazos a estilizar";
        break;
    }
    case CMD_HTR_TO_TEXT:
    case CMD_HTR_TO_MATH: {
        std::vector<std::uint64_t> ids = st->lassoSelectedIds;
        if (ids.empty() && st->selectedObjectId != 0) ids.push_back(st->selectedObjectId);
        GuiDialogs::RunOfflineInkHtr(st->hwndMain, st->doc, st->cmdStack, CurrentTarget(st), ids, (id == CMD_HTR_TO_MATH), st->statusBannerUtf8);
        break;
    }
    case CMD_GROUP_OBJECTS:
    case CMD_UNGROUP_OBJECTS: {
        std::vector<std::uint64_t> ids = st->lassoSelectedIds;
        if (ids.empty() && st->selectedObjectId != 0) ids.push_back(st->selectedObjectId);
        if (CanvasManip::GroupSelectedObjects(st->doc, st->cmdStack, CurrentTarget(st), ids, id == CMD_UNGROUP_OBJECTS)) {
            st->statusBannerUtf8 = (id == CMD_UNGROUP_OBJECTS) ? "Objetos desagrupados" : ("Agrupados " + std::to_string(ids.size()) + " objetos");
        } else {
            st->statusBannerUtf8 = "Seleccione uno o m\xC3\xA1s objetos primero";
        }
        break;
    }
    case CMD_DUPLICATE_OBJECTS: {
        std::vector<std::uint64_t> ids = st->lassoSelectedIds;
        if (ids.empty() && st->selectedObjectId != 0) ids.push_back(st->selectedObjectId);
        const auto dup = CanvasManip::DuplicateSelectedObjects(st->doc, st->cmdStack, CurrentTarget(st), ids);
        if (!dup.empty()) {
            st->lassoSelectedIds = dup;
            st->selectedObjectId = dup.back();
            st->statusBannerUtf8 = "Duplicado(s) " + std::to_string(dup.size()) + " objeto(s)";
        }
        break;
    }
    case CMD_CLIPBOARD_PASTE: {
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        if (!CanvasManip::PasteClipboardIntoDocument(st->hwndMain, st->doc, st->cmdStack, CurrentTarget(st), x, y, st->statusBannerUtf8)) {
            st->statusBannerUtf8 = "No hay imagen ni texto compatible en el portapapeles";
        }
        break;
    }
    case CMD_ADD_REDACTION: {
        double x = 90.0, y = 120.0;
        NextCardPlacement(st, x, y);
        AnnotationObject red{};
        red.kind = ObjectKind::Shape;
        red.shapeKind = ShapeKind::Rectangle;
        red.layerId = st->doc.ActiveLayerId();
        red.bounds = { x, y, 220.0, 32.0 };
        red.isRedaction = true;
        red.fillColor = DocColor::RGBA(0, 0, 0, 255);
        red.strokeColor = DocColor::RGBA(0, 0, 0, 255);
        st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(CurrentTarget(st), std::move(red)));
        st->statusBannerUtf8 = "Censura PDF permanente insertada (quema caja negra al exportar)";
        break;
    }
    case CMD_ADD_CONNECTOR: {
        const ObjectTarget tgt = CurrentTarget(st);
        const auto& vec = tgt.isCanvas ? st->doc.CanvasObjects() : st->doc.Pages()[tgt.pageIndex].objects;
        AnnotationObject conn{};
        conn.kind = ObjectKind::Shape;
        conn.shapeKind = ShapeKind::Connector;
        conn.layerId = st->doc.ActiveLayerId();
        conn.strokeColor = st->penColor;
        conn.strokeWidth = 2.0;
        if (vec.size() >= 2) {
            const auto& a = vec[vec.size() - 2];
            const auto& b = vec[vec.size() - 1];
            conn.connectorFromId = a.id;
            conn.connectorToId = b.id;
            conn.shapeStart = { a.bounds.x + a.bounds.w * 0.5, a.bounds.y + a.bounds.h * 0.5 };
            conn.shapeEnd = { b.bounds.x + b.bounds.w * 0.5, b.bounds.y + b.bounds.h * 0.5 };
        } else {
            conn.shapeStart = { 120.0, 140.0 };
            conn.shapeEnd = { 320.0, 210.0 };
        }
        conn.RecomputeBounds();
        st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(tgt, std::move(conn)));
        st->statusBannerUtf8 = "Conector magn\xC3\xA9tico enlazado";
        break;
    }
    case CMD_IMPORT_IMAGE_MULTI: {
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        GuiDialogs::RunMultiFormatImageImport(st->hwndMain, st->doc, st->cmdStack, CurrentTarget(st), x, y, st->statusBannerUtf8);
        break;
    }
    case CMD_IMG_ENHANCE_WHITEBOARD:
    case CMD_IMG_REMOVE_BG:
    case CMD_IMG_ROTATE_90:
    case CMD_IMG_TOGGLE_OPACITY:
    case CMD_IMG_CROP: {
        const int m = (id == CMD_IMG_ENHANCE_WHITEBOARD) ? 0
            : (id == CMD_IMG_REMOVE_BG) ? 1
            : (id == CMD_IMG_ROTATE_90) ? 2
            : (id == CMD_IMG_TOGGLE_OPACITY) ? 3 : 4;
        GuiDialogs::EnhanceSelectedImage(st->doc, st->cmdStack, CurrentTarget(st), st->selectedObjectId, m, st->statusBannerUtf8);
        break;
    }
    case CMD_STICKER_OPAMP:
    case CMD_STICKER_RESISTOR:
    case CMD_STICKER_AXES:
    case CMD_STICKER_NOTE: {
        double x = 90.0, y = 90.0;
        NextCardPlacement(st, x, y);
        const int sk = (id == CMD_STICKER_OPAMP) ? 0 : (id == CMD_STICKER_RESISTOR) ? 1 : (id == CMD_STICKER_AXES) ? 2 : 3;
        GuiDialogs::InsertEngineeringSticker(st->doc, st->cmdStack, CurrentTarget(st), x, y, sk, st->statusBannerUtf8);
        break;
    }
    case CMD_PAPER_ISOMETRIC:
    case CMD_PAPER_LOGLOG:
    case CMD_PAPER_POLAR:
    case CMD_PAPER_SMITH:
    case CMD_PAPER_CORNELL:
    case CMD_PAPER_MUSIC: {
        const PaperPattern pat = (id == CMD_PAPER_ISOMETRIC) ? PaperPattern::Isometric
            : (id == CMD_PAPER_LOGLOG) ? PaperPattern::LogLog
            : (id == CMD_PAPER_POLAR) ? PaperPattern::Polar
            : (id == CMD_PAPER_SMITH) ? PaperPattern::SmithChart
            : (id == CMD_PAPER_CORNELL) ? PaperPattern::Cornell : PaperPattern::MusicStaff;
        st->cmdStack.Execute(st->doc, std::make_unique<SetBackgroundCommand>(
            ObjectTarget{ st->doc.Mode() == DocumentMode::InfiniteCanvas, st->activePageIndex }, pat));
        st->statusBannerUtf8 = std::string("Plantilla aplicada: ") + PaperPatternLabelUtf8(pat);
        break;
    }
    case CMD_AUDIO_BOOKMARK: {
        std::wstring lbl = L"Explicaci\u00F3n clave";
        if (ShowTextPrompt(st->hwndMain, L"Marcador de Audio", L"Etiqueta del marcador temporal:", lbl) && !lbl.empty()) {
            const std::uint64_t tMs = st->replayActive ? st->replayCursorMs : st->audioSession.DurationMs();
            st->audioSession.AddBookmark(WideToUtf8(lbl), tMs);
            st->statusBannerUtf8 = "Marcador de audio guardado en " + std::to_string(tMs / 1000) + " s";
        }
        break;
    }
    case CMD_AUDIO_IMPORT: {
        static constexpr COMDLG_FILTERSPEC kW[] = { { L"Audio WAV PCM (*.wav)", L"*.wav" } };
        std::vector<std::wstring> paths;
        if (ShowOpenDialog(st->hwndMain, L"Importar Audio WAV", kW, 1, paths, false)) {
            std::ifstream ifs(std::filesystem::path(paths[0]), std::ios::binary | std::ios::ate);
            if (ifs.is_open()) {
                const auto sz = ifs.tellg();
                std::vector<std::uint8_t> buf(static_cast<std::size_t>(sz));
                ifs.seekg(0, std::ios::beg);
                ifs.read(reinterpret_cast<char*>(buf.data()), sz);
                if (st->audioSession.LoadFromWavBytes(buf.data(), buf.size())) {
                    st->statusBannerUtf8 = "Audio WAV importado (" + std::to_string(st->audioSession.DurationMs() / 1000) + " s)";
                }
            }
        }
        break;
    }
    case CMD_AUDIO_TRANSCRIBE: {
        const auto segs = st->audioSession.TranscribeOffline();
        std::ostringstream os;
        os << "Segmentos de voz detectados offline (" << segs.size() << "):\r\n\r\n";
        for (const auto& s : segs) {
            os << "  [" << (s.startMs / 1000.0) << "s - " << (s.endMs / 1000.0) << "s] " << s.textUtf8 << "\r\n";
        }
        if (segs.empty()) os << "  (Grabe o importe audio WAV primero para segmentar voz offline.)";
        GuiDialogs::ShowReportDialog(st->hwndMain, L"Transcripci\u00F3n y Segmentos de Audio Offline", Utf8ToWide(os.str()));
        st->statusBannerUtf8 = "Transcripci\xC3\xB3n offline: " + std::to_string(segs.size()) + " segmento(s)";
        break;
    }
    case CMD_AUDIO_EXPORT_HTML5: {
        static constexpr COMDLG_FILTERSPEC kH[] = { { L"Tutorial Interactivo HTML5 (*.html)", L"*.html" } };
        std::wstring outPath;
        if (ShowSaveDialog(st->hwndMain, L"Exportar Tutorial HTML5 Sincronizado", kH, 1, L"html", L"tutorial_cuadernu.html", outPath)) {
            std::string err;
            if (st->audioSession.ExportSyncedTutorialHtml5(st->doc, WideToUtf8(outPath), &err)) {
                st->statusBannerUtf8 = "Tutorial HTML5 sincronizado exportado";
            } else {
                st->statusBannerUtf8 = "Error HTML5: " + err;
            }
        }
        break;
    }
    case CMD_ADD_TABLE: {
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        GuiDialogs::RunReactiveTableDialog(st->hwndMain, st->doc, st->cmdStack, CurrentTarget(st), x, y, MakePromptAdapter(st), st->statusBannerUtf8);
        break;
    }
    case CMD_ADD_CODE_BLOCK: {
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        GuiDialogs::RunEngineeringToolboxOrCodeBlock(st->hwndMain, st->doc, st->cmdStack, CurrentTarget(st), x, y, 2, MakePromptAdapter(st), st->statusBannerUtf8);
        break;
    }
    case CMD_ADV_MATH: {
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        GuiDialogs::RunAdvancedMathDialog(st->hwndMain, st->doc, st->cmdStack, CurrentTarget(st), x, y, MakePromptAdapter(st), st->statusBannerUtf8);
        break;
    }
    case CMD_CHECK_MY_WORK:
    case CMD_TEACH_ME:
    case CMD_SUMMARIZE_NOTES:
    case CMD_LOCAL_QA:
    case CMD_COMPARE_4_CASIO: {
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const int m = (id == CMD_CHECK_MY_WORK) ? 0
            : (id == CMD_TEACH_ME) ? 1
            : (id == CMD_SUMMARIZE_NOTES) ? 2
            : (id == CMD_LOCAL_QA) ? 3 : 4;
        GuiDialogs::RunCheckMyWorkOrTeachMe(st->hwndMain, st->doc, st->cmdStack, CurrentTarget(st), x, y, m, MakePromptAdapter(st), st->statusBannerUtf8);
        break;
    }
    case CMD_RECOGNIZE_CIRCUIT:
    case CMD_ENG_TOOLBOX:
    case CMD_FORMULA_AST_SEARCH: {
        double x = 80.0, y = 80.0;
        NextCardPlacement(st, x, y);
        const int m = (id == CMD_RECOGNIZE_CIRCUIT) ? 0 : (id == CMD_ENG_TOOLBOX) ? 1 : 3;
        GuiDialogs::RunEngineeringToolboxOrCodeBlock(st->hwndMain, st->doc, st->cmdStack, CurrentTarget(st), x, y, m, MakePromptAdapter(st), st->statusBannerUtf8);
        break;
    }
    case CMD_LAN_QR:
        GuiDialogs::RunSecurityOrLanFlow(st->hwndMain, st->doc, 2, MakePromptAdapter(st), st->statusBannerUtf8);
        break;
    case CMD_EXAM_PIN_LOCK: {
        std::wstring pinW;
        if (!st->examModeLocked) {
            if (ShowTextPrompt(st->hwndMain, L"Activar Modo Examen", L"Establezca un PIN de 4 d\u00EDgitos para bloquear funciones asistidas:", pinW) && !pinW.empty()) {
                st->examPinUtf8 = WideToUtf8(pinW);
                st->examModeLocked = true;
                st->statusBannerUtf8 = "Modo Examen ACTIVADO (bloqueado por PIN)";
            }
        } else {
            if (ShowTextPrompt(st->hwndMain, L"Desbloquear Modo Examen", L"Ingrese el PIN para desactivar el Modo Examen:", pinW)) {
                if (WideToUtf8(pinW) == st->examPinUtf8) {
                    st->examModeLocked = false;
                    st->examPinUtf8.clear();
                    st->statusBannerUtf8 = "Modo Examen desactivado";
                } else {
                    st->statusBannerUtf8 = "PIN incorrecto";
                }
            }
        }
        break;
    }
    case CMD_TEXT_BOLD:
    case CMD_TEXT_ITALIC:
    case CMD_TEXT_UNDERLINE: {
        const ObjectTarget tgt = CurrentTarget(st);
        if (AnnotationObject* obj = CanvasManip::FindObjectMut(st->doc, tgt, st->selectedObjectId)) {
            if (obj->kind == ObjectKind::TextBox) {
                AnnotationObject before = *obj;
                AnnotationObject after = *obj;
                if (id == CMD_TEXT_BOLD) after.bold = !after.bold;
                else if (id == CMD_TEXT_ITALIC) after.italic = !after.italic;
                else after.underline = !after.underline;
                st->cmdStack.Execute(st->doc, std::make_unique<ModifyObjectCommand>(tgt, std::move(before), std::move(after)));
                st->statusBannerUtf8 = "Formato de texto actualizado";
            }
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

            // Side-by-side Split View right pane composited smoothly via Direct2D offscreen pass (F-066, F-067)
            RasterImageBgra32 splitPatch;
            if (st->splitViewActive && !st->appearancePopoverOpen && vw > 240 && vh > 80) {
                const int halfW = vw / 2;
                const int rightW = vw - halfW;
                ViewportOverlayState sov = ov;
                sov.floatingOverlayBgra = nullptr;
                sov.showSelectionHandles = false;
                if (st->renderer.RenderHwndViewportToOffscreen(st->doc, st->splitCamera, rightW, vh, &st->pageCache, splitPatch, &sov) &&
                    splitPatch.Valid()) {
                    // Subtle 2px vertical splitter line on left edge of split pane
                    for (int y = 0; y < vh; ++y) {
                        for (int x = 0; x < 2 && x < rightW; ++x) {
                            std::uint8_t* p = &splitPatch.pixels[(static_cast<std::size_t>(y) * rightW + x) * 4u];
                            p[0] = 225; p[1] = 175; p[2] = 130; p[3] = 255;
                        }
                    }
                    ov.floatingOverlayBgra = &splitPatch;
                    ov.floatingOverlayScreenRect = { static_cast<double>(halfW), 0.0, static_cast<double>(rightW), static_cast<double>(vh) };
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
    case WM_COMMAND: {
        if (st && LOWORD(wp) == CMD_INLINE_EDIT_CTRL && HIWORD(wp) == EN_KILLFOCUS) {
            CommitInlineTextEdit(st, true);
            return 0;
        }
        break;
    }
    case WM_LBUTTONDBLCLK: {
        if (!st) break;
        CommitInlineTextEdit(st, true);
        const DocPoint wpWorld = st->camera.ScreenToWorld(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
        bool isCanvas = (st->doc.Mode() == DocumentMode::InfiniteCanvas);
        std::size_t pgIdx = st->activePageIndex;
        double lx = wpWorld.x, ly = wpWorld.y;
        if (!isCanvas && !st->doc.Layout().HitTestWorldPoint(wpWorld.x, wpWorld.y, pgIdx, lx, ly)) return 0;
        const double tol = std::clamp(8.0 / (std::max)(0.25, st->camera.zoom), 3.0, 18.0);
        const auto hits = CollectHits(st, isCanvas, pgIdx, lx, ly, tol);
        if (hits.empty()) return 0;
        const ObjectTarget tgt{ isCanvas, pgIdx };
        const AnnotationObject* obj = CanvasManip::FindObjectConst(st->doc, tgt, hits.back());
        if (!obj) return 0;
        st->selectedObjectId = obj->id;
        st->selectedIsCanvas = isCanvas;
        st->selectedPageIndex = pgIdx;
        if (obj->kind == ObjectKind::TextBox) {
            BeginInlineTextEdit(st, tgt, *obj);
        } else if (obj->kind == ObjectKind::MathBlock) {
            std::wstring exprW = Utf8ToWide(obj->formulaExprUtf8);
            if (ShowTextPrompt(st->hwndMain, L"Editar F\u00F3rmula 2D", L"Expresi\u00F3n:", exprW) && !exprW.empty()) {
                AnnotationObject before = *obj;
                AnnotationObject after = *obj;
                after.formulaExprUtf8 = WideToUtf8(exprW);
                const auto ev = Cas::EvaluateExpression(after.formulaExprUtf8);
                if (ev.ok) after.evaluatedResultUtf8 = ev.formattedResultUtf8;
                st->cmdStack.Execute(st->doc, std::make_unique<ModifyObjectCommand>(tgt, std::move(before), std::move(after)));
                st->statusBannerUtf8 = "F\xC3\xB3rmula recalculada";
                UpdateWindowTitle(st);
                InvalidateViews(st);
            }
        } else if (obj->kind == ObjectKind::TableBlock) {
            ExecuteToolbarCommand(st, CMD_ADD_TABLE);
        } else if (obj->kind == ObjectKind::CircuitBlock) {
            std::wstring netW = Utf8ToWide(obj->circuitNetlistUtf8);
            for (wchar_t& ch : netW) if (ch == L'\n') ch = L';';
            if (ShowTextPrompt(st->hwndMain, L"Editar Circuito SPICE/MNA", L"Netlist (separado por ';'):", netW) && !netW.empty()) {
                std::string net = WideToUtf8(netW);
                for (char& ch : net) if (ch == ';') ch = '\n';
                std::vector<Circuit::CircuitComponent> comps;
                std::string perr;
                AnnotationObject before = *obj;
                AnnotationObject after = *obj;
                after.circuitNetlistUtf8 = net;
                if (Circuit::ParseNetlistText(net, comps, &perr)) {
                    const auto sol = Circuit::SolveMnaCircuit(comps, 0.0);
                    after.evaluatedResultUtf8 = sol.ok ? sol.summaryUtf8 : sol.errorUtf8;
                }
                st->cmdStack.Execute(st->doc, std::make_unique<ModifyObjectCommand>(tgt, std::move(before), std::move(after)));
                st->statusBannerUtf8 = "Circuito recalculado";
                UpdateWindowTitle(st);
                InvalidateViews(st);
            }
        } else if (obj->kind == ObjectKind::AudioClip) {
            if (const auto* bytes = st->doc.FindAsset(obj->assetKey)) {
                st->audioSession.LoadFromWavBytes(bytes->data(), bytes->size());
            }
            ExecuteToolbarCommand(st, CMD_REPLAY_TOGGLE);
        }
        return 0;
    }
    case WM_CAPTURECHANGED: {
        if (st) {
            st->draggingOpacitySlider = false;
            st->panning = false;
            st->drawing = false;
            st->transformDrag.active = false;
            st->pdfSelectingText = false;
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
        RECT rc{};
        GetClientRect(hwnd, &rc);
        const bool onSplitRight = st->splitViewActive && pt.x > (rc.right / 2);
        Camera2D& activeCam = onSplitRight ? st->splitCamera : st->camera;
        if (ctrl) {
            const double factor = (delta > 0) ? 1.12 : 0.89;
            activeCam.ZoomByFactorAtAnchor(onSplitRight ? (pt.x - rc.right / 2) : pt.x, pt.y, factor);
        } else {
            activeCam.panY -= (static_cast<double>(delta) / WHEEL_DELTA) * (72.0 / (std::max)(0.2, activeCam.zoom));
            if (!onSplitRight) RefreshActivePageFromCamera(st);
        }
        InvalidateViews(st);
        return 0;
    }
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        if (!st) break;
        CommitInlineTextEdit(st, true);
        st->panning = true;
        st->rbuttonMoved = false;
        st->rbuttonDownPt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        st->lastPanPt = st->rbuttonDownPt;
        SetCapture(hwnd);
        return 0;
    }
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        if (st && st->panning) {
            st->panning = false;
            ReleaseCapture();
            if (msg == WM_RBUTTONUP && !st->rbuttonMoved) {
                HMENU cm = CreatePopupMenu();
                AppendMenuW(cm, MF_STRING, CMD_CLIPBOARD_PASTE, L"Pegar (Ctrl+V)");
                if (st->selectedObjectId != 0 || !st->lassoSelectedIds.empty()) {
                    AppendMenuW(cm, MF_STRING, CMD_DUPLICATE_OBJECTS, L"Duplicar (Ctrl+D)");
                    AppendMenuW(cm, MF_STRING, CMD_GROUP_OBJECTS,     L"Agrupar (Ctrl+G)");
                    AppendMenuW(cm, MF_STRING, CMD_UNGROUP_OBJECTS,   L"Desagrupar");
                    AppendMenuW(cm, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(cm, MF_STRING, CMD_COPY_STROKE_STYLE, L"Copiar estilo al l\u00E1piz");
                    AppendMenuW(cm, MF_STRING, CMD_RESTYLE_INK,       L"Enderezar / Estilizar tinta");
                    AppendMenuW(cm, MF_STRING, CMD_HTR_TO_TEXT,       L"Escritura -> Texto editable");
                    AppendMenuW(cm, MF_STRING, CMD_HTR_TO_MATH,       L"Escritura -> Ecuaci\u00F3n 2D");
                    AppendMenuW(cm, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(cm, MF_STRING, CMD_IMG_ENHANCE_WHITEBOARD, L"Imagen: Mejorar pizarra");
                    AppendMenuW(cm, MF_STRING, CMD_IMG_ROTATE_90,          L"Imagen: Rotar +90\u00B0");
                    AppendMenuW(cm, MF_STRING, CMD_TEXT_BOLD,              L"Texto: Negrita");
                }
                POINT spt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                ClientToScreen(hwnd, &spt);
                const UINT cmd = TrackPopupMenu(cm, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, spt.x, spt.y, 0, st->hwndMain, nullptr);
                DestroyMenu(cm);
                if (cmd != 0) ExecuteToolbarCommand(st, static_cast<WORD>(cmd));
            }
        }
        return 0;
    }
    case WM_POINTERDOWN:
    case WM_LBUTTONDOWN: {
        if (!st) break;
        CommitInlineTextEdit(st, true);
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
                // Strict Palm Rejection (F-036): ignore touch while pen is active/recent
                if (pi.pointerType == PT_TOUCH && (GetTickCount64() - st->lastPenInputTickMs) < 1200ull) {
                    return 0;
                }
                POINT pt = pi.ptPixelLocation;
                ScreenToClient(hwnd, &pt);
                sp = { static_cast<double>(pt.x), static_cast<double>(pt.y) };
                POINTER_PEN_INFO pen{};
                if (pi.pointerType == PT_PEN && GetPointerPenInfo(pid, &pen)) {
                    st->lastPenInputTickMs = GetTickCount64();
                    pressure = std::clamp(static_cast<float>(pen.pressure) / 1024.0f, 0.1f, 1.0f);
                    tiltX = static_cast<float>(pen.tiltX);
                    tiltY = static_cast<float>(pen.tiltY);
                    // Inverted stylus eraser (F-037) & Barrel button temporary Lasso (F-038)
                    if ((pen.penFlags & PEN_FLAG_INVERTED) || (pen.penFlags & PEN_FLAG_ERASER)) {
                        if (!st->invertedTempEraser) {
                            st->savedToolBeforeStylusMod = st->activeTool;
                            st->activeTool = ToolKind::Eraser;
                            st->invertedTempEraser = true;
                        }
                    } else if (pen.penFlags & PEN_FLAG_BARREL) {
                        if (!st->barrelTempLasso) {
                            st->savedToolBeforeStylusMod = st->activeTool;
                            st->activeTool = ToolKind::Lasso;
                            st->barrelTempLasso = true;
                        }
                    }
                }
            }
        } else {
            extractPointerWorld(lp, pressure, sp);
        }

        const DocPoint wpWorld = st->camera.ScreenToWorld(sp.x, sp.y);
        bool isCanvas = (st->doc.Mode() == DocumentMode::InfiniteCanvas);
        std::size_t pgIdx = st->activePageIndex;
        double lx = wpWorld.x, ly = wpWorld.y;
        bool insidePage = isCanvas;
        if (!isCanvas) {
            // Check collapsed page header banner click first (F-055)
            for (std::size_t pi = 0; pi < st->doc.Layout().PageCount(); ++pi) {
                const auto* pl = st->doc.Layout().PlacementAt(pi);
                if (pl && pl->collapsedHeader && pl->WorldRect().Contains(wpWorld.x, wpWorld.y)) {
                    st->doc.ToggleCollapsedRange(static_cast<int>(pl->pageIndex), static_cast<int>(pl->collapsedEndPage0));
                    st->statusBannerUtf8 = "Rango de p\xC3\xA1ginas expandido";
                    UpdateWindowTitle(st);
                    InvalidateViews(st);
                    return 0;
                }
            }
            insidePage = st->doc.Layout().HitTestWorldPoint(wpWorld.x, wpWorld.y, pgIdx, lx, ly);
            if (insidePage) st->activePageIndex = pgIdx;
        }

        const bool spacePan = (GetKeyState(VK_SPACE) & 0x8000) != 0;
        if (st->activeTool == ToolKind::Hand && !spacePan && insidePage) {
            const ObjectTarget tgt{ isCanvas, pgIdx };
            double originX = 0.0, originY = 0.0;
            if (!isCanvas) {
                if (const auto* pl = st->doc.Layout().PlacementAt(pgIdx)) {
                    const DocRect wr = pl->WorldRect();
                    originX = wr.x; originY = wr.y;
                    // 1. Clickable InkLink badge navigation (F-132)
                    if (const InkLink* lk = CanvasManip::HitTestInkLink(st->doc, pgIdx, { lx, ly }, wr.w, wr.h)) {
                        if (lk->targetIsCanvas) {
                            st->doc.SetMode(DocumentMode::InfiniteCanvas);
                            if (const auto* tObj = st->doc.FindCanvasObject(lk->targetObjectId)) {
                                st->selectedObjectId = tObj->id;
                                st->camera.panX = tObj->bounds.x - 100.0;
                                st->camera.panY = tObj->bounds.y - 80.0;
                            }
                        } else if (lk->targetPageIndex0 >= 0 && static_cast<std::size_t>(lk->targetPageIndex0) < st->doc.Pages().size()) {
                            st->doc.SetMode(DocumentMode::PaginatedPdf);
                            CenterCameraOnPage(st, static_cast<std::size_t>(lk->targetPageIndex0));
                        }
                        st->statusBannerUtf8 = "Navegado a enlace: " + lk->labelUtf8;
                        LayoutMainChildren(st);
                        InvalidateViews(st);
                        return 0;
                    }
                    // 2. Clickable PDF hyperlink (F-082)
                    if (const PdfLinkRect* plk = CanvasManip::HitTestPdfLink(st->doc.Pages()[pgIdx], { lx, ly }, wr.w, wr.h)) {
                        if (!plk->uriUtf8.empty()) {
                            ShellExecuteW(st->hwndMain, L"open", Utf8ToWide(plk->uriUtf8).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                            st->statusBannerUtf8 = "Enlace abierto: " + plk->uriUtf8;
                        } else if (plk->targetPage0 >= 0 && static_cast<std::size_t>(plk->targetPage0) < st->doc.Pages().size()) {
                            CenterCameraOnPage(st, static_cast<std::size_t>(plk->targetPage0));
                            st->statusBannerUtf8 = "Ir a p\xC3\xA1gina " + std::to_string(plk->targetPage0 + 1);
                        }
                        InvalidateViews(st);
                        return 0;
                    }
                    // 3. Interactive AcroForm field (F-084)
                    const int ffIdx = CanvasManip::HitTestFormField(st->doc.Pages()[pgIdx], { lx, ly }, wr.w, wr.h);
                    if (ffIdx >= 0) {
                        auto& ff = st->doc.MutablePages()[pgIdx].formFields[static_cast<std::size_t>(ffIdx)];
                        if (!ff.readOnly) {
                            if (ff.fieldTypeUtf8 == "Btn") {
                                ff.checked = !ff.checked;
                                st->doc.BumpRevision();
                                st->statusBannerUtf8 = "Casilla '" + ff.nameUtf8 + "' actualizada";
                            } else {
                                std::wstring vW = Utf8ToWide(ff.valueUtf8);
                                if (ShowTextPrompt(st->hwndMain, L"Campo de Formulario PDF", Utf8ToWide(ff.nameUtf8).c_str(), vW)) {
                                    ff.valueUtf8 = WideToUtf8(vW);
                                    st->doc.BumpRevision();
                                    st->statusBannerUtf8 = "Campo '" + ff.nameUtf8 + "' guardado";
                                }
                            }
                            InvalidateViews(st);
                            return 0;
                        }
                    }
                }
            }

            // 4. Hit-test 8 resize handles + rotation stalk on currently selected object (F-026, F-103)
            if (st->selectedObjectId != 0 && st->selectedIsCanvas == isCanvas && (isCanvas || st->selectedPageIndex == pgIdx)) {
                if (const auto* selObj = CanvasManip::FindObjectConst(st->doc, tgt, st->selectedObjectId)) {
                    const auto hKind = CanvasManip::HitTestSelectionHandles(
                        selObj->bounds, originX, originY, st->camera,
                        static_cast<int>(std::lround(sp.x)), static_cast<int>(std::lround(sp.y)));
                    if (hKind != CanvasManip::HandleKind::None && hKind != CanvasManip::HandleKind::Body) {
                        st->transformDrag = CanvasManip::ActiveTransformDrag{};
                        st->transformDrag.active = true;
                        st->transformDrag.handle = hKind;
                        st->transformDrag.target = tgt;
                        st->transformDrag.objectIds = { selObj->id };
                        st->transformDrag.beforeSnapshots = { *selObj };
                        st->transformDrag.startWorld = { lx, ly };
                        st->transformDrag.initialBounds = selObj->bounds;
                        const double cx = selObj->bounds.x + selObj->bounds.w * 0.5;
                        const double cy = selObj->bounds.y + selObj->bounds.h * 0.5;
                        st->transformDrag.initialAngleRad = std::atan2(ly - cy, lx - cx);
                        st->transformDrag.initialRotDeg = selObj->rotationDeg;
                        SetCapture(hwnd);
                        return 0;
                    }
                }
            }

            // 5. Direct object hit -> Drag-to-move, Multi-lasso drag, 3D surface rotation (F-237), or Audio Seek (F-156)
            const double tol = std::clamp(8.0 / (std::max)(0.25, st->camera.zoom), 3.0, 18.0);
            const auto hits = CollectHits(st, isCanvas, pgIdx, lx, ly, tol);
            const bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (!hits.empty()) {
                const std::uint64_t hitId = hits.back();
                const auto* hitObj = CanvasManip::FindObjectConst(st->doc, tgt, hitId);
                if (hitObj && shiftHeld && (hitObj->kind == ObjectKind::AudioClip || (hitObj->kind == ObjectKind::InkStroke && hitObj->inkStartTimeMs > 0))) {
                    st->audioSession.SeekMs(hitObj->inkStartTimeMs);
                    if (st->audioSession.DurationMs() > 0) {
                        st->audioSession.StartAudioPlayback(hitObj->inkStartTimeMs);
                    }
                    st->replayCursorMs = hitObj->inkStartTimeMs;
                    st->statusBannerUtf8 = "Audio posicionado en " + std::to_string(hitObj->inkStartTimeMs / 1000) + " s";
                    InvalidateViews(st);
                    return 0;
                }

                // Expand selection with group or existing multi-lasso selection
                std::vector<std::uint64_t> dragIds;
                const bool inLasso = std::find(st->lassoSelectedIds.begin(), st->lassoSelectedIds.end(), hitId) != st->lassoSelectedIds.end();
                if (inLasso && st->lassoSelectedIds.size() > 1) {
                    dragIds = st->lassoSelectedIds;
                } else {
                    dragIds = { hitId };
                    CanvasManip::ExpandSelectionWithGroupIds(st->doc, tgt, dragIds);
                    st->lassoSelectedIds = (dragIds.size() > 1) ? dragIds : std::vector<std::uint64_t>{};
                }
                st->selectedObjectId = hitId;
                st->selectedIsCanvas = isCanvas;
                st->selectedPageIndex = pgIdx;

                st->transformDrag = CanvasManip::ActiveTransformDrag{};
                st->transformDrag.active = true;
                st->transformDrag.handle = CanvasManip::HandleKind::Body;
                st->transformDrag.target = tgt;
                st->transformDrag.objectIds = dragIds;
                st->transformDrag.startWorld = { lx, ly };
                for (std::uint64_t id : dragIds) {
                    if (const auto* o = CanvasManip::FindObjectConst(st->doc, tgt, id)) {
                        st->transformDrag.beforeSnapshots.push_back(*o);
                    }
                }
                if (hitObj && CanvasManip::IsPlot3DObject(*hitObj) && (shiftHeld || dragIds.size() == 1)) {
                    // Drag inside center of 3D surface rotates yaw/pitch interactively; edge moves box
                    const bool nearEdge = (lx - hitObj->bounds.x < 22.0) || (hitObj->bounds.Right() - lx < 22.0) ||
                                          (ly - hitObj->bounds.y < 28.0) || (hitObj->bounds.Bottom() - ly < 22.0);
                    if (shiftHeld || !nearEdge) {
                        st->transformDrag.is3DRotateDrag = true;
                        try {
                            auto j = nlohmann::json::parse(hitObj->plotConfigUtf8);
                            st->transformDrag.startYawDeg = j.value("yawDeg", 42.0);
                            st->transformDrag.startPitchDeg = j.value("pitchDeg", 28.0);
                        } catch (...) {}
                    }
                }
                st->panMoved = false;
                st->panStartPt = { static_cast<LONG>(sp.x), static_cast<LONG>(sp.y) };
                SetCapture(hwnd);
                InvalidateViews(st);
                return 0;
            }

            // 6. Shift-drag on PDF page selects PDF text (F-075, F-076)
            if (!isCanvas && shiftHeld) {
                st->pdfSelectingText = true;
                st->pdfSelPage = pgIdx;
                st->pdfSelAnchorLocal = { lx, ly };
                st->pdfSelRectLocal = { lx, ly, 1.0, 1.0 };
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        }

        if (st->activeTool == ToolKind::Hand || spacePan) {
            st->panning = true;
            st->panMoved = false;
            st->panStartPt = { static_cast<LONG>(sp.x), static_cast<LONG>(sp.y) };
            st->lastPanPt = st->panStartPt;
            SetCapture(hwnd);
            return 0;
        }

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

        if (!insidePage) return 0;

        if (st->activeTool == ToolKind::Eraser) {
            const ObjectTarget erTarget{ isCanvas, pgIdx };
            const double erRad = 11.0 / (std::max)(0.4, st->camera.zoom);
            const auto rawHits = CollectHits(st, isCanvas, pgIdx, lx, ly, erRad);
            const auto hits = InkEngine::FilterHitsByEraserMode(st->doc, erTarget, rawHits, st->eraserFilterMode, true);
            if (!hits.empty()) {
                if (st->eraserActionMode == EraserActionMode::PartialPixel) {
                    bool anySplit = false;
                    for (std::uint64_t hid : hits) {
                        const auto* o = CanvasManip::FindObjectConst(st->doc, erTarget, hid);
                        if (!o) continue;
                        if (o->kind == ObjectKind::InkStroke) {
                            std::vector<AnnotationObject> splits;
                            if (InkEngine::EraseStrokePartial(*o, { lx, ly }, erRad, splits)) {
                                st->cmdStack.Execute(st->doc, std::make_unique<ReplaceStrokeWithSplitsCommand>(erTarget, *o, std::move(splits)));
                                anySplit = true;
                            }
                        } else {
                            st->cmdStack.Execute(st->doc, std::make_unique<RemoveObjectCommand>(erTarget, hid));
                            anySplit = true;
                        }
                    }
                    if (anySplit) {
                        st->statusBannerUtf8 = "Borrado parcial aplicado";
                        AppendWalMutation(st, "erase_partial");
                    }
                } else {
                    st->cmdStack.Execute(st->doc, std::make_unique<BatchEraseObjectsCommand>(erTarget, hits));
                    AppendWalMutation(st, "erase_whole");
                }
                UpdateWindowTitle(st);
                InvalidateViews(st);
            }
            st->drawing = true;
            st->draftIsCanvas = isCanvas;
            st->draftPageIdx = pgIdx;
            SetCapture(hwnd);
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
                AppendWalMutation(st, "add_textbox");
                UpdateWindowTitle(st);
                InvalidateViews(st);
            }
            return 0;
        }

        if (st->snapToGrid && st->activeTool != ToolKind::Lasso && st->activeTool != ToolKind::Crop) {
            const PaperPattern pat = isCanvas ? st->doc.CanvasBackground() : st->doc.Pages()[pgIdx].background;
            const DocPoint sn = InkEngine::SnapPointToGrid({ lx, ly }, pat, 24.0);
            lx = sn.x; ly = sn.y;
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
        st->draftObj.penStyle = st->penStyle;
        st->draftObj.opacity = st->penOpacity;

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
                if (pi.pointerType == PT_TOUCH && (GetTickCount64() - st->lastPenInputTickMs) < 1200ull) {
                    return 0;
                }
                POINT pt = pi.ptPixelLocation;
                ScreenToClient(hwnd, &pt);
                sp = { static_cast<double>(pt.x), static_cast<double>(pt.y) };
                POINTER_PEN_INFO pen{};
                if (pi.pointerType == PT_PEN && GetPointerPenInfo(pid, &pen)) {
                    st->lastPenInputTickMs = GetTickCount64();
                    pressure = std::clamp(static_cast<float>(pen.pressure) / 1024.0f, 0.1f, 1.0f);
                    tiltX = static_cast<float>(pen.tiltX);
                    tiltY = static_cast<float>(pen.tiltY);
                }
            }
        } else {
            extractPointerWorld(lp, pressure, sp);
        }

        if (st->transformDrag.active) {
            if (std::hypot(sp.x - st->panStartPt.x, sp.y - st->panStartPt.y) > 3.0) st->panMoved = true;
            const DocPoint wpWorld = st->camera.ScreenToWorld(sp.x, sp.y);
            double lx = wpWorld.x, ly = wpWorld.y;
            if (!st->transformDrag.target.isCanvas) {
                if (const auto* pl = st->doc.Layout().PlacementAt(st->transformDrag.target.pageIndex)) {
                    const DocRect wr = pl->WorldRect();
                    lx -= wr.x; ly -= wr.y;
                }
            }
            const double dx = lx - st->transformDrag.startWorld.x;
            const double dy = ly - st->transformDrag.startWorld.y;

            if (st->transformDrag.is3DRotateDrag && !st->transformDrag.objectIds.empty()) {
                if (AnnotationObject* obj = CanvasManip::FindObjectMut(st->doc, st->transformDrag.target, st->transformDrag.objectIds[0])) {
                    CanvasManip::UpdatePlot3DRotation(*obj, st->transformDrag.startYawDeg + dx * 0.75, st->transformDrag.startPitchDeg - dy * 0.55);
                    st->doc.BumpRevision();
                }
            } else if (st->transformDrag.handle == CanvasManip::HandleKind::Body) {
                for (std::size_t i = 0; i < st->transformDrag.objectIds.size() && i < st->transformDrag.beforeSnapshots.size(); ++i) {
                    if (AnnotationObject* obj = CanvasManip::FindObjectMut(st->doc, st->transformDrag.target, st->transformDrag.objectIds[i])) {
                        *obj = st->transformDrag.beforeSnapshots[i];
                        CanvasManip::TranslateAnnotationObject(*obj, dx, dy);
                        InkEngine::UpdateMagneticConnectors(st->doc, st->transformDrag.target, obj->id);
                    }
                }
                st->doc.BumpRevision();
            } else if (st->transformDrag.handle == CanvasManip::HandleKind::Rotate && !st->transformDrag.objectIds.empty()) {
                if (AnnotationObject* obj = CanvasManip::FindObjectMut(st->doc, st->transformDrag.target, st->transformDrag.objectIds[0])) {
                    const double cx = st->transformDrag.initialBounds.x + st->transformDrag.initialBounds.w * 0.5;
                    const double cy = st->transformDrag.initialBounds.y + st->transformDrag.initialBounds.h * 0.5;
                    const double ang = std::atan2(ly - cy, lx - cx);
                    const double deltaDeg = (ang - st->transformDrag.initialAngleRad) * (180.0 / 3.14159265358979323846);
                    obj->rotationDeg = std::fmod(st->transformDrag.initialRotDeg + deltaDeg + 3600.0, 360.0);
                    st->doc.BumpRevision();
                }
            } else if (!st->transformDrag.objectIds.empty() && !st->transformDrag.beforeSnapshots.empty()) {
                if (AnnotationObject* obj = CanvasManip::FindObjectMut(st->doc, st->transformDrag.target, st->transformDrag.objectIds[0])) {
                    const DocRect nb = CanvasManip::ComputeResizedBounds(st->transformDrag.handle, st->transformDrag.initialBounds, dx, dy);
                    CanvasManip::ResizeAnnotationObject(*obj, st->transformDrag.beforeSnapshots[0], nb);
                    InkEngine::UpdateMagneticConnectors(st->doc, st->transformDrag.target, obj->id);
                    st->doc.BumpRevision();
                }
            }
            InvalidateViews(st);
            return 0;
        }

        if (st->pdfSelectingText) {
            const DocPoint wpWorld = st->camera.ScreenToWorld(sp.x, sp.y);
            if (const auto* pl = st->doc.Layout().PlacementAt(st->pdfSelPage)) {
                const DocRect wr = pl->WorldRect();
                const double lx = wpWorld.x - wr.x;
                const double ly = wpWorld.y - wr.y;
                st->pdfSelRectLocal = DocRect::FromMinMax(
                    (std::min)(st->pdfSelAnchorLocal.x, lx), (std::min)(st->pdfSelAnchorLocal.y, ly),
                    (std::max)(st->pdfSelAnchorLocal.x, lx), (std::max)(st->pdfSelAnchorLocal.y, ly));
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        if (st->panning) {
            if (std::hypot(sp.x - st->panStartPt.x, sp.y - st->panStartPt.y) > 3.5) {
                st->panMoved = true;
            }
            if (std::hypot(sp.x - st->rbuttonDownPt.x, sp.y - st->rbuttonDownPt.y) > 4.0) {
                st->rbuttonMoved = true;
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
            InvalidateViews(st);
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

        // Continuous erasing on drag (both WholeStroke & PartialPixel modes)
        if (st->activeTool == ToolKind::Eraser) {
            const ObjectTarget erTarget{ st->draftIsCanvas, st->draftPageIdx };
            const double erRad = 11.0 / (std::max)(0.4, st->camera.zoom);
            const auto rawHits = CollectHits(st, st->draftIsCanvas, st->draftPageIdx, lx, ly, erRad);
            const auto hits = InkEngine::FilterHitsByEraserMode(st->doc, erTarget, rawHits, st->eraserFilterMode, true);
            if (!hits.empty()) {
                if (st->eraserActionMode == EraserActionMode::PartialPixel) {
                    for (std::uint64_t hid : hits) {
                        const auto* o = CanvasManip::FindObjectConst(st->doc, erTarget, hid);
                        if (!o) continue;
                        if (o->kind == ObjectKind::InkStroke) {
                            std::vector<AnnotationObject> splits;
                            if (InkEngine::EraseStrokePartial(*o, { lx, ly }, erRad, splits)) {
                                st->cmdStack.Execute(st->doc, std::make_unique<ReplaceStrokeWithSplitsCommand>(erTarget, *o, std::move(splits)));
                            }
                        } else {
                            st->cmdStack.Execute(st->doc, std::make_unique<RemoveObjectCommand>(erTarget, hid));
                        }
                    }
                } else {
                    st->cmdStack.Execute(st->doc, std::make_unique<BatchEraseObjectsCommand>(erTarget, hits));
                }
                InvalidateViews(st);
            }
            return 0;
        }

        if (st->draftObj.kind == ObjectKind::InkStroke) {
            if (st->hasPredictedTip && st->draftObj.inkPoints.size() >= 2) {
                st->draftObj.inkPoints.pop_back();
                st->hasPredictedTip = false;
            }

            auto appendLocalSample = [&](double sxLocal, double syLocal, float pr, float tx, float ty) {
                if (st->activeTool != ToolKind::Lasso) {
                    if (st->rulerState.visible) {
                        const DocPoint snapped = InkEngine::ProjectPointOntoRuler({ sxLocal, syLocal }, st->rulerState);
                        sxLocal = snapped.x;
                        syLocal = snapped.y;
                    } else if (st->snapToGrid) {
                        const PaperPattern pat = st->draftIsCanvas ? st->doc.CanvasBackground() : st->doc.Pages()[st->draftPageIdx].background;
                        const DocPoint sn = InkEngine::SnapPointToGrid({ sxLocal, syLocal }, pat, 24.0);
                        sxLocal = sn.x;
                        syLocal = sn.y;
                    }
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

            if (st->activeTool != ToolKind::Lasso && !st->rulerState.visible && !st->snapToGrid && st->draftObj.inkPoints.size() >= 3) {
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
        auto restoreStylusTempTool = [&]() {
            if (st->invertedTempEraser || st->barrelTempLasso) {
                st->activeTool = st->savedToolBeforeStylusMod;
                st->invertedTempEraser = false;
                st->barrelTempLasso = false;
            }
        };
        if (msg == WM_LBUTTONUP && st->draggingOpacitySlider) {
            st->draggingOpacitySlider = false;
            ReleaseCapture();
            return 0;
        }
        if (st->transformDrag.active) {
            st->transformDrag.active = false;
            ReleaseCapture();
            if (st->panMoved && !st->transformDrag.beforeSnapshots.empty()) {
                // Commit undoable batch transform snapshot!
                std::vector<AnnotationObject> afterSnaps;
                for (std::uint64_t id : st->transformDrag.objectIds) {
                    if (const auto* cur = CanvasManip::FindObjectConst(st->doc, st->transformDrag.target, id)) {
                        afterSnaps.push_back(*cur);
                    }
                }
                if (afterSnaps.size() == st->transformDrag.beforeSnapshots.size()) {
                    // Restore pre-drag state cleanly so CommandStack::Execute records proper before->after
                    for (std::size_t i = 0; i < st->transformDrag.objectIds.size(); ++i) {
                        if (AnnotationObject* m = CanvasManip::FindObjectMut(st->doc, st->transformDrag.target, st->transformDrag.objectIds[i])) {
                            *m = st->transformDrag.beforeSnapshots[i];
                        }
                    }
                    st->cmdStack.Execute(st->doc, std::make_unique<BatchModifyObjectsCommand>(
                        st->transformDrag.target,
                        std::move(st->transformDrag.beforeSnapshots),
                        std::move(afterSnaps),
                        st->transformDrag.is3DRotateDrag ? "Rotar 3D" : "Transformar objeto"));
                    AppendWalMutation(st, "transform_obj");
                }
            } else if (!st->panMoved && st->selectedObjectId != 0) {
                AppActions::ToggleStudyTapeIfHit(
                    st->doc, st->cmdStack, st->transformDrag.target,
                    st->selectedObjectId, st->statusBannerUtf8);
            }
            UpdateWindowTitle(st);
            InvalidateViews(st);
            restoreStylusTempTool();
            return 0;
        }
        if (st->pdfSelectingText) {
            st->pdfSelectingText = false;
            ReleaseCapture();
            if (st->pdfSelPage < st->doc.Pages().size() && !st->pdfSelRectLocal.Empty()) {
                const auto& pg = st->doc.Pages()[st->pdfSelPage];
                std::vector<DocRect> quads;
                const std::string extracted = CanvasManip::ExtractPdfTextInRect(
                    pg, st->pdfSelRectLocal, pg.EffectiveWidthPt(), pg.EffectiveHeightPt(), &quads);
                if (!extracted.empty()) {
                    st->lastSelectedPdfTextUtf8 = extracted;
                    CanvasManip::CopyTextToClipboardWin32(st->hwndMain, extracted);
                    // Also record semantic QuadPoints highlight when selection covers >= 12pt
                    if (st->pdfSelRectLocal.w >= 12.0 && !quads.empty()) {
                        AnnotationObject hl{};
                        hl.kind = ObjectKind::Shape;
                        hl.shapeKind = ShapeKind::Rectangle;
                        hl.layerId = st->doc.ActiveLayerId();
                        hl.bounds = st->pdfSelRectLocal;
                        hl.fillColor = DocColor::RGBA(253, 224, 71, 95);
                        hl.strokeColor = DocColor::RGBA(234, 179, 8, 120);
                        hl.strokeWidth = 0.8;
                        hl.isHighlighter = true;
                        hl.textUtf8 = extracted;
                        hl.pdfQuadPointsNorm = quads;
                        st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(ObjectTarget{ false, st->pdfSelPage }, std::move(hl)));
                    }
                    st->statusBannerUtf8 = "Texto PDF copiado y resaltado (QuadPoints): \"" + extracted.substr(0, 48) + "\"";
                }
            }
            st->pdfSelRectLocal = {};
            InvalidateViews(st);
            restoreStylusTempTool();
            return 0;
        }
        if (st->panning) {
            st->panning = false;
            ReleaseCapture();
            if (!st->panMoved && st->activeTool == ToolKind::Hand) {
                st->selectedObjectId = 0;
                st->lassoSelectedIds.clear();
                UpdateWindowTitle(st);
                InvalidateViews(st);
            }
            restoreStylusTempTool();
            return 0;
        }
        if (!st->drawing) { restoreStylusTempTool(); return 0; }
        st->drawing = false;
        ReleaseCapture();

        if (st->activeTool == ToolKind::Eraser) {
            UpdateWindowTitle(st);
            InvalidateViews(st);
            restoreStylusTempTool();
            return 0;
        }

        if (st->activeTool == ToolKind::Laser) {
            st->laserPointer.active = false;
            InvalidateViews(st);
            restoreStylusTempTool();
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
            AppendWalMutation(st, "add_ink");
        } else {
            st->draftObj.RecomputeBounds();
            if (!st->draftObj.bounds.Empty()) {
                st->cmdStack.Execute(st->doc, std::make_unique<AddObjectCommand>(
                    ObjectTarget{ st->draftIsCanvas, st->draftPageIdx }, std::move(st->draftObj)));
                st->selectedIsCanvas = st->draftIsCanvas;
                st->selectedPageIndex = st->draftPageIdx;
                st->selectedObjectId = AppActions::LastInsertedId(st->doc, ObjectTarget{ st->draftIsCanvas, st->draftPageIdx });
                AppendWalMutation(st, "add_shape");
            }
        }
        restoreStylusTempTool();
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
            const std::size_t bmShow = (std::min<std::size_t>)(st->doc.Bookmarks().size(), 4u);
            const int bmAreaH = (bmShow > 0) ? static_cast<int>(20 + bmShow * 22 + 8) : 0;
            if (bmShow > 0 && pt.y >= kNavAreaH + 20 && pt.y < kNavAreaH + bmAreaH) {
                const std::size_t bIdx = static_cast<std::size_t>((pt.y - (kNavAreaH + 20)) / 22);
                if (bIdx < bmShow) {
                    const std::size_t pg = st->doc.Bookmarks()[bIdx].pageIndex;
                    if (pg < st->doc.Pages().size()) {
                        st->activePageIndex = pg;
                        CenterCameraOnPage(st, pg);
                        st->statusBannerUtf8 = "Secci\xC3\xB3n: " + st->doc.Bookmarks()[bIdx].titleUtf8;
                        InvalidateViews(st);
                    }
                }
                return 0;
            }
            const int y = (pt.y - (kNavAreaH + bmAreaH)) + st->sidebarScrollPos;
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
                const std::size_t bmShow = (std::min<std::size_t>)(st->doc.Bookmarks().size(), 4u);
                int pagesTopY = kNavAreaH;
                if (bmShow > 0) {
                    SelectObject(mem, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
                    SetTextColor(mem, pal.labelTertiary.ToColorRef());
                    RECT bhRc{ 16, kNavAreaH + 2, w - 12, kNavAreaH + 18 };
                    DrawTextW(mem, L"\u00CDNDICE", -1, &bhRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                    for (std::size_t b = 0; b < bmShow; ++b) {
                        const auto& bm = st->doc.Bookmarks()[b];
                        RECT br{ 14, kNavAreaH + 20 + static_cast<int>(b) * 22, w - 12, kNavAreaH + 40 + static_cast<int>(b) * 22 };
                        SetTextColor(mem, pal.accentBlue.ToColorRef());
                        const std::wstring bTxt = L"\u203A " + Utf8ToWide(bm.titleUtf8);
                        DrawTextW(mem, bTxt.c_str(), -1, &br, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                    }
                    pagesTopY = kNavAreaH + static_cast<int>(20 + bmShow * 22 + 8);
                }

                SelectObject(mem, st->hUiSmallFont ? st->hUiSmallFont : st->hUiFont);
                SetTextColor(mem, pal.labelTertiary.ToColorRef());
                std::wstring pgHead = L"P\u00C1GINAS (" + std::to_wstring(st->doc.Pages().size()) + L")";
                RECT phRc{ 16, pagesTopY + 2, w - 12, pagesTopY + 20 };
                DrawTextW(mem, pgHead.c_str(), -1, &phRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

                // Clip page list below header
                HRGN clipRgn = CreateRectRgn(0, pagesTopY + 22, w - 1, h);
                SelectClipRgn(mem, clipRgn);
                g.SetClip(Gdiplus::Rect(0, pagesTopY + 22, w - 1, (std::max)(1, h - pagesTopY - 22)));

                const std::size_t count = st->doc.Pages().size();
                const std::size_t first = static_cast<std::size_t>((std::max)(0, st->sidebarScrollPos / kItemH));
                const std::size_t last = (std::min)(count, first + static_cast<std::size_t>((h - pagesTopY) / kItemH + 3));
                for (std::size_t i = first; i < last; ++i) {
                    const int top = pagesTopY + 24 + static_cast<int>(i) * kItemH - st->sidebarScrollPos;
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
                    { st->audioSession.IsContinuousRecording() ? L"Detener audio (Mic)" : L"Grabar nota de audio",
                                                   UiGlass::IconId::MicAudio,     pal.accentRed,    st->audioSession.IsContinuousRecording() },
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
                DrawTextW(mem, st->audioSession.IsContinuousRecording() ? L"Grabando desde micr\u00F3fono (Win32 waveIn)..." : L"Listo para grabar trazos + voz offline.", -1, &ar, DT_LEFT | DT_WORDBREAK);
                drawActionBtn({ c2.left + 12, c2.top + 88, c2.right - 12, c2.top + 118 },
                    st->audioSession.IsContinuousRecording() ? L"Detener Grabaci\u00F3n" : L"Iniciar Grabaci\u00F3n (Mic)", CMD_AUDIO_REC, true);
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
        if (HMODULE hUser32 = GetModuleHandleW(L"user32.dll")) {
            using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
            if (auto pGetDpi = reinterpret_cast<GetDpiForWindowFn>(GetProcAddress(hUser32, "GetDpiForWindow"))) {
                const UINT wndDpi = pGetDpi(hwnd);
                if (wndDpi >= 72) st->currentDpi = wndDpi;
            }
        }
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
        SetTimer(hwnd, 702, 450, nullptr); // Live zero-network Casio IPC inbox poller (F-246)
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
        const UINT newDpi = HIWORD(wp) ? HIWORD(wp) : (LOWORD(wp) ? LOWORD(wp) : 96u);
        if (newDpi >= 72) {
            st->currentDpi = newDpi;
            st->dpiCycleTransitions++;
            HFONT newUiFont      = UiGlass::CreateHigUiFont(13, FW_NORMAL);
            HFONT newUiBoldFont  = UiGlass::CreateHigUiFont(13, FW_SEMIBOLD);
            HFONT newUiSmallFont = UiGlass::CreateHigUiFont(12, FW_NORMAL);
            HFONT newUiTitleFont = UiGlass::CreateHigUiFont(15, FW_SEMIBOLD);
            if (st->hwndSearchEdit && newUiFont) {
                SendMessageW(st->hwndSearchEdit, WM_SETFONT, reinterpret_cast<WPARAM>(newUiFont), FALSE);
            }
            if (st->hUiFont)      DeleteObject(st->hUiFont);
            if (st->hUiBoldFont)  DeleteObject(st->hUiBoldFont);
            if (st->hUiSmallFont) DeleteObject(st->hUiSmallFont);
            if (st->hUiTitleFont) DeleteObject(st->hUiTitleFont);
            st->hUiFont      = newUiFont;
            st->hUiBoldFont  = newUiBoldFont;
            st->hUiSmallFont = newUiSmallFont;
            st->hUiTitleFont = newUiTitleFont;
        }
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        if (suggested) {
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                suggested->right - suggested->left, suggested->bottom - suggested->top,
                SWP_NOZORDER | SWP_NOACTIVATE);
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
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool inSearchEdit = (st->hwndSearchEdit && GetFocus() == st->hwndSearchEdit);
        const bool inInlineEdit = (st->hwndInlineEdit && GetFocus() == st->hwndInlineEdit);
        if (wp == VK_ESCAPE && inInlineEdit) { CommitInlineTextEdit(st, false); return 0; }
        if (wp == VK_ESCAPE && st->appearancePopoverOpen) {
            st->appearancePopoverOpen = false;
            InvalidateViews(st);
            return 0;
        }
        if (ctrl && wp == 'S') { ExecuteToolbarCommand(st, CMD_SAVE_CUDOC); return 0; }
        if (ctrl && wp == 'O') { ExecuteToolbarCommand(st, CMD_OPEN); return 0; }
        if (ctrl && !inSearchEdit && !inInlineEdit && wp == 'Z') { ExecuteToolbarCommand(st, CMD_UNDO); return 0; }
        if (ctrl && !inSearchEdit && !inInlineEdit && wp == 'Y') { ExecuteToolbarCommand(st, CMD_REDO); return 0; }
        if (ctrl && !inSearchEdit && !inInlineEdit && wp == 'V') { ExecuteToolbarCommand(st, CMD_CLIPBOARD_PASTE); return 0; }
        if (ctrl && !inSearchEdit && !inInlineEdit && wp == 'C') {
            const ObjectTarget tgt = CurrentTarget(st);
            if (const auto* o = CanvasManip::FindObjectConst(st->doc, tgt, st->selectedObjectId)) {
                const std::string txt = !o->textUtf8.empty() ? o->textUtf8 : (!o->formulaExprUtf8.empty() ? o->formulaExprUtf8 : o->evaluatedResultUtf8);
                if (!txt.empty()) {
                    CanvasManip::CopyTextToClipboardWin32(st->hwndMain, txt);
                    st->statusBannerUtf8 = "Texto copiado al portapapeles";
                    InvalidateViews(st);
                }
            } else if (!st->lastSelectedPdfTextUtf8.empty()) {
                CanvasManip::CopyTextToClipboardWin32(st->hwndMain, st->lastSelectedPdfTextUtf8);
                st->statusBannerUtf8 = "Texto PDF copiado";
                InvalidateViews(st);
            }
            return 0;
        }
        if (ctrl && !inSearchEdit && !inInlineEdit && wp == 'G') { ExecuteToolbarCommand(st, shift ? CMD_UNGROUP_OBJECTS : CMD_GROUP_OBJECTS); return 0; }
        if (ctrl && !inSearchEdit && !inInlineEdit && wp == 'D') { ExecuteToolbarCommand(st, CMD_DUPLICATE_OBJECTS); return 0; }
        if (ctrl && wp == 'T') { ExecuteToolbarCommand(st, CMD_NEW_TAB); return 0; }
        if (ctrl && wp == VK_TAB) { ExecuteToolbarCommand(st, CMD_NEXT_TAB); return 0; }
        if (ctrl && shift && wp == 'N') { ExecuteToolbarCommand(st, CMD_QUICK_NOTE); return 0; }
        if (ctrl && !inSearchEdit && !inInlineEdit && wp == 'B') { ExecuteToolbarCommand(st, CMD_TEXT_BOLD); return 0; }
        if (ctrl && !inSearchEdit && !inInlineEdit && wp == 'I') { ExecuteToolbarCommand(st, CMD_TEXT_ITALIC); return 0; }
        if (ctrl && !inSearchEdit && !inInlineEdit && wp == 'U') { ExecuteToolbarCommand(st, CMD_TEXT_UNDERLINE); return 0; }
        if (ctrl && wp == 'F') {
            if (!st->searchVisible) ExecuteToolbarCommand(st, CMD_TOGGLE_SEARCH);
            else if (st->hwndSearchEdit) SetFocus(st->hwndSearchEdit);
            return 0;
        }
        if (ctrl && wp == 'P') { ExecuteToolbarCommand(st, CMD_PRINT); return 0; }
        if (wp == VK_F3) { ExecuteToolbarCommand(st, CMD_SEARCH_NEXT); return 0; }
        if (!inSearchEdit && !inInlineEdit && (wp == VK_DELETE || wp == VK_BACK)) {
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
        if (st && wp == 702) {
            PollCasioIpcInbox(st);
            return 0;
        }
        if (st && wp == 701 && st->replayActive) {
            if (st->audioSession.IsPlaying() && st->audioSession.DurationMs() > 0) {
                st->replayCursorMs = st->audioSession.GetHardwarePlaybackCursorMs();
            } else {
                st->replayCursorMs += 45;
                st->audioSession.AdvancePlaybackMs(45);
            }
            if (st->replayCursorMs >= st->replayMaxMs) {
                st->replayActive = false;
                KillTimer(hwnd, 701);
                st->audioSession.StopAudioPlayback();
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
        if (st && st->testDpiCycleCli) {
            RECT origRc{};
            GetWindowRect(hwnd, &origRc);
            const int origW = (std::max)(800, static_cast<int>(origRc.right - origRc.left));
            const int origH = (std::max)(600, static_cast<int>(origRc.bottom - origRc.top));
            struct DpiStep { UINT dpi; int w; int h; };
            const DpiStep steps[3] = {
                { 144u, origW + 120, origH + 80 },
                { 192u, origW + 240, origH + 140 },
                { 96u,  origW,       origH }
            };
            bool ok = true;
            for (const auto& step : steps) {
                RECT sug{ origRc.left, origRc.top, origRc.left + step.w, origRc.top + step.h };
                SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(step.dpi, step.dpi), reinterpret_cast<LPARAM>(&sug));
                if (st->currentDpi != step.dpi) ok = false;
                RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
                if (st->hwndSidebar && IsWindowVisible(st->hwndSidebar)) RedrawWindow(st->hwndSidebar, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
                if (st->hwndInspector && IsWindowVisible(st->hwndInspector)) RedrawWindow(st->hwndInspector, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
                if (st->hwndViewport && IsWindowVisible(st->hwndViewport)) RedrawWindow(st->hwndViewport, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
            }
            if (!ok || st->dpiCycleTransitions < 3 || st->currentDpi != 96u || !st->hUiFont || !st->hUiBoldFont) {
                DestroyWindow(hwnd);
                PostQuitMessage(2);
                return 0;
            }
        }
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
        KillTimer(hwnd, 702);
        if (st) {
            CommitInlineTextEdit(st, true);
            if (st->audioSession.IsContinuousRecording()) st->audioSession.StopContinuousMicRecording();
            st->audioSession.StopAudioPlayback();
            st->projectorWnd.Close();
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
        } else if (arg == L"--test-dpi-cycle") {
            st->testDpiCycleCli = true;
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

    auto registerCls = [&](const wchar_t* name, WNDPROC proc, HCURSOR cur, bool withDblClk = false, bool withIcon = false) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = withDblClk ? CS_DBLCLKS : 0;
        wc.lpfnWndProc = proc;
        wc.hInstance = hInst;
        wc.hCursor = cur;
        if (withIcon) {
            wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
            wc.hIconSm = static_cast<HICON>(LoadImageW(hInst, MAKEINTRESOURCEW(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
        }
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = name;
        RegisterClassExW(&wc);
    };
    registerCls(kMainClassName, MainWndProc, LoadCursorW(nullptr, IDC_ARROW), false, true);
    registerCls(kViewportClassName, ViewportWndProc, LoadCursorW(nullptr, IDC_CROSS), true, false);
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
        HWND hwnd = CreateWindowExW(0, kMainClassName, L"CuadernU \u2014 Calculadora Unificada",
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
            if (st->inspectorVisible) {
                const int sideW = st->sidebarVisible ? kSidebarWidth : 0;
                const int availVw = (std::max)(560, winW - 16 - sideW - kInspectorWidth);
                st->camera.zoom = std::clamp(static_cast<double>(availVw - 24) / 908.0, 0.78, 1.0);
                st->camera.panX = -14.0;
                st->camera.panY = -12.0;
            } else {
                st->camera.zoom = 1.0;
                st->camera.panX = -18.0;
                st->camera.panY = -12.0;
            }
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
        const int exitCode = (msg.message == WM_QUIT) ? static_cast<int>(msg.wParam) : 0;
        st.reset();
        if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
        RoUninitialize();
        CoUninitialize();
        return exitCode;
    }

    if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
    RoUninitialize();
    CoUninitialize();
    return 0;
}
