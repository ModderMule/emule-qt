#pragma once

/// @file NetworkCounters.h
/// @brief Statistics counter blocks: Usenet, indexers, HTTP Cache.
///
/// Core owns the storage and the modules do the counting: Statistics holds the
/// session half, Preferences the banked cumulative half, and the usenet/indexer
/// modules bump the session half on the daemon thread. So reset, backup, restore
/// and the periodic flush need nothing module-specific. (HttpCacheManager is
/// core itself and keeps its session half; only the banked half comes here.)
///
/// Each block is all uint64 with a single field walk. Persistence, the
/// base + session sum and the IPC keys all go through that walk, so a field
/// cannot be counted but forgotten by one of them. The field names are the
/// YAML and CBOR keys: renaming one orphans stored totals.

#include "utils/Types.h"

#include <QCborMap>
#include <QLatin1StringView>

#include <algorithm>
#include <cstddef>

namespace eMule {

/// How a field folds the session into the banked base.
enum class CounterAgg : uint8 {
    Sum,  ///< base + session
    Max,  ///< high-water mark: a peak, not an amount
};

/// Usenet engine counters. Rates are bytes/s, times milliseconds.
struct UsenetCounters {
    // Traffic
    uint64 wireBytes = 0;          ///< NNTP bytes as read: incl. 430s, probes, handshakes
    uint64 decodedBytes = 0;       ///< yEnc payload written to disk
    uint64 downloadTimeMs = 0;     ///< time the engine was actually receiving
    uint64 maxDownRate = 0;        ///< Max
    uint64 peakConnections = 0;    ///< Max, open NNTP connections

    // Articles
    uint64 articlesDownloaded = 0;
    uint64 articlesNotFound = 0;   ///< one account answered 430; asked elsewhere
    uint64 articlesMissing = 0;    ///< given up on: no configured server supplied it
    uint64 articlesCorrupt = 0;    ///< yEnc CRC or size mismatch
    uint64 connectionErrors = 0;   ///< provider-side transport faults, not our own shutdown

    // Downloads
    uint64 itemsCompleted = 0;
    uint64 completedBytes = 0;
    uint64 itemsFailed = 0;

    // Post-processing
    uint64 par2Verified = 0;
    uint64 par2Repaired = 0;
    uint64 par2RepairFailed = 0;
    uint64 par2BlocksRepaired = 0;
    uint64 recoveryVolumes = 0;    ///< PAR2 recovery volumes requested after a short verify
    uint64 recoveryBytes = 0;
    uint64 unpackOk = 0;
    uint64 unpackFailed = 0;
    uint64 unpackPassword = 0;
    uint64 directUnpacks = 0;      ///< archive sets extracted while still downloading
    uint64 verifyMs = 0;
    uint64 repairMs = 0;
    uint64 unpackMs = 0;

    // Health checks
    uint64 healthChecks = 0;
    uint64 healthPassed = 0;
    uint64 healthPaused = 0;       ///< found short, added paused
    uint64 healthInconclusive = 0;
    uint64 statProbes = 0;

    // Intake
    uint64 nzbFromFile = 0;        ///< file dialog, drop, command line
    uint64 nzbFromUrl = 0;
    uint64 nzbFromWatch = 0;
    uint64 nzbFromFeed = 0;
    uint64 nzbFromIndexer = 0;
    uint64 nzbDuplicate = 0;
    uint64 nzbAlreadyDownloaded = 0;
    uint64 nzbInvalid = 0;

