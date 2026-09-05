#pragma once

/// @file IndexerConfig.h
/// @brief One configured newznab/torznab indexer account.
///
/// Lives in core/prefs rather than in src/indexer for the same reason NewsServer
/// does: it is a stored preference first and a protocol object second. The file
/// format, the encryption of the credential and the identity rules are
/// Preferences' business, and core may not depend on eMule::Indexer.
/// src/indexer/IndexerConfig.h aliases these names into eMule::indexer.
///
/// newznab (Usenet) and torznab (BitTorrent) are the same HTTP API with a
/// different XML attribute namespace, and Prowlarr and NZBHydra2 serve both from
/// one endpoint — which is why `kind` exists and why this record is not called
/// UsenetIndexer.

#include <QString>
#include <QUrl>
#include <QtTypes>

namespace eMule {

/// Which flavour of the shared API an account speaks, and therefore which
/// networks may use it. `Both` is the normal answer for an aggregator.
enum class IndexerKind : quint8 {
    Newznab = 0,  ///< Usenet only. Results carry an .nzb URL.
    Torznab = 1,  ///< BitTorrent only. Results carry a .torrent URL and/or a magnet.
    Both    = 2,  ///< Prowlarr, NZBHydra2.
};

[[nodiscard]] QString indexerKindToString(IndexerKind kind);
[[nodiscard]] IndexerKind indexerKindFromString(const QString& text);

/// An indexer account. Plain value type — copied freely, no identity of its own
/// beyond `name`.
struct IndexerConfig {
    /// Display name, and the identity: the caps-cache filename and the key
    /// GetIndexerCaps takes. Must be unique and non-empty.
    QString name;

    /// The API base, stored **verbatim as the user typed it**.
    ///
    /// Normalisation happens in apiUrl(), not here, because there is no single
    /// rule: a bare host like `https://api.example.org/` wants `/api`
    /// appended, while a Jackett endpoint is already
    /// `…/api/v2.0/indexers/<id>/results/torznab/api` and appending would break
    /// it. Rewriting on save would also mean a user could never correct it.
    QString url;

    /// Plaintext in memory, AES-encrypted as `apiKeyEnc` in the YAML.
    QString apiKey;

    IndexerKind kind = IndexerKind::Newznab;

    /// Excluded from searches when false, without losing the stored key.
    bool enabled = true;

    /// Per-account, because a self-hosted Jackett and a public indexer over the
    /// open internet are not the same latency.
    int timeoutMs = 30000;

    [[nodiscard]] bool servesUsenet() const { return kind != IndexerKind::Torznab; }
    [[nodiscard]] bool servesTorrents() const { return kind != IndexerKind::Newznab; }

    /// Identity for "is this the same account", case-insensitive so a rename that
    /// only changes case keeps the stored key.
    [[nodiscard]] QString key() const { return name.trimmed().toCaseFolded(); }

    /// What to call this account in a log line or an error.
    [[nodiscard]] QString displayName() const;

    /// Filename-safe form of the name, for the caps-cache sidecar.
    [[nodiscard]] QString slug() const;

    /// The URL to actually issue a request against: `url` with `api` appended
    /// when — and only when — its path is empty or a bare slash. Any query the
    /// user typed is preserved; the client merges its own parameters onto it.
    [[nodiscard]] QUrl apiUrl() const;

    [[nodiscard]] bool isValid() const { return !name.trimmed().isEmpty() && apiUrl().isValid(); }
};

} // namespace eMule
