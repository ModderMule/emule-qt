#pragma once

/// @file Ed3kSetsBridge.h
/// @brief Followed ed3k sets and live streams over IPC (requests 1110-1119 and 1144, pushes
/// 1155-1158), for Ed3kBridge. Free of the ed3k module like Ed3kBridge.h: without it the lists
/// are empty and every other request answers [false, {error}].

#include "IpcMessage.h"

#include <QCborMap>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kSets;
class Ed3kLive;
class Ed3kSiteServer;
} // namespace ed3k

namespace Ed3kSetsBridge {

/// Set by DaemonApp, null when absent.
void setSets(ed3k::Ed3kSets* sets, ed3k::Ed3kLive* live, ed3k::Ed3kSiteServer* sites);

/// Whether an opcode is one of 1110-1119, 1144.
[[nodiscard]] bool isSetsRequest(Ipc::IpcMsgType type);

/// Answers a sets or live request through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// GetEd3kSets, also what PushEd3kSetsChanged carries.
[[nodiscard]] QCborMap setsToCbor();
/// GetEd3kLive, also what PushEd3kLiveChanged carries.
[[nodiscard]] QCborMap liveToCbor();

/// Broadcasts the pushes for as long as the objects live (@p sites may be null).
void connectPushes(ed3k::Ed3kSets& sets, ed3k::Ed3kLive& live, ed3k::Ed3kSiteServer* sites,
                   std::function<void(const Ipc::IpcMessage&)> broadcast);

} // namespace Ed3kSetsBridge
} // namespace eMule
