#pragma once

/// @file BindAddress.h
/// @brief The "bind address" preference applied to every socket the client opens.
///
/// The value selects a local interface: an IPv4/IPv6 literal, an interface name, or a
/// subnet ("10.64.0.0/10" = the interface holding an address in it). A literal binds that
/// address and runs that family only; a name or subnet binds the wildcard and relies on
/// the interface pin (InterfacePin.h), so both families work on that interface.
/// A selection that does not resolve fails closed — no socket is opened — because falling
/// back to "any" would defeat the point for someone pinning a VPN.
/// Resolved on a preference change and by refresh(); never per socket.

#include "net/Address.h"

#include <QHostAddress>
#include <QList>
#include <QString>

#include <functional>
#include <optional>

namespace eMule::BindAddress {

/// One local interface that is up, as the resolver sees it.
struct LocalInterface {
    QString name;                   ///< "en0" / "tun0" / Windows adapter id
    QString friendlyName;           ///< Windows "Ethernet 2"; elsewhere same as name
    int index = 0;                  ///< OS interface index
    QList<QHostAddress> addresses;  ///< no scope ids

    friend bool operator==(const LocalInterface&, const LocalInterface&) = default;
};

enum class State {
    Unrestricted,   ///< nothing selected, nothing pinned
    Bound,          ///< selection resolved to an interface
    Blocked         ///< selection does not resolve — no socket may be opened
};

struct Resolution {
    State state = State::Unrestricted;
    QString selector;               ///< trimmed preference text
    int index = 0;
    QString name;
    QString friendlyName;
    QHostAddress literal;           ///< set when the selector is an address
    QHostAddress v4;                ///< interface's first IPv4, may be null
    bool hasGlobalIPv6 = false;
    QString reason;                 ///< why Blocked

    [[nodiscard]] bool wholeInterface() const { return state == State::Bound && literal.isNull(); }
    friend bool operator==(const Resolution&, const Resolution&) = default;
};

/// Pure: resolve @p selector against @p interfaces.
[[nodiscard]] Resolution resolve(const QString& selector, const QList<LocalInterface>& interfaces);

/// Interfaces that are up and running, from the OS (or the test source).
[[nodiscard]] QList<LocalInterface> localInterfaces();

/// Test seam: replace the OS enumeration; an empty function restores it.
void setInterfaceSource(std::function<QList<LocalInterface>()> source);

/// The cached resolution; re-resolved when the preference text changed.
[[nodiscard]] Resolution current();

/// Re-enumerate and re-resolve. True when the resolution changed.
bool refresh();

/// False while the selection does not resolve: nothing may be sent, pinned or not.
[[nodiscard]] bool outboundAllowed();

/// True when the preference holds anything at all, usable or not.
[[nodiscard]] bool isConfigured();

/// What a socket binds to: Any when nothing is configured or a whole interface is
/// selected, the address for a literal, nullopt when blocked (do not open the socket).
[[nodiscard]] std::optional<QHostAddress> listenAddress();

/// What an outgoing socket towards @p dest binds to before it is pinned; nullopt when
/// blocked or @p dest is of the family a literal excludes.
[[nodiscard]] std::optional<QHostAddress> sourceFor(const Address& dest);

/// False when @p dest is of the family a literal excludes, or the selection is blocked.
[[nodiscard]] bool canReach(const Address& dest);

/// The bound IPv4 address, else empty — for the IPv4-only consumers
/// (ED2K local IP, UPnP discovery).
[[nodiscard]] QString ipv4Literal();

/// True when the selection leaves no IPv6 to use.
[[nodiscard]] bool isIPv4Only();

/// True when @p interfaceName (name, friendly name or index) is the bound interface, or
/// nothing is selected.
[[nodiscard]] bool isBoundInterface(const QString& interfaceName);

} // namespace eMule::BindAddress
