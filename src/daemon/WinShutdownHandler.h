#pragma once

/// @file WinShutdownHandler.h
/// @brief Lets Windows end the daemon the way a signal does elsewhere.
///
/// Ctrl+C, a closed console, logoff and system shutdown used to kill emulecored
/// without DaemonApp::stop(): no .part.met, known.met or preferences were saved,
/// and every part file written since its last save was rehashed at the next start.
/// WM_CLOSE to its hidden window (class kWindowClass) stops it the same way: the
/// GUI's stop for a core it cannot reach over IPC, and taskkill without /F.
/// A no-op on other systems, which have their signals.

#include <functional>

namespace eMule::WinShutdownHandler {

/// The hidden window's class. The GUI looks it up by this name (main.cpp
/// stopCoreProcess), so keep both in step.
inline constexpr wchar_t kWindowClass[] = L"eMuleQtCoreShutdownWindow";

/// @p shutdownNow saves everything and must be safe to call twice; main thread.
/// It runs from inside the event loop when the session ends, because Windows may
/// kill the process as soon as the notification returns.
void install(std::function<void()> shutdownNow);

/// main() is past its own shutdown: a console handler waiting for it may return.
void finished();

} // namespace eMule::WinShutdownHandler
