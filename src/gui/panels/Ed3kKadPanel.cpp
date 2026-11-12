#include "pch.h"
#include "panels/Ed3kKadPanel.h"

#include "app/Ed3kLinkActions.h"
#include "app/IpcClient.h"
#include "app/UiState.h"
#include "controls/AbstractListView.h"
#include "controls/ContactsGraph.h"
#include "controls/Ed3kKadModels.h"
#include "controls/KadContactHistogram.h"
#include "controls/KadLookupGraph.h"
#include "utils/Appearance.h"
#include "utils/CountryFlags.h"
#include "utils/PanelPoller.h"
#include "utils/StatusBarNotifier.h"

#include "IpcMessage.h"

#include "prefs/Preferences.h"

#include <QButtonGroup>
#include <QCborArray>
#include <QCheckBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

namespace eMule {

using namespace Ipc;

namespace {

/// "4a3f…" of a hex id: the first 8 digits, as the status bar shows a node.
QString shortHex(const QString& hex)
{
    return hex.left(8).toUpper();
}

/// The top 128 bits of a hex distance as four 32-bit words (LookupEntry::dist).
std::array<uint32_t, 4> distWords(const QString& hex)
{
    std::array<uint32_t, 4> out{};
    for (int i = 0; i < 4; ++i)
        out[static_cast<size_t>(i)] = hex.mid(i * 8, 8).toUInt(nullptr, 16);
    return out;
}

/// Bits of the distance as the graph's "Distance" text (as the eD2K one: binary).
QString distanceText(const QString& hex)
{
    QString bits;
    for (const QChar c : hex.left(8))
        bits += QStringLiteral("%1").arg(QString(c).toUInt(nullptr, 16), 4, 2, QLatin1Char('0'));
    return bits;
}

} // namespace

Ed3kKadPanel::Ed3kKadPanel(QWidget* parent)
    : KadPanelBase(parent)
{
    m_contactsModel = new Ed3kKadContactsModel(this);
    m_lookupsModel = new Ed3kKadLookupsModel(this);

    QWidget* contacts = buildListPanel(m_contactsModel, tr("▸ Contacts (0)"), {}, m_contactsView, m_contactsLabel);
    auto* contactsView = static_cast<ListTreeView*>(m_contactsView);
    contactsView->setDefaultSort(Ed3kKadContactsModel::ColDistance, Qt::AscendingOrder);
    // "2": the Address column is gone, an older saved layout would shift the columns
    contactsView->bindColumns(QStringLiteral("ed3kKadContacts2"), {260, 110, 90, 260, 50, 80},
                              {Ed3kKadContactsModel::ColCountry});
    CountryFlags::bindFlagColumn(contactsView);
    m_contactsView->setFont(QFont(QStringLiteral("Courier New"), scaledPointSize(9)));

    QWidget* lookups = buildListPanel(m_lookupsModel, tr("▸ Current Lookups (0)"),
                                      QStringLiteral(":/icons/KadCurrentSearches.ico"), m_searchesView, m_searchesLabel);
    auto* lookupsView = static_cast<ListTreeView*>(m_searchesView);
    lookupsView->setDefaultSort(Ed3kKadLookupsModel::ColNumber, Qt::DescendingOrder);
    lookupsView->bindColumns(QStringLiteral("ed3kKadLookups"), {70, 110, 260, 110, 60, 70, 80, 120});

    buildLayout(contacts, createControlsPanel(), lookups, [](QSplitter* s) { theUiState.bindEd3kKadSplitter(s); });

    // contacts graph: new connections per minute, also while hidden (as the eD2K page)
    m_graphTimer = new QTimer(this);
    m_graphTimer->setInterval(60'000);
    connect(m_graphTimer, &QTimer::timeout, this, [this] {
        if (!ipcReady())
            return;
        m_ipc->sendRequest(IpcMessage(IpcMsgType::GetEd3kStatus), [this](const IpcMessage& resp) {
            if (!resp.isValid() || !resp.fieldBool(0))
                return;
            const qint64 now = resp.fieldMap(1).value(QStringLiteral("stats")).toMap().value(QStringLiteral("connections")).toInteger();
            if (m_lastConnections >= 0)
                m_contactsGraph->addSample(static_cast<int>(std::max<qint64>(0, now - m_lastConnections)));
            m_lastConnections = now;
        });
    });
    refreshPrefs();
}

Ed3kKadPanel::~Ed3kKadPanel() = default;

void Ed3kKadPanel::setIpcClient(IpcClient* client)
{
    KadPanelBase::setIpcClient(client);
    if (!m_ipc) {
        m_graphTimer->stop();
        return;
    }
    // the status bar's push keeps the side column fresh between polls
    connect(m_ipc, &IpcClient::ed3kStatusChanged, this, [this](const IpcMessage& msg) {
        if (msg.isValid())
            applyStatus(msg.fieldMap(0));
    });
    if (m_ipc->isConnected())
        m_graphTimer->start();
    connect(m_ipc, &IpcClient::connected, this, [this] {
        m_lastConnections = -1;
        m_graphTimer->start();
    });
    connect(m_ipc, &IpcClient::disconnected, this, [this] { m_graphTimer->stop(); });
}

void Ed3kKadPanel::refreshPrefs()
{
    const QSignalBlocker b1(m_autoFetchCheck), b2(m_maxAgeSpin), b3(m_seedEd2kCheck);
    m_autoFetchCheck->setChecked(thePrefs.ed3kNodeListAuto());
    m_maxAgeSpin->setValue(thePrefs.ed3kNodeListMaxAgeDays());
    m_seedEd2kCheck->setChecked(thePrefs.ed3kSeedFromEd2k());
    const auto sources = thePrefs.ed3kNodeLists().size();
    m_sourcesLabel->setText(sources == 0 ? tr("No sources") : tr("%n source(s)", nullptr, int(sources)));
    updateControls();
}

// ---------------------------------------------------------------------------
// protected
// ---------------------------------------------------------------------------

void Ed3kKadPanel::refresh()
{
    requestStatus();
    requestContacts();
    requestLookups();
    if (lookupTabShown())
        requestLookupHistory();
}

void Ed3kKadPanel::requestLookupHistory()
{
    if (!ipcReady() || !lookupTabShown())
        return;
    if (m_graphSearchId == 0) {
        m_lookupGraph->clear();
        setLookupTabTitle({});
        return;
    }
    IpcMessage req(IpcMsgType::GetEd3kKadLookupGraph);
    req.append(static_cast<qint64>(m_graphSearchId));
    const std::uint64_t id = m_graphSearchId;
    m_ipc->sendPoll(this, std::move(req), [this, id](const IpcMessage& resp) {
        // a lookup that is gone keeps its last picture until another one is shown (as eD2K)
        if (id != m_graphSearchId || resp.type() != IpcMsgType::Result || !resp.fieldBool(0))
            return;
        std::vector<LookupEntry> entries;
        const QCborArray arr = resp.fieldArray(1);
        entries.reserve(static_cast<size_t>(arr.size()));
        for (const auto& v : arr) {
            const QCborMap m = v.toMap();
            LookupEntry e;
            e.contactID = m.value(QStringLiteral("id")).toString();
            const QString dist = m.value(QStringLiteral("distance")).toString();
            e.distance = distanceText(dist);
            e.dist = distWords(dist);
            for (const auto& i : m.value(QStringLiteral("from")).toArray())
                e.receivedFromIdx.push_back(static_cast<int>(i.toInteger()));
            // ed3k asks once per node: the contact phase only
            e.askedContactsAge = m.value(QStringLiteral("asked")).toBool() ? m.value(QStringLiteral("askedAgoMs")).toInteger() : -1;
            e.respondedContact = m.value(QStringLiteral("answered")).toBool() ? 1 : 0;
            e.providedCloser = m.value(QStringLiteral("closer")).toBool();
            e.contactVersion = 1;
            entries.push_back(std::move(e));
        }
        const int row = m_lookupsModel->rowOf(id);
        // a graph type as the eD2K page has it: node lookups 0, the rest "file" (2)
        const int kind = row >= 0 ? static_cast<int>(m_lookupsModel->row(row).value(QStringLiteral("kind")).toInteger()) : 0;
        m_lookupGraph->setLookup(std::move(entries), kind == 0 ? 0 : 2);
        if (row >= 0) {
            const QCborMap& l = m_lookupsModel->row(row);
            setLookupTabTitle(QStringLiteral("%1 %2").arg(l.value(QStringLiteral("purpose")).toString(),
                                                         shortHex(l.value(QStringLiteral("target")).toString())));
        }
    });
}

void Ed3kKadPanel::autoPickLookup()
{
    // as the eD2K page: keep a shown lookup while it runs, else the newest running one,
    // searches and stores before node lookups
    if (!theUiState.kadAutoShowLookups() || !m_running)
        return;
    if (const int row = m_lookupsModel->rowOf(m_graphSearchId);
        m_graphSearchId != 0 && row >= 0 && !m_lookupsModel->row(row).value(QStringLiteral("ended")).toBool())
        return;
    std::uint64_t interesting = 0, other = 0;
    for (int r = 0; r < m_lookupsModel->count() && interesting == 0; ++r) {
        const QCborMap& l = m_lookupsModel->row(r);
        if (l.value(QStringLiteral("ended")).toBool())
            continue;
        const std::uint64_t id = m_lookupsModel->idAt(r);
        if (l.value(QStringLiteral("kind")).toInteger() != 0 || l.value(QStringLiteral("store")).toBool())
            interesting = std::max(interesting, id);
        else
            other = std::max(other, id);
    }
    const std::uint64_t pick = interesting != 0 ? interesting : other;
    if (pick == 0)
        return;
    m_graphSearchId = pick;
    if (lookupTabShown())
        markSearchRow(m_lookupsModel->rowOf(pick));
    requestLookupHistory();
}

void Ed3kKadPanel::clearAll()
{
    m_contactsModel->clear();
    m_lookupsModel->clear();
    m_contactsGraph->clearSamples();
    m_networkGraph->clear();
    m_graphSearchId = 0;
    m_lookupGraph->clear();
    setLookupTabTitle({});
    setContactCount(0);
    m_searchesLabel->setText(tr("▸ Current Lookups (0)"));
    applyStatus({});
}

std::uint64_t Ed3kKadPanel::searchIdAtRow(int sourceRow) const
{
    return m_lookupsModel->idAt(sourceRow);
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

QWidget* Ed3kKadPanel::createControlsPanel()
{
    auto* widget = new QWidget;
    widget->setMinimumWidth(220);
    widget->setMaximumWidth(340);
    auto* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(4);

    // the node: ID and state, Connect / Disconnect (the ed3kEnabled switch)
    m_nodeLabel = new QLabel;
    m_nodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_stateLabel = new QLabel;
    m_stateLabel->setWordWrap(true);
    m_connectBtn = new QPushButton(tr("Connect"));
    connect(m_connectBtn, &QPushButton::clicked, this, &Ed3kKadPanel::onConnectClicked);
    auto* topRow = new QHBoxLayout;
    topRow->addWidget(m_nodeLabel, 1);
    topRow->addWidget(m_connectBtn);
    layout->addLayout(topRow);
    layout->addWidget(m_stateLabel);

    // first contact (bootstrap.md §1): an ed3k:node link, or the node lists now
    m_bootstrapGroup = new QButtonGroup(this);
    m_linkRadio = new QRadioButton(tr("ed3k:node link:"));
    m_listsRadio = new QRadioButton(tr("From sources"));
    m_listsRadio->setChecked(true);
    m_bootstrapGroup->addButton(m_linkRadio);
    m_bootstrapGroup->addButton(m_listsRadio);
    m_linkEdit = new QLineEdit;
    m_linkEdit->setPlaceholderText(QStringLiteral("ed3k:node?id=..."));
    connect(m_linkEdit, &QLineEdit::textEdited, this, [this] {
        m_linkRadio->setChecked(true);
        updateControls();
    });
    connect(m_bootstrapGroup, &QButtonGroup::buttonToggled, this, [this] { updateControls(); });
    layout->addWidget(m_linkRadio);
    layout->addWidget(m_linkEdit);
    m_bootstrapBtn = new QPushButton(tr("Bootstrap"));
    connect(m_bootstrapBtn, &QPushButton::clicked, this, &Ed3kKadPanel::onBootstrapClicked);
    auto* bsRow = new QHBoxLayout;
    bsRow->addWidget(m_listsRadio, 1);
    bsRow->addWidget(m_bootstrapBtn);
    layout->addLayout(bsRow);

    // node lists (bootstrap.md §2, §3) and the eD2K seeds (§1 source 4)
    auto* lists = new QGroupBox(tr("Node lists"));
    auto* listsLayout = new QVBoxLayout(lists);
    listsLayout->setContentsMargins(6, 4, 6, 4);
    listsLayout->setSpacing(3);
    m_sourcesLabel = new QLabel;
    m_sourcesLabel->setWordWrap(true);
    listsLayout->addWidget(m_sourcesLabel);
    auto* editSources = new QPushButton(tr("Sources..."));
    editSources->setToolTip(tr("Edit the node list sources"));
    connect(editSources, &QPushButton::clicked, this, &Ed3kKadPanel::editSourcesRequested);

    // auto fetch, max age, eD2K seeding: hidden, YAML only (defaults on, 30 days)
    m_autoFetchCheck = new QCheckBox(tr("Fetch automatically when few nodes answer"));
    m_autoFetchCheck->hide();
    connect(m_autoFetchCheck, &QCheckBox::toggled, this, [this](bool on) {
        thePrefs.setEd3kNodeListAuto(on);
        sendPref(QStringLiteral("ed3kNodeListAuto"), on);
    });
    listsLayout->addWidget(m_autoFetchCheck);

    m_maxAgeSpin = new QSpinBox;
    m_maxAgeSpin->setRange(0, 3650);
    m_maxAgeSpin->setSuffix(tr(" days"));
    m_maxAgeSpin->setSpecialValueText(tr("no limit"));
    m_maxAgeSpin->setToolTip(tr("Fetched or opened node lists older than this are refused. "
                                "The node list that comes with eMule Qt has no limit."));
    connect(m_maxAgeSpin, &QSpinBox::valueChanged, this, [this](int days) {
        thePrefs.setEd3kNodeListMaxAgeDays(days);
        sendPref(QStringLiteral("ed3kNodeListMaxAgeDays"), qint64(days));
    });
    auto* ageWidget = new QWidget;
    auto* ageRow = new QHBoxLayout(ageWidget);
    ageRow->setContentsMargins(0, 0, 0, 0);
    ageRow->addWidget(new QLabel(tr("Max age:")));
    ageRow->addWidget(m_maxAgeSpin, 1);
    ageWidget->hide();
    listsLayout->addWidget(ageWidget);

    m_seedEd2kCheck = new QCheckBox(tr("Seed from eD2K peers that speak ed3k"));
    m_seedEd2kCheck->setToolTip(tr("While ed3k Kad has fewer than 10 contacts, eD2K peers whose user hash shows "
                                   "they also run ed3k are asked for ed3k nodes."));
    connect(m_seedEd2kCheck, &QCheckBox::toggled, this, [this](bool on) {
        thePrefs.setEd3kSeedFromEd2k(on);
        sendPref(QStringLiteral("ed3kSeedFromEd2k"), on);
    });
    m_seedEd2kCheck->hide();
    listsLayout->addWidget(m_seedEd2kCheck);

    auto* openBtn = new QPushButton(tr("Open..."));
    openBtn->setToolTip(tr("Uses the nodes of an ed3k node list file (.ed3knod) someone gave you. "
                           "Open only a file of someone you trust."));
    connect(openBtn, &QPushButton::clicked, this, &Ed3kKadPanel::onOpenFileClicked);
    m_exportBtn = new QPushButton(tr("Export..."));
    m_exportBtn->setToolTip(tr("Saves the nodes this node knows as a signed node list file, "
                               "to help someone who finds no ed3k node."));
    connect(m_exportBtn, &QPushButton::clicked, this, &Ed3kKadPanel::onExportClicked);
    auto* fileRow = new QHBoxLayout;
    fileRow->addWidget(editSources);
    fileRow->addStretch();
    fileRow->addWidget(openBtn);
    fileRow->addWidget(m_exportBtn);
    listsLayout->addLayout(fileRow);
    layout->addWidget(lists);

    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);

    m_contactsGraph = new ContactsGraph;
    m_contactsGraph->setMinimumHeight(70);
    layout->addWidget(m_contactsGraph, 2);

    // distance histogram of the routing table (as eD2K)
    m_networkGraph = new KadContactHistogram;
    m_networkGraph->setMinimumHeight(50);
    layout->addWidget(m_networkGraph, 1);
    return widget;
}

void Ed3kKadPanel::requestContacts()
{
    if (!ipcReady() || !m_running)
        return;
    m_ipc->sendPoll(this, IpcMessage(IpcMsgType::GetEd3kKadContacts), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            m_contactsModel->clear();
            m_networkGraph->clear();
            setContactCount(0);
            return;
        }
        const int scroll = m_contactsView->verticalScrollBar()->value();
        const QCborArray rows = resp.fieldArray(1);
        QStringList ids;
        ids.reserve(rows.size());
        for (const auto& v : rows)
            ids.append(v.toMap().value(QStringLiteral("id")).toString());
        m_networkGraph->setIds(ids);
        m_contactsModel->setRows(rows);
        setContactCount(m_contactsModel->count());
        m_contactsView->verticalScrollBar()->setValue(scroll);
    });
}

