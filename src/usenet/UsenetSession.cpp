#include "UsenetSession.h"

#include "nntp/NntpServerPool.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueStore.h"
#include "queue/UsenetWatchFolder.h"
#include "app/AppContext.h"
#include "net/ProxySettings.h"
#include "nzb/SubjectParser.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"
#include "utils/Log.h"

#include <QTimer>

#include <algorithm>
#include <cstdlib>

namespace eMule::usenet {

UsenetSession* theUsenetSession = nullptr;

namespace {

/// How often the ED2K/Usenet budget split is recomputed. Lending goes both ways,
/// so this bounds how long a just-woken engine sits below its floor while the
/// other still holds the line. The recompute itself is a few integer ops.
constexpr int kBandwidthTickMs = 500;

/// Split log gate: a move smaller than this, in percent of the old cap, is noise
/// from the rate measurements and not worth a line.
constexpr qint64 kSplitLogChangePercent = 10;

/// And never more often than this. log.log has no timestamps, so a line per tick
/// would bury everything else without saying anything the last one did not.
constexpr qint64 kSplitLogMinIntervalMs = 5000;

/// Whether a cap moved by more than kSplitLogChangePercent.
bool movedMaterially(qint64 oldKb, qint64 newKb)
{
    return std::abs(newKb - oldKb) * 100 > std::max<qint64>(oldKb, 1) * kSplitLogChangePercent;
}

} // namespace

UsenetSession::UsenetSession(QObject* parent)
    : QObject(parent)
    , m_pool(std::make_unique<NntpServerPool>())
    , m_queue(std::make_unique<UsenetQueue>())
    , m_watchFolder(std::make_unique<UsenetWatchFolder>(m_queue.get()))
{
    connect(m_queue.get(), &UsenetQueue::itemChanged, this, &UsenetSession::itemChanged);
    connect(m_queue.get(), &UsenetQueue::itemAdded, this, &UsenetSession::itemAdded);
    connect(m_queue.get(), &UsenetQueue::itemRemoved, this, &UsenetSession::itemRemoved);
    connect(m_queue.get(), &UsenetQueue::itemFinished, this, &UsenetSession::itemFinished);
    connect(m_queue.get(), &UsenetQueue::enginePausedChanged,
            this, &UsenetSession::enginePausedChanged);

    applyPreferences();
}

UsenetSession::~UsenetSession()
{
    stop();
}

void UsenetSession::start()
{
    if (m_running)
        return;

    m_running = true;
    applyPreferences();

    const int configured = m_pool->servers().size();
    logInfo(QStringLiteral("Usenet: engine started, %1 server(s) configured")
                .arg(configured));
    if (configured == 0) {
        logInfo(QStringLiteral("Usenet: no news servers configured — "
                               "add one under Options > Usenet"));
    }

    m_queue->start();

    if (!m_bandwidthTimer) {
        m_bandwidthTimer = new QTimer(this);
        m_bandwidthTimer->setInterval(kBandwidthTickMs);
        connect(m_bandwidthTimer, &QTimer::timeout,
                this, &UsenetSession::updateBandwidthSplit);
    }
    m_bandwidthTimer->start();
    updateBandwidthSplit();
}

void UsenetSession::stop()
{
    if (!m_running)
        return;

    m_running = false;

    if (m_bandwidthTimer)
        m_bandwidthTimer->stop();

    // Hand the whole line back to ED2K. Leaving the split in place would throttle
    // it to a share of a budget nothing else is using any more, with nothing in
    // the UI to explain why.
    thePrefs.setEd2kDownloadBudget(-1);

    // Quietly: "engine stopped" below already says why, and a restart should log
    // its first split afresh rather than diff it against a stale one.
    m_split = {};
    m_loggedSplit = {};
    m_splitLogClock.invalidate();

    m_queue->stop();
    m_pool->closeIdleConnections();
    logInfo(QStringLiteral("Usenet: engine stopped"));
}

void UsenetSession::applyPreferences()
{
    m_pool->setRetryInterval(thePrefs.usenetRetryIntervalSeconds());
    m_pool->setServers(thePrefs.usenetServers());

    // Compile the subject rules and throw them away. The only reason is the log:
    // a pattern the user got wrong is named here, at start-up, instead of the
    // first time an NZB happens to be added. Once per *change* — this function
    // runs from the constructor, from start() and again on every SetPreferences,
    // and the same three warnings four times says nothing the first set did not.
    if (const quint64 rev = Preferences::usenetSubjectPatternsRevision();
        rev != m_subjectPatternRevision) {
        m_subjectPatternRevision = rev;
        (void) SubjectRuleSet::compile(thePrefs.usenetSubjectPatterns());
    }

    // Independent of the engine's own on/off switch. A folder full of .nzb files
    // is worth picking up even with downloading paused — the items simply wait,
    // which is what the queue is for.
    if (m_watchFolder)
        m_watchFolder->applyPreferences();

    if (m_queue) {
        m_queue->setEnginePaused(thePrefs.usenetPaused());

        // Before applyServers(): its worker rebuild is what hands the route out.
        m_queue->setProxy(toNetworkProxy(thePrefs.usenetProxySettings()));
        m_queue->applyServers(thePrefs.usenetServers(),
                              thePrefs.usenetRetryIntervalSeconds());

        // Read once here rather than inside the pipeline: a post-processing job
        // crosses a thread boundary and has to carry a consistent snapshot, not
        // reach back into preferences from the wrong thread mid-repair.
        m_queue->setPostProcessingOptions({
            .par2 = thePrefs.usenetPar2Repair(),
            .rename = thePrefs.usenetPar2RenameFiles(),
            .unpack = thePrefs.usenetUnpack(),
            .cleanup = thePrefs.usenetCleanupAfterUnpack(),
            .directUnpack = thePrefs.usenetDirectUnpack(),
            .sfv = thePrefs.usenetSfvCheck(),
        });
    }
}

void UsenetSession::remapCategories(const QHash<uint32, uint32>& oldToNew)
{
    // Running: the live items are the truth and persist themselves. Not running:
    // the queue is holding nothing, and the sidecars on disk are all there is.
    if (m_queue && m_queue->isRunning()) {
        m_queue->remapCategories(oldToNew);
        return;
    }

    const int changed = UsenetQueueStore::remapCategories(oldToNew);
    if (changed > 0) {
        logInfo(QStringLiteral("Usenet: renumbered the category of %1 stored item(s)")
                    .arg(changed));
    }
}

UsenetSession::DownloadSplit UsenetSession::computeDownloadSplit(uint32 ceilingKb,
                                                                int usenetSharePercent,
                                                                EngineDemand usenet,
                                                                EngineDemand ed2k)
{
    DownloadSplit split;
    split.ceilingKb = ceilingKb;

    // 0 means unlimited on the download side — there is no UNLIMITED sentinel the
    // way there is for upload — so there is nothing to divide and both engines
    // run free.
    if (ceilingKb == 0)
        return split;

    const qint64 ceiling = ceilingKb;
    const int percent = std::clamp(usenetSharePercent, 1, 99);

    // Each floor at least 1 KB/s: a cap of 0 reads as unlimited to both consumers,
    // so a tiny ceiling must round up, never down.
    const qint64 usenetFloor =
        std::clamp<qint64>(ceiling * percent / 100, 1, std::max<qint64>(1, ceiling - 1));
    const qint64 ed2kFloor = std::max<qint64>(1, ceiling - usenetFloor);

    // What an engine holds back from the other: what it measurably uses plus a
    // quarter, so the loan never becomes the thing capping it and it can grow a
    // step per tick. Never less than a quarter of its floor, so an engine that is
    // busy but momentarily at zero — between articles, sources still queueing —
    // can restart without waiting on a tick. Never more than its floor, which is
    // what makes the share hold once both engines are saturated.
    const auto reserve = [](const EngineDemand& d, qint64 floorKb) -> qint64 {
        if (!d.active)
            return 0;
        const qint64 measuredKb = std::max<qint64>(0, d.rateBytesPerSec) / 1024;
        return std::clamp(measuredKb + measuredKb / 4,
                          std::max<qint64>(1, floorKb / 4), floorKb);
    };

    // Symmetric: each engine is held back only because the other is active, and
    // only by what the other reserves. An idle engine lends its whole share.
    //
    // ED2K's -1 is the same cap as the ceiling but tracks a maxDownload change
    // live, since DownloadQueue re-reads it every 100 ms. Usenet has no such
    // sentinel — its 0 is unlimited, which would ignore maxDownload outright — so
    // it always gets a number.
    if (usenet.active)
        split.ed2kBudgetKb = std::max<qint64>(1, ceiling - reserve(usenet, usenetFloor));
    split.usenetLimitBytes = std::max<qint64>(1, ceiling - reserve(ed2k, ed2kFloor)) * 1024;
    return split;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void UsenetSession::updateBandwidthSplit()
{
    const EngineDemand usenet{m_queue->hasActiveDownloads(), m_queue->currentRate()};

    // Core runs on this thread, so the queue can be read directly. hasActiveTransfers
    // counts sources actually sending, which a throttle slows but never zeroes.
    EngineDemand ed2k;
    if (theApp.downloadQueue) {
        ed2k.active = theApp.downloadQueue->hasActiveTransfers();
        ed2k.rateBytesPerSec = theApp.downloadQueue->datarate();
    }

    m_split = computeDownloadSplit(thePrefs.maxDownload(),
                                   thePrefs.usenetDownloadSharePercent(), usenet, ed2k);

    thePrefs.setEd2kDownloadBudget(m_split.ed2kBudgetKb);
    m_queue->setRateLimit(m_split.usenetLimitBytes);
    logSplitChange(m_split);
}

void UsenetSession::logSplitChange(const DownloadSplit& split)
{
    const bool was = m_loggedSplit.isThrottling();
    const bool now = split.isThrottling();

    // Nothing throttled then or now: a run that never contends says nothing.
    if (!was && !now)
        return;

    // Compared with what was last *logged*, not last published, so drift that
    // arrives in small steps still surfaces once it adds up.
    const bool changed = was != now
        || split.ceilingKb != m_loggedSplit.ceilingKb
        || movedMaterially(m_loggedSplit.usenetKb(), split.usenetKb())
        || movedMaterially(m_loggedSplit.ed2kKb(), split.ed2kKb());
    if (!changed)
        return;
    if (m_splitLogClock.isValid() && m_splitLogClock.elapsed() < kSplitLogMinIntervalMs)
        return;

    if (now) {
        logInfo(QStringLiteral("Bandwidth: download limit %1 KB/s shared — "
                               "Usenet up to %2 KB/s, eD2K up to %3 KB/s")
                    .arg(split.ceilingKb)
                    .arg(split.usenetKb())
                    .arg(split.ed2kKb()));
    } else {
        logInfo(QStringLiteral("Bandwidth: download limit no longer shared — "
                               "Usenet and eD2K may each use all of it"));
    }

    m_loggedSplit = split;
    m_splitLogClock.start();
}

} // namespace eMule::usenet
