#include "pch.h"
/// @file ArrSearchBridge.cpp
/// @brief eD2K/Kad searches for Sonarr/Radarr — implementation.

#include "webserver/ArrSearchBridge.h"

#include "app/AppContext.h"
#include "search/FakeFileDetector.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "search/SearchParams.h"
#include "search/SearchStarter.h"
#include "utils/Log.h"

#include <algorithm>

namespace eMule {

namespace {

constexpr int kTickMs = 1000;
constexpr qint64 kCacheMs = 15 * 60 * 1000;
constexpr qint64 kFeedMaxAgeMs = 24 * 3600 * 1000;
constexpr int kFeedMax = 500;
/// background: one search at a time, this far apart (servers and Kad flood limits)
constexpr qint64 kBackgroundGapMs = 90 * 1000;
constexpr int kBackgroundWaitMs = 60 * 1000;
constexpr int kMaxRunning = 4;
constexpr int kRowsPerBackgroundTerm = 10;

[[nodiscard]] qint64 nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

[[nodiscard]] QString cacheKeyOf(const ArrSearchBridge::Request& r)
{
    QStringList cats;
    for (const int c : r.categories)
        cats << QString::number(c);
    return r.query.simplified().toLower() + u'|' + cats.join(u',');
}

[[nodiscard]] bool categoryWanted(int cat, const QList<int>& wanted)
{
    return wanted.isEmpty() || std::ranges::any_of(wanted, [cat](int w) {
        return w == cat || w / 1000 == cat / 1000;
    });
}

} // namespace

ArrSearchBridge::ArrSearchBridge(QObject* parent)
    : QObject(parent)
{
    m_tick.setInterval(kTickMs);
    connect(&m_tick, &QTimer::timeout, this, &ArrSearchBridge::tick);
}

ArrSearchBridge::~ArrSearchBridge()
{
    if (SearchList* l = list()) {
        for (const Running& run : std::as_const(m_running))
            removeSearch(*l, run.searchId);
    }
}

void ArrSearchBridge::search(const Request& request, int waitSeconds, Done done)
{
    const QString key = cacheKeyOf(request);
    if (const auto it = m_cache.constFind(key); it != m_cache.cend() && nowMs() - it->at < kCacheMs) {
        done(it->items.mid(0, request.limit));
        return;
    }
    // the same ask in flight: wait for that one
    for (Running& run : m_running) {
        if (!run.background && run.cacheKey == key) {
            run.waiters.append(std::move(done));
            return;
        }
    }
    if (m_running.size() >= kMaxRunning) {
        logDebug(QStringLiteral("Arr: too many searches at once, \"%1\" answered empty").arg(request.query));
        done({});
        return;
    }

    Running run;
    run.request = request;
    run.cacheKey = key;
    run.waitMs = std::clamp(waitSeconds, 5, 90) * 1000;
    run.waiters.append(std::move(done));
    if (!start(run)) {
        for (const Done& d : std::as_const(run.waiters))
            d({});
        return;
    }
    m_running.append(std::move(run));
    m_tick.start();
}

QList<arr::TorznabItem> ArrSearchBridge::feed(const QList<int>& categories, int limit) const
{
    QList<arr::TorznabItem> out;
    const qint64 now = nowMs();
    for (const FeedEntry& e : m_feed) {
        if (now - e.at <= kFeedMaxAgeMs && categoryWanted(e.item.category, categories))
            out.append(e.item);
    }
    std::ranges::sort(out, std::greater{}, &arr::TorznabItem::published);
    if (out.size() > limit)
        out.resize(limit);
    return out;
}

void ArrSearchBridge::setWanted(const QList<arr::WantedTerm>& terms)
{
    m_wanted = terms;
    if (m_backgroundOn && !m_wanted.isEmpty()) {
        logInfo(QStringLiteral("Arr: %1 wanted titles to search on eD2K").arg(m_wanted.size()));
        m_tick.start();
    }
}

void ArrSearchBridge::setBackgroundEnabled(bool on)
{
    m_backgroundOn = on;
    if (!on)
        m_wanted.clear();
    else if (!m_wanted.isEmpty())
        m_tick.start();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void ArrSearchBridge::tick()
{
    SearchList* l = list();
    for (qsizetype i = 0; i < m_running.size();) {
        Running& run = m_running[i];
        const auto status = l ? l->queue().status(run.searchId) : std::nullopt;
        const bool done = !status || status->state == SearchRunState::Finished
            || status->state == SearchRunState::Failed || run.age.elapsed() >= run.waitMs;
        if (!done) {
            ++i;
            continue;
        }
        finish(run);
        m_running.removeAt(i);
    }
    startNextBackground();
    if (m_running.isEmpty() && (!m_backgroundOn || m_wanted.isEmpty()))
        m_tick.stop();
}

void ArrSearchBridge::startNextBackground()
{
    if (!m_backgroundOn || m_wanted.isEmpty())
        return;
    // on-demand asks first, and one background search at a time
    if (!m_running.isEmpty())
        return;
    if (m_sinceBackground.isValid() && m_sinceBackground.elapsed() < kBackgroundGapMs)
        return;
    // Kad only: it has its own lane, server searches go one at a time and the
    // user's would wait behind ours. Without Kad the titles wait.
    if (!gatherAutoSearchState().kadConnected)
        return;

    const arr::WantedTerm term = m_wanted.takeFirst();
    Running run;
    run.background = true;
    run.backgroundCategory = term.category;
    run.request.query = term.query;
    run.request.categories = {term.category};
    run.request.limit = kRowsPerBackgroundTerm;
    run.waitMs = kBackgroundWaitMs;
    run.kadOnly = true;
    m_sinceBackground.start();
    if (start(run)) {
        logDebug(QStringLiteral("Arr: background search \"%1\" (%2)").arg(term.query, term.source));
        m_running.append(std::move(run));
    }
}

bool ArrSearchBridge::start(Running& run)
{
    SearchList* l = list();
    if (!l || run.request.query.trimmed().isEmpty())
        return false;
    SearchParams params;
    params.expression = run.request.query.simplified();
    params.keyword = params.expression;
    params.searchTitle = params.expression;
    params.fileType = arr::ed2kTypeForCategories(run.request.categories);
    params.type = run.kadOnly ? SearchType::Kademlia : SearchType::Automatic;
    const SearchStartResult outcome = startSearch(*l, params);
    if (!outcome.ok) {
        logDebug(QStringLiteral("Arr: search \"%1\" refused: %2").arg(params.expression, outcome.error));
        return false;
    }
    run.searchId = outcome.searchID;
    run.age.start();
    return true;
}

void ArrSearchBridge::finish(Running& run)
{
    const QList<arr::TorznabItem> items = harvest(run);
    if (SearchList* l = list())
        removeSearch(*l, run.searchId);

    if (run.background) {
        const qint64 now = nowMs();
        int added = 0;
        for (const arr::TorznabItem& item : items) {
            const QString key = item.hashHex;
            auto it = m_feed.find(key);
            if (it == m_feed.end()) {
                m_feed.insert(key, {item, now});
                ++added;
            } else {
                it->item.seeders = item.seeders;   // keep the first sighting's date
                it->item.peers = item.peers;
                it->at = now;
            }
        }
        pruneFeed();
        if (added > 0)
            logInfo(QStringLiteral("Arr: %1 new eD2K hits for \"%2\" in the feed").arg(added).arg(run.request.query));
        return;
    }

    m_cache.insert(run.cacheKey, {nowMs(), items});
    // drop stale cache rows now and then
    if (m_cache.size() > 200) {
        const qint64 now = nowMs();
        m_cache.removeIf([now](const auto& it) { return now - it.value().at >= kCacheMs; });
    }
    const QList<arr::TorznabItem> answer = items.mid(0, run.request.limit);
    for (const Done& d : std::as_const(run.waiters))
        d(answer);
}

QList<arr::TorznabItem> ArrSearchBridge::harvest(const Running& run) const
{
    QList<arr::TorznabItem> out;
    SearchList* l = list();
    if (!l)
        return out;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    l->forEachResult(run.searchId, [&](const SearchFile* file) {
        if (file->isMetaResult() || file->isEd3kResult() || file->fileSize() == 0)
            return;
        if (file->hasFakeVerdict() && file->fakeVerdict().band <= Confidence::LikelyFake)
            return;
        // a background term is a title: every word of it in the name
        if (run.background && !arr::nameMatchesQuery(file->fileName(), run.request.query))
            return;
        arr::TorznabItem item;
        item.hashHex = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(file->fileHash()), 16).toHex());
        item.fileName = file->fileName();
        item.title = arr::releaseTitle(file->fileName());
        item.size = file->fileSize();
        item.seeders = int(file->completeSourceCount());
        item.peers = int(file->sourceCount());
        item.category = run.background ? run.backgroundCategory
                                       : arr::categoryForResult(file->fileName(), run.request.categories);
        item.published = now;
        out.append(item);
    });
    std::ranges::stable_sort(out, std::greater{}, &arr::TorznabItem::peers);
    return out;
}

SearchList* ArrSearchBridge::list() const
{
    return m_list ? m_list : theApp.searchList;
}

void ArrSearchBridge::pruneFeed()
{
    const qint64 now = nowMs();
    m_feed.removeIf([now](const auto& it) { return now - it.value().at > kFeedMaxAgeMs; });
    while (m_feed.size() > kFeedMax) {
        auto oldest = std::ranges::min_element(m_feed, {}, &FeedEntry::at);
        m_feed.erase(oldest);
    }
}

} // namespace eMule
