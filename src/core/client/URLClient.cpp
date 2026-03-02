/// @file URLClient.cpp
/// @brief URLClient implementation — HTTP download client.
///
/// Port of MFC CUrlClient (srchybrid/URLClient.cpp).
/// Handles HTTP GET-based file downloads from web servers.

#include "client/URLClient.h"
#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "files/PartFile.h"
#include "net/ClientReqSocket.h"
#include "net/EMSocket.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"
#include "utils/TimeUtils.h"

#include "utils/Log.h"

#include <QHostInfo>
#include <QUrl>

#include <cstring>

namespace eMule {

// ===========================================================================
// Construction / Destruction
// ===========================================================================

URLClient::URLClient(QObject* parent)
    : UpDownClient(parent)
{
    // URL clients are not ed2k clients
    // Set client software to URL type
}

URLClient::~URLClient() = default;

// ===========================================================================
// setUrl — parse URL into components
// ===========================================================================

bool URLClient::setUrl(const QString& url, uint32 fromIP)
{
    if (url.isEmpty())
        return false;

    QUrl parsed(url);
    if (!parsed.isValid() || parsed.host().isEmpty())
        return false;

    m_urlHost = parsed.host();
    m_urlPort = static_cast<uint16>(parsed.port(80));
    m_urlPathLocal = parsed.path().toUtf8();

    if (m_urlPathLocal.isEmpty())
        m_urlPathLocal = "/";

    // Include query string if present
    if (parsed.hasQuery()) {
        m_urlPathLocal += '?';
        m_urlPathLocal += parsed.query().toUtf8();
    }

    // Set user identity from URL
    setUserName(m_urlHost);

    // If we have an IP from the caller, use it
    if (fromIP != 0)
        setIP(fromIP);

    // Set port for connection
    setUserPort(m_urlPort);

    return true;
}

// ===========================================================================
// setRequestFile
// ===========================================================================

void URLClient::setRequestFile(PartFile* reqFile)
{
    setReqFile(reqFile);
}

// ===========================================================================
// tryToConnect
// ===========================================================================

bool URLClient::tryToConnect(bool ignoreMaxCon)
{
    Q_UNUSED(ignoreMaxCon);

    if (m_urlHost.isEmpty()) {
        logDebug(QStringLiteral("URLClient::tryToConnect: no host set"));
        return false;
    }

    // Socket limit check
    if (theApp.listenSocket && theApp.listenSocket->tooManySockets())
        return false;

    // If we already have a connected socket, just proceed
    if (socket() && socket()->isConnected()) {
        connectionEstablished();
        return true;
    }

    setConnectingState(ConnectingState::DirectTCP);

    // If we have an IP already, create socket and connect directly
    if (connectIP() != 0) {
        connectToHost();
        return true;
    }

    // Resolve hostname to IP asynchronously
    logDebug(QStringLiteral("URLClient::tryToConnect: resolving %1").arg(m_urlHost));
    QHostInfo::lookupHost(m_urlHost, this, [this](const QHostInfo& info) {
        if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            logDebug(QStringLiteral("URLClient: DNS resolution failed for %1: %2").arg(m_urlHost, info.errorString()));
            disconnected(QStringLiteral("DNS resolution failed"));
            return;
        }
        // Use the first IPv4 address
        for (const auto& addr : info.addresses()) {
            if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
                setIP(htonl(addr.toIPv4Address()));
                connectToHost();
                return;
            }
        }
        // Fallback to first address if no IPv4
        setIP(htonl(info.addresses().first().toIPv4Address()));
        connectToHost();
    });

    return true;
}

// ===========================================================================
// connectionEstablished
// ===========================================================================

void URLClient::connectionEstablished()
{
    setConnectingState(ConnectingState::None);
    sendFileRequest();
}

// ===========================================================================
// disconnected
// ===========================================================================

bool URLClient::disconnected(const QString& reason, bool fromSocket)
{
    logDebug(QStringLiteral("URLClient disconnected: %1 reason: %2").arg(m_urlHost, reason));

    // Clean up HTTP state
    setConnectingState(ConnectingState::None);

    // Call base class disconnect
    return UpDownClient::disconnected(reason, fromSocket);
}

// ===========================================================================
// sendFileRequest — delegates to HTTP block requests
// ===========================================================================

void URLClient::sendFileRequest()
{
    sendHttpBlockRequests();
}

// ===========================================================================
// sendBlockRequests — delegates to HTTP block requests
// ===========================================================================

void URLClient::sendBlockRequests()
{
    sendHttpBlockRequests();
}

// ===========================================================================
// sendHttpBlockRequests — build HTTP GET with Range header
// ===========================================================================

bool URLClient::sendHttpBlockRequests()
{
    if (!socket())
        return false;

    const auto& pending = pendingBlocks();
    if (pending.empty()) {
        // Create block requests
        createBlockRequests(1);
        if (pendingBlocks().empty())
            return false;
    }

    // Get first pending block's range
    uint64 startPos = 0;
    uint64 endPos = 0;

    for (const auto* pb : pendingBlocks()) {
        if (pb->block) {
            startPos = pb->block->startOffset;
            endPos = pb->block->endOffset;
            break;
        }
    }

    // Build HTTP GET request
    QByteArray request;
    request += "GET ";
    request += m_urlPathLocal;
    request += " HTTP/1.1\r\n";
    request += "Host: ";
    request += m_urlHost.toUtf8();
    if (m_urlPort != 80) {
        request += ':';
        request += QByteArray::number(m_urlPort);
    }
    request += "\r\n";
    request += "Accept: */*\r\n";
    request += "Connection: keep-alive\r\n";

    // Add Range header
    if (endPos > 0) {
        request += "Range: bytes=";
        request += QByteArray::number(static_cast<qint64>(startPos));
        request += '-';
        request += QByteArray::number(static_cast<qint64>(endPos - 1));
        request += "\r\n";
    }

    request += "\r\n";

    // Send as raw packet
    auto packet = std::make_unique<RawPacket>(request.constData(),
                                               static_cast<uint32>(request.size()));
    sendPacket(std::move(packet));

    return true;
}

