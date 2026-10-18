#pragma once

/// @file WebLinks.h
/// @brief Web link sets — port of enodemeta/weblinks (Go).
///
/// A web row's metafile is a text/uri-list: http(s) links, one a line, with
/// "#key: value" hint comments before each (file, name, size, hoster) and
/// set-wide ones (title, protected). The identity is SHA-256 over
/// "web1\n" + each distinct canonical link + "\n", sorted by bytes. Hints are
/// outside it. String rules only, byte for byte as the Go port has them.
/// Test vectors: external/enodemeta/testdata/web-identity-vectors.json.

#include "utils/Types.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <expected>

namespace eMule::enodemeta {

inline constexpr char kWebIdentityPrefix[] = "web1\n";
inline constexpr int kWebMaxLinks = 4096;
inline constexpr char kWebLinksContentType[] = "text/uri-list";

/// One link of a set. Links with the same file number are mirrors.
struct WebLink {
    QByteArray url;     ///< canonical form
    uint32 file = 0;
    QString name;
    uint64 size = 0;    ///< 0 = not known
    QString hoster;
};

struct WebLinkSet {
    QString title;
    bool isProtected = false;
    QList<WebLink> links;

    /// Distinct file numbers.
    [[nodiscard]] int fileCount() const;
};

/// A link in the form the identity is taken over; the error says why not.
[[nodiscard]] std::expected<QByteArray, QString> canonicalLink(const QByteArray& link);

/// Read a container. One bad line refuses the whole document.
[[nodiscard]] std::expected<WebLinkSet, QString> parseWebLinks(const QByteArray& raw);

/// Exact bytes the digest is taken over.
[[nodiscard]] std::expected<QByteArray, QString> webIdentityPreimage(const WebLinkSet& set);

/// SHA-256 of webIdentityPreimage(parseWebLinks(raw)).
[[nodiscard]] std::expected<QByteArray, QString> webIdentity(const QByteArray& raw);

} // namespace eMule::enodemeta
