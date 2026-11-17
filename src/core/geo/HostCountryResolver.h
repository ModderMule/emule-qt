#pragma once

/// @file HostCountryResolver.h
/// @brief Hostname → country code, for lists that only know a configured host
///        (the Usenet news servers).
///
/// Resolves the host itself rather than reading a socket's peer address, so behind
/// a proxy the answer is still the news server's country, not the proxy's. A CDN's
/// address (isCdnAddress) gives none.

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace eMule {

/// Asynchronous; @p done gets {host: cc} with "" for unresolvable/unknown hosts.
/// Answers from a 1 h cache where it can, and after at most ~5 s otherwise.
/// @p context guards the callback. Main thread only.
void resolveHostCountries(const QStringList& hosts, QObject* context,
                          std::function<void(const QHash<QString, QString>&)> done);

/// Synchronous form for polled lists (torrent trackers): the cached code, else ""
/// and a lookup starts in the background so a later poll has it. A literal IP
/// answers at once. A miss is held 10 min. Main thread only (other threads: cache).
[[nodiscard]] QString cachedHostCountry(const QString& host);

} // namespace eMule
