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
    void queueForSendingControlPacket(ThrottledControlSocket* socket);
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

    /// Number of send-loop iterations run so far. Exposed for tests and wakeup
    /// diagnostics: an idle throttler must advance this only a few times a second.
    [[nodiscard]] uint64 loopIterations() const
    {
        return m_loopIterations.load(std::memory_order_relaxed);
    }

protected:
    void run() override;

private:
    void runInternal();
    bool removeFromStandardListNoLock(ThrottledFileSocket* socket);
    void removeFromAllQueuesNoLock(ThrottledControlSocket* socket);
    static uint32 calculateChangeDelta(uint32 numberOfConsecutiveChanges);

    /// Publish "there is work to do" and wake the send loop. Producers must have
    /// pushed the work onto their queue *before* calling this — see the ordering
    /// note on m_wakeMutex.
    void signalWorkAvailable();

    // Socket queues (guarded by m_sendMutex)
    std::list<ThrottledControlSocket*> m_controlQueue;
    std::vector<ThrottledFileSocket*> m_standardOrder;

    // Temp queues (guarded by m_tempMutex) — loose coupling
    std::list<ThrottledControlSocket*> m_tempControlQueue;

    // Synchronization
    mutable std::mutex m_sendMutex;
    mutable std::mutex m_tempMutex;

    /// Single wake channel for the send loop. One CV rather than one per flag so
    /// that *any* producer breaks the wait regardless of which branch the loop is
    /// parked in. Both m_dataAvailable and m_socketAvailable are stored under this
    /// mutex: the waiter holds it across its predicate check and its block, so a
    /// notify can no longer land in between and be lost. That was harmless while
    /// the loop polled every 1 ms; with the 100 ms idle wait it would be a stall.
    std::mutex m_wakeMutex;
    std::condition_variable m_wakeCV;

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
    std::atomic<uint64> m_loopIterations{0};
};

} // namespace eMule
