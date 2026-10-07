#include "pch.h"
/// @file SeenFileIndex.cpp
/// @brief Seen-files index — implementation.

#include "search/SeenFileIndex.h"

#include "utils/Log.h"

#include <QAtomicInt>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>

namespace eMule {

namespace {

constexpr int kSchemaVersion = 1;

QString nextConnectionName(const char* role)
{
    static QAtomicInt counter;
    return QStringLiteral("seenfiles-%1-%2").arg(QLatin1String(role)).arg(counter.fetchAndAddRelaxed(1));
}

bool exec(QSqlQuery& query, const QString& sql)
{
    if (query.exec(sql))
        return true;
    logWarning(QStringLiteral("seenfiles.db: %1 — %2").arg(sql, query.lastError().text()));
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// SeenFileWriter — owns the write connection, lives on the worker thread
// ---------------------------------------------------------------------------

SeenFileWriter::SeenFileWriter()
: m_connection(nextConnectionName("write"))
{
}

bool SeenFileWriter::open(const QString& path)
{
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
        db.setDatabaseName(path);
        if (!db.open()) {
            logWarning(QStringLiteral("seenfiles.db: cannot open %1 — %2")
                           .arg(path, db.lastError().text()));
            return false;
        }

        QSqlQuery query(db);
        // Readers do not wait for the writer, and a commit does not sync the disk
        // on its own: losing the last half minute of sightings in a crash is fine.
        exec(query, QStringLiteral("PRAGMA journal_mode=WAL"));
        exec(query, QStringLiteral("PRAGMA synchronous=NORMAL"));

        int version = 0;
        if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next())
            version = query.value(0).toInt();
        if (version > kSchemaVersion) {
            logWarning(QStringLiteral("seenfiles.db was written by a newer version (%1) — "
                                      "left untouched, seen-file memory is off").arg(version));
            db.close();
            return false;
        }
        if (version == 0) {
            const bool ok =
                exec(query, QStringLiteral(
                    "CREATE TABLE IF NOT EXISTS files ("
                    "hash BLOB PRIMARY KEY, size INTEGER NOT NULL, first_seen INTEGER NOT NULL,"
                    " last_seen INTEGER NOT NULL, seen_count INTEGER NOT NULL) WITHOUT ROWID"))
                && exec(query, QStringLiteral(
                    "CREATE TABLE IF NOT EXISTS names ("
                    "hash BLOB NOT NULL, name TEXT NOT NULL, last_seen INTEGER NOT NULL,"
                    " count INTEGER NOT NULL, PRIMARY KEY (hash, name)) WITHOUT ROWID"))
                && exec(query, QStringLiteral(
                    "CREATE INDEX IF NOT EXISTS files_last_seen ON files (last_seen)"))
                && exec(query, QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion));
            if (!ok) {
                db.close();
                return false;
            }
        }
    }
    m_isOpen = true;
    return true;
}

