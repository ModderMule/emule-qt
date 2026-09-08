#pragma once

/// @file CommentsPanel.h
/// @brief The Comments page shared by the file and search detail dialogs.
///
/// MFC hosts the very same page class (CCommentDialogLst) in CFileDetailDialog
/// and in the search-result sheet, so this is a widget rather than a tab baked
/// into one dialog. Layout follows srchybrid/CommentDialogLst.cpp and the
/// IDD_COMMENTLST template: the comment list on top, "Edit spam filter..."
/// bottom-left and "Search Kad" bottom-right.

#include <QCborMap>
#include <QWidget>

class QLabel;
class QPushButton;
class QTreeWidget;
class QVBoxLayout;

namespace eMule {

/// Comment/rating list for one file, with the two MFC action buttons.
class CommentsPanel : public QWidget {
    Q_OBJECT

public:
    /// Columns in MFC's order (srchybrid/CommentListCtrl.cpp:45-49).
    enum Column { ColRating = 0, ColComment, ColFileName, ColUserName, ColOrigin, ColCount };

    /// @param stateKey  UiState key for the column layout — distinct per host
    ///                  dialog so the two do not fight over one saved width set.
    explicit CommentsPanel(const QString& stateKey, QWidget* parent = nullptr);

    /// Fill from a details map (`comments[]`, `fileName`, `hash`,
    /// `notesSearchRunning`). Safe to call repeatedly as the walker moves.
    /// Virtual so the editable page can refill its own fields from the same map.
    virtual void setDetails(const QCborMap& details);

    /// Show the "(Kad search in progress...)" state, as MFC does while a NOTES
    /// lookup for this file is in flight (CommentDialogLst.cpp:154-163).
    void setKadSearchRunning(bool running);

signals:
    /// The user pressed "Search Kad" for the file currently on display.
    void searchKadNotes(const QString& fileHash, const QString& fileName);

    /// The user changed the comment spam filter to @p filter (a `|`-separated
    /// list); the host dialog owns the round trip to the daemon.
    void commentFilterChanged(const QString& filter);

protected:
    // For CommentEditPanel, which builds MFC's IDD_COMMENT by adding the editor
    // controls around the pieces this page already lays out.
    [[nodiscard]] QVBoxLayout* panelLayout() const { return m_layout; }
    [[nodiscard]] QTreeWidget* commentTree() const { return m_tree; }
    [[nodiscard]] QLabel*      emptyLabel() const { return m_emptyLabel; }
    [[nodiscard]] QPushButton* filterButton() const { return m_filterBtn; }
    [[nodiscard]] const QString& fileHash() const { return m_fileHash; }

private:
    void buildUi(const QString& stateKey);
    void editSpamFilter();
    void copySelectedComments() const;

    QVBoxLayout*  m_layout      = nullptr;
    QTreeWidget*  m_tree        = nullptr;
    QLabel*       m_emptyLabel  = nullptr;
    QPushButton*  m_searchKadBtn = nullptr;
    QPushButton*  m_filterBtn   = nullptr;

    QString m_fileHash;
    QString m_fileName;
};

} // namespace eMule
