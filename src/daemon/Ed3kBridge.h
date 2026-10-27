#pragma once

/// @file Ed3kBridge.h
/// @brief The daemon's one implementation of each ed3k request (IPC 1000-1049).
///
/// Like BtBridge.h the header stays free of the ed3k module: the daemon builds without
/// it (EMULE_ED3K=OFF), and then GetEd3kStatus answers {available: false}.

#include "IpcMessage.h"

#include <QCborMap>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kSession;
} // namespace ed3k

namespace Ed3kBridge {

/// The node the requests go to; set by DaemonApp, null when absent.
void setSession(ed3k::Ed3kSession* session);
[[nodiscard]] ed3k::Ed3kSession* session();

/// Whether the daemon was built with the ed3k module.
[[nodiscard]] bool available();

/// Whether an opcode is one of the ed3k block (1000-1049).
[[nodiscard]] bool isEd3kRequest(Ipc::IpcMsgType type);

/// Answers an ed3k request through `reply`. False: not an ed3k opcode.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// GetEd3kStatus, also what PushEd3kStatus carries (keys in IpcProtocol.h). Rates come
/// from the running BandwidthCoordinator (0 without one).
[[nodiscard]] QCborMap statusToCbor();

} // namespace Ed3kBridge
} // namespace eMule
