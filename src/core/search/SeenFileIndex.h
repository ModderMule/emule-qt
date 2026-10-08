#pragma once

/// @file SeenFileIndex.h
/// @brief Remembers which files — and under which names — the client has come across.
///
/// Every hash seen in a search result or in our own share is kept with the names
/// it went by, in `<ConfigDir>/seenfiles.db` (SQLite). A later search can then
/// say "seen before" and how many names a file has had, which is the memory a
/// fake-file check or a "hide what I know" filter needs.
///
/// Bounded from the start: a cap on files, a cap on names per file, and an age
/// after which an entry nobody has seen again is dropped.
///
/// Writes never run on the calling thread: sightings are collected in memory and
/// handed to a worker that owns the write connection. Lookups are point reads on
/// a second connection.

#include "utils/Types.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <array>

class QThread;

namespace eMule {

class SeenFileWriter;

inline constexpr auto kSeenFileDbName = "seenfiles.db";

class SeenFileIndex : public QObject {
    Q_OBJECT

public:
    struct Limits {
        int maxFiles = 200'000;
        int maxNamesPerFile = 8;
        qint64 maxAgeSecs = 180ll * 24 * 3600;
    };

    /// What is known about one file.
    struct Info {
        bool known = false;
        qint64 firstSeen = 0;    ///< unix seconds
        qint64 lastSeen = 0;
        uint32 seenCount = 0;
        int names = 0;           ///< distinct names on record
        QStringList nameList;    ///< those names, at most maxNamesPerFile

        /// True when the file was on record before @p since.
        [[nodiscard]] bool seenBefore(qint64 since) const { return known && firstSeen < since; }
    };

    explicit SeenFileIndex(QObject* parent = nullptr);
    ~SeenFileIndex() override;

    /// Open (and create) the store. False — and the index stays inert — when Qt has
    /// no SQLite driver, the file cannot be opened, or it was written by a newer
    /// version of this class.
    bool open(const QString& path);
    [[nodiscard]] bool isOpen() const { return m_open; }

    /// The user's switch. Off: nothing is recorded and nothing looked up; the file
    /// stays as it is.
    void setEnabled(bool enabled) { m_enabled = enabled; }
    [[nodiscard]] bool isActive() const { return m_open && m_enabled; }

    void setLimits(const Limits& limits);

    /// One sighting. Cheap: kept in memory until the next flush.
    void note(const uint8* fileHash, const QString& name, uint64 size, qint64 nowSecs);

    /// What was known before the sightings still waiting to be written. A file first
    /// noted a moment ago is therefore reported as known, with that moment.
    [[nodiscard]] Info lookup(const uint8* fileHash);

    /// Write what is waiting, and wait for it. Called on a timer otherwise.
    void flush();

    /// Drop what is too old and what is over the cap, and wait for it.
    void prune(qint64 nowSecs);

    [[nodiscard]] int pendingCount() const { return static_cast<int>(m_pending.size()); }

    /// One file's waiting sightings (also the unit handed to the writer).
    struct Sighting {
        uint64 size = 0;
        qint64 firstSeen = 0;
        qint64 lastSeen = 0;
        uint32 count = 0;
        QHash<QString, uint32> names;   ///< name -> times seen
    };
    using Batch = QHash<QByteArray, Sighting>;

private:
    void scheduleFlush();
    void close();

    static constexpr int kFlushMs = 30'000;
    static constexpr int kFlushAtPending = 5'000;
    static constexpr int kPruneEveryMs = 60 * 60 * 1000;
    static constexpr int kMaxNameChars = 255;

    QThread* m_thread = nullptr;
    SeenFileWriter* m_writer = nullptr;
    QString m_readConnection;
    Limits m_limits;
    Batch m_pending;
    QTimer m_flushTimer;
    QTimer m_pruneTimer;
    bool m_open = false;
    bool m_enabled = true;
};

/// Owns the write connection; lives on the index's worker thread. Not for use
/// outside SeenFileIndex.
class SeenFileWriter : public QObject {
    Q_OBJECT

public:
    SeenFileWriter();

public slots:
    bool open(const QString& path);
    void write(const eMule::SeenFileIndex::Batch& batch, int maxNamesPerFile);
    void prune(qint64 nowSecs, qint64 maxAgeSecs, int maxFiles);
    void close();

private:
    QString m_connection;
    bool m_isOpen = false;
};

} // namespace eMule

Q_DECLARE_METATYPE(eMule::SeenFileIndex::Batch)
