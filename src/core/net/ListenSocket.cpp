#include "pch.h"
/// @file ListenSocket.cpp
/// @brief TCP server accepting incoming peer connections — replaces MFC CListenSocket.

#include "net/ListenSocket.h"
#include "net/BindAddress.h"
#include "net/InterfacePin.h"
#include "net/ListenConflict.h"
#include "app/AppContext.h"
#include "server/ServerConnect.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "ipfilter/IPFilter.h"
#include "net/PortTestGate.h"
#include "net/Address.h"
#include "net/ClientReqSocket.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "utils/Log.h"

#include <QHostAddress>


namespace eMule {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ListenSocket::ListenSocket(QObject* parent)
    : QTcpServer(parent)
{
    m_elapsedTimer.start();
}

ListenSocket::~ListenSocket()
{
    killAllSockets();
}

// ---------------------------------------------------------------------------
// Listening
// ---------------------------------------------------------------------------

bool ListenSocket::startListening(uint16 port)
{
    // Any is dual-stack (AF_INET6, IPV6_V6ONLY=0): accept v4 + v6. A bind address
    // narrows it to that one address and family; a whole interface to its addresses.
    const QList<QHostAddress> addresses = BindAddress::listenAddresses();
    if (addresses.isEmpty() && !BindAddress::outboundAllowed())
        return false;   // unusable bind address, already reported
    if (addresses == QList{QHostAddress(QHostAddress::Any)}
        && ipv4WildcardHeld(port, QAbstractSocket::TcpSocket)) {
        logError(QStringLiteral("ListenSocket: port %1 is already used by another program (IPv4)")
                     .arg(port));
        return false;
    }
    m_acceptPaused = false;
    QString error;
    const bool ok = openAll(addresses, port, error);
    // nothing bindable yet (IPv6 DAD): syncAddresses() opens it
    m_listening = isSetActive();
    m_port = setPort();
    if (!ok) {
        logError(QStringLiteral("ListenSocket: Failed to listen on port %1: %2").arg(port).arg(error));
        return false;
    }
    logInfo(QStringLiteral("ListenSocket: Listening on port %1").arg(m_port));
    return true;
}

bool ListenSocket::syncAddresses(const QList<QHostAddress>& addresses)
{
    const bool changed = syncTo(addresses);
    if (m_listening)
        m_port = setPort();
    return changed;
}

void ListenSocket::stopListening()
{
    m_acceptPaused = false;
    if (m_listening) {
        closeAll();
        m_listening = false;
    }
}

bool ListenSocket::serverIsConnecting() const
{
    // a background login (handover, HighID recheck) is tested too
    return theApp.serverConnect
        && (theApp.serverConnect->isConnecting() || theApp.serverConnect->isHandoverLoggingIn());
}

bool ListenSocket::rebind(uint16 port)
{
    stopListening();
    return startListening(port);
}

// ---------------------------------------------------------------------------
// Incoming connections
// ---------------------------------------------------------------------------

void ListenSocket::incomingConnection(qintptr socketDescriptor)
{
    // Only the hard limit: our own dialling must not lock inbound peers out. At the
    // limit MFC stops accepting and leaves callers in the backlog until there is room
    // (srchybrid/ListenSocket.cpp:2060-2073) — except during a server login, whose
    // callback test would otherwise fail and earn a LowID.
    if (tooManySockets(true) && !serverIsConnecting()) {
        if (m_acceptPaused) {
            // Already accepted by Qt in the same burst as the one that paused us
            QTcpSocket temp;
            temp.setSocketDescriptor(socketDescriptor);
            temp.close();
            return;
        }
        m_acceptPaused = true;
        ++m_maxConnectionReached; // MFC counts in StopListening()
        pauseAll();
        // This one is accepted already and cannot go back to the backlog: keep it.
    }

    // Wrap the descriptor first so the peer can be read through the Qt socket, then apply
    // the same two rejections MFC does (srchybrid/ListenSocket.cpp:2152-2167 for the plain
    // Accept path, :2038-2051 for the WSAAccept condition callback).  Rejecting here — before
    // addSocket() — is what makes safeDelete() safe: it does not call removeSocket(), so a
    // socket that has already entered m_socketList must never be torn down this way.
    auto* reqSocket = new ClientReqSocket(nullptr, this);
    reqSocket->setSocketDescriptor(socketDescriptor);

    // Answers must leave through the bound interface too.
    if (!InterfacePin::pin(socketDescriptor)) {
        reqSocket->safeDelete();
        return;
    }

    admitAccepted(reqSocket);
}

void ListenSocket::adoptTransport(std::shared_ptr<StreamTransport> transport)
{
    if (!transport)
        return;
    // A punched NAT-T link is a connection we accept: same hard limit, same rejections.
    if (tooManySockets(true) && !serverIsConnecting()) {
        transport->close();
        return;
    }
    auto* reqSocket = new ClientReqSocket(nullptr, this);
    reqSocket->attachTransport(std::move(transport), /*announce*/ false);
    admitAccepted(reqSocket);
}


// ---------------------------------------------------------------------------
// Connection pool management
// ---------------------------------------------------------------------------

void ListenSocket::addSocket(ClientReqSocket* socket)
{
    if (socket && !isValidSocket(socket))
        m_socketList.push_back(socket);
}

void ListenSocket::removeSocket(ClientReqSocket* socket)
{
    m_socketList.remove(socket);
}

bool ListenSocket::isValidSocket(ClientReqSocket* socket) const
{
    return std::find(m_socketList.begin(), m_socketList.end(), socket) != m_socketList.end();
}

void ListenSocket::killAllSockets()
{
    for (auto* socket : m_socketList) {
        socket->safeDelete();
    }
    m_socketList.clear();
}

void ListenSocket::disconnectAll(const QString& reason)
{
    // disconnect() removes the socket from the list.
    const auto sockets = m_socketList;
    for (auto* socket : sockets)
        socket->disconnect(reason);
}

// ---------------------------------------------------------------------------
// Periodic maintenance
// ---------------------------------------------------------------------------

void ListenSocket::process()
{
    // Reset per-5-second connection counter every 5th call (~5s)
    if (++m_processTickCount >= 5) {
        m_processTickCount = 0;
        m_openSocketsInterval = 0;
    }

    // MFC ReStartListening (srchybrid/ListenSocket.cpp:2191): with five to spare
    if (m_acceptPaused
        && (openSockets() + 5 < thePrefs.maxConnections() || serverIsConnecting())) {
        m_acceptPaused = false;
        resumeAll();
    }

    // Check for timed-out sockets
    auto it = m_socketList.begin();
    while (it != m_socketList.end()) {
        ClientReqSocket* socket = *it;
        if (socket->checkTimeOut()) {
            logDebug([&]() -> QString { return QStringLiteral("ListenSocket: Socket timed out: %1 "
                                    "socketState=%2 qtState=%3 fd=%4 error=%5")
                         .arg(socket->debugClientInfo())
                         .arg(static_cast<int>(socket->peerSocketState()))
                         .arg(static_cast<int>(socket->state()))
                         .arg(socket->socketDescriptor())
                         .arg(socket->errorString()); });
            it = m_socketList.erase(it);
            socket->disconnect(QStringLiteral("Timeout"));
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// Rate limiting
// ---------------------------------------------------------------------------

bool ListenSocket::tooManySockets(bool ignoreInterval) const
{
    if (static_cast<uint32>(m_socketList.size()) > thePrefs.maxConnections())
        return true;
    if (!ignoreInterval
        && m_openSocketsInterval > thePrefs.maxConsPerFive() * maxConPerFiveModifier())
        return true;
    if (!ignoreInterval && m_nHalfOpen >= thePrefs.maxHalfConnections())
        return true;
    return false;
}

void ListenSocket::addConnection()
{
    ++m_openSocketsInterval;
}

void ListenSocket::noteSocketState(PeerSocketState from, PeerSocketState to)
{
    if (from == to)
        return;
    if (from == PeerSocketState::Half && m_nHalfOpen > 0)
        --m_nHalfOpen;
    else if (from == PeerSocketState::Complete && m_nComplete > 0)
        --m_nComplete;
    if (to == PeerSocketState::Half)
        ++m_nHalfOpen;
    else if (to == PeerSocketState::Complete)
        ++m_nComplete;
}

bool ListenSocket::sendPortTestReply(char result, bool doDisconnect)
{
    // Find the port test socket
    for (auto* socket : m_socketList) {
        if (socket->isPortTestConnection()) {
            auto pkt = std::make_unique<Packet>(OP_PORTTEST, 1);
            pkt->pBuffer[0] = result;
            socket->sendPacket(std::move(pkt), true, 0, true);
            if (doDisconnect)
                socket->disconnect(QStringLiteral("Port test complete"));
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

void ListenSocket::recalculateStats()
{
    m_connectionStates[0] = 0; // Other
    m_connectionStates[1] = 0; // Half
    m_connectionStates[2] = 0; // Complete

    for (const auto* socket : m_socketList)
        ++m_connectionStates[static_cast<int>(socket->peerSocketState())];
}

// MFC CListenSocket::UpdateConnectionsStatus (srchybrid/ListenSocket.cpp:2266-2295)
void ListenSocket::updateConnectionsStatus()
{
    m_activeConnections = static_cast<uint32>(m_socketList.size());
    if (m_activeConnections > m_peakConnections)
        m_peakConnections = m_activeConnections;

    if (!theApp.isConnected())
        return;

    ++m_totalConnectionChecks;
    const float keep = std::min(
        0.99f, static_cast<float>(m_totalConnectionChecks - 1)
                   / static_cast<float>(m_totalConnectionChecks));
    m_averageConnections = std::max(
        0.001f, m_averageConnections * keep
                    + static_cast<float>(m_activeConnections) * (1.0f - keep));
}

uint64 ListenSocket::bufferedBytes() const
{
    uint64 n = 0;
    for (const ClientReqSocket* sock : m_socketList)
        n += sock->bufferedBytes();
    return n;
}

float ListenSocket::maxConPerFiveModifier() const
{
    float spikeSize = std::max(1.0f,
        static_cast<float>(m_socketList.size()) - m_averageConnections);
    float spikeTolerance = 25.0f * thePrefs.maxConsPerFive() / 10.0f;
    return (spikeSize > spikeTolerance) ? 0.0f : 1.0f - spikeSize / spikeTolerance;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void ListenSocket::admitAccepted(ClientReqSocket* reqSocket)
{
    if (thePrefs.disableEd2k()) {
        reqSocket->safeDelete();
        return;
    }
    const Address peer = Address::fromQHostAddress(reqSocket->peerAddress());

    // Address-typed, so both families are checked against their own range table.  A v4
    // peer arriving on the dual-stack listener as ::ffff:a.b.c.d is normalised back to
    // Family::IPv4 by Address::fromQHostAddress, so it is matched against the v4 table
    // and not, wrongly, against the v6 one.
    // a running port test lets its server through (#266)
    if (theApp.ipFilter && !PortTestGate::isExempt(peer)
        && theApp.ipFilter->isFiltered(peer, thePrefs.ipFilterLevel())) {
        PortTestGate::noteFiltered(peer, theApp.ipFilter->lastHitDescription(), true);
        if (thePrefs.logFilteredIPs()) {
            logDebug([&]() -> QString { return QStringLiteral("Rejecting connection attempt (IP=%1) - IP filter (%2)")
                           .arg(ipstr(peer), theApp.ipFilter->lastHitDescription()); });
        }
        if (theApp.statistics)
            theApp.statistics->addFilteredClient();
        reqSocket->safeDelete();
        return;
    }

    if (theApp.clientList && theApp.clientList->isBannedClient(peer)) {
        // MFC increments no counter on this branch — only the filter branch feeds
        // theStats.filteredclients.  Keep that split so the statistic keeps its meaning.
        if (thePrefs.logBannedClients()) {
            // MFC logs the offending client's info via FindClientByIP (IP only, no port).
            // findByAddress() would need the port too, so use the IPv4 lookup and simply
            // omit the name for a v6 peer.
            const UpDownClient* banned =
                peer.isIPv4() ? theApp.clientList->findByIP(peer.toNetworkUint32()) : nullptr;
            logLeecher(QStringLiteral("Rejecting connection attempt of banned client %1 %2")
                           .arg(ipstr(peer), banned ? banned->userName() : QString()));
        }
        reqSocket->safeDelete();
        return;
    }

    reqSocket->setObfuscationConfig(thePrefs.obfuscationConfig());
    // We accepted this one, so the peer owes us an OP_HELLO before anything else.
    reqSocket->setIncoming(true);

    addSocket(reqSocket);
    addConnection();
    emit newClientConnection(reqSocket);
}

// ---------------------------------------------------------------------------
// Listen addresses (#190)
// ---------------------------------------------------------------------------

/// A further address: accepts into the owner.
class ListenSocket::Extra : public QTcpServer {
public:
    explicit Extra(ListenSocket& owner) : m_owner(owner) {}

protected:
    void incomingConnection(qintptr socketDescriptor) override { m_owner.incomingConnection(socketDescriptor); }

private:
    ListenSocket& m_owner;
};

ListenAddressSet::BindResult ListenSocket::listenOn(QTcpServer& server, const QHostAddress& address,
                                                    uint16 port, QString& error)
{
    if (!server.listen(address, port)) {
        error = server.errorString();
        // IPv6 DAD unfinished: EADDRNOTAVAIL until it is
        return server.serverError() == QAbstractSocket::SocketAddressNotAvailableError ? BindResult::Later
                                                                                       : BindResult::Failed;
    }
    if (!InterfacePin::pin(server.socketDescriptor())) {
        server.close();
        error = QStringLiteral("cannot pin the socket to the interface");
        return BindResult::Failed;
    }
    if (m_acceptPaused)
        server.pauseAccepting();
    return BindResult::Ok;
}

ListenAddressSet::BindResult ListenSocket::bindOne(const QHostAddress& address, uint16 port, QString& error)
{
    if (!isListening())
        return listenOn(*this, address, port, error);
    auto extra = std::make_unique<Extra>(*this);
    const BindResult r = listenOn(*extra, address, port, error);
    if (r == BindResult::Ok)
        m_extras.push_back(std::move(extra));
    return r;
}

void ListenSocket::closeOne(const QHostAddress& address)
{
    if (const auto it = std::ranges::find_if(m_extras, [&address](const auto& e) { return e->serverAddress() == address; });
        it != m_extras.end()) {
        (*it)->close();
        m_extras.erase(it);
        return;
    }
    if (!isListening() || serverAddress() != address)
        return;
    close();
    if (m_extras.empty())
        return;
    // this server stays the listening one (isListening(), serverPort()): it takes over the next
    const QHostAddress next = m_extras.front()->serverAddress();
    const uint16 port = m_extras.front()->serverPort();
    m_extras.front()->close();
    m_extras.erase(m_extras.begin());
    if (QString error; listenOn(*this, next, port, error) != BindResult::Ok)
        logError(QStringLiteral("ListenSocket: lost %1 port %2 while moving it: %3").arg(next.toString()).arg(port).arg(error));
}

uint16 ListenSocket::portOf(const QHostAddress& address) const
{
    if (isListening() && serverAddress() == address)
        return serverPort();
    for (const auto& e : m_extras)
        if (e->serverAddress() == address)
            return e->serverPort();
    return 0;
}

void ListenSocket::pauseAll()
{
    pauseAccepting();
    for (const auto& e : m_extras)
        e->pauseAccepting();
}

void ListenSocket::resumeAll()
{
    resumeAccepting();
    for (const auto& e : m_extras)
        e->resumeAccepting();
}

} // namespace eMule
