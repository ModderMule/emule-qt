#include "pch.h"
/// @file SharedFilesPanel.cpp
/// @brief Shared Files tab panel — implementation.

#include "panels/SharedFilesPanel.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/SharedFilesModel.h"
#include "controls/SharedPartsDelegate.h"
#include "dialogs/FileDetailDialog.h"
#include "prefs/Preferences.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QInputDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

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
        return QStringLiteral("%1 KiB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MiB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GiB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SharedFilesPanel::SharedFilesPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &SharedFilesPanel::onRefreshTimer);
}

SharedFilesPanel::~SharedFilesPanel() = default;

// ---------------------------------------------------------------------------
// IPC wiring
// ---------------------------------------------------------------------------

void SharedFilesPanel::setIpcClient(IpcClient* client)
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
            m_model->clear();
            m_headerLabel->setText(tr("Shared Files (0)"));
        });

        // Push events for immediate refresh
        connect(m_ipc, &IpcClient::sharedFileUpdated, this, [this](const IpcMessage&) {
            requestSharedFiles();
        });
    } else {
        m_refreshTimer->stop();
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
    updateStatsTab();
    updateEd2kTab();
}

void SharedFilesPanel::onFileContextMenu(const QPoint& pos)
{
    // Resolve item under cursor
    const SharedFileRow* file = nullptr;
    QString hash;
    const QModelIndex proxyIdx = m_fileView->indexAt(pos);
    if (proxyIdx.isValid()) {
        const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
        hash = m_model->hashAt(srcIdx.row());
        file = m_model->fileAt(srcIdx.row());
    }
    const bool hasSel = (file != nullptr && !hash.isEmpty());

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

    // Open File
    {
        auto* act = m_contextMenu->addAction(ico("FileOpen.ico"), tr("Open File"), this, [file]() {
            if (file)
                QDesktopServices::openUrl(QUrl::fromLocalFile(file->filePath));
        });
        act->setEnabled(hasSel && m_ipc && m_ipc->isLocalConnection());
    }

    // Open Folder
    {
        auto* act = m_contextMenu->addAction(ico("FolderOpen.ico"), tr("Open Folder"), this, [file]() {
            if (file)
                QDesktopServices::openUrl(QUrl::fromLocalFile(file->path));
        });
        act->setEnabled(hasSel && m_ipc && m_ipc->isLocalConnection());
    }

    m_contextMenu->addSeparator();

    // Rename
    {
        auto* act = m_contextMenu->addAction(ico("Rename.ico"), tr("Rename..."), this, [this, hash, file]() {
            if (!file || hash.isEmpty() || !m_ipc || !m_ipc->isConnected())
                return;
            bool ok = false;
            const QString newName = QInputDialog::getText(
                this, tr("Rename File"), tr("New file name:"),
                QLineEdit::Normal, file->fileName, &ok);
            if (!ok || newName.trimmed().isEmpty() || newName.trimmed() == file->fileName)
                return;
            IpcMessage msg(IpcMsgType::RenameSharedFile);
            msg.append(hash);
            msg.append(newName.trimmed());
            m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
                requestSharedFiles();
            });
        });
        act->setEnabled(hasSel);
    }

    // Delete From Disk
    {
        auto* act = m_contextMenu->addAction(ico("Delete.ico"), tr("Delete From Disk"), this, [this, hash, file]() {
            if (!file || hash.isEmpty() || !m_ipc || !m_ipc->isConnected())
                return;
            const auto answer = QMessageBox::warning(
                this, tr("Delete File"),
                tr("Are you sure you want to permanently delete \"%1\" from disk?").arg(file->fileName),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
            IpcMessage msg(IpcMsgType::DeleteSharedFile);
            msg.append(hash);
            m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
                requestSharedFiles();
            });
        });
        act->setEnabled(hasSel);
    }

    // Unshare
    {
        auto* act = m_contextMenu->addAction(ico("ListRemove.ico"), tr("Unshare"), this, [this, hash, file]() {
            if (!file || hash.isEmpty() || !m_ipc || !m_ipc->isConnected())
                return;
            const auto answer = QMessageBox::question(
                this, tr("Unshare File"),
                tr("Remove \"%1\" from the shared files list?\n\nThe file will remain on disk.").arg(file->fileName),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes)
                return;
            IpcMessage msg(IpcMsgType::UnshareFile);
            msg.append(hash);
            m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
                requestSharedFiles();
            });
        });
        act->setEnabled(hasSel);
    }

    m_contextMenu->addSeparator();

    // Priority (Upload) submenu
    {
        auto* prioMenu = m_contextMenu->addMenu(ico("FilePriority.ico"), tr("Priority (Upload)"));
        prioMenu->setEnabled(hasSel);
        if (hasSel) {
            auto addPrioAction = [&](const QString& text, int prio) {
                auto* act = prioMenu->addAction(text, this, [this, hash, prio]() {
                    sendSetPriority(hash, prio, false);
                });
                if (!file->isAutoUpPriority && file->upPriority == prio)
                    act->setCheckable(true), act->setChecked(true);
            };
            addPrioAction(tr("Very Low"),  PrVeryLow);
            addPrioAction(tr("Low"),       PrLow);
            addPrioAction(tr("Normal"),    PrNormal);
            addPrioAction(tr("High"),      PrHigh);
            addPrioAction(tr("Very High"), PrVeryHigh);
            prioMenu->addSeparator();
            auto* autoAct = prioMenu->addAction(tr("Auto"), this, [this, hash]() {
                sendSetPriority(hash, PrNormal, true);
            });
            if (file->isAutoUpPriority)
                autoAct->setCheckable(true), autoAct->setChecked(true);
        }
    }

    // Collection submenu (placeholder)
    {
        auto* collMenu = m_contextMenu->addMenu(tr("Collection"));
        collMenu->setEnabled(false); // ToDo: implement collection management
    }

    m_contextMenu->addSeparator();

    // Details...
    {
        auto* act = m_contextMenu->addAction(ico("FileInfo.ico"), tr("Details..."), this, [this, hash]() {
            fetchAndShowSharedFileDetails(hash, FileDetailDialog::General);
        });
        act->setEnabled(hasSel);
    }

    // Comments...
    {
        auto* act = m_contextMenu->addAction(ico("FileComments.ico"), tr("Comments..."), this, [this, hash]() {
            fetchAndShowSharedFileDetails(hash, FileDetailDialog::Comments);
        });
        act->setEnabled(hasSel);
    }

    // eD2K Links
    {
        auto* act = m_contextMenu->addAction(ico("eD2kLink.ico"), tr("eD2K Links..."), this, [this]() {
            copyEd2kLink();
        });
        act->setEnabled(hasSel);
    }

    // Find
    connect(m_contextMenu->addAction(ico("Search.ico"), tr("Find...")),
            &QAction::triggered, this, &SharedFilesPanel::showFindDialog);

    // Web Services submenu (placeholder)
    {
        auto* webMenu = m_contextMenu->addMenu(tr("Web Services"));
        webMenu->setEnabled(false); // ToDo: populate from webservices.dat
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
    m_folderTree = new QTreeWidget;
    m_folderTree->setHeaderLabel(tr("File Name"));
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
    connect(m_folderTree, &QTreeWidget::itemExpanded,
            this, &SharedFilesPanel::onFolderItemExpanded);

    m_horzSplitter->addWidget(m_folderTree);

    // --- File list view ---
    auto* rightWidget = new QWidget;
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);

    m_fileView = new QTreeView;
    m_fileView->setModel(m_proxy);
    m_fileView->setRootIsDecorated(false);
    m_fileView->setAlternatingRowColors(true);
    m_fileView->setSortingEnabled(true);
    m_fileView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileView->setUniformRowHeights(true);
    m_fileView->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_fileView, &QTreeView::customContextMenuRequested,
            this, &SharedFilesPanel::onFileContextMenu);
    connect(m_fileView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &SharedFilesPanel::onFileSelectionChanged);

    auto* header = m_fileView->header();
    header->setStretchLastSection(true);
    header->setDefaultSectionSize(90);
    header->resizeSection(SharedFilesModel::ColFileName, 220);
    header->resizeSection(SharedFilesModel::ColSize, 75);
    header->resizeSection(SharedFilesModel::ColType, 70);
    header->resizeSection(SharedFilesModel::ColPriority, 80);
    header->resizeSection(SharedFilesModel::ColRequests, 80);
    header->resizeSection(SharedFilesModel::ColTransferred, 120);
    header->resizeSection(SharedFilesModel::ColSharedParts, 80);
    header->resizeSection(SharedFilesModel::ColCompleteSources, 100);
    header->resizeSection(SharedFilesModel::ColSharedNetworks, 100);
    // Hide Folder column by default (like MFC)
    header->hideSection(SharedFilesModel::ColFolder);
    theUiState.bindHeaderView(header, QStringLiteral("sharedfiles"));

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

    auto makeBar = [](QProgressBar*& bar) {
        bar = new QProgressBar;
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setTextVisible(true);
        bar->setFormat(QStringLiteral("%p%"));
        bar->setFixedHeight(16);
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

    // --- Content tab (placeholder) ---
    auto* contentWidget = new QWidget;
    auto* contentLayout = new QVBoxLayout(contentWidget);
    auto* contentLabel = new QLabel(tr("General / Video / Audio metadata displayed here"));
    contentLabel->setAlignment(Qt::AlignCenter);
    contentLayout->addWidget(contentLabel);
    m_bottomTabs->addTab(contentWidget, QIcon(QStringLiteral(":/icons/FileInfo.ico")),
                         tr("Content"));

    // --- eD2K Links tab ---
    auto* ed2kWidget = new QWidget;
    auto* ed2kLayout = new QVBoxLayout(ed2kWidget);
    ed2kLayout->setContentsMargins(4, 4, 4, 4);

    auto* ed2kHeader = new QLabel(tr("eD2K Links"));
    ed2kHeader->setFont(boldFont);
    ed2kLayout->addWidget(ed2kHeader);

    m_ed2kText = new QTextEdit;
    m_ed2kText->setReadOnly(true);
    m_ed2kText->setLineWrapMode(QTextEdit::NoWrap);
    ed2kLayout->addWidget(m_ed2kText, 1);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    m_copyButton = new QPushButton(tr("Copy"));
    connect(m_copyButton, &QPushButton::clicked, this, &SharedFilesPanel::copyEd2kLink);
    buttonRow->addWidget(m_copyButton);
    ed2kLayout->addLayout(buttonRow);

    m_bottomTabs->addTab(ed2kWidget, QIcon(QStringLiteral(":/icons/eD2kLink.ico")),
                         tr("eD2K Links"));

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

        const QString selectedHash = saveSelection();

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

            // Capture incoming directory from first non-partfile
            if (m_incomingDir.isEmpty() && !row.isPartFile)
                m_incomingDir = row.path;

            rows.push_back(std::move(row));
        }

        const int sfScroll = m_fileView->verticalScrollBar()->value();
        m_proxy->setIncomingDir(m_incomingDir);
        m_model->setFiles(std::move(rows));
        m_headerLabel->setText(tr("Shared Files (%1)").arg(m_model->fileCount()));
        restoreSelection(selectedHash);
        m_fileView->verticalScrollBar()->setValue(sfScroll);
        updateStatsTab();
        updateEd2kTab();
    });
}

