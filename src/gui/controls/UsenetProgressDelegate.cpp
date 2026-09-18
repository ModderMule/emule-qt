#include "pch.h"
/// @file UsenetProgressDelegate.cpp
/// @brief The Usenet Progress column as a segment map, in MFC's bar style.

#include "controls/UsenetProgressDelegate.h"

#include "controls/PartBarPainter.h"
#include "controls/UsenetQueueModel.h"

#include <QPainter>

namespace eMule {

namespace {

/// Same palette family as DownloadProgressDelegate's file rows: grey is what we
/// have, blue what is still obtainable, amber what is arriving, red what is not.
QColor barColorActive(quint8 code)
{
    switch (code) {
    case kUsenetBarDone:     return {104, 104, 104};
    case kUsenetBarMissing:  return {255, 0, 0};
    case kUsenetBarInFlight: return {255, 208, 0};
    case kUsenetBarSkipped:  return {224, 224, 224};
    default:                 return {0, 150, 255};   // queued
    }
}

/// The muted twin, as partColorPaused() is to partColorActive().
QColor barColorPaused(quint8 code)
{
    switch (code) {
    case kUsenetBarDone:     return {116, 116, 116};
    case kUsenetBarMissing:  return {191, 64, 64};
    case kUsenetBarInFlight: return {191, 168, 64};
    case kUsenetBarSkipped:  return {224, 224, 224};
    default:                 return {64, 150, 191};
    }
}

} // namespace

void UsenetProgressDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    painter->save();
    painter->fillRect(opt.rect, (opt.state & QStyle::State_Selected) ? opt.palette.highlight()
                                                                     : opt.palette.base());

    const QRect bar = opt.rect.adjusted(2, 2, -2, -2);
    if (bar.width() <= 0 || bar.height() <= 0) {
        painter->restore();
        return;
    }

    if (index.data(kUsenetBarCompleteRole).toBool()) {
        painter->fillRect(bar, QColor(0, 224, 0));
        painter->restore();
        return;
    }

    const QByteArray codes = index.data(kUsenetBarRole).toByteArray();
    if (codes.isEmpty()) {
        painter->fillRect(bar, barColorActive(kUsenetBarQueued));
    } else {
        const bool paused = index.data(kUsenetBarPausedRole).toBool();
        paintPartBar(*painter, bar, codes, paused ? barColorPaused : barColorActive);
    }
    paintProgressStrip(*painter, bar, index.data(kUsenetBarPercentRole).toDouble());

    painter->restore();
}

QSize UsenetProgressDelegate::sizeHint(const QStyleOptionViewItem& option,
                                       const QModelIndex& index) const
{
    Q_UNUSED(index);
    return {option.rect.width(), std::max(option.fontMetrics.height() + 4, 16)};
}

} // namespace eMule
