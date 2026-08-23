#include "pch.h"
/// @file SearchPanel.cpp
/// @brief Search tab panel — implementation.

#include "panels/SearchPanel.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/AbstractListView.h"
#include "controls/DownloadListModel.h"
#include "controls/SearchResultsModel.h"
#include "dialogs/FindInListDialog.h"
#include "utils/IpcFeedback.h"
#include "utils/ListActivation.h"
#include "utils/MenuUtils.h"
#include "utils/PreviewLauncher.h"
#include "utils/StatusBarNotifier.h"
#include "utils/ViewNavigation.h"
#include "utils/WebServices.h"
#include "prefs/Preferences.h"
#include "search/SearchParams.h"

#include "IpcMessage.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QFile>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QLineEdit>
#include <QProcess>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QStringList>
#include <QStringListModel>
#include <QTabBar>
#include <QTreeView>
#include <QVBoxLayout>

#include <utility>

namespace eMule {

using namespace Ipc;

namespace {

/// UiState key for the shared search-results header layout (all tabs share it).
const QString kSearchHeaderKey = QStringLiteral("searchResults");

/// Longest a newly arrived result may wait before it shows up in the list.
/// Small enough that the list still fills visibly in real time, large enough
/// that a Kad flood collapses ~1500 full-list refetches into a few dozen.
constexpr int ResultRefreshWindowMs = 400;

} // namespace

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

    m_resultRefreshTimer = new QTimer(this);
    m_resultRefreshTimer->setSingleShot(true);
    m_resultRefreshTimer->setInterval(ResultRefreshWindowMs);
    connect(m_resultRefreshTimer, &QTimer::timeout, this, [this] { drainDirtySearches(); });

    connect(m_ipc, &IpcClient::searchResultReceived, this, &SearchPanel::onSearchResultPush);
    connect(m_ipc, &IpcClient::downloadAdded, this, [this]{ refreshKnownTypes(); });
    connect(m_ipc, &IpcClient::downloadRemoved, this, [this]{ refreshKnownTypes(); });
    connect(m_ipc, &IpcClient::globalSearchProgress, this, [this](const IpcMessage& msg) {
        const auto searchID = static_cast<uint32_t>(msg.fieldInt(0));
        const bool running = msg.fieldBool(3);
        m_sweepSearchID = running ? searchID : 0;
        m_sweepAsked = static_cast<int>(msg.fieldInt(1));
        m_sweepTotal = static_cast<int>(msg.fieldInt(2));
        updateSweepProgress();
    });

    // Restore the last-used search method. Stored as the SearchType value, not the
    // combo index — startSearchFromExternal() already reads the combo as data, and an
    // index would silently mean a different method if the entries were ever reordered.
    QSettings settings;
    const int lastMethod = settings.value(QStringLiteral("search/lastMethodType"),
                                          static_cast<int>(SearchType::Kademlia)).toInt();
    if (const int idx = m_methodCombo->findData(lastMethod); idx >= 0)
        m_methodCombo->setCurrentIndex(idx);

    loadSearches();
    refreshKnownTypes();
}