// ===========================================================================
// processHttpDownResponse — parse HTTP status and headers
// ===========================================================================

bool URLClient::processHttpDownResponse(const QList<QByteArray>& headers)
{
    if (headers.isEmpty())
        return false;

    // Parse status line: "HTTP/1.1 200 OK" or "HTTP/1.1 206 Partial Content"
    const QByteArray& statusLine = headers.first();
    if (!statusLine.startsWith("HTTP/"))
        return false;

    const auto spaceIdx = statusLine.indexOf(' ');
    if (spaceIdx < 0)
        return false;

    const QByteArray statusCode = statusLine.mid(spaceIdx + 1, 3);
    const int code = statusCode.toInt();

    if (code != 200 && code != 206) {
        logDebug(QStringLiteral("URLClient: HTTP error %1 from %2").arg(code).arg(m_urlHost));
        return false;
    }

    // Parse Content-Range header if present
    for (qsizetype i = 1; i < headers.size(); ++i) {
        if (headers[i].toLower().startsWith("content-range:")) {
            // Format: "Content-Range: bytes START-END/TOTAL"
            const QByteArray value = headers[i].mid(headers[i].indexOf(':') + 1).trimmed();
            if (value.startsWith("bytes ")) {
                const QByteArray range = value.mid(6); // skip "bytes "
                const auto dashIdx = range.indexOf('-');
                const auto slashIdx = range.indexOf('/');
                if (dashIdx > 0 && slashIdx > dashIdx) {
                    m_rangeStart = range.left(dashIdx).trimmed().toULongLong();
                    m_rangeEnd = range.mid(dashIdx + 1, slashIdx - dashIdx - 1).trimmed().toULongLong() + 1;
                }
            }
        }
    }

    return true;
}

// ===========================================================================
// processHttpDownResponseBody
// ===========================================================================

bool URLClient::processHttpDownResponseBody(const uint8* data, uint32 size)
{
    if (!data || size == 0)
        return false;

    processHttpBlockPacket(data, size);
    return true;
}

// ===========================================================================
// processHttpBlockPacket — process HTTP data as file block
// ===========================================================================

void URLClient::processHttpBlockPacket(const uint8* data, uint32 size)
{
    // Update transfer statistics
    const uint32 curTick = static_cast<uint32>(getTickCount());

    TransferredData td;
    td.dataLen = size;
    td.timestamp = curTick;

    // Write data to PartFile
    PartFile* file = reqFile();
    if (file && data && size > 0) {
        const uint64 startOffset = m_rangeStart;
        const uint64 endOffset = m_rangeStart + size;
        file->writeToBuffer(size, data, startOffset, endOffset, nullptr);
        m_rangeStart += size;
    }
}

// ===========================================================================
// sendCancelTransfer — close socket (HTTP has no cancel packet)
// ===========================================================================

void URLClient::sendCancelTransfer()
{
    // HTTP doesn't have a cancel packet — just close the connection
    if (socket()) {
        socket()->disconnectFromHost();
    }
}

// ===========================================================================
// checkDownloadTimeout
// ===========================================================================

void URLClient::checkDownloadTimeout()
{
    // Use base class timeout checking with HTTP-specific behavior
    UpDownClient::checkDownloadTimeout();
}

// ===========================================================================
// onSocketConnected
// ===========================================================================

void URLClient::onSocketConnected(int errorCode)
{
    if (errorCode == 0) {
        connectionEstablished();
    } else {
        logDebug(QStringLiteral("URLClient: connection failed to %1 error: %2").arg(m_urlHost).arg(errorCode));
        disconnected(QStringLiteral("Connection failed"));
    }
}

// ===========================================================================
// connectToHost — private: create socket and initiate TCP connection
// ===========================================================================

void URLClient::connectToHost()
{
    // Create a ClientReqSocket for this connection
    auto* reqSocket = new ClientReqSocket(this);
    reqSocket->createSocket();
    setSocket(reqSocket);

    // Register with ListenSocket for tracking
    if (theApp.listenSocket)
        theApp.listenSocket->addSocket(reqSocket);

    // Connect socket signals
    QObject::connect(reqSocket, &ClientReqSocket::clientDisconnected,
                     this, [this](const QString& reason) {
        disconnected(reason, true);
    });

    // Configure proxy
    reqSocket->initProxySupport(thePrefs.proxySettings());

    // Initiate TCP connection
    const uint32 ip = connectIP();
    const QHostAddress addr(ntohl(ip));
    reqSocket->connectToHost(addr, m_urlPort);
    reqSocket->waitForOnConnect();

    logDebug(QStringLiteral("URLClient::connectToHost: connecting to %1:%2").arg(addr.toString()).arg(m_urlPort));
}

} // namespace eMule
