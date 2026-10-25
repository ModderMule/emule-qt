#pragma once

/// @file RepeatChunkGuard.h
/// @brief Counts how often each part went out to a peer in full, for the last uploads.
///
/// A part counts only once every byte of it left our socket for that peer, so a slot
/// cut short or a resent block does not add up to a delivery. Keyed by user hash + file
/// hash and bounded to kMaxEntries pairs, least recently used dropped. Not MFC.

#include "utils/Types.h"

#include <array>
#include <cstddef>
#include <list>
#include <unordered_map>
#include <vector>

namespace eMule {

class RepeatChunkGuard {
public:
    static constexpr std::size_t kMaxEntries = 500;        // peer + file pairs remembered
    static constexpr uint32 kMaxDeliveries = 2;            // full sends of one part allowed (1 + a repair)
    static constexpr std::size_t kMaxPartialParts = 16;    // parts in progress kept per pair
    static constexpr uint64 kBanMs = 24ull * 60 * 60 * 1000; // not the usual 2 h

    /// Payload [start, end) of the file was sent to the peer. Parts are PARTSIZE.
    void noteSent(const uint8* userHash, const uint8* fileHash, uint64 fileSize,
                  uint64 start, uint64 end);

    /// Times the part holding `offset` went out to the peer in full.
    [[nodiscard]] uint32 deliveries(const uint8* userHash, const uint8* fileHash,
                                    uint64 offset) const;

    [[nodiscard]] std::size_t count() const { return m_index.size(); }
    void clear();

private:
    using Key = std::array<uint8, 32>;   // user hash, then file hash
    struct Range {
        uint32 start = 0;
        uint32 end = 0;                  // exclusive, both relative to the part
    };
    struct Partial {
        uint32 part = 0;
        std::vector<Range> covered;      // sorted, merged
    };
    struct Entry {
        Key key{};
        std::vector<uint8> delivered;    // per part, grown on demand, saturates at 255
        std::vector<Partial> partial;    // oldest first
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept;
    };

    static Key makeKey(const uint8* userHash, const uint8* fileHash);
    Entry& touch(const Key& key);
    static void addPiece(Entry& e, uint32 part, uint32 partLen, uint32 start, uint32 end);

    std::list<Entry> m_lru;              // front = most recently used
    std::unordered_map<Key, std::list<Entry>::iterator, KeyHash> m_index;
};

} // namespace eMule
