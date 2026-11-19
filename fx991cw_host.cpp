#include "fx991cw_host.h"
#include "operation_capture.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace {

constexpr int kCw2SkinW = 992;
constexpr int kCw2SkinH = 2082;
// Normalized LCD box inside official Casio interface.png (992x2082 body).
constexpr int kCw2LcdLeft = 107;
constexpr int kCw2LcdTop = 315;
constexpr int kCw2LcdRight = 885;
constexpr int kCw2LcdBottom = 620;

// Outer bezel alpha silhouette inside interface.png (removes white outer frame corners).
constexpr int kCw2BezelInsetX = 8;
constexpr int kCw2BezelInsetY = 7;
constexpr int kCw2BezelRadius = 118;

struct RuntimePaths {
    std::wstring baseDir;
    std::wstring emulatorDir;
    std::wstring exePath;
    std::wstring modelDir;
    std::wstring defaultRamPath;
    std::wstring stateRamPath;
    bool valid = false;
};

struct HostGlobals {
    Fx991CwState state = Fx991CwState::Stopped;
    std::wstring lastError;
    HANDLE hJob = nullptr;
    HANDLE hProcess = nullptr;
    DWORD pid = 0;
    HWND hChild = nullptr;
    ULONGLONG launchTick = 0;
    ULONGLONG lastRetryTick = 0;
    RECT lastAppliedRect{ 0, 0, 0, 0 };
    bool lastVisible = false;
    bool hasAppliedRect = false;
};

HostGlobals g_host;

