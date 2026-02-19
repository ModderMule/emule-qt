/// @file KadEntry.cpp
/// @brief DHT data entry implementation.

#include "kademlia/KadEntry.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadMiscUtils.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <algorithm>
#include <ctime>
#include <cmath>

namespace eMule::kad {

// Tag name constants matching MFC TAG_FILENAME / TAG_FILESIZE etc.
namespace {
const QByteArray kTagFilename = QByteArrayLiteral("\x01");
const QByteArray kTagFilesize = QByteArrayLiteral("\x02");
} // namespace

// ---------------------------------------------------------------------------
// Entry — public
// ---------------------------------------------------------------------------

Entry::Entry() = default;

Entry* Entry::copy() const
{
    auto* e = new Entry();
    e->m_ip = m_ip;
    e->m_tcpPort = m_tcpPort;
    e->m_udpPort = m_udpPort;
    e->m_keyID = m_keyID;
    e->m_sourceID = m_sourceID;
    e->m_size = m_size;
    e->m_lifetime = m_lifetime;
    e->m_source = m_source;
    e->m_fileNames = m_fileNames;
    e->m_tags = m_tags;
    return e;
}

uint64 Entry::getIntTagValue(const QByteArray& tagName, bool includeVirtual) const
{
    uint64 val = 0;
    getIntTagValue(tagName, val, includeVirtual);
    return val;
}

bool Entry::getIntTagValue(const QByteArray& tagName, uint64& outValue, bool includeVirtual) const
{
    // Check virtual tags first
    if (includeVirtual) {
        if (tagName == kTagFilesize) {
            outValue = m_size;
            return true;
        }
    }

    for (const auto& tag : m_tags) {
        if (tag.name() == tagName && tag.isInt64(true)) {
            outValue = tag.int64Value();
            return true;
        }
    }
    return false;
}

QString Entry::getStrTagValue(const QByteArray& tagName) const
{
    if (tagName == kTagFilename)
        return getCommonFileName();

    for (const auto& tag : m_tags) {
        if (tag.name() == tagName && tag.isStr())
            return tag.strValue();
    }
    return {};
}

void Entry::addTag(Tag tag)
{
    m_tags.push_back(std::move(tag));
}

uint32 Entry::getTagCount() const
{
    // Tag count includes virtual tags (filename, filesize)
    uint32 count = static_cast<uint32>(m_tags.size());
    if (!m_fileNames.empty())
        ++count; // filename
    if (m_size > 0)
        ++count; // filesize
    return count;
}

void Entry::writeTagList(FileDataIO& data) const
{
    writeTagListInc(data, 0);
}

QString Entry::getCommonFileName() const
{
    if (m_fileNames.empty())
        return {};

    // Return the filename with highest popularity
    const FileNameEntry* best = nullptr;
    for (const auto& entry : m_fileNames) {
        if (!best || entry.popularityIndex > best->popularityIndex)
            best = &entry;
    }
    return best ? best->fileName : QString();
}

QString Entry::getCommonFileNameLowerCase() const
{
    return kadTagStrToLower(getCommonFileName());
}

void Entry::setFileName(const QString& name)
{
    if (name.isEmpty())
        return;

    // Check if this filename already exists
    QString lower = kadTagStrToLower(name);
    for (auto& entry : m_fileNames) {
        if (kadTagStrToLower(entry.fileName) == lower) {
            ++entry.popularityIndex;
            return;
        }
    }

    // Add new filename
    m_fileNames.push_back({name, 1});
}

// ---------------------------------------------------------------------------
// Entry — protected
// ---------------------------------------------------------------------------

void Entry::writeTagListInc(FileDataIO& data, uint32 increaseTagNumber) const
{
    uint32 count = static_cast<uint32>(m_tags.size()) + increaseTagNumber;

    // Add virtual tags
    bool hasFilename = !m_fileNames.empty();
    bool hasFilesize = m_size > 0;
    if (hasFilename) ++count;
    if (hasFilesize) ++count;

    // MFC WriteTagList uses WriteByte — single uint8 for Kad tag count
    data.writeUInt8(static_cast<uint8>(count));

    // Write virtual filename tag
    if (hasFilename) {
        Tag fnTag(kTagFilename, getCommonFileName());
        io::writeKadTag(data, fnTag);
    }

    // Write virtual filesize tag
    if (hasFilesize) {
        if (m_size > 0xFFFFFFFF) {
            Tag fsTag(kTagFilesize, m_size);
            io::writeKadTag(data, fsTag);
        } else {
            Tag fsTag(kTagFilesize, static_cast<uint32>(m_size));
            io::writeKadTag(data, fsTag);
        }
    }

    // Write stored tags
    for (const auto& tag : m_tags)
        io::writeKadTag(data, tag);
}

// ---------------------------------------------------------------------------
// KeyEntry — public
// ---------------------------------------------------------------------------

std::unordered_map<uint32, uint32> KeyEntry::s_globalPublishIPs;

KeyEntry::KeyEntry() = default;

KeyEntry::~KeyEntry()
{
    if (m_publishingIPs) {
        // Decrease global tracking for each IP
        for (const auto& pip : *m_publishingIPs)
            adjustGlobalPublishTracking(pip.ip, false);
        delete m_publishingIPs;
    }
}

Entry* KeyEntry::copy() const
{
    auto* e = new KeyEntry();
    e->m_ip = m_ip;
    e->m_tcpPort = m_tcpPort;
    e->m_udpPort = m_udpPort;
    e->m_keyID = m_keyID;
    e->m_sourceID = m_sourceID;
    e->m_size = m_size;
    e->m_lifetime = m_lifetime;
    e->m_source = m_source;
    e->m_fileNames = m_fileNames;
    e->m_tags = m_tags;
    e->m_trustValue = m_trustValue;
    e->m_lastTrustValueCalc = m_lastTrustValueCalc;
    return e;
}

bool KeyEntry::startSearchTermsMatch(const SearchTerm& term)
{
    // Build search cache if needed
    if (m_searchTermCache.isEmpty()) {
        m_searchTermCache = getCommonFileNameLowerCase();
    }
    return searchTermsMatch(term);
}

void KeyEntry::mergeIPsAndFilenames(KeyEntry* from)
{
    if (!from)
        return;

    // Merge filenames
    for (const auto& fn : from->m_fileNames) {
        setFileName(fn.fileName);
    }

    // Merge publishing IPs
    if (from->m_publishingIPs && !from->m_publishingIPs->empty()) {
        if (!m_publishingIPs)
            m_publishingIPs = new std::list<PublishingIP>();

        for (const auto& pip : *from->m_publishingIPs) {
            bool found = false;
            for (auto& existing : *m_publishingIPs) {
                if (existing.ip == pip.ip) {
                    existing.lastPublish = std::max(existing.lastPublish, pip.lastPublish);
                    found = true;
                    break;
                }
            }
            if (!found) {
                m_publishingIPs->push_back(pip);
                adjustGlobalPublishTracking(pip.ip, true);
            }
        }
    }

    // Invalidate trust
    m_trustValue = 0.0f;
    m_lastTrustValueCalc = 0;
}

void KeyEntry::cleanUpTrackedPublishers()
{
    if (!m_publishingIPs)
        return;

    time_t now = time(nullptr);
    constexpr time_t kExpireTime = 60 * 60 * 48; // 48 hours

    auto it = m_publishingIPs->begin();
    while (it != m_publishingIPs->end()) {
        if ((now - it->lastPublish) > kExpireTime) {
            adjustGlobalPublishTracking(it->ip, false);
            it = m_publishingIPs->erase(it);
        } else {
            ++it;
        }
    }
}

float KeyEntry::getTrustValue()
{
    // Recalculate if stale (every 10 minutes)
    uint32 now = static_cast<uint32>(time(nullptr));
    if (m_trustValue == 0.0f || (now - m_lastTrustValueCalc) > 600) {
        recalculateTrustValue();
        m_lastTrustValueCalc = now;
    }
    return m_trustValue;
}

void KeyEntry::writePublishTrackingDataToFile(FileDataIO& data)
{
    if (!m_publishingIPs || m_publishingIPs->empty()) {
        data.writeUInt32(0);
        return;
    }

    data.writeUInt32(static_cast<uint32>(m_publishingIPs->size()));
    for (const auto& pip : *m_publishingIPs) {
        data.writeUInt32(pip.ip);
        data.writeUInt32(static_cast<uint32>(pip.lastPublish));
        // AICH hash index (v9+)
        data.writeUInt16(pip.aichHashIdx);
    }

    // Write AICH hashes
    data.writeUInt8(static_cast<uint8>(m_aichHashes.size()));
    for (const auto& hash : m_aichHashes) {
        data.write(hash.constData(), 20);
    }
}

void KeyEntry::readPublishTrackingDataFromFile(FileDataIO& data, bool includesAICH)
{
    uint32 count = data.readUInt32();
    if (count == 0)
        return;

    if (!m_publishingIPs)
        m_publishingIPs = new std::list<PublishingIP>();

    for (uint32 i = 0; i < count; ++i) {
        PublishingIP pip;
        pip.ip = data.readUInt32();
        pip.lastPublish = static_cast<time_t>(data.readUInt32());
        if (includesAICH)
            pip.aichHashIdx = data.readUInt16();
        m_publishingIPs->push_back(pip);
        adjustGlobalPublishTracking(pip.ip, true);
    }

    if (includesAICH) {
        uint8 aichCount = data.readUInt8();
        m_aichHashes.resize(aichCount);
        for (uint8 i = 0; i < aichCount; ++i) {
            m_aichHashes[i].resize(20);
            data.read(m_aichHashes[i].data(), 20);
        }
    }
}

void KeyEntry::dirtyDeletePublishData()
{
    // Delete without adjusting global tracking (for mass cleanup)
    delete m_publishingIPs;
    m_publishingIPs = nullptr;
    m_aichHashes.clear();
    m_aichHashPopularity.clear();
}

void KeyEntry::writeTagListWithPublishInfo(FileDataIO& data)
{
    // Write tags plus an additional tag for trust value
    writeTagListInc(data, 1);

    // Write trust value as a special tag
    Tag trustTag(QByteArrayLiteral("\xF0"), static_cast<uint32>(getTrustValue() * 100.0f));
    io::writeKadTag(data, trustTag);
}

void KeyEntry::resetGlobalTrackingMap()
{
    s_globalPublishIPs.clear();
}

// ---------------------------------------------------------------------------
// KeyEntry — private
// ---------------------------------------------------------------------------

bool KeyEntry::searchTermsMatch(const SearchTerm& term) const
{
    switch (term.type) {
    case SearchTerm::Type::AND:
        return (term.left && searchTermsMatch(*term.left))
               && (term.right && searchTermsMatch(*term.right));

    case SearchTerm::Type::OR:
        return (term.left && searchTermsMatch(*term.left))
               || (term.right && searchTermsMatch(*term.right));

    case SearchTerm::Type::NOT:
        return (term.left && searchTermsMatch(*term.left))
               && !(term.right && searchTermsMatch(*term.right));

    case SearchTerm::Type::String:
        for (const auto& word : term.strings) {
            if (m_searchTermCache.contains(word, Qt::CaseInsensitive))
                return true;
        }
        return term.strings.empty();

    case SearchTerm::Type::MetaTag: {
        if (term.tag.isStr()) {
            QString val = getStrTagValue(term.tag.name());
            return val.contains(term.tag.strValue(), Qt::CaseInsensitive);
        }
        return false;
    }

    case SearchTerm::Type::OpGreaterEqual:
    case SearchTerm::Type::OpLessEqual:
    case SearchTerm::Type::OpGreater:
    case SearchTerm::Type::OpLess:
    case SearchTerm::Type::OpEqual:
    case SearchTerm::Type::OpNotEqual: {
        uint64 entryVal = 0;
        if (!getIntTagValue(term.tag.name(), entryVal, true))
            return false;
        uint64 searchVal = term.tag.int64Value();
        switch (term.type) {
        case SearchTerm::Type::OpGreaterEqual: return entryVal >= searchVal;
        case SearchTerm::Type::OpLessEqual:    return entryVal <= searchVal;
        case SearchTerm::Type::OpGreater:      return entryVal > searchVal;
        case SearchTerm::Type::OpLess:         return entryVal < searchVal;
        case SearchTerm::Type::OpEqual:        return entryVal == searchVal;
        case SearchTerm::Type::OpNotEqual:     return entryVal != searchVal;
        default: return false;
        }
    }
    }
    return false;
}

void KeyEntry::recalculateTrustValue()
{
    if (!m_publishingIPs || m_publishingIPs->empty()) {
        m_trustValue = 0.0f;
        return;
    }

    // Trust = log2(unique_publishers) capped at 10
    // Penalize IPs that publish too many different entries (global tracking)
    float trust = 0.0f;
    uint32 publishers = 0;

    for (const auto& pip : *m_publishingIPs) {
        auto it = s_globalPublishIPs.find(pip.ip);
        if (it != s_globalPublishIPs.end()) {
            // Weight inversely proportional to number of entries this IP publishes
            trust += 1.0f / static_cast<float>(std::max(it->second, uint32{1}));
        } else {
            trust += 1.0f;
        }
        ++publishers;
    }

    if (publishers > 0)
        m_trustValue = std::min(std::log2(trust + 1.0f), 10.0f);
    else
        m_trustValue = 0.0f;
}

void KeyEntry::adjustGlobalPublishTracking(uint32 ip, bool increase)
{
    if (increase) {
        ++s_globalPublishIPs[ip];
    } else {
        auto it = s_globalPublishIPs.find(ip);
        if (it != s_globalPublishIPs.end()) {
            if (it->second <= 1)
                s_globalPublishIPs.erase(it);
            else
                --it->second;
        }
    }
}

} // namespace eMule::kad
