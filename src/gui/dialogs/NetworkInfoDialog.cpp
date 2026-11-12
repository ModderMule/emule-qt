#include "pch.h"
/// @file NetworkInfoDialog.cpp
/// @brief Network Information dialog — implementation.

#include "dialogs/NetworkInfoDialog.h"
#include "utils/Appearance.h"

#include "app/IpcClient.h"

#include "IpcMessage.h"
#include "prefs/Preferences.h"
#include "utils/DialogSizing.h"
#include "utils/StringUtils.h"

#include <QCborArray>
#include <QCborMap>
#include <QDialogButtonBox>
#include <QHostAddress>
#include <QLocale>
#include <QPointer>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace eMule {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Convert a Kad-style IP (first octet in MSB, same as QHostAddress) to dotted string.
QString ipFromKad(qint64 ip)
{
    return QHostAddress(static_cast<quint32>(ip)).toString();
}

/// Convert an eD2K-style IP (first octet in LSB) to dotted string — byte-swaps for QHostAddress.
QString ipFromEd2k(qint64 ip)
{
    const auto val = static_cast<quint32>(ip);
    const quint32 swapped = ((val & 0xFF) << 24) | ((val & 0xFF00) << 8)
                          | ((val >> 8) & 0xFF00) | ((val >> 24) & 0xFF);
    return QHostAddress(swapped).toString();
}

/// One label/value row of the info tables.
QString row(const QString& label, const QString& value)
{
    return QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(label, value);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

NetworkInfoDialog::NetworkInfoDialog(IpcClient* ipc, QWidget* parent, const QString& anchor)
    : RememberedSizeDialog(parent)
    , m_ipc(ipc)
    , m_anchor(anchor)
{
    setWindowTitle(tr("Network Information"));

    auto* layout = new QVBoxLayout(this);

    m_browser = new QTextBrowser(this);
    m_browser->setOpenExternalLinks(true);
    m_browser->setReadOnly(true);
    layout->addWidget(m_browser);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttonBox);

    m_browser->setHtml(tr("<i>Loading...</i>"));
    requestNetworkInfo();

    DialogSizing::applySize(this, {}, QSize(520, 650), DialogSizing::Fit::Layout);
}

NetworkInfoDialog::~NetworkInfoDialog() = default;

// ---------------------------------------------------------------------------
// IPC request
// ---------------------------------------------------------------------------

void NetworkInfoDialog::requestNetworkInfo()
{
    if (!m_ipc || !m_ipc->isConnected()) {
        m_browser->setHtml(tr("<b>Not connected to daemon.</b>"));
        return;
    }

    Ipc::IpcMessage req(Ipc::IpcMsgType::GetNetworkInfo);
    // The dialog runs modally and is destroyed once closed; a late reply must not follow it.
    m_ipc->sendRequest(std::move(req), [this, self = QPointer<NetworkInfoDialog>(this)](const Ipc::IpcMessage& resp) {
        if (!self)
            return;
        if (!resp.isValid()) {
            m_browser->setHtml(tr("<b>Not connected to daemon.</b>"));
            return;
        }
        populateInfo(resp.fieldMap(1));
    });
}

// ---------------------------------------------------------------------------
// Rich-text generation
// ---------------------------------------------------------------------------

QString NetworkInfoDialog::formatNumber(qint64 value)
{
    return QLocale().toString(value);
}

void NetworkInfoDialog::populateInfo(const QCborMap& info)
{
    m_browser->setHtml(infoHtml(info, thePrefs.showExtControls()));
    if (m_anchor.isEmpty())
        return;
    // after layout, else the anchor's position is not known yet
    QTimer::singleShot(0, this, [this] { m_browser->scrollToAnchor(m_anchor); });
}

