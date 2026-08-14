#include "pch.h"
/// @file SharedFilesPanel.cpp
/// @brief Shared Files tab panel — implementation.

#include "panels/SharedFilesPanel.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/AbstractListView.h"
#include "controls/SharedFilesModel.h"
#include "controls/SharedPartsDelegate.h"
#include "dialogs/ArchivePreviewPanel.h"
#include "dialogs/FileDetailDialog.h"
#include "dialogs/FindInListDialog.h"
#include "dialogs/MediaInfoPanel.h"
#include "prefs/Preferences.h"
#include "utils/IpcFeedback.h"
#include "utils/Log.h"
#include "utils/PanelPoller.h"
#include "utils/StatusBarNotifier.h"
#include "utils/ViewNavigation.h"
#include "utils/WebServices.h"
#include "dialogs/CollectionCreateDialog.h"
#include "dialogs/CollectionViewDialog.h"

#include "files/Collection.h"
#include "files/CollectionFile.h"
#include "search/SearchParams.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include "IpcMessage.h"

#include <QApplication>
#include <QCborArray>
#include <QCheckBox>
#include <QCborMap>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QPointer>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace eMule {

using namespace Ipc;

namespace {

/// Custom role marking items that lazy-load filesystem children.
constexpr int kRoleFsItem = Qt::UserRole + 2;

/// MFC upload priority integer constants matching KnownFile.h
constexpr int PrVeryLow  = 4;
constexpr int PrLow      = 0;
constexpr int PrNormal   = 1;
constexpr int PrHigh     = 2;
constexpr int PrVeryHigh = 3;

/// Format a byte count for display.
QString formatSize(int64_t bytes)
{
    if (bytes < 0)
        return {};
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return QStringLiteral("%1 KiB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GiB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

/// Format a data rate in bytes/sec for display (matching MFC CastItoXBytes with rate flag).
QString formatSpeed(int64_t bytesPerSec)
{
    if (bytesPerSec <= 0)
        return QStringLiteral("0 B/s");
    if (bytesPerSec < 1024)
        return QStringLiteral("%1 B/s").arg(bytesPerSec);
    if (bytesPerSec < 1024 * 1024)
        return QStringLiteral("%1 KB/s").arg(static_cast<double>(bytesPerSec) / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 MB/s").arg(static_cast<double>(bytesPerSec) / (1024.0 * 1024.0), 0, 'f', 1);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SharedFilesPanel::SharedFilesPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    m_poller = new PanelPoller(this, [this] { onRefreshTimer(); });
}

SharedFilesPanel::~SharedFilesPanel() = default;

// ---------------------------------------------------------------------------
// IPC wiring
// ---------------------------------------------------------------------------

void SharedFilesPanel::setIpcClient(IpcClient* client)
{
    m_ipc = client;

    if (m_ipc && m_ipc->isConnected()) {
        m_poller->setInterval(m_ipc->pollingInterval());
        m_poller->setEnabled(true);
    } else if (m_ipc) {
        connect(m_ipc, &IpcClient::connected, this, [this]() {
            m_poller->setInterval(m_ipc->pollingInterval());
            m_poller->setEnabled(true);
        });
        connect(m_ipc, &IpcClient::disconnected, this, [this]() {
            m_poller->setEnabled(false);
            m_model->clear();
            m_headerLabel->setText(tr("Shared Files (0)"));
        });

        // Pull the next poll forward rather than refetching here. SharedFileList
        // emits fileAdded once per file *inside* the directory scan, so a reload of
        // a large share used to mean one full-list refetch per file shared.
        connect(m_ipc, &IpcClient::sharedFileUpdated, this, [this](const IpcMessage&) {
            m_poller->nudge();
        });
    } else {
        m_poller->setEnabled(false);
        m_model->clear();
        m_headerLabel->setText(tr("Shared Files (0)"));
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void SharedFilesPanel::onRefreshTimer()
{
    requestSharedFiles();
}

void SharedFilesPanel::onFolderSelectionChanged()
{
    auto items = m_folderTree->selectedItems();
    if (items.isEmpty())
        return;

    auto* item = items.first();

    // Determine filter type from item data
    const int filterType = item->data(0, Qt::UserRole).toInt();
    const QString path = item->data(0, Qt::UserRole + 1).toString();

    m_proxy->setFolderFilter(static_cast<SharedFilterType>(filterType), path);
}

void SharedFilesPanel::onFileSelectionChanged()
{
    if (m_restoringSelection)
        return;   // our own doing, and the tabs are refreshed once the restore completes

    updateStatsTab();
    updateContentTab();
    updateEd2kTab();
}

void SharedFilesPanel::onFileContextMenu(const QPoint& pos)
{
    // Act on the selection, not on the row under the cursor: Qt leaves a multi-selection
    // intact on a right-click, and right-clicking below the last row must still target it.
    const QStringList hashes = selectedHashes();
    const bool hasSel = !hashes.isEmpty();
    const bool singleSel = hashes.size() == 1;

    // Snapshot for building the menu only — every action captures hashes by value and
    // re-resolves through findByHash, because a refresh replaces the model's rows while
    // the menu (or a dialog it opened) is still up.
    const std::vector<const SharedFileRow*> sel = rowsForHashes(hashes);
    const SharedFileRow* single = (singleSel && !sel.empty()) ? sel.front() : nullptr;

    // A part file is an unfinished download: deleting it destroys the transfer, and it
    // cannot be unshared either (MFC greys both, srchybrid/SharedFilesCtrl.cpp:768-769).
    const bool allComplete = hasSel && std::ranges::none_of(sel, &SharedFileRow::isPartFile);
    QStringList completeHashes;
    for (const SharedFileRow* f : sel) {
        if (!f->isPartFile)
            completeHashes << f->hash;
    }

    const bool localConn = m_ipc && m_ipc->isLocalConnection();

    // Rebuild menu
    if (!m_contextMenu)
        m_contextMenu = new QMenu(this);
    else
        m_contextMenu->clear();

    const bool useOriginal = thePrefs.useOriginalIcons();
    auto ico = [&](const char* res) -> QIcon {
        return useOriginal ? QIcon(QStringLiteral(":/icons/") + QLatin1String(res))
                           : QIcon();
    };

    // Open File — single, complete file only, and only when the daemon runs on this machine
    {
        auto* act = m_contextMenu->addAction(ico("FileOpen.ico"), tr("Open File"), this,
                                             [this, hashes]() {
            if (const SharedFileRow* f = m_model->findByHash(hashes.value(0)))
                QDesktopServices::openUrl(QUrl::fromLocalFile(f->filePath));
        });
        const bool canOpen = singleSel && single && !single->isPartFile && localConn;
        act->setEnabled(canOpen);
        if (canOpen)
            m_contextMenu->setDefaultAction(act);   // MFC SetDefaultItem(MP_OPEN)
    }

    // Open Folder
    {
        auto* act = m_contextMenu->addAction(ico("FolderOpen.ico"), tr("Open Folder"), this,
                                             [this, hashes]() {
            if (const SharedFileRow* f = m_model->findByHash(hashes.value(0)))
                QDesktopServices::openUrl(QUrl::fromLocalFile(f->path));
        });
        act->setEnabled(singleSel && single && !single->isPartFile && localConn);
    }

    m_contextMenu->addSeparator();

    // Rename
    {
        auto* act = m_contextMenu->addAction(ico("Rename.ico"), tr("Rename..."), this,
                                             [this, hashes]() {
            const SharedFileRow* f = m_model->findByHash(hashes.value(0));
            if (!f || !m_ipc || !m_ipc->isConnected())
                return;
            const QString hash = f->hash;
            const QString oldName = f->fileName;
            bool ok = false;
            const QString newName = QInputDialog::getText(
                this, tr("Rename File"), tr("New file name:"),
                QLineEdit::Normal, oldName, &ok);
            if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == oldName)
                return;
            IpcMessage msg(IpcMsgType::RenameSharedFile);
            msg.append(hash);
            msg.append(newName.trimmed());
            m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
                requestSharedFiles();
            });
        });
        act->setEnabled(singleSel && single && !single->isPartFile);
    }

    // Delete From Disk — every selected file must be complete, or an in-progress
    // download would be destroyed (the daemon's handler has no such guard).
    {
        auto* act = m_contextMenu->addAction(ico("Delete.ico"), tr("Delete From Disk"), this,
                                             [this, hashes]() { sendDeleteFilesBatch(hashes); });
        act->setEnabled(hasSel && allComplete);
    }

    // Unshare — acts on the complete files in the selection; part files stay put
    {
        auto* act = m_contextMenu->addAction(ico("ListRemove.ico"), tr("Unshare"), this,
                                             [this, completeHashes]() {
            sendUnshareBatch(completeHashes);
        });
        act->setEnabled(!completeHashes.isEmpty());
    }

    m_contextMenu->addSeparator();

    // Priority (Upload) submenu
    {
        auto* prioMenu = m_contextMenu->addMenu(ico("FilePriority.ico"), tr("Priority (Upload)"));
        prioMenu->setEnabled(hasSel);
        if (hasSel) {
            // A mixed selection gets no check mark at all — MFC clears uPrioMenuItem the
            // same way (srchybrid/SharedFilesCtrl.cpp:735).
            auto addPrioAction = [&](const QString& text, int prio) {
                auto* act = prioMenu->addAction(text, this, [this, hashes, prio]() {
                    sendSetPriorityBatch(hashes, prio, false);
                });
                const bool allMatch = std::ranges::all_of(sel, [prio](const SharedFileRow* f) {
                    return !f->isAutoUpPriority && f->upPriority == prio;
                });
                if (allMatch)
                    act->setCheckable(true), act->setChecked(true);
            };
            addPrioAction(tr("Very Low"),  PrVeryLow);
            addPrioAction(tr("Low"),       PrLow);
            addPrioAction(tr("Normal"),    PrNormal);
            addPrioAction(tr("High"),      PrHigh);
            addPrioAction(tr("Very High"), PrVeryHigh);
            prioMenu->addSeparator();
            auto* autoAct = prioMenu->addAction(tr("Auto"), this, [this, hashes]() {
                sendSetPriorityBatch(hashes, PrNormal, true);
            });
            if (std::ranges::all_of(sel, &SharedFileRow::isAutoUpPriority))
                autoAct->setCheckable(true), autoAct->setChecked(true);
        }
    }

    // Collection submenu
    {
        const bool isColl = singleSel && single && single->isCollection;
        const bool hasAuthorKey = isColl && single->hasCollectionAuthorKey;
        const QString hash = hasSel ? hashes.constFirst() : QString{};

        auto* collMenu = m_contextMenu->addMenu(ico("SharedFilesList.ico"), tr("Collection"));

        // Create Collection...
        auto* createAct = collMenu->addAction(tr("Create Collection..."), this, [this, hashes]() {
            auto* dlg = new CollectionCreateDialog(m_ipc, hashes, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        });
        createAct->setEnabled(hasSel);

        // Modify Collection...
        auto* modifyAct = collMenu->addAction(tr("Modify Collection..."), this, [this, hash]() {
            Ipc::IpcMessage msg(Ipc::IpcMsgType::GetCollectionInfo);
            msg.append(hash);
            m_ipc->sendRequest(std::move(msg), [this](const Ipc::IpcMessage& resp) {
                if (resp.type() != Ipc::IpcMsgType::Result || !resp.fieldBool(0))
                    return;
                const QCborMap data = resp.fieldMap(1);
                const QString name = data.value(QStringLiteral("name")).toString();
                const bool textFmt = data.value(QStringLiteral("textFormat")).toBool();
                const QCborArray filesArr = data.value(QStringLiteral("files")).toArray();

                QList<QVariantMap> files;
                for (const auto& v : filesArr) {
                    const QCborMap fm = v.toMap();
                    QVariantMap row;
                    row[QStringLiteral("hash")] = fm.value(QStringLiteral("hash")).toString();
                    row[QStringLiteral("fileName")] = fm.value(QStringLiteral("fileName")).toString();
                    row[QStringLiteral("fileSize")] = fm.value(QStringLiteral("fileSize")).toInteger();
                    files.append(row);
                }

                auto* dlg = new CollectionCreateDialog(m_ipc, {}, this);
                dlg->loadExistingCollection({}, name, files, textFmt);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();
            });
        });
        modifyAct->setEnabled(isColl);

        // View Collection...
        auto* viewAct = collMenu->addAction(tr("View Collection..."), this, [this, hash]() {
            Ipc::IpcMessage msg(Ipc::IpcMsgType::GetCollectionInfo);
            msg.append(hash);
            m_ipc->sendRequest(std::move(msg), [this](const Ipc::IpcMessage& resp) {
                if (resp.type() != Ipc::IpcMsgType::Result || !resp.fieldBool(0))
                    return;
                const QCborMap data = resp.fieldMap(1);

                // Build a temporary Collection from IPC data
                auto* coll = new Collection;
                coll->m_name = data.value(QStringLiteral("name")).toString();
                coll->m_authorName = data.value(QStringLiteral("authorName")).toString();

                const QCborArray filesArr = data.value(QStringLiteral("files")).toArray();
                for (const auto& v : filesArr) {
                    const QCborMap fm = v.toMap();
                    auto cf = std::make_unique<CollectionFile>();
                    const QString cfHash = fm.value(QStringLiteral("hash")).toString();
                    const QByteArray hashBytes = QByteArray::fromHex(cfHash.toLatin1());
                    if (hashBytes.size() == 16)
                        cf->setFileHash(reinterpret_cast<const uint8*>(hashBytes.constData()));
                    cf->setFileSize(fm.value(QStringLiteral("fileSize")).toInteger());
                    cf->setFileName(fm.value(QStringLiteral("fileName")).toString(), true);
                    coll->addFile(cf.get(), true);
                }

                auto* dlg = new CollectionViewDialog(*coll, m_ipc, this);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                // Transfer ownership of collection to dialog
                connect(dlg, &QDialog::destroyed, dlg, [coll]() { delete coll; });
                dlg->show();
            });
        });
        viewAct->setEnabled(isColl);

        // Search Author's Collections...
        // Like MFC's MP_SEARCHAUTHOR (srchybrid/SharedFilesCtrl.cpp:1013), this is just an
        // ordinary Kad keyword search on the author's public key, restricted to collection
        // files and captioned with the author's name.
        auto* searchAct = collMenu->addAction(tr("Search Author's Collections..."), this, [this, hash]() {
            if (!m_ipc || !m_ipc->isConnected())
                return;
            Ipc::IpcMessage msg(Ipc::IpcMsgType::GetCollectionInfo);
            msg.append(hash);
            QPointer<SharedFilesPanel> self(this);
            m_ipc->sendRequest(std::move(msg), [self](const Ipc::IpcMessage& resp) {
                if (!self)
                    return;
                if (!IpcFeedback::checkOrWarn(resp, self, tr("Search Author's Collections")))
                    return;

                const QCborMap data = resp.fieldMap(1);
                const QString authorKeyHex = data.value(QStringLiteral("authorKeyHex")).toString();
                if (authorKeyHex.isEmpty()) {
                    QMessageBox::warning(self, tr("Search Author's Collections"),
                        tr("This collection carries no author key, so its author's other "
                           "collections cannot be looked up."));
                    return;
                }

                const QString authorName = data.value(QStringLiteral("authorName")).toString();
                emit self->searchRequested(authorKeyHex,
                                           QStringLiteral(ED2KFTSTR_EMULECOLLECTION),
                                           static_cast<int>(SearchType::Kademlia),
                                           stringLimit(authorName, 50));
            });
        });
        searchAct->setEnabled(hasAuthorKey);
    }

    m_contextMenu->addSeparator();

    // Details... — the dialog shows one file, so single selection only (as in Transfers)
    {
        const QString hash = hasSel ? hashes.constFirst() : QString{};
        auto* act = m_contextMenu->addAction(ico("FileInfo.ico"), tr("Details..."), this,
                                             [this, hash]() {
            fetchAndShowSharedFileDetails(hash, FileDetailDialog::General);
        });
        act->setEnabled(singleSel);
    }

    // Comments...
    {
        const QString hash = hasSel ? hashes.constFirst() : QString{};
        auto* act = m_contextMenu->addAction(ico("FileComments.ico"), tr("Comments..."), this,
                                             [this, hash]() {
            fetchAndShowSharedFileDetails(hash, FileDetailDialog::Comments);
        });
        act->setEnabled(singleSel);
    }

    // eD2K Links — one link per selected file, as MFC's MP_GETED2KLINK does
    {
        auto* act = m_contextMenu->addAction(ico("eD2kLink.ico"), tr("eD2K Links..."), this,
                                             [this, hashes]() { copyEd2kLinks(hashes); });
        act->setEnabled(hasSel);
    }

    // Find — searches the list itself, so it only needs the list to be non-empty
    {
        auto* act = m_contextMenu->addAction(ico("Search.ico"), tr("Find..."));
        connect(act, &QAction::triggered, this, &SharedFilesPanel::showFindDialog);
        act->setEnabled(m_proxy->rowCount() > 0);
    }

    // Web Services submenu — the macros describe one file (MFC greys it for a multi-selection)
    {
        auto* webMenu = m_contextMenu->addMenu(ico("Web.ico"), tr("Web Services"));
        if (single) {
            WebServices::instance().populateFileMenu(webMenu, single->hash, single->fileName,
                                                      static_cast<uint64_t>(single->fileSize));
        }
        if (webMenu->isEmpty())
            webMenu->setEnabled(false);
    }

    m_contextMenu->popup(m_fileView->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void SharedFilesPanel::setupUi()
{
    m_model = new SharedFilesModel(this);
    m_proxy = new SharedFilesSortProxy(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(Qt::UserRole);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Vertical splitter: top (tree+files) / bottom (tabs)
    m_vertSplitter = new QSplitter(Qt::Vertical, this);
    m_vertSplitter->setHandleWidth(4);
    m_vertSplitter->setChildrenCollapsible(false);
    m_vertSplitter->setStyleSheet(
        QStringLiteral("QSplitter::handle { background: palette(mid); }"));

    m_vertSplitter->addWidget(createTopSection());
    m_vertSplitter->addWidget(createBottomTabs());
    m_vertSplitter->setStretchFactor(0, 3);
    m_vertSplitter->setStretchFactor(1, 1);

    theUiState.bindSharedVertSplitter(m_vertSplitter);

    mainLayout->addWidget(m_vertSplitter, 1);
}

QWidget* SharedFilesPanel::createTopSection()
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Header label
    auto* headerRow = new QHBoxLayout;
    headerRow->setContentsMargins(4, 0, 0, 0);
    headerRow->setSpacing(2);

    auto* icon = new QLabel;
    icon->setFixedSize(16, 16);
    icon->setScaledContents(true);
    icon->setPixmap(QIcon(QStringLiteral(":/icons/SharedFilesList.ico")).pixmap(16, 16));
    headerRow->addWidget(icon);

    m_headerLabel = new QLabel(tr("Shared Files (0)"));
    QFont bold = m_headerLabel->font();
    bold.setBold(true);
    m_headerLabel->setFont(bold);
    m_headerLabel->setFixedHeight(22);
    headerRow->addWidget(m_headerLabel);
    headerRow->addStretch(1);

    m_reloadButton = new QPushButton(tr("Reload"));
    m_reloadButton->setFlat(true);
    m_reloadButton->setFixedHeight(22);
    connect(m_reloadButton, &QPushButton::clicked,
            this, &SharedFilesPanel::onReloadClicked);
    headerRow->addWidget(m_reloadButton);

    layout->addLayout(headerRow);

    // Horizontal splitter: folder tree (left) + file list (right)
    m_horzSplitter = new QSplitter(Qt::Horizontal);
    m_horzSplitter->setHandleWidth(4);
    m_horzSplitter->setChildrenCollapsible(false);
    m_horzSplitter->setStyleSheet(
        QStringLiteral("QSplitter::handle { background: palette(mid); }"));

    // --- Folder tree ---
    auto* folderTree = new ListTreeWidget;
    m_folderTree = folderTree;
    m_folderTree->setHeaderLabel(tr("File Name"));
    m_folderTree->setIndentation(16);
    folderTree->bindColumns(QStringLiteral("sharedFolders"), {220});

    // Build tree structure matching MFC
    m_allSharedItem = new QTreeWidgetItem(m_folderTree, {tr("All Shared Files")});
    m_allSharedItem->setData(0, Qt::UserRole, static_cast<int>(SharedFilterType::AllShared));
    m_allSharedItem->setIcon(0, QIcon(QStringLiteral(":/icons/SharedFilesList.ico")));

    m_incomingItem = new QTreeWidgetItem(m_allSharedItem, {tr("Incoming Files")});
    m_incomingItem->setData(0, Qt::UserRole, static_cast<int>(SharedFilterType::Incoming));
    m_incomingItem->setIcon(0, QIcon(QStringLiteral(":/icons/FolderOpen.ico")));

    m_incompleteItem = new QTreeWidgetItem(m_allSharedItem, {tr("Incomplete Files")});
    m_incompleteItem->setData(0, Qt::UserRole, static_cast<int>(SharedFilterType::Incomplete));
    m_incompleteItem->setIcon(0, QIcon(QStringLiteral(":/icons/FolderOpen.ico")));

    m_sharedDirsItem = new QTreeWidgetItem(m_allSharedItem, {tr("Shared Directories")});
    m_sharedDirsItem->setData(0, Qt::UserRole, static_cast<int>(SharedFilterType::SharedDirs));
    m_sharedDirsItem->setIcon(0, QIcon(QStringLiteral(":/icons/FolderOpen.ico")));

    m_allDirsItem = new QTreeWidgetItem(m_folderTree, {tr("All Directories")});
    m_allDirsItem->setData(0, Qt::UserRole, static_cast<int>(SharedFilterType::AllShared));
    m_allDirsItem->setData(0, kRoleFsItem, true);
    m_allDirsItem->setIcon(0, QIcon(QStringLiteral(":/icons/HardDisk.ico")));

    m_allDirsItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);

    // Only expand the "All Shared Files" subtree, not "All Directories"
    m_allSharedItem->setExpanded(true);
    m_folderTree->setCurrentItem(m_allSharedItem);

    m_folderTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_folderTree, &QTreeWidget::customContextMenuRequested,
            this, &SharedFilesPanel::onFolderContextMenu);
    connect(m_folderTree, &QTreeWidget::itemSelectionChanged,
            this, &SharedFilesPanel::onFolderSelectionChanged);
    connect(m_folderTree, &QTreeWidget::itemExpanded,
            this, &SharedFilesPanel::onFolderItemExpanded);

    m_horzSplitter->addWidget(m_folderTree);

    // --- File list view ---
    auto* rightWidget = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    auto* fileView = new ListTreeView;
    m_fileView = fileView;
    m_fileView->setModel(m_proxy);
    m_fileView->setRootIsDecorated(false);
    m_fileView->setAlternatingRowColors(true);
    m_fileView->setSortingEnabled(true);
    // MFC's list is multi-select too (srchybrid/SharedFilesCtrl.cpp:262 asserts no LVS_SINGLESEL).
    m_fileView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileView->setUniformRowHeights(true);
    m_fileView->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_fileView, &QTreeView::customContextMenuRequested,
            this, &SharedFilesPanel::onFileContextMenu);
    // Both signals are needed: ctrl+arrow moves the current row without changing the
    // selection, and ctrl-clicking a non-current row changes the selection without moving
    // the current row. The bottom tabs have to follow either one.
    connect(m_fileView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &SharedFilesPanel::onFileSelectionChanged);
    connect(m_fileView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SharedFilesPanel::onFileSelectionChanged);

    auto* header = m_fileView->header();
    header->setStretchLastSection(true);
    header->setDefaultSectionSize(90);
    // Hide Folder column by default (like MFC)
    header->hideSection(SharedFilesModel::ColFolder);
    // File Name, Size, Type, Priority, Requests, Transferred, Shared Parts,
    // Complete Sources, Shared Networks, Folder.
    fileView->bindColumns(QStringLiteral("sharedfiles"),
        {220, 75, 70, 80, 80, 120, 80, 100, 100, 200});

    m_fileView->setItemDelegateForColumn(SharedFilesModel::ColSharedParts,
                                          new SharedPartsDelegate(m_fileView));

    rightLayout->addWidget(m_fileView, 1);
    m_horzSplitter->addWidget(rightWidget);

    // Set default splitter proportions (tree ~25%, files ~75%)
    m_horzSplitter->setStretchFactor(0, 1);
    m_horzSplitter->setStretchFactor(1, 3);

    theUiState.bindSharedHorzSplitter(m_horzSplitter);

    layout->addWidget(m_horzSplitter, 1);
    return widget;
}

QWidget* SharedFilesPanel::createBottomTabs()
{
    m_bottomTabs = new QTabWidget;

    // --- Statistics tab (flat grid with percentage bars, matching MFC) ---
    auto* statsWidget = new QWidget;
    auto* grid = new QGridLayout(statsWidget);
    grid->setContentsMargins(8, 4, 8, 4);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(2);

    // Column layout: [label 0] [value 1] [bar 2] [right-label 3] [right-value 4]

    // MFC uses yellow gradient bars with blue percentage text
    static const QString barStyle = QStringLiteral(
        "QProgressBar { border: 1px solid #999; background: #FFFFF0;"
        "  text-align: center; color: #1446FF; font-size: 10px; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #FFFFF0, stop:1 #FFFF00); }");

    auto makeBar = [&](QProgressBar*& bar) {
        bar = new QProgressBar;
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setTextVisible(true);
        bar->setFormat(QStringLiteral("%p%"));
        bar->setFixedHeight(16);
        bar->setStyleSheet(barStyle);
    };

    int row = 0;

    // -- Current Session header --
    auto* sessionHeader = new QLabel(tr("Current Session"));
    QFont boldFont = sessionHeader->font();
    boldFont.setBold(true);
    sessionHeader->setFont(boldFont);
    grid->addWidget(sessionHeader, row, 0, 1, 3);

    // Right side labels: Popularity Rank
    grid->addWidget(new QLabel(tr("Popularity Rank:")), row, 3);
    m_statPopularity = new QLabel(QStringLiteral("-"));
    grid->addWidget(m_statPopularity, row, 4);
    ++row;

    // Session — Requests
    grid->addWidget(new QLabel(tr("  Requests:")), row, 0);
    m_statSessionRequests = new QLabel(QStringLiteral("0"));
    grid->addWidget(m_statSessionRequests, row, 1);
    makeBar(m_barSessionRequests);
    grid->addWidget(m_barSessionRequests, row, 2);

    // Right side: On Queue
    grid->addWidget(new QLabel(tr("On Queue:")), row, 3);
    m_statOnQueue = new QLabel(QStringLiteral("0"));
    grid->addWidget(m_statOnQueue, row, 4);
    ++row;

    // Session — Accepted Uploads
    grid->addWidget(new QLabel(tr("  Accepted Uploads:")), row, 0);
    m_statSessionAccepted = new QLabel(QStringLiteral("0"));
    grid->addWidget(m_statSessionAccepted, row, 1);
    makeBar(m_barSessionAccepted);
    grid->addWidget(m_barSessionAccepted, row, 2);

    // Right side: Uploading
    grid->addWidget(new QLabel(tr("Uploading:")), row, 3);
    m_statUploading = new QLabel(QStringLiteral("0"));
    grid->addWidget(m_statUploading, row, 4);
    ++row;

    // Session — Transferred
    grid->addWidget(new QLabel(tr("  Transferred:")), row, 0);
    m_statSessionTransferred = new QLabel(QStringLiteral("0 B"));
    grid->addWidget(m_statSessionTransferred, row, 1);
    makeBar(m_barSessionTransferred);
    grid->addWidget(m_barSessionTransferred, row, 2);
    ++row;

    // -- Total header --
    auto* totalHeader = new QLabel(tr("Total"));
    totalHeader->setFont(boldFont);
    grid->addWidget(totalHeader, row, 0, 1, 3);

    // Right side: Total Popularity Rank (matching MFC IDC_FS_POPULARITY2)
    grid->addWidget(new QLabel(tr("Popularity Rank:")), row, 3);
    m_statPopularity2 = new QLabel(QStringLiteral("-"));
    grid->addWidget(m_statPopularity2, row, 4);
    ++row;

    // Total — Requests
    grid->addWidget(new QLabel(tr("  Requests:")), row, 0);
    m_statTotalRequests = new QLabel(QStringLiteral("0"));
    grid->addWidget(m_statTotalRequests, row, 1);
    makeBar(m_barTotalRequests);
    grid->addWidget(m_barTotalRequests, row, 2);
    ++row;

    // Total — Accepted Uploads
    grid->addWidget(new QLabel(tr("  Accepted Uploads:")), row, 0);
    m_statTotalAccepted = new QLabel(QStringLiteral("0"));
    grid->addWidget(m_statTotalAccepted, row, 1);
    makeBar(m_barTotalAccepted);
    grid->addWidget(m_barTotalAccepted, row, 2);
    ++row;

    // Total — Transferred
    grid->addWidget(new QLabel(tr("  Transferred:")), row, 0);
    m_statTotalTransferred = new QLabel(QStringLiteral("0 B"));
    grid->addWidget(m_statTotalTransferred, row, 1);
    makeBar(m_barTotalTransferred);
    grid->addWidget(m_barTotalTransferred, row, 2);
    ++row;

    grid->setRowStretch(row, 1);
    grid->setColumnStretch(2, 1); // bars stretch
    grid->setColumnMinimumWidth(1, 60);

    m_bottomTabs->addTab(statsWidget, QIcon(QStringLiteral(":/icons/FileInfo.ico")),
                         tr("Statistics"));

    // --- Content tab (archive preview / media info) ---
    m_contentStack = new QStackedWidget;
    m_mediaInfoPanel = new MediaInfoPanel;
    m_archivePreview = new ArchivePreviewPanel;
    m_contentStack->addWidget(m_mediaInfoPanel);   // index 0
    m_contentStack->addWidget(m_archivePreview);   // index 1
    m_bottomTabs->addTab(m_contentStack, QIcon(QStringLiteral(":/icons/FileInfo.ico")),
                         tr("Content"));

    // --- eD2K Links tab (matching MFC CED2kLinkDlg layout) ---
    auto* ed2kWidget = new QWidget;
    auto* ed2kLayout = new QVBoxLayout(ed2kWidget);
    ed2kLayout->setContentsMargins(4, 4, 4, 4);

    // Link text area
    m_ed2kText = new QTextEdit;
    m_ed2kText->setReadOnly(true);
    m_ed2kText->setLineWrapMode(QTextEdit::NoWrap);
    ed2kLayout->addWidget(m_ed2kText, 1);

    // Basic Options group (MFC IDC_LD_BASICGROUP)
    m_ed2kBasicGroup = new QGroupBox(tr("Basic Options"));
    auto* basicLayout = new QHBoxLayout(m_ed2kBasicGroup);
    basicLayout->setContentsMargins(6, 2, 6, 2);
    m_ed2kSourceCheck = new QCheckBox(tr("Add Source"));
    m_ed2kSourceCheck->setEnabled(false); // requires public IP + not firewalled
    m_ed2kSourceCheck->setToolTip(tr("Not available (requires public IP and open firewall)"));
    basicLayout->addWidget(m_ed2kSourceCheck);
    basicLayout->addStretch(1);
    ed2kLayout->addWidget(m_ed2kBasicGroup);

    // Advanced Options group (MFC IDC_LD_ADVANCEDGROUP)
    m_ed2kAdvancedGroup = new QGroupBox(tr("Advanced Options"));
    auto* advLayout = new QHBoxLayout(m_ed2kAdvancedGroup);
    advLayout->setContentsMargins(6, 2, 6, 2);
    m_ed2kHtmlCheck = new QCheckBox(tr("Add HTML"));
    m_ed2kHashsetCheck = new QCheckBox(tr("Add Hashset"));
    m_ed2kHostnameCheck = new QCheckBox(tr("Hostname"));
    advLayout->addWidget(m_ed2kHtmlCheck);
    advLayout->addWidget(m_ed2kHashsetCheck);
    advLayout->addWidget(m_ed2kHostnameCheck);
    advLayout->addStretch(1);
    ed2kLayout->addWidget(m_ed2kAdvancedGroup);

    // Copy button row
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    m_copyButton = new QPushButton(tr("Copy"));
    connect(m_copyButton, &QPushButton::clicked, this, &SharedFilesPanel::copyEd2kLink);
    buttonRow->addWidget(m_copyButton);
    ed2kLayout->addLayout(buttonRow);

    // Connect checkboxes to link rebuild
    connect(m_ed2kHtmlCheck, &QCheckBox::toggled, this, &SharedFilesPanel::rebuildEd2kLink);
    connect(m_ed2kHashsetCheck, &QCheckBox::toggled, this, &SharedFilesPanel::rebuildEd2kLink);
    connect(m_ed2kHostnameCheck, &QCheckBox::toggled, this, &SharedFilesPanel::rebuildEd2kLink);

    // Enabled once the daemon reports it has something to advertise (a hostname or a
    // confirmed public IPv6) — see the GetEd2kLink reply in rebuildEd2kLink().
    m_ed2kHostnameCheck->setEnabled(false);
    m_ed2kHostnameCheck->setToolTip(
        tr("Requires a hostname configured in Preferences, or a public IPv6"));

    m_ed2kTabIndex = m_bottomTabs->addTab(ed2kWidget,
                                          QIcon(QStringLiteral(":/icons/eD2kLink.ico")),
                                          tr("eD2K Links"));

    // Links are only built while this tab is on screen, so build them when it comes up.
    connect(m_bottomTabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == m_ed2kTabIndex)
            updateEd2kTab();
    });

    return m_bottomTabs;
}

// ---------------------------------------------------------------------------
// IPC requests
// ---------------------------------------------------------------------------

void SharedFilesPanel::requestSharedFiles()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::GetSharedFiles);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            m_model->clear();
            m_headerLabel->setText(tr("Shared Files (0)"));
            return;
        }

        const SelectionState selection = saveSelection();

        // Response is a map: { "files": [...], "totalRequests": N, ... }
        const QCborMap resultMap = resp.fieldMap(1);
        const QCborArray arr = resultMap.value(QStringLiteral("files")).toArray();

        m_totalRequests          = resultMap.value(QStringLiteral("totalRequests")).toInteger();
        m_totalAccepted          = resultMap.value(QStringLiteral("totalAccepted")).toInteger();
        m_totalTransferred       = resultMap.value(QStringLiteral("totalTransferred")).toInteger();
        m_totalAllTimeRequests   = resultMap.value(QStringLiteral("totalAllTimeRequests")).toInteger();
        m_totalAllTimeAccepted   = resultMap.value(QStringLiteral("totalAllTimeAccepted")).toInteger();
        m_totalAllTimeTransferred = resultMap.value(QStringLiteral("totalAllTimeTransferred")).toInteger();

        std::vector<SharedFileRow> rows;
        rows.reserve(static_cast<size_t>(arr.size()));

        for (const auto& val : arr) {
            const QCborMap m = val.toMap();
            SharedFileRow row;
            row.hash              = m.value(QStringLiteral("hash")).toString();
            row.fileName          = m.value(QStringLiteral("fileName")).toString();
            row.fileSize          = m.value(QStringLiteral("fileSize")).toInteger();
            row.fileType          = m.value(QStringLiteral("fileType")).toString();
            row.upPriority        = static_cast<int>(m.value(QStringLiteral("upPriority")).toInteger());
            row.isAutoUpPriority  = m.value(QStringLiteral("isAutoUpPriority")).toBool();
            row.requests          = m.value(QStringLiteral("requests")).toInteger();
            row.acceptedUploads   = m.value(QStringLiteral("acceptedUploads")).toInteger();
            row.transferred       = m.value(QStringLiteral("transferred")).toInteger();
            row.allTimeRequests   = m.value(QStringLiteral("allTimeRequests")).toInteger();
            row.allTimeAccepted   = m.value(QStringLiteral("allTimeAccepted")).toInteger();
            row.allTimeTransferred = m.value(QStringLiteral("allTimeTransferred")).toInteger();
            row.completeSources   = static_cast<int>(m.value(QStringLiteral("completeSources")).toInteger());
            row.publishedED2K     = m.value(QStringLiteral("publishedED2K")).toBool();
            row.kadPublished      = m.value(QStringLiteral("kadPublished")).toBool();
            row.path              = m.value(QStringLiteral("path")).toString();
            row.filePath          = m.value(QStringLiteral("filePath")).toString();
            row.ed2kLink          = m.value(QStringLiteral("ed2kLink")).toString();
            row.isPartFile        = m.value(QStringLiteral("isPartFile")).toBool();
            row.uploadingClients  = static_cast<int>(m.value(QStringLiteral("uploadingClients")).toInteger());
            row.queuedClients     = static_cast<int>(m.value(QStringLiteral("queuedClients")).toInteger());
            row.partCount         = static_cast<int>(m.value(QStringLiteral("partCount")).toInteger());
            row.completedSize     = m.value(QStringLiteral("completedSize")).toInteger();

            // Parse per-part availability map
            const QCborArray partMapArr = m.value(QStringLiteral("sharePartMap")).toArray();
            if (!partMapArr.isEmpty()) {
                QByteArray pm;
                pm.reserve(static_cast<qsizetype>(partMapArr.size()));
                for (const auto& v : partMapArr)
                    pm.append(static_cast<char>(v.toInteger()));
                row.sharePartMap = std::move(pm);
            }

            // Collection metadata
            row.isCollection           = m.value(QStringLiteral("isCollection")).toBool();
            row.hasCollectionAuthorKey = m.value(QStringLiteral("hasCollectionAuthorKey")).toBool();

            // ED2K link building components
            row.partHashesStr  = m.value(QStringLiteral("partHashesStr")).toString();
            row.aichHashStr    = m.value(QStringLiteral("aichHashStr")).toString();
            row.uploadDataRate = m.value(QStringLiteral("uploadDataRate")).toInteger();

            // Capture incoming directory from first non-partfile
            if (m_incomingDir.isEmpty() && !row.isPartFile)
                m_incomingDir = row.path;

            rows.push_back(std::move(row));
        }

        m_proxy->setIncomingDir(m_incomingDir);
        m_model->setFiles(std::move(rows));
        m_headerLabel->setText(tr("Shared Files (%1)").arg(m_model->fileCount()));
        restoreSelection(selection);
        updateStatsTab();
        updateContentTab();
        updateEd2kTab();
    });
}

