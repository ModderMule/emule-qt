#pragma once

/// @file VersionManifest.h
/// @brief emuleqt-version.json parsing, shared by the GUI version check and the core auto-updater.

#include <QDate>
#include <QHash>
#include <QMetaType>
#include <QString>

namespace eMule {

/// One "downloads" entry, written by scripts/update-manifest.sh.
struct VersionDownload {
    QString url;      ///< http(s) asset URL
    QString sha256;   ///< lowercase hex, 64 chars
    qint64  size = 0; ///< bytes; 0 marks a placeholder

    /// Usable for a link: http(s) URL and size > 0.
    [[nodiscard]] bool isUsable() const;
    /// Usable for installing: also a well-formed SHA-256.
    [[nodiscard]] bool isInstallable() const;
};

/// Everything emuleqt-version.json advertises.
///
/// Fields the document omits stay empty; @ref error is the only thing that makes the
/// manifest unusable. A missing or placeholder download entry is normal.
struct VersionManifestInfo {
    QString latest;        ///< "latest", e.g. "0.2.0"
    QDate   date;          ///< "date" (ISO-8601 instant), invalid when absent/malformed
    QString releaseNotes;  ///< "releaseNotes"
    QString minVersion;    ///< "minVersion"
    QString downloadUrl;   ///< this platform's usable "downloads" URL, empty otherwise
    QHash<QString, VersionDownload> downloads; ///< every entry, keyed "mac-arm64", ...
    QString error;         ///< why the document could not be used; empty on success

    [[nodiscard]] bool isValid() const { return error.isEmpty(); }
    /// Entry for @p key, empty when absent.
    [[nodiscard]] VersionDownload downloadFor(const QString& key) const { return downloads.value(key); }
};

namespace VersionManifest {

/// Parse a manifest document. Pure: no network, no preferences.
[[nodiscard]] VersionManifestInfo parse(const QByteArray& json);

/// True when @p remote is a strictly higher version than @p local (numeric compare).
[[nodiscard]] bool isNewer(const QString& remote, const QString& local);

/// "mac-arm64", "windows-x64", ... Empty on an OS/CPU combination with no entry.
[[nodiscard]] QString platformKey();

/// Manifest URL; EMULEQT_VERSION_MANIFEST_URL overrides it (fixtures, tests).
[[nodiscard]] QString manifestUrl();

} // namespace VersionManifest

} // namespace eMule

Q_DECLARE_METATYPE(eMule::VersionManifestInfo)
