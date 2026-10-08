#include "pch.h"
/// @file SharedFileList.cpp
/// @brief Shared file management — port of MFC CSharedFileList.

#include "files/SharedFileList.h"
#include "search/SeenFileIndex.h"
#include "app/AppContext.h"
#include "files/Collection.h"
#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "client/ClientList.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchManager.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "protocol/Tag.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "files/PartFile.h"
#include "files/SharedDirWatcher.h"
#include "transfer/DownloadQueue.h"
#include "utils/Log.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTextStream>

#include <map>
#include <numeric>
#include <tuple>


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
    if (job.isRehash()) {
        // A part file waits in WaitingForHash until this ran: ahead of shared files.
        const auto firstShared = std::ranges::find_if(
            m_queue, [](const Job& j) { return !j.isRehash(); });
        m_queue.insert(firstShared, std::move(job));
    } else {
        m_queue.push_back(std::move(job));
    }
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

    const QByteArray partOk = PartFile::verifyPartData(
        job.rehashPartPath, job.rehashFileSize, job.rehashFileHash, job.rehashPartHashes,
        [this](uint32 done, uint32 count) {
            emit hashingProgress(static_cast<int>(done * 100 / std::max(1u, count)));
            return true;
        });

    emit partFileRehashed(job.rehashFileHash, partOk, job.rehashToken);
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
        const KnownFile::FileStamp scanned{job.scannedSize, job.scannedMtime};
        bool changedSinceScan = false;
        bool ok = kf->createFromFile(job.directory, job.filename,
                                     [this](int percent) {
                                         emit hashingProgress(percent);
                                     },
                                     job.scannedMtime != 0 ? &scanned : nullptr,
                                     &changedSinceScan);

        if (ok) {
            logDebug(QStringLiteral("Hashed OK: %1/%2 (%3 bytes)")
                         .arg(job.directory, job.filename)
                         .arg(kf->fileSize()));
            if (!job.sharedDirectory.isEmpty())
                kf->setSharedDirectory(job.sharedDirectory);
            emit hashingFinished(kf, job.generation);
        } else {
            delete kf;
            if (changedSinceScan)
                emit hashingDeferred(job.directory, job.filename, job.generation);
            else
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
}

SharedFileList::~SharedFileList()
{
    for (HashingThread* worker : m_hashWorkers)
        worker->requestStop();
    for (HashingThread* worker : m_hashWorkers)
        worker->wait();
    saveHashFailures();
    saveKadPublishStore();
    // Note: files in m_map are owned by KnownFileList, not us
}

void SharedFileList::setKadPublishStorePath(const QString& path)
{
    m_publishStorePath = path;
    m_publishStore.load(path);
    m_publishStoreSavedAt = std::time(nullptr);
}

void SharedFileList::saveKadPublishStore()
{
    if (m_publishStorePath.isEmpty() || !m_publishStore.isDirty())
        return;
    static_cast<void>(m_publishStore.save(m_publishStorePath, std::time(nullptr)));
    m_publishStoreSavedAt = std::time(nullptr);
}

// ---------------------------------------------------------------------------
// reload — rescan shared directories
// ---------------------------------------------------------------------------

void SharedFileList::reload()
{
    rescan(QStringList{});
    if (m_watcher)
        m_watcher->setRoots(watchRoots());
}

void SharedFileList::rescanDirectory(const QString& dir)
{
    if (!dir.isEmpty())
        rescan(QStringList{dir});
}

void SharedFileList::rescanDirectories(const QStringList& dirs)
{
    if (!dirs.isEmpty())
        rescan(dirs);
}

QStringList SharedFileList::watchRoots() const
{
    // Files shared one by one live in directories that are not shared; a change to
    // them is only seen if those directories are watched as well.
    QStringList roots = shareRoots();
    QSet<QString> seen;
    for (const QString& dir : roots)
        seen.insert(pathKey(dir));
    for (const QString& filePath : m_singleSharedFiles) {
        const QString dir = QFileInfo(filePath).absolutePath();
        if (seen.contains(pathKey(dir)))
            continue;
        seen.insert(pathKey(dir));
        roots.append(dir);
    }
    return roots;
}

void SharedFileList::setWatchingEnabled(bool enabled)
{
    if (enabled == (m_watcher != nullptr))
        return;
    if (!enabled) {
        delete m_watcher;
        m_watcher = nullptr;
        m_settleSecs = 0;
        return;
    }
    m_settleSecs = 5;
    m_watcher = new SharedDirWatcher(this);
    connect(m_watcher, &SharedDirWatcher::directoriesChanged,
            this, &SharedFileList::rescanDirectories);
    connect(m_watcher, &SharedDirWatcher::overflow, this, &SharedFileList::reload);
    m_watcher->setRoots(watchRoots());
}

void SharedFileList::republishFile(KnownFile* file)
{
    republishFile(file, m_serverConnect ? m_serverConnect->currentServer() : nullptr);
}

void SharedFileList::republishFile(KnownFile* file, const Server* srv)
{
    // only servers that tell complete from partial files need the second offer
    if (file && srv && srv->supportsZlib()) {
        m_republishED2K = true;
        file->setPublishedED2K(false);
    }
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

    // What we share ourselves is a file we have seen, under the name we give it.
    if (SeenFileIndex* seen = theApp.seenFileIndex; seen && !file->isPartFile()) {
        seen->note(file->fileHash(), file->fileName(), static_cast<uint64>(file->fileSize()),
                   QDateTime::currentSecsSinceEpoch());
    }

    // Never read by the media extractor, or by an older one: its turn comes in the
    // background (a file that just finished hashing is already up to date).
    if (!file->isPartFile() && file->mediaExtractIsStale()) {
        m_metaRebuildQueue.emplace_back(file->fileHash());
        ++m_metaRebuildTotal;
    }

    emit fileAdded(file);
    return true;
}

// ---------------------------------------------------------------------------
// removeFile
// ---------------------------------------------------------------------------

bool SharedFileList::isFilePtrInList(const KnownFile* file) const
{
    return file && getFileByID(file->fileHash()) == file;
}

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
    indexDirectoryLocked(file);

    // A new file has a verdict nobody has asked for yet.
    m_containerSweepIdle = false;
    // ... and may be due for publishing
    m_srcProbeRestUntil = 0;
    m_notesProbeRestUntil = 0;
}

void SharedFileList::onEntityRemoved(KnownFile* file)
{
    // Remember the hash so isUnsharedFile() can tell a requesting peer we know this
    // file but are not offering it (MFC RemoveFile:776 -> BaseClient.cpp:2540).
    m_unsharedFiles.insert(keyFor(file));
    unindexDirectoryLocked(keyFor(file));
}