void SharedFilesPanel::sendSetPriorityBatch(const QStringList& hashes, int priority, bool isAuto)
{
    if (!m_ipc)
        return;

    m_ipc->sendBatchRequest(hashes, [priority, isAuto](const QString& hash) {
        IpcMessage msg(IpcMsgType::SetSharedFilePriority);
        msg.append(hash);
        msg.append(static_cast<qint64>(priority));
        msg.append(isAuto);
        return msg;
    }, this, [this]() { requestSharedFiles(); });
}

void SharedFilesPanel::sendDeleteFilesBatch(const QStringList& hashes)
{
    if (hashes.isEmpty() || !m_ipc || !m_ipc->isConnected())
        return;

    const SharedFileRow* first = m_model->findByHash(hashes.value(0));
    const QString question = (hashes.size() == 1)
        ? tr("Are you sure you want to permanently delete \"%1\" from disk?")
              .arg(first ? first->fileName : hashes.constFirst())
        : tr("Are you sure you want to permanently delete %n selected file(s) from disk?",
             nullptr, static_cast<int>(hashes.size()));

    if (QMessageBox::warning(this,
                             hashes.size() == 1 ? tr("Delete File") : tr("Delete Files"),
                             question, QMessageBox::Yes | QMessageBox::No,
                             QMessageBox::No) != QMessageBox::Yes)
        return;

    logInfo(tr("Deleting %n shared file(s) from disk", nullptr, static_cast<int>(hashes.size())));

    m_ipc->sendBatchRequest(hashes, [](const QString& hash) {
        IpcMessage msg(IpcMsgType::DeleteSharedFile);
        msg.append(hash);
        return msg;
    }, this, [this]() { requestSharedFiles(); });
}

