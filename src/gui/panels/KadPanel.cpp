#include "panels/KadPanel.h"

#include "app/IpcClient.h"
#include "controls/ContactsGraph.h"
#include "controls/KadContactsModel.h"
#include "controls/KadSearchesModel.h"

#include "app/UiState.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <algorithm>

#include <QButtonGroup>
#include <QCborArray>
#include <QCborMap>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

namespace eMule {

using namespace Ipc;

KadPanel::KadPanel(QWidget* parent)
    : QWidget(parent)
{
    setupUi();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &KadPanel::onRefreshTimer);

    m_graphTimer = new QTimer(this);
    m_graphTimer->setInterval(60'000);
    connect(m_graphTimer, &QTimer::timeout, this, &KadPanel::onGraphTimer);
}

KadPanel::~KadPanel() = default;

void KadPanel::setIpcClient(IpcClient* client)
{
    m_ipc = client;

    if (m_ipc && m_ipc->isConnected()) {
        m_refreshTimer->setInterval(m_ipc->pollingInterval());
        m_refreshTimer->start();
        m_graphTimer->start();
        onRefreshTimer();
    } else if (m_ipc) {
        // Start polling once connected
        connect(m_ipc, &IpcClient::connected, this, [this]() {
            m_refreshTimer->setInterval(m_ipc->pollingInterval());
            m_refreshTimer->start();
            m_graphTimer->start();
            m_lastHelloCount = 0;
            onRefreshTimer();
        });
        connect(m_ipc, &IpcClient::disconnected, this, [this]() {
            m_refreshTimer->stop();
            m_graphTimer->stop();
            m_lastHelloCount = 0;
            m_contactsModel->clear();
            m_searchesModel->clear();
            m_contactsGraph->clearSamples();
            m_kadNetworkGraph->clearSamples();
            m_contactsLabel->setText(tr("\u25B8 Contacts (0)"));
            m_searchesLabel->setText(tr("\u25B8 Current Searches (0)"));
        });
    } else {
        m_refreshTimer->stop();
        m_graphTimer->stop();
        m_lastHelloCount = 0;
        m_contactsModel->clear();
        m_searchesModel->clear();
        m_contactsGraph->clearSamples();
        m_kadNetworkGraph->clearSamples();
        m_contactsLabel->setText(tr("\u25B8 Contacts (0)"));
        m_searchesLabel->setText(tr("\u25B8 Current Searches (0)"));
    }
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void KadPanel::onRefreshTimer()
{
    requestContacts();
    requestSearches();
    requestStatus();
}

void KadPanel::onGraphTimer()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::GetKadStatus);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;

        const QCborMap status = resp.fieldMap(1);
        const int64_t hellosSent = status.value(QStringLiteral("hellosSent")).toInteger();
        const int delta = static_cast<int>(hellosSent - m_lastHelloCount);
        m_lastHelloCount = hellosSent;
        m_contactsGraph->addSample(std::max(0, delta));
    });
}

void KadPanel::onBootstrapClicked()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    if (m_bootstrapIpRadio->isChecked()) {
        const QString  ip   = m_ipEdit->text().trimmed();
        const uint16_t port = static_cast<uint16_t>(m_portEdit->text().trimmed().toUShort());
        if (ip.isEmpty() || port == 0)
            return;
        IpcMessage msg(IpcMsgType::BootstrapKad);
        msg.append(ip);
        msg.append(int64_t(port));
        m_ipc->sendRequest(std::move(msg));
    } else {
        // ToDo: Download nodes.dat from URL and bootstrap
    }
}

void KadPanel::onBootstrapTypeChanged()
{
    const bool ipMode = m_bootstrapIpRadio->isChecked();
    m_ipEdit->setEnabled(ipMode);
    m_portEdit->setEnabled(ipMode);
    m_urlEdit->setEnabled(!ipMode);
    updateBootstrapButton();
}

void KadPanel::onBootstrapInputChanged()
{
    updateBootstrapButton();
}