void SeenFileWriter::write(const eMule::SeenFileIndex::Batch& batch, int maxNamesPerFile)
{
    if (!m_isOpen || batch.isEmpty())
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    if (!db.transaction())
        return;

    QSqlQuery file(db);
    file.prepare(QStringLiteral(
        "INSERT INTO files (hash, size, first_seen, last_seen, seen_count) VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT (hash) DO UPDATE SET last_seen = max(last_seen, excluded.last_seen), "
        "seen_count = seen_count + excluded.seen_count"));
    QSqlQuery name(db);
    name.prepare(QStringLiteral(
        "INSERT INTO names (hash, name, last_seen, count) VALUES (?, ?, ?, ?) "
        "ON CONFLICT (hash, name) DO UPDATE SET last_seen = max(last_seen, excluded.last_seen), "
        "count = count + excluded.count"));
    // Keep the names seen most often; a file with a hundred names is spam, and
    // the count on record says so without keeping them all.
    QSqlQuery trim(db);
    trim.prepare(QStringLiteral(
        "DELETE FROM names WHERE hash = ? AND name NOT IN ("
        "SELECT name FROM names WHERE hash = ? ORDER BY count DESC, last_seen DESC LIMIT ?)"));

    for (auto it = batch.constBegin(); it != batch.constEnd(); ++it) {
        const SeenFileIndex::Sighting& seen = it.value();
        file.addBindValue(it.key());
        file.addBindValue(static_cast<qlonglong>(seen.size));
        file.addBindValue(seen.firstSeen);
        file.addBindValue(seen.lastSeen);
        file.addBindValue(seen.count);
        file.exec();

        for (auto n = seen.names.constBegin(); n != seen.names.constEnd(); ++n) {
            name.addBindValue(it.key());
            name.addBindValue(n.key());
            name.addBindValue(seen.lastSeen);
            name.addBindValue(n.value());
            name.exec();
        }
        if (!seen.names.isEmpty()) {
            trim.addBindValue(it.key());
            trim.addBindValue(it.key());
            trim.addBindValue(maxNamesPerFile);
            if (!trim.exec())
                logWarning(QStringLiteral("seenfiles.db: %1").arg(trim.lastError().text()));
        }
    }
    if (!db.commit())
        logWarning(QStringLiteral("seenfiles.db: commit failed — %1").arg(db.lastError().text()));
}

void SeenFileWriter::prune(qint64 nowSecs, qint64 maxAgeSecs, int maxFiles)
{
    if (!m_isOpen)
        return;
    QSqlDatabase db = QSqlDatabase::database(m_connection);
    if (!db.transaction())
        return;

    QSqlQuery query(db);
    query.prepare(QStringLiteral("DELETE FROM files WHERE last_seen < ?"));
    query.addBindValue(nowSecs - maxAgeSecs);
    query.exec();

    // Over the cap: the ones not seen for longest go first.
    query.prepare(QStringLiteral(
        "DELETE FROM files WHERE hash IN (SELECT hash FROM files ORDER BY last_seen ASC "
        "LIMIT max(0, (SELECT count(*) FROM files) - ?))"));
    query.addBindValue(maxFiles);
    query.exec();

    exec(query, QStringLiteral(
        "DELETE FROM names WHERE NOT EXISTS (SELECT 1 FROM files WHERE files.hash = names.hash)"));
    db.commit();
}

void SeenFileWriter::close()
{
    if (m_isOpen) {
        { QSqlDatabase db = QSqlDatabase::database(m_connection); db.close(); }
        m_isOpen = false;
    }
    QSqlDatabase::removeDatabase(m_connection);
}

// ---------------------------------------------------------------------------
// SeenFileIndex
// ---------------------------------------------------------------------------

SeenFileIndex::SeenFileIndex(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<eMule::SeenFileIndex::Batch>();

    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(kFlushMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &SeenFileIndex::flush);

    m_pruneTimer.setInterval(kPruneEveryMs);
    connect(&m_pruneTimer, &QTimer::timeout, this, [this] {
        prune(QDateTime::currentSecsSinceEpoch());
    });
}

SeenFileIndex::~SeenFileIndex()
{
    close();
}

bool SeenFileIndex::open(const QString& path)
{
    close();

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        logWarning(QStringLiteral("No SQLite driver in this Qt — files seen in searches are not remembered"));
        return false;
    }

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("SeenFiles"));
    m_writer = new SeenFileWriter;
    m_writer->moveToThread(m_thread);
    m_thread->start();

    bool opened = false;
    QMetaObject::invokeMethod(m_writer, "open", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(bool, opened), Q_ARG(QString, path));
    if (opened) {
        // After the writer: the schema exists by now. Never written through.
        m_readConnection = nextConnectionName("read");
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_readConnection);
        db.setDatabaseName(path);
        opened = db.open();
        if (!opened)
            logWarning(QStringLiteral("seenfiles.db: cannot open %1 for reading — %2")
                           .arg(path, db.lastError().text()));
    }
    if (!opened) {
        close();
        return false;
    }

    m_open = true;
    m_pruneTimer.start();
    prune(QDateTime::currentSecsSinceEpoch());
    return true;
}

