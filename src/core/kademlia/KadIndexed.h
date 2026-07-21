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
    // Non-locking core of clean(): caller must already hold m_mutex. Called from
    // the serve paths (which hold the lock) as well as clean() itself. Splitting
    // it out avoids re-locking the non-recursive m_mutex → deadlock.
    void cleanLocked();

    // How sources and notes differ. Everything else about inserting them is
    // identical, so the shared body below takes this instead of being forked.
    // MFC Indexed.cpp AddSources/AddNotes.
    struct SourcePolicy {
        // Is `stored` the same publisher as `incoming`? Sources match on
        // IP + (TCP or UDP) port, notes on IP *or* sourceID. Deduping on
        // sourceID alone — which is attacker-chosen — lets one host fill every
        // slot for a file just by rotating it.
        bool (*isSamePublisher)(const Entry& stored, const Entry& incoming);
        // Is `entry` well-formed enough to store? Sources need a full address
        // and an unexpired lifetime; notes only need an IP and some tags.
        bool (*isPublishable)(const Entry& entry);
    };

    static const SourcePolicy kSourcePolicy;
    static const SourcePolicy kNotePolicy;

    // Shared body for addSources/addNotes (identical except for the index map,
    // counter, per-file cap, lifetime, and the policy above). Non-locking: the
    // public wrappers hold m_mutex. addKeyword stays separate (merge semantics +
    // map container).
    bool addSourceEntry(SrcHashMap& index, uint32& counter, uint32 perFileMax,
                        time_t lifetimeSecs, const SourcePolicy& policy,
                        const UInt128& keyID, const UInt128& sourceID,
                        Entry* entry, uint8& outLoad);

    time_t m_nextClean = 0;
    KeyHashMap m_keywords;
    SrcHashMap m_sources;
    SrcHashMap m_notes;
    LoadMap m_loads;
    QMutex m_mutex;
    bool m_dataLoaded = false;
};

} // namespace eMule::kad
