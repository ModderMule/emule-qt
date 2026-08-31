#include "UsenetSession.h"

#include "nntp/NntpServerPool.h"
#include "queue/UsenetQueue.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QTimer>

#include <algorithm>

namespace eMule::usenet {

UsenetSession* theUsenetSession = nullptr;

namespace {

/// How often the ED2K/Usenet budget split is recomputed. Slow enough to be free,
/// fast enough that starting a download does not leave the other engine holding
/// the whole line for long.
constexpr int kBandwidthTickMs = 1000;

} // namespace

UsenetSession::UsenetSession(QObject* parent)
    : QObject(parent)
    , m_pool(std::make_unique<NntpServerPool>())
    , m_queue(std::make_unique<UsenetQueue>())
{
    connect(m_queue.get(), &UsenetQueue::itemChanged, this, &UsenetSession::itemChanged);
    connect(m_queue.get(), &UsenetQueue::itemAdded, this, &UsenetSession::itemAdded);
    connect(m_queue.get(), &UsenetQueue::itemRemoved, this, &UsenetSession::itemRemoved);
    connect(m_queue.get(), &UsenetQueue::itemFinished, this, &UsenetSession::itemFinished);

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

    m_queue->stop();
    m_pool->closeIdleConnections();
    logInfo(QStringLiteral("Usenet: engine stopped"));
}

void UsenetSession::applyPreferences()
{
    m_pool->setRetryInterval(thePrefs.usenetRetryIntervalSeconds());
    m_pool->setServers(thePrefs.usenetServers());

    if (m_queue) {
        m_queue->applyServers(thePrefs.usenetServers(),
                              thePrefs.usenetRetryIntervalSeconds());

        // Read once here rather than inside the pipeline: a post-processing job
        // crosses a thread boundary and has to carry a consistent snapshot, not
        // reach back into preferences from the wrong thread mid-repair.
        m_queue->setPostProcessingOptions(thePrefs.usenetPar2Repair(),
                                          thePrefs.usenetPar2RenameFiles(),
                                          thePrefs.usenetUnpack(),
                                          thePrefs.usenetCleanupAfterUnpack());
    }
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void UsenetSession::updateBandwidthSplit()
{
    const uint32 ceiling = thePrefs.maxDownload();   // KB/s, 0 = unlimited

    // 0 means unlimited on the download side — there is no UNLIMITED sentinel the
    // way there is for upload — so there is nothing to divide and both engines
    // run free.
    if (ceiling == 0) {
        thePrefs.setEd2kDownloadBudget(-1);
        m_queue->setRateLimit(0);
        return;
    }

    if (!m_queue->hasActiveDownloads()) {
        thePrefs.setEd2kDownloadBudget(-1);
        m_queue->setRateLimit(0);
        return;
    }

    const int percent = std::clamp(thePrefs.usenetDownloadSharePercent(), 1, 99);
    qint64 usenetKb = qint64(ceiling) * percent / 100;
    usenetKb = std::max<qint64>(1, usenetKb);

    // Give-back: Usenet rarely uses its whole share — every server blocked, or a
    // release down to its last few articles — and a strict split would idle the
    // rest of the line. Lend ED2K whatever Usenet measurably is not using, with a
    // little headroom so the loan does not itself become the thing capping it.
    const qint64 measuredKb = m_queue->currentRate() / 1024;
    const qint64 headroomKb = std::max<qint64>(measuredKb + measuredKb / 4, 1);
    const qint64 effectiveUsenetKb = std::min(usenetKb, std::max(headroomKb, usenetKb / 4));

    const qint64 ed2kKb = std::max<qint64>(1, qint64(ceiling) - effectiveUsenetKb);

    thePrefs.setEd2kDownloadBudget(ed2kKb);
    m_queue->setRateLimit(usenetKb * 1024);
}

} // namespace eMule::usenet