void SearchPanel::startSearchFromExternal(const QString& expression,
                                          const QString& fileType,
                                          int method,
                                          const QString& tabTitle)
{
    if (fileType.isEmpty() && method < 0 && tabTitle.isEmpty()) {
        // Plain hand-off ("Search Related Files"): behave as if the user typed and pressed
        // Start, so the visible filter settings apply.
        m_nameEdit->setText(expression);
        onStartSearch();
        return;
    }

    // Parameterized hand-off: build the request from the arguments alone and leave the
    // filter UI untouched, so a leftover filter cannot discard every result.
    SearchRequest req;
    req.expression = expression;
    req.fileType   = fileType;
    req.method     = method >= 0 ? method : m_methodCombo->currentData().toInt();
    req.tabTitle   = tabTitle;
    sendSearchRequest(req);
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
    m_tabBar->setExpanding(true);
    m_tabBar->setElideMode(Qt::ElideRight);
    m_tabBar->setVisible(false);
    connect(m_tabBar, &QTabBar::currentChanged, this, &SearchPanel::onTabChanged);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &SearchPanel::onTabCloseRequested);
    mainLayout->addWidget(m_tabBar, 0, Qt::AlignLeft);

    // Results tree view
    m_resultView = new ListTreeView(this);
    m_resultView->setRootIsDecorated(false);
    m_resultView->setAlternatingRowColors(true);
    m_resultView->setSortingEnabled(true);
    m_resultView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_resultView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_resultView->setAllColumnsShowFocus(true);
    connect(m_resultView, &QTreeView::customContextMenuRequested, this, &SearchPanel::onResultContextMenu);
    connect(m_resultView, &QTreeView::doubleClicked, this, &SearchPanel::onResultDoubleClicked);

    // MFC CSearchListCtrl (srchybrid/SearchListCtrl.cpp:800, :817): Enter downloads the
    // selection, exactly as a double click does, and Alt+Enter opens the result sheet.
    bindListActivation(m_resultView,
        [this](const QModelIndex& index) { downloadResult(index.row()); },
        [this](const QModelIndex& index) { showResultDetails(index); });
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

    // The header is bound in setupResultHeader() once a tab's model is attached —
    // binding here, with no model and therefore no sections, cannot restore.

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

    // Row 2: global-search progress, across the whole bar. A paced sweep takes
    // servers × 750ms, so without this the search looks stalled. Hidden unless the
    // tab on screen is the one sweeping. MFC has the same bar at the bottom of its
    // search parameters window.
    m_sweepProgress = new QProgressBar(container);
    m_sweepProgress->setTextVisible(true);
    m_sweepProgress->setMaximumHeight(14);
    m_sweepProgress->setVisible(false);
    grid->addWidget(m_sweepProgress, 2, 0, 1, 3);

    // Left column stretches, buttons+filters are fixed width
    grid->setColumnStretch(0, 1);

    return container;
}

// ---------------------------------------------------------------------------
// Slot: Start Search
// ---------------------------------------------------------------------------

void SearchPanel::onStartSearch()
{
    if (!m_ipc || !m_ipc->isConnected()) {
        StatusBarNotifier::post(tr("Not connected to daemon — search cannot be started."), 4000);
        return;
    }

    const SearchRequest req = requestFromUi();
    if (req.expression.isEmpty())
        return;

    addToSearchHistory(req.expression);

    // Save last-used method, by type rather than by combo position
    QSettings settings;
    settings.setValue(QStringLiteral("search/lastMethodType"), m_methodCombo->currentData().toInt());

    sendSearchRequest(req);
}

