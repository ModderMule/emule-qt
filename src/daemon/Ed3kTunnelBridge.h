#pragma once

/// @file Ed3kTunnelBridge.h
/// @brief Paired phones reach IPC over the ed3k tunnel (ALPN emuleqt-ipc/1,
/// libs/qDonkey/docs/ed3k-tunnel.md): no port forward, no VPN.
///
/// - Each accepted tunnel starts with a 48-byte preamble: "qdonkey-tunnel/1" and a pair proof
///   HMAC-SHA256(pair secret, "qdonkey-pair" || phone node key), zeros once paired.
/// - A phone not on ipc.tunnelPeers needs a valid proof: its key is added then, the secret
///   used up. Pair secrets come from GetIpcIdentity {pair: true}, 10 min, in memory only.
/// - The rest of the bytes go to IpcServer as one more remote client (token required, no
///   inner TLS: QUIC encrypts, the QR's node ID pins the core).
/// Free of the ed3k module like Ed3kBridge.h: without it the tunnel is off.

#include "IpcMessage.h"

#include <QCborMap>
#include <QStringList>

#include <functional>

namespace eMule {

namespace ed3k {
class Ed3kSession;
} // namespace ed3k

namespace Ed3kTunnelBridge {

/// The tunnel's ALPN
inline constexpr char kAlpn[] = "emuleqt-ipc/1";
/// Preamble magic, 16 bytes
inline constexpr char kMagic[] = "qdonkey-tunnel/1";
/// Magic + 32-byte proof
inline constexpr int kPreambleLen = 48;

/// Set by DaemonApp (null: stop). Listens while ipc.tunnel is on, the node runs and a phone
/// is paired or a pair secret is out.
void setSession(ed3k::Ed3kSession* session);

/// Fields for GetIpcIdentity: tunnel (bool), nodeId (hex), endpoints ["ip:port"], relays
/// ["<node hex>@ip:port"]; with @p pair also pairSecret (hex, 10 min, one use).
[[nodiscard]] QCborMap identity(bool pair);

/// The pair proof a phone sends: HMAC-SHA256(secret, "qdonkey-pair" || key).
[[nodiscard]] QByteArray pairProof(const QByteArray& secret, const QByteArray& phoneKey);

/// RegisterTunnelPeer / GetTunnelPeers / RemoveTunnelPeer. False: not one of them.
bool handleRequest(const Ipc::IpcMessage& msg, const std::function<void(const Ipc::IpcMessage&)>& reply);

/// Peers of ipc.tunnelPeers changed outside the bridge (tests): listen and allow again.
void reapply();

} // namespace Ed3kTunnelBridge
} // namespace eMule
