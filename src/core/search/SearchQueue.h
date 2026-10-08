#pragma once

/// @file SearchQueue.h
/// @brief Holds searches that cannot be sent yet and sends them in turn.
///
/// A search used to be sent or dropped on the spot: started while no server
/// was connected it left an empty tab for good, and two server searches in a
/// row shared one answer slot, so the first one's late answer landed in the
/// second. Here every search gets its id at once and a state; a server search
/// goes out only when the one before it is done, a Kad search as soon as Kad
/// is connected, a Usenet / torrent search as soon as a server offering it is known. Nothing is paced: the next search leaves the moment its lane
/// is free.

#include "search/SearchParams.h"
#include "utils/Types.h"

#include <QObject>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

namespace eMule {

enum class SearchRunState : uint8 {
    Queued   = 0,   ///< waiting; @c reason says for what
    Running  = 1,   ///< sent, answers may arrive
    Finished = 2,   ///< done asking; results stay
    Failed   = 3    ///< never sent; @c error says why
};

/// "queued" / "running" / "finished" / "failed" — the REST spelling.
[[nodiscard]] inline QString searchRunStateName(SearchRunState state)
{
    switch (state) {
    case SearchRunState::Queued:   return QStringLiteral("queued");
    case SearchRunState::Running:  return QStringLiteral("running");
    case SearchRunState::Finished: return QStringLiteral("finished");
    case SearchRunState::Failed:   return QStringLiteral("failed");
    }
    return {};
}

/// Why a queued search waits (stable strings, also on IPC and REST).
namespace SearchWait {
inline constexpr auto ServerConnection = "waiting-for-server-connection";
inline constexpr auto Kad              = "waiting-for-kad";
inline constexpr auto Connection       = "waiting-for-connection";      ///< Automatic, neither network up
inline constexpr auto PreviousSearch   = "waiting-for-previous-search";
/// A Usenet / torrent search: no server is known to offer it, but one we are
/// logging in to may still say it does.
inline constexpr auto ServerInfo       = "waiting-for-server-info";
} // namespace SearchWait

struct SearchStatus {
    uint32 searchID = 0;
    SearchRunState state = SearchRunState::Queued;
    QString reason;             ///< Queued only
    QString error;              ///< Failed only
    SearchType type = SearchType::Ed2kServer;   ///< as asked until sent, then the network used
    QString keyword;            ///< Kad: the keyword searched when it is not the first one
    QString primaryKeyword;
    /// Finished, but the server has a further page: more() fetches it. A Meta API
    /// search, or an eD2K one whose server sent the "more results" byte.
    bool hasMore = false;
};

/// What sending one search came to.
struct SearchDispatch {
    enum class Outcome {
        Sent,         ///< on its way
        SendFailed,   ///< the connection was going down under it: worth another try
        Busy,         ///< cannot go now but may later (Kad keyword still being searched)
        Refused       ///< will never go; @c error says why
    };
    Outcome outcome = Outcome::Refused;
    QString error;
    bool awaitsSweep = false;   ///< a global search: ends with its sweep, not with the TCP answer
    bool awaitsMeta = false;    ///< a Meta API search: ends with onMetaSearchFinished()
    QString keyword;
    QString primaryKeyword;
};

/// Everything the queue needs from the rest of the client. Tests supply their own.
struct SearchQueueBackend {
    /// Empty when the request is acceptable, else why it is not.
    std::function<QString(const SearchParams&)> validate;
    /// The network for this search right now; Automatic resolved. nullopt: Automatic
    /// and no network to choose from yet.
    std::function<std::optional<SearchType>(const SearchParams&)> resolve;
    /// Empty when a search of this type can be sent now, else a SearchWait reason.
    std::function<QString(SearchType)> waitReason;
    /// Allocate the id and the (empty) result list.
    std::function<uint32(const SearchParams&)> create;
    /// Drop what create() made, for a search refused on its first try.
    std::function<void(uint32 searchID)> discard;
    std::function<SearchDispatch(uint32 searchID, SearchType type, const SearchParams&)> dispatch;
    /// The server search is over: later answers are not its any more.
    std::function<void(uint32 searchID)> endServerSearch;
    std::function<bool(uint32 searchID)> kadSearchAlive;
    /// A Meta API search was stopped or removed while running. May be empty.
    std::function<void(uint32 searchID)> cancelMetaSearch;
    /// Fetch the next page of a Meta API search that ended with more to come.
    std::function<void(uint32 searchID)> continueMetaSearch;
    /// Send OP_QUERY_MORE_RESULT for the last eD2K search and route the answer to
    /// it again. False when it could not go out. May be empty.
    std::function<bool(uint32 searchID, const SearchParams&)> continueServerSearch;
    std::function<qint64()> nowMs;
};

class SearchQueue : public QObject {
    Q_OBJECT

public:
    static constexpr int kMaxQueued = 20;
    static constexpr int kMaxSendRetries = 3;
    static constexpr qint64 kMaxWaitMs = 10 * 60 * 1000;
    /// A server that got the request and says nothing is given this long.
    static constexpr qint64 kAnswerTimeoutMs = 30 * 1000;
    /// A sweep reports its own end; this only catches one that never does.
    static constexpr qint64 kSweepTimeoutMs = 15 * 60 * 1000;
    /// Same for a Meta API search (a few servers, one page, 30 s a call).
    static constexpr qint64 kMetaTimeoutMs = 90 * 1000;

