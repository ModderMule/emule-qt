#pragma once

/// @file Ed3kRoomsBridge.h
/// @brief ed3k rooms over IPC (1120-1129 requests, 1160-1164 pushes), for Ed3kBridge. Free
/// of the ed3k module like Ed3kBridge.h: without it every request answers [false, {error}]
/// and GetEd3kRooms an empty list. Links with `s` are credentials: never logged here.

#include "IpcMessage.h"

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kRooms;
} // namespace ed3k

namespace Ed3kRoomsBridge {

/// Set by DaemonApp, null when absent.
void setRooms(ed3k::Ed3kRooms* rooms);

/// Whether an opcode is one of 1120-1129, or 1044 (batch 13: mute).
[[nodiscard]] bool isRoomsRequest(Ipc::IpcMsgType type);

/// Answers a rooms request through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// Broadcasts the pushes of @p rooms through @p broadcast for as long as it lives.
void connectPushes(ed3k::Ed3kRooms& rooms, std::function<void(const Ipc::IpcMessage&)> broadcast);

} // namespace Ed3kRoomsBridge
} // namespace eMule
