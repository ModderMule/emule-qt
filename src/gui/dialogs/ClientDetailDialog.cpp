#include "pch.h"
/// @file ClientDetailDialog.cpp
/// @brief Client detail dialog implementation.

#include "ClientDetailDialog.h"

#include <QCborArray>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QHostInfo>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

#include "app/UiState.h"
#include "net/Address.h"
#include "prefs/Preferences.h"
#include "app/SkinIcons.h"
#include "utils/AddressSortKey.h"
#include "utils/ClientIcons.h"
#include "utils/CountryFlags.h"
#include "utils/DialogSizing.h"
#include "utils/OtherFunctions.h"
#include "utils/StringUtils.h"
#include "utils/TooltipDelayStyle.h"

namespace eMule {

// ── helpers ────────────────────────────────────────────────────────────

namespace {

QString str(const QCborMap& m, QLatin1StringView key)
{
    return m.value(key).toString();
}

qint64 num(const QCborMap& m, QLatin1StringView key)
{
    return m.value(key).toInteger();
}

double dbl(const QCborMap& m, QLatin1StringView key)
{
    return m.value(key).toDouble();
}

bool isEd3k(const QCborMap& m)
{
    return str(m, QLatin1StringView("network")) == QLatin1String("ed3k");
}

/// Title name: an ed3k row is named by its node ID's start
QString titleName(const QCborMap& d)
{
    const QString name = str(d, QLatin1StringView("userName"));
    if (!isEd3k(d))
        return name;
    return d.value(QLatin1StringView("anonymous")).toBool()
        ? ClientDetailDialog::tr("anonymous (ed3k)")
        : ClientDetailDialog::tr("%1 (ed3k)").arg(name);
}

/// Bold "label:" caption, right-aligned as in a QFormLayout.
QLabel* gridCaption(const QString& label, const QString& tip = {})
{
    auto* caption = new QLabel(QStringLiteral("<b>%1:</b>").arg(label));
    caption->setAlignment(Qt::AlignRight | Qt::AlignTop);
    caption->setToolTip(tip);
    return caption;
}

/// Caption at (@p row, @p col), one-line value right of it spanning @p span columns.
/// @p tip goes on both.
void addGridRow(QGridLayout* grid, int row, int col, const QString& label, const QString& value,
                const QString& tip = {}, int span = 1)
{
    // One line, no word wrap: a wrapping label in a grid has its height measured at the
    // wrong width, which left a scrollbar and a gap under the groups
    auto* valueLabel = new QLabel(value);
    valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    valueLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    valueLabel->setToolTip(tip);
    grid->addWidget(gridCaption(label, tip), row, col);
    grid->addWidget(valueLabel, row, col + 1, 1, span, Qt::AlignTop);
}

/// Flag + long country name (MorphXT IP2Country); @p fallback without a code.
QWidget* countryValue(const QString& cc, const QString& fallback)
{
    auto* country = new QWidget;
    auto* rowLayout = new QHBoxLayout(country);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    if (!cc.isEmpty()) {
        auto* flagLabel = new QLabel(country);
        flagLabel->setPixmap(CountryFlags::flag(cc).pixmap(QSize(18, 16)));
        rowLayout->addWidget(flagLabel);
    }
    rowLayout->addWidget(new QLabel(cc.isEmpty() ? fallback : CountryFlags::tooltip(cc), country), 1);
    return country;
}

/// The same peer seen by the other network (GetClientDetails `ed2kClient` / `ed3kClient`).
QCborMap otherView(const QCborMap& d)
{
    return d.value(QLatin1StringView(isEd3k(d) ? "ed2kClient" : "ed3kClient")).toMap();
}

/// Country codes of both views: a change rebuilds the flag widgets.
QString ccKey(const QCborMap& d)
{
    return str(d, QLatin1StringView("cc")) + u'/' + str(otherView(d), QLatin1StringView("cc"));
}

/// The app style with the tabs of a QTabWidget on the left (macOS centres them).
/// App base style, not the desktop one: Win10 dark Fusion fallback (#184)
class LeftTabsStyle final : public TooltipDelayStyle {
public:
    LeftTabsStyle()
        : TooltipDelayStyle(createAppBaseStyle())
    {
    }

    int styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                  QStyleHintReturn* returnData) const override
    {
        if (hint == SH_TabBar_Alignment)
            return Qt::AlignLeft;
        return TooltipDelayStyle::styleHint(hint, option, widget, returnData);
    }
};

} // anonymous namespace

// ── constructor ────────────────────────────────────────────────────────

