#include "Progress.hpp"
#include "Runtime.hpp"
#include "Version.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <system_error>
#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cwchar>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <objbase.h>
#include <thread>
#include <mutex>
#else
#include <csignal>
#include <iostream>
#include <unistd.h>
#endif

namespace progress {
namespace {

std::atomic<bool> g_stop{false};
// Two flags, because they answer different questions. g_active is "is there a
// tray icon and a taskbar bar to keep up to date", which is off when the tray
// is; g_tracking is "does this run have a start and a target", which is what
// the numbers on the console need and has nothing to do with the tray.
std::atomic<bool> g_active{false};
std::atomic<bool> g_tracking{false};
double g_start = 0.0;
double g_total = 0.0;
std::string g_title;

// The last thing that was drawn, so a step that moved the clock by a
// microsecond does not redraw anything.
int g_lastPermille = -1;
double g_current = 0.0;
std::filesystem::path g_stopFile;
std::chrono::steady_clock::time_point g_nextStopCheck{};

int permilleOf(double current) {
    if (!(g_total > g_start))
        return 1000;
    double fraction = (current - g_start) / (g_total - g_start);
    if (fraction < 0.0) fraction = 0.0;
    if (fraction > 1.0) fraction = 1.0;
    return static_cast<int>(fraction * 1000.0 + 0.5);
}

std::string statusText(double current) {
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "%s - %.3g / %.3g s (%d%%)",
                  g_title.c_str(), current, g_total,
                  permilleOf(current) / 10);
    return std::string(buffer);
}

#if defined(_WIN32)

// ------------------------------------------------------------- Windows -----

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT ID_SHOW = 1;
constexpr UINT ID_HIDE = 2;
constexpr UINT ID_OUTPUT = 3;
constexpr UINT ID_STOP = 4;

std::mutex g_mutex;
std::thread g_thread;
HWND g_window = nullptr;
std::atomic<bool> g_ready{false};
NOTIFYICONDATAW g_icon{};
HICON g_balloonIcon = nullptr;
bool g_iconLive = false;
std::atomic<bool> g_consoleHidden{false};
std::wstring g_outputDir;

// wcsncpy_s is MSVC's; MinGW only has it behind a feature macro. One helper
// rather than an #ifdef at each of the four call sites.
template <std::size_t N>
void copyInto(wchar_t (&field)[N], const std::wstring& text) {
    const std::size_t count = (text.size() < N - 1) ? text.size() : N - 1;
    std::wmemcpy(field, text.c_str(), count);
    field[count] = L'\0';
}

// ITaskbarList3, declared by hand. Linking shlwapi/shobjidl through the SDK
// headers is fine on MSVC but the MinGW ones disagree about the interface
// name, and this is four methods.
struct ITaskbarList3Vtbl;
struct ITaskbarList3 { ITaskbarList3Vtbl* lpVtbl; };
struct ITaskbarList3Vtbl {
    HRESULT (STDMETHODCALLTYPE* QueryInterface)(ITaskbarList3*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE* AddRef)(ITaskbarList3*);
    ULONG   (STDMETHODCALLTYPE* Release)(ITaskbarList3*);
    HRESULT (STDMETHODCALLTYPE* HrInit)(ITaskbarList3*);
    HRESULT (STDMETHODCALLTYPE* AddTab)(ITaskbarList3*, HWND);
    HRESULT (STDMETHODCALLTYPE* DeleteTab)(ITaskbarList3*, HWND);
    HRESULT (STDMETHODCALLTYPE* ActivateTab)(ITaskbarList3*, HWND);
    HRESULT (STDMETHODCALLTYPE* SetActiveAlt)(ITaskbarList3*, HWND);
    HRESULT (STDMETHODCALLTYPE* MarkFullscreenWindow)(ITaskbarList3*, HWND, BOOL);
    HRESULT (STDMETHODCALLTYPE* SetProgressValue)(ITaskbarList3*, HWND,
                                                  ULONGLONG, ULONGLONG);
    HRESULT (STDMETHODCALLTYPE* SetProgressState)(ITaskbarList3*, HWND, int);
};

const GUID kCLSID_TaskbarList =
    {0x56FDF344, 0xFD6D, 0x11d0, {0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90}};
const GUID kIID_ITaskbarList3 =
    {0xEA1AFB91, 0x9E28, 0x4B86, {0x90, 0xE9, 0x9E, 0x9F, 0x8A, 0x5E, 0xEF, 0xAF}};

ITaskbarList3* g_taskbar = nullptr;
HWND g_console = nullptr;

std::wstring widen(const std::string& text) {
    if (text.empty())
        return std::wstring();
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring wide(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        wide.data(), size);
    return wide;
}

void setTooltip(const std::wstring& text) {
    if (!g_iconLive)
        return;
    // szTip is 128 wide characters including the terminator, and Shell_NotifyIcon
    // simply fails on anything longer rather than truncating it.
    copyInto(g_icon.szTip, text);
    g_icon.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_icon);
}