void SharedFileList::indexDirectoryLocked(KnownFile* file)
{
    const MD4Key key = keyFor(file);
    unindexDirectoryLocked(key);
    const QString& dir = file->sharedDirectory();
    if (dir.isEmpty())
        return;
    m_byDirectory[dir].insert(key);
    m_directoryOf[key] = dir;
}

void SharedFileList::unindexDirectoryLocked(const MD4Key& key)
{
    const auto it = m_directoryOf.find(key);
    if (it == m_directoryOf.end())
        return;
    if (const auto dirIt = m_byDirectory.find(it->second); dirIt != m_byDirectory.end()) {
        dirIt->second.erase(key);
        if (dirIt->second.empty())
            m_byDirectory.erase(dirIt);
    }
    m_directoryOf.erase(it);
}

std::vector<QString> SharedFileList::sharedDirectories() const
{
    QMutexLocker locker(&m_mutex);
    std::vector<QString> dirs;
    dirs.reserve(m_byDirectory.size());
    for (const auto& [dir, files] : m_byDirectory)
        dirs.push_back(dir);
    return dirs;
}

std::vector<KnownFile*> SharedFileList::filesInDirectory(const QString& dir) const
{
    QMutexLocker locker(&m_mutex);
    std::vector<KnownFile*> files;
    const auto it = m_byDirectory.find(dir);
    if (it == m_byDirectory.end())
        return files;
    files.reserve(it->second.size());
    for (const MD4Key& key : it->second)
        if (const auto fileIt = m_map.find(key); fileIt != m_map.end())
            files.push_back(fileIt->second);
    return files;
}

