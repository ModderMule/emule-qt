#pragma once

/// @file Ed3kHideBridge.h
/// @brief Hiding (hops.md §1) over IPC: NewEd3kAnonKey (1047), MoveEd3kFileKey (1048), the
/// "hide" map of GetEd3kStatus, PushEd3kHideChanged (1081) and the Key of shared files (ed3k
/// downloads: Ed3kDownloadsBridge `key`). Free of the ed3k module like Ed3kBridge.h: without
/// it the requests answer 503.

#include "ipc/IpcMessage.h"

#include <QByteArray>
#include <QCborMap>
#include <QString>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kSession;
class Ed3kShared;
class Ed3kDownloads;
} // namespace ed3k

namespace Ed3kHideBridge {

/// Set by DaemonApp (null when gone).
void setTargets(ed3k::Ed3kSession* session, ed3k::Ed3kShared* shared, ed3k::Ed3kDownloads* downloads);

/// 1047, 1048. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// GetEd3kStatus "hide": {on, running, nodeId, hops, paths, built, waitingForHops, anonFiles}.
[[nodiscard]] QCborMap statusToCbor();

/// "Node" / "Anonymous" of a shared file (16 B MD4); empty: not shared over ed3k (yet).
[[nodiscard]] QString sharedKey(const QByteArray& md4);

/// PushEd3kHideChanged (and PushEd3kStatus) through @p broadcast when hiding moved; at most
/// twice a second.
void connectPushes(ed3k::Ed3kSession& session, std::function<void(const Ipc::IpcMessage&)> broadcast);

} // namespace Ed3kHideBridge
} // namespace eMule
