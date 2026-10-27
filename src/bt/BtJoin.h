#pragma once

/// @file BtJoin.h
/// @brief What an ed3k join (torrent.md §4, libs/packmule/docs/torrent-join.md) sees of a
/// torrent through BtSession: plain Qt values, no libtorrent type.

#include <QByteArray>
#include <QList>
#include <QString>

#include <algorithm>
#include <functional>

namespace eMule::bt {

/// The pieces and files of a torrent with metadata.
struct BtLayout {
    struct File {
        /// absolute path on disk
        QString path;
        qint64 size = 0;
        /// in the torrent's byte space
        qint64 offset = 0;
        /// a BEP 47 padding file: no member of a join
        bool pad = false;
    };
    int pieceLength = 0;
    int pieces = 0;
    qint64 total = 0;
    QList<File> files;

    /// the bytes of @p piece
    [[nodiscard]] int pieceSize(int piece) const
    {
        const qint64 start = qint64(piece) * pieceLength;
        return piece < 0 || start >= total ? 0 : int(std::min<qint64>(pieceLength, total - start));
    }
};

/// The join's side of a torrent's piece checks.
class BtJoinSink {
public:
    virtual ~BtJoinSink() = default;

    /// A piece of torrent @p id passed libtorrent's own hash; @p data is the piece as
    /// written. Called on libtorrent's network thread: hop to your own. `done(spoil)` is
    /// called once, from any thread; `spoil` makes libtorrent fail the piece and blame
    /// its peers.
    virtual void pieceHashed(const QString& id, int piece, QByteArray data, std::function<void(bool spoil)> done) = 0;
};

} // namespace eMule::bt
