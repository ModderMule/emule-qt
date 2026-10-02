#pragma once

/// @file IPv6SourcePin.h
/// @brief Send outgoing IPv6 from the stable address instead of the RFC 4941 temporary one.
///
/// macOS and Windows prefer the rotating temporary address as source, so servers and
/// peers see a different IPv6 than the one we advertise. Picking the source per socket
/// needs no root/admin (unlike disabling privacy addresses system-wide): TCP binds the
/// stable address before connecting, UDP sets it as the datagram sender (IPV6_PKTINFO /
/// WSASendMsg under Qt). Off when the "Use IPv6 privacy address" pref is on, behind a
/// proxy, or when no pin address is known.

#include "net/Address.h"

class QAbstractSocket;
class QNetworkDatagram;

namespace eMule::IPv6SourcePin {

/// Address to pin (the selected stable address, or publicIPv6Override); null clears.
/// Thread-safe: the client UDP socket sends from its own thread.
void setPinAddress(const Address& addr);
[[nodiscard]] Address pinAddress();

/// The source to use towards @p dest, or null to let the OS choose.
[[nodiscard]] Address sourceFor(const Address& dest);

/// Bind @p socket to the pinned source before connectToHost() to @p dest. No-op when
/// no pin applies; a failed bind is logged and the OS choice is kept.
void bindForConnect(QAbstractSocket& socket, const Address& dest);

/// Set the pinned source as the sender of an outgoing datagram to @p dest.
void applyToDatagram(QNetworkDatagram& datagram, const Address& dest);

} // namespace eMule::IPv6SourcePin
