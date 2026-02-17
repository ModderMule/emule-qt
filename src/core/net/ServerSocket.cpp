/// @file ServerSocket.cpp
/// @brief TCP connection to an ED2K server — replaces MFC CServerSocket.

#include "net/ServerSocket.h"
#include "net/Packet.h"
#include "app/AppContext.h"
#include "ipfilter/IPFilter.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "server/Server.h"
#include "stats/Statistics.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QHostAddress>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ServerSocket::ServerSocket(bool manualSingleConnect, QObject* parent)
    : EMSocket(parent)
    , m_manualSingleConnect(manualSingleConnect)
{
    m_elapsedTimer.start();

    connect(this, &QAbstractSocket::connected, this, &ServerSocket::onSocketConnected);
    connect(this, &QAbstractSocket::disconnected, this, &ServerSocket::onSocketDisconnected);
    connect(this, &QAbstractSocket::errorOccurred, this, &ServerSocket::onSocketError);
}

ServerSocket::~ServerSocket()
{
    m_isDeleting = true;
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

void ServerSocket::connectTo(const Server& server, bool noCrypt)
{
    m_curServer = std::make_unique<Server>(server);
    m_noCrypt = noCrypt;
    m_startNewMessageLog = true;

    setConnectionState(ServerConnState::Connecting);

    // If server has a dynamic IP, resolve it first
    if (m_curServer->hasDynIP()) {
        m_dnsLookup = std::make_unique<QDnsLookup>(QDnsLookup::A, m_curServer->dynIP(), this);
        connect(m_dnsLookup.get(), &QDnsLookup::finished, this, &ServerSocket::onDnsLookupFinished);
        m_dnsLookup->lookup();
        return;
    }

    // Direct connection via IP
    uint32 ip = m_curServer->ip();
    QHostAddress addr(ntohl(ip)); // Convert from network to host byte order
    uint16 port = m_curServer->port();

    // Configure encryption
    if (!noCrypt && m_curServer->supportsObfuscationTCP()) {
        setConnectionEncryption(true, nullptr, true);
        port = m_curServer->obfuscationPortTCP();
    }

    connectToHost(addr, port);
}

// ---------------------------------------------------------------------------
// Packet sending override
// ---------------------------------------------------------------------------

void ServerSocket::sendPacket(std::unique_ptr<Packet> packet, bool controlPacket,
                              uint32 actualPayloadSize, bool forceImmediateSend)
{
    m_lastTransmission = static_cast<uint32>(m_elapsedTimer.elapsed());
    EMSocket::sendPacket(std::move(packet), controlPacket, actualPayloadSize, forceImmediateSend);
}

// ---------------------------------------------------------------------------
// Packet processing
// ---------------------------------------------------------------------------

bool ServerSocket::packetReceived(Packet* packet)
{
    m_lastTransmission = static_cast<uint32>(m_elapsedTimer.elapsed());

    if (auto* stats = theApp.statistics)
        stats->addDownDataOverheadServer(packet->size);

    const auto* data = reinterpret_cast<const uint8*>(packet->pBuffer);
    return processPacket(data, packet->size, packet->opcode);
}

bool ServerSocket::processPacket(const uint8* packet, uint32 size, uint8 opcode)
{
    switch (opcode) {
    case OP_SERVERMESSAGE: {
        // Format: uint16 msgLen, char[msgLen] message
        if (size < 2)
            return false;

        uint16 msgLen = peekUInt16(packet);
        if (size < 2u + msgLen)
            return false;

        // ED2K servers send UTF-8 or Latin-1 text
        QString msg = QString::fromUtf8(reinterpret_cast<const char*>(packet + 2), msgLen);
        if (msg.isEmpty())
            msg = QString::fromLatin1(reinterpret_cast<const char*>(packet + 2), msgLen);

        logInfo(QStringLiteral("Server message: %1").arg(msg));
        emit serverMessage(msg);
        break;
    }

    case OP_IDCHANGE: {
        // Format: uint32 clientID [, uint32 tcpFlags]
        if (size < 4)
            return false;

        uint32 clientID = peekUInt32(packet);
        uint32 tcpFlags = 0;
        if (size >= 8)
            tcpFlags = peekUInt32(packet + 4);

        if (clientID == 0) {
            // Server is full
            setConnectionState(ServerConnState::ServerFull);
            return false;
        }

        // Apply server TCP flags to our server copy
        if (m_curServer) {
            m_curServer->setTCPFlags(tcpFlags);
        }

        setConnectionState(ServerConnState::Connected);
        emit loginReceived(clientID, tcpFlags);

        logInfo(QStringLiteral("Server login successful, ID: %1 (flags: 0x%2)")
                    .arg(clientID)
                    .arg(tcpFlags, 8, 16, QLatin1Char('0')));
        break;
    }

    case OP_SEARCHRESULT: {
        // Raw search result data — forward to search engine
        if (size < 4)
            return false;

        // The original checks the last 16 bytes for "more results" flag
        // For now, forward the entire blob and let the search engine parse
        bool moreAvailable = false;
        emit searchResultReceived(packet, size, moreAvailable);
        break;
    }

    case OP_FOUNDSOURCES:
    case OP_FOUNDSOURCES_OBFU: {
        // Format: hash16[16], uint8 sourceCount, sources...
        if (size < 17)
            return false;

        bool obfuscated = (opcode == OP_FOUNDSOURCES_OBFU);
        emit foundSourcesReceived(packet, size, obfuscated);
        break;
    }

    case OP_SERVERSTATUS: {
        // Format: uint32 users, uint32 files [, optional extended data]
        if (size < 8)
            return false;

        uint32 users = peekUInt32(packet);
        uint32 files = peekUInt32(packet + 4);

        if (m_curServer) {
            m_curServer->setUsers(users);
            m_curServer->setFiles(files);

            // Parse optional extended status
            if (size >= 12)
                m_curServer->setMaxUsers(peekUInt32(packet + 8));
            if (size >= 16)
                m_curServer->setSoftFiles(peekUInt32(packet + 12));
            if (size >= 20)
                m_curServer->setHardFiles(peekUInt32(packet + 16));
            if (size >= 24) {
                uint32 udpFlags = peekUInt32(packet + 20);
                m_curServer->setUDPFlags(udpFlags);
            }
            if (size >= 28)
                m_curServer->setLowIDUsers(peekUInt32(packet + 24));
            if (size >= 30)
                m_curServer->setObfuscationPortTCP(peekUInt16(packet + 28));
            if (size >= 32)
                m_curServer->setObfuscationPortUDP(peekUInt16(packet + 30));
            if (size >= 36) {
                m_curServer->setServerKeyUDP(peekUInt32(packet + 32));
                m_curServer->setServerKeyUDPIP(thePrefs.publicIP());
            }
        }

        emit serverStatusReceived(users, files);
        break;
    }

    case OP_SERVERIDENT: {
        // Format: hash16[16], ip[4], port[2], tagCount[4], tags...
        if (size < 26)
            return false;

        const uint8* serverHash = packet;
        uint32 serverIP = peekUInt32(packet + 16);
        uint16 serverPort = peekUInt16(packet + 20);
        uint32 tagCount = peekUInt32(packet + 22);

        QString name;
        QString description;

        // Parse tags
        try {
            SafeMemFile tagData(const_cast<uint8*>(packet + 26), size - 26);
            for (uint32 i = 0; i < tagCount; ++i) {
                Tag tag(tagData, true);
                if (tag.nameId() == ST_SERVERNAME && tag.isStr())
                    name = tag.strValue();
                else if (tag.nameId() == ST_DESCRIPTION && tag.isStr())
                    description = tag.strValue();
            }
        } catch (...) {
            // Tag parsing failed — acceptable, name/desc may be partial
        }

        if (m_curServer) {
            // Update server's reported IP if different
            if (serverIP != 0)
                m_curServer->setIP(serverIP);
        }

        emit serverIdentReceived(serverHash, serverIP, serverPort, name, description);
        break;
    }

    case OP_SERVERLIST: {
        // Format: uint8 count, [ip4 port2]* count
        if (size < 1)
            return false;

        emit serverListReceived(packet, size);
        break;
    }

    case OP_CALLBACKREQUESTED: {
        // Format: ip[4], port[2] [, cryptFlags[1], userHash[16]]
        if (size < 6)
            return false;

        uint32 clientIP = peekUInt32(packet);
        uint16 clientPort = peekUInt16(packet + 4);

        if (auto* filter = theApp.ipFilter) {
            if (filter->isFiltered(clientIP, thePrefs.ipFilterLevel())) {
                if (auto* stats = theApp.statistics)
                    stats->addFilteredClient();
                break;
            }
        }

        const uint8* cryptOptions = nullptr;
        uint32 cryptSize = 0;
        if (size > 6) {
            cryptOptions = packet + 6;
            cryptSize = size - 6;
        }

        emit callbackRequested(clientIP, clientPort, cryptOptions, cryptSize);
        break;
    }

    case OP_CALLBACK_FAIL:
        logWarning(QStringLiteral("Server: Callback attempt failed"));
        break;

    case OP_REJECT:
        logWarning(QStringLiteral("Server rejected our request"));
        emit rejectReceived();
        break;

    default:
        logDebug(QStringLiteral("ServerSocket: Unknown opcode 0x%1, size %2")
                     .arg(opcode, 2, 16, QLatin1Char('0'))
                     .arg(size));
        break;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Connection state management
// ---------------------------------------------------------------------------

void ServerSocket::setConnectionState(ServerConnState newState)
{
    if (m_connectionState == newState)
        return;

    m_connectionState = newState;
    emit connectionStateChanged(newState);

    if (newState == ServerConnState::ServerDead ||
        newState == ServerConnState::FatalError ||
        newState == ServerConnState::ServerFull) {
        emit connectionFailed(newState);
    }
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

void ServerSocket::onError(int errorCode)
{
    logWarning(QStringLiteral("ServerSocket error: %1").arg(errorCode));

    if (m_connectionState == ServerConnState::Connecting ||
        m_connectionState == ServerConnState::WaitForLogin) {
        setConnectionState(ServerConnState::ServerDead);
    } else {
        setConnectionState(ServerConnState::FatalError);
    }
}

// ---------------------------------------------------------------------------
// Socket event handlers
// ---------------------------------------------------------------------------

void ServerSocket::onSocketConnected()
{
    setConnectionState(ServerConnState::WaitForLogin);
    m_lastTransmission = static_cast<uint32>(m_elapsedTimer.elapsed());
}

void ServerSocket::onSocketDisconnected()
{
    if (m_isDeleting)
        return;

    if (m_connectionState == ServerConnState::Connected) {
        setConnectionState(ServerConnState::Disconnected);
    } else if (m_connectionState != ServerConnState::NotConnected) {
        setConnectionState(ServerConnState::ServerDead);
    }
}

void ServerSocket::onSocketError(QAbstractSocket::SocketError error)
{
    if (m_isDeleting)
        return;

    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
    case QAbstractSocket::RemoteHostClosedError:
    case QAbstractSocket::SocketTimeoutError:
    case QAbstractSocket::HostNotFoundError:
        setConnectionState(ServerConnState::ServerDead);
        break;

    case QAbstractSocket::SocketAccessError:
    case QAbstractSocket::NetworkError:
        setConnectionState(ServerConnState::FatalError);
        break;

    default:
        if (m_connectionState == ServerConnState::Connecting)
            setConnectionState(ServerConnState::ServerDead);
        else
            setConnectionState(ServerConnState::FatalError);
        break;
    }
}

void ServerSocket::onDnsLookupFinished()
{
    if (!m_dnsLookup || !m_curServer)
        return;

    if (m_dnsLookup->error() != QDnsLookup::NoError) {
        logWarning(QStringLiteral("DNS lookup failed for %1: %2")
                       .arg(m_curServer->dynIP())
                       .arg(m_dnsLookup->errorString()));
        setConnectionState(ServerConnState::ServerDead);
        return;
    }

    const auto records = m_dnsLookup->hostAddressRecords();
    if (records.isEmpty()) {
        logWarning(QStringLiteral("DNS lookup returned no results for %1")
                       .arg(m_curServer->dynIP()));
        setConnectionState(ServerConnState::ServerDead);
        return;
    }

    QHostAddress addr = records.first().value();
    uint32 ip = htonl(addr.toIPv4Address());

    if (auto* filter = theApp.ipFilter) {
        if (filter->isFiltered(ip, thePrefs.ipFilterLevel())) {
            logWarning(QStringLiteral("DNS resolved IP %1 is filtered by IPFilter")
                           .arg(ipstr(ip)));
            setConnectionState(ServerConnState::ServerDead);
            return;
        }
    }

    m_curServer->setIP(ip);
    emit dynIPResolved(ip, m_curServer->dynIP());

    // Now connect
    uint16 port = m_curServer->port();
    if (!m_noCrypt && m_curServer->supportsObfuscationTCP()) {
        setConnectionEncryption(true, nullptr, true);
        port = m_curServer->obfuscationPortTCP();
    }

    connectToHost(addr, port);
}

} // namespace eMule