void SharedFileList::refreshDirectoryOf(KnownFile* file)
{
    if (!file)
        return;
    QMutexLocker locker(&m_mutex);
    const auto it = m_map.find(keyFor(file));
    if (it != m_map.end() && it->second == file)
        indexDirectoryLocked(file);
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

    warmContainerChecks();
    stepMetaDataRebuild();
    stepDeferredScans();

    if (m_publishStore.isDirty()
        && std::time(nullptr) >= m_publishStoreSavedAt + kKadPublishResaveSecs)
        saveKadPublishStore();

    if (const time_t now = std::time(nullptr); now >= m_hashFailuresCheckedAt + 60) {
        m_hashFailuresCheckedAt = now;
        saveHashFailures();
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

QString SharedFileList::pathKey(const QString& path)
{
    return path.isEmpty() ? QString() : QDir::cleanPath(path).toCaseFolded();
}

SharedFileList::ShareRules SharedFileList::shareRules() const
{
    ShareRules rules;
    for (const QString& dir : thePrefs.allIncomingDirs())
        if (!dir.isEmpty())
            rules.incomingDirs.insert(pathKey(dir));
    for (const QString& dir : thePrefs.sharedDirs())
        if (!dir.isEmpty())
            rules.sharedDirs.insert(pathKey(dir));
    if (const QString root = thePrefs.usenetTempDir(); !root.isEmpty())
        rules.usenetTempRoot = pathKey(QDir(root).absolutePath());
    return rules;
}

bool SharedFileList::shouldBeShared(const QString& dirPath, const QString& filePath,
                                    bool mustBeShared) const
{
    return shouldBeShared(shareRules(), dirPath, filePath, mustBeShared);
}

bool SharedFileList::shouldBeShared(const ShareRules& rules, const QString& dirPath,
                                    const QString& filePath, bool mustBeShared) const
{
    // Usenet scratch is never shared, and this test comes FIRST — ahead of the
    // incoming-directory rule below, which returns true unconditionally. A user
    // whose temp directory sits inside incoming would otherwise advertise every
    // half-written article on ED2K and Kad.
    if (!rules.usenetTempRoot.isEmpty()) {
        const QString& asked = filePath.isEmpty() ? dirPath : filePath;
        const QString key = asked.isEmpty() ? QString()
                                            : pathKey(QFileInfo(asked).absoluteFilePath());
        // a separator after the root, so a sibling "Usenet Archive" does not match
        if (key == rules.usenetTempRoot || key.startsWith(rules.usenetTempRoot + QLatin1Char('/')))
            return false;
    }

    const QString dirKey = pathKey(dirPath);

    // The incoming directory is always shared and can never be unshared, and so
    // is every category that has an incoming directory of its own — those are
    // where completed downloads land, and a download you cannot be asked for is
    // not a download you shared. MFC makes both checks here, walking its
    // category array down to index 1 (srchybrid/SharedFileList.cpp:1390-1396);
    // allIncomingDirs() folds index 0 into the global dir for us.
    if (rules.incomingDirs.contains(dirKey))
        return true;

    if (mustBeShared)
        return false;

    if (!filePath.isEmpty()) {
        const QString fileKey = pathKey(filePath);
        if (m_singleExcludedFiles.contains(fileKey))
            return false;
        if (m_singleSharedFiles.contains(fileKey))
            return true;
    }

    return rules.sharedDirs.contains(dirKey);
}

bool SharedFileList::excludeFile(const QString& filePath)
{
    if (filePath.isEmpty())
        return false;

    const QString dirPath = QFileInfo(filePath).absolutePath();

    // First drop it from the explicitly-shared list, if that is why it was shared.
    const QString fileKey = pathKey(filePath);
    const bool wasSingleShared = m_singleSharedFiles.remove(fileKey);
    if (wasSingleShared && m_watcher)
        m_watcher->setRoots(watchRoots());

    if (!wasSingleShared && !shouldBeShared(dirPath, filePath, false))
        return false;   // we do not actually share it — nothing to exclude

    if (shouldBeShared(dirPath, filePath, /*mustBeShared=*/true)) {
        logWarning(QStringLiteral("Cannot unshare \"%1\": it is in the incoming directory")
                       .arg(filePath));
        return false;
    }

    m_singleExcludedFiles.insert(fileKey, filePath);

    // It need not be in the map — it may still be hashing, or not loaded yet.
    KnownFile* shared = nullptr;
    forEach([&](KnownFile* f) {
        if (!shared && pathKey(f->filePath()) == fileKey)
            shared = f;
    });
    if (shared)
        removeFile(shared);

    saveSharedFilesConfig();
    return true;
}

bool SharedFileList::addFileInSharedLocation(const QString& filePath)
{
    if (filePath.isEmpty())
        return false;

    const QString dirPath = QFileInfo(filePath).absolutePath();

    // shouldBeShared() already refuses the Usenet scratch tree ahead of the
    // incoming-directory rule, so this one test covers both questions: is this
    // location shared, and is it a place we must never publish from.
    if (!shouldBeShared(dirPath, filePath, false))
        return false;

    checkAndAddSingleFile(filePath);
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

    // shouldBeShared() would refuse this anyway, but silently: the entry would go
    // into m_singleSharedFiles, persist to sharedfiles.dat, and never share
    // anything. Refuse here so the caller — and the log — get a straight answer.
    if (thePrefs.isUsenetTempPath(filePath)) {
        logWarning(QStringLiteral("Cannot share \"%1\": Usenet downloads are shared "
                                  "when they complete, not while in progress")
                       .arg(filePath));
        return false;
    }

    // Un-excluding is enough when the directory already covers it.
    const QString fileKey = pathKey(filePath);
    const bool wasExcluded = m_singleExcludedFiles.remove(fileKey);

    if (!wasExcluded && !shouldBeShared(dirPath, filePath, false))
        m_singleSharedFiles.insert(fileKey, filePath);   // the directory is not shared, so it needs its own entry
    if (m_watcher)
        m_watcher->setRoots(watchRoots());

    checkAndAddSingleFile(filePath);
    saveSharedFilesConfig();
    return true;
}

bool SharedFileList::containsSingleSharedFiles(const QString& dirPath) const
{
    const QString dirKey = pathKey(dirPath);
    for (const QString& p : m_singleSharedFiles)
        if (pathKey(QFileInfo(p).absolutePath()) == dirKey)
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
            m_singleExcludedFiles.insert(pathKey(line.mid(1)), line.mid(1));
        else
            m_singleSharedFiles.insert(pathKey(line), line);
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

        const std::vector<Tag> tags = offeredTags(*file, srv);

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

    // Firewalled with no buddy and no direct callback: nobody could fetch from us.
    // MFC SharedFileList.cpp:1240-1249.
    if (!canPublishToKad())
        return;

    const time_t tProbe = std::time(nullptr);
    publishDueSource(tProbe);
    publishDueNotes(tProbe);

    // --- Keyword publishing ---
    if (kad->getTotalStoreKey() < KADEMLIATOTALSTOREKEY) {
        time_t tNow = std::time(nullptr);

        if (tNow >= m_keywords.nextPublishTime()) {
            // The walk never rests: each keyword carries its own 24 h, and a file added
            // after a lap must not wait one out (MFC SharedFileList.cpp:179-187, :1310).
            PublishKeyword* kw = m_keywords.getNextKeyword();
            if (!kw) {
                m_keywords.resetNextKeyword();
                kw = m_keywords.getNextKeyword();
            }
            m_keywords.setNextPublishTime(tNow + KADEMLIAPUBLISHTIME);
            if (!kw)
                return;

            if (keywordIsDue(*kw, tNow)) {
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
                    noteKeywordPublished(*kw, tNow, added > 0);
                }
            }
        }
    }
}

bool SharedFileList::keywordIsDue(PublishKeyword& kw, time_t now)
{
    // First look at this keyword since the start: a publish from the last session
    // still stands if the same files are behind it.
    if (kw.nextPublishTime() == 0) {
        if (const auto due = m_publishStore.dueTime(kw.kadID(), keywordFingerprint(kw));
            due && *due > now) {
            kw.setNextPublishTime(*due);
        }
    }
    return now >= kw.nextPublishTime();
}

void SharedFileList::noteKeywordPublished(PublishKeyword& kw, time_t now, bool sent)
{
    kw.setNextPublishTime(now + KADEMLIAREPUBLISHTIMEK);
    if (sent)
        m_publishStore.note(kw.kadID(), kw.nextPublishTime(), keywordFingerprint(kw));
}

KadPublishStore::Fingerprint SharedFileList::keywordFingerprint(const PublishKeyword& kw)
{
    // The same files publish() would offer: shared, complete ones.
    KadPublishStore::Fingerprint fingerprint{};
    QMutexLocker locker(&m_mutex);
    for (const KnownFile* file : kw.fileRefs()) {
        if (file->isPartFile() || !m_map.contains(MD4Key(file->fileHash())))
            continue;
        KadPublishStore::mix(fingerprint, file->fileHash());
    }
    return fingerprint;
}

bool SharedFileList::isShareableFile(const QString& fileName, uint64 size)
{
    if (size == 0 || size > MAX_EMULE_FILE_SIZE)
        return false;
    // MFC also checks for an OLE storage; the name is the portable part of that
    return fileName.compare(QLatin1String("thumbs.db"), Qt::CaseInsensitive) != 0;
}

// ---------------------------------------------------------------------------
// checkAndAddSingleFile — one explicitly-shared file
// ---------------------------------------------------------------------------

void SharedFileList::checkAndAddSingleFile(const QString& filePath)
{
    const QFileInfo fi(filePath);
    if (!fi.isFile() || !isShareableFile(fi.fileName(), static_cast<uint64>(fi.size())))
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

    const QString key = pathKey(fi.absoluteFilePath());
    m_hashFailures.remove(key);   // asked for by hand: try again
    const QString volume = volumeKeyFor(fi.absolutePath());

    QMutexLocker hashLocker(&m_hashMutex);
    const bool waiting = std::ranges::any_of(
        m_waitingForHash, [&key](const UnknownFileEntry& e) { return e.key == key; });
    if (!waiting && !m_hashing.contains(key))
        queueForHash({fi.absolutePath(), fi.fileName(), {}, key, static_cast<uint64>(fi.size()),
                      static_cast<time_t>(fi.lastModified().toSecsSinceEpoch()), volume});
    hashNextFile();
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
    // The workers that could take a file now. One with nothing of its own waiting is
    // marked drained and not searched for again until the queue grows: one long
    // volume must not cost a full walk of the queue per finished file.
    QSet<HashingThread*> free;
    for (HashingThread* worker : m_hashWorkers)
        if (!m_drainedWorkers.contains(worker))
            free.insert(worker);
    for (const Hashing& h : m_hashing)
        free.remove(h.worker);

    for (auto it = m_waitingForHash.begin(); it != m_waitingForHash.end() && !free.isEmpty(); ) {
        HashingThread* worker = workerFor(it->volume);
        if (!free.remove(worker)) {
            ++it;
            continue;
        }

        HashingThread::Job job;
        job.directory = it->directory;
        job.filename = it->filename;
        job.sharedDirectory = it->sharedDirectory;
        job.generation = m_generation;
        job.scannedSize = it->size;
        job.scannedMtime = it->mtime;
        m_hashing.insert(it->key, {worker, *it});
        it = m_waitingForHash.erase(it);
        worker->enqueue(std::move(job));
    }

    m_drainedWorkers.unite(free);   // walked the whole queue and found nothing for them
}

void SharedFileList::queueForHash(UnknownFileEntry entry)
{
    static_cast<void>(workerFor(entry.volume));   // exists before the queue is walked
    m_waitingForHash.push_back(std::move(entry));
    m_drainedWorkers.clear();
}

UnknownFileEntry SharedFileList::takeHashing(const QString& key)
{
    return m_hashing.take(key).entry;
}

QString SharedFileList::volumeKeyFor(const QString& directory)
{
    const QString dirKey = pathKey(directory);
    if (const auto it = m_volumeOfDir.constFind(dirKey); it != m_volumeOfDir.constEnd())
        return *it;
    QString volume = m_volumeKeyFn ? m_volumeKeyFn(directory)
                                   : QStorageInfo(directory).rootPath();
    if (volume.isEmpty())
        volume = QStringLiteral("/");   // not mounted or gone: any worker will do
    m_volumeOfDir.insert(dirKey, volume);
    return volume;
}

HashingThread* SharedFileList::workerFor(const QString& volume)
{
    if (const auto it = m_workerOfVolume.constFind(volume); it != m_workerOfVolume.constEnd())
        return *it;

    HashingThread* worker = nullptr;
    if (static_cast<int>(m_hashWorkers.size()) < kMaxHashWorkers) {
        worker = new HashingThread(this);
        connect(worker, &HashingThread::hashingFinished,
                this, &SharedFileList::onHashingFinished, Qt::QueuedConnection);
        connect(worker, &HashingThread::hashingFailed,
                this, &SharedFileList::onHashingFailed, Qt::QueuedConnection);
        connect(worker, &HashingThread::hashingDeferred,
                this, &SharedFileList::onHashingDeferred, Qt::QueuedConnection);
        connect(worker, &HashingThread::partFileRehashed,
                this, &SharedFileList::onPartFileRehashed, Qt::QueuedConnection);
        worker->start();
        m_hashWorkers.push_back(worker);
    } else {
        // More disks than workers: the extra ones share.
        worker = m_hashWorkers[qHash(volume) % m_hashWorkers.size()];
    }
    m_workerOfVolume.insert(volume, worker);
    return worker;
}

void SharedFileList::orderForHashing(std::vector<UnknownFileEntry>& entries, bool coldStart)
{
    // Path order within a volume: files of one directory lie together on disk.
    std::ranges::sort(entries, [](const UnknownFileEntry& a, const UnknownFileEntry& b) {
        return std::tie(a.volume, a.key) < std::tie(b.volume, b.key);
    });
    if (!coldStart || entries.size() < 2)
        return;

    // Nothing hashed yet: the smallest files go first, so a new profile has
    // something to offer within seconds instead of after the first big file.
    std::vector<size_t> bySize(entries.size());
    std::iota(bySize.begin(), bySize.end(), size_t{0});
    std::ranges::stable_sort(bySize, {}, [&](size_t i) { return entries[i].size; });

    std::vector<bool> early(entries.size(), false);
    std::vector<UnknownFileEntry> ordered;
    ordered.reserve(entries.size());
    uint64 bytes = 0;
    for (size_t i : bySize) {
        if (static_cast<int>(ordered.size()) >= kColdStartFiles
            || bytes + entries[i].size > kColdStartBytes)
            break;
        bytes += entries[i].size;
        early[i] = true;
        ordered.push_back(entries[i]);
    }
    for (size_t i = 0; i < entries.size(); ++i)
        if (!early[i])
            ordered.push_back(std::move(entries[i]));
    entries = std::move(ordered);
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
        static_cast<void>(takeHashing(pathKey(file->filePath())));   // its worker is free
        // Reject stale completions from a previous generation
        if (generation != m_generation) {
            delete file;
            return;
        }
    }

    // Same content already shared from another path: keep the one we have and never
    // let the known list replace (and delete) an object that is still shared —
    // MFC FileHashingFinished, srchybrid/SharedFileList.cpp:735-741.
    if (const KnownFile* dup = getFileByID(file->fileHash())) {
        logInfo(QStringLiteral("Duplicate file not shared: %1 has the same content as %2")
                    .arg(file->filePath(), dup->filePath()));
        delete file;
        QMutexLocker hashLocker(&m_hashMutex);
        hashNextFile();
        return;
    }

    m_hashFailures.remove(pathKey(file->filePath()));

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

bool SharedFileList::enqueuePartFileRehash(PartFile* file)
{
    if (!file)
        return false;

    HashingThread::Job job;
    job.rehashToken = file->beginRehash();
    job.rehashFileHash = QByteArray(reinterpret_cast<const char*>(file->fileHash()), 16);
    job.rehashPartPath = file->partDataPath();
    job.rehashFileSize = static_cast<uint64>(file->fileSize());
    job.rehashPartHashes = file->fileIdentifier().getRawMD4HashSet();

    // Everything the worker needs is copied above, on this thread — it must not reach
    // back into the PartFile, which the download queue may delete meanwhile.
    const QString volume = volumeKeyFor(QFileInfo(job.rehashPartPath).absolutePath());
    QMutexLocker hashLocker(&m_hashMutex);
    workerFor(volume)->enqueue(std::move(job));
    return true;
}

void SharedFileList::onPartFileRehashed(const QByteArray& fileHash, const QByteArray& partOk,
                                        uint64 token)
{
    if (fileHash.size() != 16 || !theApp.downloadQueue)
        return;

    // Look the file up rather than trusting a pointer: it may have been cancelled
    // while the rehash ran, and the queue is the authority on what still exists.
    PartFile* file = theApp.downloadQueue->fileByID(
        reinterpret_cast<const uint8*>(fileHash.constData()));
    if (!file)
        return;

    // Same hash, different object: cancelled and re-added while the worker ran.
    if (token != file->rehashToken())
        return;

    file->applyRehashResult(partOk);
    emit partFileRehashApplied(fileHash, partOk);
}

void SharedFileList::onHashingFailed(const QString& directory, const QString& filename, uint64 generation)
{
    QMutexLocker hashLocker(&m_hashMutex);

    const QString path = directory + u'/' + filename;
    const QString key = pathKey(path);
    UnknownFileEntry tried = takeHashing(key);   // its worker is free

    // Reject stale completions from a previous generation
    if (generation != m_generation)
        return;

    // Often passing — a file still being copied, or held open by another program — so
    // it gets a few more tries before it is left alone until it changes.
    HashFailure& failure = m_hashFailures[key];
    if (failure.entry.key.isEmpty()) {
        // The entry as it was queued: a rebuilt one lost its shared directory.
        if (tried.key.isEmpty())
            tried = {directory, filename, {}, key};
        failure.entry = std::move(tried);
    }
    failure.entry.mtime = 0;   // a retry hashes whatever is there by then
    failure.queued = false;
    const QFileInfo fi(path);
    failure.size = static_cast<uint64>(fi.size());
    failure.mtime = static_cast<time_t>(fi.lastModified().toSecsSinceEpoch());
    if (!fi.isFile()) {
        m_hashFailures.remove(key);   // gone: nothing to retry
    } else if (static_cast<size_t>(failure.attempts) < m_hashRetrySecs.size()) {
        failure.retryAt = std::time(nullptr) + m_hashRetrySecs[static_cast<size_t>(failure.attempts)];
        ++failure.attempts;
        logDebug(QStringLiteral("Failed to hash file, will retry: %1").arg(path));
    } else {
        failure.retryAt = 0;
        failure.givenUp = true;
        logWarning(QStringLiteral("Failed to hash file: %1 (not tried again until it changes)")
                       .arg(path));
    }

    // Continue with next file
    hashNextFile();
}

void SharedFileList::onHashingDeferred(const QString& directory, const QString& filename,
                                       uint64 /*generation*/)
{
    // Not a failure: the file moved on between the scan and the read. Look at the
    // directory again once it has had time to settle.
    m_settleDirs.insert(directory, std::time(nullptr) + kSettleRecheckSecs);

    QMutexLocker hashLocker(&m_hashMutex);
    static_cast<void>(takeHashing(pathKey(directory + u'/' + filename)));
    hashNextFile();
}

void SharedFileList::setHashFailureStorePath(const QString& path)
{
    m_hashFailureStorePath = path;
    loadHashFailures();
}

void SharedFileList::loadHashFailures()
{
    for (const HashFailureRecord& rec : HashFailureFile::read(m_hashFailureStorePath)) {
        const QString key = pathKey(rec.directory + u'/' + rec.filename);
        HashFailure& failure = m_hashFailures[key];
        failure.entry = {rec.directory, rec.filename, {}, key};
        failure.size = rec.size;
        failure.mtime = rec.mtime;
        failure.attempts = static_cast<int>(m_hashRetrySecs.size());
        failure.givenUp = true;
    }
    m_hashFailuresSaved = 0;
    m_hashFailuresCheckedAt = 0;
}

void SharedFileList::saveHashFailures()
{
    if (m_hashFailureStorePath.isEmpty())
        return;

    // Only what we gave up on: a retry in progress starts over after a restart anyway.
    std::vector<HashFailureRecord> records;
    for (const HashFailure& failure : m_hashFailures)
        if (failure.givenUp)
            records.push_back({failure.entry.directory, failure.entry.filename,
                               failure.size, failure.mtime});
    std::ranges::sort(records, {}, [](const HashFailureRecord& r) {
        return std::tie(r.directory, r.filename);
    });

    size_t signature = qHash(records.size()) + 1;   // never 0, the "nothing saved" mark
    for (const HashFailureRecord& rec : records)
        signature = qHashMulti(signature, rec.directory, rec.filename, rec.size,
                               static_cast<qint64>(rec.mtime));
    if (signature == m_hashFailuresSaved)
        return;
    // Nothing on disk and nothing to say: no empty file.
    if (records.empty() && m_hashFailuresSaved == 0
        && !QFile::exists(m_hashFailureStorePath)) {
        m_hashFailuresSaved = signature;
        return;
    }
    if (HashFailureFile::write(m_hashFailureStorePath, records))
        m_hashFailuresSaved = signature;
}

// ---------------------------------------------------------------------------
// rescan — bring the share in line with the disk
// ---------------------------------------------------------------------------

QStringList SharedFileList::shareRoots() const
{
    QStringList roots;
    QSet<QString> seen;
    const auto add = [&](const QString& dir) {
        if (dir.isEmpty() || seen.contains(pathKey(dir)))
            return;
        seen.insert(pathKey(dir));
        roots.append(dir);
    };
    // Every incoming directory: the global one plus each category that has one
    // of its own (MFC scans the same set, srchybrid/SharedFileList.cpp:577-580).
    for (const QString& dir : thePrefs.allIncomingDirs())
        add(dir);
    for (const QString& dir : thePrefs.sharedDirs())
        add(dir);
    return roots;
}

void SharedFileList::listDirectory(const QString& dir, QHash<QString, DiskEntry>& out) const
{
    if (!QDir(dir).exists())
        return;

    QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (!fi.isFile() || !isShareableFile(fi.fileName(), static_cast<uint64>(fi.size())))
            continue;

        const QString filename = fi.fileName();

        // Skip .part and .part.met files
        if (filename.endsWith(QStringLiteral(".part"), Qt::CaseInsensitive)
            || filename.endsWith(QStringLiteral(".part.met"), Qt::CaseInsensitive))
            continue;

        // Usenet scratch. This directory walk does not recurse, so a Usenet temp
        // folder nested under a shared directory is already invisible; the suffix
        // covers the two cases that are not. One: a user who adds the Usenet temp
        // directory itself to sharedDirs. Two, and the reason it matters more —
        // a completion across volumes, where QFile::rename degrades to a copy and
        // the growing file is briefly visible *inside the incoming directory*,
        // which is always shared. The copy carries this suffix until the final
        // in-place rename, so a scan racing it finds nothing to publish.
        if (filename.endsWith(Preferences::kUsenetPartSuffix, Qt::CaseInsensitive)
            || filename.endsWith(Preferences::kCompletingSuffix, Qt::CaseInsensitive))
            continue;

        // The user unshared this one individually. This — not the m_unsharedFiles
        // hash set — is what makes an unshare survive a reload and a restart.
        const QString key = pathKey(fi.absoluteFilePath());
        if (m_singleExcludedFiles.contains(key) || out.contains(key))
            continue;

        out.insert(key, {fi.absolutePath(), filename, {}, static_cast<uint64>(fi.size()),
                         static_cast<time_t>(fi.lastModified().toSecsSinceEpoch())});
    }
}

