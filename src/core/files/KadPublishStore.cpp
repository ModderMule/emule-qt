/// @file KadPublishStore.cpp
/// @brief Kad keyword publish times on disk — implementation.

#include "pch.h"

#include "files/KadPublishStore.h"

#include "app/AppContext.h"
#include "utils/Log.h"
#include "utils/SafeFile.h"

#include <QFile>

namespace eMule {

void KadPublishStore::load(const QString& path)
{
    m_records.clear();
    m_dirty = false;

    if (!QFile::exists(path))
        return;

    std::map<kad::UInt128, Record> records;
    try {
        SafeFile file;
        if (!file.open(path, QIODevice::ReadOnly))
            return;
        if (file.readUInt8() != kKadPublishFileVersion)
            return;

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
    } catch (const std::exception& e) {
        // Nothing half-read: a damaged file only costs one round of publishing.
        logWarning(QStringLiteral("kadpublish.dat: read failed (%1) — ignoring the file")
                       .arg(QString::fromUtf8(e.what())));
        return;
    }
    m_records = std::move(records);
}

bool KadPublishStore::save(const QString& path, time_t now)
{
    std::erase_if(m_records, [now](const auto& entry) { return entry.second.due <= now; });

    const QString tmpPath = path + QStringLiteral(".tmp");
    try {
        QFile::remove(tmpPath);

        SafeFile file;
        if (!file.open(tmpPath, QIODevice::WriteOnly)) {
            logWarning(QStringLiteral("kadpublish.dat: cannot write %1").arg(tmpPath));
            return false;
        }

        file.writeUInt8(kKadPublishFileVersion);
        file.writeUInt32(static_cast<uint32>(m_records.size()));
        for (const auto& [id, rec] : m_records) {
            uint8 raw[16];
            id.toByteArray(raw);
            file.writeHash16(raw);
            file.writeUInt64(static_cast<uint64>(rec.due));
            file.writeHash16(rec.files.data());
        }
        commitAndReplace(file, tmpPath, path, theApp.commitFilesNow());
    } catch (const std::exception& e) {
        logError(QStringLiteral("kadpublish.dat: save failed (%1)")
                     .arg(QString::fromUtf8(e.what())));
        QFile::remove(tmpPath);
        return false;
    }

    m_dirty = false;
    return true;
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
