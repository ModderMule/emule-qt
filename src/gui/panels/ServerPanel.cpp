#include "panels/ServerPanel.h"

#include "app/IpcClient.h"
#include "controls/LogWidget.h"
#include "controls/ServerListModel.h"

#include "app/AppContext.h"
#include "app/UiState.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "prefs/Preferences.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"

#include "protocol/ED2KLink.h"

#include <QActionGroup>
#include <QApplication>
#include <QCborArray>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QHostAddress>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

namespace {

/// Create a small 16×16 icon with a bold colored glyph (for context menus).
static QIcon makeMenuIcon(const QColor& color, const QString& text)
{
    constexpr int sz = 16;
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(color);
    QFont f = p.font();
    f.setPixelSize(13);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(0, 0, sz, sz), Qt::AlignCenter, text);
    return QIcon(pm);
}

} // anonymous namespace

namespace eMule {

ServerPanel::ServerPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(5000);
    connect(m_refreshTimer, &QTimer::timeout, this, &ServerPanel::onRefreshTimer);
    m_refreshTimer->start();
}

ServerPanel::~ServerPanel() = default;

void ServerPanel::setIpcClient(IpcClient* client)
{
    m_ipc = client;

    if (m_ipc) {
        // Request server list when IPC connects
        connect(m_ipc, &IpcClient::connected, this, &ServerPanel::requestServerList);

        // Refresh server list and button on server state changes
        connect(m_ipc, &IpcClient::serverStateChanged,
                this, [this](const Ipc::IpcMessage& msg) {
            requestServerList();
            const QCborMap info = msg.fieldMap(0);
            updateConnectButton(
                info.value(QStringLiteral("connected")).toBool(),
                info.value(QStringLiteral("connecting")).toBool());
        });

        // If already connected, request immediately
        if (m_ipc->isConnected())
            requestServerList();
    }
}

void ServerPanel::setServerList(ServerList* serverList)
{
    m_serverList = serverList;

    if (m_serverList) {
        connect(m_serverList, &ServerList::serverAdded, this, &ServerPanel::onServerListChanged);
        connect(m_serverList, &ServerList::serverAboutToBeRemoved, this, &ServerPanel::onServerListChanged);
        connect(m_serverList, &ServerList::listReloaded, this, &ServerPanel::onServerListChanged);
        onServerListChanged();
    }
}

