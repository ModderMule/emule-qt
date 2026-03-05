/// @file TransferPanel.cpp
/// @brief Transfer tab panel — implementation.

#include "panels/TransferPanel.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/ClientListModel.h"
#include "controls/DownloadListModel.h"
#include "controls/DownloadProgressDelegate.h"
#include "controls/TransferToolbar.h"
#include "dialogs/ClientDetailDialog.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "protocol/ED2KLink.h"

#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>

namespace eMule {

using namespace Ipc;

namespace {

/// Parse a CBOR map into a ClientRow — shared by uploads, download clients, known clients.
ClientRow parseClient(const QCborMap& m)
{
    ClientRow row;
    row.userName        = m.value(QStringLiteral("userName")).toString();
    row.software        = m.value(QStringLiteral("software")).toString();
    row.userHash        = m.value(QStringLiteral("userHash")).toString();
    row.uploadState     = m.value(QStringLiteral("uploadState")).toString();
    row.downloadState   = m.value(QStringLiteral("downloadState")).toString();
    row.transferredUp   = m.value(QStringLiteral("transferredUp")).toInteger();
    row.transferredDown = m.value(QStringLiteral("transferredDown")).toInteger();
    row.sessionUp       = m.value(QStringLiteral("sessionUp")).toInteger();
    row.sessionDown     = m.value(QStringLiteral("sessionDown")).toInteger();
    row.askedCount      = m.value(QStringLiteral("askedCount")).toInteger();
    row.waitStartTime   = m.value(QStringLiteral("waitStartTime")).toInteger();
    row.partCount       = static_cast<int>(m.value(QStringLiteral("partCount")).toInteger());
    row.availPartCount  = static_cast<int>(m.value(QStringLiteral("availPartCount")).toInteger());
    row.remoteQueueRank = static_cast<int>(m.value(QStringLiteral("remoteQueueRank")).toInteger());
    row.sourceFrom      = static_cast<int>(m.value(QStringLiteral("sourceFrom")).toInteger());
    row.ip              = static_cast<uint32_t>(m.value(QStringLiteral("ip")).toInteger());
    row.port            = static_cast<uint16_t>(m.value(QStringLiteral("port")).toInteger());
    row.isBanned        = m.value(QStringLiteral("isBanned")).toBool();
    row.softwareId      = static_cast<int>(m.value(QStringLiteral("softwareId")).toInteger(-1));
    row.hasCredit       = m.value(QStringLiteral("hasCredit")).toBool();
    row.isFriend        = m.value(QStringLiteral("isFriend")).toBool();

    // New fields for TODO columns
    row.uploadStartDelay = m.value(QStringLiteral("uploadStartDelay")).toInteger();
    row.filePriority     = static_cast<int>(m.value(QStringLiteral("filePriority")).toInteger(-1));
    row.isAutoPriority   = m.value(QStringLiteral("isAutoPriority")).toBool();
    row.fileRating       = static_cast<uint8_t>(m.value(QStringLiteral("fileRating")).toInteger());
    row.isConnected      = m.value(QStringLiteral("isConnected")).toBool();

    // Pick best file name from reqFileName or uploadFileName or fileName
    row.fileName = m.value(QStringLiteral("uploadFileName")).toString();
    if (row.fileName.isEmpty())
        row.fileName = m.value(QStringLiteral("reqFileName")).toString();
    if (row.fileName.isEmpty())
        row.fileName = m.value(QStringLiteral("fileName")).toString();
    return row;
}

/// MFC priority integer constants matching PartFile.h
constexpr int PrVeryLow = 4;
constexpr int PrLow     = 0;
constexpr int PrNormal  = 1;
constexpr int PrHigh    = 2;
constexpr int PrVeryHigh = 3;

// ---------------------------------------------------------------------------
// CategoryFilterProxy — filters downloads by category
// ---------------------------------------------------------------------------

class CategoryFilterProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void setCategoryFilter(int64_t cat)
    {
        if (m_category == cat)
            return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        m_category = cat;
        endFilterChange();
#else
        m_category = cat;
        invalidateFilter();
#endif
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
        // Always accept child (source) rows — filtering is only on top-level downloads
        if (sourceParent.isValid())
            return true;
        if (m_category == 0)
            return true; // "All" — show everything
        auto* model = qobject_cast<DownloadListModel*>(sourceModel());
        if (!model)
            return true;
        const auto* dl = model->downloadAt(sourceRow);
        return dl && dl->category == m_category;
    }

private:
    int64_t m_category = 0;
};

} // anonymous namespace

TransferPanel::TransferPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &TransferPanel::onRefreshTimer);
}

TransferPanel::~TransferPanel() = default;

void TransferPanel::switchToSubTab(int index)
{
    if (index >= 0 && index <= 3)
        setBottomClientView(index);
}

