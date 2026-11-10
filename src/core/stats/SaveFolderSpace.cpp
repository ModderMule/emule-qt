#include "pch.h"
/// @file SaveFolderSpace.cpp
/// @brief Free space in the default save folder of each download network — implementation.

#include "stats/SaveFolderSpace.h"

#include "prefs/Preferences.h"
#include "utils/PathUtils.h"

#include <QCborMap>
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QMutex>
#include <QStringList>
#include <QThreadPool>

namespace eMule {

namespace {

/// Default folder of each network that is on; key order is the display order.
std::vector<SaveFolderSpace> folders()
{
    std::vector<SaveFolderSpace> out;
    const QString incoming = thePrefs.incomingDir();
    out.push_back({QStringLiteral("incoming"), incoming, {}});
    if (thePrefs.btEnabled()) {
        const QString bt = thePrefs.btDownloadDir();
        out.push_back({QStringLiteral("torrent"), bt.isEmpty() ? incoming : bt, {}});
    }
    if (thePrefs.usenetEnabled())
        out.push_back({QStringLiteral("usenet"), thePrefs.incomingDirForCategory(0), {}});
    if (thePrefs.hosterDownloadsOn())
        out.push_back({QStringLiteral("hoster"), incoming, {}});   // extracts into incoming/<package>
    return out;
}

} // namespace

std::vector<SaveFolderSpace> saveFolderSpace()
{
    struct Cache {
        QMutex mutex;
        QHash<QString, qint64> free;   ///< by path; missing = unreadable
        QStringList paths;
        QElapsedTimer age;
        bool running = false;
    };
    static constexpr qint64 kRefreshMs = 5000;
    static Cache cache;

    std::vector<SaveFolderSpace> rows = folders();
    QStringList paths;
    for (const auto& r : rows)
        if (!paths.contains(r.path))
            paths.append(r.path);

    QMutexLocker lock(&cache.mutex);
    const bool stale = !cache.age.isValid() || cache.age.elapsed() > kRefreshMs || cache.paths != paths;
    if (stale && !cache.running) {
        cache.running = true;
        QThreadPool::globalInstance()->start([paths] {
            QHash<QString, qint64> measured;
            for (const QString& path : paths)
                if (const auto bytes = tryFreeDiskSpace(path))
                    measured.insert(path, static_cast<qint64>(*bytes));
            QMutexLocker done(&cache.mutex);
            cache.free = std::move(measured);
            cache.paths = paths;
            cache.age.start();
            cache.running = false;
        });
    }
    for (auto& r : rows)
        if (const auto it = cache.free.constFind(r.path); it != cache.free.cend())
            r.free = *it;
    return rows;
}

QCborMap saveFolderSpaceToCbor(const std::vector<SaveFolderSpace>& rows)
{
    QCborMap out;
    for (const auto& r : rows) {
        QCborMap entry{{QStringLiteral("path"), r.path}};
        if (r.free)
            entry.insert(QStringLiteral("free"), *r.free);
        out.insert(r.key, entry);
    }
    return out;
}

QJsonObject saveFolderSpaceToJson(const std::vector<SaveFolderSpace>& rows)
{
    QJsonObject out;
    for (const auto& r : rows) {
        QJsonObject entry{{QStringLiteral("path"), r.path}};
        if (r.free)
            entry.insert(QStringLiteral("free"), *r.free);
        out.insert(r.key, entry);
    }
    return out;
}

} // namespace eMule
