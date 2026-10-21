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
    KadBuddy      ///< Kad serving buddy, OP_RENDEZVOUS (compatibility target)
};

[[nodiscard]] const char* pathName(PathKind kind) noexcept;

/// What a path needs to know about a peer we want to reach.
/// Server UDP obfuscation state; 0 = not known yet.
struct ServerUdpCrypt {
    uint32 key = 0;
    uint16 port = 0;   ///< obfuscated UDP port (Lugdunum: port + 14)
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
    bool endpointReported = false;  ///< peerEndpointReady emitted
    bool finished = false;          ///< done or failed; removed on the next tick
};

} // namespace natt
} // namespace eMule
