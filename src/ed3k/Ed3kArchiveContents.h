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

#include "Ed3kJoinBase.h"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
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

    /// Reads and hashes the set @p volumes (in order) on the calling thread; nullopt:
    /// unreadable, encrypted beyond libarchive, a path the manifest refuses, @p cancel set.
    [[nodiscard]] static std::optional<ArchiveListing> readArchive(const QStringList& volumes, const QStringList& passwords,
                                                                   const std::atomic<bool>* cancel = nullptr);
    /// Shares the contents manifest of @p listing and publishes its record (daemon thread).
    /// false: not started, nothing to list, or refused (logged).
    bool publishListing(const ArchiveListing& listing);
    /// Number of listings published since start (tests).
    [[nodiscard]] int published() const { return m_publishedCount; }

    /// Indices of the opening volume of each archive set among @p names (headless sets
    /// left out); a lone archive file is its own set.
    [[nodiscard]] static QList<int> firstVolumes(const QStringList& names);
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
    };
    using Done = std::function<void(const std::vector<Found>&)>;
    /// Starts a lookup; @p done once on this thread when every volume ended or after
    /// @p timeoutMs. nullopt: the node is not running or has no contacts.
    std::optional<quint32> lookup(Request request, Done done, int timeoutMs = 120 * 1000);
    void cancelLookup(quint32 id);
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
};

} // namespace eMule::ed3k
