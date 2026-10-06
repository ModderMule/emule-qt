#pragma once

/// @file HttpCacheReach.h
/// @brief Who can reach a cache host — the local-address rule, both directions.
///
/// A chunk URL on a loopback or LAN host is only any use to a peer that is local
/// too, and only when LAN mode is on (filterLANIPs off). The uploader applies this
/// before publishing and offering, the downloader before fetching.

#include "net/Address.h"

#include <QString>

namespace eMule {

/// Ordered by how few peers can get there; Unknown sorts last so it wins a merge.
enum class CacheReach : uint8 {
    Public,     ///< routable — anybody
    Lan,        ///< private / link-local / ULA — LAN peers only
    Loopback,   ///< this machine only
    Unknown     ///< a name not resolved yet, or nothing usable — nobody, for now
};

[[nodiscard]] CacheReach classifyCacheAddress(const Address& addr);

/// Classify a URL host without a lookup: an IP literal or "localhost".
/// Unknown for any other name.
[[nodiscard]] CacheReach classifyCacheHostLiteral(const QString& host);

/// The narrower of the two — a name with one LAN address is a LAN name.
[[nodiscard]] CacheReach strictestCacheReach(CacheReach a, CacheReach b);

/// May @p peer be pointed at (or point us at) a host of this reach?
/// @param lanMode  !thePrefs.filterLANIPs()
[[nodiscard]] bool cacheReachAllowsPeer(CacheReach reach, const Address& peer, bool lanMode);

} // namespace eMule
