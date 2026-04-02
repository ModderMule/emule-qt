#include "pch.h"
/// @file KadIndexed.cpp
/// @brief Keyword/source/notes index implementation.

#include "kademlia/KadIndexed.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadPrefs.h"
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
    // Clean up keywords
    for (auto& [key, keyHash] : m_keywords) {
        for (auto& [srcKey, source] : keyHash->mapSource) {
            for (auto* entry : source->entryList)
                delete entry;
            delete source;
        }
        delete keyHash;
    }

    // Clean up sources
    for (auto& [key, srcHash] : m_sources) {
        for (auto* source : srcHash->sourceList) {
            for (auto* entry : source->entryList)
                delete entry;
            delete source;
        }
        delete srcHash;
    }

    // Clean up notes
    for (auto& [key, srcHash] : m_notes) {
        for (auto* source : srcHash->sourceList) {
            for (auto* entry : source->entryList)
                delete entry;
            delete source;
        }
        delete srcHash;
    }

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

    if (!entry)
        return false;

    if (m_totalIndexSource >= KADEMLIAMAXENTRIES) {
        outLoad = 100;
        return false;
    }

    HashKeyOwn hashKey(keyID.getData());
    SrcHash* srcHash = nullptr;
    auto it = m_sources.find(hashKey);
    if (it != m_sources.end()) {
        srcHash = it->second;

        // Check per-file source limit
        uint32 totalSrc = 0;
        for (auto* src : srcHash->sourceList)
            totalSrc += static_cast<uint32>(src->entryList.size());
        if (totalSrc >= KADEMLIAMAXSOURCEPERFILE) {
            outLoad = 100;
            return false;
        }
    } else {
        srcHash = new SrcHash();
        srcHash->keyID = keyID;
        m_sources[hashKey] = srcHash;
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
        --m_totalIndexSource;
    }
    source->entryList.clear();

    entry->m_lifetime = time(nullptr) + KADEMLIAREPUBLISHTIMES;
    source->entryList.push_back(entry);
    ++m_totalIndexSource;

    outLoad = static_cast<uint8>(
        (m_totalIndexSource * 100) / KADEMLIAMAXENTRIES);
    return true;
}

