#pragma once

/// @file KadIndexed.h
/// @brief Keyword/source/notes index for DHT data storage.
///
/// Ported from kademlia/kademlia/Indexed.h.

#include "kademlia/KadEntry.h"
#include "kademlia/KadSearchDefs.h"
#include "kademlia/KadTypes.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUInt128.h"
#include "utils/Types.h"

#include <QMutex>
#include <QObject>
#include <QString>

#include <cstdint>
#include <ctime>

namespace eMule::kad {

/// Stores keywords, sources, and notes received from other DHT nodes.
class Indexed : public QObject {
    Q_OBJECT

public:
    explicit Indexed(QObject* parent = nullptr);
    ~Indexed() override;

    Indexed(const Indexed&) = delete;
    Indexed& operator=(const Indexed&) = delete;

    bool addKeyword(const UInt128& keyID, const UInt128& sourceID,
                    KeyEntry* entry, uint8& outLoad);
    bool addSources(const UInt128& keyID, const UInt128& sourceID,
                    Entry* entry, uint8& outLoad);
    bool addNotes(const UInt128& keyID, const UInt128& sourceID,
                  Entry* entry, uint8& outLoad);
    bool addLoad(const UInt128& keyID, time_t time);

    [[nodiscard]] uint32 getFileKeyCount() const;

    void sendValidKeywordResult(const UInt128& keyID, const SearchTerm* searchTerms,
                                uint32 ip, uint16 port, bool oldClient,
                                uint16 startPosition, const KadUDPKey& senderKey);
    void sendValidSourceResult(const UInt128& keyID, uint32 ip, uint16 port,
                               uint16 startPosition, uint64 fileSize,
                               const KadUDPKey& senderKey);
    void sendValidNoteResult(const UInt128& keyID, uint32 ip, uint16 port,
                             uint64 fileSize, const KadUDPKey& senderKey);
    bool sendStoreRequest(const UInt128& keyID);

    uint32 m_totalIndexSource = 0;
    uint32 m_totalIndexKeyword = 0;
    uint32 m_totalIndexNotes = 0;
    uint32 m_totalIndexLoad = 0;

private:
    void readFile();
    void clean();

    time_t m_nextClean = 0;
    KeyHashMap m_keywords;
    SrcHashMap m_sources;
    SrcHashMap m_notes;
    LoadMap m_loads;
    QMutex m_mutex;
    bool m_dataLoaded = false;
};

} // namespace eMule::kad
