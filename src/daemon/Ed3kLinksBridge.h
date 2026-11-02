#pragma once

/// @file Ed3kLinksBridge.h
/// @brief The other ed3k links the GUI opened (batch 15), for Ed3kBridge. The GUI asks the
/// user first (it shows what the link names), these only apply them:
///
/// - OpenEd3kNodeLink (1032): an `ed3k:node` link (Protocol.md §13.2, bootstrap.md §1 source 3)
///   → parseNodeLink → bootstrap() with each endpoint.
/// - AddEd3kCacheLink (1049): an `ed3k:cache` link (http-source.md §4.1, a credential: never
///   logged) → cacheConfigure(every link) → appended to the pref ed3kCacheServers (encrypted).
///   The pref's links are applied at each start of the node.
/// - OpenEd3kNodeListFile (1137): a node list file opened by hand (bootstrap.md §3) →
///   bootstrapOpenFile.
///
/// Failure: [false, {error}] with error 0 (no ed3k node) or an ed3k::Error. Free of the ed3k
/// module like Ed3kBridge.h.

#include "IpcMessage.h"

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kSession;
} // namespace ed3k

namespace Ed3kLinksBridge {

/// From Ed3kBridge::setSession: the pref ed3kCacheServers is applied at each start of
/// @p session. Null: none.
void setSession(ed3k::Ed3kSession* session);

/// Answers 1032, 1049, 1137 through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

} // namespace Ed3kLinksBridge
} // namespace eMule
