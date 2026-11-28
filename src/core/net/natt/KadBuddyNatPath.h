#pragma once
/// @file KadBuddyNatPath.h
/// @brief NAT rendezvous through a Kad serving buddy (compatibility target's dialect).
///
/// Requester -> target's buddy, OP_REASKCALLBACKUDP:
///   <buddy id 16><zero 16><OP_RENDEZVOUS><our hash 16><opts | 0x20><file hash 16>
///   <our ip4 wire order><our udp port 2><transport hint 1>           (73 bytes, zeros = unknown)
///   [<type 1><len 1><value len>]...                                  extension block (#265)
/// The block follows the full 73 bytes so stock parsers stop before it. An unknown type is
/// skipped by its length; a length past the end voids the whole block.
/// The buddy relays it as a stock OP_REASKCALLBACKTCP with the requester's endpoint as it
/// saw it; a buddy that speaks the dialect also sends the requester a 56-byte
/// OP_NATT_ENDPOINT_HINT and an OP_HOLEPUNCH. The target unwraps the zero file hash and
/// punches back. Any stock buddy relays the envelope unchanged.
/// The hint names the served client's outside UDP endpoint: the source of its latest
/// Kad packet to us (FINDBUDDY_REQ, then its refreshes), the hello's Kad port only when
/// none is known. A refresh that moves the port within kRehintMs of a hint sends another.

#include "net/natt/NatPath.h"

#include <array>
#include <vector>

namespace eMule {

class UpDownClient;

namespace natt {

class KadBuddyNatPath final : public NatPath {
public:
    static constexpr int    kInitialPunches   = 6;
    static constexpr uint64 kRepeatMs         = 5000;
    static constexpr uint32 kMaxRequests      = 3;
    static constexpr uint32 kEnvelopeSize     = 16 + 16 + 1 + 16 + 1 + 16;   // 66
    static constexpr uint32 kEnvelopeTailSize = 4 + 2 + 1;                   // 73 with it
    static constexpr uint32 kRendezvousMin    = 16 + 1 + 16;                 // after the opcode
    static constexpr uint32 kRendezvousFull   = kRendezvousMin + kEnvelopeTailSize; // block starts here
    static constexpr uint8  kExtUdp6          = 0x01;    // <ipv6 16><udp port 2>
    static constexpr uint8  kExtUdp6Size      = 16 + 2;
    static constexpr uint32 kHintSize         = 56;
    static constexpr uint8  kHintVersion      = 1;
    static constexpr uint64 kRehintMs         = 3000;   ///< refresh after a hint re-hints

    /// The served client a hint describes.
    struct ServedPeer {
        Hash16 hash{};
        Endpoint udp;           ///< its outside IPv4 UDP endpoint (seen, else Kad port)
        uint8 connectOptions = 0;

        [[nodiscard]] static ServedPeer fromClient(const UpDownClient& client);
    };

    using NatPath::NatPath;

    [[nodiscard]] PathKind kind() const override { return PathKind::KadBuddy; }
    [[nodiscard]] bool canServe(const PeerInfo& peer) const override;
    bool request(Attempt& attempt) override;
    void repeat(Attempt& attempt) override;
    bool handlePeerPacket(uint8 opcode, const uint8* data, uint32 size, const Endpoint& from) override;
    void tick() override;

    /// Buddy role: we relayed @p data (an OP_REASKCALLBACKUDP payload) for @p served.
    void onBuddyRelay(const Endpoint& requester, const uint8* data, uint32 size, const ServedPeer& served);
    /// Buddy role: @p served refreshed its UDP endpoint; hints sent lately go out again
    /// when it moved.
    void onServedRefresh(const ServedPeer& served);
    /// Target role: our buddy relayed an OP_RENDEZVOUS; @p data starts after the opcode.
    bool onRendezvous(const Endpoint& requester, const uint8* data, uint32 size);
    /// IPv6 UDP endpoint from an extension block, null when none or the block is bad.
    [[nodiscard]] static Endpoint parseExtUdp6(const uint8* data, uint32 size);

private:
    /// A hint we sent, kept kRehintMs for a refresh to correct.
    struct SentHint {
        Endpoint requester;
        Hash16 servedHash{};
        std::array<uint8, 16> buddyID{};
        Hash16 fileHash{};
        Endpoint udp;
        uint64 atMs = 0;
    };

    void sendEnvelope(Attempt& attempt);
    void onEndpointHint(const uint8* data, uint32 size, const Endpoint& from);
    void sendHint(const SentHint& h, uint8 connectOptions);
    void pruneHints();

    std::vector<SentHint> m_sentHints;
};

} // namespace natt
} // namespace eMule