ClientDetailDialog::ClientDetailDialog(const QCborMap& d, QWidget* parent)
    : DetailDialog(parent, ContentScroll::On)
{
    setAttribute(Qt::WA_DeleteOnClose);

    // Wider than MFC's 322 DLU: General pairs two value columns.
    // Height is the content's to decide: the row count varies with the client (a friend
    // slot adds a row) and so does the wrapped height of the file-name values.
    setDesignedSize(QSize(640, 0), QSize(720, 0));

    ClientDetailDialog::setDetails(d);
}

ClientDetailDialog::~ClientDetailDialog()
{
    for (const int id : std::as_const(m_hostLookups))
        QHostInfo::abortHostLookup(id);
}

// ── re-target ──────────────────────────────────────────────────────────

void ClientDetailDialog::setDetails(const QCborMap& d)
{
    // a rebuild of the same client keeps the tab shown; else its own network's
    const bool ed3k = isEd3k(d);
    const auto* oldTabs = qobject_cast<QTabWidget*>(m_content);
    const int tab = oldTabs && str(d, QLatin1StringView("userHash")) == subjectKey() ? oldTabs->currentIndex()
                  : ed3k ? 1 : 0;
    delete m_content;
    m_content = buildPage(d);
    if (auto* tabs = qobject_cast<QTabWidget*>(m_content))
        tabs->setCurrentIndex(std::min(tab, tabs->count() - 1));
    contentLayout()->addWidget(m_content);

    m_cc = ccKey(d);

    setSubjectKey(str(d, QLatin1StringView("userHash")));
    setWindowTitle(tr("Client Details: %1").arg(titleName(d)));
    // ed3k: the same icon as its list row (ClientEd3k.ico); eD2K keeps the app's
    setWindowIcon(ed3k ? ed3kClientIcon() : QIcon());

    fitToContent();
}

void ClientDetailDialog::applyDetails(const QCborMap& d)
{
    if (!m_content || str(d, QLatin1StringView("userHash")) != subjectKey()
        || ccKey(d) != m_cc) {
        setDetails(d);
        return;
    }

    // Build off-screen and copy the texts over when the rows line up
    const std::unique_ptr<QWidget> fresh(buildPage(d));
    if (!copyValueTexts(m_content, fresh.get())) {
        setDetails(d);
        return;
    }
    setWindowTitle(tr("Client Details: %1").arg(titleName(d)));
}

// ── private helpers ────────────────────────────────────────────────────

QWidget* ClientDetailDialog::buildPage(const QCborMap& d)
{
    // one tab per network that knows the peer (eD2K compat mode: user hash derived from the
    // node ID), eD2K first; the bar shows even with one tab: which network it is
    const bool ed3k = isEd3k(d);
    const QCborMap other = otherView(d);
    const QCborMap* ed2kView = !ed3k ? &d : other.isEmpty() ? nullptr : &other;
    const QCborMap* ed3kView = ed3k ? &d : other.isEmpty() ? nullptr : &other;
    auto* tabs = new QTabWidget;
    // left-aligned tabs; a style sheet would turn the whole page non-native on macOS
    auto* style = new LeftTabsStyle;
    style->setParent(tabs);
    tabs->setStyle(style);
    DialogSizing::enableHeightForWidth(tabs);
    if (ed2kView)
        tabs->addTab(buildContent(*ed2kView), skinIcon(QStringLiteral(":/icons/Kad.ico")), QStringLiteral("eD2K"));
    if (ed3kView)
        tabs->addTab(buildEd3kContent(*ed3kView), skinIcon(QStringLiteral(":/icons/Ed3k.ico")), QStringLiteral("ed3k"));
    return tabs;
}

