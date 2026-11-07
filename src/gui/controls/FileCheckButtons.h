#pragma once

/// @file FileCheckButtons.h
/// @brief Select All / Select None / Invert Selection over a list of check boxes.
///
/// One button row for every "which files download" list: NZB files
/// (UsenetFileCheckList), torrent files (TorrentFileTree) and the links of a web
/// search row (WebLinksDialog), so they read and work alike.

#include <QList>
#include <functional>

class QPushButton;
class QTreeWidget;
class QWidget;

namespace eMule::FileCheckButtons {

enum class Bulk { All, None, Invert };

/// The three buttons, left aligned. @p apply runs on a click. @p buttons, when
/// given, receives them (to enable or disable later).
[[nodiscard]] QWidget* create(QWidget* parent, std::function<void(Bulk)> apply,
                              QList<QPushButton*>* buttons = nullptr);

/// @p mode on every user-checkable row of @p tree without children, in @p column.
/// Folder rows with Qt::ItemIsAutoTristate follow their files.
void applyToLeaves(QTreeWidget* tree, int column, Bulk mode);

} // namespace eMule::FileCheckButtons
