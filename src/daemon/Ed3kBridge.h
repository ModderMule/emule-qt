#pragma once

/// @file Ed3kBridge.h
/// @brief The daemon's one implementation of each ed3k request (IPC 1000-1049).
///
/// Like BtBridge.h the header stays free of the ed3k module: the daemon builds without
/// it (EMULE_ED3K=OFF), and then GetEd3kStatus answers {available: false}.

#include "IpcMessage.h"

#include <QByteArray>
#include <QCborMap>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kSession;
class Ed3kBootstrap;
} // namespace ed3k

namespace Ed3kBridge {

/// The node the requests go to; set by DaemonApp, null when absent.
void setSession(ed3k::Ed3kSession* session);
[[nodiscard]] ed3k::Ed3kSession* session();

/// Node list sources and their asks (OpenEd3kNodesLink, AnswerEd3kBootstrapAsk); set by
/// DaemonApp, null when absent.
void setBootstrap(ed3k::Ed3kBootstrap* bootstrap);

/// Whether the daemon was built with the ed3k module.
[[nodiscard]] bool available();

/// Whether an opcode is one of the ed3k block (1000-1049).
[[nodiscard]] bool isEd3kRequest(Ipc::IpcMsgType type);

/// Answers an ed3k request through `reply`. False: not an ed3k opcode.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// The node's UDP port; 0 when it is not running.
[[nodiscard]] quint16 udpPort();
/// A PROBE to the node itself for the port test (PortTestRunner); empty when not running.
[[nodiscard]] QByteArray portTestProbe(quint64 nonce);
/// Families a PROBE of that nonce arrived over: bit 0 IPv4, bit 1 IPv6.
[[nodiscard]] int portTestSeen(quint64 nonce);

/// GetEd3kStatus, also what PushEd3kStatus carries (keys in IpcProtocol.h). Rates come
/// from the running BandwidthCoordinator (0 without one).
[[nodiscard]] QCborMap statusToCbor();

} // namespace Ed3kBridge
} // namespace eMule
