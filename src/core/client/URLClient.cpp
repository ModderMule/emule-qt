#include "pch.h"
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
#include "net/HostResolver.h"
#include "net/HttpClientReqSocket.h"
#include "net/TlsRelay.h"
#include "net/HttpDefaults.h"
#include "net/ListenSocket.h"
#include "net/Packet.h"
#include "net/PeerVetting.h"
#include "stats/Statistics.h"

#include "utils/Log.h"

#include <QNetworkProxy>
#include <QUrl>


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
    return setUrl(url, Address::fromNetworkOrder(fromIP));
}

bool URLClient::setUrl(const QString& url, const Address& fromAddr)
{
    if (url.isEmpty())
        return false;

    QUrl parsed(url);
    if (!parsed.isValid() || parsed.host().isEmpty())
        return false;
    // http as MFC (srchybrid/URLClient.cpp:96-104), https on top; no user:password@
    const QString scheme = parsed.scheme().toLower();
    const bool tls = scheme == QLatin1String("https");
    if ((!tls && scheme != QLatin1String("http")) || !parsed.userInfo().isEmpty())
        return false;

    m_url = parsed;
    m_urlTls = tls;
    m_urlHost = parsed.host();
    m_urlPort = static_cast<uint16>(parsed.port(tls ? 443 : 80));
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

    // If the caller already knows the host's address (either family), use it
    if (!fromAddr.isNull())
        setUserAddress(fromAddr);

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
    // Without a part status no block is ever reserved, and so no GET sent
    // (MFC CUrlClient::SetRequestFile).
    markAsCompleteHttpSource(reqFile);
}

// ===========================================================================
// tryToConnect
// ===========================================================================

