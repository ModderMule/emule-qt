#pragma once

/// @file SearchList.h
/// @brief Search result manager — replaces MFC CSearchList.
///
/// Manages all search result sessions: deduplication, parent/child grouping,
/// spam detection, and persistence of search tabs and spam filters.

#include "files/KnownFileList.h"
#include "search/SearchFile.h"
#include "search/SearchParams.h"
#include "utils/Types.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

namespace eMule {

class FileDataIO;

// ---------------------------------------------------------------------------
// UDPServerRecord — per-UDP-server spam tracking
// ---------------------------------------------------------------------------

struct UDPServerRecord {
    uint32 totalResults = 0;
    uint32 spamResults = 0;
};

// ---------------------------------------------------------------------------
// SearchListEntry — one search session (tab)
// ---------------------------------------------------------------------------

struct SearchListEntry {
    uint32 searchID = 0;
    std::list<std::unique_ptr<SearchFile>> files;

    SearchListEntry() = default;
    SearchListEntry(SearchListEntry&&) = default;
    SearchListEntry& operator=(SearchListEntry&&) = default;
    SearchListEntry(const SearchListEntry&) = delete;
    SearchListEntry& operator=(const SearchListEntry&) = delete;
};

// ---------------------------------------------------------------------------
// SearchList — QObject-based search result manager
// ---------------------------------------------------------------------------

class SearchList : public QObject {
    Q_OBJECT

public:
    explicit SearchList(QObject* parent = nullptr);
    ~SearchList() override;

    // --- Session management ---

    /// Start a new search. Returns the assigned search ID.
    uint32 newSearch(const QString& resultFileType, const SearchParams& params);

    /// Clear all searches.
    void clear();

    /// Remove results for a specific search ID.
    void removeResults(uint32 searchID);

    /// Remove a single result file.
    void removeResult(SearchFile* file);

    // --- Result processing ---

    /// Process TCP search results from a server.
    /// Returns true if "more results available" flag was set.
    bool processSearchAnswer(const uint8* packet, uint32 size,
                             bool optUTF8,
                             uint32 serverIP, uint16 serverPort);

    /// Process a single UDP search result.
    void processUDPSearchAnswer(const uint8* packet, uint32 size,
                                bool optUTF8,
                                uint32 serverIP, uint16 serverPort);

    /// Core add-to-list with deduplication and parent/child grouping.
    /// Takes ownership of the SearchFile pointer.
    void addToList(SearchFile* file, bool clientResponse = false,
                   uint32 fromUDPServerIP = 0);

    // --- Kad result processing ---

    /// Add a Kademlia keyword search result. Builds a SearchFile internally.
    void addKadKeywordResult(uint32 searchID, const uint8* fileHash,
                             const QString& name, uint64 size,
                             const QString& type, uint32 sources,
                             uint32 completeSources);

    // --- Queries ---

    /// Find a top-level search file by hash within the current search.
    [[nodiscard]] SearchFile* searchFileByHash(const uint8* hash, uint32 searchID) const;

    /// Number of results in a search session.
    [[nodiscard]] uint32 resultCount(uint32 searchID) const;

    /// Number of found files for a search session.
    [[nodiscard]] uint32 foundFiles(uint32 searchID) const;

    /// Number of found sources for a search session.
    [[nodiscard]] uint32 foundSources(uint32 searchID) const;

    /// Get the current search ID.
    [[nodiscard]] uint32 currentSearchID() const { return m_currentSearchID; }

    /// Get the current search file type filter.
    [[nodiscard]] const QString& currentResultFileType() const { return m_resultFileType; }

    /// Iterate over top-level search results for a given search ID.
    /// The callback receives a const pointer to each top-level SearchFile.
    /// Returns true if the search ID was found, false otherwise.
    template <typename Func>
    bool forEachResult(uint32 searchID, Func&& callback) const
    {
        const auto* entry = findEntry(searchID);
        if (!entry)
            return false;
        for (const auto& file : entry->files) {
            if (file->listParent() == nullptr)
                callback(file.get());
        }
        return true;
    }

    // --- Spam detection ---

    /// Calculate spam rating for a search file.
    void doSpamRating(SearchFile* file,
                      bool countAsHit = true,
                      bool updateParent = false,
                      bool fromUDP = false,
                      uint32 fromUDPServerIP = 0);

    /// Mark a file as spam (adds to filter lists).
    void markFileAsSpam(SearchFile* file, bool addToFilter = true);

    /// Mark a file as not spam (removes from filter lists).
    void markFileAsNotSpam(SearchFile* file, bool removeFromFilter = true);

    /// Recalculate spam ratings for all files in a search.
    void recalculateSpamRatings(uint32 searchID);

    // --- ED2K server tracking ---

    /// Register an IP that we've sent a UDP search request to.
    void addSentUDPRequestIP(uint32 ip) { m_curED2KSentRequestsIPs.insert({ip, true}); }

    // --- Persistence ---

    /// Save active searches to disk.
    void storeSearches(const QString& configDir) const;

    /// Load saved searches from disk.
    void loadSearches(const QString& configDir);

    /// Save the spam filter database.
    void saveSpamFilter(const QString& configDir) const;

    /// Load the spam filter database.
    void loadSpamFilter(const QString& configDir);

signals:
    void resultAdded(eMule::SearchFile* file);
    void resultUpdated(eMule::SearchFile* file);
    void resultAboutToBeRemoved(eMule::SearchFile* file);
    void tabHeaderUpdated(uint32 searchID);
    void spamStatusChanged(eMule::SearchFile* file);

private:
    /// Find the SearchListEntry for a given search ID.
    [[nodiscard]] SearchListEntry* findEntry(uint32 searchID);
    [[nodiscard]] const SearchListEntry* findEntry(uint32 searchID) const;

    /// Compute the name-without-keywords for spam detection.
    static QString computeNameWithoutKeywords(const QString& name, const QString& fileType);

    // --- Data ---
    std::vector<SearchListEntry> m_fileLists;

    // Per-search counters
    std::unordered_map<uint32, uint32> m_foundFilesCount;
    std::unordered_map<uint32, uint32> m_foundSourcesCount;

    // UDP server tracking
    std::unordered_map<uint32, bool> m_curED2KSentRequestsIPs;
    std::unordered_map<uint32, UDPServerRecord> m_udpServerRecords;

    // Spam filter databases
    std::unordered_map<MD4Key, bool> m_knownSpamHashes;
    std::unordered_map<uint32, bool> m_knownSpamSourcesIPs;
    QStringList m_knownSpamNames;
    QStringList m_knownSimilarSpamNames;
    std::vector<uint64> m_knownSpamSizes;

    // Current session state
    QString m_resultFileType;
    uint32 m_currentSearchID = 0;
    uint32 m_nextSearchID = 1;
};

} // namespace eMule
