#include "pch.h"
/// @file UploadStatusDelegate.cpp
/// @brief The Obtained Parts bar, MFC CUpDownClient::DrawUpStatusBar.

#include "controls/UploadStatusDelegate.h"
#include "controls/ClientListModel.h"

#include "utils/Opcodes.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace eMule {

void paintUpStatusBar(QPainter& painter, const QRect& rect, const UpStatusBar& bar)
{
    const int64_t size = bar.fileSize;
    if (size <= 0 || rect.width() <= 0 || rect.height() <= 0)
        return;

    // MFC UploadClient.cpp:57-70, flat colours like the other part bars; greyed for a
    // slot past the active upload count.
    const QColor neither     = bar.greyed ? QColor(248, 248, 248) : QColor(224, 224, 224);
    const QColor nextSending = bar.greyed ? QColor(255, 244, 191) : QColor(255, 208, 0);
    const QColor both        = bar.greyed ? QColor(191, 191, 191) : QColor(0, 0, 0);
    const QColor sending     = bar.greyed ? QColor(191, 229, 191) : QColor(0, 150, 0);

    const double scale = static_cast<double>(rect.width()) / static_cast<double>(size);
    const auto fill = [&](int64_t start, int64_t end, const QColor& color) {
        start = std::clamp<int64_t>(start, 0, size);
        end = std::clamp<int64_t>(end, 0, size);
        if (end <= start)
            return;
        const int x0 = rect.left() + static_cast<int>(std::floor(static_cast<double>(start) * scale));
        const int x1 = rect.left() + static_cast<int>(std::ceil(static_cast<double>(end) * scale));
        painter.fillRect(x0, rect.top(), std::max(1, x1 - x0), rect.height(), color);
    };
    const auto part = static_cast<int64_t>(PARTSIZE);

    fill(0, size, neither);
    for (qsizetype i = 0; i < bar.parts.size(); ++i) {
        if (bar.parts[i])
            fill(i * part, (i + 1) * part, both);
    }
    for (const int next : bar.nextParts)
        fill(next * part, (next + 1) * part, nextSending);
    for (const auto& [start, end] : bar.sentRanges)
        fill(start, end + 1, sending);
}

UploadStatusDelegate::UploadStatusDelegate(bool onlyWithParts, QObject* parent)
    : QStyledItemDelegate(parent)
    , m_onlyWithParts(onlyWithParts)
{
}

void UploadStatusDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                 const QModelIndex& index) const
{
    // Row background and selection as for any cell; the cell itself has no text
    QStyledItemDelegate::paint(painter, option, index);

    const auto bar = index.data(ClientListModel::UpStatusRole).value<UpStatusBar>();
    if (m_onlyWithParts && bar.parts.isEmpty())
        return;

    painter->save();
    paintUpStatusBar(*painter, option.rect.adjusted(1, 1, -1, -1), bar);
    painter->restore();
}

} // namespace eMule
