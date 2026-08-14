#pragma once

/// @file UploadQueue.h
/// @brief Upload queue manager — port of MFC CUploadQueue.
///
/// Manages upload slot allocation, waiting queue, data rate tracking,
/// and coordination between clients, throttler, and disk IO thread.

#include "net/Address.h"
#include "utils/Types.h"

#include <QList>
#include <QMutex>
#include <QObject>

#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace eMule {

class Packet;
class SharedFileList;
class UpDownClient;
class UploadBandwidthThrottler;
class UploadDiskIOThread;

class UploadQueue : public QObject {
    Q_OBJECT
public:
    explicit UploadQueue(QObject* parent = nullptr);
    ~UploadQueue() override;

    UploadQueue(const UploadQueue&) = delete;
    UploadQueue& operator=(const UploadQueue&) = delete;

    // Lifecycle
    void process();

    // Queue management
    bool addClientToQueue(UpDownClient* client, bool ignoreTimeLimit = false);
    bool removeFromUploadQueue(UpDownClient* client);
    bool removeFromWaitingQueue(UpDownClient* client);

    // Query
    [[nodiscard]] bool isOnUploadQueue(const UpDownClient* client) const;
    [[nodiscard]] bool isDownloading(const UpDownClient* client) const;
    [[nodiscard]] int waitingUserCount() const;
    [[nodiscard]] int uploadQueueLength() const;
    [[nodiscard]] int waitingPosition(const UpDownClient* client) const;
    [[nodiscard]] UpDownClient* waitingClientByIP(uint32 ip) const;

    // Data rates
    void updateDatarates();
    [[nodiscard]] uint32 datarate() const { return m_datarate; }
    [[nodiscard]] bool hasActiveUploads() const { return !m_uploadingList.empty(); }
    [[nodiscard]] uint32 friendDatarate() const { return m_friendDatarate; }

    /// Slots that were fully active over the last short window — MFC's
    /// GetActiveUploadsCount() (srchybrid/UploadQueue.h:63), which is what the
    /// statistics "Active uploads" series plots. Distinct from uploadQueueLength(),
    /// the current size of the uploading list.
    [[nodiscard]] int maxActiveClientsShortTime() const { return m_maxActiveClientsShortTime; }
    [[nodiscard]] uint32 targetClientDataRate(bool minRate) const;

    // Slot gating
    //
    // Both queries take the inputs they cannot otherwise be handed, the same way
    // UploadBandwidthThrottler::getSlotLimit(uint32 currentUpSpeed) does. The upload cap
    // itself is resolved internally (USS when dynUp is on, thePrefs.maxUploadLimit()
    // otherwise), so callers and tests exercise that selection rather than bypassing it.

    /// Whether another client may start downloading from us.
    /// MFC CUploadQueue::AcceptNewClient (srchybrid/UploadQueue.cpp:383).
    [[nodiscard]] bool acceptNewClient(bool addOnNextConnect = false) const;

    /// @param curUploadSlots  slot count to test against — the overload above passes
    ///                        m_uploadingList.size(), minus one for the lowID extra slot.
    /// @param datarate        current upload datarate in bytes/s.
    /// MFC CUploadQueue::AcceptNewClient(INT_PTR) (srchybrid/UploadQueue.cpp:397).
    [[nodiscard]] bool acceptNewClient(int curUploadSlots, uint32 datarate) const;

    /// Whether the slot ladder justifies opening one more slot at this cap and datarate.
    /// Tail of MFC CUploadQueue::ForceNewClient (srchybrid/UploadQueue.cpp:432-455). The
    /// upPerClient divisors and slot floors are the deliberate "eMule 2026 bandwidth"
    /// divergence — MFC has a single /43 tier and tops out at MIN_UP_CLIENTS_ALLOWED+3.
    [[nodiscard]] bool slotLadderAllows(int curUploadSlots, uint32 datarate) const;

    // Stats
    [[nodiscard]] uint32 successfulUploadCount() const { return m_successfulUpCount; }
    [[nodiscard]] uint32 failedUploadCount() const { return m_failedUpCount; }
    [[nodiscard]] uint32 averageUpTime() const;

    // Component access
    void setThrottler(UploadBandwidthThrottler* throttler) { m_throttler = throttler; }
    void setDiskIOThread(UploadDiskIOThread* diskIO);
    [[nodiscard]] UploadDiskIOThread* diskIOThread() const { return m_diskIO; }
    void setSharedFileList(SharedFileList* sharedFiles) { m_sharedFiles = sharedFiles; }

    // Iterate
    void forEachWaiting(const std::function<void(UpDownClient*)>& callback) const;
    void forEachUploading(const std::function<void(UpDownClient*)>& callback) const;

signals:
    void clientAddedToQueue(eMule::UpDownClient* client);
    void clientRemovedFromQueue(eMule::UpDownClient* client);
    void uploadStarted(eMule::UpDownClient* client);
    void uploadEnded(eMule::UpDownClient* client);

public slots:
    /// Handle OP_REASKFILEPING from ClientUDPSocket.
    void onReaskFilePing(const Endpoint& sender,
                         const uint8* data, uint32 size);

private slots:
    void onBlockPacketsReady(eMule::UpDownClient* client,
                             QList<std::shared_ptr<eMule::Packet>> packets);
    void onReadError(eMule::UpDownClient* client);

private:
    // Slot management
    UpDownClient* findBestClientInQueue();
    bool forceNewClient(bool allowEmptyWaitingQueue = false);
    void addUpNextClient(UpDownClient* directadd = nullptr);
    bool checkForTimeOver(const UpDownClient* client);

    // Lists
    std::vector<UpDownClient*> m_waitingList;
    std::vector<UpDownClient*> m_uploadingList;
    mutable QMutex m_mutex;

    /// Family of the most recently promoted client, driving the alternating slot
    /// assignment when thePrefs.separateIPv6Queue() is on. Only consulted when clients
    /// of both families are waiting.
    bool m_lastSlotWasIPv6 = false;

    // Data rate tracking
    std::deque<uint64> m_averageDRList;       // bandwidth samples
    std::deque<uint64> m_averageFriendDRList;  // friend bandwidth
    std::deque<uint32> m_averageTickList;       // timestamps
    uint64 m_averageDRSum = 0;
    uint32 m_datarate = 0;
    uint32 m_friendDatarate = 0;
    uint32 m_lastCalculatedDataRateTick = 0;

    // Active client tracking
    std::deque<int> m_activeClientsList;
    std::deque<uint32> m_activeClientsTickList;
    int m_maxActiveClients = 0;
    int m_maxActiveClientsShortTime = 0;
    int m_highestNumberOfFullyActivatedSlotsSinceLastCall = 0;

    // Stats
    uint32 m_successfulUpCount = 0;
    uint32 m_failedUpCount = 0;
    uint32 m_totalUploadTime = 0;
    uint32 m_lastStartUpload = 0;
    uint32 m_maxScore = 0;

    // Components (not owned)
    UploadBandwidthThrottler* m_throttler = nullptr;
    UploadDiskIOThread* m_diskIO = nullptr;
    SharedFileList* m_sharedFiles = nullptr;
};

} // namespace eMule
