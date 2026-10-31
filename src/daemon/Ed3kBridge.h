#pragma once

/// @file Ed3kBridge.h
/// @brief The daemon's one implementation of each ed3k request (IPC 1000-1049, 1100-1149).
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
class Ed3kEd2kJoin;
class Ed3kShared;
} // namespace ed3k

namespace Ed3kBridge {

/// The node the requests go to; set by DaemonApp, null when absent.
void setSession(ed3k::Ed3kSession* session);
[[nodiscard]] ed3k::Ed3kSession* session();

/// Node list sources and their asks (OpenEd3kNodesLink, AnswerEd3kBootstrapAsk); set by
/// DaemonApp, null when absent.
void setBootstrap(ed3k::Ed3kBootstrap* bootstrap);

/// eD2K downloads joined from ed3k and files shared on ed3k, for File Details; set by
/// DaemonApp, null when absent.
void setEd2kJoin(ed3k::Ed3kEd2kJoin* join);
void setShared(ed3k::Ed3kShared* shared);

/// File Details of an eD2K download (16 B MD4): `ed3kEnabled` (join on); while the file is
/// tracked also `ed3kTracked`, `ed3kSearching`, `ed3kRoot` (hex), `ed3kPair` (0 unknown,
/// 1 trusted, 2 false), `ed3kBytes`, `ed3kSources`, `ed3kTransferring`. Nothing without ed3k.
void appendDownloadInfo(QCborMap& details, const QByteArray& md4);
/// The same for a shared file: `ed3kEnabled` (sharing on), `ed3kRoot` once hashed,
/// `ed3kAliasPublished`, `ed3kKeywordsPublished`.
void appendSharedInfo(QCborMap& details, const QByteArray& md4);

/// Whether the daemon was built with the ed3k module.
[[nodiscard]] bool available();

/// Whether an opcode is one of the ed3k blocks (1000-1049, 1100-1149).
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

/// Add what the node did since the last call to Statistics::ed3kSession (cumulative stats).
/// Called once a second by the daemon, and once more before the node stops.
void sampleCounters();

} // namespace Ed3kBridge
} // namespace eMule
