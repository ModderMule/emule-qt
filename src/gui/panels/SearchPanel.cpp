#include "pch.h"
/// @file SearchPanel.cpp
/// @brief Search tab panel — implementation.

#include "panels/SearchPanel.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/FilterEdit.h"
#include "controls/SearchResultsProxy.h"
#include "controls/AbstractListView.h"
#include "controls/DownloadListModel.h"
#include "controls/FitTextTabBar.h"
#include "controls/IndexerResultsModel.h"
#include "controls/SearchResultsModel.h"
#include "dialogs/FindInListDialog.h"
#include "utils/CountryFlags.h"
#include "utils/IpcFeedback.h"
#include "utils/ListActivation.h"
#include "utils/Log.h"
#include "utils/MenuUtils.h"
#include "utils/MetaResultActions.h"
#include "utils/PreviewLauncher.h"
#include "utils/StatusBarNotifier.h"
#include "dialogs/PeerPreviewDialog.h"
#include "utils/ViewNavigation.h"
#include "utils/WebServices.h"
#include "prefs/Preferences.h"
#include "search/SearchParams.h"

#include "IpcMessage.h"

#include <set>
#include <QMouseEvent>
#include <QApplication>
#include <QShortcut>
#include <QPointer>
#include <QTimer>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
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
#include <QSignalBlocker>
#include <QSortFilterProxyModel>
#include <QCheckBox>
#include <QSpinBox>
#include <QStringList>
#include <QStringListModel>
#include <QTabBar>
#include <QScrollBar>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace eMule {

using namespace Ipc;

namespace {

/// UiState key for the shared search-results header layout (all tabs share it).
const QString kSearchHeaderKey = QStringLiteral("searchResults");

/// Its own key: an indexer tab's columns are Name/Size/Age/Category/Grabs/
/// Indexer, nothing like the ED2K set, and sharing a key would apply one's saved
/// widths to the other.
const QString kIndexerHeaderKey = QStringLiteral("searchResultsIndexer");

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
    connect(m_ipc, &IpcClient::searchPreviewReceived, this, &SearchPanel::onSearchPreviewPush);
    // Download To needs the list before the menu opens, not after a round trip.
    connect(m_ipc, &IpcClient::categoriesChanged, this,
            [this](const Ipc::IpcMessage&) { requestCategories(); });
    requestCategories();
    connect(m_ipc, &IpcClient::indexerResultsReceived, this,
            &SearchPanel::onIndexerResultsPush);
    connect(m_ipc, &IpcClient::indexerSearchProgress, this,
            &SearchPanel::onIndexerProgressPush);
    connect(m_ipc, &IpcClient::indexerSearchFinished, this,
            &SearchPanel::onIndexerFinishedPush);
    connect(m_ipc, &IpcClient::downloadAdded, this, [this]{ refreshKnownTypes(); });
    connect(m_ipc, &IpcClient::downloadRemoved, this, [this]{ refreshKnownTypes(); });
    // Terminal events only. PushUsenetQueueItem fires continuously while a release
    // downloads, and re-asking on every one of those would be a lookup per tick.
    connect(m_ipc, &IpcClient::usenetItemFinished, this,
            [this]{ refreshUsenetKnownTypes(); });
    connect(m_ipc, &IpcClient::usenetItemRemoved, this,
            [this]{ refreshUsenetKnownTypes(); });
    connect(m_ipc, &IpcClient::searchStateChanged, this, &SearchPanel::onSearchStatePush);
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

void SearchPanel::startRelatedSearch(const QStringList& hashes, const QStringList& names)
{
    if (hashes.isEmpty())
        return;

    // Syntax: related::<file hash>[::<file hash>…] — an ordinary server search
    // (srchybrid/SearchResultsWnd.cpp:1665-1692).
    QString title = tr("Related") + QStringLiteral(": ") + names.join(QStringLiteral(", "));
    if (title.size() > 50)
        title = title.left(47) + QStringLiteral("...");
    startSearchFromExternal(QStringLiteral("related::") + hashes.join(QStringLiteral("::")),
                            {}, /*Ed2k Server*/ 1, title);
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
    // Tabs fit their full "title (count)"; scroll arrows once they overflow
    m_tabBar = new FitTextTabBar(this);
    m_tabBar->setTabsClosable(true);
    m_tabBar->setVisible(false);
    connect(m_tabBar, &QTabBar::currentChanged, this, &SearchPanel::onTabChanged);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &SearchPanel::onTabCloseRequested);
    connect(m_tabBar, &QTabBar::tabBarDoubleClicked, this, &SearchPanel::onTabDoubleClicked);
    m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabBar, &QTabBar::customContextMenuRequested, this, &SearchPanel::onTabContextMenu);
    m_tabBar->installEventFilter(this);   // middle click closes; show/hide carries the filter box

    // Filter box to the right of the tabs (MFC IDC_FILTER); one filter for every tab
    m_filterEdit = new FilterEdit(this);
    m_filterEdit->setFixedWidth(200);
    m_filterEdit->setVisible(false);
    connect(m_filterEdit, &FilterEdit::filterChanged, this,
            [this](const QStringList& tokens, int column) {
        m_filterTokens = tokens;
        m_filterColumn = column;
        applyTextFilter();
    });

    auto* tabRow = new QHBoxLayout;
    tabRow->setContentsMargins(0, 0, 0, 0);
    tabRow->addWidget(m_tabBar, 0, Qt::AlignLeft);
    tabRow->addStretch(1);
    tabRow->addWidget(m_filterEdit);
    mainLayout->addLayout(tabRow);

    // Results tree view
    m_resultView = new ListTreeView(this);
    // Decorated per tab in setupResultHeader(): eD2K lists expand to a file's names
    m_resultView->setRootIsDecorated(false);
    m_resultView->setAlternatingRowColors(true);
    m_resultView->setSortingEnabled(true);
    m_resultView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_resultView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_resultView->setAllColumnsShowFocus(true);
    // Type icon + full-size network badge / marks, not squeezed into 16 px
    m_resultView->setItemDelegateForColumn(SearchResultsModel::ColFileName,
                                           new FlagDecorationDelegate(m_resultView));
    connect(m_resultView, &QTreeView::customContextMenuRequested, this, &SearchPanel::onResultContextMenu);
    connect(m_resultView, &QTreeView::doubleClicked, this, &SearchPanel::onResultDoubleClicked);
    connect(m_resultView->verticalScrollBar(), &QScrollBar::valueChanged, this,
            &SearchPanel::loadMoreIfAtEnd);
    // A list short enough to need no scrollbar is at its end too; looked at once
    // the view has laid the rows out.
    m_loadMoreTimer = new QTimer(this);
    m_loadMoreTimer->setSingleShot(true);
    m_loadMoreTimer->setInterval(100);
    connect(m_loadMoreTimer, &QTimer::timeout, this, &SearchPanel::loadMoreIfAtEnd);
    connect(m_resultView->verticalScrollBar(), &QScrollBar::rangeChanged, m_loadMoreTimer,
            qOverload<>(&QTimer::start));

    // MFC CSearchListCtrl (srchybrid/SearchListCtrl.cpp:800, :817): Enter downloads the
    // selection, exactly as a double click does, and Alt+Enter opens the result sheet.
    // Del removes results from the list, Ctrl+C copies eD2K links (SearchListCtrl.cpp:804,
    // :1465), Ctrl+F/F3 find. The tab-level Ctrl+W lives in setupUi's shortcut.
    ListKeyHandlers keys;
    keys.activate = [this](const QModelIndex& index) {
        const auto rows = m_resultView->selectionModel()->selectedRows();
        downloadResults(rows.isEmpty() ? QModelIndexList{index} : rows);
    };
    keys.details = [this](const QModelIndex& index) { showResultDetails(index); };
    keys.remove = [this] { removeSelectedResults(); };
    keys.copy = [this] { copySelectedEd2kLinks(); };
    keys.find = true;
    bindListKeys(m_resultView, std::move(keys));

    // MFC CSearchDlg::PreTranslateMessage (SearchDlg.cpp:283): Ctrl+W closes the
    // current search tab. QKeySequence::Close is Cmd+W on macOS.
    auto* closeTabShortcut = new QShortcut(QKeySequence::Close, this);
    closeTabShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(closeTabShortcut, &QShortcut::activated, this, [this] {
        if (m_tabBar->currentIndex() >= 0)
            closeSearch(m_tabBar->currentIndex());
    });
    mainLayout->addWidget(m_resultView, 1);

    // Bottom bar — Download on the left (matches MFC)
    auto* bottomLayout = new QHBoxLayout;
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    m_downloadBtn = new QPushButton(tr("Download"), this);
    m_downloadBtn->setEnabled(false);
    connect(m_downloadBtn, &QPushButton::clicked, this, [this] {
        downloadResults(m_resultView->selectionModel()
                            ? m_resultView->selectionModel()->selectedRows()
                            : QModelIndexList{});
    });
    bottomLayout->addWidget(m_downloadBtn);
    // The category a download goes to (MFC IDC_STATIC_DLTOof + IDC_CATTAB2, emule.rc:130-132),
    // shown only once there is more than "All".
    m_downloadToLabel = new QLabel(QStringLiteral("->"), this);
    m_downloadToLabel->setVisible(false);
    bottomLayout->addWidget(m_downloadToLabel);
    m_categoryTabs = new QTabBar(this);
    m_categoryTabs->setDrawBase(false);
    m_categoryTabs->setExpanding(false);
    m_categoryTabs->setVisible(false);
    bottomLayout->addWidget(m_categoryTabs);
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
    // Not in "Automatic": an indexer search spends a paid quota and hands the
    // query to a third party, so it has to be chosen deliberately. See the note
    // on resolveAutomaticSearchType().
    m_methodCombo->addItem(QIcon(QStringLiteral(":/icons/UsenetSearch.ico")),
                           tr("Usenet (Indexer)"), 5);  // SearchType::UsenetIndexer
    // Asked of an eD2K server's catalogue (eNode Meta API): the connected server
    // if it has one, else the best known one. Not in "Automatic" either.
    m_methodCombo->addItem(QIcon(QStringLiteral(":/icons/Usenet.ico")),
                           tr("Usenet (Server)"), 6);   // SearchType::MetaUsenet
    m_methodCombo->addItem(QIcon(QStringLiteral(":/icons/Torrent.ico")),
                           tr("Torrent (Server)"), 7);  // SearchType::MetaTorrent
    typeRow->addWidget(m_methodCombo);
    m_resetBtn = new QPushButton(tr("Reset"), container);
    connect(m_resetBtn, &QPushButton::clicked, this, [this] {
        onResetFilters();
        // not in onResetFilters(): a tab double click wipes the form, not the view
        m_showUsenetCheck->setChecked(true);
        m_showKadCheck->setChecked(true);
        m_showTorrentCheck->setChecked(true);
    });
    typeRow->addWidget(m_resetBtn);

    // What the server found on other networks. The protocol cannot leave them
    // out of an answer, so these hide rows. Beside Reset, not in the filter
    // area: that one scrolls and would bury them.
    const auto addNetworkCheck = [&](const QString& text, const QString& icon, bool on) {
        auto* check = new QCheckBox(text, container);
        check->setIcon(QIcon(icon));
        check->setChecked(on);
        check->setToolTip(tr("Show results the server found on this network"));
        typeRow->addSpacing(8);
        typeRow->addWidget(check);
        connect(check, &QCheckBox::toggled, this, &SearchPanel::onNetworkFilterChanged);
        return check;
    };
    m_showUsenetCheck = addNetworkCheck(tr("Usenet results"), QStringLiteral(":/icons/Usenet.ico"),
                                        theUiState.searchShowUsenet());
    m_showKadCheck = addNetworkCheck(tr("Kad results"), QStringLiteral(":/icons/Kad.ico"),
                                     theUiState.searchShowKad());
    m_showTorrentCheck = addNetworkCheck(tr("Torrent results"), QStringLiteral(":/icons/Torrent.ico"),
                                         theUiState.searchShowTorrent());
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

    filterLayout->addWidget(new QLabel(tr("Min. Bitrate [Kbit/s]:"), m_filterWidget), 6, 0);
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
    // Column 1: Start, More, Cancel stacked as in MFC (srchybrid/emule.rc:1144-1146)
    auto* buttonCol = new QVBoxLayout;
    buttonCol->setSpacing(2);
    m_startBtn = new QPushButton(tr("Start"), container);
    m_startBtn->setFixedWidth(80);
    connect(m_startBtn, &QPushButton::clicked, this, &SearchPanel::onStartSearch);
    buttonCol->addWidget(m_startBtn);

    // The next page of the tab on screen; scrolling to the end of the list asks
    // for it too (loadMoreIfAtEnd()). MFC: CSearchParamsWnd::OnBnClickedMore.
    m_moreBtn = new QPushButton(tr("More"), container);
    m_moreBtn->setFixedWidth(80);
    m_moreBtn->setEnabled(false);
    m_moreBtn->setToolTip(tr("Ask the server for further results of this search"));
    connect(m_moreBtn, &QPushButton::clicked, this, [this] {
        if (auto* tab = currentTab())
            requestMore(*tab);
    });
    buttonCol->addWidget(m_moreBtn);

    m_cancelBtn = new QPushButton(tr("Cancel"), container);
    m_cancelBtn->setFixedWidth(80);
    m_cancelBtn->setEnabled(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &SearchPanel::onCancelSearch);
    buttonCol->addWidget(m_cancelBtn);
    buttonCol->addStretch();
    grid->addLayout(buttonCol, 0, 1, 2, 1);

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

    if (req.method == static_cast<int>(SearchType::UsenetIndexer)) {
        sendIndexerSearchRequest(req);
        return;
    }
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
                       [this, req, expression, method, tabTitle](const IpcMessage& resp) {
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

        // The same search asked for again while it still waits or runs: the daemon
        // hands back the one it has.
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            if (m_tabs[i].searchID == searchID && !m_tabs[i].isIndexer()) {
                m_tabBar->setCurrentIndex(static_cast<int>(i));
                return;
            }
        }

        // Create new tab
        SearchTab tab;
        tab.searchID = searchID;
        tab.title = tabTitle;
        tab.method = resolvedMethod;
        tab.request = req;
        tab.model = new SearchResultsModel(this);
        // Not connected yet, or another server search is still out: it is queued and
        // sent by the daemon when it can be.
        const auto stateValue = data.value(QStringLiteral("state"));
        tab.runState = stateValue.isInteger() ? static_cast<int>(stateValue.toInteger()) : 1;
        tab.waitReason = data.value(QStringLiteral("reason")).toString();
        m_tabBar->setCurrentIndex(addResultTab(std::move(tab)));
        m_cancelBtn->setEnabled(true);
        m_statusLabel->setText(tabStatusText(m_tabs[static_cast<size_t>(m_tabBar->currentIndex())]));

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

