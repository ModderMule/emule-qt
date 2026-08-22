#include "pch.h"
/// @file UploadQueue.cpp
/// @brief Upload queue manager — port of MFC UploadQueue.cpp.
///
/// Upload slot allocation, waiting queue management, score-based selection,
/// data rate tracking, and session time-over checks.

#include "transfer/UploadQueue.h"
#include "transfer/UploadQueueStore.h"
#include "app/AppContext.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "transfer/UploadDiskIOThread.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "net/ClientUDPSocket.h"
#include "net/LastCommonRouteFinder.h"
#include "transfer/DownloadQueue.h"
#include "files/Collection.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "net/EMSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "server/ServerConnect.h"
#include "utils/Log.h"
#include "utils/SafeFile.h"
#include "utils/TimeUtils.h"



namespace eMule {

namespace {

/// The upload cap both slot gates work against, in KB/s; UNLIMITED when uncapped.
/// USS on → the finder's live limit; USS off → thePrefs.maxUploadLimit(), which maps
/// eMuleQt's "no limit" (a raw 0) onto the UNLIMITED sentinel every MFC-derived
/// comparison below expects.
///
/// Unlike MFC (srchybrid/UploadQueue.cpp:405) the finder pointer is null-checked — it is
/// only constructed by CoreSession, so anything driving the queue without one falls back
/// to prefs rather than crashing.
uint32 uploadCapKB()
{
    if (thePrefs.dynUpEnabled() && theApp.lastCommonRouteFinder)
        return theApp.lastCommonRouteFinder->getUpload() / 1024;
    return thePrefs.maxUploadLimit();
}

} // namespace

UploadQueue::UploadQueue(QObject* parent)
    : QObject(parent)
{
}

UploadQueue::~UploadQueue() = default;

// ===========================================================================
// setDiskIOThread — connect block-ready and error signals
// ===========================================================================

void UploadQueue::setDiskIOThread(UploadDiskIOThread* diskIO)
{
    if (m_diskIO) {
        disconnect(m_diskIO, nullptr, this, nullptr);
    }
    m_diskIO = diskIO;
    if (m_diskIO) {
        connect(m_diskIO, &UploadDiskIOThread::blockPacketsReady,
                this, &UploadQueue::onBlockPacketsReady,
                Qt::QueuedConnection);
        connect(m_diskIO, &UploadDiskIOThread::readError,
                this, &UploadQueue::onReadError,
                Qt::QueuedConnection);
    }
}

// ===========================================================================
// onBlockPacketsReady — enqueue disk-read packets on the client's socket
// ===========================================================================

void UploadQueue::onBlockPacketsReady(UpDownClient* client,
                                       QList<std::shared_ptr<Packet>> packets)
{
    if (thePrefs.logRawSocketPackets())
        logDebug(QStringLiteral("onBlockPacketsReady: client=%1 packets=%2")
                     .arg(client ? client->userName() : QStringLiteral("null"))
                     .arg(packets.size()));
    if (!client || !client->socket())
        return;

    auto* sock = client->socket();
    for (const auto& pkt : packets) {
        if (pkt) {
            // EMSocket takes ownership via unique_ptr; copy from shared_ptr
            sock->sendPacket(std::make_unique<Packet>(*pkt), false, pkt->statsPayload);
        }
    }
}

// ===========================================================================
// onReadError — handle disk read failure for a client
// ===========================================================================

void UploadQueue::onReadError(UpDownClient* client)
{
    if (client)
        removeFromUploadQueue(client);
}

// ===========================================================================
// Query methods
// ===========================================================================

bool UploadQueue::isOnUploadQueue(const UpDownClient* client) const
{
    return std::find(m_waitingList.begin(), m_waitingList.end(), client) != m_waitingList.end();
}

bool UploadQueue::isDownloading(const UpDownClient* client) const
{
    return std::find(m_uploadingList.begin(), m_uploadingList.end(), client) != m_uploadingList.end();
}

int UploadQueue::waitingUserCount() const
{
    return static_cast<int>(m_waitingList.size());
}

int UploadQueue::uploadQueueLength() const
{
    return static_cast<int>(m_uploadingList.size());
}

UpDownClient* UploadQueue::waitingClientByIP(uint32 ip) const
{
    for (auto* client : m_waitingList) {
        if (client->userAddress().toNetworkUint32() == ip)
            return client;
    }
    return nullptr;
}

int UploadQueue::waitingPosition(const UpDownClient* client) const
{
    if (!isOnUploadQueue(client))
        return 0;

    uint64 myScore = client->score(false);
    int rank = 1;
    for (const auto* other : m_waitingList) {
        if (other->score(false) > myScore)
            ++rank;
    }
    return rank;
}

// ===========================================================================
// getAverageCombinedFilePrioAndCredit — MFC CUploadQueue (srchybrid/UploadQueue.cpp:678)
// ===========================================================================

float UploadQueue::getAverageCombinedFilePrioAndCredit()
{
    const uint32 curTick = static_cast<uint32>(getTickCount());

    // Two guards MFC does not have.
    //
    // Empty list: MFC divides by waitinglist.GetCount() unguarded and only survives it
    // because its single caller tests the soft limit first. Not depending on caller
    // ordering costs one comparison.
    //
    // First call: MFC leans on Windows GetTickCount() being large at process start, so its
    // "curTick >= last + 5 s" is true the first time round. Our getTickCount() is a
    // truncated steady_clock and can be small, so treat a zero tick as "never computed".
    if (m_lastCalculatedAverageCombined == 0
        || curTick >= m_lastCalculatedAverageCombined + SEC2MS(5))
    {
        m_lastCalculatedAverageCombined = curTick;

        if (m_waitingList.empty()) {
            m_averageCombinedFilePrioAndCredit = 0.0f;
        } else {
            float sum = 0.0f;
            for (const auto* client : m_waitingList)
                sum += client->getCombinedFilePrioAndCredit();

            m_averageCombinedFilePrioAndCredit =
                sum / static_cast<float>(m_waitingList.size());
        }
    }

    return m_averageCombinedFilePrioAndCredit;
}

uint32 UploadQueue::targetClientDataRate(bool minRate) const
{
    uint32 openSlots = static_cast<uint32>(m_uploadingList.size());
    // 3 slots or less: 3 KiB/s; 4+: linear growth capped at UPLOAD_CLIENT_MAXDATARATE
    uint32 result;
    if (openSlots <= 3)
        result = 3 * 1024;
    else
        result = std::min(static_cast<uint32>(UPLOAD_CLIENT_MAXDATARATE), openSlots * 1024);

    return minRate ? result * 3 / 4 : result;
}

uint32 UploadQueue::averageUpTime() const
{
    return m_successfulUpCount > 0 ? (m_totalUploadTime / m_successfulUpCount) : 0;
}

// ===========================================================================
// Iteration
// ===========================================================================

void UploadQueue::forEachWaiting(const std::function<void(UpDownClient*)>& callback) const
{
    for (auto* client : m_waitingList)
        callback(client);
}

void UploadQueue::forEachUploading(const std::function<void(UpDownClient*)>& callback) const
{
    for (auto* client : m_uploadingList)
        callback(client);
}

// ===========================================================================
// findBestClientInQueue — MFC CUploadQueue::FindBestClientInQueue
// ===========================================================================

UpDownClient* UploadQueue::findBestClientInQueue()
{
    uint64 bestScore = 0;
    uint64 bestLowScore = 0;
    UpDownClient* newClient = nullptr;
    UpDownClient* lowClient = nullptr;
    // Per-family bests, tracked alongside the global one. The global best alone can't
    // serve the alternating policy: whichever family scores higher would win every
    // slot, which is exactly how a small IPv6 population gets starved.
    uint64 bestScoreV4 = 0;
    uint64 bestScoreV6 = 0;
    UpDownClient* bestV4 = nullptr;
    UpDownClient* bestV6 = nullptr;
    const uint32 curTick = static_cast<uint32>(getTickCount());

    for (auto it = m_waitingList.begin(); it != m_waitingList.end(); ) {
        UpDownClient* cur = *it;

        // Purge stale clients (not seen in MAX_PURGEQUEUETIME = 1 hour)
        bool stale = (curTick >= cur->lastUpRequest() + MAX_PURGEQUEUETIME);
        bool noFile = m_sharedFiles && !m_sharedFiles->getFileByID(cur->reqUpFileId());

        if (stale || noFile) {
            cur->clearWaitStartTime();
            it = m_waitingList.erase(it);
            cur->setUploadState(UploadState::None);
            cur->setAddNextConnect(false);
            emit clientRemovedFromQueue(cur);
            continue;
        }

        // Natural send point for a queued OP_CHANGE_CLIENT_IP — we are touching this client
        // anyway, so telling it our new IPv6 costs no extra wakeup.
        cur->flushPendingIPChange();

        const uint64 curScore = cur->score(false);
        const bool connectable =
            !cur->hasLowID() || (cur->socket() && cur->socket()->isConnected());

        if (connectable) {
            if (cur->isIPv6Connection()) {
                if (curScore > bestScoreV6) {
                    bestScoreV6 = curScore;
                    bestV6 = cur;
                }
            } else if (curScore > bestScoreV4) {
                bestScoreV4 = curScore;
                bestV4 = cur;
            }
        }

        if (curScore > bestScore) {
            if (connectable) {
                bestScore = curScore;
                newClient = cur;
            } else if (!cur->addNextConnect()) {
                if (curScore > bestLowScore) {
                    bestLowScore = curScore;
                    lowClient = cur;
                }
            }
        }
        ++it;
    }

    // Low-ID deferral is compared against the global best, not the alternation winner,
    // so slot fairness never changes who gets flagged for the next-connect shortcut.
    if (lowClient && bestLowScore > bestScore)
        lowClient->setAddNextConnect(true);

    // Alternate only when both families actually have a candidate — otherwise fall
    // through to the plain best, so a slot is never held back for an absent family.
    if (thePrefs.separateIPv6Queue() && bestV4 && bestV6)
        return m_lastSlotWasIPv6 ? bestV4 : bestV6;

    return newClient;
}

// ===========================================================================
// acceptNewClient — MFC CUploadQueue::AcceptNewClient
// ===========================================================================

bool UploadQueue::acceptNewClient(bool addOnNextConnect) const
{
    int curUploadSlots = static_cast<int>(m_uploadingList.size());
    // We allow ONE extra slot to accommodate lowID users, who get skipped when it was
    // actually their turn. MFC srchybrid/UploadQueue.cpp:388-391.
    if (addOnNextConnect && curUploadSlots > 0)
        --curUploadSlots;

    return acceptNewClient(curUploadSlots, m_datarate);
}

bool UploadQueue::acceptNewClient(int curUploadSlots, uint32 datarate) const
{
    if (curUploadSlots < std::max(static_cast<int>(MIN_UP_CLIENTS_ALLOWED), 4))
        return true;
    if (curUploadSlots >= MAX_UP_CLIENTS_ALLOWED)
        return false;

    const uint32 maxSpeed = uploadCapKB();
    uint32 tgtRate = targetClientDataRate(false);
    uint32 minTgtRate = targetClientDataRate(true);

    if (static_cast<uint32>(curUploadSlots) >= datarate / minTgtRate)
        return false;
    // MFC lets UNLIMITED * 1024 wrap and relies on the ~1.4M result being out of reach
    // (srchybrid/UploadQueue.cpp:410); say it outright instead.
    if (maxSpeed != UNLIMITED && static_cast<uint32>(curUploadSlots) >= maxSpeed * 1024 / tgtRate)
        return false;

    // An unlimited limit is still bounded by the configured line capacity — MFC
    // srchybrid/UploadQueue.cpp:413-416. There MaxGraphUploadRate uses UNLIMITED for
    // "capacity unknown" and falls back to an estimate; here that state is 0, and there
    // is no maxGraphUploadRateEstimated to fall back to, so 0 simply means "no cap".
    return maxSpeed != UNLIMITED
        // Unreachable when USS is on: the cap is then getUpload()/1024, which cannot reach
        // UINT32_MAX, so the disjunct above has already returned. Kept for MFC fidelity —
        // srchybrid/UploadQueue.cpp:414 carries the same redundant test.
        || thePrefs.dynUpEnabled()
        || thePrefs.maxGraphUploadRate() == 0
        || static_cast<uint32>(curUploadSlots) < thePrefs.maxGraphUploadRate() * 1024 / tgtRate;
}

// ===========================================================================
// forceNewClient — MFC CUploadQueue::ForceNewClient
// ===========================================================================

bool UploadQueue::forceNewClient(bool allowEmptyWaitingQueue)
{
    if (!allowEmptyWaitingQueue && m_waitingList.empty())
        return false;

    // USS veto check
    if (theApp.lastCommonRouteFinder && thePrefs.dynUpEnabled()
        && !theApp.lastCommonRouteFinder->acceptNewClient())
        return false;

    int curUploadSlots = static_cast<int>(m_uploadingList.size());
    if (curUploadSlots < MIN_UP_CLIENTS_ALLOWED)
        return true;

    const uint32 curTick = static_cast<uint32>(getTickCount());
    if (curTick < m_lastStartUpload + SEC2MS(1) && m_datarate < 102400)
        return false;

    if (!acceptNewClient())
        return false;

    if (slotLadderAllows(curUploadSlots, m_datarate))
        return true;

    // The ladder said no, but the throttler saw more slots fully active than we have
    // open — that is evidence the line can carry another one anyway.
    return m_highestNumberOfFullyActivatedSlotsSinceLastCall >
           static_cast<int>(m_uploadingList.size());
}

// ===========================================================================
// slotLadderAllows — tail of MFC CUploadQueue::ForceNewClient
// ===========================================================================

bool UploadQueue::slotLadderAllows(int curUploadSlots, uint32 datarate) const
{
    const uint32 maxSpeed = uploadCapKB();
    uint32 upPerClient = targetClientDataRate(false);

    // eMule 2026 bandwidth: tiered upPerClient scaling matching getSlotLimit(). MFC default: single tier at >49 KB/s with /43 divisor.
    if (maxSpeed > 500) {
        upPerClient += datarate / 20;
        if (upPerClient > UPLOAD_CLIENT_MAXDATARATE)
            upPerClient = UPLOAD_CLIENT_MAXDATARATE;
    } else if (maxSpeed > 200) {
        upPerClient += datarate / 30;
        if (upPerClient > UPLOAD_CLIENT_MAXDATARATE)
            upPerClient = UPLOAD_CLIENT_MAXDATARATE;
    } else if (maxSpeed > 49) {
        upPerClient += datarate / 43;
        if (upPerClient > UPLOAD_CLIENT_MAXDATARATE)
            upPerClient = UPLOAD_CLIENT_MAXDATARATE;
    }

    if (maxSpeed == UNLIMITED)
        return static_cast<uint32>(curUploadSlots) < datarate / upPerClient;

    // eMule 2026 bandwidth: higher slot floors for broadband. MFC default: max tier at >25 KB/s.
    uint32 nMaxSlots;
    if (maxSpeed > 200)
        nMaxSlots = std::max((maxSpeed * 1024) / upPerClient,
                             static_cast<uint32>(MIN_UP_CLIENTS_ALLOWED + 5));
    else if (maxSpeed > 100)
        nMaxSlots = std::max((maxSpeed * 1024) / upPerClient,
                             static_cast<uint32>(MIN_UP_CLIENTS_ALLOWED + 4));
    else if (maxSpeed > 25)
        nMaxSlots = std::max((maxSpeed * 1024) / upPerClient,
                             static_cast<uint32>(MIN_UP_CLIENTS_ALLOWED + 3));
    else if (maxSpeed > 16)
        nMaxSlots = MIN_UP_CLIENTS_ALLOWED + 2;
    else if (maxSpeed > 9)
        nMaxSlots = MIN_UP_CLIENTS_ALLOWED + 1;
    else
        nMaxSlots = MIN_UP_CLIENTS_ALLOWED;

    return static_cast<uint32>(curUploadSlots) < nMaxSlots;
}

// ===========================================================================
// addUpNextClient — MFC CUploadQueue::AddUpNextClient
// ===========================================================================

void UploadQueue::addUpNextClient(UpDownClient* directadd)
{
    UpDownClient* newClient = directadd;
    if (!newClient) {
        newClient = findBestClientInQueue();
        if (!newClient)
            return;
    }

    removeFromWaitingQueue(newClient);

    if (isDownloading(newClient))
        return;

    // Only the collection bypass in addClientToQueue() hands us a client with this flag set,
    // and it always passes it as directadd. Reaching normal slot selection with the flag on
    // means it survived a slot it should have been cleared by. MFC ASSERT(0)s here
    // (srchybrid/UploadQueue.cpp:202-205); log and clear.
    if (!directadd && newClient->collectionUploadSlot()) {
        logDebug(QStringLiteral("addUpNextClient: %1 reached normal slot selection still "
                                "holding a collection slot — clearing")
                     .arg(newClient->userName()));
        newClient->setCollectionUploadSlot(false);
    }

    // Send accept upload request if connected
    EMSocket* sock = newClient->getFileUploadSocket();
    if (!sock || !sock->isConnected() || !newClient->checkHandshakeFinished()) {
        newClient->setUploadState(UploadState::Connecting);
        newClient->tryToConnect(true);
    } else {
        auto packet = std::make_unique<Packet>(OP_ACCEPTUPLOADREQ, 0);
        newClient->sendPacket(std::move(packet));
        newClient->setUploadState(UploadState::Uploading);
    }

    newClient->resetSessionUp();
    newClient->resetQueueSessionPayloadUp();

    // Add to throttler
    if (m_throttler && sock)
        m_throttler->addToStandardList(static_cast<int>(m_uploadingList.size()), sock);

    m_uploadingList.push_back(newClient);
    newClient->setSlotNumber(static_cast<uint32>(m_uploadingList.size()));

    // Record the family on EVERY promotion, including the directadd paths (low-ID
    // reconnect, empty-queue fast path). Updating it only in findBestClientInQueue()
    // would let those paths silently desync the alternation.
    m_lastSlotWasIPv6 = newClient->isIPv6Connection();

    m_lastStartUpload = static_cast<uint32>(getTickCount());

    // Update statistics on the requested file
    if (m_sharedFiles) {
        KnownFile* reqFile = m_sharedFiles->getFileByID(newClient->reqUpFileId());
        if (reqFile)
            reqFile->statistic.addAccepted();
    }

    emit uploadStarted(newClient);
}

// ===========================================================================
// addClientToQueue — MFC CUploadQueue::AddClientToQueue
// ===========================================================================

bool UploadQueue::lowIdAbuseGateRejects(const UpDownClient* client) const
{
    // Prevent Low ID callback abuse (MFC UploadQueue.cpp:525-546)
    // Reject clients we can never reach when our queue is already long.
    //
    // Separate from checkWaitingListAdmission() because of where MFC puts it: this runs
    // *before* the request counters, so a client rejected here does not get its asked-count
    // bumped, while a client rejected by the ban/duplicate gates does.
    return theApp.isConnected()
        && theApp.isFirewalled()
        && client->kadPort() == 0
        && client->downloadState() == DownloadState::None
        && !client->friendPtr()
        && theApp.serverConnect
        && !theApp.serverConnect->isLocalServer(client->serverAddress().toNetworkUint32(),
                                                client->serverPort())
        && static_cast<int>(m_waitingList.size()) > 50;
}

UploadQueue::QueueAdmission UploadQueue::checkWaitingListAdmission(UpDownClient* client,
                                                                   const char* context)
{
    if (client->isBanned()) {
        logDebug(QStringLiteral("%1: rejected banned client %2")
                     .arg(QLatin1String(context), client->userName()));
        return QueueAdmission::Rejected;
    }

    // Check for duplicates and IP limits
    uint16 sameIPCount = 0;
    for (auto it = m_waitingList.begin(); it != m_waitingList.end(); ++it) {
        UpDownClient* cur = *it;
        if (cur == client)
            return QueueAdmission::AlreadyQueued;
        if (client->compare(cur)) {
            // Same ip:port or user hash as a queued client. Track it regardless — MFC
            // keeps a record of every client that reaches this branch.
            if (theApp.clientList)
                theApp.clientList->addTrackClient(client);
            logDebug(QStringLiteral("%1: rejected duplicate client %2")
                         .arg(QLatin1String(context), client->userName()));
            return QueueAdmission::Rejected;
        }
        if (client->userAddress() == cur->userAddress())
            ++sameIPCount;
    }

    if (sameIPCount >= 3) {
        logDebug(QStringLiteral("%1: rejected %2 — 3+ clients from same IP")
                     .arg(QLatin1String(context), client->userName()));
        return QueueAdmission::Rejected;
    }

    // Second, independent per-address gate (MFC UploadQueue.cpp:614). Unlike the loop
    // above it counts distinct TCP ports seen from the address over KEEPTRACK_TIME, not
    // clients queued right now, so it also catches a peer that keeps reconnecting from
    // fresh ports. IPv4 only — IPv6 has its own, stricter rule below.
    if (client->userAddress().isIPv4() && theApp.clientList
        && theApp.clientList->clientsFromIP(client->userAddress()) >= 3)
    {
        logDebug(QStringLiteral("%1: rejected %2 — 3+ tracked clients from %3")
                     .arg(QLatin1String(context), client->userName(), ipstr(client->userAddress())));
        return QueueAdmission::Rejected;
    }

    // IPv6: at most one client per address, counting active uploads as well as the
    // waiting list. Deliberately an exact 128-bit match — no prefix/range logic, since
    // a single /64 legitimately belongs to one subscriber.
    //
    // Gated on the address rather than isIPv6Connection() so the test and the comparison
    // below use the same value: a peer whose socket is v6 but whose userAddress is v4
    // would otherwise be compared against IPv4 addresses.
    if (client->userAddress().isIPv6()) {
        const Address& v6 = client->userAddress();
        const auto sameV6 = [&](const UpDownClient* other) {
            return other != client && other->userAddress() == v6;
        };
        if (std::any_of(m_waitingList.begin(), m_waitingList.end(), sameV6)
            || std::any_of(m_uploadingList.begin(), m_uploadingList.end(), sameV6))
        {
            logDebug(QStringLiteral("%1: rejected %2 — IPv6 %3 already queued or uploading")
                         .arg(QLatin1String(context), client->userName(), ipstr(v6)));
            return QueueAdmission::Rejected;
        }
    }

    return QueueAdmission::Ok;
}

void UploadQueue::countFileRequest(const UpDownClient* client)
{
    if (!m_sharedFiles)
        return;

    if (KnownFile* reqFile = m_sharedFiles->getFileByID(client->reqUpFileId()))
        reqFile->statistic.addRequest();
}

bool UploadQueue::queueLimitRejects(const UpDownClient* client)
{
    // MFC srchybrid/UploadQueue.cpp:638-655. The queue limit in prefs is only a soft limit;
    // the hard limit is up to 25% higher so powershare and other high-ranking clients can
    // still get in after the soft limit has been reached.
    const int softQueueLimit = static_cast<int>(thePrefs.queueSize());
    const int hardQueueLimit = softQueueLimit + std::max(softQueueLimit, 800) / 4;

    const int waiting = static_cast<int>(m_waitingList.size());
    if (waiting < softQueueLimit)
        return false;

    if (waiting >= hardQueueLimit) {
        logDebug(QStringLiteral("addClientToQueue: rejected %1 — hard queue limit %2 reached")
                     .arg(client->userName())
                     .arg(hardQueueLimit));
        return true;
    }

    // Soft limit reached: a friend holding a friend slot always gets in. MFC's IsFriend()
    // is m_Friend != NULL, which is friendPtr() here.
    if (client->friendPtr() && client->friendSlot())
        return false;

    // ...and so does anyone wanting a higher-priority file, or carrying better credits,
    // than the average client already waiting.
    if (client->getCombinedFilePrioAndCredit() >= getAverageCombinedFilePrioAndCredit())
        return false;

    logDebug(QStringLiteral("addClientToQueue: rejected %1 — soft queue limit %2 reached "
                            "and its rank %3 is below the queue average %4")
                 .arg(client->userName())
                 .arg(softQueueLimit)
                 .arg(static_cast<double>(client->getCombinedFilePrioAndCredit()))
                 .arg(static_cast<double>(m_averageCombinedFilePrioAndCredit)));
    return true;
}

bool UploadQueue::addClientToQueue(UpDownClient* client, bool ignoreTimeLimit)
{
    if (!client)
        return false;

    if (lowIdAbuseGateRejects(client))
        return false;

    client->incAskedCount();
    client->setLastUpRequest(static_cast<uint32>(getTickCount()));

    if (!ignoreTimeLimit)
        client->addRequestCount(client->reqUpFileId());

    switch (checkWaitingListAdmission(client, "addClientToQueue")) {
    case QueueAdmission::Rejected:
        return false;
    case QueueAdmission::AlreadyQueued:
        // Already in queue — handle lowID reconnect
        if (client->addNextConnect() && acceptNewClient(true)) {
            client->setAddNextConnect(false);
            removeFromWaitingQueue(client);
            // MFC srchybrid/UploadQueue.cpp:566-570 counts this one, because the client is
            // being handed the slot it missed. The plain re-ask below is not a new request.
            countFileRequest(client);
            addUpNextClient(client);
        } else {
            client->sendRankingInfo();
        }
        return true;
    case QueueAdmission::Ok:
        break;
    }

    countFileRequest(client);

    // An eMule collection bypasses the queue. It is a few KB of index that the peer needs
    // before it can ask for anything real, so parking it behind a 5000-deep queue helps
    // nobody. MFC srchybrid/UploadQueue.cpp:627-637.
    //
    // Deliberately not gated on acceptNewClient()/forceNewClient(), exactly as in MFC: this
    // can push the slot count one over the configured limit, which the 50 KB size cap and
    // checkForTimeOver()'s enforcement keep bounded.
    KnownFile* reqFile = m_sharedFiles ? m_sharedFiles->getFileByID(client->reqUpFileId())
                                       : nullptr;
    //
    // MFC's guard here is client->IsDownloading() — the US_UPLOADING state. This uses the
    // uploading-*list* test instead, which is a strict superset: it also covers a client in
    // UploadState::Connecting, whose slot is allocated but not yet live. That closes a hole
    // MFC leaves open, because such a client already holds a slot and must not be handed a
    // collection bypass on top of it. The early-accept below makes the opposite choice, for
    // the reason given there.
    if (reqFile
        && Collection::hasCollectionExtension(reqFile->fileName())
        && reqFile->fileSize() < MAXPRIORITYCOLL_SIZE
        && !isDownloading(client)
        && client->socket() && client->socket()->isConnected())
    {
        client->setCollectionUploadSlot(true);
        // MFC calls RemoveFromWaitingQueue() here first; redundant for us, since
        // addUpNextClient() does it itself and this path is only reachable when
        // checkWaitingListAdmission() said Ok — i.e. the client is not on the list.
        addUpNextClient(client);
        return true;
    }

    // Not a collection request, so any slot this client still holds is an ordinary one.
    // MFC guards its setter with ASSERT(!IsDownloading() || bValue == m_bCollectionUploadSlot);
    // not ported, because a collection-slot holder that re-asks for a normal file reaches
    // exactly this line while uploading and would trip it.
    client->setCollectionUploadSlot(false);

    // Cap the list. MFC puts this after the admission gates and the request statistics but
    // before the already-downloading early-accept, so a peer over the limit cannot slip in
    // by asking for a second file while it downloads a first.
    if (queueLimitRejects(client))
        return false;

    // Already holding a live slot and probably just after a second file — acknowledge and
    // let it request parts. MFC srchybrid/UploadQueue.cpp:656.
    //
    // isUploadingToPeer(), MFC's IsDownloading(), and NOT the uploading-list test used by the
    // collection bypass above: a client still in UploadState::Connecting is on that list, so
    // the list test would send it OP_ACCEPTUPLOADREQ and then have addReqBlock() drop every
    // OP_REQUESTPARTS it makes in reply. Falling through to normal queueing instead is both
    // MFC's behaviour and the honest answer to the peer.
    if (client->isUploadingToPeer()) {
        auto packet = std::make_unique<Packet>(OP_ACCEPTUPLOADREQ, 0);
        client->sendPacket(std::move(packet));
        return true;
    }

    // If queue is empty and we can accept, add directly
    if (m_waitingList.empty() && forceNewClient(true)) {
        client->setWaitStartTime();
        addUpNextClient(client);
    } else {
        m_waitingList.push_back(client);
        client->setUploadState(UploadState::OnUploadQueue);
        client->sendRankingInfo();
        emit clientAddedToQueue(client);
    }

    return true;
}

// ===========================================================================
// addRestoredClient — upload queue store load path
// ===========================================================================

bool UploadQueue::addRestoredClient(UpDownClient* client)
{
    if (!client)
        return false;

    // Same admission rules as a live request — a persisted record is untrusted input and
    // must not smuggle a banned or duplicate peer past the gates. AlreadyQueued cannot
    // happen for a freshly constructed object, so it counts as a rejection here.
    if (lowIdAbuseGateRejects(client))
        return false;
    if (checkWaitingListAdmission(client, "addRestoredClient") != QueueAdmission::Ok)
        return false;
    // uploadqueue.met is untrusted input like any other; a restore must not push us past
    // the hard limit. In practice the queue is near-empty when the one-shot load fires.
    if (queueLimitRejects(client))
        return false;

    // Deliberately NOT addClientToQueue()'s tail: that promotes straight to an upload slot
    // when the waiting list is empty and a slot is free, which on a bulk restore would dial
    // one peer per free slot the instant we come online. Restored clients go on the waiting
    // list and wait their turn; the next process() promotes whoever earns a slot.
    //
    // Also skipped: incAskedCount()/addRequestCount() and countFileRequest() (a restore is
    // not a request — it must neither feed the flood counter nor inflate the per-file
    // "Requests" statistic) and sendRankingInfo() (no socket yet — it would no-op).
    m_waitingList.push_back(client);
    client->setUploadState(UploadState::OnUploadQueue);
    emit clientAddedToQueue(client);
    return true;
}

// ===========================================================================
// Upload queue store — see transfer/UploadQueueStore.h
// ===========================================================================

void UploadQueue::processStore(const QString& path)
{
    m_store.process(this, path);
}

bool UploadQueue::saveStoreNow(const QString& path)
{
    return m_store.saveNow(this, path);
}

// ===========================================================================
// removeFromUploadQueue — MFC CUploadQueue::RemoveFromUploadQueue
// ===========================================================================

bool UploadQueue::removeFromUploadQueue(UpDownClient* client)
{
    auto it = std::find(m_uploadingList.begin(), m_uploadingList.end(), client);
    if (it == m_uploadingList.end())
        return false;

    m_uploadingList.erase(it);

    if (m_throttler && client->getFileUploadSocket())
        m_throttler->removeFromStandardList(client->getFileUploadSocket());

    if (client->sessionUp() > 0) {
        ++m_successfulUpCount;
        m_totalUploadTime += client->getUpStartTimeDelay() / 1000; // convert ms to seconds
    } else {
        ++m_failedUpCount;
    }

    // Keep track of this client — it has now consumed an upload slot, which is what the
    // per-address queue gate in addClientToQueue counts.
    if (theApp.clientList)
        theApp.clientList->addTrackClient(client);

    client->setAddNextConnect(false);
    client->setUploadState(UploadState::None);
    client->setCollectionUploadSlot(false);

    m_highestNumberOfFullyActivatedSlotsSinceLastCall = 0;

    // Renumber remaining slots
    for (size_t i = 0; i < m_uploadingList.size(); ++i)
        m_uploadingList[i]->setSlotNumber(static_cast<uint32>(i + 1));

    emit uploadEnded(client);
    return true;
}

// ===========================================================================
// removeFromWaitingQueue — MFC CUploadQueue::RemoveFromWaitingQueue
// ===========================================================================

bool UploadQueue::removeFromWaitingQueue(UpDownClient* client)
{
    auto it = std::find(m_waitingList.begin(), m_waitingList.end(), client);
    if (it == m_waitingList.end())
        return false;

    m_waitingList.erase(it);
    client->setAddNextConnect(false);
    client->setUploadState(UploadState::None);
    emit clientRemovedFromQueue(client);
    return true;
}

// ===========================================================================
// checkForTimeOver — MFC CUploadQueue::CheckForTimeOver
// ===========================================================================

bool UploadQueue::checkForTimeOver(const UpDownClient* client)
{
    if (m_waitingList.empty() || client->friendSlot())
        return false;

    // A collection slot is granted for one specific small collection and is worth keeping
    // only while the client is still on it. MFC srchybrid/UploadQueue.cpp:798-808.
    if (client->collectionUploadSlot()) {
        const KnownFile* reqFile = m_sharedFiles
                                 ? m_sharedFiles->getFileByID(client->reqUpFileId())
                                 : nullptr;
        if (!reqFile)
            return true;

        if (Collection::hasCollectionExtension(reqFile->fileName())
            && reqFile->fileSize() < MAXPRIORITYCOLL_SIZE)
        {
            return false;
        }

        if (thePrefs.logUlDlEvents()) {
            logDebug(QStringLiteral("%1: upload session ended — client with a collection slot "
                                    "requested blocks from another file")
                         .arg(client->userName()));
        }
        return true;
    }

    // Session max transfer check
    if (client->queueSessionPayloadUp() > SESSIONMAXTRANS && !forceNewClient())
        return true;

    return false;
}

// ===========================================================================
// updateActiveClientsInfo — MFC CUploadQueue::UpdateActiveClientsInfo
// ===========================================================================

void UploadQueue::updateDatarates()
{
    const uint32 curTick = static_cast<uint32>(getTickCount());

    if (curTick < m_lastCalculatedDataRateTick + 500)
        return;
    m_lastCalculatedDataRateTick = curTick;

    if (m_averageDRList.size() >= 2 && m_averageTickList.back() > m_averageTickList.front()) {
        uint32 duration = m_averageTickList.back() - m_averageTickList.front();
        if (duration > 0) {
            m_datarate = static_cast<uint32>(
                (m_averageDRSum - m_averageDRList.front()) * 1000 / duration);
            if (m_averageFriendDRList.size() >= 2) {
                m_friendDatarate = static_cast<uint32>(
                    (m_averageFriendDRList.back() - m_averageFriendDRList.front()) * 1000 / duration);
            }
        }
    }
}

// ===========================================================================
// process — MFC CUploadQueue::Process (called ~100ms)
// ===========================================================================

void UploadQueue::process()
{
    const uint32 curTick = static_cast<uint32>(getTickCount());

    // Update active clients info from throttler
    if (m_throttler) {
        int tempHighest = m_throttler->getHighestNumberOfFullyActivatedSlotsSinceLastCallAndReset();
        m_highestNumberOfFullyActivatedSlotsSinceLastCall =
            std::min(tempHighest, static_cast<int>(m_uploadingList.size()) + 1);

        // Maintain active clients history (20-second window)
        while (!m_activeClientsTickList.empty() && curTick >= m_activeClientsTickList.front() + SEC2MS(20)) {
            m_activeClientsTickList.pop_front();
            if (!m_activeClientsList.empty()) {
                int removed = m_activeClientsList.front();
                m_activeClientsList.pop_front();
                if (removed > m_maxActiveClients)
                    m_maxActiveClients = removed;
            }
        }

        m_activeClientsList.push_back(m_highestNumberOfFullyActivatedSlotsSinceLastCall);
        m_activeClientsTickList.push_back(curTick);

        if (m_activeClientsList.size() > 1) {
            int tempMax = m_highestNumberOfFullyActivatedSlotsSinceLastCall;
            int tempMaxShort = m_highestNumberOfFullyActivatedSlotsSinceLastCall;
            for (size_t i = 0; i < m_activeClientsList.size(); ++i) {
                if (m_activeClientsList[i] > tempMax)
                    tempMax = m_activeClientsList[i];
                if (m_activeClientsList[i] > tempMaxShort &&
                    curTick < m_activeClientsTickList[i] + SEC2MS(10))
                    tempMaxShort = m_activeClientsList[i];
            }
            m_maxActiveClients = tempMax;
            m_maxActiveClientsShortTime = tempMaxShort;
        } else {
            m_maxActiveClients = m_highestNumberOfFullyActivatedSlotsSinceLastCall;
            m_maxActiveClientsShortTime = m_highestNumberOfFullyActivatedSlotsSinceLastCall;
        }
    }

    // Check if we should accept a new client
    if (forceNewClient())
        addUpNextClient();

    // Process each uploading client
    for (auto it = m_uploadingList.begin(); it != m_uploadingList.end(); ) {
        UpDownClient* cur = *it;

        if (!cur->socket()) {
            // Client without socket
            it = m_uploadingList.erase(it);
            if (m_throttler)
                m_throttler->removeFromStandardList(cur->getFileUploadSocket());
            cur->setUploadState(UploadState::None);
            ++m_failedUpCount;
            emit uploadEnded(cur);
            continue;
        }

        cur->updateUploadingStatisticsData();

        if (checkForTimeOver(cur)) {
            UpDownClient* client = cur;
            it = m_uploadingList.erase(it);
            if (m_throttler && client->getFileUploadSocket())
                m_throttler->removeFromStandardList(client->getFileUploadSocket());
            if (client->sessionUp() > 0)
                ++m_successfulUpCount;
            client->setUploadState(UploadState::None);
            client->sendOutOfPartReqsAndAddToWaitingQueue();
            m_highestNumberOfFullyActivatedSlotsSinceLastCall = 0;
            emit uploadEnded(client);
            continue;
        }

        // Use big send buffer for fast uploads
        if (cur->socket()) {
            EMSocket* sock = cur->getFileUploadSocket();
            if (sock)
                sock->useBigSendBuffer();
        }

        ++it;
    }

    // Save bandwidth data for rate calculation
    if (m_throttler) {
        uint64 sentBytes = m_throttler->getSentBytesSinceLastCallAndReset();
        m_averageDRList.push_back(sentBytes);
        m_averageDRSum += sentBytes;

        // Discard overhead stat
        m_throttler->getSentBytesOverheadSinceLastCallAndReset();

        // Track friend-specific bytes by summing session bytes from friend-slot clients
        uint64 friendBytes = 0;
        for (const auto* client : m_uploadingList) {
            if (client->friendSlot())
                friendBytes += client->sessionUp();
        }
        m_averageFriendDRList.push_back(friendBytes);
        m_averageTickList.push_back(curTick);

        // Keep no more than 30 seconds of data
        while (m_averageTickList.size() > 3 && !m_averageFriendDRList.empty() &&
               curTick >= m_averageTickList.front() + SEC2MS(30)) {
            m_averageDRSum -= m_averageDRList.front();
            m_averageDRList.pop_front();
            m_averageFriendDRList.pop_front();
            m_averageTickList.pop_front();
        }
    }

    updateDatarates();
}

// ===========================================================================
// onReaskFilePing — handle OP_REASKFILEPING from ClientUDPSocket
//
// MFC: CClientUDPSocket::ProcessPacket — OP_REASKFILEPING case
// Packet payload: <filehash 16>
// ===========================================================================

void UploadQueue::onReaskFilePing(const Endpoint& senderEP,
                                   const uint8* data, uint32 size)
{
    if (!data || size < 16)
        return;

    // Keep the Endpoint intact end-to-end. Flattening it to a uint32 loses the family
    // (an IPv6 sender collapses to 0) and mixes host order into a network-order lookup,
    // which is why this handler never resolved a sender and no rank was ever returned.
    UpDownClient* sender = nullptr;
    if (theApp.clientList)
        sender = theApp.clientList->findByEndpoint_UDP(senderEP.address(), senderEP.port());

    // Look up the requested file by hash
    KnownFile* reqFile = nullptr;
    if (theApp.sharedFileList)
        reqFile = theApp.sharedFileList->getFileByID(data);

    if (!reqFile) {
        // Not in shared files — check incomplete downloads with >= 1 complete part
        if (theApp.downloadQueue) {
            auto* partFile = theApp.downloadQueue->fileByID(data);
            if (partFile && static_cast<uint64>(partFile->completedSize()) >= PARTSIZE)
                reqFile = partFile;
        }
    }

    if (!reqFile) {
        if (theApp.clientUDP) {
            auto pkt = std::make_unique<Packet>(OP_FILENOTFOUND, 0, OP_EMULEPROT);
            if (sender)
                theApp.clientUDP->sendPacket(std::move(pkt), senderEP,
                                              sender->shouldReceiveCryptUDPPackets(),
                                              sender->userHash(), false, 0);
            else
                theApp.clientUDP->sendPacket(std::move(pkt), senderEP,
                                              false, nullptr, false, 0);
        }
        return;
    }

    if (sender) {
        // Re-add to queue (updates position, handles reconnect)
        // Note: addClientToQueue() calls incAskedCount() + setLastUpRequest() internally
        addClientToQueue(sender);

        // Build reask ACK with part status + queue rank
        SafeMemFile dataOut;

        if (sender->udpVer() > 3) {
            if (reqFile->isPartFile())
                static_cast<PartFile*>(reqFile)->writePartStatus(dataOut);
            else
                dataOut.writeUInt16(0);
        }

        const uint16 queueRank = static_cast<uint16>(waitingPosition(sender));
        dataOut.writeUInt16(queueRank);

        auto response = std::make_unique<Packet>(dataOut, OP_EMULEPROT, OP_REASKACK);
        if (theApp.clientUDP) {
            theApp.clientUDP->sendPacket(std::move(response), senderEP,
                                          sender->shouldReceiveCryptUDPPackets(),
                                          sender->userHash(), false, 0);
        }
    } else {
        // Unknown client — check if queue is full
        if (theApp.clientUDP) {
            // MFC srchybrid/ClientUDPSocket.cpp:299 — the same pref that caps the queue.
            if (waitingUserCount() + 50 > static_cast<int>(thePrefs.queueSize())) {
                auto pkt = std::make_unique<Packet>(OP_QUEUEFULL, 0, OP_EMULEPROT);
                theApp.clientUDP->sendPacket(std::move(pkt), senderEP,
                                              false, nullptr, false, 0);
            }
        }
    }
}

} // namespace eMule
