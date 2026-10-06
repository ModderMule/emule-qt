#pragma once

/// @file SearchStarter.h
/// @brief The one way a search is started, stopped or removed — for IPC, REST and
///        the web interface alike.

#include "search/SearchParams.h"

#include <QString>

namespace eMule {

class SearchList;

struct SearchStartResult {
    bool ok = false;            ///< false: refused, @c error says why, nothing was created
    QString error;
    uint32 searchID = 0;
    bool started = false;       ///< a request actually left (or a sweep / Kad lookup runs)
    SearchType type = SearchType::Ed2kServer;   ///< the network used, Automatic resolved
    QString keyword;            ///< Kad: the keyword searched when it is not the first one
    QString primaryKeyword;
};

/// Connectivity that decides which network an Automatic search uses.
[[nodiscard]] AutoSearchState gatherAutoSearchState();

/// Create the search in @p list and send its request(s).
[[nodiscard]] SearchStartResult startSearch(SearchList& list, SearchParams params);

/// Stop asking; results stay.
void stopSearch(uint32 searchID);

/// Stop and drop the results. False when @p list has no such search.
bool removeSearch(SearchList& list, uint32 searchID);

/// Stop everything and empty @p list.
void clearAllSearches(SearchList& list);

} // namespace eMule
