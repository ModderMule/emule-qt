#pragma once

/// @file BindAddress.h
/// @brief The "bind address" preference applied to every P2P socket.
///
/// One literal, IPv4 or IPv6. While it is set the client runs on that address and that
/// family only: listeners bind to it, outgoing TCP leaves from it, and the other family
/// is not dialled at all. An unusable literal fails closed — no socket is opened —
/// because falling back to "any" would defeat the point for someone pinning a VPN.
/// Read on use, so it follows the preference; listeners pick it up on their next bind.

#include "net/Address.h"

#include <QHostAddress>
#include <QString>

#include <optional>

namespace eMule::BindAddress {

/// True when the preference holds anything at all, usable or not.
[[nodiscard]] bool isConfigured();

/// What a P2P socket binds to: Any when nothing is configured, the address when it is,
/// nullopt when the configured value cannot be used (the caller must not open the socket).
[[nodiscard]] std::optional<QHostAddress> listenAddress();

/// False when a bind address is configured and @p dest is of the other family, or the
/// configured value is unusable. Always true when nothing is configured.
[[nodiscard]] bool canReach(const Address& dest);

/// The configured address when it is IPv4, else empty — for the IPv4-only consumers
/// (ED2K local IP, UPnP discovery).
[[nodiscard]] QString ipv4Literal();

/// True when bound to an IPv4 address, i.e. IPv6 is off.
[[nodiscard]] bool isIPv4Only();

} // namespace eMule::BindAddress
