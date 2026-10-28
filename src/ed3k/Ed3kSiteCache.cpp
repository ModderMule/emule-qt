/// @file Ed3kSiteCache.cpp
/// @brief Website manifests and members on disk (name.md §6.4) — implementation.

#include "Ed3kSiteCache.h"

#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <span>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

/// a file of this cache (others in the folder are left alone)
bool ours(const QString& name)
{
    return name.startsWith(QLatin1String("m-")) || name.startsWith(QLatin1String("s-"));
}

std::span<const std::byte> spanOf(const QByteArray& b)
{
    return std::as_bytes(std::span{b.constData(), std::size_t(b.size())});
}

void touch(const QString& path)
{
    QFile f(path);
    if (f.open(QIODevice::ReadWrite))
        f.setFileTime(QDateTime::currentDateTimeUtc(), QFileDevice::FileModificationTime);
}

} // namespace

Ed3kSiteCache::Ed3kSiteCache(QString dir, std::uint64_t max)
    : m_dir(std::move(dir))
    , m_max(max)
{
    scan();
}

void Ed3kSiteCache::setMax(std::uint64_t max)
{
    m_max = max;
    evict();
}

void Ed3kSiteCache::putManifest(const QString& site, const QByteArray& manifest)
{
    if (site.isEmpty() || manifest.isEmpty())
        return;
    put(manifestName(site), manifest);
}

std::optional<QByteArray> Ed3kSiteCache::manifest(const QString& site)
{
    return site.isEmpty() ? std::nullopt : read(manifestName(site));
}

void Ed3kSiteCache::putMember(const ffi::Root& root, const QByteArray& data)
{
    const QString name = memberName(root);
    if (QFileInfo::exists(path(name))) {
        touch(path(name));
        return;
    }
    put(name, data);
}

std::optional<QByteArray> Ed3kSiteCache::member(const ffi::Root& root)
{
    const QString name = memberName(root);
    auto data = read(name);
    if (!data)
        return std::nullopt;
    // only verified bytes are sent (§6.3): a file that is not under its root is dropped
    ffi::Hasher h;
    const bool fed = bool(h.update(spanOf(*data)));
    const auto done = h.finish();
    if (!fed || !done || done->root != root || done->size != std::uint64_t(data->size())) {
        logEd3kWarning(QStringLiteral("ed3k: a kept website member is not under its root, removed"));
        remove(name);
        return std::nullopt;
    }
    return data;
}

bool Ed3kSiteCache::hasMember(const ffi::Root& root) const
{
    return QFileInfo::exists(path(memberName(root)));
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kSiteCache::scan()
{
    m_bytes = 0;
    const QDir d(m_dir);
    for (const QFileInfo& fi : d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot))
        if (ours(fi.fileName()))
            m_bytes += std::uint64_t(std::max<qint64>(0, fi.size()));
    evict();
}

void Ed3kSiteCache::put(const QString& name, const QByteArray& data)
{
    // one larger than all is not kept at all
    if (std::uint64_t(data.size()) > m_max)
        return;
    const QString p = path(name);
    const qint64 old = QFileInfo(p).size();
    QDir().mkpath(m_dir);
    if (!writeBlob(p, spanOf(data))) {
        logEd3kWarning(QStringLiteral("ed3k: a website file was not kept (%1)").arg(name.left(10)));
        return;
    }
    m_bytes -= std::min<std::uint64_t>(m_bytes, std::uint64_t(std::max<qint64>(0, old)));
    m_bytes += std::uint64_t(data.size());
    evict(name);
}

std::optional<QByteArray> Ed3kSiteCache::read(const QString& name)
{
    const QString p = path(name);
    if (!QFileInfo::exists(p))
        return std::nullopt;
    QByteArray data = readBlob(p);
    if (data.isEmpty())
        return std::nullopt;
    touch(p);
    return data;
}

void Ed3kSiteCache::remove(const QString& name)
{
    const QString p = path(name);
    const qint64 size = QFileInfo(p).size();
    if (QFile::remove(p))
        m_bytes -= std::min<std::uint64_t>(m_bytes, std::uint64_t(std::max<qint64>(0, size)));
}

void Ed3kSiteCache::evict(const QString& keep)
{
    if (m_bytes <= m_max)
        return;
    // the sites viewed longest ago first: every file is touched when used
    QFileInfoList files = QDir(m_dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed);
    for (const QFileInfo& fi : files) {
        if (m_bytes <= m_max)
            break;
        if (!ours(fi.fileName()) || fi.fileName() == keep)
            continue;
        remove(fi.fileName());
    }
}

QString Ed3kSiteCache::path(const QString& name) const
{
    return QDir(m_dir).filePath(name);
}

QString Ed3kSiteCache::memberName(const ffi::Root& root)
{
    return QStringLiteral("m-")
           + QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(root.bytes.data()), qsizetype(root.bytes.size())).toHex());
}

QString Ed3kSiteCache::manifestName(const QString& site)
{
    return QStringLiteral("s-") + site;
}

} // namespace eMule::ed3k