void SharedFilesPanel::sendUnshareBatch(const QStringList& hashes)
{
    if (hashes.isEmpty() || !m_ipc || !m_ipc->isConnected())
        return;

    const SharedFileRow* first = m_model->findByHash(hashes.value(0));
    const QString question = (hashes.size() == 1)
        ? tr("Remove \"%1\" from the shared files list?\n\nThe file will remain on disk.")
              .arg(first ? first->fileName : hashes.constFirst())
        : tr("Remove %n selected file(s) from the shared files list?\n\n"
             "The files will remain on disk.", nullptr, static_cast<int>(hashes.size()));

    if (QMessageBox::question(this,
                              hashes.size() == 1 ? tr("Unshare File") : tr("Unshare Files"),
                              question, QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;

    m_ipc->sendBatchRequest(hashes, [](const QString& hash) {
        IpcMessage msg(IpcMsgType::UnshareFile);
        msg.append(hash);
        return msg;
    }, this, [this]() { requestSharedFiles(); });
}

// ---------------------------------------------------------------------------
// Stats / eD2K tab updates
// ---------------------------------------------------------------------------

void SharedFilesPanel::updateStatsTab()
{
    auto clearBars = [this]() {
        m_barSessionRequests->setValue(0);
        m_barSessionAccepted->setValue(0);
        m_barSessionTransferred->setValue(0);
        m_barTotalRequests->setValue(0);
        m_barTotalAccepted->setValue(0);
        m_barTotalTransferred->setValue(0);
    };

    // The anchor row, not the whole selection: a popularity rank or a percentage bar
    // summed over several files would mean nothing.
    const SharedFileRow* f = currentFile();
    if (!f) {
        m_statSessionRequests->setText(QStringLiteral("0"));
        m_statSessionAccepted->setText(QStringLiteral("0"));
        m_statSessionTransferred->setText(QStringLiteral("0 B"));
        m_statTotalRequests->setText(QStringLiteral("0"));
        m_statTotalAccepted->setText(QStringLiteral("0"));
        m_statTotalTransferred->setText(QStringLiteral("0 B"));
        m_statPopularity->setText(QStringLiteral("-"));
        m_statPopularity2->setText(QStringLiteral("-"));
        m_statOnQueue->setText(QStringLiteral("0"));
        m_statUploading->setText(QStringLiteral("0 B/s"));
        clearBars();
        return;
    }

    m_statSessionRequests->setText(QString::number(f->requests));
    m_statSessionAccepted->setText(QString::number(f->acceptedUploads));
    m_statSessionTransferred->setText(formatSize(f->transferred));
    m_statTotalRequests->setText(QString::number(f->allTimeRequests));
    m_statTotalAccepted->setText(QString::number(f->allTimeAccepted));
    m_statTotalTransferred->setText(formatSize(f->allTimeTransferred));

    // Popularity rank: file's position among all shared files sorted by request count
    const int sessionRank = computePopularityRank(f->requests, &SharedFileRow::requests);
    const int totalRank = computePopularityRank(f->allTimeRequests, &SharedFileRow::allTimeRequests);
    m_statPopularity->setText(sessionRank > 0 ? QString::number(sessionRank) : QStringLiteral("-"));
    m_statPopularity2->setText(totalRank > 0 ? QString::number(totalRank) : QStringLiteral("-"));

    m_statOnQueue->setText(QString::number(f->queuedClients));
    m_statUploading->setText(formatSpeed(f->uploadDataRate));

    // Compute percentage bars using cached aggregate totals
    auto pct = [](int64_t part, int64_t total) -> int {
        return (total > 0) ? static_cast<int>(100 * part / total) : 0;
    };

    m_barSessionRequests->setValue(pct(f->requests, m_totalRequests));
    m_barSessionAccepted->setValue(pct(f->acceptedUploads, m_totalAccepted));
    m_barSessionTransferred->setValue(pct(f->transferred, m_totalTransferred));
    m_barTotalRequests->setValue(pct(f->allTimeRequests, m_totalAllTimeRequests));
    m_barTotalAccepted->setValue(pct(f->allTimeAccepted, m_totalAllTimeAccepted));
    m_barTotalTransferred->setValue(pct(f->allTimeTransferred, m_totalAllTimeTransferred));
}

void SharedFilesPanel::updateEd2kTab()
{
    const QStringList hashes = selectedHashes();
    if (hashes.isEmpty()) {
        m_ed2kText->clear();
        m_ed2kHashsetCheck->setEnabled(false);
        m_ed2kLastHashes.clear();
        return;
    }

    // A hashset can be offered as soon as any selected file carries one; the daemon
    // simply leaves it out of the links that have none.
    const auto rows = rowsForHashes(hashes);
    const bool anyHashset = std::ranges::any_of(rows, [](const SharedFileRow* f) {
        return !f->partHashesStr.isEmpty();
    });
    m_ed2kHashsetCheck->setEnabled(anyHashset);
    if (!anyHashset)
        m_ed2kHashsetCheck->setChecked(false);

    rebuildEd2kLink();
}

void SharedFilesPanel::rebuildEd2kLink()
{
    const QStringList hashes = selectedHashes();
    if (hashes.isEmpty()) {
        m_ed2kText->clear();
        m_ed2kLastHashes.clear();
        return;
    }

    if (!m_ipc || !m_ipc->isConnected()) {
        // Last known basic links, straight from the model.
        QStringList basic;
        for (const SharedFileRow* f : rowsForHashes(hashes)) {
            if (!f->ed2kLink.isEmpty())
                basic << f->ed2kLink;
        }
        m_ed2kText->setPlainText(basic.join(QLatin1Char('\n')));
        m_ed2kLastHashes.clear();
        return;
    }

    const int flags = (m_ed2kHashsetCheck->isChecked()  ? 1 : 0)
                    | (m_ed2kHostnameCheck->isChecked() ? 2 : 0)
                    | (m_ed2kHtmlCheck->isChecked()     ? 4 : 0);

    // This runs on every poll. Nothing changed means nothing to ask for — MFC's
    // CED2kLinkDlg::UpdateLink skips the same way.
    if (flags == m_ed2kLastFlags && hashes == m_ed2kLastHashes)
        return;

    // Building links for a selection nobody is looking at would be pure socket traffic.
    if (m_ed2kTabIndex >= 0 && m_bottomTabs->currentIndex() != m_ed2kTabIndex)
        return;

    m_ed2kLastHashes = hashes;
    m_ed2kLastFlags = flags;

    // Bumped here and nowhere else — the clipboard action shares requestEd2kLinks() and
    // must not void a tab refresh that is still in flight.
    const int generation = ++m_ed2kLinkGeneration;

    requestEd2kLinks(hashes, flags & 1, flags & 2, flags & 4,
        [this, generation](const QStringList& links, bool hintAvailable) {
            // Drop a stale reply: the selection or a checkbox may have changed since.
            if (generation != m_ed2kLinkGeneration)
                return;
            if (links.isEmpty())
                return;   // nothing resolved — keep the last good text

            // Plain text even in HTML mode: "Add HTML" is there so the user can copy the
            // <a href=…> source, exactly as MFC's CED2kLinkDlg shows it in a CEdit.
            m_ed2kText->setPlainText(links.join(QLatin1Char('\n')));

            // A source hint is offerable when the daemon has something to advertise —
            // an IPv6-only advertise included, which the old "hostname contains a dot"
            // test could never enable.
            m_ed2kHostnameCheck->setEnabled(hintAvailable);
            m_ed2kHostnameCheck->setToolTip(hintAvailable
                ? tr("Add your hostname or public IPv6 as a source")
                : tr("Requires a hostname configured in Preferences, or a public IPv6"));
            if (!hintAvailable && m_ed2kHostnameCheck->isChecked())
                m_ed2kHostnameCheck->setChecked(false);   // re-triggers this request
        });
}

void SharedFilesPanel::requestEd2kLinks(
    const QStringList& hashes, bool hashset, bool sourceHint, bool html,
    std::function<void(const QStringList& links, bool hintAvailable)> apply)
{
    if (hashes.isEmpty() || !m_ipc || !m_ipc->isConnected())
        return;

    const int count = std::min<int>(static_cast<int>(hashes.size()), Ipc::MaxEd2kLinkBatch);
    if (count < hashes.size()) {
        StatusBarNotifier::post(tr("Showing eD2K links for the first %1 of %2 selected files.")
                                    .arg(count).arg(hashes.size()));
    }

    QCborArray hashArray;
    for (int i = 0; i < count; ++i)
        hashArray.append(hashes.at(i));

    // The daemon builds the links: the grammar lives in ED2KFileLink::toLink(), and only
    // the core knows what may be advertised (our public IPv6 is runtime state).
    IpcMessage req(IpcMsgType::GetEd2kLink);
    req.append(hashArray);
    req.append(hashset);
    req.append(sourceHint);
    req.append(html);

    m_ipc->sendRequest(std::move(req), [apply = std::move(apply)](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;

        const QCborArray result = resp.fieldArray(1);
        QStringList links;
        for (const auto& value : result.at(0).toArray()) {
            // Empty means the daemon no longer knows that hash — skip it rather than
            // printing a blank line.
            if (const QString link = value.toString(); !link.isEmpty())
                links << link;
        }
        apply(links, result.at(1).toBool());
    });
}

// ---------------------------------------------------------------------------
// Content tab — archive preview or media info
// ---------------------------------------------------------------------------

bool SharedFilesPanel::isArchiveFile(const QString& fileType, const QString& fileName)
{
    if (fileType == QLatin1String("Arc") || fileType == QLatin1String("Iso"))
        return true;

    const QString ext = fileName.section(u'.', -1).toLower();
    static const QSet<QString> archiveExts = {
        QStringLiteral("zip"), QStringLiteral("rar"), QStringLiteral("7z"),
        QStringLiteral("iso"), QStringLiteral("ace"), QStringLiteral("tar"),
        QStringLiteral("gz"),  QStringLiteral("bz2"), QStringLiteral("cab"),
        QStringLiteral("nrg"),
    };
    return archiveExts.contains(ext);
}

void SharedFilesPanel::updateContentTab()
{
    const SharedFileRow* f = currentFile();

    // Both panels restart a background scan on every setFile(), and this runs on every
    // poll — so do nothing at all while the shown file has not changed.
    const QString path = f ? f->filePath : QString{};
    if (path == m_shownContentPath)
        return;
    m_shownContentPath = path;

    if (!f) {
        m_archivePreview->clear();
        m_mediaInfoPanel->clear();
        m_contentStack->setCurrentIndex(0);
        return;
    }

    if (isArchiveFile(f->fileType, f->fileName)) {
        m_contentStack->setCurrentIndex(1);
        m_archivePreview->setFile(f->filePath, static_cast<uint64_t>(f->fileSize));
        m_archivePreview->setAutoScan(true);
        m_mediaInfoPanel->clear();
    } else {
        m_contentStack->setCurrentIndex(0);
        m_mediaInfoPanel->setFile(f->filePath, f->fileSize);
        m_archivePreview->clear();
    }
}

// ---------------------------------------------------------------------------
// Priority menu
// ---------------------------------------------------------------------------

void SharedFilesPanel::onReloadClicked()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage msg(IpcMsgType::ReloadSharedFiles);
    m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
        requestSharedFiles();
    });
}

