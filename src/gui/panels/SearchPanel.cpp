/// @file SearchPanel.cpp
/// @brief Search tab panel — implementation.

#include "panels/SearchPanel.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/SearchResultsModel.h"
#include "prefs/Preferences.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QStringList>
#include <QStringListModel>
#include <QTabBar>
#include <QTreeView>
#include <QVBoxLayout>

namespace eMule {

using namespace Ipc;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SearchPanel::SearchPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    setupAutoComplete();
}

SearchPanel::~SearchPanel()
{
    saveSearches();
}

// ---------------------------------------------------------------------------
// IPC wiring
// ---------------------------------------------------------------------------

void SearchPanel::setIpcClient(IpcClient* client)
{
    m_ipc = client;
    if (!m_ipc)
        return;

    connect(m_ipc, &IpcClient::searchResultReceived, this, &SearchPanel::onSearchResultPush);

    // Restore last-used search method from settings (default: Kademlia = index 1)
    QSettings settings;
    const int lastMethod = settings.value(QStringLiteral("search/lastMethod"), 1).toInt();
    if (lastMethod >= 0 && lastMethod < m_methodCombo->count())
        m_methodCombo->setCurrentIndex(lastMethod);

    loadSearches();
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void SearchPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(2);

    mainLayout->addWidget(createSearchBar());

    // Tab bar for multiple searches
    m_tabBar = new QTabBar(this);
    m_tabBar->setTabsClosable(true);
    m_tabBar->setExpanding(false);
    m_tabBar->setVisible(false);
    connect(m_tabBar, &QTabBar::currentChanged, this, &SearchPanel::onTabChanged);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &SearchPanel::onTabCloseRequested);
    mainLayout->addWidget(m_tabBar, 0, Qt::AlignLeft);

    // Results tree view
    m_resultView = new QTreeView(this);
    m_resultView->setRootIsDecorated(false);
    m_resultView->setAlternatingRowColors(true);
    m_resultView->setSortingEnabled(true);
    m_resultView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_resultView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_resultView->setAllColumnsShowFocus(true);
    connect(m_resultView, &QTreeView::customContextMenuRequested, this, &SearchPanel::onResultContextMenu);
    connect(m_resultView, &QTreeView::doubleClicked, this, &SearchPanel::onResultDoubleClicked);
    mainLayout->addWidget(m_resultView, 1);

    // Bottom bar — Download on the left (matches MFC)
    auto* bottomLayout = new QHBoxLayout;
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    m_downloadBtn = new QPushButton(tr("Download"), this);
    m_downloadBtn->setEnabled(false);
    connect(m_downloadBtn, &QPushButton::clicked, this, [this] {
        const auto sel = m_resultView->selectionModel()
                             ? m_resultView->selectionModel()->selectedRows()
                             : QModelIndexList{};
        for (const auto& idx : sel)
            downloadResult(idx.row());
    });
    bottomLayout->addWidget(m_downloadBtn);
    m_statusLabel = new QLabel(this);
    bottomLayout->addWidget(m_statusLabel, 1);
    m_closeAllBtn = new QPushButton(tr("Close All Searches"), this);
    connect(m_closeAllBtn, &QPushButton::clicked, this, &SearchPanel::closeAllSearches);
    bottomLayout->addWidget(m_closeAllBtn);
    mainLayout->addLayout(bottomLayout);

    // Bind header state persistence
    theUiState.bindHeaderView(m_resultView->header(), QStringLiteral("searchResults"));

    // Context menu
    m_contextMenu = new QMenu(this);
}

