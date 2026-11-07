#pragma once
/// @file UdpKeyIpTable.h
/// @brief Our IPv4 for the eD2K UDP obfuscation key toward one peer.
///
/// The client UDP key mixes in the sender's IP: we use the public IP we believe in, the
/// peer the datagram's source. Behind a carrier NAT that maps TCP and UDP to different
/// public IPv4s the two differ and the peer decodes nothing we send. The fix is per peer
/// (ed2k-server#40): the server's 0x61 tells us our UDP address as it saw it, a peer's
/// 0xAB echo tells us as that peer saw it. The echo wins. Never our public IP.
///
/// Thread-safe: the upload throttler thread encrypts.

#include "net/Address.h"
#include "utils/Types.h"

#include <map>
#include <mutex>

namespace eMule::natt {

class UdpKeyIpTable {
public:
    enum class Source : uint8 {
        Server,   ///< 0x61 own_ip
        Peer      ///< 0xAB echo
    };

    static constexpr uint64 kTtlMs      = 60ULL * 60 * 1000;  // since last use
    static constexpr uint64 kPeerHoldMs = 60ULL * 1000;       // server value never replaces a fresher echo
    static constexpr size_t kMaxEntries = 256;

    /// Key IP @p ourIP toward @p peer; null @p ourIP clears. A server value is ignored while
    /// an echo for the peer is younger than kPeerHoldMs.
    void set(const Address& peer, const Address& ourIP, Source src, uint64 nowMs);

    /// Key IP toward @p peer, null = our public IP. A hit counts as a use.
    [[nodiscard]] Address lookup(const Address& peer, uint64 nowMs);

    void clear();
    [[nodiscard]] size_t size() const;

private:
    struct Entry {
        Address ip;
        Source src = Source::Server;
        uint64 setMs = 0;
        uint64 usedMs = 0;
    };

    void evictOldest();

    mutable std::mutex m_lock;
    std::map<Address, Entry> m_entries;
};

} // namespace eMule::natt
