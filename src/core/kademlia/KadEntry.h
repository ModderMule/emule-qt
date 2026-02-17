#pragma once

/// @file KadEntry.h
/// @brief DHT data entries (ported from kademlia/kademlia/Entry.h).
///
/// Entry represents a stored source/keyword/notes record in the DHT.
/// KeyEntry extends Entry with search term matching and trust tracking.

#include "kademlia/KadSearchDefs.h"
#include "kademlia/KadTypes.h"
#include "kademlia/KadUInt128.h"
#include "utils/SafeFile.h"
#include "utils/Types.h"

#include <QString>

#include <cstdint>
#include <ctime>
#include <list>
#include <unordered_map>
#include <vector>

namespace eMule::kad {

// ---------------------------------------------------------------------------
// Entry — base DHT data entry
// ---------------------------------------------------------------------------

class Entry {
public:
    Entry();
    virtual ~Entry() = default;
    virtual Entry* copy() const;
    virtual bool isKeyEntry() const { return false; }

    uint64 getIntTagValue(const QByteArray& tagName, bool includeVirtual = true) const;
    bool getIntTagValue(const QByteArray& tagName, uint64& outValue, bool includeVirtual = true) const;
    QString getStrTagValue(const QByteArray& tagName) const;
    void addTag(Tag tag);
    uint32 getTagCount() const;
    void writeTagList(FileDataIO& data) const;

    QString getCommonFileName() const;
    QString getCommonFileNameLowerCase() const;
    void setFileName(const QString& name);

    uint32 m_ip = 0;
    uint16 m_tcpPort = 0;
    uint16 m_udpPort = 0;
    UInt128 m_keyID;
    UInt128 m_sourceID;
    uint64 m_size = 0;
    time_t m_lifetime = 0;
    bool m_source = false;

protected:
    struct FileNameEntry {
        QString fileName;
        uint32 popularityIndex = 0;
    };
    void writeTagListInc(FileDataIO& data, uint32 increaseTagNumber = 0) const;
    std::list<FileNameEntry> m_fileNames;
    TagList m_tags;
};

// ---------------------------------------------------------------------------
// KeyEntry — keyword entry with trust tracking
// ---------------------------------------------------------------------------

class KeyEntry : public Entry {
public:
    KeyEntry();
    ~KeyEntry() override;
    Entry* copy() const override;
    bool isKeyEntry() const override { return true; }

    bool startSearchTermsMatch(const SearchTerm& term);
    void mergeIPsAndFilenames(KeyEntry* from);
    void cleanUpTrackedPublishers();
    float getTrustValue();
    void writePublishTrackingDataToFile(FileDataIO& data);
    void readPublishTrackingDataFromFile(FileDataIO& data, bool includesAICH);
    void dirtyDeletePublishData();
    void writeTagListWithPublishInfo(FileDataIO& data);

    static void resetGlobalTrackingMap();

private:
    bool searchTermsMatch(const SearchTerm& term) const;
    void recalculateTrustValue();
    static void adjustGlobalPublishTracking(uint32 ip, bool increase);

    struct PublishingIP {
        time_t lastPublish = 0;
        uint32 ip = 0;
        uint16 aichHashIdx = 0;
    };

    float m_trustValue = 0.0f;
    std::vector<uint8> m_aichHashPopularity;
    std::vector<QByteArray> m_aichHashes;
    std::list<PublishingIP>* m_publishingIPs = nullptr;
    uint32 m_lastTrustValueCalc = 0;
    QString m_searchTermCache;

    static std::unordered_map<uint32, uint32> s_globalPublishIPs;
};

} // namespace eMule::kad