void ServerPanel::setServerConnect(ServerConnect* serverConnect)
{
    m_serverConnect = serverConnect;

    if (m_serverConnect) {
        connect(m_serverConnect, &ServerConnect::connectedToServer,
                this, &ServerPanel::onConnectedToServer);
        connect(m_serverConnect, &ServerConnect::disconnectedFromServer,
                this, &ServerPanel::onDisconnectedFromServer);
        connect(m_serverConnect, &ServerConnect::serverMessageReceived,
                this, &ServerPanel::onServerMessage);
        connect(m_serverConnect, &ServerConnect::stateChanged,
                this, [this]() {
            refreshMyInfo();
            updateConnectButton(m_serverConnect->isConnected(),
                                m_serverConnect->isConnecting());
        });
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ServerPanel::onConnectClicked()
{
    // IPC mode: send connect/disconnect to daemon
    if (m_ipc && m_ipc->isConnected()) {
        // Check button text to determine action
        if (m_connectBtn->text() == tr("Disconnect") || m_connectBtn->text() == tr("Cancel")) {
            Ipc::IpcMessage req(Ipc::IpcMsgType::DisconnectFromServer);
            m_ipc->sendRequest(std::move(req));
            m_connectBtn->setText(tr("Connect"));
        } else {
            Ipc::IpcMessage req(Ipc::IpcMsgType::ConnectToServer);
            m_ipc->sendRequest(std::move(req));
            m_connectBtn->setText(tr("Cancel"));
        }
        return;
    }

    // Direct mode: call core objects directly
    if (!m_serverConnect)
        return;

    if (m_serverConnect->isConnected() || m_serverConnect->isConnecting()) {
        if (m_serverConnect->isConnecting())
            m_serverConnect->stopConnectionTry();
        m_serverConnect->disconnect();
        m_connectBtn->setText(tr("Connect"));
    } else {
        m_serverConnect->connectToAnyServer();
        m_connectBtn->setText(tr("Cancel"));
    }
}

void ServerPanel::onAddServerClicked()
{
    if (!m_serverList)
        return;

    const QString ip = m_newServerIp->text().trimmed();
    const uint16_t port = m_newServerPort->text().trimmed().toUShort();
    const QString name = m_newServerName->text().trimmed();

    if (ip.isEmpty() || port == 0)
        return;

    // ToDo: Resolve hostname to IP if needed
    // For now, parse as numeric IP
    const QHostAddress addr(ip);
    if (addr.isNull())
        return;

    auto server = std::make_unique<Server>(addr.toIPv4Address(), port);
    if (!name.isEmpty())
        server->setName(name);
    if (thePrefs.manualServerHighPriority())
        server->setPreference(ServerPriority::High);

    if (m_serverList->addServer(std::move(server))) {
        m_newServerIp->clear();
        m_newServerPort->setText(QStringLiteral("4661")); // default port, not translatable
        m_newServerName->clear();
    }
}

void ServerPanel::onUpdateServerMetClicked()
{
    // ToDo: Download server.met from URL and merge into list
}

void ServerPanel::onRefreshTimer()
{
    if (m_ipc && m_ipc->isConnected())
        requestServerList();
    refreshMyInfo();
}

void ServerPanel::onServerListChanged()
{
    const QString key = saveSelection();
    m_serverListModel->refreshFromServerList(m_serverList);
    m_serversLabel->setText(
        tr("\u25B8 Servers (%1)").arg(m_serverListModel->rowCount()));
    restoreSelection(key);
}

void ServerPanel::onConnectedToServer()
{
    m_connectBtn->setText(tr("Disconnect"));
    refreshMyInfo();

    if (m_serverConnect) {
        if (auto* srv = m_serverConnect->currentServer()) {
            m_logWidget->appendServerInfo(
                tr("Connected to <b>%1</b> (%2:%3)")
                    .arg(srv->name(), srv->address())
                    .arg(srv->port()));
        }
    }
}

void ServerPanel::onDisconnectedFromServer()
{
    m_connectBtn->setText(tr("Connect"));
    refreshMyInfo();
    m_logWidget->appendServerInfo(tr("Disconnected from server."));
}

void ServerPanel::updateConnectButton(bool connected, bool connecting)
{
    if (connected)
        m_connectBtn->setText(tr("Disconnect"));
    else if (connecting)
        m_connectBtn->setText(tr("Disconnect"));
    else
        m_connectBtn->setText(tr("Connect"));
}

void ServerPanel::onServerMessage(const QString& msg)
{
    m_logWidget->appendServerInfo(msg);
}

void ServerPanel::onServerDoubleClicked(const QModelIndex& index)
{
    // Map proxy index to source model index
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(m_serverListView->model());
    const QModelIndex srcIndex = proxy ? proxy->mapToSource(index) : index;

    // IPC mode: send ConnectToServer with IP and port
    if (m_ipc && m_ipc->isConnected()) {
        const auto* row = m_serverListModel->rowAt(srcIndex.row());
        if (!row || row->numericIp == 0)
            return;

        Ipc::IpcMessage req(Ipc::IpcMsgType::ConnectToServer);
        req.append(static_cast<qint64>(row->numericIp));
        req.append(static_cast<qint64>(row->port));
        m_ipc->sendRequest(std::move(req));
        m_connectBtn->setText(tr("Cancel"));
        return;
    }

    // Direct mode: connect via core objects
    if (!m_serverConnect || !m_serverList)
        return;

    const auto* srv = m_serverListModel->serverAtRow(srcIndex.row());
    if (!srv)
        return;

    auto* mutableSrv = m_serverList->findByIPTcp(srv->ip(), srv->port());
    if (mutableSrv)
        m_serverConnect->connectToServer(mutableSrv);
}

void ServerPanel::onServerContextMenu(const QPoint& pos)
{
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(m_serverListView->model());
    if (!proxy)
        return;

    // Determine which row (if any) was clicked
    const QModelIndex proxyIdx = m_serverListView->indexAt(pos);
    const QModelIndex srcIdx = proxyIdx.isValid() && proxy
                                   ? proxy->mapToSource(proxyIdx)
                                   : QModelIndex();
    const ServerRow* row = srcIdx.isValid()
                               ? m_serverListModel->rowAt(srcIdx.row())
                               : nullptr;

    const bool hasSelection = (row != nullptr);
    const bool hasItems = (m_serverListModel->rowCount() > 0);

    // Build menu fresh each time
    if (!m_serverMenu)
        m_serverMenu = new QMenu(this);
    else
        m_serverMenu->clear();

    // -- Connect To -----------------------------------------------------------
    auto* connectAction = m_serverMenu->addAction(tr("Connect To"));
    connectAction->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    connectAction->setEnabled(hasSelection);
    if (hasSelection) {
        const uint32_t ip = row->numericIp;
        const uint16_t port = row->port;
        connect(connectAction, &QAction::triggered, this, [this, ip, port]() {
            if (m_ipc && m_ipc->isConnected()) {
                Ipc::IpcMessage req(Ipc::IpcMsgType::ConnectToServer);
                req.append(static_cast<qint64>(ip));
                req.append(static_cast<qint64>(port));
                m_ipc->sendRequest(std::move(req));
                m_connectBtn->setText(tr("Cancel"));
            } else if (m_serverConnect && m_serverList) {
                auto* srv = m_serverList->findByIPTcp(ip, port);
                if (srv)
                    m_serverConnect->connectToServer(srv);
            }
        });
    }

    // -- Priority submenu -----------------------------------------------------
    auto* prioMenu = m_serverMenu->addMenu(tr("Priority"));
    prioMenu->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    prioMenu->setEnabled(hasSelection);

    auto* prioGroup = new QActionGroup(prioMenu);
    prioGroup->setExclusive(true);

    auto* prioLow = prioMenu->addAction(tr("Low"));
    auto* prioNormal = prioMenu->addAction(tr("Normal"));
    auto* prioHigh = prioMenu->addAction(tr("High"));

    prioLow->setCheckable(true);
    prioNormal->setCheckable(true);
    prioHigh->setCheckable(true);
    prioGroup->addAction(prioLow);
    prioGroup->addAction(prioNormal);
    prioGroup->addAction(prioHigh);

    if (hasSelection) {
        if (row->preference == QStringLiteral("Low"))
            prioLow->setChecked(true);
        else if (row->preference == QStringLiteral("High"))
            prioHigh->setChecked(true);
        else
            prioNormal->setChecked(true);

        const uint32_t ip = row->numericIp;
        const uint16_t port = row->port;

        auto setPriority = [this, ip, port](ServerPriority prio) {
            if (m_ipc && m_ipc->isConnected()) {
                Ipc::IpcMessage req(Ipc::IpcMsgType::SetServerPriority);
                req.append(static_cast<qint64>(ip));
                req.append(static_cast<qint64>(port));
                req.append(static_cast<qint64>(prio));
                m_ipc->sendRequest(std::move(req), [this](const Ipc::IpcMessage&) {
                    requestServerList();
                });
            } else if (m_serverList) {
                auto* srv = m_serverList->findByIPTcp(ip, port);
                if (srv) {
                    srv->setPreference(prio);
                    onServerListChanged();
                }
            }
        };

        connect(prioLow, &QAction::triggered, this, [setPriority]() {
            setPriority(ServerPriority::Low);
        });
        connect(prioNormal, &QAction::triggered, this, [setPriority]() {
            setPriority(ServerPriority::Normal);
        });
        connect(prioHigh, &QAction::triggered, this, [setPriority]() {
            setPriority(ServerPriority::High);
        });
    }

    m_serverMenu->addSeparator();

    // -- Add To Static List ---------------------------------------------------
    auto* addStaticAction = m_serverMenu->addAction(tr("Add To Static List"));
    addStaticAction->setIcon(makeMenuIcon(QColor(0x22, 0x8B, 0x22), QStringLiteral("+")));
    addStaticAction->setEnabled(hasSelection && !row->isStatic);
    if (hasSelection) {
        const uint32_t ip = row->numericIp;
        const uint16_t port = row->port;
        connect(addStaticAction, &QAction::triggered, this, [this, ip, port]() {
            if (m_ipc && m_ipc->isConnected()) {
                Ipc::IpcMessage req(Ipc::IpcMsgType::SetServerStatic);
                req.append(static_cast<qint64>(ip));
                req.append(static_cast<qint64>(port));
                req.append(true);
                m_ipc->sendRequest(std::move(req), [this](const Ipc::IpcMessage&) {
                    requestServerList();
                });
            } else if (m_serverList) {
                auto* srv = m_serverList->findByIPTcp(ip, port);
                if (srv) {
                    srv->setStaticMember(true);
                    onServerListChanged();
                }
            }
        });
    }

    // -- Remove From Static List ----------------------------------------------
    auto* removeStaticAction = m_serverMenu->addAction(tr("Remove From Static List"));
    removeStaticAction->setIcon(makeMenuIcon(QColor(0x88, 0x88, 0x88), QStringLiteral("\u2212")));
    removeStaticAction->setEnabled(hasSelection && row->isStatic);
    if (hasSelection) {
        const uint32_t ip = row->numericIp;
        const uint16_t port = row->port;
        connect(removeStaticAction, &QAction::triggered, this, [this, ip, port]() {
            if (m_ipc && m_ipc->isConnected()) {
                Ipc::IpcMessage req(Ipc::IpcMsgType::SetServerStatic);
                req.append(static_cast<qint64>(ip));
                req.append(static_cast<qint64>(port));
                req.append(false);
                m_ipc->sendRequest(std::move(req), [this](const Ipc::IpcMessage&) {
                    requestServerList();
                });
            } else if (m_serverList) {
                auto* srv = m_serverList->findByIPTcp(ip, port);
                if (srv) {
                    srv->setStaticMember(false);
                    onServerListChanged();
                }
            }
        });
    }

    m_serverMenu->addSeparator();

    // -- Copy eD2K Links ------------------------------------------------------
    auto* copyLinkAction = m_serverMenu->addAction(tr("Copy eD2K Links"));
    copyLinkAction->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
    copyLinkAction->setEnabled(hasSelection);
    if (hasSelection) {
        const QString address = row->ip.section(QLatin1Char(':'), 0, 0);
        const uint16_t port = row->port;
        connect(copyLinkAction, &QAction::triggered, this, [address, port]() {
            ED2KServerLink link{address, port};
            QApplication::clipboard()->setText(link.toLink());
        });
    }

    // -- Paste eD2K Links -----------------------------------------------------
    auto* pasteLinkAction = m_serverMenu->addAction(tr("Paste eD2K Links"));
    pasteLinkAction->setIcon(style()->standardIcon(QStyle::SP_FileDialogContentsView));
    const QString clipText = QApplication::clipboard()->text().trimmed();
    const bool hasEd2kLink = clipText.startsWith(QStringLiteral("ed2k://|server|"),
                                                  Qt::CaseInsensitive);
    pasteLinkAction->setEnabled(hasEd2kLink);
    connect(pasteLinkAction, &QAction::triggered, this, [this, clipText]() {
        // Parse each line in the clipboard
        const QStringList lines = clipText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            auto parsed = parseED2KLink(line.trimmed());
            if (!parsed)
                continue;
            if (auto* srvLink = std::get_if<ED2KServerLink>(&*parsed)) {
                const QHostAddress addr(srvLink->address);
                if (addr.isNull() || srvLink->port == 0)
                    continue;
                if (m_ipc && m_ipc->isConnected()) {
                    Ipc::IpcMessage req(Ipc::IpcMsgType::AddServer);
                    req.append(srvLink->address);
                    req.append(static_cast<qint64>(srvLink->port));
                    req.append(QString()); // no name
                    m_ipc->sendRequest(std::move(req), [this](const Ipc::IpcMessage&) {
                        requestServerList();
                    });
                } else if (m_serverList) {
                    auto server = std::make_unique<Server>(addr.toIPv4Address(), srvLink->port);
                    m_serverList->addServer(std::move(server));
                }
            }
        }
    });

    // -- Remove ---------------------------------------------------------------
    auto* removeAction = m_serverMenu->addAction(tr("Remove"));
    removeAction->setIcon(makeMenuIcon(QColor(0xCC, 0x00, 0x00), QStringLiteral("\u2715")));
    removeAction->setEnabled(hasSelection);
    if (hasSelection) {
        const uint32_t ip = row->numericIp;
        const uint16_t port = row->port;
        connect(removeAction, &QAction::triggered, this, [this, ip, port]() {
            if (m_ipc && m_ipc->isConnected()) {
                Ipc::IpcMessage req(Ipc::IpcMsgType::RemoveServer);
                req.append(static_cast<qint64>(ip));
                req.append(static_cast<qint64>(port));
                m_ipc->sendRequest(std::move(req), [this](const Ipc::IpcMessage&) {
                    requestServerList();
                });
            } else if (m_serverList) {
                auto* srv = m_serverList->findByIPTcp(ip, port);
                if (srv)
                    m_serverList->removeServer(srv);
            }
        });
    }

    // -- Remove All -----------------------------------------------------------
    auto* removeAllAction = m_serverMenu->addAction(tr("Remove All"));
    removeAllAction->setIcon(makeMenuIcon(QColor(0xCC, 0x00, 0x00), QStringLiteral("\u2715")));
    removeAllAction->setEnabled(hasItems);
    connect(removeAllAction, &QAction::triggered, this, [this]() {
        if (m_ipc && m_ipc->isConnected()) {
            Ipc::IpcMessage req(Ipc::IpcMsgType::RemoveAllServers);
            m_ipc->sendRequest(std::move(req), [this](const Ipc::IpcMessage&) {
                requestServerList();
            });
        } else if (m_serverList) {
            m_serverList->removeAllServers();
        }
    });

    m_serverMenu->addSeparator();

    // -- Find... --------------------------------------------------------------
    auto* findAction = m_serverMenu->addAction(tr("Find..."));
    findAction->setIcon(makeMenuIcon(QColor(0x44, 0x44, 0x44), QStringLiteral("\u2315")));
    findAction->setEnabled(hasItems);
    connect(findAction, &QAction::triggered, this, &ServerPanel::showFindDialog);

    m_serverMenu->popup(m_serverListView->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void ServerPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(0);

    m_vertSplitter = new QSplitter(Qt::Vertical, this);
    m_vertSplitter->setHandleWidth(4);
    m_vertSplitter->setChildrenCollapsible(false);
    m_vertSplitter->setStyleSheet(
        QStringLiteral("QSplitter::handle { background: palette(mid); }"));

    // Top section: server list (left) + controls (right)
    auto* topSplitter = new QSplitter(Qt::Horizontal);
    topSplitter->addWidget(createServerListPanel());
    topSplitter->addWidget(createControlsPanel());
    topSplitter->setStretchFactor(0, 4);
    topSplitter->setStretchFactor(1, 1);

    // Bottom section: log tabs
    m_logWidget = new LogWidget;

    m_vertSplitter->addWidget(topSplitter);
    m_vertSplitter->addWidget(m_logWidget);
    m_vertSplitter->setStretchFactor(0, 2);
    m_vertSplitter->setStretchFactor(1, 1);

    // Restore saved splitter position and auto-save on change
    theUiState.bindServerSplitter(m_vertSplitter);

    mainLayout->addWidget(m_vertSplitter);
}

QWidget* ServerPanel::createServerListPanel()
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    m_serversLabel = new QLabel(tr("\u25B8 Servers (0)"));
    QFont boldFont = m_serversLabel->font();
    boldFont.setBold(true);
    m_serversLabel->setFont(boldFont);
    layout->addWidget(m_serversLabel);

    m_serverListModel = new ServerListModel(this);
    auto* serverProxy = new QSortFilterProxyModel(this);
    serverProxy->setSourceModel(m_serverListModel);
    serverProxy->setSortRole(Qt::UserRole);
    m_serverListView = new QTreeView;
    m_serverListView->setModel(serverProxy);
    m_serverListView->setRootIsDecorated(false);
    m_serverListView->setAlternatingRowColors(true);
    m_serverListView->setSortingEnabled(true);
    m_serverListView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_serverListView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_serverListView->setUniformRowHeights(true);

    auto* header = m_serverListView->header();
    header->setStretchLastSection(true);
    header->setDefaultSectionSize(80);
    header->resizeSection(ServerListModel::ColName, 140);
    header->resizeSection(ServerListModel::ColIP, 140);
    header->resizeSection(ServerListModel::ColDescription, 160);
    theUiState.bindHeaderView(header, QStringLiteral("serverList"));

    m_serverListView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_serverListView, &QTreeView::doubleClicked,
            this, &ServerPanel::onServerDoubleClicked);
    connect(m_serverListView, &QTreeView::customContextMenuRequested,
            this, &ServerPanel::onServerContextMenu);

    layout->addWidget(m_serverListView);

    return widget;
}

