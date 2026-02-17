#pragma once

/// @file SearchParams.h
/// @brief Search query parameters — replaces MFC SearchParams.h.
///
/// Encapsulates all user-specified search criteria and filter options.
/// Supports serialization for persisting active search tabs.

#include "utils/Types.h"

#include <QString>

#include <cstdint>

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
