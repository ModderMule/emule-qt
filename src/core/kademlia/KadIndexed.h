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

#include <atomic>
#include <cstdint>
#include <ctime>

class QThread;

namespace eMule::kad {

/// Stores keywords, sources, and notes received from other DHT nodes.
class Indexed : public QObject {
    Q_OBJECT

public:
    /// In memory only: nothing is loaded and nothing is saved.
    explicit Indexed(QObject* parent = nullptr);
    /// Persistent: loads key_index.dat / src_index.dat / load_index.dat from
    /// @p configDir on a background thread and writes them back on destruction.
    /// Until the load finishes every public method refuses work, as MFC does.
    explicit Indexed(const QString& configDir, QObject* parent = nullptr);
    ~Indexed() override;

    [[nodiscard]] bool isLoaded() const { return m_dataLoaded.load(std::memory_order_acquire); }

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

    /// Drop expired entries. Runs at most every 30 min unless @p force.
    void clean(bool force = false);

    uint32 m_totalIndexSource = 0;
    uint32 m_totalIndexKeyword = 0;
    uint32 m_totalIndexNotes = 0;
    uint32 m_totalIndexLoad = 0;

private:
    /// Loader thread body: read the three index files (MFC CLoadDataThread::Run).
    void loadFiles();
    void loadLoadFile(const QString& path);
    void loadKeyFile(const QString& path);
    void loadSourceFile(const QString& path);
    /// Write the three index files; main thread, load finished.
    void writeFiles();
    [[nodiscard]] bool aborting() const { return m_abortLoading.load(std::memory_order_relaxed); }

    // Non-locking cores. `fromFile` keeps the stored lifetime instead of stamping a
    // fresh one — MFC's bIgnoreThreadLock paths.
    bool addKeywordLocked(const UInt128& keyID, const UInt128& sourceID,
                          KeyEntry* entry, uint8& outLoad, bool fromFile);
    bool addLoadLocked(const UInt128& keyID, time_t time);

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
                        Entry* entry, uint8& outLoad, bool fromFile = false);

    time_t m_nextClean = 0;
    time_t m_nextForcedClean = 0;   // a full index forces a clean, but not per packet
    KeyHashMap m_keywords;
    SrcHashMap m_sources;
    SrcHashMap m_notes;
    LoadMap m_loads;
    QMutex m_mutex;

    QString m_configDir;                       // empty = not persistent
    QThread* m_loader = nullptr;
    std::atomic<bool> m_abortLoading{false};
    std::atomic<bool> m_dataLoaded{true};      // false only while m_loader runs
};

} // namespace eMule::kad