void KadPanel::updateBootstrapButton()
{
    bool valid = false;
    if (m_bootstrapIpRadio->isChecked()) {
        const QString ip   = m_ipEdit->text().trimmed();
        const int     port = m_portEdit->text().trimmed().toInt();
        valid = !ip.isEmpty() && port >= 1 && port <= 65535;
    } else {
        valid = !m_urlEdit->text().trimmed().isEmpty();
    }
    m_bootstrapBtn->setEnabled(valid);
}

void KadPanel::onDisconnectClicked()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    if (m_kadRunning) {
        IpcMessage msg(IpcMsgType::DisconnectKad);
        m_ipc->sendRequest(std::move(msg));
    } else {
        // Connect — bootstrap from known clients / nodes.dat
        IpcMessage msg(IpcMsgType::BootstrapKad);
        msg.append(QString{});
        msg.append(int64_t(0));
        m_ipc->sendRequest(std::move(msg));
    }
}

void KadPanel::onRecheckFirewall()
{
    // ToDo: add RecheckFirewall IPC message if needed
}

// ---------------------------------------------------------------------------
// UI setup
// ---------------------------------------------------------------------------

void KadPanel::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(0);

    m_vertSplitter = new QSplitter(Qt::Vertical, this);
    m_vertSplitter->setHandleWidth(4);
    m_vertSplitter->setChildrenCollapsible(false);
    m_vertSplitter->setStyleSheet(
        QStringLiteral("QSplitter::handle { background: palette(mid); }"));

    // Top section: contacts list (left) + controls panel (right)
    auto* topSplitter = new QSplitter(Qt::Horizontal);
    topSplitter->addWidget(createContactsPanel());
    topSplitter->addWidget(createControlsPanel());
    topSplitter->setStretchFactor(0, 3);
    topSplitter->setStretchFactor(1, 1);

    m_vertSplitter->addWidget(topSplitter);
    m_vertSplitter->addWidget(createSearchesPanel());
    m_vertSplitter->setStretchFactor(0, 3);
    m_vertSplitter->setStretchFactor(1, 1);

    // Restore saved splitter position and auto-save on change
    theUiState.bindKadSplitter(m_vertSplitter);

    mainLayout->addWidget(m_vertSplitter);
}

QWidget* KadPanel::createContactsPanel()
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    // Section header matching MFC style: icon-like prefix + bold text
    m_contactsLabel = new QLabel(tr("\u25B8 Contacts (0)"));
    QFont boldFont = m_contactsLabel->font();
    boldFont.setBold(true);
    m_contactsLabel->setFont(boldFont);
    layout->addWidget(m_contactsLabel);

    m_contactsModel = new KadContactsModel(this);
    auto* proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(m_contactsModel);
    m_contactsView = new QTreeView;
    m_contactsView->setModel(proxyModel);
    m_contactsView->setRootIsDecorated(false);
    m_contactsView->setAlternatingRowColors(true);
    m_contactsView->setSortingEnabled(true);
    m_contactsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_contactsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_contactsView->setUniformRowHeights(true);

    // Style the header
    auto* header = m_contactsView->header();
    header->setStretchLastSection(true);
    header->setDefaultSectionSize(200);
    header->resizeSection(KadContactsModel::ColStatus, 70);

    // Compact monospace font for hex/binary display
    QFont monoFont(QStringLiteral("Courier New"), 9);
    m_contactsView->setFont(monoFont);

    layout->addWidget(m_contactsView);

    return widget;
}