QString NetworkInfoDialog::infoHtml(const QCborMap& info, bool extended)
{
    QString html;
    html.reserve(4096);

    html += QStringLiteral("<html><body style='font-family: sans-serif; font-size: %1pt;'>")
                .arg(scaledPointSize(10));

    // -----------------------------------------------------------------------
    // Client
    // -----------------------------------------------------------------------
    const QCborMap client = info.value(QStringLiteral("client")).toMap();
    html += QStringLiteral("<b>%1</b><br>").arg(tr("Client"));
    html += QStringLiteral("<table cellpadding='1'>");
    html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Nick:"))
                .arg(client.value(QStringLiteral("nick")).toString().toHtmlEscaped());
    html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Hash:"))
                .arg(client.value(QStringLiteral("hash")).toString());
    html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("TCP Port:"))
                .arg(client.value(QStringLiteral("tcpPort")).toInteger());
    html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("UDP Port:"))
                .arg(client.value(QStringLiteral("udpPort")).toInteger());
    html += QStringLiteral("</table><br>");

    // -----------------------------------------------------------------------
    // eD2K Network
    // -----------------------------------------------------------------------
    const QCborMap ed2k = info.value(QStringLiteral("ed2k")).toMap();
    const bool ed2kConnected = ed2k.value(QStringLiteral("connected")).toBool();
    const bool ed2kConnecting = ed2k.value(QStringLiteral("connecting")).toBool();

    html += QStringLiteral("<b>%1</b><br>").arg(tr("eD2K Network"));
    html += QStringLiteral("<table cellpadding='1'>");

    // Status
    QString ed2kStatus;
    if (ed2kConnected)
        ed2kStatus = tr("Connected");
    else if (ed2kConnecting)
        ed2kStatus = tr("Connecting");
    else
        ed2kStatus = tr("Disconnected");
    html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Status:")).arg(ed2kStatus);

    if (ed2kConnected) {
        // Users / Files
        const auto totalUsers = ed2k.value(QStringLiteral("totalUsers")).toInteger();
        const auto totalFiles = ed2k.value(QStringLiteral("totalFiles")).toInteger();
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Users:")).arg(formatNumber(totalUsers));
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Files:")).arg(formatNumber(totalFiles));

        // IP:Port
        const auto publicIP = ed2k.value(QStringLiteral("publicIP")).toInteger();
        const auto tcpPort = client.value(QStringLiteral("tcpPort")).toInteger();
        const bool lowID = ed2k.value(QStringLiteral("lowID")).toBool();
        if (lowID && publicIP == 0)
            html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("IP:Port:")).arg(tr("Unknown"));
        else
            html += QStringLiteral("<tr><td>%1</td><td>%2:%3</td></tr>").arg(tr("IP:Port:"))
                        .arg(ipFromEd2k(publicIP)).arg(tcpPort);

        // Client ID
        const auto clientID = ed2k.value(QStringLiteral("clientID")).toInteger();
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("ID:")).arg(clientID);
        html += QStringLiteral("<tr><td></td><td>%1</td></tr>")
                    .arg(lowID ? tr("Low ID") : tr("High ID"));

        // IPv6 + the core's reachability verdict. A v6 session is structurally Low ID,
        // so this row is what says whether inbound v6 works — and whether peers get it.
        const QString ipv6 = ed2k.value(QStringLiteral("publicIPv6")).toString();
        if (!ipv6.isEmpty()) {
            const qint64 reach = ed2k.value(QStringLiteral("ipv6Reachability")).toInteger();
            const qint64 proof = ed2k.value(QStringLiteral("ipv6ProofSource")).toInteger();
            QString v6Status;
            if (reach == 1) {   // IPv6Reachability::Reachable
                QString how;
                switch (proof) {
                case 1: how = tr("server verified"); break;
                case 2: how = tr("inbound peer"); break;
                case 3: how = tr("port mapping"); break;
                case 4: how = tr("assumed"); break;
                default: break;
                }
                v6Status = how.isEmpty() ? tr("Open")
                                         : QStringLiteral("%1 (%2)").arg(tr("Open"), how);
            } else if (reach == 2) {
                v6Status = tr("Firewalled");
            } else {
                v6Status = tr("Not verified, not advertised to peers");
            }
            html += QStringLiteral("<tr><td>%1</td><td>[%2]:%3</td></tr>").arg(tr("IPv6:"))
                        .arg(ipv6.toHtmlEscaped()).arg(tcpPort);
            html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("IPv6 Status:")).arg(v6Status);
        }
    }
    html += QStringLiteral("</table><br>");

    // -----------------------------------------------------------------------
    // eD2K Server (if connected)
    // -----------------------------------------------------------------------
    if (ed2kConnected && ed2k.contains(QStringLiteral("server"))) {
        const QCborMap srv = ed2k.value(QStringLiteral("server")).toMap();
        html += QStringLiteral("<b>%1</b><br>").arg(tr("eD2K Server"));
        html += QStringLiteral("<table cellpadding='1'>");
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Name:"))
                    .arg(srv.value(QStringLiteral("name")).toString().toHtmlEscaped());
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Description:"))
                    .arg(srv.value(QStringLiteral("description")).toString().toHtmlEscaped());
        html += QStringLiteral("<tr><td>%1</td><td>%2:%3</td></tr>").arg(tr("IP:Port:"))
                    .arg(srv.value(QStringLiteral("address")).toString())
                    .arg(srv.value(QStringLiteral("port")).toInteger());
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Version:"))
                    .arg(srv.value(QStringLiteral("version")).toString());
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Users:"))
                    .arg(formatNumber(srv.value(QStringLiteral("users")).toInteger()));
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Files:"))
                    .arg(formatNumber(srv.value(QStringLiteral("files")).toInteger()));

        const bool obfuscated = srv.value(QStringLiteral("obfuscated")).toBool();
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Connections:"))
                    .arg(obfuscated ? tr("Obfuscated") : tr("Normal"));

        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Low ID:"))
                    .arg(formatNumber(srv.value(QStringLiteral("lowIDUsers")).toInteger()));
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Ping:"))
                    .arg(tr("%1 ms").arg(srv.value(QStringLiteral("ping")).toInteger()));
        html += QStringLiteral("</table><br>");

        // Server features
        const auto softFiles = srv.value(QStringLiteral("softFiles")).toInteger();
        const auto hardFiles = srv.value(QStringLiteral("hardFiles")).toInteger();
        html += QStringLiteral("<b>%1</b><br>").arg(tr("eD2K Server Features"));
        html += QStringLiteral("<table cellpadding='1'>");
        html += QStringLiteral("<tr><td>%1</td><td>%2/%3</td></tr>").arg(tr("Soft/Hard File Limits:"))
                    .arg(formatNumber(softFiles)).arg(formatNumber(hardFiles));
        if (extended) {
            // MFC NetworkInfoDlg.cpp:182-213, advanced controls only
            const QCborMap features = srv.value(QStringLiteral("features")).toMap();
            const std::pair<const char*, QString> lines[] = {
                {"tcpCompression", tr("TCP compression")},
                {"shortTags", tr("Short tags")},
                {"unicode", QStringLiteral("Unicode")},
                {"intTypeTags", tr("Integer type tags")},
                {"udpSources", tr("Extended UDP protocol for source requests")},
                {"udpSources2", tr("Extended UDP protocol for source requests") + QStringLiteral(" #2")},
                {"udpFiles", tr("Extended UDP protocol for file requests")},
                {"largeFiles", tr("Support for large files")},
                {"obfuscationUdp", tr("Protocol Obfuscation") + QStringLiteral(" (UDP)")},
                {"obfuscationTcp", tr("Protocol Obfuscation") + QStringLiteral(" (TCP)")},
                // Not MFC: IPv6 and NAT callback extensions
                {"ipv6Tcp", QStringLiteral("IPv6 (TCP)")},
                {"ipv6Udp", QStringLiteral("IPv6 (UDP)")},
                {"lowIdHolePunch", tr("LowID hole punching")},
                {"lugdunumNat", tr("NAT callback (Lugdunum)")},
                {"holePunchExtension", tr("Hole punching extension")},
                {"natRendezvous", tr("NAT rendezvous")},
                {"natWake", tr("NAT wake-up")},
                {"foreignCallback", tr("Callback for clients on other servers")}};
            for (const auto& [key, label] : lines) {
                html += QStringLiteral("<tr><td>%1:</td><td>%2</td></tr>")
                            .arg(label, features.value(QLatin1StringView(key)).toBool() ? tr("Yes") : tr("No"));
            }
        }
        html += QStringLiteral("</table><br>");
    }

    // -----------------------------------------------------------------------
    // Kad Network
    // -----------------------------------------------------------------------
    const QCborMap kad = info.value(QStringLiteral("kad")).toMap();
    const bool kadRunning = kad.value(QStringLiteral("running")).toBool();
    const bool kadConnected = kad.value(QStringLiteral("connected")).toBool();
    const bool kadFirewalled = kad.value(QStringLiteral("firewalled")).toBool();

    html += QStringLiteral("<b>%1</b><br>").arg(tr("Kad Network"));
    html += QStringLiteral("<table cellpadding='1'>");

    // Status
    QString kadStatus;
    if (kadConnected) {
        kadStatus = kadFirewalled ? tr("Firewalled") : tr("Open");
        if (kad.value(QStringLiteral("lanMode")).toBool())   // MFC NetworkInfoDlg.cpp:228-229
            kadStatus += QStringLiteral(" (%1)").arg(tr("LAN Mode"));
    } else if (kadRunning)
        kadStatus = tr("Connecting");
    else
        kadStatus = tr("Disconnected");
    html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Status:")).arg(kadStatus);

    if (kadConnected) {
        // UDP status
        const bool udpFW = kad.value(QStringLiteral("udpFirewalled")).toBool();
        const bool udpVerified = kad.value(QStringLiteral("udpVerified")).toBool();
        QString udpStatus;
        if (udpFW)
            udpStatus = tr("Firewalled");
        else {
            udpStatus = tr("Open");
            if (!udpVerified)
                udpStatus += QStringLiteral(" (%1)").arg(tr("unverified"));
        }
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("UDP Status:")).arg(udpStatus);

        // IP:Port
        const auto kadIP = kad.value(QStringLiteral("ip")).toInteger();
        const auto internPort = kad.value(QStringLiteral("internPort")).toInteger();
        html += QStringLiteral("<tr><td>%1</td><td>%2:%3</td></tr>").arg(tr("IP:Port:"))
                    .arg(ipFromKad(kadIP)).arg(internPort);

        // ID
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("ID:"))
                    .arg(kad.value(QStringLiteral("id")).toInteger());

        // External UDP port (if different)
        const auto externPort = kad.value(QStringLiteral("externPort")).toInteger();
        if (externPort > 0 && externPort != internPort)
            html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Extern UDP Port:"))
                        .arg(externPort);

        // Buddy — MFC shows it only while UDP-firewalled (NetworkInfoDlg.cpp:256)
        if (udpFW) {
            QString buddy;
            switch (kad.value(QStringLiteral("buddyStatus")).toInteger()) {
            case 1: buddy = tr("Connecting"); break;
            case 2: {
                buddy = tr("Connected");
                const QString addr = kad.value(QStringLiteral("buddyAddress")).toString();
                const QString name = kad.value(QStringLiteral("buddyName")).toString();
                if (!addr.isEmpty())
                    buddy += QStringLiteral(" (%1:%2%3)")
                                 .arg(addr.toHtmlEscaped())
                                 .arg(kad.value(QStringLiteral("buddyPort")).toInteger())
                                 .arg(name.isEmpty() ? QString() : QStringLiteral(", ") + name.toHtmlEscaped());
                break;
            }
            default: buddy = tr("None"); break;
            }
            html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Buddy:")).arg(buddy);
        } else if (kad.contains(QStringLiteral("buddiesServed"))) {
            // Open: the firewalled nodes we relay for (not MFC)
            html += QStringLiteral("<tr><td>%1</td><td>%2 / %3</td></tr>").arg(tr("Buddies served:"))
                        .arg(kad.value(QStringLiteral("buddiesServed")).toInteger())
                        .arg(kad.value(QStringLiteral("buddiesServedMax")).toInteger());
            for (const QCborValue& v : kad.value(QStringLiteral("servedBuddies")).toArray()) {
                const QCborMap b = v.toMap();
                QString addr = b.value(QStringLiteral("address")).toString();
                if (addr.contains(u':'))
                    addr = QStringLiteral("[%1]").arg(addr);   // IPv6 literal
                QString line = QStringLiteral("%1:%2").arg(addr.toHtmlEscaped())
                                   .arg(b.value(QStringLiteral("port")).toInteger());
                const QString name = b.value(QStringLiteral("name")).toString();
                const QString software = b.value(QStringLiteral("software")).toString();
                if (!name.isEmpty())
                    line += QStringLiteral(", ") + name.toHtmlEscaped();
                if (!software.isEmpty())
                    line += QStringLiteral(" (%1)").arg(software.toHtmlEscaped());
                line += QStringLiteral(", ")
                        + formatSecondsHM(b.value(QStringLiteral("connectedSecs")).toInteger());
                html += QStringLiteral("<tr><td></td><td>%1</td></tr>").arg(line);
            }
        }

        // Kad hash
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Hash:"))
                    .arg(kad.value(QStringLiteral("hash")).toString());

        // Users / Files
        const auto kadUsers = kad.value(QStringLiteral("users")).toInteger();
        const auto kadUsersExp = kad.value(QStringLiteral("usersExperimental")).toInteger();
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Users:"))
                    .arg(tr("%1 (Experimental: %2)").arg(formatNumber(kadUsers), formatNumber(kadUsersExp)));
        html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Files:"))
                    .arg(formatNumber(kad.value(QStringLiteral("files")).toInteger()));

        // Indexed
        if (kad.contains(QStringLiteral("indexed"))) {
            const QCborMap idx = kad.value(QStringLiteral("indexed")).toMap();
            html += QStringLiteral("<tr><td>%1</td><td></td></tr>").arg(tr("Indexed:"));
            html += QStringLiteral("<tr><td></td><td>%1 %2</td></tr>").arg(tr("Source:"))
                        .arg(idx.value(QStringLiteral("source")).toInteger());
            html += QStringLiteral("<tr><td></td><td>%1 %2</td></tr>").arg(tr("Keyword:"))
                        .arg(idx.value(QStringLiteral("keyword")).toInteger());
            html += QStringLiteral("<tr><td></td><td>%1 %2</td></tr>").arg(tr("Notes:"))
                        .arg(idx.value(QStringLiteral("notes")).toInteger());
            html += QStringLiteral("<tr><td></td><td>%1 %2</td></tr>").arg(tr("Load:"))
                        .arg(idx.value(QStringLiteral("load")).toInteger());
        }
    }
    html += QStringLiteral("</table><br>");

    html += ed3kHtml(info.value(QStringLiteral("ed3k")).toMap());
    html += bitTorrentHtml(info.value(QStringLiteral("bt")).toMap());

    // -----------------------------------------------------------------------
    // Web Interface
    // -----------------------------------------------------------------------
    html += QStringLiteral("<b>%1</b><br>").arg(tr("Web Interface"));
    html += QStringLiteral("<table cellpadding='1'>");
    const QCborMap web = info.value(QStringLiteral("web")).toMap();
    const bool webEnabled = web.value(QStringLiteral("enabled")).toBool();
    const bool webRunning = web.value(QStringLiteral("running")).toBool();
    QString webStatus = webEnabled ? tr("Enabled") : tr("Disabled");
    if (webEnabled && !webRunning)
        webStatus += QStringLiteral(" (%1)").arg(tr("not running"));
    html += QStringLiteral("<tr><td>%1</td><td>%2</td></tr>").arg(tr("Status:")).arg(webStatus);
    if (webRunning) {
        html += QStringLiteral("<tr><td></td><td>%1</td></tr>")
                    .arg(tr("%n active session(s)", nullptr,
                            static_cast<int>(web.value(QStringLiteral("sessions")).toInteger())));
        QString host = web.value(QStringLiteral("host")).toString();
        if (host.contains(u':'))
            host = QStringLiteral("[%1]").arg(host);   // IPv6 literal
        const QString url = QStringLiteral("%1://%2:%3/")
                                .arg(web.value(QStringLiteral("https")).toBool() ? QStringLiteral("https")
                                                                                 : QStringLiteral("http"))
                                .arg(host)
                                .arg(web.value(QStringLiteral("port")).toInteger());
        html += QStringLiteral("<tr><td>%1</td><td><a href='%2'>%2</a></td></tr>").arg(tr("URL:"))
                    .arg(url.toHtmlEscaped());
    }
    html += QStringLiteral("</table>");

    html += QStringLiteral("</body></html>");
    return html;
}

