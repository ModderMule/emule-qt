#include "pch.h"
/// @file PartFileConvert.cpp
/// @brief Legacy format converter — port of MFC CPartFileConvert.

#include "files/PartFileConvert.h"
#include "files/PartFile.h"
#include "app/AppContext.h"
#include "files/SharedFileList.h"
#include "transfer/DownloadQueue.h"
#include "utils/OtherFunctions.h"
#include "utils/PathUtils.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

#include <array>
#include <map>
#include <memory>
#include <optional>

namespace eMule {

// Static member definitions
std::list<ConvertJob> PartFileConvert::s_jobs;
QMutex PartFileConvert::s_mutex;
ConvertThread* PartFileConvert::s_thread = nullptr;
QWaitCondition PartFileConvert::s_condition;
bool PartFileConvert::s_running = false;

// ===========================================================================
// ConvertThread
// ===========================================================================

ConvertThread::ConvertThread(QObject* parent)
    : QThread(parent)
{
}

void ConvertThread::requestStop()
{
    QMutexLocker locker(&m_mutex);
    m_stopRequested = true;
    m_condition.wakeAll();
}

void ConvertThread::run()
{
    while (true) {
        ConvertJob* currentJob = nullptr;

        {
            QMutexLocker locker(&PartFileConvert::s_mutex);
            if (m_stopRequested || !PartFileConvert::s_running)
                return;

            // Find the next queued job
            for (auto& job : PartFileConvert::s_jobs) {
                if (job.state == ConvertStatus::Queued) {
                    job.state = ConvertStatus::InProgress;
                    currentJob = &job;
                    break;
                }
            }
        }

        if (!currentJob) {
            // No work available — wait for signal
            QMutexLocker locker(&m_mutex);
            if (m_stopRequested)
                return;
            m_condition.wait(&m_mutex, 1000); // wake periodically or on signal
            continue;
        }

        ConvertStatus result = PartFileConvert::performConvertToeMule(*currentJob);

        {
            QMutexLocker locker(&PartFileConvert::s_mutex);
            currentJob->state = result;
        }

        logInfo(QStringLiteral("PartFileConvert: job '%1' finished with status %2")
                    .arg(currentJob->filename)
                    .arg(static_cast<int>(result)));
    }
}

// ===========================================================================
// PartFileConvert
// ===========================================================================

PartFileConvert::PartFileConvert(QObject* parent)
    : QObject(parent)
{
}

PartFileConvert::~PartFileConvert()
{
    stopThread();
}

// ---------------------------------------------------------------------------
// scanFolderToAdd — scan folder for convertible files
// ---------------------------------------------------------------------------

void PartFileConvert::scanFolderToAdd(const QString& folder, bool recursive, bool removeSource)
{
    QDir dir(folder);
    if (!dir.exists())
        return;

    auto flags = recursive
                     ? QDirIterator::Subdirectories
                     : QDirIterator::NoIteratorFlags;

    // MFC ScanFolderToAdd: every *.part.met and every Shareaza *.sd. What a file
    // really is shows when it is converted.
    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot, flags);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (!fi.fileName().endsWith(QStringLiteral(".part.met"), Qt::CaseInsensitive)
            && fi.suffix().compare(QStringLiteral("sd"), Qt::CaseInsensitive) != 0)
            continue;

        ConvertJob job;
        job.folder = fi.absolutePath();
        job.filename = fi.fileName();
        job.format = detectFormat(fi.absoluteFilePath());
        job.removeSource = removeSource;
        job.state = ConvertStatus::Queued;
        addJob(std::move(job));
    }
}

// ---------------------------------------------------------------------------
// Job management
// ---------------------------------------------------------------------------

void PartFileConvert::addJob(ConvertJob job)
{
    QMutexLocker locker(&s_mutex);
    s_jobs.push_back(std::move(job));
}

void PartFileConvert::removeJob(int index)
{
    QMutexLocker locker(&s_mutex);
    if (index < 0 || index >= static_cast<int>(s_jobs.size()))
        return;
    auto it = s_jobs.begin();
    std::advance(it, index);
    s_jobs.erase(it);
}