    explicit SearchQueue(SearchQueueBackend backend, QObject* parent = nullptr);

    struct Result {
        bool ok = false;        ///< false: refused, nothing was created
        QString error;
        SearchStatus status;
        bool duplicate = false; ///< the same search already waits or runs: its status
    };

    /// Take a search: created at once, sent now if it can be.
    [[nodiscard]] Result enqueue(const SearchParams& params);

    /// Send whatever can go. Called on every change that may free a lane.
    void pump();
    /// Timeouts, ended Kad searches, then pump(). About once a second.
    void tick();

    void onServerConnected()    { pump(); }
    void onKadConnected()       { pump(); }
    /// The session a waiting answer was due on is gone.
    void onServerDisconnected();
    /// The connected server answered the search in flight. @p moreResults: it
    /// holds back further matches, kept for more().
    void onServerAnswer(bool moreResults = false);
    void onSweepFinished(uint32 searchID);
    /// A Meta API search has its page, or gave up (@p error says why).
    /// @p hasMore: the server has a further page, kept for more().
    void onMetaSearchFinished(uint32 searchID, const QString& error = {}, bool hasMore = false);
    /// Fetch the next page of a finished search. False when it has none.
    bool more(uint32 searchID);

    /// Stop asking; a queued search is not sent any more. Results stay.
    void stop(uint32 searchID);
    /// Forget the search altogether.
    void remove(uint32 searchID);
    void clear();

    [[nodiscard]] std::optional<SearchStatus> status(uint32 searchID) const;
    [[nodiscard]] int queuedCount() const;
    /// The server search in flight; 0 when the lane is free.
    [[nodiscard]] uint32 serverSearchInFlight() const { return m_inFlight; }

signals:
    void stateChanged(const eMule::SearchStatus& status);

private:
    struct Entry {
        SearchParams params;
        SearchStatus status;
        QString dedupKey;
        qint64 queuedAtMs = 0;
        int sendRetries = 0;
        bool kad = false;           ///< running in Kad (no lane to hold)
        bool awaitsSweep = false;
        bool meta = false;          ///< running on a Meta API (no lane to hold)
        bool serverPage = false;    ///< the connected server holds a further page
        int moreRequests = 0;       ///< OP_QUERY_MORE_RESULT sent so far
        qint64 sentAtMs = 0;
    };

    [[nodiscard]] Entry* find(uint32 searchID);
    [[nodiscard]] const Entry* find(uint32 searchID) const;
    [[nodiscard]] static QString dedupKeyFor(const SearchParams& params);
    [[nodiscard]] static bool isServerType(SearchType type);

    void setWaiting(Entry& entry, const char* reason);
    void setState(Entry& entry, SearchRunState state, const QString& error = {});
    /// The server search in flight is over; the lane is free.
    void finishServerSearch(SearchRunState state, const QString& error = {});
    void sendFailed(Entry& entry);
    /// Tell the backend a running Meta API search is not wanted any more;
    /// with @p parkedToo also one that only keeps its next page.
    void cancelMeta(Entry& entry, bool parkedToo = false);
    /// The server keeps the rest of its last answer only: a new search or a lost
    /// session ends every page on offer but @p keep's.
    void dropServerPages(uint32 keep = 0);

    SearchQueueBackend m_backend;
    std::vector<Entry> m_entries;   // arrival order
    uint32 m_inFlight = 0;
    qint64 m_inFlightDeadlineMs = 0;
    bool m_pumping = false;
};

} // namespace eMule

Q_DECLARE_METATYPE(eMule::SearchStatus)