void TransferPanel::setIpcClient(IpcClient* client)
{
    m_ipc = client;

    if (m_ipc && m_ipc->isConnected()) {
        m_refreshTimer->setInterval(m_ipc->pollingInterval());
        m_refreshTimer->start();
        onRefreshTimer();
    } else if (m_ipc) {
        connect(m_ipc, &IpcClient::connected, this, [this]() {
            m_refreshTimer->setInterval(m_ipc->pollingInterval());
            m_refreshTimer->start();
            onRefreshTimer();
        });
        connect(m_ipc, &IpcClient::disconnected, this, [this]() {
            m_refreshTimer->stop();
            m_downloadModel->clear();
            m_uploadingModel->clear();
            m_downloadingModel->clear();
            m_onQueueModel->clear();
            m_knownModel->clear();
            updateToolbarLabels();
        });

        // Wire push events for immediate refresh
        connect(m_ipc, &IpcClient::downloadAdded, this, [this](const IpcMessage&) {
            requestDownloads();
        });
        connect(m_ipc, &IpcClient::downloadRemoved, this, [this](const IpcMessage&) {
            requestDownloads();
        });
        connect(m_ipc, &IpcClient::downloadUpdated, this, [this](const IpcMessage&) {
            requestDownloads();
            requestDownloadClients();  // sources may have changed too
        });
        connect(m_ipc, &IpcClient::uploadUpdated, this, [this](const IpcMessage&) {
            requestUploads();
        });
        connect(m_ipc, &IpcClient::knownClientsChanged, this, [this](const IpcMessage&) {
            requestKnownClients();
        });
    } else {
        m_refreshTimer->stop();
        m_downloadModel->clear();
        m_uploadingModel->clear();
        m_downloadingModel->clear();
        m_onQueueModel->clear();
        m_knownModel->clear();
        updateToolbarLabels();
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void TransferPanel::onRefreshTimer()
{
    requestDownloads();
    requestUploads();
    requestDownloadClients();
    requestKnownClients();
}

void TransferPanel::onDownloadContextMenu(const QPoint& pos)
{
    // Resolve item under cursor (may be empty — menu still shown)
    QString hash;
    const DownloadRow* dl = nullptr;
    const QModelIndex proxyIdx = m_downloadView->indexAt(pos);
    if (proxyIdx.isValid()) {
        const QModelIndex catSrcIdx = m_categoryProxy->mapToSource(proxyIdx);
        const QModelIndex srcIdx = m_downloadProxy->mapToSource(catSrcIdx);
        // If this is a source (child) row, show the client context menu instead
        if (m_downloadModel->isSourceRow(srcIdx)) {
            if (const auto* src = m_downloadModel->sourceAt(srcIdx))
                showSourceContextMenu(*src, m_downloadView->viewport()->mapToGlobal(pos));
            return;
        }
        hash = m_downloadModel->hashAt(srcIdx.row());
        dl = m_downloadModel->downloadAt(srcIdx.row());
    }
    const bool hasSel = (dl != nullptr && !hash.isEmpty());

    // Rebuild menu each time to capture the current state
    if (!m_downloadMenu)
        m_downloadMenu = new QMenu(this);
    else
        m_downloadMenu->clear();

    const bool useOriginal = thePrefs.useOriginalIcons();
    auto ico = [&](const char* res) -> QIcon {
        return useOriginal ? QIcon(QStringLiteral(":/icons/") + QLatin1String(res))
                           : QIcon();
    };

    // -- 1. Priority (Download) submenu --
    auto* prioMenu = m_downloadMenu->addMenu(ico("FilePriority.ico"), tr("Priority (Download)"));
    prioMenu->setEnabled(hasSel);
    if (hasSel) {
        auto prioStr = [](int prio) -> QString {
            switch (prio) {
            case PrVeryLow:  return QStringLiteral("veryLow");
            case PrLow:      return QStringLiteral("low");
            case PrNormal:   return QStringLiteral("normal");
            case PrHigh:     return QStringLiteral("high");
            case PrVeryHigh: return QStringLiteral("veryHigh");
            default:         return {};
            }
        };
        auto addPrioAction = [&](const QString& text, int prio) {
            auto* act = prioMenu->addAction(text, this, [this, hash, prio]() {
                sendSetPriority(hash, prio, false);
            });
            if (!dl->isAutoDownPriority && dl->priority == prioStr(prio))
                act->setCheckable(true), act->setChecked(true);
        };
        addPrioAction(tr("Low"),    PrLow);
        addPrioAction(tr("Normal"), PrNormal);
        addPrioAction(tr("High"),   PrHigh);
        prioMenu->addSeparator();
        addPrioAction(tr("Very Low"),  PrVeryLow);
        addPrioAction(tr("Very High"), PrVeryHigh);
        prioMenu->addSeparator();
        auto* autoAct = prioMenu->addAction(tr("Auto"), this, [this, hash]() {
            sendSetPriority(hash, PrNormal, true);
        });
        if (dl->isAutoDownPriority)
            autoAct->setCheckable(true), autoAct->setChecked(true);
    }

    m_downloadMenu->addSeparator();

    // -- 2. Pause / Stop / Resume --
    {
        auto* pauseAct = m_downloadMenu->addAction(ico("Pause.ico"), tr("Pause"), this, [this, hash]() {
            sendDownloadAction(hash, 0);
        });
        if (!hasSel) {
            pauseAct->setEnabled(false);
        } else {
            const bool isComplete = (dl->status == QStringLiteral("complete"));
            pauseAct->setEnabled(!dl->isPaused && !dl->isStopped && !isComplete);
        }
    }
    {
        auto* stopAct = m_downloadMenu->addAction(ico("Stop.ico"), tr("Stop"), this, [this, hash]() {
            sendStopDownload(hash);
        });
        if (!hasSel) {
            stopAct->setEnabled(false);
        } else {
            const bool isComplete = (dl->status == QStringLiteral("complete"));
            stopAct->setEnabled(!dl->isStopped && !isComplete);
        }
    }
    {
        auto* resumeAct = m_downloadMenu->addAction(ico("Start.ico"), tr("Resume"), this, [this, hash]() {
            sendDownloadAction(hash, 1);
        });
        resumeAct->setEnabled(hasSel && (dl->isPaused || dl->isStopped));
    }

    m_downloadMenu->addSeparator();

    // -- 3. Cancel --
    {
        auto* cancelAct = m_downloadMenu->addAction(ico("Cancel.ico"), tr("Cancel"), this, [this, hash]() {
            sendDownloadAction(hash, 2);
        });
        cancelAct->setEnabled(hasSel);
    }

    m_downloadMenu->addSeparator();

    // -- 4. Open File / Preview / Details / Comments --
    {
        auto* act = m_downloadMenu->addAction(ico("FileOpen.ico"), tr("Open File"), this, [this, hash]() {
            sendOpenFile(hash);
        });
        const bool isComplete = hasSel && (dl->status == QStringLiteral("complete"));
        act->setEnabled(isComplete);
    }
    {
        auto* act = m_downloadMenu->addAction(ico("Preview.ico"), tr("Preview"), this, [this, hash]() {
            sendPreview(hash);
        });
        act->setEnabled(hasSel && dl->isPreviewPossible);
    }
    {
        auto* act = m_downloadMenu->addAction(ico("FileInfo.ico"), tr("Details..."), this, [this, hash]() {
            showDownloadDetails(hash);
        });
        act->setEnabled(hasSel);
    }
    {
        auto* act = m_downloadMenu->addAction(ico("FileComments.ico"), tr("Comments..."), this, [this, hash]() {
            showComments(hash);
        });
        act->setEnabled(hasSel);
    }

    m_downloadMenu->addSeparator();

    // -- 5. Clear Completed (greyed out when no completed downloads) --
    {
        bool hasCompleted = false;
        for (int i = 0; i < m_downloadModel->downloadCount(); ++i) {
            if (m_downloadModel->downloadAt(i)->status == QStringLiteral("complete")) {
                hasCompleted = true;
                break;
            }
        }
        auto* clearAct = m_downloadMenu->addAction(ico("DeleteAll.ico"), tr("Clear Completed"), this, [this]() {
            sendClearCompleted();
        });
        clearAct->setEnabled(hasCompleted);
    }

    m_downloadMenu->addSeparator();

    // -- 6. eD2K Links / Paste eD2K Links --
    {
        auto* act = m_downloadMenu->addAction(ico("eD2kLink.ico"), tr("eD2K Links..."), this, [this, hash]() {
            copyEd2kLink(hash);
        });
        act->setEnabled(hasSel);
    }
    {
        auto* pasteAct = m_downloadMenu->addAction(ico("eD2kLinkPaste.ico"), tr("Paste eD2K Links"));
        const QString clipText = QApplication::clipboard()->text().trimmed();
        const bool hasFileLink = clipText.contains(
            QStringLiteral("ed2k://|file|"), Qt::CaseInsensitive);
        pasteAct->setEnabled(hasFileLink && m_ipc && m_ipc->isConnected());
        connect(pasteAct, &QAction::triggered, this, [this, clipText]() {
            const QStringList lines = clipText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString& line : lines) {
                auto parsed = parseED2KLink(line.trimmed());
                if (!parsed)
                    continue;
                auto* fileLink = std::get_if<ED2KFileLink>(&*parsed);
                if (!fileLink)
                    continue;
                // Convert 16-byte MD4 hash to hex string
                QString hashHex;
                for (uint8 b : fileLink->hash)
                    hashHex += QStringLiteral("%1").arg(b, 2, 16, QLatin1Char('0'));
                Ipc::IpcMessage msg(Ipc::IpcMsgType::DownloadSearchFile);
                msg.append(hashHex);
                msg.append(fileLink->name);
                msg.append(static_cast<int64_t>(fileLink->size));
                m_ipc->sendRequest(std::move(msg));
            }
        });
    }

    m_downloadMenu->addSeparator();

    // -- 7. Find / Search Related --
    connect(m_downloadMenu->addAction(ico("Search.ico"), tr("Find...")),
            &QAction::triggered, this, &TransferPanel::showFindDialog);
    if (hasSel) {
        const QString fname = dl->fileName;
        auto* act = m_downloadMenu->addAction(ico("KadFileSearch.ico"), tr("Search Related Files"), this, [this, fname]() {
            searchRelated(fname);
        });
        act->setEnabled(true);
    }
    // TODO: Add "Web Services" submenu here — original eMule offers links to external file lookup
    // services (e.g., bitzi.com file report, Jigle search). Needs URL templates from webservices.dat config file.

    m_downloadMenu->addSeparator();

    // -- 8. Assign To Category --
    {
        auto* catMenu = m_downloadMenu->addMenu(ico("Category.ico"), tr("Assign To Category"));
        // Category 0 = "All" (default)
        auto* allAct = catMenu->addAction(tr("(All)"), this, [this, hash]() {
            sendSetCategory(hash, 0);
        });
        allAct->setEnabled(hasSel);
        // Add any user-defined categories from the tab bar
        for (int i = 1; i < m_categoryTabBar->count(); ++i) {
            const auto catId = m_categoryTabBar->tabData(i).toLongLong();
            auto* catAct = catMenu->addAction(m_categoryTabBar->tabText(i), this,
                [this, hash, catId]() {
                    sendSetCategory(hash, static_cast<int>(catId));
                });
            catAct->setEnabled(hasSel);
        }
        catMenu->setEnabled(hasSel && m_categoryTabBar->count() > 1);
    }

    m_downloadMenu->popup(m_downloadView->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void TransferPanel::setupUi()
{
    // Create models first (needed by both panes)
    m_downloadModel    = new DownloadListModel(this);
    m_uploadingModel   = new ClientListModel(ClientListMode::Uploading, this);
    m_downloadingModel = new ClientListModel(ClientListMode::Downloading, this);
    m_onQueueModel     = new ClientListModel(ClientListMode::OnQueue, this);
    m_knownModel       = new ClientListModel(ClientListMode::KnownClients, this);

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Left-side vertical action toolbar (MFC CToolbarWnd)
    m_actionToolbar = createActionToolbar();
    mainLayout->addWidget(m_actionToolbar);

    // Right side: splitter with downloads (top) and clients (bottom)
    m_vertSplitter = new QSplitter(Qt::Vertical, this);
    m_vertSplitter->setHandleWidth(4);
    m_vertSplitter->setChildrenCollapsible(false);
    m_vertSplitter->setStyleSheet(
        QStringLiteral("QSplitter::handle { background: palette(mid); }"));

    m_vertSplitter->addWidget(createDownloadsSection());
    m_vertSplitter->addWidget(createBottomPane());
    m_vertSplitter->setStretchFactor(0, 3);
    m_vertSplitter->setStretchFactor(1, 2);

    theUiState.bindTransferSplitter(m_vertSplitter);

    mainLayout->addWidget(m_vertSplitter, 1);

    // Restore saved bottom view
    QSettings settings;
    const int savedBottom = settings.value(QStringLiteral("transfer/bottomView"), 0).toInt();
    setBottomClientView(savedBottom);
}

QWidget* TransferPanel::createDownloadsSection()
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- Header row: icon + bold label (left) + category tab bar (right) ---
    auto* headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(4, 0, 0, 0);
    headerRow->setSpacing(2);

    // Green arrow icon matching MFC downloads header
    auto* dlIcon = new QLabel;
    dlIcon->setFixedSize(16, 16);
    dlIcon->setScaledContents(true);
    dlIcon->setPixmap(QIcon(QStringLiteral(":/icons/DownloadFiles.ico")).pixmap(16, 16));
    headerRow->addWidget(dlIcon);

    m_downloadsLabel = new QLabel(tr("Downloads (0)"));
    QFont bold = m_downloadsLabel->font();
    bold.setBold(true);
    m_downloadsLabel->setFont(bold);
    m_downloadsLabel->setFixedHeight(22);
    headerRow->addWidget(m_downloadsLabel);

    headerRow->addStretch(1);

    // Category tab bar — right-aligned
    m_categoryTabBar = new QTabBar;
    m_categoryTabBar->setExpanding(false);
    m_categoryTabBar->setDocumentMode(true);
    m_categoryTabBar->addTab(tr("All"));
    m_categoryTabBar->setTabData(0, QVariant::fromValue(int64_t{0}));

    connect(m_categoryTabBar, &QTabBar::currentChanged, this, [this](int tabIdx) {
        const int64_t catId = m_categoryTabBar->tabData(tabIdx).toLongLong();
        static_cast<CategoryFilterProxy*>(m_categoryProxy)->setCategoryFilter(catId);
    });

    headerRow->addWidget(m_categoryTabBar);
    layout->addLayout(headerRow);

    // --- Download view (always visible) ---
    m_downloadProxy = new QSortFilterProxyModel(this);
    m_downloadProxy->setSourceModel(m_downloadModel);
    m_downloadProxy->setSortRole(Qt::UserRole);

    // Category filter proxy sits on top of download sort proxy
    auto* catProxy = new CategoryFilterProxy(this);
    catProxy->setSourceModel(m_downloadProxy);
    catProxy->setSortRole(Qt::UserRole);
    m_categoryProxy = catProxy;

    m_downloadView = new QTreeView;
    m_downloadView->setModel(m_categoryProxy);
    m_downloadView->setRootIsDecorated(true);
    m_downloadView->setAlternatingRowColors(true);
    m_downloadView->setSortingEnabled(true);
    m_downloadView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_downloadView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_downloadView->setUniformRowHeights(true);
    m_downloadView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_downloadView->setExpandsOnDoubleClick(false); // we handle double-click ourselves

    m_downloadView->setItemDelegateForColumn(
        DownloadListModel::ColProgress,
        new DownloadProgressDelegate(m_downloadView));

    connect(m_downloadView, &QTreeView::customContextMenuRequested,
            this, &TransferPanel::onDownloadContextMenu);
    connect(m_downloadView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &TransferPanel::updateActionStates);

    // Double-click toggles expand/collapse for download rows
    connect(m_downloadView, &QTreeView::doubleClicked, this, [this](const QModelIndex& proxyIdx) {
        if (!proxyIdx.isValid())
            return;

        // If this is a source (child) row, open client details
        if (proxyIdx.parent().isValid()) {
            const QModelIndex catSrcIdx = m_categoryProxy->mapToSource(proxyIdx);
            const QModelIndex srcIdx = m_downloadProxy->mapToSource(catSrcIdx);
            if (const auto* src = m_downloadModel->sourceAt(srcIdx))
                fetchAndShowClientDetails(src->userHash);
            return;
        }

        // Map through proxy chain to get the source model hash
        const QModelIndex catSrcIdx = m_categoryProxy->mapToSource(proxyIdx);
        const QModelIndex srcIdx = m_downloadProxy->mapToSource(catSrcIdx);
        const QString hash = m_downloadModel->hashAt(srcIdx.row());
        if (hash.isEmpty())
            return;

        if (m_downloadView->isExpanded(proxyIdx)) {
            m_downloadView->collapse(proxyIdx);
            m_expandedDownloads.remove(hash);
        } else {
            m_expandedDownloads.insert(hash);
            requestDownloadSources(hash);
            m_downloadView->expand(proxyIdx);
        }
    });

    // Track collapse via user interaction (clicking the arrow)
    connect(m_downloadView, &QTreeView::collapsed, this, [this](const QModelIndex& proxyIdx) {
        const QModelIndex catSrcIdx = m_categoryProxy->mapToSource(proxyIdx);
        const QModelIndex srcIdx = m_downloadProxy->mapToSource(catSrcIdx);
        const QString hash = m_downloadModel->hashAt(srcIdx.row());
        m_expandedDownloads.remove(hash);
    });

    // Track expand via user interaction (clicking the arrow)
    connect(m_downloadView, &QTreeView::expanded, this, [this](const QModelIndex& proxyIdx) {
        const QModelIndex catSrcIdx = m_categoryProxy->mapToSource(proxyIdx);
        const QModelIndex srcIdx = m_downloadProxy->mapToSource(catSrcIdx);
        const QString hash = m_downloadModel->hashAt(srcIdx.row());
        if (!hash.isEmpty() && !m_expandedDownloads.contains(hash)) {
            m_expandedDownloads.insert(hash);
            requestDownloadSources(hash);
        }
    });

    auto* header = m_downloadView->header();
    header->setStretchLastSection(true);
    header->setDefaultSectionSize(90);
    header->resizeSection(DownloadListModel::ColFileName, 220);
    header->resizeSection(DownloadListModel::ColSize, 65);
    header->resizeSection(DownloadListModel::ColCompleted, 65);
    header->resizeSection(DownloadListModel::ColSpeed, 65);
    header->resizeSection(DownloadListModel::ColProgress, 90);
    header->resizeSection(DownloadListModel::ColSources, 65);
    header->resizeSection(DownloadListModel::ColPriority, 70);
    header->resizeSection(DownloadListModel::ColStatus, 65);
    header->resizeSection(DownloadListModel::ColRemaining, 80);
    header->resizeSection(DownloadListModel::ColSeenComplete, 80);
    header->resizeSection(DownloadListModel::ColLastReception, 80);
    header->resizeSection(DownloadListModel::ColCategory, 60);
    header->resizeSection(DownloadListModel::ColAddedOn, 120);
    theUiState.bindHeaderView(header, QStringLiteral("downloads"));

    layout->addWidget(m_downloadView, 1);

    return widget;
}

QWidget* TransferPanel::createBottomPane()
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Bottom toolbar header
    m_toolbar2 = new TransferToolbar;
    m_toolbar2->setLabelText(tr("Uploading (0)"));
    m_toolbar2->addButton(QIcon(QStringLiteral(":/icons/Upload.ico")),         tr("Uploading"));
    m_toolbar2->addButton(QIcon(QStringLiteral(":/icons/Download.ico")),       tr("Downloading"));
    m_toolbar2->addButton(QIcon(QStringLiteral(":/icons/ClientsOnQueue.ico")), tr("On Queue"));
    m_toolbar2->addButton(QIcon(QStringLiteral(":/icons/ClientsKnown.ico")),   tr("Known Clients"));
    m_toolbar2->checkButton(0);
    m_toolbar2->setLeadingIcon(QIcon(QStringLiteral(":/icons/Upload.ico")));

    connect(m_toolbar2, &TransferToolbar::buttonClicked, this, [this](int id) {
        setBottomClientView(id);
    });

    layout->addWidget(m_toolbar2);

    // Bottom client stack — 4 separate views
    m_bottomClientStack = new QStackedWidget;

    m_uploadingView   = createClientView(m_uploadingModel,   QStringLiteral("clientsUploading2"));
    m_downloadingView = createClientView(m_downloadingModel, QStringLiteral("clientsDownloading2"));
    m_onQueueView     = createClientView(m_onQueueModel,     QStringLiteral("clientsOnQueue2"));
    m_knownView       = createClientView(m_knownModel,       QStringLiteral("clientsKnown2"));

    m_bottomClientStack->addWidget(m_uploadingView);
    m_bottomClientStack->addWidget(m_downloadingView);
    m_bottomClientStack->addWidget(m_onQueueView);
    m_bottomClientStack->addWidget(m_knownView);

    // Wire context menus on all 4 client views
    connect(m_uploadingView, &QTreeView::customContextMenuRequested,
            this, [this](const QPoint& p) { onClientContextMenu(m_uploadingView, m_uploadingModel, p); });
    connect(m_downloadingView, &QTreeView::customContextMenuRequested,
            this, [this](const QPoint& p) { onClientContextMenu(m_downloadingView, m_downloadingModel, p); });
    connect(m_onQueueView, &QTreeView::customContextMenuRequested,
            this, [this](const QPoint& p) { onClientContextMenu(m_onQueueView, m_onQueueModel, p); });
    connect(m_knownView, &QTreeView::customContextMenuRequested,
            this, [this](const QPoint& p) { onClientContextMenu(m_knownView, m_knownModel, p); });

    // Wire double-click on all 4 client views to open client details
    auto connectClientDoubleClick = [this](QTreeView* view, ClientListModel* model) {
        connect(view, &QTreeView::doubleClicked, this, [this, view, model](const QModelIndex& proxyIdx) {
            auto* proxy = qobject_cast<QSortFilterProxyModel*>(view->model());
            if (!proxy) return;
            const QModelIndex srcIdx = proxy->mapToSource(proxyIdx);
            const auto* client = model->clientAt(srcIdx.row());
            if (client)
                fetchAndShowClientDetails(client->userHash);
        });
    };
    connectClientDoubleClick(m_uploadingView,   m_uploadingModel);
    connectClientDoubleClick(m_downloadingView, m_downloadingModel);
    connectClientDoubleClick(m_onQueueView,     m_onQueueModel);
    connectClientDoubleClick(m_knownView,       m_knownModel);

    layout->addWidget(m_bottomClientStack, 1);

    // Bottom status label matching MFC: "Clients on queue:    N"
    m_queueLabel = new QLabel(tr("Clients on queue:   0"));
    m_queueLabel->setContentsMargins(4, 2, 4, 2);
    layout->addWidget(m_queueLabel);

    return widget;
}

QToolBar* TransferPanel::createActionToolbar()
{
    auto* toolbar = new QToolBar;
    toolbar->setOrientation(Qt::Vertical);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setMovable(false);
    toolbar->setFixedWidth(24);

    // Remove frame styling to match MFC narrow strip
    toolbar->setStyleSheet(QStringLiteral(
        "QToolBar { border: none; spacing: 1px; padding: 1px; }"
        "QToolButton { padding: 2px; }"));

    // Group 1: Priority / Pause / Stop / Resume / Cancel
    auto* actPriority = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/FilePriority.ico")), tr("Priority"));
    connect(actPriority, &QAction::triggered, this, &TransferPanel::showPriorityMenu);
    m_selectionActions.append(actPriority);

    auto* actPause = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/Pause.ico")), tr("Pause"));
    connect(actPause, &QAction::triggered, this, [this]() {
        sendDownloadAction(saveDownloadSelection(), 0);
    });
    m_selectionActions.append(actPause);

    auto* actStop = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/Stop.ico")), tr("Stop"));
    connect(actStop, &QAction::triggered, this, [this]() {
        sendStopDownload(saveDownloadSelection());
    });
    m_selectionActions.append(actStop);

    auto* actResume = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/Start.ico")), tr("Resume"));
    connect(actResume, &QAction::triggered, this, [this]() {
        sendDownloadAction(saveDownloadSelection(), 1);
    });
    m_selectionActions.append(actResume);

    auto* actCancel = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/Delete.ico")), tr("Cancel"));
    connect(actCancel, &QAction::triggered, this, [this]() {
        sendDownloadAction(saveDownloadSelection(), 2);
    });
    m_selectionActions.append(actCancel);

    toolbar->addSeparator();

    // Group 2: Open File / Open Folder / Preview / Details / Comments / eD2K Links
    auto* actOpenFile = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/FileOpen.ico")), tr("Open File"));
    connect(actOpenFile, &QAction::triggered, this, [this]() {
        sendOpenFile(saveDownloadSelection());
    });
    m_selectionActions.append(actOpenFile);

    auto* actOpenFolder = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/FolderOpen.ico")), tr("Open Folder"));
    connect(actOpenFolder, &QAction::triggered, this, [this]() {
        sendOpenFolder(saveDownloadSelection());
    });
    m_selectionActions.append(actOpenFolder);

    auto* actPreview = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/Preview.ico")), tr("Preview"));
    connect(actPreview, &QAction::triggered, this, [this]() {
        sendPreview(saveDownloadSelection());
    });
    m_selectionActions.append(actPreview);

    auto* actDetails = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/FileInfo.ico")), tr("Details"));
    connect(actDetails, &QAction::triggered, this, [this]() {
        showDownloadDetails(saveDownloadSelection());
    });
    m_selectionActions.append(actDetails);

    auto* actComments = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/FileComments.ico")), tr("Comments"));
    connect(actComments, &QAction::triggered, this, [this]() {
        showComments(saveDownloadSelection());
    });
    m_selectionActions.append(actComments);

    auto* actEd2kLinks = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/eD2kLink.ico")), tr("eD2K Links"),
        this, [this]() { copyEd2kLink(saveDownloadSelection()); });
    m_selectionActions.append(actEd2kLinks);

    toolbar->addSeparator();

    // Group 3: Assign To Category / Clear Completed / Search Related
    auto* actCategory = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/Category.ico")), tr("Assign To Category"));
    connect(actCategory, &QAction::triggered, this, [this]() {
        const QString hash = saveDownloadSelection();
        if (hash.isEmpty())
            return;
        // Show category popup at cursor
        QMenu catMenu(this);
        catMenu.addAction(tr("(All)"), this, [this, hash]() { sendSetCategory(hash, 0); });
        for (int i = 1; i < m_categoryTabBar->count(); ++i) {
            const auto catId = m_categoryTabBar->tabData(i).toLongLong();
            catMenu.addAction(m_categoryTabBar->tabText(i), this,
                [this, hash, catId]() { sendSetCategory(hash, static_cast<int>(catId)); });
        }
        catMenu.exec(QCursor::pos());
    });
    m_selectionActions.append(actCategory);

    // Clear Completed — greyed out when no completed downloads exist
    m_clearCompletedAction = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/DeleteAll.ico")), tr("Clear Completed"),
        this, [this]() { sendClearCompleted(); });
    m_clearCompletedAction->setEnabled(false);

    auto* actSearchRelated = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/KadFileSearch.ico")), tr("Search Related"));
    connect(actSearchRelated, &QAction::triggered, this, [this]() {
        const QString hash = saveDownloadSelection();
        for (int i = 0; i < m_downloadModel->downloadCount(); ++i) {
            const auto* dl = m_downloadModel->downloadAt(i);
            if (dl && dl->hash == hash) {
                searchRelated(dl->fileName);
                return;
            }
        }
    });
    m_selectionActions.append(actSearchRelated);

    toolbar->addSeparator();

    // Group 4: Find — NOT selection-dependent
    toolbar->addAction(
        QIcon(QStringLiteral(":/icons/Search.ico")), tr("Find"),
        this, &TransferPanel::showFindDialog);

    // Start with all selection-dependent actions disabled (nothing selected)
    for (auto* act : m_selectionActions)
        act->setEnabled(false);

    return toolbar;
}

