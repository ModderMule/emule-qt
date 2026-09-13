#include "pch.h"
/// @file PathUtils.cpp
/// @brief Portable path utility implementations.

#include "PathUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QStorageInfo>

namespace eMule {

QString appDirectory(AppDir dir)
{
    QString path;

    switch (dir) {
    case AppDir::Config:
        path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        break;
    case AppDir::Temp:
        path = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
               + QStringLiteral("/eMule");
        break;
    case AppDir::Incoming:
        path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
               + QStringLiteral("/Incoming");
        break;
    case AppDir::Log:
        path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
               + QStringLiteral("/Logs");
        break;
    case AppDir::Data:
        path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        break;
    case AppDir::Cache:
        path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        break;
    }

    if (!path.isEmpty()) {
        QDir d(path);
        if (!d.exists())
            d.mkpath(QStringLiteral("."));
    }

    return path;
}

QString executablePath()
{
    return QCoreApplication::applicationFilePath();
}

QString executableDir()
{
    return QCoreApplication::applicationDirPath();
}

QString ensureTrailingSeparator(const QString& path)
{
    if (path.isEmpty() || path.endsWith(QChar(u'/')))
        return path;
#ifdef Q_OS_WIN
    if (path.endsWith(QChar(u'\\')))
        return path;
#endif
    return path + QChar(u'/');
}

QString removeTrailingSeparator(const QString& path)
{
    if (path.isEmpty())
        return path;

    QString result = path;
    while (result.size() > 1 && (result.endsWith(QChar(u'/'))
#ifdef Q_OS_WIN
           || result.endsWith(QChar(u'\\'))
#endif
           )) {
        result.chop(1);
    }
    return result;
}

QString canonicalPath(const QString& path)
{
    return QDir(path).canonicalPath();
}

bool pathsEqual(const QString& a, const QString& b)
{
    const QString ca = QDir(a).canonicalPath();
    const QString cb = QDir(b).canonicalPath();

    if (ca.isEmpty() || cb.isEmpty())
        return false;

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    // Windows and macOS file systems are typically case-insensitive
    return ca.compare(cb, Qt::CaseInsensitive) == 0;
#else
    return ca == cb;
#endif
}

std::uint64_t freeDiskSpace(const QString& path)
{
    return tryFreeDiskSpace(path).value_or(0);
}

std::optional<std::uint64_t> tryFreeDiskSpace(const QString& path)
{
    if (path.isEmpty())
        return std::nullopt;

    // ⚠️ QStorageInfo answers for a path that does not exist by being *invalid*,
    // not by resolving the volume that would hold it. A scratch or incoming
    // directory is created on first use, so asking about one before then would
    // read as "this volume cannot be measured" — and a guard acting on that
    // would park a fresh install forever. Walk up to the nearest ancestor that
    // exists; the volume is the same one either way.
    // Climbed by path rather than with QDir::cdUp(), which refuses to move into
    // a parent that does not exist either — and a scratch tree is usually
    // missing several levels at once on first use.
    QString probe = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    while (!probe.isEmpty()) {
        if (QFileInfo::exists(probe)) {
            QStorageInfo info(probe);
            if (info.isValid() && info.isReady())
                return static_cast<std::uint64_t>(info.bytesAvailable());
            return std::nullopt;
        }
        const QString parent = QFileInfo(probe).absolutePath();
        if (parent == probe)
            break;                       // reached the root without finding anything
        probe = parent;
    }
    return std::nullopt;
}

QString sanitizeFilename(const QString& name)
{
    QString result = name;

    // Replace characters that are invalid on common file systems
    static constexpr std::array invalidChars = {
        u'/', u'\\', u':', u'*', u'?', u'"', u'<', u'>', u'|'
    };
    for (auto ch : invalidChars) {
        result.replace(QChar(ch), QChar(u'_'));
    }

    // Remove leading/trailing spaces and dots (Windows disallows trailing dots)
    while (!result.isEmpty() && (result.front() == QChar(u' ') || result.front() == QChar(u'.')))
        result.removeFirst();
    while (!result.isEmpty() && (result.back() == QChar(u' ') || result.back() == QChar(u'.')))
        result.removeLast();

    return result;
}

} // namespace eMule
