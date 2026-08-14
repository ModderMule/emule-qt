#include "pch.h"
/// @file UploadBandwidthThrottler.cpp
/// @brief Thread-based per-socket bandwidth allocation — port of MFC UploadBandwidthThrottler.cpp.
///
/// Replaces Windows CEvent with std::condition_variable, CTypedPtrList with std::list,
/// CArray with std::vector, timeGetTime() with getTickCount().

#include "transfer/UploadBandwidthThrottler.h"
#include "transfer/UploadDiskIOThread.h"
#include "transfer/UploadQueue.h"
#include "app/AppContext.h"
#include "net/LastCommonRouteFinder.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/TimeUtils.h"



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

    {
        std::lock_guard lock(m_sendMutex);
        removeFromStandardListNoLock(socket);

        if (index > static_cast<int>(m_standardOrder.size()))
            index = static_cast<int>(m_standardOrder.size());

        m_standardOrder.insert(m_standardOrder.begin() + index, socket);
    }

    // A new upload slot may need the trickle loop before any standard packet is
    // queued — the OP_ACCEPTUPLOADREQ that precedes it is a *control* packet, which
    // does not set wakeThrottler in EMSocket::sendPacket().
    signalWorkAvailable();
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

void UploadBandwidthThrottler::queueForSendingControlPacket(ThrottledControlSocket* socket)
{
    if (!m_run.load())
        return;

    {
        std::lock_guard lock(m_tempMutex);
        m_tempControlQueue.push_back(socket);
    }

    // All Kad and server UDP traffic arrives here (ClientUDPSocket, UDPSocket).
    // Without this the idle wait would add up to kIdleSleepMs of datagram latency.
    signalWorkAvailable();
}