QTreeView* TransferPanel::createClientView(ClientListModel* model,
                                            const QString& headerKey)
{
    auto* proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(model);
    proxy->setSortRole(Qt::UserRole);

    auto* view = new QTreeView;
    view->setModel(proxy);
    view->setRootIsDecorated(false);
    view->setAlternatingRowColors(true);
    view->setSortingEnabled(true);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setUniformRowHeights(true);
    view->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* hdr = view->header();
    hdr->setStretchLastSection(true);
    hdr->setDefaultSectionSize(100);
    hdr->resizeSection(0, 140); // User Name column
    theUiState.bindHeaderView(hdr, headerKey);

    return view;
}

// ---------------------------------------------------------------------------
// IPC data requests
// ---------------------------------------------------------------------------

void TransferPanel::requestDownloads()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::GetDownloads);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            m_downloadModel->clear();
            updateToolbarLabels();
            return;
        }

        const auto [selHash, selSource] = saveFullDownloadSelection();

        const QCborArray arr = resp.fieldArray(1);
        std::vector<DownloadRow> rows;
        rows.reserve(static_cast<size_t>(arr.size()));

        for (const auto& val : arr) {
            const QCborMap m = val.toMap();
            DownloadRow row;
            row.hash              = m.value(QStringLiteral("hash")).toString();
            row.fileName          = m.value(QStringLiteral("fileName")).toString();
            row.fileSize          = m.value(QStringLiteral("fileSize")).toInteger();
            row.completedSize     = m.value(QStringLiteral("completedSize")).toInteger();
            row.percentCompleted  = m.value(QStringLiteral("percentCompleted")).toDouble();
            row.status            = m.value(QStringLiteral("status")).toString();
            row.datarate          = m.value(QStringLiteral("datarate")).toInteger();
            row.sourceCount       = static_cast<int>(m.value(QStringLiteral("sourceCount")).toInteger());
            row.transferringSrcCount = static_cast<int>(m.value(QStringLiteral("transferringSrcCount")).toInteger());
            row.priority          = m.value(QStringLiteral("downPriority")).toString();
            row.isAutoDownPriority = m.value(QStringLiteral("isAutoDownPriority")).toBool();
            row.isPaused          = m.value(QStringLiteral("isPaused")).toBool();
            row.isStopped         = m.value(QStringLiteral("isStopped")).toBool();
            row.category          = m.value(QStringLiteral("category")).toInteger();
            row.lastSeenComplete  = m.value(QStringLiteral("lastSeenComplete")).toInteger();
            row.lastReception     = m.value(QStringLiteral("lastReception")).toInteger();
            row.addedOn           = m.value(QStringLiteral("addedOn")).toInteger();
            row.fileType          = m.value(QStringLiteral("fileType")).toString();
            row.requests          = m.value(QStringLiteral("requests")).toInteger();
            row.acceptedRequests  = m.value(QStringLiteral("acceptedReqs")).toInteger();
            row.transferredData   = m.value(QStringLiteral("transferredData")).toInteger();
            row.isPreviewPossible = m.value(QStringLiteral("isPreviewPossible")).toBool();
            if (auto arr = m.value(QStringLiteral("partMap")).toArray(); !arr.isEmpty()) {
                row.partMap.resize(static_cast<qsizetype>(arr.size()));
                for (qsizetype i = 0; i < arr.size(); ++i)
                    row.partMap[static_cast<qsizetype>(i)] = static_cast<char>(arr[i].toInteger(0));
            }
            rows.push_back(std::move(row));
        }

        m_downloadModel->setDownloads(std::move(rows));
        updateToolbarLabels();
        updateCategoryTabs();

        // Re-expand previously expanded downloads and refresh their sources
        // (must happen BEFORE restoring selection so source child rows are visible)
        for (const QString& expHash : m_expandedDownloads) {
            for (int row = 0; row < m_downloadModel->downloadCount(); ++row) {
                if (m_downloadModel->hashAt(row) == expHash) {
                    const QModelIndex srcIdx = m_downloadModel->index(row, 0);
                    const QModelIndex sortIdx = m_downloadProxy->mapFromSource(srcIdx);
                    const QModelIndex catIdx = m_categoryProxy->mapFromSource(sortIdx);
                    if (catIdx.isValid())
                        m_downloadView->expand(catIdx);
                    break;
                }
            }
            requestDownloadSources(expHash);
        }

        restoreFullDownloadSelection(selHash, selSource);
        updateActionStates();
        updateClearCompletedState();
    });
}

