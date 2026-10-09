#include "pch.h"
/// @file SharedFilesPanel.cpp
/// @brief Shared Files tab panel — implementation.

#include "panels/SharedFilesPanel.h"
#include "utils/SharedFilesFetch.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/AbstractListView.h"
#include "controls/FilterEdit.h"
#include "controls/SharedFilesModel.h"
#include "controls/SharedPartsDelegate.h"
#include "dialogs/ArchivePreviewPanel.h"
#include "dialogs/FileDetailDialog.h"
#include "dialogs/FindInListDialog.h"
#include "dialogs/MediaInfoPanel.h"
#include "dialogs/RenameFileDialog.h"
#include "prefs/Preferences.h"
#include "utils/CountryFlags.h"
#include "utils/IpcFeedback.h"
#include "utils/ListActivation.h"
#include "utils/Log.h"
#include "utils/MenuUtils.h"
#include "utils/PanelPoller.h"
#include "utils/PreviewLauncher.h"
#include "utils/SharedDirState.h"
#include "utils/StatusBarNotifier.h"
#include "utils/StringUtils.h"
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
#include <QGuiApplication>
#include <QHash>
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
#include <QGridLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
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
#include <utility>

namespace eMule {

using namespace Ipc;

namespace {

/// Custom role marking items that lazy-load filesystem children.
constexpr int kRoleFsItem = Qt::UserRole + 2;
/// Custom role marking the entries under "Shared Directories".
constexpr int kRoleSharedDirItem = Qt::UserRole + 3;
constexpr int kRolePath = Qt::UserRole + 1;
/// Marks the category sub-nodes: 1 a category's incoming folder, 2 its part files.
constexpr int kRoleCategoryNode = Qt::UserRole + 4;

/// The folder icon with an MFC image-list overlay drawn over it
/// (srchybrid/SharedDirsTreeCtrl.cpp:169-170). An empty path is the plain folder.
QIcon folderIcon(const QString& overlayPath = {})
{
    static QHash<QString, QIcon> cache;
    if (const auto it = cache.constFind(overlayPath); it != cache.constEnd())
        return *it;

    const QIcon base(QStringLiteral(":/icons/FolderOpen.ico"));
    QIcon icon = base;
    if (!overlayPath.isEmpty()) {
        constexpr int kPx = 16;
        constexpr int kDpr = 2;
        QPixmap canvas(QSize(kPx, kPx) * kDpr);
        canvas.setDevicePixelRatio(kDpr);
        canvas.fill(Qt::transparent);
        {
            QPainter painter(&canvas);
            const QRect cell(0, 0, kPx, kPx);
            painter.drawPixmap(cell, base.pixmap(kPx, kPx));
            painter.drawPixmap(cell, QIcon(overlayPath).pixmap(kPx, kPx));
        }
        icon = QIcon(canvas);
    }
    cache.insert(overlayPath, icon);
    return icon;
}

void setItemBold(QTreeWidgetItem* item, bool bold)
{
    QFont font = item->font(0);
    if (font.bold() == bold)
        return;
    font.setBold(bold);
    item->setFont(0, font);
}

/// MFC upload priority integer constants matching KnownFile.h
constexpr int PrVeryLow  = 4;
constexpr int PrLow      = 0;
constexpr int PrNormal   = 1;
constexpr int PrHigh     = 2;
constexpr int PrVeryHigh = 3;

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
    if (m_ipc)
        disconnect(m_ipc, nullptr, this, nullptr);
    m_ipc = client;
    m_haveSnapshot = false;

    if (!m_ipc) {
        m_poller->setEnabled(false);
        m_model->clear();
        m_headerLabel->setText(tr("Shared Files (0)"));
        return;
    }

    // The list is fetched once and then kept current by pushes; the timer is only a
    // net under them, for the few things that have no push.
    m_poller->setInterval(kResyncMs);
    connect(m_ipc, &IpcClient::connected, this, [this]() {
        m_poller->setEnabled(true);
        requestCategoryNodes();
    });
    connect(m_ipc, &IpcClient::categoriesChanged, this, [this] { requestCategoryNodes(); });
    connect(m_ipc, &IpcClient::disconnected, this, [this]() {
        m_poller->setEnabled(false);
        m_haveSnapshot = false;
        m_model->clear();
        m_headerLabel->setText(tr("Shared Files (0)"));
    });
    connect(m_ipc, &IpcClient::sharedFileUpdated, this, &SharedFilesPanel::onSharedFilesPushed);
    connect(m_ipc, &IpcClient::sharedFileRemoved, this, &SharedFilesPanel::onSharedFileRemovedPush);

    m_poller->setEnabled(m_ipc->isConnected());
    if (m_ipc->isConnected())
        requestCategoryNodes();
}

QString SharedFilesPanel::dropSharePath(const QTreeWidgetItem* dragged, const QTreeWidgetItem* target) const
{
    if (!dragged || !target || !dragged->data(0, kRoleFsItem).toBool())
        return {};
    // only onto the shared side: the node itself or a folder listed under it
    if (target != m_sharedDirsItem && !target->data(0, kRoleSharedDirItem).toBool())
        return {};
    const QString path = dragged->data(0, kRolePath).toString();
    if (path.isEmpty() || SharedDirState::isSharedDir(thePrefs.sharedDirs(), path)
        || !thePrefs.isShareableDirectory(path))
        return {};
    return path;
}

bool SharedFilesPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_folderTree->viewport())
        return QWidget::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto* drag = static_cast<QDragMoveEvent*>(event);
        const bool ok = drag->source() == m_folderTree
            && !dropSharePath(m_folderTree->currentItem(),
                              m_folderTree->itemAt(drag->position().toPoint())).isEmpty();
        // DragEnter is accepted for any drag of our own, or no DragMove would follow
        if (ok || (event->type() == QEvent::DragEnter && drag->source() == m_folderTree))
            drag->acceptProposedAction();
        else
            drag->ignore();
        return true;
    }
    case QEvent::Drop: {
        auto* drop = static_cast<QDropEvent*>(event);
        const QString path = drop->source() == m_folderTree
            ? dropSharePath(m_folderTree->currentItem(), m_folderTree->itemAt(drop->position().toPoint()))
            : QString();
        if (!path.isEmpty() && m_ipc && m_ipc->isConnected()) {
            QStringList dirs = thePrefs.sharedDirs();
            dirs.append(path);
            sendShareDirsUpdate(dirs);
            drop->acceptProposedAction();
        } else {
            drop->ignore();
        }
        return true;   // never let the tree move its own items
    }
    default:
        return QWidget::eventFilter(watched, event);
    }
}

void SharedFilesPanel::requestCategoryNodes()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    QPointer<SharedFilesPanel> self(this);
    m_ipc->sendRequest(IpcMessage(IpcMsgType::GetCategories), [self](const IpcMessage& resp) {
        if (!self || !resp.fieldBool(0))
            return;
        QList<std::pair<QString, QString>> categories;
        for (const auto& value : resp.fieldArray(1)) {
            const QCborMap m = value.toMap();
            categories.append({m.value(QStringLiteral("title")).toString(),
                               m.value(QStringLiteral("incoming")).toString()});
        }
        self->rebuildCategoryNodes(categories);
    });
}