    template<class F> static constexpr void forEachField(F&& f)
    {
        using C = UsenetCounters;
        f("wireBytes", &C::wireBytes, CounterAgg::Sum);
        f("decodedBytes", &C::decodedBytes, CounterAgg::Sum);
        f("downloadTimeMs", &C::downloadTimeMs, CounterAgg::Sum);
        f("maxDownRate", &C::maxDownRate, CounterAgg::Max);
        f("peakConnections", &C::peakConnections, CounterAgg::Max);

        f("articlesDownloaded", &C::articlesDownloaded, CounterAgg::Sum);
        f("articlesNotFound", &C::articlesNotFound, CounterAgg::Sum);
        f("articlesMissing", &C::articlesMissing, CounterAgg::Sum);
        f("articlesCorrupt", &C::articlesCorrupt, CounterAgg::Sum);
        f("connectionErrors", &C::connectionErrors, CounterAgg::Sum);

        f("itemsCompleted", &C::itemsCompleted, CounterAgg::Sum);
        f("completedBytes", &C::completedBytes, CounterAgg::Sum);
        f("itemsFailed", &C::itemsFailed, CounterAgg::Sum);

        f("par2Verified", &C::par2Verified, CounterAgg::Sum);
        f("par2Repaired", &C::par2Repaired, CounterAgg::Sum);
        f("par2RepairFailed", &C::par2RepairFailed, CounterAgg::Sum);
        f("par2BlocksRepaired", &C::par2BlocksRepaired, CounterAgg::Sum);
        f("recoveryVolumes", &C::recoveryVolumes, CounterAgg::Sum);
        f("recoveryBytes", &C::recoveryBytes, CounterAgg::Sum);
        f("unpackOk", &C::unpackOk, CounterAgg::Sum);
        f("unpackFailed", &C::unpackFailed, CounterAgg::Sum);
        f("unpackPassword", &C::unpackPassword, CounterAgg::Sum);
        f("directUnpacks", &C::directUnpacks, CounterAgg::Sum);
        f("verifyMs", &C::verifyMs, CounterAgg::Sum);
        f("repairMs", &C::repairMs, CounterAgg::Sum);
        f("unpackMs", &C::unpackMs, CounterAgg::Sum);

        f("healthChecks", &C::healthChecks, CounterAgg::Sum);
        f("healthPassed", &C::healthPassed, CounterAgg::Sum);
        f("healthPaused", &C::healthPaused, CounterAgg::Sum);
        f("healthInconclusive", &C::healthInconclusive, CounterAgg::Sum);
        f("statProbes", &C::statProbes, CounterAgg::Sum);

        f("nzbFromFile", &C::nzbFromFile, CounterAgg::Sum);
        f("nzbFromUrl", &C::nzbFromUrl, CounterAgg::Sum);
        f("nzbFromWatch", &C::nzbFromWatch, CounterAgg::Sum);
        f("nzbFromFeed", &C::nzbFromFeed, CounterAgg::Sum);
        f("nzbFromIndexer", &C::nzbFromIndexer, CounterAgg::Sum);
        f("nzbDuplicate", &C::nzbDuplicate, CounterAgg::Sum);
        f("nzbAlreadyDownloaded", &C::nzbAlreadyDownloaded, CounterAgg::Sum);
        f("nzbInvalid", &C::nzbInvalid, CounterAgg::Sum);
    }

    bool operator==(const UsenetCounters&) const = default;
};

/// Load we put on newznab indexers — what their API limits cap. An NZB that a
/// feed added is counted once, as UsenetCounters::nzbFromFeed.
struct IndexerCounters {
    uint64 searches = 0;
    uint64 apiRequests = 0;
    uint64 apiErrors = 0;
    uint64 nzbFetches = 0;
    uint64 nzbFetchErrors = 0;
    uint64 feedPolls = 0;
    uint64 feedMatches = 0;

    template<class F> static constexpr void forEachField(F&& f)
    {
        using C = IndexerCounters;
        f("searches", &C::searches, CounterAgg::Sum);
        f("apiRequests", &C::apiRequests, CounterAgg::Sum);
        f("apiErrors", &C::apiErrors, CounterAgg::Sum);
        f("nzbFetches", &C::nzbFetches, CounterAgg::Sum);
        f("nzbFetchErrors", &C::nzbFetchErrors, CounterAgg::Sum);
        f("feedPolls", &C::feedPolls, CounterAgg::Sum);
        f("feedMatches", &C::feedMatches, CounterAgg::Sum);
    }

    bool operator==(const IndexerCounters&) const = default;
};

/// HTTP Cache, both directions. Publishing costs upstream once and serves it
/// many times; fetching takes a part off an HTTP server instead of a peer's
/// upload slot. The fetched bytes are eD2K file data, so they are also in the
/// Transfer branch's "Downloaded Data" — this block is the breakdown.
struct HttpCacheCounters {
    // Uploads
    uint64 bytesPublished = 0;     ///< ciphertext pushed to cache servers
    uint64 chunksPublished = 0;
    uint64 bytesSaved = 0;         ///< upstream never spent: extra peers off one upload

    // Downloads
    uint64 bytesFetched = 0;       ///< plaintext of fetches that verified
    uint64 chunksFetched = 0;
    uint64 fetchesFailed = 0;      ///< transport, size or digest failure
    uint64 partsCorrupt = 0;       ///< fetched, then the part failed its MD4 here
    uint64 resumes = 0;            ///< reconnects that picked a fetch up mid-part
    uint64 offersReceived = 0;     ///< chunk offers from peers
    uint64 offersDeclined = 0;     ///< …we said no to: disabled, busy, not wanted, bad
    uint64 kadChunks = 0;          ///< fetches started from a chunk record in Kad