void SharedFileList::rescan(const QStringList& onlyDirs)
{
    // The directories looked at as one: a file that left one of them and turned up in
    // another is a move, not a removal and an addition.
    QSet<QString> scope;
    for (const QString& dir : onlyDirs)
        if (!dir.isEmpty())
            scope.insert(pathKey(dir));
    bool full = scope.isEmpty();
    if (!full)
        widenScopeForMoves(scope);
    full = scope.isEmpty();
    const time_t now = std::time(nullptr);

    // 1. What the disk offers. Deliberately unlocked: this walks the share from disk.
    QHash<QString, DiskEntry> onDisk;
    for (const QString& dir : shareRoots())
        if (full || scope.contains(pathKey(dir)))
            listDirectory(dir, onDisk);

    // Files shared individually, outside any shared directory — otherwise a reload
    // would silently drop them (srchybrid/SharedFileList.cpp:586-587).
    for (const QString& filePath : m_singleSharedFiles) {
        const QFileInfo fi(filePath);
        const QString key = pathKey(fi.absoluteFilePath());
        if ((!full && !scope.contains(pathKey(fi.absolutePath()))) || onDisk.contains(key))
            continue;
        if (fi.isFile() && fi.size() > 0)
            onDisk.insert(key, {fi.absolutePath(), fi.fileName(), {}, static_cast<uint64>(fi.size()),
                                static_cast<time_t>(fi.lastModified().toSecsSinceEpoch())});
    }
    const QSet<QString> present(onDisk.keyBegin(), onDisk.keyEnd());

    // 2. Shared files the disk still backs stay exactly as they are; the others leave.
    // Part files are not found by a scan and are not its business.
    std::vector<KnownFile*> leaving;
    forEach([&](KnownFile* file) {
        if (!file || file->isPartFile())
            return;
        const QString key = pathKey(file->filePath());
        if (!full && !scope.contains(key.left(key.lastIndexOf(u'/'))))
            return;
        const auto it = onDisk.constFind(key);
        if (it != onDisk.constEnd() && it->size == static_cast<uint64>(file->fileSize())
            && it->mtime == file->utcFileDate()) {
            onDisk.erase(it);
            return;
        }
        leaving.push_back(file);
    });

    // 3. One file went and one came with the same size and date, and nothing else
    // fits: it was renamed. Keep the record and its hash.
    int relocated = 0;
    if (!leaving.empty() && !onDisk.isEmpty()) {
        using Stamp = std::pair<uint64, time_t>;
        std::map<Stamp, std::vector<KnownFile*>> gone;
        for (KnownFile* file : leaving)
            if (!present.contains(pathKey(file->filePath())))
                gone[{static_cast<uint64>(file->fileSize()), file->utcFileDate()}].push_back(file);
        std::map<Stamp, std::vector<QString>> came;
        for (auto it = onDisk.constBegin(); it != onDisk.constEnd(); ++it) {
            const auto goneIt = gone.find(Stamp{it->size, it->mtime});
            if (goneIt == gone.end())
                continue;
            // A known file of that name, size and date joins by itself below — unless
            // it is the very file that went: same name in another directory is a move.
            KnownFile* known = m_knownFiles
                ? m_knownFiles->findKnownFile(it->filename, it->mtime, it->size) : nullptr;
            if (known && std::ranges::find(goneIt->second, known) == goneIt->second.end())
                continue;
            came[{it->size, it->mtime}].push_back(it.key());
        }
        for (const auto& [stamp, keys] : came) {
            const auto& files = gone[stamp];
            if (keys.size() != 1 || files.size() != 1)
                continue;
            relocateFile(files.front(), onDisk.value(keys.front()));
            std::erase(leaving, files.front());
            onDisk.remove(keys.front());
            ++relocated;
        }
    }

    for (KnownFile* file : leaving)
        removeFile(file);

    // 4. What is new: known files join at once, the rest go to hashing.
    const bool coldStart = full && m_knownFiles && m_knownFiles->count() == 0;
    int joined = 0;
    std::vector<UnknownFileEntry> toHash;
    QSet<QString> wantedUnknown;
    for (auto it = onDisk.constBegin(); it != onDisk.constEnd(); ++it) {
        const QString& key = it.key();
        const DiskEntry& entry = *it;
        const QString filePath = entry.directory + u'/' + entry.filename;

        if (KnownFile* existing = m_knownFiles
                ? m_knownFiles->findKnownFile(entry.filename, entry.mtime, entry.size) : nullptr) {
            // Another path with the same name, size and date as a shared file is not
            // allowed to pull that file's record over to itself.
            if (isFilePtrInList(existing))
                continue;
            existing->setPath(entry.directory);
            existing->setFilePath(filePath);
            if (!entry.sharedDirectory.isEmpty())
                existing->setSharedDirectory(entry.sharedDirectory);
            // Through the front door: writing m_map here would skip the duplicate
            // check, the collection detection and addKeywords() — which is why a
            // re-scanned known file used to be invisible to Kad keyword publishing.
            // onlyAdd, because a bulk scan must not schedule one republish per file.
            if (safeAddKFile(existing, /*onlyAdd=*/true))
                ++joined;
            continue;
        }

        wantedUnknown.insert(key);

        if (const auto failure = m_hashFailures.find(key); failure != m_hashFailures.end()) {
            const bool same = failure->size == entry.size && failure->mtime == entry.mtime;
            if (!failure->givenUp || same)
                continue;   // a retry is on its way, or nothing changed since we gave up
            m_hashFailures.erase(failure);
        }

        // Written a moment ago, probably still being written: look again later
        // rather than hash a file that is not all there.
        if (m_settleSecs > 0 && entry.mtime <= now && now - entry.mtime < m_settleSecs) {
            m_settleDirs.insert(entry.directory, now + kSettleRecheckSecs);
            continue;
        }

        toHash.push_back({entry.directory, entry.filename, entry.sharedDirectory, key,
                          entry.size, entry.mtime, volumeKeyFor(entry.directory)});
    }
    orderForHashing(toHash, coldStart);

    if (joined > 0)
        m_republishED2K = true;   // once for the batch

    // 5. The hash queue: what is no longer wanted leaves it, what is new joins it.
    // The file on the worker is left to finish.
    {
        QMutexLocker hashLocker(&m_hashMutex);
        std::erase_if(m_waitingForHash, [&](const UnknownFileEntry& e) {
            const bool inScope = full || scope.contains(pathKey(e.directory));
            return inScope && !wantedUnknown.contains(e.key);
        });
        QSet<QString> waiting;
        for (const UnknownFileEntry& e : m_waitingForHash)
            waiting.insert(e.key);
        for (UnknownFileEntry& e : toHash)
            if (!waiting.contains(e.key) && !m_hashing.contains(e.key))
                queueForHash(std::move(e));
        hashNextFile();
    }

    if (full) {
        // Part files are shared files too (MFC re-adds them from inside
        // FindSharedFiles, srchybrid/SharedFileList.cpp:551); a no-op for those present.
        if (theApp.downloadQueue)
            theApp.downloadQueue->addPartFilesToShare();
        m_keywords.purgeUnreferencedKeywords();
        m_hashFailures.removeIf([&](const auto& it) { return !present.contains(it.key()); });
    }

    if (joined > 0 || relocated > 0 || !leaving.empty() || !toHash.empty())
        logInfo(QStringLiteral("Shared files%1: %2 joined, %3 left, %4 renamed, %5 to hash")
                    .arg(full ? QString() : QStringLiteral(" in ") + onlyDirs.join(QStringLiteral(", ")))
                    .arg(joined).arg(leaving.size()).arg(relocated).arg(toHash.size()));
}

