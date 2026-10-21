#pragma once

/// @file TorrentProgressDelegate.h
/// @brief The Torrents Progress column as a piece map, in MFC's part-bar style.
///
/// Reads the kTorrentBar* roles of TorrentListModel. Each pieceMap bucket is a
/// 0..255 share of its pieces done: a full bucket is MFC's dark grey "have",
/// an empty one blue (peers connected) or red (none), shares in between blend
/// the two. A finished torrent is solid green, as MFC draws a completed file.

#include <QColor>
#include <QStyledItemDelegate>

namespace eMule {

class TorrentProgressDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;

    /// Colour of one bucket. Public for tests.
    [[nodiscard]] static QColor bucketColor(quint8 share, bool hasSources, bool paused);
};

} // namespace eMule
