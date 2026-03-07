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

/// Manages the lifecycle of all Kademlia searches.
class SearchManager {
public:
    static bool isSearching(uint32 searchID);
    static void stopSearch(uint32 searchID, bool delayDelete);
    static void stopAllSearches();

    static Search* prepareLookup(SearchType type, bool start, const UInt128& id);
    static Search* prepareFindKeywords(const QString& keyword,
                                       uint32 searchTermsSize,
                                       const uint8* searchTermsData);
    static bool startSearch(Search* search);

    static void processResponse(const UInt128& target, uint32 fromIP, uint16 fromPort,
                                ContactArray& results);
    static uint8 getExpectedResponseContactCount(const UInt128& target);
    static void processResult(const UInt128& target, const UInt128& answer,
                              TagList& info, uint32 fromIP, uint16 fromPort);
    static void processPublishResult(const UInt128& target, uint8 load, bool loadResponse);
    static void updateStats();
    static bool alreadySearchingFor(const UInt128& target);
    static QString findActiveKeyword(const QString& expression);

    static void cancelNodeFWCheckUDPSearch();
    static bool findNodeFWCheckUDP();
    static bool isFWCheckUDPSearch(const UInt128& target);
    static void setNextSearchID(uint32 nextID);

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

    static uint32 s_nextID;
    static uint32 s_totalResponsesReceived;
    static SearchMap s_searches;
};

} // namespace eMule::kad
