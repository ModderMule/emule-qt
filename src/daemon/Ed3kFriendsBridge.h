#pragma once

/// @file Ed3kFriendsBridge.h
/// @brief ed3k friends and chat over IPC (1001-1009 requests, 1051-1056 pushes), for
/// Ed3kBridge. Free of the ed3k module like Ed3kBridge.h: without it every request answers
/// [false, {error}] and GetEd3kFriends an empty list.

#include "IpcMessage.h"

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kFriends;
} // namespace ed3k

namespace Ed3kFriendsBridge {

/// Set by DaemonApp, null when absent.
void setFriends(ed3k::Ed3kFriends* friends);

/// Whether an opcode is one of 1001-1009.
[[nodiscard]] bool isFriendsRequest(Ipc::IpcMsgType type);

/// Answers a friends/chat request through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// Broadcasts the pushes of @p friends through @p broadcast for as long as it lives.
void connectPushes(ed3k::Ed3kFriends& friends, std::function<void(const Ipc::IpcMessage&)> broadcast);

} // namespace Ed3kFriendsBridge
} // namespace eMule
