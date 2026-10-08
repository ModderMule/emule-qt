#include "pch.h"
/// @file DownloadProgressDelegate.cpp
/// @brief Download bar delegate, MFC CPartFile/CUpDownClient::DrawStatusBar.

#include "controls/DownloadProgressDelegate.h"
#include "controls/BarShader.h"
#include "controls/DownloadListModel.h"
#include "controls/PartBarPainter.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"

#include <QPainter>

#include <algorithm>

namespace eMule {

namespace {

/// Source-frequency blue of a gap; the muted twin for a paused file (MFC PartFile.cpp).
QColor gapColor(uint16_t freq, bool paused)
{
    if (freq == 0)
        return paused ? QColor(191, 64, 64) : QColor(255, 0, 0);
    const int f = freq - 1;
    return paused ? QColor(64, std::max(64, 169 - 11 * f), 191)
                  : QColor(0, std::max(0, 210 - 22 * f), 255);
}

/// Map a part status byte to a color — the per-part fallback for file rows.
QColor partColor(uint8_t status, bool paused, const QColor& have)
{
    switch (status) {
    case 0:   return have;
    case 255: return paused ? QColor(191, 168, 64) : QColor(255, 208, 0);
    default:  return gapColor(static_cast<uint16_t>(status - 1), paused);
    }
}

} // anonymous namespace

void paintDownloadBar(QPainter& painter, const QRect& rect, const DownloadBarData& bar,
                      double percent, bool paused, bool partDetail)
{
    const bool flat = useFlatBar();
    const uint64_t fileSize = static_cast<uint64_t>(std::max<int64_t>(bar.fileSize, 1));

    // MFC PartFile.cpp DrawStatusBar palette, flat or round
    const QColor progress = flat ? QColor(0, 150, 0) : QColor(0, 224, 0);
    const QColor have = paused ? (flat ? QColor(64, 64, 64) : QColor(116, 116, 116))
                               : (flat ? QColor(0, 0, 0) : QColor(104, 104, 104));
    const QColor pending = paused ? QColor(191, 168, 64) : QColor(255, 208, 0);

    BarShader shader(fileSize);
    shader.fill(have);

    if (percent >= 100.0) {
        shader.fill(progress);
        shader.draw(painter, rect);
        return;
    }

    // MorphXT: what is already in an unfinished part shows through the gaps painted below
    if (partDetail) {
        const QColor started = paused ? QColor(140, 140, 140) : QColor(160, 160, 160);
        for (qsizetype p = 0; p < bar.partMap.size(); ++p) {
            const auto status = static_cast<uint8_t>(bar.partMap.at(p));
            if (status == 0)
                continue;
            const uint64_t start = uint64_t(p) * PARTSIZE;
            shader.fillRange(start, start + PARTSIZE, status == 255 ? QColor(0, 224, 0) : started);
        }
    }

    // Gaps, split at part boundaries so each piece takes its part's frequency
    for (qsizetype i = 0; i + 1 < bar.gaps.size(); i += 2) {
        uint64_t start = static_cast<uint64_t>(bar.gaps.at(i));
        const uint64_t end = static_cast<uint64_t>(bar.gaps.at(i + 1));
        while (start <= end) {
            const uint64_t part = start / PARTSIZE;
            const uint64_t pieceEnd = std::min(end, (part + 1) * PARTSIZE - 1);
            const uint16_t freq = part < uint64_t(bar.partFreq.size()) ? bar.partFreq.at(qsizetype(part)) : 0;
            shader.fillRange(start, pieceEnd + 1, gapColor(freq, paused));
            start = pieceEnd + 1;
        }
    }
    for (qsizetype i = 0; i + 1 < bar.pending.size(); i += 2)
        shader.fillRange(static_cast<uint64_t>(bar.pending.at(i)),
                         static_cast<uint64_t>(bar.pending.at(i + 1)) + 1, pending);

    shader.draw(painter, rect);
    paintProgressStrip(painter, rect, percent);
    if (partDetail)
        paintChunkDots(painter, rect, fileSize);
}

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

    const bool flat = useFlatBar();
    if (isSourceRow) {
        // Parts sit at their real offsets when the parent file's size is known
        const QModelIndex fileIndex = index.parent().siblingAtColumn(index.column());
        const auto fileBar = fileIndex.data(DownloadListModel::BarDataRole).value<DownloadBarData>();
        if (partMap.isEmpty()) {
            BarShader::fillBarRect(*painter, barRect, sourcePartColor(0, flat), flat, barDepth3D());
        } else {
            paintPartBar(*painter, barRect, partMap,
                         [flat](uint8_t status) { return sourcePartColor(status, flat); },
                         static_cast<uint64_t>(std::max<int64_t>(fileBar.fileSize, 0)));
        }
    } else if (const QVariant barData = index.data(DownloadListModel::BarDataRole); barData.isValid()) {
        paintDownloadBar(*painter, barRect, barData.value<DownloadBarData>(), percent, paused,
                         thePrefs.showPartProgressDetail());
    } else if (percent >= 100.0) {
        BarShader::fillBarRect(*painter, barRect, flat ? QColor(0, 150, 0) : QColor(0, 224, 0),
                               flat, barDepth3D());
    } else {
        // No byte ranges from the daemon: one slice per part
        const QColor have = paused ? (flat ? QColor(64, 64, 64) : QColor(116, 116, 116))
                                   : (flat ? QColor(0, 0, 0) : QColor(104, 104, 104));
        if (partMap.isEmpty()) {
            BarShader::fillBarRect(*painter, barRect, have, flat, barDepth3D());
        } else {
            paintPartBar(*painter, barRect, partMap, [paused, have](uint8_t status) {
                return partColor(status, paused, have);
            });
        }
        paintProgressStrip(*painter, barRect, percent);
    }

    // Percentage text — file rows only, as MFC's DrawFileItem
    if (!isSourceRow && thePrefs.showDwlPercentage())
        paintPercentText(*painter, barRect, opt.font, index.data(Qt::DisplayRole).toString());

    painter->restore();
}

void SourcePartsDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);

    painter->save();
    painter->fillRect(opt.rect, (opt.state & QStyle::State_Selected) ? opt.palette.highlight()
                                                                     : opt.palette.base());

    const QRect barRect = opt.rect.adjusted(2, 2, -2, -2);
    if (barRect.width() > 0 && barRect.height() > 0) {
        const bool flat = useFlatBar();
        const QByteArray partMap = index.data(m_partMapRole).toByteArray();
        if (partMap.isEmpty()) {
            BarShader::fillBarRect(*painter, barRect, sourcePartColor(0, flat), flat, barDepth3D());
        } else {
            paintPartBar(*painter, barRect, partMap,
                         [flat](uint8_t status) { return sourcePartColor(status, flat); },
                         static_cast<uint64_t>(std::max<qint64>(index.data(m_fileSizeRole).toLongLong(), 0)));
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