QWidget* SearchPanel::createSearchBar()
{
    auto* container = new QWidget(this);
    auto* grid = new QGridLayout(container);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(2);

    // Row 0: Name label + edit field
    auto* nameRow = new QHBoxLayout;
    nameRow->setSpacing(4);
    nameRow->addWidget(new QLabel(tr("Name:"), container));
    m_nameEdit = new QLineEdit(container);
    m_nameEdit->setPlaceholderText(tr("Enter search keywords..."));
    connect(m_nameEdit, &QLineEdit::returnPressed, this, &SearchPanel::onStartSearch);
    nameRow->addWidget(m_nameEdit, 1);
    grid->addLayout(nameRow, 0, 0);

    // Row 1: Type + Method + Reset
    auto* typeRow = new QHBoxLayout;
    typeRow->setSpacing(4);
    typeRow->addWidget(new QLabel(tr("Type:"), container));
    m_typeCombo = new QComboBox(container);
    m_typeCombo->addItem(QIcon(QStringLiteral(":/icons/FileTypeAny.ico")),        tr("Any"),        QString{});
    m_typeCombo->addItem(QIcon(QStringLiteral(":/icons/FileTypeAudio.ico")),      tr("Audio"),      QStringLiteral("Audio"));
    m_typeCombo->addItem(QIcon(QStringLiteral(":/icons/FileTypeVideo.ico")),      tr("Video"),      QStringLiteral("Video"));
    m_typeCombo->addItem(QIcon(QStringLiteral(":/icons/FileTypePicture.ico")),    tr("Image"),      QStringLiteral("Image"));
    m_typeCombo->addItem(QIcon(QStringLiteral(":/icons/FileTypeDocument.ico")),   tr("Document"),   QStringLiteral("Doc"));
    m_typeCombo->addItem(QIcon(QStringLiteral(":/icons/FileTypeProgram.ico")),    tr("Program"),    QStringLiteral("Pro"));
    m_typeCombo->addItem(QIcon(QStringLiteral(":/icons/FileTypeArchive.ico")),    tr("Archive"),    QStringLiteral("Arc"));
    m_typeCombo->addItem(QIcon(QStringLiteral(":/icons/FileTypeCDImage.ico")),    tr("CD-Image"),   QStringLiteral("Iso"));
    m_typeCombo->addItem(QIcon(QStringLiteral(":/icons/emuleCollectionFileType.ico")), tr("Collection"), QStringLiteral("EmuleCollection"));
    typeRow->addWidget(m_typeCombo);
    typeRow->addWidget(new QLabel(tr("Method:"), container));
    m_methodCombo = new QComboBox(container);
    m_methodCombo->addItem(QIcon(QStringLiteral(":/icons/KadServer.ico")),  tr("Automatic"),    0);  // SearchType::Automatic
    m_methodCombo->addItem(QIcon(QStringLiteral(":/icons/SearchKad.ico")),  tr("Kad Network"),  3);  // SearchType::Kademlia
    m_methodCombo->addItem(QIcon(QStringLiteral(":/icons/Server.ico")),     tr("Ed2k Server"),  1);  // SearchType::Ed2kServer
    m_methodCombo->addItem(QIcon(QStringLiteral(":/icons/Global.ico")),     tr("Ed2k Global"),  2);  // SearchType::Ed2kGlobal
    typeRow->addWidget(m_methodCombo);
    m_resetBtn = new QPushButton(tr("Reset"), container);
    connect(m_resetBtn, &QPushButton::clicked, this, &SearchPanel::onResetFilters);
    typeRow->addWidget(m_resetBtn);
    typeRow->addStretch();
    grid->addLayout(typeRow, 1, 0);

    // Column 1: Scrollable filter area spanning both rows
    m_filterWidget = new QWidget(container);
    auto* filterLayout = new QGridLayout(m_filterWidget);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setHorizontalSpacing(4);
    filterLayout->setVerticalSpacing(1);

    filterLayout->addWidget(new QLabel(tr("Min. Size [MB]:"), m_filterWidget), 0, 0);
    m_minSizeSpin = new QSpinBox(m_filterWidget);
    m_minSizeSpin->setRange(0, 999999);
    m_minSizeSpin->setSpecialValueText(QStringLiteral(" "));
    filterLayout->addWidget(m_minSizeSpin, 0, 1);

    filterLayout->addWidget(new QLabel(tr("Max. Size [MB]:"), m_filterWidget), 1, 0);
    m_maxSizeSpin = new QSpinBox(m_filterWidget);
    m_maxSizeSpin->setRange(0, 999999);
    m_maxSizeSpin->setSpecialValueText(QStringLiteral(" "));
    filterLayout->addWidget(m_maxSizeSpin, 1, 1);

    filterLayout->addWidget(new QLabel(tr("Availability:"), m_filterWidget), 2, 0);
    m_availSpin = new QSpinBox(m_filterWidget);
    m_availSpin->setRange(0, 999);
    m_availSpin->setSpecialValueText(QStringLiteral(" "));
    filterLayout->addWidget(m_availSpin, 2, 1);

    filterLayout->addWidget(new QLabel(tr("Complete Sources:"), m_filterWidget), 3, 0);
    m_completeSpin = new QSpinBox(m_filterWidget);
    m_completeSpin->setRange(0, 999);
    m_completeSpin->setSpecialValueText(QStringLiteral(" "));
    filterLayout->addWidget(m_completeSpin, 3, 1);

    filterLayout->addWidget(new QLabel(tr("Extension:"), m_filterWidget), 4, 0);
    m_extensionEdit = new QLineEdit(m_filterWidget);
    m_extensionEdit->setMaximumWidth(80);
    filterLayout->addWidget(m_extensionEdit, 4, 1);

    filterLayout->addWidget(new QLabel(tr("Codec:"), m_filterWidget), 5, 0);
    m_codecEdit = new QLineEdit(m_filterWidget);
    m_codecEdit->setMaximumWidth(80);
    filterLayout->addWidget(m_codecEdit, 5, 1);

    filterLayout->addWidget(new QLabel(tr("Min. Bitrate [kbps]:"), m_filterWidget), 6, 0);
    m_minBitrateSpin = new QSpinBox(m_filterWidget);
    m_minBitrateSpin->setRange(0, 99999);
    m_minBitrateSpin->setSpecialValueText(QStringLiteral(" "));
    filterLayout->addWidget(m_minBitrateSpin, 6, 1);

    filterLayout->addWidget(new QLabel(tr("Min. Length [s]:"), m_filterWidget), 7, 0);
    m_minLengthSpin = new QSpinBox(m_filterWidget);
    m_minLengthSpin->setRange(0, 99999);
    m_minLengthSpin->setSpecialValueText(QStringLiteral(" "));
    filterLayout->addWidget(m_minLengthSpin, 7, 1);

    filterLayout->addWidget(new QLabel(tr("Title:"), m_filterWidget), 8, 0);
    m_titleEdit = new QLineEdit(m_filterWidget);
    m_titleEdit->setMaximumWidth(80);
    filterLayout->addWidget(m_titleEdit, 8, 1);

    filterLayout->addWidget(new QLabel(tr("Album:"), m_filterWidget), 9, 0);
    m_albumEdit = new QLineEdit(m_filterWidget);
    m_albumEdit->setMaximumWidth(80);
    filterLayout->addWidget(m_albumEdit, 9, 1);

    filterLayout->addWidget(new QLabel(tr("Artist:"), m_filterWidget), 10, 0);
    m_artistEdit = new QLineEdit(m_filterWidget);
    m_artistEdit->setMaximumWidth(80);
    filterLayout->addWidget(m_artistEdit, 10, 1);

    auto* filterScroll = new QScrollArea(container);
    filterScroll->setWidget(m_filterWidget);
    filterScroll->setWidgetResizable(true);
    filterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filterScroll->setFrameShape(QFrame::NoFrame);
    // Column 1: Start + Cancel buttons aligned with rows
    m_startBtn = new QPushButton(tr("Start"), container);
    m_startBtn->setFixedWidth(80);
    connect(m_startBtn, &QPushButton::clicked, this, &SearchPanel::onStartSearch);
    grid->addWidget(m_startBtn, 0, 1, Qt::AlignTop);

    m_cancelBtn = new QPushButton(tr("Cancel"), container);
    m_cancelBtn->setFixedWidth(80);
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &SearchPanel::onCancelSearch);
    grid->addWidget(m_cancelBtn, 1, 1, Qt::AlignTop);

    // Column 2: Scrollable filter area spanning both rows
    grid->addWidget(filterScroll, 0, 2, 2, 1);

    // Left column stretches, buttons+filters are fixed width
    grid->setColumnStretch(0, 1);

    return container;
}

