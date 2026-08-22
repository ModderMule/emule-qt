#include "pch.h"
/// @file Statistics.cpp
/// @brief Global transfer statistics — implementation.

#include "stats/Statistics.h"

#include "client/ClientStateDefs.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"
#include "utils/TimeUtils.h"

#include <algorithm>

namespace eMule {

// 40-second window for overhead rate averaging (matches MFC MAXAVERAGETIME)
inline constexpr uint32 kMaxAverageTime = SEC2MS(40);

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Statistics::Statistics(QObject* parent)
    : QObject(parent)
{
}

Statistics::~Statistics() = default;

// ---------------------------------------------------------------------------
// Initialization — load cumulative rates from preferences
// ---------------------------------------------------------------------------

void Statistics::init(Preferences& prefs)
{
    m_prefs = &prefs;

    m_maxCumDown    = prefs.connMaxDownRate();
    m_cumUpAvg      = prefs.connAvgUpRate();
    m_maxCumDownAvg = prefs.connMaxAvgDownRate();
    m_cumDownAvg    = prefs.connAvgDownRate();
    m_maxCumUpAvg   = prefs.connMaxAvgUpRate();
    m_maxCumUp      = prefs.connMaxUpRate();

    // Upgrade path. cumRunTime was never accumulated while the session clock was
    // broken, so an existing install has hours of cumulative transfer time against
    // zero run time and every percentage under Cumulative reads in the thousands.
    // The daemon cannot have transferred for longer than it was running, so raise
    // the run time to that lower bound instead of inventing one.
    if (const uint64 lowerBound = std::max({prefs.cumTransferTime(), prefs.cumUploadTime(),
                                            prefs.cumDownloadTime(), prefs.cumServerDuration()});
        prefs.cumRunTime() < lowerBound)
        prefs.setCumRunTime(lowerBound);

    rebaseCumulative(prefs);

    // Fresh install: nothing has ever been counted, so the statistics are "as of
    // now" rather than "never reset" (MFC: srchybrid/Preferences.cpp:1381).
    if (prefs.statsLastReset() == 0 && prefs.cumTotalUploaded() == 0
        && prefs.cumTotalDownloaded() == 0)
        prefs.setStatsLastReset(static_cast<uint64>(QDateTime::currentSecsSinceEpoch()));

    // The session clock starts here (MFC: CemuleDlg::OnInitDialog,
    // srchybrid/EmuleDlg.cpp:375).  Guarded, so re-initialising to pick up new
    // preferences does not silently restart the uptime.
    if (m_startTick == 0)
        m_startTick = getTickCount();
}

uint32 Statistics::uptimeSecs() const
{
    if (m_startTick == 0)
        return 0;
    return static_cast<uint32>((getTickCount() - m_startTick) / SEC2MS(1));
}

// ---------------------------------------------------------------------------
// Rate history recording
// ---------------------------------------------------------------------------

void Statistics::recordRate()
{
    if (m_transferStartTime == 0)
        return;

    const auto curTick = static_cast<uint32>(getTickCount());
    m_downRateHistory.push_front(RateEntry{m_sessionReceivedBytes.load(), curTick});
    m_upRateHistory.push_front(RateEntry{m_sessionSentBytes.load(), curTick});

    const uint32 avg = m_prefs ? MIN2MS(m_prefs->statsAverageMinutes()) : MIN2MS(5);
    if (curTick > avg) {
        const uint32 cutoff = curTick - avg;
        while (!m_downRateHistory.empty() && cutoff > m_downRateHistory.back().timestamp)
            m_downRateHistory.pop_back();
        while (!m_upRateHistory.empty() && cutoff > m_upRateHistory.back().timestamp)
            m_upRateHistory.pop_back();
    }
}

// ---------------------------------------------------------------------------
// Average rate calculation
// ---------------------------------------------------------------------------

float Statistics::avgDownloadRate(AverageType type) const
{
    switch (type) {
    case AverageType::Session:
        if (m_transferStartTime > 0) {
            const auto running = (static_cast<uint32>(getTickCount()) - m_transferStartTime) / SEC2MS(1);
            if (running >= 5)
                return static_cast<float>(m_sessionReceivedBytes.load()) / 1024.0f / static_cast<float>(running);
        }
        return 0.0f;

    case AverageType::Total:
        if (m_transferStartTime > 0) {
            const auto running = (static_cast<uint32>(getTickCount()) - m_transferStartTime) / SEC2MS(1);
            if (running >= 5) {
                const float connAvg = m_prefs ? m_prefs->connAvgDownRate() : 0.0f;
                return (static_cast<float>(m_sessionReceivedBytes.load()) / 1024.0f / static_cast<float>(running) + connAvg) / 2.0f;
            }
        }
        return m_prefs ? m_prefs->connAvgDownRate() : 0.0f;

    case AverageType::Time:
        if (!m_downRateHistory.empty()) {
            const auto running = (m_downRateHistory.front().timestamp - m_downRateHistory.back().timestamp) / SEC2MS(1);
            if (running > 0)
                return static_cast<float>(m_downRateHistory.front().dataLen - m_downRateHistory.back().dataLen) / 1024.0f / static_cast<float>(running);
        }
        [[fallthrough]];
    default:
        return 0.0f;
    }
}

float Statistics::avgUploadRate(AverageType type) const
{
    switch (type) {
    case AverageType::Session:
        if (m_transferStartTime > 0) {
            const auto running = (static_cast<uint32>(getTickCount()) - m_transferStartTime) / SEC2MS(1);
            if (running >= 5)
                return static_cast<float>(m_sessionSentBytes.load()) / 1024.0f / static_cast<float>(running);
        }
        return 0.0f;

    case AverageType::Total:
        if (m_transferStartTime > 0) {
            const auto running = (static_cast<uint32>(getTickCount()) - m_transferStartTime) / SEC2MS(1);
            if (running >= 5) {
                const float connAvg = m_prefs ? m_prefs->connAvgUpRate() : 0.0f;
                return (static_cast<float>(m_sessionSentBytes.load()) / 1024.0f / static_cast<float>(running) + connAvg) / 2.0f;
            }
        }
        return m_prefs ? m_prefs->connAvgUpRate() : 0.0f;

    case AverageType::Time:
        if (!m_upRateHistory.empty()) {
            const auto running = (m_upRateHistory.front().timestamp - m_upRateHistory.back().timestamp) / SEC2MS(1);
            if (running > 0)
                return static_cast<float>(m_upRateHistory.front().dataLen - m_upRateHistory.back().dataLen) / 1024.0f / static_cast<float>(running);
        }
        [[fallthrough]];
    default:
        return 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Connection stats update — called periodically from main timer
// ---------------------------------------------------------------------------

void Statistics::updateConnectionStats(float uploadRate, float downloadRate)
{
    m_rateUp = uploadRate;
    m_rateDown = downloadRate;

    // Update session max upload rate
    if (m_maxUp < uploadRate)
        m_maxUp = uploadRate;
    if (m_maxCumUp < m_maxUp) {
        m_maxCumUp = m_maxUp;
        if (m_prefs)
            m_prefs->setConnMaxUpRate(m_maxCumUp);
    }

    // Update session max download rate
    if (m_maxDown < downloadRate)
        m_maxDown = downloadRate;
    if (m_maxCumDown < m_maxDown) {
        m_maxCumDown = m_maxDown;
        if (m_prefs)
            m_prefs->setConnMaxDownRate(m_maxCumDown);
    }

    // Update cumulative averages
    m_cumDownAvg = avgDownloadRate(AverageType::Total);
    if (m_maxCumDownAvg < m_cumDownAvg) {
        m_maxCumDownAvg = m_cumDownAvg;
        if (m_prefs)
            m_prefs->setConnMaxAvgDownRate(m_maxCumDownAvg);
    }

    m_cumUpAvg = avgUploadRate(AverageType::Total);
    if (m_maxCumUpAvg < m_cumUpAvg) {
        m_maxCumUpAvg = m_cumUpAvg;
        if (m_prefs)
            m_prefs->setConnMaxAvgUpRate(m_maxCumUpAvg);
    }

    // Transfer time tracking
    const auto curTick = static_cast<uint32>(getTickCount());

    if (uploadRate > 0 || downloadRate > 0) {
        if (m_startTimeTransfers != 0)
            m_timeThisTransfer = (curTick - m_startTimeTransfers) / SEC2MS(1);
        else
            m_startTimeTransfers = curTick;

        if (uploadRate > 0) {
            if (m_startTimeUploads != 0)
                m_timeThisUpload = (curTick - m_startTimeUploads) / SEC2MS(1);
            else
                m_startTimeUploads = curTick;
        }

        if (downloadRate > 0) {
            if (m_startTimeDownloads != 0)
                m_timeThisDownload = (curTick - m_startTimeDownloads) / SEC2MS(1);
            else
                m_startTimeDownloads = curTick;
        }
    }

    if (uploadRate == 0 && downloadRate == 0
        && (m_timeThisTransfer > 0 || m_startTimeTransfers > 0)) {
        m_timeTransfers += m_timeThisTransfer;
        m_timeThisTransfer = 0;
        m_startTimeTransfers = 0;
    }

    if (uploadRate == 0 && (m_timeThisUpload > 0 || m_startTimeUploads > 0)) {
        m_timeUploads += m_timeThisUpload;
        m_timeThisUpload = 0;
        m_startTimeUploads = 0;
    }

    if (downloadRate == 0 && (m_timeThisDownload > 0 || m_startTimeDownloads > 0)) {
        m_timeDownloads += m_timeThisDownload;
        m_timeThisDownload = 0;
        m_startTimeDownloads = 0;
    }

    // Server duration
    if (m_serverConnectTime == 0)
        m_timeThisServerDuration = 0;
    else
        m_timeThisServerDuration = (curTick - m_serverConnectTime) / SEC2MS(1);

    emit statsUpdated();
}

// ---------------------------------------------------------------------------
// Transfer time getters
// ---------------------------------------------------------------------------

uint32 Statistics::transferTime() const
{
    return m_timeTransfers + m_timeThisTransfer;
}

uint32 Statistics::uploadTime() const
{
    return m_timeUploads + m_timeThisUpload;
}

uint32 Statistics::downloadTime() const
{
    return m_timeDownloads + m_timeThisDownload;
}

uint32 Statistics::serverDuration() const
{
    return m_timeServerDuration + m_timeThisServerDuration;
}

void Statistics::add2TotalServerDuration()
{
    m_timeServerDuration += m_timeThisServerDuration;
    m_timeThisServerDuration = 0;
}

// ---------------------------------------------------------------------------
// Session byte counters
// ---------------------------------------------------------------------------

void Statistics::addSessionReceivedBytes(uint64 bytes)
{
    m_sessionReceivedBytes.fetch_add(bytes, std::memory_order_relaxed);
    emit sessionBytesChanged();
}

void Statistics::addSessionSentBytes(uint64 bytes)
{
    m_sessionSentBytes.fetch_add(bytes, std::memory_order_relaxed);
    emit sessionBytesChanged();
}

void Statistics::addSessionSentBytesToFriend(uint64 bytes)
{
    m_sessionSentBytesToFriend.fetch_add(bytes, std::memory_order_relaxed);
    emit sessionBytesChanged();
}

// ---------------------------------------------------------------------------
// Download quality counters
// ---------------------------------------------------------------------------

void Statistics::addCompressionGain(uint64 bytes)
{
    m_sesCompressionGain.fetch_add(bytes, std::memory_order_relaxed);
}

void Statistics::addCorruptionLoss(uint64 bytes)
{
    m_sesCorruptionLoss.fetch_add(bytes, std::memory_order_relaxed);
}

void Statistics::subCorruptionLoss(uint64 bytes)
{
    // Saturate rather than wrap: the recovered bytes may have been charged to a
    // previous session, in which case this session's loss is already smaller.
    uint64 cur = m_sesCorruptionLoss.load(std::memory_order_relaxed);
    while (true) {
        const uint64 next = (cur > bytes) ? cur - bytes : 0;
        if (m_sesCorruptionLoss.compare_exchange_weak(cur, next,
                                                      std::memory_order_relaxed))
            break;
    }
}

void Statistics::addIchPartSaved()
{
    m_sesIchPartsSaved.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// Download overhead
// ---------------------------------------------------------------------------

void Statistics::addDownDataOverheadSourceExchange(uint32 data)
{
    m_downDataRateMSOverhead.fetch_add(data, std::memory_order_relaxed);
    m_downOverheadSourceExchange.fetch_add(data, std::memory_order_relaxed);
    m_downOverheadSourceExchangePackets.fetch_add(1, std::memory_order_relaxed);
}

void Statistics::addDownDataOverheadFileRequest(uint32 data)
{
    m_downDataRateMSOverhead.fetch_add(data, std::memory_order_relaxed);
    m_downOverheadFileRequest.fetch_add(data, std::memory_order_relaxed);
    m_downOverheadFileRequestPackets.fetch_add(1, std::memory_order_relaxed);
}

void Statistics::addDownDataOverheadServer(uint32 data)
{
    m_downDataRateMSOverhead.fetch_add(data, std::memory_order_relaxed);
    m_downOverheadServer.fetch_add(data, std::memory_order_relaxed);
    m_downOverheadServerPackets.fetch_add(1, std::memory_order_relaxed);
}

void Statistics::addDownDataOverheadKad(uint32 data)
{
    m_downDataRateMSOverhead.fetch_add(data, std::memory_order_relaxed);
    m_downOverheadKad.fetch_add(data, std::memory_order_relaxed);
    m_downOverheadKadPackets.fetch_add(1, std::memory_order_relaxed);
}

void Statistics::addDownDataOverheadOther(uint32 data)
{
    m_downDataRateMSOverhead.fetch_add(data, std::memory_order_relaxed);
    m_downOverheadOther.fetch_add(data, std::memory_order_relaxed);
    m_downOverheadOtherPackets.fetch_add(1, std::memory_order_relaxed);
}

void Statistics::compDownDatarateOverhead()
{
    const auto curTick = static_cast<uint32>(getTickCount());

    const uint64 msOverhead = m_downDataRateMSOverhead.exchange(0, std::memory_order_relaxed);
    m_avgDDROList.push_back(RateEntry{msOverhead, curTick});
    m_sumAvgDDRO += msOverhead;

    while (!m_avgDDROList.empty() && curTick > m_avgDDROList.front().timestamp + kMaxAverageTime) {
        m_sumAvgDDRO -= m_avgDDROList.front().dataLen;
        m_avgDDROList.pop_front();
    }

    if (m_avgDDROList.size() > 10) {
        const auto& head = m_avgDDROList.front();
        if (curTick > head.timestamp) {
            m_downDatarateOverhead = SEC2MS(m_sumAvgDDRO - head.dataLen) / (curTick - head.timestamp);
            emit overheadStatsUpdated();
            return;
        }
    }
    m_downDatarateOverhead = 0;
    emit overheadStatsUpdated();
}

void Statistics::resetDownDatarateOverhead()
{
    m_avgDDROList.clear();
    m_downDataRateMSOverhead.store(0, std::memory_order_relaxed);
    m_downDatarateOverhead = 0;
    m_sumAvgDDRO = 0;
}

// ---------------------------------------------------------------------------
// Upload overhead
// ---------------------------------------------------------------------------

void Statistics::addUpDataOverheadSourceExchange(uint32 data)
{
    m_upDataRateMSOverhead.fetch_add(data, std::memory_order_relaxed);
    m_upOverheadSourceExchange.fetch_add(data, std::memory_order_relaxed);
    m_upOverheadSourceExchangePackets.fetch_add(1, std::memory_order_relaxed);
}

void Statistics::addUpDataOverheadFileRequest(uint32 data)
{
    m_upDataRateMSOverhead.fetch_add(data, std::memory_order_relaxed);
    m_upOverheadFileRequest.fetch_add(data, std::memory_order_relaxed);
    m_upOverheadFileRequestPackets.fetch_add(1, std::memory_order_relaxed);
}

void Statistics::addUpDataOverheadServer(uint32 data)
{
    m_upDataRateMSOverhead.fetch_add(data, std::memory_order_relaxed);
    m_upOverheadServer.fetch_add(data, std::memory_order_relaxed);
    m_upOverheadServerPackets.fetch_add(1, std::memory_order_relaxed);
}

void Statistics::addUpDataOverheadKad(uint32 data)
{
    m_upDataRateMSOverhead.fetch_add(data, std::memory_order_relaxed);
    m_upOverheadKad.fetch_add(data, std::memory_order_relaxed);
    m_upOverheadKadPackets.fetch_add(1, std::memory_order_relaxed);
}

void Statistics::addUpDataOverheadOther(uint32 data)
{
    m_upDataRateMSOverhead.fetch_add(data, std::memory_order_relaxed);
    m_upOverheadOther.fetch_add(data, std::memory_order_relaxed);
    m_upOverheadOtherPackets.fetch_add(1, std::memory_order_relaxed);
}

void Statistics::compUpDatarateOverhead()
{
    const auto curTick = static_cast<uint32>(getTickCount());

    const uint64 msOverhead = m_upDataRateMSOverhead.exchange(0, std::memory_order_relaxed);
    m_avgUDROList.push_back(RateEntry{msOverhead, curTick});
    m_sumAvgUDRO += msOverhead;

    while (!m_avgUDROList.empty() && curTick > m_avgUDROList.front().timestamp + kMaxAverageTime) {
        m_sumAvgUDRO -= m_avgUDROList.front().dataLen;
        m_avgUDROList.pop_front();
    }

    if (m_avgUDROList.size() > 10) {
        const auto& head = m_avgUDROList.front();
        if (curTick > head.timestamp) {
            m_upDatarateOverhead = SEC2MS(m_sumAvgUDRO - head.dataLen) / (curTick - head.timestamp);
            emit overheadStatsUpdated();
            return;
        }
    }
    m_upDatarateOverhead = 0;
    emit overheadStatsUpdated();
}

void Statistics::resetUpDatarateOverhead()
{
    m_avgUDROList.clear();
    m_upDataRateMSOverhead.store(0, std::memory_order_relaxed);
    m_upDatarateOverhead = 0;
    m_sumAvgUDRO = 0;
}

// ---------------------------------------------------------------------------
// Per-client / port / source transfer tracking
// ---------------------------------------------------------------------------

int Statistics::clientIndex(ClientSoftware cs)
{
    // Map sparse ClientSoftware enum → compact index 0..7
    switch (cs) {
    case ClientSoftware::eMule:
    case ClientSoftware::OldEMule:     return 0; // eMule
    case ClientSoftware::eDonkeyHybrid: return 1; // eD Hybrid
    case ClientSoftware::eDonkey:       return 2; // eDonkey
    case ClientSoftware::aMule:         return 3; // aMule
    case ClientSoftware::MLDonkey:      return 4; // MLdonkey
    case ClientSoftware::Shareaza:      return 5; // Shareaza
    case ClientSoftware::cDonkey:
    case ClientSoftware::xMule:
    case ClientSoftware::lphant:        return 6; // eM Compat
    case ClientSoftware::URL:           return 7; // URL (download only)
    default:                            return 6; // unknown → compat
    }
}

void Statistics::addTransferData(ClientSoftware clientType, uint16 port,
                                  bool isPartFile, bool isUpload, uint64 bytes)
{
    if (bytes == 0)
        return;

    const int idx = clientIndex(clientType);
    const bool defaultPort = (port == 4662);

    if (isUpload) {
        if (idx < kUpClientCount)
            m_sesUpByClient[static_cast<size_t>(idx)].fetch_add(bytes, std::memory_order_relaxed);
        if (defaultPort)
            m_sesUpPort4662.fetch_add(bytes, std::memory_order_relaxed);
        else
            m_sesUpPortOther.fetch_add(bytes, std::memory_order_relaxed);
        if (isPartFile)
            m_sesUpFromPartfile.fetch_add(bytes, std::memory_order_relaxed);
        else
            m_sesUpFromFile.fetch_add(bytes, std::memory_order_relaxed);
    } else {
        if (idx < kDownClientCount)
            m_sesDownByClient[static_cast<size_t>(idx)].fetch_add(bytes, std::memory_order_relaxed);
        if (defaultPort)
            m_sesDownPort4662.fetch_add(bytes, std::memory_order_relaxed);
        else
            m_sesDownPortOther.fetch_add(bytes, std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------------------
// Cumulative totals
// ---------------------------------------------------------------------------

void Statistics::rebaseCumulative(const Preferences& prefs)
{
    CumulativeTotals& b = m_cumBase;

    b.totalUploaded = prefs.cumTotalUploaded();
    b.totalDownloaded = prefs.cumTotalDownloaded();
    b.totalUploadedToFriend = prefs.cumTotalUploadedToFriend();

    b.upSuccessfulSessions = prefs.cumUpSuccessfulSessions();
    b.upFailedSessions = prefs.cumUpFailedSessions();
    b.downSuccessfulSessions = prefs.cumDownSuccessfulSessions();
    b.downFailedSessions = prefs.cumDownFailedSessions();
    b.downCompletedFiles = prefs.cumDownCompletedFiles();

    b.connPeak = prefs.cumConnPeak();
    b.connMaxLimitReached = prefs.cumConnMaxLimitReached();
    b.connReconnects = prefs.cumConnReconnects();

    b.runTime = prefs.cumRunTime();
    b.transferTime = prefs.cumTransferTime();
    b.uploadTime = prefs.cumUploadTime();
    b.downloadTime = prefs.cumDownloadTime();
    b.serverDuration = prefs.cumServerDuration();

    b.compressionGain = prefs.cumCompressionGain();
    b.corruptionLoss = prefs.cumCorruptionLoss();
    b.ichPartsSaved = prefs.cumIchPartsSaved();

    b.upOverheadTotal = prefs.cumUpOverheadTotal();
    b.upOverheadTotalPackets = prefs.cumUpOverheadTotalPackets();
    b.upOverheadFileReq = prefs.cumUpOverheadFileReq();
    b.upOverheadFileReqPackets = prefs.cumUpOverheadFileReqPackets();
    b.upOverheadSrcExch = prefs.cumUpOverheadSrcExch();
    b.upOverheadSrcExchPackets = prefs.cumUpOverheadSrcExchPackets();
    b.upOverheadServer = prefs.cumUpOverheadServer();
    b.upOverheadServerPackets = prefs.cumUpOverheadServerPackets();
    b.upOverheadKad = prefs.cumUpOverheadKad();
    b.upOverheadKadPackets = prefs.cumUpOverheadKadPackets();

    b.downOverheadTotal = prefs.cumDownOverheadTotal();
    b.downOverheadTotalPackets = prefs.cumDownOverheadTotalPackets();
    b.downOverheadFileReq = prefs.cumDownOverheadFileReq();
    b.downOverheadFileReqPackets = prefs.cumDownOverheadFileReqPackets();
    b.downOverheadSrcExch = prefs.cumDownOverheadSrcExch();
    b.downOverheadSrcExchPackets = prefs.cumDownOverheadSrcExchPackets();
    b.downOverheadServer = prefs.cumDownOverheadServer();
    b.downOverheadServerPackets = prefs.cumDownOverheadServerPackets();
    b.downOverheadKad = prefs.cumDownOverheadKad();
    b.downOverheadKadPackets = prefs.cumDownOverheadKadPackets();

    b.upByClient = {prefs.cumUpEmule(), prefs.cumUpEDHybrid(), prefs.cumUpEDonkey(),
                    prefs.cumUpAMule(), prefs.cumUpMLdonkey(), prefs.cumUpShareaza(),
                    prefs.cumUpEMCompat()};
    b.downByClient = {prefs.cumDownEmule(), prefs.cumDownEDHybrid(), prefs.cumDownEDonkey(),
                      prefs.cumDownAMule(), prefs.cumDownMLdonkey(), prefs.cumDownShareaza(),
                      prefs.cumDownEMCompat(), prefs.cumDownURL()};

    b.upPort4662 = prefs.cumUpPort4662();
    b.upPortOther = prefs.cumUpPortOther();
    b.downPort4662 = prefs.cumDownPort4662();
    b.downPortOther = prefs.cumDownPortOther();

    b.upFromFile = prefs.cumUpFromFile();
    b.upFromPartfile = prefs.cumUpFromPartfile();

    b.httpCacheBytesPublished = prefs.cumHttpCacheBytesPublished();
    b.httpCacheBytesFetched = prefs.cumHttpCacheBytesFetched();
    b.httpCacheBytesSaved = prefs.cumHttpCacheBytesSaved();
    b.httpCacheChunksPublished = prefs.cumHttpCacheChunksPublished();
    b.httpCacheChunksFetched = prefs.cumHttpCacheChunksFetched();
}

Statistics::CumulativeTotals
Statistics::cumulativeTotals(const ExternalSessionCounters& ext) const
{
    CumulativeTotals t = m_cumBase;

    t.totalUploaded += m_sessionSentBytes.load();
    t.totalDownloaded += m_sessionReceivedBytes.load();
    t.totalUploadedToFriend += m_sessionSentBytesToFriend.load();

    t.upSuccessfulSessions += ext.upSuccessfulSessions;
    t.upFailedSessions += ext.upFailedSessions;
    t.downSuccessfulSessions += ext.downSuccessfulSessions;
    t.downFailedSessions += ext.downFailedSessions;
    t.downCompletedFiles += ext.downCompletedFiles;

    // Peak is a high-water mark across sessions, not a sum.
    t.connPeak = std::max(t.connPeak, ext.connPeak);
    t.connMaxLimitReached += ext.connMaxLimitReached;
    t.connReconnects += m_reconnects;

    t.httpCacheBytesPublished += ext.httpCacheBytesPublished;
    t.httpCacheBytesFetched += ext.httpCacheBytesFetched;
    t.httpCacheBytesSaved += ext.httpCacheBytesSaved;
    t.httpCacheChunksPublished += ext.httpCacheChunksPublished;
    t.httpCacheChunksFetched += ext.httpCacheChunksFetched;

    t.runTime += uptimeSecs();
    t.transferTime += transferTime();
    t.uploadTime += uploadTime();
    t.downloadTime += downloadTime();
    t.serverDuration += serverDuration();

    t.compressionGain += m_sesCompressionGain.load();
    t.corruptionLoss += m_sesCorruptionLoss.load();
    t.ichPartsSaved += m_sesIchPartsSaved.load();

    const uint64 upOhFileReq = m_upOverheadFileRequest.load();
    const uint64 upOhSrcExch = m_upOverheadSourceExchange.load();
    const uint64 upOhServer = m_upOverheadServer.load();
    const uint64 upOhKad = m_upOverheadKad.load();
    const uint64 upOhFileReqPkt = m_upOverheadFileRequestPackets.load();
    const uint64 upOhSrcExchPkt = m_upOverheadSourceExchangePackets.load();
    const uint64 upOhServerPkt = m_upOverheadServerPackets.load();
    const uint64 upOhKadPkt = m_upOverheadKadPackets.load();

    t.upOverheadTotal += upOhFileReq + upOhSrcExch + upOhServer + upOhKad
                         + m_upOverheadOther.load();
    t.upOverheadTotalPackets += upOhFileReqPkt + upOhSrcExchPkt + upOhServerPkt + upOhKadPkt
                                + m_upOverheadOtherPackets.load();
    t.upOverheadFileReq += upOhFileReq;
    t.upOverheadFileReqPackets += upOhFileReqPkt;
    t.upOverheadSrcExch += upOhSrcExch;
    t.upOverheadSrcExchPackets += upOhSrcExchPkt;
    t.upOverheadServer += upOhServer;
    t.upOverheadServerPackets += upOhServerPkt;
    t.upOverheadKad += upOhKad;
    t.upOverheadKadPackets += upOhKadPkt;

    const uint64 downOhFileReq = m_downOverheadFileRequest.load();
    const uint64 downOhSrcExch = m_downOverheadSourceExchange.load();
    const uint64 downOhServer = m_downOverheadServer.load();
    const uint64 downOhKad = m_downOverheadKad.load();
    const uint64 downOhFileReqPkt = m_downOverheadFileRequestPackets.load();
    const uint64 downOhSrcExchPkt = m_downOverheadSourceExchangePackets.load();
    const uint64 downOhServerPkt = m_downOverheadServerPackets.load();
    const uint64 downOhKadPkt = m_downOverheadKadPackets.load();

    t.downOverheadTotal += downOhFileReq + downOhSrcExch + downOhServer + downOhKad
                           + m_downOverheadOther.load();
    t.downOverheadTotalPackets += downOhFileReqPkt + downOhSrcExchPkt + downOhServerPkt
                                  + downOhKadPkt + m_downOverheadOtherPackets.load();
    t.downOverheadFileReq += downOhFileReq;
    t.downOverheadFileReqPackets += downOhFileReqPkt;
    t.downOverheadSrcExch += downOhSrcExch;
    t.downOverheadSrcExchPackets += downOhSrcExchPkt;
    t.downOverheadServer += downOhServer;
    t.downOverheadServerPackets += downOhServerPkt;
    t.downOverheadKad += downOhKad;
    t.downOverheadKadPackets += downOhKadPkt;

    for (size_t i = 0; i < static_cast<size_t>(kUpClientCount); ++i)
        t.upByClient[i] += m_sesUpByClient[i].load();
    for (size_t i = 0; i < static_cast<size_t>(kDownClientCount); ++i)
        t.downByClient[i] += m_sesDownByClient[i].load();

    t.upPort4662 += m_sesUpPort4662.load();
    t.upPortOther += m_sesUpPortOther.load();
    t.downPort4662 += m_sesDownPort4662.load();
    t.downPortOther += m_sesDownPortOther.load();

    t.upFromFile += m_sesUpFromFile.load();
    t.upFromPartfile += m_sesUpFromPartfile.load();

    return t;
}

void Statistics::flushCumulativeToPrefs(Preferences& prefs,
                                        const ExternalSessionCounters& ext) const
{
    const CumulativeTotals t = cumulativeTotals(ext);

    prefs.setCumTotalUploaded(t.totalUploaded);
    prefs.setCumTotalDownloaded(t.totalDownloaded);
    prefs.setCumTotalUploadedToFriend(t.totalUploadedToFriend);

    prefs.setCumUpSuccessfulSessions(t.upSuccessfulSessions);
    prefs.setCumUpFailedSessions(t.upFailedSessions);
    prefs.setCumDownSuccessfulSessions(t.downSuccessfulSessions);
    prefs.setCumDownFailedSessions(t.downFailedSessions);
    prefs.setCumDownCompletedFiles(t.downCompletedFiles);

    prefs.setCumHttpCacheBytesPublished(t.httpCacheBytesPublished);
    prefs.setCumHttpCacheBytesFetched(t.httpCacheBytesFetched);
    prefs.setCumHttpCacheBytesSaved(t.httpCacheBytesSaved);
    prefs.setCumHttpCacheChunksPublished(t.httpCacheChunksPublished);
    prefs.setCumHttpCacheChunksFetched(t.httpCacheChunksFetched);

    prefs.setCumConnPeak(t.connPeak);
    prefs.setCumConnMaxLimitReached(t.connMaxLimitReached);
    prefs.setCumConnReconnects(t.connReconnects);

    prefs.setCumRunTime(t.runTime);
    prefs.setCumTransferTime(t.transferTime);
    prefs.setCumUploadTime(t.uploadTime);
    prefs.setCumDownloadTime(t.downloadTime);
    prefs.setCumServerDuration(t.serverDuration);

    prefs.setCumCompressionGain(t.compressionGain);
    prefs.setCumCorruptionLoss(t.corruptionLoss);
    prefs.setCumIchPartsSaved(t.ichPartsSaved);

    prefs.setCumUpOverheadTotal(t.upOverheadTotal);
    prefs.setCumUpOverheadTotalPackets(t.upOverheadTotalPackets);
    prefs.setCumUpOverheadFileReq(t.upOverheadFileReq);
    prefs.setCumUpOverheadFileReqPackets(t.upOverheadFileReqPackets);
    prefs.setCumUpOverheadSrcExch(t.upOverheadSrcExch);
    prefs.setCumUpOverheadSrcExchPackets(t.upOverheadSrcExchPackets);
    prefs.setCumUpOverheadServer(t.upOverheadServer);
    prefs.setCumUpOverheadServerPackets(t.upOverheadServerPackets);
    prefs.setCumUpOverheadKad(t.upOverheadKad);
    prefs.setCumUpOverheadKadPackets(t.upOverheadKadPackets);

    prefs.setCumDownOverheadTotal(t.downOverheadTotal);
    prefs.setCumDownOverheadTotalPackets(t.downOverheadTotalPackets);
    prefs.setCumDownOverheadFileReq(t.downOverheadFileReq);
    prefs.setCumDownOverheadFileReqPackets(t.downOverheadFileReqPackets);
    prefs.setCumDownOverheadSrcExch(t.downOverheadSrcExch);
    prefs.setCumDownOverheadSrcExchPackets(t.downOverheadSrcExchPackets);
    prefs.setCumDownOverheadServer(t.downOverheadServer);
    prefs.setCumDownOverheadServerPackets(t.downOverheadServerPackets);
    prefs.setCumDownOverheadKad(t.downOverheadKad);
    prefs.setCumDownOverheadKadPackets(t.downOverheadKadPackets);

    prefs.setCumUpEmule(t.upByClient[0]);
    prefs.setCumUpEDHybrid(t.upByClient[1]);
    prefs.setCumUpEDonkey(t.upByClient[2]);
    prefs.setCumUpAMule(t.upByClient[3]);
    prefs.setCumUpMLdonkey(t.upByClient[4]);
    prefs.setCumUpShareaza(t.upByClient[5]);
    prefs.setCumUpEMCompat(t.upByClient[6]);

    prefs.setCumDownEmule(t.downByClient[0]);
    prefs.setCumDownEDHybrid(t.downByClient[1]);
    prefs.setCumDownEDonkey(t.downByClient[2]);
    prefs.setCumDownAMule(t.downByClient[3]);
    prefs.setCumDownMLdonkey(t.downByClient[4]);
    prefs.setCumDownShareaza(t.downByClient[5]);
    prefs.setCumDownEMCompat(t.downByClient[6]);
    prefs.setCumDownURL(t.downByClient[7]);

    prefs.setCumUpPort4662(t.upPort4662);
    prefs.setCumUpPortOther(t.upPortOther);
    prefs.setCumDownPort4662(t.downPort4662);
    prefs.setCumDownPortOther(t.downPortOther);

    prefs.setCumUpFromFile(t.upFromFile);
    prefs.setCumUpFromPartfile(t.upFromPartfile);
}

} // namespace eMule
