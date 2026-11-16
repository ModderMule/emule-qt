#pragma once

/// @file ListenAddressSet.h
/// @brief One bound socket per listen address, all on one port.
///
/// A whole selected interface binds each of its addresses instead of the wildcard (#190),
/// so another program can hold the same port on another interface. The set follows
/// address changes (IPv6 privacy rotation, RA renumber, a second IPv4) without closing the
/// sockets that stay. Base of the TCP listener (ListenSocket) and UdpSocketSet.

#include "utils/Types.h"

#include <QHostAddress>
#include <QList>
#include <QString>

namespace eMule {

class ListenAddressSet {
public:
    virtual ~ListenAddressSet() = default;

    /// Addresses with a bound socket, in the order asked for.
    [[nodiscard]] const QList<QHostAddress>& boundAddresses() const { return m_bound; }
    /// Addresses not bindable yet (IPv6 DAD unfinished) or held by another program;
    /// syncTo() tries them again.
    [[nodiscard]] const QList<QHostAddress>& pendingAddresses() const { return m_pending; }
    /// Between openAll() and closeAll(), bound or waiting for an address.
    [[nodiscard]] bool isSetActive() const { return m_active; }
    /// The port of every socket; 0 until one is bound to an OS-picked port.
    [[nodiscard]] uint16 setPort() const { return m_port; }

protected:
    enum class BindResult : uint8 {
        Ok,
        Later,      ///< address not usable yet: try again on the next sync
        Failed
    };

    /// Bind (and pin) one socket to @p address:@p port; @p error gets why not.
    virtual BindResult bindOne(const QHostAddress& address, uint16 port, QString& error) = 0;
    /// Close the socket of @p address.
    virtual void closeOne(const QHostAddress& address) = 0;
    /// The port a socket of @p address got (bound to port 0).
    [[nodiscard]] virtual uint16 portOf(const QHostAddress& address) const = 0;
    /// "TCP" / "client UDP" ... for the log.
    [[nodiscard]] virtual QString setName() const = 0;

    /// Bind every one of @p wanted on @p port (0: the first gets one, the rest follow it).
    /// False when a bind failed (nothing left bound) or none was bindable yet (the set
    /// stays active and syncTo() binds them when they come).
    bool openAll(const QList<QHostAddress>& wanted, uint16 port, QString& error);
    /// Close what left @p wanted and bind what came, on the set's port. True when the
    /// bound addresses changed. No-op while not active.
    bool syncTo(const QList<QHostAddress>& wanted);
    void closeAll();

private:
    QList<QHostAddress> m_bound;
    QList<QHostAddress> m_pending;
    QList<QHostAddress> m_reported;   ///< pending ones already logged
    uint16 m_port = 0;
    bool m_active = false;
};

} // namespace eMule
