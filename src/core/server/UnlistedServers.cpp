#include "pch.h"
/// @file UnlistedServers.cpp
/// @brief Probed servers outside the server list (foreign NAT callback).

#include "server/UnlistedServers.h"

#include <algorithm>

namespace eMule {

UnlistedServers::State UnlistedServers::state(const Address& addr, uint16 tcpPort, uint64 nowMs)
{
    prune(nowMs);
    Entry* e = entry(addr, tcpPort);
    if (!e) {
        // pinged before and pruned since: still once per session
        const bool pinged = std::ranges::find(m_pinged, std::pair{addr, tcpPort}) != m_pinged.end();
        return pinged ? State::NoAnswer : State::Unknown;
    }
    e->usedMs = nowMs;
    if (e->answered)
        return State::Answered;
    return nowMs - e->pingedMs < kAnswerWaitMs ? State::Pending : State::NoAnswer;
}

Server* UnlistedServers::find(const Address& addr, uint16 tcpPort) const
{
    Entry* e = entry(addr, tcpPort);
    return e ? e->server.get() : nullptr;
}

Server* UnlistedServers::findByIPUdp(const Address& addr, uint16 udpPort) const
{
    for (const auto& e : m_entries) {
        const Server& s = *e->server;
        if (s.ipAddress() != addr)
            continue;
        if (udpPort == static_cast<uint16>(s.port() + 4) || udpPort == static_cast<uint16>(s.port() + 12)
            || (s.obfuscationPortUDP() != 0 && udpPort == s.obfuscationPortUDP()))
            return e->server.get();
    }
    return nullptr;
}

Server* UnlistedServers::beginPing(const Address& addr, uint16 tcpPort, uint64 nowMs)
{
    prune(nowMs);
    if (addr.isNull() || tcpPort == 0
        || std::ranges::find(m_pinged, std::pair{addr, tcpPort}) != m_pinged.end())
        return nullptr;
    if (recentPings(nowMs) >= kMaxPings || m_entries.size() >= kMaxEntries)
        return nullptr;
    m_pinged.emplace_back(addr, tcpPort);
    m_pingTimes.push_back(nowMs);
    auto e = std::make_unique<Entry>();
    e->server = std::make_unique<Server>(addr, tcpPort);
    e->pingedMs = nowMs;
    e->usedMs = nowMs;
    Server* raw = e->server.get();
    m_entries.push_back(std::move(e));
    return raw;
}

void UnlistedServers::answered(const Server& server)
{
    for (const auto& e : m_entries) {
        if (e->server.get() == &server)
            e->answered = true;
    }
}

size_t UnlistedServers::recentPings(uint64 nowMs)
{
    while (!m_pingTimes.empty() && nowMs - m_pingTimes.front() >= kPingWindowMs)
        m_pingTimes.pop_front();
    return m_pingTimes.size();
}

UnlistedServers::Entry* UnlistedServers::entry(const Address& addr, uint16 tcpPort) const
{
    for (const auto& e : m_entries) {
        if (e->server->port() == tcpPort && e->server->ipAddress() == addr)
            return e.get();
    }
    return nullptr;
}

void UnlistedServers::prune(uint64 nowMs)
{
    std::erase_if(m_entries, [nowMs](const auto& e) { return nowMs - e->usedMs >= kIdleMs; });
}

} // namespace eMule