void SharedFileList::widenScopeForMoves(QSet<QString>& scope) const
{
    // The two ends of a move are reported apart, sometimes seconds apart. A shared
    // file that is gone from a directory in scope is looked for under its own name
    // in the other roots; a root that has it joins the scope, and the diff then sees
    // one file moving instead of one leaving now and one arriving later.
    struct Gone {
        QString path;
        QString filename;
        uint64 size;
        time_t mtime;
    };
    std::vector<Gone> candidates;
    forEach([&](KnownFile* file) {
        if (!file || file->isPartFile())
            return;
        const QString key = pathKey(file->filePath());
        if (scope.contains(key.left(key.lastIndexOf(u'/'))))
            candidates.push_back({file->filePath(), file->fileName(),
                                  static_cast<uint64>(file->fileSize()), file->utcFileDate()});
    });
    if (candidates.empty())
        return;

    QStringList others;
    for (const QString& root : shareRoots())
        if (!scope.contains(pathKey(root)))
            others.append(root);
    if (others.isEmpty())
        return;

    // Unlocked from here: this stats the disk.
    for (const Gone& gone : candidates) {
        if (QFileInfo::exists(gone.path))
            continue;   // still where it was
        for (const QString& root : std::as_const(others)) {
            if (scope.contains(pathKey(root)))
                continue;
            const QFileInfo fi(root + u'/' + gone.filename);
            if (fi.isFile() && static_cast<uint64>(fi.size()) == gone.size
                && static_cast<time_t>(fi.lastModified().toSecsSinceEpoch()) == gone.mtime)
                scope.insert(pathKey(root));
        }
    }
}