void SharedFilesPanel::rebuildCategoryNodes(const QList<std::pair<QString, QString>>& categories)
{
    // MFC FilterTreeReloadTree (SharedDirsTreeCtrl.cpp:324-368)
    const SharedCategoryNodes nodes = sharedCategoryNodes(categories, thePrefs.incomingDir());

    // Keep the user on the node they were on, by what it filters
    QTreeWidgetItem* current = m_folderTree->currentItem();
    const bool onCategoryNode = current && current->data(0, kRoleCategoryNode).toInt() != 0;
    const int keptKind = onCategoryNode ? current->data(0, kRoleCategoryNode).toInt() : 0;
    const QString keptPath = onCategoryNode ? current->data(0, kRolePath).toString() : QString();
    QTreeWidgetItem* keptParent = onCategoryNode ? current->parent() : nullptr;

    const QSignalBlocker blocker(m_folderTree);
    qDeleteAll(m_incomingItem->takeChildren());
    qDeleteAll(m_incompleteItem->takeChildren());

    QTreeWidgetItem* reselect = nullptr;
    for (const QString& dir : nodes.incomingDirs) {
        const bool accessible = QFileInfo::exists(dir);
        auto* item = new QTreeWidgetItem(m_incomingItem, {QDir(dir).dirName().isEmpty() ? dir : QDir(dir).dirName()});
        item->setToolTip(0, QDir::toNativeSeparators(dir));
        item->setData(0, Qt::UserRole, static_cast<int>(SharedFilterType::SpecificDir));
        item->setData(0, kRolePath, dir);
        item->setData(0, kRoleCategoryNode, 1);
        // a folder that is gone gets MFC's warning overlay
        item->setIcon(0, folderIcon(accessible ? QString()
                                               : QStringLiteral(":/icons/NoAccessFolderOvl.ico")));
        if (keptKind == 1 && keptPath == dir)
            reselect = item;
    }
    m_incomingItem->sortChildren(0, Qt::AscendingOrder);
    for (const auto& [index, title] : nodes.incomplete) {
        auto* item = new QTreeWidgetItem(m_incompleteItem, {title.isEmpty() ? tr("All") : title});
        item->setData(0, Qt::UserRole, static_cast<int>(SharedFilterType::IncompleteCategory));
        item->setData(0, kRolePath, QString::number(index));
        item->setData(0, kRoleCategoryNode, 2);
        item->setIcon(0, QIcon(QStringLiteral(":/icons/FolderOpen.ico")));
        if (keptKind == 2 && keptPath == QString::number(index))
            reselect = item;
    }
    if (onCategoryNode)
        m_folderTree->setCurrentItem(reselect ? reselect : keptParent);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void SharedFilesPanel::onRefreshTimer()
{
    requestSharedFiles();
    syncSharedDirState();
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

    // A real directory under "All Directories" browses the filesystem: the list then
    // holds unshared files too, each with a checkbox. Everything else — including the
    // "All Directories" root itself, which carries no path — just filters the share.
    const bool browse = item->data(0, kRoleFsItem).toBool() && !path.isEmpty();
    m_browseDir = browse ? path : QString();
    m_model->setBrowseMode(browse);

    if (browse) {
        requestBrowseDirectory(path);
        return;
    }

    m_proxy->setFolderFilter(static_cast<SharedFilterType>(filterType), path);
    requestSharedFiles();
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

    // Open File — one complete file at a time. Against a remote core the file lives on
    // the daemon's host and travels over the web server, so this stays available there
    // as long as the stream token has arrived.
    {
        auto* act = m_contextMenu->addAction(menuIcon("FileOpen.ico"), tr("Open File"), this,
                                             [this, hashes]() { openSharedFile(hashes.value(0)); });
        const bool canOpen = singleSel && single && !single->isPartFile
                             && (localConn || !m_streamToken.isEmpty());
        act->setEnabled(canOpen);

        // MFC SetDefaultItem(bSingleCompleteFileSelected ? MP_OPEN : -1)
        // (srchybrid/SharedFilesCtrl.cpp:770) — bold only while it applies.
        setMenuDefaultAction(m_contextMenu, canOpen ? act : nullptr);
    }

    // Open Folder
    {
        auto* act = m_contextMenu->addAction(menuIcon("FolderOpen.ico"), tr("Open Folder"), this,
                                             [this, hashes]() {
            if (const SharedFileRow* f = m_model->findByHash(hashes.value(0)))
                QDesktopServices::openUrl(QUrl::fromLocalFile(f->path));
        });
        act->setEnabled(singleSel && single && !single->isPartFile && localConn);
    }

    m_contextMenu->addSeparator();

    // Rename
    {
        auto* act = m_contextMenu->addAction(menuIcon("Rename.ico"), tr("Rename..."), this,
                                             &SharedFilesPanel::renameSelectedFile);
        act->setEnabled(singleSel && single && !single->isPartFile);
    }

    // Delete From Disk — every selected file must be complete, or an in-progress
    // download would be destroyed (the daemon's handler has no such guard).
    {
        auto* act = m_contextMenu->addAction(menuIcon("Delete.ico"), tr("Delete From Disk"), this,
                                             [this, hashes]() { sendDeleteFilesBatch(hashes); });
        act->setEnabled(hasSel && allComplete);
    }

    // Unshare — acts on the complete files in the selection; part files stay put.
    // A file in the incoming directory cannot be unshared at all (the daemon refuses
    // it, MFC greys it: srchybrid/SharedFilesCtrl.cpp:1598), and neither can a browsed
    // row the daemon already told us is locked.
    {
        // shareToggleable carries the daemon's own shouldBeShared(..., mustBeShared)
        // answer in both modes, so this needs no guess about where incoming lives.
        const bool anyUnshareable = std::ranges::any_of(sel, [](const SharedFileRow* f) {
            return !f->isPartFile && f->shareToggleable;
        });
        auto* act = m_contextMenu->addAction(menuIcon("ListRemove.ico"), tr("Unshare"), this,
                                             [this, completeHashes]() {
            sendUnshareBatch(completeHashes);
        });
        act->setEnabled(!completeHashes.isEmpty() && anyUnshareable);
    }

    m_contextMenu->addSeparator();

    // Priority (Upload) submenu
    {
        auto* prioMenu = m_contextMenu->addMenu(menuIcon("FilePriority.ico"), tr("Priority (Upload)"));
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
            addPrioAction(tr("Release"),   PrVeryHigh);   // MFC IDS_PRIORELEASE
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

        auto* collMenu = m_contextMenu->addMenu(menuIcon("SharedFilesList.ico"), tr("Collection"));

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
                dlg->loadExistingCollection(name, files, textFmt);
                dlg->setAttribute(Qt::WA_DeleteOnClose);
                dlg->show();
            });
        });
        modifyAct->setEnabled(isColl);

        // View Collection...
        auto* viewAct = collMenu->addAction(tr("View Collection..."), this,
                                            [this, hash]() { showCollection(hash); });
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

    // Details... / Comments... — several files open one combined sheet, as in MFC
    {
        auto* act = m_contextMenu->addAction(menuIcon("FileInfo.ico"), tr("Details..."), this,
                                             [this, hashes]() {
            fetchAndShowSharedFileDetails(hashes, FileDetailDialog::General);
        });
        act->setEnabled(hasSel);
    }
    {
        auto* act = m_contextMenu->addAction(menuIcon("FileComments.ico"), tr("Comments..."), this,
                                             [this, hashes]() {
            fetchAndShowSharedFileDetails(hashes, FileDetailDialog::Comments);
        });
        act->setEnabled(hasSel);
    }

    // eD2K Links — one link per selected file, as MFC's MP_GETED2KLINK does
    {
        auto* act = m_contextMenu->addAction(menuIcon("eD2kLink.ico"), tr("eD2K Links..."), this,
                                             [this, hashes]() { copyEd2kLinks(hashes); });
        act->setEnabled(hasSel);
    }

    // The link the IRC nick menu's "Send this to friend" offers (MFC Irc_SetSendLink,
    // srchybrid/SharedFilesCtrl.cpp:787, :826-828)
    {
        const QString link = single ? single->ed2kLink : QString();
        auto* act = m_contextMenu->addAction(menuIcon("IRCClipboard.ico"), tr("Add To IRC Clipboard"),
                                             this, [this, link]() { emit ircSendLinkChosen(link); });
        act->setEnabled(m_ircConnected && !link.isEmpty());
    }

    // Find — searches the list itself, so it only needs the list to be non-empty
    {
        auto* act = m_contextMenu->addAction(menuIcon("Search.ico"), tr("Find..."));
        connect(act, &QAction::triggered, this, &SharedFilesPanel::showFindDialog);
        act->setEnabled(m_proxy->rowCount() > 0);
    }

    // Web Services submenu — the macros describe one file (MFC greys it for a multi-selection)
    {
        auto* webMenu = m_contextMenu->addMenu(menuIcon("Web.ico"), tr("Web Services"));
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
    connect(m_model, &SharedFilesModel::shareToggleRequested,
            this, &SharedFilesPanel::sendSetFileShared);
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
#ifdef Q_OS_MACOS
    const QString modifierKey = QStringLiteral("\u2318");
#else
    const QString modifierKey = QStringLiteral("Ctrl");
#endif
    m_reloadButton->setToolTip(tr("Rescan the shared directories. Hold %1 to re-read the "
                                  "media information of all shared files instead.")
                                   .arg(modifierKey));
    // Ctrl+click re-reads the media tags instead (MFC OnBnClickedReloadSharedFiles)
    connect(m_reloadButton, &QPushButton::clicked, this, [this] {
        onReloadClicked(QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier));
    });
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
    // MFC's tree has no header; the filter box sits above it (emule.rc:417-430)
    m_folderTree->setHeaderHidden(true);
    m_folderTree->setIndentation(16);

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

    // Drag a folder from "All Directories" onto "Shared Directories" (or a folder
    // listed there) to share it, without its subfolders — MFC OnTvnBeginDrag /
    // OnLButtonUp, SharedDirsTreeCtrl.cpp:1022-1132.
    m_folderTree->setDragEnabled(true);
    m_folderTree->setAcceptDrops(true);
    m_folderTree->setDragDropMode(QAbstractItemView::DragDrop);
    m_folderTree->setDefaultDropAction(Qt::CopyAction);
    m_folderTree->viewport()->installEventFilter(this);
    connect(m_folderTree, &QTreeWidget::itemExpanded,
            this, &SharedFilesPanel::onFolderItemExpanded);

    auto* leftWidget = new QWidget;
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(2);
    m_filterEdit = new FilterEdit;
    leftLayout->addWidget(m_filterEdit);
    leftLayout->addWidget(m_folderTree, 1);
    m_horzSplitter->addWidget(leftWidget);

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

    // MFC CSharedFilesCtrl (srchybrid/SharedFilesCtrl.cpp:1269 OnNmDblClk, :871 IDA_ENTER,
    // :989 MPG_ALTENTER): a plain double click or Enter opens the file, and ALT with
    // either one opens the details sheet. openSharedFile() ignores a part file, which is
    // the original's `!file->IsPartFile()` guard.
    const auto openRow = [this](const QModelIndex& index) {
        openSharedFile(m_model->hashAt(ViewNav::toSource(index).row()));
    };
    const auto detailsForRow = [this](const QModelIndex& index) {
        fetchAndShowSharedFileDetails(m_model->hashAt(ViewNav::toSource(index).row()),
                                      FileDetailDialog::General);
    };
    connect(m_fileView, &QTreeView::doubleClicked, this,
            [openRow, detailsForRow](const QModelIndex& index) {
        if (!index.isValid())
            return;
        if (QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier))
            detailsForRow(index);
        else
            openRow(index);
    });
    // MFC CSharedFilesCtrl (SharedFilesCtrl.cpp:805 OnCommand, :1381 OnKeyDown): F2
    // renames, Del deletes from disk, Ctrl+C copies eD2K links, F5 reloads, Ctrl+F/F3 find.
    ListKeyHandlers keys;
    keys.activate = openRow;
    keys.details = detailsForRow;
    keys.rename = [this] { renameSelectedFile(); };
    keys.remove = [this] { deleteSelectedFiles(); };
    keys.copy = [this] {
        if (const QStringList hashes = selectedHashes(); !hashes.isEmpty())
            copyEd2kLinks(hashes);
    };
    keys.refresh = [this] { onReloadClicked(); };
    keys.find = true;
    // Space ticks or unticks every selected row while a directory is browsed
    // (MFC SharedFilesCtrl.cpp:1390-1405); Qt alone toggles only the current one.
    keys.toggle = [this] {
        if (!m_model->browseMode())
            return false;
        // The rows first, then the toggles: a toggle may refetch the list.
        QList<QPersistentModelIndex> rows;
        for (const QModelIndex& index : m_fileView->selectionModel()->selectedRows())
            rows.append(ViewNav::toSource(index).siblingAtColumn(SharedFilesModel::ColFileName));
        for (const QPersistentModelIndex& row : std::as_const(rows)) {
            if (!row.isValid())
                continue;
            const bool checked = row.data(Qt::CheckStateRole).toInt() == Qt::Checked;
            m_model->setData(row, checked ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
        }
        return true;
    };
    // Middle click opens the comments of the row under it (SharedFilesWnd.cpp:246-260)
    keys.middleClick = [this](const QModelIndex& index) {
        fetchAndShowSharedFileDetails(m_model->hashAt(ViewNav::toSource(index).row()),
                                      FileDetailDialog::Comments);
    };
    bindListKeys(m_fileView, std::move(keys));
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
    // Fresh layout: MFC's order (srchybrid/SharedFilesCtrl.cpp:264-281) — File ID
    // after Priority, Accepted Requests after Requests, Folder before Complete Sources
    if (!theUiState.hasHeaderState(QStringLiteral("sharedfiles"))) {
        const auto place = [header](int column, int after) {
            header->moveSection(header->visualIndex(column), header->visualIndex(after) + 1);
        };
        place(SharedFilesModel::ColFileId, SharedFilesModel::ColPriority);
        place(SharedFilesModel::ColAccepted, SharedFilesModel::ColRequests);
        header->moveSection(header->visualIndex(SharedFilesModel::ColFolder),
                            header->visualIndex(SharedFilesModel::ColCompleteSources));
    }
    // File Name, Size, Type, Priority, Requests, Transferred, Shared Parts, Complete
    // Sources, Shared eD2K|Kad, Folder, File ID, Accepted Requests, Artist, Album,
    // Title, Length, Bitrate, Codec. Hidden by default as in MFC.
    fileView->bindColumns(QStringLiteral("sharedfiles"),
        {260, 65, 60, 60, 100, 120, 170, 60, 100, 260, 220, 100, 100, 100, 100, 50, 65, 50},
        {SharedFilesModel::ColFolder, SharedFilesModel::ColFileId, SharedFilesModel::ColAccepted,
         SharedFilesModel::ColArtist, SharedFilesModel::ColAlbum, SharedFilesModel::ColTitle,
         SharedFilesModel::ColLength, SharedFilesModel::ColBitrate, SharedFilesModel::ColCodec});

    // MFC SharedFilesCtrl.cpp:1093-1134: the numeric columns start descending, and four
    // of them sort by a second value after both directions of the first — all-time
    // figures (the default) or this session's, published on Kad or on eD2K.
    fileView->setDescendingFirst({SharedFilesModel::ColPriority, SharedFilesModel::ColRequests,
                                  SharedFilesModel::ColAccepted, SharedFilesModel::ColTransferred,
                                  SharedFilesModel::ColCompleteSources,
                                  SharedFilesModel::ColSharedNetworks});
    for (const int column : {SharedFilesModel::ColRequests, SharedFilesModel::ColAccepted,
                             SharedFilesModel::ColTransferred}) {
        fileView->setSortValueColumn(column, [this, column](bool allTime) {
            m_proxy->setAltSort(column, !allTime);
        }, /*secondByDefault*/ true);
    }
    fileView->setSortValueColumn(SharedFilesModel::ColSharedNetworks, [this](bool kad) {
        m_proxy->setAltSort(SharedFilesModel::ColSharedNetworks, kad);
    });

    // The cell text of one column; Shared parts and Shared eD2K|Kad have none
    // (MFC SharedFilesWnd.cpp:95-98)
    m_filterEdit->setIgnoredColumns({SharedFilesModel::ColSharedParts,
                                     SharedFilesModel::ColSharedNetworks});
    m_filterEdit->setHeader(header);
    connect(m_filterEdit, &FilterEdit::filterChanged, m_proxy, &SharedFilesSortProxy::setTextFilter);

    m_fileView->setItemDelegateForColumn(SharedFilesModel::ColSharedParts,
                                          new SharedPartsDelegate(m_fileView));
    // Type icon + rating/container marks at full size, not squeezed into 16 px
    m_fileView->setItemDelegateForColumn(SharedFilesModel::ColFileName,
                                         new FlagDecorationDelegate(m_fileView));

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
    m_statSessionTransferred = new QLabel(formatByteSize(0));
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
    m_statTotalTransferred = new QLabel(formatByteSize(0));
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

namespace {

/// One row as the daemon sends it, in the list reply and in a push.
SharedFileRow sharedRowFromCbor(const QCborMap& m)
{
    SharedFileRow row;
    row.hash              = m.value(QStringLiteral("hash")).toString();
    row.fileName          = m.value(QStringLiteral("fileName")).toString();
    row.fileSize          = m.value(QStringLiteral("fileSize")).toInteger();
    row.fileType          = m.value(QStringLiteral("fileType")).toString();
    row.hasComment        = m.value(QStringLiteral("hasComment")).toBool();
    row.ownComment        = m.value(QStringLiteral("ownComment")).toBool();
    row.userRating        = static_cast<int>(m.value(QStringLiteral("userRating")).toInteger());
    row.containerSuspect  = m.value(QStringLiteral("containerSuspect")).toBool();
    row.containerExpected = m.value(QStringLiteral("containerExpected")).toString();
    row.containerActual   = m.value(QStringLiteral("containerActual")).toString();
    row.upPriority        = static_cast<int>(m.value(QStringLiteral("upPriority")).toInteger());
    row.isAutoUpPriority  = m.value(QStringLiteral("isAutoUpPriority")).toBool();
    row.requests          = m.value(QStringLiteral("requests")).toInteger();
    row.acceptedUploads   = m.value(QStringLiteral("acceptedUploads")).toInteger();
    row.transferred       = m.value(QStringLiteral("transferred")).toInteger();
    row.allTimeRequests   = m.value(QStringLiteral("allTimeRequests")).toInteger();
    row.allTimeAccepted   = m.value(QStringLiteral("allTimeAccepted")).toInteger();
    row.allTimeTransferred = m.value(QStringLiteral("allTimeTransferred")).toInteger();
    row.completeSources   = static_cast<int>(m.value(QStringLiteral("completeSources")).toInteger());
    row.completeSourcesLo = static_cast<int>(m.value(QStringLiteral("completeSourcesLo")).toInteger());
    row.completeSourcesHi = static_cast<int>(m.value(QStringLiteral("completeSourcesHi")).toInteger());
    row.artist            = m.value(QStringLiteral("artist")).toString();
    row.album             = m.value(QStringLiteral("album")).toString();
    row.title             = m.value(QStringLiteral("title")).toString();
    row.length            = m.value(QStringLiteral("length")).toInteger();
    row.bitrate           = m.value(QStringLiteral("bitrate")).toInteger();
    row.codec             = m.value(QStringLiteral("codec")).toString();
    row.publishedED2K     = m.value(QStringLiteral("publishedED2K")).toBool();
    row.kadPublished      = m.value(QStringLiteral("kadPublished")).toBool();
    row.path              = m.value(QStringLiteral("path")).toString();
    row.filePath          = m.value(QStringLiteral("filePath")).toString();
    row.shareToggleable   = m.value(QStringLiteral("canUnshare")).toBool();
    row.ed2kLink          = m.value(QStringLiteral("ed2kLink")).toString();
    row.isPartFile        = m.value(QStringLiteral("isPartFile")).toBool();
    row.category          = static_cast<int>(m.value(QStringLiteral("category")).toInteger());
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

    row.hasPartHashes  = m.value(QStringLiteral("hasPartHashes")).toBool();
    row.uploadDataRate = m.value(QStringLiteral("uploadDataRate")).toInteger();
    return row;
}

std::vector<SharedFileRow> sharedRowsFromCbor(const QCborArray& arr)
{
    std::vector<SharedFileRow> rows;
    rows.reserve(static_cast<size_t>(arr.size()));
    for (const auto& val : arr)
        rows.push_back(sharedRowFromCbor(val.toMap()));
    return rows;
}

} // namespace

void SharedFilesPanel::requestSharedFiles()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    // The poller and every "refresh after an action" path lands here, so browse mode
    // has to be honoured from inside rather than at each call site.
    if (!m_browseDir.isEmpty()) {
        requestBrowseDirectory(m_browseDir);
        return;
    }

    // A newer request overtakes one still collecting its pages.
    const int fetchId = ++m_listFetchId;
    fetchSharedFileRows(m_ipc, this, [this, fetchId](bool ok, const QCborArray& arr) {
        if (fetchId != m_listFetchId)
            return;
        if (!ok) {
            // a dropped connection clears the list in its own handler; a refused
            // request leaves what is shown
            return;
        }
        // The reply raced a click on a folder to browse.
        if (!m_browseDir.isEmpty())
            return;

        const ViewSelection selection = saveSelection();

        // Updated in place: the view keeps selection and scroll by itself. Only a
        // fallback to a reset (the first fill, or leaving a browsed directory) drops them.
        if (m_model->setFiles(sharedRowsFromCbor(arr)))
            restoreSelection(selection);
        m_haveSnapshot = true;
        afterSharedRowsChanged();
    });
}

