#pragma once

/// @file KadSearchManager.h
/// @brief Kademlia search lifecycle management (ported from kademlia/kademlia/SearchManager.h).

#include "kademlia/KadSearchDefs.h"
#include "kademlia/KadTypes.h"
#include "kademlia/KadUInt128.h"
#include "utils/Types.h"

#include <QString>

#include <cstdint>

namespace eMule::kad {

class KadClientSearcher;
class RoutingZone;
class Search;

/// Outcome of picking the DHT target keyword for a Kad keyword search.
enum class KeywordStatus {
    Ok,         ///< A free keyword was found (see KeywordSelection::keyword).
    TooShort,   ///< No word of the expression meets kMinKadKeywordBytes.
    AllActive   ///< Every usable keyword is already being searched for.
};

/// Which keyword of a search expression a Kad search will be indexed under.
///
/// A Kad keyword search targets exactly one keyword hash, so a second search
/// whose primary keyword is already active would collide. When the expression
/// has further words long enough to be keywords, the next free one is used
/// instead of refusing the search.
struct KeywordSelection {
    KeywordStatus status = KeywordStatus::TooShort;
    QString keyword;            ///< Chosen DHT target keyword (empty unless Ok).
    QString primaryKeyword;     ///< First word of the expression (empty on TooShort).
    bool isFallback = false;    ///< True when keyword != primaryKeyword.
};

/// Manages the lifecycle of all Kademlia searches.
class SearchManager {
public:
    static bool isSearching(uint32 searchID);
    static void stopSearch(uint32 searchID, bool delayDelete);
    static void stopAllSearches();

    static Search* prepareLookup(SearchType type, bool start, const UInt128& id, const QString& guiName = QString());

    /// Pick the keyword @p expression should be searched under: its first word
    /// that is not already the target of a running search.
    static KeywordSelection selectKeyword(const QString& expression);

    /// Create (but do not start) a keyword search for @p expression.
    /// @param targetKeyword  Keyword to hash as the DHT target; when empty the
    ///                       keyword is picked via selectKeyword(). Callers that
    ///                       build a search-terms blob must pass the same keyword
    ///                       they built it against.
    static Search* prepareFindKeywords(const QString& expression,
                                       uint32 searchTermsSize,
                                       const uint8* searchTermsData,
                                       const QString& targetKeyword = QString());
    static bool startSearch(Search* search);

    static void processResponse(const UInt128& target, uint32 fromIP, uint16 fromPort,
                                ContactArray& results);
    static uint8 getExpectedResponseContactCount(const UInt128& target);
    static void processResult(const UInt128& target, const UInt128& answer,
                              TagList& info, uint32 fromIP, uint16 fromPort);
    static void processPublishResult(const UInt128& target, uint8 load, bool loadResponse);
    static void updateStats();
    static bool alreadySearchingFor(const UInt128& target);

    static void cancelNodeFWCheckUDPSearch();
    static bool findNodeFWCheckUDP();
    static bool isFWCheckUDPSearch(const UInt128& target);
    /// True while a NodeFwCheckUDP lookup is registered. Unlike
    /// isFWCheckUDPSearch() this is not keyed on the (random) target, so it
    /// answers "is a firewall lookup running at all?".
    [[nodiscard]] static bool isNodeFWCheckUDPSearchActive();
    static const SearchMap& getSearches() { return s_searches; }
    static uint32 getTotalResponsesReceived() { return s_totalResponsesReceived; }

private:
    friend class RoutingZone;
    friend class Kademlia;

    static void findNode(const UInt128& id, bool complete);
    static bool findNodeSpecial(const UInt128& id, KadClientSearcher* requester);
    static void cancelNodeSpecial(const KadClientSearcher* requester);
    static void jumpStart();
    static void notifySearchesChanged();

    static uint32 s_totalResponsesReceived;
    static SearchMap s_searches;
};

} // namespace eMule::kad
