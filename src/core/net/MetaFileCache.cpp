/// @file MetaFileCache.cpp
/// @brief Fetched .torrent/.nzb files, kept a few hours in memory.

#include "net/MetaFileCache.h"

#include <QDateTime>

namespace eMule {

MetaFileCache::MetaFileCache(qint64 ttlMs, qint64 maxBytes)
    : m_ttlMs(ttlMs)
    , m_maxBytes(maxBytes)
{
}

MetaFileCache& MetaFileCache::instance()
{
    static MetaFileCache cache;
    return cache;
}

std::optional<MetaFileCache::Entry> MetaFileCache::find(const QString& key)
{
    prune();
    const auto it = m_entries.constFind(key);
    if (it == m_entries.cend())
        return std::nullopt;
    return *it;
}

void MetaFileCache::insert(const QString& key, const QByteArray& content, const QString& fileName,
                           const QString& contentType)
{
    if (key.isEmpty() || content.isEmpty() || content.size() > m_maxBytes)
        return;
    remove(key);
    m_entries.insert(key, {content, fileName, contentType, now()});
    m_bytes += content.size();
    prune();
}

void MetaFileCache::remove(const QString& key)
{
    const auto it = m_entries.constFind(key);
    if (it == m_entries.cend())
        return;
    m_bytes -= it->content.size();
    m_entries.erase(it);
}

void MetaFileCache::clear()
{
    m_entries.clear();
    m_bytes = 0;
}

QString MetaFileCache::enodeKey(const QString& serverAddr, const QByteArray& hash16)
{
    return QStringLiteral("enode:%1:%2").arg(serverAddr, QString::fromLatin1(hash16.toHex()));
}

QString MetaFileCache::indexerKey(const QString& resultId)
{
    return QStringLiteral("indexer:") + resultId;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void MetaFileCache::prune()
{
    const qint64 cutoff = now() - m_ttlMs;
    m_entries.removeIf([this, cutoff](const auto& it) {
        if (it.value().storedAtMs > cutoff)
            return false;
        m_bytes -= it.value().content.size();
        return true;
    });
    // over the cap: oldest out
    while (m_bytes > m_maxBytes && !m_entries.isEmpty()) {
        auto oldest = m_entries.begin();
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
            if (it->storedAtMs < oldest->storedAtMs)
                oldest = it;
        m_bytes -= oldest->content.size();
        m_entries.erase(oldest);
    }
}

qint64 MetaFileCache::now() const
{
    return m_clock ? m_clock() : QDateTime::currentMSecsSinceEpoch();
}

} // namespace eMule
