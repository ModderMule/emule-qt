#pragma once

/// @file UploadStatusDelegate.h
/// @brief The Obtained Parts bar of the Uploading and On Queue lists, MFC DrawUpStatusBar.

#include <QStyledItemDelegate>

class QPainter;
class QRect;

namespace eMule {

struct UpStatusBar;

/// Paint @p bar into @p rect as MFC CUpDownClient::DrawUpStatusBar does
/// (srchybrid/UploadClient.cpp:53-114). Nothing for a zero file size.
void paintUpStatusBar(QPainter& painter, const QRect& rect, const UpStatusBar& bar);

class UploadStatusDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    /// @p onlyWithParts: skip a peer that sent no part status, as the On Queue list does
    /// (QueueListCtrl.cpp:169); the Uploading list always draws.
    explicit UploadStatusDelegate(bool onlyWithParts, QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;

private:
    bool m_onlyWithParts;
};

} // namespace eMule
