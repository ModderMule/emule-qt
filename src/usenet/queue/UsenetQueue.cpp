#include "queue/UsenetQueue.h"

#include "nntp/NntpServerPool.h"
#include "nntp/NntpSocket.h"
#include "nzb/NzbFile.h"
#include "post/Par2Verifier.h"
#include "post/UsenetUnpacker.h"
#include "queue/ArticleWriter.h"
#include "queue/UsenetQueueStore.h"

#include "app/AppContext.h"
#include "files/SharedFileList.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/PathUtils.h"
#include "utils/StringUtils.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace eMule::usenet {

namespace {

/// Scheduler period. Fast enough that a freed connection is reused promptly,
/// slow enough that an idle queue costs nothing — the dispatch also runs on every
/// completed segment, so this timer only has to cover the "nothing came back"
/// case (every server blocked, every connection busy).
constexpr int kTickMs = 250;

/// How many times a *connection* fault is retried on the same level before the
/// segment is given up. Transport faults cure themselves — "too many connections"
/// is the commonest one — so this is generous.
constexpr int kMaxTransportRetries = 6;

/// How many times one segment may fail to *write* before the item is failed with
/// the local error. A full disk parks the queue instead and never reaches this;
/// what this bounds is the fault that parking cannot fix — a read-only volume, a
/// permission, a path that has gone — which would otherwise retry forever.
constexpr int kMaxWriteFailures = 3;

/// How often the free-space question is actually put to the file system. The
/// tick runs four times a second and QStorageInfo is a syscall; the answer does
/// not change that fast.
constexpr int kDiskCheckIntervalMs = 2000;

/// How far above the floor space has to climb before the queue starts again.
/// Without it a release resumes, writes one article, drops back under the floor
/// and parks again, once per article.
constexpr qint64 kDiskUnparkHeadroom = 16 * 1024 * 1024;

/// Upper bound on worker threads. Beyond this the TLS and decode work is no
/// longer the constraint and the thread count is just context switching.
constexpr int kMaxWorkers = 8;

/// How long one stream request keeps an item ahead of the priority field. Long
/// enough that a player's gaps between Range requests do not drop the boost,
/// short enough that a closed player stops starving the rest of the queue
/// without anything having to notice it closed.
constexpr qint64 kStreamingBoostMs = 30000;

/// Backstop on the download -> verify -> download cycle. requestPar2Volumes()
/// returning false is the real terminator; this catches the case where it keeps
/// finding volumes that somehow do not improve the block count.
constexpr int kMaxPar2Rounds = 8;

/// Refuse to read an index .par2 larger than this for its file list.
///
/// listFiles() scans no data, so the cost is reading the packets — but it is
/// blocking IO on the daemon thread, and an index this size means a release big
/// enough that post-processing can do the naming instead. A real one is
/// kilobytes.
constexpr qint64 kMaxPar2IndexBytes = 8 * 1024 * 1024;

/// Ceiling on how many articles the naming prefetch takes per file before giving
/// up on reaching PAR2's 16 KiB window. Real posts need one; this only stops an
/// NZB full of tiny segments from turning the prefetch into the whole download.
constexpr int kMaxPrefetchArticles = 8;

/// Flush the usage meters after this much unrecorded traffic, whatever the
/// timer says. An unclean exit otherwise costs a whole save interval of prepaid
/// block credit — 300 MB at 5 MB/s.
constexpr qint64 kUsageFlushBytes = 256 * 1024 * 1024;

/// Ceiling on one release's sampled probe. A release of 50-100 files costs
/// 50-100 status lines; this is only reached by something pathological, and a
/// pathological NZB must not turn into a thousand round trips before the first
/// article is fetched.
constexpr int kMaxSampleProbes = 200;

/// Share of each worker's capacity reserved for probes, as a divisor.
///
/// Reserved rather than leftover: a busy queue never *has* leftover slots, so a
/// probe dispatched only from what downloads did not want would wait for the
/// whole queue to drain. A quarter is enough to finish a sampled probe in a
/// round trip or three and small enough that downloads do not notice.
constexpr int kProbeCapacityDivisor = 4;

/// Create @p path and its directory, empty. Returns false with @p error set.
[[nodiscard]] bool createTargetFile(const QString& path, QString& error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());

    ArticleWriter writer;
    QString openError;
    if (!writer.open(path, openError)) {
        error = QObject::tr("Cannot create %1: %2").arg(path, openError);
        return false;
    }
    writer.close();
    return true;
}

} // namespace

UsenetQueue::UsenetQueue(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<UsenetFetchRequest>("eMule::usenet::UsenetFetchRequest");
    qRegisterMetaType<UsenetFetchResult>("eMule::usenet::UsenetFetchResult");
    qRegisterMetaType<QList<eMule::NewsServer>>("QList<eMule::NewsServer>");
    qRegisterMetaType<UsenetEncryptedPreviewJob>("eMule::usenet::UsenetEncryptedPreviewJob");
    qRegisterMetaType<UsenetEncryptedPreviewResult>(
        "eMule::usenet::UsenetEncryptedPreviewResult");
}

