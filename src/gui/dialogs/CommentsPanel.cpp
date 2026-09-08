#include "pch.h"
/// @file CommentsPanel.cpp
/// @brief Shared Comments page — see CommentsPanel.h.

#include "CommentsPanel.h"

#include "controls/AbstractListView.h"
#include "prefs/Preferences.h"
#include "utils/RatingIcons.h"

#include <QCborArray>
#include <QClipboard>
#include <QGuiApplication>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>

namespace eMule {

// ── construction ───────────────────────────────────────────────────────

CommentsPanel::CommentsPanel(const QString& stateKey, QWidget* parent)
    : QWidget(parent)
{
    buildUi(stateKey);
}

// ── public API ─────────────────────────────────────────────────────────

void CommentsPanel::setDetails(const QCborMap& details)
{
    m_fileHash = details.value(QLatin1StringView("hash")).toString();
    m_fileName = details.value(QLatin1StringView("fileName")).toString();

    m_tree->clear();
    const QCborArray comments = details.value(QLatin1StringView("comments")).toArray();
    for (const auto& entry : comments) {
        const QCborMap m = entry.toMap();
        const int rating = static_cast<int>(m.value(QLatin1StringView("rating")).toInteger());
        const QString userName = m.value(QLatin1StringView("userName")).toString();

        auto* item = new QTreeWidgetItem(m_tree);
        // Icon plus MFC's own words for the value, with no stars — GetRateString is
        // what CCommentListCtrl::AddComment puts in this column, for every value
        // including 0 ("Not rated"), because a comment-only row still has a cell.
        item->setText(ColRating, ratingLabel(rating));
        item->setData(ColRating, Qt::UserRole, rating);     // numeric sort key: the
                                                            // text is not monotonic
        item->setIcon(ColRating, ratingIcon(rating));
        item->setText(ColComment,  m.value(QLatin1StringView("comment")).toString());
        item->setText(ColFileName, m.value(QLatin1StringView("name")).toString());
        item->setText(ColUserName, userName);
        // MFC's Network column is eD2K for a source's comment, Kad for a note;
        // the daemon labels the latter "Kad" in the userName field.
        item->setText(ColOrigin, userName == QLatin1StringView("Kad")
                                     ? tr("Kad") : QStringLiteral("eD2K"));
    }

    m_emptyLabel->setVisible(comments.isEmpty());
    setKadSearchRunning(details.value(QLatin1StringView("notesSearchRunning")).toBool());
}

void CommentsPanel::setKadSearchRunning(bool running)
{
    m_searchKadBtn->setText(running ? tr("(Kad search in progress...)") : tr("Search Kad"));
    m_searchKadBtn->setEnabled(!running);

    // MFC moves focus off the button before disabling it, so the dialog does not
    // lose its focus widget mid-refresh (CommentDialogLst.cpp:162-163).
    if (running && m_searchKadBtn->hasFocus())
        m_tree->setFocus();
}

// ── private helpers ────────────────────────────────────────────────────

void CommentsPanel::buildUi(const QString& stateKey)
{
    auto* layout = new QVBoxLayout(this);
    m_layout = layout;

    auto* tree = new ListTreeWidget;
    m_tree = tree;
    m_tree->setHeaderLabels({tr("Rating"), tr("Comment"), tr("File Name"),
                             tr("User Name"), tr("Network")});
    m_tree->setRootIsDecorated(false);
    m_tree->setAlternatingRowColors(true);
    m_tree->setSortingEnabled(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setStretchLastSection(true);
    // Rating gets 150 rather than MFC's 80: the widest label is the one that matters
    // most ("Invalid / Corrupt / Fake"), and at 80 it elides to "Invali...", which is
    // the one case where the user must be able to read the cell at a glance.
    tree->bindColumns(stateKey, {180, 340, 200, 160, 80});

    m_emptyLabel = new QLabel(tr("No comments or ratings available for this file."));
    m_emptyLabel->setWordWrap(true);
    layout->addWidget(m_emptyLabel);
    layout->addWidget(m_tree);

    // Copy — the only entry in MFC's comment-list menu (CommentListCtrl.cpp:121-134).
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        auto* copyAct = menu.addAction(tr("Copy"), this, &CommentsPanel::copySelectedComments);
        copyAct->setEnabled(!m_tree->selectedItems().isEmpty());
        menu.exec(m_tree->viewport()->mapToGlobal(pos));
    });

    auto* buttons = new QHBoxLayout;
    m_filterBtn = new QPushButton(tr("Edit spam filter..."));
    connect(m_filterBtn, &QPushButton::clicked, this, &CommentsPanel::editSpamFilter);
    buttons->addWidget(m_filterBtn);
    buttons->addStretch(1);

    m_searchKadBtn = new QPushButton(tr("Search Kad"));
    if (thePrefs.useOriginalIcons())
        m_searchKadBtn->setIcon(QIcon(QStringLiteral(":/icons/KadFileSearch.ico")));
    connect(m_searchKadBtn, &QPushButton::clicked, this, [this]() {
        emit searchKadNotes(m_fileHash, m_fileName);
    });
    buttons->addWidget(m_searchKadBtn);
    layout->addLayout(buttons);
}

void CommentsPanel::editSpamFilter()
{
    bool ok = false;
    const QString input = QInputDialog::getText(
        this, tr("Edit spam filter for comments"),
        tr("Ignore comments containing: (Separator | )"),
        QLineEdit::Normal, thePrefs.commentFilter(), &ok);
    if (!ok)
        return;

    // MFC lower-cases the whole string, then drops empty and whitespace-only
    // tokens before storing (CommentDialogLst.cpp:192-205).
    QStringList tokens;
    for (const QString& token : input.toLower().split(QLatin1Char('|'))) {
        const QString trimmed = token.trimmed();
        if (!trimmed.isEmpty())
            tokens.append(trimmed);
    }

    const QString filter = tokens.join(QLatin1Char('|'));
    if (filter != thePrefs.commentFilter())
        emit commentFilterChanged(filter);
}

void CommentsPanel::copySelectedComments() const
{
    QStringList lines;
    for (const auto* item : m_tree->selectedItems())
        lines.append(item->text(ColComment));
    if (!lines.isEmpty())
        QGuiApplication::clipboard()->setText(lines.join(QStringLiteral("\r\n")));
}

} // namespace eMule
