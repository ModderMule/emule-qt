/// @file Ed3kOptionsPages.cpp
/// @brief The ed3k group of the Options dialog — implementation.

#include "pch.h"
#include "dialogs/Ed3kOptionsPages.h"

#include "app/Ed3kNameOpener.h"
#include "app/IpcClient.h"
#include "dialogs/Ed3kFilterPage.h"
#include "dialogs/Ed3kStakeGroup.h"
#include "dialogs/PortTest.h"
#include "dialogs/RouterPage.h"
#include "prefs/Preferences.h"
#include "IpcMessage.h"

#include <QCborArray>
#include <QCheckBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

constexpr double kMicro = 1'000'000.0;   // µ per credit

/// One entry per non-empty line.
QStringList linesOf(const QPlainTextEdit* edit)
{
    QStringList out;
    for (const QString& line : edit->toPlainText().split(QLatin1Char('\n'))) {
        if (const QString t = line.trimmed(); !t.isEmpty())
            out << t;
    }
    return out;
}

/// Exactly one http(s) endpoint and it is no node on this computer (Ed3kChain::singleRemote's
/// reading for what is typed: §4.3 wants two independent providers to agree).
bool singleRemoteEndpoint(const QStringList& urls)
{
    const auto local = [](const QUrl& url) {
        const QHostAddress ip(url.host());
        return url.host().compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0 || (!ip.isNull() && ip.isLoopback());
    };
    // usable: https, or plain http to this computer (as the daemon takes them)
    int usable = 0;
    int remote = 0;
    for (const QString& u : urls) {
        const QUrl url(u, QUrl::StrictMode);
        const QString scheme = url.scheme().toLower();
        if (!url.isValid() || url.host().isEmpty())
            continue;
        if (scheme == QLatin1String("https") || (scheme == QLatin1String("http") && local(url))) {
            ++usable;
            remote += local(url) ? 0 : 1;
        }
    }
    return usable == 1 && remote == 1;
}

/// "a b c" (IPC form) → one per line.
QString linesText(const QCborValue& v)
{
    return Preferences::rpcUrlList(v).join(QLatin1Char('\n'));
}

qint64 microOf(const QDoubleSpinBox* spin)
{
    return qint64(std::llround(spin->value() * kMicro));
}

/// A word-wrapped note in the page's text colour.
QLabel* noteLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

QTreeWidget* makeTree(const QStringList& headers, QWidget* parent)
{
    auto* t = new QTreeWidget(parent);
    t->setHeaderLabels(headers);
    t->setRootIsDecorated(false);
    t->setUniformRowHeights(true);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->header()->setStretchLastSection(true);
    // a few rows each: the three lists fit the page together
    t->setFixedHeight(t->fontMetrics().lineSpacing() * 7);
    return t;
}

/// Overwrites a buffer that held key bytes.
void scrub(QByteArray& b)
{
    b.fill('\0');
    b.clear();
}

/// uint256 big-endian hex → an approximate double.
double uint256Value(const QString& hex)
{
    double v = 0;
    for (const QChar c : hex)
        v = v * 16 + QString(c).toInt(nullptr, 16);
    return v;
}

} // namespace

Ed3kOptionsPages::Ed3kOptionsPages(IpcClient* ipc, QWidget* parent)
    : QObject(parent)
    , m_ipc(ipc)
    , m_parent(parent)
{
    m_general = buildGeneral(parent);
    m_connection = buildConnection(parent);
    m_credits = buildCredits(parent);
    m_autoCredits = buildAutoCredits(parent);
    m_names = buildNames(parent);
    m_filters = new Ed3kFilterPage(ipc, parent);   // acts at once over IPC (filter-list.md)
    m_hoster = buildHoster(parent);
    if (m_ipc) {
        connect(m_ipc, &IpcClient::hosterAccountChecked, this, [this](const IpcMessage& msg) {
            const QString host = msg.fieldString(0);
            const QString user = msg.fieldString(1);
            const QCborMap r = msg.fieldMap(2);
            for (HosterAccountRow& a : m_accounts) {
                if (a.host != host || a.user != user)
                    continue;
                if (!r.value(QStringLiteral("valid")).toBool())
                    a.status = tr("Not valid");
                else if (const qint64 exp = r.value(QStringLiteral("expires")).toInteger(); exp > 0)
                    a.status = (r.value(QStringLiteral("premium")).toBool() ? tr("Premium until %1") : tr("Free until %1"))
                                   .arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(exp).date(), QLocale::ShortFormat));
                else
                    a.status = r.value(QStringLiteral("premium")).toBool() ? tr("Premium") : tr("Free");
            }
            fillAccounts();
        });
        connect(m_ipc, &IpcClient::ed3kStatusChanged, this,
                [this](const IpcMessage& msg) { applyStatus(msg.fieldMap(0)); });
        connect(m_ipc, &IpcClient::ed3kNamesChanged, this, [this](const IpcMessage&) { refreshNames(); });
    }
    requestStatus();
    refreshNames();
}

Ed3kOptionsPages::~Ed3kOptionsPages() = default;

void Ed3kOptionsPages::load(const QCborMap& p)
{
    m_loading = true;
    m_enabledCheck->setChecked(p.value(QStringLiteral("ed3kEnabled")).toBool(true));
    m_shareFilesCheck->setChecked(p.value(QStringLiteral("ed3kShareFiles")).toBool(true));
    m_jointDownloadsCheck->setChecked(p.value(QStringLiteral("ed3kJointDownloads")).toBool(true));
    m_holdBackCheck->setChecked(p.value(QStringLiteral("ed3kHoldBackPosts")).toBool(false));
    const QStringList off = Preferences::rpcUrlList(p.value(QStringLiteral("ed3kExtOff")));
    for (QCheckBox* box : m_extChecks)
        box->setChecked(!off.contains(box->text()));

    m_udpPortSpin->setValue(int(p.value(QStringLiteral("ed3kUdpPort")).toInteger(0)));
    // eD2K TCP, eD2K UDP and ed3k on one number: one port, set on the Connection page
    {
        const qint64 tcp = p.value(QStringLiteral("port")).toInteger(0);
        m_joinedPort = int(tcp);
        m_portJoined = tcp != 0 && p.value(QStringLiteral("udpPort")).toInteger(0) == tcp
                       && p.value(QStringLiteral("ed3kUdpPort")).toInteger(0) == tcp;
        showPortJoined();
        requestCompatPlan();
    }
    m_lanCheck->setChecked(p.value(QStringLiteral("ed3kLan")).toBool(true));
    m_sitePortSpin->setValue(int(p.value(QStringLiteral("ed3kSitePort")).toInteger(0)));
    m_nodeListsEdit->setPlainText(linesText(p.value(QStringLiteral("ed3kNodeLists"))));
    m_chaintubeRpcEdit->setPlainText(linesText(p.value(QStringLiteral("ed3kChaintubeRpc"))));
    updateChainNote();
    // ToDo: decide whether the user may edit the chaintube chain in the UI (see buildConnection)
    // {
    //     Ed3kChainPrefs ch;
    //     for (auto it = p.cbegin(); it != p.cend(); ++it)
    //         Preferences::applyEd3kChainKey(ch, it.key().toString(), it.value());
    //     m_ctChainIdEdit->setText(ch.chainId == 0 ? QString() : QString::number(ch.chainId));
    //     m_ctContentEdit->setText(ch.content);
    //     m_ctCommentEdit->setText(ch.comment);
    //     m_ctVoteEdit->setText(ch.vote);
    //     m_ctForwarderEdit->setText(ch.forwarder);
    // }

    Ed3kCreditPrefs c;
    for (auto it = p.cbegin(); it != p.cend(); ++it)
        Preferences::applyEd3kCreditKey(c, it.key().toString(), it.value());
    m_creditEnabledCheck->setChecked(c.enabled);
    m_chainIdEdit->setText(c.chainId == 0 ? QString() : QString::number(c.chainId));
    m_contractEdit->setText(c.contract);
    m_creditRpcEdit->setPlainText(linesText(p.value(QStringLiteral("ed3kCreditRpc"))));
    m_quorumSpin->setValue(int(c.quorum));
    m_bidSpin->setValue(double(c.bidMaxPrice) / kMicro);
    m_sellCheck->setChecked(c.sell);
    m_sellFloorSpin->setValue(double(c.sellFloor) / kMicro);
    m_sellSlotsSpin->setValue(int(c.sellSlots));
    m_ledgerGroup->setEnabled(c.enabled);
    m_autoBidGroup->setChecked(c.autoBid);
    m_autoFactorSpin->setValue(c.autoFactor / 10.0);
    m_autoThresholdSpin->setValue(int(std::min<uint64>(c.autoThreshold, uint64(m_autoThresholdSpin->maximum()))));
    updateAutoCredits();

    Ed3kHosterPrefs h;
    for (auto it = p.cbegin(); it != p.cend(); ++it)
        Preferences::applyEd3kHosterKey(h, it.key().toString(), it.value());
    m_hosterEnabledCheck->setChecked(h.enabled);
    updateJavaButtons();
    updateHosterNodeHint();
    m_hosterStreamsSpin->setValue(int(h.maxStreams));
    m_hosterPuzzlesCheck->setChecked(h.askPuzzles);
    m_hosterPeersCheck->setChecked(h.fromPeers);
    m_hosterCheckLinksCheck->setChecked(h.checkSearchLinks);
    m_publishLinksCheck->setChecked(h.publishLinks);
    m_javaEdit->setText(h.java);
    m_autoJavaCheck->setChecked(h.autoJava);
    m_accounts.clear();
    for (const Ed3kHosterAccount& a : h.accounts)
        m_accounts.append({a.host, a.user, {}, false, {}});
    fillAccounts();
    m_loading = false;
}

