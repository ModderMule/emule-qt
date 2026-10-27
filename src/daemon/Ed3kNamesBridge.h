#pragma once

/// @file Ed3kNamesBridge.h
/// @brief ed3k readable names and websites over IPC (1010-1019 requests, 1060-1063 pushes),
/// for Ed3kBridge. Free of the ed3k module like Ed3kBridge.h: without it every request
/// answers [false, {error: 0}] and GetEd3kNames empty lists.

#include "IpcMessage.h"

#include <QCborMap>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kNames;
class Ed3kSiteServer;
} // namespace ed3k

namespace Ed3kNamesBridge {

/// Set by DaemonApp, null when absent.
void setNames(ed3k::Ed3kNames* names, ed3k::Ed3kSiteServer* sites);

/// Whether an opcode is one of 1010-1019.
[[nodiscard]] bool isNamesRequest(Ipc::IpcMsgType type);

/// Answers a names request through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// Broadcasts the pushes of @p names (and the confirmations of @p sites) through
/// @p broadcast for as long as they live.
void connectPushes(ed3k::Ed3kNames& names, ed3k::Ed3kSiteServer* sites, std::function<void(const Ipc::IpcMessage&)> broadcast);

} // namespace Ed3kNamesBridge
} // namespace eMule