// ---------------------------------------------------------------------------
// Slot: Start Search
// ---------------------------------------------------------------------------

void SearchPanel::onStartSearch()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    const QString expression = m_nameEdit->text().trimmed();
    if (expression.isEmpty())
        return;

    addToSearchHistory(expression);

    // Build search params
    const QString fileType = m_typeCombo->currentData().toString();
    const int method = m_methodCombo->currentData().toInt();
    const int64_t minSize = m_minSizeSpin->value() > 0
        ? static_cast<int64_t>(m_minSizeSpin->value()) * 1024 * 1024 : 0;
    const int64_t maxSize = m_maxSizeSpin->value() > 0
        ? static_cast<int64_t>(m_maxSizeSpin->value()) * 1024 * 1024 : 0;
    const int avail = m_availSpin->value();
    const QString extension = m_extensionEdit->text().trimmed();
    const int completeSources = m_completeSpin->value();
    const QString codec = m_codecEdit->text().trimmed();
    const int minBitrate = m_minBitrateSpin->value();
    const int minLength = m_minLengthSpin->value();
    const QString title = m_titleEdit->text().trimmed();
    const QString album = m_albumEdit->text().trimmed();
    const QString artist = m_artistEdit->text().trimmed();

    // Save last-used method
    QSettings settings;
    settings.setValue(QStringLiteral("search/lastMethod"), m_methodCombo->currentIndex());

    IpcMessage msg(IpcMsgType::StartSearch);
    msg.append(expression);              // field 0
    msg.append(fileType);                // field 1
    msg.append(static_cast<int64_t>(method));  // field 2
    msg.append(minSize);                 // field 3
    msg.append(maxSize);                 // field 4
    msg.append(static_cast<int64_t>(avail));   // field 5
    msg.append(extension);               // field 6
    msg.append(static_cast<int64_t>(completeSources)); // field 7
    msg.append(codec);                                    // field 8
    msg.append(static_cast<int64_t>(minBitrate));         // field 9
    msg.append(static_cast<int64_t>(minLength));          // field 10
    msg.append(title);                                    // field 11
    msg.append(album);                                    // field 12
    msg.append(artist);                                   // field 13

    m_ipc->sendRequest(std::move(msg), [this, expression](const IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;

        const auto data = resp.fieldMap(1);
        const auto searchID = static_cast<uint32_t>(data.value(QStringLiteral("searchID")).toInteger());

        // Create new tab
        SearchTab tab;
        tab.searchID = searchID;
        tab.title = expression;
        tab.model = new SearchResultsModel(this);
        tab.proxy = new QSortFilterProxyModel(this);
        tab.proxy->setSourceModel(tab.model);
        tab.proxy->setSortRole(Qt::UserRole);
        m_tabs.push_back(tab);

        const int idx = m_tabBar->addTab(QStringLiteral("%1 (0)").arg(expression));
        m_tabBar->setVisible(true);
        m_tabBar->setCurrentIndex(idx);
        m_cancelBtn->setEnabled(true);
    });
}