void SearchPanel::sendSearchRequest(const SearchRequest& req)
{
    if (!m_ipc || !m_ipc->isConnected()) {
        StatusBarNotifier::post(tr("Not connected to daemon — search cannot be started."), 4000);
        return;
    }
    if (req.expression.isEmpty())
        return;

    IpcMessage msg(IpcMsgType::StartSearch);
    msg.append(req.expression);          // field 0
    msg.append(req.fileType);            // field 1
    msg.append(static_cast<qint64>(req.method));  // field 2
    msg.append(req.minSize);             // field 3
    msg.append(req.maxSize);             // field 4
    msg.append(static_cast<qint64>(req.avail));   // field 5
    msg.append(req.extension);           // field 6
    msg.append(static_cast<qint64>(req.completeSources)); // field 7
    msg.append(req.codec);                                // field 8
    msg.append(static_cast<qint64>(req.minBitrate));      // field 9
    msg.append(static_cast<qint64>(req.minLength));       // field 10
    msg.append(req.title);                                // field 11
    msg.append(req.album);                                // field 12
    msg.append(req.artist);                               // field 13

    const QString expression = req.expression;
    const int method = req.method;
    // MFC's strSpecialTitle: the collection search runs on a long author-key hex that
    // would be unreadable as a tab caption, so the caller can name the tab instead.
    const QString tabTitle = req.tabTitle.isEmpty() ? req.expression : req.tabTitle;

    m_ipc->sendRequest(std::move(msg),
                       [this, expression, method, tabTitle](const IpcMessage& resp) {
        if (!resp.fieldBool(0)) {
            const QString error = resp.fieldString(1);
            if (!error.isEmpty())
                QMessageBox::warning(this, tr("Search"), error);
            return;
        }

        const auto data = resp.fieldMap(1);
        const auto searchID = static_cast<uint32_t>(data.value(QStringLiteral("searchID")).toInteger());

        // "Automatic" is resolved daemon-side to the network the search actually ran
        // on, so the tab is labelled with that rather than with the request.
        const auto typeValue = data.value(QStringLiteral("type"));
        const int resolvedMethod = typeValue.isInteger() ? static_cast<int>(typeValue.toInteger())
                                                         : method;

        // Create new tab
        SearchTab tab;
        tab.searchID = searchID;
        tab.title = tabTitle;
        tab.method = resolvedMethod;
        tab.model = new SearchResultsModel(this);
        tab.proxy = new QSortFilterProxyModel(this);
        tab.proxy->setSourceModel(tab.model);
        tab.proxy->setSortRole(Qt::UserRole);
        m_tabs.push_back(tab);

        int idx;
        if (thePrefs.useOriginalIcons()) {
            static constexpr const char* methodIcons[] = {
                ":/icons/KadServer.ico",   // 0 = Automatic
                ":/icons/Server.ico",      // 1 = Ed2k Server
                ":/icons/Global.ico",      // 2 = Ed2k Global
                ":/icons/SearchKad.ico",   // 3 = Kademlia
            };
            const int mi = (resolvedMethod >= 0 && resolvedMethod <= 3) ? resolvedMethod : 0;
            idx = m_tabBar->addTab(QIcon(QString::fromLatin1(methodIcons[mi])),
                                   QStringLiteral("%1 (0)").arg(tabTitle));
        } else {
            idx = m_tabBar->addTab(QStringLiteral("%1 (0)").arg(tabTitle));
        }
        m_tabBar->setVisible(true);
        m_tabBar->setCurrentIndex(idx);
        m_cancelBtn->setEnabled(true);

        // A Kad search is indexed under a single keyword; when the first one is
        // already busy the daemon falls back to a later word of the expression.
        const QString keyword = data.value(QStringLiteral("keyword")).toString();
        const QString primaryKeyword = data.value(QStringLiteral("primaryKeyword")).toString();
        if (!keyword.isEmpty() && keyword != primaryKeyword) {
            StatusBarNotifier::post(tr("Kad: \"%1\" is already being searched — using \"%2\" as "
                                       "the search target.")
                                        .arg(primaryKeyword, keyword),
                                    6000);
        }
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
    msg.append(static_cast<qint64>(tab->searchID));
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
    // The sweep belongs to one search; arriving at another tab must not show its bar.
    updateSweepProgress();
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

void SearchPanel::onSearchResultPush(const IpcMessage& msg)
{
    // Field 0 is the search this result belongs to; only that tab is stale.
    // A push with no field (0) predates that and means "refresh everything".
    const auto searchID = static_cast<uint32_t>(msg.fieldInt(0));
    if (searchID != 0) {
        m_dirtySearchIDs.insert(searchID);
    } else {
        for (const auto& tab : m_tabs) {
            if (tab.searchID != 0)
                m_dirtySearchIDs.insert(tab.searchID);
        }
    }

    // Fixed-window rate limit, not a restart-on-every-event debounce: during a
    // burst the latter would never fire until the search went quiet.
    if (!m_resultRefreshTimer->isActive())
        m_resultRefreshTimer->start();
}

// ---------------------------------------------------------------------------
// Slot: Context menu
// ---------------------------------------------------------------------------

void SearchPanel::onResultContextMenu(const QPoint& pos)
{
    const auto selection = m_resultView->selectionModel()
                               ? m_resultView->selectionModel()->selectedRows()
                               : QModelIndexList{};
    const bool hasSelection = !selection.isEmpty();
    const bool singleSel    = (selection.size() == 1);
    m_contextMenu->clear();

    const bool useOriginal = thePrefs.useOriginalIcons();
    auto ico = [&](const char* res) -> QIcon {
        return useOriginal ? QIcon(QStringLiteral(":/icons/") + QLatin1String(res))
                           : QIcon();
    };

    // Walk the selection once, as MFC does (SearchListCtrl.cpp:680-693): whether
    // anything is still downloadable, and whether anything is not yet spam — the
    // latter decides which way the spam item reads.
    auto* tab = currentTab();
    bool anyNotSpam    = false;
    bool anyDownloadable = false;
    QString singleHash;
    QString singleName;
    int64_t singleSize = 0;
    for (const auto& idx : selection) {
        if (!tab)
            break;
        const auto* result = tab->model->resultAt(tab->proxy->mapToSource(idx).row());
        if (!result)
            continue;
        if (!result->isSpam)
            anyNotSpam = true;
        if (!m_downloadModel || !m_downloadModel->findByHash(result->hash))
            anyDownloadable = true;
        if (singleSel) {
            singleHash = result->hash;
            singleName = result->fileName;
            singleSize = result->fileSize;
        }
    }

    // Download — the default action, bold, as in MFC (SetDefaultItem at :731).
    auto* downloadAction = m_contextMenu->addAction(ico("Download.ico"), tr("Download"));
    downloadAction->setEnabled(hasSelection && anyDownloadable);
    connect(downloadAction, &QAction::triggered, this, [this] {
        const auto sel = m_resultView->selectionModel()->selectedRows();
        for (const auto& i : sel)
            downloadResult(i.row());
    });
    setMenuDefaultAction(m_contextMenu, downloadAction->isEnabled() ? downloadAction : nullptr);

    // Details... — extended controls only, exactly as MFC gates it, because the
    // sheet's Metadata page is itself an extended-controls feature.
    if (thePrefs.showExtControls()) {
        auto* detailsAction = m_contextMenu->addAction(ico("FileInfo.ico"), tr("Details..."));
        detailsAction->setEnabled(singleSel && tab);
        const uint32_t searchID = tab ? tab->searchID : 0;
        connect(detailsAction, &QAction::triggered, this, [this, searchID, singleHash] {
            fetchAndShowSearchDetails(searchID, singleHash, SearchDetailDialog::Metadata);
        });
    }

    // Comments... — the same sheet, forced onto its Comments page (MFC passes
    // IDD_COMMENTLST for MP_CMT, SearchListCtrl.cpp:817-824).
    {
        auto* commentsAction = m_contextMenu->addAction(ico("FileComments.ico"), tr("Comments..."));
        commentsAction->setEnabled(singleSel && tab);
        const uint32_t searchID = tab ? tab->searchID : 0;
        connect(commentsAction, &QAction::triggered, this, [this, searchID, singleHash] {
            fetchAndShowSearchDetails(searchID, singleHash, SearchDetailDialog::Comments);
        });
    }

    m_contextMenu->addSeparator();

    // Copy eD2K Links
    auto* copyLinkAction = m_contextMenu->addAction(ico("eD2kLink.ico"), tr("Copy eD2K Links"));
    copyLinkAction->setEnabled(hasSelection);
    connect(copyLinkAction, &QAction::triggered, this, [this] {
        QStringList links;
        for (const auto& i : m_resultView->selectionModel()->selectedRows())
            links << buildEd2kLink(i.row());
        if (!links.isEmpty())
            QApplication::clipboard()->setText(links.join(QLatin1Char('\n')));
    });

    // Copy eD2K Links (HTML)
    auto* copyHtmlAction = m_contextMenu->addAction(ico("Copy.ico"), tr("Copy eD2K Links (HTML)"));
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

    // Mark as Spam / Mark as not Spam — MFC inserts this right before Remove, with
    // no separator of its own, and only when the search spam filter is enabled
    // (SearchListCtrl.cpp:716-720). The label points at what the click will do.
    if (thePrefs.enableSearchResultFilter()) {
        const bool markAsSpam = anyNotSpam || !hasSelection;
        auto* spamAction = m_contextMenu->addAction(
            ico("Spam.ico"), markAsSpam ? tr("Mark as Spam") : tr("Mark as not Spam"));
        spamAction->setEnabled(hasSelection);
        connect(spamAction, &QAction::triggered, this, [this, markAsSpam] {
            auto* tab = currentTab();
            if (!tab || !m_ipc) return;
            for (const auto& i : m_resultView->selectionModel()->selectedRows()) {
                const auto srcIdx = tab->proxy->mapToSource(i);
                const auto* result = tab->model->resultAt(srcIdx.row());
                if (!result) continue;
                IpcMessage msg(IpcMsgType::MarkSearchSpam);
                msg.append(static_cast<qint64>(tab->searchID));
                msg.append(result->hash);
                msg.append(markAsSpam);
                m_ipc->sendRequest(std::move(msg), [this](const IpcMessage& resp) {
                    IpcFeedback::checkOrWarn(resp, this, tr("Mark as Spam"));
                });
            }
            requestSearchResults(tab->searchID);   // spam re-scoring changed the rows
        });
    }

    // Remove (remove from local results list)
    auto* removeAction = m_contextMenu->addAction(ico("ListRemove.ico"), tr("Remove"));
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
    auto* closeAction = m_contextMenu->addAction(ico("CloseTab.ico"), tr("Close Search Results"));
    closeAction->setEnabled(m_tabBar->currentIndex() >= 0);
    connect(closeAction, &QAction::triggered, this, [this] {
        if (m_tabBar->currentIndex() >= 0)
            closeSearch(m_tabBar->currentIndex());
    });

    // Close All Search Results
    auto* closeAllAction = m_contextMenu->addAction(ico("DeleteAll.ico"), tr("Close All Search Results"));
    closeAllAction->setEnabled(!m_tabs.empty());
    connect(closeAllAction, &QAction::triggered, this, &SearchPanel::closeAllSearches);

    m_contextMenu->addSeparator();

    // Preview — MFC inserts it here, immediately above Find, and only when exactly
    // one previewable file is selected (SearchListCtrl.cpp:710-713).
    if (singleSel && m_downloadModel && !m_streamToken.isEmpty()) {
        const auto* dl = m_downloadModel->findByHash(singleHash);
        if (dl && dl->isPreviewPossible) {
            connect(m_contextMenu->addAction(ico("Preview.ico"), tr("Preview")),
                    &QAction::triggered, this, [this, singleHash] { sendPreview(singleHash); });
        }
    }

    // Find... — over the whole result list, so selection is irrelevant.
    auto* findAction = m_contextMenu->addAction(ico("Search.ico"), tr("Find..."));
    findAction->setEnabled(tab && tab->proxy->rowCount() > 0);
    connect(findAction, &QAction::triggered, this,
            [this] { showFindInListDialog(this, m_resultView); });

    // Search Related Files — MFC turns the file name into a fresh search.
    auto* relatedAction = m_contextMenu->addAction(ico("KadFileSearch.ico"),
                                                   tr("Search Related Files"));
    relatedAction->setEnabled(singleSel && !singleName.isEmpty());
    connect(relatedAction, &QAction::triggered, this, [this, singleName] {
        const qsizetype dotIdx = singleName.lastIndexOf(QLatin1Char('.'));
        startSearchFromExternal(dotIdx > 0 ? singleName.left(dotIdx) : singleName);
    });

    // Web Services — greyed when webservices.dat is empty or the selection is not
    // exactly one file, matching MFC's flag2 (SearchListCtrl.cpp:724-727).
    auto* webMenu = m_contextMenu->addMenu(ico("Web.ico"), tr("Web Services"));
    if (singleSel)
        WebServices::instance().populateFileMenu(webMenu, singleHash, singleName,
                                                 static_cast<uint64_t>(singleSize));
    webMenu->setEnabled(!webMenu->isEmpty());

    m_contextMenu->popup(m_resultView->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// Slot: Double-click to download
// ---------------------------------------------------------------------------

void SearchPanel::showResultDetails(const QModelIndex& index)
{
    auto* tab = currentTab();
    if (!tab || !index.isValid())
        return;

    // Same extended-controls gate as the "Details..." entry: the sheet opens on its
    // Metadata page, which is itself an extended-controls feature.
    if (!thePrefs.showExtControls())
        return;

    const auto* result = tab->model->resultAt(tab->proxy->mapToSource(index).row());
    if (!result)
        return;

    fetchAndShowSearchDetails(tab->searchID, result->hash, SearchDetailDialog::Metadata);
}

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
    msg.append(static_cast<qint64>(searchID));

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

    const int srcRow = srcIdx.row();

    IpcMessage msg(IpcMsgType::DownloadSearchFile);
    msg.append(result->hash);
    msg.append(result->fileName);
    msg.append(static_cast<qint64>(result->fileSize));
    const int tabIdx = m_tabBar->currentIndex();
    m_ipc->sendRequest(std::move(msg), [this, tabIdx, srcRow](const IpcMessage& resp) {
        if (resp.fieldBool(0) && tabIdx >= 0
            && tabIdx < static_cast<int>(m_tabs.size()))
            m_tabs[static_cast<size_t>(tabIdx)].model->setKnownType(srcRow, 2); // Downloading
    });
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
        msg.append(static_cast<qint64>(tab.searchID));
        m_ipc->sendRequest(std::move(msg));
    }

    // Clean up model/proxy
    delete tab.proxy;
    delete tab.model;
    m_tabs.erase(m_tabs.begin() + tabIndex);
    m_tabBar->removeTab(tabIndex);

    if (m_tabs.empty()) {
        m_tabBar->setVisible(false);
        // setModel(nullptr) wipes every section; UiState keeps the cached layout
        // and switchToTab() re-applies it once a model is attached again.
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
    // setModel() clears every section, so the layout has to be pushed back each
    // time. The first call also binds the header, now that the columns exist.
    setupResultHeader();
    theUiState.applyHeaderState(m_resultView->header(), kSearchHeaderKey);
    // Every tab has its own proxy, so re-guard the selection on the new model.
    theUiState.guardSelectionOnReset(m_resultView);
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
            rowObj[QStringLiteral("fileSize")]            = static_cast<qint64>(row->fileSize);
            rowObj[QStringLiteral("sourceCount")]         = static_cast<qint64>(row->sourceCount);
            rowObj[QStringLiteral("completeSourceCount")] = static_cast<qint64>(row->completeSourceCount);
            rowObj[QStringLiteral("artist")]              = row->artist;
            rowObj[QStringLiteral("album")]               = row->album;
            rowObj[QStringLiteral("title")]               = row->title;
            rowObj[QStringLiteral("codec")]               = row->codec;
            rowObj[QStringLiteral("length")]              = static_cast<qint64>(row->length);
            rowObj[QStringLiteral("bitrate")]             = static_cast<qint64>(row->bitrate);
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
            row.fileSize            = static_cast<qint64>(r[QStringLiteral("fileSize")].toDouble());
            row.sourceCount         = static_cast<qint64>(r[QStringLiteral("sourceCount")].toDouble());
            row.completeSourceCount = static_cast<qint64>(r[QStringLiteral("completeSourceCount")].toDouble());
            row.artist              = r[QStringLiteral("artist")].toString();
            row.album               = r[QStringLiteral("album")].toString();
            row.title               = r[QStringLiteral("title")].toString();
            row.codec               = r[QStringLiteral("codec")].toString();
            row.length              = static_cast<qint64>(r[QStringLiteral("length")].toDouble());
            row.bitrate             = static_cast<qint64>(r[QStringLiteral("bitrate")].toDouble());
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

// ---------------------------------------------------------------------------
// Preview a search result via HTTP streaming
// ---------------------------------------------------------------------------

void SearchPanel::sendPreview(const QString& hash)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;

    if (m_streamToken.isEmpty()) {
        QMessageBox::warning(this, tr("Preview"),
            tr("Preview not available — web server is not running or stream token not received."));
        return;
    }

    launchPreview(daemonStreamUrl(m_ipc, hash, m_streamToken));
}

// ---------------------------------------------------------------------------
// Search-result details (MFC CSearchResultFileDetailSheet)
// ---------------------------------------------------------------------------

void SearchPanel::fetchAndShowSearchDetails(uint32_t searchID, const QString& hash,
                                            SearchDetailDialog::Page page)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;

    IpcMessage msg(IpcMsgType::GetSearchResultDetails);
    msg.append(static_cast<qint64>(searchID));
    msg.append(hash);
    m_ipc->sendRequest(std::move(msg),
        [this, page, searchID, hash](const IpcMessage& resp) {
            if (!resp.fieldBool(0))
                return;
            auto* dlg = new SearchDetailDialog(resp.field(1).toMap(), page, this);

            // Two-field request, hence the factory overload rather than the opcode one.
            const auto makeRequest = [searchID](const QString& key) {
                IpcMessage req(IpcMsgType::GetSearchResultDetails);
                req.append(static_cast<qint64>(searchID));
                req.append(key);
                return req;
            };
            connectKadNotesSearch(dlg, m_ipc, makeRequest);
            connectCommentFilter(dlg, m_ipc);
            dlg->setWalker(makeSearchWalker(searchID, hash));
            connectDetailNavigation(dlg, m_ipc, makeRequest);
            dlg->show();
        });
}

QModelIndex SearchPanel::resultIndexFor(uint32_t searchID, const QString& hash)
{
    // Bail out when the user has switched tabs: switchToTab() swaps the view's
    // model, so walking would silently step through a different search's results.
    const auto* tab = currentTab();
    if (!tab || tab->searchID != searchID || hash.isEmpty())
        return {};

    for (int row = 0; row < tab->model->rowCount(); ++row) {
        const auto* result = tab->model->resultAt(row);
        if (result && result->hash == hash)
            return ViewNav::fromSource(m_resultView, tab->model->index(row, 0));
    }
    return {};
}

DetailWalker SearchPanel::makeSearchWalker(uint32_t searchID, const QString& hash)
{
    auto anchor = std::make_shared<QString>(hash);

    DetailWalker walker;
    walker.step = [this, searchID, anchor](int delta) -> QString {
        auto* tab = currentTab();
        if (!tab || tab->searchID != searchID)
            return {};
        const QModelIndex to =
            ViewNav::step(m_resultView, resultIndexFor(searchID, *anchor), delta);
        if (!to.isValid())
            return {};
        const auto* result = tab->model->resultAt(ViewNav::toSource(to).row());
        if (!result)
            return {};
        *anchor = result->hash;
        return *anchor;
    };
    walker.canStep = [this, searchID, anchor](int delta) {
        return ViewNav::peekStep(m_resultView, resultIndexFor(searchID, *anchor),
                                 delta).isValid();
    };
    return walker;
}

// ---------------------------------------------------------------------------
// Refresh knownType for all results in all tabs via daemon lookup
// ---------------------------------------------------------------------------

void SearchPanel::refreshKnownTypes()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    for (size_t ti = 0; ti < m_tabs.size(); ++ti) {
        auto* tab = &m_tabs[ti];
        const int count = tab->model->resultCount();
        if (count == 0)
            continue;

        QCborArray hashes;
        for (int r = 0; r < count; ++r) {
            const auto* row = tab->model->resultAt(r);
            if (row)
                hashes.append(row->hash);
        }

        IpcMessage msg(IpcMsgType::GetKnownTypes);
        msg.append(QCborValue(hashes));

        m_ipc->sendRequest(std::move(msg), [this, ti](const IpcMessage& resp) {
            if (!resp.fieldBool(0) || ti >= m_tabs.size())
                return;

            auto* model = m_tabs[ti].model;
            const auto types = resp.fieldArray(1);
            const int count = std::min(static_cast<int>(types.size()), model->resultCount());

            QHash<QString, int> typesByHash;
            for (int i = 0; i < count; ++i) {
                const auto* row = model->resultAt(i);
                if (row)
                    typesByHash.insert(row->hash, static_cast<int>(types.at(i).toInteger()));
            }
            model->updateKnownTypes(typesByHash);
        });
    }
}

// ---------------------------------------------------------------------------
// Results header — bound once, on the first tab that attaches a model
// ---------------------------------------------------------------------------

void SearchPanel::setupResultHeader()
{
    if (m_headerBound)
        return;
    m_headerBound = true;

    // File Name, Size, Availability, Complete, Type, Artist, Album, Title,
    // Length, Bitrate, Codec, Known. A saved layout overrides these defaults.
    m_resultView->bindColumns(kSearchHeaderKey,
        {300, 80, 70, 70, 70, 100, 100, 100, 60, 60, 60, 60});
}

SearchPanel::SearchRequest SearchPanel::requestFromUi() const
{
    SearchRequest req;
    req.expression = m_nameEdit->text().trimmed();
    req.fileType   = m_typeCombo->currentData().toString();
    req.method     = m_methodCombo->currentData().toInt();
    req.minSize    = m_minSizeSpin->value() > 0
        ? static_cast<qint64>(m_minSizeSpin->value()) * 1024 * 1024 : 0;
    req.maxSize    = m_maxSizeSpin->value() > 0
        ? static_cast<qint64>(m_maxSizeSpin->value()) * 1024 * 1024 : 0;
    req.avail           = m_availSpin->value();
    req.extension       = m_extensionEdit->text().trimmed();
    req.completeSources = m_completeSpin->value();
    req.codec           = m_codecEdit->text().trimmed();
    req.minBitrate      = m_minBitrateSpin->value();
    req.minLength       = m_minLengthSpin->value();
    req.title           = m_titleEdit->text().trimmed();
    req.album           = m_albumEdit->text().trimmed();
    req.artist          = m_artistEdit->text().trimmed();
    return req;
}

void SearchPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Results that arrived while this tab was hidden are still marked dirty; fetch
    // them now so the list is current the moment it comes back on screen.
    drainDirtySearches();
}

void SearchPanel::updateSweepProgress()
{
    if (!m_sweepProgress)
        return;

    const auto* tab = currentTab();
    const bool show = m_sweepSearchID != 0 && m_sweepTotal > 0
                      && tab != nullptr && tab->searchID == m_sweepSearchID;
    if (show) {
        m_sweepProgress->setRange(0, m_sweepTotal);
        m_sweepProgress->setValue(m_sweepAsked);
        m_sweepProgress->setFormat(tr("Asking servers: %1 / %2")
                                       .arg(m_sweepAsked)
                                       .arg(m_sweepTotal));
    }
    m_sweepProgress->setVisible(show);
}

void SearchPanel::drainDirtySearches()
{
    // A hidden panel fetches nothing — a Kad search left running while the user
    // works in Transfers would otherwise keep refetching a list nobody can see.
    // The IDs stay dirty and showEvent() picks them up.
    if (!isVisible())
        return;

    // Take a copy: requestSearchResults() replies asynchronously, and more
    // pushes for the same search may arrive before those replies land.
    const auto dirty = std::exchange(m_dirtySearchIDs, {});
    for (const uint32_t searchID : dirty) {
        const bool stillOpen = std::ranges::any_of(m_tabs,
            [searchID](const SearchTab& tab) { return tab.searchID == searchID; });
        if (stillOpen)
            requestSearchResults(searchID);
    }
}

} // namespace eMule
