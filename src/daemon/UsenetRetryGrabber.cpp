/// @file UsenetRetryGrabber.cpp
/// @brief Replacement NZB for a failed job: X-DNZB-Failure, then a search.

#include "UsenetRetryGrabber.h"

#include "IndexerCapsStore.h"
#include "IndexerClient.h"
#include "IndexerQuery.h"
#include "IndexerResult.h"
#include "UsenetSession.h"
#include "prefs/Preferences.h"
#include "queue/UsenetHealth.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetReleaseName.h"
#include "queue/UsenetStatistics.h"
#include "utils/Log.h"

#include <QPointer>
#include <QUrl>

#include <algorithm>
#include <memory>

namespace eMule {

namespace {

const IndexerConfig* usenetAccountNamed(const QList<IndexerConfig>& accounts, const QString& name)
{
    const auto it = std::ranges::find_if(accounts, [&name](const IndexerConfig& a) {
        return a.enabled && a.servesUsenet() && a.displayName() == name;
    });
    return it == accounts.cend() ? nullptr : &*it;
}

/// The same thing as @p title: one episode, or one folded name.
bool sameRelease(const QString& title, const QString& candidate)
{
    const auto a = usenet::parseReleaseEpisode(title);
    const auto b = usenet::parseReleaseEpisode(candidate);
    if (a || b)
        return a && b && a->key() == b->key();
    // a movie or anything else: the name minus its group tag
    const auto stem = [](const QString& t) {
        QString f = usenet::usenetFoldedReleaseName(t);
        if (const qsizetype dash = t.lastIndexOf(QLatin1Char('-')); dash > t.size() / 2)
            f = usenet::usenetFoldedReleaseName(t.left(dash));
        return f;
    };
    return stem(title) == stem(candidate);
}

bool looksLikeNzb(const QByteArray& body)
{
    return body.left(4096).contains("<nzb");
}

} // namespace

UsenetRetryGrabber::UsenetRetryGrabber(usenet::UsenetSession* session, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_client(new indexer::IndexerClient(this))
{
    connect(m_session, &usenet::UsenetSession::itemFinished, this,
            &UsenetRetryGrabber::onItemFinished);
}

UsenetRetryGrabber::~UsenetRetryGrabber()
{
    m_client->abortAll();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void UsenetRetryGrabber::onItemFinished(const QString& id, bool success, const QString&)
{
    if (success || !thePrefs.usenetRetryOtherNzb() || !m_session->queue())
        return;
    const usenet::UsenetQueueItem* item = m_session->queue()->findItem(id);
    if (!item || item->status != usenet::UsenetItemStatus::Failed || !item->origin.isSet())
        return;
    // another release would want a password just the same
    if (item->passwordRequired || m_running.contains(id))
        return;
    if (item->origin.retries >= kMaxRetries) {
        logUsenet(QStringLiteral("Usenet: \"%1\" failed; %2 replacement NZBs tried already")
                      .arg(item->name).arg(item->origin.retries));
        return;
    }

    Job job;
    job.itemId = id;
    job.itemName = item->name;
    job.indexerName = item->origin.indexerName;
    job.title = item->origin.title.isEmpty() ? item->name : item->origin.title;
    job.failureUrl = item->origin.failureUrl;
    job.triedGuids = item->origin.triedGuids;
    job.retries = item->origin.retries;
    job.size = item->nzb.totalEncodedBytes();
    job.category = item->category;
    job.priority = item->priority;
    m_running.insert(id);

    logUsenet(QStringLiteral("Usenet: \"%1\" failed; looking for another NZB").arg(job.itemName));
    if (!job.failureUrl.isEmpty())
        tryFailureUrl(job);
    else
        search(job);
}

void UsenetRetryGrabber::tryFailureUrl(const Job& job)
{
    const QUrl url(job.failureUrl);
    QPointer<UsenetRetryGrabber> self(this);
    const auto done = [self, job](bool ok, const QByteArray& body, const QString& error) {
        if (!self)
            return;
        // the answer is the replacement itself, when the indexer has one
        if (ok && looksLikeNzb(body)
            && self->add(job, body, job.title, {}, {}, job.indexerName, {})) {
            self->finish(job, QStringLiteral("the indexer's replacement"));
            return;
        }
        logUsenet(QStringLiteral("Usenet: the indexer offered no replacement for \"%1\"%2; searching")
                      .arg(job.itemName, error.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(error)));
        Job next = job;
        next.failureUrl.clear();
        self->search(next);
    };
    const QList<IndexerConfig> accounts = thePrefs.indexers();
    if (const IndexerConfig* account = usenetAccountNamed(accounts, job.indexerName))
        m_client->fetch(*account, url, done);
    else
        m_client->fetchUrl(url, thePrefs.indexerTimeoutSeconds() * 1000, done);
}

void UsenetRetryGrabber::search(const Job& job)
{
    QList<IndexerConfig> accounts;
    for (const IndexerConfig& a : thePrefs.indexers()) {
        if (a.enabled && a.servesUsenet())
            accounts.append(a);
    }
    if (accounts.isEmpty()) {
        finish(job, {});
        return;
    }

    indexer::IndexerQuery query;
    query.limit = 100;
    if (const auto ep = usenet::parseReleaseEpisode(job.title)) {
        query.text = ep->title;
        if (ep->airDate.isValid()) {
            query.text += QLatin1Char(' ') + ep->airDate.toString(QStringLiteral("yyyy MM dd"));
        } else {
            query.mode = indexer::kQueryTvSearch.toString();
            query.season = QString::number(ep->season);
            query.episode = QString::number(ep->episode);
        }
    } else {
        query.text = usenet::usenetFoldedReleaseName(job.title);
    }

    struct Gather {
        int outstanding = 0;
        QList<indexer::IndexerResult> rows;
    };
    auto gather = std::make_shared<Gather>();
    gather->outstanding = int(accounts.size());
    QPointer<UsenetRetryGrabber> self(this);
    for (const IndexerConfig& account : std::as_const(accounts)) {
        indexer::IndexerCaps caps;
        const bool haveCaps = indexer::IndexerCapsStore::load(account, caps);
        // a typed mode the caps lack becomes t=search with "S01E02" in the text
        const indexer::IndexerQuery asked = haveCaps ? indexer::queryForIndexer(query, caps) : query;
        m_client->search(account, asked, haveCaps ? &caps : nullptr,
                         [self, job, gather](bool ok, const indexer::IndexerSearchPage& page,
                                             const QString&) {
            if (!self)
                return;
            if (ok)
                gather->rows += page.results;
            if (--gather->outstanding == 0)
                self->pickAndGrab(job, std::move(gather->rows));
        });
    }
}

void UsenetRetryGrabber::pickAndGrab(const Job& job, QList<indexer::IndexerResult> rows)
{
    QList<indexer::IndexerResult> candidates;
    for (const indexer::IndexerResult& row : std::as_const(rows)) {
        if (!row.isUsenet() || row.guid.isEmpty() || job.triedGuids.contains(row.guid))
            continue;
        // same release; size within half either way, when both are known
        if (!sameRelease(job.title, row.title))
            continue;
        if (job.size > 0 && row.size > 0 && (row.size * 2 < job.size || row.size > job.size * 3 / 2))
            continue;
        candidates.append(row);
    }
    // newest first: a fresh repost has the best chance of being complete
    std::ranges::sort(candidates, [](const indexer::IndexerResult& a, const indexer::IndexerResult& b) {
        return a.published > b.published;
    });
    grabNext(job, std::move(candidates));
}

void UsenetRetryGrabber::grabNext(const Job& job, QList<indexer::IndexerResult> candidates)
{
    if (candidates.isEmpty()) {
        finish(job, {});
        return;
    }
    const indexer::IndexerResult row = candidates.takeFirst();
    const QList<IndexerConfig> accounts = thePrefs.indexers();
    const IndexerConfig* account = usenetAccountNamed(accounts, row.indexerName);
    if (!account) {
        grabNext(job, std::move(candidates));
        return;
    }
    QPointer<UsenetRetryGrabber> self(this);
    m_client->fetch(*account, row.downloadUrl,
                    [self, job, row, candidates = std::move(candidates)](bool ok, const QByteArray& body,
                                                                         const QString&) mutable {
        if (!self)
            return;
        Job next = job;
        next.triedGuids.append(row.guid);   // spent either way
        if (ok && looksLikeNzb(body)
            && self->add(next, body, row.title, row.password, row.guid, row.indexerName,
                         indexer::IndexerClient::failureUrlFor(row.downloadUrl)
                             .toString(QUrl::FullyEncoded))) {
            self->finish(next, QStringLiteral("\"%1\" from %2").arg(row.title, row.indexerName));
            return;
        }
        self->grabNext(next, std::move(candidates));
    });
}

bool UsenetRetryGrabber::add(const Job& job, const QByteArray& nzb, const QString& name,
                             const QString& password, const QString& guid,
                             const QString& indexerName, const QString& failureUrl)
{
    usenet::UsenetQueue* queue = m_session->queue();
    if (!queue)
        return false;

    usenet::UsenetOrigin origin;
    origin.indexerName = indexerName;
    origin.guid = guid;
    origin.title = name;
    origin.failureUrl = failureUrl;
    origin.triedGuids = job.triedGuids;
    origin.retries = job.retries + 1;

    QString error;
    usenet::UsenetAddOutcome outcome = usenet::UsenetAddOutcome::Failed;
    // force: the failed item has this episode's name, and was "downloaded" by nobody
    queue->addNzb(nzb, name, error,
                  {.source = usenet::UsenetAddSource::Automatic, .force = true,
                   .password = password, .category = job.category, .priority = job.priority,
                   .origin = origin},
                  &outcome);
    queue->stats().noteAdd(usenet::UsenetAddOrigin::Retry, outcome);
    if (outcome != usenet::UsenetAddOutcome::Added) {
        logUsenet(QStringLiteral("Usenet: replacement \"%1\" not added: %2").arg(name, error));
        return false;
    }
    return true;
}

void UsenetRetryGrabber::finish(const Job& job, const QString& outcome)
{
    m_running.remove(job.itemId);
    if (outcome.isEmpty()) {
        logUsenet(QStringLiteral("Usenet: no other NZB found for \"%1\"").arg(job.itemName));
        return;
    }
    logUsenet(QStringLiteral("Usenet: \"%1\" replaced by %2").arg(job.itemName, outcome));
}

} // namespace eMule
