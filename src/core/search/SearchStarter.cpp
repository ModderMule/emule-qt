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
#include "search/SearchExprParser.h"
#include "search/SearchList.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"

#include <QCoreApplication>

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

SearchStartResult startKadSearch(SearchList& list, const SearchParams& params)
{
    auto* kadInst = kad::Kademlia::instance();
    if (!kadInst || !kadInst->isConnected()) {
        return refused(QCoreApplication::translate("eMule::IpcClientHandler", "Kad is not connected.\n\nWait until Kad is connected "
                            "before starting a Kad search."));
    }

    const auto alreadySearching = [](const QString& keyword) {
        return refused(QCoreApplication::translate("eMule::IpcClientHandler", "There is already a Kad search ongoing for the keyword \"%1\".\n\n"
                            "To search again for that keyword, either wait until this keyword "
                            "search is finished or close the according search results pane.")
                           .arg(keyword));
    };

    // A Kad search is indexed under a single keyword. When the expression's first
    // keyword is already the target of a running search, fall back to the next
    // word long enough to be a keyword instead of refusing.
    const kad::KeywordSelection sel = kad::SearchManager::selectKeyword(params.expression);
    if (sel.status == kad::KeywordStatus::TooShort) {
        return refused(QCoreApplication::translate("eMule::IpcClientHandler", "Keyword too short.\n\nThe keyword(s) used in a Kad search "
                            "expression must have a minimum length of 3 characters."));
    }
    if (sel.status == kad::KeywordStatus::AllActive)
        return alreadySearching(sel.primaryKeyword);
    if (sel.isFallback) {
        logInfo(QCoreApplication::translate("eMule::IpcClientHandler", "Kad: \"%1\" is already being searched — using \"%2\" as the search "
                     "target for \"%3\"")
                    .arg(sel.primaryKeyword, sel.keyword, params.expression));
    }

    // The AND/OR/NOT tree + filters that travel with KADEMLIA2_SEARCH_KEY_REQ.
    // Without it a multi-word search degenerates to a bare single-keyword query.
    const QByteArray searchTerms = buildSearchTermsPayload(params, sel.keyword);

    auto* kadSearch = kad::SearchManager::prepareFindKeywords(
        params.expression,
        static_cast<uint32>(searchTerms.size()),
        searchTerms.isEmpty() ? nullptr
                              : reinterpret_cast<const uint8*>(searchTerms.constData()),
        sel.keyword);
    if (!kadSearch)
        return alreadySearching(sel.keyword);

    // The Kad search brings its own id.
    const uint32 searchID = kadSearch->getSearchID();
    list.newSearch(resultTypeFilter(params.fileType), params, searchID);
    if (!kad::SearchManager::startSearch(kadSearch)) {
        // Target was taken between selection and start — drop the half-built search.
        delete kadSearch;
        list.removeResults(searchID);
        return alreadySearching(sel.keyword);
    }

    SearchStartResult result;
    result.ok = true;
    result.started = true;
    result.searchID = searchID;
    result.type = SearchType::Kademlia;
    if (sel.isFallback) {
        result.keyword = sel.keyword;
        result.primaryKeyword = sel.primaryKeyword;
    }
    return result;
}

SearchStartResult startEd2kSearch(SearchList& list, const SearchParams& params)
{
    const bool connected = theApp.serverConnect && theApp.serverConnect->isConnected();
    const Server* current = connected ? theApp.serverConnect->currentServer() : nullptr;

    // A size above 4 GiB goes out as a 64-bit term only to a server that reads one
    // (MFC DoNewEd2kSearch, SearchResultsWnd.cpp:1218-1219). With no server there is
    // only the sweep, which skips the servers that cannot.
    const bool supports64Bit = connected ? (current && current->supportsLargeFilesTCP()) : true;
    bool uses64Bit = false;
    const QByteArray payload = buildSearchTermsPayload(params, {}, supports64Bit, &uses64Bit);

    SearchStartResult result;
    result.ok = true;
    result.type = params.type;

    // Nothing can be sent: do not take the server-answer routing away from a
    // search that is still collecting.
    const bool canSweep = params.type == SearchType::Ed2kGlobal && theApp.globalSearch;
    if (payload.isEmpty() || (!connected && !canSweep)) {
        logServerVerbose(payload.isEmpty()
            ? QStringLiteral("Search \"%1\" produced an empty request payload").arg(params.expression)
            : QStringLiteral("TCP server search skipped for \"%1\" — not connected to a server")
                  .arg(params.expression));
        // The id still exists, so the caller has something to show and to close.
        result.searchID = list.newSearch(resultTypeFilter(params.fileType), params, 0,
                                         /*takeEd2kRouting*/ false);
        return result;
    }

    // One ED2K search at a time — a new one supersedes whatever sweep is still
    // running. Not for Kad: MFC cancels from DoNewEd2kSearch only
    // (srchybrid/SearchResultsWnd.cpp:1225).
    if (theApp.globalSearch)
        theApp.globalSearch->cancel();

    result.searchID = list.newSearch(resultTypeFilter(params.fileType), params);

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
        theApp.serverConnect->sendPacket(std::move(pkt));
        localRequestSent = true;
        result.started = true;
    }

    if (canSweep) {
        // One server per 750 ms, and only after the local server has answered (or
        // timed out). Without a local request the sweep starts right away — that is
        // how a Kad-only session still gets a global search.
        theApp.globalSearch->start(result.searchID, payload, uses64Bit,
                                   /*awaitLocalAnswer*/ localRequestSent);
        result.started = true;
    }
    return result;
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

SearchStartResult startSearch(SearchList& list, SearchParams params)
{
    // "Automatic" is a chooser, not a network: resolve it to exactly one before
    // anything is created or sent. MFC: CSearchResultsWnd::StartNewSearch —
    // srchybrid/SearchResultsWnd.cpp:1134-1165.
    if (params.type == SearchType::Automatic) {
        const auto resolved = resolveAutomaticSearchType(gatherAutoSearchState());
        if (!resolved)
            return refused(QCoreApplication::translate("eMule::IpcClientHandler", "You are not connected to a server or the Kad network!"));
        params.type = *resolved;
        logInfo(QCoreApplication::translate("eMule::IpcClientHandler", "Automatic search method resolved to %1")
                    .arg(params.type == SearchType::Kademlia ? QCoreApplication::translate("eMule::IpcClientHandler", "Kad") : QCoreApplication::translate("eMule::IpcClientHandler", "eD2K server")));
    }

    // An indexer search has its own entry point; running a server search in its
    // place would be a wrong answer, which is worse than a refusal.
    if (params.type == SearchType::UsenetIndexer)
        return refused(QCoreApplication::translate("eMule::IpcClientHandler", "Indexer searches use StartIndexerSearch, not StartSearch."));

    if (params.type == SearchType::Kademlia)
        return startKadSearch(list, params);
    return startEd2kSearch(list, params);
}

void stopSearch(uint32 searchID)
{
    kad::SearchManager::stopSearch(searchID, false);
    if (theApp.globalSearch)
        theApp.globalSearch->cancelSearch(searchID);
}

bool removeSearch(SearchList& list, uint32 searchID)
{
    stopSearch(searchID);
    const bool known = list.hasSearch(searchID);
    list.removeResults(searchID);
    return known;
}

void clearAllSearches(SearchList& list)
{
    kad::SearchManager::stopAllSearches();
    if (theApp.globalSearch)
        theApp.globalSearch->cancel();
    list.clear();
}

} // namespace eMule
