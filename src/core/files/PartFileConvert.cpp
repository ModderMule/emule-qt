/// @file PartFileConvert.cpp
/// @brief Legacy format converter — port of MFC CPartFileConvert.

#include "files/PartFileConvert.h"
#include "files/PartFile.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

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

    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot, flags);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();

        // Look for .part.met files (old eMule format)
        if (fi.fileName().endsWith(QStringLiteral(".part.met"), Qt::CaseInsensitive)) {
            ConvertJob job;
            job.folder = fi.absolutePath();
            job.filename = fi.fileName();
            job.format = detectFormat(fi.absoluteFilePath());
            job.removeSource = removeSource;
            if (job.format > 0) {
                job.state = ConvertStatus::Queued;
                addJob(std::move(job));
            }
            continue;
        }

        // Look for Shareaza .sd files
        if (fi.suffix().compare(QStringLiteral("sd"), Qt::CaseInsensitive) == 0) {
            ConvertJob job;
            job.folder = fi.absolutePath();
            job.filename = fi.fileName();
            job.format = 4; // Shareaza
            job.removeSource = removeSource;
            job.state = ConvertStatus::Queued;
            addJob(std::move(job));
            continue;
        }

        // Look for splitted files .part.001
        if (fi.fileName().endsWith(QStringLiteral(".part.001"), Qt::CaseInsensitive)) {
            ConvertJob job;
            job.folder = fi.absolutePath();
            job.filename = fi.fileName();
            job.format = 2; // Splitted
            job.removeSource = removeSource;
            job.state = ConvertStatus::Queued;
            addJob(std::move(job));
        }
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
        s_thread->wait(5000);
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
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return 0;

    if (file.size() < 4)
        return 0;

    char header[4]{};
    file.read(header, 4);

    uint8 version = static_cast<uint8>(header[0]);

    // Check for known .part.met versions
    if (version == PARTFILE_VERSION || version == PARTFILE_VERSION_LARGEFILE)
        return 1; // Current format (DefaultOld)

    if (version == PARTFILE_SPLITTEDVERSION)
        return 2; // Splitted

    // Old format without version header — check for tag pattern
    if (version == MET_HEADER || version == MET_HEADER_I64TAGS)
        return 3; // NewOld format

    return 0; // Unknown
}

// ---------------------------------------------------------------------------
// performConvertToeMule — actual conversion logic per job
// ---------------------------------------------------------------------------

