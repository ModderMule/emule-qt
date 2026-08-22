#pragma once

/// @file PeerVetting.h
/// @brief Shared "is this peer address usable at all" check.
///
/// Extracted from DownloadQueue::vetPeerAddress() so both restore paths — Save/Load
/// Sources on the download side and the upload queue store on the upload side — run one
/// copy of the rules against untrusted on-disk input rather than two that can drift.
///
/// Dependencies are passed in rather than read from theApp: DownloadQueue holds injected
/// IPFilter/ClientList members precisely so its tests can isolate it, and reaching for the
/// globals here would take that away.

#include "net/Address.h"

namespace eMule {

class ClientList;
class IPFilter;

/// @return @p addr when it is routable, not IP-filtered and not banned; a null Address
///         otherwise. A null input is rejected, so callers can pass an optional address
///         straight through. Either dependency may be null (the matching check is skipped).
[[nodiscard]] Address vetPeerAddress(const Address& addr,
                                     const IPFilter* filter,
                                     const ClientList* clients);

} // namespace eMule
