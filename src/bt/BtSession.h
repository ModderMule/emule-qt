#pragma once

/// @file BtSession.h
/// @brief Façade of the BitTorrent engine (libtorrent): what the daemon owns.
///
/// Like UsenetSession: constructed always, the preferences gate what it does. Lives in
/// eMule::Bt, which links eMule::Core; core never reaches into bt. No libtorrent type in
/// this header (research doc §4): the session is behind a pImpl.
///
/// libtorrent runs its own threads. Its alerts are drained on this object's thread
/// (research doc §5.2); every signal and call here is on that thread.

#include "BtJoin.h"
#include "BtTypes.h"

#include <QObject>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

namespace eMule::bt {

class BtJoinDiskIo;

class BtSession : public QObject {
    Q_OBJECT

public:
    explicit BtSession(QObject* parent = nullptr);
    ~BtSession() override;

    /// Starts libtorrent with these settings, the session state and the torrents of
    /// `settings.stateDir`. Idempotent.
    void start(const BtSettings& settings);

    /// Saves resume data of every torrent and the session state, then stops libtorrent.
    /// Blocks for at most a few seconds. Idempotent.
    void stop();

    [[nodiscard]] bool isRunning() const;

    /// New settings while running. PEX changes when the session starts again.
    void applySettings(const BtSettings& settings);

    /// Session-wide caps in KB/s, 0 = unlimited. Set twice a second by the daemon's
    /// share of the global limits; overrides BtSettings' caps until the next call.
    /// Cheap when nothing changed.
    void setRateLimits(quint32 downKb, quint32 upKb);

    /// The app's IP filter (research doc §7.6): peers in these ranges are refused.
    /// Kept across stop/start; replaces the previous one. Empty = no filter.
    void setIpFilter(const std::vector<BtIpRange4>& v4, const std::vector<BtIpRange6>& v6);
    /// peers refused by the filter since start
    [[nodiscard]] quint64 blockedPeers() const;

    /// Demand on the shared lines; samples the session counters as a side effect,
    /// so call it at a steady pace (the coordinator's tick).
    [[nodiscard]] BtDemand demand();

    /// Adds a .torrent file. `savePath` empty = the download dir.
    BtAddResult addTorrentFile(const QByteArray& data, const QString& savePath = {}, bool paused = false);
    /// Adds a BitTorrent magnet link.
    BtAddResult addMagnet(const QString& uri, const QString& savePath = {}, bool paused = false);

    bool remove(const QString& id, bool deleteFiles);
    bool pause(const QString& id);
    bool resume(const QString& id);
    bool recheck(const QString& id);
    bool reannounce(const QString& id);
    /// One libtorrent priority 0..7 per file, in file order.
    bool setFilePriorities(const QString& id, const QList<int>& priorities);

    /// Per-torrent speed, peer and seed limits. nullopt: unknown id.
    [[nodiscard]] std::optional<BtTorrentOptions> options(const QString& id) const;
    bool setOptions(const QString& id, const BtTorrentOptions& options);
    /// Adds trackers to the last tier, skipping those already there.
    bool addTrackers(const QString& id, const QStringList& urls);
    bool removeTrackers(const QString& id, const QStringList& urls);

    /// The rows as of the last status update (≤ 1 s old).
    [[nodiscard]] QList<BtTorrentRow> torrents() const;
    [[nodiscard]] std::optional<BtTorrentRow> torrent(const QString& id) const;
    [[nodiscard]] QList<BtFileRow> files(const QString& id) const;
    [[nodiscard]] QList<BtPeerRow> peers(const QString& id) const;
    [[nodiscard]] QList<BtTrackerRow> trackers(const QString& id) const;
    /// Peers per piece, bucketed (BtTorrentView::availabilityMap).
    [[nodiscard]] QByteArray availabilityMap(const QString& id) const;
    [[nodiscard]] BtStatus status() const;

    /// The disk I/O of the session, for an ed3k join (torrent.md §4); null while stopped.
    [[nodiscard]] BtJoinDiskIo* diskIo() const;

    // -- an ed3k join (torrent.md §4): what it needs of a torrent ----------------

    /// Routes the piece checks of @p id through @p sink, also across stop/start. false:
    /// unknown id.
    bool attachJoin(const QString& id, std::shared_ptr<BtJoinSink> sink);
    void detachJoin(const QString& id);
    /// The bencoded info dictionary; empty before the metadata is there.
    [[nodiscard]] QByteArray infoSection(const QString& id) const;
    /// Pieces and files; nullopt before the metadata or for an unknown id.
    [[nodiscard]] std::optional<BtLayout> layout(const QString& id) const;
    /// Hands a whole piece to libtorrent (it hashes it as if a peer had sent it).
    bool addPiece(const QString& id, int piece, const QByteArray& data);
    /// Asks peers for these pieces first (a held piece waits for them).
    void raisePieces(const QString& id, const QList<int>& pieces);
    /// The ed3k:file link the torrent was added with (link or magnet `xs`); kept across
    /// restarts. Empty: none.
    [[nodiscard]] QString ed3kLink(const QString& id) const;

signals:
    void torrentAdded(const QString& id);
    void torrentChanged(const QString& id);
    void torrentRemoved(const QString& id);
    /// every wanted byte is there
    void torrentFinished(const QString& id);
    void statusChanged();
    /// The info dictionary of a magnet arrived.
    void metadataReceived(const QString& id);
    /// libtorrent's verdict on a piece of a torrent with a join attached.
    void pieceResult(const QString& id, int piece, bool passed);

private slots:
    void drainAlerts();
    void tick();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace eMule::bt