QString SearchPanel::tabStatusText(const SearchTab& tab) const
{
    if (tab.isIndexer() || tab.clientSharedFiles)
        return QStringLiteral("%1 results").arg(tab.resultCount());

    switch (tab.runState) {
    case 0:   // queued
        if (tab.waitReason == QLatin1String("waiting-for-server-connection"))
            return tr("Queued — waiting for a server connection");
        if (tab.waitReason == QLatin1String("waiting-for-kad"))
            return tr("Queued — waiting for Kad to connect");
        if (tab.waitReason == QLatin1String("waiting-for-connection"))
            return tr("Queued — waiting for a server or Kad connection");
        if (tab.waitReason == QLatin1String("waiting-for-previous-search"))
            return tr("Queued — waiting for the previous search to finish");
        if (tab.waitReason == QLatin1String("waiting-for-server-info"))
            return tr("Queued — waiting for the server to say what it offers");
        return tr("Queued");
    case 3:   // failed
        return tr("Search failed: %1").arg(tab.failure);
    case 2:   // finished
        if (tab.hasMore)
            return tr("%1 results — scroll down for more").arg(tab.resultCount());
        if (tab.resultCount() == 0 && tab.searchID != 0)
            return tr("No results");
        break;
    default:
        break;
    }
    if (const auto* proxy = qobject_cast<const SearchResultsProxy*>(tab.proxy);
        proxy && proxy->hiddenCount() > 0)
        return tr("%1 of %2 results").arg(proxy->rowCount()).arg(tab.resultCount());
    return QStringLiteral("%1 results").arg(tab.resultCount());
}

void SearchPanel::onSearchStatePush(const IpcMessage& msg)
{
    const auto searchID = static_cast<uint32_t>(msg.fieldInt(0));
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        SearchTab& tab = m_tabs[i];
        // ED2K and indexer searches number their ids independently
        if (tab.searchID != searchID || tab.isIndexer() || tab.clientSharedFiles)
            continue;

        const int before = tab.runState;
        tab.runState = static_cast<int>(msg.fieldInt(1));
        tab.waitReason = msg.fieldString(2);
        tab.failure = msg.fieldString(3);
        tab.hasMore = tab.runState == 2 && msg.fieldBool(7);
        // "Automatic" is resolved when the search is sent, which may be now.
        if (tab.runState == 1) {
            tab.method = static_cast<int>(msg.fieldInt(4));
            applyNetworkFilter(tab);
        }

        if (tab.runState == 3 && before != 3)
            StatusBarNotifier::post(tr("Search \"%1\" failed: %2").arg(tab.title, tab.failure), 8000);

        if (m_tabBar->currentIndex() == static_cast<int>(i)) {
            m_statusLabel->setText(tabStatusText(tab));
            m_cancelBtn->setEnabled(tab.runState == 0 || tab.runState == 1);
            updateMoreButton();
            if (tab.hasMore)
                m_loadMoreTimer->start();   // a page that added no row moves no scrollbar
        }
        return;
    }
}

void SearchPanel::showClientSharedFiles(uint32_t searchID, const QString& userName)
{
    if (searchID == 0)
        return;

    // Same search-ID space as ED2K/Kad tabs, separate from the indexer's.
    const auto it = std::ranges::find_if(m_tabs, [searchID](const SearchTab& tab) {
        return !tab.isIndexer() && tab.searchID == searchID;
    });
    int idx = static_cast<int>(std::distance(m_tabs.begin(), it));
    if (it == m_tabs.end()) {
        SearchTab tab;
        tab.searchID = searchID;
        tab.title = userName.isEmpty() ? QStringLiteral("-") : userName;
        tab.clientSharedFiles = true;
        tab.finished = true;   // nothing to cancel; answers just land
        tab.model = new SearchResultsModel(this);
        idx = addResultTab(std::move(tab));
    }
    m_tabBar->setCurrentIndex(idx);
    requestSearchResults(searchID);
}

int SearchTab::resultCount() const
{
    if (indexerModel)
        return indexerModel->resultCount();
    return model ? model->resultCount() : 0;
}

// ---------------------------------------------------------------------------
// Indexer search — the shared newznab client
//
// A separate request from StartSearch, because StartSearch's payload is
// ED2K-shaped: fileType, availability and complete-source counts mean nothing to
// an indexer, and the daemon would have to guess which network was meant.
// ---------------------------------------------------------------------------

void SearchPanel::sendIndexerSearchRequest(const SearchRequest& req)
{
    if (!m_ipc || !m_ipc->isConnected()) {
        StatusBarNotifier::post(tr("Not connected to daemon — search cannot be started."), 4000);
        return;
    }
    if (req.expression.isEmpty())
        return;

    IpcMessage msg(IpcMsgType::StartIndexerSearch);
    msg.append(req.expression);      // field 0 — the keywords
    msg.append(QCborArray{});        // field 1 — categories; the form has none yet
    msg.append(QString{});           // field 2 — mode; empty means plain "search"
    msg.append(QCborArray{});        // field 3 — indexers; empty means all enabled

    const QString tabTitle = req.tabTitle.isEmpty() ? req.expression : req.tabTitle;

    m_ipc->sendRequest(std::move(msg), [this, req, tabTitle](const IpcMessage& resp) {
        if (!resp.fieldBool(0)) {
            // "No search indexer is configured" is the common case here, and it
            // has to be said out loud — an empty result list reads as "nothing
            // matched" when the truth is that nothing was asked.
            const QString error = resp.fieldString(1);
            if (!error.isEmpty())
                QMessageBox::warning(this, tr("Usenet search"), error);
            return;
        }

        const auto searchID = static_cast<uint32_t>(resp.fieldInt(1));

        SearchTab tab;
        tab.searchID = searchID;
        tab.title = tabTitle;
        tab.method = static_cast<int>(SearchType::UsenetIndexer);
        tab.request = req;
        tab.indexerModel = new IndexerResultsModel(this);
        m_tabBar->setCurrentIndex(addResultTab(std::move(tab)));
        m_cancelBtn->setEnabled(true);
    });
}

SearchTab* SearchPanel::tabForIndexerSearch(uint32_t searchID)
{
    // Both kinds number their searches from their own counter, so an id alone is
    // ambiguous — an ED2K search and an indexer search can both be #1. Matching
    // the kind as well is what keeps an ED2K push from refreshing an indexer tab
    // through a model it does not have.
    for (auto& tab : m_tabs) {
        if (tab.isIndexer() && tab.searchID == searchID)
            return &tab;
    }
    return nullptr;
}

void SearchPanel::onIndexerResultsPush(const IpcMessage& msg)
{
    const auto searchID = static_cast<uint32_t>(msg.fieldInt(0));
    SearchTab* tab = tabForIndexerSearch(searchID);
    if (!tab)
        return;

    std::vector<IndexerResultRow> rows;
    const auto arr = msg.fieldArray(1);
    rows.reserve(static_cast<size_t>(arr.size()));

    for (const auto& value : arr) {
        if (!value.isMap())
            continue;
        const auto map = value.toMap();

        IndexerResultRow row;
        row.id          = map.value(QStringLiteral("id")).toString();
        row.indexerName = map.value(QStringLiteral("indexer")).toString();
        row.title       = map.value(QStringLiteral("title")).toString();
        row.size        = map.value(QStringLiteral("size")).toInteger();
        row.published   = map.value(QStringLiteral("published")).toInteger();
        row.ageDays     = static_cast<int>(map.value(QStringLiteral("ageDays")).toInteger(-1));
        row.category    = map.value(QStringLiteral("category")).toString();
        row.grabs       = static_cast<int>(map.value(QStringLiteral("grabs")).toInteger(-1));
        row.files       = static_cast<int>(map.value(QStringLiteral("files")).toInteger(-1));
        row.passwordProtected = map.value(QStringLiteral("password")).toBool(false);
        row.seeders     = static_cast<int>(map.value(QStringLiteral("seeders")).toInteger(-1));
        row.peers       = static_cast<int>(map.value(QStringLiteral("peers")).toInteger(-1));
        row.isUsenet    = map.value(QStringLiteral("isUsenet")).toBool(true);
        row.confidence  = map.value(QStringLiteral("confidence")).toString();
        row.fakeScore   = static_cast<int>(map.value(QStringLiteral("fakeScore")).toInteger());
        for (const auto& reason : map.value(QStringLiteral("fakeReasons")).toArray())
            row.fakeReasons.push_back(reason.toString());
        rows.push_back(row);
    }

    // Append rather than reset: rows arrive per indexer, and a reset on every
    // batch would throw away the user's selection each time another one replied.
    tab->indexerModel->addResults(rows);
    scheduleSaveSearches();

    const int tabIndex = int(tab - m_tabs.data());
    m_tabBar->setTabText(tabIndex, tabLabel(*tab));
    if (tabIndex == m_tabBar->currentIndex()) {
        m_statusLabel->setText(QStringLiteral("%1 results")
                                   .arg(tab->indexerModel->resultCount()));
    }
}