QWidget* ClientDetailDialog::buildContent(const QCborMap& d)
{
    auto* page = new QWidget;
    auto* mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    DialogSizing::enableHeightForWidth(page);

    // ── General group ──────────────────────────────────────────────────
    {
        // One grid with a shared label column, paired like MFC's IDD_SOURCEDETAILWND:
        // ID beside the name, the hash full width, then client | connection
        auto* group = new QGroupBox(tr("General"));
        auto* grid  = new QGridLayout(group);
        grid->setColumnStretch(1, 3);   // software names are the longest values
        grid->setColumnStretch(3, 2);
        grid->setHorizontalSpacing(12);
        DialogSizing::enableHeightForWidth(group);

        // Labels and values as MFC CClientDetailPage (srchybrid/ClientDetailDialog.cpp:86-175)
        const QString unknown = QStringLiteral("?");
        const QString userName = str(d, QLatin1StringView("userName"));
        const bool lowID = d.value(QLatin1StringView("hasLowID")).toBool();
        addGridRow(grid, 0, 0, tr("Name"), userName.isEmpty() ? unknown : userName,
                   tr("User name the client sent in its hello. Anyone can pick any name."));
        addGridRow(grid, 0, 2, tr("ID"), lowID ? tr("Low ID") : tr("High ID"),
                   tr("High ID: the client accepts incoming TCP connections.\n"
                      "Low ID: it is firewalled and can only be reached through a callback."));
        addGridRow(grid, 1, 0, tr("Hash"), d.value(QLatin1StringView("hasValidHash")).toBool(true)
                                           ? str(d, QLatin1StringView("userHash")) : unknown,
                   tr("User hash: the client's eD2K identity. Credits are stored under it."), 3);

        addGridRow(grid, 2, 0, tr("Software"), str(d, QLatin1StringView("software")),
                   tr("Client program and version, as detected from its hello."));

        // Country (MorphXT IP2Country): flag + long name
        grid->addWidget(gridCaption(tr("Country"), tr("Country of the client's IP address (IP2Country).")), 3, 0);
        grid->addWidget(countryValue(str(d, QLatin1StringView("cc")), unknown), 3, 1, Qt::AlignTop);

        const bool kadConn = d.value(QLatin1StringView("kadConnected")).toBool();
        const QString peerIp = str(d, QLatin1StringView("addr"));
        const auto peerPort = static_cast<uint16_t>(num(d, QLatin1StringView("port")));
        addGridRow(grid, 6, 0, tr("Peer IP"),
                   peerIp.isEmpty() ? tr("Unknown") : peerPort != 0 ? addressWithPort(peerIp, peerPort) : peerIp,
                   tr("Peer connection address (IPv4 or IPv6)."), 3);
        addHostnameRow(grid, 7, peerIp);
        addGridRow(grid, 4, 0, tr("Kad"), kadConn ? tr("Connected") : tr("Disconnected"),
                   tr("Whether the client told us it is connected to Kad (it sent a Kad port)."));

        const QString obfuscation = str(d, QLatin1StringView("obfuscation"));
        addGridRow(grid, 5, 0, tr("Obfuscation"),
                   obfuscation == QLatin1String("enabled") ? tr("Enabled")
                   : obfuscation == QLatin1String("supported") ? tr("Supported")
                   : tr("Not supported or disabled"),
                   tr("Protocol obfuscation for this client.\n"
                      "Enabled: the connection to it is obfuscated.\n"
                      "Supported: it can obfuscate, this connection does not.\n"
                      "Not supported or disabled: it cannot or will not obfuscate."));

        // Server. Prefer the string form: "serverIP" is 0 for an IPv6 server, which would
        // otherwise render the row as "\u2014" even though the address is known.
        const auto srvIP   = static_cast<uint32>(num(d, QLatin1StringView("serverIP")));
        const auto srvPort = static_cast<uint16>(num(d, QLatin1StringView("serverPort")));
        const QString srvName = str(d, QLatin1StringView("serverName"));
        const QString srvAddr = str(d, QLatin1StringView("serverAddr"));
        QString srvEndpoint;
        if (!srvAddr.isEmpty())
            srvEndpoint = Endpoint(Address::fromString(srvAddr), srvPort).toString();
        else if (srvIP != 0)
            srvEndpoint = ipstr(srvIP, srvPort);

        addGridRow(grid, 2, 2, tr("Server IP"), srvEndpoint.isEmpty() ? unknown : srvEndpoint,
                   tr("eD2K server the client is logged in to, if known."));
        addGridRow(grid, 3, 2, tr("Server Name"),
                   srvEndpoint.isEmpty() || srvName.isEmpty() ? unknown : srvName,
                   tr("Name of that server, when it is in our server list."));

        const bool creditsKnown = d.value(QLatin1StringView("creditsKnown")).toBool();
        const QString ident = str(d, QLatin1StringView("identification"));
        addGridRow(grid, 4, 2, tr("Identification"),
                   !creditsKnown ? unknown
                   : ident == QLatin1String("ok") ? tr("Successful")
                   : ident == QLatin1String("failed") ? tr("Invalid")
                   : tr("Not supported or disabled"),
                   tr("Secure identification: the client proved with its public key that it owns "
                      "its user hash.\n"
                      "Successful: credits are given to it.\n"
                      "Invalid: the proof failed, no credits.\n"
                      "Not supported or disabled: no proof, credits are not used."));

        // Not in MFC: NAT-T for LowID peers — the route in use, else what the peer supports
        const QString natRoute = str(d, QLatin1StringView("natRoute"));
        QString natt;
        if (natRoute == QLatin1String("quic")) {
            natt = tr("Connected (%1)").arg(tr("QUIC"));
        } else if (natRoute == QLatin1String("ipv6")) {
            natt = tr("Connected (%1)").arg(tr("IPv6 fallback"));
        } else {
            QStringList ways;
            if (d.value(QLatin1StringView("supportsIPv6")).toBool())
                ways << tr("IPv6 fallback");
            if (d.value(QLatin1StringView("supportsNatTQuic")).toBool())
                ways << tr("QUIC");
            natt = ways.isEmpty() ? tr("Not supported")
                                  : tr("Supported (%1)").arg(ways.join(QStringLiteral(", ")));
        }
        addGridRow(grid, 5, 2, tr("NAT-T"), natt,
                   tr("NAT traversal: lets two firewalled (Low ID) clients connect directly.\n"
                      "Connected: the route the current connection took.\n"
                      "Supported: the routes the client offers (QUIC over a punched UDP hole, "
                      "or a direct IPv6 connection)."));

        mainLayout->addWidget(group);
    }

    // ── Callback group (not MFC, ext controls only): how a LowID client can be reached
    if (const QCborMap cb = d.value(QLatin1StringView("callback")).toMap();
        thePrefs.showExtControls() && !cb.isEmpty()) {
        auto* group = new QGroupBox(tr("LowID Callback"));
        auto* form  = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        DialogSizing::enableHeightForWidth(group);
        const auto flag = [&cb](QLatin1StringView key) { return cb.value(key).toBool(); };
        const QString yes = tr("Available");
        const QString no  = tr("Not available");

        // Kad buddy that relays callbacks for it: flag + country (ISO) + port, never its IP
        const int buddyPort = int(cb.value(QLatin1StringView("buddyPort")).toInteger());
        const auto place = [this](const QString& cc) {
            const QString html = CountryFlags::tooltipHtml(cc);
            return html.isEmpty() ? tr("Unknown country") : html;
        };
        // one form for every buddy: IPv4 country, else the IPv6 one
        QString cc = str(cb, QLatin1StringView("buddyCC"));
        if (cc.isEmpty())
            cc = str(cb, QLatin1StringView("buddy6CC"));
        QString buddyText;
        if (buddyPort > 0 || flag(QLatin1StringView("buddyIPv6"))) {
            buddyText = buddyPort > 0 ? tr("%1, port %2").arg(place(cc)).arg(buddyPort) : place(cc);
        } else if (flag(QLatin1StringView("buddyID")))
            buddyText = tr("Known, address not resolved yet");
        else
            buddyText = tr("None");
        addDetailRow(form, tr("Kad buddy"), buddyText,
                     tr("A High ID Kad node that keeps a connection to this Low ID client and "
                        "forwards callback requests to it."))
            ->setTextFormat(Qt::RichText);   // the flag image
        addDetailRow(form, tr("Kad buddy callback"), flag(QLatin1StringView("kadBuddy")) ? yes : no,
                     tr("We can ask the client's Kad buddy to make it connect to us."));
        addDetailRow(form, tr("Direct UDP callback"), flag(QLatin1StringView("directUdp")) ? yes : no,
                     tr("The client is firewalled on TCP but its UDP port is open: we can ask it "
                        "over UDP to connect to us. Needs us to be reachable on TCP."));

        // Same-server OP_CALLBACKREQUEST
        QString server;
        if (!flag(QLatin1StringView("sameServer")))
            server = tr("Not on our server");
        else
            server = flag(QLatin1StringView("serverCallback")) ? yes : tr("Not available (we have a Low ID)");
        addDetailRow(form, tr("Server callback"), server,
                     tr("We are a High ID on the client's server: the server can tell it to "
                        "connect to us."));

        // Foreign callback through the peer's own server (UDP flag 0x100000)
        const QString foreign = str(cb, QLatin1StringView("foreign"));
        const QString foreignServer = str(cb, QLatin1StringView("foreignServer"));
        QString cross;
        if (foreign == QLatin1String("same"))
            cross = tr("Not needed (same server)");
        else if (foreign == QLatin1String("ready"))
            cross = tr("Offered by its server");
        else if (foreign == QLatin1String("pending"))
            cross = tr("Checking its server");
        else if (foreign == QLatin1String("no"))
            cross = tr("Not offered by its server");
        else if (foreign == QLatin1String("notCapable"))
            cross = tr("Not possible (peer logged in without NAT callback)");
        else
            cross = tr("Its server is unknown");
        if (!foreignServer.isEmpty() && foreign != QLatin1String("same"))
            cross += QStringLiteral(" \u2014 %1").arg(foreignServer);
        addDetailRow(form, tr("Cross-server callback"), cross,
                     tr("The client is logged in to another server: we ask that server over UDP "
                        "to make it connect to us. Only servers that offer this callback can."));

        // NAT-T introducers in try order (HighID: only the foreign callback)
        const QCborMap natt = cb.value(QLatin1StringView("natt")).toMap();
        if (!natt.isEmpty()) {
            QStringList paths;
            for (const QCborValue& v : natt.value(QLatin1StringView("paths")).toArray()) {
                const QString k = v.toString();
                if (k == QLatin1String("ed2kServer"))     paths << tr("Server hole punch");
                else if (k == QLatin1String("lugdunum"))  paths << tr("Server NAT callback");
                else if (k == QLatin1String("prNat"))     paths << QStringLiteral("PR_NAT");
                else if (k == QLatin1String("kadBuddy"))  paths << tr("Kad buddy");
                else if (k == QLatin1String("foreign"))   paths << tr("Cross-server callback");
                else if (k == QLatin1String("directUdp")) paths << tr("Direct UDP");
            }
            const auto nflag = [&natt](QLatin1StringView key) { return natt.value(key).toBool(); };
            QString text = paths.isEmpty() ? tr("None") : paths.join(QStringLiteral(", "));
            if (!nflag(QLatin1StringView("enabled")))
                text = tr("NAT-T disabled");
            else if (nflag(QLatin1StringView("active")))
                text += QStringLiteral(" (%1)").arg(tr("in progress"));
            else if (nflag(QLatin1StringView("failed")))
                text += QStringLiteral(" (%1)").arg(tr("failed this session"));
            else if (!paths.isEmpty() && !nflag(QLatin1StringView("dataPath")))
                text += QStringLiteral(" (%1)").arg(tr("no QUIC or IPv6 data path"));
            addDetailRow(form, tr("NAT-T rendezvous"), text, tr(
                "Introducers that can tell both firewalled sides each other's UDP address, so "
                "they can punch a hole and connect directly. Listed in the order they are tried.\n"
                "\n"
                "Server hole punch: our server's own hole-punch extension, for a Low ID on "
                "our server.\n"
                "Server NAT callback: our server relays a NAT callback (Lugdunum-style), for "
                "a Low ID on our server.\n"
                "PR_NAT: our server's separate NAT service, for a client on our server whose "
                "user hash is known.\n"
                "Kad buddy: the request is relayed through the client's Kad buddy.\n"
                "Cross-server callback: the client's own server, when it is another one, "
                "wakes it.\n"
                "Direct UDP: no introducer; the client published a Kad source with QUIC "
                "NAT-T and open UDP, so we ask it directly.\n"
                "\n"
                "No QUIC or IPv6 data path: an introduction could succeed, but we have neither "
                "QUIC nor a public IPv6 address to carry the connection."));
        }

        mainLayout->addWidget(group);
    }

    // ── Transfer group ─────────────────────────────────────────────────
    {
        auto* group = new QGroupBox(tr("Transfer"));
        auto* form  = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        DialogSizing::enableHeightForWidth(group);

        // MFC words this group from the remote client's side: "downloaded" is what
        // they got from us, "uploaded" what they sent us.
        const QString none = QStringLiteral("-");
        const bool creditsKnown = d.value(QLatin1StringView("creditsKnown")).toBool();

        const QString upFileName = str(d, QLatin1StringView("uploadFileName"));
        addDetailRow(form, tr("Currently downloading"), upFileName.isEmpty() ? none : upFileName,
                     tr("Our file the client downloads or waits for in our upload queue."));
        const QString reqFileName = str(d, QLatin1StringView("reqFileName"));
        addDetailRow(form, tr("Requested to upload"), reqFileName.isEmpty() ? none : reqFileName,
                     tr("Our download for which the client is a source."));

        // Lifetime of the client object, not the current slot session (IDC_DDOWN / IDC_DDUP)
        addDetailRow(form, tr("Downloaded (this session)"),
                     formatByteSize(static_cast<uint64>(num(d, QLatin1StringView("transferredUp")))),
                     tr("What the client downloaded from us since we met it in this run."));
        addDetailRow(form, tr("Uploaded (this session)"),
                     formatByteSize(static_cast<uint64>(num(d, QLatin1StringView("transferredDown")))),
                     tr("What the client uploaded to us since we met it in this run."));

        // Averages over the slot time, ours up is theirs down (MorphXT; MFC shows the
        // current rate under the same labels, IDC_DAVDR / IDC_DAVUR)
        addDetailRow(form, tr("Average Downloadrate"),
                     formatByteRate(static_cast<uint64>(num(d, QLatin1StringView("avgUpDatarate")))),
                     tr("The client's average download rate from us over its upload slot."));
        addDetailRow(form, tr("Average Upload rate"),
                     formatByteRate(static_cast<uint64>(num(d, QLatin1StringView("avgDownDatarate")))),
                     tr("The client's average upload rate to us while we download from it."));

        // Credits (IDC_DDOWNTOTAL / IDC_DUPTOTAL)
        addDetailRow(form, tr("Downloaded total"), creditsKnown
            ? formatByteSize(static_cast<uint64>(num(d, QLatin1StringView("uploadedTotal")))) : QStringLiteral("?"),
            tr("Everything the client ever downloaded from us (from the credit file)."));
        addDetailRow(form, tr("Uploaded total"), creditsKnown
            ? formatByteSize(static_cast<uint64>(num(d, QLatin1StringView("downloadedTotal")))) : QStringLiteral("?"),
            tr("Everything the client ever uploaded to us (from the credit file). This earns it credits."));

        mainLayout->addWidget(group);
    }

    // ── Scores group ───────────────────────────────────────────────────
    {
        auto* group = new QGroupBox(tr("Scores"));
        auto* form  = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        DialogSizing::enableHeightForWidth(group);

        const QString unknown = QStringLiteral("?");
        const bool creditsKnown = d.value(QLatin1StringView("creditsKnown")).toBool();

        // Theirs, then in brackets what this peer grants us (MorphXT ownCredits)
        addDetailRow(form, tr("DL/UP Modifier"), creditsKnown
            ? QStringLiteral("%1  [%2]").arg(QString::number(dbl(d, QLatin1StringView("scoreRatio")), 'f', 1),
                                             QString::number(dbl(d, QLatin1StringView("myScoreRatio")), 'f', 1))
            : unknown,
            tr("Credit modifier (1 to 10) from what the client uploaded to us; it multiplies "
               "its queue score.\n"
               "In brackets: the modifier the client should give us, from what we uploaded to it."));

        // MFC formats IDC_DRATING as %.1f from an integer score (ClientDetailDialog.cpp:159),
        // so it always renders one trailing zero. Match it.
        const auto rating = num(d, QLatin1StringView("rating"));
        addDetailRow(form, tr("Rating (total)"),
                     creditsKnown && !str(d, QLatin1StringView("userName")).isEmpty()
                         ? QString::number(static_cast<double>(rating), 'f', 1) : unknown,
                     tr("Base score: 100 times the credit modifier, with penalties for old clients "
                        "and leechers. No waiting time or file priority."));

        // A number, "(Friend Slot)", or "-" outside our upload path (ClientDetailDialog.cpp:164-170)
        QString score = QStringLiteral("-");
        if (creditsKnown && !d.value(QLatin1StringView("uploadIdle")).toBool()) {
            score = d.value(QLatin1StringView("friendSlot")).toBool()
                ? tr("(Friend Slot)") : QString::number(num(d, QLatin1StringView("score")));
        }
        addDetailRow(form, tr("Uploadqueue Score"), score,
                     tr("The client's current score in our upload queue: waiting time × credit "
                        "modifier × file priority. The highest score gets the next slot."));

        // ClientAnalyzer factor, sent only while it is on
        if (d.contains(QLatin1StringView("analyzerScore"))) {
            const QString reasons = str(d, QLatin1StringView("analyzerReasons"));
            const QString factor = QString::number(dbl(d, QLatin1StringView("analyzerScore")), 'f', 2);
            addDetailRow(form, tr("Client Analyzer"),
                         reasons.isEmpty() ? factor : QStringLiteral("%1  (%2)").arg(factor, reasons),
                         tr("Client Analyzer factor from the client's behaviour; it multiplies its "
                            "queue score. In brackets: why it is below 1."));
        }

        mainLayout->addWidget(group);
    }

    mainLayout->addStretch();
    return page;
}