void Ed3kKadPanel::requestLookups()
{
    if (!ipcReady() || !m_running)
        return;
    m_ipc->sendPoll(this, IpcMessage(IpcMsgType::GetEd3kKadLookups), [this](const IpcMessage& resp) {
        if (resp.type() != IpcMsgType::Result || !resp.fieldBool(0)) {
            m_lookupsModel->clear();
            m_searchesLabel->setText(tr("▸ Current Lookups (0)"));
            return;
        }
        const std::uint64_t selected = selectedSearchId();
        m_lookupsModel->setRows(resp.fieldArray(1));
        int running = 0;
        for (int r = 0; r < m_lookupsModel->count(); ++r)
            running += m_lookupsModel->row(r).value(QStringLiteral("ended")).toBool() ? 0 : 1;
        m_searchesLabel->setText(tr("▸ Current Lookups (%1)").arg(running));
        if (selected != 0)
            markSearchRow(m_lookupsModel->rowOf(selected));
        autoPickLookup();
    });
}

void Ed3kKadPanel::requestStatus()
{
    if (!ipcReady())
        return;
    m_ipc->sendPoll(this, IpcMessage(IpcMsgType::GetEd3kStatus), [this](const IpcMessage& resp) {
        if (resp.isValid() && resp.fieldBool(0))
            applyStatus(resp.fieldMap(1));
    });
}