void SearchPanel::onIndexerProgressPush(const IpcMessage& msg)
{
    const auto searchID = static_cast<uint32_t>(msg.fieldInt(0));
    const SearchTab* tab = tabForIndexerSearch(searchID);
    if (!tab || m_tabBar->currentIndex() != int(tab - m_tabs.data()))
        return;

    const int done = static_cast<int>(msg.fieldInt(1));
    const int total = static_cast<int>(msg.fieldInt(2));
    if (done >= total)
        return;

    m_statusLabel->setText(tr("%1 results — %2 of %3 indexers")
                               .arg(tab->resultCount()).arg(done).arg(total));
}

void SearchPanel::onIndexerFinishedPush(const IpcMessage& msg)
{
    const auto searchID = static_cast<uint32_t>(msg.fieldInt(0));
    SearchTab* tab = tabForIndexerSearch(searchID);
    if (!tab)
        return;

    tab->finished = true;
    m_cancelBtn->setEnabled(false);

    // An error here is per-indexer and rarely fatal — with three configured, one
    // being down still leaves two thirds of the results — so it goes to the
    // status bar rather than into a modal the user has to dismiss.
    if (const QString error = msg.fieldString(1); !error.isEmpty())
        StatusBarNotifier::post(tr("Usenet search: %1").arg(error), 8000);

    if (m_tabBar->currentIndex() == int(tab - m_tabs.data())) {
        m_statusLabel->setText(tab->resultCount() == 0
                                   ? tr("No results")
                                   : QStringLiteral("%1 results").arg(tab->resultCount()));
    }
}

void SearchPanel::requestCategories()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    m_ipc->sendRequest(IpcMessage(IpcMsgType::GetCategories), [this](const IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;
        m_categoryTitles.clear();
        for (const auto& value : resp.fieldArray(1)) {
            if (value.isMap())
                m_categoryTitles.append(value.toMap().value(QStringLiteral("title")).toString());
        }
        updateCategoryTabs();
    });
}

void SearchPanel::sendIndexerGrab(int proxyRow, bool force, int category)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    auto* tab = currentTab();
    if (!tab || !tab->isIndexer())
        return;

    const auto srcIdx = tab->proxy->mapToSource(tab->proxy->index(proxyRow, 0));
    const int srcRow = srcIdx.row();
    const auto* result = tab->indexerModel->resultAt(srcRow);
    if (!result)
        return;

    IpcMessage msg(IpcMsgType::GrabIndexerResult);
    msg.append(static_cast<qint64>(tab->searchID));
    msg.append(result->id);
    // Asked and answered. Carried through the fetch so a yes does not spend a
    // second of the indexer's daily grabs re-asking the same question.
    msg.append(force);
    // Download To picked one; plain Download sends 0, which the daemon reads as
    // "nobody chose" and auto-categorises.
    msg.append(qint64(category));

    const QString title = result->title;
    // The model, not the tab index and row: tabs close and results refresh while the
    // reply is out, and a stale index marks some other row — or no model at all.
    const QPointer<IndexerResultsModel> model(tab->indexerModel);
    m_ipc->sendRequest(std::move(msg), [this, title, model](const IpcMessage& resp) {
        if (!resp.isValid())
            return;   // connection dropped: nothing was queued, and nothing refused
        if (!resp.fieldBool(0)) {
            // The daemon has already decided; a refusal reaching here is a real
            // failure, because everything the user could have been asked about was
            // asked before the grab was sent.
            //
            // One event-loop turn first: a modal opened inside the stack that
            // delivered this reply spins a nested loop, and a quit arriving during
            // it unwinds every loop at once — main() destroys the IpcClient and its
            // socket while the socket read is still below us. Same rule, and same
            // reason, as Ed2kLinkImporter's.
            const QString reason = resp.fieldString(1);
            QPointer<SearchPanel> self(this);
            QTimer::singleShot(0, qApp, [self, title, reason] {
                if (self) {
                    QMessageBox::warning(self, tr("Usenet search"),
                                         tr("Could not queue \"%1\": %2").arg(title, reason));
                }
            });
            return;
        }
        if (model)
            model->updateKnownTypes({{title, 2}});   // Downloading
        StatusBarNotifier::post(tr("Queued \"%1\" for download from Usenet.").arg(title), 5000);
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

    IpcMessage msg(tab->isIndexer() ? IpcMsgType::StopIndexerSearch
                                    : IpcMsgType::StopSearch);
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

void SearchPanel::onNetworkFilterChanged()
{
    theUiState.setSearchShowNetworks(m_showUsenetCheck->isChecked(), m_showKadCheck->isChecked(),
                                     m_showTorrentCheck->isChecked());
    for (SearchTab& tab : m_tabs)
        applyNetworkFilter(tab);
    if (const auto* tab = currentTab()) {
        m_statusLabel->setText(tabStatusText(*tab));
        updateDownloadButton();
    }
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
// Slot: Tab double-clicked (MFC OnDblClickTab)
// ---------------------------------------------------------------------------

void SearchPanel::onTabDoubleClicked(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size()))
        return;
    const SearchTab& tab = m_tabs[static_cast<size_t>(index)];
    // a peer's file list was not a search: MFC leaves the form wiped
    applyRequestToUi(tab.clientSharedFiles ? SearchRequest{} : tab.request);
}

void SearchPanel::onTabContextMenu(const QPoint& pos)
{
    // MFC CSearchResultsWnd::OnContextMenu (SearchResultsWnd.cpp:1719-1736)
    const int index = m_tabBar->tabAt(pos);
    if (index < 0)
        return;

    QMenu menu(this);
    auto* restoreAct = menu.addAction(tr("Restore Search Parameters"), this,
                                      [this, index] { onTabDoubleClicked(index); });
    setMenuDefaultAction(&menu, restoreAct);
    menu.addAction(tr("Close"), this, [this, index] { closeSearch(index); });
    menu.exec(m_tabBar->mapToGlobal(pos));
}

bool SearchPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_tabBar) {
        // MFC CClosableTabCtrl: a middle click closes the tab under it
        if (event->type() == QEvent::MouseButtonRelease) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::MiddleButton) {
                if (const int index = m_tabBar->tabAt(mouse->position().toPoint()); index >= 0) {
                    closeSearch(index);
                    return true;
                }
            }
        } else if (event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent) {
            m_filterEdit->setVisible(event->type() == QEvent::ShowToParent);
        }
    }
    return QWidget::eventFilter(watched, event);
}

QString SearchPanel::tabLabel(const SearchTab& tab) const
{
    // MFC shows "title (shown/total)" while the filter box hides rows
    // (SearchListCtrl.cpp:421-424).
    const int total = tab.resultCount();
    if (!m_filterTokens.isEmpty() && tab.proxy && tab.proxy->rowCount() != total)
        return QStringLiteral("%1 (%2/%3)").arg(tab.title).arg(tab.proxy->rowCount()).arg(total);
    return QStringLiteral("%1 (%2)").arg(tab.title).arg(total);
}