QWidget* ServerPanel::createControlsPanel()
{
    auto* widget = new QWidget;
    widget->setMinimumWidth(200);
    widget->setMaximumWidth(300);
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(6);

    // Connect button (top, matching screenshot)
    m_connectBtn = new QPushButton(tr("Connect"));
    auto* connectRow = new QHBoxLayout;
    connectRow->addStretch();
    connectRow->addWidget(m_connectBtn);
    layout->addLayout(connectRow);
    connect(m_connectBtn, &QPushButton::clicked, this, &ServerPanel::onConnectClicked);

    // New Server section
    auto* newServerGroup = new QGroupBox(tr("New Server"));
    auto* nsLayout = new QVBoxLayout(newServerGroup);
    nsLayout->setSpacing(4);

    // IP + Port row
    auto* ipPortRow = new QHBoxLayout;
    ipPortRow->setSpacing(4);
    ipPortRow->addWidget(new QLabel(tr("IP Address:")));
    m_newServerIp = new QLineEdit;
    m_newServerIp->setPlaceholderText(QStringLiteral("0.0.0.0"));
    ipPortRow->addWidget(m_newServerIp, 1);
    ipPortRow->addWidget(new QLabel(tr("Port:")));
    m_newServerPort = new QLineEdit(QStringLiteral("4661"));
    m_newServerPort->setMaximumWidth(55);
    ipPortRow->addWidget(m_newServerPort);
    nsLayout->addLayout(ipPortRow);

    // Name row
    auto* nameRow = new QHBoxLayout;
    nameRow->setSpacing(4);
    nameRow->addWidget(new QLabel(tr("Name:")));
    m_newServerName = new QLineEdit;
    nameRow->addWidget(m_newServerName, 1);
    nsLayout->addLayout(nameRow);

    // Add to list button
    m_addServerBtn = new QPushButton(tr("Add to list"));
    auto* addBtnRow = new QHBoxLayout;
    addBtnRow->addStretch();
    addBtnRow->addWidget(m_addServerBtn);
    nsLayout->addLayout(addBtnRow);
    connect(m_addServerBtn, &QPushButton::clicked, this, &ServerPanel::onAddServerClicked);

    layout->addWidget(newServerGroup);

    // Update server.met from URL section
    auto* updateRow = new QHBoxLayout;
    updateRow->setSpacing(4);
    auto* updateIcon = new QLabel(QStringLiteral("\u21BB")); // ↻ refresh icon
    updateIcon->setToolTip(tr("Update server.met from URL"));
    updateRow->addWidget(updateIcon);
    updateRow->addWidget(new QLabel(tr("Update server.met from URL:")));
    updateRow->addStretch();
    layout->addLayout(updateRow);

    auto* urlRow = new QHBoxLayout;
    urlRow->setSpacing(4);
    m_updateUrlEdit = new QLineEdit;
    urlRow->addWidget(m_updateUrlEdit, 1);
    m_updateBtn = new QPushButton(tr("Update"));
    m_updateBtn->setFixedWidth(55);
    urlRow->addWidget(m_updateBtn);
    layout->addLayout(urlRow);
    connect(m_updateBtn, &QPushButton::clicked, this, &ServerPanel::onUpdateServerMetClicked);

    // Separator
    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    // My Info section
    m_infoLabel = new QLabel;
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setTextFormat(Qt::RichText);
    m_infoLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    refreshMyInfo();
    layout->addWidget(m_infoLabel, 1);

    layout->addStretch();

    return widget;
}

