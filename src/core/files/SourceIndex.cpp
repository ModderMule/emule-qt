#include "pch.h"
/// @file SourceIndex.cpp
/// @brief Identity-key index over one download's sources.

#include "files/SourceIndex.h"
#include "client/UpDownClient.h"

#include <algorithm>
#include <cstring>

namespace eMule {

namespace {

// splitmix64 finalizer; a collision only costs one extra compare().
constexpr uint64 mix(uint64 x) noexcept
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

constexpr uint64 makeKey(uint64 kind, uint64 a, uint64 b = 0, uint64 c = 0) noexcept
{
    return mix(mix(mix(mix(kind) ^ a) ^ b) ^ c);
}

} // namespace

void SourceIndex::add(UpDownClient* client)
{
    if (!client || m_keysOf.contains(client))
        return;
    const Keys keys = keysOf(*client);
    m_keysOf.emplace(client, keys);
    file(client, keys);
}

void SourceIndex::remove(const UpDownClient* client)
{
    const auto it = m_keysOf.find(client);
    if (it == m_keysOf.end())
        return;
    unfile(client, it->second);
    m_keysOf.erase(it);
}

void SourceIndex::rekey(UpDownClient* client)
{
    const auto it = m_keysOf.find(client);
    if (it == m_keysOf.end())
        return;
    const Keys now = keysOf(*client);
    if (now.count == it->second.count && now.key == it->second.key)
        return;
    unfile(client, it->second);
    it->second = now;
    file(client, now);
}

void SourceIndex::clear()
{
    m_byKey.clear();
    m_keysOf.clear();
}

void SourceIndex::candidates(const UpDownClient* probe, std::vector<UpDownClient*>& out) const
{
    out.clear();
    if (!probe || m_keysOf.empty())
        return;
    const Keys keys = keysOf(*probe);
    for (uint8 i = 0; i < keys.count; ++i) {
        const auto [first, last] = m_byKey.equal_range(keys.key[i]);
        for (auto it = first; it != last; ++it) {
            if (std::ranges::find(out, it->second) == out.end())
                out.push_back(it->second);
        }
    }
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

SourceIndex::Keys SourceIndex::keysOf(const UpDownClient& c)
{
    Keys keys;
    const auto put = [&keys](uint64 key) {
        if (std::ranges::find(keys.key.begin(), keys.key.begin() + keys.count, key)
            == keys.key.begin() + keys.count)
        {
            keys.key[keys.count++] = key;
        }
    };

    if (c.hasValidHash()) {
        uint64 lo = 0;
        uint64 hi = 0;
        std::memcpy(&lo, c.userHash(), 8);
        std::memcpy(&hi, c.userHash() + 8, 8);
        put(makeKey(1, lo, hi));
    }

    const uint16 tcpPort = c.userPort();
    const uint16 kadPort = c.kadPort();
    if (!c.userAddress().isNull()) {
        const uint64 addr = c.userAddress().hash();
        if (tcpPort != 0)
            put(makeKey(2, addr, tcpPort));
        if (kadPort != 0)
            put(makeKey(3, addr, kadPort));
    }
    // compare() also matches two IDs of 0 on an equal port, so 0 is filed like any other.
    if (tcpPort != 0)
        put(makeKey(4, c.userIDHybrid(), tcpPort));
    if (kadPort != 0)
        put(makeKey(5, c.userIDHybrid(), kadPort));
    if (c.userIDHybrid() != 0 && !c.serverAddress().isNull() && c.serverPort() != 0)
        put(makeKey(6, c.userIDHybrid(), c.serverAddress().hash(), c.serverPort()));
    if (!c.userIPv6().isNull() && tcpPort != 0)
        put(makeKey(7, c.userIPv6().hash(), tcpPort));
    return keys;
}

void SourceIndex::file(UpDownClient* client, const Keys& keys)
{
    for (uint8 i = 0; i < keys.count; ++i)
        m_byKey.emplace(keys.key[i], client);
}

void SourceIndex::unfile(const UpDownClient* client, const Keys& keys)
{
    for (uint8 i = 0; i < keys.count; ++i) {
        const auto [first, last] = m_byKey.equal_range(keys.key[i]);
        for (auto it = first; it != last; ++it) {
            if (it->second == client) {
                m_byKey.erase(it);
                break;
            }
        }
    }
}

} // namespace eMule
