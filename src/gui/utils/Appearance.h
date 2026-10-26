#pragma once

/// @file Appearance.h
/// @brief Theme (light/dark) override and app-wide font scaling (issue #18).

#include <QColor>
#include <QString>
#include <QtGlobal>

class QSplitter;

namespace eMule {

/// UiState::colorScheme() values.
enum class ThemeChoice : int { System = 0, Light = 1, Dark = 2 };

/// Asks Qt for @p scheme. Where the platform ignores the request (some Linux
/// themes, or a Windows 10 style that cannot draw dark) and @p allowFallback is set,
/// switches to Fusion with a matching palette.
/// Returns false when the scheme could not be applied live (restart needed).
bool applyColorScheme(int scheme, bool allowFallback);

/// Repaints after a theme switch; repolishes widgets with a style sheet, which keep
/// the old palette otherwise.
void refreshThemedWidgets();

/// Fills the splitter's handles with palette(mid), like a "QSplitter::handle" style
/// sheet but without one: a style sheet freezes the palette of the whole subtree.
void useThemedSplitterHandles(QSplitter* splitter);

/// True when lists are drawn dark: the forced choice, else the system scheme.
[[nodiscard]] bool isDarkScheme();

/// Semantic text colours. Each has a light and a dark value that keeps at least
/// 3:1 contrast on the matching window/base background (issue #18).
enum class TextRole : int {
    Info,     ///< log info lines, static servers
    Success,  ///< server log success (MFC blue)
    Warning,  ///< server log warning (MFC purple)
    Error,    ///< errors, failures
    Good,     ///< connected / HighID / ok
    Caution,  ///< LowID, firewalled, connecting, log warnings
};

/// @p light or @p dark, by isDarkScheme().
[[nodiscard]] QColor themed(const QColor& light, const QColor& dark);

/// Colour of @p role for the current scheme.
[[nodiscard]] QColor roleColor(TextRole role);

/// roleColor() as "#rrggbb", for HTML and style sheets.
[[nodiscard]] QString roleColorName(TextRole role);

/// Scales the app font to @p percent of the system default. Call once, before
/// any widget is created.
void applyFontScale(int percent);

/// Factor set by applyFontScale(), 1.0 by default.
[[nodiscard]] qreal fontScale();

/// A hardcoded point/pixel size scaled by fontScale(), at least 1.
[[nodiscard]] int scaledPointSize(int basePt);
[[nodiscard]] int scaledPixelSize(int basePx);

} // namespace eMule