bool Indexed::addNotes(const UInt128& keyID, const UInt128& sourceID,
                        Entry* entry, uint8& outLoad)
{
    QMutexLocker lock(&m_mutex);

    if (!entry)
        return false;

    if (m_totalIndexNotes >= KADEMLIAMAXENTRIES) {
        outLoad = 100;
        return false;
    }

    HashKeyOwn hashKey(keyID.getData());
    SrcHash* srcHash = nullptr;
    auto it = m_notes.find(hashKey);
    if (it != m_notes.end()) {
        srcHash = it->second;

        uint32 totalNotes = 0;
        for (auto* src : srcHash->sourceList)
            totalNotes += static_cast<uint32>(src->entryList.size());
        if (totalNotes >= KADEMLIAMAXNOTESPERFILE) {
            outLoad = 100;
            return false;
        }
    } else {
        srcHash = new SrcHash();
        srcHash->keyID = keyID;
        m_notes[hashKey] = srcHash;
    }

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

    // Replace existing notes from same source
    for (auto* e : source->entryList) {
        delete e;
        --m_totalIndexNotes;
    }
    source->entryList.clear();

    entry->m_lifetime = time(nullptr) + KADEMLIAREPUBLISHTIMEN;
    source->entryList.push_back(entry);
    ++m_totalIndexNotes;

    outLoad = static_cast<uint8>(
        (m_totalIndexNotes * 100) / KADEMLIAMAXENTRIES);
    return true;
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

    // Helper: write packet header (kadID + target + count placeholder)
    auto writeHeader = [&](SafeMemFile& pkt) -> qint64 {
        io::writeUInt128(pkt, Kademlia::getInstancePrefs()->kadId());
        io::writeUInt128(pkt, keyID);
        auto pos = pkt.position();
        pkt.writeUInt16(0);
        return pos;
    };

    auto packet = std::make_unique<SafeMemFile>();
    auto countPos = writeHeader(*packet);

    constexpr int kMaxResults = 300;
    int count = -static_cast<int>(startPosition); // negative = skip entries for pagination
    uint16 unsentCount = 0;

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

                // Write this result to a temp buffer to check size
                SafeMemFile tmpBuf;
                io::writeUInt128(tmpBuf, source->sourceID);
                keyEntry->writeTagListWithPublishInfo(tmpBuf);

                // Anti-fragmentation: if adding this result exceeds MTU, send current packet first
                if (packet->length() + tmpBuf.length() > UDP_KAD_MAXFRAGMENT && unsentCount > 0) {
                    auto endPos = packet->position();
                    packet->seek(countPos, 0);
                    packet->writeUInt16(unsentCount);
                    packet->seek(endPos, 0);
                    udpListener->sendPacket(*packet, KADEMLIA2_SEARCH_RES, ip, port, senderKey, nullptr);

                    // Start new packet
                    packet = std::make_unique<SafeMemFile>();
                    countPos = writeHeader(*packet);
                    unsentCount = 0;
                }

                const auto& buf = tmpBuf.buffer();
                packet->write(buf.constData(), tmpBuf.length());
                ++unsentCount;
            }
        }
        if (!onlyTrusted)
            break;
    }

    if (unsentCount > 0) {
        auto endPos = packet->position();
        packet->seek(countPos, 0);
        packet->writeUInt16(unsentCount);
        packet->seek(endPos, 0);
        udpListener->sendPacket(*packet, KADEMLIA2_SEARCH_RES, ip, port, senderKey, nullptr);
    }
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

    auto writeHeader = [&](SafeMemFile& pkt) -> qint64 {
        io::writeUInt128(pkt, Kademlia::getInstancePrefs()->kadId());
        io::writeUInt128(pkt, keyID);
        auto pos = pkt.position();
        pkt.writeUInt16(0);
        return pos;
    };

    auto packet = std::make_unique<SafeMemFile>();
    auto countPos = writeHeader(*packet);

    int count = -static_cast<int>(startPosition);
    uint16 unsentCount = 0;
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

        // Anti-fragmentation
        if (packet->length() + tmpBuf.length() > UDP_KAD_MAXFRAGMENT && unsentCount > 0) {
            auto endPos = packet->position();
            packet->seek(countPos, 0);
            packet->writeUInt16(unsentCount);
            packet->seek(endPos, 0);
            udpListener->sendPacket(*packet, KADEMLIA2_SEARCH_RES, ip, port, senderKey, nullptr);
            packet = std::make_unique<SafeMemFile>();
            countPos = writeHeader(*packet);
            unsentCount = 0;
        }

        const auto& buf = tmpBuf.buffer();
        packet->write(buf.constData(), tmpBuf.length());
        ++unsentCount;
    }

    if (unsentCount > 0) {
        auto endPos = packet->position();
        packet->seek(countPos, 0);
        packet->writeUInt16(unsentCount);
        packet->seek(endPos, 0);
        udpListener->sendPacket(*packet, KADEMLIA2_SEARCH_RES, ip, port, senderKey, nullptr);
    }
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

    auto writeHeader = [&](SafeMemFile& pkt) -> qint64 {
        io::writeUInt128(pkt, Kademlia::getInstancePrefs()->kadId());
        io::writeUInt128(pkt, keyID);
        auto pos = pkt.position();
        pkt.writeUInt16(0);
        return pos;
    };

    auto packet = std::make_unique<SafeMemFile>();
    auto countPos = writeHeader(*packet);

    uint16 unsentCount = 0;
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

            // Anti-fragmentation
            if (packet->length() + tmpBuf.length() > UDP_KAD_MAXFRAGMENT && unsentCount > 0) {
                auto endPos = packet->position();
                packet->seek(countPos, 0);
                packet->writeUInt16(unsentCount);
                packet->seek(endPos, 0);
                udpListener->sendPacket(*packet, KADEMLIA2_SEARCH_RES, ip, port, senderKey, nullptr);
                packet = std::make_unique<SafeMemFile>();
                countPos = writeHeader(*packet);
                unsentCount = 0;
            }

            const auto& buf = tmpBuf.buffer();
            packet->write(buf.constData(), tmpBuf.length());
            ++unsentCount;
            ++totalCount;
        }
    }

    if (unsentCount > 0) {
        auto endPos = packet->position();
        packet->seek(countPos, 0);
        packet->writeUInt16(unsentCount);
        packet->seek(endPos, 0);
        udpListener->sendPacket(*packet, KADEMLIA2_SEARCH_RES, ip, port, senderKey, nullptr);
    }
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

    // Clean expired keyword entries
    for (auto keyIt = m_keywords.begin(); keyIt != m_keywords.end(); ) {
        KeyHash* keyHash = keyIt->second;
        for (auto srcIt = keyHash->mapSource.begin(); srcIt != keyHash->mapSource.end(); ) {
            Source* source = srcIt->second;
            for (auto entIt = source->entryList.begin(); entIt != source->entryList.end(); ) {
                if ((*entIt)->m_lifetime > 0 && (*entIt)->m_lifetime < now) {
                    delete *entIt;
                    entIt = source->entryList.erase(entIt);
                    --m_totalIndexKeyword;
                } else {
                    ++entIt;
                }
            }
            if (source->entryList.empty()) {
                delete source;
                srcIt = keyHash->mapSource.erase(srcIt);
            } else {
                ++srcIt;
            }
        }
        if (keyHash->mapSource.empty()) {
            delete keyHash;
            keyIt = m_keywords.erase(keyIt);
        } else {
            ++keyIt;
        }
    }

    // Clean expired source entries
    for (auto hashIt = m_sources.begin(); hashIt != m_sources.end(); ) {
        SrcHash* srcHash = hashIt->second;
        for (auto srcIt = srcHash->sourceList.begin(); srcIt != srcHash->sourceList.end(); ) {
            Source* source = *srcIt;
            for (auto entIt = source->entryList.begin(); entIt != source->entryList.end(); ) {
                if ((*entIt)->m_lifetime > 0 && (*entIt)->m_lifetime < now) {
                    delete *entIt;
                    entIt = source->entryList.erase(entIt);
                    --m_totalIndexSource;
                } else {
                    ++entIt;
                }
            }
            if (source->entryList.empty()) {
                delete source;
                srcIt = srcHash->sourceList.erase(srcIt);
            } else {
                ++srcIt;
            }
        }
        if (srcHash->sourceList.empty()) {
            delete srcHash;
            hashIt = m_sources.erase(hashIt);
        } else {
            ++hashIt;
        }
    }

    // Clean expired note entries (same pattern)
    for (auto hashIt = m_notes.begin(); hashIt != m_notes.end(); ) {
        SrcHash* srcHash = hashIt->second;
        for (auto srcIt = srcHash->sourceList.begin(); srcIt != srcHash->sourceList.end(); ) {
            Source* source = *srcIt;
            for (auto entIt = source->entryList.begin(); entIt != source->entryList.end(); ) {
                if ((*entIt)->m_lifetime > 0 && (*entIt)->m_lifetime < now) {
                    delete *entIt;
                    entIt = source->entryList.erase(entIt);
                    --m_totalIndexNotes;
                } else {
                    ++entIt;
                }
            }
            if (source->entryList.empty()) {
                delete source;
                srcIt = srcHash->sourceList.erase(srcIt);
            } else {
                ++srcIt;
            }
        }
        if (srcHash->sourceList.empty()) {
            delete srcHash;
            hashIt = m_notes.erase(hashIt);
        } else {
            ++hashIt;
        }
    }
}

} // namespace eMule::kad