void TransferPanel::requestDownloadSources(const QString& hash)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;

    IpcMessage req(IpcMsgType::GetDownloadSources);
    req.append(hash);
    m_ipc->sendRequest(std::move(req), [this, hash](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;

        const QCborArray arr = resp.fieldArray(1);
        std::vector<SourceRow> sources;
        sources.reserve(static_cast<size_t>(arr.size()));

        for (const auto& val : arr) {
            const QCborMap m = val.toMap();
            SourceRow src;
            src.userName        = m.value(QStringLiteral("userName")).toString();
            src.software        = m.value(QStringLiteral("software")).toString();
            src.downloadState   = m.value(QStringLiteral("downloadState")).toString();
            src.remoteQueueRank = m.value(QStringLiteral("remoteQueueRank")).toInteger();
            src.transferredDown = m.value(QStringLiteral("transferredDown")).toInteger();
            src.sessionDown     = m.value(QStringLiteral("sessionDown")).toInteger();
            src.datarate        = m.value(QStringLiteral("datarate")).toInteger();
            src.availPartCount  = static_cast<int>(m.value(QStringLiteral("availPartCount")).toInteger());
            src.partCount       = static_cast<int>(m.value(QStringLiteral("partCount")).toInteger());
            src.sourceFrom      = static_cast<int>(m.value(QStringLiteral("sourceFrom")).toInteger());
            src.userHash        = m.value(QStringLiteral("userHash")).toString();
            src.ip              = m.value(QStringLiteral("ip")).toInteger();
            src.port            = m.value(QStringLiteral("port")).toInteger();
            src.isFriend        = m.value(QStringLiteral("isFriend")).toBool();
            if (auto spm = m.value(QStringLiteral("sourcePartMap")).toArray(); !spm.isEmpty()) {
                src.partMap.resize(static_cast<qsizetype>(spm.size()));
                for (qsizetype j = 0; j < spm.size(); ++j)
                    src.partMap[static_cast<qsizetype>(j)] = static_cast<char>(spm[j].toInteger(0));
            }
            sources.push_back(std::move(src));
        }

        m_downloadModel->setSources(hash, std::move(sources));
    });
}

