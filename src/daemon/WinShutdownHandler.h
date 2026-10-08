#pragma once

/// @file WinShutdownHandler.h
/// @brief Lets Windows end the daemon the way a signal does elsewhere.
///
/// Ctrl+C, a closed console, logoff and system shutdown used to kill emulecored
/// without DaemonApp::stop(): no .part.met, known.met or preferences were saved,
/// and every part file written since its last save was rehashed at the next start.
/// A no-op on other systems, which have their signals.

#include <functional>

namespace eMule::WinShutdownHandler {

/// @p shutdownNow saves everything and must be safe to call twice; main thread.
/// It runs from inside the event loop when the session ends, because Windows may
/// kill the process as soon as the notification returns.
void install(std::function<void()> shutdownNow);

/// main() is past its own shutdown: a console handler waiting for it may return.
void finished();

} // namespace eMule::WinShutdownHandler
