#pragma once

/// @file UploadBandwidthThrottler.h
/// @brief Thread-based per-socket bandwidth allocation — replaces MFC UploadBandwidthThrottler.
///
/// Controls how much data each socket may send per time slice.
/// Uses std::condition_variable instead of Windows CEvent,
/// std::list/std::vector instead of CTypedPtrList/CArray.

#include "net/ThrottledSocket.h"
#include "utils/Types.h"

#include <QThread>

#include <atomic>
#include <condition_variable>
#include <list>
#include <mutex>
#include <vector>

namespace eMule {

class UploadDiskIOThread;
class UploadQueue;

class UploadBandwidthThrottler : public QThread {
    Q_OBJECT
public:
    explicit UploadBandwidthThrottler(QObject* parent = nullptr);
    ~UploadBandwidthThrottler() override;

    UploadBandwidthThrottler(const UploadBandwidthThrottler&) = delete;
    UploadBandwidthThrottler& operator=(const UploadBandwidthThrottler&) = delete;

    // Data accounting (called by UploadQueue)
    uint64 getSentBytesSinceLastCallAndReset();
    uint64 getSentBytesOverheadSinceLastCallAndReset();
    int getHighestNumberOfFullyActivatedSlotsSinceLastCallAndReset();

    // Socket management
    int standardListSize() const;
    void addToStandardList(int index, ThrottledFileSocket* socket);
    bool removeFromStandardList(ThrottledFileSocket* socket);
    void queueForSendingControlPacket(ThrottledControlSocket* socket, bool hasSent = false);
    void removeFromAllQueues(ThrottledFileSocket* socket);
    void removeFromAllQueues(ThrottledControlSocket* socket);

    // Wakeup signals
    void newUploadDataAvailable();
    void socketAvailable();

    // Lifecycle
    void endThread();
    void pause(bool paused);

    // Component access
    void setUploadQueue(UploadQueue* uq) { m_uploadQueue = uq; }
    void setDiskIOThread(UploadDiskIOThread* dio) { m_diskIOThread = dio; }

    // Slot limit calculation
    uint32 getSlotLimit(uint32 currentUpSpeed) const;

protected:
    void run() override;

private:
    void runInternal();
    bool removeFromStandardListNoLock(ThrottledFileSocket* socket);
    void removeFromAllQueuesNoLock(ThrottledControlSocket* socket);
    static uint32 calculateChangeDelta(uint32 numberOfConsecutiveChanges);

    // Socket queues (guarded by m_sendMutex)
    std::list<ThrottledControlSocket*> m_controlQueue;
    std::list<ThrottledControlSocket*> m_controlQueueFirst;
    std::vector<ThrottledFileSocket*> m_standardOrder;

    // Temp queues (guarded by m_tempMutex) — loose coupling
    std::list<ThrottledControlSocket*> m_tempControlQueue;
    std::list<ThrottledControlSocket*> m_tempControlQueueFirst;

    // Synchronization
    mutable std::mutex m_sendMutex;
    mutable std::mutex m_tempMutex;
    std::mutex m_dataAvailableMutex;
    std::condition_variable m_dataAvailableCV;
    std::mutex m_socketAvailableMutex;
    std::condition_variable m_socketAvailableCV;
    std::mutex m_pauseMutex;
    std::condition_variable m_pauseCV;

    // Statistics (guarded by m_sendMutex)
    uint64 m_sentBytesSinceLastCall = 0;
    uint64 m_sentBytesOverheadSinceLastCall = 0;
    int m_highestNumberOfFullyActivatedSlots = 0;

    // Components (not owned)
    UploadQueue* m_uploadQueue = nullptr;
    UploadDiskIOThread* m_diskIOThread = nullptr;

    // State
    std::atomic<bool> m_run{true};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_dataAvailable{false};
    std::atomic<bool> m_socketAvailable{false};
};

} // namespace eMule