QWidget* ClientDetailDialog::buildEd3kContent(const QCborMap& d)
{
    auto* page = new QWidget;
    auto* mainLayout = new QVBoxLayout(page);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    DialogSizing::enableHeightForWidth(page);

    const QString unknown = QStringLiteral("?");
    const QString none = QStringLiteral("-");
    const bool anonymous = d.value(QLatin1StringView("anonymous")).toBool();

    // ── General: the node, laid out like the eD2K page ─────────────────
    {
        auto* group = new QGroupBox(tr("General"));
        auto* grid  = new QGridLayout(group);
        grid->setColumnStretch(1, 3);
        grid->setColumnStretch(3, 2);
        grid->setHorizontalSpacing(12);
        DialogSizing::enableHeightForWidth(group);

        // Hello.nick: the user name of its Options; may be empty
        const QString nick = str(d, QLatin1StringView("nick"));
        addGridRow(grid, 0, 0, tr("Name"), anonymous ? tr("anonymous") : nick.isEmpty() ? unknown : nick,
                   tr("Nick the node sent in its hello. Anyone can pick any name."));
        addGridRow(grid, 0, 2, tr("ed3k Type"), anonymous ? tr("Anonymous") : tr("Node"),
                   tr("Which of our two keys the peer is talking to."));
        // the key's value label: what the two keys are
        if (auto* keyValue = qobject_cast<QLabel*>(grid->itemAtPosition(0, 3)->widget()))
            keyValue->setToolTip(anonymous
                ? tr("It downloads from our anonymous node: a second key of ours, reached through a "
                     "path of other nodes. Neither side sees the other's IP address.")
                : tr("It downloads from our node key: the key our node ID is made from. It connects "
                     "to us directly and sees our IP address."));
        addGridRow(grid, 1, 0, tr("Node ID"), str(d, QLatin1StringView("nodeId")),
                   tr("The peer's ed3k identity, derived from its public key."), 3);

        const bool helloKnown = d.value(QLatin1StringView("helloKnown")).toBool();
        const QString peerIp = str(d, QLatin1StringView("addr"));
        const auto peerPort = static_cast<uint16_t>(num(d, QLatin1StringView("port")));
        addGridRow(grid, 5, 0, tr("Peer IP"), anonymous ? tr("Hidden")
                   : peerIp.isEmpty() ? tr("Unknown") : peerPort != 0 ? addressWithPort(peerIp, peerPort) : peerIp,
                   tr("Peer connection address (IPv4 or IPv6)."), 3);
        // the anonymous queue never tells an address
        addHostnameRow(grid, 6, anonymous ? QString() : peerIp);
        addGridRow(grid, 2, 0, tr("Software"), str(d, QLatin1StringView("software")),
                   tr("Client program and version from its hello."));
        // Hello.caps bit ED2K (§2): it speaks eD2K too
        addGridRow(grid, 2, 2, tr("eD2K"), !helloKnown ? unknown
                   : d.value(QLatin1StringView("ed2k")).toBool() ? tr("Yes") : tr("No"),
                   tr("Whether the peer also speaks eD2K. Then a second tab shows it on that network."));

        // the anonymous queue never tells an address (hops.md §1)
        grid->addWidget(gridCaption(tr("Country"), tr("Country of the peer's IP address (IP2Country). "
                                                      "Hidden for anonymous peers.")), 3, 0);
        grid->addWidget(countryValue(str(d, QLatin1StringView("cc")), anonymous ? tr("hidden") : unknown),
                        3, 1, Qt::AlignTop);
        addGridRow(grid, 3, 2, tr("Connected"),
                   d.value(QLatin1StringView("isConnected")).toBool() ? tr("Yes") : tr("No"),
                   tr("Whether a connection to the peer is open now."));
        // Hello.caps ROUTER (§2): in routing tables; else a leaf. The anonymous key never routes
        addGridRow(grid, 4, 0, tr("Role"), anonymous ? tr("hidden") : !helloKnown ? unknown
                   : d.value(QLatin1StringView("indexNode")).toBool()
                       ? (d.value(QLatin1StringView("router")).toBool() ? tr("Index node, router") : tr("Index node"))
                   : d.value(QLatin1StringView("router")).toBool() ? tr("Router") : tr("Leaf"),
                   tr("Router: reachable, kept in other nodes' routing tables.\n"
                      "Index node: stores and answers searches.\n"
                      "Leaf: not reachable from outside, only connects out."));
        if (thePrefs.showExtControls())
            addGridRow(grid, 4, 2, tr("LAN"), d.value(QLatin1StringView("lan")).toBool() ? tr("Yes") : tr("No"),
                       tr("The peer is on our local network."));

        mainLayout->addWidget(group);
    }

    // ── Transfer: its turn in our upload queue, our download from it ──
    {
        auto* group = new QGroupBox(tr("Transfer"));
        auto* form  = new QFormLayout(group);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        DialogSizing::enableHeightForWidth(group);

        // in our upload queue (else only a source of ours)
        if (!str(d, QLatin1StringView("uploadState")).isEmpty()) {
            const bool holding = str(d, QLatin1StringView("uploadState")) == QLatin1String("Transferring");
            const QString file = str(d, QLatin1StringView("uploadFileName"));
            addDetailRow(form, tr("Currently downloading"), file.isEmpty() ? none : file,
                         tr("Our file the peer downloads or waits for in our upload queue."));
            const qint64 size = num(d, QLatin1StringView("uploadFileSize"));
            addDetailRow(form, tr("File size"), size > 0 ? formatByteSize(static_cast<uint64>(size)) : unknown,
                         tr("Size of that file."));
            addDetailRow(form, tr("Root hash"), str(d, QLatin1StringView("root")),
                         tr("The ed3k root hash that identifies that file."));

            const int place = static_cast<int>(num(d, QLatin1StringView("queueRank")));
            addDetailRow(form, tr("Upload state"), holding ? tr("Transferring")
                         : place > 0 ? tr("On queue (place %1)").arg(place) : tr("On queue"),
                         tr("Transferring: the peer holds an upload slot.\n"
                            "On queue: it waits for one, at the place shown."));
            const qint64 heldMs = num(d, QLatin1StringView(holding ? "uploadStartDelay" : "waitStartTime"));
            addDetailRow(form, holding ? tr("Upload time") : tr("Waiting time"),
                         formatDuration(std::chrono::seconds(heldMs / 1000)),
                         holding ? tr("How long the peer has held its upload slot.")
                                 : tr("How long the peer has waited in our upload queue."));
            addDetailRow(form, tr("Downloaded (this turn)"),
                         formatByteSize(static_cast<uint64>(num(d, QLatin1StringView("sessionUp")))),
                         tr("What the peer downloaded from us in its current slot."));
            // the turn's 16 MiB / 1 h reached (SlotUsed due): the slot goes on next
            if (holding)
                addDetailRow(form, tr("Turn ending"),
                             d.value(QLatin1StringView("due")).toBool() ? tr("Yes") : tr("No"),
                             tr("The slot reached its limit (16 MiB or 1 hour) and goes to the next "
                                "peer in the queue."));
        }

        // a source of one of our downloads: its stream there (§12.5: one per uploader)
        if (const QCborMap src = d.value(QLatin1StringView("source")).toMap(); !src.isEmpty()) {
            const QString file = str(src, QLatin1StringView("fileName"));
            addDetailRow(form, tr("Requested to upload"), file.isEmpty() ? str(src, QLatin1StringView("root")) : file,
                         tr("Our download for which the peer is a source."));
            const bool sending = str(src, QLatin1StringView("state")) == QLatin1String("Downloading");
            addDetailRow(form, tr("Download state"), sending ? tr("Transferring") : tr("On queue"),
                         tr("Transferring: the peer sends us data.\n"
                            "On queue: we wait in its upload queue."));
            const qint64 parts = num(src, QLatin1StringView("parts"));
            addDetailRow(form, tr("Available parts"),
                         !src.value(QLatin1StringView("haveKnown")).toBool() ? unknown
                         : QStringLiteral("%1 / %2").arg(num(src, QLatin1StringView("availParts"))).arg(parts),
                         tr("Parts of the file the peer has, of all parts."));
        }

        // this daemon run, worded from the peer's side like the eD2K page: its download is our upload
        if (thePrefs.showExtControls()) {
            const auto sent = static_cast<uint64>(num(d, QLatin1StringView("sentTotal")));
            const auto received = static_cast<uint64>(num(d, QLatin1StringView("receivedTotal")));
            addDetailRow(form, tr("Downloaded (this session)"), formatByteSize(sent),
                         tr("What the peer downloaded from us since the core started."));
            addDetailRow(form, tr("Uploaded (this session)"), formatByteSize(received),
                         tr("What the peer uploaded to us since the core started."));
            addDetailRow(form, tr("Ratio (UL/DL)"), sent == 0 ? none
                         : QString::number(static_cast<double>(received) / static_cast<double>(sent), 'f', 2),
                         tr("What the peer uploaded to us divided by what it downloaded from us."));
        }

        mainLayout->addWidget(group);
    }

    mainLayout->addStretch();
    return page;
}

