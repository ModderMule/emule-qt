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

    // Get or create key hash entry
    HashKeyOwn hashKey(keyID.getData());
    KeyHash* keyHash = nullptr;
    auto it = m_keywords.find(hashKey);
    if (it != m_keywords.end()) {
        keyHash = it->second;
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
        // Source exists — update existing entry
        if (!source->entryList.empty()) {
            auto* existing = source->entryList.front();
            if (existing->isKeyEntry()) {
                auto* existingKey = static_cast<KeyEntry*>(existing);
                existingKey->mergeIPsAndFilenames(entry);
            }
            outLoad = static_cast<uint8>(
                (m_totalIndexKeyword * 100) / KADEMLIAMAXINDEX);
            return true;
        }
    } else {
        source = new Source();
        source->sourceID = sourceID;
        keyHash->mapSource[srcKey] = source;
    }

    // Add the entry
    entry->m_lifetime = time(nullptr) + KADEMLIAREPUBLISHTIMEK;
    source->entryList.push_back(entry);
    ++m_totalIndexKeyword;

    outLoad = static_cast<uint8>(
        (m_totalIndexKeyword * 100) / KADEMLIAMAXINDEX);
    return true;
}

bool Indexed::addSources(const UInt128& keyID, const UInt128& sourceID,
                          Entry* entry, uint8& outLoad)
{
    QMutexLocker lock(&m_mutex);
    return addSourceEntry(m_sources, m_totalIndexSource, KADEMLIAMAXSOURCEPERFILE,
                          KADEMLIAREPUBLISHTIMES, keyID, sourceID, entry, outLoad);
}

bool Indexed::addNotes(const UInt128& keyID, const UInt128& sourceID,
                        Entry* entry, uint8& outLoad)
{
    QMutexLocker lock(&m_mutex);
    return addSourceEntry(m_notes, m_totalIndexNotes, KADEMLIAMAXNOTESPERFILE,
                          KADEMLIAREPUBLISHTIMEN, keyID, sourceID, entry, outLoad);
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
                             time_t lifetimeSecs, const UInt128& keyID,
                             const UInt128& sourceID, Entry* entry, uint8& outLoad)
{
    // Non-locking: addSources/addNotes already hold m_mutex.
    if (!entry)
        return false;

    if (counter >= KADEMLIAMAXENTRIES) {
        outLoad = 100;
        return false;
    }

    HashKeyOwn hashKey(keyID.getData());
    SrcHash* srcHash = nullptr;
    auto it = index.find(hashKey);
    if (it != index.end()) {
        srcHash = it->second;

        // Check per-file limit
        uint32 total = 0;
        for (auto* src : srcHash->sourceList)
            total += static_cast<uint32>(src->entryList.size());
        if (total >= perFileMax) {
            outLoad = 100;
            return false;
        }
    } else {
        srcHash = new SrcHash();
        srcHash->keyID = keyID;
        index[hashKey] = srcHash;
    }

    // Find or create source
    Source* source = nullptr;
    for (auto* s : srcHash->sourceList) {
        if (s->sourceID == sourceID) {
            source = s;
            break;
        }
    }
    if (!source) {
        source = new Source();
        source->sourceID = sourceID;
        srcHash->sourceList.push_back(source);
    }

    // Replace existing entries from same source
    for (auto* e : source->entryList) {
        delete e;
        --counter;
    }
    source->entryList.clear();

    entry->m_lifetime = time(nullptr) + lifetimeSecs;
    source->entryList.push_back(entry);
    ++counter;

    outLoad = static_cast<uint8>((counter * 100) / KADEMLIAMAXENTRIES);
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
