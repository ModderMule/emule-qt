#include "pch.h"
/// @file UdpKeyIpTable.cpp
/// @brief Per-peer key IP for eD2K UDP obfuscation (ed2k-server#40).

#include "net/natt/UdpKeyIpTable.h"

#include <algorithm>

namespace eMule::natt {

void UdpKeyIpTable::set(const Address& peer, const Address& ourIP, Source src, uint64 nowMs)
{
    if (peer.isNull())
        return;
    std::lock_guard lock(m_lock);
    auto it = m_entries.find(peer);
    if (it != m_entries.end() && src == Source::Server && it->second.src == Source::Peer
        && nowMs - it->second.setMs < kPeerHoldMs)
        return;
    if (ourIP.isNull()) {
        if (it != m_entries.end())
            m_entries.erase(it);
        return;
    }
    if (it == m_entries.end() && m_entries.size() >= kMaxEntries)
        evictOldest();
    m_entries[peer] = Entry{ourIP, src, nowMs, nowMs};
}

Address UdpKeyIpTable::lookup(const Address& peer, uint64 nowMs)
{
    std::lock_guard lock(m_lock);
    auto it = m_entries.find(peer);
    if (it == m_entries.end())
        return {};
    if (nowMs - it->second.usedMs >= kTtlMs) {
        m_entries.erase(it);
        return {};
    }
    it->second.usedMs = nowMs;
    return it->second.ip;
}

void UdpKeyIpTable::clear()
{
    std::lock_guard lock(m_lock);
    m_entries.clear();
}

size_t UdpKeyIpTable::size() const
{
    std::lock_guard lock(m_lock);
    return m_entries.size();
}

// caller holds m_lock
void UdpKeyIpTable::evictOldest()
{
    const auto oldest = std::ranges::min_element(m_entries, {}, [](const auto& kv) { return kv.second.usedMs; });
    if (oldest != m_entries.end())
        m_entries.erase(oldest);
}

} // namespace eMule::natt
