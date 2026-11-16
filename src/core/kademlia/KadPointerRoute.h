#pragma once

/// @file KadPointerRoute.h
/// @brief Pointer route of Kad source records (`bpv`, docs/protocol/ipv6-spec.md §5.6.1).
///
/// Not MFC. A firewalled node with a buddy publishes a type 3 record at ~userHash naming
/// it, and `bpv` on its file records. A receiver resolves that pointer once per source,
/// caches the outcome, and reaches the source through the buddy it names: for a type 7
/// record with no usable `ip6`, and for a 3/5 record whose inline buddy did not answer.

#include "kademlia/Kademlia.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadUInt128.h"
#include "utils/Types.h"

#include <array>
#include <ctime>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace eMule::kad {

class PointerRoute {
public:
    static constexpr time_t kCacheSecs = 3600;          ///< hit per source, §5.6
    /// Miss per source: on type 7 the normal case (no buddy yet), so a short hold
    static constexpr time_t kMissCacheSecs = 600;
    static constexpr std::size_t kMaxLookups = 5;       ///< concurrent pointer lookups
    static constexpr std::size_t kMaxCandidates = Search::kMaxPointerCandidates; ///< distinct pointers per source
    static constexpr std::size_t kMaxWaitersPerSource = 32;
    static constexpr std::size_t kMaxCacheEntries = 4096;

    /// Buddy endpoint as a pointer names it: FT_SERVERIP (network order) + port.
    using BuddyEndpoint = std::pair<uint32, uint16>;
    /// Gets an untried candidate, or nullopt once the source has none left.
    using Waiter = std::function<void(std::optional<Search::PointerHit>)>;

    /// A type 7 record with `bpv` and no usable `ip6`. Delivers it as a type 3 source
    /// through Kademlia::kadSourceResultCallback() once a pointer resolves.
    static void resolve(const Kademlia::KadSourceResult& record);

    /// The buddy callback to @p failed did not reach the source: mark it tried and hand
    /// @p waiter the next candidate, now or when the lookup finds one. False when the
    /// source has no candidate left and no lookup runs or may start; @p waiter is then
    /// not called. A 3/5 record with `bpv`, or a source a pointer led to.
    static bool reroute(const UInt128& sourceID, uint32 sourceIP,
                        const std::array<uint8, 16>& fileHash, BuddyEndpoint failed,
                        Waiter waiter);

    /// Publisher: (re)publish our pointer record when the buddy changed or it is due.
    static void publishIfDue();

    /// Drop cache and publish state (Kad stop, tests).
    static void clear();

    /// Starts a pointer lookup; false if it could not start. Replaceable for tests.
    using LookupStarter = std::function<bool(const UInt128& key, const UInt128& sourceID,
                                             uint32 sourceIP, Search::PointerDone done)>;
    static void setLookupStarter(LookupStarter starter) { s_starter = std::move(starter); }
    [[nodiscard]] static std::size_t runningLookups() { return s_running; }
    /// Seconds the cached outcome for @p sourceID still holds; 0 if none (tests).
    [[nodiscard]] static time_t cachedFor(const UInt128& sourceID);

private:
    struct Waiting {
        std::array<uint8, 16> fileHash{};   ///< one waiter per file
        Waiter waiter;
    };
    struct Entry {
        bool lookupRunning = false;
        time_t expires = 0;
        uint32 sourceIP = 0;                ///< FT_SOURCEIP the pointers were checked against
        std::vector<Search::PointerHit> candidates;   ///< distinct, ≤ kMaxCandidates
        std::vector<BuddyEndpoint> failed;
        std::vector<Waiting> waiting;
    };

    /// Common path: pick an untried candidate, wait for the lookup, or start one.
    static bool request(const UInt128& sourceID, uint32 sourceIP,
                        const std::array<uint8, 16>& fileHash,
                        std::optional<BuddyEndpoint> failed, Waiter waiter);
    static const Search::PointerHit* untried(const Entry& e);
    static void onLookupResult(const UInt128& sourceID, std::optional<Search::PointerHit> hit);
    static void deliver(const Kademlia::KadSourceResult& rec, const Search::PointerHit& hit);
    static void prune(time_t now);
    static bool startLookup(const UInt128& key, const UInt128& sourceID, uint32 sourceIP,
                            Search::PointerDone done);

    static std::map<UInt128, Entry> s_cache;
    static std::size_t s_running;
    static LookupStarter s_starter;          ///< null = real Kad lookup
    // Publisher
    static uint32 s_pubBuddyIP;
    static uint16 s_pubBuddyPort;
    static time_t s_pubNext;
};

} // namespace eMule::kad