// ---------------------------------------------------------------------------
// ed3k and BitTorrent sections (no MFC original)
// ---------------------------------------------------------------------------

QString NetworkInfoDialog::ed3kHtml(const QCborMap& ed3k)
{
    // a core without ed3k, or from before the section
    if (ed3k.isEmpty() || !ed3k.value(QStringLiteral("available")).toBool())
        return {};
    QString html = QStringLiteral("<a name='%1'></a><b>%2</b><br><table cellpadding='1'>")
                       .arg(QLatin1StringView(kEd3kAnchor), tr("ed3k Network"));
    const bool running = ed3k.value(QStringLiteral("running")).toBool();
    if (!running) {
        html += row(tr("Status:"), ed3k.value(QStringLiteral("enabled")).toBool() ? tr("Not running")
                                                                                 : tr("Disabled"));
        return html + QStringLiteral("</table><br>");
    }
    const qint64 contacts = ed3k.value(QStringLiteral("contacts")).toInteger();
    html += row(tr("Status:"), contacts > 0 ? tr("Connected") : tr("Connecting"));
    html += row(tr("UDP Port:"), QString::number(ed3k.value(QStringLiteral("udpPort")).toInteger()));

    // 0 unknown, 1 open, 2 cone, 3 symmetric; bit 0 IPv4 / bit 1 IPv6 of natChecking
    const qint64 checking = ed3k.value(QStringLiteral("natChecking")).toInteger();
    const qint64 offline = ed3k.value(QStringLiteral("kadOffline")).toInteger();
    const auto reach = [&](qint64 nat, int bit) -> QString {
        if (offline & bit)
            return tr("Offline");
        if (checking & bit)
            return tr("Checking");
        switch (nat) {
        case 1: return tr("Open");
        case 2: return tr("Cone NAT");
        case 3: return tr("Symmetric NAT");
        default: return tr("Unknown");
        }
    };
    const auto family = [&](const QString& label, const QString& key, const QString& endpoint, int bit) {
        const QString ep = ed3k.value(endpoint).toString();
        const QString state = reach(ed3k.value(key).toInteger(), bit);
        html += row(label, ep.isEmpty() ? state : QStringLiteral("%1 (%2)").arg(ep.toHtmlEscaped(), state));
    };
    family(tr("IPv4:"), QStringLiteral("class4"), QStringLiteral("endpoint4"), 1);
    family(tr("IPv6:"), QStringLiteral("class6"), QStringLiteral("endpoint6"), 2);
    html += row(tr("Router:"), ed3k.value(QStringLiteral("router")).toBool() ? tr("Yes") : tr("No"));
    html += row(tr("Contacts:"), formatNumber(contacts));
    html += row(tr("Node ID:"), ed3k.value(QStringLiteral("nodeId")).toString().toHtmlEscaped());
    return html + QStringLiteral("</table><br>");
}