void SearchPanel::applyTextFilter()
{
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        auto& tab = m_tabs[i];
        auto* proxy = qobject_cast<SearchResultsProxy*>(tab.proxy);
        if (!proxy || !proxy->sourceModel())
            continue;
        // The column is one of the list on screen; another kind of list filters its first
        const bool sameKind = currentTab() && currentTab()->isIndexer() == tab.isIndexer();
        const int column = sameKind && m_filterColumn < proxy->sourceModel()->columnCount()
            ? m_filterColumn : 0;
        proxy->setTextFilter(m_filterTokens, column);
        m_tabBar->setTabText(static_cast<int>(i), tabLabel(tab));
    }
    if (auto* tab = currentTab())
        m_statusLabel->setText(tabStatusText(*tab));
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

    // An indexer tab gets its own short menu. Almost nothing below applies: an
    // indexer row has no ED2K hash, so there is no link to copy, no spam flag to
    // set, no source count to ask about and no detail sheet to open.
    if (auto* indexerTab = currentTab(); indexerTab && indexerTab->isIndexer()) {
        QAction* grab = m_contextMenu->addAction(tr("&Download"));
        grab->setEnabled(hasSelection);
        // read at trigger time: a result push may reset the model while the menu is up
        connect(grab, &QAction::triggered, this,
                [this] { downloadResults(m_resultView->selectionModel()->selectedRows()); });

        // One click for "just download it", one submenu for "and file it there".
        // Priority and paused are not offered here: the queue's own menu covers
        // both a click later, and a grab is usually something you want now.
        if (m_categoryTitles.size() > 1) {
            QMenu* toMenu = m_contextMenu->addMenu(tr("Download &To"));
            toMenu->setEnabled(hasSelection);
            for (int i = 1; i < m_categoryTitles.size(); ++i) {
                const QString title = m_categoryTitles.at(i);
                connect(toMenu->addAction(title), &QAction::triggered, this,
                        [this, i] { downloadResults(m_resultView->selectionModel()->selectedRows(), i); });
            }
        }

        QAction* copyName = m_contextMenu->addAction(tr("Copy &Name"));
        copyName->setEnabled(singleSel);
        connect(copyName, &QAction::triggered, this, [this] {
            auto* tab = currentTab();
            const auto selection = m_resultView->selectionModel()->selectedRows();
            if (!tab || !tab->isIndexer() || selection.isEmpty())
                return;
            const auto src = tab->proxy->mapToSource(selection.first());
            if (const auto* row = tab->indexerModel->resultAt(src.row()))
                QApplication::clipboard()->setText(row->title);
        });

        m_contextMenu->popup(m_resultView->viewport()->mapToGlobal(pos));
        return;
    }

    // Walk the selection once, as MFC does (SearchListCtrl.cpp:680-693): whether
    // anything is still downloadable, and whether anything is not yet spam — the
    // latter decides which way the spam item reads.
    auto* tab = currentTab();
    bool anyNotSpam    = false;
    bool anyDownloadable = false;
    bool anyEd2k = false;
    bool anyUsenet = false;
    bool anyTorrent = false;
    bool anyMagnet = false;
    bool singleMeta = false;
    QString singleHash;
    QString singleName;
    int64_t singleSize = 0;
    int peerPreviews = 0;
    QString peerPreviewHash;
    for (const auto& idx : selection) {
        if (!tab)
            break;
        const auto* result = refAt(idx).row;
        if (!result)
            continue;
        if (!result->isSpam)
            anyNotSpam = true;
        if (result->previewPossible) {
            ++peerPreviews;
            peerPreviewHash = result->hash;
        }
        anyEd2k |= !result->isMeta();
        anyUsenet |= result->isUsenet();
        anyTorrent |= result->isTorrent();
        anyMagnet |= !result->isMeta() || !result->magnet.isEmpty();
        // eD2K rows: not already in the transfer list. Usenet rows: the daemon asks.
        if (result->isUsenet() || (!result->isMeta()
                                   && (!m_downloadModel || !m_downloadModel->findByHash(result->hash))))
            anyDownloadable = true;
        if (singleSel) {
            singleHash = result->hash;
            singleName = result->fileName;
            singleSize = result->fileSize;
            singleMeta = result->isMeta();
        }
    }

    // Download — the default action, bold, as in MFC (SetDefaultItem at :731).
    // Queues eD2K rows and Usenet rows (eMuleQt's Usenet downloader).
    auto* downloadAction = m_contextMenu->addAction(menuIcon("Download.ico"), tr("Download"));
    downloadAction->setEnabled(hasSelection && anyDownloadable);
    // Advanced mode: Download starts it running, "Download (Paused)" paused, and the
    // bold one is what the option says. Otherwise the option decides alone
    // (MFC SearchListCtrl.cpp:727, :791-796, :907-911).
    const bool ext = thePrefs.showExtControls();
    connect(downloadAction, &QAction::triggered, this, [this, ext] {
        downloadResults(m_resultView->selectionModel()->selectedRows(), -1,
                        ext ? std::optional<bool>(false) : std::nullopt);
    });
    QAction* defaultAction = downloadAction->isEnabled() ? downloadAction : nullptr;
    if (ext && anyEd2k) {
        auto* pausedAction = m_contextMenu->addAction(
            menuIcon("Download.ico"), tr("%1 (%2)").arg(tr("Download"), tr("Paused")));
        pausedAction->setEnabled(downloadAction->isEnabled());
        connect(pausedAction, &QAction::triggered, this, [this] {
            downloadResults(m_resultView->selectionModel()->selectedRows(), -1, true);
        });
        if (pausedAction->isEnabled() && thePrefs.addNewFilesPaused())
            defaultAction = pausedAction;
    }

    // eNode Usenet rows: the .nzb itself
    if (anyUsenet) {
        auto* nzbAction = m_contextMenu->addAction(QIcon(QStringLiteral(":/icons/Usenet.ico")),
                                                   tr("Download NZB File..."));
        connect(nzbAction, &QAction::triggered, this, [this] { saveMetaFiles(/*nzb*/ true); });
    }
    // eNode torrent rows: the .torrent — the default for them until BitTorrent lands
    if (anyTorrent) {
        auto* torrentAction = m_contextMenu->addAction(QIcon(QStringLiteral(":/icons/Torrent.ico")),
                                                       tr("Download Torrent"));
        connect(torrentAction, &QAction::triggered, this, [this] { saveMetaFiles(/*nzb*/ false); });
        if (!anyEd2k && !anyUsenet)
            defaultAction = torrentAction;
    }
    setMenuDefaultAction(m_contextMenu, defaultAction);

    // Details... — extended controls only, exactly as MFC gates it, because the
    // sheet's Metadata page is itself an extended-controls feature.
    if (thePrefs.showExtControls()) {
        auto* detailsAction = m_contextMenu->addAction(menuIcon("FileInfo.ico"), tr("Details..."));
        detailsAction->setEnabled(singleSel && tab && !singleMeta);
        const uint32_t searchID = tab ? tab->searchID : 0;
        connect(detailsAction, &QAction::triggered, this, [this, searchID, singleHash] {
            fetchAndShowSearchDetails(searchID, singleHash, SearchDetailDialog::Metadata);
        });
    }

    // Comments... — the same sheet, forced onto its Comments page (MFC passes
    // IDD_COMMENTLST for MP_CMT, SearchListCtrl.cpp:817-824).
    {
        auto* commentsAction = m_contextMenu->addAction(menuIcon("FileComments.ico"), tr("Comments..."));
        commentsAction->setEnabled(singleSel && tab && !singleMeta);
        const uint32_t searchID = tab ? tab->searchID : 0;
        connect(commentsAction, &QAction::triggered, this, [this, searchID, singleHash] {
            fetchAndShowSearchDetails(searchID, singleHash, SearchDetailDialog::Comments);
        });
    }

    m_contextMenu->addSeparator();

    // Copy eD2K Links
    // meta rows have no eD2K link to give (never mint ed2k:// for a pseudo-hash)
    auto* copyLinkAction = m_contextMenu->addAction(menuIcon("eD2kLink.ico"), tr("Copy eD2K Links"));
    copyLinkAction->setEnabled(hasSelection && anyEd2k);
    connect(copyLinkAction, &QAction::triggered, this, &SearchPanel::copySelectedEd2kLinks);

    // Copy eD2K Links (HTML)
    auto* copyHtmlAction = m_contextMenu->addAction(menuIcon("Copy.ico"), tr("Copy eD2K Links (HTML)"));
    copyHtmlAction->setEnabled(hasSelection && anyEd2k);
    connect(copyHtmlAction, &QAction::triggered, this, [this] {
        QStringList links;
        for (const auto& i : m_resultView->selectionModel()->selectedRows()) {
            const QString link = buildEd2kLink(i);
            if (link.isEmpty()) continue;
            if (const SearchResultRef ref = refAt(i))
                links << QStringLiteral("<a href=\"%1\">%2</a>").arg(link, ref.fileName().toHtmlEscaped());
        }
        if (!links.isEmpty())
            QApplication::clipboard()->setText(links.join(QStringLiteral("<br>\n")));
    });

    // Copy Magnet Links — eD2K rows as urn:ed2k, torrents their own; Usenet has none
    auto* copyMagnetAction = m_contextMenu->addAction(menuIcon("eD2kLink.ico"), tr("Copy Magnet Links"));
    copyMagnetAction->setEnabled(hasSelection && anyMagnet);
    connect(copyMagnetAction, &QAction::triggered, this, [this] {
        QStringList links;
        for (const auto& i : m_resultView->selectionModel()->selectedRows())
            if (const QString link = buildMagnetLink(i); !link.isEmpty())
                links << link;
        if (!links.isEmpty())
            QApplication::clipboard()->setText(links.join(QLatin1Char('\n')));
    });

    // Mark as Spam / Mark as not Spam — MFC inserts this right before Remove, with
    // no separator of its own, and only when the search spam filter is enabled
    // (SearchListCtrl.cpp:716-720). The label points at what the click will do.
    if (thePrefs.enableSearchResultFilter()) {
        const bool markAsSpam = anyNotSpam || !hasSelection;
        auto* spamAction = m_contextMenu->addAction(
            menuIcon("Spam.ico"), markAsSpam ? tr("Mark as Spam") : tr("Mark as not Spam"));
        spamAction->setEnabled(hasSelection);
        connect(spamAction, &QAction::triggered, this, [this, markAsSpam] {
            auto* tab = currentTab();
            if (!tab || !m_ipc) return;
            for (const auto& i : m_resultView->selectionModel()->selectedRows()) {
                const auto srcIdx = tab->proxy->mapToSource(i);
                const auto* result = tab->model->resultAt(srcIdx).row;
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
    auto* removeAction = m_contextMenu->addAction(menuIcon("ListRemove.ico"), tr("Remove"));
    removeAction->setEnabled(hasSelection);
    connect(removeAction, &QAction::triggered, this, &SearchPanel::removeSelectedResults);

    m_contextMenu->addSeparator();

    // Close Search Results
    auto* closeAction = m_contextMenu->addAction(menuIcon("CloseTab.ico"), tr("Close Search Results"));
    closeAction->setEnabled(m_tabBar->currentIndex() >= 0);
    connect(closeAction, &QAction::triggered, this, [this] {
        if (m_tabBar->currentIndex() >= 0)
            closeSearch(m_tabBar->currentIndex());
    });

    // Close All Search Results
    auto* closeAllAction = m_contextMenu->addAction(menuIcon("DeleteAll.ico"), tr("Close All Search Results"));
    closeAllAction->setEnabled(!m_tabs.empty());
    connect(closeAllAction, &QAction::triggered, this, &SearchPanel::closeAllSearches);

    m_contextMenu->addSeparator();

    // Preview — MFC inserts it here, immediately above Find, and only when exactly
    // one previewable file is selected (SearchListCtrl.cpp:710-713).
    bool streamPreview = false;
    if (singleSel && m_downloadModel && !m_streamToken.isEmpty()) {
        const auto* dl = m_downloadModel->findByHash(singleHash);
        if (dl && dl->isPreviewPossible) {
            streamPreview = true;
            connect(m_contextMenu->addAction(menuIcon("Preview.ico"), tr("Preview")),
                    &QAction::triggered, this, [this, singleHash] { sendPreview(singleHash); });
        }
    }
    // A browsed peer's file: ask the peer for a few frames.
    if (!streamPreview && tab && peerPreviews == 1) {
        const uint32_t searchID = tab->searchID;
        connect(m_contextMenu->addAction(menuIcon("Preview.ico"), tr("Preview")),
                &QAction::triggered, this,
                [this, searchID, peerPreviewHash] { requestPeerPreview(searchID, peerPreviewHash); });
    }

    // Find... — over the whole result list, so selection is irrelevant.
    auto* findAction = m_contextMenu->addAction(menuIcon("Search.ico"), tr("Find..."));
    findAction->setEnabled(tab && tab->proxy->rowCount() > 0);
    connect(findAction, &QAction::triggered, this,
            [this] { showFindInListDialog(this, m_resultView); });

    // Search Related Files — the whole selection, when the server answers it
    // (MFC SearchListCtrl / CanSearchRelatedFiles).
    auto* relatedAction = m_contextMenu->addAction(menuIcon("KadFileSearch.ico"),
                                                   tr("Search Related Files"));
    // Without a server that answers it, one file can still be searched by name
    relatedAction->setEnabled(hasSelection && anyEd2k
                              && (m_relatedSearchSupported || (singleSel && !singleMeta)));
    connect(relatedAction, &QAction::triggered, this, [this, singleName] {
        if (!m_relatedSearchSupported) {
            const qsizetype dotIdx = singleName.lastIndexOf(QLatin1Char('.'));
            startSearchFromExternal(dotIdx > 0 ? singleName.left(dotIdx) : singleName);
            return;
        }
        QStringList hashes;
        QStringList names;
        for (const auto& i : m_resultView->selectionModel()->selectedRows()) {
            const SearchResultRef ref = refAt(i);
            if (ref && !ref.row->isMeta() && !hashes.contains(ref.row->hash)) {
                hashes << ref.row->hash;
                names << ref.row->fileName;
            }
        }
        startRelatedSearch(hashes, names);
    });

    // Web Services — greyed when webservices.dat is empty or the selection is not
    // exactly one file, matching MFC's flag2 (SearchListCtrl.cpp:724-727).
    auto* webMenu = m_contextMenu->addMenu(menuIcon("Web.ico"), tr("Web Services"));
    if (singleSel && !singleMeta)   // web services key on the eD2K hash
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

    // The detail sheet is built on a SearchFile — hash, sources, ED2K media tags.
    // An indexer row has none of them.
    if (tab->isIndexer())
        return;

    const auto* result = refAt(index).row;
    if (!result)
        return;

    fetchAndShowSearchDetails(tab->searchID, result->hash, SearchDetailDialog::Metadata);
}

void SearchPanel::onResultDoubleClicked(const QModelIndex& index)
{
    if (index.isValid())
        downloadResults({index});
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

    m_fetchingSearchIDs.insert(searchID);
    m_ipc->sendRequest(std::move(msg), [this, searchID](const IpcMessage& resp) {
        m_fetchingSearchIDs.remove(searchID);
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
            row.isKad              = m.value(QStringLiteral("isKad")).toBool();
            row.kadOrigin          = m.value(QStringLiteral("kadOrigin")).toBool();
            row.inDirectory        = m.value(QStringLiteral("inDirectory")).toBool();
            row.fileType           = m.value(QStringLiteral("fileType")).toString();
            row.knownType          = static_cast<int>(m.value(QStringLiteral("knownType")).toInteger());
            row.seenBefore         = m.value(QStringLiteral("seenBefore")).toBool();
            row.seenNames          = static_cast<int>(m.value(QStringLiteral("seenNames")).toInteger());
            row.firstSeen          = m.value(QStringLiteral("firstSeen")).toInteger();
            row.isSpam             = m.value(QStringLiteral("isSpam")).toBool();
            row.hasComment         = m.value(QStringLiteral("hasComment")).toBool();
            row.previewPossible    = m.value(QStringLiteral("previewPossible")).toBool();
            row.userRating         = static_cast<int>(m.value(QStringLiteral("userRating")).toInteger());
            row.confidence         = m.value(QStringLiteral("confidence")).toString();
            row.fakeScore          = static_cast<int>(m.value(QStringLiteral("fakeScore")).toInteger());
            for (const auto& reason : m.value(QStringLiteral("fakeReasons")).toArray())
                row.fakeReasons.push_back(reason.toString());
            row.artist             = m.value(QStringLiteral("artist")).toString();
            row.album              = m.value(QStringLiteral("album")).toString();
            row.title              = m.value(QStringLiteral("title")).toString();
            row.length             = m.value(QStringLiteral("length")).toInteger();
            row.bitrate            = m.value(QStringLiteral("bitrate")).toInteger();
            row.codec              = m.value(QStringLiteral("codec")).toString();
            row.metaKind           = static_cast<int>(m.value(QStringLiteral("metaKind")).toInteger());
            row.magnet             = m.value(QStringLiteral("magnet")).toString();
            row.metaAgeDays        = m.value(QStringLiteral("metaAge")).toInteger();
            row.metaIndexer        = m.value(QStringLiteral("metaIndexer")).toString();
            row.metaCatalogId      = m.value(QStringLiteral("metaCatalogId")).toString();
            row.metaServers        = m.value(QStringLiteral("metaServers")).toArray();
            row.directory          = m.value(QStringLiteral("directory")).toString();
            row.aichHash           = m.value(QStringLiteral("aichHash")).toString();
            row.kadPublishers      = static_cast<int>(m.value(QStringLiteral("kadPublishers")).toInteger());
            row.clientCount        = static_cast<int>(m.value(QStringLiteral("clientCount")).toInteger());
            for (const auto& childVal : m.value(QStringLiteral("children")).toArray()) {
                const auto c = childVal.toMap();
                SearchChildRow child;
                child.fileName    = c.value(QStringLiteral("fileName")).toString();
                child.sourceCount = c.value(QStringLiteral("sourceCount")).toInteger();
                child.directory   = c.value(QStringLiteral("directory")).toString();
                child.aichHash    = c.value(QStringLiteral("aichHash")).toString();
                child.artist      = c.value(QStringLiteral("artist")).toString();
                child.album       = c.value(QStringLiteral("album")).toString();
                child.title       = c.value(QStringLiteral("title")).toString();
                child.length      = c.value(QStringLiteral("length")).toInteger();
                child.bitrate     = c.value(QStringLiteral("bitrate")).toInteger();
                child.codec       = c.value(QStringLiteral("codec")).toString();
                row.children.push_back(std::move(child));
            }
            rows.push_back(std::move(row));
        }

        // Find the matching tab and update
        for (size_t i = 0; i < m_tabs.size(); ++i) {
            // ED2K and indexer searches number their ids independently
            if (m_tabs[i].searchID == searchID && !m_tabs[i].isIndexer()) {
                // Matched by hash inside the model: selection and expanded files stay
                m_tabs[i].model->setResults(std::move(rows));

                // Update tab text with result count
                m_tabBar->setTabText(static_cast<int>(i), tabLabel(m_tabs[i]));
                // The footer only refreshed on tab switch, so it lagged behind the rows
                if (m_tabBar->currentIndex() == static_cast<int>(i)) {
                    m_statusLabel->setText(tabStatusText(m_tabs[i]));
                    m_loadMoreTimer->start();
                }

                scheduleSaveSearches();
                break;
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Download a result
// ---------------------------------------------------------------------------

SearchResultRef SearchPanel::refAt(const QModelIndex& proxyIndex) const
{
    auto* tab = const_cast<SearchPanel*>(this)->currentTab();
    if (!tab || tab->isIndexer() || !tab->model || !proxyIndex.isValid())
        return {};
    return tab->model->resultAt(tab->proxy->mapToSource(proxyIndex.siblingAtColumn(0)));
}

void SearchPanel::downloadResults(const QModelIndexList& proxyRows, int category,
                                  std::optional<bool> paused)
{
    if (!m_ipc || !m_ipc->isConnected() || proxyRows.isEmpty())
        return;

    auto* tab = currentTab();
    if (!tab)
        return;

    // MFC DownloadSelected → GetSelectedCat (SearchResultsWnd.cpp:542)
    if (category < 0)
        category = m_categoryTabs->count() > 1 ? std::max(m_categoryTabs->currentIndex(), 0) : 0;

    // eNode rows leave here: Usenet goes to the Usenet queue, a torrent is saved
    // as a .torrent until BitTorrent downloads exist
    QModelIndexList ed2kRows;
    if (!tab->isIndexer() && tab->model) {
        QList<MetaResultActions::Row> usenet;
        QList<MetaResultActions::Row> torrents;
        for (const auto& idx : proxyRows) {
            const auto* r = refAt(idx).row;
            if (r && r->isUsenet())
                usenet.append({r->hash, r->fileName, true, r->metaRef()});
            else if (r && r->isTorrent())
                torrents.append({r->hash, r->fileName, false, r->metaRef()});
            else
                ed2kRows.append(idx);
        }
        if (!usenet.isEmpty())
            metaActions()->downloadUsenet(tab->searchID, usenet, category, QPointer(tab->model));
        if (!torrents.isEmpty())
            metaActions()->saveMetaFiles(tab->searchID, torrents);
        if (ed2kRows.isEmpty())
            return;
    } else {
        ed2kRows = proxyRows;
    }

    // Triaged once for the whole action, not once per row: selecting twenty rows
    // of which three are already downloaded must raise one question, not three.
    QModelIndexList plain;
    QModelIndexList known;
    QStringList knownNames;
    for (const auto& idx : std::as_const(ed2kRows)) {
        int knownType = 0;
        QString name;
        if (tab->isIndexer()) {
            const int srcRow = tab->proxy->mapToSource(tab->proxy->index(idx.row(), 0)).row();
            if (const auto* r = tab->indexerModel->resultAt(srcRow)) {
                knownType = r->knownType;
                name = r->title;
            }
        } else if (const SearchResultRef ref = refAt(idx)) {
            knownType = ref.row->knownType;
            name = ref.fileName();
        }

        // 3 downloaded, 4 cancelled — both mean it is gone from the transfer list
        // and asking again is a legitimate thing to want. Shared and downloading
        // are not asked about: re-adding those can never do anything useful.
        if (knownType == 3 || knownType == 4) {
            known.append(idx);
            knownNames.append(name);
        } else {
            plain.append(idx);
        }
    }

    bool downloadKnown = false;
    if (!known.isEmpty()) {
        // Called from a UI event rather than an IPC reply, so a direct modal is
        // safe here — unlike the reply handlers, which must defer one event-loop
        // turn (see Ed2kLinkImporter's note on nested loops).
        downloadKnown =
            QMessageBox::question(
                this, tr("Download"),
                tr("You have already downloaded the following file(s). "
                   "Download them again?\n\n%1")
                    .arg(knownNames.join(QLatin1Char('\n'))),
                QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes;
    }

    for (const QModelIndex& idx : std::as_const(plain)) {
        if (tab->isIndexer())
            sendIndexerGrab(idx.row(), /*force*/ false, category);
        else
            sendDownloadRequest(idx, category, paused);
    }
    if (!downloadKnown)
        return;
    for (const QModelIndex& idx : std::as_const(known)) {
        if (tab->isIndexer())
            sendIndexerGrab(idx.row(), /*force*/ true, category);
        else
            sendDownloadRequest(idx, category, paused);
    }
}

void SearchPanel::sendDownloadRequest(const QModelIndex& proxyIndex, int category,
                                      std::optional<bool> paused)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    auto* tab = currentTab();
    const SearchResultRef ref = refAt(proxyIndex);
    if (!tab || !ref)
        return;
    const SearchResultRow* result = ref.row;

    IpcMessage msg(IpcMsgType::DownloadSearchFile);
    msg.append(result->hash);
    msg.append(ref.fileName());   // a name row: that name, the file's data
    msg.append(static_cast<qint64>(result->fileSize));
    msg.append(QString());   // no link: the daemon builds one
    msg.append(static_cast<qint64>(category));
    msg.append(static_cast<qint64>(tab->searchID));   // daemon seeds sources + AICH from the result
    if (paused)
        msg.append(*paused);
    // Model and hash, not tab index and row: the tab at that index can be another
    // search by the time the reply lands (an indexer one has no model at all), and
    // the rows move with every result push.
    const QPointer<SearchResultsModel> model(tab->model);
    m_ipc->sendRequest(std::move(msg), [model, hash = result->hash](const IpcMessage& resp) {
        if (resp.fieldBool(0) && model)
            model->updateKnownTypes({{hash, 2}});   // Downloading
    });
}

// ---------------------------------------------------------------------------
// Copy eD2k link to clipboard
// ---------------------------------------------------------------------------

QString SearchPanel::buildEd2kLink(const QModelIndex& proxyIndex)
{
    const SearchResultRef ref = refAt(proxyIndex);
    return ref ? ref.row->ed2kLink(ref.fileName()) : QString();
}

void SearchPanel::copyEd2kLink(const QModelIndex& proxyIndex)
{
    const QString link = buildEd2kLink(proxyIndex);
    if (!link.isEmpty())
        QApplication::clipboard()->setText(link);
}

void SearchPanel::saveMetaFiles(bool nzb)
{
    auto* tab = currentTab();
    if (!tab || tab->isIndexer() || !tab->model)
        return;
    QList<MetaResultActions::Row> rows;
    for (const auto& idx : m_resultView->selectionModel()->selectedRows()) {
        const auto* r = refAt(idx).row;
        if (r && (nzb ? r->isUsenet() : r->isTorrent()))
            rows.append({r->hash, r->fileName, nzb, r->metaRef()});
    }
    metaActions()->saveMetaFiles(tab->searchID, rows);
}

MetaResultActions* SearchPanel::metaActions()
{
    if (!m_metaActions)
        m_metaActions = new MetaResultActions(m_ipc, this);
    return m_metaActions;
}

QString SearchPanel::buildMagnetLink(const QModelIndex& proxyIndex)
{
    const SearchResultRef ref = refAt(proxyIndex);
    return ref ? ref.row->magnetLink(ref.fileName()) : QString();
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
        IpcMessage msg(tab.isIndexer() ? IpcMsgType::RemoveIndexerSearch
                                       : IpcMsgType::RemoveSearch);
        msg.append(static_cast<qint64>(tab.searchID));
        m_ipc->sendRequest(std::move(msg));
    }

    // Clean up model/proxy. A tab holds one model or the other, never both.
    delete tab.proxy;
    delete tab.model;
    delete tab.indexerModel;
    m_tabs.erase(m_tabs.begin() + tabIndex);
    m_tabBar->removeTab(tabIndex);

    if (m_tabs.empty()) {
        m_tabBar->setVisible(false);
        // setModel(nullptr) wipes every section; UiState keeps the cached layout
        // and switchToTab() re-applies it once a model is attached again.
        m_resultView->setModel(nullptr);
        m_statusLabel->clear();
        m_cancelBtn->setEnabled(false);
        m_moreBtn->setEnabled(false);
    }
    scheduleSaveSearches();
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
        // ClearAllSearches above only reaches the ED2K side; the indexer searches
        // are a separate registry and each needs its own removal.
        if (tab.isIndexer() && m_ipc && m_ipc->isConnected() && tab.searchID != 0) {
            IpcMessage msg(IpcMsgType::RemoveIndexerSearch);
            msg.append(static_cast<qint64>(tab.searchID));
            m_ipc->sendRequest(std::move(msg));
        }
        delete tab.proxy;
        delete tab.model;
        delete tab.indexerModel;
    }
    m_tabs.clear();

    while (m_tabBar->count() > 0)
        m_tabBar->removeTab(0);
    m_tabBar->setVisible(false);
    m_resultView->setModel(nullptr);
    m_statusLabel->clear();
    m_cancelBtn->setEnabled(false);
    m_moreBtn->setEnabled(false);
    scheduleSaveSearches();
}

// ---------------------------------------------------------------------------
// Switch to a tab
// ---------------------------------------------------------------------------

void SearchPanel::switchToTab(int index)
{
    if (index < 0 || index >= static_cast<int>(m_tabs.size())) {
        m_resultView->setModel(nullptr);
        m_downloadBtn->setEnabled(false);
        m_moreBtn->setEnabled(false);
        return;
    }

    auto& tab = m_tabs[static_cast<size_t>(index)];
    m_resultView->setModel(tab.proxy);
    // setModel() clears every section, so the layout has to be pushed back each
    // time. The first call also binds the header, now that the columns exist.
    setupResultHeader(tab.isIndexer());
    theUiState.applyHeaderState(m_resultView->header(),
                                tab.isIndexer() ? kIndexerHeaderKey : kSearchHeaderKey);
    // Every tab has its own proxy, so re-guard the selection on the new model.
    theUiState.guardSelectionOnReset(m_resultView);
    connect(m_resultView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SearchPanel::updateDownloadButton);
    updateDownloadButton();
    m_filterEdit->setHeader(m_resultView->header());
    m_filterColumn = m_filterEdit->filterColumn();
    applyTextFilter();
    m_statusLabel->setText(tabStatusText(tab));
    updateMoreButton();
    m_loadMoreTimer->start();
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

ViewSelection SearchPanel::saveSelection() const
{
    return captureViewSelection(m_resultView, [this](int row) { return keyAtViewRow(row); });
}

void SearchPanel::restoreSelection(const ViewSelection& state)
{
    restoreViewSelection(m_resultView, state, [this](int row) { return keyAtViewRow(row); });
}

QString SearchPanel::keyAtViewRow(int viewRow) const
{
    auto* tab = const_cast<SearchPanel*>(this)->currentTab();
    if (!tab)
        return {};
    const int srcRow = tab->proxy->mapToSource(tab->proxy->index(viewRow, 0)).row();
    // an indexer row has no hash
    if (tab->isIndexer())
        return tab->indexerModel->idAt(srcRow);
    return tab->model ? tab->model->hashAt(srcRow) : QString{};
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

namespace {

QJsonObject ed2kRowToJson(const SearchResultRow& row)
{
    QJsonObject o;
    o[QStringLiteral("hash")]                = row.hash;
    o[QStringLiteral("fileName")]            = row.fileName;
    o[QStringLiteral("fileType")]            = row.fileType;
    o[QStringLiteral("fileSize")]            = static_cast<qint64>(row.fileSize);
    o[QStringLiteral("sourceCount")]         = static_cast<qint64>(row.sourceCount);
    o[QStringLiteral("completeSourceCount")] = static_cast<qint64>(row.completeSourceCount);
    o[QStringLiteral("isKad")]               = row.isKad;
    o[QStringLiteral("kadOrigin")]           = row.kadOrigin;
    o[QStringLiteral("inDirectory")]         = row.inDirectory;
    o[QStringLiteral("artist")]              = row.artist;
    o[QStringLiteral("album")]               = row.album;
    o[QStringLiteral("title")]               = row.title;
    o[QStringLiteral("codec")]               = row.codec;
    o[QStringLiteral("length")]              = static_cast<qint64>(row.length);
    o[QStringLiteral("bitrate")]             = static_cast<qint64>(row.bitrate);
    o[QStringLiteral("knownType")]           = row.knownType;
    o[QStringLiteral("seenBefore")]          = row.seenBefore;
    o[QStringLiteral("seenNames")]           = row.seenNames;
    o[QStringLiteral("firstSeen")]           = static_cast<qint64>(row.firstSeen);
    o[QStringLiteral("isSpam")]              = row.isSpam;
    o[QStringLiteral("confidence")]          = row.confidence;
    o[QStringLiteral("fakeScore")]           = row.fakeScore;
    o[QStringLiteral("fakeReasons")]         = QJsonArray::fromStringList(row.fakeReasons);
    if (!row.directory.isEmpty())
        o[QStringLiteral("directory")]       = row.directory;
    if (!row.aichHash.isEmpty())
        o[QStringLiteral("aichHash")]        = row.aichHash;
    o[QStringLiteral("kadPublishers")]       = row.kadPublishers;
    o[QStringLiteral("clientCount")]         = row.clientCount;
    if (!row.children.empty()) {
        QJsonArray children;
        for (const SearchChildRow& c : row.children) {
            children.append(QJsonObject{
                {QStringLiteral("fileName"), c.fileName},
                {QStringLiteral("sourceCount"), static_cast<qint64>(c.sourceCount)},
                {QStringLiteral("directory"), c.directory},
                {QStringLiteral("aichHash"), c.aichHash},
                {QStringLiteral("artist"), c.artist},
                {QStringLiteral("album"), c.album},
                {QStringLiteral("title"), c.title},
                {QStringLiteral("codec"), c.codec},
                {QStringLiteral("length"), static_cast<qint64>(c.length)},
                {QStringLiteral("bitrate"), static_cast<qint64>(c.bitrate)}});
        }
        o[QStringLiteral("children")] = children;
    }
    if (row.isMeta()) {
        // torrent/Usenet rows: the daemon refetches their metafile by this
        o[QStringLiteral("metaKind")]      = row.metaKind;
        o[QStringLiteral("magnet")]        = row.magnet;
        o[QStringLiteral("metaAge")]       = static_cast<qint64>(row.metaAgeDays);
        o[QStringLiteral("metaIndexer")]   = row.metaIndexer;
        o[QStringLiteral("metaCatalogId")] = row.metaCatalogId;
        o[QStringLiteral("metaServers")]   = row.metaServers.toJsonArray();
    }
    return o;
}

SearchResultRow ed2kRowFromJson(const QJsonObject& r)
{
    SearchResultRow row;
    row.hash                = r[QStringLiteral("hash")].toString();
    row.fileName            = r[QStringLiteral("fileName")].toString();
    row.fileType            = r[QStringLiteral("fileType")].toString();
    row.fileSize            = static_cast<qint64>(r[QStringLiteral("fileSize")].toDouble());
    row.sourceCount         = static_cast<qint64>(r[QStringLiteral("sourceCount")].toDouble());
    row.completeSourceCount = static_cast<qint64>(r[QStringLiteral("completeSourceCount")].toDouble());
    row.isKad               = r[QStringLiteral("isKad")].toBool();
    row.kadOrigin           = r[QStringLiteral("kadOrigin")].toBool();
    row.inDirectory         = r[QStringLiteral("inDirectory")].toBool();
    row.artist              = r[QStringLiteral("artist")].toString();
    row.album               = r[QStringLiteral("album")].toString();
    row.title               = r[QStringLiteral("title")].toString();
    row.codec               = r[QStringLiteral("codec")].toString();
    row.length              = static_cast<qint64>(r[QStringLiteral("length")].toDouble());
    row.bitrate             = static_cast<qint64>(r[QStringLiteral("bitrate")].toDouble());
    row.knownType           = r[QStringLiteral("knownType")].toInt();
    row.seenBefore          = r[QStringLiteral("seenBefore")].toBool();
    row.seenNames           = r[QStringLiteral("seenNames")].toInt();
    row.firstSeen           = static_cast<qint64>(r[QStringLiteral("firstSeen")].toDouble());
    row.isSpam              = r[QStringLiteral("isSpam")].toBool();
    row.confidence          = r[QStringLiteral("confidence")].toString();
    row.fakeScore           = r[QStringLiteral("fakeScore")].toInt();
    row.directory           = r[QStringLiteral("directory")].toString();
    row.aichHash            = r[QStringLiteral("aichHash")].toString();
    row.kadPublishers       = r[QStringLiteral("kadPublishers")].toInt();
    row.clientCount         = r[QStringLiteral("clientCount")].toInt();
    for (const auto& childVal : r[QStringLiteral("children")].toArray()) {
        const QJsonObject c = childVal.toObject();
        SearchChildRow child;
        child.fileName    = c[QStringLiteral("fileName")].toString();
        child.sourceCount = static_cast<qint64>(c[QStringLiteral("sourceCount")].toDouble());
        child.directory   = c[QStringLiteral("directory")].toString();
        child.aichHash    = c[QStringLiteral("aichHash")].toString();
        child.artist      = c[QStringLiteral("artist")].toString();
        child.album       = c[QStringLiteral("album")].toString();
        child.title       = c[QStringLiteral("title")].toString();
        child.codec       = c[QStringLiteral("codec")].toString();
        child.length      = static_cast<qint64>(c[QStringLiteral("length")].toDouble());
        child.bitrate     = static_cast<qint64>(c[QStringLiteral("bitrate")].toDouble());
        row.children.push_back(std::move(child));
    }
    for (const auto& reason : r[QStringLiteral("fakeReasons")].toArray())
        row.fakeReasons.push_back(reason.toString());
    row.metaKind            = r[QStringLiteral("metaKind")].toInt();
    row.magnet              = r[QStringLiteral("magnet")].toString();
    row.metaAgeDays         = static_cast<qint64>(r[QStringLiteral("metaAge")].toDouble());
    row.metaIndexer         = r[QStringLiteral("metaIndexer")].toString();
    row.metaCatalogId       = r[QStringLiteral("metaCatalogId")].toString();
    row.metaServers         = QCborArray::fromJsonArray(r[QStringLiteral("metaServers")].toArray());
    return row;
}

// No download URL here: the daemon keeps it and grabs a restored row by `id`.
QJsonObject requestToJson(const SearchRequest& req)
{
    QJsonObject o;
    o[QStringLiteral("expression")] = req.expression;
    o[QStringLiteral("fileType")] = req.fileType;
    o[QStringLiteral("minSize")] = req.minSize;
    o[QStringLiteral("maxSize")] = req.maxSize;
    o[QStringLiteral("avail")] = req.avail;
    o[QStringLiteral("extension")] = req.extension;
    o[QStringLiteral("completeSources")] = req.completeSources;
    o[QStringLiteral("codec")] = req.codec;
    o[QStringLiteral("minBitrate")] = req.minBitrate;
    o[QStringLiteral("minLength")] = req.minLength;
    o[QStringLiteral("title")] = req.title;
    o[QStringLiteral("album")] = req.album;
    o[QStringLiteral("artist")] = req.artist;
    return o;
}

SearchRequest requestFromJson(const QJsonObject& o)
{
    SearchRequest req;
    req.expression = o[QStringLiteral("expression")].toString();
    req.fileType = o[QStringLiteral("fileType")].toString();
    req.minSize = o[QStringLiteral("minSize")].toInteger();
    req.maxSize = o[QStringLiteral("maxSize")].toInteger();
    req.avail = o[QStringLiteral("avail")].toInt();
    req.extension = o[QStringLiteral("extension")].toString();
    req.completeSources = o[QStringLiteral("completeSources")].toInt();
    req.codec = o[QStringLiteral("codec")].toString();
    req.minBitrate = o[QStringLiteral("minBitrate")].toInt();
    req.minLength = o[QStringLiteral("minLength")].toInt();
    req.title = o[QStringLiteral("title")].toString();
    req.album = o[QStringLiteral("album")].toString();
    req.artist = o[QStringLiteral("artist")].toString();
    return req;
}

QJsonObject indexerRowToJson(const IndexerResultRow& row)
{
    QJsonObject o;
    o[QStringLiteral("id")]                = row.id;
    o[QStringLiteral("indexerName")]       = row.indexerName;
    o[QStringLiteral("title")]             = row.title;
    o[QStringLiteral("size")]              = static_cast<qint64>(row.size);
    o[QStringLiteral("published")]         = static_cast<qint64>(row.published);
    o[QStringLiteral("category")]          = row.category;
    o[QStringLiteral("grabs")]             = row.grabs;
    o[QStringLiteral("files")]             = row.files;
    o[QStringLiteral("passwordProtected")] = row.passwordProtected;
    o[QStringLiteral("seeders")]           = row.seeders;
    o[QStringLiteral("peers")]             = row.peers;
    o[QStringLiteral("isUsenet")]          = row.isUsenet;
    o[QStringLiteral("knownType")]         = row.knownType;
    o[QStringLiteral("confidence")]        = row.confidence;
    o[QStringLiteral("fakeScore")]         = row.fakeScore;
    o[QStringLiteral("fakeReasons")]       = QJsonArray::fromStringList(row.fakeReasons);
    return o;
}

IndexerResultRow indexerRowFromJson(const QJsonObject& r)
{
    IndexerResultRow row;
    row.id                = r[QStringLiteral("id")].toString();
    row.indexerName       = r[QStringLiteral("indexerName")].toString();
    row.title             = r[QStringLiteral("title")].toString();
    row.size              = static_cast<qint64>(r[QStringLiteral("size")].toDouble());
    row.published         = static_cast<qint64>(r[QStringLiteral("published")].toDouble());
    row.category          = r[QStringLiteral("category")].toString();
    row.grabs             = r[QStringLiteral("grabs")].toInt(-1);
    row.files             = r[QStringLiteral("files")].toInt(-1);
    row.passwordProtected = r[QStringLiteral("passwordProtected")].toBool();
    row.seeders           = r[QStringLiteral("seeders")].toInt(-1);
    row.peers             = r[QStringLiteral("peers")].toInt(-1);
    row.isUsenet          = r[QStringLiteral("isUsenet")].toBool(true);
    row.knownType         = r[QStringLiteral("knownType")].toInt();
    row.confidence        = r[QStringLiteral("confidence")].toString();
    row.fakeScore         = r[QStringLiteral("fakeScore")].toInt();
    for (const auto& reason : r[QStringLiteral("fakeReasons")].toArray())
        row.fakeReasons.push_back(reason.toString());
    // age moves on while the tab sits on disk
    if (row.published > 0)
        row.ageDays = static_cast<int>((QDateTime::currentSecsSinceEpoch() - row.published) / 86400);
    return row;
}

} // namespace

void SearchPanel::saveSearches()
{
    if (!m_searchesLoaded)
        return;
    const QString path = thePrefs.configDir() + QStringLiteral("/StoredSearches.json");

    if (!thePrefs.storeSearches() || m_tabs.empty()) {
        QFile::remove(path);
        return;
    }

    // Every kind is stored. Rows that download through the daemon (torrent/Usenet
    // meta rows, indexer rows) stay downloadable after a restart: meta rows carry
    // their catalog id + servers, indexer rows are grabbed from the daemon's grab
    // cache by id.
    QJsonArray searchesArr;
    for (const auto& tab : m_tabs) {
        QJsonObject searchObj;
        searchObj[QStringLiteral("title")] = tab.title;
        searchObj[QStringLiteral("method")] = tab.method;
        searchObj[QStringLiteral("clientSharedFiles")] = tab.clientSharedFiles;
        searchObj[QStringLiteral("indexer")] = tab.isIndexer();
        searchObj[QStringLiteral("request")] = requestToJson(tab.request);

        QJsonArray resultsArr;
        if (tab.isIndexer()) {
            for (int r = 0; r < tab.indexerModel->resultCount(); ++r)
                if (const auto* row = tab.indexerModel->resultAt(r))
                    resultsArr.append(indexerRowToJson(*row));
        } else {
            for (int r = 0; r < tab.model->resultCount(); ++r)
                if (const auto* row = tab.model->resultAt(r))
                    resultsArr.append(ed2kRowToJson(*row));
        }
        searchObj[QStringLiteral("results")] = resultsArr;
        searchesArr.append(searchObj);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("searches")] = searchesArr;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) >= 0)
        logInfo(QStringLiteral("Stored %1 searches to %2").arg(searchesArr.size()).arg(path));
    else
        logWarning(QStringLiteral("Could not store searches to %1: %2").arg(path, file.errorString()));
}

void SearchPanel::loadSearches()
{
    m_searchesLoaded = true;
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

        const auto resultsArr = searchObj[QStringLiteral("results")].toArray();

        SearchTab tab;
        tab.searchID = 0;
        tab.title = title;
        tab.method = searchObj[QStringLiteral("method")].toInt();
        tab.clientSharedFiles = searchObj[QStringLiteral("clientSharedFiles")].toBool();
        tab.finished = true;
        tab.request = requestFromJson(searchObj[QStringLiteral("request")].toObject());
        // stored before the request was kept: the title is the expression
        if (tab.request.expression.isEmpty() && !tab.clientSharedFiles)
            tab.request.expression = title;

        if (searchObj[QStringLiteral("indexer")].toBool()) {
            std::vector<IndexerResultRow> rows;
            rows.reserve(static_cast<size_t>(resultsArr.size()));
            for (const auto& rVal : resultsArr)
                rows.push_back(indexerRowFromJson(rVal.toObject()));
            tab.indexerModel = new IndexerResultsModel(this);
            tab.indexerModel->addResults(rows);
        } else {
            std::vector<SearchResultRow> rows;
            rows.reserve(static_cast<size_t>(resultsArr.size()));
            for (const auto& rVal : resultsArr)
                rows.push_back(ed2kRowFromJson(rVal.toObject()));
            tab.model = new SearchResultsModel(this);
            tab.model->setResults(std::move(rows));
        }
        addResultTab(std::move(tab));
    }

    if (!m_tabs.empty()) {
        m_tabBar->setVisible(true);
        m_tabBar->setCurrentIndex(0);
    }
}

void SearchPanel::scheduleSaveSearches()
{
    if (!m_saveTimer) {
        m_saveTimer = new QTimer(this);
        m_saveTimer->setSingleShot(true);
        m_saveTimer->setInterval(2000);
        connect(m_saveTimer, &QTimer::timeout, this, &SearchPanel::saveSearches);
    }
    // Not restarted: a running search pushes steadily and must still get written
    if (!m_saveTimer->isActive())
        m_saveTimer->start();
}

// ---------------------------------------------------------------------------
// Preview a search result via HTTP streaming
// ---------------------------------------------------------------------------

void SearchPanel::requestPeerPreview(uint32_t searchID, const QString& hash)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    IpcMessage msg(IpcMsgType::RequestSearchPreview);
    msg.append(static_cast<qint64>(searchID));
    msg.append(hash);
    m_ipc->sendRequest(std::move(msg), [this](const IpcMessage& resp) {
        if (IpcFeedback::checkOrWarn(resp, this, tr("Preview")))
            StatusBarNotifier::post(tr("Preview requested - please wait"), 4000);
    });
}

void SearchPanel::onSearchPreviewPush(const IpcMessage& msg)
{
    // [hash, userName, frames: bytes[]]
    const QString hash = msg.fieldString(0);
    const QString userName = msg.fieldString(1);

    std::vector<QImage> frames;
    for (const QCborValue& value : msg.field(2).toArray()) {
        if (QImage image = QImage::fromData(value.toByteArray(), "PNG"); !image.isNull())
            frames.push_back(std::move(image));
    }
    if (frames.empty()) {
        StatusBarNotifier::post(tr("%1 sent no preview").arg(userName), 4000);
        return;
    }

    // The name comes from the row; the daemon's hash text and ours differ in case.
    QString fileName;
    for (const auto& tab : m_tabs) {
        if (!tab.model)
            continue;
        for (int row = 0; row < tab.model->rowCount() && fileName.isEmpty(); ++row) {
            const auto* result = tab.model->resultAt(row);
            if (result && result->hash.compare(hash, Qt::CaseInsensitive) == 0)
                fileName = result->fileName;
        }
    }
    auto* dialog = new PeerPreviewDialog(fileName.isEmpty() ? userName : fileName,
                                         std::move(frames), this);
    dialog->show();
}

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
            // No connectCommentPosting() here on purpose: a search hit is a file on
            // someone else's disk, so its Comments page is the read-only one — as it
            // is in MFC's CSearchResultFileDetailSheet, which hosts CCommentDialogLst.
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
        const auto* result = tab->model->resultAt(ViewNav::toSource(to)).row;
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

    for (const SearchTab& tab : m_tabs) {
        // An indexer tab carries indexerModel and leaves model null, so this loop
        // would dereference nothing at all. It runs on every downloadAdded /
        // downloadRemoved, which is why one ED2K download starting while a Usenet
        // search tab was open used to take the whole GUI down. Their own pass is
        // refreshUsenetKnownTypes(), below.
        if (tab.isIndexer() || !tab.model)
            continue;
        const int count = tab.model->resultCount();
        if (count == 0)
            continue;

        // The reply is one type per hash *sent*. Pair it with this list, never with
        // the model's rows at reply time — a result push resets them in between.
        QStringList sent;
        sent.reserve(count);
        for (int r = 0; r < count; ++r) {
            if (const auto* row = tab.model->resultAt(r))
                sent.append(row->hash);
        }

        IpcMessage msg(IpcMsgType::GetKnownTypes);
        msg.append(QCborValue(QCborArray::fromStringList(sent)));

        const QPointer<SearchResultsModel> model(tab.model);
        m_ipc->sendRequest(std::move(msg), [model, sent](const IpcMessage& resp) {
            if (!resp.fieldBool(0) || !model)
                return;
            const auto types = resp.fieldArray(1);
            const qsizetype n = std::min(types.size(), sent.size());
            QHash<QString, int> typesByHash;
            for (qsizetype i = 0; i < n; ++i)
                typesByHash.insert(sent.at(i), static_cast<int>(types.at(i).toInteger()));
            model->updateKnownTypes(typesByHash);
        });
    }

    refreshUsenetKnownTypes();
}

void SearchPanel::refreshUsenetKnownTypes()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    for (const SearchTab& tab : m_tabs) {
        if (!tab.isIndexer())
            continue;
        const int count = tab.indexerModel->resultCount();
        if (count == 0)
            continue;

        // Titles, not titles and sizes. An indexer's size is its own arithmetic
        // over the NZB and disagrees with ours often enough that matching on it
        // would silently leave rows unmarked, which is the wrong way to be wrong.
        // Kept as sent, for the same reason refreshKnownTypes() keeps its hashes.
        QStringList sent;
        sent.reserve(count);
        for (int r = 0; r < count; ++r) {
            if (const auto* row = tab.indexerModel->resultAt(r))
                sent.append(row->title);
        }

        IpcMessage msg(IpcMsgType::GetUsenetKnownTypes);
        msg.append(QCborValue(QCborArray::fromStringList(sent)));

        const QPointer<IndexerResultsModel> model(tab.indexerModel);
        m_ipc->sendRequest(std::move(msg), [model, sent](const IpcMessage& resp) {
            if (!resp.fieldBool(0) || !model)
                return;
            const auto types = resp.fieldArray(1);
            const qsizetype n = std::min(types.size(), sent.size());
            QHash<QString, int> typesByTitle;
            for (qsizetype i = 0; i < n; ++i)
                typesByTitle.insert(sent.at(i), static_cast<int>(types.at(i).toInteger()));
            model->updateKnownTypes(typesByTitle);
        });
    }
}

// ---------------------------------------------------------------------------
// Results header — bound once, on the first tab that attaches a model
// ---------------------------------------------------------------------------

void SearchPanel::setupResultHeader(bool forIndexer)
{
    m_resultView->setRootIsDecorated(!forIndexer);

    const QString& key = forIndexer ? kIndexerHeaderKey : kSearchHeaderKey;
    if (m_boundHeaderKey == key)
        return;
    m_boundHeaderKey = key;

    if (forIndexer) {
        // Name, Size, Age, Category, Grabs, Indexer, the two torznab columns hidden
        // below until a BitTorrent module can populate them, then Known and Confidence.
        // A layout saved before a column existed has a different column count, so Qt
        // rejects it and these defaults apply — the indexer header resets once, then sticks.
        m_resultView->setDescendingFirst({});   // other columns than the eD2K model's
        if (!theUiState.hasHeaderState(kIndexerHeaderKey)) {
            // Fresh layout: Confidence next to Size, where the eD2K list has it
            auto* header = m_resultView->header();
            header->moveSection(header->visualIndex(IndexerResultsModel::ColConfidence),
                                header->visualIndex(IndexerResultsModel::ColSize) + 1);
        }
        m_resultView->bindColumns(kIndexerHeaderKey, {380, 80, 70, 120, 60, 110, 60, 60, 80, 100});
        m_resultView->setColumnHidden(IndexerResultsModel::ColSeeders, true);
        m_resultView->setColumnHidden(IndexerResultsModel::ColPeers, true);
        m_resultView->setLockedColumns({IndexerResultsModel::ColSeeders, IndexerResultsModel::ColPeers});
        return;
    }

    // File Name, Size, Availability, Confidence, Complete, Type, Artist, Album, Title,
    // Length, Bitrate, Codec, Known, Seen, File ID, Folder, AICH Hash — the last three
    // hidden by default as in MFC. A saved layout overrides these.
    if (!theUiState.hasHeaderState(kSearchHeaderKey)) {
        // Fresh layout: where MFC has them (SearchListCtrl.cpp:267-276)
        auto* header = m_resultView->header();
        const auto placeAfter = [header](int column, int after) {
            header->moveSection(header->visualIndex(column), header->visualIndex(after) + 1);
        };
        placeAfter(SearchResultsModel::ColFileID, SearchResultsModel::ColType);
        placeAfter(SearchResultsModel::ColFolder, SearchResultsModel::ColCodec);
        placeAfter(SearchResultsModel::ColAichHash, SearchResultsModel::ColKnown);
    }
    // MFC SearchListCtrl.cpp:543-547
    m_resultView->setDescendingFirst({SearchResultsModel::ColAvailability,
                                      SearchResultsModel::ColComplete});
    m_resultView->setDefaultSort(SearchResultsModel::ColAvailability, Qt::DescendingOrder);
    m_resultView->bindColumns(kSearchHeaderKey,
        {300, 80, 70, 100, 70, 70, 100, 100, 100, 60, 60, 60, 60, 110, 230, 150, 240},
        {SearchResultsModel::ColFileID, SearchResultsModel::ColFolder,
         SearchResultsModel::ColAichHash});
}

SearchRequest SearchPanel::requestFromUi() const
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

void SearchPanel::applyRequestToUi(const SearchRequest& req)
{
    constexpr qint64 kMiB = 1024 * 1024;
    onResetFilters();
    m_nameEdit->setText(req.expression);
    m_typeCombo->setCurrentIndex(std::max(0, m_typeCombo->findData(req.fileType)));
    m_minSizeSpin->setValue(static_cast<int>(req.minSize / kMiB));
    m_maxSizeSpin->setValue(static_cast<int>(req.maxSize / kMiB));
    m_availSpin->setValue(req.avail);
    m_extensionEdit->setText(req.extension);
    m_completeSpin->setValue(req.completeSources);
    m_codecEdit->setText(req.codec);
    m_minBitrateSpin->setValue(req.minBitrate);
    m_minLengthSpin->setValue(req.minLength);
    m_titleEdit->setText(req.title);
    m_albumEdit->setText(req.album);
    m_artistEdit->setText(req.artist);
}

void SearchPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Results that arrived while this tab was hidden are still marked dirty; fetch
    // them now so the list is current the moment it comes back on screen.
    drainDirtySearches();
    m_loadMoreTimer->start();
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
        // !isIndexer matters: the two kinds number their searches independently,
        // so an ED2K push for #1 would otherwise refresh an indexer tab #1 —
        // through a SearchResultsModel it does not have.
        const bool stillOpen = std::ranges::any_of(m_tabs,
            [searchID](const SearchTab& tab) {
                return !tab.isIndexer() && tab.searchID == searchID;
            });
        if (stillOpen)
            requestSearchResults(searchID);
    }
}

