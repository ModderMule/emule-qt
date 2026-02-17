/// @file UploadQueue.cpp
/// @brief Upload queue manager — port of MFC UploadQueue.cpp.
///
/// Upload slot allocation, waiting queue management, score-based selection,
/// data rate tracking, and session time-over checks.

#include "transfer/UploadQueue.h"
#include "transfer/UploadBandwidthThrottler.h"
#include "transfer/UploadDiskIOThread.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/SharedFileList.h"
#include "net/EMSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/TimeUtils.h"

#include <QDebug>

#include <algorithm>

namespace eMule {

UploadQueue::UploadQueue(QObject* parent)
    : QObject(parent)
{
}

UploadQueue::~UploadQueue() = default;

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
        if (client->userIP() == ip)
            return client;
    }
    return nullptr;
}

int UploadQueue::waitingPosition(const UpDownClient* client) const
{
    if (!isOnUploadQueue(client))
        return 0;

    uint32 myScore = client->score(false);
    int rank = 1;
    for (const auto* other : m_waitingList) {
        if (other->score(false) > myScore)
            ++rank;
    }
    return rank;
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
    uint32 bestScore = 0;
    uint32 bestLowScore = 0;
    UpDownClient* newClient = nullptr;
    UpDownClient* lowClient = nullptr;
    const uint32 curTick = static_cast<uint32>(getTickCount());

    for (auto it = m_waitingList.begin(); it != m_waitingList.end(); ) {
        UpDownClient* cur = *it;

        // Purge stale clients (not seen in MAX_PURGEQUEUETIME = 1 hour)
        bool stale = (curTick >= cur->askedCount() + MAX_PURGEQUEUETIME);
        bool noFile = m_sharedFiles && !m_sharedFiles->getFileByID(cur->reqUpFileId());

        if (stale || noFile) {
            cur->clearWaitStartTime();
            it = m_waitingList.erase(it);
            cur->setUploadState(UploadState::None);
            cur->setAddNextConnect(false);
            emit clientRemovedFromQueue(cur);
            continue;
        }

        uint32 curScore = cur->score(false);
        if (curScore > bestScore) {
            if (!cur->hasLowID() || (cur->socket() && cur->socket()->isConnected())) {
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

    if (lowClient && bestLowScore > bestScore)
        lowClient->setAddNextConnect(true);

    return newClient;
}

// ===========================================================================
// acceptNewClient — MFC CUploadQueue::AcceptNewClient
// ===========================================================================

bool UploadQueue::acceptNewClient(bool addOnNextConnect) const
{
    int curUploadSlots = static_cast<int>(m_uploadingList.size());
    if (addOnNextConnect && curUploadSlots > 0)
        --curUploadSlots;

    if (curUploadSlots < std::max(static_cast<int>(MIN_UP_CLIENTS_ALLOWED), 4))
        return true;
    if (curUploadSlots >= MAX_UP_CLIENTS_ALLOWED)
        return false;

    uint32 maxSpeed = thePrefs.maxUpload();
    uint32 tgtRate = targetClientDataRate(false);
    uint32 minTgtRate = targetClientDataRate(true);

    if (static_cast<uint32>(curUploadSlots) >= m_datarate / minTgtRate ||
        static_cast<uint32>(curUploadSlots) >= maxSpeed * 1024 / tgtRate)
        return false;

    return true;
}

// ===========================================================================
// forceNewClient — MFC CUploadQueue::ForceNewClient
// ===========================================================================

bool UploadQueue::forceNewClient(bool allowEmptyWaitingQueue)
{
    if (!allowEmptyWaitingQueue && m_waitingList.empty())
        return false;

    int curUploadSlots = static_cast<int>(m_uploadingList.size());
    if (curUploadSlots < MIN_UP_CLIENTS_ALLOWED)
        return true;

    const uint32 curTick = static_cast<uint32>(getTickCount());
    if (curTick < m_lastStartUpload + SEC2MS(1) && m_datarate < 102400)
        return false;

    if (!acceptNewClient())
        return false;

    uint32 maxSpeed = thePrefs.maxUpload();
    uint32 upPerClient = targetClientDataRate(false);

    if (maxSpeed > 49) {
        upPerClient += m_datarate / 43;
        if (upPerClient > UPLOAD_CLIENT_MAXDATARATE)
            upPerClient = UPLOAD_CLIENT_MAXDATARATE;
    }

    if (maxSpeed == UNLIMITED) {
        if (static_cast<uint32>(curUploadSlots) < m_datarate / upPerClient)
            return true;
    } else {
        uint32 nMaxSlots;
        if (maxSpeed > 25)
            nMaxSlots = std::max((maxSpeed * 1024) / upPerClient,
                                 static_cast<uint32>(MIN_UP_CLIENTS_ALLOWED + 3));
        else if (maxSpeed > 16)
            nMaxSlots = MIN_UP_CLIENTS_ALLOWED + 2;
        else if (maxSpeed > 9)
            nMaxSlots = MIN_UP_CLIENTS_ALLOWED + 1;
        else
            nMaxSlots = MIN_UP_CLIENTS_ALLOWED;

        if (static_cast<uint32>(curUploadSlots) < nMaxSlots)
            return true;
    }

    return m_highestNumberOfFullyActivatedSlotsSinceLastCall >
           static_cast<int>(m_uploadingList.size());
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

    // Add to throttler
    if (m_throttler && sock)
        m_throttler->addToStandardList(static_cast<int>(m_uploadingList.size()), sock);

    m_uploadingList.push_back(newClient);
    newClient->setSlotNumber(static_cast<uint32>(m_uploadingList.size()));

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

bool UploadQueue::addClientToQueue(UpDownClient* client, bool ignoreTimeLimit)
{
    if (!client)
        return false;

    client->incAskedCount();

    if (!ignoreTimeLimit)
        client->addRequestCount(client->reqUpFileId());

    if (client->isBanned())
        return false;

    // Check for duplicates and IP limits
    uint16 sameIPCount = 0;
    for (auto it = m_waitingList.begin(); it != m_waitingList.end(); ++it) {
        UpDownClient* cur = *it;
        if (cur == client) {
            // Already in queue — handle lowID reconnect
            if (client->addNextConnect() && acceptNewClient(true)) {
                client->setAddNextConnect(false);
                removeFromWaitingQueue(client);
                addUpNextClient(client);
            } else {
                client->sendRankingInfo();
            }
            return true;
        }
        if (client->compare(cur)) {
            // Duplicate client detected — skip
            return false;
        }
        if (client->userIP() == cur->userIP())
            ++sameIPCount;
    }

    if (sameIPCount >= 3)
        return false;

    // If already downloading, just send accept
    if (isDownloading(client)) {
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

} // namespace eMule
