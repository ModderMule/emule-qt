#include "pch.h"
/// @file SkinInstaller.cpp
/// @brief Download + unpack of published skin bundles.

#include "app/SkinInstaller.h"

#include "archive/ArchiveReader.h"   // src/core
#include "net/HttpDefaults.h"        // src/core
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QUrl>

namespace eMule {

namespace {

// keep in sync with scripts/publish-skin.py
constexpr qint64 kMaxZipBytes = 64LL * 1024 * 1024;
constexpr quint64 kMaxUnpackedBytes = 256ULL * 1024 * 1024;
constexpr int kMaxEntries = 5000;

const QString kReleaseFile = QStringLiteral(".skinrelease.json");

QStringList profileFilter() { return {QStringLiteral("*.eMuleSkin.ini")}; }

QStringList stripFilter()
{
    return {QStringLiteral("*.eMuleToolbar.kad02.png"), QStringLiteral("*.eMuleToolbar.kad02.bmp"),
            QStringLiteral("*.eMuleToolbar.kad02.gif")};
}

} // namespace

SkinInstaller::SkinInstaller(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

QString SkinInstaller::skinsRoot()
{
    return thePrefs.configDir() + QStringLiteral("/skins");
}

QString SkinInstaller::skinDir(const QString& id)
{
    return skinsRoot() + QLatin1Char('/') + id;
}

std::optional<SkinInstaller::Installed> SkinInstaller::installed(const QString& id)
{
    if (!SkinRelease::isValidId(id))
        return std::nullopt;
    QFile file(skinDir(id) + QLatin1Char('/') + kReleaseFile);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    Installed inst;
    inst.id = id;
    inst.name = obj.value(QStringLiteral("name")).toString();
    inst.sha256 = obj.value(QStringLiteral("sha256")).toString();
    inst.date = QDateTime::fromString(obj.value(QStringLiteral("date")).toString(), Qt::ISODate);
    if (inst.sha256.isEmpty() || profilePath(id).isEmpty())
        return std::nullopt;
    return inst;
}

QString SkinInstaller::profilePath(const QString& id)
{
    const QDir dir(skinDir(id));
    const QStringList inis = dir.entryList(profileFilter(), QDir::Files, QDir::Name);
    return inis.isEmpty() ? QString{} : dir.filePath(inis.first());
}

QString SkinInstaller::toolbarStripPath(const QString& dirPath)
{
    if (dirPath.isEmpty())
        return {};
    const QDir dir(dirPath);
    if (const QStringList top = dir.entryList(stripFilter(), QDir::Files, QDir::Name); !top.isEmpty())
        return dir.filePath(top.first());
    for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir subDir(dir.filePath(sub));
        if (const QStringList files = subDir.entryList(stripFilter(), QDir::Files, QDir::Name); !files.isEmpty())
            return subDir.filePath(files.first());
    }
    return {};
}

QString SkinInstaller::bundleIdOf(const QString& path)
{
    if (path.isEmpty())
        return {};
    const QString root = QDir::cleanPath(QFileInfo(skinsRoot()).absoluteFilePath()) + QLatin1Char('/');
    const QString abs = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (!abs.startsWith(root))
        return {};
    const QString id = abs.mid(root.size()).section(QLatin1Char('/'), 0, 0);
    // a loose profile right in skins/ is no bundle
    if (id.isEmpty() || abs.mid(root.size()) == id || !installed(id))
        return {};
    return id;
}

void SkinInstaller::install(const SkinRelease& release)
{
    if (m_busy.contains(release.id))
        return;
    if (!SkinRelease::isValidId(release.id) || !release.file.isInstallable()) {
        emit finished(release.id, false, tr("The skin entry is incomplete."));
        return;
    }
    if (release.file.size > kMaxZipBytes) {
        emit finished(release.id, false, tr("The skin is too large (%1 bytes).").arg(release.file.size));
        return;
    }
    m_busy.insert(release.id);

    QNetworkReply* reply = m_nam->get(Http::makeRequest(QUrl(release.file.url)));
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, reply, id = release.id, size = release.file.size](qint64 received, qint64) {
        // more than announced: not the published file
        if (received > size) {
            reply->abort();
            return;
        }
        emit progress(id, received, size);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, release] {
        onDownloaded(reply, release);
    });
    logInfo(QStringLiteral("Skin %1: downloading %2").arg(release.id, release.file.url));
}