void SharedFilesPanel::onSharedFilesPushed(const IpcMessage& msg)
{
    // A browsed directory lists files, shared or not: ask for it again instead.
    if (!m_browseDir.isEmpty()) {
        m_poller->nudge();
        return;
    }
    // Before the first full list there is nothing to update; it will have these rows.
    if (!m_haveSnapshot)
        return;
    m_model->upsertFiles(sharedRowsFromCbor(msg.fieldArray(0)));
    afterSharedRowsChanged();
}

void SharedFilesPanel::onSharedFileRemovedPush(const IpcMessage& msg)
{
    if (!m_browseDir.isEmpty()) {
        m_poller->nudge();
        return;
    }
    if (m_haveSnapshot && m_model->removeFile(msg.fieldString(0)))
        afterSharedRowsChanged();
}

void SharedFilesPanel::afterSharedRowsChanged()
{
    // The totals the percentage bars are measured against, over every listed file.
    m_totalRequests = m_totalAccepted = m_totalTransferred = 0;
    m_totalAllTimeRequests = m_totalAllTimeAccepted = m_totalAllTimeTransferred = 0;
    for (int i = 0; i < m_model->fileCount(); ++i) {
        const SharedFileRow* f = m_model->fileAt(i);
        m_totalRequests           += f->requests;
        m_totalAccepted           += f->acceptedUploads;
        m_totalTransferred        += f->transferred;
        m_totalAllTimeRequests    += f->allTimeRequests;
        m_totalAllTimeAccepted    += f->allTimeAccepted;
        m_totalAllTimeTransferred += f->allTimeTransferred;
    }

    m_headerLabel->setText(tr("Shared Files (%1)").arg(m_model->fileCount()));
    updateStatsTab();
    updateContentTab();
    updateEd2kTab();
}

