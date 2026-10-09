#include "pch.h"
/// @file PathUtils.cpp
/// @brief Portable path utility implementations.

#include "PathUtils.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QStorageInfo>

#if defined(Q_OS_WIN)
#include <io.h>
#include <qt_windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

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
#if defined(Q_OS_WIN)
            // By directory, not by volume root: a quota or a mount below a share
            // (\\wsl.localhost\...) has less room than the root QStorageInfo asks about.
            const QString dir = QFileInfo(probe).isDir() ? probe : QFileInfo(probe).absolutePath();
            ULARGE_INTEGER avail{};
            if (GetDiskFreeSpaceExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(dir).utf16()),
                                    &avail, nullptr, nullptr))
                return static_cast<std::uint64_t>(avail.QuadPart);
#endif
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

bool isDiskFullError(const QFileDevice& file)
{
    if (file.error() == QFileDevice::ResourceError)
        return true;
#if defined(Q_OS_WIN)
    // The engine stores qt_error_string() of the failed WriteFile and nothing else.
    if (file.error() == QFileDevice::WriteError) {
        const QString text = file.errorString();
        for (const DWORD code : {DWORD(ERROR_DISK_FULL), DWORD(ERROR_HANDLE_DISK_FULL),
                                 DWORD(ERROR_DISK_QUOTA_EXCEEDED)}) {
            if (text == qt_error_string(static_cast<int>(code)))
                return true;
        }
    }
#endif
    return false;
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

bool preallocateFile(QFile& file, std::uint64_t size)
{
    if (!file.isOpen() || size == 0)
        return false;
#if defined(Q_OS_WIN)
    const HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()));
    if (h == INVALID_HANDLE_VALUE)
        return false;
    FILE_ALLOCATION_INFO info{};
    info.AllocationSize.QuadPart = static_cast<LONGLONG>(size);
    return SetFileInformationByHandle(h, FileAllocationInfo, &info, sizeof(info)) != 0;
#elif defined(Q_OS_MACOS)
    fstore_t store{F_ALLOCATECONTIG | F_ALLOCATEALL, F_PEOFPOSMODE, 0, static_cast<off_t>(size), 0};
    if (fcntl(file.handle(), F_PREALLOCATE, &store) != -1)
        return true;
    store.fst_flags = F_ALLOCATEALL;   // contiguous was too much to ask
    return fcntl(file.handle(), F_PREALLOCATE, &store) != -1;
#elif defined(Q_OS_LINUX)
    return posix_fallocate(file.handle(), 0, static_cast<off_t>(size)) == 0;
#else
    return false;
#endif
}

bool markFileSparse(QFile& file)
{
#if defined(Q_OS_WIN)
    if (!file.isOpen())
        return false;
    const HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()));
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD returned = 0;   // fails on FAT32 / exFAT
    return DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr) != 0;
#else
    Q_UNUSED(file);
    return false;
#endif
}

} // namespace eMule
