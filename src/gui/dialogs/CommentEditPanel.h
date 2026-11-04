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
///
/// On a multi-file sheet (#123) the editor covers every shared file of the
/// selection: text and rating show only where all files agree, and a field left
/// that way keeps each file's own value on Apply — MFC's m_bMergedComment.

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
    /// @param rating     -1 keeps each file's rating (files disagreed, left untouched)
    /// @param keepEmpty  an empty @p comment keeps each file's comment
    void postComment(const QStringList& fileHashes, const QString& comment, int rating,
                     bool keepEmpty);

private:
    void buildEditor();
    void applyEdits();
    void resetEdits();
    void markDirty();
    void setEditingEnabled(bool enabled);
    void showMergedHint();

    QLineEdit*   m_commentEdit = nullptr;
    QComboBox*   m_ratingCombo = nullptr;
    QPushButton* m_resetBtn    = nullptr;
    QPushButton* m_applyBtn    = nullptr;

    QStringList m_hashes;           ///< files Apply writes to
    QString m_baseComment;          ///< last values the daemon confirmed
    int     m_baseRating = 0;       ///< -1 = files disagree
    bool    m_merged     = false;   ///< files had different comments (MFC m_bMergedComment)
    bool    m_dirty      = false;
    bool    m_filling    = false;   ///< MFC's m_bSelf: suppress change signals
};

} // namespace eMule
