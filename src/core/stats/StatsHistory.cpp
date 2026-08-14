#include "pch.h"
/// @file StatsHistory.cpp
/// @brief Core-owned sample history for the statistics graphs — implementation.

#include "stats/StatsHistory.h"

#include "app/AppContext.h"
#include "files/PartFile.h"
#include "net/ListenSocket.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"

#include <QDateTime>

#include <algorithm>

namespace eMule {

StatsHistory::StatsHistory(QObject* parent)
    : QObject(parent)
    , m_epoch(static_cast<uint32>(QDateTime::currentSecsSinceEpoch()))
{
}

void StatsHistory::sample(uint32 nowSecs)
{
    takeSpeedSample();

    const uint32 interval = thePrefs.graphsUpdateSec();
    if (interval == 0)
        return;   // graphs disabled — MFC skips SetCurrentRate entirely

    // The first sample always lands; after that the pref sets the spacing. A clock
    // that jumps backwards underflows the difference into a huge number and so
    // samples immediately, which is the harmless direction to fail in.
    if (m_statsSeq != 0 && nowSecs - m_lastStatsSampleTime < interval)
        return;

    m_lastStatsSampleTime = nowSecs;
    takeStatsSample(nowSecs);
}

std::vector<StatsGraphSample> StatsHistory::statsSince(uint32 seq) const
{
    std::vector<StatsGraphSample> out;
    for (const auto& s : m_stats)
        if (s.seq > seq)
            out.push_back(s);
    return out;
}

std::vector<SpeedSample> StatsHistory::speedSince(uint32 seq) const
{
    std::vector<SpeedSample> out;
    for (const auto& s : m_speed)
        if (s.seq > seq)
            out.push_back(s);
    return out;
}

uint32 StatsHistory::oldestStatsSeq() const
{
    return m_stats.empty() ? 0 : m_stats.front().seq;
}

uint32 StatsHistory::oldestSpeedSeq() const
{
    return m_speed.empty() ? 0 : m_speed.front().seq;
}

void StatsHistory::reset()
{
    m_stats.clear();
    m_speed.clear();
    m_statsSeq = 0;
    m_speedSeq = 0;
    m_lastStatsSampleTime = 0;
    ++m_epoch;   // tells every viewer to drop its buffer rather than append
}

// ---------------------------------------------------------------------------
// Sampling
// ---------------------------------------------------------------------------

void StatsHistory::takeStatsSample(uint32 nowSecs)
{
    StatsGraphSample s;
    s.seq = ++m_statsSeq;
    s.timestamp = nowSecs;

    if (const auto* st = theApp.statistics) {
        s.downAvgSession = st->avgDownloadRate(AverageType::Session);
        s.downAvgTime    = st->avgDownloadRate(AverageType::Time);
        s.downCurrent    = st->rateDown();

        s.upAvgSession = st->avgUploadRate(AverageType::Session);
        s.upAvgTime    = st->avgUploadRate(AverageType::Time);
        s.upCurrent    = st->rateUp();

        // MFC: uploadrate - GetUpDatarateOverhead()/1024 (StatisticsDlg.cpp:588).
        // Both there and here the upload datarate counts payload only — the
        // throttler's overhead counter is read and discarded (UploadQueue.cpp:778) —
        // so the difference can fall below zero on a mostly-idle upload. Clamped,
        // because a rate cannot be negative and the auto-scale would follow it down.
        s.upNoOverhead = std::max(0.0f,
            s.upCurrent - static_cast<float>(st->upDatarateOverhead()) / 1024.0f);
    }

    if (const auto* uq = theApp.uploadQueue) {
        // MFC: uploadrate - GetToNetworkDatarate()/1024, and GetToNetworkDatarate()
        // is datarate - friendDatarate (srchybrid/UploadQueue.cpp:941) — i.e. the
        // friend-slot rate, which we track directly.
        s.upFriend = static_cast<float>(uq->friendDatarate()) / 1024.0f;
        s.upActive = static_cast<uint32>(uq->maxActiveClientsShortTime());
        s.upTotal  = static_cast<uint32>(uq->uploadQueueLength());
    }

    if (const auto* ls = theApp.listenSocket)
        s.connActive = ls->activeConnections();

    if (const auto* dq = theApp.downloadQueue) {
        // MFC plots SDownloadStats.a[1], the sum of GetTransferringSrcCount() over
        // the queue (srchybrid/DownloadQueue.cpp:1032) — sources actually sending,
        // not the number of files.
        int transferring = 0;
        for (const auto* f : dq->files())
            transferring += f->transferringSrcCount();
        s.downTransferring = static_cast<uint32>(transferring);
    }

    m_stats.push_back(s);
    if (m_stats.size() > kStatsCapacity)
        m_stats.pop_front();
}

void StatsHistory::takeSpeedSample()
{
    SpeedSample s;
    s.seq = ++m_speedSeq;

    if (const auto* st = theApp.statistics) {
        s.down = st->rateDown();
        s.up   = st->rateUp();
    }

    m_speed.push_back(s);
    if (m_speed.size() > kSpeedCapacity)
        m_speed.pop_front();
}

} // namespace eMule
