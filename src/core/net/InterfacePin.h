#pragma once

/// @file InterfacePin.h
/// @brief Confine a socket to the bound network interface.
///
/// A bound source address does not choose the outgoing interface: the routing table
/// does. The pin makes the kernel refuse any other interface, so a socket cannot leave
/// through the default route when the selected interface (a VPN tunnel) goes away.
/// Qt has no API for it; this is one setsockopt on the native descriptor after bind().

#include "net/BindAddress.h"

#include <QtGlobal>

class QAbstractSocket;

namespace eMule::InterfacePin {

/// Pin @p fd to interface @p index (@p name is the Linux fallback). The socket family
/// is read from the descriptor. False when the option could not be set; @p error then
/// gets the OS error (errno / WSA code).
[[nodiscard]] bool pinToInterface(qintptr fd, int index, const QString& name, int* error = nullptr);

/// Try the pin on a throwaway IPv4 and IPv6 socket. Windows refuses the IPv6 pin on an
/// adapter without IPv6 (VPN adapters), which must not take IPv4 down with it.
[[nodiscard]] BindAddress::PinProbe probe(int index, const QString& name);

/// Pin @p fd to the bound interface. True when nothing is selected (nothing to do) or
/// the pin was set; false when blocked or the pin failed — the socket must not be used.
[[nodiscard]] bool pin(qintptr fd);

/// Bind @p socket before a connect by host name and pin it. No-op when
/// nothing is selected or the socket has a proxy. False = do not connect.
[[nodiscard]] bool prepareOutgoing(QAbstractSocket& socket, bool proxied);

} // namespace eMule::InterfacePin
