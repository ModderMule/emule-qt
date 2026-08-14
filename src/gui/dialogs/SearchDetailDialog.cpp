#include "pch.h"
/// @file SearchDetailDialog.cpp
/// @brief Search-result detail dialog — see SearchDetailDialog.h.

#include "SearchDetailDialog.h"

#include "MetadataPage.h"
#include "prefs/Preferences.h"

#include <QTabWidget>

namespace eMule {

// ── construction ───────────────────────────────────────────────────────

SearchDetailDialog::SearchDetailDialog(const QCborMap& details, Page initialPage,
                                       QWidget* parent)
    : DetailDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumSize(520, 320);
    resize(640, 380);

    m_pendingPage = initialPage;
    SearchDetailDialog::setDetails(details);   // no virtual dispatch from a ctor
}

// ── re-target ──────────────────────────────────────────────────────────

void SearchDetailDialog::setDetails(const QCborMap& details)
{
    // Remember the page by identity, not index: the Metadata page can come and go
    // if the extended-controls pref is toggled while the dialog is open.
    Page keepPage = m_pendingPage;
    if (m_tabs)
        keepPage = (m_tabs->currentWidget() == m_commentsPanel) ? Comments : Metadata;

    m_commentsPanel = nullptr;
    delete m_tabs;
    m_tabs = nullptr;

    setSubjectKey(details.value(QLatin1StringView("hash")).toString());
    setWindowTitle(tr("Details: %1")
        .arg(details.value(QLatin1StringView("fileName")).toString()));

    buildPages(details, keepPage);
}

// ── private helpers ────────────────────────────────────────────────────

void SearchDetailDialog::buildPages(const QCborMap& details, Page pageToSelect)
{
    m_tabs = new QTabWidget;
    const bool useIcons = thePrefs.useOriginalIcons();

    // MFC adds Metadata first and only under extended controls
    // (srchybrid/SearchListCtrl.cpp:121-124).
    QWidget* metadataPage = nullptr;
    if (thePrefs.showExtControls()) {
        metadataPage = createMetadataPage(details, QStringLiteral("searchDetailTags"));
        if (useIcons)
            m_tabs->addTab(metadataPage, QIcon(QStringLiteral(":/icons/MetaData.ico")), tr("Metadata"));
        else
            m_tabs->addTab(metadataPage, tr("Metadata"));
    }

    m_commentsPanel = new CommentsPanel(QStringLiteral("searchDetailComments"));
    connect(m_commentsPanel, &CommentsPanel::searchKadNotes,
            this, &DetailDialog::searchKadNotes);
    connect(m_commentsPanel, &CommentsPanel::commentFilterChanged,
            this, &DetailDialog::commentFilterChanged);
    m_commentsPanel->setDetails(details);

    if (useIcons)
        m_tabs->addTab(m_commentsPanel, QIcon(QStringLiteral(":/icons/FileComments.ico")), tr("Comments"));
    else
        m_tabs->addTab(m_commentsPanel, tr("Comments"));

    QWidget* target = (pageToSelect == Metadata && metadataPage) ? metadataPage
                                                                 : m_commentsPanel;
    m_tabs->setCurrentWidget(target);
    contentLayout()->addWidget(m_tabs);
}

} // namespace eMule