    template<class F> static constexpr void forEachField(F&& f)
    {
        using C = HttpCacheCounters;
        f("bytesPublished", &C::bytesPublished, CounterAgg::Sum);
        f("chunksPublished", &C::chunksPublished, CounterAgg::Sum);
        f("bytesSaved", &C::bytesSaved, CounterAgg::Sum);

        f("bytesFetched", &C::bytesFetched, CounterAgg::Sum);
        f("chunksFetched", &C::chunksFetched, CounterAgg::Sum);
        f("fetchesFailed", &C::fetchesFailed, CounterAgg::Sum);
        f("partsCorrupt", &C::partsCorrupt, CounterAgg::Sum);
        f("resumes", &C::resumes, CounterAgg::Sum);
        f("offersReceived", &C::offersReceived, CounterAgg::Sum);
        f("offersDeclined", &C::offersDeclined, CounterAgg::Sum);
        f("kadChunks", &C::kadChunks, CounterAgg::Sum);
    }

    bool operator==(const HttpCacheCounters&) const = default;
};

/// One news server account, this session only. Its all-time bytes are the
/// billing meter's (usage.yml), which a statistics reset must never touch.
struct UsenetServerCounters {
    uint64 wireBytes = 0;
    uint64 decodedBytes = 0;
    uint64 articles = 0;
    uint64 notFound = 0;
    uint64 corrupt = 0;
    uint64 errors = 0;

    template<class F> static constexpr void forEachField(F&& f)
    {
        using C = UsenetServerCounters;
        f("wireBytes", &C::wireBytes, CounterAgg::Sum);
        f("decodedBytes", &C::decodedBytes, CounterAgg::Sum);
        f("articles", &C::articles, CounterAgg::Sum);
        f("notFound", &C::notFound, CounterAgg::Sum);
        f("corrupt", &C::corrupt, CounterAgg::Sum);
        f("errors", &C::errors, CounterAgg::Sum);
    }

    bool operator==(const UsenetServerCounters&) const = default;
};

template<class C>
concept CounterBlock = requires {
    C::forEachField([](const char*, uint64 C::*, CounterAgg) {});
};

template<CounterBlock C>
[[nodiscard]] consteval std::size_t counterFieldCount()
{
    std::size_t n = 0;
    C::forEachField([&n](const char*, uint64 C::*, CounterAgg) { ++n; });
    return n;
}

// A field declared but left out of the walk would be counted and then silently
// dropped by persistence, the sum and IPC alike.
static_assert(sizeof(UsenetCounters) == counterFieldCount<UsenetCounters>() * sizeof(uint64));
static_assert(sizeof(IndexerCounters) == counterFieldCount<IndexerCounters>() * sizeof(uint64));
static_assert(sizeof(HttpCacheCounters) == counterFieldCount<HttpCacheCounters>() * sizeof(uint64));
static_assert(sizeof(UsenetServerCounters)
              == counterFieldCount<UsenetServerCounters>() * sizeof(uint64));

/// base + session, field by field: Sum fields add, Max fields keep the larger.
template<CounterBlock C>
[[nodiscard]] constexpr C combineCounters(const C& base, const C& session)
{
    C out;
    C::forEachField([&](const char*, uint64 C::* m, CounterAgg agg) {
        out.*m = agg == CounterAgg::Max ? std::max(base.*m, session.*m)
                                        : base.*m + session.*m;
    });
    return out;
}

/// Raise a Max field to @p value.
constexpr void raiseCounter(uint64& field, uint64 value)
{
    field = std::max(field, value);
}

template<CounterBlock C>
[[nodiscard]] QCborMap countersToCbor(const C& c)
{
    QCborMap map;
    C::forEachField([&](const char* key, uint64 C::* m, CounterAgg) {
        map.insert(QLatin1StringView(key), static_cast<qint64>(c.*m));
    });
    return map;
}

/// A key the sender did not have reads as 0.
template<CounterBlock C>
[[nodiscard]] C countersFromCbor(const QCborMap& map)
{
    C c;
    C::forEachField([&](const char* key, uint64 C::* m, CounterAgg) {
        const qint64 v = map.value(QLatin1StringView(key)).toInteger();
        c.*m = v > 0 ? static_cast<uint64>(v) : 0;
    });
    return c;
}

} // namespace eMule
