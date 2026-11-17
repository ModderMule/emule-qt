#pragma once

/// @file PanelLayout.h
/// @brief Shared layout pieces of the panels with a list + controls column.

class QScrollArea;
class QSplitter;
class QWidget;

namespace eMule::PanelLayout {

/// Wraps a controls column in a vertical-only scroll area. Its full height would
/// otherwise pin the minimum height of whatever holds it (window, splitter pane).
/// Keeps the column's min/max width, plus room for the scroll bar.
[[nodiscard]] QScrollArea* scrollableColumn(QWidget* column);

/// Gives every pane of the vertical @p splitter the same minimum height: a tab/title
/// row plus a few list rows. Same drag range on every panel, whatever the panes hold.
void setPaneMinHeights(QSplitter* splitter);

} // namespace eMule::PanelLayout
