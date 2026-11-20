#pragma once

/// @file Ed3kArchiveContents.h
/// @brief What an archive holds, before downloading it (hoster.md §6.1): the contents record
/// (ALIAS `b3` SET under the first volume's root) and its canonical contents manifest.
///
/// Publisher: an unpacker hands a set it read (archive/ArchiveContentsHook.h); every entry is
/// hashed while streamed out of the archive, the manifest shared and kept, the record
/// published and renewed while kept. Searcher: from hoster links (`url` SAME), NZB files
/// (`nzb-file` SAME), a torrent (`btih`/`btmh` SET, its manifest's members) or known roots to
/// each archive's first volume, then `b3` SET → contents manifest → entries.
/// §6.1 "Unpacking is the check": a listing found here is kept by its first volume; when that
/// archive is unpacked here (publishListing), its entries are compared with the manifest
/// (contentsCheck): a mismatch bans the record's publishers (aliasBan, the search is over by
/// then) and drops the listing.

#include "Ed3kJoinBase.h"
#include "archive/ArchiveContentsHook.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

class QTimer;

namespace eMule::usenet { struct NzbInfo; }

namespace eMule::ed3k {

/// One entry of an archive: path with '/' between components.
struct ArchiveEntry {
    QString path;
    std::uint64_t size = 0;
    ffi::Root root;
};

/// What reading an archive gave: its first volume and every regular-file entry.
struct ArchiveListing {
    ffi::Root first;
    QString firstName;
    QString password;   ///< the one that opened it; empty: none needed
    std::vector<ArchiveEntry> entries;
};

/// One volume hashed by a contents tap (a torrent's verify).
struct VolumeHash {
    ffi::Root root;
    std::uint64_t size = 0;
    ffi::Bytes hashes;
};

/// The contents tap (archive/ArchiveContentsHook.h): hashes the first volume and every entry
/// as the unpack or verify read passes them, on that thread. commit() hands the listing on.
class Ed3kContentsTap : public ArchiveContentsTap {
public:
    using Publish = std::function<void(ArchiveListing)>;
    /// @p volumes hashed (with block hashes) from the first; 1: the record's first volume only.
    explicit Ed3kContentsTap(Publish publish = {}, int volumes = 1);

    [[nodiscard]] int volumesWanted() const override { return m_volumeCount; }
    void volumeData(int index, const char* data, qint64 len) override;
    void volumeEnd(int index) override;
    void entryBegin(const QString& path) override;
    void entryData(const char* data, qint64 len) override;
    void entryEnd() override;
    void spoil() override;
    void commit(const QString& firstVolumeName, const QString& password) override;

