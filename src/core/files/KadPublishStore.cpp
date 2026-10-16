/// @file KadPublishStore.cpp
/// @brief Kad keyword publish times on disk — implementation.

#include "pch.h"

#include "files/KadPublishStore.h"

#include "utils/SafeFile.h"

namespace eMule {

void KadPublishStore::load(const QString& path)
{
    m_records.clear();
    m_dirty = false;

    // Nothing half-read: a damaged file only costs one round of publishing.
    std::map<kad::UInt128, Record> records;
    const bool ok = readVersioned(path, kKadPublishFileVersion, [&records](SafeFile& file) {
        const uint32 count = file.readUInt32();
        if (count > kKadPublishMaxRecords)
            return;
        for (uint32 i = 0; i < count; ++i) {
            uint8 id[16];
            file.readHash16(id);
            Record rec;
            rec.due = static_cast<time_t>(file.readUInt64());
            file.readHash16(rec.files.data());
            records.emplace(kad::UInt128(id), rec);
        }
    });
    if (ok)
        m_records = std::move(records);
}

bool KadPublishStore::save(const QString& path, time_t now)
{
    std::erase_if(m_records, [now](const auto& entry) { return entry.second.due <= now; });

    const bool ok = writeVersioned(path, kKadPublishFileVersion, [this](SafeFile& file) {
        file.writeUInt32(static_cast<uint32>(m_records.size()));
        for (const auto& [id, rec] : m_records) {
            uint8 raw[16];
            id.toByteArray(raw);
            file.writeHash16(raw);
            file.writeUInt64(static_cast<uint64>(rec.due));
            file.writeHash16(rec.files.data());
        }
    });
    if (ok)
        m_dirty = false;
    return ok;
}

std::optional<time_t> KadPublishStore::dueTime(const kad::UInt128& keywordID,
                                               const Fingerprint& files) const
{
    const auto it = m_records.find(keywordID);
    if (it == m_records.end() || it->second.files != files)
        return std::nullopt;
    return it->second.due;
}

void KadPublishStore::note(const kad::UInt128& keywordID, time_t due, const Fingerprint& files)
{
    m_records[keywordID] = Record{due, files};
    m_dirty = true;
}

void KadPublishStore::mix(Fingerprint& fingerprint, const uint8* fileHash)
{
    for (size_t i = 0; i < fingerprint.size(); ++i)
        fingerprint[i] ^= fileHash[i];
}

} // namespace eMule