void SkinInstaller::updateInstalled(const QList<SkinRelease>& releases)
{
    for (const SkinRelease& r : releases) {
        const auto inst = installed(r.id);
        if (!inst || inst->sha256 == r.file.sha256 || m_busy.contains(r.id))
            continue;
        logInfo(QStringLiteral("Skin %1: update published (%2), installing")
                    .arg(r.id, r.date.toString(Qt::ISODate)));
        install(r);
    }
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void SkinInstaller::onDownloaded(QNetworkReply* reply, const SkinRelease& release)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        fail(release.id, reply->errorString());
        return;
    }
    const QByteArray data = reply->readAll();
    if (data.size() != release.file.size) {
        fail(release.id, tr("Size mismatch: got %1 bytes, expected %2.").arg(data.size()).arg(release.file.size));
        return;
    }
    const QString sha = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    if (sha != release.file.sha256) {
        fail(release.id, tr("Checksum mismatch."));
        return;
    }

    if (!QDir().mkpath(skinsRoot())) {
        fail(release.id, tr("Cannot create %1.").arg(QDir::toNativeSeparators(skinsRoot())));
        return;
    }
    const QString zipPath = skinsRoot() + QStringLiteral("/.%1.download.zip").arg(release.id);
    {
        QSaveFile out(zipPath);
        if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size() || !out.commit()) {
            fail(release.id, tr("Cannot write %1.").arg(QDir::toNativeSeparators(zipPath)));
            return;
        }
    }
    const QString error = unpack(release, zipPath);
    QFile::remove(zipPath);
    if (!error.isEmpty()) {
        fail(release.id, error);
        return;
    }
    m_busy.remove(release.id);
    logInfo(QStringLiteral("Skin %1: installed to %2").arg(release.id, skinDir(release.id)));
    emit finished(release.id, true, {});
}

QString SkinInstaller::unpack(const SkinRelease& release, const QString& zipPath)
{
    ArchiveReader reader;
    if (!reader.open(zipPath))
        return tr("Cannot open the skin archive: %1").arg(reader.lastError());
    if (reader.entryCount() > kMaxEntries)
        return tr("The skin archive has too many files.");
    quint64 total = 0;
    for (int i = 0; i < reader.entryCount(); ++i)
        total += reader.entrySize(i);
    if (total > kMaxUnpackedBytes)
        return tr("The skin archive is too large.");

    const QString staging = skinsRoot() + QStringLiteral("/.%1.new").arg(release.id);
    QDir(staging).removeRecursively();
    if (!reader.extractAll(staging)) {
        QDir(staging).removeRecursively();
        return tr("Cannot unpack the skin archive: %1").arg(reader.lastError());
    }
    reader.close();

    // one top folder (as zipped by Finder/Explorer) becomes the bundle itself
    QDir(staging + QStringLiteral("/__MACOSX")).removeRecursively();
    QString bundle = staging;
    const QStringList top = QDir(staging).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    if (top.size() == 1 && QFileInfo(staging + QLatin1Char('/') + top.first()).isDir())
        bundle = staging + QLatin1Char('/') + top.first();
    if (QDir(bundle).entryList(profileFilter(), QDir::Files).isEmpty()) {
        QDir(staging).removeRecursively();
        return tr("The skin archive has no *.eMuleSkin.ini.");
    }

    QSaveFile meta(bundle + QLatin1Char('/') + kReleaseFile);
    const QJsonObject obj{
        {QStringLiteral("id"), release.id},
        {QStringLiteral("name"), release.name},
        {QStringLiteral("sha256"), release.file.sha256},
        {QStringLiteral("date"), release.date.toString(Qt::ISODate)},
        {QStringLiteral("url"), release.file.url},
        {QStringLiteral("installed"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
    };
    if (!meta.open(QIODevice::WriteOnly) || meta.write(QJsonDocument(obj).toJson()) < 0 || !meta.commit()) {
        QDir(staging).removeRecursively();
        return tr("Cannot write the skin metadata.");
    }

    // swap: old copy aside, new one in, old one gone
    const QString target = skinDir(release.id);
    const QString old = skinsRoot() + QStringLiteral("/.%1.old").arg(release.id);
    QDir(old).removeRecursively();
    const bool hadOld = QFileInfo::exists(target);
    if (hadOld && !QDir().rename(target, old)) {
        QDir(staging).removeRecursively();
        return tr("Cannot replace %1 (in use?).").arg(QDir::toNativeSeparators(target));
    }
    if (!QDir().rename(bundle, target)) {
        if (hadOld)
            QDir().rename(old, target);
        QDir(staging).removeRecursively();
        return tr("Cannot move the skin to %1.").arg(QDir::toNativeSeparators(target));
    }
    QDir(staging).removeRecursively();   // empty, or gone when it was the bundle
    QDir(old).removeRecursively();
    return {};
}

void SkinInstaller::fail(const QString& id, const QString& error)
{
    m_busy.remove(id);
    logWarning(QStringLiteral("Skin %1: install failed: %2").arg(id, error));
    emit finished(id, false, error);
}

} // namespace eMule