UsenetQueue::~UsenetQueue()
{
    stop();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void UsenetQueue::start()
{
    if (m_running)
        return;
    m_running = true;

    // Meters before anything can spend: the base has to be on hand before the
    // first article lands, or this run's bytes would be added to zero.
    m_usage.load();
    m_history.load();
    m_usage.setAccounts(m_servers);
    m_usage.rollOverIfDue();
    refreshQuotaState();

    // Restore before the workers exist, so nothing dispatches into a half-built
    // queue.
    for (const QString& path : UsenetQueueStore::listStateFiles()) {
        auto item = std::make_unique<UsenetQueueItem>();
        QString error;
        if (!UsenetQueueStore::load(path, *item, error)) {
            logWarning(QStringLiteral("Usenet: ignoring unreadable queue state %1: %2")
                           .arg(path, error));
            continue;
        }
        item->initFileStates(thePrefs.usenetTempDir());
        // Re-derived rather than persisted: every message-id is already in the
        // sidecar, so a stored digest would only be a second thing that could
        // disagree with the first. Without this the duplicate guard is blind to
        // everything restored from disk, which after one restart is everything.
        item->articleDigest = nzbArticleDigest(item->nzb);
        item->releaseKey = nzbReleaseKey(item->name, item->nzb.totalEncodedBytes());

        auto rt = std::make_unique<ItemRuntime>();
        rt->item = std::move(item);
        rebuildPlan(*rt);
        m_items.push_back(std::move(rt));
    }

    if (!m_items.empty()) {
        logInfo(QStringLiteral("Usenet: restored %1 queued item(s)").arg(int(m_items.size())));
    }

    startWorkers();
    startPostProcessor();

    // The counter is process-wide and monotonic, so the first tick's delta must
    // not include everything read before this start.
    m_lastWireBytes = NntpSocket::totalWireBytesRead();
    m_stats.restartSampling();

    if (!m_tickTimer) {
        m_tickTimer = new QTimer(this);
        m_tickTimer->setInterval(kTickMs);
        connect(m_tickTimer, &QTimer::timeout, this, &UsenetQueue::onTick);
    }
    m_tickTimer->start();

    dispatch();
}

void UsenetQueue::stop()
{
    if (!m_running)
        return;
    m_running = false;

    if (m_tickTimer)
        m_tickTimer->stop();

    // Nothing is arriving any more; a restart must not read the last run's rate.
    m_rateWindow.clear();

    stopWorkers();
    stopPostProcessor();

    for (auto& rt : m_items) {
        cancelDirectUnpack(*rt);
        cancelEncryptedPreview(*rt);
    }

    // A probe does not survive the workers going away. Put the item back where
    // it was so the sidecar records something resumable — load() would demote it
    // anyway, but a queue stopped and restarted in-process never goes through
    // load() at all.
    for (auto& rt : m_items) {
        if (rt->item->status == UsenetItemStatus::Checking) {
            clearHealthCheck(*rt);
            rt->item->status = UsenetItemStatus::Queued;
            rt->item->stalledReason.clear();
        }
    }

    for (auto& rt : m_items)
        persist(*rt);
    m_items.clear();

    // Absolute, so this cannot double-count whatever the last timed flush wrote.
    m_usage.flush();
}

void UsenetQueue::applyServers(const QList<NewsServer>& servers, int retryIntervalSec)
{
    // A row configured with no connections can never be leased, so keeping it
    // would put a rung on the ladder that nothing can answer — every article
    // reaching it would stall rather than escalate.
    m_servers.clear();
    m_servers.reserve(servers.size());
    for (const NewsServer& s : servers) {
        if (s.enabled && s.isValid() && s.maxConnections <= 0) {
            logWarning(QStringLiteral("Usenet: ignoring %1 — it allows no connections")
                           .arg(s.displayName()));
            continue;
        }
        m_servers.append(s);
    }
    m_retryIntervalSec = retryIntervalSec;

    // One definition of the ladder, shared with every worker's pool. Deriving it
    // here rather than taking max(level) also drops the disabled rows the pool
    // never had, which is what used to make the two disagree.
    m_ladder = nntpLevelLadder(m_servers);

    int usable = 0;
    int optional = 0;
    for (const NewsServer& s : std::as_const(m_servers)) {
        if (!s.enabled || !s.isValid())
            continue;
        ++usable;
        if (s.optional)
            ++optional;
    }
    m_allServersOptional = usable > 0 && optional == usable;

    // Context and reset days for the meters, then the answer they feed. Counters
    // survive: this runs on every settings save, so re-seeding here would zero
    // every meter each time the user pressed OK.
    m_usage.setAccounts(m_servers);
    m_usage.rollOverIfDue();
    refreshQuotaState();
    unparkQuotaStalls();

    // Abandon any probe in flight. Its answer was about the old account list, and
    // the workers holding its requests are about to be torn down — without this
    // an item left Checking has nothing to finish it and sits there for good.
    // No verdict is the right outcome: a probe that cannot complete must not
    // stop a download.
    for (auto& rt : m_items) {
        if (rt->item->status != UsenetItemStatus::Checking)
            continue;
        const UsenetItemStatus resumeTo = rt->checkResumeStatus;
        clearHealthCheck(*rt);
        rt->item->status = resumeTo;
        rt->item->stalledReason.clear();
        emit itemChanged(rt->item->id);
    }

    if (!m_running)
        return;

    // A changed server list changes the worker count, and a worker cannot be
    // resized while it holds connections. Rebuild.
    stopWorkers();
    startWorkers();
    dispatch();
}

// ---------------------------------------------------------------------------
// Queue operations
// ---------------------------------------------------------------------------

QString UsenetQueue::addNzb(const QByteArray& data, const QString& name, QString& error,
                            const UsenetAddOptions& options, UsenetAddOutcome* outcome)
{
    const auto report = [outcome](UsenetAddOutcome value) {
        if (outcome)
            *outcome = value;
    };
    report(UsenetAddOutcome::Failed);

    NzbInfo nzb;
    if (!NzbFile::parse(data, nzb, error)) {
        report(UsenetAddOutcome::Invalid);
        return {};
    }

    if (nzb.isEmpty()) {
        error = tr("The NZB contains no files.");
        report(UsenetAddOutcome::Invalid);
        return {};
    }

    QString displayName = name.isEmpty() ? nzb.name : name;

    // `Release{{secret}}.nzb`, the convention NZBGet writes. This used to live
    // in NzbFile::parseFile(), which nothing but tests ever called — every real
    // intake path comes through here with the name as a separate argument, so
    // the convention has never once fired in the running program.
    const QString namePassword = NzbFile::takePasswordFromName(displayName);

    if (displayName.isEmpty())
        displayName = tr("Usenet download");

    if (options.source == UsenetAddSource::Manual && !options.password.isEmpty())
        nzb.password = options.password;
    else if (nzb.password.isEmpty())
        nzb.password = namePassword.isEmpty() ? options.password : namePassword;

    // Before anything is created on disk: the preallocation loop below does not
    // clean up after itself, so a refusal must happen ahead of it.
    const UsenetAddOutcome verdict = findDuplicate(nzb, displayName, options.force, error);
    if (verdict != UsenetAddOutcome::Added) {
        logInfo(QStringLiteral("Usenet: not adding \"%1\": %2").arg(displayName, error));
        report(verdict);
        return {};
    }

    auto item = std::make_unique<UsenetQueueItem>();
    item->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item->name = displayName;
    item->nzb = std::move(nzb);
    item->nzb.name = item->name;
    // Enforced here rather than at each intake path, for the reason the duplicate
    // guard is: every present and future automatic caller inherits it by
    // construction. quotaParked is the counter-example — as a flag its two call
    // sites had to be remembered separately, and forgetting one costs the user a
    // share of their line indefinitely.
    item->status = options.paused
                           || (options.source == UsenetAddSource::Automatic
                               && thePrefs.usenetAutoAddPaused())
                       ? UsenetItemStatus::Paused
                       : UsenetItemStatus::Queued;
    item->priority = clampUsenetPriority(options.priority);
    // Same shape as DownloadQueue::addDownload(): a category the caller picked
    // is never overridden, and only 0 -- "nobody chose" -- reaches the matcher.
    // Here rather than at each intake path for the reason directly above.
    item->category = options.category > 0
                         ? options.category
                         : matchAutoCategory(thePrefs.categories(), item->name);
    if (options.category <= 0 && item->category > 0) {
        logInfo(QStringLiteral("Usenet: auto-categorised %1 into \"%2\"")
                    .arg(item->name,
                         thePrefs.category(item->category).displayName()));
    }
    item->articleDigest = nzbArticleDigest(item->nzb);
    item->releaseKey = nzbReleaseKey(item->name, item->nzb.totalEncodedBytes());
    item->initFileStates(thePrefs.usenetTempDir());

    // Lay each target file out once, up front. Growing a file by seeking past its
    // end article by article is what fragments it badly, and the size is only
    // knowable from the NZB's encoded total until the first =ybegin arrives — so
    // this is a first approximation that sealFile() trims.
    //
    // Recovery volumes are skipped: they are not in the plan and mostly never
    // will be. Creating them anyway leaves a work directory full of zero-byte
    // .par2 files, which par2 then logs its way past on every verify and — worse
    // — which a release with no archive to unpack would publish as payload.
    // requestPar2Volumes() creates the ones it actually asks for.
    for (int i = 0; i < item->files.size(); ++i) {
        if (item->nzb.files.at(i).isPar2Volume())
            continue;
        if (!createTargetFile(item->files[i].tempPath, error))
            return {};
    }

    const QString id = item->id;
    auto rt = std::make_unique<ItemRuntime>();
    rt->item = std::move(item);
    rebuildPlan(*rt);
    persist(*rt);
    m_items.push_back(std::move(rt));

    const NzbShortfall shortfall = m_items.back()->item->nzb.shortfall();
    logInfo(QStringLiteral("Usenet: queued \"%1\" (%2 file(s), %3 article(s))")
                .arg(m_items.back()->item->name)
                .arg(m_items.back()->item->nzb.files.size())
                .arg(m_items.back()->item->segmentCount()));
    if (shortfall.missingSegments > 0) {
        // The NZB is short of what its own subject counters claim. Said once,
        // as information: those articles may still be on every server, and this
        // has changed nothing about what will be fetched.
        logWarning(QStringLiteral("Usenet: \"%1\" lists %2% of the articles its "
                                  "subjects claim (%3 not listed)%4")
                       .arg(m_items.back()->item->name)
                       .arg(shortfall.percent())
                       .arg(shortfall.missingSegments)
                       .arg(shortfall.likelyRecoverable()
                                ? QStringLiteral("; the recovery volumes should cover it")
                                : QString()));
    }

    report(UsenetAddOutcome::Added);
    emit itemAdded(id);
    emit itemChanged(id);

    // After the item is in m_items, so dispatchProbes() can find it, and after
    // itemAdded so the GUI sees the row before it goes to Checking. Returns
    // false — leaving the item plain Queued — whenever a probe cannot or should
    // not run, which must cost nothing.
    beginHealthCheck(*m_items.back());

    dispatch();
    return id;
}

bool UsenetQueue::recheckItem(const QString& id)
{
    ItemRuntime* rt = runtimeFor(id);
    if (!rt)
        return false;

    // Not while it is downloading, post-processing or already checking: the
    // probe shares the workers' connection budget, and re-asking about an item
    // whose articles are actively arriving answers a question that is being
    // answered better by the download itself.
    // Queued and Paused only. Downloading and post-processing are answering the
    // question better by doing it, and a *completed* item has nothing to decide:
    // putting it through Checking would end with it back in the download states,
    // which is a re-download nobody asked for.
    const UsenetItemStatus status = rt->item->status;
    if (status != UsenetItemStatus::Queued && status != UsenetItemStatus::Paused)
        return false;

    if (!beginHealthCheck(*rt))
        return false;

    dispatch();
    return true;
}

bool UsenetQueue::removeItem(const QString& id, bool deleteFiles)
{
    for (int i = 0; i < int(m_items.size()); ++i) {
        if (m_items.at(size_t(i))->item->id != id)
            continue;

        // Anything still in flight will come back referring to an item that is
        // gone; onSegmentFinished tolerates that by looking the id up again.
        //
        // A post-processing job is different: it is *reading and writing* the
        // directory about to be deleted, and it can hold the thread for minutes
        // inside one repair. Ask it to stop at its next checkpoint. The
        // processor is serial, so the running job is necessarily this item's.
        if (m_items.at(size_t(i))->postRunning && m_postProcessor)
            m_postProcessor->requestStop();

        // Same reasoning for an extraction still following this item's volumes,
        // except it is ours to join: it blocks in a read of a directory that is
        // about to be deleted.
        cancelDirectUnpack(*m_items.at(size_t(i)));
        cancelEncryptedPreview(*m_items.at(size_t(i)));

        if (deleteFiles) {
            const QString itemDir =
                QDir(thePrefs.usenetTempDir()).filePath(id);
            QDir(itemDir).removeRecursively();
        }
        // Recorded before the erase, while the item is still readable. A release
        // that finished and is only now being cleared is still Downloaded —
        // record() keeps that, so clearing a row cannot read back as giving up.
        const UsenetQueueItem& item = *m_items.at(size_t(i))->item;
        m_history.record(item, item.status == UsenetItemStatus::Complete
                                   ? UsenetHistoryState::Downloaded
                                   : UsenetHistoryState::Cancelled);
        m_history.save();

        UsenetQueueStore::remove(id);
        m_items.erase(m_items.begin() + i);

        emit itemRemoved(id);
        dispatch();
        return true;
    }
    return false;
}

bool UsenetQueue::pauseItem(const QString& id)
{
    ItemRuntime* rt = runtimeFor(id);
    if (!rt || !rt->item->isActive())
        return false;

    rt->item->status = UsenetItemStatus::Paused;
    // No more volumes are coming, so a run would block until resume. Drop it;
    // the set restarts from volume one when the download does, at disk speed.
    cancelDirectUnpack(*rt);
    cancelEncryptedPreview(*rt);
    persist(*rt);
    emit itemChanged(id);
    return true;
}

bool UsenetQueue::resumeItem(const QString& id)
{
    ItemRuntime* rt = runtimeFor(id);
    if (!rt)
        return false;
    if (rt->item->status != UsenetItemStatus::Paused
        && rt->item->status != UsenetItemStatus::Failed)
        return false;

    // A failed item whose accounts have changed since it failed re-asks the
    // articles nobody had. That is what "retries from the top of the ladder"
    // below can actually mean for the usual failure, where every segment is
    // already resolved and the plan is empty.
    //
    // Gated on the ladder having changed rather than done unconditionally,
    // because Resume has three callers: this one, the category-wide resume —
    // where "All" is every item in the queue — and setItemPassword(), which
    // resumes so the new passphrase is tried. Re-asking a few thousand dead
    // articles across three rungs, to be told the same thing, is not what any of
    // the three asked for. When the account list is the same, the explicit
    // retry is the way to insist.
    if (rt->item->status == UsenetItemStatus::Failed
        && !rt->item->failedLadder.isEmpty()
        && rt->item->failedLadder != serverLadderDigest()) {
        logInfo(QStringLiteral("Usenet: \"%1\" is being resumed with a different "
                               "set of accounts — asking again for what was missing")
                    .arg(rt->item->name));
        if (retryMissingArticles(id))
            return true;
    }

    rt->item->status = UsenetItemStatus::Queued;
    rt->item->error.clear();

    // A failed item retries from the top of the ladder: whatever was wrong may
    // have been fixed, and keeping the old exclusions would skip the very server
    // the user just repaired.
    rt->attempts.clear();
    rebuildPlan(*rt);
    persist(*rt);

    // pauseItem() cancelled every run, and pumpDirectUnpack() only fires from a
    // volume that seals *after* this point — so without this a paused set is
    // never extracted while downloading again, and a preview of it dies with it.
    restartDirectUnpack(*rt);

    emit itemChanged(id);
    dispatch();
    return true;
}

bool UsenetQueue::retryMissingArticles(const QString& id)
{
    ItemRuntime* rt = runtimeFor(id);
    if (!rt)
        return false;

    // Failed only, and never over a running job. A Complete item has nothing
    // short in it, and a post-processing one is being read from another thread.
    if (rt->item->status != UsenetItemStatus::Failed || rt->postRunning)
        return false;

    // Every file has to be able to say *which* articles it is short of. A
    // sidecar written before the missing map existed carries a count and no
    // map, and guessing from the count would re-ask the whole file.
    for (const UsenetFileState& st : rt->item->files) {
        if (st.missingSegments != int(st.missing.count(true))) {
            logWarning(QStringLiteral("Usenet: cannot retry \"%1\" — it was queued "
                                      "before this version recorded which articles "
                                      "were missing")
                           .arg(rt->item->name));
            return false;
        }
    }

    rt->refetch = {};
    QStringList refused;
    for (int f = 0; f < rt->item->files.size(); ++f) {
        if (rt->item->files.at(f).missingSegments <= 0)
            continue;
        if (!rearmMissingSegments(*rt, f))
            refused.append(rt->item->bestFileName(f));
    }

    if (rt->refetch.armed == 0) {
        if (!refused.isEmpty()) {
            logWarning(QStringLiteral("Usenet: nothing to retry for \"%1\" — %2 no "
                                      "longer on disk")
                           .arg(rt->item->name, refused.join(QStringLiteral(", "))));
        }
        return false;
    }

    // The recovery cycle gets its rounds back: a retry that fills holes changes
    // what a verify will conclude, and the old count belongs to the old attempt.
    // requestedPar2 is kept — those volumes were already fetched or planned, and
    // asking for them twice would buy the same blocks twice.
    rt->par2Rounds = 0;

    // Not restartDirectUnpack(), which resumeItem() does: a replay offers every
    // sealed volume and then blocks forever on the one with the hole, and at
    // end-of-set libarchive returns a *prefix* extraction that post-processing
    // would treat as a finished unpack. Direct unpack is a latency optimisation
    // for a live download; a retry is not latency-sensitive.
    cancelDirectUnpack(*rt);

    rt->item->status = UsenetItemStatus::Queued;
    rt->item->error.clear();
    rt->attempts.clear();       // back to rung 0 over the accounts configured now
    rebuildPlan(*rt);
    persist(*rt);

    logInfo(QStringLiteral("Usenet: asking again for %1 missing article(s) of \"%2\"")
                .arg(rt->refetch.armed)
                .arg(rt->item->name));

    emit itemChanged(id);
    dispatch();
    return true;
}

bool UsenetQueue::setItemPriority(const QString& id, int priority)
{
    ItemRuntime* rt = runtimeFor(id);
    if (!rt)
        return false;

    // Clamped here rather than at the handler, for the reason addNzb() clamps
    // there too: a value outside the five levels makes a bucket nothing can name
    // and no menu entry can ever select again.
    rt->item->priority = clampUsenetPriority(priority);
    persist(*rt);
    emit itemChanged(id);
    dispatch();
    return true;
}

bool UsenetQueue::setItemCategory(const QString& id, int category)
{
    ItemRuntime* rt = runtimeFor(id);
    if (!rt)
        return false;

    const int wanted = category > 0 ? category : 0;
    if (rt->item->category == wanted)
        return true;

    // No dispatch(): a category decides where a release *lands*, not when it
    // runs. Only the ED2K queue sorts on it, and only because a category carries
    // the a4af rank that Usenet has no equivalent of.
    rt->item->category = wanted;
    persist(*rt);
    emit itemChanged(id);
    return true;
}

void UsenetQueue::remapCategories(const QHash<uint32, uint32>& oldToNew)
{
    for (auto& rt : m_items) {
        const int mapped = remapCategoryIndex(rt->item->category, oldToNew);
        if (mapped == rt->item->category)
            continue;

        rt->item->category = mapped;
        persist(*rt);
        emit itemChanged(rt->item->id);
    }
}

bool UsenetQueue::setItemPassword(const QString& id, const QString& password)
{
    ItemRuntime* rt = runtimeFor(id);
    if (!rt)
        return false;

    if (rt->item->nzb.password == password)
        return true;

    rt->item->nzb.password = password;

    // The streaming index caches what it parsed out of the volumes, and for an
    // encrypted set that answer was "cannot read this". A new password makes it
    // a different question.
    rt->streamIndex.invalidate();

    // A prefix decrypted with the old password is garbage, and the high-water
    // mark that protects it from shrinking would otherwise protect the garbage.
    cancelEncryptedPreview(*rt);
    if (!rt->encryptedPreview.path.isEmpty())
        QFile::remove(rt->encryptedPreview.path);
    rt->encryptedPreview = EncryptedPreviewRun{};

    if (rt->item->status == UsenetItemStatus::Failed) {
        // Everything is already on disk; resumeItem() clears the error, finds
        // nothing left to fetch and runs post-processing again on the next tick.
        persist(*rt);
        return resumeItem(id);
    }

    persist(*rt);
    emit itemChanged(id);
    return true;
}

QList<const UsenetQueueItem*> UsenetQueue::items() const
{
    QList<const UsenetQueueItem*> out;
    out.reserve(int(m_items.size()));
    for (const auto& rt : m_items)
        out.append(rt->item.get());
    return out;
}

const UsenetQueueItem* UsenetQueue::findItem(const QString& id) const
{
    for (const auto& rt : m_items) {
        if (rt->item->id == id)
            return rt->item.get();
    }
    return nullptr;
}

UsenetQueue::PreviewInfo UsenetQueue::previewability(const QString& itemId, int fileIndex)
{
    PreviewInfo out;

    ItemRuntime* rt = runtimeFor(itemId);
    if (!rt)
        return out;

    const UsenetQueueItem& item = *rt->item;
    if (fileIndex < 0 || fileIndex >= item.files.size() || fileIndex >= item.nzb.files.size())
        return out;

    const UsenetFileState& st = item.files.at(fileIndex);

    // A media file posted raw needs no container work at all — that is phase 6a,
    // and answering it here keeps the index out of the hot path entirely.
    if (item.isFilePreviewable(fileIndex)) {
        out.previewable = st.availableEnd() > 0;
        return out;
    }

    const QString name = item.bestFileName(fileIndex);
    if (name.isEmpty() || !UsenetUnpacker::isArchiveVolume(name))
        return out;

    // From here it is an archive volume, so the index decides. resolve() reads
    // headers but never fetches, and its refusals are cached — this is called
    // once per file on every queue push.
    // -1 asks for the first *playable* file inside the set, so a release that
    // packs an .nfo ahead of the feature is previewable rather than reported as
    // holding nothing playable. The index answers that; whether any member
    // qualifies is its judgement, not a second test here.
    const StreamResolve resolved = rt->streamIndex.resolve(item, fileIndex, -1, 0);
    if (resolved.plan == StreamPlan::NotSeekable) {
        // Unmappable, but perhaps being unpacked anyway. Read-only on purpose:
        // this runs once per file on every queue push and must not move the
        // high-water mark that requestStream() maintains.
        const UsenetDirectUnpackEntry* entry = extractionEntryFor(*rt, fileIndex, -1);
        if (entry && entry->entrySize > 0 && entry->bytesReadable > 0) {
            out.previewable = true;
            return out;
        }
        if (entry) {
            out.note = tr("Extracting — the preview starts once there is something to play");
            return out;
        }
        out.note = resolved.reason;
        return out;
    }
    if (resolved.plan != StreamPlan::Ready)
        return out;   // still reading headers; ask again next push

    out.previewable = availableFrom(item, resolved.extents, 0) > 0;
    return out;
}

UsenetQueue::StreamInfo UsenetQueue::requestStream(const QString& itemId, int fileIndex,
                                                   qint64 wantOffset, qint64 wantLength,
                                                   int entryOrdinal)
{
    StreamInfo info;

    ItemRuntime* rt = runtimeFor(itemId);
    if (!rt)
        return info;

    const UsenetQueueItem& item = *rt->item;
    if (fileIndex < 0 || fileIndex >= item.files.size())
        return info;

    info.found = true;

    // Register the interest first, and unconditionally within an active item:
    // every branch below either serves bytes or asks for some, and both want the
    // item ahead of an unattended download for the next kStreamingBoostMs.
    const bool active = item.isActive();
    if (active)
        rt->streamingUntilMs = QDateTime::currentMSecsSinceEpoch() + kStreamingBoostMs;

    const StreamResolve resolved =
        rt->streamIndex.resolve(item, fileIndex, entryOrdinal, wantOffset);

    switch (resolved.plan) {
    case StreamPlan::NotSeekable:
        // The map cannot describe this set — compressed, solid, header-encrypted.
        // libarchive can still unpack it, and when direct unpack is following it
        // down its own output is a perfectly good byte source. The map goes
        // first because only it can seek ahead of the write head.
        if (streamFromExtraction(*rt, fileIndex, entryOrdinal, info)) {
            if (active) {
                promoteExtractionVolume(*rt, fileIndex);
                dispatch();
            }
            return info;
        }
        // Last resort, and the only one that works for a password-protected
        // set: re-run the external unpacker over the volumes that have landed.
        if (streamFromEncryptedPreview(*rt, fileIndex, info)) {
            if (active) {
                promoteEncryptedPreviewVolumes(*rt, fileIndex);
                dispatch();
            }
            return info;
        }
        // Nothing to fetch and nothing to wait for. Saying so beats holding the
        // request open for the full poll window and then refusing anyway.
        info.notSeekableReason = resolved.reason;
        return info;

    case StreamPlan::NeedBytes:
        // The index is missing a header or a declared size. Ask for exactly that
        // range; the caller's poll brings us back here a tick later.
        if (active) {
            promoteRange(*rt, resolved.needFileIndex, resolved.needOffset, resolved.needLength);
            dispatch();
        }
        return info;

    case StreamPlan::Unknown:
        return info;

    case StreamPlan::Ready:
        break;
    }

    info.fileName = resolved.fileName;
    info.totalSize = resolved.totalSize;
    info.availableEnd = availableFrom(item, resolved.extents, wantOffset);

    bool everyPieceFinal = !resolved.extents.isEmpty();
    for (const StreamExtent& e : resolved.extents) {
        if (e.fileIndex < 0 || e.fileIndex >= item.files.size())
            continue;
        const UsenetFileState& st = item.files.at(e.fileIndex);

        // finalPath first: once the item is published the scratch file is gone,
        // and a completed release is the case where preview is least interesting
        // but most likely to be asked for by a stale URL.
        const QString path = !st.finalPath.isEmpty() ? st.finalPath : st.tempPath;
        if (path.isEmpty())
            continue;
        info.pieces.append({path, e.virtualOffset, e.fileOffset, e.length});
        everyPieceFinal = everyPieceFinal && st.finalized;
    }
    info.complete = everyPieceFinal;

    if (active) {
        // Ask for what was requested, mapped back through the extents onto the
        // volumes that hold it. A seek to 80% therefore promotes the articles of
        // one volume and leaves everything before it alone.
        const qint64 span = wantLength > 0 ? wantLength : 1;
        qint64 cursor = wantOffset;
        const qint64 limit = wantOffset + span;
        for (const StreamExtent& e : resolved.extents) {
            if (e.virtualOffset + e.length <= cursor)
                continue;
            if (e.virtualOffset >= limit)
                break;
            const qint64 from = qMax(cursor, e.virtualOffset);
            const qint64 to = qMin(limit, e.virtualOffset + e.length);
            if (to <= from)
                continue;
            promoteRange(*rt, e.fileIndex, e.fileOffset + (from - e.virtualOffset), to - from);
            cursor = to;
        }
        dispatch();
    }

    return info;
}

UsenetQueue::ArchiveListing UsenetQueue::listArchiveEntries(const QString& itemId, int fileIndex)
{
    ArchiveListing listing;

    ItemRuntime* rt = runtimeFor(itemId);
    if (!rt)
        return listing;

    const UsenetQueueItem& item = *rt->item;
    if (fileIndex < 0 || fileIndex >= item.files.size())
        return listing;

    const StreamListing found = rt->streamIndex.list(item, fileIndex);

    for (const StreamMember& m : found.members) {
        ArchiveEntryInfo row;
        row.entry = m.index;
        row.name = m.name;
        row.size = m.size;
        row.playable = m.playable && m.mappable;
        row.note = row.playable ? QString() : m.note;

        // A member the map cannot place is still playable if the extraction has
        // reached it — the compression note stops being the whole truth the
        // moment libarchive starts writing the file out.
        if (!row.playable && m.playable)
            annotateFromExtraction(*rt, fileIndex, m.index, row);

        listing.entries.append(row);
    }

    // A solid or header-encrypted set is refused a volume at a time, so the
    // index never enumerates a member and there is nothing above to annotate.
    // The extraction is then the only thing that knows what is inside.
    if (found.members.isEmpty())
        appendExtractionRows(*rt, fileIndex, listing);

    switch (found.plan) {
    case StreamPlan::NotSeekable:
        if (!listing.entries.isEmpty()) {
            // Listed after all, by the extraction. Saying NotSeekable here would
            // have the chooser throw the rows away.
            listing.status = ArchiveListing::Status::Scanning;
            return listing;
        }
        listing.status = ArchiveListing::Status::NotSeekable;
        listing.note = found.reason;
        return listing;

    case StreamPlan::NeedBytes:
        // The scan is short of a header. Ask for exactly that range within this
        // item's own plan — no cross-item boost, see the header comment.
        if (item.isActive()) {
            promoteRange(*rt, found.needFileIndex, found.needOffset, found.needLength);
            dispatch();
            listing.status = ArchiveListing::Status::Scanning;
        }
        return listing;

    case StreamPlan::Unknown:
        return listing;

    case StreamPlan::Ready:
        break;
    }

    listing.status = found.isArchive ? ArchiveListing::Status::Complete
                                     : ArchiveListing::Status::NotAnArchive;
    if (found.isArchive && !found.complete)
        listing.status = ArchiveListing::Status::Scanning;
    return listing;
}

QList<int> UsenetQueue::segmentsCovering(const UsenetQueueItem& item, int fileIndex,
                                         qint64 offset, qint64 length)
{
    if (fileIndex < 0 || fileIndex >= item.files.size() || fileIndex >= item.nzb.files.size())
        return {};

    const UsenetFileState& st = item.files.at(fileIndex);
    const NzbFileInfo& info = item.nzb.files.at(fileIndex);
    if (info.segments.isEmpty() || length <= 0)
        return {};

    // A volume nobody has touched has no part length of its own — and a seek
    // into one is exactly the case phase 6b exists for. Borrow a sibling's: the
    // volumes of a release come out of one posting run with one part size, so
    // this is not an approximation in practice, and the ±1 probe below absorbs
    // it if some release ever proves otherwise.
    qint64 partLength = st.partLength;
    if (partLength <= 0) {
        for (const UsenetFileState& other : item.files)
            partLength = qMax(partLength, other.partLength);
    }
    if (partLength <= 0)
        return {};

    const qint64 from = qMax<qint64>(0, offset);
    const qint64 to = from + length - 1;

    // §7.1: parts are uniform with a short remainder, so the part number is
    // arithmetic. One either side covers a poster who padded differently — the
    // doc's "probe ±1", paid up front because an extra article costs far less
    // than another round trip through the poll loop.
    // Part numbers are 1-based, so the part holding byte B is B/partLength + 1.
    const int firstPart = int(from / partLength) + 1 - 1;   // one early
    const int lastPart = int(to / partLength) + 1 + 1;      // one late

    QList<int> out;
    for (int i = 0; i < info.segments.size(); ++i) {
        const int number = info.segments.at(i).number;
        if (number >= firstPart && number <= lastPart)
            out.append(i);
    }
    return out;
}

void UsenetQueue::promoteRange(ItemRuntime& rt, int fileIndex, qint64 offset, qint64 length)
{
    const QList<int> wanted = segmentsCovering(*rt.item, fileIndex, offset, length);
    if (wanted.isEmpty())
        return;

    const UsenetFileState& st = rt.item->files.at(fileIndex);

    QList<quint64> keys;
    for (const int segmentIndex : wanted) {
        if (segmentIndex < st.done.size() && st.done.testBit(segmentIndex))
            continue;
        const quint64 key = SegmentKey{fileIndex, segmentIndex}.packed();
        if (rt.inFlight.contains(key))
            continue;
        keys.append(key);
    }
    if (keys.isEmpty())
        return;

    for (const quint64 key : std::as_const(keys)) {
        const qsizetype at = rt.plan.indexOf(key);
        if (at < 0)
            continue;
        rt.plan.removeAt(at);
        if (at < rt.planCursor)
            --rt.planCursor;
    }

    // Behind the cursor, so the next dispatch() round hands these out first and
    // then carries on reading ahead from the same point.
    rt.planCursor = qBound(0, rt.planCursor, int(rt.plan.size()));
    const int insertAt = rt.planCursor;
    for (qsizetype k = keys.size() - 1; k >= 0; --k) {
        if (!rt.plan.contains(keys.at(k)))
            rt.plan.insert(insertAt, keys.at(k));
    }
}

void UsenetQueue::setRateLimit(qint64 bytesPerSecond)
{
    m_rateLimit = std::max<qint64>(0, bytesPerSecond);

    const int workerCount = m_workers.size();
    if (workerCount <= 0)
        return;

    const qint64 share = m_rateLimit <= 0
                             ? 0
                             : std::max<qint64>(1, m_rateLimit / workerCount);
    for (UsenetWorker* w : m_workers) {
        QMetaObject::invokeMethod(w, "setRateLimit", Qt::QueuedConnection,
                                  Q_ARG(qint64, share));
    }
}

bool UsenetQueue::setAccountUsage(const QString& accountId, qint64 periodBytes,
                                  qint64 totalBytes)
{
    if (!m_usage.setUsage(accountId, periodBytes, totalBytes))
        return false;

    refreshQuotaState();
    unparkQuotaStalls();
    dispatch();
    return true;
}

bool UsenetQueue::isOverQuota(const NewsServer& s) const
{
    return m_overQuota.contains(s.key());
}

bool UsenetQueue::hasActiveDownloads() const
{
    for (const auto& rt : m_items) {
        // A parked item is waiting on a billing day, not on the network.
        // Counting it would reserve a share of the line for an engine that is
        // downloading nothing, for as long as the allowance lasts.
        if (rt->item->isActive() && !rt->quotaParked)
            return true;
    }
    return false;
}

int UsenetQueue::activeFetches() const
{
    int total = 0;
    for (const int inFlight : m_workerInFlight)
        total += inFlight;
    return total;
}

// ---------------------------------------------------------------------------
// Private — workers
// ---------------------------------------------------------------------------

void UsenetQueue::startWorkers()
{
    // Distinct buckets, not rows: two hostnames of one grouped account share a
    // budget, so counting both would size the thread pool for connections that
    // can never be opened.
    int totalConnections = 0;
    {
        const auto buckets = nntpConnectionBuckets(m_servers);
        QHash<QString, int> distinct;
        for (auto it = buckets.cbegin(); it != buckets.cend(); ++it)
            distinct.insert(it->id, it->limit);
        for (const int limit : std::as_const(distinct))
            totalConnections += std::max(0, limit);
    }
    if (totalConnections <= 0)
        return;

    // One worker per connection at most: four threads sharing two connections is
    // two idle threads and a needlessly divided budget.
    const int ideal = std::max(1, QThread::idealThreadCount());
    const int workerCount = std::clamp(std::min(ideal, totalConnections), 1, kMaxWorkers);

    for (int i = 0; i < workerCount; ++i) {
        auto* thread = new QThread;
        thread->setObjectName(QStringLiteral("UsenetWorker%1").arg(i));

        auto* worker = new UsenetWorker(i);
        worker->moveToThread(thread);

        // The worker outlives quit() only long enough to be deleted inside its own
        // thread. A socket destroyed from another thread is a crash, not a leak.
        connect(thread, &QThread::finished, worker, &QObject::deleteLater);

        // Both carry this worker set's generation. Results and capacities are
        // addressed by slot index, the indices restart at 0 on every rebuild,
        // and everything a torn-down worker posted is still in the queue.
        const quint32 generation = m_workerGeneration;
        connect(worker, &UsenetWorker::segmentFinished, this,
                [this, generation](const UsenetFetchResult& result) {
                    onSegmentFinished(result, generation == m_workerGeneration);
                }, Qt::QueuedConnection);
        connect(worker, &UsenetWorker::capacityChanged, this,
                [this, generation](int workerIndex, int capacity) {
                    if (generation == m_workerGeneration)
                        onCapacityChanged(workerIndex, capacity);
                }, Qt::QueuedConnection);

        m_threads.append(thread);
        m_workers.append(worker);
        m_workerCapacity.append(0);
        m_workerInFlight.append(0);

        thread->start();
    }

    // Divide the connection budget rather than replicating it. N workers each
    // honouring maxConnections would open N times what the user configured, and
    // exceeding a provider's limit gets an account throttled or suspended.
    m_workerLevels.clear();
    m_workerServers.clear();
    for (int i = 0; i < workerCount; ++i) {
        // Every slice keeps every row, differing only in maxConnections. That is
        // load-bearing: nntpLevelLadder() ignores maxConnections, so a slice
        // builds an identical ladder to this queue's and a rung means the same
        // thing on both sides. Dropping a row whose share divided to zero used to
        // renumber that worker's rungs.
        QList<NewsServer> slice;
        slice.reserve(m_servers.size());
        for (const NewsServer& s : m_servers) {
            NewsServer copy = s;
            const int base = std::max(0, s.maxConnections) / workerCount;
            const int remainder = std::max(0, s.maxConnections) % workerCount;
            copy.maxConnections = base + (i < remainder ? 1 : 0);
            slice.append(copy);
        }

        // Which rungs this worker can actually lease on. A grouped account's
        // share can divide to zero here while another worker still holds it.
        QSet<int> levels;
        // The accounts behind those rungs, not just the rungs. A rung is only
        // worth handing this worker an article for if the worker holds budget on
        // an account there that the article is actually allowed to ask — and a
        // spent allowance changes that answer without changing the slices.
        QSet<QString> keys;
        const auto buckets = nntpConnectionBuckets(slice);
        for (const NewsServer& s : std::as_const(slice)) {
            if (!s.enabled || !s.isValid())
                continue;
            if (buckets.value(s.key()).limit <= 0)
                continue;
            const auto rung = m_ladder.indexOf(s.level);
            if (rung >= 0) {
                levels.insert(int(rung));
                keys.insert(s.key());
            }
        }
        m_workerLevels.append(levels);
        m_workerServers.append(keys);

        QMetaObject::invokeMethod(m_workers.at(i), "setServers", Qt::QueuedConnection,
                                  Q_ARG(QList<eMule::NewsServer>, slice),
                                  Q_ARG(int, m_retryIntervalSec));
    }

    setRateLimit(m_rateLimit);

    logInfo(QStringLiteral("Usenet: %1 worker thread(s) over %2 connection(s)")
                .arg(workerCount)
                .arg(totalConnections));
}

void UsenetQueue::stopWorkers()
{
    // First: everything these workers have already posted, and everything the
    // blocking shutdown below is about to post, is now stale.
    ++m_workerGeneration;

    for (UsenetWorker* w : m_workers) {
        // Blocking, so every socket is torn down inside its own thread before the
        // event loop that owns it stops.
        QMetaObject::invokeMethod(w, "shutdown", Qt::BlockingQueuedConnection);
    }
    for (QThread* t : m_threads) {
        t->quit();
        t->wait();
        delete t;
    }

    m_threads.clear();
    m_workers.clear();
    m_workerCapacity.clear();
    m_workerInFlight.clear();
    m_workerLevels.clear();
    m_workerServers.clear();

    for (auto& rt : m_items) {
        rt->inFlight.clear();
        rt->planCursor = 0;
    }
}

void UsenetQueue::startPostProcessor()
{
    if (m_postThread)
        return;

    qRegisterMetaType<UsenetPostJob>("eMule::usenet::UsenetPostJob");
    qRegisterMetaType<UsenetPostResult>("eMule::usenet::UsenetPostResult");

    m_postThread = new QThread;
    m_postThread->setObjectName(QStringLiteral("UsenetPostProcessor"));

    m_postProcessor = new UsenetPostProcessor;
    m_postProcessor->moveToThread(m_postThread);

    connect(m_postThread, &QThread::finished, m_postProcessor, &QObject::deleteLater);
    connect(m_postProcessor, &UsenetPostProcessor::stageChanged,
            this, &UsenetQueue::onPostStage, Qt::QueuedConnection);
    connect(m_postProcessor, &UsenetPostProcessor::finished,
            this, &UsenetQueue::onPostFinished, Qt::QueuedConnection);

    m_postThread->start();
}

void UsenetQueue::stopPostProcessor()
{
    if (!m_postThread)
        return;

    // The stop flag first, and not through the event loop: a repair can hold the
    // thread for minutes inside one Process() call, so a queued message would
    // not be looked at until it finished. requestStop() is atomic for exactly
    // this — it is the one piece of state the module shares across threads.
    if (m_postProcessor)
        m_postProcessor->requestStop();

    m_postThread->quit();
    m_postThread->wait();
    delete m_postThread;

    m_postThread = nullptr;
    m_postProcessor = nullptr;   // deleted by the finished -> deleteLater above
}

void UsenetQueue::setPostProcessingOptions(const PostProcessingOptions& options)
{
    m_par2Enabled = options.par2;
    m_renameEnabled = options.rename;
    m_unpackEnabled = options.unpack;
    m_cleanupEnabled = options.cleanup;
    m_directUnpackEnabled = options.directUnpack;
}

void UsenetQueue::onCapacityChanged(int workerIndex, int capacity)
{
    if (workerIndex >= 0 && workerIndex < m_workerCapacity.size()) {
        m_workerCapacity[workerIndex] = capacity;
        dispatch();
    }
}

// ---------------------------------------------------------------------------
// Private — scheduling
// ---------------------------------------------------------------------------

void UsenetQueue::rebuildPlan(ItemRuntime& rt)
{
    rt.plan.clear();
    rt.planCursor = 0;

    const UsenetQueueItem& item = *rt.item;

    // Unfetched segments of a file, in part-number order rather than document
    // order. An NZB is free to list its <segment> elements in any order and
    // plenty do; NzbInfo only sorts inside hasAllSegments(). Fetching in part
    // order is what makes the written prefix grow from byte 0, which is the
    // whole of what streaming needs.
    //
    // The *indices* are sorted, never the segment list itself: `done` is a bit
    // per index into that list, so reordering it would silently reattribute
    // every bit in the release.
    const auto pendingSegments = [&](int f) {
        QList<int> order;
        if (f < 0 || f >= item.files.size() || f >= item.nzb.files.size())
            return order;

        const UsenetFileState& st = item.files.at(f);
        const auto& segments = item.nzb.files.at(f).segments;
        order.reserve(segments.size());
        for (int s = 0; s < segments.size(); ++s) {
            if (s < st.done.size() && st.done.testBit(s))
                continue;
            order.append(s);
        }
        std::stable_sort(order.begin(), order.end(), [&segments](int a, int b) {
            return segments.at(a).number < segments.at(b).number;
        });
        return order;
    };

    // The index .par2, as opposed to a recovery volume. Testing isPar2() alone
    // here would hoist *requested* recovery volumes to the front of a round-two
    // plan, ahead of the payload the repair is for.
    const auto isIndexPar2 = [&](int f) {
        const NzbFileInfo& info = item.nzb.files.at(f);
        return info.isPar2() && !info.isPar2Volume();
    };

    // Normally the index .par2 goes last: it is small and only interesting if
    // something came up short. On a release that cannot name itself that is
    // exactly backwards — it is then the one file that says what the others are.
    // It is already in the plan either way, so this costs no extra articles.
    const bool hoistPar2 = rt.par2NamesState != ItemRuntime::Par2NamesState::Loaded
                           && m_par2Enabled && m_renameEnabled
                           && Par2Verifier::available()
                           && looksObfuscated(item);

    // File -> the segments pass 2 already took, so pass 3 does not list them
    // twice. Harmless in dispatch(), which checks inFlight and done, but a plan
    // nobody can read back is its own bug.
    QHash<int, QSet<int>> prefetched;

    if (hoistPar2) {
        for (int f = 0; f < item.nzb.files.size(); ++f) {
            if (f >= item.files.size() || !isPlanned(rt, f) || !isIndexPar2(f))
                continue;
            for (const int s : pendingSegments(f))
                rt.plan.append(SegmentKey{f, s}.packed());
        }

        // The opening of every still-unnamed payload file: the prefetch pass.
        //
        // Enough articles to reach PAR2's 16 KiB identity window, not one --
        // "one article" is only the same thing while articles are ~700 KB, and a
        // post with small ones would leave every name unsettled forever while
        // the prefetch asked again each tick. Sized from the *encoded* bytes the
        // NZB declares, which over-estimates the decoded payload and is
        // therefore safe in the direction that matters, and capped so a
        // pathological NZB cannot turn the prefetch into the whole download.
        for (int f = 0; f < item.nzb.files.size(); ++f) {
            if (f >= item.files.size() || !isPlanned(rt, f))
                continue;
            if (item.nzb.files.at(f).isPar2() || !item.files.at(f).par2FileName.isEmpty())
                continue;

            const auto& segments = item.nzb.files.at(f).segments;
            qint64 have = item.files.at(f).availableFrom(0);
            int taken = 0;

            for (const int s : pendingSegments(f)) {
                if (have >= kPar2Hash16kBytes || taken >= kMaxPrefetchArticles)
                    break;
                prefetched[f].insert(s);
                rt.plan.append(SegmentKey{f, s}.packed());
                have += s < segments.size() ? segments.at(s).bytes : 0;
                ++taken;
            }
        }
    }

    // Payload, then PAR2. Recovery volumes are not scheduled at all: they are
    // typically a tenth of a release and pure waste on a healthy one, so
    // isPlanned() keeps them out until a verify says how many are needed and
    // requestPar2Volumes() puts exactly those into requestedPar2.
    for (const bool par2Pass : {false, true}) {
        for (int f = 0; f < item.nzb.files.size(); ++f) {
            if (item.nzb.files.at(f).isPar2() != par2Pass)
                continue;
            if (f >= item.files.size() || !isPlanned(rt, f))
                continue;
            if (hoistPar2 && par2Pass && isIndexPar2(f))
                continue;   // already at the front

            const QSet<int> taken = prefetched.value(f);
            for (const int s : pendingSegments(f)) {
                if (taken.contains(s))
                    continue;
                rt.plan.append(SegmentKey{f, s}.packed());
            }
        }
    }
}

void UsenetQueue::dispatch()
{
    if (!m_running || m_workers.isEmpty())
        return;

    // No usable account configured. nextServableLevel() answers -1 for every
    // segment in that state, and acting on it would convert the whole queue into
    // missing articles because the user disabled their servers.
    if (m_ladder.isEmpty())
        return;

    // A dispatch round that found nothing leasable stays parked until the next
    // tick clears this. Without it, every "no connection available" result would
    // trigger another identical round.
    if (m_starved)
        return;

    // No room to put what we would ask for. Like the allowance, this waits: it
    // never touches `tried`, never spends a retry and never reaches
    // markSegmentMissing(), so a full disk cannot invent a hole in a release
    // that is fine.
    //
    // Measured here as well as on the tick because an add dispatches
    // immediately: without this the first release of a session would be fetched
    // before the first tick had ever looked at the volume. Self-gated, so this
    // is a syscall at most every kDiskCheckIntervalMs however often it is asked.
    refreshDiskState();
    if (m_diskBlocked)
        return;

    // Once per round rather than once per segment, so every segment in a round
    // sees the same answer and a meter crossing its allowance mid-round cannot
    // make two articles disagree.
    refreshQuotaState();

    // Probes first, out of a reserved slice. They are cheap, short-lived and
    // must not wait for the queue to drain; a Checking item is not in the order
    // below at all, because isActive() is false for it.
    dispatchProbes();

    // Highest priority first, then insertion order. Sorting the view rather than
    // m_items keeps the queue's own order stable for the GUI.
    QList<ItemRuntime*> order;
    order.reserve(qsizetype(m_items.size()));
    for (auto& rt : m_items) {
        // postRunning as well as isActive(): the status flips synchronously in
        // beginPostProcessing(), but the flag is what makes it impossible for a
        // second job to be queued for the same item while the first is out.
        // quotaParked: waiting on a billing day, not on the network. It leaves
        // the order until a rollover, a usage edit or a config change can change
        // the answer — otherwise the cursor runs to the end with work still
        // pending and onTick() rebuilds the whole plan four times a second for
        // the rest of the month.
        if (rt->item->isActive() && !rt->postRunning && !rt->quotaParked)
            order.append(rt.get());
    }
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    std::stable_sort(order.begin(), order.end(), [nowMs](ItemRuntime* a, ItemRuntime* b) {
        // An item somebody is watching outranks the priority field. Preview is
        // a real-time deadline and the user is staring at the result; a normal
        // download is not and they are not.
        const bool as = a->streamingUntilMs > nowMs;
        const bool bs = b->streamingUntilMs > nowMs;
        if (as != bs)
            return as;
        return a->item->priority > b->item->priority;
    });

    // Segments no account can be asked for any more. Resolved after the scan,
    // never inside it: markSegmentMissing() reaches checkFileCompletion() ->
    // beginPostProcessing(), which would flip an item `order` still lists as
    // active. Skipping the completion call instead is worse — the file would
    // never be finalized and onTick()'s pending test would rebuild its plan four
    // times a second forever.
    QList<QPair<ItemRuntime*, quint64>> unservable;
    bool noMoreWork = false;

    for (int w = 0; w < m_workers.size() && !noMoreWork; ++w) {
        while (m_workerInFlight.at(w) < m_workerCapacity.at(w)) {
            bool dispatched = false;
            bool rungMismatch = false;

            for (ItemRuntime* rt : order) {
                while (rt->planCursor < rt->plan.size()) {
                    const quint64 key = rt->plan.at(rt->planCursor);
                    const int fileIndex = int(key >> 32);
                    const int segIndex = int(key & 0xFFFFFFFFu);

                    if (rt->inFlight.contains(key)) {
                        ++rt->planCursor;
                        continue;
                    }
                    if (fileIndex >= rt->item->files.size()) {
                        ++rt->planCursor;
                        continue;
                    }
                    const UsenetFileState& st = rt->item->files.at(fileIndex);
                    if (segIndex < st.done.size() && st.done.testBit(segIndex)) {
                        ++rt->planCursor;
                        continue;
                    }

                    const NzbFileInfo& info = rt->item->nzb.files.at(fileIndex);
                    const SegmentAttempt attempt = rt->attempts.value(key);

                    // The rung is derived here, not stored on the attempt: while
                    // an untried account remains on the current level this comes
                    // back with the same level, which is what "escalate only when
                    // every server below said 430" actually means.
                    bool relaxed = false;
                    const int level = nextServableLevel(attempt.tried, info.date, relaxed);
                    if (level < 0) {
                        unservable.append({rt, key});
                        ++rt->planCursor;
                        continue;
                    }

                    // An allowance is a scheduling filter laid over that verdict,
                    // never part of it. -1 here means "wait", which is why the
                    // cursor is left where it is: this segment is the next thing
                    // to try once the money is there again.
                    const int payLevel =
                        nextAffordableLevel(attempt.tried, info.date, level, relaxed);
                    if (payLevel < 0) {
                        noteQuotaStall(*rt, attempt.tried, info.date);
                        break;
                    }

                    QStringList ignore = attempt.tried;
                    // Retention is dropped when relaxed; the allowance half never
                    // is. A spent allowance is a measured fact, not a figure off
                    // a pricing page.
                    ignore += dispatchExclusions(payLevel, info.date, !relaxed);

                    // This worker holds no budget on an account this article may
                    // actually ask — a grouped account's share can divide to zero
                    // here while another worker still holds it, and an account
                    // over its allowance is excluded outright. Leave the cursor
                    // alone and let a worker that does hold one take the segment.
                    if (w < m_workerServers.size()
                        && !workerCanServe(w, payLevel, ignore)) {
                        rungMismatch = true;
                        break;
                    }

                    UsenetFetchRequest req;
                    req.itemId = rt->item->id;
                    req.fileIndex = fileIndex;
                    req.segmentIndex = segIndex;
                    req.segment = info.segments.at(segIndex);
                    req.targetPath = st.tempPath;
                    req.group = info.groups.isEmpty() ? QString() : info.groups.first();
                    req.level = payLevel;
                    req.ignoreServers = std::move(ignore);

                    rt->inFlight.insert(key);
                    ++rt->planCursor;

                    if (rt->item->status == UsenetItemStatus::Queued) {
                        rt->item->status = UsenetItemStatus::Downloading;
                        emit itemChanged(rt->item->id);
                    }

                    m_workerInFlight[w] += 1;
                    QMetaObject::invokeMethod(
                        m_workers.at(w), "fetchSegment", Qt::QueuedConnection,
                        Q_ARG(eMule::usenet::UsenetFetchRequest, req));

                    dispatched = true;
                    break;
                }
                if (dispatched || rungMismatch)
                    break;
            }

            if (!dispatched) {
                // A rung this worker cannot serve says nothing about the next
                // worker; anything else means there is no work left at all.
                if (!rungMismatch)
                    noMoreWork = true;
                break;
            }
        }
    }

    for (const auto& [rt, key] : std::as_const(unservable)) {
        const int fileIndex = int(key >> 32);
        const int segIndex = int(key & 0xFFFFFFFFu);
        const NzbFileInfo& info = rt->item->nzb.files.at(fileIndex);
        markSegmentMissing(*rt, fileIndex, segIndex,
                           info.segments.at(segIndex).messageId,
                           tr("no configured server can supply it"));
        checkFileCompletion(*rt, fileIndex);
    }
}

void UsenetQueue::onTick()
{
    m_starved = false;

    // Cheap: self-gated to kDiskCheckIntervalMs, and a no-op when the user has
    // the check switched off.
    refreshDiskState();

    // Wire bytes as they are read, not decoded bytes at article completion: a
    // throttled connection spends seconds on one article, and forty that started
    // together finish together, so a completion count reads as bursts and gaps
    // while the line is in fact full the whole time.
    const qint64 wire = NntpSocket::totalWireBytesRead();
    m_rateWindow.push(wire - m_lastWireBytes, kTickMs);
    m_lastWireBytes = wire;
    m_stats.tick(kTickMs, currentRate(), NntpSocket::openConnectionCount());

    // The allowance clock, on the queue's own tick rather than lazily inside
    // add(). A parked queue is by definition spending nothing, so a check that
    // only ran when bytes arrived would never fire and a single-account queue
    // would stay parked past its own billing day forever.
    //
    // Flushed on time *and* on volume: statsSaveInterval says how much
    // measurement an unclean exit may cost, and a minute at 5 MB/s is 300 MB of
    // prepaid block credit.
    const uint32 saveInterval = thePrefs.statsSaveInterval();
    const bool dueByTime = saveInterval > 0
        && ++m_usageTicks * kTickMs >= int(saveInterval) * 1000;
    if (dueByTime || m_usage.unflushedBytes() >= kUsageFlushBytes) {
        m_usageTicks = 0;
        if (m_usage.rollOverIfDue()) {
            refreshQuotaState();
            unparkQuotaStalls();
        }
        m_usage.flush();
    }

    for (auto& rt : m_items) {
        if (rt->dirty) {
            persist(*rt);
            emit itemChanged(rt->item->id);
        }
    }

    // A segment that found every server blocked comes back immediately and its
    // plan cursor has already moved past it, so a rewind is what actually retries
    // it once the backoff expires.
    for (auto& rt : m_items) {
        if (!rt->item->isActive() || !rt->inFlight.isEmpty())
            continue;
        if (rt->planCursor < rt->plan.size())
            continue;
        // Only worth rewinding if something is still unresolved. Without this an
        // item stuck for another reason — no incoming directory, say — would
        // rebuild a 10 000-entry plan four times a second forever.
        //
        // isPlanned() rather than a bare loop over files: the recovery volumes
        // nobody asked for are never finalized, so counting them here would make
        // *every* finished item look pending and rebuild its plan forever.
        bool pending = false;
        for (int f = 0; f < rt->item->files.size(); ++f) {
            if (isPlanned(*rt, f) && !rt->item->files.at(f).finalized) {
                pending = true;
                break;
            }
        }

        // The restart path for the PAR2 name list. A restored item's index .par2
        // sealed in an earlier session, so no sealFile() is coming to ask for it
        // — the same gap checkItemCompletion() is called here to cover.
        learnPar2Names(*rt);

        if (pending) {
            rebuildPlan(*rt);
        } else {
            // Nothing left to fetch. Normally checkFileCompletion() has already
            // moved the item on, but a restored one arrives with every file
            // sealed and no segment event coming to trigger that — so this is
            // what resumes post-processing after a restart.
            checkItemCompletion(*rt);
        }
    }

    dispatch();
}

// ---------------------------------------------------------------------------
// Private — results
// ---------------------------------------------------------------------------

void UsenetQueue::onSegmentFinished(const UsenetFetchResult& result, bool current)
{
    // First, and above the item lookup below. Bytes spent on an article whose
    // item was removed mid-flight were still spent, and stopWorkers() delivers
    // its last results *after* stop() has cleared m_items. Keyed by the id the
    // result carries rather than by a lookup into m_servers, because
    // applyServers() replaces that list on every settings save.
    if (!result.accountId.isEmpty() && result.rawBytes > 0)
        m_usage.add(result.accountId, result.rawBytes);
    m_stats.noteResult(result);

    // Only for a worker that still exists. A torn-down worker's slot number now
    // belongs to its replacement, and stopWorkers() already zeroed the list.
    if (current && result.workerIndex >= 0 && result.workerIndex < m_workerInFlight.size()
        && m_workerInFlight.at(result.workerIndex) > 0) {
        m_workerInFlight[result.workerIndex] -= 1;
    }

    ItemRuntime* rt = runtimeFor(result.itemId);
    if (!rt) {
        // Removed while in flight. Nothing to record.
        dispatch();
        return;
    }

    // A probe answered. Routed here, above everything below, because both of the
    // terminals below are destructive for a STAT: markSegmentDone() would add
    // zero decoded bytes, seal the file and start post-processing, and
    // markSegmentMissing() would set the resolved bit — stopping the article
    // ever being fetched — and inflate the PAR2 damage estimate at the same time.
    if (result.probeOnly) {
        // A probe from a torn-down worker answers a check that no longer exists;
        // applyServers() put the item back to its resume status.
        if (current)
            handleProbeResult(*rt, result);
        dispatch();
        return;
    }

    const quint64 key = SegmentKey{result.fileIndex, result.segmentIndex}.packed();
    if (current)
        rt->inFlight.remove(key);

    // A failure nobody is to blame for and nothing can be learned from:
    //
    //   - aborted / stale: we cut the article off ourselves (engine stop,
    //     settings save). stopWorkers() already cleared every in-flight marker
    //     and rewound every plan cursor, so the segment is dispatched again as
    //     it stands. Spending a retry here is how seven settings saves used to
    //     fail an item with "Shutting down";
    //   - the item is no longer active: a paused or failed item's leftovers.
    //     resumeItem() clears the attempts and rebuilds the plan, and failing an
    //     already-failed item again would count and announce it twice.
    if (result.error != NntpError::None
        && (result.aborted || !current || !rt->item->isActive())) {
        // The one case where the plan still needs it back: a live worker's own
        // shutdown result, which stopWorkers() has not rewound (yet).
        if (current && rt->item->isActive()) {
            rt->plan.insert(qBound(0, rt->planCursor, int(rt->plan.size())), key);
        }
        dispatch();
        return;
    }

    if (result.error == NntpError::None) {
        markSegmentDone(*rt, result);
    } else if (result.noServerAvailable) {
        // Nothing was leasable. Put the segment back untouched — no retry spent,
        // no level moved — and wait for the tick. Re-dispatching here instead
        // would spin: the next attempt would fail the same way, immediately.
        //
        // Back at the cursor, not at the tail, for the same reason as the retry
        // in handleSegmentFailure — and appending here silently undoes that one.
        // A dropped connection backs its server off, so the retry comes straight
        // back as "nothing leasable", and a tail append then moves it behind
        // every segment it was supposed to jump ahead of.
        //
        // It cannot block the rest of the plan: dispatch() advances the cursor
        // as it hands a segment out, so the same round goes on to the next one,
        // and m_starved is what holds this to one attempt per tick.
        rt->plan.insert(qBound(0, rt->planCursor, int(rt->plan.size())),
                        SegmentKey{result.fileIndex, result.segmentIndex}.packed());
        m_starved = true;
        return;
    } else {
        handleSegmentFailure(*rt, result);
    }

    dispatch();
}

void UsenetQueue::markSegmentDone(ItemRuntime& rt, const UsenetFetchResult& result)
{
    if (result.fileIndex < 0 || result.fileIndex >= rt.item->files.size())
        return;

    UsenetFileState& st = rt.item->files[result.fileIndex];
    if (result.segmentIndex >= 0 && result.segmentIndex < st.done.size()) {
        // Already resolved. A torn-down worker's result can arrive after the
        // segment was dispatched again, and adding its bytes a second time
        // would put the file over its own size.
        if (st.done.testBit(result.segmentIndex))
            return;
        st.done.setBit(result.segmentIndex);
    }

    st.decodedBytes += result.decodedBytes;

    // Where those bytes actually landed. This is the only record of it: `done`
    // above means *resolved*, and a segment missing on every server sets that
    // bit having written nothing at all.
    st.addWritten(result.decodedOffset, result.decodedBytes);

    // The largest article seen is the part length: a poster cuts a file into
    // equal parts and one short remainder. Phase 6b maps an unfetched byte to
    // its article with it, and there is no other source for the number.
    st.partLength = qMax(st.partLength, result.decodedBytes);

    if (st.articleFileName.isEmpty() && !result.articleFileName.isEmpty())
        st.articleFileName = result.articleFileName;
    if (st.declaredSize == 0 && result.declaredFileSize > 0)
        st.declaredSize = result.declaredFileSize;

    rt.attempts.remove(SegmentKey{result.fileIndex, result.segmentIndex}.packed());
    rt.dirty = true;

    // The one place a hole is allowed to close: the bytes are on disk now. Doing
    // it when a retry *arms* the segment instead would tell direct unpack, the
    // encrypted preview and post-processing that a file padded with zeros is
    // whole, and each of them would act on it.
    if (result.segmentIndex >= 0 && result.segmentIndex < st.missing.size()
        && st.missing.testBit(result.segmentIndex)) {
        st.missing.clearBit(result.segmentIndex);
        st.missingSegments = qMax(0, st.missingSegments - 1);
        noteRefetchResolved(rt, result.fileIndex, result.segmentIndex, /*landed*/ true);
    }

    // Payload of a recovery volume a short verify asked for. The decoded bytes
    // themselves were counted in noteResult(); the Statistics Transfer branch is
    // eD2K's alone.
    if (rt.item->requestedPar2.contains(result.fileIndex) && result.decodedBytes > 0)
        m_stats.bump(&UsenetCounters::recoveryBytes, quint64(result.decodedBytes));

    // Before the completion check, which may seal the file: a name may only be
    // learned while it is still unsealed.
    resolvePar2Name(rt, result.fileIndex);

    checkFileCompletion(rt, result.fileIndex);
}

void UsenetQueue::handleSegmentFailure(ItemRuntime& rt, const UsenetFetchResult& result)
{
    const quint64 key = SegmentKey{result.fileIndex, result.segmentIndex}.packed();
    SegmentAttempt attempt = rt.attempts.value(key);

    // A local fault says nothing about any server, so it takes none of the
    // ladder's machinery with it: the account is not added to `tried`, no
    // transport retry is spent, `requiredFailure` stays clear, and the segment
    // simply goes back into the plan at the cursor. Before this branch existed a
    // full disk was a ProtocolError, which backed the provider off for a minute
    // and — if the account happened to be optional — booked the article as
    // missing on a release that was perfectly fine.
    if (result.error == NntpError::WriteFailed) {
        attempt.writeFailures += 1;

        // Re-check now rather than waiting for the tick: if the volume really is
        // full the queue should park this round, not after another six articles
        // have failed the same way.
        refreshDiskState(/*force*/ true);

        // A fault that is not about space — permissions, a read-only volume, a
        // path that has gone — would otherwise spin here forever. Fail the item
        // with the local error text, which is the one thing this code never used
        // to do: a local problem failing locally, naming the path and not a
        // provider.
        if (attempt.writeFailures > kMaxWriteFailures && !m_diskBlocked) {
            rt.attempts.remove(key);
            failItem(rt, tr("Cannot write to the download folder: %1")
                             .arg(result.text.isEmpty() ? describeNntpError(result.error)
                                                        : result.text));
            return;
        }

        rt.attempts.insert(key, attempt);
        rt.plan.insert(qBound(0, rt.planCursor, int(rt.plan.size())), key);
        rt.dirty = true;
        return;
    }

    const qint64 posted = (result.fileIndex >= 0
                           && result.fileIndex < rt.item->nzb.files.size())
                              ? rt.item->nzb.files.at(result.fileIndex).date
                              : 0;

    if (escalatesToNextLevel(result.error)) {
        // This account does not have the article. Record that and nothing else:
        // the rung is derived from `tried`, so a sibling on the *same* level is
        // asked next and the ladder only moves up once the level is exhausted.
        // Incrementing a stored level here is what used to send the first 430
        // straight to a paid fill server past an idle sibling.
        if (!result.serverKey.isEmpty() && !attempt.tried.contains(result.serverKey))
            attempt.tried.append(result.serverKey);

        // nextServableLevel() is quota-blind by construction, so a -1 here means
        // every account was actually *asked* and said no — an allowance can
        // never reach this verdict. That is the whole safety property, and it is
        // why there is no quota test on this branch.
        bool relaxed = false;
        if (!m_ladder.isEmpty()
            && nextServableLevel(attempt.tried, posted, relaxed) < 0) {
            // Genuinely missing everywhere. The file is short; PAR2 repair in
            // phase 4 is what will rescue it. Do not fail the whole item — a
            // release with one dead article is usually still repairable.
            //
            // "Damaged" rather than "missing" when the last server to be asked
            // had a copy it could not decode: the two look identical from here
            // and read very differently in a log.
            markSegmentMissing(rt, result.fileIndex, result.segmentIndex, result.messageId,
                               result.error == NntpError::ArticleCorrupt
                                   ? tr("no server has an undamaged copy")
                                   : tr("missing on every server"));
            checkFileCompletion(rt, result.fileIndex);
            return;
        }
    } else {
        // A connection fault, not a content one. Stay on this level — the worker
        // has already backed the server off, so a sibling account picks it up.
        //
        // Whose fault it was decides what happens when the budget runs out. A
        // block or fill account is allowed to be down; the download must not die
        // with it. An account with no key is a local fault (ArticleWriter failing
        // to open the target), and that is nobody's block account.
        const NewsServer* server = serverFor(result.serverKey);
        const bool blameless = server && server->optional && !m_allServersOptional;
        if (!blameless)
            attempt.requiredFailure = true;

        // An account skipped only because it has spent its allowance is a
        // candidate we chose not to pay for, so this retry was spent against an
        // artificially narrowed set. Charging it would let a spending limit burn
        // a budget sized on the premise that a sibling picks the article up.
        const bool quotaNarrowed = quotaBlockedCandidateExists(attempt.tried);
        if (!quotaNarrowed)
            attempt.transportRetries += 1;
        if (!quotaNarrowed && attempt.transportRetries > kMaxTransportRetries) {
            if (!attempt.requiredFailure) {
                // Only optional accounts ever failed here, so the article is
                // simply unavailable rather than the item being broken.
                markSegmentMissing(rt, result.fileIndex, result.segmentIndex,
                                   result.messageId, tr("optional server unavailable"));
                checkFileCompletion(rt, result.fileIndex);
                return;
            }
            // Through failItem(), not by hand: an item that dies here has the
            // same extraction and preview runs waiting on volumes that are now
            // never coming as one that dies in post-processing.
            rt.attempts.remove(key);
            failItem(rt, result.text.isEmpty() ? describeNntpError(result.error)
                                               : result.text);
            return;
        }
    }

    rt.attempts.insert(key, attempt);

    // The cursor has already passed this segment, so put it back in the plan —
    // at the cursor, so it is the *next* thing dispatched rather than the last.
    //
    // Appending it to the tail is what the first cut did, and it pins the
    // written prefix at this byte for the rest of the download: every later
    // segment lands, and the one hole the player is waiting on is refetched
    // only once everything else is finished. Streaming aside, retrying near the
    // front also finishes files sooner.
    //
    // This does not weaken the retry bound. Termination is still whatever
    // handleSegmentFailure decided above: the level ladder, kMaxTransportRetries,
    // or the missing-everywhere path that sets the done bit and returns before
    // reaching here. Only the position changed.
    rt.plan.insert(qBound(0, rt.planCursor, int(rt.plan.size())), key);
}

void UsenetQueue::checkFileCompletion(ItemRuntime& rt, int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= rt.item->files.size())
        return;

    UsenetFileState& st = rt.item->files[fileIndex];
    if (st.finalized || !st.allSegmentsDone())
        return;

    sealFile(rt, fileIndex);
    pumpDirectUnpack(rt, fileIndex);
    checkItemCompletion(rt);
}