void Ed3kOptionsPages::appendTo(IpcMessage& req) const
{
    const auto put = [&req](const char* key, const QCborValue& v) {
        req.append(QString::fromLatin1(key));
        req.append(v);
    };
    put("ed3kEnabled", m_enabledCheck->isChecked());
    put("ed3kShareFiles", m_shareFilesCheck->isChecked());
    put("ed3kJointDownloads", m_jointDownloadsCheck->isChecked());
    put("ed3kHoldBackPosts", m_holdBackCheck->isChecked());
    put("ed3kExtOff", extOff().join(QLatin1Char(' ')));
    if (!m_portJoined)   // joined: the Connection page sends it
        put("ed3kUdpPort", qint64(m_udpPortSpin->value()));
    put("ed3kLan", m_lanCheck->isChecked());
    put("ed3kSitePort", qint64(m_sitePortSpin->value()));
    put("ed3kNodeLists", linesOf(m_nodeListsEdit).join(QLatin1Char(' ')));
    put("ed3kChaintubeRpc", linesOf(m_chaintubeRpcEdit).join(QLatin1Char(' ')));
    // ToDo: chaintube chain not in the UI for now: the daemon keeps what it has
    // {
    //     const Ed3kChainPrefs ch = chainPrefs();
    //     put("ed3kChainId", QString::number(ch.chainId));   // a u64: as text
    //     put("ed3kChainContent", ch.content);
    //     put("ed3kChainComment", ch.comment);
    //     put("ed3kChainVote", ch.vote);
    //     put("ed3kChainForwarder", ch.forwarder);
    // }
    put("ed3kCreditRpc", linesOf(m_creditRpcEdit).join(QLatin1Char(' ')));
    const Ed3kCreditPrefs c = creditPrefs();
    put("ed3kCreditEnabled", c.enabled);
    put("ed3kCreditChainId", QString::number(c.chainId));   // a u64: as text
    put("ed3kCreditContract", c.contract);
    put("ed3kCreditQuorum", qint64(c.quorum));
    put("ed3kCreditBidMaxPrice", qint64(c.bidMaxPrice));
    put("ed3kCreditSell", c.sell);
    put("ed3kCreditSellFloor", qint64(c.sellFloor));
    put("ed3kCreditSellSlots", qint64(c.sellSlots));
    put("ed3kCreditAutoBid", c.autoBid);
    put("ed3kCreditAutoFactor", c.autoFactor / 10.0);
    put("ed3kCreditAutoThreshold", qint64(c.autoThreshold));
    const Ed3kHosterPrefs h = hosterPrefs();
    put("ed3kHosterEnabled", h.enabled);
    put("ed3kHosterJava", h.java);
    put("ed3kHosterAutoJava", h.autoJava);
    put("ed3kHosterMaxStreams", qint64(h.maxStreams));
    put("ed3kHosterPuzzles", h.askPuzzles);
    put("ed3kHosterPublishLinks", h.publishLinks);
    put("ed3kHosterFromPeers", h.fromPeers);
    put("ed3kHosterCheckSearchLinks", h.checkSearchLinks);
    // a password only when typed here: the daemon keeps the stored one otherwise
    QCborArray accounts;
    for (const HosterAccountRow& a : m_accounts) {
        QCborMap m{{QStringLiteral("host"), a.host}, {QStringLiteral("user"), a.user}};
        if (a.passwordSet)
            m.insert(QStringLiteral("password"), a.password);
        accounts.append(m);
    }
    put("ed3kHosterAccounts", accounts);
}

void Ed3kOptionsPages::saveLocal() const
{
    thePrefs.setEd3kEnabled(m_enabledCheck->isChecked());
    thePrefs.setEd3kShareFiles(m_shareFilesCheck->isChecked());
    thePrefs.setEd3kJointDownloads(m_jointDownloadsCheck->isChecked());
    thePrefs.setEd3kHoldBackPosts(m_holdBackCheck->isChecked());
    thePrefs.setEd3kExtOff(extOff());
    if (!m_portJoined && m_udpPortSpin->value() > 0)
        thePrefs.setEd3kUdpPort(uint16(m_udpPortSpin->value()));
    thePrefs.setEd3kLan(m_lanCheck->isChecked());
    if (m_sitePortSpin->value() > 0)
        thePrefs.setEd3kSitePort(uint16(m_sitePortSpin->value()));
    thePrefs.setEd3kNodeLists(linesOf(m_nodeListsEdit));
    thePrefs.setEd3kChaintubeRpc(linesOf(m_chaintubeRpcEdit));
    // thePrefs.setEd3kChain(chainPrefs());   // ToDo: chaintube chain not in the UI for now
    thePrefs.setEd3kCreditRpc(linesOf(m_creditRpcEdit));
    thePrefs.setEd3kCredit(creditPrefs());
    Ed3kHosterPrefs h = hosterPrefs();
    // without a daemon the stored passwords are here: keep those not typed again
    const Ed3kHosterPrefs old = thePrefs.ed3kHoster();
    for (Ed3kHosterAccount& a : h.accounts)
        if (a.password.isEmpty())
            for (const Ed3kHosterAccount& o : old.accounts)
                if (o.host == a.host && o.user == a.user)
                    a.password = o.password;
    thePrefs.setEd3kHoster(h);
}

bool Ed3kOptionsPages::validate()
{
    const QString contract = m_contractEdit->text().trimmed();
    static const QRegularExpression kAddress(QStringLiteral("^0[xX][0-9a-fA-F]{40}$"));
    if (!contract.isEmpty() && !kAddress.match(contract).hasMatch()) {
        emit showPage(m_credits);
        m_contractEdit->setFocus();
        QMessageBox::warning(m_parent, tr("ed3k credits"),
                             tr("The ledger contract is an address: 0x and 40 hexadecimal digits."));
        return false;
    }
    bool ok = true;
    const QString chain = m_chainIdEdit->text().trimmed();
    if (!chain.isEmpty())
        chain.toULongLong(&ok);
    if (!ok) {
        emit showPage(m_credits);
        m_chainIdEdit->setFocus();
        QMessageBox::warning(m_parent, tr("ed3k credits"), tr("The chain ID is a whole number."));
        return false;
    }
    // chaintube (chaintube.md §4.2): each contract empty or an address
    // ToDo: back when the chain is editable in the UI again (see buildConnection)
    // for (QLineEdit* e : {m_ctContentEdit, m_ctCommentEdit, m_ctVoteEdit, m_ctForwarderEdit}) {
    //     if (const QString t = e->text().trimmed(); !t.isEmpty() && !kAddress.match(t).hasMatch()) {
    //         emit showPage(m_connection);
    //         e->setFocus();
    //         QMessageBox::warning(m_parent, tr("Chaintube"), tr("A contract is an address: 0x and 40 hexadecimal digits."));
    //         return false;
    //     }
    // }
    return true;
}

QWidget* Ed3kOptionsPages::filtersPage() const
{
    return m_filters;
}

QWidget* Ed3kOptionsPages::buildGeneral(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    m_enabledCheck = new QCheckBox(tr("Enable the ed3k node"), page);
    m_enabledCheck->setToolTip(tr("Runs the ed3k node (packmule) in the core, on the eD2K UDP port or one of its own. "
                                  "It finds other nodes over Kad and serves the shared files."));
    layout->addWidget(m_enabledCheck);
    m_runningLabel = noteLabel(QString(), page);
    layout->addWidget(m_runningLabel);

    auto* shareGroup = new QGroupBox(tr("Sharing"), page);
    auto* shareLayout = new QVBoxLayout(shareGroup);
    m_shareFilesCheck = new QCheckBox(tr("Share the shared files over ed3k too"), shareGroup);
    shareLayout->addWidget(m_shareFilesCheck);
    m_holdBackCheck = new QCheckBox(tr("Hold Usenet posts back"), shareGroup);
    m_holdBackCheck->setToolTip(tr("Share the files of Usenet downloads, but not their article lists, post sets or nzb records."));
    shareLayout->addWidget(m_holdBackCheck);
    layout->addWidget(shareGroup);

    // eD2K compat (Protocol.md §14): derived user hash, one port for eD2K and ed3k
    auto* compatGroup = new QGroupBox(tr("eD2K compatibility"), page);
    auto* compatOuter = new QVBoxLayout(compatGroup);
    auto* compatLayout = new QHBoxLayout;
    compatOuter->addLayout(compatLayout);
    m_compatLabel = noteLabel(tr("One port for eD2K and ed3k, and a user hash that tells eD2K peers this client "
                                 "also speaks ed3k."), compatGroup);
    compatLayout->addWidget(m_compatLabel, 1);
    m_compatBtn = new QPushButton(tr("Migrate..."), compatGroup);
    m_compatBtn->setToolTip(tr("Shows the changes for this configuration and asks before making them."));
    connect(m_compatBtn, &QPushButton::clicked, this, &Ed3kOptionsPages::migrateEd2kCompat);
    compatLayout->addWidget(m_compatBtn, 0, Qt::AlignTop);
    // §14.4: one download from both networks
    m_jointDownloadsCheck = new QCheckBox(tr("Download eD2K files from ed3k peers too"), compatGroup);
    m_jointDownloadsCheck->setToolTip(tr("Finds the ed3k copy of each eD2K download by its eD2K hash and fetches the "
                                         "missing parts there too. A source that also speaks ed3k is used over ed3k."));
    compatOuter->addWidget(m_jointDownloadsCheck);
    layout->addWidget(compatGroup);

    // ToDo: decide which extensions the user can switch. Until then none are shown:
    // m_extChecks stays empty, so extOff() is empty and every extension is on
    // auto* extGroup = new QGroupBox(tr("Extensions"), page);
    // auto* extLayout = new QVBoxLayout(extGroup);
    // extLayout->addWidget(noteLabel(tr("Optional parts of the ed3k protocol. A change restarts the node."), extGroup));
    // // one box per extension, three columns (no list: fits the page without scrolling)
    // auto* grid = new QGridLayout;
    // const QStringList names = Preferences::ed3kExtensionNames();
    // const int rows = int((names.size() + 2) / 3);
    // for (int n = 0; n < names.size(); ++n) {
    //     auto* box = new QCheckBox(names.at(n), extGroup);
    //     m_extChecks.append(box);
    //     grid->addWidget(box, n % rows, n / rows);
    // }
    // extLayout->addLayout(grid);
    // layout->addWidget(extGroup);
    layout->addStretch();

    for (QCheckBox* box : {m_enabledCheck, m_shareFilesCheck, m_holdBackCheck, m_jointDownloadsCheck})
        connect(box, &QCheckBox::toggled, this, [this] {
            if (!m_loading)
                emit changed();
        });
    for (QCheckBox* box : m_extChecks)
        connect(box, &QCheckBox::toggled, this, [this] {
            if (!m_loading)
                emit changed();
        });
    return page;
}

