#pragma once

/// @file Ed3kKadBridge.h
/// @brief The ed3k Kad window over IPC (1200-1204), for Ed3kBridge:
///
/// - GetEd3kKadContacts (1200): the routing table (kadContacts), with country codes.
/// - GetEd3kKadLookups (1201): running and ended lookups (kadLookups).
/// - GetEd3kKadLookupGraph (1202): candidates of one lookup for the graph (kadLookupGraph).
/// - ExportEd3kNodeList (1203): own contacts as a signed node list (Ed3kBootstrap::exportList).
/// - FetchEd3kNodeLists (1204): node lists from the sources now (Ed3kBootstrap::fetchNow).
///
/// Failure: [false, {error}] with error 0 (no ed3k node) or an ed3k::Error, plus `message`
/// where one helps. Free of the ed3k module like Ed3kBridge.h.

#include "IpcMessage.h"

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kBootstrap;
class Ed3kSession;
} // namespace ed3k

namespace Ed3kKadBridge {

/// From Ed3kBridge::setSession. Null: none.
void setSession(ed3k::Ed3kSession* session);
/// From Ed3kBridge::setBootstrap. Null: none.
void setBootstrap(ed3k::Ed3kBootstrap* bootstrap);

/// Answers 1200-1204 through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

} // namespace Ed3kKadBridge
} // namespace eMule
