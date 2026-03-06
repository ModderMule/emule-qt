#include "pch.h"
/// @file UploadBandwidthThrottler.cpp
/// @brief Thread-based per-socket bandwidth allocation — port of MFC UploadBandwidthThrottler.cpp.
///
/// Replaces Windows CEvent with std::condition_variable, CTypedPtrList with std::list,
/// CArray with std::vector, timeGetTime() with getTickCount().

#include "transfer/UploadBandwidthThrottler.h"
#include "transfer/UploadDiskIOThread.h"
#include "transfer/UploadQueue.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"
#include "utils/TimeUtils.h"


#include <algorithm>
#include <cmath>

namespace eMule {

UploadBandwidthThrottler::UploadBandwidthThrottler(QObject* parent)
    : QThread(parent)
{
    start();
}

UploadBandwidthThrottler::~UploadBandwidthThrottler()
{
    endThread();
}

uint64 UploadBandwidthThrottler::getSentBytesSinceLastCallAndReset()
{
    std::lock_guard lock(m_sendMutex);
    uint64 result = m_sentBytesSinceLastCall;
    m_sentBytesSinceLastCall = 0;
    return result;
}

uint64 UploadBandwidthThrottler::getSentBytesOverheadSinceLastCallAndReset()
{
    std::lock_guard lock(m_sendMutex);
    uint64 result = m_sentBytesOverheadSinceLastCall;
    m_sentBytesOverheadSinceLastCall = 0;
    return result;
}

int UploadBandwidthThrottler::getHighestNumberOfFullyActivatedSlotsSinceLastCallAndReset()
{
    std::lock_guard lock(m_sendMutex);
    int result = m_highestNumberOfFullyActivatedSlots;
    m_highestNumberOfFullyActivatedSlots = 0;
    return result;
}

int UploadBandwidthThrottler::standardListSize() const
{
    std::lock_guard lock(m_sendMutex);
    return static_cast<int>(m_standardOrder.size());
}

void UploadBandwidthThrottler::addToStandardList(int index, ThrottledFileSocket* socket)
{
    if (!socket)
        return;

    std::lock_guard lock(m_sendMutex);
    removeFromStandardListNoLock(socket);

    if (index > static_cast<int>(m_standardOrder.size()))
        index = static_cast<int>(m_standardOrder.size());

    m_standardOrder.insert(m_standardOrder.begin() + index, socket);
}

bool UploadBandwidthThrottler::removeFromStandardList(ThrottledFileSocket* socket)
{
    std::lock_guard lock(m_sendMutex);
    return removeFromStandardListNoLock(socket);
}

bool UploadBandwidthThrottler::removeFromStandardListNoLock(ThrottledFileSocket* socket)
{
    for (auto it = m_standardOrder.begin(); it != m_standardOrder.end(); ++it) {
        if (*it == socket) {
            m_standardOrder.erase(it);
            int listSize = static_cast<int>(m_standardOrder.size());
            if (m_highestNumberOfFullyActivatedSlots > listSize)
                m_highestNumberOfFullyActivatedSlots = listSize;
            return true;
        }
    }
    return false;
}

void UploadBandwidthThrottler::queueForSendingControlPacket(ThrottledControlSocket* socket, bool hasSent)
{
    if (!m_run.load())
        return;

    std::lock_guard lock(m_tempMutex);
    if (hasSent)
        m_tempControlQueueFirst.push_back(socket);
    else
        m_tempControlQueue.push_back(socket);
}

void UploadBandwidthThrottler::removeFromAllQueuesNoLock(ThrottledControlSocket* socket)
{
    if (!m_run.load())
        return;

    // Remove from main control queues
    m_controlQueue.remove(socket);
    m_controlQueueFirst.remove(socket);

    // Remove from temp control queues
    {
        std::lock_guard lock(m_tempMutex);
        m_tempControlQueue.remove(socket);
        m_tempControlQueueFirst.remove(socket);
    }
}

void UploadBandwidthThrottler::removeFromAllQueues(ThrottledFileSocket* socket)
{
    if (!m_run.load())
        return;

    std::lock_guard lock(m_sendMutex);
    removeFromAllQueuesNoLock(socket);
    removeFromStandardListNoLock(socket);
}

void UploadBandwidthThrottler::removeFromAllQueues(ThrottledControlSocket* socket)
{
    std::lock_guard lock(m_sendMutex);
    removeFromAllQueuesNoLock(socket);
}

void UploadBandwidthThrottler::newUploadDataAvailable()
{
    if (m_run.load()) {
        m_dataAvailable.store(true);
        m_dataAvailableCV.notify_one();
    }
}

void UploadBandwidthThrottler::socketAvailable()
{
    if (m_run.load()) {
        m_socketAvailable.store(true);
        m_socketAvailableCV.notify_one();
    }
}

void UploadBandwidthThrottler::endThread()
{
    m_run.store(false);
    pause(false);
    m_dataAvailableCV.notify_all();
    m_socketAvailableCV.notify_all();

    if (isRunning())
        wait();
}

void UploadBandwidthThrottler::pause(bool paused)
{
    m_paused.store(paused);
    if (!paused)
        m_pauseCV.notify_all();
}

uint32 UploadBandwidthThrottler::getSlotLimit(uint32 currentUpSpeed)
{
    uint32 upPerClient = 3 * 1024; // default 3 KiB/s minimum per client

    // if throttler doesn't require another slot, go with a slightly more restrictive method
    if (currentUpSpeed > 49 * 1024) {
        upPerClient += currentUpSpeed / 43;
        if (upPerClient > UPLOAD_CLIENT_MAXDATARATE)
            upPerClient = UPLOAD_CLIENT_MAXDATARATE;
    }

    if (currentUpSpeed > 25 * 1024)
        return std::max(currentUpSpeed / upPerClient, static_cast<uint32>(MIN_UP_CLIENTS_ALLOWED + 3));
    if (currentUpSpeed > 16 * 1024)
        return MIN_UP_CLIENTS_ALLOWED + 2;
    if (currentUpSpeed > 9 * 1024)
        return MIN_UP_CLIENTS_ALLOWED + 1;
    return MIN_UP_CLIENTS_ALLOWED;
}

uint32 UploadBandwidthThrottler::calculateChangeDelta(uint32 numberOfConsecutiveChanges)
{
    static constexpr uint32 deltas[9] =
        {50u, 50u, 128u, 256u, 512u, 512u + 256u, 1024u, 1024u + 256u, 1024u + 512u};
    return deltas[std::min(numberOfConsecutiveChanges, static_cast<uint32>(std::size(deltas) - 1))];
}

void UploadBandwidthThrottler::run()
{
    runInternal();
}

void UploadBandwidthThrottler::runInternal()
{
    int64 realBytesToSpend = 0;
    int rememberedSlotCounter = 0;

    uint32 nEstimatedDataRate = 0;
    int nSlotsBusyLevel = 0;
    uint32 nUploadStartTime = 0;
    uint32 numberOfConsecutiveUpChanges = 0;
    uint32 numberOfConsecutiveDownChanges = 0;
    uint32 changesCount = 0;
    uint32 loopsCount = 0;

    uint32 lastLoopTick = static_cast<uint32>(getTickCount());
    uint32 lastTickReachedBandwidth = lastLoopTick;

    while (m_run.load()) {
        // Pause check
        if (m_paused.load()) {
            std::unique_lock lock(m_pauseMutex);
            m_pauseCV.wait(lock, [this] { return !m_paused.load() || !m_run.load(); });
            if (!m_run.load())
                break;
        }

        uint32 timeSinceLastLoop = static_cast<uint32>(getTickCount()) - lastLoopTick;

        // Get current allowed data rate from preferences
        uint32 allowedDataRate = thePrefs.maxUpload();
        if (allowedDataRate != UNLIMITED)
            allowedDataRate *= 1024; // convert KB/s to bytes/s

        // Check busy level for slots
        uint32 nBusy = 0;
        uint32 nCanSend = 0;

        {
            std::lock_guard lock(m_sendMutex);
            m_dataAvailable.store(false);
            m_socketAvailable.store(false);

            int slotLimit = static_cast<int>(std::max(getSlotLimit(0), 3u));
            int checkCount = std::min(static_cast<int>(m_standardOrder.size()), slotLimit);

            for (int i = checkCount - 1; i >= 0; --i) {
                ThrottledFileSocket* pSocket = m_standardOrder[static_cast<size_t>(i)];
                if (pSocket && pSocket->hasQueues()) {
                    ++nCanSend;
                    nBusy += static_cast<uint32>(pSocket->isBusyExtensiveCheck());
                }
            }
        }

        // When no upload limit has been set, try to guess a good upload limit
        if (thePrefs.maxUpload() == UNLIMITED) {
            ++loopsCount;
            if (nCanSend > 0) {
                const int iBusyFraction = static_cast<int>((nBusy << 5) / nCanSend);
                if (nBusy > 2 && iBusyFraction > 24 && nSlotsBusyLevel < 255)
                    ++nSlotsBusyLevel;
                else if ((nBusy <= 2 || iBusyFraction < 8) && nSlotsBusyLevel > -255)
                    --nSlotsBusyLevel;
            }

            if (nUploadStartTime == 0) {
                if (static_cast<int>(m_standardOrder.size()) >= 3)
                    nUploadStartTime = static_cast<uint32>(getTickCount());
            } else if (static_cast<uint32>(getTickCount()) >= nUploadStartTime + SEC2MS(60)) {
                if (nEstimatedDataRate == 0) {
                    if (nSlotsBusyLevel >= 250) {
                        nEstimatedDataRate = allowedDataRate > 0 ? allowedDataRate : 10 * 1024;
                        nSlotsBusyLevel = -200;
                        changesCount = 0;
                        loopsCount = 0;
                    }
                } else if (nSlotsBusyLevel > 250) {
                    if (changesCount > 500 || (changesCount > 300 && loopsCount > 1000) || loopsCount > 2000)
                        numberOfConsecutiveDownChanges = 0;
                    else
                        ++numberOfConsecutiveDownChanges;
                    uint32 changeDelta = calculateChangeDelta(numberOfConsecutiveDownChanges);
                    if (nEstimatedDataRate < changeDelta + 1024)
                        changeDelta = (nEstimatedDataRate > 1024) ? nEstimatedDataRate - 1024 : 0;
                    nEstimatedDataRate -= changeDelta;
                    numberOfConsecutiveUpChanges = 0;
                    nSlotsBusyLevel = 0;
                    changesCount = 0;
                    loopsCount = 0;
                } else if (nSlotsBusyLevel < -250) {
                    if (changesCount > 500 || (changesCount > 300 && loopsCount > 1000) || loopsCount > 2000)
                        numberOfConsecutiveUpChanges = 0;
                    else
                        ++numberOfConsecutiveUpChanges;
                    uint32 changeDelta = calculateChangeDelta(numberOfConsecutiveUpChanges);
                    nEstimatedDataRate += changeDelta;
                    if (nEstimatedDataRate > allowedDataRate && allowedDataRate != UNLIMITED)
                        nEstimatedDataRate = allowedDataRate;
                    numberOfConsecutiveDownChanges = 0;
                    nSlotsBusyLevel = 0;
                    changesCount = 0;
                    loopsCount = 0;
                }

                if (allowedDataRate > nEstimatedDataRate && allowedDataRate != UNLIMITED)
                    allowedDataRate = nEstimatedDataRate;
            }

            int listSize = static_cast<int>(m_standardOrder.size());
            if (nCanSend == nBusy && listSize > 0 && nSlotsBusyLevel < 125)
                nSlotsBusyLevel = 125;
        }

        uint32 minFragSize;
        uint32 doubleSendSize;
        if (allowedDataRate < 6 * 1024) {
            doubleSendSize = minFragSize = 536;
        } else {
            minFragSize = 1300;
            doubleSendSize = minFragSize * 2;
        }

        constexpr uint32 kTimeBetweenUploadLoops = 1;
        uint32 sleepTime;
        if (allowedDataRate == UNLIMITED || realBytesToSpend >= 1000 || (allowedDataRate | nEstimatedDataRate) == 0) {
            sleepTime = kTimeBetweenUploadLoops;
        } else {
            if (allowedDataRate > 0)
                sleepTime = static_cast<uint32>(std::ceil((1000.0 - realBytesToSpend) / static_cast<double>(allowedDataRate)));
            else
                sleepTime = static_cast<uint32>(std::ceil((doubleSendSize * 1000.0) / static_cast<double>(nEstimatedDataRate)));
            if (sleepTime < kTimeBetweenUploadLoops)
                sleepTime = kTimeBetweenUploadLoops;
        }

        if (timeSinceLastLoop < sleepTime) {
            uint32 dwSleep = sleepTime - timeSinceLastLoop;
            if (nCanSend == 0) {
                std::unique_lock lock(m_dataAvailableMutex);
                m_dataAvailableCV.wait_for(lock, std::chrono::milliseconds(dwSleep),
                    [this] { return m_dataAvailable.load() || !m_run.load(); });
            } else if (nCanSend == nBusy) {
                std::unique_lock lock(m_socketAvailableMutex);
                m_socketAvailableCV.wait_for(lock, std::chrono::milliseconds(dwSleep),
                    [this] { return m_socketAvailable.load() || !m_run.load(); });
            } else {
                sleepMs(dwSleep);
            }
        }

        if (!m_run.load())
            break;

        const uint32 thisLoopTick = static_cast<uint32>(getTickCount());
        timeSinceLastLoop = thisLoopTick - lastLoopTick;

        // Calculate how many bytes we can spend
        int64 bytesToSpend;
        if (allowedDataRate != UNLIMITED) {
            if (timeSinceLastLoop == 0) {
                bytesToSpend = realBytesToSpend / 1000;
            } else {
                if (timeSinceLastLoop >= sleepTime + SEC2MS(2))
                    timeSinceLastLoop = sleepTime + SEC2MS(2);

                realBytesToSpend += static_cast<int64>(allowedDataRate) * static_cast<int64>(timeSinceLastLoop);
                bytesToSpend = realBytesToSpend / 1000;
            }
        } else {
            realBytesToSpend = 0;
            bytesToSpend = INT32_MAX;
        }

        lastLoopTick = thisLoopTick;

        if (bytesToSpend > 0 || allowedDataRate == 0) {
            std::lock_guard lock(m_sendMutex);

            // Move temp queues to main queues
            {
                std::lock_guard tempLock(m_tempMutex);
                while (!m_tempControlQueueFirst.empty()) {
                    m_controlQueueFirst.push_back(m_tempControlQueueFirst.front());
                    m_tempControlQueueFirst.pop_front();
                }
                while (!m_tempControlQueue.empty()) {
                    m_controlQueue.push_back(m_tempControlQueue.front());
                    m_tempControlQueue.pop_front();
                }
            }

            uint64 spentBytes = 0;
            uint64 spentOverhead = 0;
            bool bNeedMoreData = false;

            // Send control packets first
            while ((bytesToSpend > 0 && spentBytes < static_cast<uint64>(bytesToSpend)) ||
                   (allowedDataRate == 0 && spentBytes < 500)) {
                ThrottledControlSocket* socket = nullptr;
                if (!m_controlQueueFirst.empty()) {
                    socket = m_controlQueueFirst.front();
                    m_controlQueueFirst.pop_front();
                } else if (!m_controlQueue.empty()) {
                    socket = m_controlQueue.front();
                    m_controlQueue.pop_front();
                } else {
                    break;
                }

                if (socket) {
                    uint32 sendLimit = allowedDataRate > 0
                        ? static_cast<uint32>(bytesToSpend - static_cast<int64>(spentBytes))
                        : 1u;
                    SocketSentBytes sent = socket->sendControlData(sendLimit, minFragSize);
                    spentBytes += sent.sentBytesStandardPackets + sent.sentBytesControlPackets;
                    spentOverhead += sent.sentBytesControlPackets;
                }
            }

            // Trickle: send to sockets that haven't sent in over 1 second
            int listSize = static_cast<int>(m_standardOrder.size());
            for (int slotCounter = 0; slotCounter < listSize; ++slotCounter) {
                ThrottledFileSocket* socket = m_standardOrder[static_cast<size_t>(slotCounter)];
                if (socket) {
                    if (!socket->isBusyQuickCheck() && thisLoopTick >= socket->getLastCalledSend() + SEC2MS(1)) {
                        uint32 neededBytes = socket->getNeededBytes();
                        if (neededBytes > 0) {
                            SocketSentBytes sent = socket->sendFileAndControlData(neededBytes, minFragSize);
                            uint32 lastSpent = sent.sentBytesControlPackets + sent.sentBytesStandardPackets;
                            spentBytes += lastSpent;
                            spentOverhead += sent.sentBytesControlPackets;
                            if (sent.sentBytesStandardPackets > 0 && !socket->isEnoughFileDataQueued(EMBLOCKSIZE))
                                bNeedMoreData = true;
                            if (lastSpent > 0 && slotCounter < m_highestNumberOfFullyActivatedSlots)
                                m_highestNumberOfFullyActivatedSlots = slotCounter;
                        }
                    }
                }
            }

            // Equal bandwidth for all slots — use actual targetClientDataRate from UploadQueue
            uint32 targetDataRate = m_uploadQueue
                ? m_uploadQueue->targetClientDataRate(false)
                : 3u * 1024u;
            int maxSlot = std::min(listSize,
                static_cast<int>(allowedDataRate != UNLIMITED ? allowedDataRate / targetDataRate : static_cast<uint32>(listSize)));

            if (maxSlot > m_highestNumberOfFullyActivatedSlots)
                m_highestNumberOfFullyActivatedSlots = maxSlot;

            for (int maxCounter = 0;
                 maxCounter < std::min(maxSlot, listSize) && bytesToSpend > 0 && spentBytes < static_cast<uint64>(bytesToSpend);
                 ++maxCounter) {
                if (rememberedSlotCounter >= listSize || rememberedSlotCounter >= maxSlot)
                    rememberedSlotCounter = 0;

                ThrottledFileSocket* socket = m_standardOrder[static_cast<size_t>(rememberedSlotCounter)];
                if (socket && !socket->isBusyQuickCheck()) {
                    uint32 sendAmount = std::min(
                        std::max(doubleSendSize, maxSlot > 0 ? static_cast<uint32>(bytesToSpend / maxSlot) : doubleSendSize),
                        static_cast<uint32>(bytesToSpend - static_cast<int64>(spentBytes)));
                    SocketSentBytes sent = socket->sendFileAndControlData(sendAmount, doubleSendSize);
                    if (sent.sentBytesStandardPackets > 0) {
                        spentBytes += sent.sentBytesStandardPackets;
                        if (!socket->isEnoughFileDataQueued(EMBLOCKSIZE))
                            bNeedMoreData = true;
                    }
                    spentBytes += sent.sentBytesControlPackets;
                    spentOverhead += sent.sentBytesControlPackets;
                }
                ++rememberedSlotCounter;
            }

            // Full priority: remaining bandwidth first-come first-served
            for (int slotCounter = 0;
                 slotCounter < listSize && bytesToSpend > 0 && spentBytes < static_cast<uint64>(bytesToSpend);
                 ++slotCounter) {
                ThrottledFileSocket* socket = m_standardOrder[static_cast<size_t>(slotCounter)];
                if (socket && !socket->isBusyQuickCheck()) {
                    uint32 bytesToSpendTemp = static_cast<uint32>(bytesToSpend - static_cast<int64>(spentBytes));
                    SocketSentBytes sent = socket->sendFileAndControlData(
                        std::max(bytesToSpendTemp, doubleSendSize), doubleSendSize);
                    uint32 lastSpent = sent.sentBytesControlPackets + sent.sentBytesStandardPackets;
                    spentBytes += lastSpent;
                    spentOverhead += sent.sentBytesControlPackets;
                    if (sent.sentBytesStandardPackets > 0 && !socket->isEnoughFileDataQueued(EMBLOCKSIZE))
                        bNeedMoreData = true;

                    if (slotCounter >= m_highestNumberOfFullyActivatedSlots &&
                        (lastSpent < bytesToSpendTemp || lastSpent >= doubleSendSize))
                        m_highestNumberOfFullyActivatedSlots = slotCounter + 1;
                }
            }

            realBytesToSpend -= static_cast<int64>(spentBytes) * 1000;

            // Limit carry-over
            int64 newRealBytesToSpend = -(static_cast<int64>(listSize) + 1) * minFragSize * 1000;
            if (realBytesToSpend < newRealBytesToSpend) {
                realBytesToSpend = newRealBytesToSpend;
                lastTickReachedBandwidth = thisLoopTick;
            } else if (realBytesToSpend > 999) {
                realBytesToSpend = 999;
                if (thisLoopTick >= lastTickReachedBandwidth + std::max(500u, timeSinceLastLoop) * 2) {
                    m_highestNumberOfFullyActivatedSlots = listSize + 1;
                    lastTickReachedBandwidth = thisLoopTick;
                }
            } else {
                lastTickReachedBandwidth = thisLoopTick;
            }

            // Accumulate statistics
            m_sentBytesSinceLastCall += spentBytes;
            m_sentBytesOverheadSinceLastCall += spentOverhead;

            // Signal disk IO thread to prepare more data when buffers are running low
            if (bNeedMoreData && m_diskIOThread)
                m_diskIOThread->wakeUp();
        }
    }

    // Cleanup on exit
    {
        std::lock_guard lock(m_sendMutex);
        std::lock_guard tempLock(m_tempMutex);
        m_tempControlQueue.clear();
        m_tempControlQueueFirst.clear();
        m_controlQueue.clear();
        m_controlQueueFirst.clear();
        m_standardOrder.clear();
    }
}

} // namespace eMule
