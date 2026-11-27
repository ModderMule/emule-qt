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
/// `{family: 4|6, ip, tcp?: {open, reason}, udp?: {open, reason, copies?, packets?}}`. Reasons
/// are the server's (open, closed, rejected, filtered, server_egress_blocked, server_no_ipv6,
/// send_failed, ...). ed3k: `copies` = datagrams the server sent (plugin >= 1.7.0), `packets` =
/// all the node waited for, over every attempt. Peer evidence: `peerAddress`, `peerSame`,
/// `peerTcp` / `peerUdp` (the proof's source: kProof*). eD2K, result level: `filterHits`
/// [{address, description, tcp}] = peers the IP filter rejected during the test (#266).

#include <QByteArray>
#include <QCborArray>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtGlobal>

namespace eMule::BindAddress {
struct Resolution;
}

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
    int families = 0;      ///< kSeenIPv4/kSeenIPv6 bits to test (bound interface); 0 = both
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
    NoBoundAddress = 4009, ///< bound interface: no address of its families known to test (#246)
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
/// eD2K TCP (plugin >= 1.9.0): the client closed the link at once, without a byte (IP filter, ban)
inline constexpr QLatin1StringView kReasonDropped{"dropped"};

/// Family bits of PortTestRunner / ed3k_port_test_seen.
inline constexpr int kSeenIPv4 = 1;
inline constexpr int kSeenIPv6 = 2;
inline constexpr int kBothFamilies = kSeenIPv4 | kSeenIPv6;

/// Error reason (data.reason) of a server that had no target of the requested families.
inline constexpr QLatin1StringView kReasonNoTarget{"no_target"};

[[nodiscard]] QUrl url(const Request& request);

/// `porttest/source` (plugin >= 1.9.0): the addresses the server dials from, so the core lets
/// them past its IP filter for the test (#266).
[[nodiscard]] QUrl sourceUrl();

/// The addresses of a `porttest/source` reply (`data.ip4[]`, `data.ip6[]`); empty from an older
/// server (404) or a bad body.
[[nodiscard]] QStringList parseSources(const QByteArray& body);

/// The `proto` value of @p network ("bt", "ed3k", "ed2k").
[[nodiscard]] QString protoName(Network network);

/// Parses the JSON body; @p httpStatus is used for the error text when the body has none.
/// Not ok when the server answered for another protocol than @p network: an older server
/// ignores `proto` and runs the eD2K test.
[[nodiscard]] Reply parseReply(const QByteArray& body, int httpStatus, Network network);

/// Families (kSeenIPv4/kSeenIPv6 bits) the selected network interface @p r can be tested on (#246):
/// both when nothing is bound; a literal's family; a whole interface: IPv4 when it has one,
/// IPv6 when it has a global one, each only if pinnable. 0: none.
[[nodiscard]] int boundFamilies(const BindAddress::Resolution& r);

/// @p targets of the @p families bits only (a server older than plugin 1.8.0 ignores `families`).
[[nodiscard]] QCborArray onlyFamilies(const QCborArray& targets, int families);

/// Family bits of the targets whose UDP probe the server sent (ed3k).
[[nodiscard]] int sentFamilies(const QCborArray& targets);

/// ed3k: datagrams the server sent to the targets of one family (kSeenIPv4 / kSeenIPv6);
/// `copies`, 1 from a server older than plugin 1.7.0.
[[nodiscard]] int sentCopies(const QCborArray& targets, int familyBit);

/// ed3k: turns each "sent" UDP entry into open/filtered by the families the node saw, with
/// `packets` = @p packets4 / @p packets6 (0: the entry's own copies).
[[nodiscard]] QCborArray applySeen(const QCborArray& targets, int seenFamilies, int packets4 = 0, int packets6 = 0);

/// Sources of peer evidence that a port is open.
inline constexpr QLatin1StringView kProofEd3k{"ed3k"};     ///< ed3k NAT check: other nodes reach it (UDP)
inline constexpr QLatin1StringView kProofKad{"kad"};       ///< Kad UDP firewall check passed (eD2K clients)
inline constexpr QLatin1StringView kProofServer{"server"}; ///< eD2K server gave a High ID (TCP)

/// What peers see of this node in one family: "ip", "ip:port" or "[ip]:port"; empty = unknown.
struct PeerView {
    QString address;
    QString tcpProof;      ///< kProofServer, or empty: nothing proves TCP open
    QString udpProof;      ///< kProofEd3k / kProofKad, or empty
};

/// Host part of "ip", "ip:port" or "[ip]:port".
[[nodiscard]] QString hostOf(const QString& address);

/// Adds peerAddress / peerSame (peers see the tested address) / peerTcp / peerUdp to each
/// target whose family has a known @p v4 / @p v6 view. A VPN or proxy can send the website request
/// out from another address than the one peers reach (#216).
[[nodiscard]] QCborArray applyPeerView(const QCborArray& targets, const PeerView& v4, const PeerView& v6);

/// eD2K: the TCP verdict of the targets. Open when one target is open; closed only when the
/// server reached a target and got no answer it could trust, otherwise unknown.
/// @p family 4 or 6 = only targets of that family count; 0 = all. HighID checks pass 4:
/// an open IPv6 port says nothing about the IPv4 one the server dials back.
enum class Verdict : quint8 { Unknown, Open, Closed };
[[nodiscard]] Verdict tcpVerdict(const QCborArray& targets, bool obfuscated, bool cryptRequired,
                                 int family = 0);

} // namespace eMule::PortTestApi