void SharedFilesPanel::showPriorityMenu()
{
    // Called from context menu, already handled inline
}

// ---------------------------------------------------------------------------
// Find dialog
// ---------------------------------------------------------------------------

void SharedFilesPanel::showFindDialog()
{
    showFindInListDialog(this, m_fileView);
}

// ---------------------------------------------------------------------------
// Clipboard / eD2K link
// ---------------------------------------------------------------------------

void SharedFilesPanel::copyEd2kLink()
{
    const QString text = m_ed2kText->toPlainText();
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

void SharedFilesPanel::copyEd2kLinks(const QStringList& hashes)
{
    if (hashes.isEmpty())
        return;

    // Ask for the links rather than copying whatever the eD2K tab happens to hold: that
    // text is stale whenever another bottom tab is showing. The checkboxes still apply —
    // the user set them, visibly.
    requestEd2kLinks(hashes,
                     m_ed2kHashsetCheck->isChecked(),
                     m_ed2kHostnameCheck->isChecked(),
                     m_ed2kHtmlCheck->isChecked(),
        [](const QStringList& links, bool) {
            if (!links.isEmpty())
                QApplication::clipboard()->setText(links.join(QLatin1Char('\n')));
        });
}

const SharedFileRow* SharedFilesPanel::currentFile() const
{
    const QModelIndex proxyIdx = m_fileView->selectionModel()->currentIndex();
    if (!proxyIdx.isValid())
        return nullptr;
    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
    return m_model->fileAt(srcIdx.row());
}

int SharedFilesPanel::computePopularityRank(int64_t value,
                                             int64_t (SharedFileRow::*field)) const
{
    if (value <= 0)
        return 0; // no rank when no requests
    int rank = 1;
    const int count = m_model->fileCount();
    for (int i = 0; i < count; ++i) {
        const auto* row = m_model->fileAt(i);
        if (row && row->*field > value)
            ++rank;
    }
    return rank;
}

// ---------------------------------------------------------------------------
// Selection save/restore
// ---------------------------------------------------------------------------

QStringList SharedFilesPanel::selectedHashes() const
{
    auto rows = m_fileView->selectionModel()->selectedRows(0);

    // Selection order is the order rows were clicked; the user thinks in list order, and so
    // do the link lists and the batch confirmations.
    std::ranges::sort(rows, {}, &QModelIndex::row);

    QStringList hashes;
    hashes.reserve(rows.size());
    for (const QModelIndex& proxyIdx : rows) {
        const QString hash = m_model->hashAt(m_proxy->mapToSource(proxyIdx).row());
        if (!hash.isEmpty() && !hashes.contains(hash))
            hashes.append(hash);
    }
    return hashes;
}

std::vector<const SharedFileRow*> SharedFilesPanel::rowsForHashes(const QStringList& hashes) const
{
    std::vector<const SharedFileRow*> rows;
    rows.reserve(static_cast<size_t>(hashes.size()));
    for (const QString& hash : hashes) {
        if (const SharedFileRow* row = m_model->findByHash(hash))
            rows.push_back(row);
    }
    return rows;
}

SharedFilesPanel::SelectionState SharedFilesPanel::saveSelection() const
{
    SelectionState state;
    state.hashes = selectedHashes();
    state.scrollValue = m_fileView->verticalScrollBar()->value();

    // The anchor, not hashes.first(): it is what the bottom tabs show.
    const QModelIndex current = m_fileView->selectionModel()->currentIndex();
    if (current.isValid())
        state.currentHash = m_model->hashAt(m_proxy->mapToSource(current).row());

    return state;
}

void SharedFilesPanel::restoreSelection(const SelectionState& state)
{
    m_fileView->verticalScrollBar()->setValue(state.scrollValue);
    if (state.hashes.isEmpty())
        return;

    // One pass over the model instead of a linear scan per selected hash.
    QHash<QString, int> rowByHash;
    rowByHash.reserve(m_model->fileCount());
    for (int row = 0; row < m_model->fileCount(); ++row)
        rowByHash.insert(m_model->hashAt(row), row);

    QItemSelection selection;
    QModelIndex currentIdx;
    for (const QString& hash : state.hashes) {
        const auto it = rowByHash.constFind(hash);
        if (it == rowByHash.cend())
            continue;   // gone from the share since the request went out

        const QModelIndex proxyIdx = m_proxy->mapFromSource(m_model->index(it.value(), 0));
        if (!proxyIdx.isValid())
            continue;   // hidden by the folder filter

        // selectedRows() only reports a row when every model column is selected — the
        // hidden Folder column included — so select the whole row, not just column 0.
        selection.select(proxyIdx,
                         m_proxy->index(proxyIdx.row(), SharedFilesModel::ColCount - 1));
        if (hash == state.currentHash)
            currentIdx = proxyIdx;
    }

    if (selection.isEmpty())
        return;
    if (!currentIdx.isValid())
        currentIdx = selection.indexes().constFirst();   // anchor vanished — take a survivor

    m_restoringSelection = true;
    // QAbstractItemView::setCurrentIndex() would ClearAndSelect and collapse the whole
    // selection to one row, so go through the selection model.
    m_fileView->selectionModel()->setCurrentIndex(currentIdx, QItemSelectionModel::NoUpdate);
    m_fileView->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    m_restoringSelection = false;

    m_fileView->verticalScrollBar()->setValue(state.scrollValue);
}

// ---------------------------------------------------------------------------
// Folder tree context menu
// ---------------------------------------------------------------------------

void SharedFilesPanel::onFolderContextMenu(const QPoint& pos)
{
    auto* item = m_folderTree->itemAt(pos);
    if (!item || !item->data(0, kRoleFsItem).toBool())
        return;

    const QString path = item->data(0, Qt::UserRole + 1).toString();
    if (path.isEmpty())
        return;

    QMenu menu(this);

    // Open Folder (local only)
    auto* openAct = menu.addAction(QIcon(QStringLiteral(":/icons/FolderOpen.ico")),
                                    tr("Open Folder"), this, [path]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    openAct->setEnabled(m_ipc && m_ipc->isLocalConnection());

    menu.addSeparator();

    const auto dirs = thePrefs.sharedDirs();
    const bool isShared = dirs.contains(path);

    // Share Directory
    auto* shareAct = menu.addAction(tr("Share Directory"), this, [this, path]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        auto dirs = thePrefs.sharedDirs();
        if (!dirs.contains(path))
            dirs.append(path);
        sendShareDirsUpdate(dirs);
    });
    shareAct->setEnabled(!isShared);

    // Share with Subdirectories
    auto* shareSubAct = menu.addAction(tr("Share with Subdirectories"), this, [this, path]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        auto dirs = thePrefs.sharedDirs();
        collectSubdirectories(path, dirs);
        sendShareDirsUpdate(dirs);
    });
    shareSubAct->setEnabled(!isShared);

    menu.addSeparator();

    // Unshare Directory
    auto* unshareAct = menu.addAction(tr("Unshare Directory"), this, [this, path]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        auto dirs = thePrefs.sharedDirs();
        dirs.removeAll(path);
        sendShareDirsUpdate(dirs);
    });
    unshareAct->setEnabled(isShared);

    // Unshare with Subdirectories
    auto* unshareSubAct = menu.addAction(tr("Unshare with Subdirectories"), this, [this, path]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        auto dirs = thePrefs.sharedDirs();
        QStringList toRemove;
        collectSubdirectories(path, toRemove);
        for (const auto& d : toRemove)
            dirs.removeAll(d);
        sendShareDirsUpdate(dirs);
    });
    unshareSubAct->setEnabled(isShared);

    menu.exec(m_folderTree->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// Shared file details (IPC fetch + dialog)
// ---------------------------------------------------------------------------

void SharedFilesPanel::fetchAndShowSharedFileDetails(const QString& hash, int tab)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;
    IpcMessage msg(IpcMsgType::GetSharedFileDetails);
    msg.append(hash);
    m_ipc->sendRequest(std::move(msg), [this, tab, hash](const IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;
        const QCborMap details = resp.field(1).toMap();
        auto* dlg = new FileDetailDialog(details,
                                          static_cast<FileDetailDialog::Tab>(tab), this);
        connectEd2kLinkRequests(dlg, m_ipc);
        connectKadNotesSearch(dlg, m_ipc, IpcMsgType::GetSharedFileDetails);
        connectCommentFilter(dlg, m_ipc);
        dlg->setWalker(makeSharedFileWalker(hash));
        connectDetailNavigation(dlg, m_ipc, IpcMsgType::GetSharedFileDetails);
        dlg->show();
    });
}

