/// @file EMSocket.cpp
/// @brief Core eMule protocol socket implementation.

#include "net/EMSocket.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"
#include "utils/Log.h"

#include <QTimer>

#include <algorithm>
#include <cstring>
#include <mutex>

#include <sys/socket.h>

namespace eMule {

namespace {

/// Maximum read buffer size (~2MB, matching original).
constexpr std::size_t kMaxReadBuffer = 2'000'000;

} // anonymous namespace

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

EMSocket::EMSocket(QObject* parent)
    : EncryptedStreamSocket(parent)
    , m_readBuffer(kMaxReadBuffer)
{
    m_elapsedTimer.start();
    m_lastCalledSend = static_cast<uint32>(m_elapsedTimer.elapsed());
    m_lastSent = m_lastCalledSend > SEC2MS(1) ? m_lastCalledSend - SEC2MS(1) : 0;

    connect(this, &QTcpSocket::readyRead, this, &EMSocket::onReadyRead);
    connect(this, &QTcpSocket::connected, this, &EMSocket::onConnected);
    connect(this, &QTcpSocket::disconnected, this, &EMSocket::onDisconnected);
    connect(this, &QTcpSocket::bytesWritten, this, &EMSocket::onBytesWritten);
    connect(this, &QAbstractSocket::errorOccurred, this, &EMSocket::onSocketError);
}

EMSocket::~EMSocket()
{
    // Disconnect all signals BEFORE destroying m_sendLock.
    // QAbstractSocket::~QAbstractSocket() calls disconnectFromHost() which emits
    // the 'disconnected' signal. Our onDisconnected() slot locks m_sendLock via
    // setConState(). If the mutex is already destroyed, this crashes with EINVAL.
    disconnect(this, nullptr, this, nullptr);

    {
        std::lock_guard lock(m_sendLock);
        m_conState = EMSState::Disconnected;
    }
    clearQueues();
}

// ---------------------------------------------------------------------------
// Connection state
// ---------------------------------------------------------------------------

void EMSocket::setConState(EMSState val)
{
    std::lock_guard lock(m_sendLock);
    m_conState = val;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void EMSocket::onConnected()
{
    logDebug(QStringLiteral("EMSocket::onConnected — peer=%1:%2 fd=%3")
                 .arg(peerAddress().toString()).arg(peerPort()).arg(socketDescriptor()));
    m_conState = EMSState::Connected;
    onSocketConnected(); // trigger encryption handshake if needed
}

void EMSocket::onDisconnected()
{
    setConState(EMSState::Disconnected);
    clearQueues();
}

void EMSocket::onSocketError(QAbstractSocket::SocketError /*error*/)
{
    logDebug(QStringLiteral("EMSocket::onSocketError — error=%1 (%2) cryptState=%3 bytesAvail=%4 peer=%5:%6")
                 .arg(static_cast<int>(error())).arg(errorString())
                 .arg(static_cast<int>(m_streamCryptState))
                 .arg(bytesAvailable())
                 .arg(peerAddress().toString()).arg(peerPort()));
    // MFC's OnClose reads remaining buffered data before closing.
    // When the remote closes (RemoteHostClosedError), there may still be
    // data in Qt's read buffer that we need to process first.
    if (error() == QAbstractSocket::RemoteHostClosedError) {
        qint64 avail = bytesAvailable();
        if (avail > 0) {
            logDebug(QStringLiteral("EMSocket::onSocketError — RemoteHostClosedError with %1 bytes still available, processing")
                         .arg(avail));
            onReadyRead();
        }
    }
    onError(static_cast<int>(error()));
}

void EMSocket::onBytesWritten(qint64 /*bytes*/)
{
    m_busy = false;
    // If we have more queued data, the throttler will call sendControlData/sendFileAndControlData
}

// ---------------------------------------------------------------------------
// Packet framing — OnReceive equivalent
// ---------------------------------------------------------------------------

void EMSocket::onReadyRead()
{
    if (m_conState == EMSState::Disconnected)
        return;

    m_conState = EMSState::Connected;

    // Check download rate limit
    if (m_downloadLimitEnable && m_downloadLimit == 0) {
        m_pendingOnReceive = true;
        return;
    }

    // Calculate max bytes to read
    std::size_t readMax = m_readBuffer.size() - m_pendingHeaderSize;
    if (m_downloadLimitEnable && readMax > m_downloadLimit)
        readMax = m_downloadLimit;

    // Read from the encrypted stream
    qint64 bytesAvailable = this->bytesAvailable();
    if (bytesAvailable <= 0)
        return;

    if (thePrefs.logRawSocketPackets())
        logDebug(QStringLiteral("EMSocket::onReadyRead — %1 bytes available from %2:%3 cryptState=%4")
                     .arg(bytesAvailable).arg(peerAddress().toString()).arg(peerPort())
                     .arg(static_cast<int>(m_streamCryptState)));

    qint64 toRead = std::min(static_cast<qint64>(readMax), bytesAvailable);
    qint64 ret = read(m_readBuffer.data() + m_pendingHeaderSize, toRead);
    if (ret <= 0 || m_conState == EMSState::Disconnected)
        return;

    // Process through encryption layer
    bool wasReady = isEncryptionLayerReady();
    int decryptedLen = processReceivedData(m_readBuffer.data() + m_pendingHeaderSize, static_cast<int>(ret));

    // If encryption layer just became ready, flush any queued control packets.
    // This handles the case where sendPacket() queued packets during connection
    // setup but the QTimer fired before the encryption handshake completed.
    if (!wasReady && isEncryptionLayerReady()) {
        if (thePrefs.logRawSocketPackets())
            logDebug(QStringLiteral("EMSocket::onReadyRead — encryption just became ready for %1:%2")
                         .arg(peerAddress().toString()).arg(peerPort()));
        std::lock_guard lock(m_sendLock);
        bool hasQueued = !m_controlQueue.empty() || m_sendBuffer != nullptr;
        if (thePrefs.logRawSocketPackets())
            logDebug(QStringLiteral("EMSocket::onReadyRead — controlQueue=%1 sendBuffer=%2")
                         .arg(m_controlQueue.size()).arg(m_sendBuffer != nullptr));
        if (hasQueued) {
            QTimer::singleShot(0, this, [this] {
                if (m_conState == EMSState::Connected && isEncryptionLayerReady()) {
                    auto result = send(1024 * 64, 0, true);
                    if (thePrefs.logRawSocketPackets())
                        logDebug(QStringLiteral("EMSocket::onReadyRead — post-enc flush: ctrl=%1 std=%2 success=%3 peer=%4:%5")
                                     .arg(result.sentBytesControlPackets).arg(result.sentBytesStandardPackets)
                                     .arg(result.success).arg(peerAddress().toString()).arg(peerPort()));
                }
            });
        }
    } else if (!wasReady && !isEncryptionLayerReady()) {
        if (thePrefs.logRawSocketPackets())
            logDebug(QStringLiteral("EMSocket::onReadyRead — encryption NOT ready yet after processReceivedData for %1:%2")
                         .arg(peerAddress().toString()).arg(peerPort()));
    }

    // Update download rate limit
    if (m_downloadLimitEnable)
        m_downloadLimit -= static_cast<uint32>(getRealReceivedBytes());

    m_pendingOnReceive = m_fullReceive;

    if (decryptedLen == 0)
        return;

    ret = decryptedLen;

    // Prepend any partial header from previous read
    if (m_pendingHeaderSize > 0) {
        std::memmove(m_readBuffer.data() + m_pendingHeaderSize - m_pendingHeaderSize,
                     m_pendingHeader, m_pendingHeaderSize);
        // Actually copy the pending header to the front
        std::memcpy(m_readBuffer.data(), m_pendingHeader, m_pendingHeaderSize);
        ret += static_cast<qint64>(m_pendingHeaderSize);
        m_pendingHeaderSize = 0;
    }

    if (isRawDataMode()) {
        dataReceived(reinterpret_cast<const uint8*>(m_readBuffer.data()), static_cast<uint32>(ret));
        return;
    }

    // Parse packets from the buffer
    const char* rptr = m_readBuffer.data();
    const char* rend = m_readBuffer.data() + ret;

    while (rend >= rptr + static_cast<ptrdiff_t>(kPacketHeaderSize)
           || (m_pendingPacket && rend > rptr)) {

        if (!m_pendingPacket) {
            // Check protocol byte
            const auto* hdr = reinterpret_cast<const HeaderStruct*>(rptr);
            switch (hdr->eDonkeyID) {
            case OP_EDONKEYPROT:
            case OP_PACKEDPROT:
            case OP_EMULEPROT:
                break;
            default: {
                // Debug: dump first bytes for diagnosis
                int dumpLen = std::min(static_cast<int>(rend - rptr), 32);
                QString hex;
                for (int i = 0; i < dumpLen; ++i)
                    hex += QStringLiteral("%1 ").arg(static_cast<uint8>(rptr[i]), 2, 16, QLatin1Char('0'));
                logWarning(QStringLiteral("EMSocket: kErrWrongHeader — first %1 bytes: %2 (peer %3:%4)")
                               .arg(dumpLen).arg(hex)
                               .arg(peerAddress().toString()).arg(peerPort()));
                onError(kErrWrongHeader);
                return;
            }
            }

            // Check for oversized packets (2MB limit)
            if (hdr->packetLength - 1 > kMaxReadBuffer) {
                onError(kErrTooBig);
                return;
            }

            m_pendingPacket = std::make_unique<Packet>(const_cast<char*>(rptr));
            rptr += kPacketHeaderSize;
            m_pendingPacket->pBuffer = new char[m_pendingPacket->size + 1];
            m_pendingPacketSize = 0;
        }

        // Copy available data into the pending packet
        uint32 toCopy = std::min(m_pendingPacket->size - m_pendingPacketSize,
                                 static_cast<uint32>(rend - rptr));
        std::memcpy(&m_pendingPacket->pBuffer[m_pendingPacketSize], rptr, toCopy);
        m_pendingPacketSize += toCopy;
        rptr += toCopy;

        // Check if packet is complete
        if (m_pendingPacket->size == m_pendingPacketSize) {
            bool result = packetReceived(m_pendingPacket.get());
            m_pendingPacket.reset();
            m_pendingPacketSize = 0;

            if (!result)
                return;
        }
    }

    // Save any leftover bytes (partial header) for next read
    if (rptr < rend) {
        m_pendingHeaderSize = static_cast<std::size_t>(rend - rptr);
        std::memcpy(m_pendingHeader, rptr, m_pendingHeaderSize);
    }
}

// ---------------------------------------------------------------------------
// Download rate limiting
// ---------------------------------------------------------------------------

void EMSocket::setDownloadLimit(uint32 limit)
{
    m_downloadLimit = limit;
    m_downloadLimitEnable = true;

    if (limit > 0 && m_pendingOnReceive)
        onReadyRead();
}

void EMSocket::disableDownloadLimit()
{
    m_downloadLimitEnable = false;
    if (m_pendingOnReceive)
        onReadyRead();
}

// ---------------------------------------------------------------------------
// Packet sending
// ---------------------------------------------------------------------------

void EMSocket::sendPacket(std::unique_ptr<Packet> packet, bool controlPacket,
                          uint32 actualPayloadSize, bool forceImmediateSend)
{
    if (m_conState == EMSState::Disconnected) {
        if (thePrefs.logRawSocketPackets())
            logDebug(QStringLiteral("EMSocket::sendPacket — DROPPED (disconnected) opcode=0x%1")
                         .arg(packet ? packet->opcode : 0, 2, 16, QLatin1Char('0')));
        return;
    }

    uint8 opcode = packet ? packet->opcode : 0;
    uint32 pktSize = packet ? packet->size : 0;
    if (thePrefs.logRawSocketPackets())
        logDebug(QStringLiteral("EMSocket::sendPacket — opcode=0x%1 size=%2 ctrl=%3 force=%4 conState=%5 peer=%6:%7")
                     .arg(opcode, 2, 16, QLatin1Char('0')).arg(pktSize).arg(controlPacket)
                     .arg(forceImmediateSend).arg(static_cast<int>(m_conState))
                     .arg(peerAddress().toString()).arg(peerPort()));

    {
        std::lock_guard lock(m_sendLock);
        if (controlPacket) {
            m_controlQueue.push_back(std::move(packet));
        } else {
            bool first = (m_sendBuffer == nullptr || m_currentPacketIsControl) && m_standardQueue.empty();
            m_standardQueue.push_back({std::move(packet), actualPayloadSize});
            if (first) {
                m_lastFinishedStandard = static_cast<uint32>(m_elapsedTimer.elapsed());
                m_accelerateUpload = true;
            }
        }
    }

    if (forceImmediateSend) {
        if (thePrefs.logRawSocketPackets())
            logDebug(QStringLiteral("EMSocket::sendPacket — forceImmediateSend, calling send() now"));
        send(1024, 0, true);
    } else if (controlPacket) {
        // Schedule send on the socket's thread. Unlike MFC Winsock which is thread-safe,
        // Qt sockets require write() to be called from the thread that created the socket.
        // Using QTimer::singleShot(0) defers to the next event loop iteration on the correct thread.
        QTimer::singleShot(0, this, [this] {
            if (thePrefs.logRawSocketPackets())
                logDebug(QStringLiteral("EMSocket::sendPacket — QTimer fired, conState=%1 encReady=%2 peer=%3:%4")
                             .arg(static_cast<int>(m_conState)).arg(isEncryptionLayerReady())
                             .arg(peerAddress().toString()).arg(peerPort()));
            if (m_conState == EMSState::Connected) {
                auto result = send(1024 * 64, 0, true);
                if (thePrefs.logRawSocketPackets())
                    logDebug(QStringLiteral("EMSocket::sendPacket — send() returned ctrl=%1 std=%2 success=%3")
                                 .arg(result.sentBytesControlPackets).arg(result.sentBytesStandardPackets)
                                 .arg(result.success));
            }
        });
    }
}

// ---------------------------------------------------------------------------
// ThrottledFileSocket implementation
// ---------------------------------------------------------------------------

SocketSentBytes EMSocket::sendControlData(uint32 maxNumberOfBytesToSend, uint32 minFragSize)
{
    return send(maxNumberOfBytesToSend, minFragSize, true);
}

SocketSentBytes EMSocket::sendFileAndControlData(uint32 maxNumberOfBytesToSend, uint32 minFragSize)
{
    return send(maxNumberOfBytesToSend, minFragSize, false);
}

// ---------------------------------------------------------------------------
// Send implementation
// ---------------------------------------------------------------------------

SocketSentBytes EMSocket::send(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyControlPackets)
{
    SocketSentBytes ret{0, 0, true};

    std::lock_guard lock(m_sendLock);
    if (m_conState != EMSState::Connected || !isEncryptionLayerReady())
        return ret;

    if (minFragSize < 1)
        minFragSize = 1;

    maxNumberOfBytesToSend = getNextFragSize(maxNumberOfBytesToSend, minFragSize);
    m_lastCalledSend = static_cast<uint32>(m_elapsedTimer.elapsed());
    bool wasLongTimeSinceSend = (m_lastCalledSend >= m_lastSent + SEC2MS(1));

    uint32 sentBytes = 0;
    while (sentBytes < maxNumberOfBytesToSend
        && ret.success
        && (m_sendBuffer != nullptr || !m_controlQueue.empty() || (!m_standardQueue.empty() && !onlyControlPackets))
        && (!onlyControlPackets
            || (m_sendBuffer != nullptr && m_currentPacketIsControl)
            || (sentBytes > 0 && sentBytes % minFragSize != 0)
            || (m_sendBuffer == nullptr && !m_controlQueue.empty())
            || (m_sendBuffer != nullptr && !m_currentPacketIsControl && wasLongTimeSinceSend && !m_controlQueue.empty() && sentBytes < minFragSize)))
    {
        // Get the next packet to send if needed
        if (m_sendBuffer == nullptr) {
            Packet* curPacket = nullptr;
            m_currentPacketIsControl = !m_controlQueue.empty();

            if (m_currentPacketIsControl) {
                curPacket = m_controlQueue.front().release();
                m_controlQueue.pop_front();
            } else {
                if (m_standardQueue.empty()) {
                    return ret;
                }
                auto& entry = m_standardQueue.front();
                curPacket = entry.packet.release();
                m_actualPayloadSize = entry.actualPayloadSize;
                m_currentPackageIsFromPartFile = curPacket->isFromPF();
                m_standardQueue.pop_front();
            }

            m_sendBufferLen = curPacket->getRealPacketSize();
            m_sendBuffer = curPacket->detachPacket();
            m_sent = 0;
            delete curPacket;

            // Encrypt the data
            cryptPrepareSendData(reinterpret_cast<uint8*>(m_sendBuffer), m_sendBufferLen);

            // Debug: dump first 16 bytes AFTER encryption
            if (thePrefs.logRawSocketPackets()) {
                int dumpLen = std::min(static_cast<int>(m_sendBufferLen), 16);
                QString hex;
                for (int i = 0; i < dumpLen; ++i)
                    hex += QStringLiteral("%1 ").arg(static_cast<uint8>(m_sendBuffer[i]), 2, 16, QLatin1Char('0'));
                logDebug(QStringLiteral("EMSocket::send — ENCRYPTED first %1 bytes: [%2] cryptState=%3 peer=%4:%5")
                             .arg(dumpLen).arg(hex.trimmed())
                             .arg(static_cast<int>(m_streamCryptState))
                             .arg(peerAddress().toString()).arg(peerPort()));
            }
        }

        // Send as much as allowed
        while (m_sent < m_sendBufferLen
            && sentBytes < maxNumberOfBytesToSend
            && (!onlyControlPackets || m_currentPacketIsControl
                || (wasLongTimeSinceSend && sentBytes < minFragSize)
                || sentBytes % minFragSize != 0)
            && ret.success)
        {
            uint32 toSend = m_sendBufferLen - m_sent;
            if (!onlyControlPackets || m_currentPacketIsControl) {
                if (toSend > maxNumberOfBytesToSend - sentBytes)
                    toSend = maxNumberOfBytesToSend - sentBytes;
            } else if (wasLongTimeSinceSend && minFragSize > sentBytes) {
                if (toSend > minFragSize - sentBytes)
                    toSend = minFragSize - sentBytes;
            } else {
                uint32 nextFrag = getNextFragSize(sentBytes, minFragSize);
                if (nextFrag >= sentBytes && toSend > nextFrag - sentBytes)
                    toSend = nextFrag - sentBytes;
            }

            m_lastSent = static_cast<uint32>(m_elapsedTimer.elapsed());

            qint64 result = write(m_sendBuffer + m_sent, toSend);
            if (result < 0) {
                m_busy = true;
                return ret;
            }

            m_busy = false;
            m_hasSent = true;
            auto written = static_cast<uint32>(result);

            m_sent += written;
            sentBytes += written;

            if (!m_currentPacketIsControl) {
                ret.sentBytesStandardPackets += written;
                if (m_currentPackageIsFromPartFile)
                    m_sentBytesPartFile.fetch_add(written, std::memory_order_relaxed);
                else
                    m_sentBytesCompleteFile.fetch_add(written, std::memory_order_relaxed);
            } else {
                ret.sentBytesControlPackets += written;
                m_sentBytesControlPacket.fetch_add(written, std::memory_order_relaxed);
            }
        }

        if (m_sent == m_sendBufferLen) {
            delete[] m_sendBuffer;
            m_sendBuffer = nullptr;
            m_sendBufferLen = 0;

            if (!m_currentPacketIsControl) {
                m_actualPayloadSizeSent.fetch_add(m_actualPayloadSize, std::memory_order_relaxed);
                m_actualPayloadSize = 0;
                m_lastFinishedStandard = static_cast<uint32>(m_elapsedTimer.elapsed());
                m_accelerateUpload = false;
            }
            m_sent = 0;
        }
    }

    return ret;
}

uint32 EMSocket::getNextFragSize(uint32 current, uint32 minFragSize)
{
    return (std::min(static_cast<uint32>(INT32_MAX), current + minFragSize - 1) / minFragSize) * minFragSize;
}

// ---------------------------------------------------------------------------
// Queue management
// ---------------------------------------------------------------------------

void EMSocket::clearQueues()
{
    {
        std::lock_guard lock(m_sendLock);
        m_controlQueue.clear();
        m_standardQueue.clear();
    }

    m_downloadLimit = 0;
    m_downloadLimitEnable = false;
    m_pendingOnReceive = false;
    m_pendingHeaderSize = 0;
    m_pendingPacket.reset();
    m_pendingPacketSize = 0;

    delete[] m_sendBuffer;
    m_sendBuffer = nullptr;
    m_sendBufferLen = 0;
    m_sent = 0;
}

void EMSocket::truncateQueues()
{
    std::lock_guard lock(m_sendLock);
    m_standardQueue.clear();
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

uint64 EMSocket::getSentBytesCompleteFileSinceLastCallAndReset()
{
    return m_sentBytesCompleteFile.exchange(0, std::memory_order_relaxed);
}

uint64 EMSocket::getSentBytesPartFileSinceLastCallAndReset()
{
    return m_sentBytesPartFile.exchange(0, std::memory_order_relaxed);
}

uint64 EMSocket::getSentBytesControlPacketSinceLastCallAndReset()
{
    return m_sentBytesControlPacket.exchange(0, std::memory_order_relaxed);
}

uint32 EMSocket::getSentPayloadSinceLastCall(bool reset)
{
    if (reset)
        return m_actualPayloadSizeSent.exchange(0, std::memory_order_relaxed);
    return m_actualPayloadSizeSent.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// GetNeededBytes
// ---------------------------------------------------------------------------

uint32 EMSocket::getNeededBytes()
{
    std::lock_guard lock(m_sendLock);
    if (m_conState == EMSState::Disconnected)
        return 0;

    bool isControl = (m_sendBuffer == nullptr) || m_currentPacketIsControl;
    if (isControl && m_standardQueue.empty())
        return 0;

    if (!isControl && !m_controlQueue.empty())
        m_accelerateUpload = true;

    uint32 now = static_cast<uint32>(m_elapsedTimer.elapsed());
    uint32 timeSinceLastFinished = now - m_lastFinishedStandard;
    uint32 timeSinceLastSend = now - m_lastCalledSend;
    uint32 timeTotal = SEC2MS(m_accelerateUpload ? 45 : 90);
    uint64 sizeLeft, sizeTotal;

    if (!isControl) {
        sizeLeft = m_sendBufferLen - m_sent;
        sizeTotal = m_sendBufferLen;
    } else {
        sizeLeft = sizeTotal = m_standardQueue.front().packet->getRealPacketSize();
    }

    if (timeSinceLastFinished >= timeTotal)
        return static_cast<uint32>(sizeLeft);

    uint32 timeLeft = timeTotal - timeSinceLastFinished;
    if (static_cast<uint64>(timeLeft) * sizeTotal >= static_cast<uint64>(timeTotal) * sizeLeft)
        return (timeSinceLastSend >= SEC2MS(20)) ? 1 : 0;

    uint64 decval = static_cast<uint64>(timeLeft) * sizeTotal / timeTotal;
    if (decval == 0)
        return static_cast<uint32>(sizeLeft);
    if (decval < sizeLeft)
        return static_cast<uint32>(sizeLeft - decval + 1);
    return 1;
}

// ---------------------------------------------------------------------------
// Busy checks
// ---------------------------------------------------------------------------

bool EMSocket::isBusyExtensiveCheck()
{
    return m_busy;
}

bool EMSocket::isBusyQuickCheck() const
{
    return m_busy;
}

bool EMSocket::hasQueues(bool onlyStandardPackets) const
{
    return m_sendBuffer != nullptr
        || !m_standardQueue.empty()
        || (!m_controlQueue.empty() && !onlyStandardPackets);
}

bool EMSocket::isEnoughFileDataQueued(uint32 nMinFilePayloadBytes) const
{
    for (const auto& entry : m_standardQueue) {
        if (entry.actualPayloadSize > nMinFilePayloadBytes)
            return true;
        nMinFilePayloadBytes -= entry.actualPayloadSize;
    }
    return false;
}

bool EMSocket::useBigSendBuffer()
{
    if (!m_useBigSendBuffers) {
        // Try to increase the send buffer
        auto fd = socketDescriptor();
        if (fd != -1) {
            constexpr int bigSize = 128 * 1024;
            int optval = bigSize;
            if (setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_SNDBUF, &optval, sizeof(optval)) == 0)
                m_useBigSendBuffers = true;
        }
    }
    return m_useBigSendBuffers;
}

// ---------------------------------------------------------------------------
// Proxy support
// ---------------------------------------------------------------------------

void EMSocket::initProxySupport(const ProxySettings& settings)
{
    if (!settings.useProxy || settings.type == PROXYTYPE_NOPROXY)
        return;

    QNetworkProxy proxy;
    switch (settings.type) {
    case PROXYTYPE_SOCKS4:
    case PROXYTYPE_SOCKS4A:
    case PROXYTYPE_SOCKS5:
        proxy.setType(QNetworkProxy::Socks5Proxy);
        break;
    case PROXYTYPE_HTTP10:
    case PROXYTYPE_HTTP11:
        proxy.setType(QNetworkProxy::HttpProxy);
        break;
    default:
        return;
    }

    proxy.setHostName(settings.host);
    proxy.setPort(settings.port);
    if (settings.enablePassword) {
        proxy.setUser(settings.user);
        proxy.setPassword(settings.password);
    }
    setProxy(proxy);
}

// ---------------------------------------------------------------------------
// Raw data mode default
// ---------------------------------------------------------------------------

void EMSocket::dataReceived(const uint8* /*data*/, uint32 /*size*/)
{
    // Default: do nothing. Subclasses override for HTTP mode.
}

} // namespace eMule
