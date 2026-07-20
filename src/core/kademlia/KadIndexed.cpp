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

#include <QDir>
#include <QFile>


namespace eMule::kad {

namespace {
constexpr uint32 kCleanInterval = 60 * 30; // 30 minutes
} // namespace

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

Indexed::Indexed(QObject* parent)
    : QObject(parent)
{
    m_nextClean = time(nullptr) + kCleanInterval;
}

Indexed::~Indexed()
{
    destroyIndex(m_keywords);
    destroyIndex(m_sources);
    destroyIndex(m_notes);

    // Clean up loads
    for (auto& [key, load] : m_loads)
        delete load;
}

bool Indexed::addKeyword(const UInt128& keyID, const UInt128& sourceID,
                          KeyEntry* entry, uint8& outLoad)
{
    QMutexLocker lock(&m_mutex);

    if (!entry)
        return false;

    // Check global limits
    if (m_totalIndexKeyword >= KADEMLIAMAXINDEX) {
        outLoad = 100;
        return false;
    }

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

        // Back-pressure before the index saturates: once we are near the cap,
        // stop accepting refreshes for keywords we already hold, so a hot
        // keyword cannot crowd out everything else. MFC Indexed.cpp:407.
        if (m_totalIndexKeyword > KADEMLIAMAXINDEX - 5000) {
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
        outLoad = static_cast<uint8>(
            (m_totalIndexKeyword * 100) / KADEMLIAMAXINDEX);
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
        (m_totalIndexKeyword * 100) / KADEMLIAMAXINDEX);
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
    QMutexLocker lock(&m_mutex);
    return addSourceEntry(m_sources, m_totalIndexSource, KADEMLIAMAXSOURCEPERFILE,
                          KADEMLIAREPUBLISHTIMES, kSourcePolicy,
                          keyID, sourceID, entry, outLoad);
}

bool Indexed::addNotes(const UInt128& keyID, const UInt128& sourceID,
                        Entry* entry, uint8& outLoad)
{
    QMutexLocker lock(&m_mutex);
    return addSourceEntry(m_notes, m_totalIndexNotes, KADEMLIAMAXNOTESPERFILE,
                          KADEMLIAREPUBLISHTIMEN, kNotePolicy,
                          keyID, sourceID, entry, outLoad);
}

bool Indexed::addLoad(const UInt128& keyID, time_t loadTime)
{
    QMutexLocker lock(&m_mutex);

    HashKeyOwn hashKey(keyID.getData());
    auto it = m_loads.find(hashKey);
    if (it != m_loads.end()) {
        it->second->time = loadTime;
        return true;
    }

    auto* load = new Load();
    load->keyID = keyID;
    load->time = loadTime;
    m_loads[hashKey] = load;
    ++m_totalIndexLoad;
    return true;
}

uint32 Indexed::getFileKeyCount() const
{
    return static_cast<uint32>(m_keywords.size());
}

void Indexed::sendValidKeywordResult(const UInt128& keyID, const SearchTerm* searchTerms,
                                      uint32 ip, uint16 port, bool /*oldClient*/,
                                      uint16 startPosition, const KadUDPKey& senderKey)
{
    QMutexLocker lock(&m_mutex);

    auto* udpListener = Kademlia::getInstanceUDPListener();
    if (!udpListener)
        return;

    HashKeyOwn hashKey(keyID.getData());
    auto it = m_keywords.find(hashKey);
    if (it == m_keywords.end())
        return;

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
}

void Indexed::sendValidSourceResult(const UInt128& keyID, uint32 ip, uint16 port,
                                     uint16 startPosition, uint64 fileSize,
                                     const KadUDPKey& senderKey)
{
    QMutexLocker lock(&m_mutex);

    auto* udpListener = Kademlia::getInstanceUDPListener();
    if (!udpListener)
        return;

    HashKeyOwn hashKey(keyID.getData());
    auto it = m_sources.find(hashKey);
    if (it == m_sources.end())
        return;

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
}

void Indexed::sendValidNoteResult(const UInt128& keyID, uint32 ip, uint16 port,
                                   uint64 fileSize, const KadUDPKey& senderKey)
{
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

    for (auto* source : srcHash->sourceList) {
        if (totalCount >= kMaxResults)
            break;
        for (auto* entry : source->entryList) {
            if (totalCount >= kMaxResults)
                break;
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
    QMutexLocker lock(&m_mutex);

    HashKeyOwn hashKey(keyID.getData());
    auto it = m_loads.find(hashKey);
    if (it != m_loads.end()) {
        // Check if enough time has passed since last store
        time_t now = time(nullptr);
        if ((now - it->second->time) < KADEMLIAREPUBLISHTIMEK)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

bool Indexed::addSourceEntry(SrcHashMap& index, uint32& counter, uint32 perFileMax,
                             time_t lifetimeSecs, const SourcePolicy& policy,
                             const UInt128& keyID, const UInt128& sourceID,
                             Entry* entry, uint8& outLoad)
{
    // Non-locking: addSources/addNotes already hold m_mutex.
    if (!entry)
        return false;

    // Reject malformed publishes outright rather than letting them occupy a
    // slot. MFC Indexed.cpp:452-458 / :524-526.
    entry->m_lifetime = time(nullptr) + lifetimeSecs;
    if (!policy.isPublishable(*entry))
        return false;

    if (counter >= KADEMLIAMAXENTRIES) {
        outLoad = 100;
        return false;
    }

    HashKeyOwn hashKey(keyID.getData());
    auto it = index.find(hashKey);
    if (it == index.end()) {
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

    auto* source = new Source();
    source->sourceID = sourceID;
    source->entryList.push_back(entry);
    srcHash->sourceList.push_front(source);
    ++counter;
    outLoad = static_cast<uint8>((bucketCount * 100) / perFileMax);
    return true;
}

void Indexed::readFile()
{
    auto* prefs = Kademlia::getInstancePrefs();
    if (!prefs) {
        m_dataLoaded = true;
        return;
    }

    // Determine config directory from prefs filename (same dir as preferencesKad.dat)
    // The index files are stored in the same directory.
    QString configDir = QDir::tempPath();

    // Load key_index.dat
    {
        QString keyFile = configDir + QStringLiteral("/key_index.dat");
        if (QFile::exists(keyFile)) {
            try {
                SafeFile sf;
                if (sf.open(keyFile, QIODevice::ReadOnly)) {
                    uint32 version = sf.readUInt32();
                    if (version == 3) {
                        time_t savetime = static_cast<time_t>(sf.readUInt32());
                        // Check if data is too old (more than 24h)
                        if ((time(nullptr) - savetime) < 86400) {
                            uint32 numKeys = sf.readUInt32();
                            for (uint32 k = 0; k < numKeys && sf.position() < sf.length(); ++k) {
                                uint8 keyIDBytes[16];
                                sf.readHash16(keyIDBytes);
                                UInt128 keyID(keyIDBytes);
                                uint32 numSources = sf.readUInt32();
                                for (uint32 s = 0; s < numSources && sf.position() < sf.length(); ++s) {
                                    uint8 srcIDBytes[16];
                                    sf.readHash16(srcIDBytes);
                                    UInt128 sourceID(srcIDBytes);
                                    uint32 numEntries = sf.readUInt32();
                                    for (uint32 e = 0; e < numEntries && sf.position() < sf.length(); ++e) {
                                        TagList tags = io::readKadTagList(sf);
                                        auto* entry = new KeyEntry();
                                        entry->m_keyID = keyID;
                                        entry->m_sourceID = sourceID;
                                        for (auto& tag : tags) {
                                            if (tag.nameId() == FT_FILENAME && tag.isStr()) {
                                                if (entry->getCommonFileName().isEmpty())
                                                    entry->setFileName(tag.strValue());
                                            } else if (tag.nameId() == FT_FILESIZE) {
                                                if (entry->m_size == 0)
                                                    entry->m_size = tag.isInt() ? tag.intValue()
                                                                  : tag.isInt64(false) ? tag.int64Value() : 0;
                                            } else {
                                                entry->addTag(std::move(tag));
                                            }
                                        }
                                        uint8 load = 0;
                                        if (!addKeyword(keyID, sourceID, entry, load))
                                            delete entry;
                                    }
                                }
                            }
                        }
                    }
                    logKad(QStringLiteral("Kad: Loaded %1 keywords from key_index.dat")
                               .arg(m_totalIndexKeyword));
                }
            } catch (const FileException& ex) {
                logKad(QStringLiteral("Kad: Failed to load key_index.dat: %1").arg(QLatin1StringView(ex.what())));
            }
        }
    }

    // Load src_index.dat
    {
        QString srcFile = configDir + QStringLiteral("/src_index.dat");
        if (QFile::exists(srcFile)) {
            try {
                SafeFile sf;
                if (sf.open(srcFile, QIODevice::ReadOnly)) {
                    uint32 version = sf.readUInt32();
                    if (version == 2) {
                        time_t savetime = static_cast<time_t>(sf.readUInt32());
                        if ((time(nullptr) - savetime) < 86400) {
                            uint32 numKeys = sf.readUInt32();
                            for (uint32 k = 0; k < numKeys && sf.position() < sf.length(); ++k) {
                                uint8 keyIDBytes[16];
                                sf.readHash16(keyIDBytes);
                                UInt128 keyID(keyIDBytes);
                                uint32 numSources = sf.readUInt32();
                                for (uint32 s = 0; s < numSources && sf.position() < sf.length(); ++s) {
                                    uint8 srcIDBytes[16];
                                    sf.readHash16(srcIDBytes);
                                    UInt128 sourceID(srcIDBytes);
                                    TagList tags = io::readKadTagList(sf);
                                    auto* entry = new Entry();
                                    entry->m_keyID = keyID;
                                    entry->m_sourceID = sourceID;
                                    for (auto& tag : tags)
                                        entry->addTag(std::move(tag));
                                    uint8 load = 0;
                                    if (!addSources(keyID, sourceID, entry, load))
                                        delete entry;
                                }
                            }
                        }
                    }
                    logKad(QStringLiteral("Kad: Loaded %1 sources from src_index.dat")
                               .arg(m_totalIndexSource));
                }
            } catch (const FileException& ex) {
                logKad(QStringLiteral("Kad: Failed to load src_index.dat: %1").arg(QLatin1StringView(ex.what())));
            }
        }
    }

    // Load load_index.dat
    {
        QString loadFile = configDir + QStringLiteral("/load_index.dat");
        if (QFile::exists(loadFile)) {
            try {
                SafeFile sf;
                if (sf.open(loadFile, QIODevice::ReadOnly)) {
                    uint32 version = sf.readUInt32();
                    if (version == 1) {
                        uint32 numEntries = sf.readUInt32();
                        for (uint32 i = 0; i < numEntries && sf.position() < sf.length(); ++i) {
                            uint8 keyIDBytes[16];
                            sf.readHash16(keyIDBytes);
                            UInt128 keyID(keyIDBytes);
                            time_t loadTime = static_cast<time_t>(sf.readUInt32());
                            addLoad(keyID, loadTime);
                        }
                    }
                    logKad(QStringLiteral("Kad: Loaded %1 load entries from load_index.dat")
                               .arg(m_totalIndexLoad));
                }
            } catch (const FileException& ex) {
                logKad(QStringLiteral("Kad: Failed to load load_index.dat: %1").arg(QLatin1StringView(ex.what())));
            }
        }
    }

    m_dataLoaded = true;
}

void Indexed::clean()
{
    QMutexLocker lock(&m_mutex);

    time_t now = time(nullptr);
    if (now < m_nextClean)
        return;
    m_nextClean = now + kCleanInterval;

    cleanIndex(m_keywords, now, m_totalIndexKeyword);
    cleanIndex(m_sources, now, m_totalIndexSource);
    cleanIndex(m_notes, now, m_totalIndexNotes);
}

} // namespace eMule::kad