QWidget* KadPanel::createControlsPanel()
{
    auto* widget = new QWidget;
    widget->setMinimumWidth(220);
    widget->setMaximumWidth(320);
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);

    // Recheck Firewall + Connect/Disconnect buttons
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);
    m_recheckFwBtn  = new QPushButton(tr("Recheck Firewall"));
    m_disconnectBtn = new QPushButton(tr("Connect"));
    btnRow->addStretch();
    btnRow->addWidget(m_recheckFwBtn);
    btnRow->addWidget(m_disconnectBtn);
    layout->addLayout(btnRow);

    connect(m_recheckFwBtn,  &QPushButton::clicked, this, &KadPanel::onRecheckFirewall);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &KadPanel::onDisconnectClicked);

    // Mutual-exclusion group for the two radio buttons
    m_bootstrapGroup = new QButtonGroup(this);

    // Row 1: ○ Bootstrap   IP Address: [______]  Port: [___]
    auto* ipRow = new QHBoxLayout;
    ipRow->setSpacing(4);
    m_bootstrapIpRadio = new QRadioButton(tr("Bootstrap"));
    m_bootstrapIpRadio->setChecked(true);
    m_bootstrapGroup->addButton(m_bootstrapIpRadio);
    ipRow->addWidget(m_bootstrapIpRadio);
    ipRow->addWidget(new QLabel(tr("IP Address:")));
    m_ipEdit = new QLineEdit;
    m_ipEdit->setPlaceholderText(QStringLiteral("0.0.0.0"));
    ipRow->addWidget(m_ipEdit, 2);
    ipRow->addWidget(new QLabel(tr("Port:")));
    m_portEdit = new QLineEdit;
    m_portEdit->setPlaceholderText(QStringLiteral("4672"));
    m_portEdit->setMaximumWidth(48);
    ipRow->addWidget(m_portEdit);
    layout->addLayout(ipRow);

    // Row 2: ○ Nodes.dat from URL:  [_______________________]
    auto* urlRow = new QHBoxLayout;
    urlRow->setSpacing(4);
    m_bootstrapUrlRadio = new QRadioButton(tr("Nodes.dat from URL:"));
    m_bootstrapGroup->addButton(m_bootstrapUrlRadio);
    urlRow->addWidget(m_bootstrapUrlRadio);
    m_urlEdit = new QLineEdit;
    m_urlEdit->setPlaceholderText(QStringLiteral("http://"));
    m_urlEdit->setEnabled(false);
    urlRow->addWidget(m_urlEdit, 1);
    layout->addLayout(urlRow);

    // Bootstrap button — right-aligned, disabled until valid input
    m_bootstrapBtn = new QPushButton(tr("Bootstrap"));
    m_bootstrapBtn->setEnabled(false);
    auto* bsBtnRow = new QHBoxLayout;
    bsBtnRow->addStretch();
    bsBtnRow->addWidget(m_bootstrapBtn);
    layout->addLayout(bsBtnRow);

    connect(m_bootstrapBtn,      &QPushButton::clicked,  this, &KadPanel::onBootstrapClicked);
    connect(m_bootstrapIpRadio,  &QRadioButton::toggled, this, &KadPanel::onBootstrapTypeChanged);
    connect(m_bootstrapUrlRadio, &QRadioButton::toggled, this, &KadPanel::onBootstrapTypeChanged);
    connect(m_ipEdit,   &QLineEdit::textChanged, this, &KadPanel::onBootstrapInputChanged);
    connect(m_portEdit, &QLineEdit::textChanged, this, &KadPanel::onBootstrapInputChanged);
    connect(m_urlEdit,  &QLineEdit::textChanged, this, &KadPanel::onBootstrapInputChanged);

    // Separator
    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    // Contacts histogram graph
    m_contactsGraph = new ContactsGraph;
    m_contactsGraph->setMinimumHeight(70);
    layout->addWidget(m_contactsGraph, 2);

    // Kad Network graph
    m_kadNetworkGraph = new ContactsGraph;
    m_kadNetworkGraph->setMinimumHeight(50);
    layout->addWidget(m_kadNetworkGraph, 1);

    return widget;
}

