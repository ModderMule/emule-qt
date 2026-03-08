#include "pch.h"
/// @file SharedFileList.cpp
/// @brief Shared file management — port of MFC CSharedFileList.

#include "files/SharedFileList.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUInt128.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "utils/Log.h"
#include "utils/SafeFile.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>

namespace eMule {

// ===========================================================================
// HashingThread
// ===========================================================================

HashingThread::HashingThread(QObject* parent)
    : QThread(parent)
{
}

void HashingThread::enqueue(Job job)
{
    QMutexLocker locker(&m_mutex);
    m_queue.push_back(std::move(job));
    m_condition.wakeOne();
}

void HashingThread::clearQueue()
{
    QMutexLocker locker(&m_mutex);
    m_queue.clear();
}

void HashingThread::requestStop()
{
    QMutexLocker locker(&m_mutex);
    m_stopRequested = true;
    m_condition.wakeAll();
}

void HashingThread::run()
{
    while (true) {
        Job job;

        {
            QMutexLocker locker(&m_mutex);
            while (m_queue.empty() && !m_stopRequested)
                m_condition.wait(&m_mutex);

            if (m_stopRequested)
                return;

            job = std::move(m_queue.front());
            m_queue.pop_front();
        }

        logDebug(QStringLiteral("Hashing: %1/%2").arg(job.directory, job.filename));

        auto* kf = new KnownFile();
        bool ok = kf->createFromFile(job.directory, job.filename,
                                     [this](int percent) {
                                         emit hashingProgress(percent);
                                     });

        if (ok) {
            logDebug(QStringLiteral("Hashed OK: %1/%2 (%3 bytes)")
                         .arg(job.directory, job.filename)
                         .arg(kf->fileSize()));
            if (!job.sharedDirectory.isEmpty())
                kf->setSharedDirectory(job.sharedDirectory);
            emit hashingFinished(kf, job.generation);
        } else {
            delete kf;
            emit hashingFailed(job.directory, job.filename, job.generation);
        }
    }
}

// ===========================================================================
// SharedFileList
// ===========================================================================

SharedFileList::SharedFileList(KnownFileList* knownFiles, QObject* parent)
    : QObject(parent)
    , m_knownFiles(knownFiles)
{
    m_hashingThread = new HashingThread(this);
    connect(m_hashingThread, &HashingThread::hashingFinished,
            this, &SharedFileList::onHashingFinished, Qt::QueuedConnection);
    connect(m_hashingThread, &HashingThread::hashingFailed,
            this, &SharedFileList::onHashingFailed, Qt::QueuedConnection);
    m_hashingThread->start();
}

SharedFileList::~SharedFileList()
{
    if (m_hashingThread) {
        m_hashingThread->requestStop();
        m_hashingThread->wait();
    }
    // Note: files in m_filesMap are owned by KnownFileList, not us
}

// ---------------------------------------------------------------------------
// reload — rescan shared directories
// ---------------------------------------------------------------------------

void SharedFileList::reload()
{
    QMutexLocker locker(&m_mutex);
    ++m_generation;
    m_hashingThread->clearQueue();
    m_filesMap.clear();
    m_waitingForHash.clear();
    m_hashingInProgress = false;
    findSharedFiles();
    hashNextFile();
}

// ---------------------------------------------------------------------------
// safeAddKFile — add a file to the shared list
// ---------------------------------------------------------------------------

bool SharedFileList::safeAddKFile(KnownFile* file, bool onlyAdd)
{
    if (!file)
        return false;

    QMutexLocker locker(&m_mutex);

    MD4Key key(file->fileHash());

    // Check unshared list
    if (m_unsharedFiles.contains(key))
        return false;

    if (m_filesMap.contains(key)) {
        logDebug(QStringLiteral("Duplicate hash: \"%1\" has same MD4 as existing \"%2\" — skipped")
                     .arg(file->fileName(), m_filesMap[key]->fileName()));
        return false;
    }

    m_filesMap[key] = file;
    addKeywords(file);

    if (!onlyAdd) {
        file->setLastSeen(std::time(nullptr));
    }

    emit fileAdded(file);
    return true;
}

// ---------------------------------------------------------------------------
// removeFile
// ---------------------------------------------------------------------------

bool SharedFileList::removeFile(KnownFile* file)
{
    if (!file)
        return false;

    QMutexLocker locker(&m_mutex);

    MD4Key key(file->fileHash());
    auto it = m_filesMap.find(key);
    if (it == m_filesMap.end())
        return false;

    removeKeywords(file);
    m_filesMap.erase(it);
    m_unsharedFiles.insert(key);

    emit fileRemoved(file);
    return true;
}

// ---------------------------------------------------------------------------
// process — periodic tick
// ---------------------------------------------------------------------------

void SharedFileList::process()
{
    publish();

    // ED2K server publishing — send unpublished files to connected server
    if (m_serverConnect && m_serverConnect->isConnected())
        sendListToServer();
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

KnownFile* SharedFileList::getFileByID(const uint8* hash) const
{
    QMutexLocker locker(&m_mutex);
    auto it = m_filesMap.find(MD4Key(hash));
    return (it != m_filesMap.end()) ? it->second : nullptr;
}

bool SharedFileList::isUnsharedFile(const uint8* hash) const
{
    QMutexLocker locker(&m_mutex);
    return m_unsharedFiles.contains(MD4Key(hash));
}

int SharedFileList::getCount() const
{
    QMutexLocker locker(&m_mutex);
    return static_cast<int>(m_filesMap.size());
}

uint64 SharedFileList::getDataSize(uint64& largestOut) const
{
    QMutexLocker locker(&m_mutex);
    uint64 total = 0;
    largestOut = 0;
    for (const auto& [key, file] : m_filesMap) {
        auto sz = static_cast<uint64>(file->fileSize());
        total += sz;
        if (sz > largestOut)
            largestOut = sz;
    }
    return total;
}

// ---------------------------------------------------------------------------
// Keywords
// ---------------------------------------------------------------------------

void SharedFileList::addKeywords(KnownFile* file)
{
    m_keywords.addKeywords(file);
}

void SharedFileList::removeKeywords(KnownFile* file)
{
    m_keywords.removeKeywords(file);
}

// ---------------------------------------------------------------------------
// Server / Kad publishing
// ---------------------------------------------------------------------------

/// Map internal priority to a sortable integer (higher = published first).
static int realPriority(uint8 prio)
{
    switch (prio) {
    case kPrVeryHigh: return 4;
    case kPrHigh:     return 3;
    case kPrNormal:   return 2;
    case kPrLow:      return 1;
    case kPrVeryLow:  return 0;
    default:          return 2;
    }
}

void SharedFileList::sendListToServer()
{
    if (!m_serverConnect || !m_serverConnect->isConnected())
        return;

    Server* srv = m_serverConnect->currentServer();

    // Collect unpublished files, sorted by priority (highest first)
    std::vector<KnownFile*> sortedFiles;
    {
        QMutexLocker locker(&m_mutex);
        for (auto& [key, file] : m_filesMap) {
            if (!file->publishedED2K())
                sortedFiles.push_back(file);
        }
    }

    std::sort(sortedFiles.begin(), sortedFiles.end(),
              [](const KnownFile* a, const KnownFile* b) {
                  return realPriority(a->upPriority()) > realPriority(b->upPriority());
              });

    constexpr uint32 kMaxFiles = 200;
    if (sortedFiles.size() > kMaxFiles)
        sortedFiles.resize(kMaxFiles);

    if (sortedFiles.empty())
        return;

    const bool newServer = srv && srv->supportsZlib(); // compression flag as "newer server" indicator
    const bool useNewTags = srv && srv->supportsNewTags();
    const bool useUTF8 = srv && srv->supportsUnicode();
    const auto utfMode = useUTF8 ? UTF8Mode::Raw : UTF8Mode::None;

    SafeMemFile files;
    files.writeUInt32(static_cast<uint32>(sortedFiles.size()));

    for (KnownFile* file : sortedFiles) {
        // 16-byte MD4 hash
        files.writeHash16(file->fileHash());

        // Client ID + port — newer servers use magic values for file status
        uint32 clientID = 0;
        uint16 clientPort = 0;
        if (newServer) {
            if (file->isPartFile()) {
                clientID = 0xFCFCFCFC;
                clientPort = 0xFCFC;
            } else {
                clientID = 0xFBFBFBFB;
                clientPort = 0xFBFB;
            }
        }
        files.writeUInt32(clientID);
        files.writeUInt16(clientPort);

        // Build tag list
        std::vector<Tag> tags;
        tags.emplace_back(FT_FILENAME, file->fileName());

        auto sz = static_cast<uint64>(file->fileSize());
        tags.emplace_back(FT_FILESIZE, static_cast<uint32>(sz & 0xFFFFFFFF));
        if (file->isLargeFile())
            tags.emplace_back(FT_FILESIZE_HI, static_cast<uint32>(sz >> 32));

        if (!file->fileType().isEmpty())
            tags.emplace_back(FT_FILETYPE, file->fileType());

        if (file->getFileRating() > 0)
            tags.emplace_back(FT_FILERATING, file->getFileRating());

        files.writeUInt32(static_cast<uint32>(tags.size()));
        for (const auto& tag : tags) {
            if (useNewTags)
                tag.writeNewEd2kTag(files, utfMode);
            else
                tag.writeTagToFile(files, utfMode);
        }
    }

    auto packet = std::make_unique<Packet>(files, OP_EDONKEYPROT, OP_OFFERFILES);
    if (srv && srv->supportsZlib())
        packet->packPacket();

    m_serverConnect->sendPacket(std::move(packet));

    // Mark files as published
    for (KnownFile* file : sortedFiles)
        file->setPublishedED2K(true);

    logDebug(QStringLiteral("Sent %1 shared files to server").arg(sortedFiles.size()));
}

// ---------------------------------------------------------------------------
// setServerConnect — wire up reconnect signal to reset publish flags
// ---------------------------------------------------------------------------

void SharedFileList::setServerConnect(ServerConnect* sc)
{
    if (m_serverConnect)
        disconnect(m_serverConnect, nullptr, this, nullptr);

    m_serverConnect = sc;

    if (m_serverConnect) {
        connect(m_serverConnect, &ServerConnect::connectedToServer,
                this, [this]() { clearED2KPublishFlags(); });
    }
}

// ---------------------------------------------------------------------------
// clearED2KPublishFlags — reset all files so they get re-offered to new server
// ---------------------------------------------------------------------------

void SharedFileList::clearED2KPublishFlags()
{
    QMutexLocker locker(&m_mutex);
    for (auto& [key, file] : m_filesMap)
        file->setPublishedED2K(false);
}

void SharedFileList::publish()
{
    auto* kad = kad::Kademlia::instance();
    if (!kad || !kad->isKadReady())
        return;

    // --- Source publishing (round-robin by index) ---
    if (kad->getTotalStoreSrc() < KADEMLIATOTALSTORESRC) {
        QMutexLocker locker(&m_mutex);
        const auto fileCount = static_cast<uint32>(m_filesMap.size());
        if (fileCount > 0) {
            for (uint32 i = 0; i < fileCount; ++i) {
                uint32 idx = (m_currFileSrc + i) % fileCount;
                KnownFile* file = getFileByIndex(idx);
                if (file && file->publishSrc()) {
                    kad::UInt128 target;
                    target.setValueBE(file->fileHash());
                    auto* search = kad::SearchManager::prepareLookup(
                        kad::SearchType::StoreFile, true, target);
                    if (search)
                        kad::SearchManager::startSearch(search);
                    m_currFileSrc = (idx + 1) % fileCount;
                    break;
                }
            }
        }
    }

    // --- Notes publishing (round-robin by index) ---
    if (kad->getTotalStoreNotes() < KADEMLIATOTALSTORENOTES) {
        QMutexLocker locker(&m_mutex);
        const auto fileCount = static_cast<uint32>(m_filesMap.size());
        if (fileCount > 0) {
            for (uint32 i = 0; i < fileCount; ++i) {
                uint32 idx = (m_currFileNotes + i) % fileCount;
                KnownFile* file = getFileByIndex(idx);
                if (file && file->publishNotes()) {
                    kad::UInt128 target;
                    target.setValueBE(file->fileHash());
                    auto* search = kad::SearchManager::prepareLookup(
                        kad::SearchType::StoreNotes, true, target);
                    if (search)
                        kad::SearchManager::startSearch(search);
                    m_currFileNotes = (idx + 1) % fileCount;
                    break;
                }
            }
        }
    }

    // --- Keyword publishing ---
    if (kad->getTotalStoreKey() < KADEMLIATOTALSTOREKEY) {
        time_t tNow = std::time(nullptr);

        if (tNow >= m_keywords.nextPublishTime()) {
            PublishKeyword* kw = m_keywords.getNextKeyword();
            if (!kw) {
                // Cycled through all keywords — reset and schedule next round
                m_keywords.resetNextKeyword();
                m_keywords.setNextPublishTime(tNow + KADEMLIAREPUBLISHTIMEK);
                return;
            }

            if (tNow >= kw->nextPublishTime()) {
                // Prepare StoreKeyword search
                auto* search = kad::SearchManager::prepareLookup(
                    kad::SearchType::StoreKeyword, false, kw->kadID());

                if (search) {
                    // Add file IDs (max 150 per keyword, rotate after)
                    constexpr int kMaxFilesPerKeyword = 150;
                    int added = 0;

                    QMutexLocker locker(&m_mutex);
                    for (KnownFile* file : kw->fileRefs()) {
                        if (added >= kMaxFilesPerKeyword)
                            break;
                        // Skip part files and verify the file is still shared
                        if (file->isPartFile())
                            continue;
                        if (!m_filesMap.contains(MD4Key(file->fileHash())))
                            continue;

                        kad::UInt128 fileID;
                        fileID.setValueBE(file->fileHash());
                        search->addFileID(fileID);
                        ++added;
                    }
                    locker.unlock();

                    if (added > 0) {
                        kad::SearchManager::startSearch(search);
                        kw->incPublishedCount();
                    } else {
                        delete search;
                    }

                    // Rotate references so next publish starts with different files
                    kw->rotateReferences(added);
                    kw->setNextPublishTime(tNow + KADEMLIAREPUBLISHTIMEK);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// findSharedFiles — scan configured shared directories
// ---------------------------------------------------------------------------

void SharedFileList::findSharedFiles()
{
    // Add incoming directory
    const QString incomingDir = thePrefs.incomingDir();
    if (!incomingDir.isEmpty())
        addFilesFromDirectory(incomingDir);

    // Add configured shared directories
    for (const auto& dir : thePrefs.sharedDirs()) {
        if (!dir.isEmpty() && dir != incomingDir)
            addFilesFromDirectory(dir);
    }
}

// ---------------------------------------------------------------------------
// addFilesFromDirectory
// ---------------------------------------------------------------------------

void SharedFileList::addFilesFromDirectory(const QString& dir, const QString& sharedDir)
{
    QDir directory(dir);
    if (!directory.exists())
        return;

    QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (!fi.isFile() || fi.size() == 0)
            continue;

        const QString filename = fi.fileName();
        const QString fileDir = fi.absolutePath();

        // Skip .part and .part.met files
        if (filename.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive)
            || filename.endsWith(QStringLiteral(".part.met"), Qt::CaseInsensitive))
            continue;

        // Check if already known
        if (m_knownFiles) {
            KnownFile* existing = m_knownFiles->findKnownFile(
                filename,
                static_cast<time_t>(fi.lastModified().toSecsSinceEpoch()),
                static_cast<uint64>(fi.size()));

            if (existing) {
                existing->setPath(fileDir);
                existing->setFilePath(fi.absoluteFilePath());
                if (!sharedDir.isEmpty())
                    existing->setSharedDirectory(sharedDir);
                m_filesMap[MD4Key(existing->fileHash())] = existing;
                emit fileAdded(existing);
                continue;
            }
        }

        // Queue for hashing
        m_waitingForHash.push_back({fileDir, filename, sharedDir});
    }
}

// ---------------------------------------------------------------------------
// hashNextFile — feed one file to the hashing thread
// ---------------------------------------------------------------------------

void SharedFileList::hashNextFile()
{
    if (m_waitingForHash.empty() || !m_hashingThread) {
        m_hashingInProgress = false;
        return;
    }

    m_hashingInProgress = true;
    auto entry = std::move(m_waitingForHash.front());
    m_waitingForHash.pop_front();

    m_hashingThread->enqueue({entry.directory, entry.filename, entry.sharedDirectory, m_generation});
}

// ---------------------------------------------------------------------------
// Hashing callbacks
// ---------------------------------------------------------------------------

void SharedFileList::onHashingFinished(KnownFile* file, uint64 generation)
{
    if (!file)
        return;

    QMutexLocker locker(&m_mutex);

    // Reject stale completions from a previous generation
    if (generation != m_generation) {
        delete file;
        return;
    }

    locker.unlock();

    // Add to known files
    if (m_knownFiles)
        m_knownFiles->safeAddKFile(file);

    // Add to shared list
    safeAddKFile(file, true);

    // Hash next file in queue
    QMutexLocker locker2(&m_mutex);
    hashNextFile();
}

void SharedFileList::onHashingFailed(const QString& directory, const QString& filename, uint64 generation)
{
    QMutexLocker locker(&m_mutex);

    // Reject stale completions from a previous generation
    if (generation != m_generation)
        return;

    logWarning(QStringLiteral("Failed to hash file: %1/%2").arg(directory, filename));

    // Continue with next file
    hashNextFile();
}

void SharedFileList::forEachFile(const std::function<void(KnownFile*)>& callback) const
{
    QMutexLocker locker(&m_mutex);
    for (auto& [key, file] : m_filesMap)
        callback(file);
}

int SharedFileList::getHashingCount() const
{
    QMutexLocker locker(&m_mutex);
    int count = static_cast<int>(m_waitingForHash.size());
    if (m_hashingInProgress)
        ++count;
    return count;
}

// ---------------------------------------------------------------------------
// getFileByIndex — index-based access for round-robin publishing
// ---------------------------------------------------------------------------

KnownFile* SharedFileList::getFileByIndex(uint32 index) const
{
    if (index >= m_filesMap.size())
        return nullptr;
    auto it = m_filesMap.begin();
    std::advance(it, index);
    return it->second;
}

} // namespace eMule
