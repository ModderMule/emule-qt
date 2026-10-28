#pragma once

/// @file Ed3kPaidBridge.h
/// @brief ed3k paid subscriptions over IPC (requests 1035-1038, pushes 1064-1066), for
/// Ed3kBridge. Free of the ed3k module like Ed3kBridge.h: without it the list is empty and
/// every other request answers [false, {error}].

#include "IpcMessage.h"

#include <QCborMap>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kPaid;
} // namespace ed3k

namespace Ed3kPaidBridge {

/// Set by DaemonApp, null when absent.
void setPaid(ed3k::Ed3kPaid* paid);

/// Whether an opcode is one of 1035-1038.
[[nodiscard]] bool isPaidRequest(Ipc::IpcMsgType type);

/// Answers a paid request through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// GetEd3kPaid, also what PushEd3kPaidChanged carries.
[[nodiscard]] QCborMap paidToCbor();

/// Broadcasts the pushes for as long as @p paid lives.
void connectPushes(ed3k::Ed3kPaid& paid, std::function<void(const Ipc::IpcMessage&)> broadcast);

} // namespace Ed3kPaidBridge
} // namespace eMule