void ClientDetailDialog::addHostnameRow(QGridLayout* grid, int row, const QString& addr)
{
    if (!theUiState.resolvePeerHostnames() || QHostAddress(addr).isNull())
        return;
    addGridRow(grid, row, 0, tr("Hostname"), hostnameText(addr),
               tr("Name the peer's IP address resolves to (reverse DNS)."), 3);
    // found again when the lookup finishes
    if (auto* item = grid->itemAtPosition(row, 1); item && item->widget()) {
        item->widget()->setObjectName(QStringLiteral("peerHostname"));
        item->widget()->setProperty("peerAddr", addr);
    }
}

QString ClientDetailDialog::hostnameText(const QString& addr)
{
    if (const auto it = m_hostnames.constFind(addr); it != m_hostnames.cend())
        return it->isEmpty() ? tr("Not found") : *it;
    if (!m_hostLookups.contains(addr)) {
        const int id = QHostInfo::lookupHost(addr, this, [this, addr](const QHostInfo& info) {
            m_hostLookups.remove(addr);
            // Qt returns the IP itself when there is no PTR record
            const bool found = info.error() == QHostInfo::NoError
                && QHostAddress(info.hostName()).isNull();
            m_hostnames.insert(addr, found ? info.hostName() : QString());
            if (!m_content)
                return;
            for (auto* label : m_content->findChildren<QLabel*>(QStringLiteral("peerHostname"))) {
                if (label->property("peerAddr").toString() == addr)
                    label->setText(hostnameText(addr));
            }
        });
        m_hostLookups.insert(addr, id);
    }
    return tr("Resolving…");
}

} // namespace eMule
