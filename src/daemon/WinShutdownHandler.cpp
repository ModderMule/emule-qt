/// @file WinShutdownHandler.cpp
/// @brief Lets Windows end the daemon the way a signal does elsewhere.

#include "WinShutdownHandler.h"

#include <QtGlobal>

#ifdef Q_OS_WIN

#include "utils/Log.h"

#include <QCoreApplication>
#include <QSemaphore>

#include <windows.h>

namespace eMule::WinShutdownHandler {

namespace {

std::function<void()> s_shutdownNow;
QSemaphore s_finished;
HWND s_window = nullptr;

// Windows ends the process itself well before this; it only bounds the wait.
constexpr int kMaxWaitMs = 30'000;

BOOL WINAPI consoleHandler(DWORD event)
{
    // Runs on a thread of its own. Nothing here may touch the core: hand over to
    // the main thread and, where returning means being killed, wait for it.
    switch (event) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        if (QCoreApplication* app = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(app, [] {
                logInfo(QStringLiteral("Console event received — shutting down gracefully..."));
                QCoreApplication::quit();
            }, Qt::QueuedConnection);
        }
        if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT)
            s_finished.tryAcquire(1, kMaxWaitMs);
        return TRUE;
    default:
        return FALSE;
    }
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (wParam) {
            // The process may be gone once this returns, so the save happens here.
            ShutdownBlockReasonCreate(hwnd, L"Saving downloads and settings");
            logInfo(QStringLiteral("Session is ending — shutting down gracefully..."));
            if (s_shutdownNow)
                s_shutdownNow();
            ShutdownBlockReasonDestroy(hwnd);
            QCoreApplication::quit();
        }
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

void install(std::function<void()> shutdownNow)
{
    s_shutdownNow = std::move(shutdownNow);

    SetConsoleCtrlHandler(consoleHandler, TRUE);

    // A daemon started detached by the GUI has no console, and a process without a
    // console is told about logoff and shutdown only through a top-level window.
    // Never shown; Qt's event dispatcher delivers its messages.
    static const wchar_t kClassName[] = L"eMuleQtCoreShutdownWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    if (RegisterClassW(&wc))
        s_window = CreateWindowExW(0, kClassName, L"eMule Qt Core", WS_OVERLAPPED,
                                   0, 0, 0, 0, nullptr, nullptr, wc.hInstance, nullptr);
    if (!s_window)
        logWarning(QStringLiteral("No shutdown window: logoff may end the daemon unsaved"));
}

void finished()
{
    s_finished.release();
}

} // namespace eMule::WinShutdownHandler

#else

namespace eMule::WinShutdownHandler {

void install(std::function<void()>) {}
void finished() {}

} // namespace eMule::WinShutdownHandler

#endif
