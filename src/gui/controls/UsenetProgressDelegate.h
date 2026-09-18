#pragma once

/// @file UsenetProgressDelegate.h
/// @brief The Usenet Progress column as a segment map, in MFC's bar style.
///
/// Reads the kUsenetBar* roles from UsenetQueueModel.h, so the queue tree and the
/// release details dialog draw one bar from one set of values: dark grey for
/// articles in, blue for ones still to fetch, amber in flight, red missing on
/// every server, light grey for a file left out. A finished file or release is
/// solid green, as MFC draws a completed download.

#include <QStyledItemDelegate>

namespace eMule {

class UsenetProgressDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const override;
};

} // namespace eMule