// ---------------------------------------------------------------------------
// My Info refresh
// ---------------------------------------------------------------------------

void ServerPanel::refreshMyInfo()
{
    QString html;
    html += QStringLiteral("<b>") + tr("My Info") + QStringLiteral("</b><br>");

    // eD2K Network
    html += QStringLiteral("<b>") + tr("eD2K Network") + QStringLiteral("</b><br>");
    if (m_serverConnect && m_serverConnect->isConnected()) {
        html += QStringLiteral("&nbsp;&nbsp;") + tr("Status:") + QStringLiteral(" <font color='green'>") + tr("Connected") + QStringLiteral("</font><br>");
        if (auto* srv = m_serverConnect->currentServer()) {
            html += QStringLiteral("&nbsp;&nbsp;") + tr("Server: %1").arg(srv->name()) + QStringLiteral("<br>");
        }
        html += QStringLiteral("&nbsp;&nbsp;") + tr("Client ID: %1").arg(m_serverConnect->clientID()) + QStringLiteral("<br>");
        html += QStringLiteral("&nbsp;&nbsp;%1<br>")
                    .arg(m_serverConnect->isLowID()
                             ? QStringLiteral("<font color='orange'>") + tr("Low ID (Firewalled)") + QStringLiteral("</font>")
                             : QStringLiteral("<font color='green'>") + tr("High ID") + QStringLiteral("</font>"));
    } else if (m_serverConnect && m_serverConnect->isConnecting()) {
        html += QStringLiteral("&nbsp;&nbsp;") + tr("Status:") + QStringLiteral(" <font color='orange'>") + tr("Connecting...") + QStringLiteral("</font><br>");
    } else {
        html += QStringLiteral("&nbsp;&nbsp;") + tr("Status:") + QStringLiteral(" ") + tr("Disconnected") + QStringLiteral("<br>");
    }

    // Kad Network
    html += QStringLiteral("<br><b>") + tr("Kad Network") + QStringLiteral("</b><br>");
    if (theApp.serverConnect) {
        // ToDo: Get Kad status from Kademlia instance
        html += QStringLiteral("&nbsp;&nbsp;") + tr("Status:") + QStringLiteral(" --<br>");
    } else {
        html += QStringLiteral("&nbsp;&nbsp;") + tr("Status:") + QStringLiteral(" --<br>");
    }

    // Network info
    html += QStringLiteral("<br><b>") + tr("Network") + QStringLiteral("</b><br>");
    html += QStringLiteral("&nbsp;&nbsp;") + tr("IP:Port: %1:%2")
                .arg(thePrefs.bindAddress().isEmpty()
                         ? QStringLiteral("0.0.0.0")
                         : thePrefs.bindAddress())
                .arg(thePrefs.port()) + QStringLiteral("<br>");
    html += QStringLiteral("&nbsp;&nbsp;") + tr("UDP Port: %1").arg(thePrefs.udpPort()) + QStringLiteral("<br>");

    m_infoLabel->setText(html);
}