void PartFileConvert::removeAllJobs()
{
    QMutexLocker locker(&s_mutex);
    s_jobs.clear();
}

void PartFileConvert::retryJob(int index)
{
    QMutexLocker locker(&s_mutex);
    if (index < 0 || index >= static_cast<int>(s_jobs.size()))
        return;
    auto it = s_jobs.begin();
    std::advance(it, index);
    // Only retry terminal-state jobs
    if (it->state != ConvertStatus::Queued && it->state != ConvertStatus::InProgress)
        it->state = ConvertStatus::Queued;
}

int PartFileConvert::jobCount()
{
    QMutexLocker locker(&s_mutex);
    return static_cast<int>(s_jobs.size());
}

ConvertJob PartFileConvert::jobAt(int index)
{
    QMutexLocker locker(&s_mutex);
    auto it = s_jobs.begin();
    std::advance(it, index);
    return *it;
}

const std::list<ConvertJob>& PartFileConvert::jobs()
{
    return s_jobs;
}

// ---------------------------------------------------------------------------
// Thread management
// ---------------------------------------------------------------------------

void PartFileConvert::startThread()
{
    QMutexLocker locker(&s_mutex);
    if (s_thread && s_thread->isRunning())
        return;

    s_running = true;

    if (!s_thread)
        s_thread = new ConvertThread();

    s_thread->start();
    logInfo(QStringLiteral("PartFileConvert: conversion thread started"));
}

void PartFileConvert::stopThread()
{
    {
        QMutexLocker locker(&s_mutex);
        s_running = false;
    }

    if (s_thread) {
        s_thread->requestStop();
        // The worker may be waiting for this thread (runOnMain), so keep serving it.
        QElapsedTimer deadline;
        deadline.start();
        while (!s_thread->wait(50) && deadline.elapsed() < 30000)
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (s_thread->isRunning())
            s_thread->terminate();
        delete s_thread;
        s_thread = nullptr;
    }
}

// ---------------------------------------------------------------------------
// processQueue — start the thread if there are queued jobs
// ---------------------------------------------------------------------------

void PartFileConvert::processQueue()
{
    QMutexLocker locker(&s_mutex);
    bool hasQueued = false;
    for (const auto& job : s_jobs) {
        if (job.state == ConvertStatus::Queued) {
            hasQueued = true;
            break;
        }
    }

    if (hasQueued) {
        locker.unlock();
        startThread();

        // Wake the thread in case it's sleeping
        if (s_thread) {
            QMutexLocker tl(&s_thread->m_mutex);
            s_thread->m_condition.wakeOne();
        }
    }
}

// ---------------------------------------------------------------------------
// Format detection
// ---------------------------------------------------------------------------

int PartFileConvert::detectFormat(const QString& filePath)
{
    const QFileInfo fi(filePath);
    PartFileFormat format = PartFileFormat::Unknown;
    PartFile probe;
    if (probe.loadPartFile(fi.absolutePath(), fi.fileName(), &format)
        != PartFileLoadResult::CheckSuccess)
        return 0;
    return format == PartFileFormat::BadFormat ? 0 : static_cast<int>(format);
}

// ---------------------------------------------------------------------------
// performConvertToeMule — MFC CPartFileConvert::performConvertToeMule
// (srchybrid/PartFileConvert.cpp:168-381)
// ---------------------------------------------------------------------------

