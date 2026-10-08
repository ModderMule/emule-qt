#pragma once

/// @file KadStats.h
/// @brief Where the Kad code counts: the session KadCounters block and the node census.
///
/// Both are optional — unit tests run Kad without a Statistics or a census
/// installed, and counting is then a no-op.

#include "app/AppContext.h"
#include "kademlia/KadNodeCensus.h"
#include "stats/Statistics.h"

namespace eMule::kad {

inline void countKad(uint64 KadCounters::* field, uint64 n = 1)
{
    if (auto* stats = theApp.statistics)
        stats->kadSession().*field += n;
}

inline void raiseKad(uint64 KadCounters::* field, uint64 value)
{
    if (auto* stats = theApp.statistics)
        raiseCounter(stats->kadSession().*field, value);
}

} // namespace eMule::kad
