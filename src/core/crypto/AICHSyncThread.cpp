#include "pch.h"
/// @file AICHSyncThread.cpp
/// @brief Background AICH hash synchronization — port of MFC CAICHSyncThread.

#include "AICHSyncThread.h"
#include "AICHHashSet.h"
#include "FileIdentifier.h"
#include "files/KnownFile.h"
#include "files/SharedFileList.h"
#include "utils/Log.h"
#include "utils/SafeFile.h"

#include <QFile>
#include <QMutexLocker>

namespace eMule {

AICHSyncThread::AICHSyncThread(const QString& configDir, SharedFileList* sharedFiles,
                               QObject* parent)
    : QThread(parent)
    , m_configDir(configDir)
    , m_sharedFiles(sharedFiles)
{
    // Queued by default: the signals are emitted from run(), this object lives elsewhere.
    connect(this, &AICHSyncThread::indexLoaded, this, &AICHSyncThread::onIndexLoaded);
    connect(this, &AICHSyncThread::hashSetBuilt, this, &AICHSyncThread::onHashSetBuilt);
}

AICHSyncThread::~AICHSyncThread()
{
    requestStop();
    wait();
}

void AICHSyncThread::requestStop()
{
    m_stopping.store(true, std::memory_order_relaxed);
    QMutexLocker locker(&m_jobMutex);
    m_jobReady.wakeAll();
}

void AICHSyncThread::run()
{
    if (isClosing() || !loadIndex())
        return;

    emit indexLoaded();

    // Wait for the owner thread's list of files without a hashset, then work it off.
    for (;;) {
        Job job;
        {
            QMutexLocker locker(&m_jobMutex);
            while (!isClosing() && m_jobs.empty() && !m_jobsQueued)
                m_jobReady.wait(&m_jobMutex);
            if (isClosing() || m_jobs.empty())
                break;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
        }

        AICHRecoveryHashSet hashSet(job.size);
        bool ok = KnownFile::buildAICHHashSet(job.path, job.size, hashSet);
        QByteArray master;
        if (ok) {
            master = QByteArray(reinterpret_cast<const char*>(hashSet.getMasterHash().getRawHash()),
                                kAICHHashSize);
            ok = hashSet.saveHashSet();
        }
        if (!ok)
            logWarning(QStringLiteral("Failed to create AICH hashset for %1").arg(job.path));
        emit hashSetBuilt(job.fileHash, master, ok);
    }
}

bool AICHSyncThread::loadIndex()
{
    const QString known2Path = m_configDir + QChar(u'/') + QString::fromUtf16(kKnown2MetFilename);

    std::vector<std::pair<AICHHash, uint64>> entries;

    QMutexLocker lockKnown2Met(&AICHRecoveryHashSet::s_mutKnown2File);

    SafeFile file;
    const bool justCreated = convertKnown2ToKnown264(file);
    if (!justCreated && !file.open(known2Path, QIODevice::ReadWrite)
        && !file.open(known2Path, QIODevice::ReadWrite | QIODevice::NewOnly))
    {
        logWarning(QStringLiteral("Failed to open known2_64.met"));
        return false;
    }

    // Everything up to here parsed cleanly; anything behind it is a torn record.
    qint64 verifiedEnd = 0;
    bool damaged = false;
    try {
        const qint64 size = file.length();
        if (size >= 1) {
            file.seek(0, 0);
            if (file.readUInt8() != kKnown2MetVersion) {
                logWarning(QStringLiteral("known2_64.met has wrong version header"));
                return false;
            }
            verifiedEnd = file.position();

            while (file.position() < size) {
                const auto pos = static_cast<uint64>(file.position());
                const AICHHash hash(file);
                const uint32 hashCount = file.readUInt32();
                if (file.position() + static_cast<qint64>(hashCount) * kAICHHashSize > size) {
                    damaged = true;
                    break;
                }
                file.seek(static_cast<qint64>(hashCount) * kAICHHashSize, 1);   // SEEK_CUR
                entries.emplace_back(hash, pos);
                verifiedEnd = file.position();
            }
        } else {
            file.writeUInt8(kKnown2MetVersion);
            file.commit(false);
        }
    } catch (const std::exception&) {
        damaged = true;   // a record header cut short
    }

    if (damaged) {
        logWarning(QStringLiteral("known2_64.met is damaged at offset %1 — cutting off the tail")
                       .arg(verifiedEnd));
        file.close();
        // verifiedEnd is never below the version byte, so the header survives.
        if (!QFile::resize(known2Path, verifiedEnd))
            return false;
    }

    for (const auto& [hash, pos] : entries)
        AICHRecoveryHashSet::addStoredAICHHash(hash, pos);
    return true;
}

// -- Owner thread -------------------------------------------------------------

void AICHSyncThread::onIndexLoaded()
{
    std::deque<Job> jobs;
    if (m_sharedFiles && !isClosing()) {
        m_sharedFiles->forEachFile([&](KnownFile* file) {
            if (file->isPartFile())
                return;

            FileIdentifier& fileId = file->fileIdentifier();
            if (fileId.hasAICHHash() && AICHRecoveryHashSet::isStored(fileId.getAICHHash())) {
                file->setAICHRecoverHashSetAvailable(true);
                if (!fileId.hasExpectedAICHHashCount())
                    applyStoredHashSet(file);
                return;
            }

            file->setAICHRecoverHashSetAvailable(false);
            if (file->filePath().isEmpty())
                return;
            jobs.push_back({QByteArray(reinterpret_cast<const char*>(file->fileHash()), 16),
                            file->filePath(), static_cast<uint64>(file->fileSize())});
        });
    }

    const auto count = static_cast<int>(jobs.size());
    if (count > 0)
        logInfo(QStringLiteral("AICH sync: %1 files need hashing").arg(count));
    {
        QMutexLocker locker(&m_jobMutex);
        m_jobs = std::move(jobs);
        m_jobsQueued = true;
        m_jobReady.wakeAll();
    }
    emit syncComplete(count);
}

void AICHSyncThread::onHashSetBuilt(const QByteArray& fileHash, const QByteArray& masterHash,
                                    bool success)
{
    // By hash, not by pointer: the file may have been unshared or replaced meanwhile.
    KnownFile* file = (m_sharedFiles && fileHash.size() == 16)
        ? m_sharedFiles->getFileByID(reinterpret_cast<const uint8*>(fileHash.constData()))
        : nullptr;
    if (file && !file->isPartFile() && success && masterHash.size() == kAICHHashSize) {
        file->fileIdentifier().setAICHHash(
            AICHHash(reinterpret_cast<const uint8*>(masterHash.constData())));
        applyStoredHashSet(file);
        file->setAICHRecoverHashSetAvailable(true);
    }
    emit fileHashed(fileHash, success);
}

void AICHSyncThread::applyStoredHashSet(KnownFile* file)
{
    FileIdentifier& fileId = file->fileIdentifier();
    AICHRecoveryHashSet stored(file->fileSize());
    stored.setMasterHash(fileId.getAICHHash(), EAICHStatus::HashSetComplete);
    if (!stored.loadHashSet() || !fileId.setAICHHashSet(stored))
        logDebug(QStringLiteral("Failed to create AICH part hashset for %1").arg(file->fileName()));
}

bool AICHSyncThread::convertKnown2ToKnown264(SafeFile& targetFile)
{
    const QString oldPath = m_configDir + QStringLiteral("/known2.met");
    const QString newPath = m_configDir + QChar(u'/') + QString::fromUtf16(kKnown2MetFilename);

    // Only convert if old exists and new doesn't
    if (QFile::exists(newPath) || !QFile::exists(oldPath))
        return false;

    SafeFile oldFile;
    if (!oldFile.open(oldPath, QIODevice::ReadOnly)) {
        return false;
    }
    if (!targetFile.open(newPath, QIODevice::ReadWrite | QIODevice::NewOnly)) {
        return false;
    }

    logInfo(QStringLiteral("Converting known2.met to known2_64.met"));

    try {
        targetFile.writeUInt8(kKnown2MetVersion);
        const qint64 oldLength = oldFile.length();

        while (oldFile.position() < oldLength) {
            AICHHash hash(oldFile);
            const uint32 hashCount = oldFile.readUInt16(); // old format uses 16-bit count
            if (oldFile.position() + static_cast<qint64>(hashCount) * kAICHHashSize > oldLength) {
                qCWarning(lcEmuleGeneral, "known2.met truncated during conversion");
                break;
            }

            // Read all hashes
            std::vector<uint8> buffer(static_cast<std::size_t>(hashCount) * kAICHHashSize);
            oldFile.read(buffer.data(), static_cast<qint64>(buffer.size()));

            // Write with 32-bit count
            hash.write(targetFile);
            targetFile.writeUInt32(hashCount);
            targetFile.write(buffer.data(), static_cast<qint64>(buffer.size()));
        }

        logInfo(QStringLiteral("known2.met conversion complete"));
    } catch (const std::exception& ex) {
        qCWarning(lcEmuleGeneral, "known2.met conversion failed: %s", ex.what());
        targetFile.close();
        return false;
    }

    targetFile.seek(0, 0); // rewind
    return true;
}

} // namespace eMule