bool UsenetQueue::isPlanned(const ItemRuntime& rt, int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= rt.item->nzb.files.size())
        return false;

    // A recovery volume counts only once somebody has asked for it. Everything
    // else — payload and the index .par2 alike — is always in the plan.
    if (!rt.item->nzb.files.at(fileIndex).isPar2Volume())
        return true;
    return rt.item->requestedPar2.contains(fileIndex);
}

void UsenetQueue::sealFile(ItemRuntime& rt, int fileIndex)
{
    UsenetFileState& st = rt.item->files[fileIndex];

    // The streaming index caches volume paths and parsed headers, and this
    // renames the file out from under both. A stale path is indistinguishable
    // from a missing one, so drop the lot rather than try to patch it.
    rt.streamIndex.invalidate();

    // Pad the file out to the length yEnc declared for it.
    //
    // A missing article leaves a hole, and PAR2 recovers a hole perfectly well
    // — it slides a window over the file and matches blocks by CRC. What it
    // cannot do anything with is a file that is simply *short*, which is what a
    // missing article at the end of a file produces: every block after the hole
    // sits at the wrong offset and the release reads as unrecoverable.
    //
    // declaredSize arrives with the first article that turns up, so it is zero
    // only when a file is missing its opening article too. Nothing can be
    // inferred in that case — the NZB's own `bytes` is the *encoded* size — so
    // the file is left as it is and verification will call it damaged, which is
    // the honest answer.
    padToDeclaredSize(st);

    // Drop the .usenetpart suffix and take the real filename, still inside the
    // item's scratch directory.
    //
    // Everything downstream works by extension: par2 finds its own set by name,
    // the unpacker picks volume one out of ".part01.rar", and neither can see
    // anything through a ".usenetpart" tail. Post-processing therefore needs the
    // real names before it runs, and it runs before anything is published.
    //
    // That gives up guard 2 of the four that keep Usenet scratch off the network
    // — the by-name suffix skip — for these files, deliberately. The two that
    // matter still hold: shouldBeShared() refuses the whole usenetTempDir() tree
    // whatever a file is called, and the share scan does not recurse into it in
    // the first place. Guard 2 goes on doing its real job at the other end, on
    // the staged copy inside the incoming directory.
    QString name = rt.item->bestFileName(fileIndex);
    if (name.isEmpty())
        name = QStringLiteral("%1-%2").arg(rt.item->name).arg(fileIndex);
    name = QFileInfo(name).fileName();       // never let a stranger choose a directory

    const QString itemDir = QFileInfo(st.tempPath).absolutePath();

    // Already sealed under this exact name — a retry re-armed one of its
    // articles and the last of them has just landed. uniqueDestination() tests
    // QFile::exists() with no identity check, so asking it again would collide
    // the file with itself and mint "movie (1).mkv".
    if (st.tempPath == QDir(itemDir).filePath(name)) {
        st.finalized = true;
        rt.dirty = true;
        return;
    }

    const QString sealedPath = uniqueDestination(itemDir, name);

    if (st.tempPath != sealedPath) {
        if (QFile::rename(st.tempPath, sealedPath)) {
            st.tempPath = sealedPath;
        } else {
            logWarning(QStringLiteral("Usenet: cannot name \"%1\" in the work folder; "
                                      "post-processing may not recognise it").arg(name));
        }
    }

    st.finalized = true;
    rt.dirty = true;

    // The index .par2 is the one file in the release that says what the others
    // are called, so the moment it lands is the moment to ask it.
    if (fileIndex < rt.item->nzb.files.size()
        && rt.item->nzb.files.at(fileIndex).isPar2()
        && !rt.item->nzb.files.at(fileIndex).isPar2Volume()) {
        learnPar2Names(rt);
    }

    // The file is now its declared length on disk, so the whole of it is
    // readable — holes included, as zeros. Collapsing the interval list says
    // exactly that and keeps a completed file's state to one entry.
    //
    // A hole is not a lie here: the bytes exist and every later byte is at its
    // right offset, which is what a player needs. PAR2 repair is what turns the
    // zeros back into content. With declaredSize unknown nothing was padded, so
    // the ranges stay as they are.
    if (st.declaredSize > 0) {
        st.written.clear();
        st.written.append({qint64(0), st.declaredSize});
    }

    if (st.missingSegments > 0) {
        logInfo(QStringLiteral("Usenet: assembled \"%1\" with %2 article(s) missing")
                    .arg(QFileInfo(st.tempPath).fileName())
                    .arg(st.missingSegments));
    }
}

