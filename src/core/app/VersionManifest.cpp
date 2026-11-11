#include "pch.h"
/// @file VersionManifest.cpp
/// @brief emuleqt-version.json parsing.

#include "app/VersionManifest.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QVersionNumber>

#include <algorithm>

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

bool SkinRelease::isValidId(const QString& id)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$"));
    return re.match(id).hasMatch();
}

bool VersionManifestInfo::isBelowMinimum(const QString& local) const
{
    return !minVersion.isEmpty() && VersionManifest::isNewer(minVersion, local);
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
    info.description = obj.value(QStringLiteral("description")).toString().trimmed();
    for (const QJsonValue& v : obj.value(QStringLiteral("changelog")).toArray()) {
        if (const QString line = v.toString().trimmed(); !line.isEmpty())
            info.changelog.append(line);
    }

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

    const QJsonObject res = obj.value(QStringLiteral("hosterResolver")).toObject();
    if (!res.isEmpty()) {
        info.hosterResolver.file.url = res.value(QStringLiteral("url")).toString();
        info.hosterResolver.file.sha256 = res.value(QStringLiteral("sha256")).toString().toLower();
        info.hosterResolver.file.size = res.value(QStringLiteral("size")).toInteger(0);
        info.hosterResolver.jdRevision = int(res.value(QStringLiteral("jdRevision")).toInteger(0));
        info.hosterResolver.api = int(res.value(QStringLiteral("api")).toInteger(0));
    }

    const auto readDownload = [](const QJsonObject& e) {
        VersionDownload d;
        d.url = e.value(QStringLiteral("url")).toString();
        d.sha256 = e.value(QStringLiteral("sha256")).toString().toLower();
        d.size = e.value(QStringLiteral("size")).toInteger(0);
        return d;
    };
    const QJsonObject skins = obj.value(QStringLiteral("skins")).toObject();
    for (auto it = skins.begin(); it != skins.end(); ++it) {
        const QJsonObject e = it.value().toObject();
        SkinRelease s;
        s.id = it.key();
        s.file = readDownload(e);
        // skip what could not be installed anyway
        if (!SkinRelease::isValidId(s.id) || !s.file.isInstallable())
            continue;
        s.name = e.value(QStringLiteral("name")).toString().trimmed();
        if (s.name.isEmpty())
            s.name = s.id;
        s.author = e.value(QStringLiteral("author")).toString().trimmed();
        s.description = e.value(QStringLiteral("description")).toString().trimmed();
        s.date = QDateTime::fromString(e.value(QStringLiteral("date")).toString(), Qt::ISODate);
        if (const VersionDownload p = readDownload(e.value(QStringLiteral("preview")).toObject());
            p.isInstallable())
            s.preview = p;
        info.skins.append(s);
    }
    std::ranges::sort(info.skins, [](const SkinRelease& a, const SkinRelease& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

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