QWidget* Ed3kOptionsPages::buildConnection(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* portGroup = new QGroupBox(tr("Ports"), page);
    auto* portForm = new QFormLayout(portGroup);
    m_udpPortSpin = new QSpinBox(portGroup);
    m_udpPortSpin->setRange(0, 65535);
    m_udpPortSpin->setSpecialValueText(tr("Random"));
    m_udpPortSpin->setToolTip(tr("UDP port of the ed3k node. On the eD2K UDP port it shares that socket. A change restarts the node."));
    auto* udpRow = new QHBoxLayout;
    udpRow->addWidget(m_udpPortSpin);
    m_udpPortJoinedLabel = new QLabel(portGroup);
    m_udpPortJoinedLabel->setToolTip(tr("ed3k shares the eD2K TCP/UDP port; change it on the Connection page."));
    m_udpPortJoinedLabel->setVisible(false);
    udpRow->addWidget(m_udpPortJoinedLabel);
    auto* testBtn = new QPushButton(tr("Test Ports"), portGroup);
    testBtn->setToolTip(tr("Asks emule-qt.org whether the UDP port the node uses now is reachable. ed3k needs only "
                           "UDP. Apply a changed port first."));
    connect(testBtn, &QPushButton::clicked, this, [this, testBtn] {
        PortTest::NetworkTest::run(m_ipc, m_parent, testBtn, PortTest::Network::Ed3k);
    });
    udpRow->addWidget(testBtn);
    auto* routerBtn = new QPushButton(tr("Open Router"), portGroup);
    routerBtn->setToolTip(tr("Opens the web page of this computer's router, where ports are forwarded"));
    connect(routerBtn, &QPushButton::clicked, this, [this] { RouterPage::open(m_parent); });
    udpRow->addWidget(routerBtn);
    udpRow->addStretch();
    portForm->addRow(tr("UDP port:"), udpRow);
    m_sitePortSpin = new QSpinBox(portGroup);
    m_sitePortSpin->setRange(0, 65535);
    m_sitePortSpin->setSpecialValueText(tr("Random"));
    m_sitePortSpin->setToolTip(tr("TCP port of the local website server (this computer only). It is part of every "
                                  "site's address: a change loses what sites stored in the browser."));
    portForm->addRow(tr("Website server port:"), m_sitePortSpin);
    layout->addWidget(portGroup);

    auto* findGroup = new QGroupBox(tr("Finding nodes"), page);
    auto* findLayout = new QVBoxLayout(findGroup);
    m_lanCheck = new QCheckBox(tr("Find ed3k nodes in the local network (restarts the node)"), findGroup);
    findLayout->addWidget(m_lanCheck);
    findLayout->addWidget(noteLabel(tr("Node lists for the first contact, one ed3k:nodes link per line:"), findGroup));
    m_nodeListsEdit = new QPlainTextEdit(findGroup);
    m_nodeListsEdit->setPlaceholderText(QStringLiteral("ed3k:nodes?url=…&key=…"));
    m_nodeListsEdit->setFixedHeight(m_nodeListsEdit->fontMetrics().lineSpacing() * 5 + 12);
    findLayout->addWidget(m_nodeListsEdit);
    layout->addWidget(findGroup);

    auto* chainGroup = new QGroupBox(tr("Chaintube"), page);
    auto* chainLayout = new QVBoxLayout(chainGroup);
    chainLayout->addWidget(noteLabel(tr("JSON-RPC endpoints of the chaintube chain, one URL per line (https, or http "
                                        "to a node on this computer)."),
                                     chainGroup));
    m_chaintubeRpcEdit = new QPlainTextEdit(chainGroup);
    m_chaintubeRpcEdit->setPlaceholderText(QStringLiteral("https://…"));
    m_chaintubeRpcEdit->setFixedHeight(m_chaintubeRpcEdit->fontMetrics().lineSpacing() * 4 + 12);
    chainLayout->addWidget(m_chaintubeRpcEdit);
    m_chainSingleNote = noteLabel(tr("Only one remote endpoint: nothing checks its answers. Two endpoints of independent "
                                     "providers are recommended, or a chain node on this computer."),
                                  chainGroup);
    m_chainSingleNote->setVisible(false);
    chainLayout->addWidget(m_chainSingleNote);
    m_chainRefusedLabel = noteLabel({}, chainGroup);
    m_chainRefusedLabel->setTextFormat(Qt::PlainText);
    m_chainRefusedLabel->setVisible(false);
    chainLayout->addWidget(m_chainRefusedLabel);
    connect(m_chaintubeRpcEdit, &QPlainTextEdit::textChanged, this, &Ed3kOptionsPages::updateChainNote);
    // ToDo: decide whether the user may edit the chaintube chain in the UI; only the JSON-RPC
    // endpoints for now, the daemon keeps the rest (load/appendTo/saveLocal/validate likewise)
    // // the network's parameters (§4.2): no defaults; a test deployment is named in the spec
    // auto* chainForm = new QFormLayout;
    // chainForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    // m_ctChainIdEdit = new QLineEdit(chainGroup);
    // m_ctChainIdEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,20}")), m_ctChainIdEdit));
    // m_ctChainIdEdit->setPlaceholderText(tr("assigned at deployment"));
    // chainForm->addRow(tr("Chain ID:"), m_ctChainIdEdit);
    // const auto addressEdit = [chainGroup](const QString& tip) {
    //     auto* e = new QLineEdit(chainGroup);
    //     e->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("(0[xX][0-9a-fA-F]{0,40})?")), e));
    //     e->setPlaceholderText(QStringLiteral("0x…"));
    //     e->setToolTip(tip);
    //     return e;
    // };
    // m_ctContentEdit = addressEdit(tr("The content contract of the network. No default."));
    // m_ctCommentEdit = addressEdit(tr("The comment contract of the network. No default."));
    // m_ctVoteEdit = addressEdit(tr("The vote contract of the network. No default."));
    // m_ctForwarderEdit = addressEdit(tr("The forwarder of requests a site sends; empty: none."));
    // chainForm->addRow(tr("Content contract:"), m_ctContentEdit);
    // chainForm->addRow(tr("Comment contract:"), m_ctCommentEdit);
    // chainForm->addRow(tr("Vote contract:"), m_ctVoteEdit);
    // chainForm->addRow(tr("Forwarder:"), m_ctForwarderEdit);
    // chainLayout->addLayout(chainForm);
    // for (QLineEdit* e : {m_ctChainIdEdit, m_ctContentEdit, m_ctCommentEdit, m_ctVoteEdit, m_ctForwarderEdit})
    //     connect(e, &QLineEdit::textEdited, this, [this] { emit changed(); });
    layout->addWidget(chainGroup);
    layout->addStretch();

    for (QSpinBox* s : {m_udpPortSpin, m_sitePortSpin})
        connect(s, &QSpinBox::valueChanged, this, [this] {
            if (!m_loading)
                emit changed();
        });
    connect(m_lanCheck, &QCheckBox::toggled, this, [this] {
        if (!m_loading)
            emit changed();
    });
    for (QPlainTextEdit* e : {m_nodeListsEdit, m_chaintubeRpcEdit})
        connect(e, &QPlainTextEdit::textChanged, this, [this] {
            if (!m_loading)
                emit changed();
        });
    return page;
}