void UsenetQueue::checkItemCompletion(ItemRuntime& rt)
{
    if (rt.postRunning)
        return;

    // Paused, failed or checking. pauseItem() does not cancel the articles
    // already in flight, and the last of them landing must not carry the item
    // off into post-processing behind the user's back. onTick() comes back here
    // once the item is active again and its plan is exhausted.
    if (!rt.item->isActive())
        return;

    // "Every *planned* file is finished", not every file. The recovery volumes
    // deliberately left out of the plan must not hold the item open forever.
    for (int f = 0; f < rt.item->files.size(); ++f) {
        if (!isPlanned(rt, f))
            continue;
        if (!rt.item->files.at(f).finalized)
            return;
    }

    // Every volume is in. Tell the extractions so, then wait for them: a run
    // still reading is about to produce exactly what post-processing would
    // otherwise redo, and its result has to be in the job.
    endDirectUnpackSets(rt);
    for (const DirectUnpackRun& run : std::as_const(rt.directUnpack)) {
        if (run.running)
            return;   // onDirectUnpackFinished() comes back here
    }

    beginPostProcessing(rt);
}

// ---------------------------------------------------------------------------
// Direct unpack — extraction that keeps pace with the download
//
// A volume is complete the moment its last article lands, and libarchive reads
// a set front to back, so the extraction can simply follow the download instead
// of starting after it. What arrives here is one sealed volume; the run that is
// following that set takes it and carries on.
// ---------------------------------------------------------------------------

