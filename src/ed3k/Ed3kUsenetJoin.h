#pragma once

/// @file Ed3kUsenetJoin.h
/// @brief A Usenet download joined with ed3k (usenet.md §4): the host side of packmule's
/// UsenetJoin, one per queue item.
///
/// Contract: libs/packmule/docs/usenet-join.md and usenet-join-host.md. The usenet module
/// fetches and decodes; packmule checks blocks, tests ALIAS claims, blames and builds what
/// a complete post publishes. Everything runs on the daemon thread except hashing a
/// complete member (QThreadPool, result posted back).
///
/// - Members are the files of the item's NzbInfo, in order: member index == fileIndex.
/// - articleDecoded → ybegin (crcOk) + article → reads served from the file's tempPath.
/// - Join events → UsenetQueue hooks (accept/refetch), hashing, sharing, publishing.
/// - ed3k downloads of members with a root: sources by Kad, blocks into tempPath.
/// - The join is saved in Config/ed3k/usenet/<itemId>.join (debounced, and at stop). The
///   post set (manifest, article list) fetched or made and the verified members are kept
///   under Config/ed3k/usenet/<itemId>/ and shared again after a restart.
/// - §4.1 a link (<meta type="ed3k">), §4.2 an `nzb` set (ALIAS search of the post id, the
///   base's set search): manifest, then the article list it names, fetched → setManifest.

#include "Ed3kJoinBase.h"

#include <QByteArray>
#include <QHash>
#include <QList>
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
#include <span>
#include <vector>

namespace eMule::usenet {
struct NzbInfo;
class UsenetQueueItem;
class UsenetQueue;
}

namespace eMule::ed3k {

class Ed3kSession;

/// What the join needs of the usenet queue: UsenetQueue in the daemon
/// (Ed3kUsenetJoin::queueAdapter), a fake in tests.
class UsenetJoinQueue {
public:
    using SealHold = std::function<bool(const QString& itemId, int fileIndex)>;

    virtual ~UsenetJoinQueue() = default;
    [[nodiscard]] virtual const usenet::UsenetQueueItem* findItem(const QString& id) const = 0;
    [[nodiscard]] virtual QStringList itemIds() const = 0;
    virtual int refetchRange(const QString& itemId, int fileIndex, qint64 offset, qint64 length,
                             const QString& excludeServerKey) = 0;
    virtual int acceptExternalRange(const QString& itemId, int fileIndex, qint64 offset, qint64 length) = 0;
    virtual void setSealHold(SealHold hold) = 0;
    virtual void releaseSealHold(const QString& itemId, int fileIndex) = 0;
    /// STAT of @p messageIds (UsenetQueue::checkArticles); the answer comes to
    /// Ed3kUsenetJoin::onArticlesChecked. false: nobody to ask.
    virtual bool checkArticles(const QString& checkId, const QStringList& messageIds, const QStringList& groups, qint64 date)
    {
        Q_UNUSED(checkId) Q_UNUSED(messageIds) Q_UNUSED(groups) Q_UNUSED(date) return false;
    }
};

class Ed3kUsenetJoin : public Ed3kJoinBase {
    Q_OBJECT

public:
    /// @p configDir: the joins go to <configDir>/ed3k/usenet. Both must outlive this.
    Ed3kUsenetJoin(Ed3kSession& session, UsenetJoinQueue& queue, const QString& configDir,
                   QObject* parent = nullptr);
    ~Ed3kUsenetJoin() override;

    /// UsenetQueue as a UsenetJoinQueue. The queue must outlive it.
    [[nodiscard]] static std::unique_ptr<UsenetJoinQueue> queueAdapter(usenet::UsenetQueue& queue);
    /// Connects the queue's signals to the slots below.
    void connectQueue(usenet::UsenetQueue& queue);

    /// Joins for the items of the queue (saved ones loaded); installs the seal hold.
    /// Called when ed3k starts; idempotent.
    void start() override;
    /// Saves every join and drops them; closes downloads. Before the ed3k node stops.
    void stop() override;

    // -- the NZB buffer (usenet-join.md "The object") ----------------------

    /// Per file every <segment> in NZB order: raw `number`, mᵢ (empty when voided).
    [[nodiscard]] static std::vector<::ed3k::NzbFileSegments> nzbSegments(const usenet::NzbInfo& nzb);
    /// The buffer ed3k_usenet_new takes: u32 files; per file u32 segments; per segment
    /// u32 len ‖ number ‖ u32 len ‖ mᵢ, little-endian.
    [[nodiscard]] static QByteArray nzbBuffer(const usenet::NzbInfo& nzb);
    /// The root of <meta type="ed3k"> (an ed3k:file link); nullopt without or unparsable.
    [[nodiscard]] static std::optional<::ed3k::Root> metaRoot(const usenet::NzbInfo& nzb);
    /// The u64 tag of a news server account for the core (§7: never its identity).
    [[nodiscard]] static std::uint64_t serverTag(const QString& serverKey);