void SharedFilesPanel::requestBrowseDirectory(const QString& dirPath)
{
    if (!m_ipc || !m_ipc->isConnected() || dirPath.isEmpty())
        return;

    IpcMessage req(IpcMsgType::BrowseDirectory);
    req.append(dirPath);
    m_ipc->sendRequest(std::move(req), [this, dirPath](const IpcMessage& resp) {
        // The user may have clicked another folder while this was in flight.
        if (m_browseDir != dirPath)
            return;

        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            m_model->clear();
            m_headerLabel->setText(tr("Shared Files (0)"));
            return;
        }

        const ViewSelection selection = saveSelection();
        const QCborArray arr = resp.fieldArray(1);

        std::vector<SharedFileRow> rows;
        rows.reserve(static_cast<size_t>(arr.size()));
        int sharedCount = 0;

        for (const auto& val : arr) {
            const QCborMap m = val.toMap();
            SharedFileRow row;
            row.fileName        = m.value(QStringLiteral("name")).toString();
            row.filePath        = m.value(QStringLiteral("path")).toString();
            row.fileSize        = m.value(QStringLiteral("size")).toInteger();
            row.hash            = m.value(QStringLiteral("hash")).toString();
            row.shareChecked    = m.value(QStringLiteral("shared")).toBool();
            row.shareToggleable = m.value(QStringLiteral("canToggle")).toBool();
            row.containerSuspect  = m.value(QStringLiteral("containerSuspect")).toBool();
            row.containerExpected = m.value(QStringLiteral("containerExpected")).toString();
            row.containerActual   = m.value(QStringLiteral("containerActual")).toString();
            row.path            = dirPath;
            if (row.shareChecked)
                ++sharedCount;

            // A browsed row carries only what the directory listing knows. Anything the
            // share tracks — priority, request counts, part map — stays at its default
            // until the file is actually shared and GetSharedFiles fills it in.
            rows.push_back(std::move(row));
        }

        m_haveSnapshot = false;   // the list now holds a directory, not the share
        m_model->resetFiles(std::move(rows));
        m_headerLabel->setText(tr("%1 (%2 of %3 shared)")
                                   .arg(QDir(dirPath).dirName().isEmpty() ? dirPath
                                                                          : QDir(dirPath).dirName())
                                   .arg(sharedCount)
                                   .arg(m_model->fileCount()));
        restoreSelection(selection);
        updateStatsTab();
        updateContentTab();
        updateEd2kTab();
    });
}

