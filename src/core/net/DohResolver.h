#pragma once

/// @file DohResolver.h
/// @brief DNS over HTTPS (RFC 8484 GET, wire format) through the Proxy page's proxy.
///
/// Behind a proxy the local resolver must not see names (a DNS leak). A DoH query is a
/// plain HTTPS request, so it goes through any proxy that tunnels HTTPS: SOCKS4a/5 and
/// HTTP CONNECT. Used where a name only feeds a display (country flags), never to dial.

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace eMule {

/// A built-in DoH endpoint offered on the Proxy page.
struct DohProvider {
    QString name;
    QString url;
};

/// Built-in endpoints, all answering RFC 8484 GET (tested 2026-11-17). The first is the default.
[[nodiscard]] const QList<DohProvider>& dohProviders();

/// The default endpoint: an IP literal, so no name lookup is needed to reach it.
[[nodiscard]] QString defaultDohUrl();

/// https://, or http:// to a local host (a local DoH proxy); anything else is refused.
[[nodiscard]] bool isValidDohUrl(const QString& url);

/// The DoH endpoint to use now: the Proxy page's when a proxy is set and DoH is on, else
/// empty (the system resolver). Main thread only.
[[nodiscard]] QString activeDohUrl();

/// Names only the system resolver knows (localhost, single label, .local/.lan/.internal/
/// .home.arpa); never sent to DoH.
[[nodiscard]] bool isLocalName(const QString& name);

/// A and AAAA of @p name via @p dohUrl; @p done gets an empty list on any failure or after
/// @p timeoutMs. Requests follow the Proxy page (GuardedNetworkAccessManager).
/// @p context guards the callback. Main thread only.
void dohLookup(const QString& dohUrl, const QString& name, QObject* context,
               std::function<void(QList<QHostAddress>)> done, int timeoutMs = 8000);

/// The raw answer message for one @p type query; nullopt on transport failure or timeout.
void dohQuery(const QString& dohUrl, const QString& name, quint16 type, QObject* context,
              std::function<void(std::optional<QByteArray>)> done, int timeoutMs = 8000);

namespace dns {

constexpr quint16 kTypeA = 1;
constexpr quint16 kTypeTXT = 16;
constexpr quint16 kTypeAAAA = 28;

/// TXT answer: one entry per record, its strings joined (no separator); lowest TTL.
struct TxtAnswer {
    QList<QByteArray> records;
    quint32 ttl = 0;
};

/// Query message for @p name (ACE form) and @p type, id 0 (RFC 8484 §4.1, cache friendly).
/// Empty for a name that cannot be encoded.
[[nodiscard]] QByteArray buildQuery(const QString& name, quint16 type);

/// A/AAAA addresses in the answer section of @p message; empty on error rcode or garbage.
[[nodiscard]] QList<QHostAddress> parseAddresses(const QByteArray& message);

/// TXT records of @p message; NXDOMAIN gives an empty answer, other errors and garbage nullopt.
[[nodiscard]] std::optional<TxtAnswer> parseTxt(const QByteArray& message);

} // namespace dns

} // namespace eMule
