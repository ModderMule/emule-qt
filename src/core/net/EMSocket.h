#pragma once

/// @file EMSocket.h
/// @brief Core eMule protocol socket with packet framing, queuing, and throttling.
///
/// Replaces MFC CEMSocket. Provides:
/// - ED2K packet framing (header parsing, reassembly across reads)
/// - Dual-priority packet queues (control + standard/file data)
/// - Integration with bandwidth throttler via ThrottledFileSocket
/// - Proxy support via QNetworkProxy

#include "net/EncryptedStreamSocket.h"
#include "net/Packet.h"
#include "net/ThrottledSocket.h"

#include <QElapsedTimer>
#include <QNetworkProxy>

#include <deque>
#include <memory>
#include <mutex>

namespace eMule {

// ---------------------------------------------------------------------------
// Proxy configuration
// ---------------------------------------------------------------------------

/// Proxy settings (replaces the ProxySettings struct used by thePrefs).
struct ProxySettings {
    bool useProxy = false;
    int type = PROXYTYPE_NOPROXY;   ///< PROXYTYPE_* from Opcodes.h.
    QString host;
    uint16 port = 0;
    bool enablePassword = false;
    QString user;
    QString password;
};

// ---------------------------------------------------------------------------
// Queue entry for standard (file data) packets
// ---------------------------------------------------------------------------

struct StandardPacketQueueEntry {
    std::unique_ptr<Packet> packet;
    uint32 actualPayloadSize = 0;
};

// ---------------------------------------------------------------------------
// Connection states
// ---------------------------------------------------------------------------

enum class EMSState : uint8 {
    Disconnected = 0xFF,
    NotConnected = 0x00,
    Connected = 0x01
};

// ---------------------------------------------------------------------------
// EMSocket
// ---------------------------------------------------------------------------

/// Core eMule protocol socket.
///
/// Provides packet framing, dual-priority queuing, and bandwidth throttling
/// on top of the encrypted stream socket layer.
///
/// Subclasses must implement packetReceived() and onError().
class EMSocket : public EncryptedStreamSocket, public ThrottledFileSocket {
    Q_OBJECT

public:
    explicit EMSocket(QObject* parent = nullptr);
    ~EMSocket() override;

    // --- Packet sending ---

    /// Queue a packet for sending. Takes ownership.
    virtual void sendPacket(std::unique_ptr<Packet> packet, bool controlPacket = true,
                            uint32 actualPayloadSize = 0, bool forceImmediateSend = false);

    // --- Connection state ---

    [[nodiscard]] bool isConnected() const { return m_conState == EMSState::Connected; }
    [[nodiscard]] EMSState getConState() const { return m_conState; }
    void setConState(EMSState val);

    /// Whether this socket is in raw data mode (for HTTP subclass).
    [[nodiscard]] virtual bool isRawDataMode() const { return false; }

    // --- Timeouts ---

    [[nodiscard]] virtual uint32 getTimeOut() const { return m_timeOut; }
    virtual void setTimeOut(uint32 timeOut) { m_timeOut = timeOut; }

    // --- Download rate limiting ---

    void setDownloadLimit(uint32 limit);
    void disableDownloadLimit();

    // --- Queue inspection ---

    void truncateQueues();

    // --- Bandwidth statistics ---

    uint64 getSentBytesCompleteFileSinceLastCallAndReset();
    uint64 getSentBytesPartFileSinceLastCallAndReset();
    uint64 getSentBytesControlPacketSinceLastCallAndReset();
    uint32 getSentPayloadSinceLastCall(bool reset);

    // --- ThrottledFileSocket interface ---

    SocketSentBytes sendControlData(uint32 maxNumberOfBytesToSend, uint32 minFragSize) override;
    SocketSentBytes sendFileAndControlData(uint32 maxNumberOfBytesToSend, uint32 minFragSize) override;
    [[nodiscard]] uint32 getLastCalledSend() const override { return m_lastCalledSend; }
    [[nodiscard]] uint32 getNeededBytes() override;
    [[nodiscard]] bool isBusyExtensiveCheck() override;
    [[nodiscard]] bool isBusyQuickCheck() const override;
    [[nodiscard]] bool isEnoughFileDataQueued(uint32 nMinFilePayloadBytes) const override;
    [[nodiscard]] bool hasQueues(bool onlyStandardPackets = false) const override;
    bool useBigSendBuffer() override;

    // --- Proxy ---

    void initProxySupport(const ProxySettings& settings);

protected:
    // --- Abstract methods (subclasses must implement) ---

    /// Process a complete incoming packet. Return false to stop processing.
    virtual bool packetReceived(Packet* packet) = 0;

    /// Handle socket errors.
    void onError(int errorCode) override = 0;

    /// Called when raw data is received in raw data mode.
    virtual void dataReceived(const uint8* data, uint32 size);

    uint32 m_timeOut = CONNECTION_TIMEOUT;
    EMSState m_conState = EMSState::NotConnected;

private:
    // --- Internal send implementation ---
    SocketSentBytes send(uint32 maxNumberOfBytesToSend, uint32 minFragSize, bool onlyControlPackets);

    static uint32 getNextFragSize(uint32 current, uint32 minFragSize);

    void clearQueues();

    // --- Slots ---
    void onReadyRead();
    void onBytesWritten(qint64 bytes);
    void onSocketError(QAbstractSocket::SocketError error);
    void onConnected();
    void onDisconnected();

    // --- Download partial packet state ---
    uint32 m_pendingPacketSize = 0;
    std::unique_ptr<Packet> m_pendingPacket;

    // Download partial header
    std::size_t m_pendingHeaderSize = 0;
    char m_pendingHeader[kPacketHeaderSize]{};

    // Download rate limiting
    bool m_downloadLimitEnable = false;
    bool m_pendingOnReceive = false;
    uint32 m_downloadLimit = 0;

    // Upload / send state
    char* m_sendBuffer = nullptr;
    uint32 m_sendBufferLen = 0;
    uint32 m_sent = 0;

    std::deque<std::unique_ptr<Packet>> m_controlQueue;
    std::deque<StandardPacketQueueEntry> m_standardQueue;
    mutable std::mutex m_sendLock;

    // Statistics
    std::atomic<uint64> m_sentBytesCompleteFile{0};
    std::atomic<uint64> m_sentBytesPartFile{0};
    std::atomic<uint64> m_sentBytesControlPacket{0};
    uint32 m_lastCalledSend = 0;
    uint32 m_lastSent = 0;
    uint32 m_lastFinishedStandard = 0;
    std::atomic<uint32> m_actualPayloadSizeSent{0};
    uint32 m_actualPayloadSize = 0;
    bool m_currentPacketIsControl = false;
    bool m_currentPackageIsFromPartFile = false;
    bool m_accelerateUpload = false;
    bool m_busy = false;
    bool m_hasSent = false;
    bool m_useBigSendBuffers = false;

    // Read buffer
    std::vector<char> m_readBuffer;

    QElapsedTimer m_elapsedTimer;
};

} // namespace eMule
