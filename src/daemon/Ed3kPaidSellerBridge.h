#pragma once

/// @file Ed3kPaidSellerBridge.h
/// @brief ed3k selling over IPC (requests 1033, 1034, 1040, 1041; pushes 1067, 1068), for
/// Ed3kBridge. Free of the ed3k module like Ed3kBridge.h: without it the list is empty and
/// every other request answers [false, {error}].

#include "IpcMessage.h"

#include <QCborMap>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kPaidSeller;
class Ed3kLiveBroadcast;
} // namespace ed3k

namespace Ed3kPaidSellerBridge {

/// Set by DaemonApp, null when absent; @p broadcast sells channels on air.
void setSeller(ed3k::Ed3kPaidSeller* seller, ed3k::Ed3kLiveBroadcast* broadcast);

/// Whether an opcode is one of 1033, 1034, 1040, 1041.
[[nodiscard]] bool isSellerRequest(Ipc::IpcMsgType type);

/// Answers a selling request through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// GetEd3kSelling, also what PushEd3kSellingChanged carries.
[[nodiscard]] QCborMap sellingToCbor();

/// Broadcasts the pushes for as long as @p seller lives.
void connectPushes(ed3k::Ed3kPaidSeller& seller, std::function<void(const Ipc::IpcMessage&)> broadcast);

} // namespace Ed3kPaidSellerBridge
} // namespace eMule
