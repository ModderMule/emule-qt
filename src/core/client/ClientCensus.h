#pragma once

/// @file ClientCensus.h
/// @brief How many different eD2K clients we have seen, by country.
///
/// Counted by user hash, the only identity that survives an IP change:
///  - seen: the hash arrived in a hello on a TCP connection, so the address
///    is real. Split by country.
///  - identified: the client proved the hash with SecureIdent. A spoof-proof
///    lower bound of "seen"; one number.
///
/// Counting and persistence are CountryCensus (clientcensus.dat).
/// Owned by CoreSession. Daemon thread only.

#include "net/Address.h"
#include "stats/CountryCensus.h"

namespace eMule {

class ClientCensus : public CountryCensus {
public:
    /// @p dir holds clientcensus.dat and its backup; empty keeps it in memory only.
    explicit ClientCensus(const QString& dir = {});

    // @p userHash is 16 bytes; the null hash is not a client.
    void noteSeen(const uint8* userHash, const Address& addr);
    void noteSeen(const uint8* userHash, const QString& cc);
    void noteIdentified(const uint8* userHash);

    [[nodiscard]] uint64 seen(Scope scope) const { return total(scope); }
    [[nodiscard]] uint64 identified(Scope scope) const { return secondary(scope); }
};

} // namespace eMule
