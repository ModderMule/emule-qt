#pragma once
/// @file UnlistedServers.h
/// @brief Servers we learned from a source (SX, saved sources) but never listed.
///
/// A foreign NAT callback may go to a source's server that is not in our server list
/// (pref natDialUnlistedServers). Such a server is first confirmed by one
/// obfuscated status ping; the answer carries its UDP flags and our UDP key. Entries are
/// never saved, shown or added to the server list.
///
/// A peer can name any address as its server, so pings are capped: one per address
/// per session whatever the answer, and kMaxPings per kPingWindowMs overall.

#include "net/Address.h"
#include "server/Server.h"
#include "utils/Types.h"

#include <deque>
#include <memory>
#include <vector>

namespace eMule {

class UnlistedServers {
public:
    static constexpr size_t kMaxEntries   = 64;
    static constexpr uint64 kIdleMs       = 60ull * 60 * 1000;   // dropped after 1 h unused
    static constexpr uint64 kAnswerWaitMs = 15000;
    static constexpr size_t kMaxPings     = 10;
    static constexpr uint64 kPingWindowMs = 10ull * 60 * 1000;

    enum class State : uint8 { Unknown, Pending, Answered, NoAnswer };

    /// State of @p addr:@p tcpPort; refreshes its idle timer.
    [[nodiscard]] State state(const Address& addr, uint16 tcpPort, uint64 nowMs);

    /// The entry, or nullptr.
    [[nodiscard]] Server* find(const Address& addr, uint16 tcpPort) const;

    /// Entry a status reply from @p addr:@p udpPort belongs to (TCP + 4, + 12, or its UDP
    /// obfuscation port), or nullptr.
    [[nodiscard]] Server* findByIPUdp(const Address& addr, uint16 udpPort) const;

    /// New entry for a first ping, or nullptr when the address had one this session or
    /// the cap is reached. The caller sends the ping and sets its challenge.
    Server* beginPing(const Address& addr, uint16 tcpPort, uint64 nowMs);

    /// A status reply for @p server was read into it.
    void answered(const Server& server);

    /// Pings sent in the current window.
    [[nodiscard]] size_t recentPings(uint64 nowMs);

private:
    struct Entry {
        std::unique_ptr<Server> server;
        uint64 pingedMs = 0;
        uint64 usedMs = 0;
        bool answered = false;
    };

    [[nodiscard]] Entry* entry(const Address& addr, uint16 tcpPort) const;
    void prune(uint64 nowMs);

    std::vector<std::unique_ptr<Entry>> m_entries;
    std::vector<std::pair<Address, uint16>> m_pinged;   ///< every address pinged this session
    std::deque<uint64> m_pingTimes;
};

} // namespace eMule