QWidget* Ed3kOptionsPages::buildCredits(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);

    // credit.md §9: off by default, and the user is told when switching on
    m_creditEnabledCheck = new QCheckBox(tr("Use credits (off by default)"), page);
    m_creditEnabledCheck->setToolTip(tr("Earn credits for uploading, and spend them on priority upload slots at others."));
    layout->addWidget(m_creditEnabledCheck);

    m_ledgerGroup = new QGroupBox(tr("Ledger of the network"), page);
    auto* ledgerForm = new QFormLayout(m_ledgerGroup);
    // macOS keeps fields at their hint: the contract (42 characters) wants the room
    ledgerForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    m_chainIdEdit = new QLineEdit(m_ledgerGroup);
    m_chainIdEdit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[0-9]{0,20}")), m_chainIdEdit));
    m_chainIdEdit->setPlaceholderText(tr("assigned at deployment"));
    m_chainIdEdit->setToolTip(tr("There is no default: take it from the network you are in (credit.md §7.1)."));
    ledgerForm->addRow(tr("Chain ID:"), m_chainIdEdit);
    m_contractEdit = new QLineEdit(m_ledgerGroup);
    m_contractEdit->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("(0[xX][0-9a-fA-F]{0,40})?")), m_contractEdit));
    m_contractEdit->setPlaceholderText(QStringLiteral("0x…"));
    m_contractEdit->setToolTip(tr("The ledger contract of the network: 0x and 40 hexadecimal digits. No default."));
    ledgerForm->addRow(tr("Ledger contract:"), m_contractEdit);
    m_creditRpcEdit = new QPlainTextEdit(m_ledgerGroup);
    m_creditRpcEdit->setPlaceholderText(QStringLiteral("https://<host>/ext/bc/<blockchain ID>/rpc"));
    m_creditRpcEdit->setToolTip(tr("JSON-RPC endpoints of the ledger, one URL per line, best of different operators. "
                                   "They go through the proxy you configured. Without one, receipts are signed and kept, "
                                   "nothing is sold or settled."));
    m_creditRpcEdit->setFixedHeight(m_creditRpcEdit->fontMetrics().lineSpacing() * 4 + 12);
    ledgerForm->addRow(tr("RPC endpoints:"), m_creditRpcEdit);
    m_quorumSpin = new QSpinBox(m_ledgerGroup);
    m_quorumSpin->setRange(1, 16);
    m_quorumSpin->setToolTip(tr("How many endpoints must agree on a read the node acts on (credit.md §7.3: two)."));
    ledgerForm->addRow(tr("Endpoints that must agree:"), m_quorumSpin);

    m_bidSpin = new QDoubleSpinBox(m_ledgerGroup);
    m_bidSpin->setDecimals(6);
    m_bidSpin->setRange(0, 1'000'000);
    m_bidSpin->setSuffix(tr(" credits/MiB"));
    m_bidSpin->setSpecialValueText(tr("Never pay (receipts only)"));
    m_bidSpin->setMinimumWidth(m_bidSpin->fontMetrics().horizontalAdvance(m_bidSpin->specialValueText()) + 44);
    m_bidSpin->setToolTip(tr("The most paid for a priority slot per MiB, for every ed3k download."));
    ledgerForm->addRow(tr("Bid at most:"), m_bidSpin);
    m_sellCheck = new QCheckBox(tr("Sell priority upload slots"), m_ledgerGroup);
    ledgerForm->addRow(QString(), m_sellCheck);
    m_sellFloorSpin = new QDoubleSpinBox(m_ledgerGroup);
    m_sellFloorSpin->setDecimals(6);
    m_sellFloorSpin->setRange(0, 1'000'000);
    m_sellFloorSpin->setSuffix(tr(" credits/MiB"));
    ledgerForm->addRow(tr("Lowest bid taken:"), m_sellFloorSpin);
    m_sellSlotsSpin = new QSpinBox(m_ledgerGroup);
    m_sellSlotsSpin->setRange(0, 1000);
    m_sellSlotsSpin->setSpecialValueText(tr("Those of the upload queue"));
    m_sellSlotsSpin->setMinimumWidth(m_sellSlotsSpin->fontMetrics().horizontalAdvance(m_sellSlotsSpin->specialValueText()) + 44);
    m_sellSlotsSpin->setToolTip(tr("Upload slots the priority lane counts; at most two thirds go to bidders."));
    ledgerForm->addRow(tr("Upload slots:"), m_sellSlotsSpin);
    layout->addWidget(m_ledgerGroup);

    auto* keyGroup = new QGroupBox(tr("Credit key"), page);
    auto* keyLayout = new QVBoxLayout(keyGroup);
    m_addressLabel = noteLabel(QString(), keyGroup);
    keyLayout->addWidget(m_addressLabel);
    auto* keyButtons = new QHBoxLayout;
    m_newKeyBtn = new QPushButton(tr("New key…"), keyGroup);
    m_importKeyBtn = new QPushButton(tr("Import…"), keyGroup);
    m_exportKeyBtn = new QPushButton(tr("Export…"), keyGroup);
    keyButtons->addWidget(m_newKeyBtn);
    keyButtons->addWidget(m_importKeyBtn);
    keyButtons->addWidget(m_exportKeyBtn);
    keyButtons->addStretch();
    keyLayout->addLayout(keyButtons);
    if (!keyActionsAllowed()) {
        for (QPushButton* b : {m_newKeyBtn, m_importKeyBtn, m_exportKeyBtn}) {
            b->setEnabled(false);
            b->setToolTip(tr("Only from a GUI on the computer of the core: the key is a secret."));
        }
    }
    layout->addWidget(keyGroup);

    auto* accountGroup = new QGroupBox(tr("Account"), page);
    auto* accountLayout = new QVBoxLayout(accountGroup);
    m_accountLabel = noteLabel(tr("Not known while credits are off."), accountGroup);
    accountLayout->addWidget(m_accountLabel);
    layout->addWidget(accountGroup);

    // stake tokens (credit.md §6.7): burns and claims, at once over IPC
    layout->addWidget(new Ed3kStakeGroup(m_ipc, page));

    // also shown and asked when the switch is ticked (confirmCredits)
    auto* notes = new QGroupBox(tr("What switching credits on means"), page);
    auto* notesLayout = new QVBoxLayout(notes);
    auto* notesLabel = noteLabel(notesText(), notes);
    notesLabel->setTextFormat(Qt::RichText);
    notesLayout->addWidget(notesLabel);
    layout->addWidget(notes);
    layout->addStretch();

    connect(m_creditEnabledCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        if (on && !confirmCredits()) {
            const QSignalBlocker block(m_creditEnabledCheck);
            m_creditEnabledCheck->setChecked(false);
            return;
        }
        m_ledgerGroup->setEnabled(on);
        emit changed();
    });
    for (QLineEdit* e : {m_chainIdEdit, m_contractEdit})
        connect(e, &QLineEdit::textEdited, this, [this] { emit changed(); });
    connect(m_creditRpcEdit, &QPlainTextEdit::textChanged, this, [this] {
        if (!m_loading)
            emit changed();
    });
    for (QSpinBox* s : {m_quorumSpin, m_sellSlotsSpin})
        connect(s, &QSpinBox::valueChanged, this, [this] {
            if (!m_loading)
                emit changed();
        });
    for (QDoubleSpinBox* s : {m_bidSpin, m_sellFloorSpin})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this] {
            if (!m_loading)
                emit changed();
        });
    connect(m_sellCheck, &QCheckBox::toggled, this, [this] {
        if (!m_loading)
            emit changed();
    });
    connect(m_newKeyBtn, &QPushButton::clicked, this, &Ed3kOptionsPages::newKey);
    connect(m_importKeyBtn, &QPushButton::clicked, this, &Ed3kOptionsPages::importKey);
    connect(m_exportKeyBtn, &QPushButton::clicked, this, &Ed3kOptionsPages::exportKey);
    return page;
}

