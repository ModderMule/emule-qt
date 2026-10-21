#pragma once
/// @file KadBuddyNatPath.h
/// @brief NAT rendezvous through a Kad serving buddy (compatibility target's dialect).
///
/// Requester -> target's buddy, OP_REASKCALLBACKUDP:
///   <buddy id 16><zero 16><OP_RENDEZVOUS><our hash 16><opts | 0x20><file hash 16>
///   [<our ip4 wire order><our udp port 2><transport hint 1>]          (66 or 73 bytes)
/// The buddy relays it as a stock OP_REASKCALLBACKTCP with the requester's endpoint as it
/// saw it; a buddy that speaks the dialect also sends the requester a 56-byte
/// OP_NATT_ENDPOINT_HINT and an OP_HOLEPUNCH. The target unwraps the zero file hash and
/// punches back. Any stock buddy relays the envelope unchanged.

#include "net/natt/NatPath.h"

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
    static constexpr uint32 kHintSize         = 56;
    static constexpr uint8  kHintVersion      = 1;

    /// The served client a hint describes.
    struct ServedPeer {
        Hash16 hash{};
        Endpoint udp;           ///< its public IPv4 and Kad/UDP port
        uint8 connectOptions = 0;

        [[nodiscard]] static ServedPeer fromClient(const UpDownClient& client);
    };

    using NatPath::NatPath;

    [[nodiscard]] PathKind kind() const override { return PathKind::KadBuddy; }
    [[nodiscard]] bool canServe(const PeerInfo& peer) const override;
    bool request(Attempt& attempt) override;
    void repeat(Attempt& attempt) override;
    bool handlePeerPacket(uint8 opcode, const uint8* data, uint32 size, const Endpoint& from) override;

    /// Buddy role: we relayed @p data (an OP_REASKCALLBACKUDP payload) for @p served.
    void onBuddyRelay(const Endpoint& requester, const uint8* data, uint32 size, const ServedPeer& served);
    /// Target role: our buddy relayed an OP_RENDEZVOUS; @p data starts after the opcode.
    bool onRendezvous(const Endpoint& requester, const uint8* data, uint32 size);

private:
    void sendEnvelope(Attempt& attempt);
    void onEndpointHint(const uint8* data, uint32 size, const Endpoint& from);
};

} // namespace natt
} // namespace eMule