void Ed3kKadPanel::applyStatus(const QCborMap& status)
{
    m_available = status.value(QStringLiteral("available")).toBool();
    const bool wasRunning = m_running;
    m_running = status.value(QStringLiteral("running")).toBool();
    m_contacts = status.value(QStringLiteral("contacts")).toInteger();
    if (wasRunning && !m_running) {
        m_networkGraph->clear();
        m_contactsModel->clear();
        m_lookupsModel->clear();
        setContactCount(0);
        m_graphSearchId = 0;
        m_lookupGraph->clear();
        setLookupTabTitle({});
    }
    // short: the column is narrow
    const auto className = [](qint64 c) {
        switch (c) {
        case 1: return tr("open");
        case 2: return tr("cone");
        case 3: return tr("symmetric");
        default: return QStringLiteral("?");
        }
    };
    if (!m_available) {
        m_nodeLabel->setText(tr("ed3k"));
        m_stateLabel->setText(status.isEmpty() ? QString() : tr("This core is built without ed3k."));
    } else if (!m_running) {
        m_nodeLabel->setText(tr("ed3k node"));
        m_stateLabel->setText(tr("Not running."));
    } else {
        const QString id = status.value(QStringLiteral("nodeId")).toString();
        m_nodeLabel->setText(tr("Node %1").arg(shortHex(id)));
        m_nodeLabel->setToolTip(id);
        m_stateLabel->setText(tr("UDP %1 · %2 contacts\nIPv4: %3 · IPv6: %4\nRouter: %5")
                                  .arg(status.value(QStringLiteral("udpPort")).toInteger())
                                  .arg(m_contacts)
                                  .arg(className(status.value(QStringLiteral("class4")).toInteger()),
                                       className(status.value(QStringLiteral("class6")).toInteger()),
                                       status.value(QStringLiteral("router")).toBool() ? tr("yes") : tr("no")));
        m_stateLabel->setToolTip(tr("NAT class per address family: open, cone NAT, symmetric NAT or ? (not known yet)"));
    }
    updateControls();
}