QWidget* KadPanel::createSearchesPanel()
{
    auto* widget = new QWidget;
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(2);

    // Section header matching MFC style
    m_searchesLabel = new QLabel(tr("\u25B8 Current Searches (0)"));
    QFont boldFont = m_searchesLabel->font();
    boldFont.setBold(true);
    m_searchesLabel->setFont(boldFont);
    layout->addWidget(m_searchesLabel);

    m_searchesModel = new KadSearchesModel(this);
    m_searchesView = new QTreeView;
    m_searchesView->setModel(m_searchesModel);
    m_searchesView->setRootIsDecorated(false);
    m_searchesView->setAlternatingRowColors(true);
    m_searchesView->setSortingEnabled(true);
    m_searchesView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_searchesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_searchesView->setUniformRowHeights(true);

    auto* header = m_searchesView->header();
    header->setStretchLastSection(true);

    layout->addWidget(m_searchesView);

    return widget;
}

// ---------------------------------------------------------------------------
// IPC data requests
// ---------------------------------------------------------------------------

void KadPanel::requestContacts()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::GetKadContacts);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            m_contactsModel->clear();
            m_contactsLabel->setText(tr("\u25B8 Contacts (0)"));
            return;
        }

        const QCborArray arr = resp.fieldArray(1);
        std::vector<KadContactRow> rows;
        rows.reserve(static_cast<size_t>(arr.size()));

        for (const auto& val : arr) {
            const QCborMap m = val.toMap();
            KadContactRow row;
            row.clientId = m.value(QStringLiteral("clientId")).toString();
            row.distance = m.value(QStringLiteral("distance")).toString();
            row.ip = static_cast<uint32_t>(m.value(QStringLiteral("ip")).toInteger());
            row.udpPort = static_cast<uint16_t>(m.value(QStringLiteral("udpPort")).toInteger());
            row.tcpPort = static_cast<uint16_t>(m.value(QStringLiteral("tcpPort")).toInteger());
            row.version = static_cast<uint8_t>(m.value(QStringLiteral("version")).toInteger());
            row.type = static_cast<uint8_t>(m.value(QStringLiteral("type")).toInteger());
            rows.push_back(std::move(row));
        }

        m_contactsModel->setContacts(std::move(rows));
        const int count = m_contactsModel->contactCount();
        m_contactsLabel->setText(tr("\u25B8 Contacts (%1)").arg(count));
    });
}

void KadPanel::requestSearches()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::GetKadSearches);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            m_searchesModel->clear();
            m_searchesLabel->setText(tr("\u25B8 Current Searches (0)"));
            return;
        }

        const QCborArray arr = resp.fieldArray(1);
        std::vector<KadSearchRow> rows;
        rows.reserve(static_cast<size_t>(arr.size()));

        for (const auto& val : arr) {
            const QCborMap m = val.toMap();
            KadSearchRow row;
            row.searchId = static_cast<uint32_t>(m.value(QStringLiteral("searchId")).toInteger());
            row.key      = m.value(QStringLiteral("key")).toString();
            row.type     = m.value(QStringLiteral("type")).toString();
            row.name     = m.value(QStringLiteral("name")).toString();
            row.status   = m.value(QStringLiteral("status")).toString();
            row.load     = static_cast<float>(m.value(QStringLiteral("load")).toInteger());
            row.packetsSent = static_cast<uint32_t>(m.value(QStringLiteral("packetsSent")).toInteger());
            row.responses   = static_cast<uint32_t>(m.value(QStringLiteral("responses")).toInteger());
            rows.push_back(std::move(row));
        }

        m_searchesModel->setSearches(std::move(rows));
        const int count = m_searchesModel->searchCount();
        m_searchesLabel->setText(tr("\u25B8 Current Searches (%1)").arg(count));
    });
}

void KadPanel::requestStatus()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage req(IpcMsgType::GetKadStatus);
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;

        const QCborMap status = resp.fieldMap(1);
        m_kadRunning = status.value(QStringLiteral("running")).toBool();
        m_disconnectBtn->setText(m_kadRunning
            ? tr("Disconnect")
            : tr("Connect"));
    });
}

} // namespace eMule