QModelIndex SharedFilesPanel::fileIndexFor(const QString& hash) const
{
    if (hash.isEmpty())
        return {};
    for (int row = 0; row < m_model->fileCount(); ++row)
        if (m_model->hashAt(row) == hash)
            return ViewNav::fromSource(m_fileView, m_model->index(row, 0));
    return {};
}

DetailWalker SharedFilesPanel::makeSharedFileWalker(const QString& hash)
{
    // Anchored on the hash, not a row: setFiles() resets the model on every poll
    // tick and UiState::guardSelectionOnReset() clears the current index with it.
    auto anchor = std::make_shared<QString>(hash);

    DetailWalker walker;
    walker.step = [this, anchor](int delta) -> QString {
        const QModelIndex to = ViewNav::step(m_fileView, fileIndexFor(*anchor), delta);
        if (!to.isValid())
            return {};
        *anchor = m_model->hashAt(ViewNav::toSource(to).row());
        return *anchor;
    };
    walker.canStep = [this, anchor](int delta) {
        return ViewNav::peekStep(m_fileView, fileIndexFor(*anchor), delta).isValid();
    };
    return walker;
}

// ---------------------------------------------------------------------------
// Folder share helpers
// ---------------------------------------------------------------------------

void SharedFilesPanel::sendShareDirsUpdate(const QStringList& dirs)
{
    thePrefs.setSharedDirs(dirs);
    IpcMessage req(IpcMsgType::SetPreferences);
    req.append(QStringLiteral("sharedDirs"));
    QCborArray arr;
    for (const auto& d : dirs)
        arr.append(d);
    req.append(arr);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage&) {
        requestSharedFiles();
    });
}

