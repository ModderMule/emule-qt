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

#include "BtTypes.h"

#include <QObject>
#include <QStringList>

#include <memory>
#include <optional>

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

signals:
    void torrentAdded(const QString& id);
    void torrentChanged(const QString& id);
    void torrentRemoved(const QString& id);
    /// every wanted byte is there
    void torrentFinished(const QString& id);
    void statusChanged();

private slots:
    void drainAlerts();
    void tick();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace eMule::bt
