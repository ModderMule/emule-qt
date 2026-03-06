#include "pch.h"
/// @file DownloadProgressDelegate.cpp
/// @brief Per-part chunk map progress bar delegate matching MFC eMule style.

#include "controls/DownloadProgressDelegate.h"
#include "controls/DownloadListModel.h"

#include <QPainter>

#include <algorithm>

namespace eMule {

namespace {

/// Map a part status byte to a color (active palette) — download (file) rows.
QColor partColorActive(uint8_t status)
{
    switch (status) {
    case 0:   return {104, 104, 104};       // complete — dark grey
    case 1:   return {255, 0, 0};           // gap, no sources — red
    case 255: return {255, 208, 0};         // downloading — amber/yellow
    default: {
        // gap with sources — blue gradient: freq = status - 1
        int freq = status - 1;
        int g = std::max(0, 210 - 22 * freq);
        return {0, g, 255};
    }
    }
}

/// Map a part status byte to a color (paused/stopped muted palette) — download rows.
QColor partColorPaused(uint8_t status)
{
    switch (status) {
    case 0:   return {116, 116, 116};
    case 1:   return {191, 64, 64};
    case 255: return {191, 168, 64};
    default: {
        int freq = status - 1;
        int g = std::max(64, 169 - 11 * freq);
        return {64, g, 191};
    }
    }
}

/// Map a source part status byte to a color — source (client) rows.
QColor sourcePartColor(uint8_t status)
{
    switch (status) {
    case 0:  return {224, 224, 224};  // client doesn't have — light grey
    case 1:  return {0, 0, 0};       // both have — black
    case 2:  return {0, 100, 255};   // client has, we need — blue
    case 3:  return {255, 208, 0};   // pending block queued — amber
    case 4:  return {0, 150, 0};     // currently receiving — dark green
    default: return {224, 224, 224};
    }
}

} // anonymous namespace

void DownloadProgressDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    painter->save();

    // Selection background
    if (opt.state & QStyle::State_Selected)
        painter->fillRect(opt.rect, opt.palette.highlight());
    else
        painter->fillRect(opt.rect, opt.palette.base());

    const double percent = index.data(Qt::UserRole).toDouble();
    const QByteArray partMap = index.data(DownloadListModel::PartMapRole).toByteArray();
    const bool paused = index.data(DownloadListModel::PausedRole).toBool();
    const bool isSourceRow = index.parent().isValid();
    const QRect barRect = opt.rect.adjusted(2, 2, -2, -2);

    if (barRect.width() <= 0 || barRect.height() <= 0) {
        painter->restore();
        return;
    }

    // Layer 1: Part map (full bar height)
    if (!partMap.isEmpty()) {
        const int partCount = partMap.size();
        const double partWidth = static_cast<double>(barRect.width()) / partCount;

        for (int i = 0; i < partCount; ++i) {
            const auto status = static_cast<uint8_t>(partMap[i]);
            const QColor color = isSourceRow ? sourcePartColor(status)
                               : paused     ? partColorPaused(status)
                                            : partColorActive(status);

            int x0 = barRect.left() + static_cast<int>(i * partWidth);
            int x1 = barRect.left() + static_cast<int>((i + 1) * partWidth);
            if (i == partCount - 1)
                x1 = barRect.right() + 1;  // fill to edge

            painter->fillRect(x0, barRect.top(), x1 - x0, barRect.height(), color);
        }
    } else if (!isSourceRow) {
        // Fallback: simple bar when no part map (e.g. completed files)
        painter->fillRect(barRect, QColor(104, 104, 104));
        if (percent > 0.0) {
            int filledWidth = static_cast<int>(barRect.width() * percent / 100.0);
            QRect filledRect(barRect.left(), barRect.top(),
                             std::min(filledWidth, barRect.width()), barRect.height());
            painter->fillRect(filledRect, QColor(0, 224, 0));
        }
    } else {
        // Source row with no part map — light grey placeholder
        painter->fillRect(barRect, QColor(224, 224, 224));
    }

    // Layer 2: Thin green progress overlay (top 3px) — file rows only
    if (!isSourceRow) {
        constexpr int progressHeight = 3;
        if (barRect.height() > progressHeight) {
            QRect greyBar(barRect.left(), barRect.top(), barRect.width(), progressHeight);
            painter->fillRect(greyBar, QColor(224, 224, 224));

            if (percent > 0.0) {
                int greenWidth = static_cast<int>(barRect.width() * percent / 100.0);
                QRect greenBar(barRect.left(), barRect.top(),
                               std::min(greenWidth, barRect.width()), progressHeight);
                painter->fillRect(greenBar, QColor(0, 224, 0));
            }
        }
    }

    painter->restore();
}

QSize DownloadProgressDelegate::sizeHint(const QStyleOptionViewItem& option,
                                          const QModelIndex& index) const
{
    Q_UNUSED(index);
    return {option.rect.width(), std::max(option.fontMetrics.height() + 4, 16)};
}

} // namespace eMule
