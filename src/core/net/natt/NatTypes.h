#pragma once
/// @file NatTypes.h
/// @brief Plain data shared by the NAT rendezvous hub and its paths.

#include "net/Address.h"
#include "utils/Types.h"

#include <QPointer>

#include <array>

namespace eMule {

class UpDownClient;

namespace natt {

using Hash16 = std::array<uint8, 16>;

/// Who started the rendezvous. The target is the connector (dials the TCP handoff).
enum class Role : uint8 { Requester, Target };

/// Server or peer that introduced the two sides.
enum class PathKind : uint8 {
    Ed2kServer,   ///< ed2k-server 0x60-0x62 (flag 0x20000)
    Lugdunum,     ///< Lugdunum eserver 0xA6/0xA7/0x37 (flag 0x1000)
    EnodeNat,     ///< eNode-go PR_NAT 0xF1 (flag 0x40000)
    KadBuddy,     ///< Kad serving buddy, OP_RENDEZVOUS (compatibility target)
    ForeignCallback ///< 0xA6 to the source's own server, we are not logged in there (UDP flag 0x100000)
};

[[nodiscard]] const char* pathName(PathKind kind) noexcept;

/// Crypt bits (CONNECT_OPT_CRYPT_*: 1 supported, 2 requested, 4 required) that can meet:
/// neither side requires what the other cannot do.
[[nodiscard]] constexpr bool cryptCompatible(uint8 ours, uint8 theirs) noexcept
{
    return !((ours & 0x04) && !(theirs & 0x01)) && !((theirs & 0x04) && !(ours & 0x01));
}

/// What a path needs to know about a peer we want to reach.
/// Server UDP obfuscation state; 0 = not known yet.
struct ServerUdpCrypt {
    uint32 key = 0;
    uint16 port = 0;   ///< obfuscated UDP port from OP_GLOBSERVSTATRES (Lugdunum and eNode-go: port + 14); port + 12 when the reply has none
};

/// The server a source is logged in to, for a foreign callback (we are not logged in there).
struct ForeignServer {
    enum class State : uint8 {
        No,        ///< unusable: no flag 0x100000, unlisted and not allowed, or no answer
        Pending,   ///< unlisted, status ping out
        Ready      ///< announces 0x100000; crypt may still lack the key
    };
    State state = State::No;
    Address ipv4;
    uint16 tcpPort = 0;
    ServerUdpCrypt crypt;
    bool listed = false;   ///< in our server list (else a probed unlisted one)
};

struct PeerInfo {
    Hash16 hash{};
    bool hashKnown = false;
    uint32 clientID = 0;            ///< server-assigned ID (LowID), 0 if none
    Address serverAddress;          ///< server the peer was found on
    uint16 serverPort = 0;
    Endpoint kadEndpoint;           ///< peer's own Kad/UDP endpoint, if known
    Endpoint buddyEndpoint;         ///< serving buddy (Kad)
    Hash16 buddyID{};
    bool buddyValid = false;
    Hash16 fileHash{};
    uint8 cryptOptions = 0;         ///< peer's CONNECT_OPT_CRYPT_* bits, valid when cryptKnown
    bool cryptKnown = false;        ///< from a 30-byte 0x61 or a 0x37/0xA7 crypt byte

    /// Built from a source we want to download from.
    [[nodiscard]] static PeerInfo fromClient(const UpDownClient& client);
};

/// One rendezvous in flight.
struct Attempt {
    enum class State : uint8 { Requested, Punching, Dialed, Failed };

    uint32 id = 0;                  ///< local handle
    Role role = Role::Requester;
    PathKind path = PathKind::Ed2kServer;
    State state = State::Requested;

    PeerInfo peer;                  ///< what we knew when starting (requester) or from the notice
    QPointer<UpDownClient> client;  ///< requester: the source; target: the client we dialed
    Endpoint peerUdp;               ///< peer's public UDP endpoint once known
    Endpoint peerUdp6;              ///< second family (PR_NAT IPv6), may be null
    uint16 peerTcpPort = 0;
    Address peerIPv6;               ///< from the peer's handoff
    bool peerHandoffSeen = false;   ///< got a 0xB2 0x11 from the peer
    bool peerSentNoAddress = false; ///< its handoff carried no IPv6

    uint32 connAck = 0;             ///< PR_NAT pairing nonce
    uint64 startedMs = 0;
    uint64 deadlineMs = 0;
    uint64 lastRequestMs = 0;       ///< last path request / repeat
    uint32 requestCount = 0;
    uint64 punchUntilMs = 0;
    uint64 lastPunchMs = 0;
    uint64 lastHandoffMs = 0;
    uint64 dialAtMs = 0;            ///< requester's fallback dial time, 0 = none
    bool dialed = false;
    uint64 quicDialAtMs = 0;        ///< when this side dials QUIC, 0 = not decided yet
    bool endpointReported = false;  ///< peerEndpointReady emitted
    bool finished = false;          ///< done or failed; removed on the next tick
    uint8 triedPaths = 0;           ///< requester: bit (1 << PathKind) per path tried
    int8 serverRole = -1;           ///< 30-byte 0x61: 0 = we dial, 1 = the peer dials; -1 = own rule
    bool awaitingInfo = false;      ///< target woken by 0x37, no 0x61 yet
    uint64 infoDeadlineMs = 0;      ///< then: Lugdunum's reciprocal 0xA6, or (requester ID 0) go on alone
    bool foreignWake = false;       ///< target woken by a 0x37 naming requester ID 0 (logged in elsewhere)
    uint8 burstLeft = 0;            ///< target: 0x9F still due of the 0 / 0.5 / 1 s burst
    uint64 burstStartMs = 0;
};

} // namespace natt
} // namespace eMule
