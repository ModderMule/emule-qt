#pragma once

/// @file UploadQueue.h
/// @brief Upload queue manager — port of MFC CUploadQueue.
///
/// Manages upload slot allocation, waiting queue, data rate tracking,
/// and coordination between clients, throttler, and disk IO thread.

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
    [[nodiscard]] uint32 friendDatarate() const { return m_friendDatarate; }
    [[nodiscard]] uint32 targetClientDataRate(bool minRate) const;

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

private slots:
    void onBlockPacketsReady(eMule::UpDownClient* client,
                             QList<std::shared_ptr<eMule::Packet>> packets);
    void onReadError(eMule::UpDownClient* client);

private:
    // Slot management
    UpDownClient* findBestClientInQueue();
    bool acceptNewClient(bool addOnNextConnect = false) const;
    bool forceNewClient(bool allowEmptyWaitingQueue = false);
    void addUpNextClient(UpDownClient* directadd = nullptr);
    bool checkForTimeOver(const UpDownClient* client);

    // Lists
    std::vector<UpDownClient*> m_waitingList;
    std::vector<UpDownClient*> m_uploadingList;
    mutable QMutex m_mutex;

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
