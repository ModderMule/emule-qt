#pragma once

/// @file UsenetStatistics.h
/// @brief How Usenet engine events become statistics.
///
/// The counters live in core — Statistics::usenetSession() for this session,
/// Preferences for the banked total — so reset, restore and the periodic flush
/// need nothing from this module. What lives here is the judgement: which
/// result was a miss, which a fault and which our own doing, plus the two
/// things core has no business holding — the once-a-second sampler and the
/// per-account session figures.
///
/// A plain class: no signals, and every call is on the daemon thread. (Do not
/// write the moc macro name in this comment; scripts/sync_module_vcxproj.py
/// greps whole files for it.) Counting is a no-op while theApp.statistics is
/// null, which is what a bare queue in a test has unless it installs one.

#include "post/UsenetPostProcessor.h"
#include "queue/UsenetHealth.h"
#include "stats/NetworkCounters.h"

#include <QHash>
#include <QList>
#include <QString>

namespace eMule::usenet {

struct UsenetFetchResult;
class UsenetQueueItem;

/// The Statistics "Queue" rows. Sizes are the queue's own: encoded for the
/// total, as the Size column shows, decoded for what has arrived.
struct UsenetQueueSummary {
    int count = 0;
    int downloading = 0;
    int queued = 0;
    int paused = 0;
    int checking = 0;
    int postProcessing = 0;
    int failed = 0;
    int complete = 0;
    qint64 totalBytes = 0;
    qint64 downloadedBytes = 0;
    qint64 leftBytes = 0;
};

[[nodiscard]] UsenetQueueSummary summarizeQueue(const QList<const UsenetQueueItem*>& items);

class UsenetStatistics {
public:
    /// One finished fetch, download or probe, whatever came of it. Called for
    /// results whose item is already gone too: those bytes were still spent.
    void noteResult(const UsenetFetchResult& result);

    void noteItemFinished(bool success, qint64 bytes);

    /// A post-processing round came back. Counts the terminal verify and the
    /// unpack; a NeedMoreBlocks round is not terminal and counts nothing.
    void notePostFinished(const UsenetPostResult& result);

    /// Wall time spent in @p stage. Idle and Staging are not reported.
    void addPostStageTime(PostStage stage, qint64 ms);

    /// An NZB offered to the queue, and what came of it. A Failed add is not
    /// counted — the watch folder and feeds retry it, and every retry would.
    void noteAdd(UsenetAddOrigin origin, UsenetAddOutcome outcome);

    /// Add @p n to one Sum field, for the events that need no judgement.
    void bump(uint64 UsenetCounters::* field, uint64 n = 1);

    /// The queue's scheduler tick: download time, and once a second the peaks.
    void tick(qint64 elapsedMs, qint64 rateBytesPerSec, int openConnections);

    /// The rate window starts empty on (re)start; hold the peaks off until it
    /// has filled, or one burst stands as the session maximum.
    void restartSampling();

    /// This session's figures per NewsServer::accountId. Not persisted — the
    /// all-time bytes are the billing meter's.
    [[nodiscard]] const QHash<QString, UsenetServerCounters>& servers() const { return m_servers; }

private:
    [[nodiscard]] static UsenetCounters* counters();

    QHash<QString, UsenetServerCounters> m_servers;
    qint64 m_sinceRestartMs = 0;
    qint64 m_sinceSampleMs = 0;
};

} // namespace eMule::usenet
