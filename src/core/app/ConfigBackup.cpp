#include "app/ConfigBackup.h"

#include "utils/Log.h"
#include "utils/ArgStr.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <array>

namespace eMule::ConfigBackup {

namespace {

// regenerated, downloaded again, or of no use in another place
constexpr std::array kSkippedSuffixes{
    QLatin1StringView(".log"), QLatin1StringView(".lock"), QLatin1StringView(".pid"),
    QLatin1StringView(".tmp"), QLatin1StringView(".backup"),
};
constexpr std::array kSkippedDirs{
    QLatin1StringView("crashes"),       QLatin1StringView("I2P/netDb"), QLatin1StringView("I2P/bin"),
    QLatin1StringView("ed3k/resolver"), QLatin1StringView("ed3k/java"),
};

bool isWithin(const QString& path, const QString& root)
{
    const QString p = QDir::cleanPath(path);
    const QString r = QDir::cleanPath(root);
#ifdef Q_OS_WIN
    constexpr auto cs = Qt::CaseInsensitive;
#else
    constexpr auto cs = Qt::CaseSensitive;
#endif
    return p.compare(r, cs) == 0 || p.startsWith(r + u'/', cs);
}

} // namespace

bool skipped(const QString& relPath)
{
    for (const auto dir : kSkippedDirs)
        if (relPath == dir || relPath.startsWith(dir + u'/'))
            return true;
    const QString name = relPath.section(u'/', -1);
    if (name.contains(QLatin1StringView(".log.")))   // rotated logs
        return true;
    return std::ranges::any_of(kSkippedSuffixes, [&name](QLatin1StringView s) { return name.endsWith(s); });
}

std::expected<ConfigBackupResult, QString> run(const QString& configDir, const QString& destParent)
{
    const QFileInfo src(configDir);
    if (configDir.isEmpty() || !src.isDir())
        return std::unexpected(QStringLiteral("config folder not found"));
    const QFileInfo dst(destParent);
    if (destParent.isEmpty() || !dst.isDir())
        return std::unexpected(QStringLiteral("backup folder not found: %1").arg(QDir::toNativeSeparators(destParent)));
    const QString srcRoot = src.canonicalFilePath();
    if (isWithin(dst.canonicalFilePath(), srcRoot))
        return std::unexpected(QStringLiteral("the backup folder must not be inside the config folder"));

    ConfigBackupResult out;
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString folder = QDir(dst.canonicalFilePath()).filePath(QStringLiteral("eMuleQt-config-") + stamp);
    for (int n = 2; QFileInfo::exists(folder); ++n)
        folder = QDir(dst.canonicalFilePath()).filePath(QStringLiteral("eMuleQt-config-%1-%2").arg(stamp).arg(n));
    if (!QDir().mkpath(folder))
        return std::unexpected(QStringLiteral("cannot create %1").arg(QDir::toNativeSeparators(folder)));
    out.folder = folder;

    const QDir root(srcRoot);
    // no symlinks followed: a link out of the config folder is not ours to copy
    QDirIterator it(srcRoot, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = root.relativeFilePath(path);
        if (skipped(rel))
            continue;
        const QString target = QDir(folder).filePath(rel);
        QDir().mkpath(QFileInfo(target).absolutePath());
        if (QFile::copy(path, target)) {
            ++out.files;
            out.bytes += QFileInfo(target).size();
        } else {
            ++out.failed;
            logWarning(QStringLiteral("Config backup: %1 not copied").arg(rel));
        }
    }
    if (out.files == 0)
        return std::unexpected(QStringLiteral("nothing copied into %1").arg(QDir::toNativeSeparators(folder)));
    logInfo(QStringLiteral("Config backed up to %1 (%2 files%3)")
                .arg(argStr(QDir::toNativeSeparators(folder)),
                     argStr(out.files),
                     argStr(out.failed ? QStringLiteral(", %1 failed").arg(out.failed) : QString())));
    return out;
}

} // namespace eMule::ConfigBackup
