#pragma once
/// @file DirectUdpNatPath.h
/// @brief No introducer: the source is firewalled on TCP but its UDP is open.
///
/// The source published Kad type 6 with TAG_NATT (QUIC). We are firewalled too, so the
/// stock direct callback (it dials our TCP) can't work. Instead we send the stock
/// OP_DIRECTCALLBACKREQ <tcp port 2><our hash 16><opts | 0x40> to its Kad endpoint; a
/// source that speaks QUIC NAT-T takes 0x40 as "arm QUIC for me" and does not dial back.
/// Our datagrams open our own NAT mapping, we dial QUIC to its endpoint 1 s later.

#include "net/natt/NatPath.h"

namespace eMule::natt {

class DirectUdpNatPath final : public NatPath {
public:
    static constexpr uint64 kRepeatMs    = 2000;
    static constexpr uint32 kMaxRequests = 3;

    using NatPath::NatPath;

    [[nodiscard]] PathKind kind() const override { return PathKind::DirectUdp; }
    [[nodiscard]] bool canServe(const PeerInfo& peer) const override;
    bool request(Attempt& attempt) override;
    void repeat(Attempt& attempt) override;

private:
    void sendRequest(Attempt& attempt);
};

} // namespace eMule::natt
