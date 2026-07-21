#pragma once

/// @file KadResultPacketWriter.h
/// @brief Header-only helpers shared by the three KadIndexed index types.
///
/// Factors out the parts of `eMule::kad::Indexed` (KadIndexed.cpp) that are
/// byte-for-byte identical across keywords/sources/notes:
///   - `ResultPacketSender` — the KADEMLIA2_SEARCH_RES packet header write +
///     anti-fragmentation flush + final flush that the three `sendValid*Result`
///     methods repeat verbatim.
///   - `cleanIndex` / `destroyIndex` — the nested expiry/teardown walk used by
///     `clean()` and the destructor, bridging the keyword map vs source/notes
///     list shape via the `derefSource`/`innerContainer` overloads.
///
/// All of these assume the caller already holds `Indexed::m_mutex`; none of them
/// lock. Kept header-only so they compile inside KadIndexed.cpp and stay directly
/// unit-testable without a socket or the Kademlia singleton.

#include "kademlia/KadEntry.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadTypes.h"
#include "kademlia/KadUInt128.h"
#include "utils/MapKey.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"
#include "utils/Types.h"

#include <ctime>
#include <functional>
#include <memory>
#include <utility>

namespace eMule::kad {

/// Accumulates KADEMLIA2_SEARCH_RES results into UDP packets, flushing on the
/// anti-fragmentation boundary. Packet layout: kadId(16) + keyID(16) +
/// uint16 count + N serialized results. The flush sink performs the actual send
/// (keeps the Kademlia singleton / socket out of this header so it is testable).
class ResultPacketSender {
public:
    using FlushSink = std::function<void(SafeMemFile& packet)>;

    ResultPacketSender(const UInt128& kadId, const UInt128& keyID, FlushSink sink)
        : m_kadId(kadId)
        , m_keyID(keyID)
        , m_sink(std::move(sink))
    {
        startPacket();
    }

    /// Append one already-serialized result (sourceID + tag list, written into
    /// `tmpBuf` by the caller). Flushes the current packet first if appending
    /// would exceed UDP_KAD_MAXFRAGMENT and at least one result is buffered.
    void addResult(const SafeMemFile& tmpBuf)
    {
        if (m_packet->length() + tmpBuf.length() > UDP_KAD_MAXFRAGMENT && m_unsentCount > 0) {
            flushCurrent();
            startPacket();
        }
        const auto& buf = tmpBuf.buffer();
        m_packet->write(buf.constData(), tmpBuf.length());
        ++m_unsentCount;
    }

    /// Final flush; call once after the iteration loop. No-op if nothing buffered.
    void flush()
    {
        if (m_unsentCount > 0)
            flushCurrent();
    }

private:
    void startPacket()
    {
        m_packet = std::make_unique<SafeMemFile>();
        io::writeUInt128(*m_packet, m_kadId);
        io::writeUInt128(*m_packet, m_keyID);
        m_countPos = m_packet->position();
        m_packet->writeUInt16(0);
        m_unsentCount = 0;
    }

    void flushCurrent()
    {
        auto endPos = m_packet->position();
        m_packet->seek(m_countPos, 0);
        m_packet->writeUInt16(m_unsentCount);
        m_packet->seek(endPos, 0);
        m_sink(*m_packet);
    }

    UInt128 m_kadId;
    UInt128 m_keyID;
    FlushSink m_sink;
    std::unique_ptr<SafeMemFile> m_packet;
    qint64 m_countPos = 0;
    uint16 m_unsentCount = 0;
};

// ---------------------------------------------------------------------------
// Container-shape bridges: keywords store sources in an unordered_map
// (KeyHash::mapSource), sources/notes in a list (SrcHash::sourceList). These
// tiny overloads let cleanIndex/destroyIndex walk both with one body.
// ---------------------------------------------------------------------------

inline Source* derefSource(const std::pair<const HashKeyOwn, Source*>& p) { return p.second; }
inline Source* derefSource(Source* s) { return s; }

inline auto& innerContainer(KeyHash* h) { return h->mapSource; }   // unordered_map
inline auto& innerContainer(SrcHash* h) { return h->sourceList; }  // list

/// No-op survivor hook (the default): keyword cleaning passes a real one to
/// prune each surviving entry's publisher-tracking list.
struct NoSurvivorHook {
    void operator()(Entry*) const {}
};

/// Prune entries whose lifetime has expired, erasing emptied sources and key
/// nodes; decrement `counter` per removed entry. Works for KeyHashMap (keywords)
/// and SrcHashMap (sources/notes). `onSurvivor` is invoked on each entry that is
/// kept (used to call KeyEntry::cleanUpTrackedPublishers on keywords). Caller
/// holds the lock.
template<class HashMap, class OnSurvivor = NoSurvivorHook>
void cleanIndex(HashMap& index, time_t now, uint32& counter, OnSurvivor onSurvivor = {})
{
    for (auto hashIt = index.begin(); hashIt != index.end(); ) {
        auto* hash = hashIt->second;
        auto& inner = innerContainer(hash);
        for (auto innerIt = inner.begin(); innerIt != inner.end(); ) {
            Source* source = derefSource(*innerIt);
            for (auto entIt = source->entryList.begin(); entIt != source->entryList.end(); ) {
                if ((*entIt)->m_lifetime > 0 && (*entIt)->m_lifetime < now) {
                    delete *entIt;
                    entIt = source->entryList.erase(entIt);
                    --counter;
                } else {
                    onSurvivor(*entIt);
                    ++entIt;
                }
            }
            if (source->entryList.empty()) {
                delete source;
                innerIt = inner.erase(innerIt);
            } else {
                ++innerIt;
            }
        }
        if (inner.empty()) {
            delete hash;
            hashIt = index.erase(hashIt);
        } else {
            ++hashIt;
        }
    }
}

/// Delete every entry/source/key node in the index (no counter, no erase — the
/// outer map clears itself when the owning member is destroyed). Used by ~Indexed.
template<class HashMap>
void destroyIndex(HashMap& index)
{
    for (auto& [key, hash] : index) {
        for (auto& innerElem : innerContainer(hash)) {
            Source* source = derefSource(innerElem);
            for (auto* entry : source->entryList)
                delete entry;
            delete source;
        }
        delete hash;
    }
}

} // namespace eMule::kad
