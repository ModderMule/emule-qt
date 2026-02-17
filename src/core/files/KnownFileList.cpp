/// @file KnownFileList.cpp
/// @brief Known file database — port of MFC CKnownFileList.
///
/// Manages known.met and cancelled.met persistence.

#include "files/KnownFileList.h"
#include "files/KnownFile.h"
#include "crypto/MD4Hash.h"
#include "utils/Log.h"
#include "utils/SafeFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstring>
#include <ctime>
#include <random>

namespace eMule {

static constexpr uint32 kKnownFileListSaveInterval = MIN2S(11);
static constexpr uint8 kCancelledMetHeader = 0xE1;
static constexpr uint8 kCancelledMetVersion = 0x01;

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

KnownFileList::KnownFileList() = default;

KnownFileList::~KnownFileList()
{
    clear();
}

// ---------------------------------------------------------------------------
// init — load known.met + cancelled.met
// ---------------------------------------------------------------------------

bool KnownFileList::init(const QString& configDir)
{
    m_configDir = configDir;
    m_lastSaveTime = static_cast<uint32>(std::time(nullptr));

    bool ok = true;
    if (!loadKnownFiles())
        ok = false;
    if (!loadCancelledFiles())
        ok = false;
    return ok;
}

// ---------------------------------------------------------------------------
// save — persist both files
// ---------------------------------------------------------------------------

void KnownFileList::save()
{
    const QString knownPath = m_configDir + QStringLiteral("/known.met");

    try {
        SafeFile file(knownPath, QIODevice::WriteOnly | QIODevice::Truncate);

        file.writeUInt8(MET_HEADER_I64TAGS);
        file.writeUInt32(static_cast<uint32>(m_filesMap.size()));

        for (const auto& [key, knownFile] : m_filesMap) {
            knownFile->writeToFile(file);
        }
    } catch (const std::exception& e) {
        logError(QStringLiteral("Failed to save known.met: %1").arg(QString::fromUtf8(e.what())));
    }

    saveCancelledFiles();
    m_lastSaveTime = static_cast<uint32>(std::time(nullptr));
}

// ---------------------------------------------------------------------------
// clear
// ---------------------------------------------------------------------------

void KnownFileList::clear()
{
    for (auto& [key, file] : m_filesMap)
        delete file;
    m_filesMap.clear();
    m_cancelledFiles.clear();
    totalTransferred = 0;
    totalRequested = 0;
    totalAccepted = 0;
}

// ---------------------------------------------------------------------------
// process — periodic auto-save
// ---------------------------------------------------------------------------

void KnownFileList::process()
{
    const auto now = static_cast<uint32>(std::time(nullptr));
    if (now - m_lastSaveTime >= kKnownFileListSaveInterval) {
        save();
    }
}

// ---------------------------------------------------------------------------
// safeAddKFile
// ---------------------------------------------------------------------------

bool KnownFileList::safeAddKFile(KnownFile* file)
{
    if (!file)
        return false;

    MD4Key key(file->fileHash());
    auto it = m_filesMap.find(key);
    if (it != m_filesMap.end()) {
        // Merge statistics from old file
        KnownFile* existing = it->second;
        totalTransferred += file->statistic.allTimeTransferred() - existing->statistic.allTimeTransferred();
        totalRequested += file->statistic.allTimeRequests() - existing->statistic.allTimeRequests();
        totalAccepted += file->statistic.allTimeAccepts() - existing->statistic.allTimeAccepts();
        delete existing;
        it->second = file;
    } else {
        m_filesMap[key] = file;
        totalTransferred += file->statistic.allTimeTransferred();
        totalRequested += file->statistic.allTimeRequests();
        totalAccepted += file->statistic.allTimeAccepts();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Lookup methods
// ---------------------------------------------------------------------------

KnownFile* KnownFileList::findKnownFile(const QString& filename, time_t date, uint64 size) const
{
    for (const auto& [key, file] : m_filesMap) {
        if (file->utcFileDate() == date
            && static_cast<uint64>(file->fileSize()) == size
            && file->fileName().compare(filename, Qt::CaseInsensitive) == 0)
        {
            return file;
        }
    }
    return nullptr;
}

KnownFile* KnownFileList::findKnownFileByID(const uint8* hash) const
{
    auto it = m_filesMap.find(MD4Key(hash));
    return (it != m_filesMap.end()) ? it->second : nullptr;
}

KnownFile* KnownFileList::findKnownFileByPath(const QString& path) const
{
    for (const auto& [key, file] : m_filesMap) {
        if (file->filePath().compare(path, Qt::CaseInsensitive) == 0)
            return file;
    }
    return nullptr;
}

bool KnownFileList::isKnownFile(const KnownFile* file) const
{
    if (!file)
        return false;
    return findKnownFileByID(file->fileHash()) != nullptr;
}

bool KnownFileList::isFilePtrInList(const KnownFile* file) const
{
    for (const auto& [key, f] : m_filesMap) {
        if (f == file)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Cancelled files
// ---------------------------------------------------------------------------

void KnownFileList::addCancelledFileID(const uint8* hash)
{
    m_cancelledFiles.insert(makeCancelledKey(hash));
}

bool KnownFileList::isCancelledFileByID(const uint8* hash) const
{
    return m_cancelledFiles.contains(makeCancelledKey(hash));
}

MD4Key KnownFileList::makeCancelledKey(const uint8* hash) const
{
    // Apply MD5(seed + hash) to obfuscate stored hashes
    MD4Key result;
    QCryptographicHash md5(QCryptographicHash::Md5);
    md5.addData(QByteArrayView(reinterpret_cast<const char*>(&m_cancelledSeed), 4));
    md5.addData(QByteArrayView(reinterpret_cast<const char*>(hash), 16));
    auto digest = md5.result();
    std::memcpy(result.data.data(), digest.constData(), 16);
    return result;
}

// ---------------------------------------------------------------------------
// loadKnownFiles
// ---------------------------------------------------------------------------

bool KnownFileList::loadKnownFiles()
{
    const QString filePath = m_configDir + QStringLiteral("/known.met");

    if (!QFile::exists(filePath))
        return true; // first run — no file is fine

    try {
        SafeFile file(filePath, QIODevice::ReadOnly);

        uint8 version = file.readUInt8();
        if (version != MET_HEADER && version != MET_HEADER_I64TAGS) {
            logError(QStringLiteral("known.met: unsupported version 0x%1")
                         .arg(version, 2, 16, QChar(u'0')));
            return false;
        }

        uint32 count = file.readUInt32();
        for (uint32 i = 0; i < count; ++i) {
            auto* kf = new KnownFile();
            if (!kf->loadFromFile(file)) {
                logWarning(QStringLiteral("known.met: corrupt entry %1 of %2").arg(i).arg(count));
                delete kf;
                continue;
            }

            MD4Key key(kf->fileHash());
            if (m_filesMap.contains(key)) {
                delete kf;
                continue;
            }

            m_filesMap[key] = kf;
            totalTransferred += kf->statistic.allTimeTransferred();
            totalRequested += kf->statistic.allTimeRequests();
            totalAccepted += kf->statistic.allTimeAccepts();
        }

        logInfo(QStringLiteral("Loaded %1 known files").arg(m_filesMap.size()));
        return true;
    } catch (const std::exception& e) {
        logError(QStringLiteral("known.met load error: %1").arg(QString::fromUtf8(e.what())));
        return false;
    }
}

// ---------------------------------------------------------------------------
// loadCancelledFiles
// ---------------------------------------------------------------------------

bool KnownFileList::loadCancelledFiles()
{
    const QString filePath = m_configDir + QStringLiteral("/cancelled.met");

    if (!QFile::exists(filePath))
        return true;

    try {
        SafeFile file(filePath, QIODevice::ReadOnly);

        uint8 header = file.readUInt8();
        if (header != kCancelledMetHeader) {
            logWarning(QStringLiteral("cancelled.met: bad header"));
            return false;
        }

        uint8 version = file.readUInt8();
        if (version != kCancelledMetVersion)
            return false;

        m_cancelledSeed = file.readUInt32();
        uint32 count = file.readUInt32();

        for (uint32 i = 0; i < count; ++i) {
            MD4Key key;
            file.read(key.data.data(), 16);
            m_cancelledFiles.insert(key);
        }

        logInfo(QStringLiteral("Loaded %1 cancelled file hashes").arg(m_cancelledFiles.size()));
        return true;
    } catch (const std::exception& e) {
        logError(QStringLiteral("cancelled.met load error: %1").arg(QString::fromUtf8(e.what())));
        return false;
    }
}

// ---------------------------------------------------------------------------
// saveCancelledFiles
// ---------------------------------------------------------------------------

void KnownFileList::saveCancelledFiles()
{
    const QString filePath = m_configDir + QStringLiteral("/cancelled.met");

    // Generate seed on first save
    if (m_cancelledSeed == 0) {
        std::random_device rd;
        m_cancelledSeed = rd();
    }

    try {
        SafeFile file(filePath, QIODevice::WriteOnly | QIODevice::Truncate);

        file.writeUInt8(kCancelledMetHeader);
        file.writeUInt8(kCancelledMetVersion);
        file.writeUInt32(m_cancelledSeed);
        file.writeUInt32(static_cast<uint32>(m_cancelledFiles.size()));

        for (const auto& key : m_cancelledFiles) {
            file.write(key.data.data(), 16);
        }
    } catch (const std::exception& e) {
        logError(QStringLiteral("Failed to save cancelled.met: %1").arg(QString::fromUtf8(e.what())));
    }
}

} // namespace eMule
