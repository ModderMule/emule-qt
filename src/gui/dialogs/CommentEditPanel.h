#pragma once

/// @file CommentEditPanel.h
/// @brief The editable Comments page — port of MFC CCommentDialog (IDD_COMMENT).
///
/// The original has two comment pages that share nothing but the comment list
/// control: CCommentDialogLst, which only reads (Downloads and Search), and this
/// one, which the shared-file details sheet hosts so the user can say something
/// about a file of their own (srchybrid/SharedFilesCtrl.cpp:131-135). That is the
/// shape reproduced here — the two group boxes, the rating combo and Reset sit on
/// top of CommentsPanel, and "Edit spam filter..." (which belongs to the read-only
/// template) is hidden.
///
/// eMule will not publish a comment for a file it is not sharing, so the whole
/// editor greys out when the daemon says `canComment` is false, exactly as
/// CCommentDialog::EnableDialog does (CommentDialog.cpp:117-124, 320-330).

#include "CommentsPanel.h"

class QComboBox;
class QLineEdit;

namespace eMule {

/// Comment list plus the controls to post one.
class CommentEditPanel : public CommentsPanel {
    Q_OBJECT

public:
    explicit CommentEditPanel(const QString& stateKey, QWidget* parent = nullptr);

    void setDetails(const QCborMap& details) override;

    /// Outcome of the round trip Apply started. On success the typed values become
    /// the new baseline; on failure everything stays dirty so the user can retry.
    void commentApplied(bool ok);

signals:
    /// Apply was pressed. The host dialog owns the exchange with the daemon.
    void postComment(const QString& fileHash, const QString& comment, int rating);

private:
    void buildEditor();
    void applyEdits();
    void resetEdits();
    void markDirty();
    void setEditingEnabled(bool enabled);

    QLineEdit*   m_commentEdit = nullptr;
    QComboBox*   m_ratingCombo = nullptr;
    QPushButton* m_resetBtn    = nullptr;
    QPushButton* m_applyBtn    = nullptr;

    QString m_baseComment;          ///< last values the daemon confirmed
    int     m_baseRating = 0;
    bool    m_dirty      = false;
    bool    m_filling    = false;   ///< MFC's m_bSelf: suppress change signals
};

} // namespace eMule
