#pragma once

/// @file Ed3kFiltersBridge.h
/// @brief ed3k filter lists over IPC (1130-1139 requests, push 1170, filter-list.md), for
/// Ed3kBridge. Free of the ed3k module like Ed3kBridge.h: without it every request answers
/// [false, {error: 0}].

#include "IpcMessage.h"

#include <QCborMap>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kFilters;
} // namespace ed3k

namespace Ed3kFiltersBridge {

/// Set by DaemonApp, null when absent.
void setFilters(ed3k::Ed3kFilters* filters);

/// Whether an opcode is one of 1130-1139, or 1042-1043 (batch 13).
[[nodiscard]] bool isFiltersRequest(Ipc::IpcMsgType type);

/// Answers a filter request through `reply`. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// GetEd3kFilters' map (running false without filters).
[[nodiscard]] QCborMap filtersToCbor();

/// Broadcasts PushEd3kFiltersChanged of @p filters through @p broadcast for as long as it lives.
void connectPushes(ed3k::Ed3kFilters& filters, std::function<void(const Ipc::IpcMessage&)> broadcast);

} // namespace Ed3kFiltersBridge
} // namespace eMule