    /// What the read gave; nullopt: spoiled, incomplete, or a path the manifest refuses.
    [[nodiscard]] std::optional<ArchiveListing> listing(const QString& firstVolumeName, const QString& password) const;
    /// Volume @p index once read to its end.
    [[nodiscard]] std::optional<VolumeHash> volume(int index) const;
    /// Forgets the entries (and a spoil) for another read of the same set; volumes stay.
    void restartEntries();

private:
    Publish m_publish;
    int m_volumeCount = 1;
    std::vector<std::unique_ptr<ffi::Hasher>> m_volumeHashers;
    std::vector<std::optional<VolumeHash>> m_volumes;
    std::unique_ptr<ffi::Hasher> m_entry;
    QString m_entryPath;
    std::vector<ArchiveEntry> m_entries;
    bool m_spoiled = false;
};

class Ed3kArchiveContents : public Ed3kJoinBase {
    Q_OBJECT

public:
    Ed3kArchiveContents(Ed3kSession& session, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kArchiveContents() override;

    void start() override;
    void stop() override;

    /// Publishing on/off (pref ed3k.archiveContents.publish); off withdraws what was published.
    void setPublish(bool on);
    [[nodiscard]] bool publishes() const { return m_publish; }

    /// Reads the set @p volumes (in order) once on the calling thread, feeding @p tap: libarchive,
    /// or for RAR/7z encrypted beyond it 7-Zip/unrar (@p externalTool, empty: found) with each
    /// of @p passwords. The volumes reach the tap complete either way. Returns the password
    /// that opened it (empty: none needed) when every entry was read; nullopt otherwise.
    [[nodiscard]] static std::optional<QString> readSet(const QStringList& volumes, const QStringList& passwords,
                                                        const QString& externalTool, Ed3kContentsTap& tap,
                                                        const std::atomic<bool>* cancel = nullptr);
    /// readSet() into a listing (tests, tools).
    [[nodiscard]] static std::optional<ArchiveListing> readArchive(const QStringList& volumes, const QStringList& passwords,
                                                                   const std::atomic<bool>* cancel = nullptr,
                                                                   const QString& externalTool = {});
    /// Shares the contents manifest of @p listing and publishes its record (daemon thread).
    /// false: not started, nothing to list, or refused (logged).
    bool publishListing(const ArchiveListing& listing);
    /// Number of listings published since start (tests).
    [[nodiscard]] int published() const { return m_publishedCount; }

    /// §6.1: @p listing (read here) against the listing found for its first volume; a
    /// mismatch bans that record's publishers and drops it, a match drops it too (checked).
    /// nullopt: none found for it, or the manifest unreadable. Called by publishListing.
    std::optional<ffi::ContentsCheck> checkListing(const ArchiveListing& listing);
    /// contentsCheck of @p manifest (the contents manifest file) against @p entries.
    [[nodiscard]] static std::optional<ffi::ContentsCheck> compareListing(const QByteArray& manifest,
                                                                         const std::vector<ArchiveEntry>& entries);
    /// A listing found for @p first is kept for the check.
    [[nodiscard]] bool holdsFoundListing(const ffi::Root& first) const { return m_foundListings.contains(first); }
    /// Listings found false since start (tests).
    [[nodiscard]] int mismatches() const { return m_mismatchCount; }

    /// Indices of the opening volume of each archive set among @p names (headless sets
    /// left out); a lone archive file is its own set.
    [[nodiscard]] static QList<int> firstVolumes(const QStringList& names);
    /// The archive sets among @p names: each set's indices, opening volume first, then by
    /// volume order; sets in order of their opening volume (headless sets left out).
    [[nodiscard]] static QList<QList<int>> volumeSets(const QStringList& names);
    /// The file ids (usenet.md §1) of files @p indices of an NZB (its text); empty where none.
    [[nodiscard]] static std::vector<QByteArray> nzbFileIds(const QByteArray& nzbText, const QList<int>& indices);
    [[nodiscard]] static std::vector<QByteArray> nzbFileIds(const usenet::NzbInfo& nzb, const QList<int>& indices);

    // -- lookups ---------------------------------------------------------------

    /// One archive to look inside, by its first volume.
    struct Volume {
        QString name;                  ///< file name; a torrent's member path ('/' between)
        std::optional<ffi::Root> root; ///< known root (queued items, nested entries)
        QString url;                   ///< a hoster link: `url` SAME
        QByteArray nzbFileId;          ///< 32 B: `nzb-file` SAME
    };
    struct Request {
        std::vector<Volume> volumes;
        QByteArray btih;   ///< 20 B: volumes without a root are the torrent set's members
        QByteArray btmh;   ///< 32 B, the same
    };
    enum class State { Pending, Found, NotPublished, NoRoot, Failed, TimedOut };
    struct Found {
        QString name;
        State state = State::Pending;
        std::optional<ffi::Root> first;
        ffi::Root manifest;
        std::uint32_t sites = 0;
        QString password;
        std::vector<ArchiveEntry> entries;
        /// #224: per entry, a whole file of its root on disk here (localPathOf); empty: none
        std::vector<QString> localPaths;
        /// every entry is on disk here: the archive need not be downloaded
        [[nodiscard]] bool allLocal() const
        {
            return !entries.empty() && std::ranges::none_of(localPaths, [](const QString& p) { return p.isEmpty(); })
                   && localPaths.size() == entries.size();
        }
    };
    using Done = std::function<void(const std::vector<Found>&)>;
    /// Starts a lookup; @p done once on this thread when every volume ended or after
    /// @p timeoutMs. nullopt: the node is not running or has no contacts.
    std::optional<quint32> lookup(Request request, Done done, int timeoutMs = 120 * 1000);
    void cancelLookup(quint32 id);
    /// #224: a whole file of @p root (@p size bytes) here: shared by any join of the node or
    /// found by a folder scan; empty: none.
    [[nodiscard]] QString localPathOf(const ffi::Root& root, std::uint64_t size) const;
    /// The node (a folder scan runs on it).
    [[nodiscard]] Ed3kSession& session() const { return m_session; }
    /// Fills Found::localPaths of @p found (done by lookup before its callback).
    void markLocal(std::vector<Found>& found) const;
    [[nodiscard]] static QString stateName(State s);

protected:
    [[nodiscard]] std::vector<std::uint64_t> wantedBlocks(const Download& d) const override;
    void onFileBlock(const Download& d, const ffi::event::Block& b) override;
    void onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data) override;
    [[nodiscard]] std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override;
    void onOtherEvent(const ffi::Event& event) override;
    [[nodiscard]] bool keepsOwner(const QString& owner) const override;
    [[nodiscard]] bool sharesKeptOwner(const QString& owner) const override;
    [[nodiscard]] bool hasOwner(const QString& owner) const override;
    void saveOwner(const QString& owner) override;

private:
    enum class Stage { Resolve, Torrent, Contents };
    struct Candidate {
        ffi::Root root;
        std::uint64_t size = 0;
        std::uint32_t sites = 0;
        QString password;
        std::vector<ffi::PublisherKey> publishers;
    };
    /// A found listing (§6.1) until its archive is unpacked here.
    struct FoundListing {
        QString name;
        QByteArray manifest;   ///< the contents manifest file
        std::vector<ffi::PublisherKey> publishers;
    };
    struct Lookup {
        quint32 id = 0;
        Request request;
        std::vector<Found> found;
        /// per volume: contents candidates left to try
        std::vector<std::vector<Candidate>> candidates;
        Done done;
        QTimer* timer = nullptr;
        bool torrentPending = false;
    };
    struct Search {
        quint32 lookup = 0;
        int volume = -1;
        Stage stage = Stage::Resolve;
    };
    static constexpr int kTorrentManifest = -2;

    [[nodiscard]] static QString ownerOf(quint32 lookup);
    [[nodiscard]] static std::optional<quint32> lookupOf(const QString& owner);
    [[nodiscard]] static QString keptOwner(const ffi::Root& first);
    bool startSearch(Lookup& l, int volume, Stage stage, std::string_view scheme, std::span<const std::uint8_t> id,
                     std::uint32_t rel);
    void startContents(Lookup& l, int volume);
    void tryNextContents(Lookup& l, int volume);
    void onAliasFound(const ffi::event::AliasFound& found);
    void onTorrentManifest(Lookup& l, const QByteArray& data);
    void settle(Lookup& l, int volume, State state);
    void finishIfDone(quint32 id);
    void finish(quint32 id, bool timedOut);

    bool m_publish = true;
    int m_publishedCount = 0;
    quint32 m_nextLookup = 1;
    std::map<quint32, Lookup> m_lookups;
    std::map<std::uint32_t, Search> m_searches;
    std::map<ffi::Root, FoundListing> m_foundListings;   ///< by first volume
    int m_mismatchCount = 0;
};

} // namespace eMule::ed3k
