#pragma once

/// @file Ed3kLiveWatch.h
/// @brief Watching an ed3k live stream from the GUI: the answer of WatchEd3kLive /
/// WatchEd3kLiveFile shown (the stream played from the core's loopback when the core is on
/// this machine, the ed3k live list opened; or the error, e.g. §2.3 "can't play"), and a
/// channel file found by an ed3k channel search (SearchType::Ed3kLive) watched: fetched by
/// the core, the answer may come later (PushEd3kLiveFileWatched).

#include <QCborMap>
#include <QString>

class QWidget;

namespace eMule {

namespace Ipc {
class IpcMessage;
}
class IpcClient;

namespace Ed3kLiveWatch {

/// The answer of a watch ([ok, {id, url} | {error}]) shown to the user.
void showReply(QWidget* window, IpcClient* ipc, const Ipc::IpcMessage& reply);
void show(QWidget* window, IpcClient* ipc, bool ok, const QCborMap& data);
/// Watches the channel file @p root (hex) of @p size found by an ed3k channel search.
void watchFound(QWidget* window, IpcClient* ipc, const QString& root, quint64 size);

} // namespace Ed3kLiveWatch

} // namespace eMule