void balloon(const std::wstring& title, const std::wstring& text, bool error) {
    if (!g_iconLive)
        return;
    NOTIFYICONDATAW data = g_icon;
    data.uFlags = NIF_INFO;
    if (g_balloonIcon && !error) {
        // The project's own icon in the balloon rather than the generic blue
        // "i". NIIF_USER is what makes hBalloonIcon be looked at at all.
        data.hBalloonIcon = g_balloonIcon;
        data.dwInfoFlags = NIIF_USER | NIIF_LARGE_ICON;
    } else {
        data.dwInfoFlags = error ? NIIF_WARNING : NIIF_INFO;
    }
    copyInto(data.szInfoTitle, title);
    copyInto(data.szInfo, text);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void showConsole(bool visible) {
    if (!g_console)
        return;
    ShowWindow(g_console, visible ? SW_RESTORE : SW_HIDE);
    if (visible)
        SetForegroundWindow(g_console);
    g_consoleHidden.store(!visible);
}

void popupMenu(HWND window) {
    HMENU menu = CreatePopupMenu();
    if (!menu)
        return;
    const bool hidden = g_consoleHidden.load();
    AppendMenuW(menu, MF_STRING, hidden ? ID_SHOW : ID_HIDE,
                hidden ? L"Show the console" : L"Hide to the tray");
    AppendMenuW(menu, MF_STRING, ID_OUTPUT, L"Open the output folder");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_STOP,
                L"Stop after the current step (saves a frame)");

    POINT point{};
    GetCursorPos(&point);
    // Documented dance: the menu will not close on a click elsewhere without
    // these two calls around it.
    SetForegroundWindow(window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, point.x, point.y, 0,
                   window, nullptr);
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

LRESULT CALLBACK wndProc(HWND window, UINT message, WPARAM wParam,
                         LPARAM lParam) {
    switch (message) {
        case WM_TRAY:
            if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
                showConsole(g_consoleHidden.load());
            else if (LOWORD(lParam) == WM_RBUTTONUP ||
                     LOWORD(lParam) == WM_CONTEXTMENU)
                popupMenu(window);
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_SHOW: showConsole(true); break;
                case ID_HIDE: showConsole(false); break;
                case ID_OUTPUT:
                    if (!g_outputDir.empty())
                        ShellExecuteW(nullptr, L"open", g_outputDir.c_str(),
                                      nullptr, nullptr, SW_SHOWNORMAL);
                    break;
                case ID_STOP:
                    g_stop.store(true);
                    setTooltip(widen(g_title) + L" - stopping after this step");
                    break;
                default: break;
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wParam, lParam);
    }
}

