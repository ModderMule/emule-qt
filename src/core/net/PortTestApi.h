#pragma once

/// @file PortTestApi.h
/// @brief The emule-qt.org port test API (`/wp-json/emqt/v1/porttest`) for the BitTorrent and
///        ed3k ports: request URL and reply parsing. No sockets here; PortTestRunner (daemon)
///        sends the request.
///
/// - BitTorrent (`proto=bt`): the server dials TCP and sends a DHT ping + uTP SYN over UDP.
///   Both must be reachable.
/// - eD2K (`proto=ed2k`): the server dials TCP and sends OP_PORTTEST, then `C5 FE 12` over
///   UDP; the core confirms the datagram over the same TCP link. With `hash` (our user hash)
///   the TCP link is obfuscated, so a core that requires encryption answers too (#156).
/// - ed3k (`proto=ed3k`): UDP only. The node never answers a PROBE (spec §4.4), so the daemon
///   seals one to itself, the server only sends it, and the node says whether it arrived.
///
/// Result targets (CBOR, also the IPC reply of TestPorts):
/// `{family: 4|6, ip, tcp?: {open, reason}, udp?: {open, reason}}`. Reasons are the server's
/// (open, closed, rejected, filtered, server_egress_blocked, server_no_ipv6, send_failed, ...).

#include <QByteArray>
#include <QCborArray>
#include <QString>
#include <QUrl>
#include <QtGlobal>

namespace eMule::PortTestApi {

/// Values of TestPorts field 0.
enum class Network : int {
    BitTorrent = 1,
    Ed3k = 2,
    Ed2k = 3,
};

struct Request {
    Network network = Network::BitTorrent;
    quint16 tcpPort = 0;   ///< BitTorrent and eD2K
    quint16 udpPort = 0;
    QString ip4;           ///< own public addresses, for the family the request does not use
    QString ip6;
    QByteArray probe;      ///< ed3k: the sealed PROBE datagram
    QByteArray userHash;   ///< eD2K: 16 bytes; the server then dials obfuscated
};

/// TestPorts error codes (IpcMessage::makeError); the GUI words them, the detail is the server's.
enum class Failure : int {
    BtNotRunning = 4001,
    Ed3kNotRunning = 4002,
    NoProbe = 4003,        ///< the node made no PROBE
    UnknownNetwork = 4004,
    NoAnswer = 4005,       ///< detail: the network error
    Refused = 4006,        ///< detail: the server's message
    OldServer = 4007,      ///< the server ignored `proto`
    Ed2kNotListening = 4008, ///< no eD2K TCP listen port
};

/// The reply of the server, reduced to what the GUI shows.
struct Reply {
    bool ok = false;
    QString error;         ///< set when !ok
    Failure failure = Failure::Refused;
    QString proto;         ///< data.proto; empty from a server older than the bt/ed3k tests
    bool obfuscated = false; ///< eD2K: the server dialed obfuscated (plugin >= 1.6.0)
    QCborArray targets;
};

/// Reason of an ed3k UDP entry the server only sent.
inline constexpr QLatin1StringView kReasonSent{"sent"};
inline constexpr QLatin1StringView kReasonOpen{"open"};
inline constexpr QLatin1StringView kReasonFiltered{"filtered"};

/// Family bits of PortTestRunner / ed3k_port_test_seen.
inline constexpr int kSeenIPv4 = 1;
inline constexpr int kSeenIPv6 = 2;

[[nodiscard]] QUrl url(const Request& request);

/// The `proto` value of @p network ("bt", "ed3k", "ed2k").
[[nodiscard]] QString protoName(Network network);

/// Parses the JSON body; @p httpStatus is used for the error text when the body has none.
/// Not ok when the server answered for another protocol than @p network: an older server
/// ignores `proto` and runs the eD2K test.
[[nodiscard]] Reply parseReply(const QByteArray& body, int httpStatus, Network network);

/// Family bits of the targets whose UDP probe the server sent (ed3k).
[[nodiscard]] int sentFamilies(const QCborArray& targets);

/// ed3k: turns each "sent" UDP entry into open/filtered by the families the node saw.
[[nodiscard]] QCborArray applySeen(const QCborArray& targets, int seenFamilies);

/// eD2K: the TCP verdict of the targets. Open when one target is open; closed only when the
/// server reached a target and got no answer it could trust, otherwise unknown.
/// @p family 4 or 6 = only targets of that family count; 0 = all. HighID checks pass 4:
/// an open IPv6 port says nothing about the IPv4 one the server dials back.
enum class Verdict : quint8 { Unknown, Open, Closed };
[[nodiscard]] Verdict tcpVerdict(const QCborArray& targets, bool obfuscated, bool cryptRequired,
                                 int family = 0);

} // namespace eMule::PortTestApi