bool FileExistsW(const std::wstring& path)
{
    if (path.empty()) return false;
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirExistsW(const std::wstring& path)
{
    if (path.empty()) return false;
    DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

std::wstring ParentDir(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L"";
    return path.substr(0, pos);
}

std::wstring GetModuleDirectoryDynamicW()
{
    DWORD cap = 512;
    std::wstring buf;
    for (;;) {
        buf.assign(cap, L'\0');
        SetLastError(ERROR_SUCCESS);
        const DWORD len = GetModuleFileNameW(nullptr, buf.data(), cap);
        if (len == 0) return L"";
        if (len < cap && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            buf.resize(len);
            return ParentDir(buf);
        }
        if (cap >= 32768) {
            buf.resize(len);
            return ParentDir(buf);
        }
        cap *= 2;
    }
}

std::wstring GetCurrentDirectoryDynamicW()
{
    const DWORD need = GetCurrentDirectoryW(0, nullptr);
    if (need == 0) return L"";
    std::wstring buf(need, L'\0');
    const DWORD written = GetCurrentDirectoryW(need, buf.data());
    if (written == 0 || written >= need) return L"";
    buf.resize(written);
    return buf;
}

std::wstring GetEnvVarDynamicW(const wchar_t* name)
{
    if (!name) return L"";
    const DWORD need = GetEnvironmentVariableW(name, nullptr, 0);
    if (need == 0) return L"";
    std::wstring buf(need, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, buf.data(), need);
    if (written == 0 || written >= need) return L"";
    buf.resize(written);
    return buf;
}

RuntimePaths ResolveRuntimePaths(std::wstring* outDiag = nullptr)
{
    RuntimePaths out;
    const std::wstring exeDir = GetModuleDirectoryDynamicW();
    const std::wstring cwd = GetCurrentDirectoryDynamicW();
    const std::wstring envRuntime = GetEnvVarDynamicW(L"CASIO_FX991CW_RUNTIME_DIR");
    const std::wstring envHome = GetEnvVarDynamicW(L"CASIO_UNIFIED_HOME");

    std::vector<std::wstring> candidates;
    if (!envRuntime.empty()) candidates.push_back(envRuntime);
    if (!envHome.empty()) candidates.push_back(envHome + L"\\runtime\\fx991cw");
    if (!exeDir.empty()) {
        candidates.push_back(exeDir + L"\\runtime\\fx991cw");
        const std::wstring p1 = ParentDir(exeDir);
        if (!p1.empty()) {
            candidates.push_back(p1 + L"\\runtime\\fx991cw");
            const std::wstring p2 = ParentDir(p1);
            if (!p2.empty()) {
                candidates.push_back(p2 + L"\\runtime\\fx991cw");
                const std::wstring p3 = ParentDir(p2);
                if (!p3.empty()) candidates.push_back(p3 + L"\\runtime\\fx991cw");
            }
        }
    }
    if (!cwd.empty()) {
        candidates.push_back(cwd + L"\\runtime\\fx991cw");
    }

    for (const auto& cand : candidates) {
        if (cand.empty()) continue;
        std::wstring emuDir = cand + L"\\emulator";
        std::wstring exe = emuDir + L"\\casioemu.exe";
        std::wstring sdl2 = emuDir + L"\\SDL2.dll";
        std::wstring modelDir = cand + L"\\models\\fx991cw_emu";
        std::wstring lua = modelDir + L"\\model.lua";
        std::wstring rom = modelDir + L"\\rom.bin";
        std::wstring iface = modelDir + L"\\interface.png";
        if (FileExistsW(exe) && FileExistsW(sdl2) && DirExistsW(modelDir) &&
            FileExistsW(lua) && FileExistsW(rom) && FileExistsW(iface)) {
            out.baseDir = cand;
            out.emulatorDir = emuDir;
            out.exePath = exe;
            out.modelDir = modelDir;
            out.defaultRamPath = modelDir + L"\\default_en.ram";
            std::wstring stateDir = ResolveWritableOutputDir(L"fx991cw_state");
            if (!stateDir.empty() && IsDirectoryWritable(stateDir)) {
                out.stateRamPath = stateDir + L"\\fx991cw.ram";
            }
            out.valid = true;
            return out;
        }
        if (FileExistsW(exe) && outDiag && outDiag->empty()) {
            if (!FileExistsW(sdl2)) *outDiag = L"Falta runtime/fx991cw/emulator/SDL2.dll.";
            else if (!FileExistsW(iface)) *outDiag = L"Falta runtime/fx991cw/models/fx991cw_emu/interface.png.";
            else if (!FileExistsW(rom) || !FileExistsW(lua)) *outDiag = L"Faltan archivos de modelo fx991cw_emu.";
        }
    }
    if (outDiag && outDiag->empty()) {
        *outDiag = L"No se encontro runtime/fx991cw/emulator/casioemu.exe.";
    }
    return out;
}

void EnsureStateRamFile(const RuntimePaths& paths, bool forceReset)
{
    if (paths.stateRamPath.empty()) return;
    if (forceReset && FileExistsW(paths.stateRamPath)) {
        DeleteFileW(paths.stateRamPath.c_str());
    }
    if (!FileExistsW(paths.stateRamPath) && FileExistsW(paths.defaultRamPath)) {
        CopyFileW(paths.defaultRamPath.c_str(), paths.stateRamPath.c_str(), FALSE);
    }
}

void EnsureJobObject()
{
    if (g_host.hJob) return;
    HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
    if (!hJob) return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
        CloseHandle(hJob);
        return;
    }
    g_host.hJob = hJob;
}

bool IsHostProcessAlive()
{
    if (!g_host.hProcess) return false;
    DWORD waitRes = WaitForSingleObject(g_host.hProcess, 0);
    return waitRes == WAIT_TIMEOUT;
}

void ClearDeadProcessHandles()
{
    if (g_host.hProcess) {
        CloseHandle(g_host.hProcess);
        g_host.hProcess = nullptr;
    }
    g_host.pid = 0;
    g_host.hChild = nullptr;
    g_host.hasAppliedRect = false;
    g_host.lastVisible = false;
}

struct EnumFindCtx {
    DWORD pid = 0;
    HWND found = nullptr;
};

bool QueryProcessExitCode(DWORD& outExitCode)
{
    outExitCode = 0;
    if (!g_host.hProcess) return false;
    return GetExitCodeProcess(g_host.hProcess, &outExitCode) != FALSE;
}

BOOL CALLBACK FindSdlWindowProc(HWND hwnd, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<EnumFindCtx*>(lParam);
    if (!ctx || hwnd == nullptr || !IsWindow(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid != ctx->pid) return TRUE;

    wchar_t cls[64]{};
    GetClassNameW(hwnd, cls, 64);
    if (wcscmp(cls, L"SDL_app") == 0) {
        ctx->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

void ApplyBezelRegion(HWND hChild, int w, int h)
{
    if (!hChild || !IsWindow(hChild) || w <= 0 || h <= 0) return;
    int ix = (std::max)(0, (w * kCw2BezelInsetX) / kCw2SkinW);
    int iy = (std::max)(0, (h * kCw2BezelInsetY) / kCw2SkinH);
    int rad = (std::max)(4, (w * kCw2BezelRadius) / kCw2SkinW);
    int right = (std::max)(ix + 8, w - ix);
    int bottom = (std::max)(iy + 8, h - iy);
    HRGN hRgn = CreateRoundRectRgn(ix, iy, right, bottom, rad * 2, rad * 2);
    if (hRgn) {
        if (!SetWindowRgn(hChild, hRgn, TRUE)) {
            DeleteObject(hRgn);
        }
    }
}

void ApplyChildGeometry(HWND hParent, const RECT& viewportRect, bool visible, bool force)
{
    if (!g_host.hChild || !IsWindow(g_host.hChild)) return;
    if (!hParent || !IsWindow(hParent)) return;

    int w = (std::max)(120, static_cast<int>(viewportRect.right - viewportRect.left));
    int h = (std::max)(240, static_cast<int>(viewportRect.bottom - viewportRect.top));
    RECT normalizedRect{
        viewportRect.left,
        viewportRect.top,
        viewportRect.left + w,
        viewportRect.top + h
    };

    if (!force && g_host.hasAppliedRect &&
        EqualRect(&g_host.lastAppliedRect, &normalizedRect) &&
        g_host.lastVisible == visible) {
        return;
    }

    HWND curParent = GetParent(g_host.hChild);
    if (curParent != hParent) {
        ShowWindow(g_host.hChild, SW_HIDE);
        LONG_PTR style = GetWindowLongPtrW(g_host.hChild, GWL_STYLE);
        style &= ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                   WS_MAXIMIZEBOX | WS_SYSMENU | WS_BORDER | WS_DLGFRAME);
        style |= WS_CHILD | WS_CLIPSIBLINGS;
        if (visible) style |= WS_VISIBLE;
        else style &= ~WS_VISIBLE;
        SetWindowLongPtrW(g_host.hChild, GWL_STYLE, style);

        LONG_PTR exStyle = GetWindowLongPtrW(g_host.hChild, GWL_EXSTYLE);
        exStyle &= ~(WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE |
                     WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);
        SetWindowLongPtrW(g_host.hChild, GWL_EXSTYLE, exStyle);

        SetParent(g_host.hChild, hParent);
    }

    UINT flags = SWP_NOACTIVATE | SWP_NOZORDER | SWP_ASYNCWINDOWPOS;
    if (curParent != hParent) flags |= SWP_FRAMECHANGED;
    flags |= visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW;

    SetWindowPos(
        g_host.hChild,
        nullptr,
        normalizedRect.left,
        normalizedRect.top,
        w,
        h,
        flags);

    if (visible) {
        ApplyBezelRegion(g_host.hChild, w, h);
    }

    g_host.lastAppliedRect = normalizedRect;
    g_host.lastVisible = visible;
    g_host.hasAppliedRect = true;
}

bool TryAttachChildWindow(HWND hParent, const RECT& viewportRect, bool visible)
{
    if (!IsHostProcessAlive()) return false;
    if (g_host.hChild && IsWindow(g_host.hChild)) {
        g_host.state = Fx991CwState::Ready;
        return true;
    }

    EnumFindCtx ctx;
    ctx.pid = g_host.pid;
    EnumWindows(FindSdlWindowProc, reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.found) return false;

    g_host.hChild = ctx.found;
    g_host.state = Fx991CwState::Ready;
    g_host.lastError.clear();
    ApplyChildGeometry(hParent, viewportRect, visible, true);
    return true;
}

bool StartProcessInternal(HWND hParent, const RECT& viewportRect, bool visible, bool resetRam)
{
    std::wstring pathDiag;
    RuntimePaths paths = ResolveRuntimePaths(&pathDiag);
    if (!paths.valid) {
        g_host.state = Fx991CwState::NotFound;
        g_host.lastError = pathDiag.empty()
            ? L"No se encontro runtime/fx991cw/emulator/casioemu.exe."
            : pathDiag;
        return false;
    }

    EnsureStateRamFile(paths, resetRam);
    EnsureJobObject();

    int initW = (std::max)(284, static_cast<int>(viewportRect.right - viewportRect.left));
    int initH = (std::max)(562, static_cast<int>(viewportRect.bottom - viewportRect.top));

    std::wstring cmdLine = L"\"" + paths.exePath + L"\"";
    cmdLine += L" model=../models/fx991cw_emu";
    cmdLine += L" width=" + std::to_wstring(initW);
    cmdLine += L" height=" + std::to_wstring(initH);
    cmdLine += L" resizable=1";
    if (!paths.stateRamPath.empty()) {
        cmdLine += L" \"ram=" + paths.stateRamPath + L"\"";
    }

    std::vector<wchar_t> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    DWORD createFlags = CREATE_SUSPENDED | CREATE_NO_WINDOW;
    if (!CreateProcessW(
            paths.exePath.c_str(),
            cmdBuf.data(),
            nullptr,
            nullptr,
            FALSE,
            createFlags,
            nullptr,
            paths.emulatorDir.c_str(),
            &si,
            &pi)) {
        DWORD err = ::GetLastError();
        g_host.state = Fx991CwState::Failed;
        g_host.lastError = L"CreateProcessW fallo con codigo " + std::to_wstring(err);
        return false;
    }

    if (g_host.hJob) {
        AssignProcessToJobObject(g_host.hJob, pi.hProcess);
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);

    g_host.hProcess = pi.hProcess;
    g_host.pid = pi.dwProcessId;
    g_host.hChild = nullptr;
    g_host.launchTick = GetTickCount64();
    g_host.lastRetryTick = g_host.launchTick;
    g_host.state = Fx991CwState::Launching;
    g_host.lastError.clear();
    g_host.hasAppliedRect = false;

    for (int i = 0; i < 12; ++i) {
        if (!IsHostProcessAlive()) {
            DWORD exitCode = 0;
            QueryProcessExitCode(exitCode);
            ClearDeadProcessHandles();
            g_host.state = Fx991CwState::Failed;
            g_host.lastError = L"casioemu.exe termino al iniciar (codigo " + std::to_wstring(exitCode) + L").";
            return false;
        }
        if (TryAttachChildWindow(hParent, viewportRect, visible)) {
            break;
        }
        Sleep(25);
    }
    return true;
}

} // namespace

bool Fx991CwHost::IsRuntimeAvailable()
{
    return ResolveRuntimePaths().valid;
}

Fx991CwState Fx991CwHost::GetState()
{
    if (g_host.state == Fx991CwState::Ready || g_host.state == Fx991CwState::Launching) {
        if (!IsHostProcessAlive()) {
            DWORD exitCode = 0;
            const bool hasExit = QueryProcessExitCode(exitCode);
            ClearDeadProcessHandles();
            g_host.state = IsRuntimeAvailable() ? Fx991CwState::Failed : Fx991CwState::NotFound;
            if (g_host.lastError.empty()) {
                g_host.lastError = hasExit
                    ? (L"El proceso casioemu.exe finalizo de forma inesperada (codigo " + std::to_wstring(exitCode) + L").")
                    : L"El proceso casioemu.exe finalizo de forma inesperada.";
            }
        }
    } else if (g_host.state == Fx991CwState::Stopped && !IsRuntimeAvailable()) {
        g_host.state = Fx991CwState::NotFound;
    }
    return g_host.state;
}

std::wstring Fx991CwHost::GetLastError()
{
    return g_host.lastError;
}

HWND Fx991CwHost::GetChildHwnd()
{
    if (g_host.hChild && IsWindow(g_host.hChild)) {
        return g_host.hChild;
    }
    return nullptr;
}

bool Fx991CwHost::EnsureLaunched(HWND hParent, const RECT& viewportRect, bool visible)
{
    if (IsHostProcessAlive()) {
        if (!g_host.hChild || !IsWindow(g_host.hChild)) {
            TryAttachChildWindow(hParent, viewportRect, visible);
        }
        return true;
    }
    ClearDeadProcessHandles();

    ULONGLONG now = GetTickCount64();
    if (g_host.state == Fx991CwState::Failed && (now - g_host.lastRetryTick) < 1500) {
        return false;
    }
    return StartProcessInternal(hParent, viewportRect, visible, false);
}

void Fx991CwHost::PollAttach(HWND hParent, const RECT& viewportRect, bool visible)
{
    if (!IsHostProcessAlive()) {
        if (g_host.hProcess) {
            DWORD exitCode = 0;
            const bool hasExit = QueryProcessExitCode(exitCode);
            ClearDeadProcessHandles();
            g_host.state = Fx991CwState::Failed;
            g_host.lastError = hasExit
                ? (L"casioemu.exe se cerro (codigo " + std::to_wstring(exitCode) + L").")
                : L"casioemu.exe se cerro.";
        } else if (!IsRuntimeAvailable()) {
            g_host.state = Fx991CwState::NotFound;
        }
        return;
    }

    if (!g_host.hChild || !IsWindow(g_host.hChild)) {
        if (!TryAttachChildWindow(hParent, viewportRect, visible)) {
            if ((GetTickCount64() - g_host.launchTick) > 8000) {
                g_host.state = Fx991CwState::Failed;
                g_host.lastError = L"Tiempo agotado esperando la ventana SDL de fx-991CW.";
            }
            return;
        }
    }

    ApplyChildGeometry(hParent, viewportRect, visible, false);
}

void Fx991CwHost::SyncLayout(HWND hParent, const RECT& viewportRect, bool visible)
{
    if (!hParent || !IsWindow(hParent)) return;
    if (visible && !IsHostProcessAlive() && g_host.state != Fx991CwState::Failed) {
        EnsureLaunched(hParent, viewportRect, visible);
    }
    if (g_host.hChild && IsWindow(g_host.hChild)) {
        ApplyChildGeometry(hParent, viewportRect, visible, false);
    }
}

bool Fx991CwHost::IsChildFocused()
{
    HWND hChild = GetChildHwnd();
    if (!hChild || !IsWindowVisible(hChild)) return false;
    DWORD tid = GetWindowThreadProcessId(hChild, nullptr);
    if (!tid) return false;
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(gti);
    if (!GetGUIThreadInfo(tid, &gti)) return false;
    return (gti.hwndFocus == hChild || gti.hwndActive == hChild);
}

bool Fx991CwHost::ForwardKey(WPARAM vk, bool down)
{
    HWND hChild = GetChildHwnd();
    if (!hChild || !IsWindowVisible(hChild)) return false;

    UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
    LPARAM lParam = 1 | (static_cast<LPARAM>(scan & 0xFF) << 16);
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
        vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END ||
        vk == VK_PRIOR || vk == VK_NEXT) {
        lParam |= (1 << 24);
    }
    if (!down) {
        lParam |= (1 << 30) | (1 << 31);
    }
    return PostMessageW(hChild, down ? WM_KEYDOWN : WM_KEYUP, vk, lParam) != FALSE;
}

bool Fx991CwHost::ClickNormalized(double normX, double normY)
{
    if (!std::isfinite(normX) || !std::isfinite(normY)) return false;
    HWND hChild = GetChildHwnd();
    if (!hChild || !IsWindowVisible(hChild)) return false;
    RECT rc{};
    if (!GetClientRect(hChild, &rc)) return false;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return false;

    const double clampedX = std::clamp(normX, 0.0, 1.0);
    const double clampedY = std::clamp(normY, 0.0, 1.0);
    int px = (std::min)(w - 1, (std::max)(0, static_cast<int>(clampedX * w)));
    int py = (std::min)(h - 1, (std::max)(0, static_cast<int>(clampedY * h)));
    LPARAM lp = MAKELPARAM(px, py);
    PostMessageW(hChild, WM_MOUSEMOVE, 0, lp);
    PostMessageW(hChild, WM_LBUTTONDOWN, MK_LBUTTON, lp);
    Sleep(45);
    if (!IsWindow(hChild)) return false;
    PostMessageW(hChild, WM_LBUTTONUP, 0, lp);
    return true;
}

HBITMAP Fx991CwHost::CaptureBitmap(bool lcdOnly, int& outW, int& outH)
{
    outW = 0;
    outH = 0;
    HWND hChild = GetChildHwnd();
    if (!hChild) return nullptr;

    RECT rc{};
    if (!GetClientRect(hChild, &rc)) return nullptr;
    int fullW = rc.right - rc.left;
    int fullH = rc.bottom - rc.top;
    if (fullW <= 0 || fullH <= 0) return nullptr;

    HDC hdcScreen = GetDC(hChild);
    if (!hdcScreen) return nullptr;

    HDC hdcFull = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmpFull = CreateCompatibleBitmap(hdcScreen, fullW, fullH);
    if (!hdcFull || !hBmpFull) {
        if (hdcFull) DeleteDC(hdcFull);
        if (hBmpFull) DeleteObject(hBmpFull);
        ReleaseDC(hChild, hdcScreen);
        return nullptr;
    }

    HGDIOBJ oldFull = SelectObject(hdcFull, hBmpFull);
    BOOL ok = PrintWindow(hChild, hdcFull, PW_CLIENTONLY | PW_RENDERFULLCONTENT);
    if (!ok) {
        ok = PrintWindow(hChild, hdcFull, PW_RENDERFULLCONTENT);
    }
    if (!ok) {
        ok = BitBlt(hdcFull, 0, 0, fullW, fullH, hdcScreen, 0, 0, SRCCOPY);
    }
    if (!ok) {
        SelectObject(hdcFull, oldFull);
        DeleteObject(hBmpFull);
        DeleteDC(hdcFull);
        ReleaseDC(hChild, hdcScreen);
        return nullptr;
    }

    if (!lcdOnly) {
        SelectObject(hdcFull, oldFull);
        DeleteDC(hdcFull);
        ReleaseDC(hChild, hdcScreen);
        outW = fullW;
        outH = fullH;
        return hBmpFull;
    }

    int lx = MulDiv(fullW, kCw2LcdLeft, kCw2SkinW);
    int ly = MulDiv(fullH, kCw2LcdTop, kCw2SkinH);
    int rx = MulDiv(fullW, kCw2LcdRight, kCw2SkinW);
    int by = MulDiv(fullH, kCw2LcdBottom, kCw2SkinH);
    int lw = (std::max)(1, rx - lx);
    int lh = (std::max)(1, by - ly);

    HDC hdcCrop = CreateCompatibleDC(hdcScreen);
    HBITMAP resultBmp = CreateCompatibleBitmap(hdcScreen, lw, lh);
    if (hdcCrop && resultBmp) {
        HGDIOBJ oldCrop = SelectObject(hdcCrop, resultBmp);
        if (BitBlt(hdcCrop, 0, 0, lw, lh, hdcFull, lx, ly, SRCCOPY)) {
            outW = lw;
            outH = lh;
        } else {
            SelectObject(hdcCrop, oldCrop);
            DeleteObject(resultBmp);
            resultBmp = nullptr;
        }
        if (resultBmp) SelectObject(hdcCrop, oldCrop);
    } else {
        if (resultBmp) {
            DeleteObject(resultBmp);
            resultBmp = nullptr;
        }
    }
    if (hdcCrop) DeleteDC(hdcCrop);

    SelectObject(hdcFull, oldFull);
    DeleteObject(hBmpFull);
    DeleteDC(hdcFull);
    ReleaseDC(hChild, hdcScreen);
    return resultBmp;
}

bool Fx991CwHost::Restart(HWND hParent, const RECT& viewportRect, bool visible, bool resetRam)
{
    Shutdown();
    g_host.state = Fx991CwState::Stopped;
    g_host.lastError.clear();
    return StartProcessInternal(hParent, viewportRect, visible, resetRam);
}

void Fx991CwHost::Shutdown()
{
    HWND hChild = g_host.hChild;
    HANDLE hProc = g_host.hProcess;
    g_host.hChild = nullptr;
    g_host.hasAppliedRect = false;
    g_host.lastVisible = false;
    g_host.lastError.clear();

    if (hChild && IsWindow(hChild)) {
        ShowWindow(hChild, SW_HIDE);
        PostMessageW(hChild, WM_CLOSE, 0, 0);
        SetParent(hChild, nullptr);
    }

    if (hProc) {
        DWORD waitRes = WaitForSingleObject(hProc, 2500);
        if (waitRes != WAIT_OBJECT_0) {
            TerminateProcess(hProc, 0);
            WaitForSingleObject(hProc, 1000);
        }
        CloseHandle(hProc);
        g_host.hProcess = nullptr;
    }

    g_host.pid = 0;
    if (g_host.hJob) {
        CloseHandle(g_host.hJob);
        g_host.hJob = nullptr;
    }
    g_host.state = IsRuntimeAvailable() ? Fx991CwState::Stopped : Fx991CwState::NotFound;
}
