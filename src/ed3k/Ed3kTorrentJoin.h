#pragma once

/// @file Ed3kTorrentJoin.h
/// @brief A torrent joined with ed3k (torrent.md §4): the host side of packmule's Torrent,
/// one per torrent with metadata.
///
/// Contract: libs/packmule/docs/torrent-join.md (Part C, the host loop). BitTorrent
/// (bt::BtSession) downloads and checks pieces; packmule judges them (BLAKE3 for a torrent
/// started from an ed3k:file link, the torrent's hashes otherwise), tests the sets ALIAS
/// records claim, and builds what a complete torrent publishes. Everything runs on the
/// daemon thread except the piece check's entry (libtorrent's thread, hopped over) and
/// hashing a complete member (QThreadPool).
///
/// - Members are the torrent's files without padding, in order.
/// - Piece checks: libtorrent's hash job waits for the verdict (BtJoinSink); reads are
///   served from the member files; a held piece fails after kHoldTimeoutMs.
/// - §4.1 (link root): the link's manifest is fetched over ed3k first. §4.2: the ALIAS
///   records of the infohashes (btih/btmh, relation SET) name sets; at most 3 are tried.
/// - Member downloads: ed3k blocks are written into the member files; each piece they
///   complete goes to libtorrent (add_piece), which hashes it as from a peer.
/// - The join is saved in Config/ed3k/torrent/<id>.join (debounced, and at stop). The
///   manifests and info dictionary fetched or made, and the verified members, are kept
///   under Config/ed3k/torrent/<id>/ and shared again after a restart.

#ifdef EMULE_HAVE_BITTORRENT

#include "Ed3kJoinBase.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace eMule::bt {
class BtSession;
}

namespace eMule::ed3k {

class Ed3kTorrentJoin : public Ed3kJoinBase {
    Q_OBJECT

public:
    /// A held piece fails after this long (torrent-join.md: the host's timeout).
    static constexpr int kHoldTimeoutMs = 60 * 1000;

    /// @p configDir: the joins go to <configDir>/ed3k/torrent. Both sessions must outlive this.
    Ed3kTorrentJoin(Ed3kSession& session, bt::BtSession& bt, const QString& configDir, QObject* parent = nullptr);
    ~Ed3kTorrentJoin() override;

    /// Joins for the torrents of the session (saved ones loaded). Called when ed3k
    /// starts; idempotent.
    void start() override;
    /// Saves every join and drops them; lets every waiting piece go. Before either
    /// session stops.
    void stop() override;

    // -- state, for the daemon and tests -----------------------------------

    [[nodiscard]] bool hasJoin(const QString& id) const;
    [[nodiscard]] std::optional<::ed3k::TorrentInfo> info(const QString& id) const;
    [[nodiscard]] std::optional<::ed3k::TorrentMember> member(const QString& id, int index) const;
    [[nodiscard]] QString joinPath(const QString& id) const;
    /// A set's manifest for the torrent (§4.1 the link's, §4.2 a claim). false: refused.
    bool setManifest(const QString& id, std::span<const std::byte> manifest,
                     std::span<const ::ed3k::PublisherKey> publishers = {});
    /// A source of a member's root (from Kad or known): asked for the file.
    void addSource(const QString& id, int member, const ::ed3k::Source& source);
    /// A source of the manifest being fetched (§4.1 the link's, §4.2 the set tried).
    void addManifestSource(const QString& id, const ::ed3k::Source& source);
    /// The manifest TORRENT_COMPLETE gave (shared); empty before.
    [[nodiscard]] QByteArray completedManifest(const QString& id) const;
    /// ed3k blocks written into @p member (tests: libtorrent's pieces are skipped).
    [[nodiscard]] int receivedBlocks(const QString& id, int member) const;

public slots:
    void onTorrentAdded(const QString& id);
    void onMetadataReceived(const QString& id);
    void onTorrentRemoved(const QString& id);
    void onPieceResult(const QString& id, int piece, bool passed);
    /// libtorrent's resume check is done: its pieces are skipped on ed3k.
    void onTorrentChecked(const QString& id);

signals:
    /// A member verified under its root and is shared.
    void memberVerified(const QString& id, int member);
    /// MEMBER_REJECTED, LINK_MISMATCH or CLAIM_FALSE; @p message is localized.
    void memberProblem(const QString& id, int member, const QString& message);
    /// TORRENT_COMPLETE handled: the manifest is shared, the records published.
    void torrentCompleted(const QString& id);

protected:
    [[nodiscard]] std::vector<std::uint64_t> wantedBlocks(const Download& d) const override;
    void onFileBlock(const Download& d, const ::ed3k::event::Block& b) override;
    void onBlobFetched(const QString& owner, int member, const ::ed3k::Root& root, const QByteArray& data) override;
    void onDownloadHashes(const Download& d, const ::ed3k::Bytes& blob) override;
    [[nodiscard]] std::optional<FileRead> sharedRead(const Shared& s, std::uint64_t offset, std::uint32_t len) const override;
    // was: onOtherEvent for the AliasFound of the set searches; now the base's
    [[nodiscard]] bool wantsSet(const QString& owner) const override;
    void onSetManifest(const QString& owner, const ::ed3k::Root& root, const QByteArray& data) override;
    [[nodiscard]] bool keepsOwner(const QString& owner) const override;
    [[nodiscard]] QString keptMemberPath(const QString& owner, int member) const override;
    [[nodiscard]] bool hasOwner(const QString& owner) const override;
    void saveOwner(const QString& owner) override;

private:
    struct Join;
    class Sink;
    struct Gate;

