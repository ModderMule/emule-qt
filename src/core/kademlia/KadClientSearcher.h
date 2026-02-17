#pragma once

/// @file KadClientSearcher.h
/// @brief Abstract callback interface for Kademlia client searches.
///
/// Ported from kademlia/utils/KadClientSearcher.h.

#include "utils/Types.h"

#include <cstdint>

namespace eMule::kad {

/// Result status for Kademlia client searches.
enum class KadClientSearchResult {
    Succeeded,
    NotFound,
    Timeout
};

/// Interface for non-Kad classes that want to perform Kademlia client searches.
class KadClientSearcher {
public:
    virtual void kadSearchNodeIDByIPResult(KadClientSearchResult status, const uchar* nodeID) = 0;
    virtual void kadSearchIPByNodeIDResult(KadClientSearchResult status, uint32 ip, uint16 port) = 0;

protected:
    virtual ~KadClientSearcher() = default;
};

} // namespace eMule::kad
