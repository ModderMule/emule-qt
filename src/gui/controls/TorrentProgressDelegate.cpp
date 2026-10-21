#include "pch.h"
/// @file TorrentProgressDelegate.cpp
/// @brief The Torrents Progress column as a piece map — implementation.

#include "controls/TorrentProgressDelegate.h"

#include "controls/BarShader.h"
#include "controls/PartBarPainter.h"
#include "controls/TorrentListModel.h"
#include "prefs/Preferences.h"

#include <QPainter>

namespace eMule {

namespace {

QColor blend(const QColor& from, const QColor& to, int share)
{
    auto mix = [share](int a, int b) { return a + (b - a) * share / 255; };
    return {mix(from.red(), to.red()), mix(from.green(), to.green()), mix(from.blue(), to.blue())};
}

} // namespace

QColor TorrentProgressDelegate::bucketColor(quint8 share, bool hasSources, bool paused)
{
    // Same family as DownloadProgressDelegate: grey have, blue obtainable, red not
    const bool flat = useFlatBar();
    const QColor have = paused ? (flat ? QColor(64, 64, 64) : QColor(116, 116, 116))
                               : (flat ? QColor(0, 0, 0) : QColor(104, 104, 104));
    const QColor missing = hasSources ? (paused ? QColor(64, 150, 191) : QColor(0, 150, 255))
                                      : (paused ? QColor(191, 64, 64) : QColor(255, 0, 0));
    if (share == 0)
        return missing;
    if (share == 255)
        return have;
    return blend(missing, have, share);
}

void TorrentProgressDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
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

    if (index.data(kTorrentBarCompleteRole).toBool()) {
        const bool flat = useFlatBar();
        BarShader::fillBarRect(*painter, bar, flat ? QColor(0, 150, 0) : QColor(0, 224, 0), flat,
                               barDepth3D());
    } else {
        const QByteArray map = index.data(kTorrentBarRole).toByteArray();
        const bool paused = index.data(kTorrentBarPausedRole).toBool();
        const bool sources = index.data(kTorrentBarSourcesRole).toInt() > 0;
        if (map.isEmpty()) {
            // no metadata yet: one empty bucket
            BarShader::fillBarRect(*painter, bar, bucketColor(0, sources, paused), useFlatBar(),
                                   barDepth3D());
        } else {
            paintPartBar(*painter, bar, map, [sources, paused](quint8 share) {
                return bucketColor(share, sources, paused);
            });
        }
        paintProgressStrip(*painter, bar, index.data(kTorrentBarPercentRole).toDouble());
    }

    if (thePrefs.showDwlPercentage())
        paintPercentText(*painter, bar, opt.font, index.data(Qt::DisplayRole).toString());

    painter->restore();
}

QSize TorrentProgressDelegate::sizeHint(const QStyleOptionViewItem& option,
                                        const QModelIndex& index) const
{
    Q_UNUSED(index);
    return {option.rect.width(), std::max(option.fontMetrics.height() + 4, 16)};
}

} // namespace eMule
