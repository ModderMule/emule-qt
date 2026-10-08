#include "kademlia/KadNodeCensus.h"

#include "geo/IP2Country.h"

namespace eMule::kad {

namespace {

constexpr quint32 kMagic = 0x4B4E4331;  // "KNC1"

struct Halves {
    uint64 lo;
    uint64 hi;
};

Halves halvesOf(const UInt128& id)
{
    return {(uint64{id.get32BitChunk(2)} << 32) | id.get32BitChunk(3),
            (uint64{id.get32BitChunk(0)} << 32) | id.get32BitChunk(1)};
}

} // namespace

KadNodeCensus::KadNodeCensus(const QString& dir)
    : CountryCensus(dir, QStringLiteral("kadcensus"), kMagic)
{
}

void KadNodeCensus::noteContacted(const UInt128& id, const Address& addr)
{
    noteContacted(id, countryCodeOf(addr));
}

void KadNodeCensus::noteContacted(const UInt128& id, const QString& cc)
{
    const Halves h = halvesOf(id);
    addByCountry(h.lo, h.hi, cc);
}

void KadNodeCensus::noteListed(const UInt128& id)
{
    const Halves h = halvesOf(id);
    addSecondary(h.lo, h.hi);
}

} // namespace eMule::kad
