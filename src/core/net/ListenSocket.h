#pragma once

/// @file ListenSocket.h
/// @brief TCP server accepting incoming peer connections — replaces MFC CListenSocket.
///
/// Uses QTcpServer instead of CAsyncSocketEx. Manages connection pool,
/// rate limiting, and connection state statistics.

#include "utils/Types.h"

#include <QTcpServer>
#include <QElapsedTimer>

#include <list>
#include <memory>

namespace eMule {

class ClientReqSocket;

// ---------------------------------------------------------------------------
// ListenSocket
// ---------------------------------------------------------------------------

/// TCP server that accepts incoming peer-to-peer connections.
///
/// Each incoming connection creates a ClientReqSocket. Provides
/// connection pool management, rate limiting, and statistics.
class ListenSocket : public QTcpServer {
    Q_OBJECT

public:
    explicit ListenSocket(QObject* parent = nullptr);
    ~ListenSocket() override;

    /// Start listening on the specified port.
    bool startListening(uint16 port);

    /// Stop listening and close all connections.
    void stopListening();

    /// Rebind to a new port (for port change).
    bool rebind(uint16 port);

    /// Periodic maintenance (timeout checks, cleanup).
    void process();

    // -- Connection management -----------------------------------------------

    /// Register a socket in the connection pool.
    void addSocket(ClientReqSocket* socket);

    /// Remove a socket from the connection pool.
    void removeSocket(ClientReqSocket* socket);

    /// Check if a socket is in the pool.
    [[nodiscard]] bool isValidSocket(ClientReqSocket* socket) const;

    /// Close and delete all sockets.
    void killAllSockets();

    /// Whether too many connections are open (for rate limiting).
    [[nodiscard]] bool tooManySockets(bool ignoreInterval = false) const;

    /// Record a new connection attempt for rate tracking.
    void addConnection();

    /// Send a port test reply.
    bool sendPortTestReply(char result, bool doDisconnect = false);

    // -- Statistics -----------------------------------------------------------

    /// Recalculate connection statistics.
    void recalculateStats();

    /// Update connection state counters.
    void updateConnectionsStatus();

    [[nodiscard]] uint32 openSockets() const { return static_cast<uint32>(m_socketList.size()); }
    [[nodiscard]] uint32 maxConnectionReached() const { return m_maxConnectionReached; }
    [[nodiscard]] uint32 peakConnections() const { return m_peakConnections; }
    [[nodiscard]] uint32 totalConnectionChecks() const { return m_totalConnectionChecks; }
    [[nodiscard]] float  averageConnections() const { return m_averageConnections; }
    [[nodiscard]] uint32 activeConnections() const { return m_activeConnections; }
    [[nodiscard]] uint16 connectedPort() const { return m_port; }
    [[nodiscard]] uint32 totalHalfOpen() const { return m_nHalfOpen; }
    [[nodiscard]] uint32 totalComplete() const { return m_nComplete; }

    /// Connection rate modifier (for MAXCONPER5SEC scaling).
    [[nodiscard]] float maxConPerFiveModifier() const;

signals:
    /// New incoming connection accepted.
    void newClientConnection(eMule::ClientReqSocket* socket);

protected:
    void incomingConnection(qintptr socketDescriptor) override;

private:
    std::list<ClientReqSocket*> m_socketList;

    uint16 m_port = 0;
    bool m_listening = false;

    // Rate limiting
    uint16 m_openSocketsInterval = 0;
    uint16 m_processTickCount = 0;
    uint32 m_maxConnectionReached = 0;

    // Statistics
    uint32 m_peakConnections = 0;
    uint32 m_totalConnectionChecks = 0;
    float  m_averageConnections = 0.0f;
    uint32 m_activeConnections = 0;
    uint32 m_nHalfOpen = 0;
    uint32 m_nComplete = 0;
    int    m_pendingConnections = 0;

    // Connection state counters [Other, Half, Complete]
    uint16 m_connectionStates[3]{0, 0, 0};

    QElapsedTimer m_elapsedTimer;
};

} // namespace eMule
