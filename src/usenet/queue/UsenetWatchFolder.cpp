#include "pch.h"
/// @file UsenetWatchFolder.cpp
/// @brief A folder dropped .nzb files are picked up from — implementation.

#include "queue/UsenetWatchFolder.h"

#include "queue/UsenetQueue.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QTimer>

namespace eMule::usenet {

namespace {

/// `name`, `name (2)`, `name (3)`… Same shape as UsenetPostProcessor's, and for
/// the same reason: two releases can honestly share a filename.
QString uniqueIn(const QString& dir, const QString& fileName)
{
    QDir target(dir);
    if (!target.exists(fileName))
        return target.filePath(fileName);

    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString suffix = info.suffix().isEmpty() ? QString()
                                                   : QLatin1Char('.') + info.suffix();

    for (int n = 2; n < 10000; ++n) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(n).arg(suffix);
        if (!target.exists(candidate))
            return target.filePath(candidate);
    }
    return target.filePath(fileName + QStringLiteral(".dup"));
}

} // namespace

UsenetWatchFolder::UsenetWatchFolder(UsenetQueue* queue, QObject* parent)
    : QObject(parent)
    , m_queue(queue)
    , m_rescan(new QTimer(this))
{
    m_rescan->setInterval(kRescanMs);
    connect(m_rescan, &QTimer::timeout, this, &UsenetWatchFolder::scan);
}

UsenetWatchFolder::~UsenetWatchFolder() = default;

void UsenetWatchFolder::applyPreferences()
{
    // Already sanitised by Preferences: a directory at or below the temp,
    // incoming or config trees comes back empty, because the scanner would
    // otherwise be reading files the daemon itself is writing.
    const QString wanted = thePrefs.usenetWatchDir();
    if (wanted == m_dir)
        return;

    stopWatching();
    m_dir = wanted;
    m_seen.clear();

    if (m_dir.isEmpty())
        return;

    if (!QDir().mkpath(m_dir)) {
        logWarning(QStringLiteral("Usenet: cannot use the watch folder %1").arg(m_dir));
        m_dir.clear();
        return;
    }

    m_watcher = new QFileSystemWatcher(this);
    m_watcher->addPath(m_dir);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &UsenetWatchFolder::scan);
    m_rescan->start();

    logInfo(QStringLiteral("Usenet: watching %1 for .nzb files").arg(m_dir));

    // Nothing ever fires an event for what was already there.
    scan();
}

void UsenetWatchFolder::scan()
{
    if (m_dir.isEmpty() || m_queue == nullptr)
        return;

    QDir dir(m_dir);
    // Top level only, and never the two output folders: a scanner that reads its
    // own output re-queues every release forever.
    const auto entries = dir.entryInfoList({QStringLiteral("*.nzb")}, QDir::Files);

    const QDateTime now = QDateTime::currentDateTimeUtc();
    QSet<QString> present;

    for (const QFileInfo& info : entries) {
        const QString path = info.absoluteFilePath();
        present.insert(path);

        Sighting& sighting = m_seen[path];
        if (!sighting.firstSeen.isValid()) {
            sighting.firstSeen = now;
            sighting.size = info.size();
            sighting.modified = info.lastModified();
            continue;   // never on first sight: it may still be being written
        }

        if (sighting.size != info.size() || sighting.modified != info.lastModified()) {
            // Still growing. Restart the clock rather than reading a fragment.
            sighting.size = info.size();
            sighting.modified = info.lastModified();
            sighting.firstSeen = now;
            continue;
        }

        if (sighting.firstSeen.msecsTo(now) < kSettleMs)
            continue;

        consume(path);
    }

    // Forget files that are gone, so a name reused later starts a fresh settle.
    for (auto it = m_seen.begin(); it != m_seen.end();)
        it = present.contains(it.key()) ? std::next(it) : m_seen.erase(it);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void UsenetWatchFolder::consume(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        // Very likely still locked by whatever is writing it. Left alone: the
        // next scan is thirty seconds away and costs nothing.
        return;
    }
    const QByteArray data = file.readAll();
    file.close();

    QString error;
    UsenetAddOutcome outcome = UsenetAddOutcome::Failed;
    m_queue->addNzb(data, QFileInfo(path).completeBaseName(), error,
                    {.source = UsenetAddSource::Automatic}, &outcome);
    m_queue->stats().noteAdd(UsenetAddOrigin::WatchFolder, outcome);

    switch (outcome) {
    case UsenetAddOutcome::Added:
        logInfo(QStringLiteral("Usenet: queued \"%1\" from the watch folder")
                    .arg(QFileInfo(path).fileName()));
        fileTo(path, kProcessedDir);
        return;

    case UsenetAddOutcome::Duplicate:
    case UsenetAddOutcome::AlreadyDownloaded:
        // The answer, not a failure. Filing it as failed would be a lie, and
        // leaving it would make every scan re-read it. The reason leads, because
        // it already says which kind of "already" this was — the line used to
        // claim "already queued" for both.
        logInfo(QStringLiteral("Usenet: %1 (\"%2\" was not queued)")
                    .arg(error, QFileInfo(path).fileName()));
        fileTo(path, kProcessedDir);
        return;

    case UsenetAddOutcome::Invalid:
        logWarning(QStringLiteral("Usenet: \"%1\" is not a usable NZB — %2")
                       .arg(QFileInfo(path).fileName(), error));
        fileTo(path, kFailedDir);
        return;

    case UsenetAddOutcome::Failed:
        break;
    }

    // Transient. Bounded, so a file that can never be created on disk does not
    // get retried on every scan for as long as the daemon runs.
    Sighting& sighting = m_seen[path];
    if (++sighting.attempts >= kMaxAttempts) {
        logWarning(QStringLiteral("Usenet: giving up on \"%1\" — %2")
                       .arg(QFileInfo(path).fileName(), error));
        fileTo(path, kFailedDir);
    }
}

void UsenetWatchFolder::fileTo(const QString& path, const char* subdir)
{
    const QString target = QDir(m_dir).filePath(QLatin1String(subdir));
    if (!QDir().mkpath(target)) {
        logWarning(QStringLiteral("Usenet: cannot create %1").arg(target));
        return;
    }

    const QString destination = uniqueIn(target, QFileInfo(path).fileName());
    if (!QFile::rename(path, destination)) {
        logWarning(QStringLiteral("Usenet: could not move %1 into %2")
                       .arg(QFileInfo(path).fileName(), QLatin1String(subdir)));
        return;
    }

    m_seen.remove(path);
}

void UsenetWatchFolder::stopWatching()
{
    m_rescan->stop();
    delete m_watcher;
    m_watcher = nullptr;
}

} // namespace eMule::usenet
