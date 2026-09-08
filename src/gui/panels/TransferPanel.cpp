#include "pch.h"
/// @file TransferPanel.cpp
/// @brief Transfer tab panel — implementation.

#include "panels/TransferPanel.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/AbstractListView.h"
#include "controls/ClientListModel.h"
#include "controls/DownloadListModel.h"
#include "controls/DownloadProgressDelegate.h"
#include "controls/TransferToolbar.h"
#include "dialogs/CategoryDialog.h"
#include "dialogs/ClientDetailDialog.h"
#include "dialogs/FindInListDialog.h"
#include "utils/StatusBarNotifier.h"
#include "utils/Ed2kLinkImporter.h"
#include "utils/ListActivation.h"
#include "utils/MenuUtils.h"
#include "utils/PanelPoller.h"
#include "utils/PreviewLauncher.h"
#include "utils/ViewNavigation.h"
#include "utils/WebServices.h"

#include "IpcMessage.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QScrollBar>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QPointer>
#include <QTabBar>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QUrl>
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
    row.queueSessionPayloadUp = m.value(QStringLiteral("queueSessionPayloadUp")).toInteger();
    row.upDatarate      = m.value(QStringLiteral("upDatarate")).toInteger();
    row.sessionDown     = m.value(QStringLiteral("sessionDown")).toInteger();
    row.askedCount      = m.value(QStringLiteral("askedCount")).toInteger();
    row.waitStartTime   = m.value(QStringLiteral("waitStartTime")).toInteger();
    row.partCount       = static_cast<int>(m.value(QStringLiteral("partCount")).toInteger());
    row.upPartCount     = static_cast<int>(m.value(QStringLiteral("upPartCount")).toInteger());
    row.availPartCount  = static_cast<int>(m.value(QStringLiteral("availPartCount")).toInteger());
    row.remoteQueueRank = static_cast<int>(m.value(QStringLiteral("remoteQueueRank")).toInteger());
    row.sourceFrom      = static_cast<int>(m.value(QStringLiteral("sourceFrom")).toInteger());
    row.ip              = static_cast<uint32_t>(m.value(QStringLiteral("ip")).toInteger());
    row.addr            = m.value(QStringLiteral("addr")).toString();
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

    /// The sort proxy this one is stacked on. Needed because filterAcceptsRow's
    /// `sourceRow` is in *that* proxy's coordinate space, not the model's — the
    /// bug that made every category tab show every download.
    void setDownloadProxy(QAbstractProxyModel* proxy) { m_downloadProxy = proxy; }

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
        if (!m_downloadProxy)
            return true;

        // Two hops: sourceModel() is the sort proxy, so the row has to be
        // mapped through it before the model can be asked about it. Casting
        // sourceModel() straight to DownloadListModel* always failed, and the
        // `if (!model) return true` fallback then accepted every row — the tab
        // bar looked like it worked and filtered nothing.
        auto* model = qobject_cast<DownloadListModel*>(m_downloadProxy->sourceModel());
        if (!model)
            return true;

        const QModelIndex srcIdx =
            m_downloadProxy->mapToSource(m_downloadProxy->index(sourceRow, 0, sourceParent));
        if (!srcIdx.isValid())
            return true;

        const auto* dl = model->downloadAt(srcIdx.row());
        return dl && dl->category == m_category;
    }

private:
    QAbstractProxyModel* m_downloadProxy = nullptr;
    int64_t m_category = 0;
};

} // anonymous namespace

TransferPanel::TransferPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    m_poller = new PanelPoller(this, [this] { onRefreshTimer(); });
}

TransferPanel::~TransferPanel() = default;

void TransferPanel::switchToSubTab(int index)
{
    if (index >= 0 && index < ClientViewCount)
        setBottomView(index);
}

void TransferPanel::switchToTopView(int index)
{
    if (index >= 0 && index <= ClientViewCount)
        setTopView(index);
}

