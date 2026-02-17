/// @file AICHSyncThread.cpp
/// @brief Background AICH hash synchronization — port of MFC CAICHSyncThread.

#include "AICHSyncThread.h"
#include "AICHData.h"
#include "AICHHashSet.h"
#include "FileIdentifier.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "utils/DebugUtils.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QDir>
#include <QFile>
#include <QMutexLocker>

namespace eMule {

AICHSyncThread::AICHSyncThread(const QString& configDir,
                                SharedFileList* sharedFiles,
                                KnownFileList* knownFiles,
                                QObject* parent)
    : QThread(parent)
    , m_configDir(configDir)
    , m_sharedFiles(sharedFiles)
    , m_knownFiles(knownFiles)
{
}

void AICHSyncThread::run()
{
    if (isClosing())
        return;

    const QString known2Path = m_configDir + QChar(u'/') + QString::fromUtf16(kKnown2MetFilename);
    AICHRecoveryHashSet::setKnown2MetPath(known2Path);

    // Collect all master hashes from known2_64.met
    std::vector<AICHHash> known2Hashes;
    std::vector<uint64> known2HashPositions;

    QMutexLocker lockKnown2Met(&AICHRecoveryHashSet::s_mutKnown2File);

    SafeFile file;
    bool justCreated = convertKnown2ToKnown264(file);

    if (!justCreated) {
        if (!file.open(known2Path, QIODevice::ReadWrite)) {
            // Try creating the file
            if (!file.open(known2Path, QIODevice::ReadWrite | QIODevice::NewOnly)) {
                qCWarning(lcEmuleGeneral, "Failed to open known2_64.met");
                return;
            }
        }
    }

    uint64 lastVerifiedPos = 0;
    try {
        if (file.length() >= 1) {
            file.seek(0, 0);
            const uint8 header = file.readUInt8();
            if (header != kKnown2MetVersion) {
                qCWarning(lcEmuleGeneral, "known2_64.met has wrong version header");
                return;
            }

            const qint64 existingSize = file.length();
            while (file.position() < existingSize) {
                known2HashPositions.push_back(static_cast<uint64>(file.position()));
                AICHHash hash(file);
                known2Hashes.push_back(hash);
                const uint32 hashCount = file.readUInt32();
                if (file.position() + static_cast<qint64>(hashCount) * kAICHHashSize > existingSize) {
                    qCWarning(lcEmuleGeneral, "known2_64.met truncated");
                    break;
                }
                file.seek(static_cast<qint64>(hashCount) * kAICHHashSize, 1); // SEEK_CUR
                lastVerifiedPos = static_cast<uint64>(file.position());
            }
        } else {
            file.writeUInt8(kKnown2MetVersion);
        }
    } catch (const std::exception& ex) {
        qCWarning(lcEmuleGeneral, "Error reading known2_64.met: %s", ex.what());
        // Truncate to last verified position
        if (lastVerifiedPos > 0) {
            try {
                // Reopen with truncation
                file.close();
                QFile qf(known2Path);
                if (qf.open(QIODevice::ReadWrite))
                    qf.resize(static_cast<qint64>(lastVerifiedPos));
            } catch (...) {}
        }
        return;
    }

    // Match shared files against known hashes
    std::vector<AICHHash> usedHashes;
    std::vector<KnownFile*> filesToHash;
    bool loggedPartHashWarning = false;

    m_sharedFiles->forEachFile([&](KnownFile* pFile) {
        if (isClosing() || pFile->isPartFile())
            return;

        FileIdentifier& fileId = pFile->fileIdentifier();
        if (fileId.hasAICHHash()) {
            bool aichFound = false;
            for (std::size_t i = known2Hashes.size(); i-- > 0;) {
                if (known2Hashes[i] == fileId.getAICHHash()) {
                    aichFound = true;
                    usedHashes.push_back(known2Hashes[i]);
                    pFile->setAICHRecoverHashSetAvailable(true);

                    // Upgrade: create AICH part hashes if missing
                    if (!fileId.hasExpectedAICHHashCount()) {
                        if (!loggedPartHashWarning) {
                            loggedPartHashWarning = true;
                            qCWarning(lcEmuleGeneral,
                                "Missing AICH part hashsets - creating from recovery sets");
                        }
                        AICHRecoveryHashSet tempHashSet(pFile->fileSize());
                        tempHashSet.setMasterHash(fileId.getAICHHash(),
                                                   EAICHStatus::HashSetComplete);
                        if (tempHashSet.loadHashSet()) {
                            if (!fileId.setAICHHashSet(tempHashSet)) {
                                qCWarning(lcEmuleGeneral,
                                    "Failed to create AICH part hashset for %s",
                                    qUtf8Printable(pFile->fileName()));
                            }
                        } else {
                            qCWarning(lcEmuleGeneral,
                                "Failed to load AICH recovery hashset for %s",
                                qUtf8Printable(pFile->fileName()));
                        }
                    }
                    break;
                }
            }
            if (aichFound)
                return;
        }
        pFile->setAICHRecoverHashSetAvailable(false);
        filesToHash.push_back(pFile);
    });

    // Index all hashes (regardless of purging)
    for (std::size_t i = 0; i < known2Hashes.size() && !isClosing(); ++i)
        AICHRecoveryHashSet::addStoredAICHHash(known2Hashes[i], known2HashPositions[i]);

    lockKnown2Met.unlock();

    emit syncComplete(static_cast<int>(filesToHash.size()));

    // Hash files that need AICH hashing
    if (!filesToHash.empty()) {
        logInfo(QStringLiteral("AICH sync: %1 files need hashing")
                    .arg(filesToHash.size()));

        // Wait for any normal hashing to complete
        while (m_sharedFiles->getHashingCount() > 0) {
            if (isClosing())
                return;
            QThread::msleep(100);
        }

        int done = 0;
        for (KnownFile* pFile : filesToHash) {
            if (isClosing())
                return;

            emit hashingProgress(static_cast<int>(filesToHash.size()) - done);

            // Verify file is still in the known/shared lists
            if (!m_knownFiles->isKnownFile(pFile)
                || !m_sharedFiles->getFileByID(pFile->fileHash()))
            {
                ++done;
                continue;
            }

            qCDebug(lcEmuleGeneral, "AICH hashing: %s",
                     qUtf8Printable(pFile->fileName()));

            const bool success = pFile->createAICHHashSetOnly();
            emit fileHashed(pFile, success);

            if (!success) {
                qCWarning(lcEmuleGeneral, "Failed to create AICH hashset for %s",
                           qUtf8Printable(pFile->fileName()));
            }
            ++done;
        }

        emit hashingProgress(0);
    }

    qCDebug(lcEmuleGeneral, "AICHSyncThread finished");
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
