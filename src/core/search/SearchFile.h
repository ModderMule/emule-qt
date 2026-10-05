#pragma once

/// @file SearchFile.h
/// @brief Search result file entry — replaces MFC CSearchFile.
///
/// Represents a single search result from ED2K or Kademlia, with source
/// tracking, spam scoring, and parent/child hierarchy for duplicate grouping.

#include "enodemeta/MetaHash.h"
#include "files/AbstractFile.h"
#include "search/SearchParams.h"
#include "utils/Types.h"

#include <QString>

#include <list>
#include <vector>

namespace eMule {

class FileDataIO;

// ---------------------------------------------------------------------------
// Free function: validate search result client IP/port
// ---------------------------------------------------------------------------

/// Returns true if the IP:port pair is valid for a search result source.
[[nodiscard]] bool isValidSearchResultClientIPPort(uint32 ip, uint16 port);

// ---------------------------------------------------------------------------
// SearchFile — a single search result
// ---------------------------------------------------------------------------

class SearchFile : public AbstractFile {
public:
    // --- Nested types ---

    /// Client that provided this search result.
    struct SClient {
        uint32 ip = 0;
        uint16 port = 0;
        uint32 serverIP = 0;
        uint16 serverPort = 0;

        bool operator==(const SClient&) const = default;
    };

    /// Server that returned this search result.
    struct SServer {
        uint32 ip = 0;
        uint16 port = 0;
        uint32 avail = 0;
        bool udpAnswer = false;

        friend bool operator==(const SServer& a, const SServer& b)
        {
            return a.ip == b.ip && a.port == b.port;
        }
    };

    /// eNode meta row (torrent/Usenet release inside an eD2K answer).
    struct MetaInfo {
        enodemeta::Kind kind = enodemeta::Kind::Unspecified;  ///< from the hash, not the name
        uint8  hashFlags = 0;       ///< enodemeta::HashFlag bits
        uint32 fileIndex = enodemeta::kFileIndexWholeSet32;
        uint64 totalSize = 0;
        uint32 seeders = 0;
        uint32 peers = 0;
        uint32 ageDays = 0;
        uint32 flags = 0;           ///< META_FLAG_* (FT_META_FLAGS)
        QString catalogId;          ///< FT_META_ID, echoed on GetMetaFile
        QString filePath;
        QString indexer;
        QString magnet;

        [[nodiscard]] bool isTorrent() const
        {
            return kind == enodemeta::Kind::BtV1 || kind == enodemeta::Kind::BtV2;
        }
        [[nodiscard]] bool isNzb() const { return kind == enodemeta::Kind::Nzb; }
    };

    /// Whether this file is known locally.
    enum class KnownType : uint8 {
        NotDetermined,
        Shared,
        Downloading,
        Downloaded,
        Cancelled,
        Unknown
    };

    // --- Construction ---

    SearchFile() = default;

    /// Deserialize from a network packet (ED2K search result).
    SearchFile(FileDataIO& data, bool optUTF8,
               uint32 serverIP = 0, uint16 serverPort = 0,
               const QString& directory = {},
               bool kadResult = false);

    /// Copy constructor from an existing SearchFile (for child creation).
    explicit SearchFile(const SearchFile* other);

    ~SearchFile() override = default;

    // --- AbstractFile override ---
    void updateFileRatingCommentAvail(bool forceUpdate = false) override;

    // --- Source counts ---

    /// ED2K: additive. Kad: max.
    void addSources(uint32 count);
    void addCompleteSources(uint32 count);

    [[nodiscard]] uint32 sourceCount() const { return m_sourceCount; }
    [[nodiscard]] uint32 completeSourceCount() const { return m_completeSourceCount; }

    /// Is file complete? ED2K: checks complete sources. Kad: returns -1 (unknown).
    [[nodiscard]] int isComplete() const;

    // --- Kad publish info ---

    [[nodiscard]] uint32 kadPublishInfo() const { return m_kadPublishInfo; }
    void setKadPublishInfo(uint32 val) { m_kadPublishInfo = val; }

    // --- Client/server lists ---

    [[nodiscard]] const std::list<SClient>& clients() const { return m_clients; }
    [[nodiscard]] const std::list<SServer>& servers() const { return m_servers; }

