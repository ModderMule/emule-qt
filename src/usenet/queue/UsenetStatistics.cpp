#include "queue/UsenetStatistics.h"

#include "queue/UsenetQueueItem.h"
#include "queue/UsenetWorker.h"

#include "app/AppContext.h"
#include "stats/Statistics.h"

#include <algorithm>

namespace eMule::usenet {

namespace {

/// The rate window's span (TickRateWindow: 8 x 250 ms). Until it has filled,
/// its mean is over fewer ticks and a single burst reads as the line's speed.
constexpr qint64 kRateWarmupMs = 2000;

constexpr qint64 kSampleIntervalMs = 1000;

uint64 nonNegative(qint64 v)
{
    return v > 0 ? static_cast<uint64>(v) : 0;
}

} // namespace

UsenetQueueSummary summarizeQueue(const QList<const UsenetQueueItem*>& items)
{
    UsenetQueueSummary s;
    for (const UsenetQueueItem* item : items) {
        if (!item)
            continue;
        ++s.count;
        const qint64 total = item->totalEncodedBytes();
        const qint64 decoded = item->decodedBytes();
        s.totalBytes += total;
        s.downloadedBytes += decoded;

        switch (item->status) {
        case UsenetItemStatus::Queued:      ++s.queued; break;
        case UsenetItemStatus::Downloading: ++s.downloading; break;
        case UsenetItemStatus::Paused:      ++s.paused; break;
        case UsenetItemStatus::Checking:    ++s.checking; break;
        case UsenetItemStatus::Verifying:
        case UsenetItemStatus::Repairing:
        case UsenetItemStatus::Unpacking:   ++s.postProcessing; break;
        case UsenetItemStatus::Failed:      ++s.failed; break;
        case UsenetItemStatus::Complete:    ++s.complete; break;
        }

        // Encoded runs a few percent over decoded, so a finished release would
        // otherwise leave that yEnc overhead behind as "left to download".
        if (item->status != UsenetItemStatus::Complete)
            s.leftBytes += std::max<qint64>(0, total - decoded);
    }
    return s;
}

void UsenetStatistics::noteResult(const UsenetFetchResult& result)
{
    UsenetCounters* c = counters();
    UsenetServerCounters* server =
        result.accountId.isEmpty() ? nullptr : &m_servers[result.accountId];

    // Whatever else happened, these bytes crossed the wire — the same figure
    // the billing meter charges.
    const uint64 raw = nonNegative(result.rawBytes);
    if (c)
        c->wireBytes += raw;
    if (server)
        server->wireBytes += raw;

    // Nothing could be leased, so nobody was asked anything; or we tore the
    // worker down mid-article (engine stop, settings save) and nobody answered.
    if (result.noServerAvailable || result.aborted)
        return;

    if (result.probeOnly) {
        if (c)
            ++c->statProbes;
        return;
    }

    if (result.error == NntpError::None) {
        const uint64 decoded = nonNegative(result.decodedBytes);
        if (c) {
            ++c->articlesDownloaded;
            c->decodedBytes += decoded;
        }
        if (server) {
            ++server->articles;
            server->decodedBytes += decoded;
        }
        return;
    }

    // Before the 430 test below, which it would otherwise be swallowed by:
    // a damaged copy escalates the same way, but it is a different fault.
    if (result.error == NntpError::ArticleCorrupt) {
        if (c)
            ++c->articlesCorrupt;
        if (server)
            ++server->corrupt;
        return;
    }

    // This account does not have it; the ladder asks the next one.
    if (escalatesToNextLevel(result.error)) {
        if (c)
            ++c->articlesNotFound;
        if (server)
            ++server->notFound;
        return;
    }

    // A local fault with no connection behind it (the target file would not
    // open): no provider did anything wrong. Nor did one when the proxy in front
    // of every account refused the connection.
    if (result.serverKey.isEmpty() || result.error == NntpError::ProxyFailed)
        return;

    if (c)
        ++c->connectionErrors;
    if (server)
        ++server->errors;
}

void UsenetStatistics::noteItemFinished(bool success, qint64 bytes)
{
    UsenetCounters* c = counters();
    if (!c)
        return;
    if (success) {
        ++c->itemsCompleted;
        c->completedBytes += nonNegative(bytes);
    } else {
        ++c->itemsFailed;
    }
}

void UsenetStatistics::notePostFinished(const UsenetPostResult& result)
{
    UsenetCounters* c = counters();
    if (!c)
        return;

    switch (result.par2Outcome) {
    case Par2Outcome::Clean:
    case Par2Outcome::Repaired:
        ++c->par2Verified;
        break;
    case Par2Outcome::RepairFailed:
        ++c->par2Verified;
        ++c->par2RepairFailed;
        break;
    default:
        break;
    }

    // Not the outcome: par2's rename pass can repair on its own, after which
    // the verify reports Clean.
    if (result.repaired)
        ++c->par2Repaired;
    c->par2BlocksRepaired += nonNegative(result.blocksRepaired);

    switch (result.unpackOutcome) {
    case UsenetUnpackOutcome::Unpacked:         ++c->unpackOk; break;
    case UsenetUnpackOutcome::Failed:           ++c->unpackFailed; break;
    case UsenetUnpackOutcome::PasswordRequired: ++c->unpackPassword; break;
    case UsenetUnpackOutcome::NotRun:
    case UsenetUnpackOutcome::NothingToUnpack:  break;
    }
}

void UsenetStatistics::addPostStageTime(PostStage stage, qint64 ms)
{
    UsenetCounters* c = counters();
    if (!c || ms <= 0)
        return;
    switch (stage) {
    case PostStage::Verifying: c->verifyMs += uint64(ms); break;
    case PostStage::Repairing: c->repairMs += uint64(ms); break;
    case PostStage::Unpacking: c->unpackMs += uint64(ms); break;
    case PostStage::Idle:
    case PostStage::Staging:   break;
    }
}

void UsenetStatistics::noteAdd(UsenetAddOrigin origin, UsenetAddOutcome outcome)
{
    UsenetCounters* c = counters();
    if (!c)
        return;

    switch (outcome) {
    case UsenetAddOutcome::Added:
        switch (origin) {
        case UsenetAddOrigin::File:        ++c->nzbFromFile; break;
        case UsenetAddOrigin::Url:         ++c->nzbFromUrl; break;
        case UsenetAddOrigin::WatchFolder: ++c->nzbFromWatch; break;
        case UsenetAddOrigin::Feed:        ++c->nzbFromFeed; break;
        case UsenetAddOrigin::IndexerGrab: ++c->nzbFromIndexer; break;
        }
        break;
    case UsenetAddOutcome::Duplicate:         ++c->nzbDuplicate; break;
    case UsenetAddOutcome::AlreadyDownloaded: ++c->nzbAlreadyDownloaded; break;
    case UsenetAddOutcome::Invalid:           ++c->nzbInvalid; break;
    case UsenetAddOutcome::Failed:            break;
    }
}

void UsenetStatistics::bump(uint64 UsenetCounters::* field, uint64 n)
{
    if (UsenetCounters* c = counters())
        c->*field += n;
}

void UsenetStatistics::tick(qint64 elapsedMs, qint64 rateBytesPerSec, int openConnections)
{
    UsenetCounters* c = counters();
    if (!c || elapsedMs <= 0)
        return;

    if (rateBytesPerSec > 0)
        c->downloadTimeMs += uint64(elapsedMs);

    if (m_sinceRestartMs < kRateWarmupMs) {
        m_sinceRestartMs += elapsedMs;
        return;
    }
    m_sinceSampleMs += elapsedMs;
    if (m_sinceSampleMs < kSampleIntervalMs)
        return;
    m_sinceSampleMs = 0;

    raiseCounter(c->maxDownRate, nonNegative(rateBytesPerSec));
    raiseCounter(c->peakConnections, nonNegative(openConnections));
}

void UsenetStatistics::restartSampling()
{
    m_sinceRestartMs = 0;
    m_sinceSampleMs = 0;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

UsenetCounters* UsenetStatistics::counters()
{
    return theApp.statistics ? &theApp.statistics->usenetSession() : nullptr;
}

} // namespace eMule::usenet
