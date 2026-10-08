#include "client/ClientCensus.h"

#include "geo/IP2Country.h"

#include <QtEndian>

namespace eMule {

namespace {

constexpr quint32 kMagic = 0x434C4331;  // "CLC1"

struct Halves {
    uint64 lo;
    uint64 hi;
    [[nodiscard]] bool isNull() const { return lo == 0 && hi == 0; }
};

Halves halvesOf(const uint8* userHash)
{
    if (!userHash)
        return {0, 0};
    return {qFromLittleEndian<quint64>(userHash), qFromLittleEndian<quint64>(userHash + 8)};
}

} // namespace

ClientCensus::ClientCensus(const QString& dir)
    : CountryCensus(dir, QStringLiteral("clientcensus"), kMagic)
{
}

void ClientCensus::noteSeen(const uint8* userHash, const Address& addr)
{
    noteSeen(userHash, countryCodeOf(addr));
}

void ClientCensus::noteSeen(const uint8* userHash, const QString& cc)
{
    if (const Halves h = halvesOf(userHash); !h.isNull())
        addByCountry(h.lo, h.hi, cc);
}

void ClientCensus::noteIdentified(const uint8* userHash)
{
    if (const Halves h = halvesOf(userHash); !h.isNull())
        addSecondary(h.lo, h.hi);
}

} // namespace eMule