void SharedFilesPanel::sendSetFileShared(const QString& filePath, bool shared)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::SetFileShared);
    req.append(filePath);
    req.append(shared);
    m_ipc->sendRequest(std::move(req), [this, shared](const IpcMessage& resp) {
        if (!resp.isValid())
            return;   // connection dropped: the refetch below could not run either
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            StatusBarNotifier::post(shared ? tr("Could not share that file")
                                           : tr("Could not unshare that file"));
        }
        // Refetch either way: on success to pick the new state up, on failure to put
        // the checkbox back where it was.
        requestSharedFiles();
        syncSharedDirState();   // a single shared file bolds its folder
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
        m_statSessionTransferred->setText(formatByteSize(0));
        m_statTotalRequests->setText(QStringLiteral("0"));
        m_statTotalAccepted->setText(QStringLiteral("0"));
        m_statTotalTransferred->setText(formatByteSize(0));
        m_statPopularity->setText(QStringLiteral("-"));
        m_statPopularity2->setText(QStringLiteral("-"));
        m_statOnQueue->setText(QStringLiteral("0"));
        m_statUploading->setText(formatByteRate(0));
        clearBars();
        return;
    }

    m_statSessionRequests->setText(QString::number(f->requests));
    m_statSessionAccepted->setText(QString::number(f->acceptedUploads));
    m_statSessionTransferred->setText(formatByteSize(f->transferred));
    m_statTotalRequests->setText(QString::number(f->allTimeRequests));
    m_statTotalAccepted->setText(QString::number(f->allTimeAccepted));
    m_statTotalTransferred->setText(formatByteSize(f->allTimeTransferred));

    // Popularity rank: file's position among all shared files sorted by request count
    const int sessionRank = computePopularityRank(f->requests, &SharedFileRow::requests);
    const int totalRank = computePopularityRank(f->allTimeRequests, &SharedFileRow::allTimeRequests);
    m_statPopularity->setText(sessionRank > 0 ? QString::number(sessionRank) : QStringLiteral("-"));
    m_statPopularity2->setText(totalRank > 0 ? QString::number(totalRank) : QStringLiteral("-"));

    m_statOnQueue->setText(QString::number(f->queuedClients));
    m_statUploading->setText(formatByteRate(f->uploadDataRate));

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
        return f->hasPartHashes;
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
        m_archivePreview->setAutoScan(thePrefs.autoArchivePreviewStart());   // else: its Update button
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