void trayThread() {
    // The tray icon and the taskbar interface both live on this thread, so the
    // apartment is initialised here and nowhere else.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEXW cls{};
    cls.cbSize = sizeof(cls);
    cls.lpfnWndProc = wndProc;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"FluidSolverTrayWindow";
    RegisterClassExW(&cls);

    g_window = CreateWindowExW(0, cls.lpszClassName, L"Fluid Solver", 0, 0, 0, 0,
                               0, HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
    if (!g_window) {
        g_ready.store(true);
        CoUninitialize();
        return;
    }

    g_icon = NOTIFYICONDATAW{};
    g_icon.cbSize = sizeof(g_icon);
    g_icon.hWnd = g_window;
    g_icon.uID = 1;
    g_icon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_icon.uCallbackMessage = WM_TRAY;
    // Resource 1 is the application icon, put there by src/app.rc.in. A build
    // without the resource script falls back to the generic application icon
    // rather than to nothing at all.
    // LoadImage rather than LoadIcon: LoadIcon always returns the large
    // variant, which the shell then squashes into the tray's 16 pixels and
    // makes a mess of. The .ico carries a real 16x16 drawing, and asking for
    // the small-icon metric is what picks it.
    g_icon.hIcon = static_cast<HICON>(LoadImageW(
        cls.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR));
    if (!g_icon.hIcon)
        // IDI_APPLICATION spelled out: the macro expands to MAKEINTRESOURCE,
        // which is the ANSI form unless UNICODE is defined, and this project
        // does not define it.
        g_icon.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    // The balloon gets the 32x32 drawing out of the same .ico.
    g_balloonIcon = static_cast<HICON>(LoadImageW(
        cls.hInstance, MAKEINTRESOURCEW(1), IMAGE_ICON,
        GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
        LR_DEFAULTCOLOR));
    copyInto(g_icon.szTip, widen(g_title));
    g_iconLive = Shell_NotifyIconW(NIM_ADD, &g_icon) != FALSE;

    if (SUCCEEDED(CoCreateInstance(kCLSID_TaskbarList, nullptr,
                                   CLSCTX_INPROC_SERVER, kIID_ITaskbarList3,
                                   reinterpret_cast<void**>(&g_taskbar)))) {
        if (FAILED(g_taskbar->lpVtbl->HrInit(g_taskbar))) {
            g_taskbar->lpVtbl->Release(g_taskbar);
            g_taskbar = nullptr;
        }
    }

    g_ready.store(true);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (g_iconLive) {
        Shell_NotifyIconW(NIM_DELETE, &g_icon);
        g_iconLive = false;
    }
    if (g_taskbar) {
        g_taskbar->lpVtbl->Release(g_taskbar);
        g_taskbar = nullptr;
    }
    CoUninitialize();
}

BOOL WINAPI consoleHandler(DWORD type) {
    if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT &&
        type != CTRL_CLOSE_EVENT)
        return FALSE;
    // The first one asks for a clean stop: the step finishes, the frame is
    // written, the run returns. A second one means the user is not willing to
    // wait for that, so it is let through to the default handler and the
    // process dies as it always did.
    if (g_stop.exchange(true))
        return FALSE;
    std::fputs("\nStopping after this step - the frame is still being "
               "written. Ctrl+C again to kill it now.\n", stderr);
    return TRUE;
}

#else

// --------------------------------------------------------------- POSIX -----

// The same bargain as the Windows console handler: the first Ctrl+C asks for a
// clean stop, a second one is left to the default disposition and kills the
// process. Reset to SIG_DFL rather than counted, so nothing here has to be
// async-signal-safe beyond one store.
void interruptHandler(int signalNumber) {
    g_stop.store(true);
    std::signal(signalNumber, SIG_DFL);
}

bool terminalTakesTitles() {
    if (!isatty(STDOUT_FILENO))
        return false;
    const char* term = std::getenv("TERM");
    if (!term || !*term)
        return false;
    // "dumb" is the one value that promises nothing at all.
    return std::string(term) != "dumb";
}

void setTitle(const std::string& text) {
    if (!terminalTakesTitles())
        return;
    // OSC 0: set both the icon name and the window title, which is what the
    // taskbar entry and the Dock label read.
    std::cout << "\033]0;" << text << "\007" << std::flush;
}

#endif

}   // namespace

void requestStop() { g_stop.store(true); }

// A file called "stop" in the output folder asks the run to finish the step it
// is on, write the frame and come back - the same clean stop as Ctrl+C or the
// tray menu, and the only one that works when the run has no console of its
// own to press Ctrl+C in. That is what the window's Stop button now makes,
// instead of killing the process in the middle of writing a two hundred
// megabyte frame; and from a shell it is one command:
//
//     echo > output/run-.../stop            (cmd)
//     touch output/run-.../stop             (bash)
//
// Checked twice a second rather than every step: a step can be a millisecond
// on a small grid, and asking the filesystem that often for a file that is
// almost never there is a waste of a syscall.
bool stopRequested() {
    if (g_stop.load())
        return true;
    if (g_stopFile.empty())
        return false;
    const auto now = std::chrono::steady_clock::now();
    if (now < g_nextStopCheck)
        return false;
    g_nextStopCheck = now + std::chrono::milliseconds(500);
    std::error_code ignored;
    if (std::filesystem::exists(g_stopFile, ignored)) {
        std::cout << "\n  stop file found - finishing this step and saving.\n";
        g_stop.store(true);
        return true;
    }
    return false;
}

