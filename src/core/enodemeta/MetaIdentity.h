#pragma once

/// @file MetaIdentity.h
/// @brief Release identities behind a meta hash, and the fetched-file check.
///
/// Port of enodemeta's torrentmeta identity, nzbmeta.Identity and
/// VerifyMetaFile. The check binds a row (arrived over eD2K) to the metafile
/// (arrived over HTTPS from a separate listener). It cannot catch a server
/// lying consistently; it catches a different origin (cache, proxy, MITM).

#include "enodemeta/MetaHash.h"

#include <QByteArray>
#include <QString>

#include <expected>

namespace eMule::enodemeta {

/// Infohashes of a torrent's info dictionary.
struct TorrentIdentity {
    Kind kind = Kind::Unspecified;   ///< BtV1 for v1/hybrid, BtV2 for v2-only
    QByteArray identity;             ///< the hash a meta hash folds (20 or 32 bytes)
    QByteArray infoHashV1;           ///< SHA-1 of info (empty for v2-only)
    QByteArray infoHashV2;           ///< SHA-256 of info (empty for v1-only)
};

/// Identity of a whole .torrent or a bare info dictionary.
[[nodiscard]] std::expected<TorrentIdentity, QString> torrentIdentity(const QByteArray& metafile);

/// Pre-image of the canonical NZB digest ("nzb1\n" + "number:msgid\n"...).
[[nodiscard]] std::expected<QByteArray, QString> nzbIdentityPreimage(const QByteArray& nzb);

/// SHA-256 of nzbIdentityPreimage.
[[nodiscard]] std::expected<QByteArray, QString> nzbIdentity(const QByteArray& nzb);

/// Identity of a metafile for the given kind.
[[nodiscard]] std::expected<QByteArray, QString> identityOf(Kind kind, const QByteArray& metafile);

/// fold10(identityOf(bytes)) == hash digest; the error says why not.
[[nodiscard]] std::expected<void, QString> verifyMetaFile(const uint8* hash16, const QByteArray& metafile);

} // namespace eMule::enodemeta
