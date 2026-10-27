#pragma once

/// @file Ed3kJoinBase.h
/// @brief What every ed3k join (usenet.md §4, torrent.md §4) does on the ed3k side: ed3k
/// downloads of member roots (Kad sources, streams, block requests), manifests fetched
/// into memory, serving shared roots, sharing blobs, hashing complete files on a worker,
/// debounced saving.
///
/// Everything runs on the daemon thread (the engine's). The derived join says which
/// blocks it wants, where a block goes and what may be served; it keys its own state by
/// an owner id (queue item, torrent) and a member index.

#include "Ed3kFfi.h"

#include <QByteArray>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <vector>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

class Ed3kSession;

class Ed3kJoinBase : public QObject {
    Q_OBJECT

public:
    ~Ed3kJoinBase() override;

    /// Joins for what is there (saved ones loaded). Called when ed3k starts; idempotent.
    virtual void start() = 0;
    /// Saves every join and drops them; closes downloads. Before the ed3k node stops.
    virtual void stop() = 0;
    [[nodiscard]] bool isStarted() const { return m_started; }

    /// Holding back (§6): no ALIAS records. Default off.
    void setHoldBack(bool on) { m_holdBack = on; }
    /// Kad searches (sources, claims) on/off; tests switch them off.
    void setSearchEnabled(bool on) { m_searchEnabled = on; }

protected:
    static constexpr std::uint64_t kBlock = 256 * 1024;

    /// One ed3k download: a member's root into its file, or a manifest into memory.
    struct Download {
        QString owner;
        int member = -1;
        std::uint64_t size = 0;
        std::map<std::uint64_t, bool> streams;                ///< stream → holds a slot
        std::map<std::uint64_t, std::uint64_t> outstanding;   ///< offset → stream
        std::set<ffi::NodeId> asked;
        bool hashes = false;
        /// fetched into memory (fetchBlob): the bytes and the blocks there
        bool inMemory = false;
        QByteArray memory;
        std::set<std::uint64_t> got;
    };

    /// A root this node serves: a member's file (owner, member) or a blob in memory.
    struct Shared {
        QString owner;
        int member = -1;
        std::uint64_t size = 0;
        QByteArray blob;
    };

    /// @p tag names the join in log lines ("usenet", "torrent").
    Ed3kJoinBase(Ed3kSession& session, const QString& configDir, QString tag, QObject* parent);

    // -- what the derived join decides ------------------------------------

    /// Block offsets of a file download still wanted (outstanding ones are skipped here).
    [[nodiscard]] virtual std::vector<std::uint64_t> wantedBlocks(const Download& d) const = 0;
    /// A verified block of a file download arrived.
    virtual void onFileBlock(const Download& d, const ffi::event::Block& b) = 0;
    /// A blob (fetchBlob) is complete and verified block by block. The download is gone.
    virtual void onBlobFetched(const QString& owner, int member, const ffi::Root& root, const QByteArray& data) = 0;
    /// The block values of a download arrived (event::Hashes).
    virtual void onDownloadHashes(const Download& d, const ffi::Bytes& blob) { Q_UNUSED(d) Q_UNUSED(blob) }
    /// Bytes of a shared member to serve; nullopt: not servable (not verified, gone).
    [[nodiscard]] virtual std::optional<QByteArray> readShared(const Shared& s, std::uint64_t offset, std::uint32_t len) const = 0;
    /// A shared member could not be read: whether its file is gone for good (not shared then).
    [[nodiscard]] virtual bool sharedGone(const Shared& s) const { Q_UNUSED(s) return false; }
    /// Engine events the base leaves alone (AliasFound ...).
    virtual void onOtherEvent(const ffi::Event& event) { Q_UNUSED(event) }
    [[nodiscard]] virtual bool hasOwner(const QString& owner) const = 0;
    virtual void saveOwner(const QString& owner) = 0;

    // -- ed3k downloads ----------------------------------------------------

    /// Opens a download of @p root into the member's file and searches its sources.
    /// false: open already, or refused.
    bool openDownload(const ffi::Root& root, std::uint64_t size, const QString& owner, int member);
    /// The same into memory: onBlobFetched() when every block is there.
    bool fetchBlob(const ffi::Root& root, std::uint64_t size, const QString& owner, int member);
    void stopDownload(const ffi::Root& root);
    /// Every download of @p owner.
    void stopDownloads(const QString& owner);
    [[nodiscard]] bool downloading(const ffi::Root& root) const { return m_downloads.contains(root); }
    /// A source of a download's root: asked for the file.
    void addSource(const ffi::Root& root, const ffi::Source& source);
    void requestBlocks(const ffi::Root& root);

    // -- serving -----------------------------------------------------------

    /// Shares @p data from memory under its root; logs when it is not @p expect.
    void shareBlob(const QByteArray& data, const QString& name, std::optional<ffi::Root> expect);
    /// Opens a member file on the engine (complete or partial). false: refused (logged).
    bool shareMember(const ffi::SharedFile& file, const QString& owner, int member);
    void unshare(const ffi::Root& root);
    /// Every root of @p owner.
    void unshareOwner(const QString& owner);
    [[nodiscard]] bool isShared(const ffi::Root& root) const { return m_shared.contains(root); }

    // -- hashing -----------------------------------------------------------

    struct FileHash {
        ffi::Root root;
        std::uint64_t size = 0;
        ffi::Bytes hashes;
    };
    /// Hashes @p path on a worker; @p done on this thread (nullopt: unreadable), not at
    /// all once stopped or destroyed.
    void hashInBackground(const QString& path, std::function<void(std::optional<FileHash>)> done);

    // -- state -------------------------------------------------------------

    void scheduleSave(const QString& owner);
    void dropDirty(const QString& owner) { m_dirty.remove(owner); }
    /// Marks started, a fresh cancel flag.
    void beginStart();
    /// Closes every download, stream and share; cancels workers; saves nothing.
    void closeAll();
    void pumpSession();
    [[nodiscard]] QString joinDir(const QString& sub) const;
    void logDebug(const QString& msg) const;
    void logWarning(const QString& msg) const;
    void logInfo(const QString& msg) const;

    static QString hexOf(std::span<const std::uint8_t> bytes);
    static QString errorName(ffi::Error e);
    static std::span<const std::byte> spanOf(const QByteArray& b);

    Ed3kSession& m_session;
    QString m_configDir;
    bool m_started = false;
    bool m_holdBack = false;
    bool m_searchEnabled = true;

private:
    void onEventPolled(const ffi::Event& event, bool* handled);
    void onSessionStatus();
    bool answerBlock(std::uint64_t id, const ffi::Root& root, std::uint64_t offset, std::optional<std::uint32_t> len);
    void onBlock(const ffi::event::Block& b);
    void onKadFound(const ffi::event::KadFound& found);
    bool open(const ffi::Root& root, std::uint64_t size, const QString& owner, int member, bool inMemory);
    void saveDirty();

    QString m_tag;
    std::map<ffi::Root, Shared> m_shared;
    std::map<ffi::Root, std::unique_ptr<Download>> m_downloads;
    /// download streams → root; Kad source lookups → root
    std::map<std::uint64_t, ffi::Root> m_streams;
    std::map<std::uint64_t, ffi::Root> m_sourceLookups;
    QSet<QString> m_dirty;
    QTimer m_saveTimer;
    std::shared_ptr<std::atomic<bool>> m_cancel;
};

} // namespace eMule::ed3k