    void addClient(const SClient& client);
    void addServer(const SServer& server);

    // --- Directory ---

    [[nodiscard]] const QString& directory() const { return m_directory; }
    void setDirectory(const QString& dir) { m_directory = dir; }

    // --- Spam ---

    [[nodiscard]] uint32 spamRating() const { return m_spamRating; }
    void setSpamRating(uint32 val) { m_spamRating = val; }
    [[nodiscard]] bool isConsideredSpam() const { return m_spamRating >= SEARCH_SPAM_THRESHOLD; }

    // --- Known type ---

    [[nodiscard]] KnownType knownType() const { return m_knownType; }
    void setKnownType(KnownType t) { m_knownType = t; }

    // --- Search identity ---

    [[nodiscard]] uint32 searchID() const { return m_searchID; }
    void setSearchID(uint32 id) { m_searchID = id; }
    [[nodiscard]] bool isKadResult() const { return m_kadResult; }
    void setKadResult(bool val) { m_kadResult = val; }

    // --- eNode meta rows ---

    /// Hash parsed as an eNode meta hash (and its tags agreed).
    [[nodiscard]] bool isMetaResult() const { return m_meta.kind != enodemeta::Kind::Unspecified; }
    [[nodiscard]] const MetaInfo& meta() const { return m_meta; }
    /// Meta row whose tags contradict its hash, or of an unknown scheme version —
    /// the search list drops these (enodemeta plan §8.1).
    [[nodiscard]] bool isInvalidMetaResult() const { return m_metaInvalid; }

    /// A server result the server itself found on Kad (FT_META_NETWORK): an ordinary
    /// eD2K file, downloadable like any other. Not a result of our own Kad search —
    /// that is isKadResult().
    [[nodiscard]] bool isKadOrigin() const { return m_kadOrigin; }
    void setKadOrigin(bool val) { m_kadOrigin = val; }

    // --- GUI parent/child hierarchy ---

    [[nodiscard]] SearchFile* listParent() const { return m_listParent; }
    void setListParent(SearchFile* parent) { m_listParent = parent; }

    [[nodiscard]] uint32 listChildCount() const { return static_cast<uint32>(m_listChildren.size()); }
    [[nodiscard]] const std::list<SearchFile*>& listChildren() const { return m_listChildren; }
    void addListChild(SearchFile* child) { m_listChildren.push_back(child); }

    [[nodiscard]] bool isListExpanded() const { return m_listExpanded; }
    void setListExpanded(bool val) { m_listExpanded = val; }

    // --- Name without keywords (for spam detection) ---

    [[nodiscard]] const QString& nameWithoutKeywords() const { return m_nameWithoutKeywords; }
    void setNameWithoutKeywords(const QString& name) { m_nameWithoutKeywords = name; }

    // --- Persistence ---

    void storeToFile(FileDataIO& file) const;

private:
    /// Convert old-style string-named ED2K tags to numeric-named equivalents.
    static void convertED2KTag(Tag& tag);

    /// Classify by hash, cross-check the FT_META_* tags, strip the name prefix.
    void resolveMeta(bool hadKindTag, uint8 tagKind, bool hadVersionTag, uint8 tagVersion,
                     bool hadIndexTag);

    /// Drop the "[kad …] " name prefix of a Kad-origin row; the icon says it instead.
    void stripKadPrefix();

    // Data members
    std::list<SClient>   m_clients;
    std::list<SServer>   m_servers;
    std::list<SearchFile*> m_listChildren;
    QString m_directory;
    QString m_nameWithoutKeywords;
    SearchFile* m_listParent = nullptr;
    uint32 m_sourceCount = 0;
    uint32 m_completeSourceCount = 0;
    uint32 m_kadPublishInfo = 0;
    uint32 m_searchID = 0;
    uint32 m_spamRating = 0;
    uint32 m_clientID = 0;
    uint16 m_clientPort = 0;
    KnownType m_knownType = KnownType::NotDetermined;
    bool m_kadResult = false;
    bool m_listExpanded = false;
    MetaInfo m_meta;
    bool m_metaInvalid = false;
    bool m_kadOrigin = false;
};

} // namespace eMule
