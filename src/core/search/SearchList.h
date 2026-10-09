#pragma once

/// @file SearchList.h
/// @brief Search result manager — replaces MFC CSearchList.
///
/// Manages all search result sessions: deduplication, parent/child grouping,
/// spam detection, and persistence of search tabs and spam filters.

#include "files/KnownFileList.h"
#include "net/Address.h"
#include "search/SearchFile.h"
#include "search/SearchParams.h"
#include "search/SearchQueue.h"
#include "utils/Types.h"

#include <QDateTime>

#include <QObject>
#include <QString>
#include <QSet>
#include <QStringList>

#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class tst_SearchList;

namespace eMule {

class AICHHash;
class FileDataIO;
class UpDownClient;

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
    QString title;                  ///< tab title for client lists (peer's user name)
    bool clientSharedFiles = false; ///< MFC SSearchParams::bClientSharedFiles
    bool kad = false;               ///< our own Kad keyword search (MFC SearchTypeKademlia)
    /// When the search was asked for: a file on record before this is "seen before".
    qint64 startedAt = QDateTime::currentSecsSinceEpoch();
    /// The words searched for: in every name the search returns (see countNameGroups).
    QSet<QString> keywords;
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

    /// Start a new search. Returns the assigned search ID. @p takeEd2kRouting false
    /// makes an ED2K search that sends nothing leave the server answers with the
    /// search still collecting them.
    uint32 newSearch(const QString& resultFileType, const SearchParams& params,
                     uint32 forcedID = 0, bool takeEd2kRouting = true);

    /// An empty result list for a search that is sent later (see SearchQueue). Takes
    /// @p forcedID when given, else the next id of its own.
    uint32 reserveSearch(uint32 forcedID = 0);

    /// The search is going out now: results are filtered by its file type, and for an
    /// ED2K search the server answers (TCP and UDP) are filed under it from here on.
    /// @p expression is what was searched for; empty leaves the entry's keywords alone.
    void beginSearch(uint32 searchID, const QString& resultFileType, bool ed2k,
                     const QString& expression = {});

    /// The search text of a search that never goes through beginSearch() (catalogue searches).
    void setSearchExpression(uint32 searchID, const QString& expression);

    /// The ED2K search @p searchID is over. A server answer arriving after this is
    /// nobody's and is dropped — it used to land in whichever search came next.
    void releaseEd2kRouting(uint32 searchID);

    /// The search server answers are filed under; 0 when none is running.
    [[nodiscard]] uint32 ed2kSearchInFlight() const { return m_currentEd2kSearchID; }

    /// Holds and sends the searches of this list.
    [[nodiscard]] SearchQueue& queue() { return *m_queue; }

    /// True while @p searchID has a result list.
    [[nodiscard]] bool hasSearch(uint32 searchID) const { return findEntry(searchID) != nullptr; }

    /// Clear all searches.
    void clear();

    /// Remove results for a specific search ID.
    void removeResults(uint32 searchID);

    /// Remove a single result file.
    void removeResult(SearchFile* file);

    // --- Result processing ---

    /// Process TCP search results from a server.
    /// Returns true if "more results available" flag was set.
    /// @param server The answering server's address and TCP port (either family).
    bool processSearchAnswer(const uint8* packet, uint32 size,
                             bool optUTF8, const Endpoint& server);

    /// Process a peer's shared file list (MFC ProcessSearchAnswer with sender).
    /// Results go to the sender's own tab, created on first use; returns its ID.
    uint32 processClientSharedFiles(UpDownClient& sender, const uint8* packet, uint32 size,
                                    const QString& directory = {});

    /// Entry for a search ID, or nullptr.
    [[nodiscard]] const SearchListEntry* searchEntry(uint32 searchID) const { return findEntry(searchID); }

    /// Process a single UDP search result.
    /// @param server The answering server's address and TCP port (UDP port - 4).
    void processUDPSearchAnswer(const uint8* packet, uint32 size,
                                bool optUTF8, const Endpoint& server);

