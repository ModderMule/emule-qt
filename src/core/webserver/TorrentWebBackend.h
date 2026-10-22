#pragma once

/// @file TorrentWebBackend.h
/// @brief What the REST API may ask of the BitTorrent engine.
///
/// Core never links the bt module, so the /api/v1/torrents routes talk to this
/// interface and DaemonApp installs the implementation — the UsenetWebBackend seam.
/// Rows travel as CBOR maps in the IPC shape (GetTorrents, GetTorrentDetails), so the
/// GUI and the REST API read one field vocabulary from one serializer.
///
/// The routes exist only in a build with EMULE_HAVE_BITTORRENT; the interface itself
/// is always declared so WebServer has one layout in every build.

#include <QByteArray>
#include <QCborArray>
#include <QCborMap>
#include <QList>
#include <QString>
#include <QStringList>

namespace eMule {

/// Mirrors bt::BtAddOutcome; core cannot name the bt enum.
enum class TorrentWebAddOutcome : int {
    Added = 0,
    Duplicate = 1,
    Invalid = 2,
    NotRunning = 3,
    Failed = 4,
};

struct TorrentWebAddResult {
    TorrentWebAddOutcome outcome = TorrentWebAddOutcome::Failed;
    QString id;         ///< the torrent's id; also set for a Duplicate
    QString message;    ///< English
};

class TorrentWebBackend {
public:
    virtual ~TorrentWebBackend() = default;

    /// False while BitTorrent is switched off in the preferences.
    [[nodiscard]] virtual bool running() const = 0;

    /// The GetBtStatus map.
    [[nodiscard]] virtual QCborMap status() const = 0;
    /// The GetTorrents rows.
    [[nodiscard]] virtual QCborArray torrents() const = 0;
    /// One GetTorrents row; empty for an unknown id.
    [[nodiscard]] virtual QCborMap torrent(const QString& id) const = 0;
    /// The GetTorrentDetails map (files, peers, trackers, options); empty for an unknown id.
    [[nodiscard]] virtual QCborMap details(const QString& id) const = 0;

    virtual TorrentWebAddResult addTorrentFile(const QByteArray& data, const QString& savePath,
                                               bool paused) = 0;
    virtual TorrentWebAddResult addMagnet(const QString& uri, const QString& savePath,
                                          bool paused) = 0;

    virtual bool remove(const QString& id, bool deleteFiles) = 0;
    virtual bool pause(const QString& id) = 0;
    virtual bool resume(const QString& id) = 0;
    virtual bool recheck(const QString& id) = 0;
    virtual bool reannounce(const QString& id) = 0;

    /// One priority per file, in file order (0 = skip, 1-7).
    virtual bool setFilePriorities(const QString& id, const QList<int>& priorities) = 0;
    /// A SetTorrentOptions map, every field present.
    virtual bool setOptions(const QString& id, const QCborMap& options) = 0;
    virtual bool addTrackers(const QString& id, const QStringList& urls) = 0;
    virtual bool removeTrackers(const QString& id, const QStringList& urls) = 0;
};

} // namespace eMule
