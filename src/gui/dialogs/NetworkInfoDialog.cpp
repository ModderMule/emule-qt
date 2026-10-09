#include "pch.h"
/// @file NetworkInfoDialog.cpp
/// @brief Network Information dialog — implementation.

#include "dialogs/NetworkInfoDialog.h"

#include "app/IpcClient.h"

#include "IpcMessage.h"
#include "prefs/Preferences.h"
#include "utils/DialogSizing.h"

#include <QCborMap>
#include <QDialogButtonBox>
#include <QHostAddress>
#include <QLocale>
#include <QPointer>
#include <QTextBrowser>
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

} // anonymous namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

NetworkInfoDialog::NetworkInfoDialog(IpcClient* ipc, QWidget* parent)
    : QDialog(parent)
    , m_ipc(ipc)
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
}

QString NetworkInfoDialog::infoHtml(const QCborMap& info, bool extended)
{
    QString html;
    html.reserve(4096);

    html += QStringLiteral("<html><body style='font-family: sans-serif; font-size: 10pt;'>");

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

        // IPv6 + the server's dial-back verdict (ST_IPV6_STATUS). A v6 session is
        // structurally Low ID, so this row is what says whether inbound v6 works.
        const QString ipv6 = ed2k.value(QStringLiteral("publicIPv6")).toString();
        if (!ipv6.isEmpty()) {
            constexpr qint64 kReachable = 0x02;   // IPV6ST_REACHABLE
            constexpr qint64 kProbed    = 0x04;   // IPV6ST_PROBED
            const qint64 st = ed2k.value(QStringLiteral("ipv6Status")).toInteger();
            QString v6Status;
            if ((st & kProbed) && (st & kReachable))
                v6Status = tr("Open");
            else if (st & kProbed)
                v6Status = tr("Firewalled");
            else if (st & kReachable)
                v6Status = QStringLiteral("%1 (%2)").arg(tr("Open"), tr("unverified"));
            else
                v6Status = tr("Unknown");
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
                {"obfuscationTcp", tr("Protocol Obfuscation") + QStringLiteral(" (TCP)")}};
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

} // namespace eMule