// ---------------------------------------------------------------------------
// IPC: request server list from daemon
// ---------------------------------------------------------------------------

void ServerPanel::requestServerList()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    const QString key = saveSelection();

    Ipc::IpcMessage req(Ipc::IpcMsgType::GetServers);
    m_ipc->sendRequest(std::move(req), [this, key](const Ipc::IpcMessage& resp) {
        if (!resp.fieldBool(0))
            return;
        const QCborArray servers = resp.fieldArray(1);
        m_serverListModel->refreshFromCborArray(servers);
        m_serversLabel->setText(
            tr("\u25B8 Servers (%1)").arg(m_serverListModel->rowCount()));
        restoreSelection(key);
    });
}

// ---------------------------------------------------------------------------
// Selection save/restore — keyed by IP:port column value
// ---------------------------------------------------------------------------

QString ServerPanel::saveSelection() const
{
    auto* sel = m_serverListView->selectionModel();
    if (!sel || !sel->hasSelection())
        return {};
    const QModelIndex idx = sel->currentIndex();
    if (!idx.isValid())
        return {};
    return idx.siblingAtColumn(ServerListModel::ColIP)
        .data(Qt::DisplayRole).toString();
}

void ServerPanel::restoreSelection(const QString& key)
{
    if (key.isEmpty())
        return;

    auto* proxyModel = qobject_cast<QSortFilterProxyModel*>(
        m_serverListView->model());
    if (!proxyModel)
        return;

    for (int row = 0; row < proxyModel->rowCount(); ++row) {
        const QModelIndex idx = proxyModel->index(row, ServerListModel::ColIP);
        if (idx.data(Qt::DisplayRole).toString() == key) {
            m_serverListView->setCurrentIndex(idx);
            return;
        }
    }
}