    /// Core add-to-list with deduplication and parent/child grouping.
    /// Takes ownership of the SearchFile pointer.
    void addToList(SearchFile* file, bool clientResponse = false,
                   uint32 fromUDPServerIP = 0);

    /// Add a row a server's Meta API returned for @p searchID. The server applied
    /// the type filter itself (a release row stays when one of its files matches),
    /// so the list's own is not run. Takes ownership.
    void addMetaSearchResult(uint32 searchID, SearchFile* file);

    // --- Kad result processing ---

    /// Add a Kademlia keyword search result. Builds a SearchFile internally.
    /// metaTags carries the imported media/format metadata (FT_MEDIA_*,
    /// TAG_FILEFORMAT, …) so the result shows bitrate/length/codec like an ED2K hit.
    void addKadKeywordResult(uint32 searchID, const uint8* fileHash,
                             const QString& name, uint64 size,
                             const QString& type, uint32 sources,
                             uint32 completeSources,
                             const std::vector<Tag>& metaTags = {},
                             uint32 fromIP = 0);

    // --- Queries ---

    /// Find a top-level search file by hash within the current search.
    [[nodiscard]] SearchFile* searchFileByHash(const uint8* hash, uint32 searchID) const;

    /// A row of a search tab the GUI restored from disk. Its search is gone, so the
    /// row is kept here by hash: detail sheet, Kad notes and spam marking need a file.
    [[nodiscard]] SearchFile* restoredFile(const uint8* hash) const;

    /// Keep @p file as a restored row (see restoredFile). Returns the file already
    /// held for its hash when there is one.
    SearchFile* adoptRestoredFile(std::unique_ptr<SearchFile> file);

    /// Number of results in a search session.
    [[nodiscard]] uint32 resultCount(uint32 searchID) const;

    /// Number of found files for a search session.
    [[nodiscard]] uint32 foundFiles(uint32 searchID) const;

    /// Number of found sources for a search session.
    [[nodiscard]] uint32 foundSources(uint32 searchID) const;

    /// Our own searches that still hold a result list, oldest first. Not the lists
    /// of a peer's shared files.
    [[nodiscard]] std::vector<uint32> searchIDs() const;

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

    // --- Kad notes (comments/ratings) ---

    /// Offer a Kad NOTES result to every search list holding @p fileHash.
    ///
    /// A note may arrive for a hash that is neither downloading nor shared — the
    /// user asked for comments on a plain search hit. Without this the note is
    /// dropped and the Comments tab stays empty forever. MFC does the same, and
    /// first: CSearchList::AddNotes (srchybrid/SearchList.cpp:665-677).
    /// @return true if at least one search result took the note.
    bool addNotes(const uint8* fileHash, const QByteArray& publisherId,
                  uint8 rating, const QString& comment);

    /// Flag every search result with @p fileHash as having a NOTES lookup in
    /// flight, so the detail dialog can show "(Kad search in progress...)".
    /// MFC: CSearchList::SetNotesSearchStatus (srchybrid/SearchList.cpp:679-690).
    void setNotesSearchStatus(const uint8* fileHash, bool running);

    // --- Spam detection ---

    /// Calculate spam rating for a search file.
    void addResultCount(uint32 searchID, const uint8* hash, uint32 count, bool spam);
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

    /// Register an IP that we've sent a UDP search request to. Answers from a server
    /// that is not in this set are dropped as unsolicited, so the set must not be
    /// grown by a search that has already been superseded.
    /// MFC: CSearchList::SentUDPRequestNotification — srchybrid/SearchList.cpp:1183-1187.
    void addSentUDPRequestIP(uint32 searchID, const Address& ip)
    {
        if (searchID == m_currentEd2kSearchID && !ip.isNull()) {
            m_curED2KSentRequestsIPs.insert(ip);
            m_requestedUdpAnswers[searchID] = static_cast<uint32>(m_curED2KSentRequestsIPs.size());
        }
    }

    // --- Persistence ---

    /// Save the spam filter database (SearchSpam.met, MFC layout). No-op until it was
    /// loaded, so a list that never read the file cannot overwrite it with nothing.
    void saveSpamFilter(const QString& configDir) const;