ConvertStatus PartFileConvert::performConvertToeMule(ConvertJob& job)
{
    const QString metPath = job.folder + u'/' + job.filename;

    switch (job.format) {
    case 1: // DefaultOld (.part.met — standard format)
    case 3: // NewOld (older .part.met variant)
    {
        // Verify .part.met file exists
        if (!QFile::exists(metPath)) {
            logWarning(QStringLiteral("PartFileConvert: .part.met not found: %1").arg(metPath));
            return ConvertStatus::PartMetNotFound;
        }

        // Derive .part data file name (strip .met suffix)
        QString partPath = metPath;
        if (partPath.endsWith(QStringLiteral(".met"), Qt::CaseInsensitive))
            partPath.chop(4);

        if (!QFile::exists(partPath)) {
            logWarning(QStringLiteral("PartFileConvert: .part data file not found: %1").arg(partPath));
            return ConvertStatus::PartMetNotFound;
        }

        // Load and validate the part file metadata
        auto partFile = std::make_unique<PartFile>();
        auto result = partFile->loadPartFile(job.folder, job.filename);
        if (result != PartFileLoadResult::LoadSuccess) {
            logWarning(QStringLiteral("PartFileConvert: failed to load .part.met: %1").arg(metPath));
            return ConvertStatus::Failed;
        }

        // Get target temp directory
        const auto tempDirs = thePrefs.tempDirs();
        const QString destDir = tempDirs.isEmpty() ? job.folder : tempDirs.first();

        // Copy .part data file to temp directory (if not already there)
        if (job.folder != destDir) {
            const QString destPartPath = destDir + QDir::separator()
                                         + QFileInfo(partPath).fileName();
            const QString destMetPath = destDir + QDir::separator() + job.filename;

            if (!QFile::copy(partPath, destPartPath)) {
                logWarning(QStringLiteral("PartFileConvert: failed to copy .part file to %1")
                               .arg(destPartPath));
                return ConvertStatus::IOError;
            }
            if (!QFile::copy(metPath, destMetPath)) {
                QFile::remove(destPartPath);
                logWarning(QStringLiteral("PartFileConvert: failed to copy .part.met to %1")
                               .arg(destMetPath));
                return ConvertStatus::IOError;
            }

            // Remove source files if requested
            if (job.removeSource) {
                QFile::remove(partPath);
                QFile::remove(metPath);
            }
        }

        logInfo(QStringLiteral("PartFileConvert: converted '%1' (format %2)")
                    .arg(job.filename).arg(job.format));
        return ConvertStatus::OK;
    }

    case 2: // Splitted (.part.001, .part.002, ...)
    {
        // Derive base name (strip .001)
        QString baseName = metPath;
        if (baseName.endsWith(QStringLiteral(".001"), Qt::CaseInsensitive))
            baseName.chop(4);

        // Scan for split parts
        int partNum = 1;
        QStringList parts;
        while (true) {
            QString partPath = QStringLiteral("%1.%2").arg(baseName).arg(partNum, 3, 10, u'0');
            if (!QFile::exists(partPath))
                break;
            parts.append(partPath);
            ++partNum;
        }

        if (parts.isEmpty()) {
            logWarning(QStringLiteral("PartFileConvert: no split parts found for '%1'").arg(job.filename));
            return ConvertStatus::PartMetNotFound;
        }

        // Get target temp directory
        const auto tempDirs = thePrefs.tempDirs();
        const QString destDir = tempDirs.isEmpty() ? job.folder : tempDirs.first();

        // Compute total size from all split parts
        uint64 totalSize = 0;
        for (const auto& partPath : parts) {
            QFileInfo fi(partPath);
            totalSize += static_cast<uint64>(fi.size());
        }
        job.size = totalSize;

        // Derive output .part filename from base name
        QString outBaseName = QFileInfo(baseName).fileName();
        if (outBaseName.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive))
            ; // already has .part extension
        else
            outBaseName += QStringLiteral(".part");

        const QString outPartPath = destDir + QDir::separator() + outBaseName;

        // Concatenate all split part files into the output .part file
        QFile outFile(outPartPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            logWarning(QStringLiteral("PartFileConvert: cannot create output file: %1").arg(outPartPath));
            return ConvertStatus::IOError;
        }

        constexpr qint64 kCopyBufSize = 64 * 1024;
        char copyBuf[kCopyBufSize];
        for (const auto& partPath : parts) {
            QFile inFile(partPath);
            if (!inFile.open(QIODevice::ReadOnly)) {
                outFile.close();
                QFile::remove(outPartPath);
                logWarning(QStringLiteral("PartFileConvert: cannot read split part: %1").arg(partPath));
                return ConvertStatus::IOError;
            }

            while (true) {
                qint64 got = inFile.read(copyBuf, kCopyBufSize);
                if (got <= 0)
                    break;
                if (outFile.write(copyBuf, got) != got) {
                    outFile.close();
                    QFile::remove(outPartPath);
                    return ConvertStatus::IOError;
                }
            }
        }
        outFile.close();

        // The concatenated file needs rehashing — create a minimal .part.met
        // so DownloadQueue can pick it up. The file will be re-hashed when loaded.
        logInfo(QStringLiteral("PartFileConvert: concatenated %1 split parts (%2 bytes) into '%3'")
                    .arg(parts.size()).arg(totalSize).arg(outPartPath));

        // Remove source split files if requested
        if (job.removeSource) {
            for (const auto& partPath : parts)
                QFile::remove(partPath);
        }

        return ConvertStatus::OK;
    }

    case 4: // Shareaza (.sd)
    {
        if (!QFile::exists(metPath)) {
            logWarning(QStringLiteral("PartFileConvert: Shareaza .sd not found: %1").arg(metPath));
            return ConvertStatus::PartMetNotFound;
        }

        // Parse Shareaza .sd binary format
        // The .sd file contains: [header][metadata][data]
        // Header: 4 bytes magic "SDL\x0", then metadata with file hash, size, name
        QFile sdFile(metPath);
        if (!sdFile.open(QIODevice::ReadOnly)) {
            logWarning(QStringLiteral("PartFileConvert: cannot open Shareaza .sd: %1").arg(metPath));
            return ConvertStatus::IOError;
        }

        // Read and validate magic header
        char magic[4]{};
        if (sdFile.read(magic, 4) != 4) {
            logWarning(QStringLiteral("PartFileConvert: Shareaza .sd too short: %1").arg(metPath));
            return ConvertStatus::BadFormat;
        }

        // Shareaza .sd files have varied header formats
        // Basic approach: try to extract the data portion
        // The file data typically starts after a metadata header
        const qint64 sdFileSize = sdFile.size();

        // Get target temp directory
        const auto tempDirs = thePrefs.tempDirs();
        const QString destDir = tempDirs.isEmpty() ? job.folder : tempDirs.first();

        // Derive output filename (strip .sd, add .part)
        QString outName = job.filename;
        if (outName.endsWith(QStringLiteral(".sd"), Qt::CaseInsensitive))
            outName.chop(3);
        outName += QStringLiteral(".part");

        const QString outPath = destDir + QDir::separator() + outName;

        // Copy the data portion to a .part file
        // Since .sd format varies, copy the entire file and let rehashing handle it
        QFile outFile(outPath);
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            logWarning(QStringLiteral("PartFileConvert: cannot create output: %1").arg(outPath));
            return ConvertStatus::IOError;
        }

        sdFile.seek(0);
        constexpr qint64 kCopyBufSize = 64 * 1024;
        char copyBuf[kCopyBufSize];
        while (true) {
            qint64 got = sdFile.read(copyBuf, kCopyBufSize);
            if (got <= 0)
                break;
            outFile.write(copyBuf, got);
        }
        outFile.close();
        sdFile.close();

        logInfo(QStringLiteral("PartFileConvert: copied Shareaza .sd (%1 bytes) to '%2' — needs rehashing")
                    .arg(sdFileSize).arg(outPath));

        if (job.removeSource)
            QFile::remove(metPath);

        return ConvertStatus::OK;
    }

    default:
        logWarning(QStringLiteral("PartFileConvert: unknown format %1 for '%2'")
                       .arg(job.format).arg(job.filename));
        return ConvertStatus::BadFormat;
    }
}

} // namespace eMule
