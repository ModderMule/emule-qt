/// @file ClientReqSocket.cpp
/// @brief Peer-to-peer TCP socket — replaces MFC CClientReqSocket.

#include "net/ClientReqSocket.h"
#include "app/AppContext.h"
#include "stats/Statistics.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QHostAddress>

namespace eMule {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ClientReqSocket::ClientReqSocket(UpDownClient* client, QObject* parent)
    : EMSocket(parent)
    , m_client(client)
{
    m_elapsedTimer.start();
    m_timeoutTimer = static_cast<uint32>(m_elapsedTimer.elapsed());

    connect(this, &QAbstractSocket::connected, this, &ClientReqSocket::onSocketConnected);
    connect(this, &QAbstractSocket::disconnected, this, &ClientReqSocket::onSocketDisconnected);
    connect(this, &QAbstractSocket::errorOccurred, this, &ClientReqSocket::onSocketError);
}

ClientReqSocket::~ClientReqSocket()
{
    m_client = nullptr;
}

// ---------------------------------------------------------------------------
// Client association
// ---------------------------------------------------------------------------

void ClientReqSocket::setClient(UpDownClient* client)
{
    m_client = client;
}

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------

void ClientReqSocket::disconnect(const QString& reason)
{
    if (m_deleteThis)
        return;

    logDebug(QStringLiteral("ClientReqSocket::disconnect: %1").arg(reason));
    emit clientDisconnected(reason);

    m_deleteThis = true;
    close();
}

void ClientReqSocket::waitForOnConnect()
{
    setPeerSocketState(PeerSocketState::Half);
}

void ClientReqSocket::resetTimeOutTimer()
{
    m_timeoutTimer = static_cast<uint32>(m_elapsedTimer.elapsed());
}

bool ClientReqSocket::checkTimeOut()
{
    uint32 now = static_cast<uint32>(m_elapsedTimer.elapsed());
    uint32 elapsed = now - m_timeoutTimer;
    uint32 timeout = getTimeOut();

    if (m_socketState == PeerSocketState::Half) {
        // Give connecting sockets 4x timeout
        timeout *= 4;
    }

    return elapsed > timeout;
}

void ClientReqSocket::safeDelete()
{
    m_deleteThis = true;
    m_deleteTimer = static_cast<uint32>(m_elapsedTimer.elapsed());
    close();
    deleteLater();
}

bool ClientReqSocket::createSocket()
{
    // Socket is already created by QTcpSocket constructor
    return true;
}

// ---------------------------------------------------------------------------
// Packet sending override
// ---------------------------------------------------------------------------

void ClientReqSocket::sendPacket(std::unique_ptr<Packet> packet, bool controlPacket,
                                 uint32 actualPayloadSize, bool forceImmediateSend)
{
    resetTimeOutTimer();
    EMSocket::sendPacket(std::move(packet), controlPacket, actualPayloadSize, forceImmediateSend);
}

// ---------------------------------------------------------------------------
// Packet processing
// ---------------------------------------------------------------------------

bool ClientReqSocket::packetReceived(Packet* packet)
{
    resetTimeOutTimer();

    if (auto* stats = theApp.statistics)
        stats->addDownDataOverheadFileRequest(packet->size);

    const auto* data = reinterpret_cast<const uint8*>(packet->pBuffer);
    uint32 size = packet->size;
    uint8 opcode = packet->opcode;
    uint8 protocol = packet->prot;

    if (protocol == OP_EDONKEYPROT) {
        return processPacket(data, size, opcode);
    } else if (protocol == OP_EMULEPROT) {
        return processExtPacket(data, size, opcode);
    } else if (protocol == OP_PACKEDPROT) {
        // Compressed eMule packet — decompress handled by EMSocket
        return processExtPacket(data, size, opcode);
    }

    logDebug(QStringLiteral("ClientReqSocket: Unknown protocol 0x%1")
                 .arg(protocol, 2, 16, QLatin1Char('0')));
    return false;
}

bool ClientReqSocket::processPacket(const uint8* packet, uint32 size, uint8 opcode)
{
    switch (opcode) {
    case OP_HELLO:
    case OP_HELLOANSWER:
        emit helloReceived(packet, size, opcode);
        break;

    case OP_REQUESTFILENAME:
    case OP_SETREQFILEID:
    case OP_FILEREQANSNOFIL:
    case OP_REQFILENAMEANSWER:
    case OP_FILESTATUS:
    case OP_HASHSETREQUEST:
    case OP_HASHSETANSWER:
        emit fileRequestReceived(packet, size, opcode);
        break;

    case OP_STARTUPLOADREQ:
        emit uploadRequestReceived(packet, size);
        break;

    case OP_ACCEPTUPLOADREQ:
    case OP_CANCELTRANSFER:
    case OP_OUTOFPARTREQS:
    case OP_REQUESTPARTS:
    case OP_SENDINGPART:
    case OP_QUEUERANK:
    case OP_CHANGE_CLIENT_ID:
    case OP_CHANGE_SLOT:
    case OP_ASKSHAREDFILES:
    case OP_ASKSHAREDFILESANSWER:
    case OP_ASKSHAREDDIRS:
    case OP_ASKSHAREDFILESDIR:
    case OP_ASKSHAREDDIRSANS:
    case OP_ASKSHAREDFILESDIRANS:
    case OP_ASKSHAREDDENIEDANS:
    case OP_MESSAGE:
    case OP_END_OF_DOWNLOAD:
        emit packetForClient(packet, size, opcode, OP_EDONKEYPROT);
        break;

    default:
        logDebug(QStringLiteral("ClientReqSocket: Unknown ED2K opcode 0x%1, size %2")
                     .arg(opcode, 2, 16, QLatin1Char('0'))
                     .arg(size));
        break;
    }

    return true;
}

bool ClientReqSocket::processExtPacket(const uint8* packet, uint32 size, uint8 opcode)
{
    switch (opcode) {
    case OP_EMULEINFO:
    case OP_EMULEINFOANSWER:
    case OP_COMPRESSEDPART:
    case OP_COMPRESSEDPART_I64:
    case OP_SENDINGPART_I64:
    case OP_REQUESTPARTS_I64:
    case OP_QUEUERANKING:
    case OP_FILEDESC:
    case OP_REQUESTSOURCES2:
    case OP_ANSWERSOURCES2:
    case OP_PUBLICKEY:
    case OP_SIGNATURE:
    case OP_SECIDENTSTATE:
    case OP_REQUESTPREVIEW:
    case OP_PREVIEWANSWER:
    case OP_PUBLICIP_REQ:
    case OP_PUBLICIP_ANSWER:
    case OP_CALLBACK:
    case OP_REASKCALLBACKTCP:
    case OP_AICHREQUEST:
    case OP_AICHANSWER:
    case OP_BUDDYPING:
    case OP_BUDDYPONG:
    case OP_CHATCAPTCHAREQ:
    case OP_CHATCAPTCHARES:
    case OP_FWCHECKUDPREQ:
    case OP_KAD_FWTCPCHECK_ACK:
    case OP_MULTIPACKET_EXT2:
    case OP_MULTIPACKETANSWER_EXT2:
    case OP_HASHSETREQUEST2:
    case OP_HASHSETANSWER2:
        emit extPacketReceived(packet, size, opcode);
        break;

    default:
        logDebug(QStringLiteral("ClientReqSocket: Unknown eMule opcode 0x%1, size %2")
                     .arg(opcode, 2, 16, QLatin1Char('0'))
                     .arg(size));
        break;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Socket state
// ---------------------------------------------------------------------------

void ClientReqSocket::setPeerSocketState(PeerSocketState val)
{
    m_socketState = val;
}

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

void ClientReqSocket::onError(int errorCode)
{
    logDebug(QStringLiteral("ClientReqSocket error: %1").arg(errorCode));
    disconnect(QStringLiteral("Socket error %1").arg(errorCode));
}

// ---------------------------------------------------------------------------
// Socket event handlers
// ---------------------------------------------------------------------------

void ClientReqSocket::onSocketConnected()
{
    setPeerSocketState(PeerSocketState::Complete);
    resetTimeOutTimer();
}

void ClientReqSocket::onSocketDisconnected()
{
    if (!m_deleteThis)
        disconnect(QStringLiteral("Remote disconnected"));
}

void ClientReqSocket::onSocketError(QAbstractSocket::SocketError error)
{
    if (m_deleteThis)
        return;

    QString reason = QStringLiteral("Socket error: %1").arg(static_cast<int>(error));
    disconnect(reason);
}

// ---------------------------------------------------------------------------
// Debug info
// ---------------------------------------------------------------------------

QString ClientReqSocket::debugClientInfo() const
{
    QString info;
    if (peerAddress().isNull())
        info = QStringLiteral("(not connected)");
    else
        info = QStringLiteral("%1:%2").arg(peerAddress().toString()).arg(peerPort());

    if (m_client)
        info += QStringLiteral(" [client attached]");
    else
        info += QStringLiteral(" [no client]");

    return info;
}

} // namespace eMule