void SharedFilesPanel::onReloadClicked(bool rebuildMetaData)
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage msg(IpcMsgType::ReloadSharedFiles);
    if (rebuildMetaData)
        msg.append(true);
    m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
        requestSharedFiles();
        syncSharedDirState();
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

ViewSelection SharedFilesPanel::saveSelection() const
{
    return captureViewSelection(m_fileView, [this](int row) { return hashAtViewRow(row); });
}

void SharedFilesPanel::restoreSelection(const ViewSelection& state)
{
    m_restoringSelection = true;
    restoreViewSelection(m_fileView, state, [this](int row) { return hashAtViewRow(row); });
    m_restoringSelection = false;
}

QString SharedFilesPanel::hashAtViewRow(int viewRow) const
{
    return m_model->hashAt(m_proxy->mapToSource(m_proxy->index(viewRow, 0)).row());
}

// ---------------------------------------------------------------------------
// Folder tree context menu
// ---------------------------------------------------------------------------

void SharedFilesPanel::onFolderContextMenu(const QPoint& pos)
{
    auto* item = m_folderTree->itemAt(pos);
    if (!item)
        return;

    // Every node that stands for a set of shared files gets the file menu
    // (MFC m_SharedFilesMenu); only unshared filesystem folders get the share one.
    if (item == m_allSharedItem || item == m_incomingItem || item == m_incompleteItem
        || item == m_sharedDirsItem || item->data(0, kRoleSharedDirItem).toBool()
        || item->data(0, kRoleCategoryNode).toInt() != 0) {
        m_folderTree->setCurrentItem(item);   // the list shows what the menu acts on
        showSharedDirMenu(item, m_folderTree->viewport()->mapToGlobal(pos));
        return;
    }

    if (!item->data(0, kRoleFsItem).toBool())
        return;

    const QString path = item->data(0, kRolePath).toString();
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

    // Enablement as MFC (srchybrid/SharedDirsTreeCtrl.cpp:525-528)
    const auto dirs = thePrefs.sharedDirs();
    const bool isShared = SharedDirState::isSharedDir(dirs, path);
    const bool hasSharedSub = SharedDirState::hasSharedSubdir(dirs, path);
    const bool shareable = thePrefs.isShareableDirectory(path);

    // Share Directory
    auto* shareAct = menu.addAction(tr("Share Directory"), this, [this, path]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        auto dirs = thePrefs.sharedDirs();
        if (!SharedDirState::isSharedDir(dirs, path))
            dirs.append(path);
        sendShareDirsUpdate(dirs);
    });
    shareAct->setEnabled(!isShared && shareable);

    // Share with Subdirectories
    auto* shareSubAct = menu.addAction(tr("Share with Subdirectories"), this, [this, path]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        auto dirs = thePrefs.sharedDirs();
        collectSubdirectories(path, dirs);
        sendShareDirsUpdate(dirs);
    });
    shareSubAct->setEnabled(shareable && hasSubdirectories(path));

    menu.addSeparator();

    // Unshare Directory
    auto* unshareAct = menu.addAction(tr("Unshare Directory"), this, [this, path]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        sendShareDirsUpdate(SharedDirState::withoutDir(thePrefs.sharedDirs(), path, false));
    });
    unshareAct->setEnabled(isShared);

    // Unshare with Subdirectories — off the list, so a shared folder that is gone
    // from disk goes too
    auto* unshareSubAct = menu.addAction(tr("Unshare with Subdirectories"), this, [this, path]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        sendShareDirsUpdate(SharedDirState::withoutDir(thePrefs.sharedDirs(), path, true));
    });
    unshareSubAct->setEnabled(isShared || hasSharedSub);

    menu.exec(m_folderTree->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// Shared file details (IPC fetch + dialog)
// ---------------------------------------------------------------------------

void SharedFilesPanel::fetchAndShowSharedFileDetails(const QStringList& hashes, int tab)
{
    if (hashes.size() == 1) {
        fetchAndShowSharedFileDetails(hashes.constFirst(), tab);
        return;
    }
    if (!m_ipc || !m_ipc->isConnected() || hashes.isEmpty())
        return;

    struct Pending {
        QList<QCborMap> files;
        qsizetype outstanding = 0;
    };
    auto pending = std::make_shared<Pending>();
    pending->outstanding = hashes.size();
    for (const QString& hash : hashes) {
        IpcMessage msg(IpcMsgType::GetSharedFileDetails);
        msg.append(hash);
        m_ipc->sendRequest(std::move(msg), [this, tab, pending](const IpcMessage& resp) {
            if (resp.isValid() && resp.fieldBool(0))
                pending->files.append(resp.field(1).toMap());
            if (--pending->outstanding > 0 || pending->files.isEmpty())
                return;
            auto* dlg = new FileDetailDialog(pending->files,
                                             static_cast<FileDetailDialog::Tab>(tab), this);
            connectCommentFilter(dlg, m_ipc);
            dlg->show();
        });
    }
}

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
        connectCommentPosting(dlg, m_ipc);
        dlg->setWalker(makeSharedFileWalker(hash));
        connectDetailNavigation(dlg, m_ipc, IpcMsgType::GetSharedFileDetails);
        dlg->show();
    });
}

void SharedFilesPanel::openSharedFile(const QString& hash)
{
    const SharedFileRow* file = m_model->findByHash(hash);
    if (!file)
        return;

    // A collection opens in the collection viewer rather than in whatever the system
    // has registered for .emulecollection — MFC branches the same way inside OpenFile()
    // (srchybrid/SharedFilesCtrl.cpp:1260-1265).
    if (file->isCollection) {
        showCollection(hash);
        return;
    }

    // A part file has no playable data at a path the shell could take, so the original
    // simply ignores it here (:1281). Preview is the feature for those.
    if (file->isPartFile)
        return;

    // Local core: the daemon's host is this host, so the recorded path is ours to open.
    if (m_ipc && m_ipc->isLocalConnection()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(file->filePath));
        return;
    }

    // Remote core: the bytes are on the daemon's machine and only reachable over its web
    // server, the same channel Preview uses.
    const QString url = daemonStreamUrl(m_ipc, hash, m_streamToken);
    if (url.isEmpty()) {
        logWarning(tr("Open File not available — web server is not running or stream token not received."));
        return;
    }

    // Media streams into the configured player; anything else goes to the system
    // default handler, which for an http URL means the browser downloads it.
    const ED2KFileType fileType = getED2KFileTypeID(file->fileName);
    if (fileType == ED2KFileType::Video || fileType == ED2KFileType::Audio)
        launchPreview(url);
    else
        QDesktopServices::openUrl(QUrl(url));
}