QString NetworkInfoDialog::bitTorrentHtml(const QCborMap& bt)
{
    // a core built without BitTorrent, or from before the section
    if (bt.isEmpty() || !bt.value(QStringLiteral("available")).toBool())
        return {};
    QString html = QStringLiteral("<b>%1</b><br><table cellpadding='1'>").arg(tr("BitTorrent"));
    if (!bt.value(QStringLiteral("running")).toBool()) {
        html += row(tr("Status:"), bt.value(QStringLiteral("enabled")).toBool() ? tr("Not running")
                                                                               : tr("Disabled"));
        return html + QStringLiteral("</table><br>");
    }
    const quint16 port = static_cast<quint16>(bt.value(QStringLiteral("listenPort")).toInteger());
    html += row(tr("Status:"), bt.value(QStringLiteral("listening")).toBool()
                                   ? tr("Listening on port %1").arg(port)
                                   : tr("Not listening"));
    // libtorrent's vote from peers, trackers, DHT and the router (external_ip_alert)
    const QString v4 = bt.value(QStringLiteral("externalIPv4")).toString();
    const QString v6 = bt.value(QStringLiteral("externalIPv6")).toString();
    html += row(tr("External IPv4:"), v4.isEmpty() ? tr("Unknown") : v4.toHtmlEscaped());
    if (!v6.isEmpty())
        html += row(tr("External IPv6:"), v6.toHtmlEscaped());
    html += row(tr("DHT Nodes:"), formatNumber(bt.value(QStringLiteral("dhtNodes")).toInteger()));
    html += row(tr("Torrents:"), formatNumber(bt.value(QStringLiteral("torrents")).toInteger()));
    html += QStringLiteral("</table><br>");
    return html + i2pHtml(bt);
}

