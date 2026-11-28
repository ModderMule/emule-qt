#pragma once

/// @file ArrSearchBridge.h
/// @brief eD2K/Kad searches for Sonarr/Radarr: on demand (Torznab t=search) and in
///        the background from the Arr Wanted lists (the synthetic RSS feed).
///
/// eD2K has no feed, so an Arr RSS sync would find nothing between manual searches.
/// ArrWanted polls each instance's Wanted > Missing; this searches those titles one
/// at a time, spaced out, and keeps the hits for the no-query feed (mularr's idea).
/// Lives in the daemon across web server restarts.

#include "webserver/ArrCompat.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QTimer>

#include <functional>

namespace eMule {

class SearchList;

class ArrSearchBridge : public QObject {
    Q_OBJECT

public:
    explicit ArrSearchBridge(QObject* parent = nullptr);
    ~ArrSearchBridge() override;

    /// Unset: theApp.searchList.
    void setSearchList(SearchList* list) { m_list = list; }

    struct Request {
        QString query;
        QList<int> categories;   ///< newznab ids, may be empty
        int limit = 100;
    };
    using Done = std::function<void(const QList<arr::TorznabItem>&)>;

    /// Searches eD2K/Kad and answers after @p waitSeconds, or sooner when the search
    /// finishes. Same query within 15 min: answered from the cache.
    void search(const Request& request, int waitSeconds, Done done);

    /// The synthetic feed: background hits of the last 24 h, newest first, in
    /// @p categories (groups match; empty = all).
    [[nodiscard]] QList<arr::TorznabItem> feed(const QList<int>& categories, int limit) const;

    /// From ArrWanted: replaces the queue of titles still to search.
    void setWanted(const QList<arr::WantedTerm>& terms);
    /// Background searches on or off (arrApiEnabled and a poll interval).
    void setBackgroundEnabled(bool on);

    [[nodiscard]] int feedSize() const { return int(m_feed.size()); }

private:
    struct Running {
        quint32 searchId = 0;
        QElapsedTimer age;
        int waitMs = 0;
        Request request;
        QString cacheKey;
        QList<Done> waiters;
        bool background = false;
        bool kadOnly = false;
        int backgroundCategory = 0;
    };
    struct Cached {
        qint64 at = 0;
        QList<arr::TorznabItem> items;
    };
    struct FeedEntry {
        arr::TorznabItem item;
        qint64 at = 0;
    };

    void tick();
    void startNextBackground();
    void finish(Running& run);
    [[nodiscard]] QList<arr::TorznabItem> harvest(const Running& run) const;
    [[nodiscard]] SearchList* list() const;
    [[nodiscard]] bool start(Running& run);
    void pruneFeed();

    SearchList* m_list = nullptr;
    QList<Running> m_running;
    QHash<QString, Cached> m_cache;
    QHash<QString, FeedEntry> m_feed;          ///< by MD4
    QList<arr::WantedTerm> m_wanted;           ///< still to search this round
    QTimer m_tick;
    QElapsedTimer m_sinceBackground;
    bool m_backgroundOn = false;
};

} // namespace eMule
