#pragma once

/// @file KadNodeCensus.h
/// @brief How many different Kad nodes we have seen, by country.
///
/// Two tiers, because a node's word is not evidence:
///  - contacted: the node sent us a HELLO or answered a lookup we addressed
///    to it, so the ID goes with a real packet source address. Only this tier
///    is split by country.
///  - listed: the ID appeared in another node's lookup answer. One node can
///    list anything, so this is a single number and no countries.
///
/// Counting and persistence are CountryCensus (kadcensus.dat).
/// Owned by CoreSession, not Kademlia: Kad stops and restarts within a session.

#include "kademlia/KadUInt128.h"
#include "net/Address.h"
#include "stats/CountryCensus.h"

namespace eMule::kad {

class KadNodeCensus : public CountryCensus {
public:
    /// @p dir holds kadcensus.dat and its backup; empty keeps it in memory only.
    explicit KadNodeCensus(const QString& dir = {});

    void noteContacted(const UInt128& id, const Address& addr);
    void noteContacted(const UInt128& id, const QString& cc);
    void noteListed(const UInt128& id);

    [[nodiscard]] uint64 contacted(Scope scope) const { return total(scope); }
    [[nodiscard]] uint64 listed(Scope scope) const { return secondary(scope); }
};

} // namespace eMule::kad