void UsenetQueue::pumpDirectUnpack(ItemRuntime& rt, int fileIndex)
{
    if (!m_directUnpackEnabled || !m_unpackEnabled)
        return;
    // A volume sealed by an article that outlived the download. pauseItem() and
    // failItem() cancelled the runs precisely so nothing would sit holding a
    // thread for volumes that are not coming; restartDirectUnpack() picks the
    // set up again on resume.
    if (!rt.item->isActive())
        return;
    if (fileIndex < 0 || fileIndex >= rt.item->files.size())
        return;

    const UsenetFileState& st = rt.item->files.at(fileIndex);
    const auto position = UsenetUnpacker::volumePositionOf(QFileInfo(st.tempPath).fileName());
    if (position.index < 0)
        return;   // not an archive volume; nothing to follow

    // A hole is padded with zeros by sealFile(), which decompresses into
    // garbage or a CRC failure. Stop the set here and let PAR2 do its job; the
    // end-of-download unpack will run on the repaired volumes.
    if (st.missingSegments > 0) {
        auto it = rt.directUnpack.find(position.baseName);
        if (it != rt.directUnpack.end() && it->running && it->worker)
            it->worker->cancel();
        return;
    }

    // The naming schemes do not agree on where a set starts: `.partNN.rar`
    // counts from 1, while a bare `.rar` is volume 0 of the `.rNN` scheme. So
    // the position number is not the ordinal — rank the whole set and use that.
    const int ordinal = volumeOrdinal(rt, fileIndex, position.baseName);
    if (ordinal < 0)
        return;

    // Only ever *start* on volume one — handed a later volume first, libarchive
    // reads a headerless fragment and calls the set corrupt. But the volume that
    // happens to seal first is not the set's state: a release whose NZB lists
    // part03 ahead of part01 sealed part03 long ago, and dropping it on the floor
    // parks the run on an index nobody will ever offer.
    const auto existing = rt.directUnpack.constFind(position.baseName);
    const bool haveRun = existing != rt.directUnpack.constEnd() && existing->worker != nullptr;
    if (!haveRun) {
        if (!startDirectUnpack(rt, position.baseName))
            return;
    }

    DirectUnpackRun& run = rt.directUnpack[position.baseName];
    if (run.running && run.worker)
        run.worker->offerVolume(ordinal, st.tempPath);
}

bool UsenetQueue::startDirectUnpack(ItemRuntime& rt, const QString& baseName)
{
    // Volume one has to be on disk, whichever volume brought us here.
    int firstIndex = -1;
    for (int f = 0; f < rt.item->files.size(); ++f) {
        const auto pos = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, f));
        if (pos.index < 0 || pos.baseName != baseName)
            continue;
        if (volumeOrdinal(rt, f, baseName) == 0) {
            firstIndex = f;
            break;
        }
    }
    if (firstIndex < 0 || !rt.item->files.at(firstIndex).finalized)
        return false;
    if (m_directUnpackRuns >= kMaxDirectUnpacks)
        return false;   // over the cap: this set falls back to the end-of-download path

    DirectUnpackRun& run = rt.directUnpack[baseName];
    run.worker = new UsenetDirectUnpack;
    run.thread = new QThread;
    run.thread->setObjectName(QStringLiteral("UsenetDirectUnpack"));
    run.worker->moveToThread(run.thread);
    connect(run.worker, &UsenetDirectUnpack::finished,
            this, &UsenetQueue::onDirectUnpackFinished, Qt::QueuedConnection);
    connect(run.worker, &UsenetDirectUnpack::progress,
            this, &UsenetQueue::onDirectUnpackProgress, Qt::QueuedConnection);
    run.thread->start();
    run.running = true;
    run.closing = false;
    ++m_directUnpackRuns;

    const UsenetFileState& first = rt.item->files.at(firstIndex);
    UsenetDirectUnpackJob job;
    job.itemId = rt.item->id;
    job.setKey = baseName;
    job.destDir = QDir(QFileInfo(first.tempPath).absolutePath()).filePath(QString(kUnpackDirName));
    job.password = rt.item->nzb.password;
    QMetaObject::invokeMethod(run.worker, "run", Qt::QueuedConnection,
                              Q_ARG(eMule::usenet::UsenetDirectUnpackJob, job));

    // Replay what already sealed. The run blocks on the first index it has not
    // been given, so anything offered ahead of time simply waits in the map.
    for (int f = 0; f < rt.item->files.size(); ++f) {
        const UsenetFileState& st = rt.item->files.at(f);
        if (!st.finalized || st.missingSegments > 0)
            continue;
        const auto pos = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, f));
        if (pos.index < 0 || pos.baseName != baseName)
            continue;
        const int ordinal = volumeOrdinal(rt, f, baseName);
        if (ordinal >= 0)
            run.worker->offerVolume(ordinal, st.tempPath);
    }
    return true;
}

void UsenetQueue::restartDirectUnpack(ItemRuntime& rt)
{
    if (!m_directUnpackEnabled || !m_unpackEnabled)
        return;

    QSet<QString> seen;
    for (int f = 0; f < rt.item->files.size(); ++f) {
        const auto pos = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, f));
        if (pos.index < 0 || seen.contains(pos.baseName))
            continue;
        seen.insert(pos.baseName);

        const auto it = rt.directUnpack.constFind(pos.baseName);
        if (it != rt.directUnpack.constEnd() && it->worker != nullptr)
            continue;
        startDirectUnpack(rt, pos.baseName);
    }
}

void UsenetQueue::onDirectUnpackProgress(const eMule::usenet::UsenetDirectUnpackProgress& state)
{
    ItemRuntime* rt = runtimeFor(state.itemId);
    if (!rt)
        return;

    auto it = rt->directUnpack.find(state.setKey);
    if (it != rt->directUnpack.end())
        it->progress = state;
}

const UsenetDirectUnpackEntry* UsenetQueue::extractionEntryFor(ItemRuntime& rt, int fileIndex,
                                                              int entryOrdinal)
{
    const auto position = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, fileIndex));
    if (position.index < 0)
        return nullptr;

    const auto run = rt.directUnpack.constFind(position.baseName);
    if (run == rt.directUnpack.constEnd() || run->progress.entries.isEmpty())
        return nullptr;

    // The index numbers the members it managed to place; libarchive numbers
    // everything it walks past. Where both have an opinion the name is the only
    // thing that means the same on either side, so the ordinal is translated
    // through it rather than trusted across.
    QString wanted;
    const StreamListing listed = rt.streamIndex.list(*rt.item, fileIndex);
    if (!listed.members.isEmpty()) {
        for (const StreamMember& m : listed.members) {
            if (entryOrdinal < 0 ? isPlayableName(m.name) : m.index == entryOrdinal) {
                wanted = QFileInfo(m.name).fileName();
                break;
            }
        }
        if (wanted.isEmpty())
            return nullptr;
    }

    for (const UsenetDirectUnpackEntry& e : run->progress.entries) {
        if (e.index < 0 || e.path.isEmpty())
            continue;
        if (!wanted.isEmpty()) {
            if (QFileInfo(e.path).fileName() == wanted)
                return &e;
            continue;
        }
        if (entryOrdinal < 0 ? isPlayableName(e.name) : e.index == entryOrdinal)
            return &e;
    }
    return nullptr;
}

bool UsenetQueue::streamFromExtraction(ItemRuntime& rt, int fileIndex, int entryOrdinal,
                                       StreamInfo& info)
{
    if (!m_directUnpackEnabled || !m_unpackEnabled)
        return false;

    const auto position = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, fileIndex));
    if (position.index < 0)
        return false;

    const auto run = rt.directUnpack.constFind(position.baseName);
    if (run != rt.directUnpack.constEnd() && !run->running && !run->result.ok
        && !run->result.error.isEmpty()) {
        return false;   // the extraction failed and is not coming back
    }

    const UsenetDirectUnpackEntry* entry = extractionEntryFor(rt, fileIndex, entryOrdinal);
    if (!entry) {
        // Nothing has been extracted yet, and the first request for a set always
        // arrives before the first volume has. Answering "never" here ends a
        // preview that is a minute from working — but only wait when there is
        // something worth waiting for: a media member the map could not place.
        // A release with nothing playable in it says so at once, as it should.
        if (!rt.item->isActive())
            return false;

        const StreamListing listed = rt.streamIndex.list(*rt.item, fileIndex);
        for (const StreamMember& m : listed.members) {
            if (!m.playable || m.mappable)
                continue;
            if (entryOrdinal < 0 || m.index == entryOrdinal)
                return true;   // "wait": it is coming
        }
        return false;
    }

    if (run == rt.directUnpack.constEnd())
        return false;

    // Everything from here answers "wait" rather than "never": the caller must
    // not turn any of it into a refusal, because the route treats a reason as
    // final and a player that gets one does not come back.
    QString path = entry->path;
    qint64 readable = entry->bytesReadable;
    bool complete = entry->finished;

    if (!QFileInfo::exists(path)) {
        // Staging renamed it into the incoming directory, flattened to its base
        // name. Following it there is what keeps playback alive across the
        // moment the download finishes.
        const QFileInfo published(QDir(thePrefs.incomingDirForCategory(rt.item->category))
                                      .filePath(QFileInfo(entry->path).fileName()));
        if (!published.exists())
            return true;
        path = published.absoluteFilePath();
        readable = published.size();
        complete = true;
    } else if (run->closing) {
        return true;   // a repair is about to discard it, or staging to move it
    }

    // No declared size, no answer. serveRange() derives the total from the
    // pieces when it is not told one, and for a growing file that total is
    // whatever had been extracted — which makes the player's opening request,
    // the one with no Range header, a 200 OK carrying "the whole movie".
    if (entry->entrySize <= 0)
        return true;

    // A run that restarted truncated its output back to zero. Never advertise
    // less than was advertised before: wait for the new run to catch up.
    qint64& high = rt.streamHighWater[path];
    if (readable < high)
        return true;
    high = readable;

    if (readable <= 0)
        return true;

    info.fileName = entry->name;
    info.totalSize = entry->entrySize;
    info.availableEnd = qMin(readable, entry->entrySize);
    info.complete = complete;
    info.notSeekableReason.clear();
    info.pieces.append({path, 0, 0, info.availableEnd});
    return true;
}

void UsenetQueue::annotateFromExtraction(ItemRuntime& rt, int fileIndex, int entryOrdinal,
                                         ArchiveEntryInfo& row)
{
    const UsenetDirectUnpackEntry* entry = extractionEntryFor(rt, fileIndex, entryOrdinal);
    if (!entry)
        return;

    if (entry->entrySize > 0 && entry->bytesReadable > 0) {
        row.playable = true;
        row.note.clear();
        return;
    }
    row.note = tr("Extracting — playable shortly");
}

void UsenetQueue::appendExtractionRows(ItemRuntime& rt, int fileIndex, ArchiveListing& listing)
{
    const auto position = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, fileIndex));
    if (position.index < 0)
        return;

    const auto run = rt.directUnpack.constFind(position.baseName);
    if (run == rt.directUnpack.constEnd())
        return;

    for (const UsenetDirectUnpackEntry& e : run->progress.entries) {
        if (e.index < 0 || e.name.isEmpty())
            continue;

        ArchiveEntryInfo row;
        row.entry = e.index;
        row.name = e.name;
        row.size = e.entrySize;
        row.playable = isPlayableName(e.name) && e.entrySize > 0 && e.bytesReadable > 0;
        if (!row.playable) {
            row.note = isPlayableName(e.name) ? tr("Extracting — playable shortly")
                                              : tr("Not playable");
        }
        listing.entries.append(row);
    }
}

void UsenetQueue::promoteExtractionVolume(ItemRuntime& rt, int fileIndex)
{
    const auto position = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, fileIndex));
    if (position.index < 0)
        return;

    auto run = rt.directUnpack.find(position.baseName);

    // A compressed member has no byte map, so the articles covering the
    // requested *offset* mean nothing. What unblocks playback is the volume the
    // extraction is parked on — or, before there is an extraction at all, volume
    // one, the only volume a run may start on. Without that a preview waits for
    // the scheduler to reach volume one in whatever order the NZB listed it.
    const bool live = run != rt.directUnpack.end() && run->running && run->worker;
    const int waiting = live ? run->worker->waitingForVolume() : 0;
    if (waiting < 0)
        return;
    if (run != rt.directUnpack.end()) {
        if (waiting == run->promotedVolume)
            return;   // promoting a whole volume is O(plan) per segment: once each
        run->promotedVolume = waiting;
    }

    // That volume and the one after it, or the pipeline empties every time a
    // volume completes.
    for (int f = 0; f < rt.item->files.size(); ++f) {
        const auto pos = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, f));
        if (pos.index < 0 || pos.baseName != position.baseName)
            continue;
        const int ordinal = volumeOrdinal(rt, f, position.baseName);
        if (ordinal < waiting || ordinal > waiting + 1)
            continue;
        const UsenetFileState& st = rt.item->files.at(f);
        if (!st.finalized && st.declaredSize > 0)
            promoteRange(rt, f, 0, st.declaredSize);
    }
}

int UsenetQueue::volumeOrdinal(const ItemRuntime& rt, int fileIndex, const QString& baseName)
{
    QList<QPair<int, int>> members;   // (volume number, NZB file index)
    for (int f = 0; f < rt.item->files.size(); ++f) {
        const auto pos = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, f));
        if (pos.index >= 0 && pos.baseName == baseName)
            members.append({pos.index, f});
    }
    std::sort(members.begin(), members.end());

    for (int k = 0; k < members.size(); ++k) {
        if (members.at(k).second == fileIndex)
            return k;
    }
    return -1;
}

QString UsenetQueue::volumeNameOf(const ItemRuntime& rt, int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= rt.item->files.size())
        return {};
    // A sealed file already carries its real name on disk, which beats every
    // guess; before that, whatever the best source says.
    const UsenetFileState& st = rt.item->files.at(fileIndex);
    if (st.finalized)
        return QFileInfo(st.tempPath).fileName();
    return rt.item->bestFileName(fileIndex);
}

void UsenetQueue::endDirectUnpackSets(ItemRuntime& rt)
{
    for (DirectUnpackRun& run : rt.directUnpack) {
        if (run.running && run.worker)
            run.worker->endOfSet();
    }
}

// ---------------------------------------------------------------------------
// Encrypted preview — the third byte source
//
// A password-protected set defeats both of the others. The byte map cannot
// work in principle: the bytes on disk are encrypted, so a slice of a volume is
// not a slice of the movie. Direct unpack cannot work in practice: it feeds
// libarchive one volume at a time, and libarchive is exactly the thing that
// cannot decrypt RAR.
//
// So run the external tool over the volumes that *have* landed and keep the
// prefix it produces before it hits one that has not. RAR allows that because
// its headers sit at the front of every volume. 7z does not and never will —
// its metadata lives at the end of the set, so an incomplete one decodes to
// zero bytes.
//
// Every run restarts at byte 0 with no resume, so the cost of this grows with
// the prefix. Nothing below starts on its own: it takes a preview request, new
// volumes since the last run, and kEncryptedPreviewRerunMs.
// ---------------------------------------------------------------------------

bool UsenetQueue::streamFromEncryptedPreview(ItemRuntime& rt, int fileIndex, StreamInfo& info)
{
    if (!thePrefs.usenetEncryptedPreview())
        return false;

    // No password, nothing to decrypt with. The refusal the user then sees names
    // the password, which is the actionable half of the problem.
    if (rt.item->nzb.password.isEmpty())
        return false;

    const auto position = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, fileIndex));
    if (position.index < 0)
        return false;
    if (!setIsRar(rt, position.baseName))
        return false;

    // A keep-pace extraction that already ran and failed for some *other* reason
    // settles the question: an external decryptor can do nothing libarchive
    // could not, and spawning one per poll for a set that is merely broken is
    // pure cost. Only meaningful when direct unpack is on — with it off there is
    // no run to ask, and the password plus the format is all we have.
    const auto unpack = rt.directUnpack.constFind(position.baseName);
    if (unpack != rt.directUnpack.constEnd() && !unpack->running && !unpack->result.ok
        && !unpack->result.encrypted) {
        return false;
    }

    EncryptedPreviewRun& run = rt.encryptedPreview;
    if (!run.setKey.isEmpty() && run.setKey != position.baseName)
        return false;   // a second set in the same release; one run per item
    if (run.refused)
        return false;

    maybeStartEncryptedPreview(rt, position.baseName);

    // Everything from here answers "wait" rather than "never", the same
    // contract streamFromExtraction() keeps: the route treats a reason as final,
    // and a player that gets one does not come back.
    if (run.bytes <= 0 || run.memberSize <= 0 || run.member.isEmpty())
        return true;
    if (!QFileInfo::exists(run.path))
        return true;

    info.fileName = QFileInfo(run.member).fileName();
    info.totalSize = run.memberSize;
    info.availableEnd = qMin(run.bytes, run.memberSize);
    info.complete = run.bytes >= run.memberSize;
    info.notSeekableReason.clear();
    info.pieces.append({run.path, 0, 0, info.availableEnd});
    return true;
}

void UsenetQueue::maybeStartEncryptedPreview(ItemRuntime& rt, const QString& baseName)
{
    EncryptedPreviewRun& run = rt.encryptedPreview;
    if (run.running) {
        run.rerunWanted = true;
        return;
    }
    if (run.refused)
        return;
    if (m_encryptedPreviewRuns >= kMaxEncryptedPreviews)
        return;

    const QStringList volumes = sealedVolumesOf(rt, baseName);
    if (volumes.isEmpty())
        return;   // volume one is not in yet; the caller is already saying "wait"

    // A run over the same volumes decrypts the same bytes to the same length.
    if (volumes.size() <= run.volumesAtLastRun && run.bytes > 0)
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (run.lastRunMs != 0 && now - run.lastRunMs < kEncryptedPreviewRerunMs)
        return;

    run.setKey = baseName;
    run.lastRunMs = now;
    run.volumesAtLastRun = int(volumes.size());
    run.rerunWanted = false;
    if (run.path.isEmpty()) {
        // Beside the volumes rather than in the incoming directory: this is
        // scratch, and the share scan must never see it. usenetTempDir() is
        // refused by shouldBeShared() ahead of every other rule.
        run.path = QDir(QDir(thePrefs.usenetTempDir()).filePath(rt.item->id))
                       .filePath(QStringLiteral(".preview-") + baseName);
    }

    if (!run.worker) {
        run.worker = new UsenetEncryptedPreview;
        run.thread = new QThread;
        run.thread->setObjectName(QStringLiteral("UsenetEncryptedPreview"));
        run.worker->moveToThread(run.thread);
        connect(run.worker, &UsenetEncryptedPreview::finished,
                this, &UsenetQueue::onEncryptedPreviewFinished, Qt::QueuedConnection);
        run.thread->start();
    }

    UsenetEncryptedPreviewJob job;
    job.itemId = rt.item->id;
    job.setKey = baseName;
    job.volumes = volumes;
    job.member = run.member;         // empty on the first run; the worker chooses
    job.memberSize = run.memberSize;
    job.password = rt.item->nzb.password;
    job.externalTool = thePrefs.usenetExternalUnpacker();
    job.outPath = run.path;

    run.running = true;
    ++m_encryptedPreviewRuns;
    QMetaObject::invokeMethod(run.worker, "run", Qt::QueuedConnection,
                              Q_ARG(eMule::usenet::UsenetEncryptedPreviewJob, job));
}

