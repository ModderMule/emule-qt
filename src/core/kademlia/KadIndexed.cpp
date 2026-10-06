#include "pch.h"
/// @file KadIndexed.cpp
/// @brief Keyword/source/notes index implementation.

#include "kademlia/KadIndexed.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadResultPacketWriter.h"
#include "kademlia/KadUDPListener.h"
#include "utils/SafeFile.h"

#include "app/AppContext.h"

#include <QDir>
#include <QFile>
#include <QThread>


namespace eMule::kad {

namespace {
constexpr uint32 kCleanInterval = 60 * 30; // 30 minutes

// File versions stock writes (srchybrid/kademlia/kademlia/Indexed.cpp:146-235) and
// the highest ones its reader accepts.
constexpr uint32 kLoadFileVersion = 1;
constexpr uint32 kSourceFileVersion = 2;
constexpr uint32 kKeyFileVersion = 4;

constexpr auto kKeyFileName = "/key_index.dat";
constexpr auto kSourceFileName = "/src_index.dat";
constexpr auto kLoadFileName = "/load_index.dat";

// The address tags stay in the tag list and are mirrored into the entry's fields.
void mirrorAddressTag(Entry& entry, const Tag& tag)
{
    if (!tag.isInt())
        return;
    switch (tag.nameId()) {
    case FT_SOURCEIP:    entry.m_address = Address::fromHostOrder(static_cast<uint32>(tag.intValue())); break;
    case FT_SOURCEPORT:  entry.m_tcpPort = static_cast<uint16>(tag.intValue()); break;
    case FT_SOURCEUPORT: entry.m_udpPort = static_cast<uint16>(tag.intValue()); break;
    default: break;
    }
}
} // namespace

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

Indexed::Indexed(QObject* parent)
    : QObject(parent)
{
    m_nextClean = time(nullptr) + kCleanInterval;
}

Indexed::Indexed(const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_configDir(configDir)
{
    m_nextClean = time(nullptr) + kCleanInterval;

    m_dataLoaded.store(false, std::memory_order_release);
    m_loader = QThread::create([this] { loadFiles(); });
    m_loader->setObjectName(QStringLiteral("KadIndexLoad"));
    m_loader->start(QThread::LowPriority);
}

Indexed::~Indexed()
{
    if (m_loader) {
        // Still loading: what is in memory is partial, so the files stay as they are.
        const bool complete = isLoaded();
        m_abortLoading.store(true, std::memory_order_relaxed);
        m_loader->wait();
        delete m_loader;
        m_loader = nullptr;
        if (complete)
            writeFiles();
    }

    destroyIndex(m_keywords);
    destroyIndex(m_sources);
    destroyIndex(m_notes);

    // Clean up loads
    for (auto& [key, load] : m_loads)
        delete load;

    // MFC resets it with the index (Indexed.cpp:251). Only for the real one: an
    // in-memory index must not wipe the tracking of entries it never owned.
    if (!m_configDir.isEmpty())
        KeyEntry::resetGlobalTrackingMap();
}

bool Indexed::addKeyword(const UInt128& keyID, const UInt128& sourceID,
                          KeyEntry* entry, uint8& outLoad)
{
    if (!isLoaded())
        return false;
    QMutexLocker lock(&m_mutex);
    return addKeywordLocked(keyID, sourceID, entry, outLoad, false);
}

bool Indexed::addKeywordLocked(const UInt128& keyID, const UInt128& sourceID,
                               KeyEntry* entry, uint8& outLoad, bool fromFile)
{
    if (!entry)
        return false;

    // Global index ceiling. MFC caps the keyword index at KADEMLIAMAXENTRIES
    // (60000), not KADEMLIAMAXINDEX (50000, which is the *per-keyword* cap
    // below). Indexed.cpp:371.
    if (m_totalIndexKeyword > KADEMLIAMAXENTRIES) {
        outLoad = 100;
        return false;
    }

    if (!fromFile)
        entry->m_lifetime = time(nullptr) + KADEMLIAREPUBLISHTIMEK;

    // Reject malformed publishes rather than indexing something unservable.
    // MFC Indexed.cpp:361-362.
    if (entry->m_size == 0 || entry->getCommonFileName().isEmpty()
        || entry->getTagCount() == 0 || entry->m_lifetime < time(nullptr)) {
        return false;
    }

    // Get or create key hash entry
    HashKeyOwn hashKey(keyID.getData());
    KeyHash* keyHash = nullptr;
    auto it = m_keywords.find(hashKey);
    if (it != m_keywords.end()) {
        keyHash = it->second;

        // Per-keyword caps are keyed off *this keyword's* source count, not the
        // global counter — otherwise one hot keyword could evict the whole index
        // and its load %/back-pressure would be diluted across every other key.
        const size_t perKeyCount = keyHash->mapSource.size();
        // Hard per-keyword cap. MFC Indexed.cpp:393-398.
        if (perKeyCount > KADEMLIAMAXINDEX) {
            outLoad = 100;
            return false;
        }
        // Back-pressure before this keyword saturates. MFC Indexed.cpp:402.
        if (perKeyCount > KADEMLIAMAXINDEX - 5000) {
            outLoad = 100;
            return false;
        }
    } else {
        keyHash = new KeyHash();
        keyHash->keyID = keyID;
        m_keywords[hashKey] = keyHash;
    }

    // Get or create source entry
    HashKeyOwn srcKey(sourceID.getData());
    Source* source = nullptr;
    auto srcIt = keyHash->mapSource.find(srcKey);
    if (srcIt != keyHash->mapSource.end()) {
        source = srcIt->second;

        // Replace the stored entry that describes the same file size, folding
        // its publisher/AICH/filename history into the *new* entry. The merge
        // runs new-absorbs-old and the old entry is then destroyed: doing it the
        // other way round (as before) left the incoming entry unowned and
        // unreferenced — one leaked KeyEntry per refresh publish — and never
        // refreshed the stored tags, size or lifetime.
        // MFC Indexed.cpp:410-426.
        KeyEntry* oldEntry = nullptr;
        for (auto eIt = source->entryList.begin(); eIt != source->entryList.end(); ++eIt) {
            if ((*eIt)->m_size != entry->m_size || !(*eIt)->isKeyEntry())
                continue;
            oldEntry = static_cast<KeyEntry*>(*eIt);
            source->entryList.erase(eIt);
            break;
        }

        entry->mergeIPsAndFilenames(oldEntry); // nullptr is fine and still needed
        if (oldEntry == nullptr)
            ++m_totalIndexKeyword; // a new size for a keyword we already had
        delete oldEntry;

        source->entryList.push_back(entry);
        // Publishers throttle republishing of *this specific keyword* off this
        // number, so it must be the per-keyword load, not the global index load.
        // MFC Indexed.cpp:431,440.
        outLoad = static_cast<uint8>(
            (keyHash->mapSource.size() * 100) / KADEMLIAMAXINDEX);
        return true;
    }

    source = new Source();
    source->sourceID = sourceID;
    keyHash->mapSource[srcKey] = source;

    // First publish for this source — still needs the merge call so the
    // publisher tracking list gets initialised and this publisher recorded.
    entry->mergeIPsAndFilenames(nullptr);
    source->entryList.push_back(entry);
    ++m_totalIndexKeyword;

    outLoad = static_cast<uint8>(
        (keyHash->mapSource.size() * 100) / KADEMLIAMAXINDEX);
    return true;
}

const Indexed::SourcePolicy Indexed::kSourcePolicy{
    // MFC Indexed.cpp:485-489 — one IP:port pair owns one slot, no matter how
    // many sourceIDs it invents.
    [](const Entry& stored, const Entry& incoming) {
        return stored.m_address == incoming.m_address
               && (stored.m_tcpPort == incoming.m_tcpPort
                   || stored.m_udpPort == incoming.m_udpPort);
    },
    // MFC Indexed.cpp:452-458.
    [](const Entry& e) {
        return !e.m_address.isNull() && e.m_tcpPort != 0 && e.m_udpPort != 0
               && e.getTagCount() != 0 && e.m_lifetime >= time(nullptr);
    },
};

const Indexed::SourcePolicy Indexed::kNotePolicy{
    // MFC Indexed.cpp:551-556 — notes collapse per IP as well as per sourceID,
    // so one host cannot post 150 comments on the same file.
    [](const Entry& stored, const Entry& incoming) {
        return stored.m_address == incoming.m_address
               || stored.m_sourceID == incoming.m_sourceID;
    },
    // MFC Indexed.cpp:524-526 — notes carry no ports or lifetime requirement.
    [](const Entry& e) { return !e.m_address.isNull() && e.getTagCount() != 0; },
};

bool Indexed::addSources(const UInt128& keyID, const UInt128& sourceID,
                          Entry* entry, uint8& outLoad)
{
    if (!isLoaded())
        return false;
    QMutexLocker lock(&m_mutex);
    return addSourceEntry(m_sources, m_totalIndexSource, KADEMLIAMAXSOURCEPERFILE,
                          KADEMLIAREPUBLISHTIMES, kSourcePolicy,
                          keyID, sourceID, entry, outLoad);
}

bool Indexed::addNotes(const UInt128& keyID, const UInt128& sourceID,
                        Entry* entry, uint8& outLoad)
{
    if (!isLoaded())
        return false;
    QMutexLocker lock(&m_mutex);
    return addSourceEntry(m_notes, m_totalIndexNotes, KADEMLIAMAXNOTESPERFILE,
                          KADEMLIAREPUBLISHTIMEN, kNotePolicy,
                          keyID, sourceID, entry, outLoad);
}

bool Indexed::addLoad(const UInt128& keyID, time_t loadTime)
{
    if (!isLoaded())
        return false;
    QMutexLocker lock(&m_mutex);
    return addLoadLocked(keyID, loadTime);
}

// MFC AddLoad (Indexed.cpp:583-602): `loadTime` is when the overload mark expires.
bool Indexed::addLoadLocked(const UInt128& keyID, time_t loadTime)
{
    // Needed when the client restarts: a mark from the file may be long over.
    if (time(nullptr) > loadTime)
        return false;

    HashKeyOwn hashKey(keyID.getData());
    if (m_loads.contains(hashKey))
        return false;

    auto* load = new Load();
    load->keyID = keyID;
    load->time = loadTime;
    m_loads[hashKey] = load;
    ++m_totalIndexLoad;
    return true;
}

uint32 Indexed::getFileKeyCount() const
{
    // The loader is filling the map; MFC answers 0 until it is done.
    if (!isLoaded())
        return 0;
    return static_cast<uint32>(m_keywords.size());
}

void Indexed::sendValidKeywordResult(const UInt128& keyID, const SearchTerm* searchTerms,
                                      uint32 ip, uint16 port, bool /*oldClient*/,
                                      uint16 startPosition, const KadUDPKey& senderKey)
{
    if (!isLoaded())
        return;
    QMutexLocker lock(&m_mutex);

    auto* udpListener = Kademlia::getInstanceUDPListener();
    if (!udpListener)
        return;

    HashKeyOwn hashKey(keyID.getData());
    auto it = m_keywords.find(hashKey);
    if (it == m_keywords.end()) {
        cleanLocked();   // on a miss too (MFC Indexed.cpp:690)
        return;
    }

    KeyHash* keyHash = it->second;

    ResultPacketSender sender(Kademlia::getInstancePrefs()->kadId(), keyID,
        [&](SafeMemFile& pkt) {
            udpListener->sendPacket(pkt, KADEMLIA2_SEARCH_RES, ip, port, senderKey, nullptr);
        });

    constexpr int kMaxResults = 300;
    int count = -static_cast<int>(startPosition); // negative = skip entries for pagination

    // Two-pass loop: first send only trusted entries (trust >= 1.0), then untrusted.
    // This ensures the 300 result cap isn't filled with spam (MFC lines 634-676).
    for (bool onlyTrusted = true; count < kMaxResults; onlyTrusted = false) {
        for (auto& [srcKey, source] : keyHash->mapSource) {
            if (count >= kMaxResults)
                break;
            for (auto* entry : source->entryList) {
                if (count >= kMaxResults)
                    break;
                if (!entry->isKeyEntry())
                    continue;
                auto* keyEntry = static_cast<KeyEntry*>(entry);
                // XOR filter: in pass 1 skip untrusted, in pass 2 skip trusted
                if (onlyTrusted == (keyEntry->getTrustValue() < 1.0f))
                    continue;
                if (searchTerms && !keyEntry->startSearchTermsMatch(*searchTerms))
                    continue;
                if (count < 0) {
                    ++count;
                    continue;
                }
                ++count;

                SafeMemFile tmpBuf;
                io::writeUInt128(tmpBuf, source->sourceID);
                keyEntry->writeTagListWithPublishInfo(tmpBuf);
                sender.addResult(tmpBuf);
            }
        }
        if (!onlyTrusted)
            break;
    }

    sender.flush();

    // Expire stale index state opportunistically after a serve, as MFC does at
    // the end of SendValidKeywordResult (Indexed.cpp:690). Rate-limited inside;
    // runs under the lock we already hold (cleanLocked, not clean, to avoid
    // re-locking the non-recursive m_mutex).
    cleanLocked();
}

void Indexed::sendValidSourceResult(const UInt128& keyID, uint32 ip, uint16 port,
                                     uint16 startPosition, uint64 fileSize,
                                     const KadUDPKey& senderKey)
{
    if (!isLoaded())
        return;
    QMutexLocker lock(&m_mutex);

    auto* udpListener = Kademlia::getInstanceUDPListener();
    if (!udpListener)
        return;

    HashKeyOwn hashKey(keyID.getData());
    auto it = m_sources.find(hashKey);
    if (it == m_sources.end()) {
        cleanLocked();   // on a miss too (MFC Indexed.cpp:766)
        return;
    }

    SrcHash* srcHash = it->second;

    ResultPacketSender sender(Kademlia::getInstancePrefs()->kadId(), keyID,
        [&](SafeMemFile& pkt) {
            udpListener->sendPacket(pkt, KADEMLIA2_SEARCH_RES, ip, port, senderKey, nullptr);
        });

    int count = -static_cast<int>(startPosition);
    constexpr int kMaxResults = 300;

    for (auto* source : srcHash->sourceList) {
        if (count >= kMaxResults)
            break;
        if (source->entryList.empty())
            continue;
        auto* entry = source->entryList.front();
        // MFC fileSize filter: match exact size or accept if either is 0
        if (fileSize && entry->m_size && entry->m_size != fileSize)
            continue;
        if (count < 0) {
            ++count;
            continue;
        }
        ++count;

        SafeMemFile tmpBuf;
        io::writeUInt128(tmpBuf, source->sourceID);
        entry->writeTagList(tmpBuf);
        sender.addResult(tmpBuf);
    }

    sender.flush();

    // MFC runs Clean() at the end of SendValidSourceResult too (Indexed.cpp:766).
    cleanLocked();
}

void Indexed::sendValidNoteResult(const UInt128& keyID, uint32 ip, uint16 port,
                                   uint64 fileSize, const KadUDPKey& senderKey)
{
    if (!isLoaded())
        return;
    QMutexLocker lock(&m_mutex);

    auto* udpListener = Kademlia::getInstanceUDPListener();
    if (!udpListener)
        return;

    HashKeyOwn hashKey(keyID.getData());
    auto it = m_notes.find(hashKey);
    if (it == m_notes.end())
        return;

    SrcHash* srcHash = it->second;

    ResultPacketSender sender(Kademlia::getInstancePrefs()->kadId(), keyID,
        [&](SafeMemFile& pkt) {
            udpListener->sendPacket(pkt, KADEMLIA2_SEARCH_RES, ip, port, senderKey, nullptr);
        });

    constexpr uint16 kMaxResults = 150;
    uint16 totalCount = 0;
    const time_t now = time(nullptr);

    for (auto* source : srcHash->sourceList) {
        if (totalCount >= kMaxResults)
            break;
        for (auto* entry : source->entryList) {
            if (totalCount >= kMaxResults)
                break;
            if (entry->m_lifetime < now)
                continue;   // expired, waiting for the next clean
            // MFC fileSize filter
            if (fileSize && entry->m_size && entry->m_size != fileSize)
                continue;

            SafeMemFile tmpBuf;
            io::writeUInt128(tmpBuf, source->sourceID);
            entry->writeTagList(tmpBuf);
            sender.addResult(tmpBuf);
            ++totalCount;
        }
    }

    sender.flush();
}

bool Indexed::sendStoreRequest(const UInt128& keyID)
{
    // MFC SendStoreRequest (Indexed.cpp:841-859).
    if (!isLoaded())
        return true;   // not "overloaded" just because we are still loading
    QMutexLocker lock(&m_mutex);

    HashKeyOwn hashKey(keyID.getData());
    auto it = m_loads.find(hashKey);
    if (it != m_loads.end()) {
        if (it->second->time >= time(nullptr))
            return false;   // still marked overloaded
        delete it->second;
        m_loads.erase(it);
        --m_totalIndexLoad;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

bool Indexed::addSourceEntry(SrcHashMap& index, uint32& counter, uint32 perFileMax,
                             time_t lifetimeSecs, const SourcePolicy& policy,
                             const UInt128& keyID, const UInt128& sourceID,
                             Entry* entry, uint8& outLoad, bool fromFile)
{
    // Non-locking: addSources/addNotes already hold m_mutex.
    if (!entry)
        return false;

    // Reject malformed publishes outright rather than letting them occupy a
    // slot. MFC Indexed.cpp:452-458 / :524-526.
    if (!fromFile)
        entry->m_lifetime = time(nullptr) + lifetimeSecs;
    if (!policy.isPublishable(*entry))
        return false;

    // A full index first gets rid of what has expired (at most once a minute), and
    // even then only refuses what would grow it: a publisher replacing its own entry
    // is still served.
    if (counter >= KADEMLIAMAXENTRIES && !fromFile) {
        const time_t now = time(nullptr);
        if (now >= m_nextForcedClean) {
            m_nextForcedClean = now + 60;
            m_nextClean = 0;
            cleanLocked();
        }
    }
    const bool full = counter >= KADEMLIAMAXENTRIES;

    HashKeyOwn hashKey(keyID.getData());
    auto it = index.find(hashKey);
    if (it == index.end()) {
        if (full) {
            outLoad = 100;
            return false;
        }
        auto* srcHash = new SrcHash();
        srcHash->keyID = keyID;
        auto* source = new Source();
        source->sourceID = sourceID;
        source->entryList.push_back(entry);
        srcHash->sourceList.push_back(source);
        index[hashKey] = srcHash;
        ++counter;
        outLoad = 1;
        return true;
    }

    SrcHash* srcHash = it->second;
    // MFC counts Source buckets, not total entries — each publisher owns at most
    // one bucket, so this is "how many distinct publishers do we hold".
    const auto bucketCount = static_cast<uint32>(srcHash->sourceList.size());

    // Does an existing bucket already belong to this publisher? If so it just
    // replaces its own entry — this is what stops sourceID rotation from
    // consuming extra slots.
    for (auto* source : srcHash->sourceList) {
        if (source->entryList.empty())
            continue;
        if (!policy.isSamePublisher(*source->entryList.front(), *entry))
            continue;

        for (auto* e : source->entryList) {
            delete e;
            --counter;
        }
        source->entryList.clear();
        source->sourceID = sourceID;
        source->entryList.push_back(entry);
        ++counter;
        outLoad = static_cast<uint8>((bucketCount * 100) / perFileMax);
        return true;
    }

    if (bucketCount > perFileMax) {
        // Full: recycle the least recently inserted bucket rather than refusing.
        // Refusing (as this port used to) freezes the list forever, since
        // clean() is never called and nothing expires — the first N publishers
        // would own the file permanently.
        Source* oldest = srcHash->sourceList.back();
        srcHash->sourceList.pop_back();
        for (auto* e : oldest->entryList) {
            delete e;
            --counter;
        }
        oldest->entryList.clear();
        oldest->sourceID = sourceID;
        oldest->entryList.push_back(entry);
        srcHash->sourceList.push_front(oldest);
        ++counter;
        outLoad = 100;
        return true;
    }

    if (full) {
        outLoad = 100;
        return false;
    }

    auto* source = new Source();
    source->sourceID = sourceID;
    source->entryList.push_back(entry);
    srcHash->sourceList.push_front(source);
    ++counter;
    outLoad = static_cast<uint8>((bucketCount * 100) / perFileMax);
    return true;
}

// -- Persistence ----------------------------------------------------------------
// Formats are stock's, so an index written by either client loads in the other:
//   load_index.dat  <1><save time><count>{<key 16><expiry 4>}
//   src_index.dat   <2><expiry><keys>{<key><sources>{<source><entries>{<lifetime><tags>}}}
//   key_index.dat   <4><expiry><own KadID><keys>{<key><sources>{<source><entries>
//                                               {<lifetime><publish tracking><tags>}}}
// MFC Indexed.cpp:88-265 (write) and :903-1064 (read).

void Indexed::loadFiles()
{
    // Never under m_mutex as a whole: each insert takes it, so a waiting destructor
    // is not held up behind a large file.
    try {
        loadLoadFile(m_configDir + QLatin1StringView(kLoadFileName));
        loadKeyFile(m_configDir + QLatin1StringView(kKeyFileName));
        loadSourceFile(m_configDir + QLatin1StringView(kSourceFileName));
    } catch (const std::exception& ex) {
        logKad(QStringLiteral("Kad: index load stopped: %1").arg(QLatin1StringView(ex.what())));
    }

    if (!aborting()) {
        logKad(QStringLiteral("Kad: index loaded — %1 keyword, %2 source and %3 load entries")
                   .arg(m_totalIndexKeyword).arg(m_totalIndexSource).arg(m_totalIndexLoad));
        m_dataLoaded.store(true, std::memory_order_release);
    }
}

void Indexed::loadLoadFile(const QString& path)
{
    SafeFile sf;
    if (aborting() || !sf.open(path, QIODevice::ReadOnly))
        return;
    if (sf.readUInt32() >= 2)
        return;
    sf.readUInt32();   // save time
    for (uint32 n = sf.readUInt32(); n > 0 && !aborting(); --n) {
        const UInt128 keyID = io::readUInt128(sf);
        const auto expiry = static_cast<time_t>(sf.readUInt32());
        QMutexLocker lock(&m_mutex);
        addLoadLocked(keyID, expiry);
    }
}

void Indexed::loadKeyFile(const QString& path)
{
    SafeFile sf;
    if (aborting() || !sf.open(path, QIODevice::ReadOnly))
        return;

    const uint32 version = sf.readUInt32();
    if (version >= 5)
        return;
    if (static_cast<time_t>(sf.readUInt32()) <= time(nullptr))
        return;   // everything in it has expired
    // Keywords are stored with the nodes closest to them; under another KadID these
    // are no longer ours to answer for.
    auto* prefs = Kademlia::getInstancePrefs();
    if (!prefs || !(io::readUInt128(sf) == prefs->kadId()))
        return;

    for (uint32 keys = sf.readUInt32(); keys > 0 && !aborting(); --keys) {
        const UInt128 keyID = io::readUInt128(sf);
        for (uint32 sources = sf.readUInt32(); sources > 0 && !aborting(); --sources) {
            const UInt128 sourceID = io::readUInt128(sf);
            for (uint32 entries = sf.readUInt32(); entries > 0 && !aborting(); --entries) {
                auto entry = std::make_unique<KeyEntry>();
                entry->m_keyID = keyID;
                entry->m_sourceID = sourceID;
                entry->m_source = false;
                entry->m_lifetime = static_cast<time_t>(sf.readUInt32());
                if (version >= 3)
                    entry->readPublishTrackingDataFromFile(sf, version >= 4);

                for (auto& tag : io::readKadTagList(sf)) {
                    if (tag.nameId() == FT_FILENAME) {
                        if (tag.isStr() && entry->getCommonFileName().isEmpty())
                            entry->setFileName(tag.strValue());
                    } else if (tag.nameId() == FT_FILESIZE) {
                        entry->m_size = tag.isInt() ? tag.intValue()
                                      : tag.isInt64(false) ? tag.int64Value() : 0;
                    } else {
                        mirrorAddressTag(*entry, tag);
                        entry->addTag(std::move(tag));
                    }
                }

                uint8 load = 0;
                QMutexLocker lock(&m_mutex);
                if (addKeywordLocked(keyID, sourceID, entry.get(), load, true))
                    entry.release();   // the index owns it now
            }
        }
    }
}

void Indexed::loadSourceFile(const QString& path)
{
    SafeFile sf;
    if (aborting() || !sf.open(path, QIODevice::ReadOnly))
        return;

    if (sf.readUInt32() >= 3)
        return;
    if (static_cast<time_t>(sf.readUInt32()) <= time(nullptr))
        return;

    for (uint32 keys = sf.readUInt32(); keys > 0 && !aborting(); --keys) {
        const UInt128 keyID = io::readUInt128(sf);
        for (uint32 sources = sf.readUInt32(); sources > 0 && !aborting(); --sources) {
            const UInt128 sourceID = io::readUInt128(sf);
            for (uint32 entries = sf.readUInt32(); entries > 0 && !aborting(); --entries) {
                auto entry = std::make_unique<Entry>();
                entry->m_keyID = keyID;
                entry->m_sourceID = sourceID;
                entry->m_source = true;
                entry->m_lifetime = static_cast<time_t>(sf.readUInt32());
                for (auto& tag : io::readKadTagList(sf)) {
                    mirrorAddressTag(*entry, tag);
                    entry->addTag(std::move(tag));
                }

                uint8 load = 0;
                QMutexLocker lock(&m_mutex);
                if (addSourceEntry(m_sources, m_totalIndexSource, KADEMLIAMAXSOURCEPERFILE,
                                   KADEMLIAREPUBLISHTIMES, kSourcePolicy, keyID, sourceID,
                                   entry.get(), load, true))
                    entry.release();
            }
        }
    }
}

void Indexed::writeFiles()
{
    if (m_configDir.isEmpty())
        return;
    QDir().mkpath(m_configDir);
    QMutexLocker lock(&m_mutex);
    const auto now = static_cast<uint32>(time(nullptr));
    const bool sync = theApp.commitFilesNow();

    auto save = [&](const char* name, auto&& body) {
        const QString path = m_configDir + QLatin1StringView(name);
        const QString tmp = path + QStringLiteral(".tmp");
        try {
            QFile::remove(tmp);
            SafeFile sf(tmp, QIODevice::WriteOnly);
            body(sf);
            commitAndReplace(sf, tmp, path, sync);
        } catch (const std::exception& ex) {
            logKad(QStringLiteral("Kad: failed to write %1: %2")
                       .arg(path, QLatin1StringView(ex.what())));
            QFile::remove(tmp);
        }
    };

    save(kLoadFileName, [&](SafeFile& sf) {
        sf.writeUInt32(kLoadFileVersion);
        sf.writeUInt32(now);
        sf.writeUInt32(static_cast<uint32>(m_loads.size()));
        for (const auto& [key, load] : m_loads) {
            io::writeUInt128(sf, load->keyID);
            sf.writeUInt32(static_cast<uint32>(load->time));
        }
    });

    save(kSourceFileName, [&](SafeFile& sf) {
        sf.writeUInt32(kSourceFileVersion);
        sf.writeUInt32(now + KADEMLIAREPUBLISHTIMES);
        sf.writeUInt32(static_cast<uint32>(m_sources.size()));
        for (const auto& [key, srcHash] : m_sources) {
            io::writeUInt128(sf, srcHash->keyID);
            sf.writeUInt32(static_cast<uint32>(srcHash->sourceList.size()));
            for (const Source* source : srcHash->sourceList) {
                io::writeUInt128(sf, source->sourceID);
                sf.writeUInt32(static_cast<uint32>(source->entryList.size()));
                for (const Entry* entry : source->entryList) {
                    sf.writeUInt32(static_cast<uint32>(entry->m_lifetime));
                    entry->writeTagList(sf);
                }
            }
        }
    });

    save(kKeyFileName, [&](SafeFile& sf) {
        auto* prefs = Kademlia::getInstancePrefs();
        if (!prefs)
            throw FileException("no Kad ID to stamp the keyword index with");
        sf.writeUInt32(kKeyFileVersion);
        sf.writeUInt32(now + KADEMLIAREPUBLISHTIMEK);
        io::writeUInt128(sf, prefs->kadId());
        sf.writeUInt32(static_cast<uint32>(m_keywords.size()));
        for (const auto& [key, keyHash] : m_keywords) {
            io::writeUInt128(sf, keyHash->keyID);
            sf.writeUInt32(static_cast<uint32>(keyHash->mapSource.size()));
            for (const auto& [srcKey, source] : keyHash->mapSource) {
                io::writeUInt128(sf, source->sourceID);
                // Counted first: only key entries carry the tracking block.
                const auto keyEntries = static_cast<uint32>(std::ranges::count_if(
                    source->entryList, [](const Entry* e) { return e->isKeyEntry(); }));
                sf.writeUInt32(keyEntries);
                for (Entry* entry : source->entryList) {
                    if (!entry->isKeyEntry())
                        continue;
                    auto* keyEntry = static_cast<KeyEntry*>(entry);
                    sf.writeUInt32(static_cast<uint32>(keyEntry->m_lifetime));
                    keyEntry->writePublishTrackingDataToFile(sf);
                    keyEntry->writeTagList(sf);
                }
            }
        }
    });
}

void Indexed::clean(bool force)
{
    if (!isLoaded())
        return;
    QMutexLocker lock(&m_mutex);
    if (force)
        m_nextClean = 0;
    cleanLocked();
}

void Indexed::cleanLocked()
{
    time_t now = time(nullptr);
    if (now < m_nextClean)
        return;
    m_nextClean = now + kCleanInterval;

    // Keywords: also prune each surviving entry's stale publisher IPs so the /24
    // trust map shrinks without waiting for the whole entry to expire.
    // MFC Indexed.cpp:303.
    cleanIndex(m_keywords, now, m_totalIndexKeyword, [](Entry* e) {
        if (e->isKeyEntry())
            static_cast<KeyEntry*>(e)->cleanUpTrackedPublishers();
    });
    cleanIndex(m_sources, now, m_totalIndexSource);
    // Notes too: they carry a 24 h lifetime nothing else enforces, and the index is
    // capped in total, so entries that never expire end up locking it.
    cleanIndex(m_notes, now, m_totalIndexNotes);
}

} // namespace eMule::kad
