#pragma once

/// @file Ed3kPlumbing.h
/// @brief Small pure helpers between eMuleQt's preferences and the ed3k engine: extension
/// switches (Config::extOff), `ed3k:nodes` links (bootstrap.md §3), the advertised IPv6
/// source (socket-modes.md §8.3, IPv6SourcePin).

#include "Ed3kFfi.h"

#include <QHostAddress>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace eMule::ed3k {

/// ED3K_EXT_* bits of @p names (Preferences::ed3kExtensionNames(); `lan` = lan-discovery,
/// case and blanks ignored). Unknown names go to @p unknown and are left out.
[[nodiscard]] std::uint64_t extOffBits(const QStringList& names, QStringList* unknown = nullptr);
/// The ED3K_EXT_* bit of one name; 0: unknown.
[[nodiscard]] std::uint64_t extBit(const QString& name);

/// A node list source (bootstrap.md §3): an HTTPS URL and the key signing its lists.
struct NodesSource {
    QString url;
    ::ed3k::Key32 key{};
};
/// `ed3k:nodes?url=<url-encoded HTTPS URL>&key=<base32 lower case>`; nullopt: invalid (url or
/// key missing or repeated, key not 32 B, url not an absolute HTTPS URL).
[[nodiscard]] std::optional<NodesSource> parseNodesLink(const QString& link);
/// The link of a source (what parseNodesLink reads).
[[nodiscard]] QString nodesLink(const QString& url, const ::ed3k::Key32& key);
/// RFC 4648 base32, lower case, no padding.
[[nodiscard]] QString base32(std::span<const std::uint8_t> bytes);
/// nullopt: not base32 (lower or upper case, no padding).
[[nodiscard]] std::optional<QByteArray> fromBase32(const QString& text);

/// An address of the engine (IPv4 also for ::ffff:a.b.c.d; no scope id).
[[nodiscard]] ::ed3k::Address toAddress(const QHostAddress& ip, std::uint16_t port);
[[nodiscard]] QHostAddress toQHostAddress(const ::ed3k::Address& a);

/// The IPv6 address to advertise as `local6` for a socket bound to @p bind: @p pin (the
/// stable address, IPv6SourcePin) for a wildcard bind (Any, AnyIPv6), else null (`[::]`, or
/// the bound address itself).
[[nodiscard]] QHostAddress advertisedIPv6(const QHostAddress& bind, const QHostAddress& pin);
/// IPv6SourcePin's address now; null while it does not apply (privacy address, proxy, none).
[[nodiscard]] QHostAddress ipv6Pin();

} // namespace eMule::ed3k
