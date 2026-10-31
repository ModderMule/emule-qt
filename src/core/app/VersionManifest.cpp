#include "pch.h"
/// @file VersionManifest.cpp
/// @brief emuleqt-version.json parsing.

#include "app/VersionManifest.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QVersionNumber>

// Key this build uses in the manifest's "downloads" map (windows-x64, mac-arm64, ...).
#if defined(Q_OS_WIN)
#  define EMULE_MANIFEST_OS "windows"
#elif defined(Q_OS_MACOS)
#  define EMULE_MANIFEST_OS "mac"
#elif defined(Q_OS_LINUX)
#  define EMULE_MANIFEST_OS "linux"
#endif

#if defined(Q_PROCESSOR_X86_64)
#  define EMULE_MANIFEST_ARCH "x64"
#elif defined(Q_PROCESSOR_ARM_64)
#  define EMULE_MANIFEST_ARCH "arm64"
#endif

#if defined(EMULE_MANIFEST_OS) && defined(EMULE_MANIFEST_ARCH)
#  define EMULE_MANIFEST_PLATFORM EMULE_MANIFEST_OS "-" EMULE_MANIFEST_ARCH
#endif

namespace eMule {

namespace {

constexpr QLatin1StringView kDefaultManifestUrl{"https://emule-qt.org/pub/emuleqt-version.json"};

// Old context, so the shipped translations keep matching
QString trManifest(const char* text)
{
    return QCoreApplication::translate("eMule::VersionChecker", text);
}

bool isHttpUrl(const QString& s)
{
    const QUrl url(s, QUrl::StrictMode);
    return url.isValid() && !url.host().isEmpty()
           && (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"));
}

} // namespace

bool VersionDownload::isUsable() const
{
    return size > 0 && isHttpUrl(url);
}

bool VersionDownload::isInstallable() const
{
    static const QRegularExpression hex64(QStringLiteral("^[0-9a-f]{64}$"));
    return isUsable() && hex64.match(sha256).hasMatch();
}

namespace VersionManifest {

VersionManifestInfo parse(const QByteArray& json)
{
    VersionManifestInfo info;

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject()) {
        info.error = parseError.error == QJsonParseError::NoError
                         ? trManifest("the version manifest is not a JSON object")
                         : trManifest("invalid JSON response: %1").arg(parseError.errorString());
        return info;
    }

    const QJsonObject obj = doc.object();
    info.latest = obj.value(QStringLiteral("latest")).toString();
    if (info.latest.isEmpty()) {
        info.error = trManifest("the version manifest has no 'latest' field");
        return info;
    }

    // Optional from here on
    info.date = QDateTime::fromString(obj.value(QStringLiteral("date")).toString(), Qt::ISODate).date();
    info.releaseNotes = obj.value(QStringLiteral("releaseNotes")).toString();
    info.minVersion = obj.value(QStringLiteral("minVersion")).toString();

    const QJsonObject downloads = obj.value(QStringLiteral("downloads")).toObject();
    for (auto it = downloads.begin(); it != downloads.end(); ++it) {
        const QJsonObject e = it.value().toObject();
        if (e.isEmpty())
            continue;
        VersionDownload d;
        d.url = e.value(QStringLiteral("url")).toString();
        d.sha256 = e.value(QStringLiteral("sha256")).toString().toLower();
        d.size = e.value(QStringLiteral("size")).toInteger(0);
        info.downloads.insert(it.key(), d);
    }

    const QString key = platformKey();
    if (const VersionDownload d = info.downloadFor(key); !key.isEmpty() && d.isUsable())
        info.downloadUrl = QUrl(d.url, QUrl::StrictMode).toString();
    return info;
}

bool isNewer(const QString& remote, const QString& local)
{
    const QVersionNumber r = QVersionNumber::fromString(remote);
    const QVersionNumber l = QVersionNumber::fromString(local);
    if (r.isNull() || l.isNull())
        return false;
    return r > l;
}

QString platformKey()
{
#ifdef EMULE_MANIFEST_PLATFORM
    return QStringLiteral(EMULE_MANIFEST_PLATFORM);
#else
    return {};
#endif
}

QString manifestUrl()
{
    const QString override = qEnvironmentVariable("EMULEQT_VERSION_MANIFEST_URL");
    return override.isEmpty() ? QString(kDefaultManifestUrl) : override;
}

} // namespace VersionManifest

} // namespace eMule