bool URLClient::tryToConnect(bool ignoreMaxCon, bool noCallbacks)
{
    Q_UNUSED(ignoreMaxCon);
    // A URL source is always dialled directly over HTTP; there is no callback to suppress.
    Q_UNUSED(noCallbacks);

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
    if (!connectAddress().isNull()) {
        if (!acceptResolvedAddress(connectAddress()))
            return false;
        connectToHost();
        return true;
    }

    // Resolve hostname to IP asynchronously. IPv4-preferred with an IPv6 fallback: an
    // HTTP source is fine over either family, but IPv4 is the safer default for a random
    // web server. The shared resolver handles the timeout and cancels on destruction.
    logDebug(QStringLiteral("URLClient::tryToConnect: resolving %1").arg(m_urlHost));
    if (!m_hostResolver)
        m_hostResolver = new HostResolver(this);

    m_hostResolver->resolve(m_urlHost, HostResolver::Preference::PreferIPv4, this,
        [this](const HostResolver::Result& result) {
            if (!result.ok()) {
                logDebug(QStringLiteral("URLClient: DNS resolution failed for %1: %2")
                             .arg(m_urlHost, result.errorString));
                disconnected(QStringLiteral("DNS resolution failed"));
                return;
            }
            if (!acceptResolvedAddress(result.first()))
                return;
            setUserAddress(result.first());
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
    sendHttpBlockRequests();
}

// ===========================================================================
// disconnected
// ===========================================================================

bool URLClient::disconnected(const QString& reason, bool fromSocket)
{
    logDebug(QStringLiteral("URLClient disconnected: %1 reason: %2").arg(m_urlHost, reason));

    // Clean up HTTP state
    setConnectingState(ConnectingState::None);
    m_rangeStart = kNoRange;

    // Call base class disconnect
    return UpDownClient::disconnected(reason, fromSocket);
}

// ===========================================================================
// sendFileRequest — delegates to HTTP block requests
// ===========================================================================

void URLClient::sendFileRequest()
{
    // MFC ignores it: the GET goes out from connectionEstablished(), and a second one
    // on a busy connection would interleave two bodies.
}

// ===========================================================================
// sendBlockRequests — delegates to HTTP block requests
// ===========================================================================

void URLClient::sendBlockRequests()
{
    // only when nothing is outstanding — one request per connection at a time
    if (pendingBlocks().empty())
        sendHttpBlockRequests();
}

// ===========================================================================
// sendHttpBlockRequests — build HTTP GET with Range header
// ===========================================================================

bool URLClient::sendHttpBlockRequests()
{
    if (!socket() || !reqFile())
        return false;

    // MFC URLClient.cpp:138-162: a part's worth of blocks, merged into one range
    if (!reserveHttpRange(m_reqStart, m_reqEnd)) {
        setDownloadState(DownloadState::NoNeededParts);
        swapToAnotherFile(QStringLiteral("A4AF for NNP file. URLClient::sendHttpBlockRequests()"),
                          true, false, false, nullptr, true, true);
        return false;
    }
    m_rangeStart = m_reqStart;

    QByteArray request = buildGetHeader();
    // both ends inclusive, as the blocks are
    request += "Range: bytes=";
    request += QByteArray::number(static_cast<qulonglong>(m_reqStart));
    request += '-';
    request += QByteArray::number(static_cast<qulonglong>(m_reqEnd));
    request += "\r\n\r\n";

    // the answer starts with headers again (keep-alive)
    if (auto* http = dynamic_cast<HttpClientReqSocket*>(socket())) {
        http->clearHttpHeaders();
        http->setHttpState(HttpSocketState::RecvExpected);
    }
    return sendRawRequest(request);
}

// ===========================================================================
// processHttpDownResponse — parse HTTP status and headers
// ===========================================================================

bool URLClient::processHttpDownResponse(const QList<QByteArray>& headers)
{
    // MFC URLClient.cpp:237-327. False disconnects.
    PartFile* file = reqFile();
    if (!file || headers.isEmpty())
        return false;

    const int code = parseStatusCode(headers.first());
    const bool expectData = code == 200 || code == 206;
    const bool redirection = code == 301 || code == 302;
    if (!expectData && !redirection) {
        logDebug(QStringLiteral("URLClient: unexpected HTTP status %1 from %2").arg(code).arg(m_urlHost));
        return false;
    }

    if (redirection) {
        QString location = QString::fromUtf8(headerValue(headers, "location"));
        if (location.isEmpty())
            return false;
        // "/other/path" or "next.bin": relative to the URL just asked for
        if (const QUrl target(location); target.isRelative())
            location = m_url.resolved(target).toString();
        if (++m_redirected >= kMaxRedirects) {
            logDebug(QStringLiteral("URLClient: too many redirections from %1").arg(m_urlHost));
            return false;
        }
        // the new host is resolved afresh; the reserved blocks go with the old request
        setUserAddress({});
        if (!setUrl(location, Address{})) {
            logDebug(QStringLiteral("URLClient: bad redirection URL \"%1\"").arg(location));
            return false;
        }
        clearDownloadBlockRequests();
        releaseSocket(/*destroy*/ true);
        if (!tryToConnect(true))
            disconnected(QStringLiteral("Failed to connect to redirected URL"));
        return false;   // the old socket, no longer ours, closes
    }

    const uint64 fileSize = static_cast<uint64>(file->fileSize());
    if (const QByteArray length = headerValue(headers, "content-length"); !length.isEmpty()) {
        const uint64 contentLength = length.toULongLong();
        // the whole file is tolerated here; the range check below still has to pass
        if (contentLength != m_reqEnd - m_reqStart + 1 && contentLength != fileSize) {
            logDebug(QStringLiteral("URLClient: unexpected Content-Length %1 from %2")
                         .arg(contentLength).arg(m_urlHost));
            return false;
        }
    }

    uint64 first = 0, last = 0, total = 0;
    if (!parseContentRange(headerValue(headers, "content-range"), first, last, total)
        || first != m_reqStart || last != m_reqEnd || total != fileSize) {
        logDebug(QStringLiteral("URLClient: no valid Content-Range from %1").arg(m_urlHost));
        return false;
    }

    m_rangeStart = first;
    setDownloadState(DownloadState::Downloading);
    return true;
}

// ===========================================================================
// processHttpDownResponseBody
// ===========================================================================

bool URLClient::processHttpDownResponseBody(const uint8* data, uint32 size)
{
    if (!data || size == 0)
        return false;

    return processHttpBlockPacket(data, size);
}

// ===========================================================================
// processHttpBlockPacket — process HTTP data as file block
// ===========================================================================

bool URLClient::processHttpBlockPacket(const uint8* data, uint32 size)
{
    // MFC URLClient.cpp:341-414. False disconnects.
    PartFile* file = reqFile();
    if (!file || file->isStopped()
        || (file->status() != PartFileStatus::Ready && file->status() != PartFileStatus::Empty))
        return false;
    if (m_rangeStart == kNoRange)
        return false;   // data nobody asked for
    if (downloadState() != DownloadState::Downloading
        && downloadState() != DownloadState::NoNeededParts)
        return false;

    // Accumulate for rate averaging (drained in calculateDownloadRate)
    accumulateDownBytes(size);
    bookHttpDownload(size);

    writeHttpData(m_rangeStart, data, size);
    m_rangeStart += size;

    // range done: ask for the next one over the same connection
    if (pendingBlocks().empty()) {
        m_rangeStart = kNoRange;
        sendHttpBlockRequests();
    }
    return true;
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
// buildGetHeader — protected: request line + headers common to every GET
// ===========================================================================

QByteArray URLClient::buildGetHeader() const
{
    QByteArray request;
    request += "GET ";
    request += m_urlPathLocal;
    request += " HTTP/1.1\r\n";
    request += "Host: ";
    request += m_urlHost.toUtf8();
    if (m_urlPort != (m_urlTls ? 443 : 80)) {
        request += ':';
        request += QByteArray::number(m_urlPort);
    }
    request += "\r\n";
    // Identify ourselves. An anonymous GET is what a default WAF ruleset challenges,
    // and a cache or web source behind one then looks broken rather than blocked.
    request += Http::userAgentHeaderLine();
    request += "Accept: */*\r\n";
    request += "Connection: keep-alive\r\n";

    return request;
}

// ===========================================================================
// sendRawRequest — protected
// ===========================================================================

bool URLClient::sendRawRequest(const QByteArray& request)
{
    if (!socket())
        return false;

    auto packet = std::make_unique<RawPacket>(request.constData(),
                                              static_cast<uint32>(request.size()));
    sendPacket(std::move(packet));

    return true;
}

// ===========================================================================
// parseStatusCode — protected, static
// ===========================================================================

int URLClient::parseStatusCode(const QByteArray& statusLine)
{
    if (!statusLine.startsWith("HTTP/"))
        return -1;

    const auto spaceIdx = statusLine.indexOf(' ');
    if (spaceIdx < 0)
        return -1;

    bool ok = false;
    const int code = statusLine.mid(spaceIdx + 1, 3).toInt(&ok);

    return ok ? code : -1;
}

// ===========================================================================
// headerValue — protected, static
// ===========================================================================

QByteArray URLClient::headerValue(const QList<QByteArray>& headers, const char* name)
{
    const QByteArray prefix = QByteArray(name).toLower() + ':';

    // Skip index 0: that is the status line, not a header.
    for (qsizetype i = 1; i < headers.size(); ++i) {
        if (headers[i].toLower().startsWith(prefix))
            return headers[i].mid(headers[i].indexOf(':') + 1).trimmed();
    }

    return {};
}

// ===========================================================================
// connectToHost — private: create socket and initiate TCP connection
// ===========================================================================

bool URLClient::parseContentRange(const QByteArray& value, uint64& first, uint64& last,
                                        uint64& total)
{
    // "bytes <first>-<last>/<total>" — RFC 9110 §14.4. Anything else, including
    // the unsatisfied "bytes */<total>" form, is not something we can follow.
    if (!value.startsWith("bytes "))
        return false;

    const QByteArray spec = value.mid(6).trimmed();
    const auto dash = spec.indexOf('-');
    const auto slash = spec.indexOf('/');
    if (dash <= 0 || slash <= dash)
        return false;

    bool okFirst = false;
    bool okLast = false;
    bool okTotal = false;

    first = spec.left(dash).trimmed().toULongLong(&okFirst);
    last = spec.mid(dash + 1, slash - dash - 1).trimmed().toULongLong(&okLast);
    total = spec.mid(slash + 1).trimmed().toULongLong(&okTotal);

    return okFirst && okLast && okTotal;
}

void URLClient::connectToHost()
{
    // An HTTP response is not ed2k-framed, so it must arrive on a raw-data socket.
    // A plain ClientReqSocket sends it through EMSocket's packet parser instead,
    // where the 'H' of "HTTP/1.1" is not one of OP_EDONKEYPROT/OP_PACKEDPROT/
    // OP_EMULEPROT and the connection dies with kErrWrongHeader before a single
    // body byte reaches processHttpDownResponseBody(). MFC picks the same class
    // here (srchybrid/URLClient.cpp:186 passes RUNTIME_CLASS(CHttpClientDownSocket)).
    // https: the socket talks plain HTTP to a relay on loopback, which carries it to
    // the server over TLS (see TlsRelay.h for why the socket cannot do TLS itself).
    uint16 relayPort = 0;
    if (m_tlsRelay) {
        m_tlsRelay->deleteLater();
        m_tlsRelay = nullptr;
    }
    if (m_urlTls) {
        m_tlsRelay = new TlsRelay(this);
        relayPort = m_tlsRelay->start(connectAddress(), m_urlPort, m_urlHost,
                                      thePrefs.proxySettings());
        if (relayPort == 0) {
            logDebug(QStringLiteral("URLClient: no TLS relay for %1: %2")
                         .arg(m_urlHost, m_tlsRelay->failure()));
            // queued: callers treat tryToConnect() as started
            QMetaObject::invokeMethod(this, [this] {
                disconnected(QStringLiteral("TLS relay failed"));
            }, Qt::QueuedConnection);
            return;
        }
    }

    auto* reqSocket = new HttpClientDownSocket(this);
    reqSocket->createSocket();
    setSocket(reqSocket);

    // Register with ListenSocket for tracking
    if (theApp.listenSocket) {
        theApp.listenSocket->addSocket(reqSocket);
        theApp.listenSocket->addConnection();
    }

    // Connect socket signals
    QObject::connect(reqSocket, &ClientReqSocket::clientDisconnected,
                     this, [this, reqSocket](const QString& reason) {
        if (socket() == reqSocket)   // a replaced socket must not tear down its successor
            disconnected(reason, true);
    });

    // Without this the TCP connection completes and then nothing happens: the
    // request is only built from connectionEstablished(). UpDownClient::connectToHost()
    // makes the same connection for ed2k peers (UpDownClient.cpp:1787).
    const auto connAddr = connectAddress();
    const QHostAddress addr = connAddr.toQHostAddress();
    if (relayPort != 0) {
        // The relay takes only the connection coming from this socket's port
        QObject::connect(reqSocket, &ClientReqSocket::socketConnected,
                         this, [this, reqSocket, relay = m_tlsRelay] {
            if (socket() != reqSocket || m_tlsRelay != relay)
                return;
            relay->expectClientPort(reqSocket->localPort());
            connectionEstablished();
        });
        // Proxy, bind address and interface pin belong to the TLS leg
        reqSocket->setProxy(QNetworkProxy::NoProxy);
        reqSocket->connectToHost(QHostAddress(QHostAddress::LocalHost), relayPort);
    } else {
        QObject::connect(reqSocket, &ClientReqSocket::socketConnected,
                         this, &URLClient::connectionEstablished);

        // Configure proxy
        reqSocket->initProxySupport(thePrefs.proxySettings());

        // Initiate TCP connection
        reqSocket->connectToPeer(connAddr, m_urlPort);
    }
    reqSocket->waitForOnConnect();

    logDebug(QStringLiteral("URLClient::connectToHost: connecting to %1:%2").arg(addr.toString()).arg(m_urlPort));
}

// ===========================================================================
// acceptResolvedAddress
// ===========================================================================

bool URLClient::acceptResolvedAddress(const Address& addr)
{
    // Same three rules DownloadQueue::addLinkUrlSource applies to a literal a link
    // carried (DownloadQueue.cpp:1036) — routable, not IP-filtered, not banned. Doing
    // it here as well is what finally makes the promise the callers have always
    // printed: HttpCacheManager::urlIsAcceptable screens a literal host and leaves a
    // name "to be vetted after resolution", and until now nothing vetted it. A name is
    // the easier half to abuse, since the resolver answer is the attacker's to choose.
    if (!vetPeerAddress(addr, theApp.ipFilter, theApp.clientList).isNull())
        return true;

    logDebug(QStringLiteral("URLClient: refusing %1 for host %2 — not a usable address")
                 .arg(addr.toString(), m_urlHost));
    disconnected(QStringLiteral("URL host resolved to an unusable address"));
    return false;
}

// ===========================================================================
// bookHttpDownload
// ===========================================================================

void URLClient::bookHttpDownload(uint64 bytes)
{
    // Explicit ClientSoftware::URL, because an HTTP client's own software field
    // is never set to it; port 0 lands under "Other Ports", like MFC's -2.
    if (bytes > 0 && theApp.statistics)
        theApp.statistics->addTransferData(ClientSoftware::URL, 0, false, false, bytes);
}

} // namespace eMule