QString NetworkInfoDialog::i2pHtml(const QCborMap& bt)
{
    // Preferences::kBtI2pOff / kBtI2pBundled / kBtI2pExternal
    const qint64 mode = bt.value(QStringLiteral("i2pMode")).toInteger();
    if (mode == 0 || !bt.value(QStringLiteral("i2pEnabled")).toBool())
        return {};
    QString html = QStringLiteral("<b>%1</b><br><table cellpadding='1'>").arg(tr("BitTorrent over I2P"));
    html += row(tr("Router:"), mode == 1 ? tr("Bundled (i2pd)") : tr("External"));
    if (mode == 1) {
        // I2pRouter::State: Stopped, Starting, Ready, Failed, Downloading
        QString state;
        switch (bt.value(QStringLiteral("i2pRouter")).toInteger()) {
        case 1: state = tr("Starting"); break;
        case 2: state = tr("Ready"); break;
        case 3: state = tr("Failed"); break;
        case 4: state = tr("Downloading (%1%)").arg(bt.value(QStringLiteral("i2pRouterProgress")).toInteger()); break;
        default: state = tr("Stopped"); break;
        }
        if (const QString err = bt.value(QStringLiteral("i2pRouterError")).toString(); !err.isEmpty())
            state += QStringLiteral(": ") + err.toHtmlEscaped();
        html += row(tr("Router Status:"), state);
        html += row(tr("Router Port:"), QString::number(bt.value(QStringLiteral("i2pRouterPort")).toInteger()));
        html += row(tr("Bandwidth Shared:"), QStringLiteral("%1%").arg(bt.value(QStringLiteral("i2pShare")).toInteger()));
    }
    const QString samHost = bt.value(QStringLiteral("i2pSamHost")).toString();
    const qint64 samPort = bt.value(QStringLiteral("i2pSamPort")).toInteger();
    const QString samError = bt.value(QStringLiteral("i2pError")).toString();
    QString sam;
    if (!bt.value(QStringLiteral("i2pSam")).toBool())
        sam = tr("Not set");
    else
        sam = QStringLiteral("%1:%2").arg(samHost.toHtmlEscaped()).arg(samPort);
    if (!samError.isEmpty())
        sam += QStringLiteral(" (%1)").arg(samError.toHtmlEscaped());
    html += row(tr("SAM Bridge:"), sam);
    html += row(tr("Tunnels:"), tr("%1 hops, %2 each way")
                                    .arg(bt.value(QStringLiteral("i2pTunnelLength")).toInteger())
                                    .arg(bt.value(QStringLiteral("i2pTunnelQuantity")).toInteger()));
    html += row(tr("Mixed Mode:"), bt.value(QStringLiteral("i2pMixed")).toBool() ? tr("Yes") : tr("No"));
    html += row(tr("I2P Torrents:"), tr("%1 (%2 peers)")
                                         .arg(formatNumber(bt.value(QStringLiteral("i2pTorrents")).toInteger()),
                                              formatNumber(bt.value(QStringLiteral("i2pPeers")).toInteger())));
    return html + QStringLiteral("</table><br>");
}

} // namespace eMule