void UploadBandwidthThrottler::removeFromAllQueuesNoLock(ThrottledControlSocket* socket)
{
    if (!m_run.load())
        return;

    // Remove from main control queues
    m_controlQueue.remove(socket);

    // Remove from temp control queues
    {
        std::lock_guard lock(m_tempMutex);
        m_tempControlQueue.remove(socket);
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
    signalWorkAvailable();
}

void UploadBandwidthThrottler::socketAvailable()
{
    if (!m_run.load())
        return;
    {
        std::lock_guard lock(m_wakeMutex);
        m_socketAvailable.store(true);
    }
    m_wakeCV.notify_all();
}

void UploadBandwidthThrottler::endThread()
{
    // m_run must go false under m_wakeMutex too, or the send loop can miss it and
    // sit out the full idle timeout before noticing shutdown.
    {
        std::lock_guard lock(m_wakeMutex);
        m_run.store(false);
    }
    m_wakeCV.notify_all();
    pause(false);

    if (isRunning())
        wait();
}

void UploadBandwidthThrottler::pause(bool paused)
{
    m_paused.store(paused);
    if (!paused)
        m_pauseCV.notify_all();
}

uint32 UploadBandwidthThrottler::getSlotLimit(uint32 currentUpSpeed) const
{
    uint32 upPerClient = m_uploadQueue
        ? m_uploadQueue->targetClientDataRate(true)
        : 3u * 1024u;

    // eMule 2026 bandwidth: tiered upPerClient scaling for modern speeds. MFC default: single tier at >49 KB/s with /43 divisor.
    if (currentUpSpeed > 500 * 1024) {
        upPerClient += currentUpSpeed / 20;
        if (upPerClient > UPLOAD_CLIENT_MAXDATARATE)
            upPerClient = UPLOAD_CLIENT_MAXDATARATE;
    } else if (currentUpSpeed > 200 * 1024) {
        upPerClient += currentUpSpeed / 30;
        if (upPerClient > UPLOAD_CLIENT_MAXDATARATE)
            upPerClient = UPLOAD_CLIENT_MAXDATARATE;
    } else if (currentUpSpeed > 49 * 1024) {
        upPerClient += currentUpSpeed / 43;
        if (upPerClient > UPLOAD_CLIENT_MAXDATARATE)
            upPerClient = UPLOAD_CLIENT_MAXDATARATE;
    }

    // eMule 2026 bandwidth: higher slot floors for broadband. MFC default: max tier at >25 KB/s.
    if (currentUpSpeed > 200 * 1024)
        return std::max(currentUpSpeed / upPerClient, static_cast<uint32>(MIN_UP_CLIENTS_ALLOWED + 5));
    if (currentUpSpeed > 100 * 1024)
        return std::max(currentUpSpeed / upPerClient, static_cast<uint32>(MIN_UP_CLIENTS_ALLOWED + 4));
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
    // eMule 2026 bandwidth: larger steps for faster convergence at high bandwidth. MFC default: 9 entries, max 1536.
    static constexpr uint32 deltas[12] =
        {50u, 50u, 128u, 256u, 512u, 768u, 1024u, 1280u, 1536u, 2048u, 3072u, 4096u};
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
    bool recentlySentData = false;

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
        m_loopIterations.fetch_add(1, std::memory_order_relaxed);

        // Pause check
        if (m_paused.load()) {
            std::unique_lock lock(m_pauseMutex);
            m_pauseCV.wait(lock, [this] { return !m_paused.load() || !m_run.load(); });
            if (!m_run.load())
                break;
        }

        uint32 timeSinceLastLoop = static_cast<uint32>(getTickCount()) - lastLoopTick;

        // Get current allowed data rate — prefer USS-aware value
        uint32 allowedDataRate = 0;
        if (theApp.lastCommonRouteFinder)
            allowedDataRate = theApp.lastCommonRouteFinder->getUpload();
        if (allowedDataRate == 0) {
            // USS not running or returned 0 — fall back to prefs. maxUploadLimit(), not
            // maxUpload(): the raw pref stores "no limit" as 0, which would survive the
            // *1024 as 0 and starve every send loop below down to the 1 Hz trickle.
            allowedDataRate = thePrefs.maxUploadLimit();
            if (allowedDataRate != UNLIMITED)
                allowedDataRate *= 1024;
        }

        // Check busy level for slots
        uint32 nBusy = 0;
        uint32 nCanSend = 0;
        bool standardListEmpty = false;
        bool controlQueuesEmpty = false;

        {
            std::lock_guard lock(m_sendMutex);
            // Reset the flags BEFORE snapshotting the queues. Producers publish their
            // work and only then set the flag, so either the reset wins the race and
            // this snapshot may miss the work but the flag is set again (no block), or
            // the set wins and the push necessarily precedes this snapshot.
            m_dataAvailable.store(false);
            m_socketAvailable.store(false);

            uint32 currentDatarate = m_uploadQueue ? m_uploadQueue->datarate() : 0;
            int slotLimit = static_cast<int>(std::max(getSlotLimit(currentDatarate), 3u));
            int checkCount = std::min(static_cast<int>(m_standardOrder.size()), slotLimit);

            for (int i = checkCount - 1; i >= 0; --i) {
                ThrottledFileSocket* pSocket = m_standardOrder[static_cast<size_t>(i)];
                if (pSocket && pSocket->hasQueues()) {
                    ++nCanSend;
                    nBusy += static_cast<uint32>(pSocket->isBusyExtensiveCheck());
                }
            }

            // Idleness test for the wait below. It keys off the *whole* standard list,
            // not nCanSend: nCanSend only scans the first slotLimit entries, while the
            // trickle / equal-bandwidth / full-priority loops all walk the full list.
            standardListEmpty = m_standardOrder.empty();
            controlQueuesEmpty = m_controlQueue.empty();
            if (controlQueuesEmpty) {
                std::lock_guard tempLock(m_tempMutex);  // same nesting as the drain below
                controlQueuesEmpty = m_tempControlQueue.empty();
            }
        }

        // When no upload limit has been set, try to guess a good upload limit.
        // maxUploadLimit(), not maxUpload(): the raw pref never equals UNLIMITED, which
        // left this whole block unreachable (and changesCount below never incremented).
        if (thePrefs.maxUploadLimit() == UNLIMITED) {
            ++loopsCount;
            if (nCanSend > 0) {
                const int iBusyFraction = static_cast<int>((nBusy << 5) / nCanSend);
                // changesCount counts busy-level movements — it damps the change-delta
                // ramp below once the estimate starts hunting. srchybrid:413,418.
                if (nBusy > 2 && iBusyFraction > 24 && nSlotsBusyLevel < 255) {
                    ++nSlotsBusyLevel;
                    ++changesCount;
                } else if ((nBusy <= 2 || iBusyFraction < 8) && nSlotsBusyLevel > -255) {
                    --nSlotsBusyLevel;
                    ++changesCount;
                }
            }

            if (nUploadStartTime == 0) {
                if (static_cast<int>(m_standardOrder.size()) >= 3)
                    nUploadStartTime = static_cast<uint32>(getTickCount());
            } else if (static_cast<uint32>(getTickCount()) >= nUploadStartTime + SEC2MS(60)) {
                if (nEstimatedDataRate == 0) {
                    if (nSlotsBusyLevel >= 250) {
                        // MFC seeds straight from the queue datarate (srchybrid:430); the
                        // fallbacks are ours, for the case where the queue has no reading
                        // yet. Never seed UNLIMITED — the deltas below top out at 4 KB per
                        // change, so an estimate starting at 4 GB/s would never converge.
                        nEstimatedDataRate = (m_uploadQueue && m_uploadQueue->datarate() > 0)
                            ? m_uploadQueue->datarate()
                            : ((allowedDataRate > 0 && allowedDataRate != UNLIMITED)
                                   ? allowedDataRate
                                   : 10u * 1024u);
                        nSlotsBusyLevel = -200;
                        changesCount = 0;
                        loopsCount = 0;
                        if (thePrefs.verbose())
                            logDebug(QStringLiteral("Throttler: initial guessed upload limit %1 KB/s")
                                         .arg(nEstimatedDataRate / 1024.0, 0, 'f', 1));
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
                    if (thePrefs.verbose())
                        logDebug(QStringLiteral("Throttler: REDUCED guessed limit #%1 by %2 bytes to %3 KB/s "
                                                "(changes %4, loops %5)")
                                     .arg(numberOfConsecutiveDownChanges).arg(changeDelta)
                                     .arg(nEstimatedDataRate / 1024.0, 0, 'f', 1)
                                     .arg(changesCount).arg(loopsCount));
                    changesCount = 0;
                    loopsCount = 0;
                } else if (nSlotsBusyLevel < -250) {
                    if (changesCount > 500 || (changesCount > 300 && loopsCount > 1000) || loopsCount > 2000)
                        numberOfConsecutiveUpChanges = 0;
                    else
                        ++numberOfConsecutiveUpChanges;
                    uint32 changeDelta = calculateChangeDelta(numberOfConsecutiveUpChanges);
                    nEstimatedDataRate += changeDelta;
                    // Don't raise the estimate above the rate we are actually allowed to
                    // send at. No UNLIMITED guard — nothing exceeds UINT32_MAX, so this is
                    // already a no-op in the unlimited case (srchybrid:465-470).
                    if (nEstimatedDataRate > allowedDataRate)
                        nEstimatedDataRate = allowedDataRate;
                    numberOfConsecutiveDownChanges = 0;
                    nSlotsBusyLevel = 0;
                    if (thePrefs.verbose())
                        logDebug(QStringLiteral("Throttler: INCREASED guessed limit #%1 by %2 bytes to %3 KB/s "
                                                "(changes %4, loops %5)")
                                     .arg(numberOfConsecutiveUpChanges).arg(changeDelta)
                                     .arg(nEstimatedDataRate / 1024.0, 0, 'f', 1)
                                     .arg(changesCount).arg(loopsCount));
                    changesCount = 0;
                    loopsCount = 0;
                }

                // The guessed limit replaces "unlimited" — this is the whole point of the
                // block, so it must NOT be skipped when allowedDataRate is UNLIMITED
                // (srchybrid:481-482).
                if (allowedDataRate > nEstimatedDataRate)
                    allowedDataRate = nEstimatedDataRate;
            }

            int listSize = static_cast<int>(m_standardOrder.size());
            if (nCanSend == nBusy && listSize > 0 && nSlotsBusyLevel < 125)
                nSlotsBusyLevel = 125;
        }

        // eMule 2026 bandwidth: larger fragments for modern speeds reduce per-loop overhead.
        // MFC default: two tiers, threshold at 6 KB/s (minFrag 536 or 1300).
        uint32 minFragSize;
        uint32 doubleSendSize;
        if (allowedDataRate > 1024 * 1024) {
            minFragSize = 8000;
            doubleSendSize = 16000;
        } else if (allowedDataRate > 100 * 1024) {
            minFragSize = 4000;
            doubleSendSize = 8000;
        } else if (allowedDataRate < 6 * 1024) {
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
                sleepTime = static_cast<uint32>(std::ceil((1000.0 - static_cast<double>(realBytesToSpend)) / static_cast<double>(allowedDataRate)));
            else
                sleepTime = static_cast<uint32>(std::ceil((doubleSendSize * 1000.0) / static_cast<double>(nEstimatedDataRate)));
            if (sleepTime < kTimeBetweenUploadLoops)
                sleepTime = kTimeBetweenUploadLoops;
        }

        // Idle cadence. MFC polls at 1 ms unconditionally (TIME_BETWEEN_UPLOAD_LOOPS,
        // srchybrid/UploadBandwidthThrottler.cpp:501); on macOS that trips the kernel
        // wakeup monitor, which allows 150 wakes/s sustained. Every producer now
        // signals m_wakeCV, so the long timeout is a safety net rather than the
        // delivery mechanism — it costs no latency. The 1 ms cadence is kept verbatim
        // the moment there is anything at all in the throttler.
        constexpr uint32 kIdleSleepMs = 100;

        if (timeSinceLastLoop < sleepTime) {
            uint32 dwSleep = sleepTime - timeSinceLastLoop;
            if (nCanSend == 0 && !recentlySentData) {
                // No active sockets and nothing sent recently — wait for new data.
                // std::max keeps the bandwidth pacing floor intact.
                if (standardListEmpty && controlQueuesEmpty)
                    dwSleep = std::max(dwSleep, kIdleSleepMs);
                std::unique_lock lock(m_wakeMutex);
                m_wakeCV.wait_for(lock, std::chrono::milliseconds(dwSleep),
                    [this] { return m_dataAvailable.load() || m_socketAvailable.load() || !m_run.load(); });
            } else if (nCanSend == nBusy && nCanSend > 0) {
                std::unique_lock lock(m_wakeMutex);
                m_wakeCV.wait_for(lock, std::chrono::milliseconds(dwSleep),
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
                if (m_controlQueue.empty())
                    break;
                ThrottledControlSocket* socket = m_controlQueue.front();
                m_controlQueue.pop_front();

                if (socket) {
                    uint32 sendLimit = allowedDataRate > 0
                        ? static_cast<uint32>(bytesToSpend - static_cast<int64>(spentBytes))
                        : 1u;
                    SocketSentBytes sent = socket->sendControlData(sendLimit, minFragSize);
                    uint32 totalSent = sent.sentBytesStandardPackets + sent.sentBytesControlPackets;
                    spentBytes += totalSent;
                    spentOverhead += sent.sentBytesControlPackets;
                    // Re-queue if the socket still has unsent control data (e.g. the
                    // kernel buffer was full from data packets, or the byte budget ran
                    // out mid-drain). Ask the socket rather than inferring it from
                    // "sent 0 bytes": queueForSendingControlPacket() enqueues one entry
                    // per packet while sendControlData() drains the socket's whole
                    // queue, so every entry after the first legitimately sends nothing.
                    // Re-queueing on totalSent == 0 alone left m_controlQueue
                    // permanently non-empty, which kept this loop churning at 1 kHz and
                    // made throttler idleness undetectable. MFC drops the socket
                    // unconditionally (srchybrid/UploadBandwidthThrottler.cpp:588-593),
                    // which instead strands datagrams when the budget runs out.
                    if (!sent.success) {
                        m_controlQueue.push_back(socket);
                        break;
                    }
                    if (socket->hasControlQueue()) {
                        m_controlQueue.push_back(socket);
                        // No progress — let data packets drain and retry next loop.
                        if (totalSent == 0)
                            break;
                    }
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
                ? m_uploadQueue->targetClientDataRate(true)
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

            recentlySentData = (spentBytes > 0);
        } else {
            recentlySentData = false;
        }
    }

    // Cleanup on exit
    {
        std::lock_guard lock(m_sendMutex);
        std::lock_guard tempLock(m_tempMutex);
        m_tempControlQueue.clear();
        m_controlQueue.clear();
        m_standardOrder.clear();
    }
}

void UploadBandwidthThrottler::signalWorkAvailable()
{
    if (!m_run.load())
        return;
    {
        // The store must happen under m_wakeMutex. The send loop holds that mutex
        // across both its predicate evaluation and its block, so this can no longer
        // slip in between the two and be lost.
        std::lock_guard lock(m_wakeMutex);
        m_dataAvailable.store(true);
    }
    // notify_all rather than notify_one: there is one waiter today, so the cost is
    // nil, and it stays correct if a second one is ever added.
    m_wakeCV.notify_all();
}

} // namespace eMule
