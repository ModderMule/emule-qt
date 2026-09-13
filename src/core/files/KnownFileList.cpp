#include "pch.h"
/// @file KnownFileList.cpp
/// @brief Known file database — port of MFC CKnownFileList.
///
/// Manages known.met and cancelled.met persistence.

#include "files/KnownFileList.h"
#include "app/AppContext.h"
#include "files/KnownFile.h"
#include "files/SharedFileList.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "utils/Log.h"
#include "utils/SafeFile.h"

#include <QFile>
#include <QRandomGenerator>

#include <vector>


namespace eMule {

static constexpr uint32 kKnownFileListSaveInterval = MIN2S(11);

// cancelled.met, MFC's layout (srchybrid/KnownFileList.cpp:44-48):
//   <header 1><version 1><seed 4><count 4>[<keyedHash 16><tagCount 1>[tags] * count]
// The tag count is always written as 0 and exists so a later version can add
// fields without breaking this reader. An earlier port wrote 0xE1 with bare
// 16-byte records; that file is dropped rather than migrated — see
// loadCancelledFiles(), and note the seed bug meant its keys matched nothing.
static constexpr uint8 kCancelledMetHeader = MET_HEADER_I64TAGS;
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
    const QString tmpPath   = knownPath + QStringLiteral(".tmp");
    const QString bakPath   = knownPath + QStringLiteral(".bak");

