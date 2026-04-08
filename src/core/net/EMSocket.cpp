#include "pch.h"
/// @file EMSocket.cpp
/// @brief Core eMule protocol socket implementation.

#include "net/EMSocket.h"
#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "utils/Log.h"

#include <QTimer>


#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <cerrno>
#endif

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

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
    // Remove from bandwidth throttler before destruction to prevent dangling
    // pointer access from the throttler thread (SIGSEGV in runInternal).
    if (auto* throttler = theApp.uploadBandwidthThrottler)
        throttler->removeFromAllQueues(this);

    // Disconnect all signals BEFORE destroying m_sendLock.
    // QAbstractSocket::~QAbstractSocket() calls disconnectFromHost() which emits
    // the 'disconnected' signal. Our onDisconnected() slot locks m_sendLock via
    // setConState(). If the mutex is already destroyed, this crashes with EINVAL.
    disconnect(this, nullptr, this, nullptr);

    {
        std::lock_guard lock(m_sendLock);
        m_conState.store(EMSState::Disconnected, std::memory_order_release);
    }
    clearQueues();
}

// ---------------------------------------------------------------------------
// Connection state
// ---------------------------------------------------------------------------

void EMSocket::setConState(EMSState val)
{
    std::lock_guard lock(m_sendLock);
    m_conState.store(val, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void EMSocket::onConnected()
{
    logDebug(QStringLiteral("EMSocket::onConnected — peer=%1:%2 fd=%3")
                 .arg(peerAddress().toString()).arg(peerPort()).arg(socketDescriptor()));
    m_conState.store(EMSState::Connected, std::memory_order_release);
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
    bool wasBusy = m_busy;
    m_cachedBytesToWrite.store(bytesToWrite(), std::memory_order_relaxed);
    m_busy = (m_cachedBytesToWrite.load(std::memory_order_relaxed) >= kBusyThreshold);

    // Wake the throttler when transitioning from busy to available
    if (wasBusy && !m_busy && theApp.uploadBandwidthThrottler)
        theApp.uploadBandwidthThrottler->socketAvailable();
}

// ---------------------------------------------------------------------------
// Packet framing — OnReceive equivalent
// ---------------------------------------------------------------------------

void EMSocket::onReadyRead()
{
    if (m_conState.load(std::memory_order_acquire) == EMSState::Disconnected)
        return;

    m_conState.store(EMSState::Connected, std::memory_order_release);

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
    if (ret <= 0 || m_conState.load(std::memory_order_acquire) == EMSState::Disconnected)
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
                if (m_conState.load(std::memory_order_acquire) == EMSState::Connected && isEncryptionLayerReady()) {
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

    try {
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
    } catch (const std::exception& ex) {
        logWarning(QStringLiteral("EMSocket::onReadyRead — exception in packet processing: %1 (peer %2:%3)")
                       .arg(QLatin1String(ex.what()))
                       .arg(peerAddress().toString()).arg(peerPort()));
        m_pendingPacket.reset();
        m_pendingPacketSize = 0;
        onError(kErrWrongHeader);
        return;
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
    if (m_conState.load(std::memory_order_acquire) == EMSState::Disconnected) {
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
                     .arg(forceImmediateSend).arg(static_cast<int>(m_conState.load(std::memory_order_relaxed)))
                     .arg(peerAddress().toString()).arg(peerPort()));

    bool wakeThrottler = false;
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
            wakeThrottler = true;
        }
    }

    // Wake the throttler AFTER releasing m_sendLock to avoid contention
    // with the throttler's send() which also acquires m_sendLock.
    if (wakeThrottler && theApp.uploadBandwidthThrottler)
        theApp.uploadBandwidthThrottler->newUploadDataAvailable();

    if (forceImmediateSend) {
        if (thePrefs.logRawSocketPackets())
            logDebug(QStringLiteral("EMSocket::sendPacket — forceImmediateSend, calling send() now"));
        send(1024, 0, true);
        scheduleRetryIfNeeded();
    } else if (controlPacket) {
        // Schedule send on the socket's thread via QTimer.
        QTimer::singleShot(0, this, [this] {
            if (m_conState.load(std::memory_order_acquire) == EMSState::Connected) {
                send(1024 * 64, 0, true);
                scheduleRetryIfNeeded();
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
    if (m_conState.load(std::memory_order_relaxed) != EMSState::Connected || !isEncryptionLayerReady())
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
            // Drain a partially-sent standard packet when control packets
            // are waiting — otherwise control packets are blocked behind
            // the send buffer until wasLongTimeSinceSend (>1s).
            || (m_sendBuffer != nullptr && !m_currentPacketIsControl && !m_controlQueue.empty())))
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

            // Flush DH delayed response before first payload write
            flushPendingNegotiationData();

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

        // Must drain a standard send buffer blocking queued control packets?
        const bool drainForControl = (onlyControlPackets && m_sendBuffer != nullptr
                                      && !m_currentPacketIsControl && !m_controlQueue.empty());

        // Send as much as allowed
        while (m_sent < m_sendBufferLen
            && sentBytes < maxNumberOfBytesToSend
            && (!onlyControlPackets || m_currentPacketIsControl
                || drainForControl
                || (wasLongTimeSinceSend && sentBytes < minFragSize)
                || sentBytes % minFragSize != 0)
            && ret.success)
        {
            uint32 toSend = m_sendBufferLen - m_sent;
            if (!onlyControlPackets || m_currentPacketIsControl) {
                if (toSend > maxNumberOfBytesToSend - sentBytes)
                    toSend = maxNumberOfBytesToSend - sentBytes;
            } else if (drainForControl) {
                // Drain the blocking standard packet so control packets can follow
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

            // Use native ::send() when called from the throttler's background
            // thread (Qt's write() is not thread-safe from non-owner threads).
            // Use Qt's write() when called from the socket's owning thread to
            // preserve Qt's event notification system (readyRead on the peer).
            qint64 result;
            if (QThread::currentThread() != thread()) {
                // Background thread (throttler) — bypass Qt's write buffer
                auto fd = socketDescriptor();
                if (fd == -1) {
                    m_busy = true;
                    return ret;
                }
#ifdef Q_OS_WIN
                result = ::send(static_cast<SOCKET>(fd), m_sendBuffer + m_sent, static_cast<int>(toSend), 0);
#else
                result = ::send(static_cast<int>(fd), m_sendBuffer + m_sent, toSend, MSG_NOSIGNAL);
#endif
                if (result <= 0) {
#ifdef Q_OS_WIN
                    int err = WSAGetLastError();
                    if (err == WSAEWOULDBLOCK) {
#else
                    int err = errno;
                    if (err == EAGAIN || err == EWOULDBLOCK) {
#endif
                        return ret;
                    }
                    ret.success = false;
                    return ret;
                }
            } else {
                // Owner thread — use Qt's write() for proper event handling
                result = write(m_sendBuffer + m_sent, toSend);
                if (result <= 0) {
                    m_busy = true;
                    return ret;
                }
            }

            // Only call Qt's bytesToWrite() from the owning thread — it's not
            // thread-safe and accessing it from the throttler thread can corrupt
            // Qt's internal socket state (breaking QSocketNotifier events).
            if (QThread::currentThread() == thread()) {
                m_cachedBytesToWrite.store(bytesToWrite(), std::memory_order_relaxed);
                m_busy = (m_cachedBytesToWrite.load(std::memory_order_relaxed) >= kBusyThreshold);
            }
            auto written = static_cast<uint32>(result);

            m_sent += written;
            sentBytes += written;
            onSendProgress();

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

// ---------------------------------------------------------------------------
// scheduleRetryIfNeeded — retry sending when ::send() hit EAGAIN
//
// With native ::send() we bypass Qt's write buffer and write notifier.
// When the kernel socket buffer is full, ::send() returns EAGAIN and the
// data stays in m_sendBuffer.  Without this retry, that data is stuck
// forever — especially control packets sent via QTimer::singleShot which
// fire only once.  This replaces Qt's automatic write-notifier retry.
// ---------------------------------------------------------------------------

void EMSocket::scheduleRetryIfNeeded()
{
    if (m_retryScheduled || m_conState.load(std::memory_order_acquire) != EMSState::Connected)
        return;

    bool hasPending;
    {
        std::lock_guard lock(m_sendLock);
        hasPending = (m_sendBuffer != nullptr) || !m_controlQueue.empty();
    }

    if (hasPending) {
        m_retryScheduled = true;
        QTimer::singleShot(10, this, [this] {
            m_retryScheduled = false;
            if (m_conState.load(std::memory_order_acquire) == EMSState::Connected) {
                // Only retry control packets — standard (file data) packets
                // are sent by the UploadBandwidthThrottler on its own thread.
                send(1024 * 64, 0, true);
                scheduleRetryIfNeeded();
            }
        });
    }
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
    if (m_conState.load(std::memory_order_relaxed) == EMSState::Disconnected)
        return 0;

    bool isControl = (m_sendBuffer == nullptr) || m_currentPacketIsControl;
    if (isControl && m_standardQueue.empty())
        return 0;

    if (!isControl && !m_controlQueue.empty())
        m_accelerateUpload = true;

    uint32 now = static_cast<uint32>(m_elapsedTimer.elapsed());
    uint32 timeSinceLastFinished = now - m_lastFinishedStandard;
    uint32 timeSinceLastSend = now - m_lastCalledSend;
    uint32 timeTotal = SEC2MS(m_accelerateUpload ? 3 : 5);
    uint64 sizeLeft, sizeTotal;

    if (!isControl) {
        sizeLeft = m_sendBufferLen - m_sent;
        sizeTotal = m_sendBufferLen;
    } else {
        const auto& front = m_standardQueue.front();
        if (!front.packet)
            return 0;
        sizeLeft = sizeTotal = front.packet->getRealPacketSize();
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
            constexpr int bigSize = 1024 * 1024;
            int optval = bigSize;
            if (setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_SNDBUF,
                           reinterpret_cast<const char*>(&optval), sizeof(optval)) == 0)
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
