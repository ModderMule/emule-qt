#pragma once

/// @file SharedPartsDelegate.h
/// @brief Custom delegate that paints per-part availability bars in the shared files list.
///
/// Matches the MFC eMule DrawShareStatusBar() style with frequency-based coloring.

#include <QStyledItemDelegate>

namespace eMule {

class SharedPartsDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const override;
};

} // namespace eMule