// ---------------------------------------------------------------------------
// Slot: Cancel Search
// ---------------------------------------------------------------------------

void SearchPanel::onCancelSearch()
{
    auto* tab = currentTab();
    if (!tab || !m_ipc)
        return;

    IpcMessage msg(IpcMsgType::StopSearch);
    msg.append(static_cast<int64_t>(tab->searchID));
    m_ipc->sendRequest(std::move(msg));
    m_cancelBtn->setEnabled(false);
}

// ---------------------------------------------------------------------------
// Slot: Reset Filters
// ---------------------------------------------------------------------------

void SearchPanel::onResetFilters()
{
    m_nameEdit->clear();
    m_typeCombo->setCurrentIndex(0);
    m_minSizeSpin->setValue(0);
    m_maxSizeSpin->setValue(0);
    m_availSpin->setValue(0);
    m_completeSpin->setValue(0);
    m_extensionEdit->clear();
    m_codecEdit->clear();
    m_minBitrateSpin->setValue(0);
    m_minLengthSpin->setValue(0);
    m_titleEdit->clear();
    m_albumEdit->clear();
    m_artistEdit->clear();
}

// ---------------------------------------------------------------------------
// Slot: Tab changed
// ---------------------------------------------------------------------------

void SearchPanel::onTabChanged(int index)
{
    switchToTab(index);
}

// ---------------------------------------------------------------------------
// Slot: Tab close requested
// ---------------------------------------------------------------------------

void SearchPanel::onTabCloseRequested(int index)
{
    closeSearch(index);
}

// ---------------------------------------------------------------------------
// Slot: Push event — search result arrived
// ---------------------------------------------------------------------------

void SearchPanel::onSearchResultPush()
{
    // Request fresh results for all active tabs (skip stored searches with ID 0)
    for (const auto& tab : m_tabs) {
        if (tab.searchID != 0)
            requestSearchResults(tab.searchID);
    }
}

// ---------------------------------------------------------------------------
// Slot: Context menu
// ---------------------------------------------------------------------------