void TransferPanel::requestUploads()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::GetUploads);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;

        const QCborMap result = resp.fieldMap(1);

        const QCborArray uploadingArr = result.value(QStringLiteral("uploading")).toArray();
        std::vector<ClientRow> uploadingRows;
        uploadingRows.reserve(static_cast<size_t>(uploadingArr.size()));

        const QCborArray waitingArr = result.value(QStringLiteral("waiting")).toArray();
        std::vector<ClientRow> waitingRows;
        waitingRows.reserve(static_cast<size_t>(waitingArr.size()));

        for (const auto& val : uploadingArr)
            uploadingRows.push_back(parseClient(val.toMap()));

        for (const auto& val : waitingArr)
            waitingRows.push_back(parseClient(val.toMap()));

        const QString selUp = saveClientSelection(m_uploadingView, m_uploadingModel);
        const QString selQ  = saveClientSelection(m_onQueueView, m_onQueueModel);
        m_uploadingModel->setClients(std::move(uploadingRows));
        m_onQueueModel->setClients(std::move(waitingRows));
        restoreClientSelection(m_uploadingView, m_uploadingModel, selUp);
        restoreClientSelection(m_onQueueView, m_onQueueModel, selQ);
        updateToolbarLabels();
    });
}

void TransferPanel::requestDownloadClients()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::GetDownloadClients);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;

        const QCborArray arr = resp.fieldArray(1);
        std::vector<ClientRow> rows;
        rows.reserve(static_cast<size_t>(arr.size()));

        for (const auto& val : arr)
            rows.push_back(parseClient(val.toMap()));

        const QString selDl = saveClientSelection(m_downloadingView, m_downloadingModel);
        m_downloadingModel->setClients(std::move(rows));
        restoreClientSelection(m_downloadingView, m_downloadingModel, selDl);
        updateToolbarLabels();
    });
}

void TransferPanel::requestKnownClients()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::GetKnownClients);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;

        const QCborArray arr = resp.fieldArray(1);
        std::vector<ClientRow> rows;
        rows.reserve(static_cast<size_t>(arr.size()));

        for (const auto& val : arr)
            rows.push_back(parseClient(val.toMap()));

        const QString selKn = saveClientSelection(m_knownView, m_knownModel);
        m_knownModel->setClients(std::move(rows));
        restoreClientSelection(m_knownView, m_knownModel, selKn);
        updateToolbarLabels();
    });
}

// ---------------------------------------------------------------------------
// Download actions
// ---------------------------------------------------------------------------

