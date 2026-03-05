#include "panels/KadPanel.h"

#include "app/IpcClient.h"
#include "controls/ContactsGraph.h"
#include "controls/KadContactHistogram.h"
#include "controls/KadContactsModel.h"
#include "controls/KadLookupGraph.h"
#include "controls/KadSearchesModel.h"

#include "app/UiState.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include "prefs/Preferences.h"

#include <algorithm>

#include <QButtonGroup>
#include <QCborArray>
#include <QCborMap>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QRadioButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTabWidget>
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

void KadPanel::switchToSubTab(int index)
{
    if (!m_topTabWidget || index < 0 || index >= m_topTabWidget->count())
        return;

    m_topTabWidget->setCurrentIndex(index);

    // Auto-select the first search if none is selected (needed for Search Details tab)
    if (index == 1 && m_searchesView->selectionModel()->selectedRows().isEmpty()
        && m_searchesModel->searchCount() > 0) {
        m_searchesView->selectionModel()->select(
            m_searchesView->model()->index(0, 0),
            QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
}

void KadPanel::setIpcClient(IpcClient* client)
{
    m_ipc = client;

    if (m_ipc) {
        connect(m_ipc, &IpcClient::kadSearchesChanged, this, [this]() {
            requestSearches();
        });
    }

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
            m_lastResponseCount = 0;
            onRefreshTimer();
        });
        connect(m_ipc, &IpcClient::disconnected, this, [this]() {
            m_refreshTimer->stop();
            m_graphTimer->stop();
            m_lastResponseCount = 0;
            m_contactsModel->clear();
            m_searchesModel->clear();
            m_contactsGraph->clearSamples();
            m_kadNetworkGraph->clear();
            m_lookupGraph->clear();
            m_contactsLabel->setText(tr("\u25B8 Contacts (0)"));
            m_searchesLabel->setText(tr("\u25B8 Current Searches (0)"));
        });
    } else {
        m_refreshTimer->stop();
        m_graphTimer->stop();
        m_lastResponseCount = 0;
        m_contactsModel->clear();
        m_searchesModel->clear();
        m_contactsGraph->clearSamples();
        m_kadNetworkGraph->clear();
        m_lookupGraph->clear();
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

    // Only fetch lookup history when the Search Details tab is visible
    if (m_topTabWidget->currentIndex() == 1)
        requestLookupHistory();
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
        const int64_t hellosReceived = status.value(QStringLiteral("hellosReceived")).toInteger();
        const int delta = static_cast<int>(hellosReceived - m_lastResponseCount);
        m_lastResponseCount = hellosReceived;
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
        const QString url = m_urlEdit->text().trimmed();
        if (url.isEmpty())
            return;

        m_bootstrapBtn->setEnabled(false);
        m_bootstrapBtn->setText(tr("Downloading..."));

        auto* nam = new QNetworkAccessManager(this);
        auto* reply = nam->get(QNetworkRequest(QUrl(url)));
        connect(reply, &QNetworkReply::finished, this, [this, reply, nam]() {
            reply->deleteLater();
            nam->deleteLater();
            m_bootstrapBtn->setEnabled(true);
            m_bootstrapBtn->setText(tr("Bootstrap"));

            if (reply->error() != QNetworkReply::NoError) {
                QMessageBox::warning(this, tr("Kademlia"),
                    tr("Failed to download nodes.dat: %1").arg(reply->errorString()));
                return;
            }

            const QByteArray data = reply->readAll();
            if (data.isEmpty()) {
                QMessageBox::warning(this, tr("Kademlia"),
                    tr("Downloaded nodes.dat is empty."));
                return;
            }

            const QString path = QDir(thePrefs.configDir()).filePath(QStringLiteral("nodes.dat"));
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                QMessageBox::warning(this, tr("Kademlia"),
                    tr("Failed to save nodes.dat: %1").arg(f.errorString()));
                return;
            }
            f.write(data);
            f.close();

            // Bootstrap from the downloaded nodes.dat file
            IpcMessage msg(IpcMsgType::BootstrapKad);
            msg.append(QString());   // empty IP = bootstrap from nodes.dat file
            msg.append(int64_t(0));  // port 0
            m_ipc->sendRequest(std::move(msg));
        });
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
    if (!m_ipc || !m_ipc->isConnected())
        return;

    IpcMessage msg(IpcMsgType::RecheckFirewall);
    m_ipc->sendRequest(std::move(msg));
}

