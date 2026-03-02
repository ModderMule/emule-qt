/// @file DownloadProgressDelegate.cpp
/// @brief Colored progress bar delegate — implementation.

#include "controls/DownloadProgressDelegate.h"

#include <QPainter>

namespace eMule {

void DownloadProgressDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const
{
    // Draw selection / highlight background
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    painter->save();

    // Selection background
    if (opt.state & QStyle::State_Selected) {
        painter->fillRect(opt.rect, opt.palette.highlight());
    } else {
        painter->fillRect(opt.rect, opt.palette.base());
    }

    // Get percent from UserRole (sorting data)
    const double percent = index.data(Qt::UserRole).toDouble();
    const QRect barRect = opt.rect.adjusted(2, 2, -2, -2);

    if (barRect.width() <= 0 || barRect.height() <= 0) {
        painter->restore();
        return;
    }

    // Dark gray background for the entire bar
    painter->fillRect(barRect, QColor(0x60, 0x60, 0x60));

    // Green filled portion (matching MFC green progress style)
    if (percent > 0.0) {
        const int filledWidth = static_cast<int>(barRect.width() * percent / 100.0);
        QRect filledRect = barRect;
        filledRect.setWidth(std::min(filledWidth, barRect.width()));
        painter->fillRect(filledRect, QColor(0x00, 0xA0, 0x00));
    }

    // Draw 1px border around the bar
    painter->setPen(QColor(0x40, 0x40, 0x40));
    painter->drawRect(barRect);

    // Centered percentage text
    const QString text = QStringLiteral("%1%").arg(percent, 0, 'f', 1);
    painter->setPen(Qt::white);
    QFont font = opt.font;
    font.setPixelSize(std::max(barRect.height() - 4, 9));
    painter->setFont(font);
    painter->drawText(barRect, Qt::AlignCenter, text);

    painter->restore();
}

QSize DownloadProgressDelegate::sizeHint(const QStyleOptionViewItem& option,
                                          const QModelIndex& index) const
{
    Q_UNUSED(index);
    return {option.rect.width(), option.fontMetrics.height() + 4};
}

} // namespace eMule
