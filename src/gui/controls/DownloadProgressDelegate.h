#pragma once

/// @file DownloadProgressDelegate.h
/// @brief Custom delegate that paints colored progress bars in the download list.
///
/// Matches the MFC eMule progress bar style: green filled portion,
/// dark gray remainder, with centered percentage text overlay.

#include <QStyledItemDelegate>

namespace eMule {

class DownloadProgressDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const override;
};

} // namespace eMule