    // -- state, for the search side and tests ------------------------------

    [[nodiscard]] bool hasJoin(const QString& itemId) const;
    [[nodiscard]] std::optional<::ed3k::UsenetMember> member(const QString& itemId, int fileIndex) const;
    [[nodiscard]] QString joinPath(const QString& itemId) const;
    /// §4.2: an nzb-file record claims @p root for a member. false: refused (blame it).
    bool fileClaim(const QString& itemId, int fileIndex, const ::ed3k::Root& root, std::uint64_t size,
                   std::span<const ::ed3k::PublisherKey> publishers = {});
    /// A post set with its article list (§4.1, or found by `nzb`). false: refused.
    bool setManifest(const QString& itemId, std::span<const std::byte> manifest, std::span<const std::byte> list,
                     std::span<const ::ed3k::PublisherKey> publishers = {});
    /// A source of a member's root (from Kad or known): asked for the file.
    void addSource(const QString& itemId, int fileIndex, const ::ed3k::Source& source);
    /// §4.2: a post set known otherwise (tests): its manifest and article list fetched as
    /// for one an `nzb` record names. false: not fetched (a set in use, refused).
    bool trySet(const QString& itemId, const ::ed3k::Root& root, std::uint64_t size,
                std::span<const ::ed3k::PublisherKey> publishers = {});
    /// A source of a blob being fetched (the post set's manifest or article list).
    void addBlobSource(const ::ed3k::Root& root, const ::ed3k::Source& source);

public slots:
    void onItemAdded(const QString& itemId);
    // was: void onItemRemoved(const QString& itemId);
    /// @p deleteFiles: the user removed it with its files: its deleted rows end too (alias.md §3)
    void onItemRemoved(const QString& itemId, bool deleteFiles = false);
    void onItemFinished(const QString& itemId, bool success, const QString& message);
    void onArticleDecoded(const QString& itemId, int fileIndex, qint64 offset, qint64 length, bool crcOk,
                          int partNumber, const QByteArray& rawYbeginLine, const QString& serverKey,
                          const QString& messageId);
    void onFilesRepaired(const QString& itemId, const QList<int>& fileIndices);
    /// alias.md §3 "Deleted file": a deleted volume's articles asked (@p checkId: its root
    /// in hex). Present: kept; every account refused them all: its records end.
    void onArticlesChecked(const QString& checkId, int present, int missing, int unknown);

signals:
    /// A member verified under @p root and is shared (tests, GUI later).
    void memberVerified(const QString& itemId, int fileIndex);
    /// MEMBER_REJECTED, LINK_MISMATCH or CLAIM_FALSE; @p message is localized.
    void memberProblem(const QString& itemId, int fileIndex, const QString& message);
    /// POST_COMPLETE handled: what it gave is published.
    void postCompleted(const QString& itemId, bool hasPostRoot);

protected:
    [[nodiscard]] std::vector<std::uint64_t> wantedBlocks(const Download& d) const override;
    [[nodiscard]] QString downloadName(const ffi::Root& root, const Download& d) const override;
    void onFileBlock(const Download& d, const ::ed3k::event::Block& b) override;
    void onBlobFetched(const QString& owner, int member, const ::ed3k::Root& root, const QByteArray& data) override;
    void onDownloadHashes(const Download& d, const ::ed3k::Bytes& blob) override;
    [[nodiscard]] std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override;
    [[nodiscard]] bool sharedGone(const Shared& s) const override;
    void onOtherEvent(const ::ed3k::Event& event) override;
    [[nodiscard]] bool wantsSet(const QString& owner) const override;
    void onSetManifest(const QString& owner, const ::ed3k::Root& root, const QByteArray& data) override;
    void onSetSearchDone(const QString& owner) override;
    [[nodiscard]] bool keepsOwner(const QString& owner) const override;
    [[nodiscard]] QString keptMemberPath(const QString& owner, int member) const override;
    [[nodiscard]] bool shareKeptBlobs() const override { return !m_holdBack; }
    [[nodiscard]] bool hasOwner(const QString& owner) const override;
    void saveOwner(const QString& owner) override;
    /// the articles of deleted volumes asked once kDeletedCheckMs passed
    void checkDeleted(qint64 now) override;

private:
    struct Post;

