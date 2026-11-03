#include "pch.h"
#include "panels/Ed2kKadPanel.h"

#include "app/IpcClient.h"
#include "app/SkinIcons.h"
#include "app/UiState.h"
#include "controls/AbstractListView.h"
#include "controls/ContactsGraph.h"
#include "controls/KadContactHistogram.h"
#include "controls/KadContactsModel.h"
#include "controls/KadLookupGraph.h"
#include "controls/KadSearchesModel.h"
#include "utils/Appearance.h"
#include "utils/CountryFlags.h"
#include "utils/IpcFeedback.h"
#include "utils/PanelPoller.h"
#include "utils/StatusBarNotifier.h"
#include "utils/UrlPrefField.h"

#include "IpcMessage.h"

#include "prefs/Preferences.h"

#include <QButtonGroup>
#include <QCborArray>
#include <QCborMap>
#include <QEvent>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QTabWidget>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

namespace eMule {

using namespace Ipc;

Ed2kKadPanel::Ed2kKadPanel(QWidget* parent)
    : KadPanelBase(parent)
{
    m_contactsModel = new KadContactsModel(this);
    m_searchesModel = new KadSearchesModel(this);
    QWidget* contacts = createContactsPanel();
    QWidget* controls = createControlsPanel();
    QWidget* searches = createSearchesPanel();
    buildLayout(contacts, controls, searches, [](QSplitter* s) { theUiState.bindKadSplitter(s); });

    // Deliberately not a PanelPoller: this one accumulates the contacts graph's
    // history from its own GetKadStatus request, so it has to keep running while
    // the panel is hidden or the trace grows a hole for every tab switch.
    m_graphTimer = new QTimer(this);
    m_graphTimer->setInterval(60'000);
    connect(m_graphTimer, &QTimer::timeout, this, &Ed2kKadPanel::onGraphTimer);
}

Ed2kKadPanel::~Ed2kKadPanel() = default;

void Ed2kKadPanel::setIpcClient(IpcClient* client)
{
    // row-update pushes only while this list is on screen
    m_poller->setOnScreenHook(client ? client->pushGroupHold(Ipc::PushGroupKad) : nullptr);
    KadPanelBase::setIpcClient(client);
    if (!m_ipc) {
        m_graphTimer->stop();
        m_lastResponseCount = 0;
        return;
    }
    connect(m_ipc, &IpcClient::kadSearchesChanged, this, [this] { m_poller->nudge(); });
    if (m_ipc->isConnected()) {
        m_graphTimer->start();
    } else {
        connect(m_ipc, &IpcClient::connected, this, [this] {
            m_graphTimer->start();
            m_lastResponseCount = 0;
        });
        connect(m_ipc, &IpcClient::disconnected, this, [this] {
            m_graphTimer->stop();
            m_lastResponseCount = 0;
        });
    }
}

void Ed2kKadPanel::refreshNodesUrl()
{
    refreshUrlPrefField(m_urlEdit, &Preferences::nodesDatURL);
}

// ---------------------------------------------------------------------------
// protected
// ---------------------------------------------------------------------------

bool Ed2kKadPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::FocusIn) {
        if (watched == m_ipEdit || watched == m_portEdit)
            m_bootstrapIpRadio->setChecked(true);
        else if (watched == m_urlEdit)
            m_bootstrapUrlRadio->setChecked(true);
    }
    return QWidget::eventFilter(watched, event);
}

void Ed2kKadPanel::refresh()
{
    requestContacts();
    requestSearches();
    requestStatus();

    // Only fetch lookup history when the Search Details tab is visible
    if (lookupTabShown())
        requestLookupHistory();
}