void SearchPanel::onResultContextMenu(const QPoint& pos)
{
    const bool hasSelection = m_resultView->selectionModel()
                              && m_resultView->selectionModel()->hasSelection();
    m_contextMenu->clear();

    // Download
    auto* downloadAction = m_contextMenu->addAction(tr("Download"));
    downloadAction->setEnabled(hasSelection);
    connect(downloadAction, &QAction::triggered, this, [this] {
        const auto sel = m_resultView->selectionModel()->selectedRows();
        for (const auto& i : sel)
            downloadResult(i.row());
    });

    m_contextMenu->addSeparator();

    // Copy eD2K Links
    auto* copyLinkAction = m_contextMenu->addAction(tr("Copy eD2K Links"));
    copyLinkAction->setEnabled(hasSelection);
    connect(copyLinkAction, &QAction::triggered, this, [this] {
        QStringList links;
        for (const auto& i : m_resultView->selectionModel()->selectedRows())
            links << buildEd2kLink(i.row());
        if (!links.isEmpty())
            QApplication::clipboard()->setText(links.join(QLatin1Char('\n')));
    });

    // Copy eD2K Links (HTML)
    auto* copyHtmlAction = m_contextMenu->addAction(tr("Copy eD2K Links (HTML)"));
    copyHtmlAction->setEnabled(hasSelection);
    connect(copyHtmlAction, &QAction::triggered, this, [this] {
        QStringList links;
        for (const auto& i : m_resultView->selectionModel()->selectedRows()) {
            const QString link = buildEd2kLink(i.row());
            auto* tab = currentTab();
            if (!tab) continue;
            const auto proxyIdx = tab->proxy->index(i.row(), 0);
            const auto srcIdx = tab->proxy->mapToSource(proxyIdx);
            const auto* result = tab->model->resultAt(srcIdx.row());
            if (result)
                links << QStringLiteral("<a href=\"%1\">%2</a>").arg(link, result->fileName.toHtmlEscaped());
        }
        if (!links.isEmpty())
            QApplication::clipboard()->setText(links.join(QStringLiteral("<br>\n")));
    });

    m_contextMenu->addSeparator();

    // Mark as Spam
    // ToDo: Implement spam marking via IPC (send to core SearchList::markAsSpam)
    auto* spamAction = m_contextMenu->addAction(tr("Mark as Spam"));
    spamAction->setEnabled(false);

    // Remove (remove from local results list)
    auto* removeAction = m_contextMenu->addAction(tr("Remove"));
    removeAction->setEnabled(hasSelection);
    connect(removeAction, &QAction::triggered, this, [this] {
        auto* tab = currentTab();
        if (!tab) return;
        std::vector<int> sourceRows;
        for (const auto& i : m_resultView->selectionModel()->selectedRows()) {
            const auto srcIdx = tab->proxy->mapToSource(i);
            sourceRows.push_back(srcIdx.row());
        }
        std::sort(sourceRows.rbegin(), sourceRows.rend());
        for (int r : sourceRows)
            tab->model->removeRow(r);
        m_tabBar->setTabText(m_tabBar->currentIndex(),
            QStringLiteral("%1 (%2)").arg(tab->title).arg(tab->model->resultCount()));
    });

    m_contextMenu->addSeparator();

    // Close Search Results
    auto* closeAction = m_contextMenu->addAction(tr("Close Search Results"));
    closeAction->setEnabled(m_tabBar->currentIndex() >= 0);
    connect(closeAction, &QAction::triggered, this, [this] {
        if (m_tabBar->currentIndex() >= 0)
            closeSearch(m_tabBar->currentIndex());
    });

    // Close All Search Results
    auto* closeAllAction = m_contextMenu->addAction(tr("Close All Search Results"));
    closeAllAction->setEnabled(!m_tabs.empty());
    connect(closeAllAction, &QAction::triggered, this, &SearchPanel::closeAllSearches);

    m_contextMenu->popup(m_resultView->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// Slot: Double-click to download
// ---------------------------------------------------------------------------

void SearchPanel::onResultDoubleClicked(const QModelIndex& index)
{
    if (index.isValid())
        downloadResult(index.row());
}

// ---------------------------------------------------------------------------
// Request search results via IPC
// ---------------------------------------------------------------------------

void SearchPanel::requestSearchResults(uint32_t searchID)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage msg(IpcMsgType::GetSearchResults);
    msg.append(static_cast<int64_t>(searchID));

    m_ipc->sendRequest(std::move(msg), [this, searchID](const IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;

        const auto arr = resp.fieldArray(1);
        std::vector<SearchResultRow> rows;
        rows.reserve(static_cast<size_t>(arr.size()));

        for (const auto& val : arr) {
            const auto m = val.toMap();
            SearchResultRow row;
            row.hash               = m.value(QStringLiteral("hash")).toString();
            row.fileName           = m.value(QStringLiteral("fileName")).toString();
            row.fileSize           = m.value(QStringLiteral("fileSize")).toInteger();
            row.sourceCount        = m.value(QStringLiteral("sourceCount")).toInteger();
            row.completeSourceCount = m.value(QStringLiteral("completeSourceCount")).toInteger();
            row.fileType           = m.value(QStringLiteral("fileType")).toString();
            row.knownType          = static_cast<int>(m.value(QStringLiteral("knownType")).toInteger());
            row.isSpam             = m.value(QStringLiteral("isSpam")).toBool();
            row.artist             = m.value(QStringLiteral("artist")).toString();
            row.album              = m.value(QStringLiteral("album")).toString();
            row.title              = m.value(QStringLiteral("title")).toString();
            row.length             = m.value(QStringLiteral("length")).toInteger();
            row.bitrate            = m.value(QStringLiteral("bitrate")).toInteger();
            row.codec              = m.value(QStringLiteral("codec")).toString();
            rows.push_back(std::move(row));
        }

        // Find the matching tab and update
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            if (m_tabs[i].searchID == searchID) {
                const QString selKey = (m_tabBar->currentIndex() == static_cast<int>(i))
                    ? saveSelection() : QString{};

                m_tabs[i].model->setResults(std::move(rows));

                // Update tab text with result count
                m_tabBar->setTabText(static_cast<int>(i),
                    QStringLiteral("%1 (%2)").arg(m_tabs[i].title)
                        .arg(m_tabs[i].model->resultCount()));

                if (!selKey.isEmpty())
                    restoreSelection(selKey);
                break;
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Download a result
// ---------------------------------------------------------------------------

void SearchPanel::downloadResult(int row)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    auto* tab = currentTab();
    if (!tab)
        return;

    // Map from proxy row to source row
    const auto proxyIdx = tab->proxy->index(row, 0);
    const auto srcIdx = tab->proxy->mapToSource(proxyIdx);
    const auto* result = tab->model->resultAt(srcIdx.row());
    if (!result)
        return;

    IpcMessage msg(IpcMsgType::DownloadSearchFile);
    msg.append(result->hash);
    msg.append(result->fileName);
    msg.append(static_cast<int64_t>(result->fileSize));
    m_ipc->sendRequest(std::move(msg));
}

// ---------------------------------------------------------------------------
// Copy eD2k link to clipboard
// ---------------------------------------------------------------------------

QString SearchPanel::buildEd2kLink(int proxyRow)
{
    auto* tab = currentTab();
    if (!tab)
        return {};

    const auto proxyIdx = tab->proxy->index(proxyRow, 0);
    const auto srcIdx = tab->proxy->mapToSource(proxyIdx);
    const auto* result = tab->model->resultAt(srcIdx.row());
    if (!result)
        return {};

    return QStringLiteral("ed2k://|file|%1|%2|%3|/")
        .arg(result->fileName).arg(result->fileSize).arg(result->hash);
}

void SearchPanel::copyEd2kLink(int row)
{
    const QString link = buildEd2kLink(row);
    if (!link.isEmpty())
        QApplication::clipboard()->setText(link);
}

// ---------------------------------------------------------------------------
// Close a search tab
// ---------------------------------------------------------------------------

void SearchPanel::closeSearch(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= static_cast<int>(m_tabs.size()))
        return;

    auto& tab = m_tabs[static_cast<size_t>(tabIndex)];

    // Send remove request to daemon (skip for stored/passive searches with ID 0)
    if (m_ipc && m_ipc->isConnected() && tab.searchID != 0) {
        IpcMessage msg(IpcMsgType::RemoveSearch);
        msg.append(static_cast<int64_t>(tab.searchID));
        m_ipc->sendRequest(std::move(msg));
    }

    // Clean up model/proxy
    delete tab.proxy;
    delete tab.model;
    m_tabs.erase(m_tabs.begin() + tabIndex);
    m_tabBar->removeTab(tabIndex);

    if (m_tabs.empty()) {
        m_tabBar->setVisible(false);
        m_resultView->setModel(nullptr);
        m_statusLabel->clear();
        m_cancelBtn->setEnabled(false);
    }
}

// ---------------------------------------------------------------------------
// Close all search tabs
// ---------------------------------------------------------------------------

void SearchPanel::closeAllSearches()
{
    if (m_ipc && m_ipc->isConnected()) {
        IpcMessage msg(IpcMsgType::ClearAllSearches);
        m_ipc->sendRequest(std::move(msg));
    }

    for (auto& tab : m_tabs) {
        delete tab.proxy;
        delete tab.model;
    }
    m_tabs.clear();

    while (m_tabBar->count() > 0)
        m_tabBar->removeTab(0);
    m_tabBar->setVisible(false);
    m_resultView->setModel(nullptr);
    m_statusLabel->clear();
    m_cancelBtn->setEnabled(false);
}

// ---------------------------------------------------------------------------
// Switch to a tab
// ---------------------------------------------------------------------------

void SearchPanel::switchToTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) {
        m_resultView->setModel(nullptr);
        m_downloadBtn->setEnabled(false);
        return;
    }

    auto& tab = m_tabs[static_cast<size_t>(index)];
    m_resultView->setModel(tab.proxy);
    connect(m_resultView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SearchPanel::updateDownloadButton);
    updateDownloadButton();
    m_statusLabel->setText(QStringLiteral("%1 results")
        .arg(tab.model->resultCount()));
}

// ---------------------------------------------------------------------------
// Get current tab
// ---------------------------------------------------------------------------

SearchTab* SearchPanel::currentTab()
{
    const int idx = m_tabBar->currentIndex();
    if (idx < 0 || idx >= static_cast<int>(m_tabs.size()))
        return nullptr;
    return &m_tabs[static_cast<size_t>(idx)];
}

// ---------------------------------------------------------------------------
// Selection preservation
// ---------------------------------------------------------------------------

QString SearchPanel::saveSelection() const
{
    const auto* sel = m_resultView->selectionModel();
    if (!sel || !sel->hasSelection())
        return {};

    const auto rows = sel->selectedRows();
    if (rows.isEmpty())
        return {};

    // Get hash from the proxy model's first selected row
    const auto proxyIdx = rows.first();
    auto* tab = const_cast<SearchPanel*>(this)->currentTab();
    if (!tab)
        return {};

    const auto srcIdx = tab->proxy->mapToSource(proxyIdx);
    return tab->model->hashAt(srcIdx.row());
}

void SearchPanel::restoreSelection(const QString& key)
{
    if (key.isEmpty())
        return;

    auto* tab = currentTab();
    if (!tab)
        return;

    // Find the row with matching hash in the source model, then map to proxy
    for (int r = 0; r < tab->model->resultCount(); ++r) {
        if (tab->model->hashAt(r) == key) {
            const auto srcIdx = tab->model->index(r, 0);
            const auto proxyIdx = tab->proxy->mapFromSource(srcIdx);
            if (proxyIdx.isValid()) {
                m_resultView->setCurrentIndex(proxyIdx);
                m_resultView->scrollTo(proxyIdx);
            }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Update Download button enabled state based on selection
// ---------------------------------------------------------------------------

void SearchPanel::updateDownloadButton()
{
    auto* sm = m_resultView->selectionModel();
    m_downloadBtn->setEnabled(sm && sm->hasSelection());
}

// ---------------------------------------------------------------------------
// Autocomplete setup
// ---------------------------------------------------------------------------

void SearchPanel::setupAutoComplete()
{
    m_historyModel = new QStringListModel(this);

    QSettings settings;
    const QStringList history = settings.value(QStringLiteral("search/history")).toStringList();
    m_historyModel->setStringList(history);

    m_completer = new QCompleter(m_historyModel, this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setFilterMode(Qt::MatchContains);

    if (thePrefs.useAutoCompletion())
        m_nameEdit->setCompleter(m_completer);
}

void SearchPanel::addToSearchHistory(const QString& expression)
{
    if (!thePrefs.useAutoCompletion() || expression.isEmpty())
        return;

    QStringList list = m_historyModel->stringList();
    list.removeAll(expression);
    list.prepend(expression);
    if (list.size() > 50)
        list = list.mid(0, 50);

    m_historyModel->setStringList(list);

    QSettings settings;
    settings.setValue(QStringLiteral("search/history"), list);
}

// ---------------------------------------------------------------------------
// Search persistence
// ---------------------------------------------------------------------------

void SearchPanel::saveSearches()
{
    const QString path = thePrefs.configDir() + QStringLiteral("/StoredSearches.json");

    if (!thePrefs.storeSearches() || m_tabs.empty()) {
        QFile::remove(path);
        return;
    }

    QJsonArray searchesArr;
    for (const auto& tab : m_tabs) {
        QJsonObject searchObj;
        searchObj[QStringLiteral("title")] = tab.title;

        QJsonArray resultsArr;
        for (int r = 0; r < tab.model->resultCount(); ++r) {
            const auto* row = tab.model->resultAt(r);
            if (!row) continue;
            QJsonObject rowObj;
            rowObj[QStringLiteral("hash")]                = row->hash;
            rowObj[QStringLiteral("fileName")]            = row->fileName;
            rowObj[QStringLiteral("fileType")]            = row->fileType;
            rowObj[QStringLiteral("fileSize")]            = row->fileSize;
            rowObj[QStringLiteral("sourceCount")]         = row->sourceCount;
            rowObj[QStringLiteral("completeSourceCount")] = row->completeSourceCount;
            rowObj[QStringLiteral("artist")]              = row->artist;
            rowObj[QStringLiteral("album")]               = row->album;
            rowObj[QStringLiteral("title")]               = row->title;
            rowObj[QStringLiteral("codec")]               = row->codec;
            rowObj[QStringLiteral("length")]              = row->length;
            rowObj[QStringLiteral("bitrate")]             = row->bitrate;
            rowObj[QStringLiteral("knownType")]           = row->knownType;
            rowObj[QStringLiteral("isSpam")]              = row->isSpam;
            resultsArr.append(rowObj);
        }
        searchObj[QStringLiteral("results")] = resultsArr;
        searchesArr.append(searchObj);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("searches")] = searchesArr;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void SearchPanel::loadSearches()
{
    if (!thePrefs.storeSearches())
        return;

    const QString path = thePrefs.configDir() + QStringLiteral("/StoredSearches.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    const auto doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return;

    const auto root = doc.object();
    if (root[QStringLiteral("version")].toInt() != 1)
        return;

    const auto searches = root[QStringLiteral("searches")].toArray();
    for (const auto& searchVal : searches) {
        const auto searchObj = searchVal.toObject();
        const QString title = searchObj[QStringLiteral("title")].toString();
        if (title.isEmpty()) continue;

        std::vector<SearchResultRow> rows;
        const auto resultsArr = searchObj[QStringLiteral("results")].toArray();
        rows.reserve(static_cast<size_t>(resultsArr.size()));

        for (const auto& rVal : resultsArr) {
            const auto r = rVal.toObject();
            SearchResultRow row;
            row.hash                = r[QStringLiteral("hash")].toString();
            row.fileName            = r[QStringLiteral("fileName")].toString();
            row.fileType            = r[QStringLiteral("fileType")].toString();
            row.fileSize            = static_cast<int64_t>(r[QStringLiteral("fileSize")].toDouble());
            row.sourceCount         = static_cast<int64_t>(r[QStringLiteral("sourceCount")].toDouble());
            row.completeSourceCount = static_cast<int64_t>(r[QStringLiteral("completeSourceCount")].toDouble());
            row.artist              = r[QStringLiteral("artist")].toString();
            row.album               = r[QStringLiteral("album")].toString();
            row.title               = r[QStringLiteral("title")].toString();
            row.codec               = r[QStringLiteral("codec")].toString();
            row.length              = static_cast<int64_t>(r[QStringLiteral("length")].toDouble());
            row.bitrate             = static_cast<int64_t>(r[QStringLiteral("bitrate")].toDouble());
            row.knownType           = r[QStringLiteral("knownType")].toInt();
            row.isSpam              = r[QStringLiteral("isSpam")].toBool();
            rows.push_back(std::move(row));
        }

        SearchTab tab;
        tab.searchID = 0;
        tab.title = title;
        tab.model = new SearchResultsModel(this);
        tab.proxy = new QSortFilterProxyModel(this);
        tab.proxy->setSourceModel(tab.model);
        tab.proxy->setSortRole(Qt::UserRole);
        tab.model->setResults(std::move(rows));
        m_tabs.push_back(tab);

        m_tabBar->addTab(QStringLiteral("%1 (%2)").arg(title).arg(tab.model->resultCount()));
    }

    if (!m_tabs.empty()) {
        m_tabBar->setVisible(true);
        m_tabBar->setCurrentIndex(0);
    }
}

} // namespace eMule
