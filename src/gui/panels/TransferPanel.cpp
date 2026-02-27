/// @file TransferPanel.cpp
/// @brief Transfer tab panel — implementation.

#include "panels/TransferPanel.h"

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/ClientListModel.h"
#include "controls/DownloadListModel.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCborArray>
#include <QCborMap>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

namespace eMule {

using namespace Ipc;

// ---------------------------------------------------------------------------
// Tab icon helpers — small colored icons matching MFC Transfers tab bar
// ---------------------------------------------------------------------------

namespace {

/// Create a small pixmap with two arrows (upload red ↑, download green ↓, etc.)
QPixmap makeTabIcon(const QColor& color, const QString& arrowChar)
{
    constexpr int sz = 16;
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QFont font;
    font.setPixelSize(12);
    font.setBold(true);
    p.setFont(font);
    p.setPen(color);
    p.drawText(QRect(0, 0, sz, sz), Qt::AlignCenter, arrowChar);
    return pm;
}

QPixmap uploadingIcon()
{
    // Red up-arrow like MFC
    return makeTabIcon(QColor(0xCC, 0x00, 0x00), QStringLiteral("\u25B2"));
}

QPixmap downloadingIcon()
{
    // Green down-arrow like MFC
    return makeTabIcon(QColor(0x00, 0x88, 0x00), QStringLiteral("\u25BC"));
}

QPixmap onQueueIcon()
{
    // Blue/teal queue icon
    return makeTabIcon(QColor(0x00, 0x66, 0xCC), QStringLiteral("\u29BF"));
}

QPixmap knownClientsIcon()
{
    // Gray/olive known clients icon
    return makeTabIcon(QColor(0x66, 0x88, 0x00), QStringLiteral("\u263A"));
}

} // anonymous namespace

TransferPanel::TransferPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &TransferPanel::onRefreshTimer);
}

TransferPanel::~TransferPanel() = default;

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
            m_downloadsLabel->setText(tr("\u25B8 Downloads (0)"));
            m_queueLabel->setText(tr("Clients on queue:   0"));
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
        });
        connect(m_ipc, &IpcClient::uploadUpdated, this, [this](const IpcMessage&) {
            requestUploads();
        });
    } else {
        m_refreshTimer->stop();
        m_downloadModel->clear();
        m_uploadingModel->clear();
        m_downloadingModel->clear();
        m_onQueueModel->clear();
        m_knownModel->clear();
        m_downloadsLabel->setText(tr("\u25B8 Downloads (0)"));
        m_queueLabel->setText(tr("Clients on queue:   0"));
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void TransferPanel::onRefreshTimer()
{
    requestDownloads();
    requestUploads();
}

void TransferPanel::onDownloadContextMenu(const QPoint& pos)
{
    const QModelIndex proxyIdx = m_downloadView->indexAt(pos);
    if (!proxyIdx.isValid())
        return;

    const QModelIndex srcIdx = m_downloadProxy->mapToSource(proxyIdx);
    const QString hash = m_downloadModel->hashAt(srcIdx.row());
    if (hash.isEmpty())
        return;

    // Rebuild menu each time to capture the current hash
    if (!m_downloadMenu)
        m_downloadMenu = new QMenu(this);
    else
        m_downloadMenu->clear();

    m_downloadMenu->addAction(tr("Pause"), this, [this, hash]() {
        sendDownloadAction(hash, 0);
    });
    m_downloadMenu->addAction(tr("Resume"), this, [this, hash]() {
        sendDownloadAction(hash, 1);
    });
    m_downloadMenu->addSeparator();
    m_downloadMenu->addAction(tr("Cancel"), this, [this, hash]() {
        sendDownloadAction(hash, 2);
    });

    m_downloadMenu->popup(m_downloadView->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void TransferPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_vertSplitter = new QSplitter(Qt::Vertical, this);
    m_vertSplitter->setHandleWidth(4);
    m_vertSplitter->setChildrenCollapsible(false);
    m_vertSplitter->setStyleSheet(
        QStringLiteral("QSplitter::handle { background: palette(mid); }"));

    m_vertSplitter->addWidget(createDownloadsSection());
    m_vertSplitter->addWidget(createClientsSection());
    m_vertSplitter->setStretchFactor(0, 3);
    m_vertSplitter->setStretchFactor(1, 2);

    // Restore saved splitter position
    theUiState.bindTransferSplitter(m_vertSplitter);

    mainLayout->addWidget(m_vertSplitter);
}

QWidget* TransferPanel::createDownloadsSection()
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Section header matching MFC: "▸ Downloads (N)" with bold font
    m_downloadsLabel = new QLabel(tr("\u25B8 Downloads (0)"));
    m_downloadsLabel->setContentsMargins(4, 2, 4, 2);
    QFont boldFont = m_downloadsLabel->font();
    boldFont.setBold(true);
    m_downloadsLabel->setFont(boldFont);
    layout->addWidget(m_downloadsLabel);

    // Downloads model + proxy for sorting
    m_downloadModel = new DownloadListModel(this);
    m_downloadProxy = new QSortFilterProxyModel(this);
    m_downloadProxy->setSourceModel(m_downloadModel);
    m_downloadProxy->setSortRole(Qt::UserRole);

    m_downloadView = new QTreeView;
    m_downloadView->setModel(m_downloadProxy);
    m_downloadView->setRootIsDecorated(false);
    m_downloadView->setAlternatingRowColors(true);
    m_downloadView->setSortingEnabled(true);
    m_downloadView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_downloadView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_downloadView->setUniformRowHeights(true);
    m_downloadView->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(m_downloadView, &QTreeView::customContextMenuRequested,
            this, &TransferPanel::onDownloadContextMenu);

    auto* header = m_downloadView->header();
    header->setStretchLastSection(true);
    header->setDefaultSectionSize(90);
    // Match MFC column widths approximately
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

    layout->addWidget(m_downloadView);

    return widget;
}

QWidget* TransferPanel::createClientsSection()
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Tab widget for the 4 client lists — tabs at top, left-aligned like MFC
    m_clientTabs = new QTabWidget;
    m_clientTabs->setTabPosition(QTabWidget::North);

    m_uploadingModel   = new ClientListModel(ClientListMode::Uploading, this);
    m_downloadingModel = new ClientListModel(ClientListMode::Downloading, this);
    m_onQueueModel     = new ClientListModel(ClientListMode::OnQueue, this);
    m_knownModel       = new ClientListModel(ClientListMode::KnownClients, this);

    m_clientTabs->addTab(createClientView(m_uploadingModel),
                         QIcon(uploadingIcon()),
                         tr("Uploading (0)"));
    m_clientTabs->addTab(createClientView(m_downloadingModel),
                         QIcon(downloadingIcon()),
                         tr("Downloading (0)"));
    m_clientTabs->addTab(createClientView(m_onQueueModel),
                         QIcon(onQueueIcon()),
                         tr("On Queue (0)"));
    m_clientTabs->addTab(createClientView(m_knownModel),
                         QIcon(knownClientsIcon()),
                         tr("Known Clients (0)"));

    layout->addWidget(m_clientTabs, 1);

    // Bottom status label matching MFC: "Clients on queue:    N"
    m_queueLabel = new QLabel(tr("Clients on queue:   0"));
    m_queueLabel->setContentsMargins(4, 2, 4, 2);
    layout->addWidget(m_queueLabel);

    return widget;
}

