#pragma once

/// @file DownloadProgressDelegate.h
/// @brief Custom delegate that paints colored progress bars in the download list.
///
/// Matches MFC eMule's CPartFile::DrawStatusBar: byte-exact gaps and pending
/// blocks, the 3 px completion strip, flat or round per the Display page, plus
/// the centered percentage when showDwlPercentage is on.

#include <QStyledItemDelegate>

class QPainter;
class QRect;

namespace eMule {

struct DownloadBarData;

/// Paint a file row's bar into @p rect. @p partDetail adds MorphXT's detail:
/// downloaded data of unfinished parts in its own colour, and part boundary dots.
void paintDownloadBar(QPainter& painter, const QRect& rect, const DownloadBarData& bar,
                      double percent, bool paused, bool partDetail);

class DownloadProgressDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    [[nodiscard]] QSize sizeHint(const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const override;
};

/// A client's part bar for the file we ask it for (MFC CUpDownClient::DrawStatusBar),
/// as the Downloading list's Available Parts column draws it.
class SourcePartsDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    SourcePartsDelegate(int partMapRole, int fileSizeRole, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_partMapRole(partMapRole), m_fileSizeRole(fileSizeRole) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

private:
    int m_partMapRole;
    int m_fileSizeRole;
};

} // namespace eMule