void SharedFileList::relocateFile(KnownFile* file, const DiskEntry& entry)
{
    removeKeywords(file);
    if (file->fileName() != entry.filename)
        file->setFileName(entry.filename);
    file->setPath(entry.directory);
    file->setFilePath(entry.directory + u'/' + entry.filename);
    file->setSharedDirectory(entry.sharedDirectory);
    addKeywords(file);
    refreshDirectoryOf(file);
    emit fileRelocated(file);
}

void SharedFileList::stepDeferredScans()
{
    const time_t now = std::time(nullptr);

    if (!m_hashFailures.isEmpty()) {
        QMutexLocker hashLocker(&m_hashMutex);
        for (HashFailure& failure : m_hashFailures) {
            if (failure.givenUp || failure.queued || failure.retryAt == 0 || failure.retryAt > now)
                continue;
            failure.queued = true;
            failure.retryAt = 0;
            UnknownFileEntry entry = failure.entry;
            if (entry.volume.isEmpty())
                entry.volume = volumeKeyFor(entry.directory);
            queueForHash(std::move(entry));
        }
        hashNextFile();
    }

    QStringList due;
    for (auto it = m_settleDirs.begin(); it != m_settleDirs.end(); ) {
        if (it.value() <= now) {
            due.append(it.key());
            it = m_settleDirs.erase(it);
        } else {
            ++it;
        }
    }
    for (const QString& dir : due)
        rescanDirectory(dir);
}

