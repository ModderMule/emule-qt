#include "pch.h"
/// @file SearchStarter.cpp
/// @brief Search start / stop — port of MFC CSearchResultsWnd::StartNewSearch,
///        DoNewEd2kSearch and DoNewKadSearch (srchybrid/SearchResultsWnd.cpp).

#include "search/SearchStarter.h"

#include "app/AppContext.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchManager.h"
#include "net/Packet.h"
#include "search/GlobalSearchScheduler.h"
#include "search/MetaSearchRequest.h"
#include "search/MetaSearchRunner.h"
#include "search/SearchExprParser.h"
#include "search/SearchList.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/TimeUtils.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstring>

namespace eMule {

namespace {

// The messages keep the context they had in the daemon's handler. Daemon-side texts
// are not in the catalogues today (scripts/localize.sh), so they show in English.
SearchStartResult refused(const QString& why)
{
    SearchStartResult result;
    result.error = why;
    return result;
}

// Stock shows a "Pro" search unfiltered: servers file archives and CD images under it.
QString resultTypeFilter(const QString& fileType)
{
    return ed2kFileTypeSearchTerm(fileType) == QLatin1StringView(ED2KFTSTR_PROGRAM) ? QString() : fileType;
}

SearchDispatch refusedDispatch(const QString& why)
{
    SearchDispatch out;
    out.outcome = SearchDispatch::Outcome::Refused;
    out.error = why;
    return out;
}

QString keywordTooShort()
{
    return QCoreApplication::translate("eMule::IpcClientHandler", "Keyword too short.\n\nThe keyword(s) used in a Kad search "
                            "expression must have a minimum length of 3 characters.");
}

SearchDispatch dispatchKadSearch(SearchList& list, uint32 searchID, const SearchParams& params)
{
    SearchDispatch out;

    // A Kad search is indexed under a single keyword. When the expression's first
    // keyword is already the target of a running search, fall back to the next
    // word long enough to be a keyword. With every keyword taken the search waits
    // for one of them to come free.
    const kad::KeywordSelection sel = kad::SearchManager::selectKeyword(params.expression);
    if (sel.status == kad::KeywordStatus::TooShort)
        return refusedDispatch(keywordTooShort());
    if (sel.status == kad::KeywordStatus::AllActive) {
        out.outcome = SearchDispatch::Outcome::Busy;
        return out;
    }
    if (sel.isFallback) {
        logInfo(QCoreApplication::translate("eMule::IpcClientHandler", "Kad: \"%1\" is already being searched — using \"%2\" as the search "
                     "target for \"%3\"")
                    .arg(sel.primaryKeyword, sel.keyword, params.expression));
    }

    // The AND/OR/NOT tree + filters that travel with KADEMLIA2_SEARCH_KEY_REQ.
    // Without it a multi-word search degenerates to a bare single-keyword query.
    const QByteArray searchTerms = buildSearchTermsPayload(params, sel.keyword);

    // Under the id the search was given when it was queued.
    auto* kadSearch = kad::SearchManager::prepareFindKeywords(
        params.expression,
        static_cast<uint32>(searchTerms.size()),
        searchTerms.isEmpty() ? nullptr
                              : reinterpret_cast<const uint8*>(searchTerms.constData()),
        sel.keyword, searchID);
    if (!kadSearch) {
        out.outcome = SearchDispatch::Outcome::Busy;
        return out;
    }

    list.beginSearch(searchID, resultTypeFilter(params.fileType), /*ed2k*/ false, params.expression);
    if (!kad::SearchManager::startSearch(kadSearch)) {
        // Target was taken between selection and start — drop the half-built search.
        delete kadSearch;
        out.outcome = SearchDispatch::Outcome::Busy;
        return out;
    }

    out.outcome = SearchDispatch::Outcome::Sent;
    if (sel.isFallback) {
        out.keyword = sel.keyword;
        out.primaryKeyword = sel.primaryKeyword;
    }
    return out;
}

SearchDispatch dispatchEd2kSearch(SearchList& list, uint32 searchID, SearchType type,
                                  const SearchParams& asked)
{
    SearchParams params = asked;
    params.type = type;   // Automatic resolved

    const bool connected = theApp.serverConnect && theApp.serverConnect->isConnected();
    const Server* current = connected ? theApp.serverConnect->currentServer() : nullptr;

    // A size above 4 GiB goes out as a 64-bit term only to a server that reads one
    // (MFC DoNewEd2kSearch, SearchResultsWnd.cpp:1218-1219). With no server there is
    // only the sweep, which skips the servers that cannot.
    const bool supports64Bit = connected ? (current && current->supportsLargeFilesTCP()) : true;
    bool uses64Bit = false;
    const QByteArray payload = buildSearchTermsPayload(params, {}, supports64Bit, &uses64Bit);
    if (payload.isEmpty()) {
        return refusedDispatch(QCoreApplication::translate(
            "eMule::IpcClientHandler", "The search expression contains nothing to search for."));
    }

    // One ED2K search at a time; the queue sees to it that the one before is over,
    // this only clears a sweep left behind by anything else. Not for Kad: MFC cancels
    // from DoNewEd2kSearch only (srchybrid/SearchResultsWnd.cpp:1225).
    if (theApp.globalSearch)
        theApp.globalSearch->cancel();

    list.beginSearch(searchID, resultTypeFilter(params.fileType), /*ed2k*/ true, params.expression);

    // Both ED2K methods start by asking the connected server over TCP; "global"
    // then walks the rest of the list over UDP once that answer is in.
    bool localRequestSent = false;
    if (connected) {
        auto pkt = std::make_unique<Packet>(OP_SEARCHREQUEST, static_cast<uint32>(payload.size()));
        pkt->prot = OP_EDONKEYPROT;
        std::memcpy(pkt->pBuffer, payload.constData(), static_cast<size_t>(payload.size()));
        logServerVerbose(QStringLiteral(">>> TCP server search: expr=\"%1\" -> %2 (%3 byte payload)")
                             .arg(params.expression)
                             .arg(current ? current->name() : QStringLiteral("connected server"))
                             .arg(payload.size()));
        localRequestSent = theApp.serverConnect->sendPacket(std::move(pkt));
    }

    SearchDispatch out;
    const bool canSweep = type == SearchType::Ed2kGlobal && theApp.globalSearch;
    if (canSweep) {
        // One server per 750 ms, and only after the local server has answered (or
        // timed out). Without a local request the sweep starts right away — that is
        // how a Kad-only session still gets a global search.
        theApp.globalSearch->start(searchID, payload, uses64Bit,
                                   /*awaitLocalAnswer*/ localRequestSent);
        out.outcome = SearchDispatch::Outcome::Sent;
        out.awaitsSweep = true;
        return out;
    }

    if (!localRequestSent) {
        // The session went down between the check and the send.
        list.releaseEd2kRouting(searchID);
        out.outcome = SearchDispatch::Outcome::SendFailed;
        return out;
    }
    out.outcome = SearchDispatch::Outcome::Sent;
    return out;
}

SearchDispatch dispatchMetaSearch(uint32 searchID, SearchType type, const SearchParams& params)
{
    if (!theApp.metaSearch) {
        return refusedDispatch(QCoreApplication::translate(
            "eMule::IpcClientHandler", "Searching through a server's catalogue is not available here."));
    }
    auto request = buildMetaSearchRequest(params, type);
    if (!request)
        return refusedDispatch(request.error());

    auto candidates = metaSearchCandidates(type);
    if (candidates.empty()) {
        return refusedDispatch(QCoreApplication::translate(
            "eMule::IpcClientHandler",
            "No known server offers a %1 search. Connect once to a server that does, "
            "and it is remembered.").arg(metaNetworkName(type)));
    }

    theApp.metaSearch->startMetaSearch(searchID, type, params, *request, std::move(candidates));
    SearchDispatch out;
    out.outcome = SearchDispatch::Outcome::Sent;
    out.awaitsMeta = true;
    return out;
}

} // anonymous namespace

AutoSearchState gatherAutoSearchState()
{
    AutoSearchState state;

    const Server* server = nullptr;
    if (theApp.serverConnect && theApp.serverConnect->isConnected()) {
        state.serverConnected = true;
        server = theApp.serverConnect->currentServer();
    }
    if (server) {
        state.serverIsStatic = server->isStaticMember();
        state.serverUsers = server->users();
        state.serverFiles = server->files();
    }

    const auto* kadInst = kad::Kademlia::instance();
    state.kadConnected = kadInst != nullptr && kadInst->isRunning() && kadInst->isConnected();

    state.serverCount = theApp.serverList ? theApp.serverList->serverCount() : 0;
    return state;
}

SearchQueueBackend defaultSearchQueueBackend(SearchList& list)
{
    SearchQueueBackend backend;

    backend.validate = [](const SearchParams& params) -> QString {
        // An indexer search has its own entry point; running a server search in its
        // place would be a wrong answer, which is worse than a refusal.
        if (params.type == SearchType::UsenetIndexer)
            return QCoreApplication::translate("eMule::IpcClientHandler", "Indexer searches use StartIndexerSearch, not StartSearch.");
        if (params.type == SearchType::Kademlia
            && kad::SearchManager::selectKeyword(params.expression).status
                   == kad::KeywordStatus::TooShort)
            return keywordTooShort();
        if (isMetaSearchType(params.type)) {
            if (const auto request = buildMetaSearchRequest(params, params.type); !request)
                return request.error();
        }
        return {};
    };

    // "Automatic" is a chooser, not a network: resolved to exactly one at the moment
    // the search is sent. MFC: CSearchResultsWnd::StartNewSearch —
    // srchybrid/SearchResultsWnd.cpp:1134-1165.
    backend.resolve = [](const SearchParams& params) -> std::optional<SearchType> {
        if (params.type != SearchType::Automatic)
            return params.type;
        return resolveAutomaticSearchType(gatherAutoSearchState());
    };

    backend.waitReason = [](SearchType type) -> QString {
        const auto* kadInst = kad::Kademlia::instance();
        const bool kadUp = kadInst != nullptr && kadInst->isConnected();
        if (type == SearchType::Kademlia)
            return kadUp ? QString() : QString::fromLatin1(SearchWait::Kad);

        // Asked over HTTP of a server we know, connected or not. With none known it
        // waits only while a login may still bring one; else the dispatch refuses.
        if (isMetaSearchType(type)) {
            return metaSearchCandidates(type).empty() && metaServerInfoPending()
                ? QString::fromLatin1(SearchWait::ServerInfo) : QString();
        }

        const bool serverUp = theApp.serverConnect && theApp.serverConnect->isConnected();
        // A global search can sweep the list without a server of our own, as long as
        // the line is up at all (the UDP send asks for either network).
        if (serverUp || (type == SearchType::Ed2kGlobal && theApp.globalSearch && kadUp))
            return {};
        return QString::fromLatin1(SearchWait::ServerConnection);
    };

    // Ids come from the counter Kad uses too, so the id handed back now is the one a
    // Kad search started later runs — and reports its results — under.
    backend.create = [&list](const SearchParams&) -> uint32 { return list.reserveSearch(); };

    backend.discard = [&list](uint32 searchID) { list.removeResults(searchID); };

    backend.dispatch = [&list](uint32 searchID, SearchType type, const SearchParams& params) {
        if (type == SearchType::Kademlia)
            return dispatchKadSearch(list, searchID, params);
        if (isMetaSearchType(type))
            return dispatchMetaSearch(searchID, type, params);
        return dispatchEd2kSearch(list, searchID, type, params);
    };

    backend.endServerSearch = [&list](uint32 searchID) { list.releaseEd2kRouting(searchID); };

    backend.kadSearchAlive = [](uint32 searchID) {
        return kad::SearchManager::isSearching(searchID);
    };

    backend.cancelMetaSearch = [](uint32 searchID) {
        if (theApp.metaSearch)
            theApp.metaSearch->cancelMetaSearch(searchID);
    };

    backend.continueMetaSearch = [&list](uint32 searchID) {
        if (theApp.metaSearch)
            theApp.metaSearch->continueMetaSearch(searchID);
        else
            QTimer::singleShot(0, &list, [&list, searchID] { list.queue().onMetaSearchFinished(searchID); });
    };

    // MFC CSearchResultsWnd::SearchMore — srchybrid/SearchResultsWnd.cpp:1273-1289.
    backend.continueServerSearch = [&list](uint32 searchID, const SearchParams& params) {
        if (!theApp.serverConnect || !theApp.serverConnect->isConnected())
            return false;
        list.beginSearch(searchID, resultTypeFilter(params.fileType), /*ed2k*/ true, params.expression);
        auto pkt = std::make_unique<Packet>(OP_QUERY_MORE_RESULT, 0);
        pkt->prot = OP_EDONKEYPROT;
        logServerVerbose(QStringLiteral(">>> OP_QUERY_MORE_RESULT for search %1").arg(searchID));
        if (theApp.serverConnect->sendPacket(std::move(pkt)))
            return true;
        list.releaseEd2kRouting(searchID);
        return false;
    };

    backend.nowMs = [] { return static_cast<qint64>(getTickCount()); };
    return backend;
}

SearchStartResult startSearch(SearchList& list, SearchParams params)
{
    const SearchQueue::Result queued = list.queue().enqueue(params);
    if (!queued.ok)
        return refused(queued.error);

    SearchStartResult result;
    result.ok = true;
    result.searchID = queued.status.searchID;
    result.state = queued.status.state;
    result.reason = queued.status.reason;
    result.started = queued.status.state == SearchRunState::Running;
    result.type = queued.status.type;
    result.keyword = queued.status.keyword;
    result.primaryKeyword = queued.status.primaryKeyword;
    return result;
}

void stopSearch(SearchList& list, uint32 searchID)
{
    kad::SearchManager::stopSearch(searchID, false);
    if (theApp.globalSearch)
        theApp.globalSearch->cancelSearch(searchID);
    list.queue().stop(searchID);
}

bool searchMore(SearchList& list, uint32 searchID)
{
    return list.queue().more(searchID);
}

bool removeSearch(SearchList& list, uint32 searchID)
{
    stopSearch(list, searchID);
    const bool known = list.hasSearch(searchID);
    list.queue().remove(searchID);
    list.removeResults(searchID);
    return known;
}

void clearAllSearches(SearchList& list)
{
    kad::SearchManager::stopAllSearches();
    if (theApp.globalSearch)
        theApp.globalSearch->cancel();
    list.queue().clear();
    list.clear();
}

} // namespace eMule
