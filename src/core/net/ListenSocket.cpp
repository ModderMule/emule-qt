/// @file ListenSocket.cpp
/// @brief TCP server accepting incoming peer connections — replaces MFC CListenSocket.

#include "net/ListenSocket.h"
#include "net/ClientReqSocket.h"
#include "utils/Log.h"

#include <QHostAddress>

#include <algorithm>

namespace eMule {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr uint16 kMaxOpenSockets = 512;
static constexpr uint16 kSoftMaxOpenSockets = 400;
static constexpr uint16 kRateCheckIntervalMs = 5000;

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
    m_port = port;

    if (!listen(QHostAddress::AnyIPv4, port)) {
        logError(QStringLiteral("ListenSocket: Failed to listen on port %1: %2")
                     .arg(port).arg(errorString()));
        return false;
    }

    m_listening = true;
    logInfo(QStringLiteral("ListenSocket: Listening on port %1").arg(port));
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

    auto* reqSocket = new ClientReqSocket(nullptr, this);
    reqSocket->setSocketDescriptor(socketDescriptor);

    addSocket(reqSocket);
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
    // Check for timed-out sockets
    auto it = m_socketList.begin();
    while (it != m_socketList.end()) {
        ClientReqSocket* socket = *it;
        if (socket->checkTimeOut()) {
            logDebug(QStringLiteral("ListenSocket: Socket timed out: %1")
                         .arg(socket->debugClientInfo()));
            socket->disconnect(QStringLiteral("Timeout"));
            it = m_socketList.erase(it);
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
    uint32 count = static_cast<uint32>(m_socketList.size());

    // Hard limit
    if (count > kMaxOpenSockets)
        return true;

    // Soft limit with rate check
    if (!ignoreInterval && count > kSoftMaxOpenSockets)
        return true;

    return false;
}

void ListenSocket::addConnection()
{
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
            auto pkt = std::make_unique<Packet>(static_cast<uint8>(result), 0, OP_EDONKEYPROT);
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
    uint32 count = static_cast<uint32>(m_socketList.size());
    if (count < 20)
        return 1.0f;
    if (count < 100)
        return 0.8f;
    if (count < 200)
        return 0.6f;
    return 0.4f;
}

} // namespace eMule
