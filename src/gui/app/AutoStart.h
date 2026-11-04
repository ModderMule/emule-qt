#pragma once

/// @file AutoStart.h
/// @brief Register/unregister the GUI to start with the operating system.
///
/// macOS: writes/removes a LaunchAgent plist in ~/Library/LaunchAgents.
/// Linux: writes/removes a .desktop file in ~/.config/autostart.
/// Windows: writes/removes a registry key in HKCU\...\Run.

#include <QString>

namespace eMule {

/// Register or unregister the application for autostart on login.
void setAutoStart(bool enabled);

/// Check whether the application is currently registered for autostart.
[[nodiscard]] bool isAutoStartEnabled();

/// Rewrites a stale entry, e.g. one of an older version that started the bare
/// .bin (#129). Never creates one. Linux only, a no-op elsewhere.
void refreshAutoStart();

} // namespace eMule
