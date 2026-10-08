#pragma once

/// @file ListenConflict.h
/// @brief Finds local addresses another program already listens on at a port.
///
/// macOS and Windows let a wildcard listener start although another program holds
/// the same port on a specific address (127.0.0.1, say). Connections to that
/// address then go to the other program, silently. Ask before listening.

#include <QHostAddress>
#include <QList>

namespace eMule {

/// Local addresses another program listens on at `port`. Empty unless `listenAddr`
/// is a wildcard and `port` is not 0. Call before the own listen().
[[nodiscard]] QList<QHostAddress> heldLocalAddresses(const QHostAddress& listenAddr, quint16 port);

/// "127.0.0.1, ::1" for a log line.
[[nodiscard]] QString addressListText(const QList<QHostAddress>& addresses);

} // namespace eMule