void Ed3kKadPanel::onConnectClicked()
{
    if (!ipcReady() || !m_available)
        return;
    // as the toolbar Connect: the node of this run; ed3kEnabled stays the feature switch
    const bool on = !m_running;
    if (on && !thePrefs.ed3kEnabled()) {
        thePrefs.setEd3kEnabled(true);
        sendPref(QStringLiteral("ed3kEnabled"), true);
    }
    IpcMessage req(IpcMsgType::SetEd3kConnected);
    req.append(on);
    m_ipc->sendRequest(std::move(req));
    m_connectBtn->setEnabled(false);
    QTimer::singleShot(1500, this, [this] { m_poller->nudge(); });
}

void Ed3kKadPanel::onBootstrapClicked()
{
    if (!ipcReady() || !m_running)
        return;
    const auto feedback = [self = QPointer<Ed3kKadPanel>(this)](const IpcMessage& resp) {
        if (!self)
            return;
        if (resp.fieldBool(0))
            StatusBarNotifier::post(tr("ed3k: first contact started"));
        else
            QMessageBox::warning(self, tr("ed3k Kad"), Ed3kLinkActions::replyError(resp));
    };
    if (m_linkRadio->isChecked()) {
        const QString link = m_linkEdit->text().trimmed();
        if (!link.startsWith(QLatin1String("ed3k:node"), Qt::CaseInsensitive)) {
            m_linkEdit->setFocus();
            return;
        }
        IpcMessage msg(IpcMsgType::OpenEd3kNodeLink);
        msg.append(link);
        m_ipc->sendRequest(std::move(msg), feedback);
    } else {
        m_ipc->sendRequest(IpcMessage(IpcMsgType::FetchEd3kNodeLists), feedback);
    }
}