void SearchPanel::updateCategoryTabs()
{
    // MFC CSearchResultsWnd::UpdateCatTabs (SearchResultsWnd.cpp:1570-1586)
    const int previous = m_categoryTabs->currentIndex();
    const QSignalBlocker blocker(m_categoryTabs);
    while (m_categoryTabs->count() > 0)
        m_categoryTabs->removeTab(0);
    for (qsizetype i = 0; i < m_categoryTitles.size(); ++i) {
        QString label = i == 0 ? tr("All") : m_categoryTitles.at(i);
        m_categoryTabs->addTab(label.replace(QLatin1Char('&'), QStringLiteral("&&")));
    }
    m_categoryTabs->setCurrentIndex(previous > 0 && previous < m_categoryTabs->count() ? previous : 0);
    const bool show = m_categoryTabs->count() > 1;
    m_downloadToLabel->setVisible(show);
    m_categoryTabs->setVisible(show);
}

// ---------------------------------------------------------------------------
// Result list commands — context menu and list keys
// ---------------------------------------------------------------------------

void SearchPanel::removeSelectedResults()
{
    // MFC searchlist->RemoveResult: a file row goes with its names, a name row alone.
    // The daemon drops it as well, or its next snapshot would bring the row back; a
    // restored tab (no daemon search behind it) is local only.
    auto* tab = currentTab();
    if (!tab || !m_resultView->selectionModel())
        return;
    // Last row first, so the indexes still to go stay valid
    std::set<int, std::greater<>> fileRows;
    std::set<std::pair<int, int>, std::greater<>> nameRows;
    for (const auto& i : m_resultView->selectionModel()->selectedRows()) {
        const QModelIndex src = tab->proxy->mapToSource(i);
        if (src.parent().isValid())
            nameRows.insert({src.parent().row(), src.row()});
        else
            fileRows.insert(src.row());
    }
    if (fileRows.empty() && nameRows.empty())
        return;

    const auto tellDaemon = [this, tab](const QString& hash, const QString& name) {
        if (!m_ipc || tab->searchID == 0 || hash.isEmpty())
            return;
        IpcMessage msg(IpcMsgType::RemoveSearchResult);
        msg.append(static_cast<qint64>(tab->searchID));
        msg.append(hash);
        msg.append(name);
        m_ipc->sendRequest(std::move(msg));
    };
    for (const auto& [row, childRow] : nameRows) {
        if (fileRows.contains(row))
            continue;   // goes with its file
        const SearchResultRow* file = tab->model->resultAt(row);
        if (!file || childRow >= static_cast<int>(file->children.size()))
            continue;
        tellDaemon(file->hash, file->children[static_cast<size_t>(childRow)].fileName);
        tab->model->removeChild(row, childRow);
    }
    for (int r : fileRows) {
        tellDaemon(tab->model->hashAt(r), {});
        tab->model->removeRow(r);
    }
    m_tabBar->setTabText(m_tabBar->currentIndex(), tabLabel(*tab));
    scheduleSaveSearches();
}

