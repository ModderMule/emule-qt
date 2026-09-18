#include "pch.h"
/// @file SharedPartsDelegate.cpp
/// @brief Per-part availability bar delegate matching MFC eMule DrawShareStatusBar().

#include "controls/SharedPartsDelegate.h"
#include "controls/PartBarPainter.h"
#include "controls/SharedFilesModel.h"

#include <QPainter>


namespace eMule {

namespace {

/// Map a share-part-map byte to a color matching MFC eMule's DrawShareStatusBar().
///
///   0   → (104,104,104) dark grey  — no availability data
///   1   → (255,0,0) red            — part complete, frequency = 0
///   2..253 → cyan→blue gradient    — part complete, frequency = byte − 1
///   254 → (0,0,255) saturated blue — part complete, high frequency
///   255 → (224,224,224) light grey  — part not complete (PartFile gap)
QColor sharePartColor(uint8_t b)
{
    switch (b) {
    case 0:   return {104, 104, 104};
    case 1:   return {255, 0, 0};
    case 254: return {0, 0, 255};
    case 255: return {224, 224, 224};
    default: {
        const int freq = b - 1;
        const int g = std::max(0, 210 - 22 * (freq - 1));
        return {0, g, 255};
    }
    }
}

} // anonymous namespace

void SharedPartsDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    painter->save();

    // Selection / alternating background
    if (opt.state & QStyle::State_Selected)
        painter->fillRect(opt.rect, opt.palette.highlight());
    else
        painter->fillRect(opt.rect, opt.palette.base());

    const QByteArray partMap = index.data(SharedFilesModel::SharePartMapRole).toByteArray();
    const QRect barRect = opt.rect.adjusted(2, 2, -2, -2);

    if (barRect.width() <= 0 || barRect.height() <= 0) {
        painter->restore();
        return;
    }

    if (partMap.isEmpty()) {
        // No availability data — solid dark grey bar
        painter->fillRect(barRect, QColor(104, 104, 104));
    } else {
        paintPartBar(*painter, barRect, partMap, sharePartColor);
    }

    painter->restore();
}

QSize SharedPartsDelegate::sizeHint(const QStyleOptionViewItem& option,
                                     const QModelIndex& index) const
{
    Q_UNUSED(index);
    return {option.rect.width(), std::max(option.fontMetrics.height() + 4, 16)};
}

} // namespace eMule
