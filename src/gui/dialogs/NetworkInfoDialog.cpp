#include "pch.h"
/// @file NetworkInfoDialog.cpp
/// @brief Network Information dialog — implementation.

#include "dialogs/NetworkInfoDialog.h"

#include "app/IpcClient.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCborMap>
#include <QDialogButtonBox>
#include <QHostAddress>
#include <QLocale>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace eMule {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

/// Convert a network-byte-order uint32 IP to "x.x.x.y" string.
QString ipFromUint32(qint64 ip)
{
    return QHostAddress(static_cast<quint32>(ip)).toString();
}

/// Convert a network-byte-order uint32 IP to "x.x.x.y" string with htonl.
QString ipFromUint32Htonl(qint64 ip)
{
    const auto native = static_cast<quint32>(ip);
    // Kad stores IP in host byte order, needs htonl for display
    const quint32 net = ((native & 0xFF) << 24) | ((native & 0xFF00) << 8)
                      | ((native >> 8) & 0xFF00) | ((native >> 24) & 0xFF);
    return QHostAddress(net).toString();
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
    resize(520, 650);

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
    m_ipc->sendRequest(std::move(req), [this](const Ipc::IpcMessage& resp) {
        const QCborMap info = resp.fieldMap(1);
        populateInfo(info);
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
    QString html;
    html.reserve(4096);

    html += QStringLiteral("<html><body style='font-family: sans-serif; font-size: 10pt;'>");

    // -----------------------------------------------------------------------
    // Client
    // -----------------------------------------------------------------------
    const QCborMap client = info.value(QStringLiteral("client")).toMap();
    html += QStringLiteral("<b>Client</b><br>");
    html += QStringLiteral("<table cellpadding='1'>");
    html += QStringLiteral("<tr><td>Nick:</td><td>%1</td></tr>")
                .arg(client.value(QStringLiteral("nick")).toString().toHtmlEscaped());
    html += QStringLiteral("<tr><td>Hash:</td><td>%1</td></tr>")
                .arg(client.value(QStringLiteral("hash")).toString());
    html += QStringLiteral("<tr><td>TCP Port:</td><td>%1</td></tr>")
                .arg(client.value(QStringLiteral("tcpPort")).toInteger());
    html += QStringLiteral("<tr><td>UDP Port:</td><td>%1</td></tr>")
                .arg(client.value(QStringLiteral("udpPort")).toInteger());
    html += QStringLiteral("</table><br>");

    // -----------------------------------------------------------------------
    // eD2K Network
    // -----------------------------------------------------------------------
    const QCborMap ed2k = info.value(QStringLiteral("ed2k")).toMap();
    const bool ed2kConnected = ed2k.value(QStringLiteral("connected")).toBool();
    const bool ed2kConnecting = ed2k.value(QStringLiteral("connecting")).toBool();

    html += QStringLiteral("<b>eD2K Network</b><br>");
    html += QStringLiteral("<table cellpadding='1'>");

    // Status
    QString ed2kStatus;
    if (ed2kConnected)
        ed2kStatus = tr("Connected");
    else if (ed2kConnecting)
        ed2kStatus = tr("Connecting");
    else
        ed2kStatus = tr("Disconnected");
    html += QStringLiteral("<tr><td>Status:</td><td>%1</td></tr>").arg(ed2kStatus);

    if (ed2kConnected) {
        // Users / Files
        const auto totalUsers = ed2k.value(QStringLiteral("totalUsers")).toInteger();
        const auto totalFiles = ed2k.value(QStringLiteral("totalFiles")).toInteger();
        html += QStringLiteral("<tr><td>Users:</td><td>%1</td></tr>").arg(formatNumber(totalUsers));
        html += QStringLiteral("<tr><td>Files:</td><td>%1</td></tr>").arg(formatNumber(totalFiles));

        // IP:Port
        const auto publicIP = ed2k.value(QStringLiteral("publicIP")).toInteger();
        const auto tcpPort = client.value(QStringLiteral("tcpPort")).toInteger();
        const bool lowID = ed2k.value(QStringLiteral("lowID")).toBool();
        if (lowID && publicIP == 0)
            html += QStringLiteral("<tr><td>IP:Port:</td><td>%1</td></tr>").arg(tr("Unknown"));
        else
            html += QStringLiteral("<tr><td>IP:Port:</td><td>%1:%2</td></tr>")
                        .arg(ipFromUint32(publicIP)).arg(tcpPort);

        // Client ID
        const auto clientID = ed2k.value(QStringLiteral("clientID")).toInteger();
        html += QStringLiteral("<tr><td>ID:</td><td>%1</td></tr>").arg(clientID);
        html += QStringLiteral("<tr><td></td><td>%1</td></tr>")
                    .arg(lowID ? tr("Low ID") : tr("High ID"));
    }
    html += QStringLiteral("</table><br>");

    // -----------------------------------------------------------------------
    // eD2K Server (if connected)
    // -----------------------------------------------------------------------
    if (ed2kConnected && ed2k.contains(QStringLiteral("server"))) {
        const QCborMap srv = ed2k.value(QStringLiteral("server")).toMap();
        html += QStringLiteral("<b>eD2K Server</b><br>");
        html += QStringLiteral("<table cellpadding='1'>");
        html += QStringLiteral("<tr><td>Name:</td><td>%1</td></tr>")
                    .arg(srv.value(QStringLiteral("name")).toString().toHtmlEscaped());
        html += QStringLiteral("<tr><td>Description:</td><td>%1</td></tr>")
                    .arg(srv.value(QStringLiteral("description")).toString().toHtmlEscaped());
        html += QStringLiteral("<tr><td>IP:Port:</td><td>%1:%2</td></tr>")
                    .arg(srv.value(QStringLiteral("address")).toString())
                    .arg(srv.value(QStringLiteral("port")).toInteger());
        html += QStringLiteral("<tr><td>Version:</td><td>%1</td></tr>")
                    .arg(srv.value(QStringLiteral("version")).toString());
        html += QStringLiteral("<tr><td>Users:</td><td>%1</td></tr>")
                    .arg(formatNumber(srv.value(QStringLiteral("users")).toInteger()));
        html += QStringLiteral("<tr><td>Files:</td><td>%1</td></tr>")
                    .arg(formatNumber(srv.value(QStringLiteral("files")).toInteger()));

        const bool obfuscated = srv.value(QStringLiteral("obfuscated")).toBool();
        html += QStringLiteral("<tr><td>Connections:</td><td>%1</td></tr>")
                    .arg(obfuscated ? tr("Obfuscated") : tr("Normal"));

        html += QStringLiteral("<tr><td>Low ID:</td><td>%1</td></tr>")
                    .arg(formatNumber(srv.value(QStringLiteral("lowIDUsers")).toInteger()));
        html += QStringLiteral("<tr><td>Ping:</td><td>%1 ms</td></tr>")
                    .arg(srv.value(QStringLiteral("ping")).toInteger());
        html += QStringLiteral("</table><br>");

        // Server features
        const auto softFiles = srv.value(QStringLiteral("softFiles")).toInteger();
        const auto hardFiles = srv.value(QStringLiteral("hardFiles")).toInteger();
        html += QStringLiteral("<b>eD2K Server Features</b><br>");
        html += QStringLiteral("<table cellpadding='1'>");
        html += QStringLiteral("<tr><td>Soft/Hard File Limits:</td><td>%1/%2</td></tr>")
                    .arg(formatNumber(softFiles)).arg(formatNumber(hardFiles));
        html += QStringLiteral("</table><br>");
    }

    // -----------------------------------------------------------------------
    // Kad Network
    // -----------------------------------------------------------------------
    const QCborMap kad = info.value(QStringLiteral("kad")).toMap();
    const bool kadRunning = kad.value(QStringLiteral("running")).toBool();
    const bool kadConnected = kad.value(QStringLiteral("connected")).toBool();
    const bool kadFirewalled = kad.value(QStringLiteral("firewalled")).toBool();

    html += QStringLiteral("<b>Kad Network</b><br>");
    html += QStringLiteral("<table cellpadding='1'>");

    // Status
    QString kadStatus;
    if (kadConnected)
        kadStatus = kadFirewalled ? tr("Firewalled") : tr("Open");
    else if (kadRunning)
        kadStatus = tr("Connecting");
    else
        kadStatus = tr("Disconnected");
    html += QStringLiteral("<tr><td>Status:</td><td>%1</td></tr>").arg(kadStatus);

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
        html += QStringLiteral("<tr><td>UDP Status:</td><td>%1</td></tr>").arg(udpStatus);

        // IP:Port
        const auto kadIP = kad.value(QStringLiteral("ip")).toInteger();
        const auto internPort = kad.value(QStringLiteral("internPort")).toInteger();
        html += QStringLiteral("<tr><td>IP:Port:</td><td>%1:%2</td></tr>")
                    .arg(ipFromUint32Htonl(kadIP)).arg(internPort);

        // ID
        html += QStringLiteral("<tr><td>ID:</td><td>%1</td></tr>")
                    .arg(kad.value(QStringLiteral("id")).toInteger());

        // External UDP port (if different)
        const auto externPort = kad.value(QStringLiteral("externPort")).toInteger();
        if (externPort > 0 && externPort != internPort)
            html += QStringLiteral("<tr><td>Extern UDP Port:</td><td>%1</td></tr>")
                        .arg(externPort);

        // Kad hash
        html += QStringLiteral("<tr><td>Hash:</td><td>%1</td></tr>")
                    .arg(kad.value(QStringLiteral("hash")).toString());

        // Users / Files
        const auto kadUsers = kad.value(QStringLiteral("users")).toInteger();
        const auto kadUsersExp = kad.value(QStringLiteral("usersExperimental")).toInteger();
        html += QStringLiteral("<tr><td>Users:</td><td>%1 (Experimental: %2)</td></tr>")
                    .arg(formatNumber(kadUsers)).arg(formatNumber(kadUsersExp));
        html += QStringLiteral("<tr><td>Files:</td><td>%1</td></tr>")
                    .arg(formatNumber(kad.value(QStringLiteral("files")).toInteger()));

        // Indexed
        if (kad.contains(QStringLiteral("indexed"))) {
            const QCborMap idx = kad.value(QStringLiteral("indexed")).toMap();
            html += QStringLiteral("<tr><td>Indexed:</td><td></td></tr>");
            html += QStringLiteral("<tr><td></td><td>Source: %1</td></tr>")
                        .arg(idx.value(QStringLiteral("source")).toInteger());
            html += QStringLiteral("<tr><td></td><td>Keyword: %1</td></tr>")
                        .arg(idx.value(QStringLiteral("keyword")).toInteger());
            html += QStringLiteral("<tr><td></td><td>Notes: %1</td></tr>")
                        .arg(idx.value(QStringLiteral("notes")).toInteger());
            html += QStringLiteral("<tr><td></td><td>Load: %1</td></tr>")
                        .arg(idx.value(QStringLiteral("load")).toInteger());
        }
    }
    html += QStringLiteral("</table><br>");

    // -----------------------------------------------------------------------
    // Web Interface
    // -----------------------------------------------------------------------
    html += QStringLiteral("<b>Web Interface</b><br>");
    html += QStringLiteral("<table cellpadding='1'>");
    html += QStringLiteral("<tr><td>Status:</td><td>%1</td></tr>").arg(tr("Disabled"));
    html += QStringLiteral("</table>");

    html += QStringLiteral("</body></html>");

    m_browser->setHtml(html);
}

} // namespace eMule