    [[nodiscard]] Join* ensureJoin(const QString& id);
    [[nodiscard]] Join* findJoin(const QString& id) const;
    void dropJoin(const QString& id, bool keepBlob);
    /// libtorrent hashed a piece (hopped onto this thread by the Sink).
    void onPieceHashed(const QString& id, int piece, const QByteArray& data, std::function<void(bool)> done);
    /// Serves reads a check asked for, then drains the join's events.
    void serveReads(const QString& id, const std::vector<::ed3k::TorrentRead>& reads);
    void drainEvents(const QString& id);
    void handleEvent(const QString& id, const ::ed3k::TorrentEvent& ev);
    /// Hands the verdict of a waiting piece back to libtorrent.
    void finishPiece(const QString& id, int piece, bool spoil);
    void onHoldTimeout(const QString& id, int piece);
    void onMemberComplete(const QString& id, int member);
    void onMemberHashed(const QString& id, int member, quint64 generation, const std::optional<FileHash>& hashed);
    void onMemberVerified(const QString& id, int member, const ::ed3k::Root& root);
    void onHaveChanged(const QString& id, int member);
    void onClaimFalse(const QString& id, const ::ed3k::tev::ClaimFalse& ev);
    void onTorrentComplete(const QString& id);
    /// Member downloads of every member with a root that is still open.
    void startDownloads(const QString& id);
    /// §4.2: ALIAS searches of the infohashes.
    void startSetSearch(const QString& id);
    // was: tryNextSet, the next set a search names; now the base's
    /// Pieces an ed3k block completed: read and handed to libtorrent.
    void feedPieces(const QString& id, const std::vector<std::uint64_t>& pieces);
    void reportProblem(const QString& id, int member, const QString& message);
    [[nodiscard]] QString memberPath(const Join& j, int member) const;
    [[nodiscard]] QString name(const QString& id) const;
    void saveJoin(const QString& id);
    /// Every piece over [begin, end) of the piece space is in libtorrent (Join::have).
    [[nodiscard]] bool piecesHad(const Join& j, std::uint64_t begin, std::uint64_t end) const;
    [[nodiscard]] QString torrentDir() const;

    bt::BtSession& m_bt;
    std::map<QString, std::unique_ptr<Join>> m_joins;
    /// torrents with no join (private, extension off, complete before ed3k saw them)
    QSet<QString> m_noJoin;
    // was: alias searches → torrent id; now the base's set searches
    /// the sinks' way back here; closed in the destructor
    std::shared_ptr<Gate> m_gate;
    quint64 m_hashGeneration = 0;
};

} // namespace eMule::ed3k

#endif // EMULE_HAVE_BITTORRENT
