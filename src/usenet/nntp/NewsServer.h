#pragma once

/// @file NewsServer.h
/// @brief The provider record, under the names the protocol layer uses.
///
/// The type itself lives in core/prefs/NewsServer.h: Preferences owns the
/// persisted shape, and core must not depend on this module. Aliasing here
/// rather than duplicating the fourteen fields keeps one definition, while
/// letting nntp/ code say TlsMode instead of NntpTlsMode where nothing else
/// could be meant.

#include "prefs/NewsServer.h"

namespace eMule::usenet {

using eMule::NewsServer;
using TlsMode = eMule::NntpTlsMode;
using CertVerification = eMule::NntpCertVerification;
using QuotaKind = eMule::NntpQuotaKind;
using eMule::kDefaultNntpPort;
using eMule::kDefaultNntpTlsPort;

} // namespace eMule::usenet
