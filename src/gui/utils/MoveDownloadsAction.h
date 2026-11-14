#pragma once

/// @file MoveDownloadsAction.h
/// @brief "Move to..." for the download lists of every network (#187): asks for a folder
/// (a picker for a local core, a typed path for a remote one) and sends one MoveDownloads.

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

class QWidget;

namespace eMule {

class IpcClient;

namespace MoveDownloadsAction {

/// The MoveDownloads network field.
enum class Network : int { Ed2k = 0, Ed3k = 1, BitTorrent = 2, Usenet = 3, Hoster = 4 };

/// The ids of one network.
struct Group {
    Network network;
    QStringList ids;
};

/// Asks for one folder (starting at @p startDir) and moves every group's ids there, one
/// request per group; @p names (id → name) label the failures, listed in one box.
/// @p refresh runs once per answer.
void run(QWidget* parent, IpcClient* ipc, QList<Group> groups, const QHash<QString, QString>& names,
         const QString& startDir, std::function<void()> refresh);

/// The menu entry's text, one for all lists.
[[nodiscard]] QString menuText();

} // namespace MoveDownloadsAction
} // namespace eMule