void SharedFilesPanel::showCollection(const QString& hash)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;

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
    // Anchored on the hash, not a row: rows move when the list changes, and a reset
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
    ++m_dirStateFetchId;   // a reply still on its way describes the old list
    refreshSharedDirs();
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage&) {
        requestSharedFiles();
        syncSharedDirState();   // what the daemon kept, in case it differs
    });
}

void SharedFilesPanel::collectSubdirectories(const QString& root, QStringList& list)
{
    if (!SharedDirState::isSharedDir(list, root) && thePrefs.isShareableDirectory(root))
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
    applyShareState(item);

    if (hasSubdirectories(path))
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
}

// ---------------------------------------------------------------------------
// Share marks in the folder tree
// ---------------------------------------------------------------------------

void SharedFilesPanel::refreshSharedDirs()
{
    // Only what is loaded: an unexpanded folder gets its marks when it is filled.
    QList<QTreeWidgetItem*> pending{m_allDirsItem};
    while (!pending.isEmpty()) {
        QTreeWidgetItem* item = pending.takeLast();
        for (int i = 0; i < item->childCount(); ++i) {
            applyShareState(item->child(i));
            pending.append(item->child(i));
        }
    }
    rebuildSharedDirsNode();
}

void SharedFilesPanel::syncSharedDirState()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    const int fetchId = ++m_dirStateFetchId;
    m_ipc->sendRequest(IpcMessage(IpcMsgType::GetSharedDirState),
                       [this, fetchId](const IpcMessage& resp) {
        // Dropped connection, or overtaken by a newer request or a local change
        if (fetchId != m_dirStateFetchId || !resp.isValid()
            || resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;

        const QCborMap state = resp.fieldMap(1);
        QStringList shared;
        for (const auto& v : state.value(QStringLiteral("sharedDirs")).toArray())
            shared.append(v.toString());
        QStringList single;
        for (const auto& v : state.value(QStringLiteral("singleSharedDirs")).toArray())
            single.append(v.toString());

        if (shared == thePrefs.sharedDirs() && single == m_singleSharedDirs)
            return;
        thePrefs.setSharedDirs(shared);
        m_singleSharedDirs = single;
        refreshSharedDirs();
    });
}

void SharedFilesPanel::applyShareState(QTreeWidgetItem* item)
{
    const QString path = item->data(0, kRolePath).toString();
    if (path.isEmpty())
        return;

    // MFC: an overlay on the shared folder itself, bold on it and on every folder
    // above something shared (srchybrid/SharedDirsTreeCtrl.cpp:727, :743).
    const auto dirs = thePrefs.sharedDirs();
    const bool shared = SharedDirState::isSharedDir(dirs, path);
    item->setIcon(0, folderIcon(shared ? QStringLiteral(":/icons/SharedFolderOvl.ico")
                                       : QString()));
    setItemBold(item, shared || SharedDirState::hasSharedSubdir(dirs, path)
                          || SharedDirState::hasDirAtOrBelow(m_singleSharedDirs, path));
}

