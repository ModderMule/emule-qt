#pragma once

/// @file SearchParams.h
/// @brief Search query parameters — replaces MFC SearchParams.h.
///
/// Encapsulates all user-specified search criteria and filter options.
/// Supports serialization for persisting active search tabs.

#include "utils/Types.h"

#include <QString>

#include <cstdint>
#include <optional>

namespace eMule {

class FileDataIO;

// ---------------------------------------------------------------------------
// SearchType — the network/method used for a search
// ---------------------------------------------------------------------------

enum class SearchType : uint8 {
    Automatic   = 0,
    Ed2kServer  = 1,
    Ed2kGlobal  = 2,
    Kademlia    = 3,
    ContentDB   = 4,

    /// Keyword search against the configured newznab indexers, answered by
    /// eMule::Indexer rather than by either ED2K network. A network-selection
    /// enum already existed, so this extends it rather than bolting a second
    /// search system alongside it — and a future TorrentIndexer slots in here
    /// the same way.
    UsenetIndexer = 5,

    /// Usenet / torrent releases asked of an eD2K server's Meta API (eNode,
    /// MetaApi.Search) — the server answers from its catalogues, no indexer and
    /// no crawler is asked by us. Which server: see metaSearchCandidates().
    MetaUsenet  = 6,
    MetaTorrent = 7,
    /// Web network: link sets, link containers, and the eD2K files, torrents and
    /// magnets web pages offer (enodemeta amendment 27).
    MetaWeb     = 8,
    /// ed3k keyword search (packmule, Protocol.md §10.3), answered by the daemon's ed3k
    /// node (Ed3kSearch) through the Meta search runner (MetaSearchService forwards it).
    Ed3k        = 9,
    /// ed3k live channels (live.md §1.2): the ed3k keyword search filtered to channel files
    /// (`.ed3klive`), on the same path as Ed3k; a row is watched, not downloaded.
    Ed3kLive    = 10,
    /// Keyword search against the configured torznab indexers (Jackett, Prowlarr):
    /// UsenetIndexer's path, torrent rows only.
    TorrentIndexer = 11,
};

/// A search the configured newznab/torznab indexers answer (StartIndexerSearch).
[[nodiscard]] constexpr bool isIndexerSearchType(SearchType type)
{
    return type == SearchType::UsenetIndexer || type == SearchType::TorrentIndexer;
}

/// A search the daemon's ed3k node answers (Ed3kSearch through MetaSearchService).
[[nodiscard]] constexpr bool isEd3kSearchType(SearchType type)
{
    return type == SearchType::Ed3k || type == SearchType::Ed3kLive;
}

/// A search an eD2K server's Meta API answers.
[[nodiscard]] constexpr bool isMetaSearchType(SearchType type)
{
    return type == SearchType::MetaUsenet || type == SearchType::MetaTorrent || type == SearchType::MetaWeb;
}

/// SearchParams::blendMeta bits: Meta API networks also asked beside an ED2K / Kad search.
inline constexpr uint8 kBlendMetaUsenet  = 0x01;
inline constexpr uint8 kBlendMetaTorrent = 0x02;
inline constexpr uint8 kBlendMetaWeb     = 0x04;

/// A search whose rows may be joined by Meta API rows (blendMeta).
[[nodiscard]] constexpr bool canBlendMetaSearch(SearchType type)
{
    return type == SearchType::Automatic || type == SearchType::Ed2kServer
        || type == SearchType::Ed2kGlobal || type == SearchType::Kademlia;
}

// ---------------------------------------------------------------------------
// Automatic search-method resolution
// ---------------------------------------------------------------------------

/// The observable state that decides which network an Automatic search runs on.
/// Passed by value so the rule itself stays free of theApp/Kademlia coupling and
/// can be unit-tested without a network stack.
struct AutoSearchState {
    bool   serverConnected = false;
    bool   kadConnected    = false;   ///< Kad is running *and* connected
    bool   serverIsStatic  = false;   ///< the connected server is a static list member
    uint32 serverUsers     = 0;       ///< users reported by the connected server
    uint32 serverFiles     = 0;       ///< files reported by the connected server
    size_t serverCount     = 0;       ///< size of our whole server list
};

/// Resolve SearchType::Automatic down to the one network the search will use.
///
/// Returns std::nullopt when neither network is available — the caller reports
/// that to the user and starts nothing (MFC shows IDS_NOTCONNECTEDANY).
/// The result is never Automatic, Ed2kGlobal, ContentDB or a Meta type.
///
/// **It is never UsenetIndexer either, and that is deliberate.** This function
/// picks between networks that cost nothing to ask; an indexer search spends a
/// quota the user pays for and discloses the query to a third party. Both have
/// to be an explicit choice, so Automatic stays ED2K/Kad-only. Do not "fix"
/// this by adding indexers to the ladder.
///
/// MFC: CSearchResultsWnd::StartNewSearch — srchybrid/SearchResultsWnd.cpp:1134-1165.
[[nodiscard]] std::optional<SearchType> resolveAutomaticSearchType(const AutoSearchState& state);

// ---------------------------------------------------------------------------
// SearchParams — all parameters for a single search query
// ---------------------------------------------------------------------------

struct SearchParams {
    SearchParams() = default;

    /// Deserialize from a FileDataIO stream (partial — for persistence).
    explicit SearchParams(FileDataIO& file);

    /// Serialize to a FileDataIO stream (partial — for persistence).
    void storePartially(FileDataIO& file) const;

    // User-visible search fields
    QString searchTitle;        ///< Tab title / display name
    QString expression;         ///< Raw search expression string
    QString keyword;            ///< Parsed keyword
    QString booleanExpr;        ///< Boolean expression (AND/OR/NOT)

    // Filters
    QString fileType;           ///< ED2K file type filter ("Audio", "Video", etc.)
    QString extension;          ///< File extension filter
    QString minSizeStr;         ///< Min size as user-entered string
    QString maxSizeStr;         ///< Max size as user-entered string

    // Media-specific filters
    QString codec;
    QString title;
    QString album;
    QString artist;

    // Special / tab title
    QString specialTitle;       ///< Special title for persisted searches

    // Numeric filters
    uint64 minSize = 0;
    uint64 maxSize = 0;
    uint32 searchID = UINT32_MAX;
    uint32 availability = 0;
    uint32 completeSources = 0;
    uint32 minBitrate = 0;
    uint32 minLength = 0;

    // Search method
    SearchType type = SearchType::Ed2kServer;

    // Flags
    bool clientSharedFiles = false;
    bool matchKeywords = false;
    /// kBlendMeta* bits: Meta API searches run beside an ED2K / Kad search, rows in the
    /// same list (first page only). Not persisted.
    uint8 blendMeta = 0;
    /// Core's own search (download size lookup): never shown in the GUI. Not persisted.
    bool internal = false;
};

} // namespace eMule
