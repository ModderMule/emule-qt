#pragma once

/// @file Ed3kDownloadsBridge.h
/// @brief Plain ed3k downloads over IPC (requests 1140-1143, push 1180), for Ed3kBridge.
/// Free of the ed3k module like Ed3kBridge.h: without it GetEd3kDownloads answers an empty
/// list and every other request [false, {error}].
/// A row's bar: `blocks` (held, encodeBlockRuns of BlockRuns.h) of `blockSize` bytes,
/// `pending` (asked for; only while running), `avail` (a byte per `pieceSize` piece: sources
/// holding it; only when one told its bitmap).

#include "IpcMessage.h"

#include <QCborMap>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kDownloads;
} // namespace ed3k

namespace Ed3kDownloadsBridge {

/// Set by DaemonApp, null when absent.
void setDownloads(ed3k::Ed3kDownloads* downloads);

/// Whether an opcode is one of 1140-1149.
[[nodiscard]] bool isDownloadsRequest(Ipc::IpcMsgType type);

/// Answers a downloads request through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// GetEd3kDownloads, also what PushEd3kDownloads carries.
[[nodiscard]] QCborMap listToCbor();

/// Broadcasts PushEd3kDownloads on every change of @p downloads for as long as it lives.
void connectPushes(ed3k::Ed3kDownloads& downloads, std::function<void(const Ipc::IpcMessage&)> broadcast);

} // namespace Ed3kDownloadsBridge
} // namespace eMule