    /// Load the spam filter database, replacing what is in memory.
    void loadSpamFilter(const QString& configDir);

signals:
    /// A search was queued, sent, finished or failed.
    void searchStateChanged(const eMule::SearchStatus& status);
    void resultAdded(eMule::SearchFile* file);
    void resultUpdated(eMule::SearchFile* file);
    void resultAboutToBeRemoved(eMule::SearchFile* file);
    void tabHeaderUpdated(uint32 searchID);
    void spamStatusChanged(eMule::SearchFile* file);

private:
    // The spam filter lists have no accessors; the persistence test compares them.
    friend class ::tst_SearchList;

    /// @p forcedID if given (and marked as taken), else a fresh id.
    static uint32 takeSearchID(uint32 forcedID);

    /// Record a sighting in the seen-files index; for a result new to its search,
    /// first note on it what the index knew until now.
    static void noteSeen(SearchFile& file, const SearchListEntry& entry, bool newToSearch);

    /// Find the SearchListEntry for a given search ID.
    [[nodiscard]] SearchListEntry* findEntry(uint32 searchID);
    [[nodiscard]] const SearchListEntry* findEntry(uint32 searchID) const;

    /// Fake-file verdict of a top-level row, from its children, notes and tags.
    /// True when the verdict changed.
    bool assess(SearchFile* file);

    /// A new ED2K search took the server answers: forget who was asked and who answered.
    void resetUdpRequestTracking();

    /// Compute the name-without-keywords for spam detection.
    static QString computeNameWithoutKeywords(const QString& name, const QString& fileType);

    /// Drop a row's Kad-origin flag and its FT_META_NETWORK tag.
    static void clearKadOrigin(SearchFile* file);

    /// The AICH hash a Kad result's votes agree on, if there is exactly one and enough
    /// publishers reported it. @p votes is the raw TAG_KADAICHHASHRESULT blob.
    [[nodiscard]] static bool acceptedKadAICHHash(const QByteArray& votes, uint32 publishInfo,
                                                  AICHHash& out);

    // --- Data ---
    std::vector<SearchListEntry> m_fileLists;
    /// Rows of restored tabs, searchID 0, oldest first. In no list: never pushed.
    std::list<std::unique_ptr<SearchFile>> m_restoredFiles;

    // Per-search counters
    std::unordered_map<uint32, uint32> m_foundFilesCount;
    std::unordered_map<uint32, uint32> m_foundSourcesCount;

    // UDP server tracking
    std::unordered_set<Address> m_curED2KSentRequestsIPs;   // Address: v6 servers are 0 as uint32
    std::unordered_set<Address> m_curED2KAnsweredIPs;       // servers that answered this sweep
    // Per search: UDP servers asked / that answered (MFC m_RequestedUDPAnswersCount,
    // m_ReceivedUDPAnswersCount)
    std::unordered_map<uint32, uint32> m_requestedUdpAnswers;
    std::unordered_map<uint32, uint32> m_receivedUdpAnswers;
    std::unordered_map<uint32, UDPServerRecord> m_udpServerRecords;

    // Spam filter databases
    std::unordered_map<MD4Key, bool> m_knownSpamHashes;
    std::unordered_map<uint32, bool> m_knownSpamSourcesIPs;
    std::unordered_set<uint32> m_knownSpamServerIPs;   // servers whose UDP answers were marked
    bool m_spamFilterLoaded = false;
    QStringList m_knownSpamNames;
    QStringList m_knownSimilarSpamNames;
    std::vector<uint64> m_knownSpamSizes;

    // Current session state
    QString m_resultFileType;
    uint32 m_currentSearchID = 0;
    /// The newest *ED2K* search — server answers (TCP and UDP) are filed under this,
    /// not under m_currentSearchID, which a Kad search started meanwhile would own.
    uint32 m_currentEd2kSearchID = 0;
    SearchQueue* m_queue = nullptr;
};

} // namespace eMule
