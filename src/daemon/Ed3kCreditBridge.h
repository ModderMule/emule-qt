#pragma once

/// @file Ed3kCreditBridge.h
/// @brief ed3k credits over IPC (1020-1029, credit.md; stake 1023-1027 and pushes 1076-1078)
/// and the "credit" map of GetEd3kStatus, for Ed3kBridge. Free of the ed3k module like Ed3kBridge.h: without it
/// every request answers [false, {error}] and the status has no "credit".

#include "IpcMessage.h"

#include <QCborMap>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kChain;
class Ed3kCredit;
} // namespace ed3k

namespace Ed3kCreditBridge {

/// Set by DaemonApp, null when absent.
void setCredit(ed3k::Ed3kCredit* credit);
/// The chaintube chain (its status rides along, batch 13); null when absent.
void setChain(ed3k::Ed3kChain* chain);

/// Whether an opcode is one of 1020-1029.
[[nodiscard]] bool isCreditRequest(Ipc::IpcMsgType type);
/// Whether it carries the credit key (a secret): local IPC clients only.
[[nodiscard]] bool carriesKey(Ipc::IpcMsgType type);

/// Answers a credit request through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// The "credit" map of GetEd3kStatus; empty without a credit object.
[[nodiscard]] QCborMap statusToCbor();

/// The "chain" map of GetEd3kStatus: {configured, singleRemote (one remote endpoint: no
/// second provider, chaintube.md §4.3), refused (the last refused read/write, plain text; ""
/// none)}; empty without a chain object.
[[nodiscard]] QCborMap chainStatusToCbor();

/// GetEd3kStakeClaims / PushEd3kStakeClaims; empty without a credit object.
[[nodiscard]] QCborMap stakeToCbor();

/// Broadcasts PushEd3kStakeClaims / PushEd3kStakeTx / PushEd3kStakeLookup of @p credit through @p broadcast for as
/// long as it lives.
void connectPushes(ed3k::Ed3kCredit& credit, std::function<void(const Ipc::IpcMessage&)> broadcast);

} // namespace Ed3kCreditBridge
} // namespace eMule
