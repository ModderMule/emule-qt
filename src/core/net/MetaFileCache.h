#pragma once

/// @file MetaFileCache.h
/// @brief Fetched .torrent/.nzb files, kept a few hours in memory.
///
/// A search row's Details fetches the metafile to show its files; the
/// Download that usually follows takes the same bytes from here instead of
/// spending another request (an indexer counts grabs, a Meta API may count
/// downloads). Only successful fetches go in. Memory only: a restart forgets.

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QtGlobal>

#include <functional>
#include <optional>

namespace eMule {

class MetaFileCache {
public:
    struct Entry {
        QByteArray content;
        QString fileName;      ///< as served, may be empty
        QString contentType;   ///< as served, may be empty
        qint64 storedAtMs = 0;
    };

    static constexpr qint64 kDefaultTtlMs = 3LL * 60 * 60 * 1000;
    static constexpr qint64 kDefaultMaxBytes = 64LL * 1024 * 1024;

    MetaFileCache(qint64 ttlMs = kDefaultTtlMs, qint64 maxBytes = kDefaultMaxBytes);

    /// The daemon's one cache.
    static MetaFileCache& instance();

    /// A live entry, or nothing once expired.
    [[nodiscard]] std::optional<Entry> find(const QString& key);

    /// Store a fetched file. Empty content or one over the size cap is ignored.
    void insert(const QString& key, const QByteArray& content, const QString& fileName = {},
                const QString& contentType = {});

    void remove(const QString& key);
    void clear();

    [[nodiscard]] qsizetype count() const { return m_entries.size(); }
    [[nodiscard]] qint64 totalBytes() const { return m_bytes; }

    /// Tests set a fake clock (ms since epoch).
    void setClock(std::function<qint64()> clock) { m_clock = std::move(clock); }

    /// Key of an eNode meta row: server and meta hash.
    [[nodiscard]] static QString enodeKey(const QString& serverAddr, const QByteArray& hash16);
    /// Key of an indexer row: its result id (slug + guid).
    [[nodiscard]] static QString indexerKey(const QString& resultId);

private:
    void prune();
    [[nodiscard]] qint64 now() const;

    qint64 m_ttlMs;
    qint64 m_maxBytes;
    qint64 m_bytes = 0;
    QHash<QString, Entry> m_entries;
    std::function<qint64()> m_clock;
};

} // namespace eMule
