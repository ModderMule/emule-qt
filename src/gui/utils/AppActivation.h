#pragma once

/// @file AppActivation.h
/// @brief Bring the app itself to the front, not only one of its windows.
///
/// macOS: a window raised from the tray menu while another app is active only makes
/// the Dock icon bounce; NSApp has to be activated first. Elsewhere raise() and
/// activateWindow() already do it.

namespace eMule {

void activateApp();

} // namespace eMule
