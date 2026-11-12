#pragma once

/// @file OwnAddress.h
/// @brief Is an address or host name this machine? Bounded, cached DNS for the
/// IPC listen address: shared by the core's bind and the GUI's dial, so both agree.

#include <QHostAddress>
#include <QList>
#include <QString>

class QObject;

#include <functional>
#include <optional>

namespace eMule::OwnAddress {

/// Name -> addresses, nullopt = timed out
using Lookup = std::function<std::optional<QList<QHostAddress>>(const QString&, int)>;

inline constexpr int kLookupTimeoutMs = 2000;

/// One of this machine's own addresses, a wildcard, or loopback.
[[nodiscard]] bool isOwn(QHostAddress addr);
/// Lower case, no trailing dot
[[nodiscard]] QString normalizedName(const QString& name);
/// localhost or *.localhost (RFC 6761): loopback without DNS. @p name normalized.
[[nodiscard]] bool isLocalhostName(const QString& name);
/// This machine's own name, also as its mDNS / short form. @p name normalized.
[[nodiscard]] bool isSelfName(const QString& name);

/// Addresses of @p name, waits up to kLookupTimeoutMs. Answers are cached (an
/// empty one too), a timeout is not: may be a passing DNS outage.
[[nodiscard]] std::optional<QList<QHostAddress>> resolve(const QString& name);

/// Looks @p name up again without blocking. An answer replaces the cached one,
/// then @p done(true) runs in @p context's thread; a failed lookup keeps the old
/// one, done(false). With a test seam it answers at once.
void refresh(const QString& name, const QObject* context, std::function<void(bool answered)> done);

/// The own addresses a server for @p name binds: per family the first one @p name
/// resolves to that is ours, not loopback and needs no scope id. Empty = none
/// (localhost too), nullopt = the lookup timed out.
[[nodiscard]] std::optional<QList<QHostAddress>> ownBindAddressesOf(const QString& name);

/// Test seam: replaces DNS and clears the cache; empty = system lookup again.
void setLookup(Lookup lookup);

} // namespace eMule::OwnAddress