void Ed2kKadPanel::requestLookupHistory()
{
    // Polled only while the Search Details tab shows
    if (!ipcReady() || !lookupTabShown())
        return;
    if (m_graphSearchId == 0) {
        m_lookupGraph->clear();
        setLookupTabTitle({});
        return;
    }

    IpcMessage req(IpcMsgType::GetKadLookupHistory);
    req.append(static_cast<qint64>(m_graphSearchId));
    const std::uint64_t searchId = m_graphSearchId;
    m_ipc->sendPoll(this, std::move(req), [this, searchId](const IpcMessage& resp) {
        if (searchId != m_graphSearchId || resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;
        const QCborMap meta = resp.fieldMap(2);
        // A deleted search keeps its last picture until another one is shown (MFC)
        if (!meta.value(QStringLiteral("found")).toBool())
            return;

        const QCborArray arr = resp.fieldArray(1);
        std::vector<LookupEntry> entries;
        entries.reserve(static_cast<size_t>(arr.size()));
        for (const auto& val : arr) {
            const QCborMap m = val.toMap();
            LookupEntry e;
            e.contactID = m.value(QStringLiteral("contactID")).toString();
            e.distance  = m.value(QStringLiteral("distance")).toString();
            e.contactVersion = static_cast<uint8_t>(m.value(QStringLiteral("contactVersion")).toInteger());
            e.askedContactsAge = m.value(QStringLiteral("askedContactsAge")).toInteger(-1);
            e.respondedContact = static_cast<uint32_t>(m.value(QStringLiteral("respondedContact")).toInteger());
            e.askedSearchItemAge = m.value(QStringLiteral("askedSearchItemAge")).toInteger(-1);
            e.respondedSearchItem = static_cast<uint32_t>(m.value(QStringLiteral("respondedSearchItem")).toInteger());
            e.providedCloser = m.value(QStringLiteral("providedCloser")).toBool();
            e.forcedInteresting = m.value(QStringLiteral("forcedInteresting")).toBool();
            e.dist = {static_cast<uint32_t>(m.value(QStringLiteral("dist0")).toInteger()),
                      static_cast<uint32_t>(m.value(QStringLiteral("dist1")).toInteger()),
                      static_cast<uint32_t>(m.value(QStringLiteral("dist2")).toInteger()),
                      static_cast<uint32_t>(m.value(QStringLiteral("dist3")).toInteger())};
            for (const auto& idx : m.value(QStringLiteral("receivedFromIdx")).toArray())
                e.receivedFromIdx.push_back(static_cast<int>(idx.toInteger()));
            entries.push_back(std::move(e));
        }

        const int typeId = static_cast<int>(meta.value(QStringLiteral("typeId")).toInteger(-1));
        m_lookupGraph->setLookup(std::move(entries), typeId);

        // MFC CKadLookupGraph::GetCurrentLookupTitle: quoted name, else type
        const QString name = meta.value(QStringLiteral("name")).toString();
        setLookupTabTitle(name.isEmpty() ? KadSearchesModel::typeText(typeId)
                                         : QStringLiteral("\"%1\"").arg(name));
    });
}

void Ed2kKadPanel::autoPickLookup()
{
    // MFC UpdateSearchGraph + CKadSearchListCtrl::FetchAndSelectActiveSearch:
    // while the shown search lives, keep it; else take the newest running one,
    // preferring file/keyword/notes/store over node lookups.
    if (!theUiState.kadAutoShowLookups() || !m_kadRunning)
        return;
    if (m_graphSearchId != 0 && m_searchesModel->rowOf(static_cast<uint32_t>(m_graphSearchId)) >= 0)
        return;

    const auto& rows = m_searchesModel->searches();
    uint32_t interesting = 0, other = 0;
    for (auto it = rows.rbegin(); it != rows.rend() && interesting == 0; ++it) {
        if (it->stopping)
            continue;
        switch (it->typeId) {
        case 2: case 3: case 4: case 5: case 6: case 7: case 9:   // File..StoreNotes, FindSource
            interesting = it->searchId;
            break;
        default:
            if (other == 0)
                other = it->searchId;
        }
    }
    const uint32_t pick = interesting != 0 ? interesting : other;
    if (pick == 0)
        return;
    m_graphSearchId = pick;

    // Mark it in the list only while the graph shows (MFC bMark)
    if (lookupTabShown())
        markSearchRow(m_searchesModel->rowOf(pick));
    requestLookupHistory();
}

void Ed2kKadPanel::clearAll()
{
    m_contactsModel->clear();
    m_searchesModel->clear();
    m_contactsGraph->clearSamples();
    m_kadNetworkGraph->clear();
    clearLookupGraph();
    m_contactsLabel->setText(tr("▸ Contacts (0)"));
    m_searchesLabel->setText(tr("▸ Current Searches (0)"));
}

std::uint64_t Ed2kKadPanel::searchIdAtRow(int sourceRow) const
{
    return m_searchesModel->data(m_searchesModel->index(sourceRow, KadSearchesModel::ColNumber), Qt::UserRole).toUInt();
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void Ed2kKadPanel::onGraphTimer()
{
    if (!ipcReady())
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

void Ed2kKadPanel::onBootstrapClicked()
{
    if (!ipcReady())
        return;

    if (m_bootstrapIpRadio->isChecked()) {
        // "ip:port" in the address box is split (MFC KademliaWnd.cpp:262-268)
        QString ip = m_ipEdit->text().trimmed();
        if (const qsizetype colon = ip.indexOf(u':'); colon >= 0) {
            m_portEdit->setText(ip.mid(colon + 1));
            ip.truncate(colon);
            m_ipEdit->setText(ip);
        }
        const uint16_t port = m_portEdit->text().trimmed().toUShort();
        if (ip.size() < 7 || port == 0) {
            (port ? m_ipEdit : m_portEdit)->setFocus();
            return;
        }
        IpcMessage msg(IpcMsgType::BootstrapKad);
        msg.append(ip);
        msg.append(qint64(port));
        m_ipc->sendRequest(std::move(msg));
    } else if (m_bootstrapUrlRadio->isChecked()) {
        const QString url = urlPrefFieldValue(m_urlEdit, Preferences::kDefaultNodesDatURL);
        if (!url.contains(QLatin1String("://"))) {
            StatusBarNotifier::post(tr("The URL is invalid."));
            return;
        }

        m_downloadingNodes = true;
        m_bootstrapBtn->setEnabled(false);
        m_bootstrapBtn->setText(tr("Downloading..."));

        // The daemon fetches and reads it, also while Kad is already running
        IpcMessage msg(IpcMsgType::ImportKadNodes);
        msg.append(url);
        m_ipc->sendRequest(std::move(msg), [self = QPointer<Ed2kKadPanel>(this)](const IpcMessage& resp) {
            if (!self)
                return;
            self->m_downloadingNodes = false;
            self->m_bootstrapBtn->setText(tr("Bootstrap"));
            self->updateBootstrapButton();
            IpcFeedback::checkOrWarn(resp, self, tr("Kademlia"));
        });
    } else if (!m_kadRunning) {
        // From known clients: just start; hellos from peers do the rest
        IpcMessage msg(IpcMsgType::BootstrapKad);
        msg.append(QString{});
        msg.append(qint64(0));
        m_ipc->sendRequest(std::move(msg));
    }
}

void Ed2kKadPanel::onDisconnectClicked()
{
    if (!ipcReady())
        return;

    if (m_kadRunning) {   // Disconnect, or Cancel while still connecting
        IpcMessage msg(IpcMsgType::DisconnectKad);
        m_ipc->sendRequest(std::move(msg));
    } else {
        // Connect — bootstrap from known clients / nodes.dat
        IpcMessage msg(IpcMsgType::BootstrapKad);
        msg.append(QString{});
        msg.append(qint64(0));
        m_ipc->sendRequest(std::move(msg));
    }
}

void Ed2kKadPanel::onRecheckFirewall()
{
    if (!ipcReady())
        return;

    IpcMessage msg(IpcMsgType::RecheckFirewall);
    QPointer<Ed2kKadPanel> self(this);
    m_ipc->sendRequest(std::move(msg), [self](const IpcMessage& resp) {
        if (self)
            IpcFeedback::checkOrWarn(resp, self, tr("Kademlia"));
    });
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

QWidget* Ed2kKadPanel::createContactsPanel()
{
    QWidget* widget = buildListPanel(m_contactsModel, tr("▸ Contacts (0)"), {}, m_contactsView, m_contactsLabel);
    auto* contactsView = static_cast<ListTreeView*>(m_contactsView);

    // Style the header — defaults used on first launch, then overridden by saved state
    auto* header = m_contactsView->header();
    header->setDefaultSectionSize(200);
    // Type, ID, Distance, Country. Fresh layout: ID first, as MFC
    // (srchybrid/KadContactListCtrl.cpp:53-55).
    if (!theUiState.hasHeaderState(QStringLiteral("kadContacts")))
        header->moveSection(KadContactsModel::ColClientId, 0);
    contactsView->setDefaultSort(KadContactsModel::ColStatus, Qt::AscendingOrder);
    contactsView->bindColumns(QStringLiteral("kadContacts"), {110, 200, 200, 100}, {KadContactsModel::ColCountry});
    CountryFlags::bindFlagColumn(contactsView);

    // Compact monospace font for hex/binary display
    QFont monoFont(QStringLiteral("Courier New"), scaledPointSize(9));
    m_contactsView->setFont(monoFont);
    return widget;
}

QWidget* Ed2kKadPanel::createControlsPanel()
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

    connect(m_recheckFwBtn,  &QPushButton::clicked, this, &Ed2kKadPanel::onRecheckFirewall);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &Ed2kKadPanel::onDisconnectClicked);

    // Mutual-exclusion group for the three radio buttons
    m_bootstrapGroup = new QButtonGroup(this);

    // Row 1: ○ Bootstrap   IP Address: [______]  Port: [___]
    auto* ipRow = new QHBoxLayout;
    ipRow->setSpacing(4);
    m_bootstrapIpRadio = new QRadioButton(tr("Bootstrap"));
    m_bootstrapGroup->addButton(m_bootstrapIpRadio);
    ipRow->addWidget(m_bootstrapIpRadio);
    ipRow->addWidget(new QLabel(tr("IP or Address:")));
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
    bindUrlPrefField(m_urlEdit, QStringLiteral("nodesDatURL"), Preferences::kDefaultNodesDatURL,
                     &Preferences::nodesDatURL, &Preferences::setNodesDatURL,
                     [this] { return m_ipc; });
    urlRow->addWidget(m_urlEdit, 1);
    layout->addLayout(urlRow);

    // Row 3: ○ From known clients — MFC's default (KademliaWnd.cpp:210)
    m_bootstrapClientsRadio = new QRadioButton(tr("From known clients"));
    m_bootstrapClientsRadio->setChecked(true);
    m_bootstrapGroup->addButton(m_bootstrapClientsRadio);
    layout->addWidget(m_bootstrapClientsRadio);

    // Focusing an edit picks its radio (MFC OnEnSetfocusBootstrapip / ...Nodesdat)
    m_ipEdit->installEventFilter(this);
    m_portEdit->installEventFilter(this);
    m_urlEdit->installEventFilter(this);

    // Bootstrap button — right-aligned, disabled until valid input
    m_bootstrapBtn = new QPushButton(tr("Bootstrap"));
    if (thePrefs.useOriginalIcons())
        m_bootstrapBtn->setIcon(skinIcon(QStringLiteral(":/icons/KadBootstrap.ico")));
    m_bootstrapBtn->setEnabled(false);
    auto* bsBtnRow = new QHBoxLayout;
    bsBtnRow->addStretch();
    bsBtnRow->addWidget(m_bootstrapBtn);
    layout->addLayout(bsBtnRow);

    connect(m_bootstrapBtn, &QPushButton::clicked, this, &Ed2kKadPanel::onBootstrapClicked);
    for (auto* radio : {m_bootstrapIpRadio, m_bootstrapUrlRadio, m_bootstrapClientsRadio})
        connect(radio, &QRadioButton::toggled, this, [this] { updateBootstrapButton(); });
    for (auto* edit : {m_ipEdit, m_portEdit, m_urlEdit})
        connect(edit, &QLineEdit::textChanged, this, [this] { updateBootstrapButton(); });

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

QWidget* Ed2kKadPanel::createSearchesPanel()
{
    QWidget* widget = buildListPanel(m_searchesModel, tr("▸ Current Searches (0)"),
                                     QStringLiteral(":/icons/KadCurrentSearches.ico"), m_searchesView, m_searchesLabel);
    // Number, Key, Type, Name, Status, Load, Packets Sent, Responses.
    static_cast<ListTreeView*>(m_searchesView)->bindColumns(QStringLiteral("kadSearches"), {80, 170, 100, 240, 100, 60, 90, 90});
    return widget;
}

void Ed2kKadPanel::updateBootstrapButton()
{
    if (m_downloadingNodes)
        return;
    // MFC CKademliaWnd::UpdateControlsState (KademliaWnd.cpp:377-390): only while
    // not connected; an address needs a port, typed apart or as "ip:port"
    bool valid = true;   // known clients; an empty URL means the default
    if (m_bootstrapIpRadio->isChecked()) {
        const QString ip = m_ipEdit->text().trimmed();
        valid = !ip.isEmpty() && (ip.contains(u':') || !m_portEdit->text().trimmed().isEmpty());
    }
    m_bootstrapBtn->setEnabled(valid && !m_kadConnected);
}

void Ed2kKadPanel::requestContacts()
{
    if (!ipcReady())
        return;

    IpcMessage req(IpcMsgType::GetKadContacts);
    m_ipc->sendPoll(this, std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            m_contactsModel->clear();
            m_contactsLabel->setText(tr("▸ Contacts (0)"));
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
            row.addr = m.value(QStringLiteral("addr")).toString();
            row.cc = m.value(QStringLiteral("cc")).toString();
            row.udpPort = static_cast<uint16_t>(m.value(QStringLiteral("udpPort")).toInteger());
            row.tcpPort = static_cast<uint16_t>(m.value(QStringLiteral("tcpPort")).toInteger());
            row.version = static_cast<uint8_t>(m.value(QStringLiteral("version")).toInteger());
            row.type = static_cast<uint8_t>(m.value(QStringLiteral("type")).toInteger());
            row.ipVerified = m.value(QStringLiteral("ipVerified")).toBool();
            row.bootstrap = m.value(QStringLiteral("bootstrap")).toBool();
            rows.push_back(std::move(row));
        }

        const int ctScroll = m_contactsView->verticalScrollBar()->value();
        m_kadNetworkGraph->setContacts(rows);
        m_contactsModel->setContacts(std::move(rows));
        setContactCount(m_contactsModel->contactCount());
        m_contactsView->verticalScrollBar()->setValue(ctScroll);
    });
}

void Ed2kKadPanel::requestSearches()
{
    if (!ipcReady())
        return;

    IpcMessage req(IpcMsgType::GetKadSearches);
    m_ipc->sendPoll(this, std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            m_searchesModel->clear();
            m_searchesLabel->setText(tr("▸ Current Searches (0)"));
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
            row.typeId   = static_cast<int>(m.value(QStringLiteral("typeId")).toInteger(-1));
            row.name     = m.value(QStringLiteral("name")).toString();
            row.stopping = m.value(QStringLiteral("stopping")).toBool();
            row.load     = static_cast<uint32_t>(m.value(QStringLiteral("load")).toInteger());
            row.loadResponses = static_cast<uint32_t>(m.value(QStringLiteral("loadResponses")).toInteger());
            row.loadTotal = static_cast<uint32_t>(m.value(QStringLiteral("loadTotal")).toInteger());
            row.packetsSent    = static_cast<uint32_t>(m.value(QStringLiteral("packetsSent")).toInteger());
            row.requestAnswers = static_cast<uint32_t>(m.value(QStringLiteral("requestAnswers")).toInteger());
            row.responses      = static_cast<uint32_t>(m.value(QStringLiteral("responses")).toInteger());
            rows.push_back(std::move(row));
        }

        m_searchesModel->setSearches(std::move(rows));
        m_searchesLabel->setText(tr("▸ Current Searches (%1)").arg(m_searchesModel->searchCount()));

        autoPickLookup();
    });
}

void Ed2kKadPanel::requestStatus()
{
    if (!ipcReady())
        return;

    IpcMessage req(IpcMsgType::GetKadStatus);
    m_ipc->sendPoll(this, std::move(req), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;

        const QCborMap status = resp.fieldMap(1);
        m_kadRunning = status.value(QStringLiteral("running")).toBool();
        // Wipe the graph while Kad is off (MFC CKademliaWnd::UpdateSearchGraph)
        if (!m_kadRunning && m_graphSearchId != 0)
            clearLookupGraph();
        m_kadConnected = status.value(QStringLiteral("connected")).toBool();
        // MFC CKademliaWnd::UpdateControlsState (KademliaWnd.cpp:364-375)
        m_disconnectBtn->setText(m_kadConnected ? tr("Disconnect")
                                 : m_kadRunning ? tr("Cancel") : tr("Connect"));
        m_recheckFwBtn->setEnabled(m_kadConnected);
        updateBootstrapButton();
    });
}

void Ed2kKadPanel::clearLookupGraph()
{
    m_graphSearchId = 0;
    m_lookupGraph->clear();
    setLookupTabTitle({});
}

} // namespace eMule