namespace {

/// Queue, shared list and PartFile objects belong to the main thread.
template <typename Fn> void runOnMain(Fn&& fn)
{
    QCoreApplication* app = QCoreApplication::instance();
    if (!app || QThread::currentThread() == app->thread())
        fn();
    else
        QMetaObject::invokeMethod(app, std::forward<Fn>(fn), Qt::BlockingQueuedConnection);
}

/// Rename, or copy and delete where rename cannot (another volume).
bool moveFile(const QString& from, const QString& to)
{
    if (QFile::rename(from, to))
        return true;
    return QFile::copy(from, to) && QFile::remove(from);
}

/// The chunk files of a split download: "<number>.<index>.part", by index.
std::map<int, QString> splitChunks(const QString& folder, const QString& partIndex)
{
    std::map<int, QString> chunks;
    const QDir dir(folder);
    for (const QString& name : dir.entryList({partIndex + QStringLiteral(".*.part")}, QDir::Files)) {
        const qsizetype first = name.indexOf(u'.');
        const qsizetype second = name.indexOf(u'.', first + 1);
        const int index = name.mid(first + 1, second - first - 1).toInt();
        if (index > 0)
            chunks.emplace(index, dir.filePath(name));
    }
    return chunks;
}

} // namespace

ConvertStatus PartFileConvert::performConvertToeMule(ConvertJob& job)
{
    const QString metPath = job.folder + u'/' + job.filename;
    if (!QFile::exists(metPath))
        return ConvertStatus::PartMetNotFound;
    // "001.part.met" -> "001", "Name.ext.sd" -> "Name"
    const QString partIndex = job.filename.left(job.filename.indexOf(u'.'));

    // 1. What is it, and do we have it already?
    PartFileFormat format = PartFileFormat::Unknown;
    PartFileLoadResult checked = PartFileLoadResult::FailedOther;
    std::array<uint8, 16> hash{};
    QString name;
    uint64 size = 0;
    bool duplicate = false;
    QString tempDir;
    runOnMain([&] {
        PartFile probe;
        checked = probe.loadPartFile(job.folder, job.filename, &format);
        if (checked != PartFileLoadResult::CheckSuccess)
            return;
        md4cpy(hash.data(), probe.fileHash());
        name = probe.fileName();
        size = static_cast<uint64>(probe.fileSize());
        duplicate = theApp.downloadQueue && theApp.downloadQueue->fileByID(hash.data());
        tempDir = DownloadQueue::defaultTempDir();
    });
    if (checked != PartFileLoadResult::CheckSuccess || format == PartFileFormat::Unknown
        || format == PartFileFormat::BadFormat || size == 0)
        return ConvertStatus::BadFormat;

    {
        QMutexLocker locker(&s_mutex);
        job.size = size;
        job.fileHash = md4str(hash.data());
        job.format = static_cast<int>(format);
    }
    if (duplicate)
        return ConvertStatus::AlreadyExists;
    if (!theApp.downloadQueue || tempDir.isEmpty())
        return ConvertStatus::Failed;

    // 2. Space. A split download is rebuilt, so it always needs room; a plain one
    //    only when the source stays.
    const bool split = format == PartFileFormat::Splitted;
    QString oldData = metPath;
    oldData.chop(format == PartFileFormat::Shareaza ? 3 : 4);     // ".sd" / ".met"
    const std::map<int, QString> chunks =
        split ? splitChunks(job.folder, partIndex) : std::map<int, QString>{};
    uint64 needed = 0;
    if (split) {
        if (!chunks.empty())
            needed = static_cast<uint64>(chunks.rbegin()->first - 1) * PARTSIZE
                   + static_cast<uint64>(QFileInfo(chunks.rbegin()->second).size());
    } else if (!job.removeSource) {
        needed = static_cast<uint64>(QFileInfo(oldData).size());
    }
    {
        QMutexLocker locker(&s_mutex);
        job.spaceNeeded = needed;
    }
    if (needed > 0) {
        const std::optional<uint64> free = tryFreeDiskSpace(tempDir);
        if (free.has_value() && *free < needed)
            return ConvertStatus::OutOfDiskSpace;
    }

    // 3. A download of our own, under the next free number
    PartFile* file = nullptr;
    QString newPart;
    QString newMet;
    runOnMain([&] {
        auto created = std::make_unique<PartFile>();
        created->setFileHash(hash.data());
        created->setFileName(name, true);
        created->setFileSize(size);
        if (!created->createPartFile(tempDir))
            return;
        created->closeDataFile();
        newMet = created->fullName();
        newPart = newMet.left(newMet.size() - 4);
        file = created.release();
    });
    if (!file)
        return ConvertStatus::Failed;

    bool dataMoved = false;     // the source data now lives under newPart
    const auto abandon = [&](ConvertStatus status) {
        runOnMain([&] { delete file; });    // first: it may still write its met
        if (dataMoved)
            moveFile(newPart, oldData);     // give it back
        else
            QFile::remove(newPart);
        QFile::remove(newMet);
        QFile::remove(newMet + QStringLiteral(".bak"));
        QFile::remove(newMet + QStringLiteral(".backup"));
        return status;
    };

    // 4. The data
    if (split) {
        QFile out(newPart);
        if (!out.open(QIODevice::ReadWrite))
            return abandon(ConvertStatus::IOError);
        for (const auto& [index, path] : chunks) {
            QFile in(path);
            if (!in.open(QIODevice::ReadOnly))
                return abandon(ConvertStatus::IOError);
            const QByteArray data = in.read(static_cast<qint64>(PARTSIZE));
            // Chunk n holds part n. (The reference tree's "index * PARTSIZE +
            // PARTSIZE - 1" at PartFileConvert.cpp:268 lands every chunk two parts on.)
            if (!out.seek(static_cast<qint64>(static_cast<uint64>(index - 1) * PARTSIZE))
                || out.write(data) != data.size())
                return abandon(ConvertStatus::IOError);
        }
        if (!out.flush())
            return abandon(ConvertStatus::IOError);
    } else {
        QFile::remove(newPart);
        bool ok;
        if (!QFile::exists(oldData)) {
            QFile empty(newPart);       // metadata without data: all of it is missing
            ok = empty.open(QIODevice::WriteOnly);
        } else if (job.removeSource) {
            ok = dataMoved = moveFile(oldData, newPart);
        } else {
            ok = QFile::copy(oldData, newPart);
        }
        if (!ok)
            return abandon(ConvertStatus::Failed);
    }

    // 5. The metadata: always a copy, so a file that does not load costs nothing
    QFile::remove(newMet);
    if (!QFile::copy(metPath, newMet))
        return abandon(ConvertStatus::Failed);
    QFile::setPermissions(newMet, QFile::permissions(newMet) | QFile::WriteOwner);

    PartFileLoadResult loaded = PartFileLoadResult::FailedOther;
    runOnMain([&] {
        file->resetForImportLoad();
        loaded = file->loadPartFile(tempDir, QFileInfo(newMet).fileName());
        if (loaded != PartFileLoadResult::LoadSuccess)
            return;
        if (format == PartFileFormat::NewOld || format == PartFileFormat::Splitted)
            file->resetImportedCounters();

        // 6. Queue it
        theApp.downloadQueue->addDownload(file, thePrefs.addNewFilesPaused());
        file->savePartFile();
        if (theApp.sharedFileList
            && file->status(/*ignorePause=*/true) == PartFileStatus::Ready)
            theApp.sharedFileList->safeAddKFile(file);
        file->finishLoadedDownload();
    });
    if (loaded != PartFileLoadResult::LoadSuccess)
        return abandon(ConvertStatus::BadFormat);

    // 7. Only now, and only what was imported, is removed. (MFC unlinks every
    //    "<number>.*" in the folder, which for "Name.ext.sd" is every "Name.*".)
    if (job.removeSource) {
        QFile::remove(metPath);
        QFile::remove(metPath + QStringLiteral(".bak"));
        QFile::remove(metPath + QStringLiteral(".backup"));
        if (!split)
            QFile::remove(oldData);     // gone already unless it was copied across volumes
        for (const auto& [index, path] : chunks)
            QFile::remove(path);
        if (split)
            QDir().rmdir(job.folder);   // only if empty
    }

    logInfo(QStringLiteral("Imported download: %1").arg(name));
    return ConvertStatus::OK;
}

} // namespace eMule