void TransferPanel::sendDownloadAction(const QString& hash, int action)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;

    IpcMsgType msgType;
    switch (action) {
    case 0: msgType = IpcMsgType::PauseDownload;  break;
    case 1: msgType = IpcMsgType::ResumeDownload; break;
    case 2: msgType = IpcMsgType::CancelDownload; break;
    default: return;
    }

    IpcMessage msg(msgType);
    msg.append(hash);
    m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
        requestDownloads();
    });
}

void TransferPanel::sendSetPriority(const QString& hash, int priority, bool isAuto)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;

    IpcMessage msg(IpcMsgType::SetDownloadPriority);
    msg.append(hash);
    msg.append(static_cast<int64_t>(priority));
    msg.append(isAuto);
    m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
        requestDownloads();
    });
}

void TransferPanel::sendClearCompleted()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage msg(IpcMsgType::ClearCompleted);
    m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
        requestDownloads();
    });
}

void TransferPanel::copyEd2kLink(const QString& hash)
{
    for (int i = 0; i < m_downloadModel->downloadCount(); ++i) {
        const auto* dl = m_downloadModel->downloadAt(i);
        if (dl && dl->hash == hash) {
            const QString link = QStringLiteral("ed2k://|file|%1|%2|%3|/")
                .arg(dl->fileName)
                .arg(dl->fileSize)
                .arg(dl->hash);
            QApplication::clipboard()->setText(link);
            return;
        }
    }
}

void TransferPanel::sendStopDownload(const QString& hash)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;
    IpcMessage msg(IpcMsgType::StopDownload);
    msg.append(hash);
    m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
        requestDownloads();
    });
}

void TransferPanel::sendOpenFile(const QString& hash)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;
    IpcMessage msg(IpcMsgType::OpenDownloadFile);
    msg.append(hash);
    m_ipc->sendRequest(std::move(msg));
}

void TransferPanel::sendOpenFolder(const QString& hash)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;
    IpcMessage msg(IpcMsgType::OpenDownloadFolder);
    msg.append(hash);
    m_ipc->sendRequest(std::move(msg));
}

void TransferPanel::sendPreview(const QString& hash)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;

    if (m_streamToken.isEmpty()) {
        logWarning(tr("Preview not available — web server is not running or stream token not received."));
        return;
    }

    // Construct the streaming URL via the daemon's web server
    const QString host = m_ipc->daemonHost();
    const uint16_t wsPort = thePrefs.webServerPort();
    const QString url = QStringLiteral("http://%1:%2/api/v1/downloads/%3/preview?token=%4")
                            .arg(host).arg(wsPort).arg(hash, m_streamToken);

    // Launch configured video player with the streaming URL
    const QString playerCmd = thePrefs.videoPlayerCommand();
    if (playerCmd.isEmpty()) {
        logWarning(tr("No video player configured. Set it in Options → Files."));
        return;
    }

    QString args = thePrefs.videoPlayerArgs();
    QStringList argList;
    if (args.contains(QStringLiteral("%1"))) {
        args.replace(QStringLiteral("%1"), url);
        argList = QProcess::splitCommand(args);
    } else {
        if (!args.isEmpty())
            argList = QProcess::splitCommand(args);
        argList.append(url);
    }

    QProcess::startDetached(playerCmd, argList);
}

void TransferPanel::sendSetCategory(const QString& hash, int category)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;
    IpcMessage msg(IpcMsgType::SetDownloadCategory);
    msg.append(hash);
    msg.append(static_cast<int64_t>(category));
    m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
        requestDownloads();
    });
}

void TransferPanel::showDownloadDetails(const QString& hash)
{
    fetchAndShowFileDetails(hash, FileDetailDialog::General);
}

void TransferPanel::showComments(const QString& hash)
{
    fetchAndShowFileDetails(hash, FileDetailDialog::Comments);
}

void TransferPanel::fetchAndShowFileDetails(const QString& hash,
                                             FileDetailDialog::Tab tab)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;
    IpcMessage msg(IpcMsgType::GetDownloadDetails);
    msg.append(hash);
    m_ipc->sendRequest(std::move(msg), [this, tab](const IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;
        const QCborMap details = resp.field(1).toMap();
        auto* dlg = new FileDetailDialog(details, tab, this);
        dlg->show();
    });
}

void TransferPanel::fetchAndShowClientDetails(const QString& clientHash)
{
    if (!m_ipc || !m_ipc->isConnected() || clientHash.isEmpty())
        return;
    IpcMessage msg(IpcMsgType::GetClientDetails);
    msg.append(clientHash);
    m_ipc->sendRequest(std::move(msg), [this](const IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;
        const QCborMap details = resp.field(1).toMap();
        auto* dlg = new ClientDetailDialog(details, this);
        dlg->show();
    });
}

void TransferPanel::searchRelated(const QString& fileName)
{
    // Strip extension and emit search request
    const int dotIdx = fileName.lastIndexOf(QLatin1Char('.'));
    const QString term = (dotIdx > 0) ? fileName.left(dotIdx) : fileName;
    emit searchRequested(term);
}

// ---------------------------------------------------------------------------
// Selection preservation
// ---------------------------------------------------------------------------

QString TransferPanel::saveDownloadSelection() const
{
    const auto sel = m_downloadView->selectionModel()->currentIndex();
    if (!sel.isValid())
        return {};

    // Map through category proxy → sort proxy → source model
    const QModelIndex catSrcIdx = m_categoryProxy->mapToSource(sel);
    const QModelIndex srcIdx = m_downloadProxy->mapToSource(catSrcIdx);

    // If a source (child) row is selected, return the parent download's hash
    if (m_downloadModel->isSourceRow(srcIdx)) {
        const QModelIndex parentSrcIdx = srcIdx.parent();
        return m_downloadModel->hashAt(parentSrcIdx.row());
    }

    return m_downloadModel->hashAt(srcIdx.row());
}

std::pair<QString, QString> TransferPanel::saveFullDownloadSelection() const
{
    const auto sel = m_downloadView->selectionModel()->currentIndex();
    if (!sel.isValid())
        return {};

    const QModelIndex catSrcIdx = m_categoryProxy->mapToSource(sel);
    const QModelIndex srcIdx = m_downloadProxy->mapToSource(catSrcIdx);

    if (m_downloadModel->isSourceRow(srcIdx)) {
        const QModelIndex parentSrcIdx = srcIdx.parent();
        const QString fileHash = m_downloadModel->hashAt(parentSrcIdx.row());
        const auto* src = m_downloadModel->sourceAt(srcIdx);
        const QString sourceHash = src ? src->userHash : QString{};
        return {fileHash, sourceHash};
    }

    return {m_downloadModel->hashAt(srcIdx.row()), {}};
}

void TransferPanel::restoreDownloadSelection(const QString& key)
{
    restoreFullDownloadSelection(key, {});
}

void TransferPanel::restoreFullDownloadSelection(const QString& fileHash, const QString& sourceHash)
{
    if (fileHash.isEmpty())
        return;

    for (int row = 0; row < m_downloadModel->downloadCount(); ++row) {
        if (m_downloadModel->hashAt(row) != fileHash)
            continue;

        // Try to restore source-level selection first
        if (!sourceHash.isEmpty()) {
            const QModelIndex parentSrcIdx = m_downloadModel->index(row, 0);
            const int childCount = m_downloadModel->rowCount(parentSrcIdx);
            for (int c = 0; c < childCount; ++c) {
                const QModelIndex childSrcIdx = m_downloadModel->index(c, 0, parentSrcIdx);
                const auto* src = m_downloadModel->sourceAt(childSrcIdx);
                if (src && src->userHash == sourceHash) {
                    const QModelIndex sortIdx = m_downloadProxy->mapFromSource(childSrcIdx);
                    const QModelIndex catIdx = m_categoryProxy->mapFromSource(sortIdx);
                    if (catIdx.isValid()) {
                        m_downloadView->setCurrentIndex(catIdx);
                        m_downloadView->scrollTo(catIdx);
                    }
                    return;
                }
            }
        }

        // Fall back to selecting the parent download row
        const QModelIndex srcIdx = m_downloadModel->index(row, 0);
        const QModelIndex sortIdx = m_downloadProxy->mapFromSource(srcIdx);
        const QModelIndex catIdx = m_categoryProxy->mapFromSource(sortIdx);
        if (catIdx.isValid()) {
            m_downloadView->setCurrentIndex(catIdx);
            m_downloadView->scrollTo(catIdx);
        }
        return;
    }
}

