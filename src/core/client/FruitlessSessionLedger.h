#pragma once

/// @file FruitlessSessionLedger.h
/// @brief Per-peer record of download sessions that gave nothing, keyed by user hash.
///
/// Outlives the client object, so a peer cannot wipe its record by reconnecting.
/// Deliberately no address key: peers behind one NAT must not pay for each other.

#include "utils/Types.h"

#include <array>
#include <cstddef>
#include <unordered_map>

namespace eMule {

class FruitlessSessionLedger {
public:
    using Hash = std::array<uint8, 16>;

    enum class Verdict : uint8 {
        Noted,        // on record, nothing follows
        Paused,       // not asked, accepts declined, for kPauseMs
        Quarantined   // the same for kQuarantineMs
    };

    static constexpr uint64 kWindowMs = 5 * 60 * 1000;         // two fruitless sessions within → pause
    static constexpr uint64 kPauseMs = 3 * 60 * 1000;
    static constexpr uint32 kPausesToQuarantine = 2;
    static constexpr uint32 kEventsToQuarantine = 10;          // within kWindowMs
    static constexpr uint64 kQuarantineMs = 4 * 60 * 60 * 1000;
    static constexpr uint64 kEntryTtlMs = 4 * 60 * 60 * 1000;  // since last use
    static constexpr std::size_t kMaxEntries = 4096;

    /// The peer ended a session that carried less than a block.
    Verdict noteFruitlessSession(const uint8* userHash, uint64 now);
    /// An accept turned down while the peer is held back. Counts towards quarantine.
    Verdict noteDeclinedAccept(const uint8* userHash, uint64 now);
    /// A session that carried a block's worth: the counters start over. Does not
    /// lift a pause or a quarantine already earned.
    void noteProductiveSession(const uint8* userHash);

    /// Milliseconds the peer is still held back, 0 if it is free.
    [[nodiscard]] uint64 heldBackFor(const uint8* userHash, uint64 now) const;
    [[nodiscard]] bool isQuarantined(const uint8* userHash, uint64 now) const;
    [[nodiscard]] std::size_t count() const { return m_entries.size(); }

private:
    struct Entry {
        uint64 lastUsed = 0;
        uint64 lastFruitless = 0;   // 0: none on record
        uint64 windowStart = 0;
        uint32 windowEvents = 0;
        uint32 pauses = 0;
        uint64 heldUntil = 0;
        bool quarantined = false;
    };
    struct HashOf {
        std::size_t operator()(const Hash& h) const noexcept;
    };

    Entry& entry(const uint8* userHash, uint64 now);
    [[nodiscard]] const Entry* find(const uint8* userHash) const;
    /// Count one event in the long window; true once it calls for quarantine.
    static bool countEvent(Entry& e, uint64 now);
    static Verdict quarantine(Entry& e, uint64 now);
    void makeRoom(uint64 now);

    std::unordered_map<Hash, Entry, HashOf> m_entries;
};

} // namespace eMule