void begin(const std::string& title, double startAt, double total,
           const std::string& outputDir) {
    g_title = title;
    g_start = startAt;
    g_total = total;
    g_lastPermille = -1;
    g_current = startAt;
    g_stop.store(false);
    g_tracking.store(true);
    g_stopFile = outputDir.empty()
        ? std::filesystem::path()
        : std::filesystem::path(outputDir) / "stop";
    g_nextStopCheck = std::chrono::steady_clock::now();
    // A stop file left behind by the previous run would stop this one before
    // it started.
    if (!g_stopFile.empty()) {
        std::error_code ignored;
        std::filesystem::remove(g_stopFile, ignored);
    }

#if defined(_WIN32)
    // The console window keeps whatever title it was launched with, which is
    // the full path of the executable when it was double-clicked and the name
    // of the shell when it was not. Neither is what the taskbar button and the
    // Task Manager entry should say.
    SetConsoleTitleW(widen(CFD_APP_NAME).c_str());
    // Installed whether or not the tray is on: Ctrl+C asking for a clean stop
    // - one that finishes the step and writes the frame - beats killing the
    // process halfway through a file, and that is worth having in every build.
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#else
    std::signal(SIGINT, interruptHandler);
    std::signal(SIGTERM, interruptHandler);
#endif

    if (!runtime::trayEnabled()) {
        g_active.store(false);
        return;
    }
    g_active.store(true);

#if defined(_WIN32)
    g_console = GetConsoleWindow();
    g_outputDir = widen(outputDir);
    {
        std::lock_guard<std::mutex> guard(g_mutex);
        if (!g_thread.joinable()) {
            g_ready.store(false);
            g_thread = std::thread(trayThread);
            // A few milliseconds at most; the icon has to exist before the
            // first update() tries to change its tooltip.
            while (!g_ready.load())
                Sleep(5);
        }
    }
    if (g_taskbar && g_console)
        g_taskbar->lpVtbl->SetProgressState(g_taskbar, g_console, 0x2);   // normal
    setTooltip(widen(statusText(startAt)));
#else
    (void)outputDir;
    setTitle(statusText(startAt));
#endif
}

void update(double current) {
    if (!g_tracking.load())
        return;
    g_current = current;
    if (!g_active.load())
        return;
    const int permille = permilleOf(current);
    if (permille == g_lastPermille)
        return;
    g_lastPermille = permille;

#if defined(_WIN32)
    setTooltip(widen(statusText(current)));
    if (g_taskbar && g_console)
        g_taskbar->lpVtbl->SetProgressValue(
            g_taskbar, g_console, static_cast<ULONGLONG>(permille), 1000ull);
#else
    setTitle(statusText(current));
#endif
}

std::string statusLine() {
    if (!g_tracking.load() || !(g_total > g_start))
        return std::string();
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "%.4g / %.4g s (%d%%)", g_current,
                  g_total, permilleOf(g_current) / 10);
    return std::string(buffer);
}

void finish(bool ok) {
    g_tracking.store(false);
    if (!g_stopFile.empty()) {
        std::error_code ignored;
        std::filesystem::remove(g_stopFile, ignored);
        g_stopFile.clear();
    }
    if (!g_active.load())
        return;
#if defined(_WIN32)
    if (g_taskbar && g_console)
        g_taskbar->lpVtbl->SetProgressState(g_taskbar, g_console,
                                            ok ? 0x0 : 0x4);   // none : error
    if (g_consoleHidden.load())
        showConsole(true);
    balloon(widen(g_title),
            ok ? L"The simulation finished. The frames are in output."
               : L"The simulation stopped early. The last frame was saved.",
            !ok);
#else
    setTitle(g_title + (ok ? " - finished" : " - stopped"));
#endif
}

void shutdown() {
    if (!g_active.exchange(false))
        return;
#if defined(_WIN32)
    std::lock_guard<std::mutex> guard(g_mutex);
    if (g_window) {
        PostMessageW(g_window, WM_CLOSE, 0, 0);
        g_window = nullptr;
    }
    if (g_thread.joinable())
        g_thread.join();
    SetConsoleCtrlHandler(consoleHandler, FALSE);
#else
    setTitle(CFD_APP_NAME);
#endif
}

}   // namespace progress