    [[nodiscard]] std::shared_ptr<Post> ensurePost(const QString& itemId);
    [[nodiscard]] std::shared_ptr<Post> findPost(const QString& itemId) const;
    void dropPost(const QString& itemId, bool keepBlob);
    /// Serves @p reads and those block_read gives, then drains the join's events.
    void serveReads(const QString& itemId, std::vector<::ed3k::TorrentRead> reads);
    /// One batch on the read pool; @p again: a second try under the paths now (no third).
    void readBatch(const QString& itemId, std::vector<::ed3k::TorrentRead> reads, bool again);
    /// Polls the join of @p itemId until empty; handlers may drop it.
    void drainEvents(const QString& itemId);
    void handleEvent(const QString& itemId, const ::ed3k::UsenetEvent& ev);
    void onMemberComplete(const QString& itemId, int fileIndex);
    void onMemberHashed(const QString& itemId, int fileIndex, quint64 generation, const QString& path,
                        const std::optional<FileHash>& hashed);
    void onMemberVerified(const QString& itemId, int fileIndex, const ::ed3k::Root& root);
    void onHaveChanged(const QString& itemId, int fileIndex);
    void onClaimFalse(const QString& itemId, const ::ed3k::uev::ClaimFalse& ev);
    void onPostComplete(const QString& itemId);
    void reportProblem(const QString& itemId, int fileIndex, const QString& message);
    [[nodiscard]] bool sealHold(const QString& itemId, int fileIndex);
    /// Seal holds taken for reads of @p itemId released once none is pending.
    void releaseReadHolds(const QString& itemId);
    /// The file of a member on disk now: tempPath, or finalPath once published.
    [[nodiscard]] QString memberPath(const QString& itemId, int fileIndex) const;
    /// The member paths of @p itemId kept for after its row is cleared (keepMemberPaths).
    void rememberPaths(const QString& itemId);
    /// alias.md §3 "Deleted file": the verified members of a finished item not in incoming
    /// (volumes, repair files: post-processing deletes them) keep their nzb-file records.
    void keepDeletedVolumes(const QString& itemId);
    /// Cleared from the queue, files of it still on disk: shared and published on (cached
    /// in m_cleared).
    [[nodiscard]] bool clearedKept(const QString& itemId) const;
    [[nodiscard]] std::optional<QByteArray> readMember(const QString& itemId, int fileIndex, std::uint64_t offset,
                                                       std::uint32_t len) const;
    void startDownload(const QString& itemId, int fileIndex);
    void onAliasFound(const ::ed3k::event::AliasFound& f);
    /// §4.2: the `nzb` search of the post id; none (no post id, searches off): the claims.
    void startPostSearch(const QString& itemId);
    void startClaimSearches(const QString& itemId);
    /// The manifest of a post set is there: the article list it names is fetched next.
    void fetchList(const QString& itemId, const ::ed3k::Root& root, const QByteArray& manifest,
                   std::vector<::ed3k::PublisherKey> publishers, bool fromLink);
    void onListFetched(const QString& itemId, const ::ed3k::Root& root, const QByteArray& list);
    /// A post set that does not do: a link's is told, a candidate's blamed (next one tried).
    void setRefused(const QString& itemId, const ::ed3k::Root& root, bool fromLink);
    [[nodiscard]] QString postName(const QString& itemId) const;
    /// <meta type="ed3k"> parsed; nullopt without or unparsable.
    [[nodiscard]] static std::optional<::ed3k::FileLink> metaLink(const usenet::NzbInfo& nzb);
    void savePost(const QString& itemId);
    [[nodiscard]] QString usenetDir() const;

    UsenetJoinQueue& m_queue;
    std::map<QString, std::shared_ptr<Post>> m_posts;
    /// items with no join (extension off, malformed NZB, complete): never asked again
    QSet<QString> m_noJoin;
    /// items cleared from the queue whose kept files are still there: member → path
    mutable std::map<QString, QHash<int, QString>> m_cleared;
    /// alias searches → (item, member)
    std::map<std::uint32_t, std::pair<QString, int>> m_claimSearches;
    QHash<std::uint64_t, QString> m_serverKeys;
    quint64 m_hashGeneration = 0;
    /// members complete now are hashed on this thread (volumes about to be deleted)
    bool m_hashNow = false;
    /// deleted volumes (root hex) whose articles are being asked; asked again after a
    /// check that could not tell
    QSet<QString> m_deletedChecking;
    std::map<QString, qint64> m_deletedRetryAt;
};

} // namespace eMule::ed3k
