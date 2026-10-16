#pragma once

/// @file Appearance.h
/// @brief Theme (light/dark) override and app-wide font scaling (issue #18).

#include <QtGlobal>

namespace eMule {

/// UiState::colorScheme() values.
enum class ThemeChoice : int { System = 0, Light = 1, Dark = 2 };

/// Asks Qt for @p scheme. Where the platform ignores the request (some Linux
/// themes) and @p allowFallback is set, switches to Fusion with a matching palette.
/// Returns false when the scheme could not be applied live (restart needed).
bool applyColorScheme(int scheme, bool allowFallback);

/// True when lists are drawn dark: the forced choice, else the system scheme.
[[nodiscard]] bool isDarkScheme();

/// Scales the app font to @p percent of the system default. Call once, before
/// any widget is created.
void applyFontScale(int percent);

/// Factor set by applyFontScale(), 1.0 by default.
[[nodiscard]] qreal fontScale();

/// A hardcoded point/pixel size scaled by fontScale(), at least 1.
[[nodiscard]] int scaledPointSize(int basePt);
[[nodiscard]] int scaledPixelSize(int basePx);

} // namespace eMule
