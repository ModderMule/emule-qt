#include "pch.h"
/// @file SharedFileList.cpp
/// @brief Shared file management — port of MFC CSharedFileList.

#include "files/SharedFileList.h"
#include "app/AppContext.h"
#include "files/Collection.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchManager.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "files/PartFile.h"
#include "transfer/DownloadQueue.h"
#include "utils/Log.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>


namespace eMule {

/// Minimum spacing between two OP_OFFERFILES sends. MFC ED2KREPUBLISHTIME,
/// srchybrid/Opcodes.h:88 — one minute.
constexpr time_t kEd2kRepublishSecs = 60;

/// Client-side ceiling on files per OP_OFFERFILES packet. A server's advertised
/// GetSoftFiles() may lower this, never raise it (srchybrid/SharedFileList.cpp:832-834).
constexpr uint32 kMaxOfferedFiles = 200;

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
    std::erase_if(m_queue, [](const Job& j) { return !j.isRehash(); });
}

void HashingThread::requestStop()
{
    QMutexLocker locker(&m_mutex);
    m_stopRequested = true;
    m_condition.wakeAll();
}

void HashingThread::runRehash(const Job& job)
{
    logInfo(QStringLiteral("Rehashing part file: %1").arg(job.rehashPartPath));

    const auto partCount = static_cast<uint32>(job.rehashPartHashes.size());
    QByteArray partOk(static_cast<qsizetype>(partCount), '\0');

    QFile file(job.rehashPartPath);
    if (!file.open(QIODevice::ReadOnly)) {
        logError(QStringLiteral("Rehash failed to open %1").arg(job.rehashPartPath));
        // Every part reads as bad, which is the safe answer: the data gets re-fetched.
        emit partFileRehashed(job.rehashFileHash, partOk);
        return;
    }

    for (uint32 part = 0; part < partCount; ++part) {
        const uint64 start = static_cast<uint64>(part) * PARTSIZE;
        if (start >= job.rehashFileSize)
            break;
        const uint64 len = std::min<uint64>(PARTSIZE, job.rehashFileSize - start);

        if (!file.seek(static_cast<qint64>(start)))
            break;
        const QByteArray data = file.read(static_cast<qint64>(len));
        if (static_cast<uint64>(data.size()) != len)
            break;   // truncated .part — the rest stays marked missing

        std::array<uint8, 16> actual{};
        KnownFile::createHashFromMemory(reinterpret_cast<const uint8*>(data.constData()),
                                        static_cast<uint32>(len), actual.data(), nullptr);

        if (actual == job.rehashPartHashes[part])
            partOk[static_cast<qsizetype>(part)] = 1;

        emit hashingProgress(static_cast<int>((part + 1) * 100 / std::max(1u, partCount)));
    }

    emit partFileRehashed(job.rehashFileHash, partOk);
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

        if (job.isRehash()) {
            runRehash(job);
            continue;
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
    : EntityMap<MD4Key, KnownFile>(parent)
    , m_knownFiles(knownFiles)
{
    loadSharedFilesConfig();

    m_hashingThread = new HashingThread(this);
    connect(m_hashingThread, &HashingThread::hashingFinished,
            this, &SharedFileList::onHashingFinished, Qt::QueuedConnection);
    connect(m_hashingThread, &HashingThread::hashingFailed,
            this, &SharedFileList::onHashingFailed, Qt::QueuedConnection);
    connect(m_hashingThread, &HashingThread::partFileRehashed,
            this, &SharedFileList::onPartFileRehashed, Qt::QueuedConnection);
    m_hashingThread->start();
}

SharedFileList::~SharedFileList()
{
    if (m_hashingThread) {
        m_hashingThread->requestStop();
        m_hashingThread->wait();
    }
    // Note: files in m_map are owned by KnownFileList, not us
}

// ---------------------------------------------------------------------------
// reload — rescan shared directories
// ---------------------------------------------------------------------------

void SharedFileList::reload()
{
    {
        QMutexLocker hashLocker(&m_hashMutex);
        ++m_generation;
        m_hashingThread->clearQueue();
        m_waitingForHash.clear();
        m_hashingInProgress = false;
    }

    clearEntities();

    // Deliberately unlocked: the scan feeds every file back in through safeAddKFile()
    // -> addEntity(), which takes the map lock per file, and it walks the whole share
    // from disk — not something to hold any lock across.
    findSharedFiles();

    {
        QMutexLocker hashLocker(&m_hashMutex);
        hashNextFile();
    }

    // Part files are shared files too, and the clear above just dropped them all.
    // MFC re-adds from inside FindSharedFiles (srchybrid/SharedFileList.cpp:551).
    if (theApp.downloadQueue)
        theApp.downloadQueue->addPartFilesToShare();
}

// ---------------------------------------------------------------------------
// safeAddKFile — add a file to the shared list
// ---------------------------------------------------------------------------

bool SharedFileList::safeAddKFile(KnownFile* file, bool onlyAdd)
{
    // EntityMap::addEntity takes the lock and runs keyFor -> isDuplicate -> insert
    // -> onEntityAdded under it, then releases. Everything below runs unlocked,
    // matching MFC, which drops its own lock at srchybrid/SharedFileList.cpp:699
    // before exactly this work: collection parsing (disk I/O), keywords, last-seen.
    if (!addEntity(file))
        return false;

    detectCollection(file);
    addKeywords(file);
    file->setLastSeen(std::time(nullptr));

    // MFC SafeAddKFile:662-668 — bOnlyAdd suppresses the republish, not the last-seen
    // stamp, which AddFile:723 sets unconditionally.
    if (!onlyAdd)
        m_republishED2K = true;

    emit fileAdded(file);
    return true;
}

// ---------------------------------------------------------------------------
// removeFile
// ---------------------------------------------------------------------------

bool SharedFileList::removeFile(KnownFile* file)
{
    // EntityMap::removeEntity: erase by keyFor -> onEntityRemoved (under lock).
    if (!removeEntity(file))
        return false;

    removeKeywords(file);
    emit fileRemoved(file);
    return true;
}

// ---------------------------------------------------------------------------
// EntityMap hooks
// ---------------------------------------------------------------------------

MD4Key SharedFileList::keyFor(KnownFile* file) const
{
    return MD4Key(file->fileHash());
}

bool SharedFileList::isDuplicate(const MD4Key& key, KnownFile* file) const
{
    // Deliberately does NOT consult m_unsharedFiles. That set records what we used
    // to share so isUnsharedFile() can answer a peer; it is not a re-add gate, and
    // MFC's AddFile clears it on every successful add (srchybrid/SharedFileList.cpp:695).
    // What actually keeps an unshared file out is shouldBeShared(), applied by the
    // directory scan.
    if (m_map.contains(key)) {
        logDebug(QStringLiteral("Duplicate hash: \"%1\" has same MD4 as existing \"%2\" — skipped")
                     .arg(file->fileName(), m_map.at(key)->fileName()));
        return true;
    }
    return false;
}

// Both hooks run with the base's m_mutex held (EntityMap.h), so they carry only the
// m_unsharedFiles bookkeeping — which is guarded by that same lock. Collection
// parsing, keywords, last-seen and the signals live in safeAddKFile()/removeFile(),
// where the lock is no longer held.

void SharedFileList::onEntityAdded(KnownFile* file)
{
    // We share it again, so we no longer "used to" — MFC AddFile:695.
    m_unsharedFiles.erase(keyFor(file));
}

void SharedFileList::onEntityRemoved(KnownFile* file)
{
    // Remember the hash so isUnsharedFile() can tell a requesting peer we know this
    // file but are not offering it (MFC RemoveFile:776 -> BaseClient.cpp:2540).
    m_unsharedFiles.insert(keyFor(file));
}

// ---------------------------------------------------------------------------
// process — periodic tick
// ---------------------------------------------------------------------------

void SharedFileList::process()
{
    publish();

    // ED2K server publishing — MFC gates on a dirty flag plus a one-minute spacing
    // (ED2KREPUBLISHTIME, srchybrid/SharedFileList.cpp:1229-1236) rather than walking
    // the whole share on every tick.
    if (m_republishED2K && std::time(nullptr) >= m_lastPublishED2K + kEd2kRepublishSecs) {
        sendListToServer();
        m_lastPublishED2K = std::time(nullptr);
    }
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

KnownFile* SharedFileList::getFileByID(const uint8* hash) const
{
    return findByKey(MD4Key(hash));
}

bool SharedFileList::isUnsharedFile(const uint8* hash) const
{
    QMutexLocker locker(&m_mutex);
    return m_unsharedFiles.contains(MD4Key(hash));
}

int SharedFileList::getCount() const
{
    return count();
}

uint64 SharedFileList::getDataSize(uint64& largestOut) const
{
    QMutexLocker locker(&m_mutex);
    uint64 total = 0;
    largestOut = 0;
    for (const auto& [key, file] : m_map) {
        auto sz = static_cast<uint64>(file->fileSize());
        total += sz;
        if (sz > largestOut)
            largestOut = sz;
    }
    return total;
}

// ---------------------------------------------------------------------------
// Share membership — MFC CSharedFileList::ShouldBeShared and friends
// ---------------------------------------------------------------------------

namespace {

/// MFC compares these paths with CompareNoCase. Normalise separators and trailing
/// slashes too, so "/a/b" and "/a/b/" are the same directory.
[[nodiscard]] bool samePath(const QString& a, const QString& b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;
    return QDir::cleanPath(a).compare(QDir::cleanPath(b), Qt::CaseInsensitive) == 0;
}

[[nodiscard]] bool containsPath(const QSet<QString>& set, const QString& path)
{
    for (const QString& p : set)
        if (samePath(p, path))
            return true;
    return false;
}

} // namespace

bool SharedFileList::shouldBeShared(const QString& dirPath, const QString& filePath,
                                    bool mustBeShared) const
{
    // The incoming directory is always shared and can never be unshared. MFC also
    // checks each category's incoming path here; this port has no categories.
    if (samePath(dirPath, thePrefs.incomingDir()))
        return true;

    if (mustBeShared)
        return false;

    if (!filePath.isEmpty()) {
        if (containsPath(m_singleExcludedFiles, filePath))
            return false;
        if (containsPath(m_singleSharedFiles, filePath))
            return true;
    }

    for (const QString& dir : thePrefs.sharedDirs())
        if (samePath(dirPath, dir))
            return true;

    return false;
}

bool SharedFileList::excludeFile(const QString& filePath)
{
    if (filePath.isEmpty())
        return false;

    const QString dirPath = QFileInfo(filePath).absolutePath();

    // First drop it from the explicitly-shared list, if that is why it was shared.
    bool wasSingleShared = false;
    for (const QString& p : m_singleSharedFiles) {
        if (samePath(p, filePath)) {
            m_singleSharedFiles.remove(p);
            wasSingleShared = true;
            break;
        }
    }

    if (!wasSingleShared && !shouldBeShared(dirPath, filePath, false))
        return false;   // we do not actually share it — nothing to exclude

    if (shouldBeShared(dirPath, filePath, /*mustBeShared=*/true)) {
        logWarning(QStringLiteral("Cannot unshare \"%1\": it is in the incoming directory")
                       .arg(filePath));
        return false;
    }

    m_singleExcludedFiles.insert(filePath);

    // It need not be in the map — it may still be hashing, or not loaded yet.
    KnownFile* shared = nullptr;
    forEach([&](KnownFile* f) {
        if (!shared && samePath(f->filePath(), filePath))
            shared = f;
    });
    if (shared)
        removeFile(shared);

    saveSharedFilesConfig();
    return true;
}

bool SharedFileList::addSingleSharedFile(const QString& filePath)
{
    if (filePath.isEmpty())
        return false;

    const QString dirPath = QFileInfo(filePath).absolutePath();
    if (!thePrefs.isShareableDirectory(dirPath)) {
        logWarning(QStringLiteral("Cannot share \"%1\": its directory is not shareable")
                       .arg(filePath));
        return false;
    }

    // Un-excluding is enough when the directory already covers it.
    bool wasExcluded = false;
    for (const QString& p : m_singleExcludedFiles) {
        if (samePath(p, filePath)) {
            m_singleExcludedFiles.remove(p);
            wasExcluded = true;
            break;
        }
    }

    if (!wasExcluded && !shouldBeShared(dirPath, filePath, false))
        m_singleSharedFiles.insert(filePath);   // the directory is not shared, so it needs its own entry

    checkAndAddSingleFile(filePath);
    saveSharedFilesConfig();
    return true;
}

bool SharedFileList::containsSingleSharedFiles(const QString& dirPath) const
{
    for (const QString& p : m_singleSharedFiles)
        if (samePath(QFileInfo(p).absolutePath(), dirPath))
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// sharedfiles.dat — MFC's own format, so a config directory round-trips with eMule:
// UTF-16LE with a BOM, CRLF lines, a '-' prefix marking an excluded path.
// srchybrid/SharedFileList.cpp:1578-1600.
// ---------------------------------------------------------------------------

QString SharedFileList::sharedFilesConfigPath() const
{
    return QDir(thePrefs.configDir()).filePath(QStringLiteral("sharedfiles.dat"));
}

void SharedFileList::loadSharedFilesConfig()
{
    m_singleSharedFiles.clear();
    m_singleExcludedFiles.clear();

    QFile file(sharedFilesConfigPath());
    if (!file.open(QIODevice::ReadOnly))
        return;   // absent on a fresh install; not an error

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf16LE);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        // A BOM read as text arrives as U+FEFF on the first line.
        if (line.startsWith(QChar(0xFEFF)))
            line.remove(0, 1);
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('-')))
            m_singleExcludedFiles.insert(line.mid(1));
        else
            m_singleSharedFiles.insert(line);
    }

    logDebug(QStringLiteral("sharedfiles.dat: %1 shared, %2 excluded")
                 .arg(m_singleSharedFiles.size())
                 .arg(m_singleExcludedFiles.size()));
}