void SharedFilesPanel::collectSubdirectories(const QString& root, QStringList& list)
{
    if (!list.contains(root))
        list.append(root);
    QDir dir(root);
    const auto entries = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& fi : entries) {
        if (!fi.fileName().startsWith(u'.') && fi.isReadable())
            collectSubdirectories(fi.absoluteFilePath(), list);
    }
}

// ---------------------------------------------------------------------------
// Filesystem tree lazy-loading
// ---------------------------------------------------------------------------

void SharedFilesPanel::onFolderItemExpanded(QTreeWidgetItem* item)
{
    if (!item->data(0, kRoleFsItem).toBool())
        return;

    if (item->childCount() == 0)
        populateFilesystemChildren(item);
}

void SharedFilesPanel::populateFilesystemChildren(QTreeWidgetItem* parentItem)
{
    if (parentItem == m_allDirsItem) {
        initFilesystemRoot();
        return;
    }

    const QString parentPath = parentItem->data(0, Qt::UserRole + 1).toString();
    if (parentPath.isEmpty())
        return;

    QDir dir(parentPath);
    const auto entries = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
        QDir::Name | QDir::IgnoreCase);

    for (const QFileInfo& fi : entries) {
        // Skip hidden directories (name starting with '.')
        if (fi.fileName().startsWith(u'.'))
            continue;
        // Skip unreadable directories
        if (!fi.isReadable())
            continue;

        addFilesystemChild(parentItem, fi.absoluteFilePath(), fi.fileName());
    }
}