void TransferPanel::setIpcClient(IpcClient* client)
{
    m_ipc = client;

    auto clearAll = [this] {
        m_downloadModel->clear();
        for (const auto& slot : m_clients)
            slot.model->clear();
        m_queueCount = -1;
        updateToolbarLabels();
    };

    if (!m_ipc) {
        m_poller->setEnabled(false);
        clearAll();
        return;
    }

    connect(m_ipc, &IpcClient::connected, this, [this]() {
        m_poller->setInterval(m_ipc->pollingInterval());
        m_poller->setEnabled(true);
        // Not on the poll timer: the list changes only when someone edits it,
        // and the tab bar has to exist before the first download arrives.
        requestCategories();
    });
    connect(m_ipc, &IpcClient::disconnected, this, [this, clearAll]() {
        m_poller->setEnabled(false);
        clearAll();
    });

    // Push events pull the next poll forward rather than issuing their own
    // refetch. PushDownloadUpdate alone fires once per source added or removed
    // on every download, and answering each one with a full list refetch — on
    // top of the poll that was going to fetch it anyway — is what made the
    // socket unusable for anything else during a busy transfer.
    auto nudge = [this](const IpcMessage&) { m_poller->nudge(); };
    connect(m_ipc, &IpcClient::downloadAdded,       this, nudge);
    connect(m_ipc, &IpcClient::downloadRemoved,     this, nudge);
    connect(m_ipc, &IpcClient::downloadUpdated,     this, nudge);
    connect(m_ipc, &IpcClient::uploadUpdated,       this, nudge);
    connect(m_ipc, &IpcClient::knownClientsChanged, this, nudge);

    // Another GUI — or this one's own edit — changed the list.
    connect(m_ipc, &IpcClient::categoriesChanged, this,
            [this](const IpcMessage&) { requestCategories(); });

    // "Clients on queue: N" sits under the bottom pane whichever list is showing, but
    // the count is all it needs. Taking it off the stats push — which the daemon
    // broadcasts about once a second anyway — is what lets the tick skip GetUploads
    // when neither queue list is up.
    connect(m_ipc, &IpcClient::statsUpdated, this, [this](const IpcMessage& msg) {
        const QCborValue waiting = msg.fieldMap(0).value(QStringLiteral("upWaiting"));
        if (waiting.isInteger()) {
            m_queueCount = static_cast<int>(waiting.toInteger());
            updateToolbarLabels();
        }
    });

    if (m_ipc->isConnected()) {
        m_poller->setInterval(m_ipc->pollingInterval());
        m_poller->setEnabled(true);
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void TransferPanel::onRefreshTimer()
{
    // Fetch only what is mounted. A list lives in at most one pane, so this is two
    // lists at most — one when the top pane is on Downloads. Everything skipped here
    // is expensive to skip for a hidden list: GetKnownClients serialises every client
    // the session has seen and GetDownloadClients walks every PartFile against every
    // one of its sources. applyViews() refreshes on a switch, so a list just switched
    // to is never left blank.
    const int topClient = (m_topView == TopView::Downloads)
                        ? -1 : m_topView - TopView::TopClientFirst;
    auto onScreen = [this, topClient](int clientView) {
        return clientView == topClient || clientView == m_bottomView;
    };

    if (m_topView == TopView::Downloads)
        requestDownloads();

    // One GetUploads reply fills both the Uploading and the On Queue list.
    if (onScreen(Uploading) || onScreen(OnQueue))
        requestUploads();
    if (onScreen(Downloading))
        requestDownloadClients();
    if (onScreen(Known))
        requestKnownClients();
}

void TransferPanel::onDownloadContextMenu(const QPoint& pos)
{
    // Check if the right-click is on a source (child) row — show client menu instead
    const QModelIndex proxyIdx = m_downloadView->indexAt(pos);
    if (proxyIdx.isValid()) {
        const QModelIndex catSrcIdx = m_categoryProxy->mapToSource(proxyIdx);
        const QModelIndex srcIdx = m_downloadProxy->mapToSource(catSrcIdx);
        if (m_downloadModel->isSourceRow(srcIdx)) {
            if (const auto* src = m_downloadModel->sourceAt(srcIdx))
                showSourceContextMenu(*src, m_downloadModel->hashAt(srcIdx.parent().row()),
                                      m_downloadView->viewport()->mapToGlobal(pos));
            return;
        }
    }

    // Collect all selected download hashes and rows
    const QStringList allHashes = saveDownloadSelectionMulti();
    const auto selCount = allHashes.size();
    const bool hasSel = (selCount > 0);
    const bool singleSel = (selCount == 1);

    // For single-selection state checks, resolve the first selected download
    const DownloadRow* dl = nullptr;
    QString singleHash;
    if (singleSel) {
        singleHash = allHashes.first();
        for (int i = 0; i < m_downloadModel->downloadCount(); ++i) {
            if (m_downloadModel->hashAt(i) == singleHash) {
                dl = m_downloadModel->downloadAt(i);
                break;
            }
        }
    }

    // For multi-selection state checks, collect all selected download rows
    std::vector<const DownloadRow*> selectedDls;
    if (hasSel) {
        for (const QString& h : allHashes) {
            for (int i = 0; i < m_downloadModel->downloadCount(); ++i) {
                if (m_downloadModel->hashAt(i) == h) {
                    selectedDls.push_back(m_downloadModel->downloadAt(i));
                    break;
                }
            }
        }
    }

    // Rebuild menu each time to capture the current state
    if (!m_downloadMenu)
        m_downloadMenu = new QMenu(this);
    else
        m_downloadMenu->clear();


    // -- 1. Priority (Download) submenu --
    auto* prioMenu = m_downloadMenu->addMenu(menuIcon("FilePriority.ico"), tr("Priority (Download)"));
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
        auto allMatchPrio = [&](int prio) {
            const QString ps = prioStr(prio);
            return std::all_of(selectedDls.begin(), selectedDls.end(),
                [&](const DownloadRow* d) { return !d->isAutoDownPriority && d->priority == ps; });
        };
        auto allAuto = [&]() {
            return std::all_of(selectedDls.begin(), selectedDls.end(),
                [](const DownloadRow* d) { return d->isAutoDownPriority; });
        };
        auto addPrioAction = [&](const QString& text, int prio) {
            auto* act = prioMenu->addAction(text, this, [this, allHashes, prio]() {
                sendSetPriorityBatch(allHashes, prio, false);
            });
            if (allMatchPrio(prio))
                act->setCheckable(true), act->setChecked(true);
        };
        addPrioAction(tr("Low"),    PrLow);
        addPrioAction(tr("Normal"), PrNormal);
        addPrioAction(tr("High"),   PrHigh);
        prioMenu->addSeparator();
        addPrioAction(tr("Very Low"),  PrVeryLow);
        addPrioAction(tr("Very High"), PrVeryHigh);
        prioMenu->addSeparator();
        auto* autoAct = prioMenu->addAction(tr("Auto"), this, [this, allHashes]() {
            sendSetPriorityBatch(allHashes, PrNormal, true);
        });
        if (allAuto())
            autoAct->setCheckable(true), autoAct->setChecked(true);
    }

    m_downloadMenu->addSeparator();

    // -- 2. Pause / Stop / Resume (batch — enabled if ANY selected supports the action) --
    {
        auto* pauseAct = m_downloadMenu->addAction(menuIcon("Pause.ico"), tr("Pause"), this, [this, allHashes]() {
            sendDownloadActionBatch(allHashes, 0);
        });
        bool canPause = std::any_of(selectedDls.begin(), selectedDls.end(), [](const DownloadRow* d) {
            return !d->isPaused && !d->isStopped && d->status != QStringLiteral("complete");
        });
        pauseAct->setEnabled(hasSel && canPause);
    }
    {
        auto* stopAct = m_downloadMenu->addAction(menuIcon("Stop.ico"), tr("Stop"), this, [this, allHashes]() {
            sendStopDownloadBatch(allHashes);
        });
        bool canStop = std::any_of(selectedDls.begin(), selectedDls.end(), [](const DownloadRow* d) {
            return !d->isStopped && d->status != QStringLiteral("complete");
        });
        stopAct->setEnabled(hasSel && canStop);
    }
    {
        auto* resumeAct = m_downloadMenu->addAction(menuIcon("Start.ico"), tr("Resume"), this, [this, allHashes]() {
            sendDownloadActionBatch(allHashes, 1);
        });
        bool canResume = std::any_of(selectedDls.begin(), selectedDls.end(), [](const DownloadRow* d) {
            return d->isPaused || d->isStopped;
        });
        resumeAct->setEnabled(hasSel && canResume);
    }

    m_downloadMenu->addSeparator();

    // -- 3. Cancel (batch, with confirmation for multiple) --
    {
        auto* cancelAct = m_downloadMenu->addAction(menuIcon("Cancel.ico"), tr("Cancel"), this, [this, allHashes, dl]() {
            if (allHashes.size() == 1) {
                const QString name = dl ? dl->fileName : allHashes.first();
                if (QMessageBox::question(this, tr("Cancel Download"),
                        tr("Cancel download \"%1\"?").arg(name),
                        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
                    return;
            } else {
                if (QMessageBox::question(this, tr("Cancel Downloads"),
                        tr("Cancel %1 selected downloads?").arg(allHashes.size()),
                        QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
                    return;
            }
            sendDownloadActionBatch(allHashes, 2);
        });
        cancelAct->setEnabled(hasSel);
    }

    m_downloadMenu->addSeparator();

    // -- 4. Open File / Preview / Details / Comments (single-item only) --
    {
        auto* act = m_downloadMenu->addAction(menuIcon("FileOpen.ico"), tr("Open File"), this, [this, singleHash]() {
            openDownload(singleHash);
        });
        const bool isComplete = singleSel && dl && dl->isComplete();
        act->setEnabled(isComplete);
        // Completed download: Open File becomes the default action (rendered bold).
        setMenuDefaultAction(m_downloadMenu, isComplete ? act : nullptr);
    }
    {
        auto* act = m_downloadMenu->addAction(menuIcon("Preview.ico"), tr("Preview"), this, [this, singleHash]() {
            sendPreview(singleHash);
        });
        act->setEnabled(singleSel && dl && dl->isPreviewPossible);
    }
    {
        auto* act = m_downloadMenu->addAction(menuIcon("FileInfo.ico"), tr("Details..."), this, [this, singleHash]() {
            showDownloadDetails(singleHash);
        });
        act->setEnabled(singleSel);
    }
    {
        auto* act = m_downloadMenu->addAction(menuIcon("FileComments.ico"), tr("Comments..."), this, [this, singleHash]() {
            showComments(singleHash);
        });
        act->setEnabled(singleSel);
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
        auto* clearAct = m_downloadMenu->addAction(menuIcon("DeleteAll.ico"), tr("Clear Completed"), this, [this]() {
            sendClearCompleted();
        });
        clearAct->setEnabled(hasCompleted);
    }

    m_downloadMenu->addSeparator();

    // -- 6. eD2K Links / Paste eD2K Links --
    {
        auto* act = m_downloadMenu->addAction(menuIcon("eD2kLink.ico"), tr("eD2K Links..."), this, [this, allHashes]() {
            copyEd2kLinks(allHashes);
        });
        act->setEnabled(hasSel);
    }
    {
        auto* pasteAct = m_downloadMenu->addAction(menuIcon("eD2kLinkPaste.ico"), tr("Paste eD2K Links"));
        const QString clipText = QApplication::clipboard()->text().trimmed();
        // Everything importLinks() below can act on, which includes HTTP Cache
        // configuration links — those start no download, but pasting one here is a
        // legitimate way to apply it, and greying the entry hid that entirely.
        const bool hasImportableLink =
            Ed2kLinkImporter::linkKindsIn(clipText)
            & (Ed2kLinkImporter::LinkKind::File | Ed2kLinkImporter::LinkKind::HttpCache);
        pasteAct->setEnabled(hasImportableLink && m_ipc && m_ipc->isConnected());
        connect(pasteAct, &QAction::triggered, this, [this, clipText]() {
            // Manual: picking the menu entry is the confirmation, and a completed or
            // cancelled file is left alone so it can be re-downloaded on purpose.
            Ed2kLinkImporter::importLinks(clipText, m_ipc, this,
                                          Ed2kLinkImporter::Source::Manual,
                                          Ed2kLinkImporter::Prompt::Silent);
        });
    }

    m_downloadMenu->addSeparator();

    // -- 7. Find / Search Related --
    connect(m_downloadMenu->addAction(menuIcon("Search.ico"), tr("Find...")),
            &QAction::triggered, this, &TransferPanel::showFindDialog);
    if (singleSel && dl) {
        const QString fname = dl->fileName;
        auto* act = m_downloadMenu->addAction(menuIcon("KadFileSearch.ico"), tr("Search Related Files"), this, [this, fname]() {
            searchRelated(fname);
        });
        act->setEnabled(true);
    }
    // Web Services submenu — external file lookup links from webservices.dat
    if (singleSel && dl) {
        auto* webMenu = m_downloadMenu->addMenu(menuIcon("Web.ico"), tr("Web Services"));
        WebServices::instance().populateFileMenu(webMenu, dl->hash, dl->fileName,
                                                  static_cast<uint64_t>(dl->fileSize));
        if (webMenu->isEmpty())
            webMenu->setEnabled(false);
    }

    m_downloadMenu->addSeparator();

    // -- 8. Assign To Category (batch) --
    {
        auto* catMenu = m_downloadMenu->addMenu(menuIcon("Category.ico"), tr("Assign To Category"));
        // Index 0 is not a category to assign *to* — picking it takes the file
        // out of whatever category it is in, which is why MFC labels it
        // "(Unassign)" rather than "All" (srchybrid/DownloadListCtrl.cpp:1170).
        auto* allAct = catMenu->addAction(tr("(Unassign)"), this, [this, allHashes]() {
            sendSetCategoryBatch(allHashes, 0);
        });
        allAct->setEnabled(hasSel);
        for (int i = 1; i < m_categories.size(); ++i) {
            auto* catAct = catMenu->addAction(categoryTitle(i), this,
                [this, allHashes, i]() { sendSetCategoryBatch(allHashes, i); });
            catAct->setEnabled(hasSel);
        }
        catMenu->setEnabled(hasSel && m_categories.size() > 1);
    }

    m_downloadMenu->popup(m_downloadView->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void TransferPanel::setupUi()
{
    // Create models first (needed by both panes)
    m_downloadModel = new DownloadListModel(this);

    clientSlot(Uploading).model   = new ClientListModel(ClientListMode::Uploading, this);
    clientSlot(Downloading).model = new ClientListModel(ClientListMode::Downloading, this);
    clientSlot(OnQueue).model     = new ClientListModel(ClientListMode::OnQueue, this);
    clientSlot(Known).model       = new ClientListModel(ClientListMode::KnownClients, this);

    clientSlot(Uploading).icon   = ":/icons/Upload.ico";
    clientSlot(Downloading).icon = ":/icons/Download.ico";
    clientSlot(OnQueue).icon     = ":/icons/ClientsOnQueue.ico";
    clientSlot(Known).icon       = ":/icons/ClientsKnown.ico";

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

    // Restore both saved views. The top pane wins a clash, which also repairs a
    // bottomView written before the top pane could hold that list.
    QSettings settings;
    const int savedTop =
        settings.value(QStringLiteral("transfer/topView"), TopView::Downloads).toInt();
    const int savedBottom =
        settings.value(QStringLiteral("transfer/bottomView"), ClientView::Uploading).toInt();
    applyViews(savedTop, savedBottom, Pane::Top);
}

QWidget* TransferPanel::createDownloadsSection()
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- Header row: view switcher (left) + category tab bar (right) ---
    auto* headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(2);

    // Same switcher widget and icons as the bottom pane, with the download list added
    // in front — MFC's m_btnWnd1 offers the download list on top of wnd2's four.
    m_toolbar1 = new TransferToolbar;
    m_toolbar1->addButton(QIcon(QStringLiteral(":/icons/DownloadFiles.ico")), tr("Downloads"));
    for (int v = 0; v < ClientViewCount; ++v)
        m_toolbar1->addButton(QIcon(QString::fromLatin1(clientSlot(v).icon)), clientViewName(v));

    connect(m_toolbar1, &TransferToolbar::buttonClicked, this, [this](int id) {
        setTopView(id);
    });

    headerRow->addWidget(m_toolbar1, 1);

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

    // Right-click is the only way into the category editor, as in MFC. A click
    // on empty tab-bar space (tabAt returns -1) still opens the menu, because
    // that is how "Add Category..." is reached before any category exists.
    m_categoryTabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_categoryTabBar, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
                showCategoryMenu(m_categoryTabBar->tabAt(pos),
                                 m_categoryTabBar->mapToGlobal(pos));
            });

    // Double-click edits, matching srchybrid/TransferWnd.cpp:1253-1262.
    connect(m_categoryTabBar, &QTabBar::tabBarDoubleClicked, this,
            &TransferPanel::editCategory);

    // Drag to reorder, as MFC's tab control does (OnTabMovement,
    // srchybrid/TransferWnd.cpp:1264-1291). QTabBar moves the tab itself and
    // tells us afterwards; the daemon does the renumbering from the oldIndex
    // list, and its push rebuilds the bar either way.
    m_categoryTabBar->setMovable(true);
    connect(m_categoryTabBar, &QTabBar::tabMoved, this, [this](int from, int to) {
        // Index 0 is "All" and is pinned — MFC refuses the same move.
        if (from == 0 || to == 0 || from >= m_categories.size()
            || to >= m_categories.size())
        {
            updateCategoryTabs(); // put the bar back
            return;
        }

        auto categories = m_categories;
        auto oldIndex = m_categoryOldIndex;
        categories.move(from, to);
        oldIndex.move(from, to);
        sendCategories(categories, oldIndex);
    });

    headerRow->addWidget(m_categoryTabBar);
    layout->addLayout(headerRow);

    // --- Download view (mounted while the top switcher is on Downloads) ---
    m_downloadProxy = new QSortFilterProxyModel(this);
    m_downloadProxy->setSourceModel(m_downloadModel);
    m_downloadProxy->setSortRole(Qt::UserRole);

    // Category filter proxy sits on top of download sort proxy
    auto* catProxy = new CategoryFilterProxy(this);
    catProxy->setSourceModel(m_downloadProxy);
    catProxy->setDownloadProxy(m_downloadProxy);
    catProxy->setSortRole(Qt::UserRole);
    m_categoryProxy = catProxy;

    // Parented on the panel from the start: the top pane may open on a client list,
    // and an unparented view would be owned by nobody until it is first mounted.
    auto* downloadView = new ListTreeView(this);
    m_downloadView = downloadView;
    m_downloadView->setModel(m_categoryProxy);
    m_downloadView->setRootIsDecorated(true);
    m_downloadView->setAlternatingRowColors(true);
    m_downloadView->setSortingEnabled(true);
    m_downloadView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_downloadView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_downloadView->setUniformRowHeights(true);
    m_downloadView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_downloadView->setExpandsOnDoubleClick(false); // we handle double-click ourselves

    m_downloadView->setItemDelegateForColumn(
        DownloadListModel::ColProgress,
        new DownloadProgressDelegate(m_downloadView));

    connect(m_downloadView, &QTreeView::customContextMenuRequested,
            this, &TransferPanel::onDownloadContextMenu);
    connect(m_downloadView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &TransferPanel::updateActionStates);

    // Double-click toggles expand/collapse for download rows
    connect(m_downloadView, &QTreeView::doubleClicked, this, [this](const QModelIndex& proxyIdx) {
        if (!proxyIdx.isValid())
            return;

        // If this is a source (child) row, open client details
        if (proxyIdx.parent().isValid()) {
            const QModelIndex catSrcIdx = m_categoryProxy->mapToSource(proxyIdx);
            const QModelIndex srcIdx = m_downloadProxy->mapToSource(catSrcIdx);
            if (const auto* src = m_downloadModel->sourceAt(srcIdx)) {
                const QString parentHash = m_downloadModel->hashAt(srcIdx.parent().row());
                fetchAndShowClientDetails(src->userHash,
                                          makeSourceWalker(parentHash, src->userHash));
            }
            return;
        }

        // Map through proxy chain to get the source model hash
        const QModelIndex catSrcIdx = m_categoryProxy->mapToSource(proxyIdx);
        const QModelIndex srcIdx = m_downloadProxy->mapToSource(catSrcIdx);
        const QString hash = m_downloadModel->hashAt(srcIdx.row());
        if (hash.isEmpty())
            return;

        // Completed download: default action is Open File — no expansion, since
        // sources are no longer allocated to it.
        if (const auto* dl = m_downloadModel->downloadAt(srcIdx.row()); dl && dl->isComplete()) {
            openDownload(hash);
            return;
        }

        if (m_downloadView->isExpanded(proxyIdx)) {
            m_downloadView->collapse(proxyIdx);
            m_expandedDownloads.remove(hash);
        } else {
            m_expandedDownloads.insert(hash);
            requestDownloadSources(hash);
            m_downloadView->expand(proxyIdx);
        }
    });

    // MFC CDownloadListCtrl (srchybrid/DownloadListCtrl.cpp:1443 IDA_ENTER, :1420 and
    // :1545 MPG_ALTENTER): Enter opens a finished download and does nothing on a source
    // row — expanding is the double click's job, not Enter's — while Alt+Enter opens the
    // file sheet for a download row and the client sheet for a source row.
    bindListActivation(m_downloadView,
        [this](const QModelIndex& index) {
            if (index.parent().isValid())
                return;
            const QModelIndex srcIdx = ViewNav::toSource(index);
            const auto* dl = m_downloadModel->downloadAt(srcIdx.row());
            if (dl && dl->isComplete())
                openDownload(m_downloadModel->hashAt(srcIdx.row()));
        },
        [this](const QModelIndex& index) {
            const QModelIndex srcIdx = ViewNav::toSource(index);
            if (!index.parent().isValid()) {
                showDownloadDetails(m_downloadModel->hashAt(srcIdx.row()));
                return;
            }
            if (const auto* src = m_downloadModel->sourceAt(srcIdx)) {
                const QString parentHash = m_downloadModel->hashAt(srcIdx.parent().row());
                fetchAndShowClientDetails(src->userHash,
                                          makeSourceWalker(parentHash, src->userHash));
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
    // Name, Size, Completed, Speed, Progress, Sources, Priority, Status,
    // Remaining, Last Seen Complete, Last Reception, Category, Added On.
    downloadView->bindColumns(QStringLiteral("downloads"),
        {220, 65, 65, 65, 90, 65, 70, 65, 80, 80, 80, 60, 120});

    // Hidden until mounted, for the reason given in createClientView().
    downloadView->hide();

    // The view itself is mounted by applyViews() — this pane shows whichever list the
    // top switcher is on.
    m_topContent = new QVBoxLayout;
    m_topContent->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(m_topContent, 1);

    return widget;
}

QWidget* TransferPanel::createBottomPane()
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Bottom toolbar header — the same four lists the top switcher offers, minus
    // Downloads, which the top pane keeps to itself.
    m_toolbar2 = new TransferToolbar;
    for (int v = 0; v < ClientViewCount; ++v)
        m_toolbar2->addButton(QIcon(QString::fromLatin1(clientSlot(v).icon)), clientViewName(v));

    connect(m_toolbar2, &TransferToolbar::buttonClicked, this, [this](int id) {
        setBottomView(id);
    });

    layout->addWidget(m_toolbar2);

    // Column widths follow ClientListModel::headerLabel() order. The keys carry a
    // "3" because the "2" generation was saved with a flat 100 px per column;
    // reusing them would let that stale layout override these defaults.
    clientSlot(Uploading).view = createClientView(
        clientSlot(Uploading).model, QStringLiteral("clientsUploading3"),
        // User Name, File, Speed, Transferred, Waited, Upload Time, Status, Obtained Parts
        {150, 220, 70, 90, 80, 80, 90, 120});
    clientSlot(Downloading).view = createClientView(
        clientSlot(Downloading).model, QStringLiteral("clientsDownloading3"),
        // User Name, Software, File, Speed, Available Parts, Transferred, Transferred, Source Type
        {150, 90, 220, 70, 100, 90, 90, 100});
    clientSlot(OnQueue).view = createClientView(
        clientSlot(OnQueue).model, QStringLiteral("clientsOnQueue3"),
        // User Name, File, File Priority, Rating, Score, Asked, Last Seen,
        // Entered Queue, Banned, Obtained Parts
        {150, 220, 80, 70, 60, 60, 100, 110, 60, 120});
    clientSlot(Known).view = createClientView(
        clientSlot(Known).model, QStringLiteral("clientsKnown3"),
        // User Name, Upload Status, Transferred, Download Status, Transferred Down,
        // Software, Connected, Hash
        {150, 100, 90, 100, 110, 90, 80, 240});

    // Context menu and double-click are wired per view, and a view keeps them when it
    // moves to the other pane.
    for (const auto& slot : m_clients) {
        QTreeView* view = slot.view;
        ClientListModel* model = slot.model;

        connect(view, &QTreeView::customContextMenuRequested, this,
                [this, view, model](const QPoint& p) { onClientContextMenu(view, model, p); });

        const auto showDetails = [this, view, model](const QModelIndex& proxyIdx) {
            auto* proxy = qobject_cast<QSortFilterProxyModel*>(view->model());
            if (!proxy) return;
            const QModelIndex srcIdx = proxy->mapToSource(proxyIdx);
            const auto* client = model->clientAt(srcIdx.row());
            if (client)
                fetchAndShowClientDetails(client->userHash,
                                          makeClientWalker(view, model, client->userHash));
        };
        connect(view, &QTreeView::doubleClicked, this, showDetails);

        // MFC maps Enter and Alt+Enter to the same command on every client list
        // (srchybrid/ClientListCtrl.cpp:387, UploadListCtrl.cpp:420,
        // QueueListCtrl.cpp:451, DownloadClientsCtrl.cpp:397).
        bindListActivation(view, showDetails, showDetails);
    }

    // The view itself is mounted by applyViews().
    m_bottomContent = new QVBoxLayout;
    m_bottomContent->setContentsMargins(0, 0, 0, 0);
    layout->addLayout(m_bottomContent, 1);

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

    m_actPause = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/Pause.ico")), tr("Pause"));
    connect(m_actPause, &QAction::triggered, this, [this]() {
        sendDownloadActionBatch(saveDownloadSelectionMulti(), 0);
    });

    m_actStop = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/Stop.ico")), tr("Stop"));
    connect(m_actStop, &QAction::triggered, this, [this]() {
        sendStopDownloadBatch(saveDownloadSelectionMulti());
    });

    m_actResume = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/Start.ico")), tr("Resume"));
    connect(m_actResume, &QAction::triggered, this, [this]() {
        sendDownloadActionBatch(saveDownloadSelectionMulti(), 1);
    });

    m_actCancel = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/Delete.ico")), tr("Cancel"));
    connect(m_actCancel, &QAction::triggered, this, [this]() {
        const QStringList hashes = saveDownloadSelectionMulti();
        if (hashes.isEmpty())
            return;
        if (hashes.size() == 1) {
            // Find file name for single-download confirmation
            QString fileName;
            for (int i = 0; i < m_downloadModel->downloadCount(); ++i) {
                if (m_downloadModel->hashAt(i) == hashes.first()) {
                    fileName = m_downloadModel->downloadAt(i)->fileName;
                    break;
                }
            }
            if (QMessageBox::question(this, tr("Cancel Download"),
                    tr("Cancel download \"%1\"?").arg(fileName),
                    QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
                return;
        } else {
            if (QMessageBox::question(this, tr("Cancel Downloads"),
                    tr("Cancel %1 selected downloads?").arg(hashes.size()),
                    QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
                return;
        }
        sendDownloadActionBatch(hashes, 2);
    });

    toolbar->addSeparator();

    // Group 2: Open File / Open Folder / Preview / Details / Comments / eD2K Links
    auto* actOpenFile = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/FileOpen.ico")), tr("Open File"));
    connect(actOpenFile, &QAction::triggered, this, [this]() {
        openDownload(saveDownloadSelection());
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
        this, [this]() { copyEd2kLinks(saveDownloadSelectionMulti()); });
    m_selectionActions.append(actEd2kLinks);

    toolbar->addSeparator();

    // Group 3: Assign To Category / Clear Completed / Search Related
    auto* actCategory = toolbar->addAction(
        QIcon(QStringLiteral(":/icons/Category.ico")), tr("Assign To Category"));
    connect(actCategory, &QAction::triggered, this, [this]() {
        const QStringList hashes = saveDownloadSelectionMulti();
        if (hashes.isEmpty())
            return;
        // Show category popup at cursor
        QMenu catMenu(this);
        catMenu.addAction(tr("(Unassign)"), this,
                          [this, hashes]() { sendSetCategoryBatch(hashes, 0); });
        for (int i = 1; i < m_categories.size(); ++i) {
            catMenu.addAction(categoryTitle(i), this,
                              [this, hashes, i]() { sendSetCategoryBatch(hashes, i); });
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
    m_actPause->setEnabled(false);
    m_actStop->setEnabled(false);
    m_actResume->setEnabled(false);
    m_actCancel->setEnabled(false);

    return toolbar;
}

QTreeView* TransferPanel::createClientView(ClientListModel* model,
                                            const QString& headerKey,
                                            std::initializer_list<int> columnWidths)
{
    auto* proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(model);
    proxy->setSortRole(Qt::UserRole);

    // Parented on the panel: a view is unparented from its pane's layout whenever it
    // moves to the other one, and must stay owned in between.
    auto* view = new ListTreeView(this);
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
    view->bindColumns(headerKey, columnWidths);

    // A child widget that is in no layout is still shown with its parent, at its
    // default geometry — an unmounted list would float over the pane headers at the
    // top-left corner. A view is visible only while a pane holds it; attachView()
    // shows it again.
    view->hide();

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
            row.fileOp            = static_cast<int>(m.value(QStringLiteral("fileOp")).toInteger());
            row.completionError   = m.value(QStringLiteral("completionError")).toBool();
            row.category          = m.value(QStringLiteral("category")).toInteger();
            row.lastSeenComplete  = m.value(QStringLiteral("lastSeenComplete")).toInteger();
            row.lastReception     = m.value(QStringLiteral("lastReception")).toInteger();
            row.addedOn           = m.value(QStringLiteral("addedOn")).toInteger();
            row.fileType          = m.value(QStringLiteral("fileType")).toString();
            row.requests          = m.value(QStringLiteral("requests")).toInteger();
            row.acceptedRequests  = m.value(QStringLiteral("acceptedReqs")).toInteger();
            row.transferredData   = m.value(QStringLiteral("transferredData")).toInteger();
            row.isPreviewPossible = m.value(QStringLiteral("isPreviewPossible")).toBool();
            row.hasComment        = m.value(QStringLiteral("hasComment")).toBool();
            row.userRating        = static_cast<int>(m.value(QStringLiteral("userRating")).toInteger());
            row.containerSuspect  = m.value(QStringLiteral("containerSuspect")).toBool();
            row.containerExpected = m.value(QStringLiteral("containerExpected")).toString();
            row.containerActual   = m.value(QStringLiteral("containerActual")).toString();
            if (auto partArr = m.value(QStringLiteral("partMap")).toArray(); !partArr.isEmpty()) {
                row.partMap.resize(static_cast<qsizetype>(partArr.size()));
                for (qsizetype i = 0; i < partArr.size(); ++i)
                    row.partMap[static_cast<qsizetype>(i)] = static_cast<char>(partArr[i].toInteger(0));
            }
            rows.push_back(std::move(row));
        }

        // Incremental update preserves selection, expansion, and scroll natively
        m_downloadModel->setDownloads(std::move(rows));
        updateToolbarLabels();
        updateCategoryTabs();
        updateActionStates();
        updateClearCompletedState();

        // Completed downloads no longer have sources — stop tracking/fetching them.
        for (auto it = m_expandedDownloads.begin(); it != m_expandedDownloads.end();) {
            const auto* dl = m_downloadModel->findByHash(*it);
            if (dl && dl->isComplete())
                it = m_expandedDownloads.erase(it);
            else
                ++it;
        }

        // Refresh sources for expanded downloads
        for (const QString& expHash : m_expandedDownloads)
            requestDownloadSources(expHash);
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
            src.addr            = m.value(QStringLiteral("addr")).toString();
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

        applyClients(Uploading, std::move(uploadingRows));
        applyClients(OnQueue, std::move(waitingRows));
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

        applyClients(Downloading, std::move(rows));
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

        applyClients(Known, std::move(rows));
        updateToolbarLabels();
    });
}

void TransferPanel::applyClients(int clientView, std::vector<ClientRow> rows)
{
    // setClients() resets the model, which drops the view's selection and scroll
    // position — both are keyed back on afterwards.
    const ClientSlot& slot = clientSlot(clientView);
    const int scroll = slot.view->verticalScrollBar()->value();
    const QString selected = saveClientSelection(slot.view, slot.model);

    slot.model->setClients(std::move(rows));

    restoreClientSelection(slot.view, slot.model, selected);
    slot.view->verticalScrollBar()->setValue(scroll);
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

void TransferPanel::sendDownloadActionBatch(const QStringList& hashes, int action)
{
    if (!m_ipc || !m_ipc->isConnected() || hashes.isEmpty())
        return;

    IpcMsgType msgType;
    switch (action) {
    case 0: msgType = IpcMsgType::PauseDownload;  break;
    case 1: msgType = IpcMsgType::ResumeDownload; break;
    case 2: msgType = IpcMsgType::CancelDownload; break;
    default: return;
    }

    m_ipc->sendBatchRequest(hashes, [msgType](const QString& hash) {
        IpcMessage msg(msgType);
        msg.append(hash);
        return msg;
    }, this, [this]() { requestDownloads(); });
}

void TransferPanel::sendSetPriority(const QString& hash, int priority, bool isAuto)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;

    IpcMessage msg(IpcMsgType::SetDownloadPriority);
    msg.append(hash);
    msg.append(static_cast<qint64>(priority));
    msg.append(isAuto);
    m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
        requestDownloads();
    });
}

void TransferPanel::sendSetPriorityBatch(const QStringList& hashes, int priority, bool isAuto)
{
    if (!m_ipc)
        return;

    m_ipc->sendBatchRequest(hashes, [priority, isAuto](const QString& hash) {
        IpcMessage msg(IpcMsgType::SetDownloadPriority);
        msg.append(hash);
        msg.append(static_cast<qint64>(priority));
        msg.append(isAuto);
        return msg;
    }, this, [this]() { requestDownloads(); });
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

void TransferPanel::copyEd2kLinks(const QStringList& hashes)
{
    QStringList links;
    for (const QString& hash : hashes) {
        for (int i = 0; i < m_downloadModel->downloadCount(); ++i) {
            const auto* dl = m_downloadModel->downloadAt(i);
            if (dl && dl->hash == hash) {
                links.append(QStringLiteral("ed2k://|file|%1|%2|%3|/")
                    .arg(dl->fileName)
                    .arg(dl->fileSize)
                    .arg(dl->hash));
                break;
            }
        }
    }
    if (!links.isEmpty())
        QApplication::clipboard()->setText(links.join(QLatin1Char('\n')));
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

void TransferPanel::sendStopDownloadBatch(const QStringList& hashes)
{
    if (!m_ipc)
        return;

    m_ipc->sendBatchRequest(hashes, [](const QString& hash) {
        IpcMessage msg(IpcMsgType::StopDownload);
        msg.append(hash);
        return msg;
    }, this, [this]() { requestDownloads(); });
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

    // The daemon opens the folder on its own host. That is right only while its
    // host is this one -- otherwise it would pop a window on someone else's
    // desktop, and silently, since the reply is discarded.
    if (!m_ipc->isLocalConnection()) {
        openIncomingFolder(m_ipc, m_streamToken);
        return;
    }

    IpcMessage msg(IpcMsgType::OpenDownloadFolder);
    msg.append(hash);
    m_ipc->sendRequest(std::move(msg));
}

void TransferPanel::sendPreview(const QString& hash)
{
    const QString url = streamUrl(hash);
    if (url.isEmpty()) {
        logWarning(tr("Preview not available — web server is not running or stream token not received."));
        return;
    }
    launchPreview(url);
}

QString TransferPanel::streamUrl(const QString& hash) const
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty() || m_streamToken.isEmpty())
        return {};

    // Streaming URL via the daemon's web server (backs preview and remote open).
    return daemonStreamUrl(m_ipc, hash, m_streamToken);
}

void TransferPanel::openDownload(const QString& hash)
{
    if (hash.isEmpty())
        return;

    // Local core: the daemon opens the file on its own host, which is the same
    // machine as the GUI — nothing needs to travel over the network.
    if (m_ipc && m_ipc->isLocalConnection()) {
        sendOpenFile(hash);
        return;
    }

    // Remote core: the completed file lives on the daemon host, so open it over
    // HTTP through the web server (same channel Preview uses).
    const QString url = streamUrl(hash);
    if (url.isEmpty()) {
        logWarning(tr("Open File not available — web server is not running or stream token not received."));
        return;
    }

    // Resolve the file name to decide media vs. non-media handling.
    QString fileName;
    for (int i = 0; i < m_downloadModel->downloadCount(); ++i) {
        if (m_downloadModel->hashAt(i) == hash) {
            if (const auto* dl = m_downloadModel->downloadAt(i))
                fileName = dl->fileName;
            break;
        }
    }

    // Media files stream into the configured player; everything else is handed
    // to the system default handler (browser download / associated app).
    const ED2KFileType ft = getED2KFileTypeID(fileName);
    if (ft == ED2KFileType::Video || ft == ED2KFileType::Audio)
        launchPreview(url);
    else
        QDesktopServices::openUrl(QUrl(url));
}

void TransferPanel::sendSetCategory(const QString& hash, int category)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;
    IpcMessage msg(IpcMsgType::SetDownloadCategory);
    msg.append(hash);
    msg.append(static_cast<qint64>(category));
    m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
        requestDownloads();
    });
}

void TransferPanel::sendSetCategoryBatch(const QStringList& hashes, int category)
{
    if (!m_ipc)
        return;

    m_ipc->sendBatchRequest(hashes, [category](const QString& hash) {
        IpcMessage msg(IpcMsgType::SetDownloadCategory);
        msg.append(hash);
        msg.append(static_cast<qint64>(category));
        return msg;
    }, this, [this]() { requestDownloads(); });
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
    m_ipc->sendRequest(std::move(msg), [this, tab, hash](const IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;
        const QCborMap details = resp.field(1).toMap();
        auto* dlg = new FileDetailDialog(details, tab, this);
        connectEd2kLinkRequests(dlg, m_ipc);
        connectKadNotesSearch(dlg, m_ipc, IpcMsgType::GetDownloadDetails);
        connectCommentFilter(dlg, m_ipc);
        connectCommentPosting(dlg, m_ipc);
        dlg->setWalker(makeDownloadWalker(hash));
        connectDetailNavigation(dlg, m_ipc, IpcMsgType::GetDownloadDetails);
        dlg->show();
    });
}

void TransferPanel::fetchAndShowClientDetails(const QString& clientHash, DetailWalker walker)
{
    showClientDetails(this, m_ipc, clientHash, std::move(walker));
}

void TransferPanel::searchRelated(const QString& fileName)
{
    // Strip extension and emit search request
    const qsizetype dotIdx = fileName.lastIndexOf(QLatin1Char('.'));
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

QStringList TransferPanel::saveDownloadSelectionMulti() const
{
    const auto selected = m_downloadView->selectionModel()->selectedRows(0);
    if (selected.isEmpty())
        return {};

    QSet<QString> seen;
    QStringList result;
    for (const QModelIndex& proxyIdx : selected) {
        const QModelIndex catSrcIdx = m_categoryProxy->mapToSource(proxyIdx);
        const QModelIndex srcIdx = m_downloadProxy->mapToSource(catSrcIdx);

        QString hash;
        if (m_downloadModel->isSourceRow(srcIdx)) {
            // Source (child) row → resolve to parent download hash
            const QModelIndex parentSrcIdx = srcIdx.parent();
            hash = m_downloadModel->hashAt(parentSrcIdx.row());
        } else {
            hash = m_downloadModel->hashAt(srcIdx.row());
        }

        if (!hash.isEmpty() && !seen.contains(hash)) {
            seen.insert(hash);
            result.append(hash);
        }
    }
    return result;
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

void TransferPanel::setTopView(int topId)
{
    applyViews(topId, m_bottomView, Pane::Top);
}

void TransferPanel::setBottomView(int clientView)
{
    applyViews(m_topView, clientView, Pane::Bottom);
}

void TransferPanel::detachView(QVBoxLayout* layout, QTreeView*& slot, QTreeView* keep)
{
    if (!slot || slot == keep)
        return;

    // QLayout::removeWidget drops the layout item but leaves the parent alone, so the
    // outgoing view stays owned — and stays visible unless it is hidden by hand.
    layout->removeWidget(slot);
    slot->hide();
    slot = nullptr;
}

void TransferPanel::attachView(QVBoxLayout* layout, QTreeView*& slot, QTreeView* view)
{
    if (slot == view)
        return;

    layout->addWidget(view, 1); // reparents, which hides it again
    view->show();
    slot = view;
}

void TransferPanel::applyViews(int topId, int clientView, Pane priority)
{
    topId      = std::clamp(topId, 0, static_cast<int>(ClientViewCount));
    clientView = std::clamp(clientView, 0, static_cast<int>(ClientViewCount) - 1);

    // There is one view per list, so the two panes cannot both show it. Whichever
    // pane asked last keeps what it asked for and the other one moves: the bottom
    // pane falls to the first list still free, the top pane falls back to Downloads,
    // which no other pane can be holding.
    int topClient = (topId == TopView::Downloads) ? -1 : topId - TopView::TopClientFirst;
    if (topClient == clientView) {
        if (priority == Pane::Top) {
            for (int v = 0; v < ClientViewCount; ++v) {
                if (v != topClient) {
                    clientView = v;
                    break;
                }
            }
        } else {
            topId = TopView::Downloads;
            topClient = -1;
        }
    }

    // Both panes let go before either takes hold: a list moving from one pane to the
    // other must leave its old layout first, or it is reparented out from under a
    // live layout item.
    QTreeView* topWidget = (topClient < 0) ? m_downloadView : clientSlot(topClient).view;
    QTreeView* bottomWidget = clientSlot(clientView).view;
    detachView(m_topContent, m_topMounted, topWidget);
    detachView(m_bottomContent, m_bottomMounted, bottomWidget);
    attachView(m_topContent, m_topMounted, topWidget);
    attachView(m_bottomContent, m_bottomMounted, bottomWidget);

    m_topView = topId;
    m_bottomView = clientView;

    m_toolbar1->checkButton(topId);
    m_toolbar1->setLeadingIcon(QIcon(QString::fromLatin1(
        topClient < 0 ? ":/icons/DownloadFiles.ico" : clientSlot(topClient).icon)));

    m_toolbar2->checkButton(clientView);
    m_toolbar2->setLeadingIcon(QIcon(QString::fromLatin1(clientSlot(clientView).icon)));

    // Grey out the list the top pane is holding, as the MFC toolbar does for a list
    // that is not available. The clash was resolved above, so this never disables the
    // bottom pane's own checked button.
    for (int v = 0; v < ClientViewCount; ++v)
        m_toolbar2->setButtonEnabled(v, v != topClient);

    // Both belong to the download list and mean nothing while it is off screen.
    const bool showingDownloads = (topId == TopView::Downloads);
    m_categoryTabBar->setVisible(showingDownloads);
    m_actionToolbar->setEnabled(showingDownloads);

    QSettings settings;
    settings.setValue(QStringLiteral("transfer/topView"), topId);
    settings.setValue(QStringLiteral("transfer/bottomView"), clientView);

    updateToolbarLabels();

    // The poll only fetches the lists that are showing, so a list just switched to
    // holds whatever it had when it was last visible — refill it now.
    if (m_poller)
        m_poller->refreshNow();
}

void TransferPanel::updateActionStates()
{
    const QStringList hashes = saveDownloadSelectionMulti();
    const bool hasSelection = !hashes.isEmpty();

    // Generic selection-dependent actions (priority, open file, details, etc.)
    for (auto* act : m_selectionActions)
        act->setEnabled(hasSelection);

    // State-dependent actions: collect selected download rows
    std::vector<const DownloadRow*> selectedDls;
    if (hasSelection) {
        for (const QString& h : hashes) {
            for (int i = 0; i < m_downloadModel->downloadCount(); ++i) {
                if (m_downloadModel->hashAt(i) == h) {
                    selectedDls.push_back(m_downloadModel->downloadAt(i));
                    break;
                }
            }
        }
    }

    const bool canPause = std::any_of(selectedDls.begin(), selectedDls.end(), [](const DownloadRow* d) {
        return !d->isPaused && !d->isStopped && d->status != QStringLiteral("complete");
    });
    const bool canStop = std::any_of(selectedDls.begin(), selectedDls.end(), [](const DownloadRow* d) {
        return !d->isStopped && d->status != QStringLiteral("complete");
    });
    const bool canResume = std::any_of(selectedDls.begin(), selectedDls.end(), [](const DownloadRow* d) {
        return d->isPaused || d->isStopped;
    });

    m_actPause->setEnabled(hasSelection && canPause);
    m_actStop->setEnabled(hasSelection && canStop);
    m_actResume->setEnabled(hasSelection && canResume);
    m_actCancel->setEnabled(hasSelection);
}

QString TransferPanel::clientViewName(int clientView)
{
    switch (clientView) {
    case Uploading:   return tr("Uploading");
    case Downloading: return tr("Downloading");
    case OnQueue:     return tr("On Queue");
    case Known:       return tr("Known Clients");
    default:          return {};
    }
}

QString TransferPanel::clientLabelText(int clientView) const
{
    const int n = clientSlot(clientView).model->clientCount();
    switch (clientView) {
    case Uploading:   return tr("Uploading (%1)").arg(n);
    case Downloading: return tr("Downloading (%1)").arg(n);
    case OnQueue:     return tr("On Queue (%1)").arg(n);
    case Known:       return tr("Known Clients (%1)").arg(n);
    default:          return {};
    }
}

void TransferPanel::updateToolbarLabels()
{
    m_toolbar1->setLabelText(m_topView == TopView::Downloads
        ? tr("Downloads (%1)").arg(m_downloadModel->downloadCount())
        : clientLabelText(m_topView - TopView::TopClientFirst));

    m_toolbar2->setLabelText(clientLabelText(m_bottomView));

    // The count rides along with the stats push, so this label stays live even when
    // neither queue list is on screen and GetUploads is not being issued at all. The
    // model's own count stands in until the first push lands.
    m_queueLabel->setText(tr("Clients on queue:   %1").arg(
        m_queueCount >= 0 ? m_queueCount : clientSlot(OnQueue).model->clientCount()));
}

void TransferPanel::updateCategoryTabs()
{
    // Block signals to avoid triggering filter changes during rebuild
    const QSignalBlocker blocker(m_categoryTabBar);

    // Remember the *category*, not the tab position: a removed category shifts
    // every tab behind it, and restoring by index would silently move the user
    // to a different one.
    const int64_t currentCat = m_categoryTabBar->currentIndex() >= 0
        ? m_categoryTabBar->tabData(m_categoryTabBar->currentIndex()).toLongLong()
        : 0;

    while (m_categoryTabBar->count() > 0)
        m_categoryTabBar->removeTab(0);

    // One tab per category, in list order — the order *is* the identity, since
    // that index is what part.met stores. Index 0 is the "All" tab.
    for (int i = 0; i < m_categories.size(); ++i) {
        const int idx = m_categoryTabBar->addTab(categoryTitle(i));
        m_categoryTabBar->setTabData(idx, QVariant::fromValue(static_cast<int64_t>(i)));

        if (!m_categories.at(i).comment.isEmpty())
            m_categoryTabBar->setTabToolTip(idx, m_categories.at(i).comment);

        // MFC colours the tab text, not its background (SetTabTextColor,
        // srchybrid/TransferWnd.cpp:1035). kCategoryColorAuto means "leave the
        // theme alone", which is not the same as black.
        if (m_categories.at(i).color != kCategoryColorAuto)
            m_categoryTabBar->setTabTextColor(idx, QColor::fromRgb(m_categories.at(i).color));
    }

    int restoreIdx = 0;
    for (int i = 0; i < m_categoryTabBar->count(); ++i) {
        if (m_categoryTabBar->tabData(i).toLongLong() == currentCat) {
            restoreIdx = i;
            break;
        }
    }
    m_categoryTabBar->setCurrentIndex(restoreIdx);

    // The blocker above suppressed currentChanged, so the filter still holds
    // whatever the previous tab set. Re-apply it for the tab that is actually
    // selected now.
    const int64_t activeCat = m_categoryTabBar->tabData(restoreIdx).toLongLong();
    static_cast<CategoryFilterProxy*>(m_categoryProxy)->setCategoryFilter(activeCat);
}

// ---------------------------------------------------------------------------
// Categories
// ---------------------------------------------------------------------------

QString TransferPanel::categoryTitle(int index) const
{
    if (index == 0)
        return tr("All");
    if (index > 0 && index < m_categories.size())
        return m_categories.at(index).displayName();

    // A download can outlive the category it names — a stale part.met, or a
    // list edited by another GUI. Showing the bare index says more than an
    // empty cell would.
    return tr("Cat %1").arg(index);
}

QStringList TransferPanel::categoryNames() const
{
    QStringList names;
    names.reserve(m_categories.size());
    for (int i = 0; i < m_categories.size(); ++i)
        names.append(categoryTitle(i));
    return names;
}

void TransferPanel::requestCategories()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    m_ipc->sendRequest(IpcMessage(IpcMsgType::GetCategories), [this](const IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;

        m_categories.clear();
        m_categoryOldIndex.clear();
        for (const auto& value : resp.fieldArray(1)) {
            if (!value.isMap())
                continue;
            const QCborMap map = value.toMap();

            DownloadCategory cat;
            cat.title = map.value(QStringLiteral("title")).toString();
            cat.incomingPath = map.value(QStringLiteral("incoming")).toString();
            cat.comment = map.value(QStringLiteral("comment")).toString();
            cat.autocat = map.value(QStringLiteral("autocat")).toString();
            cat.autocatIsRegexp = map.value(QStringLiteral("autocatRegexp")).toBool(false);
            cat.regexp = map.value(QStringLiteral("regexp")).toString();
            cat.color = static_cast<quint32>(
                map.value(QStringLiteral("color")).toInteger(kCategoryColorAuto));
            cat.prio = static_cast<quint8>(map.value(QStringLiteral("prio")).toInteger(cat.prio));
            // Index 0's resolved path is the global incoming dir — the folder
            // an unset category falls back to, and the sensible starting point
            // for the browse button.
            if (m_categories.isEmpty())
                m_defaultIncomingDir = map.value(QStringLiteral("resolvedIncoming")).toString();

            m_categories.append(cat);
            m_categoryOldIndex.append(
                static_cast<int>(map.value(QStringLiteral("index"))
                                     .toInteger(m_categories.size() - 1)));
        }

        // The daemon guarantees index 0; a reply without it would leave the tab
        // bar empty and the download list unreachable.
        if (m_categories.isEmpty()) {
            m_categories.append(DownloadCategory{.title = tr("All")});
            m_categoryOldIndex.append(0);
        }

        m_downloadModel->setCategoryNames(categoryNames());
        updateCategoryTabs();
    });
}

void TransferPanel::sendCategories(const QList<DownloadCategory>& categories,
                                   const QList<int>& oldIndex)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    QCborArray rows;
    for (int i = 0; i < categories.size(); ++i) {
        const auto& cat = categories.at(i);
        rows.append(QCborMap{
            // -1 for an entry the user just created: nothing points at it yet.
            {QStringLiteral("oldIndex"),      i < oldIndex.size() ? oldIndex.at(i) : -1},
            {QStringLiteral("title"),         cat.title},
            {QStringLiteral("incoming"),      cat.incomingPath},
            {QStringLiteral("comment"),       cat.comment},
            {QStringLiteral("autocat"),       cat.autocat},
            {QStringLiteral("autocatRegexp"), cat.autocatIsRegexp},
            {QStringLiteral("regexp"),        cat.regexp},
            {QStringLiteral("color"),         static_cast<qint64>(cat.color)},
            {QStringLiteral("prio"),          static_cast<int>(cat.prio)},
        });
    }

    IpcMessage msg(IpcMsgType::SetCategories);
    msg.append(rows);
    m_ipc->sendRequest(msg, [this](const IpcMessage& resp) {
        if (!resp.fieldBool(0)) {
            StatusBarNotifier::post(tr("Could not save categories: %1")
                                        .arg(resp.field(1).toString()));
        }
        // Refetch either way: on success to pick up what the daemon sanitised
        // (a folder it refused, say), on failure to get back in step with what
        // is actually stored.
        requestCategories();
    });
}

void TransferPanel::showCategoryMenu(int tabIndex, const QPoint& globalPos)
{
    const int index = tabIndex >= 0 ? tabIndex : 0;
    const bool isAll = index == 0;

    QMenu menu(this);
    menu.addSection(isAll ? tr("Category")
                          : tr("Category (%1)").arg(categoryTitle(index)));

    // Priority for the category — MFC's own submenu, and the value it edits is
    // the a4af priority that decides which paused file resumes first, not the
    // download priority of the files in it.
    if (!isAll) {
        auto* prioMenu = menu.addMenu(tr("Priority"));
        const std::array<std::pair<int, QString>, 3> prios{{
            {PrLow, tr("Low")}, {PrNormal, tr("Normal")}, {PrHigh, tr("High")}}};
        for (const auto& [value, label] : prios) {
            const int prioValue = value;
            auto* act = prioMenu->addAction(label, this, [this, index, prioValue] {
                auto categories = m_categories;
                categories[index].prio = static_cast<quint8>(prioValue);
                sendCategories(categories, m_categoryOldIndex);
            });
            act->setCheckable(true);
            act->setChecked(m_categories.at(index).prio == value);
        }
        menu.addSeparator();
    }

    menu.addAction(menuIcon("Pause.ico"), tr("Pause"), this,
                   [this, index] { sendCategoryStatus(index, Ipc::CategoryAction::Pause); });
    menu.addAction(menuIcon("Stop.ico"), tr("Stop"), this,
                   [this, index] { sendCategoryStatus(index, Ipc::CategoryAction::Stop); });
    menu.addAction(menuIcon("Start.ico"), tr("Resume"), this,
                   [this, index] { sendCategoryStatus(index, Ipc::CategoryAction::Resume); });
    menu.addAction(menuIcon("Delete.ico"), tr("Cancel"), this, [this, index] {
        // MFC asks first (IDS_Q_CANCELDL) and so does this: the action deletes
        // every part file in the category, and there is no undo.
        if (QMessageBox::question(
                this, tr("Cancel"),
                tr("Are you sure you want to cancel every download in \"%1\"?")
                    .arg(categoryTitle(index)))
            == QMessageBox::Yes)
        {
            sendCategoryStatus(index, Ipc::CategoryAction::Cancel);
        }
    });
    menu.addAction(tr("Resume next file"), this, [this, index] {
        sendCategoryStatus(index, Ipc::CategoryAction::ResumeNext);
    });

    menu.addSeparator();
    // MFC's MP_HM_OPENINC (srchybrid/TransferWnd.cpp:1021). The daemon may be on
    // another machine, so the shared helper decides between the file manager and
    // the browse page; "!N" is how that page addresses a category root.
    menu.addAction(tr("Open Incoming Folder"), this, [this, index] {
        const QString localPath = index > 0 && index < m_categories.size()
                                          && !m_categories.at(index).incomingPath.isEmpty()
                                      ? m_categories.at(index).incomingPath
                                      : m_defaultIncomingDir;
        const QString relPath = localPath == m_defaultIncomingDir
                                    ? QString{}
                                    : QStringLiteral("!%1").arg(index);
        openIncomingFolder(m_ipc, m_streamToken, localPath, relPath);
    });

    menu.addSeparator();
    menu.addAction(tr("Add Category..."), this, &TransferPanel::addCategoryInteractive);
    auto* editAct = menu.addAction(tr("Edit Category..."), this,
                                   [this, index] { editCategory(index); });
    auto* removeAct = menu.addAction(tr("Remove Category"), this,
                                     [this, index] { removeCategory(index); });
    // "All" is not a category the user owns — it has no folder, no colour and
    // no place in the list. MFC greys the same two entries on index 0
    // (srchybrid/TransferWnd.cpp:766-767).
    editAct->setEnabled(!isAll);
    removeAct->setEnabled(!isAll);

    menu.exec(globalPos);
}

void TransferPanel::addCategoryInteractive()
{
    CategoryDialog dialog(DownloadCategory{}, m_defaultIncomingDir, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    auto categories = m_categories;
    auto oldIndex = m_categoryOldIndex;
    categories.append(dialog.category());
    oldIndex.append(-1); // brand new — no downloads point at it yet
    sendCategories(categories, oldIndex);
}

void TransferPanel::editCategory(int index)
{
    if (index <= 0 || index >= m_categories.size())
        return;

    CategoryDialog dialog(m_categories.at(index), m_defaultIncomingDir, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    auto categories = m_categories;
    categories[index] = dialog.category();
    sendCategories(categories, m_categoryOldIndex);
}

void TransferPanel::removeCategory(int index)
{
    if (index <= 0 || index >= m_categories.size())
        return;

    // Removing renumbers: the downloads in this category fall back to "All" and
    // everything behind it shifts down one. Worth saying out loud, because the
    // list is the only place that mapping is recorded.
    if (QMessageBox::question(this, tr("Remove Category"),
                              tr("Remove the category \"%1\"?\n\n"
                                 "Its downloads keep their files and move to All.")
                                  .arg(categoryTitle(index)))
        != QMessageBox::Yes)
    {
        return;
    }

    // A plain removal: the surviving entries carry their old indices with them,
    // so the daemon moves each download to wherever its category ended up and
    // sends the ones that named this category back to All.
    auto categories = m_categories;
    auto oldIndex = m_categoryOldIndex;
    categories.removeAt(index);
    oldIndex.removeAt(index);

    sendCategories(categories, oldIndex);
}

void TransferPanel::sendCategoryStatus(int index, Ipc::CategoryAction action)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage msg(IpcMsgType::SetCategoryStatus);
    msg.append(static_cast<qint64>(index));
    msg.append(static_cast<qint64>(action));
    m_ipc->sendRequest(msg, [](const IpcMessage&) {});
}

void TransferPanel::showPriorityMenu()
{
    const QStringList hashes = saveDownloadSelectionMulti();
    if (hashes.isEmpty())
        return;

    // Collect selected download rows for check-mark logic
    std::vector<const DownloadRow*> selectedDls;
    for (const QString& h : hashes) {
        for (int i = 0; i < m_downloadModel->downloadCount(); ++i) {
            if (m_downloadModel->hashAt(i) == h) {
                selectedDls.push_back(m_downloadModel->downloadAt(i));
                break;
            }
        }
    }
    if (selectedDls.empty())
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

    // Only show check marks when all selected items share the same priority
    auto allMatchPrio = [&](int prio) {
        const QString ps = prioStr(prio);
        return std::all_of(selectedDls.begin(), selectedDls.end(),
            [&](const DownloadRow* d) { return !d->isAutoDownPriority && d->priority == ps; });
    };
    auto allAuto = [&]() {
        return std::all_of(selectedDls.begin(), selectedDls.end(),
            [](const DownloadRow* d) { return d->isAutoDownPriority; });
    };

    QMenu menu(this);
    auto addPrioAction = [&](const QString& text, int prio) {
        auto* act = menu.addAction(text, this, [this, hashes, prio]() {
            sendSetPriorityBatch(hashes, prio, false);
        });
        if (allMatchPrio(prio))
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
        auto* autoAct = menu.addAction(tr("Auto"), this, [this, hashes]() {
            sendSetPriorityBatch(hashes, PrNormal, true);
        });
        if (allAuto())
            autoAct->setCheckable(true), autoAct->setChecked(true);
    }

    // Show below the priority button in the action toolbar
    menu.exec(QCursor::pos());
}

void TransferPanel::showFindDialog()
{
    showFindInListDialog(this, m_downloadView);
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


    // 1. Details...
    if (client) {
        const QString clientHash = client->userHash;
        auto* detailsAct = menu.addAction(menuIcon("UserDetails.ico"), tr("Details..."), this,
            [this, view, model, clientHash]() {
                fetchAndShowClientDetails(clientHash, makeClientWalker(view, model, clientHash));
            });
        detailsAct->setEnabled(true);
        // MFC ClientMenu.SetDefaultItem(MP_DETAIL) (srchybrid/ClientListCtrl.cpp:346).
        setMenuDefaultAction(&menu, detailsAct);
    }

    // 2. Add To Friends
    auto* addFriendAct = menu.addAction(menuIcon("UserAdd.ico"), tr("Add To Friends"));
    addFriendAct->setEnabled(client != nullptr && !client->isFriend);
    if (client && !client->isFriend) {
        const QString hash = client->userHash;
        const QString name = client->userName;
        const auto ip   = client->ip;
        const QString addr = client->addr;
        const auto port = client->port;
        connect(addFriendAct, &QAction::triggered, this, [this, hash, name, ip, addr, port]() {
            if (!m_ipc || !m_ipc->isConnected())
                return;
            IpcMessage msg(IpcMsgType::AddFriend);
            msg.append(hash);
            msg.append(name);
            msg.append(static_cast<qint64>(ip));
            msg.append(static_cast<qint64>(port));
            msg.append(addr);   // IPv6-capable form; ip is 0 for an IPv6 peer
            m_ipc->sendRequest(std::move(msg));
        });
    }

    // 3. Send Message
    auto* sendMsgAct = menu.addAction(menuIcon("UserMessage.ico"), tr("Send Message"));
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
    auto* viewSharedAct = menu.addAction(menuIcon("UserFiles.ico"), tr("View Shared Files"));
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
    menu.addAction(menuIcon("Search.ico"), tr("Find..."), this, [this, view]() {
        showClientFindDialog(view);
    });

    menu.exec(view->viewport()->mapToGlobal(pos));
}

void TransferPanel::showSourceContextMenu(const SourceRow& src, const QString& parentHash,
                                          const QPoint& globalPos)
{
    QMenu menu(this);


    // 1. Details...
    {
        const QString srcHash = src.userHash;
        auto* detailsAct = menu.addAction(menuIcon("UserDetails.ico"), tr("Details..."), this,
            [this, srcHash, parentHash]() {
                fetchAndShowClientDetails(srcHash, makeSourceWalker(parentHash, srcHash));
            });
        // MFC ClientMenu.SetDefaultItem(MP_DETAIL) (srchybrid/DownloadListCtrl.cpp:1064).
        setMenuDefaultAction(&menu, detailsAct);
    }

    // 2. Add To Friends
    auto* addFriendAct = menu.addAction(menuIcon("UserAdd.ico"), tr("Add To Friends"));
    addFriendAct->setEnabled(!src.isFriend);
    if (!src.isFriend) {
        const QString hash = src.userHash;
        const QString name = src.userName;
        const auto ip   = src.ip;
        const QString addr = src.addr;
        const auto port = src.port;
        connect(addFriendAct, &QAction::triggered, this, [this, hash, name, ip, addr, port]() {
            if (!m_ipc || !m_ipc->isConnected())
                return;
            IpcMessage msg(IpcMsgType::AddFriend);
            msg.append(hash);
            msg.append(name);
            msg.append(static_cast<qint64>(ip));
            msg.append(static_cast<qint64>(port));
            msg.append(addr);   // IPv6-capable form; ip is 0 for an IPv6 peer
            m_ipc->sendRequest(std::move(msg));
        });
    }

    // 3. Send Message
    {
        const QString clientHash = src.userHash;
        menu.addAction(menuIcon("UserMessage.ico"), tr("Send Message"), this, [this, clientHash]() {
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
        menu.addAction(menuIcon("UserFiles.ico"), tr("View Shared Files"), this, [this, clientHash]() {
            if (!m_ipc || !m_ipc->isConnected())
                return;
            IpcMessage msg(IpcMsgType::RequestClientSharedFiles);
            msg.append(clientHash);
            m_ipc->sendRequest(std::move(msg));
        });
    }

    menu.addSeparator();

    // 5. Find...
    menu.addAction(menuIcon("Search.ico"), tr("Find..."), this, [this]() {
        showClientFindDialog(m_downloadView);
    });

    menu.exec(globalPos);
}

void TransferPanel::showClientFindDialog(QTreeView* view)
{
    showFindInListDialog(this, view);
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

// ---------------------------------------------------------------------------
// Detail-dialog Prev/Next walkers
// ---------------------------------------------------------------------------

QModelIndex TransferPanel::downloadIndexFor(const QString& fileHash) const
{
    if (fileHash.isEmpty())
        return {};
    for (int row = 0; row < m_downloadModel->downloadCount(); ++row)
        if (m_downloadModel->hashAt(row) == fileHash)
            return ViewNav::fromSource(m_downloadView, m_downloadModel->index(row, 0));
    return {};
}

QModelIndex TransferPanel::sourceIndexFor(const QString& fileHash,
                                          const QString& userHash) const
{
    if (fileHash.isEmpty() || userHash.isEmpty())
        return {};

    for (int row = 0; row < m_downloadModel->downloadCount(); ++row) {
        if (m_downloadModel->hashAt(row) != fileHash)
            continue;
        const QModelIndex parent = m_downloadModel->index(row, 0);
        for (int child = 0; child < m_downloadModel->rowCount(parent); ++child) {
            const QModelIndex childIdx = m_downloadModel->index(child, 0, parent);
            const auto* src = m_downloadModel->sourceAt(childIdx);
            if (src && src->userHash == userHash)
                return ViewNav::fromSource(m_downloadView, childIdx);
        }
        return {};
    }
    return {};
}

QModelIndex TransferPanel::clientIndexFor(const QTreeView* view,
                                          const ClientListModel* model,
                                          const QString& userHash) const
{
    if (!view || !model || userHash.isEmpty())
        return {};
    for (int row = 0; row < model->clientCount(); ++row) {
        const auto* client = model->clientAt(row);
        if (client && client->userHash == userHash)
            return ViewNav::fromSource(view, model->index(row, 0));
    }
    return {};
}

DetailWalker TransferPanel::makeDownloadWalker(const QString& fileHash)
{
    // Shared anchor: the walker advances it on the click so a second Next before
    // the daemon has answered still makes progress.
    auto anchor = std::make_shared<QString>(fileHash);

    DetailWalker walker;
    walker.step = [this, anchor](int delta) -> QString {
        const QModelIndex to = ViewNav::step(m_downloadView, downloadIndexFor(*anchor),
                                             delta, &ViewNav::isTopLevel);
        if (!to.isValid())
            return {};
        *anchor = m_downloadModel->hashAt(ViewNav::toSource(to).row());
        return *anchor;
    };
    walker.canStep = [this, anchor](int delta) {
        return ViewNav::peekStep(m_downloadView, downloadIndexFor(*anchor),
                                 delta, &ViewNav::isTopLevel).isValid();
    };
    return walker;
}

DetailWalker TransferPanel::makeSourceWalker(const QString& parentHash,
                                             const QString& userHash)
{
    // A user hash alone is ambiguous — the same peer is a source of several
    // downloads — so the anchor is the (download, source) pair.
    auto anchor = std::make_shared<std::pair<QString, QString>>(parentHash, userHash);

    DetailWalker walker;
    walker.step = [this, anchor](int delta) -> QString {
        const QModelIndex from = sourceIndexFor(anchor->first, anchor->second);
        const QModelIndex to = ViewNav::step(m_downloadView, from, delta, &ViewNav::isChild);
        if (!to.isValid())
            return {};

        // isChild lets the walk cross into the next expanded download's sources,
        // matching CDownloadListListCtrlItemWalk over MFC's flat list.
        const QModelIndex srcIdx = ViewNav::toSource(to);
        const auto* row = m_downloadModel->sourceAt(srcIdx);
        if (!row)
            return {};
        anchor->first  = m_downloadModel->hashAt(srcIdx.parent().row());
        anchor->second = row->userHash;
        return row->userHash;
    };
    walker.canStep = [this, anchor](int delta) {
        return ViewNav::peekStep(m_downloadView, sourceIndexFor(anchor->first, anchor->second),
                                 delta, &ViewNav::isChild).isValid();
    };
    return walker;
}

DetailWalker TransferPanel::makeClientWalker(QTreeView* view, ClientListModel* model,
                                             const QString& userHash)
{
    auto anchor = std::make_shared<QString>(userHash);

    DetailWalker walker;
    walker.step = [this, view, model, anchor](int delta) -> QString {
        const QModelIndex to = ViewNav::step(view, clientIndexFor(view, model, *anchor), delta);
        if (!to.isValid())
            return {};
        const auto* row = model->clientAt(ViewNav::toSource(to).row());
        if (!row)
            return {};
        *anchor = row->userHash;
        return *anchor;
    };
    walker.canStep = [this, view, model, anchor](int delta) {
        return ViewNav::peekStep(view, clientIndexFor(view, model, *anchor), delta).isValid();
    };
    return walker;
}

} // namespace eMule
