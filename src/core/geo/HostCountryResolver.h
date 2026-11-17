#pragma once

/// @file HostCountryResolver.h
/// @brief Hostname → country code, for lists that only know a configured host
///        (the Usenet news servers).
///
/// Resolves the host itself rather than reading a socket's peer address, so behind
/// a proxy the answer is still the news server's country, not the proxy's. A CDN's
/// address (isCdnAddress) gives none. Behind a proxy names go out by DoH through it
/// (Preferences::proxyDohUrl), never to the local resolver.

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace eMule {

/// false: a proxy is set and DoH is off, names must not be looked up at all.
[[nodiscard]] bool countryDnsAvailable();

/// A and AAAA of @p name for a country flag: the system resolver without a proxy, DoH
/// through it with one. Empty list on failure or while !countryDnsAvailable(). Main thread only.
void countryDnsLookup(const QString& name, QObject* context, std::function<void(QList<QHostAddress>)> done);

/// Asynchronous; @p done gets {host: cc} with "" for unresolvable/unknown hosts.
/// Answers from a 1 h cache where it can, and after at most ~5 s otherwise (~9 s by DoH).
/// @p context guards the callback. Main thread only.
void resolveHostCountries(const QStringList& hosts, QObject* context,
                          std::function<void(const QHash<QString, QString>&)> done);

/// Synchronous form for polled lists (torrent trackers): the cached code, else ""
/// and a lookup starts in the background so a later poll has it. A literal IP
/// answers at once. A miss is held 10 min. Main thread only (other threads: cache).
[[nodiscard]] QString cachedHostCountry(const QString& host);

} // namespace eMule