void SharedFilesPanel::rebuildSharedDirsNode()
{
    QStringList dirs = thePrefs.sharedDirs();
    dirs.removeAll(QString());
    std::ranges::sort(dirs, [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    // Nothing to do on the periodic sync unless the list moved
    QStringList shown;
    QList<QTreeWidgetItem*> pending{m_sharedDirsItem};
    while (!pending.isEmpty()) {
        QTreeWidgetItem* item = pending.takeLast();
        for (int i = 0; i < item->childCount(); ++i) {
            shown.append(item->child(i)->data(0, kRolePath).toString());
            pending.append(item->child(i));
        }
    }
    std::ranges::sort(shown, [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    if (shown == dirs)
        return;

    // The selection survives by path; a folder that left the share falls back to
    // the node itself.
    QTreeWidgetItem* current = m_folderTree->currentItem();
    const bool hadSelection = current && current->data(0, kRoleSharedDirItem).toBool();
    const QString selectedKey =
        hadSelection ? SharedDirState::dirKey(current->data(0, kRolePath).toString())
                     : QString();
    QSet<QString> expandedKeys;
    pending = {m_sharedDirsItem};
    while (!pending.isEmpty()) {
        QTreeWidgetItem* item = pending.takeLast();
        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem* child = item->child(i);
            if (child->isExpanded())
                expandedKeys.insert(SharedDirState::dirKey(child->data(0, kRolePath).toString()));
            pending.append(child);
        }
    }
    const bool wasEmpty = m_sharedDirsItem->childCount() == 0;

    // Silent: deleting the current item would otherwise re-filter the list mid-rebuild
    const bool wasBlocked = m_folderTree->blockSignals(true);
    qDeleteAll(m_sharedDirsItem->takeChildren());

    // Nested under the nearest shared folder above, as MFC's FilterTreeAddSubDirectories
    // (:277). Sorted, so a parent is always created before what goes under it.
    const bool local = m_ipc && m_ipc->isLocalConnection();
    const auto parents = SharedDirState::nearestSharedParents(dirs);
    QHash<QString, QTreeWidgetItem*> items;
    QSet<QString> seenKeys;
    QTreeWidgetItem* reselect = nullptr;
    for (const QString& dir : dirs) {
        const QString key = SharedDirState::dirKey(dir);
        if (seenKeys.contains(key))
            continue;   // the same folder listed twice, e.g. with a trailing separator
        seenKeys.insert(key);
        const QString parentDir = parents.value(dir);
        QTreeWidgetItem* parentItem = items.value(parentDir, m_sharedDirsItem);

        // MFC GetFolderLabel: a top-level entry says where it lives
        const QString clean = QDir::cleanPath(dir);
        const QFileInfo info(clean);
        QString label = info.fileName().isEmpty() ? QDir::toNativeSeparators(clean)
                                                  : info.fileName();
        if (parentItem == m_sharedDirsItem && !info.fileName().isEmpty())
            label += QStringLiteral("  (%1)").arg(QDir::toNativeSeparators(info.path()));

        auto* item = new QTreeWidgetItem(parentItem, {label});
        item->setData(0, Qt::UserRole, static_cast<int>(SharedFilterType::SpecificDir));
        item->setData(0, kRolePath, dir);
        item->setData(0, kRoleSharedDirItem, true);
        item->setToolTip(0, QDir::toNativeSeparators(clean));
        // Only this host can tell whether the daemon's folder is there
        const bool missing = local && !QDir(dir).exists();
        item->setIcon(0, folderIcon(missing ? QStringLiteral(":/icons/NoAccessFolderOvl.ico")
                                            : QString()));
        items.insert(dir, item);

        if (hadSelection && key == selectedKey)
            reselect = item;
    }
    for (QTreeWidgetItem* item : std::as_const(items))
        if (expandedKeys.contains(SharedDirState::dirKey(item->data(0, kRolePath).toString())))
            item->setExpanded(true);
    if (wasEmpty && m_sharedDirsItem->childCount() > 0)
        m_sharedDirsItem->setExpanded(true);

    if (hadSelection)
        m_folderTree->setCurrentItem(reselect ? reselect : m_sharedDirsItem);
    m_folderTree->blockSignals(wasBlocked);
    // The list was showing a folder that is no longer shared
    if (hadSelection && !reselect)
        onFolderSelectionChanged();
}

QStringList SharedFilesPanel::listedHashes() const
{
    QStringList hashes;
    const QAbstractItemModel* shown = m_fileView->model();
    for (int row = 0; row < shown->rowCount(); ++row) {
        const QString hash = m_model->hashAt(ViewNav::toSource(shown->index(row, 0)).row());
        if (!hash.isEmpty())
            hashes << hash;
    }
    return hashes;
}

void SharedFilesPanel::showSharedDirMenu(QTreeWidgetItem* item, const QPoint& globalPos)
{
    // MFC m_SharedFilesMenu (SharedDirsTreeCtrl.cpp:401-435, enabled at :456-521). Its
    // file commands act on every file the list shows for the node, not on a selection.
    const bool isDirNode = item->data(0, kRoleSharedDirItem).toBool();
    const bool isRoot = (item == m_sharedDirsItem);
    // A category's part files are a set narrow enough for Delete and Comment
    // (MFC bWideRangeSelection: m_nCatFilter == -1 && m_eItemType != SDI_NO).
    const int categoryNode = item->data(0, kRoleCategoryNode).toInt();
    const bool wideNode = !isDirNode && categoryNode != 2;
    QString path;
    if (isDirNode || categoryNode == 1)
        path = item->data(0, kRolePath).toString();
    else if (categoryNode == 2)
        path = thePrefs.tempDirs().value(0);
    else if (item == m_incomingItem)
        path = thePrefs.incomingDir();
    else if (item == m_incompleteItem)
        path = thePrefs.tempDirs().value(0);
    const bool connected = m_ipc && m_ipc->isConnected();

    const QStringList hashes = listedHashes();
    QList<const SharedFileRow*> files;
    for (const QString& hash : hashes) {
        if (const SharedFileRow* f = m_model->findByHash(hash))
            files << f;
    }
    const bool allComplete = std::ranges::none_of(files, &SharedFileRow::isPartFile);
    const auto state = SharedDirState::fileSetMenuState(static_cast<int>(files.size()), allComplete,
                                                        wideNode, !path.isEmpty());

    QMenu menu(this);

    auto* openAct = menu.addAction(QIcon(QStringLiteral(":/icons/FolderOpen.ico")),
                                   tr("Open Folder"), this, [path]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    openAct->setEnabled(state.openFolder && m_ipc && m_ipc->isLocalConnection());
    menu.addAction(menuIcon("Delete.ico"), tr("Delete From Disk"), this, [this, hashes] {
        sendDeleteFilesBatch(hashes);
    })->setEnabled(connected && state.remove);

    menu.addSeparator();
    {
        auto* prioMenu = menu.addMenu(menuIcon("FilePriority.ico"), tr("Priority (Upload)"));
        prioMenu->setEnabled(connected && state.priority);
        // A mixed set gets no check mark (MFC clears uPrioMenuItem)
        const auto addPrio = [&](const QString& text, int prio, bool isAuto) {
            auto* act = prioMenu->addAction(text, this, [this, hashes, prio, isAuto] {
                sendSetPriorityBatch(hashes, prio, isAuto);
            });
            const bool allMatch = !files.isEmpty()
                && std::ranges::all_of(files, [prio, isAuto](const SharedFileRow* f) {
                       return isAuto ? f->isAutoUpPriority
                                     : !f->isAutoUpPriority && f->upPriority == prio;
                   });
            if (allMatch) {
                act->setCheckable(true);
                act->setChecked(true);
            }
        };
        addPrio(tr("Very Low"), PrVeryLow, false);
        addPrio(tr("Low"), PrLow, false);
        addPrio(tr("Normal"), PrNormal, false);
        addPrio(tr("High"), PrHigh, false);
        addPrio(tr("Release"), PrVeryHigh, false);
        addPrio(tr("Auto"), PrNormal, true);
    }

    menu.addSeparator();
    menu.addAction(menuIcon("FileInfo.ico"), tr("Details..."), this, [this, hashes] {
        fetchAndShowSharedFileDetails(hashes, FileDetailDialog::General);
    })->setEnabled(connected && state.details);
    menu.addAction(menuIcon("FileComments.ico"), tr("Comments..."), this, [this, hashes] {
        fetchAndShowSharedFileDetails(hashes, FileDetailDialog::Comments);
    })->setEnabled(connected && state.comment);
    menu.addAction(menuIcon("eD2kLink.ico"), tr("Copy eD2K Links"), this, [this, hashes] {
        copyEd2kLinks(hashes);
    })->setEnabled(connected && state.link);

    menu.addSeparator();

    auto* unshareAct = menu.addAction(tr("Unshare Directory"), this, [this, path]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        sendShareDirsUpdate(SharedDirState::withoutDir(thePrefs.sharedDirs(), path, false));
    });
    unshareAct->setEnabled(connected && isDirNode);

    // On the node itself this unshares everything (MFC RemoveAllSharedDirectories,
    // srchybrid/SharedDirsTreeCtrl.cpp:934)
    auto* unshareSubAct = menu.addAction(tr("Unshare with Subdirectories"), this,
                                         [this, path, isRoot]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        sendShareDirsUpdate(isRoot ? QStringList()
                                   : SharedDirState::withoutDir(thePrefs.sharedDirs(), path, true));
    });
    unshareSubAct->setEnabled(connected && (isRoot ? item->childCount() > 0 : isDirNode));

    menu.exec(globalPos);
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

// ---------------------------------------------------------------------------
// File list commands — context menu and list keys
// ---------------------------------------------------------------------------

void SharedFilesPanel::renameSelectedFile()
{
    const QStringList hashes = selectedHashes();
    const SharedFileRow* f = hashes.size() == 1 ? m_model->findByHash(hashes.first()) : nullptr;
    if (!f || !m_ipc || !m_ipc->isConnected())
        return;
    if (f->isPartFile) {
        QApplication::beep();   // MFC MessageBeep: a part file is renamed on the download list
        return;
    }
    const QString hash = f->hash;
    const QString oldName = f->fileName;
    const auto newName = RenameFileDialog::ask(this, oldName);
    if (!newName)
        return;
    IpcMessage msg(IpcMsgType::RenameSharedFile);
    msg.append(hash);
    msg.append(*newName);
    m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
        requestSharedFiles();
    });
}

void SharedFilesPanel::deleteSelectedFiles()
{
    // Part files are skipped, as in MFC (SharedFilesCtrl.cpp:936) — deleting one here
    // would destroy an in-progress download. sendDeleteFilesBatch() asks first.
    QStringList complete;
    for (const QString& hash : selectedHashes()) {
        if (const SharedFileRow* f = m_model->findByHash(hash); f && !f->isPartFile)
            complete << hash;
    }
    if (!complete.isEmpty())
        sendDeleteFilesBatch(complete);
}

} // namespace eMule
