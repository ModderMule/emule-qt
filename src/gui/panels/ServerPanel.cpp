#include "panels/ServerPanel.h"

#include "controls/LogWidget.h"
#include "controls/ServerListModel.h"

#include "app/AppContext.h"
#include "app/UiState.h"
#include "prefs/Preferences.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"

#include <QFont>
#include <QFrame>
#include <QHostAddress>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

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
                this, &ServerPanel::refreshMyInfo);
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void ServerPanel::onConnectClicked()
{
    if (!m_serverConnect)
        return;

    if (m_serverConnect->isConnected() || m_serverConnect->isConnecting()) {
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
    refreshMyInfo();
}

void ServerPanel::onServerListChanged()
{
    m_serverListModel->refreshFromServerList(m_serverList);
    m_serversLabel->setText(
        tr("\u25B8 Servers (%1)").arg(m_serverListModel->rowCount()));
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

void ServerPanel::onServerMessage(const QString& msg)
{
    m_logWidget->appendServerInfo(msg);
}

void ServerPanel::onServerDoubleClicked(const QModelIndex& index)
{
    if (!m_serverConnect || !m_serverList)
        return;

    const auto* srv = m_serverListModel->serverAtRow(index.row());
    if (!srv)
        return;

    // Find the mutable server in the list
    auto* mutableSrv = m_serverList->findByIPTcp(srv->ip(), srv->port());
    if (mutableSrv)
        m_serverConnect->connectToServer(mutableSrv);
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

    connect(m_serverListView, &QTreeView::doubleClicked,
            this, &ServerPanel::onServerDoubleClicked);

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

} // namespace eMule