QTreeView* TransferPanel::createClientView(ClientListModel* model)
{
    auto* proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(model);

    auto* view = new QTreeView;
    view->setModel(proxy);
    view->setRootIsDecorated(false);
    view->setAlternatingRowColors(true);
    view->setSortingEnabled(true);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setUniformRowHeights(true);

    auto* header = view->header();
    header->setStretchLastSection(true);
    header->setDefaultSectionSize(100);
    header->resizeSection(0, 140); // User Name column

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
            m_downloadsLabel->setText(tr("\u25B8 Downloads (0)"));
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
            row.category          = m.value(QStringLiteral("category")).toInteger();
            row.lastSeenComplete  = m.value(QStringLiteral("lastSeenComplete")).toInteger();
            row.lastReception     = m.value(QStringLiteral("lastReception")).toInteger();
            row.addedOn           = m.value(QStringLiteral("addedOn")).toInteger();
            rows.push_back(std::move(row));
        }

        m_downloadModel->setDownloads(std::move(rows));
        const int count = m_downloadModel->downloadCount();
        m_downloadsLabel->setText(tr("\u25B8 Downloads (%1)").arg(count));
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

        // Parse uploading clients
        const QCborArray uploadingArr = result.value(QStringLiteral("uploading")).toArray();
        std::vector<ClientRow> uploadingRows;
        uploadingRows.reserve(static_cast<size_t>(uploadingArr.size()));

        // Parse waiting clients
        const QCborArray waitingArr = result.value(QStringLiteral("waiting")).toArray();
        std::vector<ClientRow> waitingRows;
        waitingRows.reserve(static_cast<size_t>(waitingArr.size()));

        auto parseClient = [](const QCborMap& m) -> ClientRow {
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
            row.isBanned        = m.value(QStringLiteral("isBanned")).toBool();

            // Pick best file name from reqFileName or uploadFileName or fileName
            row.fileName = m.value(QStringLiteral("uploadFileName")).toString();
            if (row.fileName.isEmpty())
                row.fileName = m.value(QStringLiteral("reqFileName")).toString();
            if (row.fileName.isEmpty())
                row.fileName = m.value(QStringLiteral("fileName")).toString();
            return row;
        };

        for (const auto& val : uploadingArr)
            uploadingRows.push_back(parseClient(val.toMap()));

        for (const auto& val : waitingArr)
            waitingRows.push_back(parseClient(val.toMap()));

        // Uploading tab = actively uploading clients
        m_uploadingModel->setClients(std::move(uploadingRows));

        // On Queue tab = waiting clients
        m_onQueueModel->setClients(std::move(waitingRows));

        // Update tab titles with counts
        m_clientTabs->setTabText(0,
            tr("Uploading (%1)").arg(m_uploadingModel->clientCount()));
        m_clientTabs->setTabText(2,
            tr("On Queue (%1)").arg(m_onQueueModel->clientCount()));

        // Update bottom bar label
        m_queueLabel->setText(
            tr("Clients on queue:   %1").arg(m_onQueueModel->clientCount()));
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
        // Refresh after action
        requestDownloads();
    });
}

} // namespace eMule
