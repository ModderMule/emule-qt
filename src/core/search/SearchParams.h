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
    ContentDB   = 4
};

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
/// The result is never Automatic, Ed2kGlobal or ContentDB.
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
};

} // namespace eMule
