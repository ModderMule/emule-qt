#pragma once

/// @file CdnRanges.h
/// @brief Addresses of CDNs and DDoS shields (Cloudflare, Akamai, Fastly, DDoS-Guard, ...):
///        such an address says nothing about where a site's server stands, so a flag
///        from it would lie. By AS, from scripts/update-cdn-ranges.py (CdnRanges.inc).

#include <QHostAddress>

namespace eMule {

/// @p address (v4, v6 or v4-mapped) is in a CDN's range. Any thread.
[[nodiscard]] bool isCdnAddress(const QHostAddress& address);

} // namespace eMule
