#pragma once

/// @file KadEntry.h
/// @brief DHT data entries (ported from kademlia/kademlia/Entry.h).
///
/// Entry represents a stored source/keyword/notes record in the DHT.
/// KeyEntry extends Entry with search term matching and trust tracking.

#include "kademlia/KadSearchDefs.h"
#include "kademlia/KadTypes.h"
#include "kademlia/KadUInt128.h"
#include "net/Address.h"
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

    /// Canonical lookup key for @p tag: its name, or its single-byte numeric ID
    /// rendered as a one-byte name. Kad tags arrive in either shape depending on
    /// whether they came from io::readKadTag or were built with a nameId.
    [[nodiscard]] static QByteArray tagLookupKey(const Tag& tag);

    uint64 getIntTagValue(const QByteArray& tagName, bool includeVirtual = true) const;
    bool getIntTagValue(const QByteArray& tagName, uint64& outValue, bool includeVirtual = true) const;
    bool getFloatTagValue(const QByteArray& tagName, float& outValue) const;
    QString getStrTagValue(const QByteArray& tagName) const;
    void addTag(Tag tag);
    uint32 getTagCount() const;
    void writeTagList(FileDataIO& data) const;

    QString getCommonFileName() const;
    QString getCommonFileNameLowerCase() const;
    void setFileName(const QString& name);

    Address m_address;        // publisher's IP (host byte order convention)
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

    /// Reference-count an AICH hash reported by a publisher.
    /// Returns the hash's stable index. Removal only decrements the popularity
    /// count — the slot itself is never erased, so indices stay valid.
    /// MFC Entry.cpp:715-737.
    uint16 addRemoveAICHHash(const QByteArray& hash, bool add);
    [[nodiscard]] uint16 aichHashCount() const
    {
        return static_cast<uint16>(m_aichHashes.size());
    }

    static void resetGlobalTrackingMap();

    /// Sentinel for "this publisher reported no AICH hash".
    static constexpr uint16 kNoAICHHash = 0xFFFF;

private:
    bool searchTermsMatch(const SearchTerm& term) const;
    void recalculateTrustValue();
    /// @param ip publisher address; the tracking map is keyed by its /24 block.
    static void adjustGlobalPublishTracking(const Address& ip, bool increase);

    struct PublishingIP {
        time_t lastPublish = 0;
        Address ip;
        uint16 aichHashIdx = kNoAICHHash;
    };

    float m_trustValue = 0.0f;
    std::vector<uint8> m_aichHashPopularity;
    std::vector<QByteArray> m_aichHashes;
    std::list<PublishingIP>* m_publishingIPs = nullptr;
    uint32 m_lastTrustValueCalc = 0;
    QString m_searchTermCache;

    /// Publish counts per /24 block (key = host-order IP masked with ~0xFF).
    /// Keying on the exact IP — as this previously did — would give anyone with
    /// a /24 a full 256 independent trust budgets. MFC Entry.cpp:327.
    static std::unordered_map<uint32, uint32> s_globalPublishIPs;
};

} // namespace eMule::kad
