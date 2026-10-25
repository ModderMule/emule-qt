#include "pch.h"
/// @file RepeatChunkGuard.cpp
/// @brief Counts how often each part went out to a peer in full.

#include "client/RepeatChunkGuard.h"

#include "utils/Opcodes.h"

#include <algorithm>
#include <cstring>

namespace eMule {

void RepeatChunkGuard::noteSent(const uint8* userHash, const uint8* fileHash, uint64 fileSize,
                                uint64 start, uint64 end)
{
    if (!userHash || !fileHash || start >= end || end > fileSize)
        return;

    Entry& e = touch(makeKey(userHash, fileHash));
    // A block may cross a part boundary; each side counts towards its own part.
    while (start < end) {
        const uint64 part = start / PARTSIZE;
        const uint64 partStart = part * PARTSIZE;
        const uint64 partEnd = std::min(partStart + PARTSIZE, fileSize);
        const uint64 pieceEnd = std::min(end, partEnd);
        addPiece(e, static_cast<uint32>(part), static_cast<uint32>(partEnd - partStart),
                 static_cast<uint32>(start - partStart), static_cast<uint32>(pieceEnd - partStart));
        start = pieceEnd;
    }
}

uint32 RepeatChunkGuard::deliveries(const uint8* userHash, const uint8* fileHash,
                                    uint64 offset) const
{
    if (!userHash || !fileHash)
        return 0;
    const auto it = m_index.find(makeKey(userHash, fileHash));
    if (it == m_index.end())
        return 0;
    const auto& delivered = it->second->delivered;
    const uint64 part = offset / PARTSIZE;
    return part < delivered.size() ? delivered[part] : 0;
}

void RepeatChunkGuard::clear()
{
    m_index.clear();
    m_lru.clear();
}

std::size_t RepeatChunkGuard::KeyHash::operator()(const Key& k) const noexcept
{
    // Both halves are MD4 digests, already uniform.
    uint64 a = 0;
    uint64 b = 0;
    std::memcpy(&a, k.data(), sizeof a);
    std::memcpy(&b, k.data() + 16, sizeof b);
    return static_cast<std::size_t>(a ^ (b * 0x9E3779B97F4A7C15ull));
}

RepeatChunkGuard::Key RepeatChunkGuard::makeKey(const uint8* userHash, const uint8* fileHash)
{
    Key key;
    std::memcpy(key.data(), userHash, 16);
    std::memcpy(key.data() + 16, fileHash, 16);
    return key;
}

RepeatChunkGuard::Entry& RepeatChunkGuard::touch(const Key& key)
{
    if (const auto it = m_index.find(key); it != m_index.end()) {
        m_lru.splice(m_lru.begin(), m_lru, it->second);
        return m_lru.front();
    }
    if (m_index.size() >= kMaxEntries) {
        m_index.erase(m_lru.back().key);
        m_lru.pop_back();
    }
    m_lru.emplace_front();
    m_lru.front().key = key;
    m_index.emplace(key, m_lru.begin());
    return m_lru.front();
}

void RepeatChunkGuard::addPiece(Entry& e, uint32 part, uint32 partLen, uint32 start, uint32 end)
{
    auto pit = std::ranges::find(e.partial, part, &Partial::part);
    if (pit == e.partial.end()) {
        if (e.partial.size() >= kMaxPartialParts)
            e.partial.erase(e.partial.begin());
        e.partial.push_back({part, {}});
        pit = std::prev(e.partial.end());
    }

    // Insert and merge overlapping or touching ranges.
    auto& covered = pit->covered;
    Range merged{start, end};
    std::erase_if(covered, [&merged](const Range& r) {
        if (r.end < merged.start || r.start > merged.end)
            return false;
        merged.start = std::min(merged.start, r.start);
        merged.end = std::max(merged.end, r.end);
        return true;
    });
    covered.insert(std::ranges::upper_bound(covered, merged.start, {}, &Range::start), merged);

    if (covered.size() == 1 && covered.front().start == 0 && covered.front().end >= partLen) {
        if (e.delivered.size() <= part)
            e.delivered.resize(part + 1, 0);
        if (e.delivered[part] < 255)
            ++e.delivered[part];
        e.partial.erase(pit);
    }
}

} // namespace eMule
