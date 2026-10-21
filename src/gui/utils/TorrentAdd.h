#pragma once

/// @file TorrentAdd.h
/// @brief Send an AddTorrentFile / AddMagnet request and say how it went.
///
/// Every BitTorrent intake path — the file dialog, a drop, a Finder open, the
/// command line, a clicked magnet, a pasted one — ends in the same reply
/// {outcome, id, message}. One place turns it into a status-bar line or a box,
/// as NzbAdd does for Usenet.

#include "IpcMessage.h"

#include <QString>

#include <functional>

class QWidget;

namespace eMule {

class IpcClient;

namespace gui {

/// Mirrors bt::BtAddOutcome (src/bt/BtTypes.h). The GUI never links eMule::Bt,
/// so the value arrives as an int. Values are IPC: append only.
enum class BtAddOutcome : int {
    Added = 0,
    Duplicate = 1,
    Invalid = 2,
    NotRunning = 3,
    Failed = 4,
};

/// One AddTorrentFile / AddMagnet reply, decoded.
struct BtAddReply {
    BtAddOutcome outcome = BtAddOutcome::Failed;
    QString id;
    QString detail;      ///< the daemon's English message
    bool dropped = false;///< connection lost, no verdict
};

/// Decode a reply: Result(true, {outcome,id,message}), or Error 503 = not running.
[[nodiscard]] BtAddReply decodeTorrentAddReply(const Ipc::IpcMessage& resp);

/// The user-facing sentence for @p outcome about @p title. @p detail is the
/// daemon's English message, appended for Invalid/Failed when not empty.
[[nodiscard]] QString btAddOutcomeText(BtAddOutcome outcome, const QString& title,
                                       const QString& detail = {});

/// AddTorrentFile [data, savePath, paused].
[[nodiscard]] Ipc::IpcMessage torrentFileRequest(const QByteArray& data, const QString& savePath,
                                                 bool paused);

/// AddMagnet [uri, savePath, paused].
[[nodiscard]] Ipc::IpcMessage magnetRequest(const QString& uri, const QString& savePath,
                                            bool paused);

/// Send @p request; Added and Duplicate go to the status bar, the rest to a box
/// one event-loop turn later (never inside the reply's stack, see NzbAdd.h).
/// @p done gets whether a torrent was added, once.
void sendTorrentAdd(IpcClient* ipc, QWidget* parent, const QString& title,
                    Ipc::IpcMessage request, std::function<void(bool added)> done = {});

} // namespace gui
} // namespace eMule