void SharedFilesPanel::initFilesystemRoot()
{
#ifdef Q_OS_MACOS
    // macOS: add root and readable volumes
    addFilesystemChild(m_allDirsItem, QStringLiteral("/"), QStringLiteral("/"));

    QDir volumes(QStringLiteral("/Volumes"));
    const auto entries = volumes.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks,
        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& fi : entries) {
        if (fi.isReadable())
            addFilesystemChild(m_allDirsItem, fi.absoluteFilePath(), fi.fileName());
    }
#else
    // Cross-platform fallback: system drives
    const auto drives = QDir::drives();
    for (const QFileInfo& fi : drives)
        addFilesystemChild(m_allDirsItem, fi.absoluteFilePath(), fi.absoluteFilePath());
#endif
}

void SharedFilesPanel::addFilesystemChild(QTreeWidgetItem* parent,
                                          const QString& path,
                                          const QString& displayName)
{
    auto* item = new QTreeWidgetItem(parent, {displayName});
    item->setData(0, Qt::UserRole, static_cast<int>(SharedFilterType::SpecificDir));
    item->setData(0, Qt::UserRole + 1, path);
    item->setData(0, kRoleFsItem, true);
    item->setIcon(0, QIcon(QStringLiteral(":/icons/FolderOpen.ico")));

    if (hasSubdirectories(path))
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
}

bool SharedFilesPanel::hasSubdirectories(const QString& path)
{
    QDir dir(path);
    const auto entries = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);

    for (const QFileInfo& fi : entries) {
        if (!fi.fileName().startsWith(u'.') && fi.isReadable())
            return true;
    }
    return false;
}

} // namespace eMule