void SharedFilesPanel::sendSetPriority(const QString& hash, int priority, bool isAuto)
{
    if (!m_ipc || !m_ipc->isConnected() || hash.isEmpty())
        return;

    IpcMessage msg(IpcMsgType::SetSharedFilePriority);
    msg.append(hash);
    msg.append(static_cast<int64_t>(priority));
    msg.append(isAuto);
    m_ipc->sendRequest(std::move(msg), [this](const IpcMessage&) {
        requestSharedFiles();
    });
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

    const QModelIndex proxyIdx = m_fileView->selectionModel()->currentIndex();
    if (!proxyIdx.isValid()) {
        m_statSessionRequests->setText(QStringLiteral("0"));
        m_statSessionAccepted->setText(QStringLiteral("0"));
        m_statSessionTransferred->setText(QStringLiteral("0 B"));
        m_statTotalRequests->setText(QStringLiteral("0"));
        m_statTotalAccepted->setText(QStringLiteral("0"));
        m_statTotalTransferred->setText(QStringLiteral("0 B"));
        m_statPopularity->setText(QStringLiteral("-"));
        m_statOnQueue->setText(QStringLiteral("0"));
        m_statUploading->setText(QStringLiteral("0"));
        clearBars();
        return;
    }

    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
    const auto* f = m_model->fileAt(srcIdx.row());
    if (!f)
        return;

    m_statSessionRequests->setText(QString::number(f->requests));
    m_statSessionAccepted->setText(QString::number(f->acceptedUploads));
    m_statSessionTransferred->setText(formatSize(f->transferred));
    m_statTotalRequests->setText(QString::number(f->allTimeRequests));
    m_statTotalAccepted->setText(QString::number(f->allTimeAccepted));
    m_statTotalTransferred->setText(formatSize(f->allTimeTransferred));
    m_statPopularity->setText(f->allTimeRequests > 0 ? QString::number(f->allTimeRequests) : QStringLiteral("-"));
    m_statOnQueue->setText(QStringLiteral("0")); // ToDo: populate when queue data available
    m_statUploading->setText(QString::number(f->uploadingClients));

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
    const QModelIndex proxyIdx = m_fileView->selectionModel()->currentIndex();
    if (!proxyIdx.isValid()) {
        m_ed2kText->clear();
        return;
    }

    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
    const auto* f = m_model->fileAt(srcIdx.row());
    if (!f) {
        m_ed2kText->clear();
        return;
    }

    m_ed2kText->setPlainText(f->ed2kLink);
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
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Search"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QFormLayout(dlg);

    auto* searchEdit = new QLineEdit(dlg);
    layout->addRow(tr("Search for:"), searchEdit);

    auto* columnCombo = new QComboBox(dlg);
    columnCombo->addItem(tr("File Name"),        SharedFilesModel::ColFileName);
    columnCombo->addItem(tr("Size"),             SharedFilesModel::ColSize);
    columnCombo->addItem(tr("Type"),             SharedFilesModel::ColType);
    columnCombo->addItem(tr("Priority"),         SharedFilesModel::ColPriority);
    columnCombo->addItem(tr("Requests"),         SharedFilesModel::ColRequests);
    columnCombo->addItem(tr("Transferred Data"), SharedFilesModel::ColTransferred);
    columnCombo->addItem(tr("Shared parts"),     SharedFilesModel::ColSharedParts);
    columnCombo->addItem(tr("Complete Sources"), SharedFilesModel::ColCompleteSources);
    columnCombo->addItem(tr("Shared eD2K/Kad"), SharedFilesModel::ColSharedNetworks);
    columnCombo->addItem(tr("Folder"),           SharedFilesModel::ColFolder);
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

        for (int row = 0; row < m_proxy->rowCount(); ++row) {
            const QModelIndex idx = m_proxy->index(row, column);
            if (idx.data(Qt::DisplayRole).toString().contains(term, Qt::CaseInsensitive)) {
                m_fileView->setCurrentIndex(idx);
                m_fileView->scrollTo(idx);
                return;
            }
        }
    });

    dlg->exec();
}

