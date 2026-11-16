#pragma once

/// @file BtCreateBridge.h
/// @brief Torrent creation jobs of the daemon (#191): IPC CreateTorrent, CancelTorrentCreation,
/// GetCreatedTorrent; pushes PushTorrentCreation / PushTorrentCreated.
///
/// Works with the BitTorrent engine off (only seeding needs it). Header free of the bt
/// module like BtBridge.h.

#include "IpcMessage.h"

#include <functional>

namespace eMule::BtCreateBridge {

/// Where the pushes go (IpcServer::broadcast); set by DaemonApp.
void setPushSink(std::function<void(const Ipc::IpcMessage&)> sink);

/// Called with the torrent id when a created torrent was added for seeding: the ed3k
/// join hashes it (Ed3kTorrentJoin::adoptCreated). Set by DaemonApp.
void setSeededHook(std::function<void(const QString& id)> hook);

/// Whether this is one of the creation opcodes.
[[nodiscard]] bool isCreateRequest(Ipc::IpcMsgType type);

/// Answers a creation request through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// Cancels every job and waits for them; before the daemon stops.
void shutdown();

} // namespace eMule::BtCreateBridge
