#pragma once

/// @file PanelLayout.h
/// @brief Shared layout pieces of the panels with a list + controls column.

class QScrollArea;
class QSplitter;
class QWidget;

namespace eMule::PanelLayout {

/// Wraps a controls column in a vertical-only scroll area. Its full height would
/// otherwise pin the minimum height of whatever holds it (window, splitter pane).
/// Keeps the column's min/max width, plus room for the scroll bar. Scrolls
/// sideways when the content needs more width than the pane has.
[[nodiscard]] QScrollArea* scrollableColumn(QWidget* column);

/// @p basePx (sized for the default font) grown with @p widget's font, so a larger
/// text size (font scale, Windows "Text size") doesn't clip fixed-width columns.
[[nodiscard]] int fontScaled(const QWidget* widget, int basePx);

/// Gives every pane of the vertical @p splitter the same minimum height: a tab/title
/// row plus a few list rows, or more if a pane's content needs it. Same drag range on
/// every panel as long as the content fits. Call after the panes are filled.
void setPaneMinHeights(QSplitter* splitter);

} // namespace eMule::PanelLayout
