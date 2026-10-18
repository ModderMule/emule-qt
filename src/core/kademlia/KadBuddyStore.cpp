/// @file KadBuddyStore.cpp
/// @brief Remembered Kad buddy nodes — implementation.

#include "pch.h"

#include "kademlia/KadBuddyStore.h"

#include "kademlia/KadIO.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadMiscUtils.h"
#include "utils/SafeFile.h"

#include <algorithm>

namespace eMule::kad {

void BuddyStore::load(const QString& path, std::time_t now)
{
    m_entries.clear();
    m_lastSessionVerdict = SessionVerdict::Unknown;

    std::vector<Entry> entries;
    uint8 verdict = 0;
    const auto readEntries = [&entries](SafeFile& file) {
        const uint32 count = file.readUInt32();
        if (count > kMaxEntries)
            return;
        for (uint32 i = 0; i < count; ++i) {
            Entry e;
            e.ip = file.readUInt32();
            e.udpPort = file.readUInt16();
            e.tcpPort = file.readUInt16();
            file.readHash16(e.userHash.data());
            e.kadId = io::readUInt128(file);
            e.connectOptions = file.readUInt8();
            e.lastOffered = static_cast<std::time_t>(file.readUInt64());
            e.lastAsked = static_cast<std::time_t>(file.readUInt64());
            e.lastConnected = static_cast<std::time_t>(file.readUInt64());
            entries.push_back(e);
        }
    };
    // v2: verdict byte, then the v1 body
    bool ok = readVersioned(path, kFileVersion, [&](SafeFile& file) {
        verdict = file.readUInt8();
        readEntries(file);
    });
    if (!ok) {
        entries.clear();
        verdict = 0;
        ok = readVersioned(path, kFileVersionV1, readEntries);
    }
    if (!ok)
        return;
    m_entries = std::move(entries);
    if (verdict <= static_cast<uint8>(SessionVerdict::Open))
        m_lastSessionVerdict = static_cast<SessionVerdict>(verdict);
    prune(now);
    logKad(QStringLiteral("Kad: loaded %1 remembered buddies, last session %2")
               .arg(m_entries.size())
               .arg(m_lastSessionVerdict == SessionVerdict::Firewalled ? QStringLiteral("firewalled")
                    : m_lastSessionVerdict == SessionVerdict::Open     ? QStringLiteral("open")
                                                                       : QStringLiteral("unknown")));
}

bool BuddyStore::save(const QString& path, std::time_t now)
{
    prune(now);
    return writeVersioned(path, kFileVersion, [this](SafeFile& file) {
        file.writeUInt8(static_cast<uint8>(m_lastSessionVerdict));
        file.writeUInt32(static_cast<uint32>(m_entries.size()));
        for (const Entry& e : m_entries) {
            file.writeUInt32(e.ip);
            file.writeUInt16(e.udpPort);
            file.writeUInt16(e.tcpPort);
            file.writeHash16(e.userHash.data());
            io::writeUInt128(file, e.kadId);
            file.writeUInt8(e.connectOptions);
            file.writeUInt64(static_cast<uint64>(e.lastOffered));
            file.writeUInt64(static_cast<uint64>(e.lastAsked));
            file.writeUInt64(static_cast<uint64>(e.lastConnected));
        }
    });
}

void BuddyStore::noteOffer(const Entry& offer, std::time_t now)
{
    auto it = std::ranges::find_if(m_entries, [&offer](const Entry& e) {
        return e.ip == offer.ip && e.udpPort == offer.udpPort;
    });
    if (it == m_entries.end()) {
        if (m_entries.size() >= kMaxEntries) {
            const auto oldest = std::ranges::min_element(m_entries, {}, &Entry::lastOffered);
            logKad(QStringLiteral("Kad: forgetting buddy %1 (oldest offer)").arg(ipToString(oldest->ip)));
            m_entries.erase(oldest);
        }
        m_entries.push_back(offer);
        it = std::prev(m_entries.end());
    } else {
        // Keep the history, refresh what the node just told us.
        const std::time_t asked = it->lastAsked;
        const std::time_t connected = it->lastConnected;
        *it = offer;
        it->lastAsked = asked;
        it->lastConnected = connected;
    }
    it->lastOffered = now;
    logKad(QStringLiteral("Kad: remembered buddy offer from %1:%2 (%3 stored)")
               .arg(ipToString(offer.ip)).arg(offer.udpPort).arg(m_entries.size()));
}

void BuddyStore::noteConnected(uint32 ip, uint16 tcpPort, std::time_t now)
{
    for (Entry& e : m_entries) {
        if (e.ip == ip && e.tcpPort == tcpPort)
            e.lastConnected = now;
    }
}

std::vector<BuddyStore::Entry> BuddyStore::takeDue(std::time_t now, bool ignoreReask)
{
    prune(now);

    std::vector<Entry*> due;
    for (Entry& e : m_entries) {
        if (ignoreReask || now - e.lastAsked >= kReaskSecs)
            due.push_back(&e);
    }
    // most recent buddy first: last link up, then last offer
    std::ranges::sort(due, [](const Entry* a, const Entry* b) {
        if (a->lastConnected != b->lastConnected)
            return a->lastConnected > b->lastConnected;
        return a->lastOffered > b->lastOffered;
    });
    if (due.size() > kAskPerSearch)
        due.resize(kAskPerSearch);

    std::vector<Entry> picked;
    picked.reserve(due.size());
    for (Entry* e : due) {
        e->lastAsked = now;
        picked.push_back(*e);
    }
    return picked;
}

void BuddyStore::prune(std::time_t now)
{
    std::erase_if(m_entries, [now](const Entry& e) {
        const bool missed = e.lastAsked > e.lastOffered && now - e.lastAsked >= kAnswerWindowSecs;
        const bool expired = now - e.lastOffered >= kExpireSecs;
        if (missed || expired)
            logKad(QStringLiteral("Kad: forgetting buddy %1:%2 (%3)")
                       .arg(ipToString(e.ip)).arg(e.udpPort)
                       .arg(missed ? QStringLiteral("no offer when asked") : QStringLiteral("expired")));
        return missed || expired;
    });
}

} // namespace eMule::kad
