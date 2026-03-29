#include "pch.h"
/// @file Statistics.cpp
/// @brief Global transfer statistics — implementation.

#include "stats/Statistics.h"

#include "client/ClientStateDefs.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"
#include "utils/TimeUtils.h"

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
// Save cumulative stats to Preferences on shutdown
// ---------------------------------------------------------------------------

void Statistics::saveCumulativeToPrefs(Preferences& prefs) const
{
    // Transfer totals
    prefs.setCumTotalUploaded(prefs.cumTotalUploaded() + m_sessionSentBytes.load());
    prefs.setCumTotalDownloaded(prefs.cumTotalDownloaded() + m_sessionReceivedBytes.load());
    prefs.setCumTotalUploadedToFriend(prefs.cumTotalUploadedToFriend() + m_sessionSentBytesToFriend.load());

    // Connection stats
    prefs.setCumConnReconnects(prefs.cumConnReconnects() + m_reconnects);

    // Times
    const uint32 uptime = (m_startTime > 0)
        ? (static_cast<uint32>(getTickCount()) - m_startTime) / SEC2MS(1) : 0;
    prefs.setCumRunTime(prefs.cumRunTime() + uptime);
    prefs.setCumTransferTime(prefs.cumTransferTime() + transferTime());
    prefs.setCumUploadTime(prefs.cumUploadTime() + uploadTime());
    prefs.setCumDownloadTime(prefs.cumDownloadTime() + downloadTime());
    prefs.setCumServerDuration(prefs.cumServerDuration() + serverDuration());

    // Overhead — upload
    prefs.setCumUpOverheadTotal(prefs.cumUpOverheadTotal()
        + m_upOverheadFileRequest.load() + m_upOverheadSourceExchange.load()
        + m_upOverheadServer.load() + m_upOverheadKad.load() + m_upOverheadOther.load());
    prefs.setCumUpOverheadTotalPackets(prefs.cumUpOverheadTotalPackets()
        + m_upOverheadFileRequestPackets.load() + m_upOverheadSourceExchangePackets.load()
        + m_upOverheadServerPackets.load() + m_upOverheadKadPackets.load() + m_upOverheadOtherPackets.load());
    prefs.setCumUpOverheadFileReq(prefs.cumUpOverheadFileReq() + m_upOverheadFileRequest.load());
    prefs.setCumUpOverheadFileReqPackets(prefs.cumUpOverheadFileReqPackets() + m_upOverheadFileRequestPackets.load());
    prefs.setCumUpOverheadSrcExch(prefs.cumUpOverheadSrcExch() + m_upOverheadSourceExchange.load());
    prefs.setCumUpOverheadSrcExchPackets(prefs.cumUpOverheadSrcExchPackets() + m_upOverheadSourceExchangePackets.load());
    prefs.setCumUpOverheadServer(prefs.cumUpOverheadServer() + m_upOverheadServer.load());
    prefs.setCumUpOverheadServerPackets(prefs.cumUpOverheadServerPackets() + m_upOverheadServerPackets.load());
    prefs.setCumUpOverheadKad(prefs.cumUpOverheadKad() + m_upOverheadKad.load());
    prefs.setCumUpOverheadKadPackets(prefs.cumUpOverheadKadPackets() + m_upOverheadKadPackets.load());

    // Overhead — download
    prefs.setCumDownOverheadTotal(prefs.cumDownOverheadTotal()
        + m_downOverheadFileRequest.load() + m_downOverheadSourceExchange.load()
        + m_downOverheadServer.load() + m_downOverheadKad.load() + m_downOverheadOther.load());
    prefs.setCumDownOverheadTotalPackets(prefs.cumDownOverheadTotalPackets()
        + m_downOverheadFileRequestPackets.load() + m_downOverheadSourceExchangePackets.load()
        + m_downOverheadServerPackets.load() + m_downOverheadKadPackets.load() + m_downOverheadOtherPackets.load());
    prefs.setCumDownOverheadFileReq(prefs.cumDownOverheadFileReq() + m_downOverheadFileRequest.load());
    prefs.setCumDownOverheadFileReqPackets(prefs.cumDownOverheadFileReqPackets() + m_downOverheadFileRequestPackets.load());
    prefs.setCumDownOverheadSrcExch(prefs.cumDownOverheadSrcExch() + m_downOverheadSourceExchange.load());
    prefs.setCumDownOverheadSrcExchPackets(prefs.cumDownOverheadSrcExchPackets() + m_downOverheadSourceExchangePackets.load());
    prefs.setCumDownOverheadServer(prefs.cumDownOverheadServer() + m_downOverheadServer.load());
    prefs.setCumDownOverheadServerPackets(prefs.cumDownOverheadServerPackets() + m_downOverheadServerPackets.load());
    prefs.setCumDownOverheadKad(prefs.cumDownOverheadKad() + m_downOverheadKad.load());
    prefs.setCumDownOverheadKadPackets(prefs.cumDownOverheadKadPackets() + m_downOverheadKadPackets.load());

    // Per-client — upload
    prefs.setCumUpEmule(prefs.cumUpEmule() + m_sesUpByClient[0].load());
    prefs.setCumUpEDHybrid(prefs.cumUpEDHybrid() + m_sesUpByClient[1].load());
    prefs.setCumUpEDonkey(prefs.cumUpEDonkey() + m_sesUpByClient[2].load());
    prefs.setCumUpAMule(prefs.cumUpAMule() + m_sesUpByClient[3].load());
    prefs.setCumUpMLdonkey(prefs.cumUpMLdonkey() + m_sesUpByClient[4].load());
    prefs.setCumUpShareaza(prefs.cumUpShareaza() + m_sesUpByClient[5].load());
    prefs.setCumUpEMCompat(prefs.cumUpEMCompat() + m_sesUpByClient[6].load());

    // Per-client — download
    prefs.setCumDownEmule(prefs.cumDownEmule() + m_sesDownByClient[0].load());
    prefs.setCumDownEDHybrid(prefs.cumDownEDHybrid() + m_sesDownByClient[1].load());
    prefs.setCumDownEDonkey(prefs.cumDownEDonkey() + m_sesDownByClient[2].load());
    prefs.setCumDownAMule(prefs.cumDownAMule() + m_sesDownByClient[3].load());
    prefs.setCumDownMLdonkey(prefs.cumDownMLdonkey() + m_sesDownByClient[4].load());
    prefs.setCumDownShareaza(prefs.cumDownShareaza() + m_sesDownByClient[5].load());
    prefs.setCumDownEMCompat(prefs.cumDownEMCompat() + m_sesDownByClient[6].load());
    prefs.setCumDownURL(prefs.cumDownURL() + m_sesDownByClient[7].load());

    // Per-port
    prefs.setCumUpPort4662(prefs.cumUpPort4662() + m_sesUpPort4662.load());
    prefs.setCumUpPortOther(prefs.cumUpPortOther() + m_sesUpPortOther.load());
    prefs.setCumDownPort4662(prefs.cumDownPort4662() + m_sesDownPort4662.load());
    prefs.setCumDownPortOther(prefs.cumDownPortOther() + m_sesDownPortOther.load());

    // Per-source
    prefs.setCumUpFromFile(prefs.cumUpFromFile() + m_sesUpFromFile.load());
    prefs.setCumUpFromPartfile(prefs.cumUpFromPartfile() + m_sesUpFromPartfile.load());
}

} // namespace eMule