void SearchPanel::copySelectedEd2kLinks()
{
    if (!m_resultView->selectionModel())
        return;
    // selectedRows() is click order; the clipboard wants view order.
    QModelIndexList rows = m_resultView->selectionModel()->selectedRows();
    std::ranges::sort(rows, {}, &QModelIndex::row);
    QStringList links;
    for (const auto& i : rows)
        if (const QString link = buildEd2kLink(i); !link.isEmpty())
            links << link;
    if (!links.isEmpty())
        QApplication::clipboard()->setText(links.join(QLatin1Char('\n')));
}

// ---------------------------------------------------------------------------
// Tab creation helpers
// ---------------------------------------------------------------------------

int SearchPanel::addResultTab(SearchTab tab)
{
    tab.proxy = new SearchResultsProxy(this);
    if (tab.isIndexer())
        tab.proxy->setSourceModel(tab.indexerModel);
    else
        tab.proxy->setSourceModel(tab.model);
    tab.proxy->setSortRole(Qt::UserRole);
    applyNetworkFilter(tab);

    if (auto* proxy = qobject_cast<SearchResultsProxy*>(tab.proxy))
        proxy->setTextFilter(m_filterTokens, tab.isIndexer() ? 0 : m_filterColumn);

    const QString label = tabLabel(tab);
    const QIcon icon = tabIcon(tab);
    m_tabs.push_back(std::move(tab));

    const int idx = icon.isNull() ? m_tabBar->addTab(label) : m_tabBar->addTab(icon, label);
    m_tabBar->setVisible(true);
    return idx;
}