void SharedFileList::saveSharedFilesConfig() const
{
    const QString path = sharedFilesConfigPath();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        logError(QStringLiteral("Failed to save %1").arg(path));
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf16LE);
    out.setGenerateByteOrderMark(true);
    for (const QString& p : m_singleSharedFiles)
        out << p << QStringLiteral("\r\n");
    for (const QString& p : m_singleExcludedFiles)
        out << QLatin1Char('-') << p << QStringLiteral("\r\n");
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

std::vector<KnownFile*> SharedFileList::takeFilesToOffer(const Server* srv)
{
    // A server that cannot index files over ~4 GB must not be offered them, or the
    // whole entry is wasted (srchybrid/SharedFileList.cpp:817).
    const bool serverTakesLargeFiles = srv && srv->supportsLargeFilesTCP();

    std::vector<KnownFile*> sortedFiles;

    // Selecting and marking happen under one lock, so a concurrent reload() cannot
    // free a file between reading the map and marking it published.
    QMutexLocker locker(&m_mutex);
    if (m_map.empty())
        return sortedFiles;

    for (auto& [key, file] : m_map) {
        if (file->publishedED2K())
            continue;
        if (file->isLargeFile() && !serverTakesLargeFiles)
            continue;
        sortedFiles.push_back(file);
    }

    std::sort(sortedFiles.begin(), sortedFiles.end(),
              [](const KnownFile* a, const KnownFile* b) {
                  return realPriority(a->upPriority()) > realPriority(b->upPriority());
              });

    // The server's own soft limit, clamped: 0 means "unknown", and anything above our
    // own ceiling is ignored (srchybrid/SharedFileList.cpp:832-834).
    uint32 limit = srv ? srv->softFiles() : 0;
    if (limit == 0 || limit > kMaxOfferedFiles)
        limit = kMaxOfferedFiles;
    if (sortedFiles.size() > limit)
        sortedFiles.resize(limit);

    for (KnownFile* file : sortedFiles)
        file->setPublishedED2K(true);

    return sortedFiles;
}

