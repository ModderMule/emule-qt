#pragma once

/// @file MetaSearchRunner.h
/// @brief Who runs a Usenet / torrent search on a server's Meta API.
///
/// The search queue lives in the core, the Meta API accounts in the daemon; the
/// daemon registers its runner as theApp.metaSearch.

#include "search/MetaSearchRequest.h"

#include <vector>

namespace eMule {

class MetaSearchRunner {
public:
    virtual ~MetaSearchRunner() = default;

    /// Ask @p candidates in turn until one answers, add its rows to the search
    /// list under @p searchID and report to SearchQueue::onMetaSearchFinished.
    /// Never reports before returning.
    virtual void startMetaSearch(uint32 searchID, SearchType type, const SearchParams& params,
                                 const enodemeta::pb::SearchRequest& request,
                                 std::vector<MetaSearchCandidate> candidates) = 0;

    /// Fetch the next page of a search that reported more to come, from the
    /// server that answered it. Reports as startMetaSearch does, never before
    /// returning.
    virtual void continueMetaSearch(uint32 searchID) = 0;

    /// Stop asking and forget a kept page; nothing is reported for @p searchID any more.
    virtual void cancelMetaSearch(uint32 searchID) = 0;
};

} // namespace eMule