void SeenFileIndex::setLimits(const Limits& limits)
{
    m_limits = limits;
}

void SeenFileIndex::note(const uint8* fileHash, const QString& name, uint64 size, qint64 nowSecs)
{
    if (!isActive() || !fileHash)
        return;

    Sighting& seen = m_pending[QByteArray(reinterpret_cast<const char*>(fileHash), 16)];
    if (seen.count == 0)
        seen.firstSeen = nowSecs;
    seen.lastSeen = nowSecs;
    seen.size = size;
    ++seen.count;
    if (const QString kept = name.left(kMaxNameChars); !kept.isEmpty())
        ++seen.names[kept];

    if (m_pending.size() >= kFlushAtPending)
        flush();
    else
        scheduleFlush();
}

SeenFileIndex::Info SeenFileIndex::lookup(const uint8* fileHash)
{
    Info info;
    if (!isActive() || !fileHash)
        return info;
    const QByteArray key(reinterpret_cast<const char*>(fileHash), 16);

    QSet<QString> pendingNames;
    if (const auto it = m_pending.constFind(key); it != m_pending.constEnd()) {
        info.known = true;
        info.firstSeen = it->firstSeen;
        info.lastSeen = it->lastSeen;
        info.seenCount = it->count;
        pendingNames = QSet<QString>(it->names.keyBegin(), it->names.keyEnd());
    }

    QSqlQuery query(QSqlDatabase::database(m_readConnection));
    query.prepare(QStringLiteral("SELECT first_seen, last_seen, seen_count FROM files WHERE hash = ?"));
    query.addBindValue(key);
    if (query.exec() && query.next()) {
        const qint64 first = query.value(0).toLongLong();
        info.firstSeen = info.known ? std::min(info.firstSeen, first) : first;
        info.lastSeen = std::max(info.lastSeen, query.value(1).toLongLong());
        info.seenCount += query.value(2).toUInt();
        info.known = true;

        query.prepare(QStringLiteral("SELECT name FROM names WHERE hash = ?"));
        query.addBindValue(key);
        if (query.exec())
            while (query.next())
                pendingNames.insert(query.value(0).toString());
    }
    info.names = std::min(static_cast<int>(pendingNames.size()), m_limits.maxNamesPerFile);
    return info;
}

void SeenFileIndex::flush()
{
    m_flushTimer.stop();
    if (!m_open || m_pending.isEmpty())
        return;
    Batch batch;
    batch.swap(m_pending);
    // Blocking only for the hand-over of the batch to the worker's queue would not
    // do: a lookup right after must see it. Batches are small (seconds of sightings).
    QMetaObject::invokeMethod(m_writer, "write", Qt::BlockingQueuedConnection,
                              Q_ARG(eMule::SeenFileIndex::Batch, batch),
                              Q_ARG(int, m_limits.maxNamesPerFile));
}

void SeenFileIndex::prune(qint64 nowSecs)
{
    if (!m_open)
        return;
    QMetaObject::invokeMethod(m_writer, "prune", Qt::BlockingQueuedConnection,
                              Q_ARG(qint64, nowSecs), Q_ARG(qint64, m_limits.maxAgeSecs),
                              Q_ARG(int, m_limits.maxFiles));
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void SeenFileIndex::scheduleFlush()
{
    if (!m_flushTimer.isActive())
        m_flushTimer.start();
}

void SeenFileIndex::close()
{
    if (m_open)
        flush();
    m_open = false;
    m_flushTimer.stop();
    m_pruneTimer.stop();
    m_pending.clear();

    if (!m_readConnection.isEmpty()) {
        { QSqlDatabase db = QSqlDatabase::database(m_readConnection, false); db.close(); }
        QSqlDatabase::removeDatabase(m_readConnection);
        m_readConnection.clear();
    }
    if (m_thread) {
        QMetaObject::invokeMethod(m_writer, "close", Qt::BlockingQueuedConnection);
        m_thread->quit();
        m_thread->wait();
        delete m_writer;
        m_writer = nullptr;
        delete m_thread;
        m_thread = nullptr;
    }
}

} // namespace eMule