QIcon SearchPanel::tabIcon(const SearchTab& tab)
{
    if (!thePrefs.useOriginalIcons())
        return {};
    if (tab.isIndexer())
        return QIcon(QStringLiteral(":/icons/UsenetSearch.ico"));
    // MFC SearchResultsWnd.cpp:1367 — sriClient ("StatsClients" = User.ico)
    if (tab.clientSharedFiles)
        return QIcon(QStringLiteral(":/icons/User.ico"));

    switch (tab.method) {
    case 1:  return QIcon(QStringLiteral(":/icons/Server.ico"));      // Ed2k Server
    case 2:  return QIcon(QStringLiteral(":/icons/Global.ico"));      // Ed2k Global
    case 3:  return QIcon(QStringLiteral(":/icons/SearchKad.ico"));   // Kademlia
    case 6:  return QIcon(QStringLiteral(":/icons/Usenet.ico"));      // Usenet (Server)
    case 7:  return QIcon(QStringLiteral(":/icons/Torrent.ico"));     // Torrent (Server)
    default: return QIcon(QStringLiteral(":/icons/KadServer.ico"));   // Automatic
    }
}

void SearchPanel::loadMoreIfAtEnd()
{
    auto* tab = currentTab();
    const QScrollBar* bar = m_resultView->verticalScrollBar();
    if (!tab || !tab->hasMore || !isVisible() || bar->value() < bar->maximum())
        return;
    // rows of the last page are still on their way: the list is not at its end yet
    if (m_dirtySearchIDs.contains(tab->searchID) || m_fetchingSearchIDs.contains(tab->searchID))
        return;
    requestMore(*tab);
}

