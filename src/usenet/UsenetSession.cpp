#include "UsenetSession.h"

#include "nntp/NntpServerPool.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueStore.h"
#include "queue/UsenetWatchFolder.h"
#include "app/AppContext.h"
#include "net/ProxySettings.h"
#include "nzb/SubjectParser.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <algorithm>
#include <cstdlib>

namespace eMule::usenet {

UsenetSession* theUsenetSession = nullptr;

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
    connect(m_queue.get(), &UsenetQueue::articleDecoded, this, &UsenetSession::articleDecoded);

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
    logUsenet(QStringLiteral("Usenet: engine started, %1 server(s) configured")
                .arg(configured));
    if (configured == 0) {
        logUsenet(QStringLiteral("Usenet: no news servers configured — "
                               "add one under Options > Usenet"));
    }

    m_queue->start();
}

void UsenetSession::stop()
{
    if (!m_running)
        return;

    // The BandwidthCoordinator sees the stopped engine as idle on its next tick
    // and hands its share back to the others.
    m_running = false;

    m_queue->stop();
    m_pool->closeIdleConnections();
    logUsenet(QStringLiteral("Usenet: engine stopped"));
}

void UsenetSession::onNetworkRouteChanged()
{
    if (m_pool)
        m_pool->closeIdleConnections();
    if (m_queue)
        m_queue->rebuildForRouteChange();
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

int UsenetSession::refetchRange(const QString& itemId, int fileIndex, qint64 offset,
                                qint64 length, const QString& excludeServerKey)
{
    return m_queue->refetchRange(itemId, fileIndex, offset, length, excludeServerKey);
}

int UsenetSession::acceptExternalRange(const QString& itemId, int fileIndex, qint64 offset,
                                       qint64 length)
{
    return m_queue->acceptExternalRange(itemId, fileIndex, offset, length);
}

void UsenetSession::remapCategories(const QHash<uint32, uint32>& oldToNew)
{
    // Running: the live items are the truth and persist themselves. Not running:
    // the queue is holding nothing, and the sidecars on disk are all there is.
    if (m_queue && m_queue->isRunning()) {
        m_queue->remapCategories(oldToNew);
        return;
    }

    if (m_queue)
        m_queue->flushState();   // pending writes first, or they would undo the remap
    const int changed = UsenetQueueStore::remapCategories(oldToNew);
    if (changed > 0) {
        logUsenet(QStringLiteral("Usenet: renumbered the category of %1 stored item(s)")
                    .arg(changed));
    }
}

UsenetSession::DownloadSplit UsenetSession::computeDownloadSplit(uint32 ceilingKb,
                                                                int usenetSharePercent,
                                                                EngineDemand usenet,
                                                                EngineDemand ed2k,
                                                                SplitState* state)
{
    DownloadSplit split;
    split.ceilingKb = ceilingKb;

    PairState pair;
    if (state)
        pair = {state->ed2kReserveKb, state->usenetReserveKb, state->ed2kCapKb, state->usenetCapKb};
    const PairSplit caps = computePairSplit(ceilingKb, usenetSharePercent, ed2k, usenet,
                                            state ? &pair : nullptr);
    if (state)
        *state = {pair.secondReserveKb, pair.firstReserveKb, pair.secondCapKb, pair.firstCapKb};

    // 0 means unlimited on the download side, so both engines run free
    if (ceilingKb == 0)
        return split;

    // ED2K's -1 is the same cap as the ceiling but tracks a maxDownload change
    // live, since DownloadQueue re-reads it every 100 ms. Usenet has no such
    // sentinel — its 0 is unlimited, which would ignore maxDownload outright — so
    // it always gets a number.
    if (usenet.active)
        split.ed2kBudgetKb = caps.firstCapKb;
    split.usenetLimitBytes = caps.secondCapKb * 1024;
    return split;
}

} // namespace eMule::usenet