    try {
        // Clean up stale .tmp from a previous failed save
        QFile::remove(tmpPath);

        // What "Remember downloaded files" actually does: with it off, only files
        // we are currently sharing survive the write, so a completed download is
        // forgotten as soon as it leaves the shared list. MFC decides it the same
        // way (srchybrid/KnownFileList.cpp:211). Collected before the write rather
        // than rewinding to patch the count afterwards, as MFC does at :217.
        const bool rememberAll = thePrefs.rememberDownloadedFiles();
        std::vector<const KnownFile*> toWrite;
        toWrite.reserve(m_filesMap.size());
        for (const auto& [key, knownFile] : m_filesMap) {
            if (rememberAll
                || (theApp.sharedFileList
                    && theApp.sharedFileList->getFileByID(knownFile->fileHash()) == knownFile))
            {
                toWrite.push_back(knownFile);
            }
        }

        // Write to temp file
        {
            SafeFile file(tmpPath, QIODevice::WriteOnly);
            file.writeUInt8(MET_HEADER_I64TAGS);
            file.writeUInt32(static_cast<uint32>(toWrite.size()));
            for (const KnownFile* knownFile : toWrite) {
                knownFile->writeToFile(file);
            }
        } // file closed before rename

        // Rotate: current → .bak (preserves old data as backup)
        QFile::remove(bakPath);
        if (QFile::exists(knownPath)) {
            if (!QFile::rename(knownPath, bakPath)) {
                logWarning(QStringLiteral("known.met: failed to create backup"));
                QFile::remove(knownPath);
            }
        }

        // Rename temp → final
        if (!QFile::rename(tmpPath, knownPath)) {
            logError(QStringLiteral("known.met: failed to rename tmp → known.met"));
            if (QFile::exists(bakPath))
                QFile::rename(bakPath, knownPath);
            return;
        }
    } catch (const std::exception& e) {
        logError(QStringLiteral("Failed to save known.met: %1").arg(QString::fromUtf8(e.what())));
        QFile::remove(tmpPath);
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

void KnownFileList::remove(const KnownFile* file)
{
    if (!file)
        return;
    MD4Key key(file->fileHash());
    m_filesMap.erase(key);
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
    if (!thePrefs.rememberCancelledFiles())
        return;
    // Minted here, on the first insert, and never again — the seed is what every
    // stored key was derived from, so generating it at save time (as this used to)
    // wrote a header seed that disagreed with every record beneath it, and nothing
    // matched after a restart. MFC mints it in the same place, and forces it
    // non-zero for the same reason: 0 is the "not seeded yet" sentinel, so a
    // genuinely zero seed would be re-minted on every call
    // (srchybrid/KnownFileList.cpp:383-386).
    if (m_cancelledSeed == 0)
        m_cancelledSeed = (QRandomGenerator::global()->generate() % 0xFFFFFFFEu) + 1;
    m_cancelledFiles.insert(makeCancelledKey(hash));
}

bool KnownFileList::isCancelledFileByID(const uint8* hash) const
{
    if (!thePrefs.rememberCancelledFiles())
        return false;
    return m_cancelledFiles.contains(makeCancelledKey(hash));
}

MD4Key KnownFileList::makeCancelledKey(const uint8* hash) const
{
    // The file stores MD5(seed || md4hash), never the hash itself, so reading it
    // tells nobody which files were cancelled: without the seed a candidate hash
    // cannot be tested, and two installations' files cannot be compared.
    // Little-endian seed bytes, matching MFC's PokeUInt32.
    uint8 seedLe[4];
    seedLe[0] = static_cast<uint8>(m_cancelledSeed);
    seedLe[1] = static_cast<uint8>(m_cancelledSeed >> 8);
    seedLe[2] = static_cast<uint8>(m_cancelledSeed >> 16);
    seedLe[3] = static_cast<uint8>(m_cancelledSeed >> 24);

    MD4Key result;
    QCryptographicHash md5(QCryptographicHash::Md5);
    md5.addData(QByteArrayView(reinterpret_cast<const char*>(seedLe), 4));
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

    if (QFileInfo(filePath).size() == 0)
        return true; // empty file — treat as first run

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
                break; // file cursor is at unknown position, can't continue
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

        // Try .bak fallback (once only — remove .bak to prevent infinite recursion)
        const QString bakPath = filePath + QStringLiteral(".bak");
        if (QFile::exists(bakPath)) {
            logInfo(QStringLiteral("Trying known.met.bak fallback..."));
            QFile::remove(filePath);
            if (QFile::rename(bakPath, filePath))
                return loadKnownFiles();
        }
        return false;
    }
}

// ---------------------------------------------------------------------------
// loadCancelledFiles
// ---------------------------------------------------------------------------

bool KnownFileList::loadCancelledFiles()
{
    const QString filePath = m_configDir + QStringLiteral("/cancelled.met");

    // Off means forget, as it does in MFC (srchybrid/KnownFileList.cpp:126): the
    // list is not read, and the next save writes it out empty.
    if (!thePrefs.rememberCancelledFiles())
        return true;

    if (!QFile::exists(filePath))
        return true;

    try {
        SafeFile file(filePath, QIODevice::ReadOnly);

        uint8 header = file.readUInt8();
        if (header != kCancelledMetHeader) {
            // Either MFC's pre-0x0F layout or this port's old 0xE1 one. Neither is
            // worth converting: the 0xE1 files were written with a seed that never
            // matched their own records, so there is nothing in them to recover.
            logWarning(QStringLiteral("cancelled.met: unsupported header 0x%1, starting a new list")
                           .arg(header, 2, 16, QChar(u'0')));
            QFile::remove(filePath);
            return true;
        }

        uint8 version = file.readUInt8();
        if (version > kCancelledMetVersion)
            return false;

        m_cancelledSeed = file.readUInt32();
        if (m_cancelledSeed == 0) {
            // An empty list written before anything was ever cancelled. Mint now so
            // the first insert does not have to.
            m_cancelledSeed = (QRandomGenerator::global()->generate() % 0xFFFFFFFEu) + 1;
        }

        uint32 count = file.readUInt32();
        for (uint32 i = 0; i < count; ++i) {
            MD4Key key;
            file.read(key.data.data(), 16);
            // Always 0 today. Read and discarded so a later version can add fields
            // here without this reader losing its place.
            for (uint8 t = file.readUInt8(); t > 0; --t)
                Tag skipped(file, false);
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
    const QString tmpPath  = filePath + QStringLiteral(".tmp");
    const QString bakPath  = filePath + QStringLiteral(".bak");

    // No seed minting here: addCancelledFileID() owns that, so the header can never
    // claim a seed the records below it were not derived from.
    const bool remember = thePrefs.rememberCancelledFiles();

    try {
        QFile::remove(tmpPath);

        {
            SafeFile file(tmpPath, QIODevice::WriteOnly);
            file.writeUInt8(kCancelledMetHeader);
            file.writeUInt8(kCancelledMetVersion);
            // Written whether or not the list is: an empty file that keeps its seed
            // stays consistent with itself if the preference comes back on.
            file.writeUInt32(m_cancelledSeed);
            if (!remember) {
                file.writeUInt32(0);
            } else {
                file.writeUInt32(static_cast<uint32>(m_cancelledFiles.size()));
                for (const auto& key : m_cancelledFiles) {
                    file.write(key.data.data(), 16);
                    file.writeUInt8(0);   // tag count
                }
            }
        }

        QFile::remove(bakPath);
        if (QFile::exists(filePath)) {
            if (!QFile::rename(filePath, bakPath))
                QFile::remove(filePath);
        }

        if (!QFile::rename(tmpPath, filePath)) {
            logError(QStringLiteral("cancelled.met: failed to rename tmp → cancelled.met"));
            if (QFile::exists(bakPath))
                QFile::rename(bakPath, filePath);
        }
    } catch (const std::exception& e) {
        logError(QStringLiteral("Failed to save cancelled.met: %1").arg(QString::fromUtf8(e.what())));
        QFile::remove(tmpPath);
    }
}

} // namespace eMule