void SharedFileList::sendListToServer()
{
    if (!m_serverConnect || !m_serverConnect->isConnected())
        return;

    Server* srv = m_serverConnect->currentServer();
    std::vector<KnownFile*> sortedFiles = takeFilesToOffer(srv);

    if (sortedFiles.empty()) {
        // Nothing left to offer — stop re-entering until something changes.
        m_republishED2K = false;
        return;
    }

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
    {
        QMutexLocker locker(&m_mutex);
        for (auto& [key, file] : m_map)
            file->setPublishedED2K(false);
    }
    // MFC ClearED2KPublishInfo (srchybrid/SharedFileList.cpp:872-877) arms the flag
    // too — otherwise the whole share is marked unpublished and never re-offered.
    m_republishED2K = true;
    m_lastPublishED2K = 0;
}

void SharedFileList::publish()
{
    auto* kad = kad::Kademlia::instance();
    if (!kad || !kad->isKadReady())
        return;

    // Don't publish until self-lookup (NodeComplete) finishes populating
    // the routing table.  Matches MFC SharedFileList.cpp:1247.
    if (!kad->getPublish())
        return;

    // --- Source publishing (round-robin by index) ---
    if (kad->getTotalStoreSrc() < KADEMLIATOTALSTORESRC) {
        QMutexLocker locker(&m_mutex);
        const auto fileCount = static_cast<uint32>(m_map.size());
        if (fileCount > 0) {
            for (uint32 i = 0; i < fileCount; ++i) {
                uint32 idx = (m_currFileSrc + i) % fileCount;
                KnownFile* file = fileAtIndexLocked(idx);
                if (file && file->publishSrc()) {
                    kad::UInt128 target;
                    target.setValueBE(file->fileHash());
                    auto* search = kad::SearchManager::prepareLookup(
                            kad::SearchType::StoreFile, true, target);
                    if (!search)
                        file->setLastPublishTimeKadSrc(0, 0);
                    else
                        search->setGUIName(file->fileName());
                    m_currFileSrc = (idx + 1) % fileCount;
                    break;
                }
            }
        }
    }

    // --- Notes publishing (round-robin by index) ---
    if (kad->getTotalStoreNotes() < KADEMLIATOTALSTORENOTES) {
        QMutexLocker locker(&m_mutex);
        const auto fileCount = static_cast<uint32>(m_map.size());
        if (fileCount > 0) {
            for (uint32 i = 0; i < fileCount; ++i) {
                uint32 idx = (m_currFileNotes + i) % fileCount;
                KnownFile* file = fileAtIndexLocked(idx);
                if (file && file->publishNotes()) {
                    kad::UInt128 target;
                    target.setValueBE(file->fileHash());
                    auto* search = kad::SearchManager::prepareLookup(
                            kad::SearchType::StoreNotes, true, target);
                    if (!search)
                        file->setLastPublishTimeKadNotes(0);
                    else
                        search->setGUIName(file->fileName());
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
                    search->setGUIName(kw->keyword());
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
                        if (!m_map.contains(MD4Key(file->fileHash())))
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

    // Files shared individually, outside any shared directory — otherwise a reload
    // would silently drop them (srchybrid/SharedFileList.cpp:586-587).
    for (const QString& filePath : m_singleSharedFiles)
        checkAndAddSingleFile(filePath);
}

// ---------------------------------------------------------------------------
// checkAndAddSingleFile — one explicitly-shared file
// ---------------------------------------------------------------------------

void SharedFileList::checkAndAddSingleFile(const QString& filePath)
{
    const QFileInfo fi(filePath);
    if (!fi.isFile() || fi.size() == 0)
        return;

    if (m_knownFiles) {
        KnownFile* existing = m_knownFiles->findKnownFile(
            fi.fileName(),
            static_cast<time_t>(fi.lastModified().toSecsSinceEpoch()),
            static_cast<uint64>(fi.size()));

        if (existing) {
            existing->setPath(fi.absolutePath());
            existing->setFilePath(fi.absoluteFilePath());
            safeAddKFile(existing, /*onlyAdd=*/true);
            return;
        }
    }

    QMutexLocker hashLocker(&m_hashMutex);
    m_waitingForHash.push_back({fi.absolutePath(), fi.fileName(), {}});
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

        // The user unshared this one individually. This — not the m_unsharedFiles
        // hash set — is what makes an unshare survive a reload and a restart.
        if (containsPath(m_singleExcludedFiles, fi.absoluteFilePath()))
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
                // Through the front door: writing m_map here would skip the duplicate
                // check, the collection detection and addKeywords() — which is why a
                // re-scanned known file used to be invisible to Kad keyword publishing.
                // onlyAdd, because a bulk scan must not schedule one republish per file.
                safeAddKFile(existing, /*onlyAdd=*/true);
                continue;
            }
        }

        // Queue for hashing
        {
            QMutexLocker hashLocker(&m_hashMutex);
            m_waitingForHash.push_back({fileDir, filename, sharedDir});
        }
    }
}

// ---------------------------------------------------------------------------
// detectCollection — attach a parsed .emulecollection, if this is one
// ---------------------------------------------------------------------------

void SharedFileList::detectCollection(KnownFile* file)
{
    // Must happen before addKeywords(): setCollection() rebuilds the Kad keyword list
    // to include the collection's author key, and publishing that key is what makes
    // "Search Author's Collections" work (srchybrid/SharedFileList.cpp:703-721).
    if (file->isPartFile() || file->collection()
        || !Collection::hasCollectionExtension(file->fileName()))
        return;

    auto coll = std::make_unique<Collection>();
    if (coll->initFromFile(file->filePath(), file->fileName()))
        file->setCollection(std::move(coll));
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

    {
        QMutexLocker hashLocker(&m_hashMutex);
        // Reject stale completions from a previous generation
        if (generation != m_generation) {
            delete file;
            return;
        }
    }

    // Add to known files
    if (m_knownFiles)
        m_knownFiles->safeAddKFile(file);

    // The user may have unshared it while it hashed — MFC re-checks at the same point
    // (FileHashingFinished, srchybrid/SharedFileList.cpp:743).
    if (!file->filePath().isEmpty()
        && !shouldBeShared(QFileInfo(file->filePath()).absolutePath(), file->filePath(), false))
    {
        QMutexLocker hashLocker(&m_hashMutex);
        hashNextFile();
        return;
    }

    // Add to shared list. Not onlyAdd — a file that has just finished hashing is new
    // to the share and has to be offered, which is what arming the republish does
    // (MFC FileHashingFinished, srchybrid/SharedFileList.cpp:751).
    safeAddKFile(file);

    // Hash next file in queue
    QMutexLocker hashLocker(&m_hashMutex);
    hashNextFile();
}

void SharedFileList::enqueuePartFileRehash(PartFile* file)
{
    if (!file || !m_hashingThread)
        return;

    HashingThread::Job job;
    job.rehashFileHash = QByteArray(reinterpret_cast<const char*>(file->fileHash()), 16);
    job.rehashPartPath = file->partDataPath();
    job.rehashFileSize = static_cast<uint64>(file->fileSize());
    job.rehashPartHashes = file->fileIdentifier().getRawMD4HashSet();

    // Everything the worker needs is copied above, on this thread — it must not reach
    // back into the PartFile, which the download queue may delete meanwhile.
    m_hashingThread->enqueue(std::move(job));
}

void SharedFileList::onPartFileRehashed(const QByteArray& fileHash, const QByteArray& partOk)
{
    if (fileHash.size() != 16 || !theApp.downloadQueue)
        return;

    // Look the file up rather than trusting a pointer: it may have been cancelled
    // while the rehash ran, and the queue is the authority on what still exists.
    PartFile* file = theApp.downloadQueue->fileByID(
        reinterpret_cast<const uint8*>(fileHash.constData()));
    if (!file)
        return;

    file->applyRehashResult(partOk);
}

void SharedFileList::onHashingFailed(const QString& directory, const QString& filename, uint64 generation)
{
    QMutexLocker hashLocker(&m_hashMutex);

    // Reject stale completions from a previous generation
    if (generation != m_generation)
        return;

    logWarning(QStringLiteral("Failed to hash file: %1/%2").arg(directory, filename));

    // Continue with next file
    hashNextFile();
}

void SharedFileList::forEachFile(const std::function<void(KnownFile*)>& callback) const
{
    forEach(callback);
}

int SharedFileList::getHashingCount() const
{
    QMutexLocker hashLocker(&m_hashMutex);
    int count = static_cast<int>(m_waitingForHash.size());
    if (m_hashingInProgress)
        ++count;
    return count;
}

// ---------------------------------------------------------------------------
// fileAtIndexLocked — index-based access for round-robin publishing.
// Caller must already hold m_mutex; this deliberately does not lock.
// ---------------------------------------------------------------------------

KnownFile* SharedFileList::fileAtIndexLocked(uint32 index) const
{
    if (index >= m_map.size())
        return nullptr;
    auto it = m_map.begin();
    std::advance(it, index);
    return it->second;
}

} // namespace eMule
