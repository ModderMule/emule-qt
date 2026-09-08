#include "pch.h"
/// @file CommentEditPanel.cpp
/// @brief The editable Comments page — see CommentEditPanel.h.

#include "CommentEditPanel.h"

#include "utils/Opcodes.h"
#include "utils/RatingIcons.h"

#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>

namespace eMule {

// ── construction ───────────────────────────────────────────────────────

CommentEditPanel::CommentEditPanel(const QString& stateKey, QWidget* parent)
    : CommentsPanel(stateKey, parent)
{
    // IDD_COMMENT has no spam-filter button; that one belongs to IDD_COMMENTLST.
    filterButton()->hide();
    buildEditor();
}

// ── public API ─────────────────────────────────────────────────────────

void CommentEditPanel::setDetails(const QCborMap& details)
{
    CommentsPanel::setDetails(details);

    // A late Kad-notes refresh lands here too, and must not overwrite what the user
    // is halfway through typing — MFC reloads its fields only on m_bDataChanged
    // (CommentDialog.cpp:110-148).
    if (!m_dirty) {
        m_baseComment = details.value(QLatin1StringView("myComment")).toString();
        m_baseRating  = static_cast<int>(details.value(QLatin1StringView("myRating")).toInteger());

        m_filling = true;
        m_commentEdit->setText(m_baseComment);
        m_ratingCombo->setCurrentIndex(qBound(0, m_baseRating, 5));
        m_filling = false;

        m_applyBtn->setEnabled(false);
    }

    setEditingEnabled(details.value(QLatin1StringView("canComment")).toBool());
}

void CommentEditPanel::commentApplied(bool ok)
{
    if (!ok)
        return;   // leave it dirty; checkOrWarn() already said what went wrong

    m_baseComment = m_commentEdit->text();
    m_baseRating  = m_ratingCombo->currentIndex();
    m_dirty       = false;
    m_applyBtn->setEnabled(false);
}

// ── private helpers ────────────────────────────────────────────────────

void CommentEditPanel::buildEditor()
{
    // 1. The comment box (IDC_CMT_LQUEST / IDC_CMT_LAIDE / IDC_CMT_TEXT).
    auto* commentGroup = new QGroupBox(
        tr("Comment This File! (This text will be shown to all users.)"), this);
    auto* commentLayout = new QVBoxLayout(commentGroup);

    auto* commentHelp = new QLabel(
        tr("For a film, you can say its length, its story, the language... "
           "And if it is a fake, you can inform other eMule users..."));
    commentHelp->setWordWrap(true);
    commentLayout->addWidget(commentHelp);

    m_commentEdit = new QLineEdit;
    m_commentEdit->setMaxLength(MAXFILECOMMENTLEN);   // MFC SetLimitText, same 128
    connect(m_commentEdit, &QLineEdit::textEdited, this, &CommentEditPanel::markDirty);
    connect(m_commentEdit, &QLineEdit::returnPressed, this, &CommentEditPanel::applyEdits);
    commentLayout->addWidget(m_commentEdit);

    // 2. The rating box (IDC_RATEQUEST / IDC_RATEHELP / IDC_RATELIST) with Reset and
    //    Apply to its right, where IDD_COMMENT puts Reset.
    auto* ratingRow = new QHBoxLayout;

    auto* ratingGroup = new QGroupBox(tr("File Quality"), this);
    auto* ratingLayout = new QVBoxLayout(ratingGroup);

    auto* ratingHelp = new QLabel(
        tr("Choose the file rating or advice users if the file is invalid!"));
    ratingHelp->setWordWrap(true);
    ratingLayout->addWidget(ratingHelp);

    m_ratingCombo = new QComboBox;
    for (int rating = 0; rating <= 5; ++rating)
        m_ratingCombo->addItem(ratingIcon(rating), ratingLabel(rating));
    connect(m_ratingCombo, &QComboBox::currentIndexChanged, this, &CommentEditPanel::markDirty);
    ratingLayout->addWidget(m_ratingCombo);
    ratingRow->addWidget(ratingGroup, 1);

    auto* buttonColumn = new QVBoxLayout;
    buttonColumn->addStretch(1);
    m_resetBtn = new QPushButton(tr("Reset"));
    connect(m_resetBtn, &QPushButton::clicked, this, &CommentEditPanel::resetEdits);
    buttonColumn->addWidget(m_resetBtn);

    // MFC applies on the property sheet's OK/Apply. This dialog's only bottom button
    // is Close, which is a reject — and the walker rebuilds every tab on each step,
    // so an apply-on-close or apply-on-navigate scheme would publish behind the user
    // or silently drop what they typed. An explicit button does neither.
    m_applyBtn = new QPushButton(tr("Apply"));
    m_applyBtn->setEnabled(false);
    connect(m_applyBtn, &QPushButton::clicked, this, &CommentEditPanel::applyEdits);
    buttonColumn->addWidget(m_applyBtn);
    ratingRow->addLayout(buttonColumn);

    // 3. Wrap the inherited list in IDD_COMMENT's own "Comment" group (IDC_USERCOMMENTS).
    auto* listGroup = new QGroupBox(tr("Comment"), this);
    auto* listLayout = new QVBoxLayout(listGroup);
    const int listIndex = panelLayout()->indexOf(commentTree());
    panelLayout()->removeWidget(emptyLabel());
    panelLayout()->removeWidget(commentTree());
    listLayout->addWidget(emptyLabel());     // addWidget reparents
    listLayout->addWidget(commentTree());
    panelLayout()->insertWidget(listIndex - 1, listGroup);

    panelLayout()->insertWidget(0, commentGroup);
    panelLayout()->insertLayout(1, ratingRow);
}

void CommentEditPanel::applyEdits()
{
    if (!m_applyBtn->isEnabled())
        return;
    emit postComment(fileHash(), m_commentEdit->text(), m_ratingCombo->currentIndex());
}

void CommentEditPanel::resetEdits()
{
    // MFC OnBnClickedReset: blank the text, back to "Not rated". It does not post —
    // clearing a published comment still takes an Apply.
    m_filling = true;
    m_commentEdit->clear();
    m_ratingCombo->setCurrentIndex(0);
    m_filling = false;
    markDirty();
}

void CommentEditPanel::markDirty()
{
    if (m_filling)
        return;
    m_dirty = true;
    m_applyBtn->setEnabled(true);
}

void CommentEditPanel::setEditingEnabled(bool enabled)
{
    m_commentEdit->setEnabled(enabled);
    m_ratingCombo->setEnabled(enabled);
    m_resetBtn->setEnabled(enabled);
    m_applyBtn->setEnabled(enabled && m_dirty);

    // Deliberately not the list or "Search Kad", which MFC does disable — but only
    // because its whole page is the editor. Here a file nobody can comment still has
    // other people's comments worth reading and a Kad lookup worth running, which is
    // precisely what the read-only page offers.
}

} // namespace eMule
