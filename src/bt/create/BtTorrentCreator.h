#pragma once

/// @file BtTorrentCreator.h
/// @brief Makes a .torrent from a file, a folder or some files of one folder (#191).
///
/// Version 1 (SHA-1 pieces), version 2 (SHA-256 merkle trees) or hybrid; BitTorrent has no
/// other hash. BLAKE3 is ed3k's: a torrent seeded here gets it from its ed3k join
/// (Ed3kTorrentJoin::adoptCreated). Runs on a pool thread; signals arrive queued.
/// No libtorrent type in this header.

#include <QByteArray>
#include <QObject>
#include <QRunnable>
#include <QString>
#include <QStringList>

#include <atomic>
#include <expected>

namespace eMule::bt {

enum class BtTorrentFormat : int { V1 = 0, V2 = 1, Hybrid = 2 };

struct BtCreateParams {
    /// a file or a folder; '/' separators
    QString path;
    /// folder mode: only these names below `path` ('/' separators); empty = every file
    QStringList onlyFiles;
    BtTorrentFormat format = BtTorrentFormat::Hybrid;
    /// bytes, a power of two ≥ 16 KiB; 0 = libtorrent picks
    int pieceSize = 0;
    bool isPrivate = false;
    bool ignoreDotfiles = true;
    /// in tier order; an empty line starts the next tier (qBittorrent's rule)
    QStringList trackers;
    QStringList webSeeds;
    QString comment;
    /// info["source"]: some private trackers want it
    QString source;
    QString creator;
};

struct BtCreateResult {
    bool ok = false;
    bool cancelled = false;
    /// English reason when !ok
    QString error;
    /// the bencoded .torrent
    QByteArray data;
    /// torrent name (file or folder name)
    QString name;
    /// where the content lies for seeding: the parent of `path`
    QString savePath;
    int pieceSize = 0;
    int pieces = 0;
};

class BtTorrentCreator : public QObject, public QRunnable {
    Q_OBJECT

public:
    explicit BtTorrentCreator(BtCreateParams params, QObject* parent = nullptr);

    void run() override;
    /// stops at the next piece; finished() follows with cancelled
    void cancel() { m_cancel.store(true, std::memory_order_relaxed); }

    [[nodiscard]] const BtCreateParams& params() const { return m_params; }

    /// Pieces the torrent would have, without hashing. Error: an English reason.
    [[nodiscard]] static std::expected<int, QString> countPieces(const BtCreateParams& params);

signals:
    /// 0..100
    void progress(int percent);
    void finished(const eMule::bt::BtCreateResult& result);

private:
    BtCreateParams m_params;
    std::atomic_bool m_cancel{false};
};

} // namespace eMule::bt
