#pragma once

/// @file UploadQueue.h
/// @brief Upload queue manager — port of MFC CUploadQueue.
///
/// Manages upload slot allocation, waiting queue, data rate tracking,
/// and coordination between clients, throttler, and disk IO thread.

#include "net/Address.h"
#include "transfer/UploadQueueStore.h"
#include "utils/Types.h"

#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>

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

    /// Put a client rebuilt from uploadqueue.met straight onto the waiting list.
    /// Unlike addClientToQueue() this never takes the empty-queue fast path, so restoring
    /// a saved queue does not dial a burst of peers — the next process() promotes whoever
    /// earns a slot. Returns false when the shared admission gates reject the client.
    bool addRestoredClient(UpDownClient* client);

    /// One-shot load once we are online, then a self-throttled periodic save.
    /// Driven from CoreSession::onTimer()'s 1 s branch, like the clients.met/server.met
    /// autosaves. See transfer/UploadQueueStore.h for the policy.
    void processStore(const QString& path);

    /// Write the queue now, ignoring the resave timer — the clean-shutdown path.
    bool saveStoreNow(const QString& path);

    // Query
    [[nodiscard]] bool isOnUploadQueue(const UpDownClient* client) const;

    /// Whether this client is on the uploading list. MFC CUploadQueue::IsDownloading
    /// (srchybrid/UploadQueue.h:54) — the name is MFC's and means "the peer is downloading
    /// from us".
    ///
    /// A superset of UpDownClient::isUploadingToPeer(): addUpNextClient() pushes onto the
    /// list on both of its branches, so this is also true for a client still in
    /// UploadState::Connecting whose slot has not yet gone live. Pick per site — see the two
    /// commented decisions in addClientToQueue().
    [[nodiscard]] bool isDownloading(const UpDownClient* client) const;
    [[nodiscard]] int waitingUserCount() const;
    [[nodiscard]] int uploadQueueLength() const;
    [[nodiscard]] int waitingPosition(const UpDownClient* client) const;
    [[nodiscard]] UpDownClient* waitingClientByIP(uint32 ip) const;

    /// Mean UpDownClient::getCombinedFilePrioAndCredit() over the waiting list, recomputed
    /// at most once every 5 s. This is what decides "high ranking" for the soft queue
    /// limit — a client below the average is turned away once the queue passes
    /// thePrefs.queueSize(). MFC CUploadQueue::GetAverageCombinedFilePrioAndCredit
    /// (srchybrid/UploadQueue.cpp:678).
    ///
    /// Public where MFC has it private, so the soft-limit behaviour is directly assertable;
    /// acceptNewClient()/slotLadderAllows() are exposed for the same reason. Non-const: it
    /// refreshes the cache.
    [[nodiscard]] float getAverageCombinedFilePrioAndCredit();

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

    /// Whether this client's upload session should end now, freeing its slot.
    /// MFC CUploadQueue::CheckForTimeOver (srchybrid/UploadQueue.cpp:791). Public where MFC
    /// has it private, for the same reason as acceptNewClient()/slotLadderAllows(): the
    /// collection-slot rules it enforces are only directly assertable from outside.
    [[nodiscard]] bool checkForTimeOver(const UpDownClient* client);

    /// Refresh the cached best score on the waiting list, which checkForTimeOver() compares
    /// a slot holder against. MFC CUploadQueue::UpdateMaxClientScore
    /// (srchybrid/UploadQueue.cpp:781-789).
    ///
    /// Uses score(true, false) — the system value — so a Low-ID peer we cannot reach right
    /// now contributes 0 and cannot evict a client we *are* uploading to on the strength of
    /// a slot it could not accept.
    ///
    /// Self-throttled to MFC's 5 s cadence, since process() runs every ~100 ms. Pass
    /// @p force from the one site MFC refreshes out of band, addUpNextClient(), where the
    /// highest scorer has just left the list.
    void updateMaxClientScore(bool force = false);

    [[nodiscard]] uint64 maxClientScore() const { return m_maxScore; }

    /// Promote a client into an upload slot. @return false when the slot was NOT opened —
    /// either there was nobody to promote, or the dial failed. MFC CUploadQueue::AddUpNextClient
    /// (srchybrid/UploadQueue.cpp:181), which likewise refuses to insert into the uploading
    /// list when TryToConnect() fails.
    ///
    /// Public where MFC has it private, for the same reason as acceptNewClient() and
    /// checkForTimeOver(): "a slot is not opened for a peer we could not dial" is only
    /// directly assertable from outside.
    bool addUpNextClient(UpDownClient* directadd = nullptr);

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
    /// Outcome of the shared waiting-list admission gates.
    enum class QueueAdmission {
        Ok,             ///< may be appended to the waiting list
        AlreadyQueued,  ///< this exact object is already waiting
        Rejected        ///< banned, duplicate, or over a per-address limit
    };

    /// The low-ID callback-abuse gate. Kept apart from checkWaitingListAdmission() because
    /// MFC runs it *before* the request counters, unlike the gates below.
    [[nodiscard]] bool lowIdAbuseGateRejects(const UpDownClient* client) const;

    /// Ban / duplicate / per-IP / IPv6 gates, shared by addClientToQueue() and
    /// addRestoredClient() so the two cannot drift. @p context only labels the log lines.
    QueueAdmission checkWaitingListAdmission(UpDownClient* client, const char* context);

    /// Bump the per-file request counter on the file this client is asking for — MFC
    /// srchybrid/UploadQueue.cpp:625. Feeds the "Requests" figures in the Shared Files panel,
    /// the web UI's shared-file table and the all-time counter persisted in known.met.
    ///
    /// TODO: Maybe we should change this to count each request for a file only once and
    /// ignore re-asks. (MFC carries the same TODO at the call site.)
    void countFileRequest(const UpDownClient* client);

    /// The soft/hard queue-size cap — MFC CUploadQueue::AddClientToQueue
    /// (srchybrid/UploadQueue.cpp:638-655). thePrefs.queueSize() is only a *soft* limit:
    /// past it we still admit friends holding a friend slot and anyone scoring above
    /// getAverageCombinedFilePrioAndCredit(). The hard limit sits 25% higher and admits
    /// nobody.
    ///
    /// Deliberately not folded into checkWaitingListAdmission(): MFC runs this cap *after*
    /// the already-queued early-return, so a client re-asking from the queue still gets its
    /// ranking info when we are over the limit. Non-const: refreshes the average cache.
    [[nodiscard]] bool queueLimitRejects(const UpDownClient* client);

    // Slot management
    UpDownClient* findBestClientInQueue();
    bool forceNewClient(bool allowEmptyWaitingQueue = false);


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

    /// Best score on the waiting list, refreshed by updateMaxClientScore() and read only by
    /// checkForTimeOver()'s score kick. Both are inert while thePrefs.transferFullChunks()
    /// is on, which is the default — MFC gates them the same way.
    uint64 m_maxScore = 0;
    uint32 m_lastCalculatedMaxScore = 0;

    /// Earliest tick at which the score kick may fire again. MFC seeds it with the current
    /// tick and pushes it 6 s ahead on every kick (srchybrid/UploadQueue.cpp:833), so a
    /// batch of slots cannot all be dropped against one stale max score.
    uint32 m_removedClientByScore = 0;

    // 5 s cache behind getAverageCombinedFilePrioAndCredit(). A tick of 0 means "never
    // computed" — see the note there.
    uint32 m_lastCalculatedAverageCombined = 0;
    float m_averageCombinedFilePrioAndCredit = 0.0f;

    // Components (not owned)
    UploadBandwidthThrottler* m_throttler = nullptr;
    UploadDiskIOThread* m_diskIO = nullptr;
    SharedFileList* m_sharedFiles = nullptr;

    /// Held by value, mirroring PartFile's SourceSaver: the load-once flag and the resave
    /// timer belong to the queue's lifetime, and the queue is global so one instance is all
    /// there is.
    UploadQueueStore m_store;
};

} // namespace eMule