void SharedFileList::noteFileChanged(const uint8* fileHash)
{
    emit fileChanged(QByteArray(reinterpret_cast<const char*>(fileHash), 16));
}

void SharedFileList::forEachFile(const std::function<void(KnownFile*)>& callback) const
{
    forEach(callback);
}

int SharedFileList::getHashingCount() const
{
    QMutexLocker hashLocker(&m_hashMutex);
    return static_cast<int>(m_waitingForHash.size() + static_cast<size_t>(m_hashing.size()));
}

// ---------------------------------------------------------------------------
// nextDueFile — round-robin pick for Kad publishing
// ---------------------------------------------------------------------------

KnownFile* SharedFileList::nextDueFile(uint32& cursor, const std::function<bool(KnownFile*)>& due)
{
    // due() marks the file as published when it says yes, so it may only be asked
    // until the first yes. Two walks, each file asked at most once: from the cursor
    // to the end, then the part before it.
    QMutexLocker locker(&m_mutex);
    if (cursor >= m_map.size())
        cursor = 0;
    for (const bool wrapped : {false, true}) {
        uint32 idx = 0;
        for (auto& [key, file] : m_map) {
            const bool inRange = wrapped ? idx < cursor : idx >= cursor;
            if (wrapped && !inRange)
                break;
            if (inRange && file && due(file)) {
                cursor = idx + 1;
                return file;
            }
            ++idx;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// rebuildMetaData — re-read media tags, a slice per tick
// ---------------------------------------------------------------------------

int SharedFileList::rebuildMetaData()
{
    m_metaRebuildQueue.clear();
    forEach([this](KnownFile* file) {
        if (file && !file->isPartFile())
            m_metaRebuildQueue.emplace_back(file->fileHash());
    });
    m_metaRebuildTotal = static_cast<int>(m_metaRebuildQueue.size());
    if (m_metaRebuildTotal > 0)
        logInfo(QStringLiteral("Rebuilding meta data of %1 shared files").arg(m_metaRebuildTotal));
    return m_metaRebuildTotal;
}

void SharedFileList::stepMetaDataRebuild()
{
    if (m_metaRebuildQueue.empty())
        return;

    // Each file costs a header read; keep it to a small part of the 1 s tick.
    constexpr qint64 kBudgetMs = 50;
    QElapsedTimer budget;
    budget.start();
    while (!m_metaRebuildQueue.empty() && budget.elapsed() < kBudgetMs) {
        const MD4Key key = m_metaRebuildQueue.back();
        m_metaRebuildQueue.pop_back();
        // Looked up again: the file may have left the share since it was queued.
        if (KnownFile* file = getFileByID(key.data.data()); file && !file->isPartFile())
            file->updateMetaDataTags();
    }

    if (m_metaRebuildQueue.empty()) {
        logInfo(QStringLiteral("Meta data of %1 shared files rebuilt").arg(m_metaRebuildTotal));
        m_metaRebuildTotal = 0;
        if (m_knownFiles)
            m_knownFiles->save();
    }
}

// ---------------------------------------------------------------------------
// warmContainerChecks — settle fake-file verdicts a slice at a time
// ---------------------------------------------------------------------------

void SharedFileList::warmContainerChecks()
{
    if (m_containerSweepIdle)
        return;

    // 15 ms of a 1 s tick. A verdict costs one 12-byte read and only for the ~20
    // media extensions expectedContainer() promises anything about, so a share of a
    // few thousand settles in the first tick or two and a huge one fills in visibly.
    constexpr qint64 kBudgetMs = 15;
    constexpr size_t kSliceFiles = 512;

    // Collect under the map lock, read outside it: containerCheck() opens files and
    // forEach() holds the lock for the whole callback. Keeping raw pointers across
    // the release is safe because files join and leave the share on this same thread.
    std::vector<KnownFile*> pending;
    pending.reserve(kSliceFiles);
    size_t unresolved = 0;
    forEach([&](KnownFile* file) {
        if (!file || file->containerCheckResolved())
            return;
        if (unresolved++ < m_containerSweepSkip)
            return;
        if (pending.size() < kSliceFiles)
            pending.push_back(file);
    });

    if (pending.empty()) {
        // Nothing left, or we stepped past the end of the ones that refuse to answer.
        m_containerSweepIdle = (m_containerSweepSkip == 0);
        m_containerSweepSkip = 0;
        return;
    }

    QElapsedTimer budget;
    budget.start();
    size_t tried = 0;
    size_t stillUnresolved = 0;
    for (auto* file : pending) {
        file->containerCheck();   // resolves and memoises, or the bytes are not there yet
        ++tried;
        if (!file->containerCheckResolved())
            ++stillUnresolved;
        else
            noteFileChanged(file->fileHash());   // the row carries the verdict
        if (budget.elapsed() >= kBudgetMs)
            break;
    }

    // Step over the ones that could not answer so the next slice reaches new files.
    // A part file gets its turn again on the next wrap, by which time the first part
    // may have landed.
    m_containerSweepSkip = (tried == stillUnresolved) ? m_containerSweepSkip + tried : 0;
}

// ---------------------------------------------------------------------------
// offeredTags (private)
// ---------------------------------------------------------------------------

std::vector<Tag> SharedFileList::offeredTags(KnownFile& file, const Server* srv)
{
    std::vector<Tag> tags;
    tags.emplace_back(FT_FILENAME, file.fileName());

    const auto sz = static_cast<uint64>(file.fileSize());
    tags.emplace_back(FT_FILESIZE, static_cast<uint32>(sz & 0xFFFFFFFF));
    if (file.isLargeFile())
        tags.emplace_back(FT_FILESIZE_HI, static_cast<uint32>(sz >> 32));

    // Archives and CD images are published as "Pro"; servers that take an integer
    // type get one where there is one. MFC SharedFileList.cpp:967-985.
    const ED2KFileType typeId = getED2KFileTypeID(file.fileName());
    const ED2KFileType searchId = ed2kFileTypeSearchID(typeId);
    if (srv && (srv->tcpFlags() & SrvTcpFlag::TypeTagInteger) && searchId != ED2KFileType::Any) {
        tags.emplace_back(FT_FILETYPE, static_cast<uint32>(searchId));
    } else if (const QString term = ed2kFileTypeSearchTerm(typeId); !term.isEmpty()) {
        tags.emplace_back(FT_FILETYPE, term);
    }

    if (file.getFileRating() > 0)
        tags.emplace_back(FT_FILERATING, file.getFileRating());

    // Media tags, so the server can match the length / bitrate / codec constraints of
    // a search. Artist, album and title go to clients only. MFC SharedFileList.cpp:997-1056.
    if (file.metaDataVer() == 0)
        return tags;

    const bool newTags = srv && srv->supportsNewTags();
    static constexpr struct { uint8 id; const char* name; } kMediaTags[] = {
        {FT_MEDIA_LENGTH, FT_ED2K_MEDIA_LENGTH},
        {FT_MEDIA_BITRATE, FT_ED2K_MEDIA_BITRATE},
        {FT_MEDIA_CODEC, FT_ED2K_MEDIA_CODEC},
    };
    for (const auto& [id, name] : kMediaTags) {
        const Tag* tag = file.getTag(id);
        if (!tag)
            continue;
        if (tag->isStr() && !tag->strValue().isEmpty()) {
            if (newTags)
                tags.emplace_back(id, tag->strValue());
            else
                tags.emplace_back(QByteArray(name), tag->strValue());
        } else if (tag->isInt() && tag->intValue() != 0) {
            const uint32 value = tag->intValue();
            if (id == FT_MEDIA_LENGTH && !(srv && srv->supportsZlib())) {
                // Servers that old take the length as "h:mm:ss" text only.
                const QString text = value >= 3600
                    ? QStringLiteral("%1:%2:%3").arg(value / 3600)
                          .arg((value / 60) % 60, 2, 10, QChar(u'0')).arg(value % 60, 2, 10, QChar(u'0'))
                    : QStringLiteral("%1:%2").arg(value / 60).arg(value % 60, 2, 10, QChar(u'0'));
                tags.emplace_back(QByteArray(name), text);
            } else if (newTags) {
                tags.emplace_back(id, value);
            } else {
                tags.emplace_back(QByteArray(name), value);
            }
        }
    }
    return tags;
}

bool SharedFileList::canPublishToKad()
{
    if (!theApp.isFirewalled())
        return true;
    if (theApp.clientList && theApp.clientList->buddyStatus() == BuddyStatus::Connected)
        return true;
    return !kad::UDPFirewallTester::isFirewalledUDP(true) && kad::UDPFirewallTester::isVerified();
}

// One probe per KADEMLIAPUBLISHTIME, as MFC SharedFileList.cpp:1314-1329.
void SharedFileList::publishDueSource(time_t tProbe)
{
    auto* kad = kad::Kademlia::instance();
    if (!kad || kad->getTotalStoreSrc() >= KADEMLIATOTALSTORESRC
        || tProbe < m_lastPublishKadSrc || tProbe < m_srcProbeRestUntil)
        return;

    m_lastPublishKadSrc = tProbe + KADEMLIAPUBLISHTIME;
    if (KnownFile* file = nextDueFile(m_currFileSrc, [](KnownFile* f) { return f->publishSrc(); })) {
        kad::UInt128 target;
        target.setValueBE(file->fileHash());
        auto* search = kad::SearchManager::prepareLookup(
                kad::SearchType::StoreFile, true, target);
        if (!search)
            file->setLastPublishTimeKadSrc(0, 0);
        else
            search->setGUIName(file->fileName());
    } else {
        m_srcProbeRestUntil = tProbe + kPublishProbeRestSecs;
    }
}

// MFC SharedFileList.cpp:1331-1346.
void SharedFileList::publishDueNotes(time_t tProbe)
{
    auto* kad = kad::Kademlia::instance();
    if (!kad || kad->getTotalStoreNotes() >= KADEMLIATOTALSTORENOTES
        || tProbe < m_lastPublishKadNotes || tProbe < m_notesProbeRestUntil)
        return;

    m_lastPublishKadNotes = tProbe + KADEMLIAPUBLISHTIME;
    if (KnownFile* file = nextDueFile(m_currFileNotes, [](KnownFile* f) { return f->publishNotes(); })) {
        kad::UInt128 target;
        target.setValueBE(file->fileHash());
        auto* search = kad::SearchManager::prepareLookup(
                kad::SearchType::StoreNotes, true, target);
        if (!search)
            file->setLastPublishTimeKadNotes(0);
        else
            search->setGUIName(file->fileName());
    } else {
        m_notesProbeRestUntil = tProbe + kPublishProbeRestSecs;
    }
}

} // namespace eMule
