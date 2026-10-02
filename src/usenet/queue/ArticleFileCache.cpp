#include "queue/ArticleFileCache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace eMule::usenet {

namespace {

/// Expired entries are swept once the map grows past this.
constexpr qsizetype kPruneThreshold = 32;

} // namespace

std::shared_ptr<QFile> ArticleFileCache::acquire(const QString& path, QString& error)
{
    if (const auto it = m_files.constFind(path); it != m_files.constEnd()) {
        if (auto file = it->lock())
            return file;
    }

    if (m_files.size() > kPruneThreshold)
        m_files.removeIf([](const auto& entry) { return entry.value().expired(); });

    auto file = openFile(path, error);
    if (file)
        m_files.insert(path, file);
    return file;
}

int ArticleFileCache::openCount() const
{
    return int(std::ranges::count_if(m_files, [](const auto& w) { return !w.expired(); }));
}

std::shared_ptr<QFile> ArticleFileCache::openFile(const QString& path, QString& error)
{
    auto file = std::make_shared<QFile>(path);
    // ReadWrite rather than WriteOnly: WriteOnly implies Truncate for some
    // backends, and truncating is precisely wrong for a resumed download.
    constexpr auto mode = QIODevice::ReadWrite | QIODevice::Unbuffered;
    if (file->open(mode))
        return file;

    const QDir parent = QFileInfo(path).absoluteDir();
    if (parent.exists()) {
        error = file->errorString();
        return {};
    }
    if (!parent.mkpath(QStringLiteral("."))) {
        error = QStringLiteral("Could not create %1").arg(parent.absolutePath());
        return {};
    }
    if (!file->open(mode)) {
        error = file->errorString();
        return {};
    }
    return file;
}

} // namespace eMule::usenet