void UsenetQueue::onEncryptedPreviewFinished(
    const eMule::usenet::UsenetEncryptedPreviewResult& result)
{
    ItemRuntime* rt = runtimeFor(result.itemId);
    if (m_encryptedPreviewRuns > 0)
        --m_encryptedPreviewRuns;
    if (!rt)
        return;

    EncryptedPreviewRun& run = rt->encryptedPreview;
    run.running = false;
    run.refused = run.refused || result.refused;

    if (!result.member.isEmpty()) {
        run.member = result.member;
        run.memberSize = result.memberSize;
    }
    // Only ever raised. A run killed early, or one that lost a race with a
    // repair, must not shorten a file a player is already reading.
    if (result.bytes > run.bytes)
        run.bytes = result.bytes;

    if (run.rerunWanted && !run.refused) {
        run.rerunWanted = false;
        // Past the interval by construction: the run itself took longer than a
        // poll, and this only fires when somebody asked during it.
        run.lastRunMs = 0;
        maybeStartEncryptedPreview(*rt, run.setKey);
    }
}

void UsenetQueue::promoteEncryptedPreviewVolumes(ItemRuntime& rt, int fileIndex)
{
    const auto position = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, fileIndex));
    if (position.index < 0)
        return;

    // The first unsealed volume, and the one after it. A run stops at the first
    // gap, so that volume is exactly what the next run needs; asking for two
    // keeps the pipeline from emptying every time one completes.
    const int have = int(sealedVolumesOf(rt, position.baseName).size());
    for (int f = 0; f < rt.item->files.size(); ++f) {
        const auto pos = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, f));
        if (pos.index < 0 || pos.baseName != position.baseName)
            continue;
        const int ordinal = volumeOrdinal(rt, f, position.baseName);
        if (ordinal < have || ordinal > have + 1)
            continue;
        const UsenetFileState& st = rt.item->files.at(f);
        if (!st.finalized && st.declaredSize > 0)
            promoteRange(rt, f, 0, st.declaredSize);
    }
}

QStringList UsenetQueue::sealedVolumesOf(const ItemRuntime& rt, const QString& baseName) const
{
    QHash<int, QString> byOrdinal;
    for (int f = 0; f < rt.item->files.size(); ++f) {
        const auto pos = UsenetUnpacker::volumePositionOf(volumeNameOf(rt, f));
        if (pos.index < 0 || pos.baseName != baseName)
            continue;
        const UsenetFileState& st = rt.item->files.at(f);
        // missingSegments matters as much as finalized: sealFile() pads a hole
        // with zeros, and zeros inside an encrypted stream are not a short read,
        // they are wrong bytes that decrypt to garbage.
        if (!st.finalized || st.missingSegments > 0)
            continue;
        const int ordinal = volumeOrdinal(rt, f, baseName);
        if (ordinal >= 0)
            byOrdinal.insert(ordinal, st.tempPath);
    }

    QStringList volumes;
    for (int i = 0; byOrdinal.contains(i); ++i)
        volumes.append(byOrdinal.value(i));
    return volumes;
}

bool UsenetQueue::setIsRar(const ItemRuntime& rt, const QString& baseName) const
{
    for (int f = 0; f < rt.item->files.size(); ++f) {
        const QString name = volumeNameOf(rt, f);
        const auto pos = UsenetUnpacker::volumePositionOf(name);
        if (pos.index < 0 || pos.baseName != baseName)
            continue;
        const QString lower = name.toLower();
        if (lower.endsWith(QLatin1String(".rar")))
            return true;
        // `.r00`, `.r01`, … — the old scheme, whose first volume is the bare
        // `.rar` the branch above catches.
        static const QRegularExpression rNN(QStringLiteral("\\.r\\d{2,3}$"));
        if (rNN.match(lower).hasMatch())
            return true;
    }
    return false;
}

void UsenetQueue::cancelEncryptedPreview(ItemRuntime& rt)
{
    EncryptedPreviewRun& run = rt.encryptedPreview;
    if (!run.worker)
        return;

    run.worker->cancel();
    run.thread->quit();
    run.thread->wait();
    delete run.worker;
    delete run.thread;
    if (run.running && m_encryptedPreviewRuns > 0)
        --m_encryptedPreviewRuns;
    run.worker = nullptr;
    run.thread = nullptr;
    run.running = false;
    run.rerunWanted = false;
}

void UsenetQueue::cancelDirectUnpack(ItemRuntime& rt)
{
    for (DirectUnpackRun& run : rt.directUnpack) {
        if (!run.worker)
            continue;
        run.worker->cancel();
        run.thread->quit();
        run.thread->wait();
        delete run.worker;
        delete run.thread;
        if (run.running)
            --m_directUnpackRuns;
        run.worker = nullptr;
        run.thread = nullptr;
        run.running = false;
    }
    rt.directUnpack.clear();
}

void UsenetQueue::onDirectUnpackFinished(const eMule::usenet::UsenetDirectUnpackResult& result)
{
    ItemRuntime* rt = runtimeFor(result.itemId);
    if (!rt)
        return;

    auto it = rt->directUnpack.find(result.setKey);
    if (it == rt->directUnpack.end())
        return;

    it->result = result;
    if (it->running)
        --m_directUnpackRuns;
    it->running = false;
    if (result.ok)
        m_stats.bump(&UsenetCounters::directUnpacks);

    // The worker has returned from run(); its thread can go. Deleting it here
    // rather than in cancelDirectUnpack() keeps a finished run from holding a
    // thread for the rest of a long download.
    if (it->thread) {
        it->thread->quit();
        it->thread->wait();
        delete it->worker;
        delete it->thread;
        it->worker = nullptr;
        it->thread = nullptr;
    }

    checkItemCompletion(*rt);
}

void UsenetQueue::beginPostProcessing(ItemRuntime& rt)
{
    // The *other* volume. Staging renames the payload into the incoming
    // directory and falls back to a copy when that crosses a filesystem, so
    // publishing needs room for the whole release on a disk the download phase
    // never touched. Failing there would throw away a download that finished.
    //
    // Left where it is rather than failed: checkItemCompletion() runs again on
    // the next tick, so this is a wait, like every other floor in this module.
    const QString incoming = thePrefs.incomingDirForCategory(rt.item->category);
    if (!volumeHasRoom(incoming)) {
        if (rt.item->stalledReason.isEmpty()) {
            rt.item->stalledReason = tr("waiting for disk space to publish");
            logWarning(QStringLiteral("Usenet: \"%1\" is downloaded but \"%2\" has no "
                                      "room for it")
                           .arg(rt.item->name, incoming));
            emit itemChanged(rt.item->id);
        }
        return;
    }
    if (!rt.item->stalledReason.isEmpty()) {
        rt.item->stalledReason.clear();
        emit itemChanged(rt.item->id);
    }

    // From here the extracted files belong to post-processing: a repair discards
    // them, staging renames them into the incoming directory. A preview must
    // stop being served from them before either happens.
    for (DirectUnpackRun& run : rt.directUnpack)
        run.closing = true;

    if (!m_postProcessor) {
        // No pipeline at all: fall back to phase 3's behaviour, minus the part
        // of it that was wrong. A clean release is published; a short one is not.
        int missing = 0;
        for (const auto& st : rt.item->files)
            missing += st.missingSegments;

        if (missing > 0) {
            failItem(rt, tr("%n article(s) are missing and post-processing is unavailable",
                            nullptr, missing));
            return;
        }

        UsenetPostResult synthetic;
        synthetic.itemId = rt.item->id;
        synthetic.success = true;
        for (const auto& st : rt.item->files) {
            if (st.tempPath.isEmpty() || !QFile::exists(st.tempPath))
                continue;
            // fileName(), not completeBaseName(): sealFile() has already renamed
            // this to the release's real filename, extension and all, precisely
            // because everything downstream works by extension. Stripping it here
            // published "movie" for movie.mkv — no OS handler, and
            // getED2KFileTypeID() reads it as Any.
            const QString name = QFileInfo(st.tempPath).fileName();
            const QString finalPath = uniqueDestination(
                thePrefs.incomingDirForCategory(rt.item->category), name);
            const QString stagedPath = finalPath + QString(Preferences::kUsenetPartSuffix);

            // uniqueDestination() checked the final name; the rename target is the
            // staged one, so clear a leftover the way stageForPublish() does.
            QFile::remove(stagedPath);

            if (QFile::rename(st.tempPath, stagedPath))
                synthetic.staged.append({st.tempPath, stagedPath, finalPath});
        }
        onPostFinished(synthetic);
        return;
    }

    rt.postRunning = true;
    rt.postStage = PostStage::Idle;   // queued behind other jobs until it reports
    rt.postStageClock.start();
    rt.item->status = UsenetItemStatus::Verifying;
    rt.item->postPercent = 0;
    rt.item->postDetail.clear();
    rt.item->error.clear();
    persist(rt);
    emit itemChanged(rt.item->id);

    UsenetPostJob job;
    job.itemId = rt.item->id;
    job.workDir = QDir(thePrefs.usenetTempDir()).filePath(rt.item->id);
    // The index is stored on the item; the folder is asked for here, at the last
    // moment. A category repointed, renamed or deleted while the release was
    // downloading resolves now — and incomingDirForCategory() re-tests the
    // directory on every call, so an unmounted volume falls back rather than
    // stranding a finished release.
    job.destDir = thePrefs.incomingDirForCategory(rt.item->category);
    job.password = rt.item->nzb.password;
    job.externalUnpacker = thePrefs.usenetExternalUnpacker();
    job.par2Enabled = m_par2Enabled;
    job.renameEnabled = m_renameEnabled;
    job.unpackEnabled = m_unpackEnabled;
    job.cleanupEnabled = m_cleanupEnabled;

    for (const DirectUnpackRun& run : std::as_const(rt.directUnpack)) {
        if (run.result.ok)
            job.directUnpacked.append(run.result);
    }

    for (const auto& st : rt.item->files) {
        if (st.missingSegments > 0) {
            job.hasMissingSegments = true;
            break;
        }
    }

    QMetaObject::invokeMethod(m_postProcessor, "process", Qt::QueuedConnection,
                              Q_ARG(eMule::usenet::UsenetPostJob, job));
}

void UsenetQueue::onPostStage(const QString& itemId, int stage, int percent,
                              const QString& detail)
{
    ItemRuntime* rt = runtimeFor(itemId);
    if (!rt)
        return;

    if (PostStage(stage) != rt->postStage) {
        closePostStage(*rt);
        rt->postStage = PostStage(stage);
        rt->postStageClock.start();
    }

    switch (PostStage(stage)) {
    case PostStage::Repairing: rt->item->status = UsenetItemStatus::Repairing; break;
    case PostStage::Unpacking: rt->item->status = UsenetItemStatus::Unpacking; break;
    case PostStage::Verifying:
    case PostStage::Staging:
    case PostStage::Idle:      rt->item->status = UsenetItemStatus::Verifying; break;
    }

    rt->item->postPercent = percent;
    rt->item->postDetail = detail;
    emit itemChanged(itemId);
}

void UsenetQueue::onPostFinished(const UsenetPostResult& result)
{
    ItemRuntime* rt = runtimeFor(result.itemId);
    if (!rt)
        return;

    rt->postRunning = false;
    rt->item->postPercent = 0;
    rt->item->postDetail.clear();

    closePostStage(*rt);
    m_stats.notePostFinished(result);

    // Repair rewrites volumes, rename moves them and unpack publishes elsewhere.
    // Every cached path and parsed header in the streaming index is suspect.
    rt->streamIndex.invalidate();

    // -- the cycle: verification came up short, go and fetch the blocks ----
    if (result.needsMoreBlocks) {
        // A short verify only counts once it is final: normally it sends the
        // item back for recovery volumes and a later round is the verdict.
        const auto giveUp = [this, rt](const QString& message) {
            m_stats.bump(&UsenetCounters::par2Verified);
            m_stats.bump(&UsenetCounters::par2RepairFailed);
            failItem(*rt, message);
        };
        if (++rt->par2Rounds > kMaxPar2Rounds) {
            giveUp(tr("Repair still incomplete after %1 rounds").arg(kMaxPar2Rounds));
            return;
        }
        if (!requestPar2Volumes(*rt, result.blocksNeeded)) {
            giveUp(tr("Not enough recovery data: %n more block(s) needed",
                      nullptr, result.blocksNeeded));
            return;
        }

        logInfo(QStringLiteral("Usenet: \"%1\" needs %2 recovery block(s); fetching volumes")
                    .arg(rt->item->name).arg(result.blocksNeeded));

        rt->item->status = UsenetItemStatus::Downloading;
        rebuildPlan(*rt);
        persist(*rt);
        emit itemChanged(rt->item->id);
        dispatch();
        return;
    }

    if (!result.success) {
        // Recorded before failItem(), which persists: a restart must still know
        // this item is one password away from working, or the GUI's
        // "Set Password…" hint disappears across a daemon restart.
        rt->item->passwordRequired = result.passwordRequired;
        failItem(*rt, result.message);
        return;
    }

    rt->item->passwordRequired = false;

    // The volumes are about to be deleted and the work directory removed, so a
    // preview re-run over them has nothing left to read. Joined here rather than
    // left to the item's removal, which may be days away.
    cancelEncryptedPreview(*rt);

    publishStaged(*rt, result);

    for (const QString& path : result.consumed)
        QFile::remove(path);

    rt->item->status = UsenetItemStatus::Complete;
    rt->item->error.clear();
    rt->item->failedLadder.clear();   // it succeeded; there is nothing to retry against
    rt->refetch = {};
    // Only the terminal round reaches here — needsMoreBlocks and a failed verify
    // both return above — so this records exactly one completion per release.
    m_history.record(*rt->item, UsenetHistoryState::Downloaded);
    m_history.save();
    persist(*rt);

    emit itemChanged(rt->item->id);
    finishItem(*rt, true, tr("Download complete"));

    // Everything worth keeping has been moved out by now; what is left is
    // scratch, archive volumes and recovery data.
    QDir(QDir(thePrefs.usenetTempDir()).filePath(rt->item->id)).removeRecursively();
}

bool UsenetQueue::requestPar2Volumes(ItemRuntime& rt, int blocks)
{
    struct Candidate {
        int fileIndex;
        int blocks;
    };
    QList<Candidate> available;

    for (int f = 0; f < rt.item->nzb.files.size(); ++f) {
        if (rt.item->requestedPar2.contains(f))
            continue;
        const int supplied = rt.item->nzb.files.at(f).par2RecoveryBlocks();
        if (supplied > 0)
            available.append({f, supplied});
    }
    if (available.isEmpty())
        return false;

    // Smallest first, so covering a two-block shortfall costs a two-block volume
    // rather than the 64-block one that happens to come first in the NZB.
    std::sort(available.begin(), available.end(),
              [](const Candidate& a, const Candidate& b) { return a.blocks < b.blocks; });

    int covered = 0;
    for (const Candidate& c : available) {
        // addNzb() skipped these, so the scratch file has to appear now — the
        // worker writes into it at absolute offsets and will not create it.
        QString error;
        if (c.fileIndex >= rt.item->files.size()
            || !createTargetFile(rt.item->files.at(c.fileIndex).tempPath, error)) {
            logWarning(QStringLiteral("Usenet: %1").arg(error));
            continue;
        }

        rt.item->requestedPar2.insert(c.fileIndex);
        m_stats.bump(&UsenetCounters::recoveryVolumes);
        covered += c.blocks;
        if (covered >= blocks)
            break;
    }

    // Even taking everything left may not be enough. Say so now rather than
    // downloading the whole recovery set and failing afterwards.
    return covered > 0;
}

void UsenetQueue::publishStaged(ItemRuntime& rt, const UsenetPostResult& result)
{
    QStringList published;

    for (const auto& [source, stagedPath, finalPath] : result.staged) {
        // The staged file already sits in the incoming directory carrying the
        // .usenetpart suffix, which the share scan skips by name. This rename is
        // in-place and therefore atomic: there is no instant at which a
        // half-written file is visible under a shareable name.
        if (!QFile::rename(stagedPath, finalPath)) {
            logError(QStringLiteral("Usenet: cannot name \"%1\"")
                         .arg(QFileInfo(finalPath).fileName()));
            QFile::remove(stagedPath);
            continue;
        }

        // addFileInSharedLocation(), not addSingleSharedFile(): the latter is for
        // a file no shared directory covers, and it refuses the incoming
        // directory outright because isShareableDirectory() excludes it. Using
        // the wrong one logs a warning and shares nothing until a full rescan.
        if (theApp.sharedFileList && !theApp.sharedFileList->addFileInSharedLocation(finalPath)) {
            logWarning(QStringLiteral("Usenet: \"%1\" completed but is not in a shared "
                                      "location; it will not be offered to peers")
                           .arg(QFileInfo(finalPath).fileName()));
        }

        logInfo(QStringLiteral("Usenet: completed \"%1\"")
                    .arg(QFileInfo(finalPath).fileName()));

        published.append(finalPath);

        // The per-file record is what the GUI shows; point it at where the file
        // actually ended up rather than the scratch path it no longer occupies.
        //
        // Matched by source path, never by position: the payload list is sorted
        // by name and drops every .par2, so the two lists differ in order and in
        // length as soon as a release ships a recovery set. Only a file staged
        // straight out of the NZB has an NZB file to belong to at all — an
        // extracted member deliberately leaves every finalPath empty.
        for (auto& st : rt.item->files) {
            if (!st.tempPath.isEmpty() && st.tempPath == source) {
                st.finalPath = finalPath;
                break;
            }
        }
    }

    // What the release actually published, which `files` cannot answer for an
    // unpacked one. This is what "Open File" opens.
    rt.item->publishedPaths = published;
}

void UsenetQueue::failItem(ItemRuntime& rt, const QString& message)
{
    rt.postRunning = false;
    // No more volumes are coming, so a run would hold its thread until shutdown.
    cancelDirectUnpack(rt);
    cancelEncryptedPreview(rt);
    rt.item->status = UsenetItemStatus::Failed;

    // What the retry achieved, said once, here — otherwise a second failure is
    // word for word the first one and the button reads as broken. The counters
    // are cleared with it: they describe the attempt that just ended.
    rt.item->error = rt.refetch.armed > 0
        ? (rt.refetch.landed > 0
               ? tr("%1; %2 of %3 re-fetched article(s) came back")
                     .arg(message).arg(rt.refetch.landed).arg(rt.refetch.armed)
               : tr("%1; none of the %2 missing article(s) came back")
                     .arg(message).arg(rt.refetch.armed))
        : message;
    rt.refetch = {};

    // Which accounts could not supply it. Resume re-asks only once this differs
    // from what is configured then, so the same button pressed twice costs
    // nothing and the same button after adding a fill account costs a retry.
    rt.item->failedLadder = serverLadderDigest();
    persist(rt);

    logWarning(QStringLiteral("Usenet: \"%1\" failed: %2")
                   .arg(rt.item->name, rt.item->error));

    emit itemChanged(rt.item->id);
    finishItem(rt, false, rt.item->error);
}

void UsenetQueue::persist(ItemRuntime& rt)
{
    rt.dirty = false;
    UsenetQueueStore::save(*rt.item);
}

