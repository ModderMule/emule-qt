/// @file Ed3kKeyFile.cpp
/// @brief Owner-only access for secret files — implementation.

#include "Ed3kKeyFile.h"

#include "utils/OwnerOnly.h"

namespace eMule::ed3k {

bool ownerOnly(const QString& path, QString* error)
{
    return makeOwnerOnly(path, error);   // shared with core (enodemeta tokens)
}

} // namespace eMule::ed3k
