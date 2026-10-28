#pragma once

/// @file TorrentListModel.h
/// @brief Model for the Torrents tab: one row per torrent, files as children.
///
/// Rows come off the wire as the CBOR maps of src/daemon/BtBridge.cpp (the GUI
/// never links eMule::Bt). Updates are incremental — upsert/remove per push and
/// a keyed merge per poll — so selection and scroll survive the once-a-second
/// refresh, as in UsenetQueueModel.
///
/// Multi-file torrents get one child row per file. Files are not part of the
/// torrent row: they come from GetTorrentDetails, asked for when a torrent is
/// expanded (fetchMore -> filesWanted). internalId of a child = its torrent's
/// key (stable, never a row number), 0 = top level.

#include <QAbstractItemModel>
#include <QByteArray>
#include <QCborArray>
#include <QCborMap>
#include <QHash>
#include <QList>
#include <QString>

#include <vector>

namespace eMule {

/// Mirrors bt::BtState. Values are IPC: append only.
enum class TorrentState : int {
    Queued = 0,
    CheckingFiles = 1,
    DownloadingMetadata = 2,
    Downloading = 3,
    Finished = 4,
    Seeding = 5,
    CheckingResume = 6,
    Paused = 7,
    Error = 8,
    Moving = 9,
};

/// Localized name of @p state, as the Status column shows it.
[[nodiscard]] QString torrentStateName(TorrentState state);

/// Downloading, metadata, checking or moving: the torrent is doing something.
[[nodiscard]] bool torrentStateActive(TorrentState state);

/// Localized name of a libtorrent file priority 0..7.
[[nodiscard]] QString torrentFilePriorityName(int priority);

/// What TorrentProgressDelegate reads off the Progress column.
enum TorrentBarRoles : int {
    kTorrentBarRole = Qt::UserRole + 0x100,  ///< QByteArray pieceMap, 0..255 per bucket
    kTorrentBarPercentRole,                  ///< 0-100, the thin strip on top
    kTorrentBarPausedRole,                   ///< muted palette
    kTorrentBarCompleteRole,                 ///< solid green
    kTorrentBarSourcesRole,                  ///< peers + seeds connected; 0 = red missing parts
};

struct TorrentRow {
    QString id;          ///< "bt:v1:<40 hex>" / "bt:v2:<64 hex>"
    QString name;
    QString savePath;
    TorrentState state = TorrentState::Queued;
    qint64 size = 0;     ///< wanted bytes
    qint64 done = 0;
    double progress = 0; ///< 0..1
    qint64 rateDown = 0;
    qint64 rateUp = 0;
    int peers = 0;
    int seeds = 0;
    int peersTotal = -1; ///< scrape, -1 = unknown
    int seedsTotal = -1;
    qint64 downloaded = 0;
    qint64 uploaded = 0;
    qint64 failedBytes = 0;
    QString error;
    QString hashV1;      ///< lower-case hex, may be empty
    QString hashV2;
    bool paused = false;
    bool hasMetadata = false;
    bool isPrivate = false;
    qint64 addedAt = 0;      ///< Unix seconds
    qint64 completedAt = 0;
    qint64 seedingSeconds = 0;
    int queuePosition = -1;
    QByteArray pieceMap;
    int pieces = 0;
    int pieceLength = 0;
    int numFiles = 0;
    qint64 eta = -1;     ///< seconds, -1 = unknown
    QString comment;
    QString creator;
    qint64 createdAt = 0;     ///< Unix seconds, 0 = unknown
    qint64 lastActivity = 0;  ///< Unix seconds, 0 = never
    qint64 activeSeconds = 0;
    qint64 haveBytes = 0;     ///< verified, of the whole torrent
    qint64 totalSize = 0;     ///< the whole torrent, wanted or not
    double availability = -1; ///< copies in the swarm, -1 = unknown

    /// uploaded / downloaded; -1 when nothing was downloaded yet.
    [[nodiscard]] double ratio() const;
    /// Finished or Seeding.
    [[nodiscard]] bool isComplete() const;
};

/// Decode one GetTorrents / PushTorrentUpdate row.
[[nodiscard]] TorrentRow torrentRowFromCbor(const QCborMap& m);

/// One file of a torrent, as a child row. Pad files are left out.
struct TorrentFileRow {
    int index = -1;      ///< libtorrent file index
    QString path;        ///< inside the torrent, '/' separated
    qint64 size = 0;
    qint64 done = 0;
    int priority = 4;    ///< 0 skip .. 7 maximum
};

class TorrentListModel : public QAbstractItemModel {
    Q_OBJECT

public:
    enum Column {
        ColName = 0,
        ColSize,
        ColDone,
        ColProgress,
        ColStatus,
        ColDownSpeed,
        ColUpSpeed,
        ColSeeds,
        ColPeers,
        ColRatio,
        ColEta,
        ColAdded,
        ColCount
    };

    using QAbstractItemModel::QAbstractItemModel;

    [[nodiscard]] QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    [[nodiscard]] QModelIndex parent(const QModelIndex& child) const override;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] bool hasChildren(const QModelIndex& parent = {}) const override;
    [[nodiscard]] bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

    /// Merge a full snapshot: drop departed ids, update survivors, append new ones.
    void setRows(const QList<TorrentRow>& rows);
    /// One pushed row: update in place or append.
    void upsertRow(const TorrentRow& row);
    void removeRow(const QString& id);
    void clear();

    [[nodiscard]] const TorrentRow* findById(const QString& id) const;
    [[nodiscard]] const TorrentRow* rowAt(int row) const;
    [[nodiscard]] QString idAt(int row) const;
    [[nodiscard]] int rowOf(const QString& id) const;
    [[nodiscard]] int count() const { return static_cast<int>(m_rows.size()); }

    /// Torrent id of @p index, a torrent or one of its files.
    [[nodiscard]] QString idOf(const QModelIndex& index) const;
    /// The file of a child @p index, else nullptr.
    [[nodiscard]] const TorrentFileRow* fileAt(const QModelIndex& index) const;
    /// Whether the files of @p id were loaded (expanded once).
    [[nodiscard]] bool hasFiles(const QString& id) const;

    /// The `files` array of a GetTorrentDetails reply: replace @p id's children.
    void setFiles(const QString& id, const QCborArray& files);
    /// Priorities of every file of @p id by file index (pad files too), for
    /// SetTorrentFilePriorities. Empty before setFiles().
    [[nodiscard]] QList<int> priorities(const QString& id) const;

signals:
    /// A torrent was expanded and its files are not loaded yet.
    void filesWanted(const QString& id);

private:
    struct FileSet {
        std::vector<TorrentFileRow> files;   ///< child rows, no pad files
        QList<int> priorities;               ///< by file index, pad files too
    };

    [[nodiscard]] int rowOfKey(quintptr key) const;
    [[nodiscard]] const TorrentRow* parentRowOf(const QModelIndex& child) const;
    [[nodiscard]] QVariant fileData(const TorrentRow& r, const TorrentFileRow& f,
                                    int column, int role) const;

    [[nodiscard]] QVariant displayData(const TorrentRow& r, int column) const;
    [[nodiscard]] QVariant sortData(const TorrentRow& r, int column) const;
    [[nodiscard]] QVariant barData(const TorrentRow& r, int role) const;
    [[nodiscard]] QString statusText(const TorrentRow& r) const;

    std::vector<TorrentRow> m_rows;
    std::vector<quintptr> m_keys;            ///< parallel to m_rows
    QHash<quintptr, FileSet> m_files;        ///< by key, loaded torrents only
    quintptr m_nextKey = 1;
};

} // namespace eMule
