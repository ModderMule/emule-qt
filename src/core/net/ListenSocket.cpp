#include "pch.h"
/// @file ListenSocket.cpp
/// @brief TCP server accepting incoming peer connections — replaces MFC CListenSocket.

#include "net/ListenSocket.h"
#include "net/BindAddress.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "ipfilter/IPFilter.h"
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
    // narrows it to that one address and family.
    const auto bindTo = BindAddress::listenAddress();
    if (!bindTo)
        return false;   // unusable bind address, already reported
    if (!listen(*bindTo, port)) {
        logError(QStringLiteral("ListenSocket: Failed to listen on port %1: %2")
                     .arg(port).arg(errorString()));
        return false;
    }

    // When port=0, the OS assigns a random port. Read it back.
    m_port = serverPort();

    m_listening = true;
    logInfo(QStringLiteral("ListenSocket: Listening on port %1").arg(m_port));
    return true;
}

void ListenSocket::stopListening()
{
    if (m_listening) {
        close();
        m_listening = false;
    }
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
    // Only the hard limit: our own dialling must not lock inbound peers out
    // (MFC srchybrid/ListenSocket.cpp:2068).
    if (tooManySockets(true)) {
        // Reject — close immediately
        QTcpSocket temp;
        temp.setSocketDescriptor(socketDescriptor);
        temp.close();
        return;
    }

    // Wrap the descriptor first so the peer can be read through the Qt socket, then apply
    // the same two rejections MFC does (srchybrid/ListenSocket.cpp:2152-2167 for the plain
    // Accept path, :2038-2051 for the WSAAccept condition callback).  Rejecting here — before
    // addSocket() — is what makes safeDelete() safe: it does not call removeSocket(), so a
    // socket that has already entered m_socketList must never be torn down this way.
    auto* reqSocket = new ClientReqSocket(nullptr, this);
    reqSocket->setSocketDescriptor(socketDescriptor);

    const Address peer = Address::fromQHostAddress(reqSocket->peerAddress());

    // Address-typed, so both families are checked against their own range table.  A v4
    // peer arriving on the dual-stack listener as ::ffff:a.b.c.d is normalised back to
    // Family::IPv4 by Address::fromQHostAddress, so it is matched against the v4 table
    // and not, wrongly, against the v6 one.
    if (theApp.ipFilter && theApp.ipFilter->isFiltered(peer, thePrefs.ipFilterLevel())) {
        if (thePrefs.logFilteredIPs()) {
            logWarning(QStringLiteral("Rejecting connection attempt (IP=%1) - IP filter (%2)")
                           .arg(ipstr(peer), theApp.ipFilter->lastHitDescription()));
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
            logWarning(QStringLiteral("Rejecting connection attempt of banned client %1 %2")
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

    // Check for timed-out sockets
    auto it = m_socketList.begin();
    while (it != m_socketList.end()) {
        ClientReqSocket* socket = *it;
        if (socket->checkTimeOut()) {
            logDebug(QStringLiteral("ListenSocket: Socket timed out: %1 "
                                    "socketState=%2 qtState=%3 fd=%4 error=%5")
                         .arg(socket->debugClientInfo())
                         .arg(static_cast<int>(socket->peerSocketState()))
                         .arg(static_cast<int>(socket->state()))
                         .arg(socket->socketDescriptor())
                         .arg(socket->errorString()));
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
    if (m_activeConnections > m_maxConnectionReached)
        m_maxConnectionReached = m_activeConnections;

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

float ListenSocket::maxConPerFiveModifier() const
{
    float spikeSize = std::max(1.0f,
        static_cast<float>(m_socketList.size()) - m_averageConnections);
    float spikeTolerance = 25.0f * thePrefs.maxConsPerFive() / 10.0f;
    return (spikeSize > spikeTolerance) ? 0.0f : 1.0f - spikeSize / spikeTolerance;
}

} // namespace eMule