void Ed3kKadPanel::onExportClicked()
{
    if (!ipcReady() || !m_running)
        return;
    IpcMessage msg(IpcMsgType::ExportEd3kNodeList);
    msg.append(QStringLiteral("eMule Qt"));
    m_ipc->sendRequest(std::move(msg), [self = QPointer<Ed3kKadPanel>(this)](const IpcMessage& resp) {
        if (!self)
            return;
        if (!resp.fieldBool(0)) {
            const QString why = resp.fieldMap(1).value(QStringLiteral("message")).toString();
            QMessageBox::warning(self, tr("Export Node List"), why.isEmpty() ? Ed3kLinkActions::replyError(resp) : why);
            return;
        }
        const QByteArray file = resp.fieldMap(1).value(QStringLiteral("file")).toByteArray();
        const QString path = QFileDialog::getSaveFileName(self, tr("Export Node List"), QStringLiteral("nodes.ed3knod"),
                                                          tr("ed3k node lists (*.ed3knod)"));
        if (path.isEmpty())
            return;
        QSaveFile out(path);
        if (!out.open(QIODevice::WriteOnly) || out.write(file) != file.size() || !out.commit())
            QMessageBox::warning(self, tr("Export Node List"), tr("Cannot write %1.").arg(path));
        else
            StatusBarNotifier::post(tr("ed3k node list saved to %1").arg(path));
    });
}

void Ed3kKadPanel::onOpenFileClicked()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Open ed3k Node List"), QString(),
                                                      tr("ed3k node lists (*.ed3knod);;All files (*)"));
    if (!path.isEmpty())
        Ed3kLinkActions::sendNodeListFile(this, m_ipc, path);
}

void Ed3kKadPanel::sendPref(const QString& key, const QCborValue& value)
{
    if (!ipcReady())
        return;
    IpcMessage req(IpcMsgType::SetPreferences);
    req.append(key);
    req.append(value);
    m_ipc->sendRequest(std::move(req));
}

void Ed3kKadPanel::updateControls()
{
    if (!m_connectBtn)
        return;
    m_connectBtn->setEnabled(m_available && ipcReady());
    m_connectBtn->setText(m_running ? tr("Disconnect") : tr("Connect"));
    const bool linkOk = !m_linkRadio->isChecked() || !m_linkEdit->text().trimmed().isEmpty();
    const bool listsOk = !m_listsRadio->isChecked() || !thePrefs.ed3kNodeLists().isEmpty();
    m_bootstrapBtn->setEnabled(m_running && linkOk && listsOk);
    m_exportBtn->setEnabled(m_running && m_contacts > 0);
}

} // namespace eMule