// ---------------------------------------------------------------------------
// Clipboard / eD2K link
// ---------------------------------------------------------------------------

void SharedFilesPanel::copyEd2kLink()
{
    const QModelIndex proxyIdx = m_fileView->selectionModel()->currentIndex();
    if (!proxyIdx.isValid())
        return;

    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
    const auto* f = m_model->fileAt(srcIdx.row());
    if (f && !f->ed2kLink.isEmpty())
        QApplication::clipboard()->setText(f->ed2kLink);
}

// ---------------------------------------------------------------------------
// Selection save/restore
// ---------------------------------------------------------------------------

QString SharedFilesPanel::saveSelection() const
{
    const auto sel = m_fileView->selectionModel()->currentIndex();
    if (!sel.isValid())
        return {};

    const QModelIndex srcIdx = m_proxy->mapToSource(sel);
    return m_model->hashAt(srcIdx.row());
}

void SharedFilesPanel::restoreSelection(const QString& key)
{
    if (key.isEmpty())
        return;

    for (int row = 0; row < m_model->fileCount(); ++row) {
        if (m_model->hashAt(row) == key) {
            const QModelIndex srcIdx = m_model->index(row, 0);
            const QModelIndex proxyIdx = m_proxy->mapFromSource(srcIdx);
            if (proxyIdx.isValid()) {
                m_fileView->setCurrentIndex(proxyIdx);
                m_fileView->scrollTo(proxyIdx);
            }
            return;
        }
    }
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

    // Share Directory
    menu.addAction(tr("Share Directory"), this, [this, path]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        auto dirs = thePrefs.sharedDirs();
        if (!dirs.contains(path))
            dirs.append(path);
        sendShareDirsUpdate(dirs);
    });

    // Share with Subdirectories
    menu.addAction(tr("Share with Subdirectories"), this, [this, path]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        auto dirs = thePrefs.sharedDirs();
        collectSubdirectories(path, dirs);
        sendShareDirsUpdate(dirs);
    });

    menu.addSeparator();

    // Unshare Directory
    menu.addAction(tr("Unshare Directory"), this, [this, path]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        auto dirs = thePrefs.sharedDirs();
        dirs.removeAll(path);
        sendShareDirsUpdate(dirs);
    });

    // Unshare with Subdirectories
    menu.addAction(tr("Unshare with Subdirectories"), this, [this, path]() {
        if (!m_ipc || !m_ipc->isConnected())
            return;
        auto dirs = thePrefs.sharedDirs();
        QStringList toRemove;
        collectSubdirectories(path, toRemove);
        for (const auto& d : toRemove)
            dirs.removeAll(d);
        sendShareDirsUpdate(dirs);
    });

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
    m_ipc->sendRequest(std::move(msg), [this, tab](const IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;
        const QCborMap details = resp.field(1).toMap();
        auto* dlg = new FileDetailDialog(details,
                                          static_cast<FileDetailDialog::Tab>(tab), this);
        dlg->show();
    });
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