void SearchPanel::requestMore(SearchTab& tab)
{
    if (!m_ipc || !tab.hasMore || tab.isIndexer() || tab.searchID == 0)
        return;
    tab.hasMore = false;   // once a page; the next state push says whether another follows
    IpcMessage msg(IpcMsgType::SearchMore);
    msg.append(static_cast<qint64>(tab.searchID));
    m_ipc->sendRequest(std::move(msg));
    if (&tab == currentTab()) {
        m_statusLabel->setText(tabStatusText(tab));
        updateMoreButton();
    }
}

void SearchPanel::updateMoreButton()
{
    const auto* tab = currentTab();
    const bool on = tab && tab->hasMore && !tab->isIndexer() && tab->searchID != 0;
    // a button that goes grey must not keep the focus (MFC OnBnClickedMore)
    if (!on && m_moreBtn->hasFocus())
        m_nameEdit->setFocus();
    m_moreBtn->setEnabled(on);
}

void SearchPanel::applyNetworkFilter(SearchTab& tab)
{
    auto* proxy = qobject_cast<SearchResultsProxy*>(tab.proxy);
    if (!proxy)
        return;
    SearchResultsProxy::NetworkFilter filter;
    // Usenet / torrent via server: that network was asked for, and an emptied
    // list would have loadMoreIfAtEnd() pull every page
    if (tab.method != 6 && tab.method != 7 && m_showUsenetCheck) {
        filter.usenet = m_showUsenetCheck->isChecked();
        filter.kad = m_showKadCheck->isChecked();
        filter.torrent = m_showTorrentCheck->isChecked();
    }
    proxy->setNetworkFilter(filter);
}

} // namespace eMule
