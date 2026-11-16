#pragma once

/// @file Ed3kLinkMenu.h
/// @brief "Copy ed3k Links" / "ed3k Links..." for the Transfers and Shared Files menus, the
/// counterparts of the eD2K link entries (only while ed3k is on). The daemon writes the
/// links (GetEd3kFileLinks 1149): eD2K files by their root once hashed, ed3k rows by theirs.

#include <QList>
#include <QString>

class QMenu;
class QWidget;

namespace eMule {

class IpcClient;

namespace Ed3kLinkMenu {

/// One file: MD4 (32 hex) or ed3k root (64 hex), with its name and size.
struct File {
    QString hash;
    QString name;
    qint64 size = 0;
};

/// Adds both entries to @p menu while ed3k is on: grey until the core answers that at least
/// one of @p files has a root (asked once on open; the click uses that answer).
void addActions(QMenu* menu, QWidget* parent, IpcClient* ipc, const QList<File>& files);

/// The entries' texts (menus that grey all but a few keep them on).
[[nodiscard]] QString copyText();
[[nodiscard]] QString showText();

} // namespace Ed3kLinkMenu
} // namespace eMule