UsenetQueue::ItemRuntime* UsenetQueue::runtimeFor(const QString& id)
{
    for (auto& rt : m_items) {
        if (rt->item->id == id)
            return rt.get();
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Private — the failover ladder
// ---------------------------------------------------------------------------

const NewsServer* UsenetQueue::serverFor(const QString& serverKey) const
{
    if (serverKey.isEmpty())
        return nullptr;
    for (const NewsServer& s : m_servers) {
        if (s.key() == serverKey)
            return &s;
    }
    return nullptr;
}

bool UsenetQueue::retentionCovers(const NewsServer& s, qint64 date)
{
    // Both unknowns mean "no opinion", and an article that claims to be from the
    // future is a broken NZB rather than evidence about the server.
    if (s.retention <= 0 || date <= 0)
        return true;

    const qint64 ageDays = (QDateTime::currentSecsSinceEpoch() - date) / 86400;
    if (ageDays < 0)
        return true;
    return ageDays <= s.retention;
}

int UsenetQueue::lowestUntriedRung(const QStringList& tried, qint64 date, int floorRung,
                                   bool respectRetention, bool respectQuota) const
{
    int best = -1;
    for (const NewsServer& s : m_servers) {
        if (!s.enabled || !s.isValid())
            continue;
        if (tried.contains(s.key()))
            continue;
        if (respectRetention && !retentionCovers(s, date))
            continue;
        if (respectQuota && m_overQuota.contains(s.key()))
            continue;

        const auto rung = m_ladder.indexOf(s.level);
        if (rung < 0 || int(rung) < floorRung)
            continue;
        if (best < 0 || int(rung) < best)
            best = int(rung);
    }
    return best;
}

int UsenetQueue::nextServableLevel(const QStringList& tried, qint64 date, bool& relaxed) const
{
    relaxed = false;
    if (m_ladder.isEmpty())
        return -1;

    // respectQuota is false at both call sites of this function, and that is the
    // whole safety property: this answer is the *exhaustion verdict*, and an
    // allowance is a spending decision, never evidence about where an article
    // lives.
    const int strict = lowestUntriedRung(tried, date, 0, /*retention*/ true, /*quota*/ false);
    if (strict >= 0)
        return strict;

    // Nothing anywhere claims to be old enough. Retention is a figure the user
    // typed off a pricing page, so believing it here would turn a fetchable
    // article into a missing one — the expensive error. Ask anyway, starting
    // from the bottom: the last account tried before giving up is the one
    // retention had skipped.
    const int fallback = lowestUntriedRung(tried, date, 0, false, false);
    relaxed = fallback >= 0;
    return fallback;
}

int UsenetQueue::nextAffordableLevel(const QStringList& tried, qint64 date, int floorRung,
                                     bool& relaxed) const
{
    if (floorRung < 0 || floorRung >= m_ladder.size())
        return -1;

    const auto affordableAt = [&](int rung, bool respectRetention) {
        const int userLevel = m_ladder.at(rung);
        for (const NewsServer& s : m_servers) {
            if (!s.enabled || !s.isValid() || s.level != userLevel)
                continue;
            if (tried.contains(s.key()) || m_overQuota.contains(s.key()))
                continue;
            if (respectRetention && !retentionCovers(s, date))
                continue;
            return true;
        }
        return false;
    };

    if (affordableAt(floorRung, true))
        return floorRung;

    // Same relaxation the ladder makes, one level down and for the same reason:
    // without it a retention figure and an allowance could empty a rung between
    // them, and the download would stop dead with nothing to say.
    if (affordableAt(floorRung, false)) {
        relaxed = true;
        return floorRung;
    }

    // Nothing on this rung can be paid for. Whether to spend the next rung's
    // money instead is the user's call, not ours — block credit is normally
    // dearer per GB than the plan it would be covering, so the default is to
    // wait.
    const int userLevel = m_ladder.at(floorRung);
    bool mayFallThrough = false;
    for (const NewsServer& s : m_servers) {
        if (!s.enabled || !s.isValid() || s.level != userLevel)
            continue;
        if (tried.contains(s.key()) || !m_overQuota.contains(s.key()))
            continue;
        if (s.quotaFallThrough) {
            mayFallThrough = true;
            break;
        }
    }
    if (!mayFallThrough)
        return -1;

    const int strictUp = lowestUntriedRung(tried, date, floorRung + 1, true, true);
    if (strictUp >= 0)
        return strictUp;

    const int relaxedUp = lowestUntriedRung(tried, date, floorRung + 1, false, true);
    if (relaxedUp >= 0)
        relaxed = true;
    return relaxedUp;
}

QStringList UsenetQueue::dispatchExclusions(int rung, qint64 date, bool applyRetention) const
{
    QStringList out;
    if (rung < 0 || rung >= m_ladder.size())
        return out;

    const int userLevel = m_ladder.at(rung);
    for (const NewsServer& s : m_servers) {
        if (!s.enabled || !s.isValid() || s.level != userLevel)
            continue;
        if (m_overQuota.contains(s.key()) || (applyRetention && !retentionCovers(s, date)))
            out.append(s.key());
    }
    return out;
}

bool UsenetQueue::workerCanServe(int w, int rung, const QStringList& ignore) const
{
    if (w < 0 || w >= m_workerServers.size())
        return true;   // no slice information: fall back to letting it try
    if (rung < 0 || rung >= m_ladder.size())
        return false;

    const QSet<QString>& held = m_workerServers.at(w);
    const int userLevel = m_ladder.at(rung);
    for (const NewsServer& s : m_servers) {
        if (!s.enabled || !s.isValid() || s.level != userLevel)
            continue;
        if (held.contains(s.key()) && !ignore.contains(s.key()))
            return true;
    }
    return false;
}

bool UsenetQueue::quotaBlockedCandidateExists(const QStringList& tried) const
{
    for (const NewsServer& s : m_servers) {
        if (!s.enabled || !s.isValid())
            continue;
        if (tried.contains(s.key()))
            continue;
        if (m_overQuota.contains(s.key()))
            return true;
    }
    return false;
}

void UsenetQueue::refreshQuotaState()
{
    m_overQuota.clear();
    if (m_servers.isEmpty())
        return;

    // Grouped accounts share one meter. `group` already means "one provider
    // reached through two host names", so the plan behind them is one plan, and
    // billing each row separately would simply be a wrong number. The allowance
    // is the smallest one configured in the bucket — the same "smallest member
    // wins" rule the connection limit uses, and chosen the same way: too high
    // gets an account suspended.
    const QHash<QString, NntpConnectionBucket> buckets = nntpConnectionBuckets(m_servers);

    QHash<QString, qint64> spentByBucket;
    QHash<QString, qint64> allowanceByBucket;

    for (const NewsServer& s : m_servers) {
        if (!s.enabled || !s.isValid())
            continue;
        const QString bucket = buckets.value(s.key()).id;
        if (bucket.isEmpty())
            continue;

        spentByBucket[bucket] += m_usage.periodBytes(s.accountId);
        if (s.isMetered()) {
            const auto it = allowanceByBucket.constFind(bucket);
            if (it == allowanceByBucket.constEnd() || s.quotaBytes < *it)
                allowanceByBucket[bucket] = s.quotaBytes;
        }
    }

    QSet<QString> warned;
    for (const NewsServer& s : m_servers) {
        if (!s.enabled || !s.isValid())
            continue;
        const QString bucket = buckets.value(s.key()).id;
        const auto allowance = allowanceByBucket.constFind(bucket);
        if (allowance == allowanceByBucket.constEnd())
            continue;   // nothing in this bucket is metered

        const qint64 spent = spentByBucket.value(bucket);
        if (spent >= *allowance) {
            m_overQuota.insert(s.key());
            continue;
        }

        // Warn before it bites, once per bucket per period. A download that
        // stops at 3 a.m. is a surprise; a line in the log the day before is
        // the whole difference, and it is worth more than shaving the last
        // 32 MB off the overshoot.
        if (spent * 10 >= *allowance * 9 && !m_quotaWarned.contains(bucket)) {
            warned.insert(bucket);
            logWarning(QStringLiteral("Usenet: %1 has used %2% of its allowance")
                           .arg(s.displayName())
                           .arg(*allowance > 0 ? spent * 100 / *allowance : 0));
        }
    }

    // Recomputed, not accumulated: a rollover or a correction drops the bucket
    // back under 90% and the warning is armed again for the new period.
    for (auto it = m_quotaWarned.begin(); it != m_quotaWarned.end();) {
        const qint64 spent = spentByBucket.value(*it);
        const auto allowance = allowanceByBucket.constFind(*it);
        if (allowance == allowanceByBucket.constEnd() || spent * 10 < *allowance * 9)
            it = m_quotaWarned.erase(it);
        else
            ++it;
    }
    m_quotaWarned.unite(warned);
}

void UsenetQueue::noteQuotaStall(ItemRuntime& rt, const QStringList& tried, qint64 date)
{
    rt.quotaParked = true;

    // When the item comes back is the only useful half of the sentence, so work
    // it out from the accounts that are actually blocking *this* article. A
    // block account has no answer — it needs a top-up, not a wait.
    QDate soonest;
    bool blockOnly = true;
    const QDate today = QDate::currentDate();

    for (const NewsServer& s : m_servers) {
        if (!s.enabled || !s.isValid() || tried.contains(s.key()))
            continue;
        if (!m_overQuota.contains(s.key()) || !retentionCovers(s, date))
            continue;
        if (s.quotaKind != NntpQuotaKind::Monthly)
            continue;

        blockOnly = false;
        const QDate next = nntpQuotaNextReset(s.quotaResetDay, today);
        if (next.isValid() && (!soonest.isValid() || next < soonest))
            soonest = next;
    }

    rt.item->stalledReason = (!blockOnly && soonest.isValid())
        ? tr("allowance spent, resumes %1").arg(soonest.toString(Qt::ISODate))
        : tr("allowance spent — add credit or raise the limit");

    // The stall is re-evaluated four times a second; unguarded this line would
    // fill log.log inside an hour.
    if (!m_quotaStallLogged) {
        m_quotaStallLogged = true;
        logWarning(QStringLiteral("Usenet: %1 is waiting — %2")
                       .arg(rt.item->name, rt.item->stalledReason));
    }

    emit itemChanged(rt.item->id);
}

void UsenetQueue::unparkQuotaStalls()
{
    m_quotaStallLogged = false;
    for (auto& rt : m_items) {
        if (!rt->quotaParked)
            continue;
        rt->quotaParked = false;
        rt->item->stalledReason.clear();
        emit itemChanged(rt->item->id);
    }
}

void UsenetQueue::markSegmentMissing(ItemRuntime& rt, int fileIndex, int segmentIndex,
                                     const QString& messageId, const QString& reason)
{
    if (fileIndex < 0 || fileIndex >= rt.item->files.size())
        return;

    UsenetFileState& st = rt.item->files[fileIndex];
    if (segmentIndex >= 0 && segmentIndex < st.done.size()) {
        // Guarded on the *missing* bit, not the done bit: a retry clears the
        // done bit of an article already counted here, and guarding on that one
        // would count the same hole — and bump articlesMissing — a second time
        // when the retry fails the same way.
        if (st.missing.size() > segmentIndex && st.missing.testBit(segmentIndex)) {
            st.done.setBit(segmentIndex);  // re-armed, and still nowhere
            rt.dirty = true;
            noteRefetchResolved(rt, fileIndex, segmentIndex, /*landed*/ false);
            return;
        }
        if (st.done.testBit(segmentIndex))
            return;                       // already resolved; do not double-count
        st.done.setBit(segmentIndex);     // resolved, not arrived — stop asking
        if (st.missing.size() < st.done.size())
            st.missing.resize(st.done.size());
        st.missing.setBit(segmentIndex);
    }
    st.missingSegments += 1;
    m_stats.bump(&UsenetCounters::articlesMissing);

    rt.attempts.remove(SegmentKey{fileIndex, segmentIndex}.packed());
    rt.dirty = true;

    logWarning(QStringLiteral("Usenet: article %1 unavailable — %2")
                   .arg(messageId, reason));
}

// ---------------------------------------------------------------------------
// Availability probe
//
// "Does anybody still have this?", asked with STAT before a single article is
// paid for. The rule it lives under, and the reason it can only ever pause an
// item rather than fail one, is in docs/usenet-module.md beside its two
// siblings: retention falls back to asking anyway, an allowance falls back to
// waiting, and a health check falls back to downloading anyway.
//
// The ladder functions are reused verbatim — nextServableLevel(),
// lowestUntriedRung(), nextAffordableLevel(), dispatchExclusions(),
// workerCanServe(). That reuse *is* the correctness argument: a 430 appends to
// `tried` and the rung is re-derived, so "unavailable" can only ever mean every
// rung refused it. Deriving it any other way would be writing the ladder twice.
// ---------------------------------------------------------------------------

bool UsenetQueue::beginHealthCheck(ItemRuntime& rt)
{
    clearHealthCheck(rt);

    const auto mode = usenetHealthCheckFromInt(thePrefs.usenetHealthCheck());
    if (mode == UsenetHealthCheck::Off)
        return false;

    // Every one of these means "no verdict", never "do not download". A probe
    // that cannot run must cost nothing, not even a tick.
    if (!m_running || m_workers.isEmpty() || m_ladder.isEmpty())
        return false;

    const UsenetQueueItem& item = *rt.item;
    for (int f = 0; f < item.nzb.files.size(); ++f) {
        const NzbFileInfo& info = item.nzb.files.at(f);
        if (info.segments.isEmpty())
            continue;

        if (mode == UsenetHealthCheck::Full) {
            for (int seg = 0; seg < info.segments.size(); ++seg)
                rt.checkPlan.append(SegmentKey{f, seg}.packed());
            continue;
        }

        // One article stands for its file. Providers expire by post date and
        // every article of one posted file carries that date, so a file is
        // overwhelmingly present or absent as a unit.
        rt.checkPlan.append(SegmentKey{f, 0}.packed());
    }

    if (mode == UsenetHealthCheck::Sample && rt.checkPlan.size() > kMaxSampleProbes)
        rt.checkPlan.resize(kMaxSampleProbes);

    if (rt.checkPlan.isEmpty())
        return false;

    rt.checkResumeStatus = rt.item->status;
    rt.item->status = UsenetItemStatus::Checking;
    rt.item->stalledReason = tr("checking availability");
    m_stats.bump(&UsenetCounters::healthChecks);
    emit itemChanged(rt.item->id);
    return true;
}

void UsenetQueue::dispatchProbes()
{
    for (int w = 0; w < m_workers.size(); ++w) {
        if (w >= m_workerCapacity.size() || w >= m_workerInFlight.size())
            break;

        const int reserved = std::max(1, m_workerCapacity.at(w) / kProbeCapacityDivisor);
        int spent = 0;

        while (spent < reserved && m_workerInFlight.at(w) < m_workerCapacity.at(w)) {
            bool dispatched = false;

            for (auto& owner : m_items) {
                ItemRuntime* rt = owner.get();
                if (rt->item->status != UsenetItemStatus::Checking)
                    continue;

                while (rt->checkCursor < rt->checkPlan.size()) {
                    const quint64 key = rt->checkPlan.at(rt->checkCursor);
                    const int fileIndex = int(key >> 32);
                    const int segIndex = int(key & 0xFFFFFFFFu);

                    if (rt->checkInFlight.contains(key)
                        || fileIndex >= rt->item->nzb.files.size()) {
                        ++rt->checkCursor;
                        continue;
                    }

                    const NzbFileInfo& info = rt->item->nzb.files.at(fileIndex);
                    if (segIndex >= info.segments.size()) {
                        ++rt->checkCursor;
                        continue;
                    }

                    const SegmentAttempt attempt = rt->checkAttempts.value(key);

                    bool relaxed = false;
                    const int level = nextServableLevel(attempt.tried, info.date, relaxed);
                    if (level < 0) {
                        // Every account has now refused it. That is the only
                        // thing "unavailable" is ever allowed to mean.
                        const qint64 weight = probeWeight(*rt, fileIndex, segIndex);
                        rt->checkProbedBytes += weight;
                        rt->checkMissingBytes += weight;
                        rt->checkMissingFiles.insert(fileIndex);
                        ++rt->checkCursor;
                        continue;
                    }

                    // An allowance is not worth spending on advice. Skipping
                    // leaves the article unprobed — no verdict about it, which is
                    // the honest answer and costs nothing.
                    const int payLevel =
                        nextAffordableLevel(attempt.tried, info.date, level, relaxed);
                    if (payLevel < 0) {
                        ++rt->checkCursor;
                        continue;
                    }

                    QStringList ignore = attempt.tried;
                    ignore += dispatchExclusions(payLevel, info.date, !relaxed);

                    if (w < m_workerServers.size() && !workerCanServe(w, payLevel, ignore))
                        break;   // another worker holds this rung

                    UsenetFetchRequest req;
                    req.itemId = rt->item->id;
                    req.fileIndex = fileIndex;
                    req.segmentIndex = segIndex;
                    req.segment = info.segments.at(segIndex);
                    req.group = info.groups.isEmpty() ? QString() : info.groups.first();
                    req.level = payLevel;
                    req.ignoreServers = std::move(ignore);
                    req.probeOnly = true;
                    // targetPath deliberately left empty: nothing is written, and
                    // the worker skips the mkpath and the open entirely.

                    rt->checkInFlight.insert(key);
                    ++rt->checkCursor;
                    m_workerInFlight[w] += 1;
                    ++spent;
                    dispatched = true;

                    QMetaObject::invokeMethod(
                        m_workers.at(w), "fetchSegment", Qt::QueuedConnection,
                        Q_ARG(eMule::usenet::UsenetFetchRequest, req));
                    break;
                }

                if (dispatched)
                    break;
            }

            if (!dispatched)
                break;
        }
    }

    // Resolved after the scan, never inside it: finishHealthCheck() changes the
    // status of an item the loop above is still walking.
    QList<ItemRuntime*> done;
    for (auto& owner : m_items) {
        ItemRuntime* rt = owner.get();
        if (rt->item->status == UsenetItemStatus::Checking
            && rt->checkInFlight.isEmpty()
            && rt->checkCursor >= rt->checkPlan.size()) {
            done.append(rt);
        }
    }
    for (ItemRuntime* rt : std::as_const(done))
        finishHealthCheck(*rt);
}

void UsenetQueue::handleProbeResult(ItemRuntime& rt, const UsenetFetchResult& result)
{
    const quint64 key = SegmentKey{result.fileIndex, result.segmentIndex}.packed();
    rt.checkInFlight.remove(key);

    if (rt.item->status != UsenetItemStatus::Checking)
        return;   // recheck superseded, or the item moved on

    const qint64 weight = probeWeight(rt, result.fileIndex, result.segmentIndex);

    if (result.error == NntpError::None) {
        rt.checkProbedBytes += weight;
        rt.checkAttempts.remove(key);
        return;
    }

    if (result.noServerAvailable) {
        // Nothing leasable. Not an answer about the article — put it back and
        // let the next round ask.
        rt.checkPlan.insert(qBound(0, rt.checkCursor, int(rt.checkPlan.size())), key);
        m_starved = true;
        return;
    }

    SegmentAttempt attempt = rt.checkAttempts.value(key);

    if (escalatesToNextLevel(result.error)) {
        if (!result.serverKey.isEmpty() && !attempt.tried.contains(result.serverKey))
            attempt.tried.append(result.serverKey);

        const NzbFileInfo* info = result.fileIndex >= 0
                                          && result.fileIndex < rt.item->nzb.files.size()
                                      ? &rt.item->nzb.files.at(result.fileIndex)
                                      : nullptr;
        bool relaxed = false;
        if (info && nextServableLevel(attempt.tried, info->date, relaxed) >= 0) {
            // A sibling or a higher rung is still untried. One account saying no
            // is what the ladder exists to survive, and calling it "unavailable"
            // here would report 0% on releases that download perfectly.
            rt.checkAttempts.insert(key, attempt);
            rt.checkPlan.insert(qBound(0, rt.checkCursor, int(rt.checkPlan.size())), key);
            return;
        }

        rt.checkProbedBytes += weight;
        rt.checkMissingBytes += weight;
        rt.checkMissingFiles.insert(result.fileIndex);
        rt.checkAttempts.remove(key);
        return;
    }

    // A transport fault says nothing about the article — the connection broke,
    // the server was busy, the credential was wrong. Retry, and on giving up
    // record **no opinion** rather than a shortfall: a provider having a bad
    // minute must not read as a dead release.
    attempt.transportRetries += 1;
    if (attempt.transportRetries > kMaxTransportRetries) {
        rt.checkAttempts.remove(key);
        return;
    }
    rt.checkAttempts.insert(key, attempt);
    rt.checkPlan.insert(qBound(0, rt.checkCursor, int(rt.checkPlan.size())), key);
}

void UsenetQueue::finishHealthCheck(ItemRuntime& rt)
{
    UsenetQueueItem& item = *rt.item;

    // Both halves, combined into the one number a user can act on: articles the
    // NZB never listed, and articles no account still holds.
    const NzbShortfall nzbShort = item.nzb.shortfall();

    UsenetHealthVerdict verdict;
    verdict.probed = rt.checkProbedBytes > 0;
    verdict.missingBytes = nzbShort.missingBytes + rt.checkMissingBytes;

    // Recovery data the probe did not find missing. A volume the servers no
    // longer hold repairs nothing, so counting it would make a dead release look
    // rescuable.
    for (int f = 0; f < item.nzb.files.size(); ++f) {
        const NzbFileInfo& info = item.nzb.files.at(f);
        if (info.isPar2Volume() && !rt.checkMissingFiles.contains(f))
            verdict.recoveryBytes += info.encodedBytes();
    }

    const qint64 claimed = item.nzb.totalEncodedBytes() + nzbShort.missingBytes;
    if (claimed > 0) {
        const qint64 obtainable = std::max<qint64>(0, claimed - verdict.missingBytes);
        verdict.percent = int(obtainable * 100 / claimed);
    } else {
        verdict.percent = 100;
    }

    item.healthPercent = verdict.percent;
    item.healthMissingBytes = verdict.missingBytes;
    item.healthRecoveryBytes = verdict.recoveryBytes;
    item.healthProbed = verdict.probed;

    const UsenetItemStatus resumeTo = rt.checkResumeStatus;
    clearHealthCheck(rt);

    const int threshold = thePrefs.usenetHealthMinPercent();
    const bool short_ = threshold > 0 && verdict.percent < threshold
                        && !verdict.likelyRecoverable();

    m_stats.bump(short_           ? &UsenetCounters::healthPaused
                 : verdict.probed ? &UsenetCounters::healthPassed
                                  : &UsenetCounters::healthInconclusive);

    if (short_) {
        // Paused, never failed and never refused: the figure is a guess about
        // articles nobody can ask a second question about, and the user is the
        // only actor allowed to act on it. Resuming downloads the release
        // exactly as if this had never run.
        item.status = UsenetItemStatus::Paused;
        item.stalledReason = tr("only %1% of this release looks available")
                                 .arg(verdict.percent);
        logWarning(QStringLiteral("Usenet: \"%1\" paused before downloading — "
                                  "%2% available, %3 short, %4 recovery")
                       .arg(item.name)
                       .arg(verdict.percent)
                       .arg(verdict.missingBytes)
                       .arg(verdict.recoveryBytes));
    } else {
        // Back to where it was, not unconditionally to Queued: a recheck of a
        // paused item must leave it paused. Asking a question about something is
        // not a decision to start it.
        item.status = resumeTo;
        item.stalledReason.clear();
        if (verdict.probed) {
            logInfo(QStringLiteral("Usenet: \"%1\" checked out at %2%")
                        .arg(item.name)
                        .arg(verdict.percent));
        }
    }

    rt.dirty = true;
    persist(rt);
    emit itemChanged(item.id);
}

void UsenetQueue::clearHealthCheck(ItemRuntime& rt) const
{
    rt.checkPlan.clear();
    rt.checkCursor = 0;
    rt.checkInFlight.clear();
    rt.checkAttempts.clear();
    rt.checkProbedBytes = 0;
    rt.checkMissingBytes = 0;
    rt.checkMissingFiles.clear();
}

qint64 UsenetQueue::probeWeight(const ItemRuntime& rt, int fileIndex, int segIndex) const
{
    if (fileIndex < 0 || fileIndex >= rt.item->nzb.files.size())
        return 0;
    const NzbFileInfo& info = rt.item->nzb.files.at(fileIndex);

    // In Full mode every article is asked about, so each stands for itself. In
    // Sample mode one article stands for its whole file, and weighting it as one
    // article would make a dead 4 GB volume look like a rounding error.
    if (usenetHealthCheckFromInt(thePrefs.usenetHealthCheck()) == UsenetHealthCheck::Full) {
        return segIndex >= 0 && segIndex < info.segments.size()
                   ? info.segments.at(segIndex).bytes
                   : 0;
    }
    return info.encodedBytes();
}

UsenetAddOutcome UsenetQueue::findDuplicate(const NzbInfo& nzb, const QString& name,
                                            bool force, QString& why) const
{
    const QString digest = nzbArticleDigest(nzb);
    if (digest.isEmpty())
        return UsenetAddOutcome::Added;   // nothing to compare on; never a refusal

    const QString key = nzbReleaseKey(name, nzb.totalEncodedBytes());
    const UsenetQueueItem* softMatch = nullptr;
    const UsenetQueueItem* inFlight = nullptr;
    const UsenetQueueItem* finished = nullptr;

    // The live queue, always — force does not reach this loop, which is what
    // guarantees a download in flight can never be started a second time.
    //
    // The whole list, not the first hit: a forced re-download leaves the finished
    // item listed *and* adds a second one, so both exist with the same digest and
    // whichever came first would decide the answer. In flight always wins — it is
    // the stronger claim and the only one nothing can override.
    for (const auto& rt : m_items) {
        const UsenetQueueItem* item = rt->item.get();

        if (item->articleDigest != digest) {
            // A repost carries fresh message-ids, so only the soft key can see
            // it. Remembered and reported at the end, never acted on: the key is
            // a folded name plus a size, and two releases can honestly share both.
            if (softMatch == nullptr && !key.isEmpty() && item->releaseKey == key)
                softMatch = item;
            continue;
        }

        // Literally the same articles from literally the same servers.
        if (item->status == UsenetItemStatus::Complete) {
            if (finished == nullptr)
                finished = item;
        } else {
            inFlight = item;
            break;
        }
    }

    // No em dash in either sentence: AddNzbUrlDialog splits its failure lines
    // on " — " to recover the URL.
    if (inFlight != nullptr) {
        why = tr("\"%1\" is already in the queue.").arg(inFlight->name);
        return UsenetAddOutcome::Duplicate;
    }
    if (finished != nullptr && !force) {
        why = tr("\"%1\" has already been downloaded.").arg(finished->name);
        return UsenetAddOutcome::AlreadyDownloaded;
    }
    if (finished != nullptr)
        return UsenetAddOutcome::Added;   // asked, and answered yes

    if (softMatch != nullptr) {
        logInfo(QStringLiteral("Usenet: \"%1\" looks like a repost of \"%2\" "
                               "(same name and size, different articles) — adding it anyway")
                    .arg(name, softMatch->name));
    }

    // Then what has left the queue. Reached only when the caller has not already
    // answered the question — the structural half of "force overrides the history
    // and nothing else".
    if (!force) {
        m_history.load();
        if (const UsenetHistoryEntry* past = m_history.findByDigest(digest)) {
            why = past->state == UsenetHistoryState::Cancelled
                      ? tr("You previously cancelled the download of \"%1\".").arg(past->name)
                      : tr("You already downloaded \"%1\".").arg(past->name);
            return UsenetAddOutcome::AlreadyDownloaded;
        }
    }

    return UsenetAddOutcome::Added;
}

int UsenetQueue::knownTypeForTitle(const QString& title) const
{
    const QString folded = usenetFoldedReleaseName(title);
    if (folded.isEmpty())
        return 0;

    for (const auto& rt : m_items) {
        if (usenetFoldedReleaseName(rt->item->name) != folded)
            continue;
        if (rt->item->status != UsenetItemStatus::Complete)
            return 2;   // Downloading — in the queue right now
    }

    m_history.load();
    if (const UsenetHistoryEntry* past = m_history.findByName(title))
        return past->state == UsenetHistoryState::Cancelled ? 4 : 3;

    return 0;
}

// ---------------------------------------------------------------------------
// Names from the PAR2 set
// ---------------------------------------------------------------------------

bool UsenetQueue::looksObfuscated(const UsenetQueueItem& item)
{
    bool sawPayload = false;
    bool sawIndex = false;

    for (int f = 0; f < item.nzb.files.size(); ++f) {
        const NzbFileInfo& info = item.nzb.files.at(f);
        if (info.isPar2()) {
            sawIndex = sawIndex || !info.isPar2Volume();
            continue;
        }

        sawPayload = true;
        const QString name = item.bestFileName(f);
        if (name.isEmpty())
            continue;   // no name yet is as obfuscated as a junk one

        if (UsenetUnpacker::volumePositionOf(name).index >= 0)
            return false;

        const ED2KFileType type = getED2KFileTypeID(name);
        if (type == ED2KFileType::Video || type == ED2KFileType::Audio)
            return false;
    }

    // Nothing to hoist without an index .par2 to hoist -- and a release with no
    // payload is not a release.
    return sawPayload && sawIndex;
}

void UsenetQueue::learnPar2Names(ItemRuntime& rt)
{
    if (rt.par2NamesState != ItemRuntime::Par2NamesState::Unread)
        return;

    // The same switch that governs post-processing's rename pass: one setting
    // for one behaviour, rather than a second one nobody would think to look for.
    if (!m_par2Enabled || !m_renameEnabled || !Par2Verifier::available()) {
        rt.par2NamesState = ItemRuntime::Par2NamesState::Unavailable;
        return;
    }

    QString indexPath;
    for (int i = 0; i < rt.item->nzb.files.size() && i < rt.item->files.size(); ++i) {
        const NzbFileInfo& info = rt.item->nzb.files.at(i);
        if (!info.isPar2() || info.isPar2Volume())
            continue;

        const UsenetFileState& st = rt.item->files.at(i);
        if (!st.finalized || st.tempPath.isEmpty() || !QFileInfo::exists(st.tempPath))
            continue;

        indexPath = st.tempPath;
        break;
    }

    // Left Unread on purpose: the index may simply not have landed yet, and the
    // next sealed file asks again. Only a *failed read* is final.
    if (indexPath.isEmpty())
        return;

    if (QFileInfo(indexPath).size() > kMaxPar2IndexBytes) {
        logInfo(QStringLiteral("Usenet: PAR2 index for \"%1\" is too large to read for names")
                    .arg(rt.item->name));
        rt.par2NamesState = ItemRuntime::Par2NamesState::Unavailable;
        return;
    }

    QElapsedTimer clock;
    clock.start();

    Par2Verifier verifier;
    const Par2FileList list =
        verifier.listFiles(indexPath, QFileInfo(indexPath).absolutePath());

    if (!list.ok()) {
        rt.par2NamesState = ItemRuntime::Par2NamesState::Unavailable;
        return;
    }

    rt.par2Names.setFiles(list.files);
    rt.par2NamesState = ItemRuntime::Par2NamesState::Loaded;

    // A file that already sealed owns the name it sealed under, so nothing else
    // may be given it.
    for (const UsenetFileState& st : rt.item->files) {
        if (st.finalized && !st.tempPath.isEmpty())
            rt.par2Names.claim(QFileInfo(st.tempPath).fileName());
    }

    logInfo(QStringLiteral("Usenet: PAR2 set names %1 file(s) of \"%2\" (%3 ms)")
                .arg(rt.par2Names.size())
                .arg(rt.item->name)
                .arg(clock.elapsed()));

    // Whatever has already arrived far enough can be named right now, in file
    // order so the answer does not depend on what landed first.
    for (int i = 0; i < rt.item->files.size(); ++i)
        resolvePar2Name(rt, i);
}

void UsenetQueue::resolvePar2Name(ItemRuntime& rt, int fileIndex)
{
    if (rt.par2NamesState != ItemRuntime::Par2NamesState::Loaded)
        return;
    if (fileIndex < 0 || fileIndex >= rt.item->files.size()
        || fileIndex >= rt.item->nzb.files.size()) {
        return;
    }
    if (rt.item->nzb.files.at(fileIndex).isPar2())
        return;

    UsenetFileState& st = rt.item->files[fileIndex];
    if (st.finalized || !st.par2FileName.isEmpty() || st.declaredSize <= 0)
        return;

    // availableFrom(0), never QFileInfo::size(): the scratch file is written at
    // absolute offsets, so its length on disk is the highest byte written and
    // says nothing about whether the opening 16 KiB are real. A file whose first
    // article was missing everywhere never satisfies this, and correctly falls
    // through to post-processing's sliding-window rename.
    if (st.availableFrom(0) < Par2NameIndex::bytesNeededFor(st.declaredSize))
        return;

    const QString matched = rt.par2Names.matchFile(st.tempPath, st.declaredSize);
    if (matched.isEmpty())
        return;

    // PAR2 stores paths, and this one came off Usenet.
    const QString safe = sanitizeName(QFileInfo(matched).fileName());
    if (safe.isEmpty() || !rt.par2Names.claim(safe))
        return;

    st.par2FileName = safe;
    rt.dirty = true;

    // The set cache is keyed by base name, and this changes it.
    rt.streamIndex.invalidate();

    logInfo(QStringLiteral("Usenet: PAR2 names file %1 of \"%2\" \"%3\"")
                .arg(fileIndex).arg(rt.item->name, safe));
    emit itemChanged(rt.item->id);
}

void UsenetQueue::finishItem(ItemRuntime& rt, bool success, const QString& message)
{
    m_stats.noteItemFinished(success, rt.item->decodedBytes());
    emit itemFinished(rt.item->id, success, message);
}

void UsenetQueue::closePostStage(ItemRuntime& rt)
{
    if (rt.postStageClock.isValid())
        m_stats.addPostStageTime(rt.postStage, rt.postStageClock.elapsed());
    rt.postStageClock.invalidate();
    rt.postStage = PostStage::Idle;
}

void UsenetQueue::padToDeclaredSize(const UsenetFileState& st)
{
    // declaredSize arrives with the first article that turns up, so it is zero
    // only when a file is missing its opening article too. Nothing can be
    // inferred in that case — the NZB's own `bytes` is the *encoded* size — so
    // the file is left as it is and verification will call it damaged, which is
    // the honest answer.
    if (st.declaredSize <= 0 || st.tempPath.isEmpty())
        return;

    QFile f(st.tempPath);
    if (!f.open(QIODevice::ReadWrite))
        return;
    if (f.size() < st.declaredSize)
        f.resize(st.declaredSize);
    f.close();
}

bool UsenetQueue::rearmMissingSegments(ItemRuntime& rt, int fileIndex)
{
    if (fileIndex < 0 || fileIndex >= rt.item->files.size())
        return false;

    UsenetFileState& st = rt.item->files[fileIndex];

    // The file has to still be where the state says, and be the length it says.
    // ArticleWriter::open() creates what it cannot find, so a re-arm against a
    // renamed or deleted file would build a fresh sparse file holding one
    // article — which the unpack and share scans would then find beside the real
    // one. par2's rename pass is the commonest cause and it leaves the release
    // intact under another name, so look there before giving up.
    const auto usable = [&st](const QString& path) {
        const QFileInfo info(path);
        return info.isFile() && (st.declaredSize <= 0 || info.size() >= st.declaredSize);
    };

    if (!usable(st.tempPath)) {
        const QString itemDir = QFileInfo(st.tempPath).absolutePath();
        const QString byName = rt.item->bestFileName(fileIndex);
        const QString rescued = byName.isEmpty()
            ? QString()
            : QDir(itemDir).filePath(QFileInfo(byName).fileName());

        if (rescued.isEmpty() || !usable(rescued))
            return false;

        logInfo(QStringLiteral("Usenet: \"%1\" was renamed by the repair — "
                               "retrying against \"%2\"")
                    .arg(rt.item->name, QFileInfo(rescued).fileName()));
        st.tempPath = rescued;
    }

    int armed = 0;
    for (qsizetype s = 0; s < st.missing.size(); ++s) {
        if (!st.missing.testBit(s))
            continue;
        if (s < st.done.size())
            st.done.clearBit(s);        // the only place a done bit is ever cleared
        ++armed;
    }
    if (armed == 0)
        return false;

    rt.refetch.armed += armed;
    rt.refetch.outstanding += armed;
    rt.refetch.files.insert(fileIndex);
    rt.dirty = true;
    return true;
}

void UsenetQueue::noteRefetchResolved(ItemRuntime& rt, int fileIndex, int segmentIndex,
                                      bool landed)
{
    Q_UNUSED(fileIndex)
    Q_UNUSED(segmentIndex)

    if (rt.refetch.outstanding <= 0)
        return;

    if (landed)
        ++rt.refetch.landed;
    if (--rt.refetch.outstanding > 0)
        return;

    // The last one. Pad now rather than per article: a hole at the *end* of a
    // file leaves nothing to grow it later, and sealFile() will not run again on
    // a file that is already finalized.
    for (const int f : rt.refetch.files) {
        if (f >= 0 && f < rt.item->files.size())
            padToDeclaredSize(rt.item->files.at(f));
    }

    // Paths did not move — that is the point of not un-sealing — but the index
    // caches parsed headers and how far it scanned, and a re-fetched article can
    // turn a region it read as zeros into a real volume header.
    rt.streamIndex.invalidate();

    logInfo(QStringLiteral("Usenet: retry of \"%1\" recovered %2 of %3 article(s)")
                .arg(rt.item->name)
                .arg(rt.refetch.landed)
                .arg(rt.refetch.armed));
}

void UsenetQueue::refreshDiskState(bool force)
{
    if (!thePrefs.checkDiskspace()) {
        if (m_diskBlocked) {
            m_diskBlocked = false;
            m_diskStallLogged = false;
            for (auto& rt : m_items) {
                if (!rt->item->stalledReason.isEmpty() && rt->item->isActive()) {
                    rt->item->stalledReason.clear();
                    emit itemChanged(rt->item->id);
                }
            }
        }
        return;
    }

    // QStorageInfo is a syscall and the tick runs four times a second.
    if (!force && m_diskCheckClock.isValid()
        && m_diskCheckClock.elapsed() < kDiskCheckIntervalMs) {
        return;
    }
    m_diskCheckClock.start();

    const QString dir = thePrefs.usenetTempDir();
    const qint64 floor = qint64(thePrefs.minFreeDiskSpace());
    const std::optional<std::uint64_t> free = eMule::tryFreeDiskSpace(dir);

    // Unknown counts as blocked. Writing into a volume nothing can measure is
    // how the article-level failure path gets exercised, and that path is the
    // one this exists to keep out of the ladder. Waiting is always recoverable;
    // a wrong verdict about an article is not.
    const bool blocked = !free.has_value()
        ? true
        : (m_diskBlocked ? qint64(*free) < floor + kDiskUnparkHeadroom
                         : qint64(*free) < floor);

    if (blocked == m_diskBlocked)
        return;

    m_diskBlocked = blocked;

    const QString reason = !free.has_value()
        ? tr("cannot read free space on the download folder")
        : tr("waiting for disk space — %1 free, %2 required")
              .arg(formatByteSize(qint64(*free)), formatByteSize(floor));

    for (auto& rt : m_items) {
        if (!rt->item->isActive())
            continue;
        rt->item->stalledReason = blocked ? reason : QString();
        emit itemChanged(rt->item->id);
    }

    if (blocked) {
        if (!m_diskStallLogged) {
            m_diskStallLogged = true;
            logWarning(QStringLiteral("Usenet: paused — %1 (%2)").arg(reason, dir));
        }
    } else {
        m_diskStallLogged = false;
        logInfo(QStringLiteral("Usenet: resuming — the download folder has room again"));
    }
}

bool UsenetQueue::volumeHasRoom(const QString& dir) const
{
    if (!thePrefs.checkDiskspace() || dir.isEmpty())
        return true;

    const std::optional<std::uint64_t> free = eMule::tryFreeDiskSpace(dir);
    return free.has_value() && qint64(*free) >= qint64(thePrefs.minFreeDiskSpace());
}

QString UsenetQueue::serverLadderDigest() const
{
    // The accounts, not their settings: key() is host/port/user, so switching a
    // provider to TLS or fixing a password is a different connection and counts
    // as a change. Retention and level are deliberately out — retention may
    // never cause a missing verdict, and level only reorders.
    QStringList keys;
    keys.reserve(m_servers.size());
    for (const NewsServer& s : m_servers) {
        if (s.enabled && s.isValid())
            keys.append(s.key());
    }
    keys.sort();

    return QString::fromLatin1(
        QCryptographicHash::hash(keys.join(QChar(u'\n')).toUtf8(),
                                 QCryptographicHash::Sha1)
            .toHex()
            .left(16));
}

} // namespace eMule::usenet