QString TransferPanel::saveClientSelection(QTreeView* view, ClientListModel* model) const
{
    const auto sel = view->selectionModel()->currentIndex();
    if (!sel.isValid())
        return {};

    // Map through proxy to source model
    const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(view->model());
    if (!proxy)
        return {};

    const QModelIndex srcIdx = proxy->mapToSource(sel);
    const auto* client = model->clientAt(srcIdx.row());
    return client ? client->userHash : QString{};
}

void TransferPanel::restoreClientSelection(QTreeView* view, ClientListModel* model, const QString& key)
{
    if (key.isEmpty())
        return;

    const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(view->model());
    if (!proxy)
        return;

    for (int row = 0; row < model->clientCount(); ++row) {
        const auto* client = model->clientAt(row);
        if (client && client->userHash == key) {
            const QModelIndex srcIdx = model->index(row, 0);
            const QModelIndex proxyIdx = proxy->mapFromSource(srcIdx);
            if (proxyIdx.isValid()) {
                view->setCurrentIndex(proxyIdx);
                view->scrollTo(proxyIdx);
            }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// View switching
// ---------------------------------------------------------------------------

void TransferPanel::setBottomClientView(int index)
{
    if (index >= 0 && index < m_bottomClientStack->count()) {
        m_bottomClientStack->setCurrentIndex(index);
        m_toolbar2->checkButton(index);
        QSettings settings;
        settings.setValue(QStringLiteral("transfer/bottomView"), index);

        // Update leading icon to match selected client view
        static constexpr const char* iconPaths[] = {
            ":/icons/Upload.ico",
            ":/icons/Download.ico",
            ":/icons/ClientsOnQueue.ico",
            ":/icons/ClientsKnown.ico",
        };
        m_toolbar2->setLeadingIcon(QIcon(QString::fromLatin1(iconPaths[index])));

        updateToolbarLabels();
    }
}

void TransferPanel::updateActionStates()
{
    const bool hasSelection = !saveDownloadSelection().isEmpty();
    for (auto* act : m_selectionActions)
        act->setEnabled(hasSelection);
}

void TransferPanel::updateToolbarLabels()
{
    const int dlCount  = m_downloadModel->downloadCount();
    const int ulCount  = m_uploadingModel->clientCount();
    const int dlcCount = m_downloadingModel->clientCount();
    const int qCount   = m_onQueueModel->clientCount();
    const int knCount  = m_knownModel->clientCount();

    // Top label always shows download count
    m_downloadsLabel->setText(tr("Downloads (%1)").arg(dlCount));

    // Bottom toolbar label — based on selected bottom client view
    const int bottomIdx = m_bottomClientStack->currentIndex();
    switch (bottomIdx) {
    case 0: m_toolbar2->setLabelText(tr("Uploading (%1)").arg(ulCount));      break;
    case 1: m_toolbar2->setLabelText(tr("Downloading (%1)").arg(dlcCount));   break;
    case 2: m_toolbar2->setLabelText(tr("On Queue (%1)").arg(qCount));        break;
    case 3: m_toolbar2->setLabelText(tr("Known Clients (%1)").arg(knCount));  break;
    default: break;
    }

    m_queueLabel->setText(tr("Clients on queue:   %1").arg(qCount));
}

void TransferPanel::updateCategoryTabs()
{
    // Scan downloads for unique non-zero category values
    QSet<int64_t> cats;
    for (int i = 0; i < m_downloadModel->downloadCount(); ++i) {
        const auto* dl = m_downloadModel->downloadAt(i);
        if (dl && dl->category != 0)
            cats.insert(dl->category);
    }

    // Only rebuild tabs when the set has changed
    if (cats == m_categorySet)
        return;
    m_categorySet = cats;

    // Block signals to avoid triggering filter changes during rebuild
    const QSignalBlocker blocker(m_categoryTabBar);

    // Remember current selection
    const int64_t currentCat = m_categoryTabBar->currentIndex() >= 0
        ? m_categoryTabBar->tabData(m_categoryTabBar->currentIndex()).toLongLong()
        : 0;

    // Remove all tabs except "All" (index 0)
    while (m_categoryTabBar->count() > 1)
        m_categoryTabBar->removeTab(1);

    // Add category tabs
    QList<int64_t> sorted(cats.begin(), cats.end());
    std::sort(sorted.begin(), sorted.end());
    for (int64_t cat : sorted) {
        const int idx = m_categoryTabBar->addTab(tr("Cat %1").arg(cat));
        m_categoryTabBar->setTabData(idx, QVariant::fromValue(cat));
    }

    // Restore previous selection
    int restoreIdx = 0;
    for (int i = 0; i < m_categoryTabBar->count(); ++i) {
        if (m_categoryTabBar->tabData(i).toLongLong() == currentCat) {
            restoreIdx = i;
            break;
        }
    }
    m_categoryTabBar->setCurrentIndex(restoreIdx);
}

void TransferPanel::showPriorityMenu()
{
    const QString hash = saveDownloadSelection();
    if (hash.isEmpty())
        return;

    // Find the download row for current state
    const DownloadRow* dl = nullptr;
    for (int i = 0; i < m_downloadModel->downloadCount(); ++i) {
        if (m_downloadModel->hashAt(i) == hash) {
            dl = m_downloadModel->downloadAt(i);
            break;
        }
    }
    if (!dl)
        return;

    auto prioStr = [](int prio) -> QString {
        switch (prio) {
        case PrVeryLow:  return QStringLiteral("veryLow");
        case PrLow:      return QStringLiteral("low");
        case PrNormal:   return QStringLiteral("normal");
        case PrHigh:     return QStringLiteral("high");
        case PrVeryHigh: return QStringLiteral("veryHigh");
        default:         return {};
        }
    };

    QMenu menu(this);
    auto addPrioAction = [&](const QString& text, int prio) {
        auto* act = menu.addAction(text, this, [this, hash, prio]() {
            sendSetPriority(hash, prio, false);
        });
        if (!dl->isAutoDownPriority && dl->priority == prioStr(prio))
            act->setCheckable(true), act->setChecked(true);
    };

    addPrioAction(tr("Low"),    PrLow);
    addPrioAction(tr("Normal"), PrNormal);
    addPrioAction(tr("High"),   PrHigh);
    menu.addSeparator();
    addPrioAction(tr("Very Low"),  PrVeryLow);
    addPrioAction(tr("Very High"), PrVeryHigh);
    menu.addSeparator();
    {
        auto* autoAct = menu.addAction(tr("Auto"), this, [this, hash]() {
            sendSetPriority(hash, PrNormal, true);
        });
        if (dl->isAutoDownPriority)
            autoAct->setCheckable(true), autoAct->setChecked(true);
    }

    // Show below the priority button in the action toolbar
    menu.exec(QCursor::pos());
}

void TransferPanel::showFindDialog()
{
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Search"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QFormLayout(dlg);

    auto* searchEdit = new QLineEdit(dlg);
    layout->addRow(tr("Search for:"), searchEdit);

    auto* columnCombo = new QComboBox(dlg);
    columnCombo->addItem(tr("File Name"),       DownloadListModel::ColFileName);
    columnCombo->addItem(tr("Size"),            DownloadListModel::ColSize);
    columnCombo->addItem(tr("Transferred"),     DownloadListModel::ColCompleted);
    columnCombo->addItem(tr("Speed"),           DownloadListModel::ColSpeed);
    columnCombo->addItem(tr("Progress"),        DownloadListModel::ColProgress);
    columnCombo->addItem(tr("Sources"),         DownloadListModel::ColSources);
    columnCombo->addItem(tr("Priority"),        DownloadListModel::ColPriority);
    columnCombo->addItem(tr("Status"),          DownloadListModel::ColStatus);
    columnCombo->addItem(tr("Remaining"),       DownloadListModel::ColRemaining);
    columnCombo->addItem(tr("Last reception"),  DownloadListModel::ColLastReception);
    columnCombo->addItem(tr("Category"),        DownloadListModel::ColCategory);
    columnCombo->addItem(tr("Added On"),        DownloadListModel::ColAddedOn);
    layout->addRow(tr("Search in column:"), columnCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    layout->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    connect(dlg, &QDialog::accepted, this, [this, searchEdit, columnCombo]() {
        const QString term = searchEdit->text().trimmed();
        if (term.isEmpty())
            return;

        const int column = columnCombo->currentData().toInt();

        for (int row = 0; row < m_categoryProxy->rowCount(); ++row) {
            const QModelIndex idx = m_categoryProxy->index(row, column);
            if (idx.data(Qt::DisplayRole).toString().contains(term, Qt::CaseInsensitive)) {
                m_downloadView->setCurrentIndex(idx);
                m_downloadView->scrollTo(idx);
                return;
            }
        }
    });

    dlg->exec();
}

void TransferPanel::onClientContextMenu(QTreeView* view, ClientListModel* model,
                                         const QPoint& pos)
{
    // Resolve the clicked row through the view's proxy model
    const ClientRow* client = nullptr;
    const QModelIndex proxyIdx = view->indexAt(pos);
    if (proxyIdx.isValid()) {
        auto* proxy = qobject_cast<QSortFilterProxyModel*>(view->model());
        if (proxy) {
            const QModelIndex srcIdx = proxy->mapToSource(proxyIdx);
            client = model->clientAt(srcIdx.row());
        }
    }

    QMenu menu(this);

    const bool useOriginal = thePrefs.useOriginalIcons();
    auto ico = [&](const char* res) -> QIcon {
        return useOriginal ? QIcon(QStringLiteral(":/icons/") + QLatin1String(res))
                           : QIcon();
    };

    // 1. Details...
    if (client) {
        const QString clientHash = client->userHash;
        auto* detailsAct = menu.addAction(ico("UserDetails.ico"), tr("Details..."), this, [this, clientHash]() {
            fetchAndShowClientDetails(clientHash);
        });
        detailsAct->setEnabled(true);
        QFont f = detailsAct->font();
        f.setBold(true);
        detailsAct->setFont(f);
    }

    // 2. Add To Friends
    auto* addFriendAct = menu.addAction(ico("UserAdd.ico"), tr("Add To Friends"));
    addFriendAct->setEnabled(client != nullptr && !client->isFriend);
    if (client && !client->isFriend) {
        const QString hash = client->userHash;
        const QString name = client->userName;
        const auto ip   = client->ip;
        const auto port = client->port;
        connect(addFriendAct, &QAction::triggered, this, [this, hash, name, ip, port]() {
            if (!m_ipc || !m_ipc->isConnected())
                return;
            IpcMessage msg(IpcMsgType::AddFriend);
            msg.append(hash);
            msg.append(name);
            msg.append(static_cast<int64_t>(ip));
            msg.append(static_cast<int64_t>(port));
            m_ipc->sendRequest(std::move(msg));
        });
    }

    // 3. Send Message
    auto* sendMsgAct = menu.addAction(ico("UserMessage.ico"), tr("Send Message"));
    sendMsgAct->setEnabled(client != nullptr);
    if (client) {
        const QString clientHash = client->userHash;
        connect(sendMsgAct, &QAction::triggered, this, [this, clientHash]() {
            bool ok = false;
            const QString text = QInputDialog::getText(
                this, tr("Send Message"), tr("Message:"), QLineEdit::Normal, {}, &ok);
            if (!ok || text.isEmpty() || !m_ipc || !m_ipc->isConnected())
                return;
            IpcMessage msg(IpcMsgType::SendChatMessage);
            msg.append(clientHash);
            msg.append(text);
            m_ipc->sendRequest(std::move(msg));
        });
    }

    // 4. View Shared Files
    auto* viewSharedAct = menu.addAction(ico("UserFiles.ico"), tr("View Shared Files"));
    viewSharedAct->setEnabled(client != nullptr);
    if (client) {
        const QString clientHash = client->userHash;
        connect(viewSharedAct, &QAction::triggered, this, [this, clientHash]() {
            if (!m_ipc || !m_ipc->isConnected())
                return;
            IpcMessage msg(IpcMsgType::RequestClientSharedFiles);
            msg.append(clientHash);
            m_ipc->sendRequest(std::move(msg));
        });
    }

    menu.addSeparator();

    // 5. Find...
    menu.addAction(ico("Search.ico"), tr("Find..."), this, [this, view]() {
        showClientFindDialog(view);
    });

    menu.exec(view->viewport()->mapToGlobal(pos));
}

void TransferPanel::showSourceContextMenu(const SourceRow& src, const QPoint& globalPos)
{
    QMenu menu(this);

    const bool useOriginal = thePrefs.useOriginalIcons();
    auto ico = [&](const char* res) -> QIcon {
        return useOriginal ? QIcon(QStringLiteral(":/icons/") + QLatin1String(res))
                           : QIcon();
    };

    // 1. Details...
    {
        const QString srcHash = src.userHash;
        auto* detailsAct = menu.addAction(ico("UserDetails.ico"), tr("Details..."), this, [this, srcHash]() {
            fetchAndShowClientDetails(srcHash);
        });
        QFont f = detailsAct->font();
        f.setBold(true);
        detailsAct->setFont(f);
    }

    // 2. Add To Friends
    auto* addFriendAct = menu.addAction(ico("UserAdd.ico"), tr("Add To Friends"));
    addFriendAct->setEnabled(!src.isFriend);
    if (!src.isFriend) {
        const QString hash = src.userHash;
        const QString name = src.userName;
        const auto ip   = src.ip;
        const auto port = src.port;
        connect(addFriendAct, &QAction::triggered, this, [this, hash, name, ip, port]() {
            if (!m_ipc || !m_ipc->isConnected())
                return;
            IpcMessage msg(IpcMsgType::AddFriend);
            msg.append(hash);
            msg.append(name);
            msg.append(static_cast<int64_t>(ip));
            msg.append(static_cast<int64_t>(port));
            m_ipc->sendRequest(std::move(msg));
        });
    }

    // 3. Send Message
    {
        const QString clientHash = src.userHash;
        menu.addAction(ico("UserMessage.ico"), tr("Send Message"), this, [this, clientHash]() {
            bool ok = false;
            const QString text = QInputDialog::getText(
                this, tr("Send Message"), tr("Message:"), QLineEdit::Normal, {}, &ok);
            if (!ok || text.isEmpty() || !m_ipc || !m_ipc->isConnected())
                return;
            IpcMessage msg(IpcMsgType::SendChatMessage);
            msg.append(clientHash);
            msg.append(text);
            m_ipc->sendRequest(std::move(msg));
        });
    }

    // 4. View Shared Files
    {
        const QString clientHash = src.userHash;
        menu.addAction(ico("UserFiles.ico"), tr("View Shared Files"), this, [this, clientHash]() {
            if (!m_ipc || !m_ipc->isConnected())
                return;
            IpcMessage msg(IpcMsgType::RequestClientSharedFiles);
            msg.append(clientHash);
            m_ipc->sendRequest(std::move(msg));
        });
    }

    menu.addSeparator();

    // 5. Find...
    menu.addAction(ico("Search.ico"), tr("Find..."), this, [this]() {
        showClientFindDialog(m_downloadView);
    });

    menu.exec(globalPos);
}

void TransferPanel::showClientFindDialog(QTreeView* view)
{
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(view->model());
    if (!proxy)
        return;

    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Search"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QFormLayout(dlg);

    auto* searchEdit = new QLineEdit(dlg);
    layout->addRow(tr("Search for:"), searchEdit);

    auto* columnCombo = new QComboBox(dlg);
    const int colCount = proxy->columnCount();
    for (int col = 0; col < colCount; ++col) {
        const QString label = proxy->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString();
        if (!label.isEmpty())
            columnCombo->addItem(label, col);
    }
    layout->addRow(tr("Search in column:"), columnCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    layout->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    connect(dlg, &QDialog::accepted, this, [view, proxy, searchEdit, columnCombo]() {
        const QString term = searchEdit->text().trimmed();
        if (term.isEmpty())
            return;

        const int column = columnCombo->currentData().toInt();

        for (int row = 0; row < proxy->rowCount(); ++row) {
            const QModelIndex idx = proxy->index(row, column);
            if (idx.data(Qt::DisplayRole).toString().contains(term, Qt::CaseInsensitive)) {
                view->setCurrentIndex(idx);
                view->scrollTo(idx);
                return;
            }
        }
    });

    dlg->exec();
}

void TransferPanel::updateClearCompletedState()
{
    if (!m_clearCompletedAction)
        return;

    bool hasCompleted = false;
    for (int i = 0; i < m_downloadModel->downloadCount(); ++i) {
        if (m_downloadModel->downloadAt(i)->status == QStringLiteral("complete")) {
            hasCompleted = true;
            break;
        }
    }
    m_clearCompletedAction->setEnabled(hasCompleted);
}

} // namespace eMule
