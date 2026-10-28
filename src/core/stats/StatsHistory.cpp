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

void StatsHistory::setUsenetDownRateSource(std::function<float()> source)
{
    m_usenetDownRate = std::move(source);
}

void StatsHistory::setBtRateSource(std::function<EngineRates()> source)
{
    m_btRates = std::move(source);
}

void StatsHistory::setEd3kRateSource(std::function<EngineRates()> source)
{
    m_ed3kRates = std::move(source);
}

QCborArray statsSampleToCbor(const StatsGraphSample& s)
{
    // MFC's scope order plus the appended Usenet, BitTorrent and ed3k rates; new fields
    // go at the end so an older GUI still reads its 17
    return QCborArray{static_cast<qint64>(s.seq),
                      static_cast<qint64>(s.timestamp),
                      static_cast<double>(s.downAvgSession),
                      static_cast<double>(s.downAvgTime),
                      static_cast<double>(s.downCurrent),
                      static_cast<double>(s.upAvgSession),
                      static_cast<double>(s.upAvgTime),
                      static_cast<double>(s.upCurrent),
                      static_cast<double>(s.upNoOverhead),
                      static_cast<double>(s.upFriend),
                      static_cast<qint64>(s.connActive),
                      static_cast<qint64>(s.upActive),
                      static_cast<qint64>(s.upTotal),
                      static_cast<qint64>(s.downTransferring),
                      static_cast<double>(s.usenetDown),
                      static_cast<double>(s.btDown),
                      static_cast<double>(s.btUp),
                      static_cast<double>(s.ed3kDown),
                      static_cast<double>(s.ed3kUp)};
}

std::optional<StatsGraphSample> statsSampleFromCbor(const QCborArray& a)
{
    if (a.size() < kStatsSampleFieldsOld)
        return std::nullopt;
    const auto f = [&a](qsizetype i) { return i < a.size() ? static_cast<float>(a.at(i).toDouble()) : 0.0f; };
    const auto u = [&a](qsizetype i) { return static_cast<uint32>(std::max<qint64>(0, a.at(i).toInteger())); };
    StatsGraphSample s;
    s.seq = u(0);
    s.timestamp = u(1);
    s.downAvgSession = f(2);
    s.downAvgTime = f(3);
    s.downCurrent = f(4);
    s.upAvgSession = f(5);
    s.upAvgTime = f(6);
    s.upCurrent = f(7);
    s.upNoOverhead = f(8);
    s.upFriend = f(9);
    s.connActive = u(10);
    s.upActive = u(11);
    s.upTotal = u(12);
    s.downTransferring = u(13);
    s.usenetDown = f(14);
    s.btDown = f(15);
    s.btUp = f(16);
    s.ed3kDown = f(17);   // 0 from a 17-field core
    s.ed3kUp = f(18);
    return s;
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

    if (m_usenetDownRate)
        s.usenetDown = std::max(0.0f, m_usenetDownRate());
    const EngineRates bt = btRates();
    s.btDown = bt.down;
    s.btUp = bt.up;
    const EngineRates e3 = clamped(m_ed3kRates);
    s.ed3kDown = e3.down;
    s.ed3kUp = e3.up;

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
    // The toolbar shows the totals of every network
    if (m_usenetDownRate)
        s.down += std::max(0.0f, m_usenetDownRate());
    const EngineRates bt = btRates();
    s.down += bt.down;
    s.up += bt.up;
    // ed3k's sent bytes are counted in eD2K's upload rate already (noteBytesSentOutsideTheLoop)
    s.down += clamped(m_ed3kRates).down;

    m_speed.push_back(s);
    if (m_speed.size() > kSpeedCapacity)
        m_speed.pop_front();
}

EngineRates StatsHistory::btRates() const
{
    // if (!m_btRates)
    //     return {};
    // const EngineRates r = m_btRates();
    // return {std::max(0.0f, r.down), std::max(0.0f, r.up)};
    return clamped(m_btRates);   // shared with ed3k
}

EngineRates StatsHistory::clamped(const std::function<EngineRates()>& source)
{
    if (!source)
        return {};
    const EngineRates r = source();
    return {std::max(0.0f, r.down), std::max(0.0f, r.up)};
}

} // namespace eMule