void KadPanel::onSearchSelectionChanged()
{
    // When a search is selected in the bottom panel, update the lookup graph
    if (m_topTabWidget->currentIndex() == 1)
        requestLookupHistory();
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

    // Top section: tab widget (left) + controls panel (right)
    auto* topSplitter = new QSplitter(Qt::Horizontal);

    // Create the tab widget with Contacts + Search Details tabs
    m_topTabWidget = new QTabWidget;
    m_topTabWidget->setDocumentMode(true);
    if (thePrefs.useOriginalIcons()) {
        m_topTabWidget->addTab(createContactsPanel(),
            QIcon(QStringLiteral(":/icons/KadContactList.ico")), tr("\u25B8 Contacts (0)"));
    } else {
        m_topTabWidget->addTab(createContactsPanel(), tr("\u25B8 Contacts (0)"));
    }

    // Search Details tab with lookup graph
    m_lookupGraph = new KadLookupGraph;
    m_topTabWidget->addTab(m_lookupGraph, tr("\u25B8 Search Details"));

    topSplitter->addWidget(m_topTabWidget);
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

    // When switching to the Search Details tab, fetch lookup history
    connect(m_topTabWidget, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1)
            requestLookupHistory();
    });
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
    proxyModel->setSortRole(Qt::UserRole);
    m_contactsView = new QTreeView;
    m_contactsView->setModel(proxyModel);
    m_contactsView->setRootIsDecorated(false);
    m_contactsView->setAlternatingRowColors(true);
    m_contactsView->setSortingEnabled(true);
    m_contactsView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_contactsView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_contactsView->setUniformRowHeights(true);

    // Style the header — defaults used on first launch, then overridden by saved state
    auto* header = m_contactsView->header();
    header->setStretchLastSection(true);
    header->setDefaultSectionSize(200);
    header->resizeSection(KadContactsModel::ColStatus, 70);
    theUiState.bindHeaderView(header, QStringLiteral("kadContacts"));

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
    if (thePrefs.useOriginalIcons())
        m_bootstrapBtn->setIcon(QIcon(QStringLiteral(":/icons/KadBootstrap.ico")));
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

    // Kad Network distance histogram
    m_kadNetworkGraph = new KadContactHistogram;
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
    auto* searchesHeader = new QHBoxLayout;
    searchesHeader->setSpacing(4);
    if (thePrefs.useOriginalIcons()) {
        auto* searchIcon = new QLabel;
        searchIcon->setPixmap(QIcon(QStringLiteral(":/icons/KadCurrentSearches.ico")).pixmap(16, 16));
        searchesHeader->addWidget(searchIcon);
    }
    m_searchesLabel = new QLabel(tr("\u25B8 Current Searches (0)"));
    QFont boldFont = m_searchesLabel->font();
    boldFont.setBold(true);
    m_searchesLabel->setFont(boldFont);
    searchesHeader->addWidget(m_searchesLabel);
    searchesHeader->addStretch();
    layout->addLayout(searchesHeader);

    m_searchesModel = new KadSearchesModel(this);
    auto* searchProxy = new QSortFilterProxyModel(this);
    searchProxy->setSourceModel(m_searchesModel);
    searchProxy->setSortRole(Qt::UserRole);
    m_searchesView = new QTreeView;
    m_searchesView->setModel(searchProxy);
    m_searchesView->setRootIsDecorated(false);
    m_searchesView->setAlternatingRowColors(true);
    m_searchesView->setSortingEnabled(true);
    m_searchesView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_searchesView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_searchesView->setUniformRowHeights(true);

    auto* header = m_searchesView->header();
    header->setStretchLastSection(true);
    theUiState.bindHeaderView(header, QStringLiteral("kadSearches"));

    layout->addWidget(m_searchesView);

    // Wire search selection to update the lookup graph
    connect(m_searchesView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &KadPanel::onSearchSelectionChanged);

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

        m_kadNetworkGraph->setContacts(rows);
        m_contactsModel->setContacts(std::move(rows));
        const int count = m_contactsModel->contactCount();
        m_contactsLabel->setText(tr("\u25B8 Contacts (%1)").arg(count));

        // Update the Contacts tab title with count
        m_topTabWidget->setTabText(0, tr("\u25B8 Contacts (%1)").arg(count));
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
            row.packetsSent    = static_cast<uint32_t>(m.value(QStringLiteral("packetsSent")).toInteger());
            row.requestAnswers = static_cast<uint32_t>(m.value(QStringLiteral("requestAnswers")).toInteger());
            row.responses      = static_cast<uint32_t>(m.value(QStringLiteral("responses")).toInteger());
            rows.push_back(std::move(row));
        }

        // Save selected search ID before model reset clears selection
        const uint32_t prevSelected = selectedSearchId();

        m_searchesModel->setSearches(std::move(rows));
        const int count = m_searchesModel->searchCount();
        m_searchesLabel->setText(tr("\u25B8 Current Searches (%1)").arg(count));

        // Restore selection: find the row matching the previous search ID
        if (prevSelected != 0 && count > 0) {
            const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(m_searchesView->model());
            for (int r = 0; r < count; ++r) {
                const uint32_t rowId = m_searchesModel->data(
                    m_searchesModel->index(r, KadSearchesModel::ColNumber),
                    Qt::UserRole).toUInt();
                if (rowId == prevSelected) {
                    const auto srcIdx = m_searchesModel->index(r, 0);
                    const auto proxyIdx = proxy ? proxy->mapFromSource(srcIdx) : srcIdx;
                    m_searchesView->selectionModel()->select(
                        proxyIdx,
                        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
                    break;
                }
            }
        }

        // Auto-select first search if nothing is selected (initial load)
        if (m_searchesView->selectionModel()->selectedRows().isEmpty() && count > 0) {
            m_searchesView->selectionModel()->select(
                m_searchesView->model()->index(0, 0),
                QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        }
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

void KadPanel::requestLookupHistory()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;

    const uint32_t searchId = selectedSearchId();
    if (searchId == 0) {
        m_lookupGraph->clear();
        m_topTabWidget->setTabText(1, tr("\u25B8 Search Details"));
        return;
    }

    IpcMessage req(IpcMsgType::GetKadLookupHistory);
    req.append(static_cast<int64_t>(searchId));
    m_ipc->sendRequest(std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            m_lookupGraph->clear();
            return;
        }

        const QCborArray arr = resp.fieldArray(1);
        std::vector<LookupEntry> entries;
        entries.reserve(static_cast<size_t>(arr.size()));

        for (const auto& val : arr) {
            const QCborMap m = val.toMap();
            LookupEntry e;
            e.contactID = m.value(QStringLiteral("contactID")).toString();
            e.distance  = m.value(QStringLiteral("distance")).toString();
            e.contactVersion = static_cast<uint8_t>(m.value(QStringLiteral("contactVersion")).toInteger());
            e.askedContactsTime = static_cast<uint32_t>(m.value(QStringLiteral("askedContactsTime")).toInteger());
            e.respondedContact = static_cast<uint32_t>(m.value(QStringLiteral("respondedContact")).toInteger());
            e.askedSearchItemTime = static_cast<uint32_t>(m.value(QStringLiteral("askedSearchItemTime")).toInteger());
            e.respondedSearchItem = static_cast<uint32_t>(m.value(QStringLiteral("respondedSearchItem")).toInteger());
            e.providedCloser = m.value(QStringLiteral("providedCloser")).toBool();
            e.forcedInteresting = m.value(QStringLiteral("forcedInteresting")).toBool();

            e.dist[0] = static_cast<uint32_t>(m.value(QStringLiteral("dist0")).toInteger());
            e.dist[1] = static_cast<uint32_t>(m.value(QStringLiteral("dist1")).toInteger());
            e.dist[2] = static_cast<uint32_t>(m.value(QStringLiteral("dist2")).toInteger());
            e.dist[3] = static_cast<uint32_t>(m.value(QStringLiteral("dist3")).toInteger());

            const QCborArray fromArr = m.value(QStringLiteral("receivedFromIdx")).toArray();
            for (const auto& idx : fromArr)
                e.receivedFromIdx.push_back(static_cast<int>(idx.toInteger()));

            entries.push_back(std::move(e));
        }

        m_lookupGraph->setEntries(std::move(entries));

        // Update the Search Details tab title to show the search type
        // Find the selected search's type name from the searches model
        const uint32_t sid = selectedSearchId();
        for (int r = 0; r < m_searchesModel->searchCount(); ++r) {
            const auto idx = m_searchesModel->index(r, KadSearchesModel::ColNumber);
            const uint32_t rowId = idx.data(Qt::UserRole).toUInt();
            if (rowId == sid) {
                const QString typeName = m_searchesModel->index(r, KadSearchesModel::ColType).data().toString();
                m_topTabWidget->setTabText(1, tr("\u25B8 Search Details (%1)").arg(typeName));
                break;
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

uint32_t KadPanel::selectedSearchId() const
{
    // Get the currently selected search ID from the searches view
    const auto selection = m_searchesView->selectionModel()->selectedRows();
    if (!selection.isEmpty()) {
        // Map from proxy to source model
        const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(m_searchesView->model());
        if (proxy) {
            const auto sourceIdx = proxy->mapToSource(selection.first());
            return m_searchesModel->data(
                m_searchesModel->index(sourceIdx.row(), KadSearchesModel::ColNumber),
                Qt::UserRole).toUInt();
        }
    }

    // No selection — use the first search if available
    if (m_searchesModel->searchCount() > 0) {
        return m_searchesModel->data(
            m_searchesModel->index(0, KadSearchesModel::ColNumber),
            Qt::UserRole).toUInt();
    }

    return 0;
}

} // namespace eMule
