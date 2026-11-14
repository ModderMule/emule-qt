#include "pch.h"

#include "utils/FileRelocate.h"
#include "utils/PathUtils.h"
#include "utils/StringUtils.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <algorithm>
#include <optional>

namespace eMule {

namespace {

/// Room for @p need bytes in @p dir's volume; empty, else why not.
QString checkRoom(const QString& dir, std::uint64_t need)
{
    if (const std::optional<std::uint64_t> free = tryFreeDiskSpace(dir); free && *free < need)
        return QStringLiteral("not enough disk space (%1 free, %2 needed)")
            .arg(formatByteSize(qint64(*free)), formatByteSize(qint64(need)));
    return {};
}

/// A sparse-keeping copy of @p src to @p dst through a staged name.
QString copyFile(const QString& src, const QString& dst)
{
    const QString staged = dst + QStringLiteral(".moving");
    QFile in(src);
    QFile out(staged);
    if (!in.open(QIODevice::ReadOnly))
        return in.errorString();
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return out.errorString();
    const qint64 size = in.size();
    QString error;
    if (!out.resize(size))
        error = out.errorString();
    constexpr qint64 kChunk = 4 * 1024 * 1024;
    QByteArray buf;
    for (qint64 pos = 0; error.isEmpty() && pos < size; pos += kChunk) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            error = QStringLiteral("interrupted");
            break;
        }
        buf = in.read(std::min(kChunk, size - pos));
        if (buf.isEmpty()) {
            error = in.errorString();
            break;
        }
        if (std::ranges::all_of(buf, [](char c) { return c == 0; }))
            continue;   // a hole stays a hole
        if (!out.seek(pos) || out.write(buf) != buf.size())
            error = out.errorString();
    }
    if (error.isEmpty() && !out.flush())
        error = out.errorString();
    out.close();
    if (error.isEmpty()) {
        QFile::remove(dst);
        if (!QFile::rename(staged, dst))
            error = QStringLiteral("cannot rename %1").arg(staged);
    }
    if (!error.isEmpty())
        QFile::remove(staged);
    return error;
}

} // namespace

QString relocateFile(const QString& src, const QString& dst, std::uint64_t floor, bool removeSource, bool* renamed,
                     std::uint64_t dataSize)
{
    if (renamed)
        *renamed = false;
    if (!QFile::exists(src))
        return QStringLiteral("%1 is missing").arg(src);
    QDir().mkpath(QFileInfo(dst).absolutePath());
    QFile::remove(dst);
    if (QFile::rename(src, dst)) {
        if (renamed)
            *renamed = true;
        return {};
    }
    // another volume
    const std::uint64_t size = dataSize ? dataSize : std::uint64_t(QFileInfo(src).size());
    if (QString error = checkRoom(QFileInfo(dst).absolutePath(), size + floor);
        !error.isEmpty())
        return error;
    if (QString error = copyFile(src, dst); !error.isEmpty())
        return error;
    if (removeSource)
        QFile::remove(src);
    return {};
}

QString relocateDir(const QString& from, const QString& to, std::uint64_t floor)
{
    if (QFileInfo::exists(to))
        return QStringLiteral("%1 already exists").arg(to);
    if (!QFileInfo::exists(from))
        return QDir().mkpath(to) ? QString() : QStringLiteral("cannot create %1").arg(to);
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (QDir().rename(from, to))
        return {};

    std::uint64_t need = floor;
    for (QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories); it.hasNext();)
        need += std::uint64_t(it.nextFileInfo().size());
    QString error = checkRoom(QFileInfo(to).absolutePath(), need);
    for (QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories); error.isEmpty() && it.hasNext();) {
        const QString src = it.next();
        const QString dst = to + src.mid(from.size());
        QDir().mkpath(QFileInfo(dst).absolutePath());
        error = copyFile(src, dst);
    }
    if (!error.isEmpty()) {
        QDir(to).removeRecursively();
        return error;
    }
    QDir(from).removeRecursively();
    return {};
}

} // namespace eMule
