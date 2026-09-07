#pragma once

/// @file CategoryDialog.h
/// @brief Add/edit one download category — MFC's CCatDialog (IDD_CAT).
///
/// Field order and labels follow the resource verbatim
/// (`srchybrid/emule.rc:612-635`): Title, Comment, Incoming Dir + browse,
/// Priority, Colour, Auto-cat + "As Regular Expression", and the view-filter
/// regular expression.
///
/// The dialog owns a copy, not a live pointer. MFC edits `catArr` in place and
/// only *then* decides whether the user pressed OK, so a Cancel after a failed
/// path validation can leave the category half-edited
/// (`srchybrid/CatDialog.cpp:45-52, 168`). Here Cancel changes nothing, and the
/// caller reads `category()` after `exec()` returns `Accepted` — the same
/// contract AddFriendDialog uses.

#include "prefs/DownloadCategory.h"

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace eMule {

class CategoryDialog : public QDialog {
    Q_OBJECT

public:
    /// @param defaultIncomingDir  what an empty path means — shown greyed as the
    ///        placeholder so the user can see where files would go without
    ///        having to type it, and so clearing the field is an obvious way
    ///        back to "wherever the global incoming dir is".
    explicit CategoryDialog(const DownloadCategory& category,
                            const QString& defaultIncomingDir, QWidget* parent = nullptr);

    /// The edited category. Only meaningful after exec() returned Accepted.
    [[nodiscard]] DownloadCategory category() const { return m_category; }

private slots:
    void onBrowseClicked();
    void onColorClicked();
    void onAccept();

private:
    /// Report a rejected field without a modal box: the message goes under the
    /// form and focus moves to the field. MFC's ErrorBalloon, in Qt terms.
    void showFieldError(QWidget* field, const QString& text);
    void updateColorButton();

    DownloadCategory m_category;
    QString m_defaultIncomingDir;

    /// kCategoryColorAuto until the user picks one — kept apart from the
    /// button's palette so "Automatic" survives a repaint.
    quint32 m_color = kCategoryColorAuto;

    QLineEdit*   m_titleEdit = nullptr;
    QLineEdit*   m_commentEdit = nullptr;
    QLineEdit*   m_incomingEdit = nullptr;
    QComboBox*   m_prioCombo = nullptr;
    QPushButton* m_colorButton = nullptr;
    QLineEdit*   m_autocatEdit = nullptr;
    QCheckBox*   m_autocatRegexpCheck = nullptr;
    QLineEdit*   m_regexpEdit = nullptr;
    QLabel*      m_errorLabel = nullptr;
};

} // namespace eMule
