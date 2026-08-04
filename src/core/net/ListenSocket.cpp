#include "pch.h"
/// @file ListenSocket.cpp
/// @brief TCP server accepting incoming peer connections — replaces MFC CListenSocket.

#include "net/ListenSocket.h"
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
    if (!listen(QHostAddress::Any, port)) {   // dual-stack (AF_INET6, IPV6_V6ONLY=0): accept v4 + v6
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
    if (tooManySockets()) {
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
    ++m_totalConnectionChecks;
    uint32 count = static_cast<uint32>(m_socketList.size());
    if (count > m_maxConnectionReached)
        m_maxConnectionReached = count;
    if (count > m_peakConnections)
        m_peakConnections = count;
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
    uint32 count = static_cast<uint32>(m_socketList.size());
    m_activeConnections = count;

    if (m_totalConnectionChecks > 0) {
        m_averageConnections =
            (m_averageConnections * static_cast<float>(m_totalConnectionChecks - 1) +
             static_cast<float>(count)) /
            static_cast<float>(m_totalConnectionChecks);
    }
}

void ListenSocket::updateConnectionsStatus()
{
    m_connectionStates[0] = 0; // Other
    m_connectionStates[1] = 0; // Half
    m_connectionStates[2] = 0; // Complete

    for (const auto* socket : m_socketList) {
        // Count by examining socket state via isConnected()
        if (socket->isConnected())
            ++m_connectionStates[2];
        else
            ++m_connectionStates[0];
    }

    m_nHalfOpen = m_connectionStates[1];
    m_nComplete = m_connectionStates[2];
}

float ListenSocket::maxConPerFiveModifier() const
{
    float spikeSize = std::max(1.0f,
        static_cast<float>(m_socketList.size()) - m_averageConnections);
    float spikeTolerance = 25.0f * thePrefs.maxConsPerFive() / 10.0f;
    return (spikeSize > spikeTolerance) ? 0.0f : 1.0f - spikeSize / spikeTolerance;
}

} // namespace eMule
