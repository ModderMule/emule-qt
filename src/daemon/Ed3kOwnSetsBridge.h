#pragma once

/// @file Ed3kOwnSetsBridge.h
/// @brief My ed3k Sets over IPC (requests 1145-1148, push 1165), for Ed3kBridge. Free of the
/// ed3k module like Ed3kBridge.h: without it the list is empty and every other request answers
/// [false, {error}]. A hidden set's link is a credential: only GetEd3kOwnSetLink carries it,
/// never logged.

#include "IpcMessage.h"

#include <QCborMap>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kOwnSets;
} // namespace ed3k

namespace Ed3kOwnSetsBridge {

/// Set by DaemonApp, null when absent.
void setOwnSets(ed3k::Ed3kOwnSets* sets);

/// Whether an opcode is one of 1145-1148.
[[nodiscard]] bool isOwnSetsRequest(Ipc::IpcMsgType type);

/// Answers an own-sets request through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// GetEd3kOwnSets, also what PushEd3kOwnSetsChanged carries.
[[nodiscard]] QCborMap ownSetsToCbor();

/// Broadcasts PushEd3kOwnSetsChanged for as long as @p sets lives.
void connectPushes(ed3k::Ed3kOwnSets& sets, std::function<void(const Ipc::IpcMessage&)> broadcast);

} // namespace Ed3kOwnSetsBridge
} // namespace eMule