void ServerPanel::showFindDialog()
{
    auto* dlg = new QDialog(this);
    dlg->setWindowTitle(tr("Search"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    auto* layout = new QFormLayout(dlg);

    auto* searchEdit = new QLineEdit;
    layout->addRow(tr("Search for:"), searchEdit);

    auto* columnCombo = new QComboBox;
    columnCombo->addItem(tr("Server Name"),     ServerListModel::ColName);
    columnCombo->addItem(tr("IP"),              ServerListModel::ColIP);
    columnCombo->addItem(tr("Description"),     ServerListModel::ColDescription);
    columnCombo->addItem(tr("Ping"),            ServerListModel::ColPing);
    columnCombo->addItem(tr("Users"),           ServerListModel::ColUsers);
    columnCombo->addItem(tr("Max Users"),       ServerListModel::ColMaxUsers);
    columnCombo->addItem(tr("Preference"),      ServerListModel::ColPreference);
    columnCombo->addItem(tr("Failed"),          ServerListModel::ColFailed);
    columnCombo->addItem(tr("Static"),          ServerListModel::ColStatic);
    columnCombo->addItem(tr("Soft File Limit"), ServerListModel::ColSoftFiles);
    columnCombo->addItem(tr("Low ID"),          ServerListModel::ColLowID);
    columnCombo->addItem(tr("Obfuscation"),     ServerListModel::ColObfuscation);
    layout->addRow(tr("Search in column:"), columnCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addRow(buttons);

    connect(buttons, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dlg, &QDialog::reject);

    connect(dlg, &QDialog::accepted, this, [this, searchEdit, columnCombo]() {
        const QString term = searchEdit->text().trimmed();
        if (term.isEmpty())
            return;

        const int column = columnCombo->currentData().toInt();
        auto* proxyModel = qobject_cast<QSortFilterProxyModel*>(
            m_serverListView->model());
        if (!proxyModel)
            return;

        for (int row = 0; row < proxyModel->rowCount(); ++row) {
            const QModelIndex idx = proxyModel->index(row, column);
            if (idx.data(Qt::DisplayRole).toString().contains(term, Qt::CaseInsensitive)) {
                m_serverListView->setCurrentIndex(idx);
                m_serverListView->scrollTo(idx);
                return;
            }
        }
    });

    dlg->exec();
}

} // namespace eMule