QWidget* Ed3kOptionsPages::buildNames(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    m_namesNote = noteLabel(tr("Changes on this page take effect at once."), page);
    layout->addWidget(m_namesNote);

    const auto buttonRow = [](QGroupBox* g, std::initializer_list<QPushButton*> buttons) {
        auto* row = new QHBoxLayout;
        for (QPushButton* b : buttons)
            row->addWidget(b);
        row->addStretch();
        static_cast<QVBoxLayout*>(g->layout())->addLayout(row);
    };
    const auto selected = [](QTreeWidget* t) -> QTreeWidgetItem* {
        const auto items = t->selectedItems();
        return items.isEmpty() ? nullptr : items.first();
    };

    // -- Pins (name.md §4.4): only the user changes them
    auto* pinGroup = new QGroupBox(tr("Pinned names"), page);
    new QVBoxLayout(pinGroup);
    m_pinsTree = makeTree({tr("Name"), tr("Key"), tr("How"), tr("Vouched")}, pinGroup);
    pinGroup->layout()->addWidget(m_pinsTree);
    auto* unpin = new QPushButton(tr("Unpin"), pinGroup);
    auto* vouch = new QPushButton(tr("Vouch / Withdraw"), pinGroup);
    vouch->setToolTip(tr("Vouching publishes a claim for the name and answers friends with it."));
    buttonRow(pinGroup, {unpin, vouch});
    layout->addWidget(pinGroup);
    connect(unpin, &QPushButton::clicked, this, [this, selected] {
        if (auto* it = selected(m_pinsTree); it && m_ipc)
            m_ipc->unpinEd3kName(it->text(0), [this](const IpcMessage& r) { failed(r, tr("Unpin")); });
    });
    connect(vouch, &QPushButton::clicked, this, [this, selected] {
        if (auto* it = selected(m_pinsTree); it && m_ipc)
            m_ipc->vouchEd3kName(it->text(0), !it->data(3, Qt::UserRole).toBool(),
                                 [this](const IpcMessage& r) { failed(r, tr("Vouch")); });
    });

    // -- Petnames
    auto* petGroup = new QGroupBox(tr("Petnames"), page);
    new QVBoxLayout(petGroup);
    m_petnamesTree = makeTree({tr("Petname"), tr("Key"), tr("Shared with friends")}, petGroup);
    petGroup->layout()->addWidget(m_petnamesTree);
    auto* addPet = new QPushButton(tr("Add…"), petGroup);
    auto* removePet = new QPushButton(tr("Remove"), petGroup);
    buttonRow(petGroup, {addPet, removePet});
    layout->addWidget(petGroup);
    connect(addPet, &QPushButton::clicked, this, &Ed3kOptionsPages::addPetname);
    connect(removePet, &QPushButton::clicked, this, [this, selected] {
        if (auto* it = selected(m_petnamesTree); it && m_ipc)
            m_ipc->removeEd3kPetname(it->text(0), [this](const IpcMessage& r) { failed(r, tr("Remove")); });
    });

    // -- Followed name lists, in order: the first wins
    auto* listGroup = new QGroupBox(tr("Followed name lists (the first one wins)"), page);
    new QVBoxLayout(listGroup);
    m_listsTree = makeTree({tr("Label"), tr("Entries"), tr("Refused"), tr("Link")}, listGroup);
    listGroup->layout()->addWidget(m_listsTree);
    auto* follow = new QPushButton(tr("Follow…"), listGroup);
    auto* unfollow = new QPushButton(tr("Unfollow"), listGroup);
    auto* up = new QPushButton(tr("Move up"), listGroup);
    auto* down = new QPushButton(tr("Move down"), listGroup);
    buttonRow(listGroup, {follow, unfollow, up, down});
    layout->addWidget(listGroup);
    layout->addStretch();
    connect(follow, &QPushButton::clicked, this, &Ed3kOptionsPages::followList);
    connect(unfollow, &QPushButton::clicked, this, [this, selected] {
        if (auto* it = selected(m_listsTree); it && m_ipc)
            m_ipc->unfollowEd3kNameList(it->data(0, Qt::UserRole).toLongLong(),
                                        [this](const IpcMessage& r) { failed(r, tr("Unfollow")); });
    });
    connect(up, &QPushButton::clicked, this, [this] { moveList(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveList(1); });
    return page;
}

void Ed3kOptionsPages::applyStatus(const QCborMap& st)
{
    const bool available = st.value(QStringLiteral("available")).toBool(false);
    const bool running = st.value(QStringLiteral("running")).toBool(false);
    if (!available)
        m_runningLabel->setText(tr("This core was built without ed3k: the settings are kept, but have no effect."));
    else if (running)
        m_runningLabel->setText(tr("The node is running on UDP port %1.").arg(st.value(QStringLiteral("udpPort")).toInteger()));
    else
        m_runningLabel->setText(tr("The node is not running."));

    applyHosterStatus(st.value(QStringLiteral("hoster")).toMap());
    // batch 13: the last chain read/write the node refused (its text is plain text of a stranger)
    const QString chainRefused = st.value(QStringLiteral("chain")).toMap().value(QStringLiteral("refused")).toString();
    m_chainRefusedLabel->setText(chainRefused);
    m_chainRefusedLabel->setVisible(!chainRefused.isEmpty());

    const QCborMap c = st.value(QStringLiteral("credit")).toMap();
    m_creditStatus = c;
    updateAutoCredits();
    const QString address = c.value(QStringLiteral("address")).toString();
    m_addressLabel->setText(address.isEmpty() ? tr("No credit key yet: one is made when credits are switched on.")
                                              : tr("Credit address: %1").arg(address));
    m_exportKeyBtn->setEnabled(keyActionsAllowed() && !address.isEmpty());

    if (c.isEmpty() || !c.value(QStringLiteral("enabled")).toBool()) {
        m_accountLabel->setText(tr("Not known while credits are off."));
        return;
    }
    QStringList lines;
    if (!c.value(QStringLiteral("configured")).toBool()) {
        lines << tr("Not in use: %1").arg(c.value(QStringLiteral("problem")).toString());
    } else {
        if (const QString problem = c.value(QStringLiteral("problem")).toString(); !problem.isEmpty())
            lines << problem;
        const qint64 balance = c.value(QStringLiteral("balance")).toInteger(-1);
        lines << tr("Balance: %1").arg(balance < 0 ? tr("not read yet") : creditsText(balance));
        if (c.value(QStringLiteral("overdrawn")).toBool())
            lines << tr("The account is overdrawn: a new credit key is the way on.");
        else
            lines << (c.value(QStringLiteral("mature")).toBool() ? tr("Mature: the receipts you sign mint credits.")
                                                                 : tr("Not mature yet: the receipts you sign mint nothing."));
        lines << tr("Receipts signed: %1, received: %2")
                     .arg(c.value(QStringLiteral("receiptsSigned")).toInteger())
                     .arg(c.value(QStringLiteral("receiptsTaken")).toInteger());
        const QString rate = c.value(QStringLiteral("stakeRate")).toString();
        if (!rate.isEmpty()) {
            const double perCredit = uint256Value(rate) / 1e18;
            lines << (perCredit > 0 ? tr("Stake: a credit buys %1 E3S").arg(QLocale().toString(perCredit, 'g', 6))
                                    : tr("Stake: burning is off"));
        }
        const qint64 claims = qint64(c.value(QStringLiteral("claims")).toArray().size());
        if (claims > 0)
            lines << tr("Stake claims waiting: %1").arg(claims);
        if (!c.value(QStringLiteral("wrongChain")).toArray().isEmpty())
            lines << tr("Some endpoints are on another chain and are not used.");
    }
    m_accountLabel->setText(lines.join(QLatin1Char('\n')));
}

void Ed3kOptionsPages::requestStatus()
{
    if (!m_ipc || !m_ipc->isConnected()) {
        applyStatus({});
        return;
    }
    m_ipc->sendRequest(IpcMessage(IpcMsgType::GetEd3kStatus), [self = QPointer<Ed3kOptionsPages>(this)](const IpcMessage& r) {
        if (self && r.type() == IpcMsgType::Result && r.fieldBool(0))
            self->applyStatus(r.fieldMap(1));
    });
}

bool Ed3kOptionsPages::confirmCredits()
{
    QMessageBox box(QMessageBox::Warning, tr("ed3k credits"), tr("Switch credits on?"), QMessageBox::Yes | QMessageBox::No, m_parent);
    box.setInformativeText(notesText());
    box.setTextFormat(Qt::RichText);
    box.setDefaultButton(QMessageBox::No);
    return box.exec() == QMessageBox::Yes;
}

void Ed3kOptionsPages::newKey()
{
    if (!m_ipc)
        return;
    if (QMessageBox::question(m_parent, tr("ed3k credits"),
                              tr("A new credit key gives a new credit address: no balance, and not mature for 30 days. "
                                 "The old address keeps its credits; they move only by a transfer, which is public and "
                                 "costs a part of them. Export the old key first if you may need it.\n\nMake a new key?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    m_ipc->sendRequest(IpcMessage(IpcMsgType::NewEd3kCreditKey), [self = QPointer<Ed3kOptionsPages>(this)](const IpcMessage& r) {
        if (self && !self->failed(r, tr("New key")))
            self->requestStatus();
    });
}

void Ed3kOptionsPages::importKey()
{
    if (!m_ipc)
        return;
    const QString path = QFileDialog::getOpenFileName(m_parent, tr("Import credit key"), QString(),
                                                      tr("Credit keys (*.key *.txt);;All files (*)"));
    if (path.isEmpty())
        return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(m_parent, tr("ed3k credits"), tr("Could not read %1.").arg(path));
        return;
    }
    QByteArray raw = f.read(4096);
    f.close();
    // 32 bytes as they are, or 64 hex digits (0x optional), as wallets write them
    QByteArray hex;
    if (raw.size() == 32) {
        hex = raw.toHex();
    } else {
        QByteArray t = raw.trimmed();
        if (t.startsWith("0x") || t.startsWith("0X"))
            t = t.mid(2);
        if (t.size() == 64)
            hex = t;
        scrub(t);
    }
    scrub(raw);
    if (hex.isEmpty()) {
        QMessageBox::warning(m_parent, tr("ed3k credits"), tr("This is not a credit key: 32 bytes or 64 hexadecimal digits."));
        return;
    }
    if (QMessageBox::question(m_parent, tr("ed3k credits"),
                              tr("The imported key replaces the current credit key. Export the current one first if "
                                 "you may need it.\n\nImport?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes) {
        scrub(hex);
        return;
    }
    IpcMessage req(IpcMsgType::ImportEd3kCreditKey);
    req.append(QString::fromLatin1(hex));
    scrub(hex);
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kOptionsPages>(this)](const IpcMessage& r) {
        if (self && !self->failed(r, tr("Import")))
            self->requestStatus();
    });
}

void Ed3kOptionsPages::exportKey()
{
    if (!m_ipc)
        return;
    if (QMessageBox::warning(m_parent, tr("ed3k credits"),
                             tr("The file will hold the secret credit key: whoever has it can spend your credits and "
                                "sign as you. Keep it as private as a password.\n\nExport?"),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;
    const QString path = QFileDialog::getSaveFileName(m_parent, tr("Export credit key"), QStringLiteral("credit.key"),
                                                      tr("Credit keys (*.key *.txt);;All files (*)"));
    if (path.isEmpty())
        return;
    m_ipc->sendRequest(IpcMessage(IpcMsgType::ExportEd3kCreditKey), [self = QPointer<Ed3kOptionsPages>(this), path](const IpcMessage& r) {
        if (!self || self->failed(r, tr("Export")))
            return;
        QByteArray text = "0x" + r.fieldMap(1).value(QStringLiteral("key")).toString().toLatin1() + "\n";
        QFile f(path);
        // owner-only before the first byte
        bool ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        if (ok) {
            f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            ok = f.write(text) == text.size();
            f.close();
        }
        scrub(text);
        if (!ok)
            QMessageBox::warning(self->m_parent, tr("ed3k credits"), tr("Could not write %1.").arg(path));
    });
}

void Ed3kOptionsPages::refreshNames()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    m_ipc->requestEd3kNames([self = QPointer<Ed3kOptionsPages>(this)](const IpcMessage& r) {
        if (self && r.type() == IpcMsgType::Result && r.fieldBool(0))
            self->fillNames(r.fieldMap(1));
    });
}

void Ed3kOptionsPages::fillNames(const QCborMap& names)
{
    const bool running = names.value(QStringLiteral("running")).toBool();
    m_namesNote->setText(running ? tr("Changes on this page take effect at once.")
                                 : tr("The ed3k node is not running: names can be changed while it runs."));
    m_pinsTree->clear();
    for (const QCborValue& v : names.value(QStringLiteral("pins")).toArray()) {
        const QCborMap p = v.toMap();
        auto* it = new QTreeWidgetItem(m_pinsTree);
        it->setText(0, p.value(QStringLiteral("name")).toString());
        it->setText(1, Ed3kNameOpener::zoneText(p.value(QStringLiteral("zone")).toString()));
        it->setText(2, Ed3kNameOpener::howText(p));
        const bool vouched = p.value(QStringLiteral("vouched")).toBool();
        it->setText(3, vouched ? tr("Yes") : tr("No"));
        it->setData(3, Qt::UserRole, vouched);
        it->setToolTip(0, p.value(QStringLiteral("link")).toString());
    }
    m_petnamesTree->clear();
    for (const QCborValue& v : names.value(QStringLiteral("petnames")).toArray()) {
        const QCborMap p = v.toMap();
        auto* it = new QTreeWidgetItem(m_petnamesTree);
        it->setText(0, p.value(QStringLiteral("text")).toString());
        it->setText(1, Ed3kNameOpener::zoneText(p.value(QStringLiteral("zone")).toString()));
        it->setText(2, p.value(QStringLiteral("shareable")).toBool() ? tr("Yes") : tr("No"));
    }
    m_listsTree->clear();
    for (const QCborValue& v : names.value(QStringLiteral("lists")).toArray()) {
        const QCborMap l = v.toMap();
        auto* it = new QTreeWidgetItem(m_listsTree);
        it->setText(0, l.value(QStringLiteral("label")).toString());
        it->setText(1, QString::number(l.value(QStringLiteral("entries")).toInteger()));
        it->setText(2, QString::number(l.value(QStringLiteral("refused")).toInteger()));
        it->setText(3, l.value(QStringLiteral("link")).toString());
        it->setData(0, Qt::UserRole, l.value(QStringLiteral("id")).toInteger());
    }
}

void Ed3kOptionsPages::addPetname()
{
    if (!m_ipc)
        return;
    bool ok = false;
    const QString text = QInputDialog::getText(m_parent, tr("Add petname"), tr("Petname:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || text.isEmpty())
        return;
    const QString zone = QInputDialog::getText(m_parent, tr("Add petname"), tr("Key of the zone (hex or base32):"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || zone.isEmpty())
        return;
    const bool share = QMessageBox::question(m_parent, tr("Add petname"), tr("Share this petname with your ed3k friends?"),
                                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
                       == QMessageBox::Yes;
    m_ipc->setEd3kPetname(text, zone, share, [this](const IpcMessage& r) { failed(r, tr("Add petname")); });
}

void Ed3kOptionsPages::followList()
{
    if (!m_ipc)
        return;
    bool ok = false;
    const QString link = QInputDialog::getText(m_parent, tr("Follow name list"), tr("Link of the name list (ed3k:set?…):"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || link.isEmpty())
        return;
    const QString label = QInputDialog::getText(m_parent, tr("Follow name list"), tr("Label:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok)
        return;
    m_ipc->followEd3kNameList(link, label, [this](const IpcMessage& r) { failed(r, tr("Follow name list")); });
}

void Ed3kOptionsPages::moveList(int step)
{
    const auto items = m_listsTree->selectedItems();
    if (items.isEmpty() || !m_ipc)
        return;
    const int row = m_listsTree->indexOfTopLevelItem(items.first());
    const int place = row + step;
    if (place < 0 || place >= m_listsTree->topLevelItemCount())
        return;
    m_ipc->moveEd3kNameList(items.first()->data(0, Qt::UserRole).toLongLong(), place,
                            [this](const IpcMessage& r) { failed(r, tr("Move")); });
}

void Ed3kOptionsPages::setJoinedPort(int port)
{
    m_joinedPort = port;
    showPortJoined();
}

void Ed3kOptionsPages::showPortJoined()
{
    m_udpPortSpin->setVisible(!m_portJoined);
    m_udpPortJoinedLabel->setVisible(m_portJoined);
    m_udpPortJoinedLabel->setText(tr("%1 (the eD2K TCP/UDP port)").arg(m_joinedPort));
}

void Ed3kOptionsPages::requestCompatPlan()
{
    if (!m_ipc || !m_ipc->isConnected())
        return;
    IpcMessage req(IpcMsgType::MigrateEd2kCompat);
    req.append(false);
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kOptionsPages>(this)](const IpcMessage& reply) {
        if (self && reply.type() == IpcMsgType::Result && reply.fieldBool(0))
            self->applyCompatPlan(reply.fieldMap(1));
    });
}

void Ed3kOptionsPages::applyCompatPlan(const QCborMap& plan)
{
    const int port = int(plan.value(QStringLiteral("port")).toInteger());
    const bool done = !plan.value(QStringLiteral("hashChange")).toBool()
                      && plan.value(QStringLiteral("udpFrom")).toInteger() == port
                      && plan.value(QStringLiteral("ed3kFrom")).toInteger() == port;
    m_compatBtn->setEnabled(!done);
    if (done)
        m_compatLabel->setText(tr("In use: eD2K and ed3k share port %1, and the user hash is the one derived "
                                  "from the ed3k node.").arg(port));
}

void Ed3kOptionsPages::migrateEd2kCompat()
{
    if (!m_ipc || !m_ipc->isConnected()) {
        QMessageBox::warning(m_parent, tr("eD2K compatibility"), tr("No connection to the core."));
        return;
    }
    IpcMessage req(IpcMsgType::MigrateEd2kCompat);
    req.append(false);
    m_ipc->sendRequest(std::move(req), [self = QPointer<Ed3kOptionsPages>(this)](const IpcMessage& reply) {
        if (!self || self->failed(reply, tr("eD2K compatibility")))
            return;
        const QCborMap plan = reply.fieldMap(1);
        self->applyCompatPlan(plan);
        const int port = int(plan.value(QStringLiteral("port")).toInteger());
        const int udpFrom = int(plan.value(QStringLiteral("udpFrom")).toInteger());
        const int ed3kFrom = int(plan.value(QStringLiteral("ed3kFrom")).toInteger());
        const bool hashChange = plan.value(QStringLiteral("hashChange")).toBool();
        if (plan.value(QStringLiteral("busy")).toBool()) {
            QMessageBox::warning(self->m_parent, tr("eD2K compatibility"),
                                 tr("UDP port %1 is used by another program or by BitTorrent. Change the TCP "
                                    "port first.").arg(port));
            return;
        }
        QStringList changes;
        if (hashChange)
            changes << tr("New user hash: credits earned at other eD2K clients are lost once. The core restarts.");
        if (udpFrom != port || ed3kFrom != port)
            changes << tr("eD2K UDP and ed3k move to port %1 (was UDP %2, ed3k %3).").arg(port).arg(udpFrom).arg(ed3kFrom);
        if (udpFrom != port)
            changes << tr("Open UDP port %1 in your router.").arg(port);
        if (changes.isEmpty()) {
            QMessageBox::information(self->m_parent, tr("eD2K compatibility"), tr("Nothing to change."));
            return;
        }
        const QString text = tr("Switch to eD2K compatibility?") + QStringLiteral("\n\n• ")
                             + changes.join(QStringLiteral("\n• "));
        if (QMessageBox::question(self->m_parent, tr("eD2K compatibility"), text, QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No)
            != QMessageBox::Yes)
            return;
        IpcMessage apply(IpcMsgType::MigrateEd2kCompat);
        apply.append(true);
        self->m_ipc->sendRequest(std::move(apply), [self](const IpcMessage& done) {
            if (!self || self->failed(done, tr("eD2K compatibility")))
                return;
            const QCborMap result = done.fieldMap(1);
            if (result.value(QStringLiteral("busy")).toBool())
                return;   // taken meanwhile: nothing applied
            const int joined = int(result.value(QStringLiteral("port")).toInteger());
            self->m_loading = true;
            self->m_udpPortSpin->setValue(joined);
            self->m_loading = false;
            self->m_portJoined = true;
            self->setJoinedPort(joined);
            self->m_compatBtn->setEnabled(false);
            emit self->portsJoined(joined);
        });
    });
}

bool Ed3kOptionsPages::failed(const IpcMessage& reply, const QString& what)
{
    if (reply.type() == IpcMsgType::Result && reply.fieldBool(0))
        return false;
    QString why;
    if (reply.type() == IpcMsgType::Result) {
        const QCborValue err = reply.fieldMap(1).value(QStringLiteral("error"));
        why = err.isString() ? err.toString()
                             : (err.toInteger() == 0 ? tr("the ed3k node is not running") : tr("error %1").arg(err.toInteger()));
    } else {
        why = reply.fieldString(1).isEmpty() ? tr("no answer from the core") : reply.fieldString(1);
    }
    QMessageBox::warning(m_parent, tr("ed3k"), tr("%1 failed: %2").arg(what, why));
    return true;
}

QStringList Ed3kOptionsPages::extOff() const
{
    QStringList off;
    for (const QCheckBox* box : m_extChecks) {
        if (!box->isChecked())
            off << box->text();
    }
    return off;
}

Ed3kCreditPrefs Ed3kOptionsPages::creditPrefs() const
{
    Ed3kCreditPrefs c;
    c.enabled = m_creditEnabledCheck->isChecked();
    c.chainId = m_chainIdEdit->text().trimmed().toULongLong();
    c.contract = m_contractEdit->text().trimmed();
    c.quorum = uint32(m_quorumSpin->value());
    c.bidMaxPrice = uint64(microOf(m_bidSpin));
    c.sell = m_sellCheck->isChecked();
    c.sellFloor = uint64(microOf(m_sellFloorSpin));
    c.sellSlots = uint32(m_sellSlotsSpin->value());
    c.autoBid = m_autoBidGroup->isChecked();
    c.autoFactor = Ed3kCreditPrefs::autoFactorOf(m_autoFactorSpin->value());
    c.autoThreshold = uint64(m_autoThresholdSpin->value());
    return c;
}

// ToDo: back with the chaintube fields (buildConnection)
// Ed3kChainPrefs Ed3kOptionsPages::chainPrefs() const
// {
//     Ed3kChainPrefs c;
//     c.chainId = m_ctChainIdEdit->text().trimmed().toULongLong();
//     c.content = m_ctContentEdit->text().trimmed();
//     c.comment = m_ctCommentEdit->text().trimmed();
//     c.vote = m_ctVoteEdit->text().trimmed();
//     c.forwarder = m_ctForwarderEdit->text().trimmed();
//     return c;
// }

bool Ed3kOptionsPages::keyActionsAllowed() const
{
    // the daemon refuses a remote GUI too (403)
    return m_ipc && m_ipc->isLocalConnection();
}

QString Ed3kOptionsPages::notesText()
{
    // credit.md §9 (MUST tell), §9.1, §7.3, §10 (SHOULD say), paraphrased
    return tr("<p><b>The ledger is public and stays so.</b> For every pair of credit addresses it shows how much "
              "one downloaded from the other, on which day, and what was paid; no file, host or node ID. Your node "
              "tells its credit address to every peer that opens a file stream to it, so others can link your node "
              "and host address to your credit address and read your whole trade history. A bid (a hold) is public "
              "at once. A burn for stake tokens is public too: it ties your credit address to the address it pays, "
              "and through the bridge to an address on the C-Chain, where an exchange may know your name.</p>"
              "<p><b>Hidden (anonymous) use</b> needs a credit address of its own that is never used outside the "
              "anonymous path; never move credits or gas coin between that address and this one, and never burn "
              "for it: a transfer is a public link for good.</p>"
              "<p>A remote ledger endpoint sees your IP address and every credit address it is asked about; the "
              "calls go through the proxy you configured.</p>"
              "<p><b>Not checked by a lawyer:</b> credits buy E3S stake tokens, which are traded on exchanges. "
              "Whether credits and E3S are crypto-assets under the law of your country, and what being paid in "
              "them means for you, was not checked by a lawyer.</p>");
}

QString Ed3kOptionsPages::creditsText(qint64 micro)
{
    const qint64 whole = micro / 1'000'000;
    const qint64 frac = micro % 1'000'000;
    return tr("%1.%2 credits").arg(QLocale().toString(whole)).arg(frac, 6, 10, QLatin1Char('0'));
}

QWidget* Ed3kOptionsPages::buildHoster(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    const auto edited = [this] {
        if (!m_loading)
            emit changed();
    };

    auto* dlGroup = new QGroupBox(tr("Downloads from file hosters"), page);
    auto* dlLayout = new QVBoxLayout(dlGroup);
    m_hosterEnabledCheck = new QCheckBox(tr("Allow downloads from file hosters"), dlGroup);
    m_hosterEnabledCheck->setToolTip(tr("Lets the resolver open hoster pages, wait out their timers, show their "
                                        "puzzles and use the accounts below. On by default; needs the ed3k node."));
    dlLayout->addWidget(m_hosterEnabledCheck);
    m_hosterNodeLabel = noteLabel(tr("Hoster downloads need the ed3k node (Options, ed3k, General)."), dlGroup);
    dlLayout->addWidget(m_hosterNodeLabel);
    m_resolverLabel = noteLabel(QString(), dlGroup);
    dlLayout->addWidget(m_resolverLabel);
    auto* dlForm = new QFormLayout;
    dlForm->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    dlForm->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    m_hosterStreamsSpin = new QSpinBox(dlGroup);
    m_hosterStreamsSpin->setRange(1, 16);
    m_hosterStreamsSpin->setToolTip(tr("Hoster downloads at once. Never more than one per hoster and account."));
    dlForm->addRow(tr("Downloads at once:"), m_hosterStreamsSpin);
    dlLayout->addLayout(dlForm);
    m_hosterPuzzlesCheck = new QCheckBox(tr("Show image puzzles (captchas)"), dlGroup);
    m_hosterPuzzlesCheck->setToolTip(tr("Off: a link that asks for a puzzle waits instead."));
    dlLayout->addWidget(m_hosterPuzzlesCheck);
    m_hosterPeersCheck = new QCheckBox(tr("Use hoster links that other ed3k nodes name"), dlGroup);
    m_hosterPeersCheck->setVisible(thePrefs.showExtControls());
    dlLayout->addWidget(m_hosterPeersCheck);
    m_hosterCheckLinksCheck = new QCheckBox(tr("Check hoster links on search automatically"), dlGroup);
    m_hosterCheckLinksCheck->setToolTip(tr("The links window of a web search result checks which links are online. "
                                           "Download prefers online links at hosters you have an account at."));
    dlLayout->addWidget(m_hosterCheckLinksCheck);
    layout->addWidget(dlGroup);

    auto* javaGroup = new QGroupBox(tr("Java runtime"), page);
    auto* javaLayout = new QVBoxLayout(javaGroup);
    javaLayout->addWidget(noteLabel(tr("The resolver runs JDownloader's hoster plugins in a Java program of its own."),
                                    javaGroup));
    m_javaLabel = noteLabel(QString(), javaGroup);
    javaLayout->addWidget(m_javaLabel);
    auto* pathRow = new QHBoxLayout;
    m_javaEdit = new QLineEdit(javaGroup);
    m_javaEdit->setPlaceholderText(tr("Find automatically"));
    pathRow->addWidget(new QLabel(tr("Java program:"), javaGroup));
    pathRow->addWidget(m_javaEdit, 1);
    auto* browseBtn = new QPushButton(tr("Browse..."), javaGroup);
    connect(browseBtn, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getOpenFileName(m_parent, tr("Java program"), m_javaEdit->text());
        if (!f.isEmpty())
            m_javaEdit->setText(f);
    });
    pathRow->addWidget(browseBtn);
    auto* pathBox = new QWidget(javaGroup);
    pathBox->setLayout(pathRow);
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathBox->setVisible(thePrefs.showExtControls());
    javaLayout->addWidget(pathBox);
    m_autoJavaCheck = new QCheckBox(tr("Download a Java runtime when none is found"), javaGroup);
    m_autoJavaCheck->setToolTip(tr("Eclipse Temurin from adoptium.net, checked by its SHA-256, into the eMuleQt folder."));
    javaLayout->addWidget(m_autoJavaCheck);
    auto* javaBtns = new QHBoxLayout;
    auto* locateBtn = new QPushButton(tr("Look again"), javaGroup);
    m_javaLocateBtn = locateBtn;
    connect(locateBtn, &QPushButton::clicked, this, [this] {
        if (m_ipc)
            m_ipc->sendRequest(IpcMessage(IpcMsgType::LocateJavaRuntime));
    });
    m_javaDownloadBtn = new QPushButton(tr("Download Java"), javaGroup);
    connect(m_javaDownloadBtn, &QPushButton::clicked, this, [this] {
        if (m_ipc)
            m_ipc->sendRequest(IpcMessage(IpcMsgType::DownloadJavaRuntime));
    });
    javaBtns->addWidget(locateBtn);
    javaBtns->addWidget(m_javaDownloadBtn);
    javaBtns->addStretch();
    javaLayout->addLayout(javaBtns);
    layout->addWidget(javaGroup);

    auto* linkGroup = new QGroupBox(tr("Sharing links"), page);
    auto* linkLayout = new QVBoxLayout(linkGroup);
    m_publishLinksCheck = new QCheckBox(tr("Publish hoster links"), linkGroup);
    linkLayout->addWidget(m_publishLinksCheck);
    linkLayout->addWidget(noteLabel(tr("A downloaded file is shared either way. Published, its link is found in ed3k "
                                       "and handed to other nodes: a link is often all that protects a file at a hoster."),
                                    linkGroup));
    layout->addWidget(linkGroup);

    auto* accGroup = new QGroupBox(tr("Accounts"), page);
    auto* accLayout = new QVBoxLayout(accGroup);
    accLayout->addWidget(noteLabel(tr("Logins at hosters. They stay on this computer: never sent to another node, "
                                      "never logged."),
                                   accGroup));
    m_accountsTree = makeTree({tr("Hoster"), tr("User"), tr("Status")}, accGroup);
    accLayout->addWidget(m_accountsTree);
    auto* accBtns = new QHBoxLayout;
    auto* addBtn = new QPushButton(tr("Add..."), accGroup);
    auto* editBtn = new QPushButton(tr("Edit..."), accGroup);
    auto* removeBtn = new QPushButton(tr("Remove"), accGroup);
    auto* checkBtn = new QPushButton(tr("Check"), accGroup);
    for (QPushButton* b : {addBtn, editBtn, removeBtn, checkBtn})
        accBtns->addWidget(b);
    accBtns->addStretch();
    accLayout->addLayout(accBtns);
    layout->addWidget(accGroup);
    layout->addStretch();

    connect(addBtn, &QPushButton::clicked, this, [this] { editAccount(-1); });
    connect(editBtn, &QPushButton::clicked, this, [this] {
        if (auto* it = m_accountsTree->currentItem())
            editAccount(m_accountsTree->indexOfTopLevelItem(it));
    });
    connect(m_accountsTree, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* it) { editAccount(m_accountsTree->indexOfTopLevelItem(it)); });
    connect(removeBtn, &QPushButton::clicked, this, [this] {
        const int row = m_accountsTree->currentItem() ? m_accountsTree->indexOfTopLevelItem(m_accountsTree->currentItem()) : -1;
        if (row < 0 || row >= m_accounts.size())
            return;
        m_accounts.removeAt(row);
        fillAccounts();
        emit changed();
    });
    connect(checkBtn, &QPushButton::clicked, this, &Ed3kOptionsPages::checkAccount);

    connect(m_hosterEnabledCheck, &QCheckBox::toggled, this, [this] {
        if (m_loading)
            return;
        updateJavaButtons();
        updateHosterNodeHint();
        emit changed();
    });
    // the hoster runs on the ed3k node: say so while the node is unticked
    connect(m_enabledCheck, &QCheckBox::toggled, this, [this] { updateHosterNodeHint(); });
    updateHosterNodeHint();
    connect(m_publishLinksCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        if (on && !confirmShareLinks()) {
            const QSignalBlocker block(m_publishLinksCheck);
            m_publishLinksCheck->setChecked(false);
            return;
        }
        emit changed();
    });
    for (QCheckBox* box : {m_hosterPuzzlesCheck, m_hosterPeersCheck, m_hosterCheckLinksCheck, m_autoJavaCheck})
        connect(box, &QCheckBox::toggled, this, edited);
    connect(m_hosterStreamsSpin, &QSpinBox::valueChanged, this, edited);
    connect(m_javaEdit, &QLineEdit::textEdited, this, edited);
#ifndef EMULE_HAVE_HOSTER_JD
    // built without the resolver: links are still passed on, nothing is downloaded
    dlGroup->setEnabled(false);
    javaGroup->setVisible(false);
    accGroup->setVisible(false);
    m_resolverLabel->setText(tr("This build has no hoster resolver: hoster links are stored and passed on, "
                                "but not downloaded."));
#endif
    return page;
}

void Ed3kOptionsPages::applyHosterStatus(const QCborMap& h)
{
    if (!m_resolverLabel || h.isEmpty())
        return;
    if (!h.value(QStringLiteral("compiled")).toBool())
        return;
    const QCborMap r = h.value(QStringLiteral("resolver")).toMap();
    const QString description = r.value(QStringLiteral("description")).toString();
    const QString reason = r.value(QStringLiteral("reason")).toString();
    if (r.value(QStringLiteral("available")).toBool())
        m_resolverLabel->setText(description.isEmpty() ? tr("The resolver is ready.") : tr("Resolver: %1").arg(description));
    else
        m_resolverLabel->setText(tr("The resolver is not ready: %1").arg(reason));

    const QCborMap j = h.value(QStringLiteral("java")).toMap();
    // Ed3kJavaRuntime::State: Unknown, Probing, Ready, Missing, Downloading, Unpacking, Failed
    const int state = int(j.value(QStringLiteral("state")).toInteger());
    QString text;
    switch (state) {
    case 1: text = tr("Looking for Java..."); break;
    case 2:
        text = tr("Java %1 (%2): %3")
                   .arg(j.value(QStringLiteral("version")).toInteger())
                   .arg(j.value(QStringLiteral("vendor")).toString(), j.value(QStringLiteral("path")).toString());
        break;
    case 3: text = tr("No Java 17 or newer found."); break;
    case 4: text = tr("Downloading Java... %1%").arg(j.value(QStringLiteral("progress")).toInteger()); break;
    case 5: text = tr("Installing Java..."); break;
    case 6: text = tr("Java: %1").arg(j.value(QStringLiteral("error")).toString()); break;
    default: text = tr("Java is looked for when downloads from hosters are allowed."); break;
    }
    m_javaLabel->setText(text);
    m_javaState = state;
    m_hosterOnInCore = h.value(QStringLiteral("enabled")).toBool();
    updateJavaButtons();
}

void Ed3kOptionsPages::updateJavaButtons()
{
    if (!m_javaDownloadBtn || !m_hosterEnabledCheck)
        return;
    // a runtime is fetched only for a user who switched hoster downloads on (and applied it)
    const bool on = m_hosterEnabledCheck->isChecked() && m_hosterOnInCore;
    const QString why = on ? QString() : tr("Allow downloads from file hosters and apply first.");
    m_javaDownloadBtn->setEnabled(on && (m_javaState == 3 || m_javaState == 6 || m_javaState == 0));
    m_javaDownloadBtn->setToolTip(why);
    if (m_javaLocateBtn) {
        m_javaLocateBtn->setEnabled(on);
        m_javaLocateBtn->setToolTip(why);
    }
}

void Ed3kOptionsPages::updateHosterNodeHint()
{
    if (m_hosterNodeLabel && m_enabledCheck && m_hosterEnabledCheck)
        m_hosterNodeLabel->setVisible(m_hosterEnabledCheck->isChecked() && !m_enabledCheck->isChecked());
}

bool Ed3kOptionsPages::confirmShareLinks()
{
    const QString text =
        tr("<p>Sharing hoster links means: once a file from a link is downloaded and verified, its link is published "
           "in ed3k and handed to other nodes that download the same file.</p>"
           "<p>A link is often all that keeps a file at a hoster private. Whoever knows the link can find the file in "
           "ed3k, and anyone can find the link of a file you share.</p><p>Publish hoster links?</p>");
    return QMessageBox::question(m_parent, tr("Publish hoster links"), text, QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No)
           == QMessageBox::Yes;
}

void Ed3kOptionsPages::editAccount(int row)
{
    const bool add = row < 0 || row >= m_accounts.size();
    HosterAccountRow a = add ? HosterAccountRow{} : m_accounts.at(row);
    QDialog dlg(m_parent);
    dlg.setWindowTitle(add ? tr("Add hoster account") : tr("Edit hoster account"));
    auto* form = new QFormLayout(&dlg);
    auto* hostEdit = new QLineEdit(a.host, &dlg);
    hostEdit->setPlaceholderText(QStringLiteral("rapidgator.net"));
    auto* userEdit = new QLineEdit(a.user, &dlg);
    auto* passEdit = new QLineEdit(&dlg);
    passEdit->setEchoMode(QLineEdit::Password);
    passEdit->setPlaceholderText(add ? QString() : tr("unchanged"));
    form->addRow(tr("Hoster:"), hostEdit);
    form->addRow(tr("User:"), userEdit);
    form->addRow(tr("Password:"), passEdit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString host = hostEdit->text().trimmed().toLower();
    if (host.startsWith(QLatin1StringView("www.")))
        host = host.mid(4);
    const QString user = userEdit->text().trimmed();
    if (host.isEmpty() || user.isEmpty())
        return;
    const bool renamed = host != a.host || user != a.user;
    a.host = host;
    a.user = user;
    if (!passEdit->text().isEmpty() || renamed) {
        a.password = passEdit->text();
        a.passwordSet = true;
    }
    a.status.clear();
    if (add)
        m_accounts.append(a);
    else
        m_accounts[row] = a;
    fillAccounts();
    emit changed();
}

void Ed3kOptionsPages::checkAccount()
{
    const int row = m_accountsTree->currentItem() ? m_accountsTree->indexOfTopLevelItem(m_accountsTree->currentItem()) : -1;
    if (row < 0 || row >= m_accounts.size() || !m_ipc)
        return;
    HosterAccountRow& a = m_accounts[row];
    if (!a.passwordSet) {
        // the stored password stays in the core: type it to check
        bool ok = false;
        const QString pass = QInputDialog::getText(m_parent, tr("Check hoster account"),
                                                   tr("Password of %1 at %2:").arg(a.user, a.host), QLineEdit::Password,
                                                   {}, &ok);
        if (!ok)
            return;
        a.password = pass;
        a.passwordSet = true;
    }
    IpcMessage req(IpcMsgType::CheckHosterAccount);
    req.append(a.host);
    req.append(a.user);
    req.append(a.password);
    m_ipc->sendRequest(std::move(req));
    a.status = tr("Checking...");
    fillAccounts();
}

void Ed3kOptionsPages::fillAccounts()
{
    m_accountsTree->clear();
    for (const HosterAccountRow& a : m_accounts)
        m_accountsTree->addTopLevelItem(new QTreeWidgetItem({a.host, a.user, a.status}));
}

Ed3kHosterPrefs Ed3kOptionsPages::hosterPrefs() const
{
    Ed3kHosterPrefs h = thePrefs.ed3kHoster();   // keeps the YAML-only fields
    h.accounts.clear();
    h.enabled = m_hosterEnabledCheck->isChecked();
    h.java = m_javaEdit->text().trimmed();
    h.autoJava = m_autoJavaCheck->isChecked();
    h.maxStreams = uint32(m_hosterStreamsSpin->value());
    h.askPuzzles = m_hosterPuzzlesCheck->isChecked();
    h.publishLinks = m_publishLinksCheck->isChecked();
    h.fromPeers = m_hosterPeersCheck->isChecked();
    h.checkSearchLinks = m_hosterCheckLinksCheck->isChecked();
    for (const HosterAccountRow& a : m_accounts)
        h.accounts.append({a.host, a.user, a.passwordSet ? a.password : QString()});
    return h;
}

void Ed3kOptionsPages::updateChainNote()
{
    if (m_chainSingleNote)
        m_chainSingleNote->setVisible(singleRemoteEndpoint(linesOf(m_chaintubeRpcEdit)));
}

QWidget* Ed3kOptionsPages::buildAutoCredits(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(4, 4, 4, 4);
    const auto edited = [this] {
        if (!m_loading)
            emit changed();
    };

    // spends credits on its own: off by default
    m_autoBidGroup = new QGroupBox(tr("Autostart highspeed download"), page);
    m_autoBidGroup->setCheckable(true);
    m_autoBidGroup->setChecked(false);
    m_autoBidGroup->setToolTip(tr("Once the credit balance reaches the threshold, every ed3k download bids the credit "
                                  "factor for priority upload slots. It stops before about 20 credits are left, "
                                  "from what downloads spent in the past seconds, so the account is never overdrawn."));
    auto* groupLayout = new QVBoxLayout(m_autoBidGroup);
    auto* form = new QFormLayout;
    form->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    m_autoFactorSpin = new QDoubleSpinBox(m_autoBidGroup);
    m_autoFactorSpin->setDecimals(1);
    m_autoFactorSpin->setSingleStep(0.5);
    m_autoFactorSpin->setRange(Ed3kCreditPrefs::kAutoFactorMin / 10.0, Ed3kCreditPrefs::kAutoFactorMax / 10.0);
    m_autoFactorSpin->setValue(5.0);
    m_autoFactorSpin->setSuffix(tr(" credits/MiB"));
    m_autoFactorSpin->setToolTip(tr("The most paid per MiB for a priority slot while the autostart runs."));
    form->addRow(tr("Credit factor:"), m_autoFactorSpin);
    m_autoThresholdSpin = new QSpinBox(m_autoBidGroup);
    m_autoThresholdSpin->setRange(int(Ed3kCreditPrefs::kAutoThresholdMin), 1'000'000'000);
    m_autoThresholdSpin->setValue(2000);
    m_autoThresholdSpin->setSuffix(tr(" credits"));
    m_autoThresholdSpin->setToolTip(tr("The balance at which the autostart begins. It then runs down to about 20 credits."));
    form->addRow(tr("Autostart threshold:"), m_autoThresholdSpin);
    groupLayout->addLayout(form);
    m_autoNoteLabel = noteLabel(QString(), m_autoBidGroup);
    groupLayout->addWidget(m_autoNoteLabel);
    layout->addWidget(m_autoBidGroup);
    layout->addStretch();

    connect(m_autoBidGroup, &QGroupBox::toggled, this, edited);
    connect(m_autoFactorSpin, &QDoubleSpinBox::valueChanged, this, edited);
    connect(m_autoThresholdSpin, &QSpinBox::valueChanged, this, edited);
    // no effect while ed3k is off
    connect(m_enabledCheck, &QCheckBox::toggled, this, [this] { updateAutoCredits(); });
    updateAutoCredits();
    return page;
}

void Ed3kOptionsPages::updateAutoCredits()
{
    if (!m_autoBidGroup)
        return;
    const bool ed3kOn = m_enabledCheck->isChecked();
    m_autoBidGroup->setEnabled(ed3kOn);
    if (!ed3kOn) {
        m_autoNoteLabel->setText(tr("ed3k is off: this has no effect."));
        return;
    }
    const QCborMap& c = m_creditStatus;
    if (c.isEmpty() || !c.value(QStringLiteral("configured")).toBool()) {
        m_autoNoteLabel->clear();
        return;
    }
    if (c.value(QStringLiteral("overdrawn")).toBool()) {
        m_autoNoteLabel->setText(tr("The account is overdrawn: nothing is bid."));
        return;
    }
    const qint64 balance = c.value(QStringLiteral("balance")).toInteger(-1);
    const QString bal = balance < 0 ? tr("not read yet") : creditsText(balance);
    m_autoNoteLabel->setText(c.value(QStringLiteral("autoActive")).toBool()
                                 ? tr("Highspeed download is running. Balance: %1").arg(bal)
                                 : tr("Highspeed download is not running. Balance: %1").arg(bal));
}

} // namespace eMule

