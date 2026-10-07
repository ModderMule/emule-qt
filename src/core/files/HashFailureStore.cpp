/// @file HashFailureStore.cpp
/// @brief Hash-failure memory on disk — implementation.

#include "pch.h"

#include "files/HashFailureStore.h"

#include "app/AppContext.h"
#include "utils/Log.h"
#include "utils/SafeFile.h"

#include <QFile>

namespace eMule::HashFailureFile {

std::vector<HashFailureRecord> read(const QString& path)
{
    if (!QFile::exists(path))
        return {};

    std::vector<HashFailureRecord> records;
    try {
        SafeFile file;
        if (!file.open(path, QIODevice::ReadOnly))
            return {};
        if (file.readUInt8() != kHashFailureFileVersion)
            return {};

        const uint32 count = file.readUInt32();
        if (count > kHashFailureMaxRecords)
            return {};

        records.reserve(count);
        for (uint32 i = 0; i < count; ++i) {
            HashFailureRecord rec;
            rec.directory = file.readString(true);
            rec.filename = file.readString(true);
            rec.size = file.readUInt64();
            rec.mtime = static_cast<time_t>(file.readUInt64());
            records.push_back(std::move(rec));
        }
    } catch (const std::exception& e) {
        logWarning(QStringLiteral("hashfailures.dat: read failed (%1) — ignoring the file")
                       .arg(QString::fromUtf8(e.what())));
        return {};
    }
    return records;
}

bool write(const QString& path, const std::vector<HashFailureRecord>& records)
{
    const QString tmpPath = path + QStringLiteral(".tmp");
    try {
        QFile::remove(tmpPath);

        SafeFile file;
        if (!file.open(tmpPath, QIODevice::WriteOnly)) {
            logWarning(QStringLiteral("hashfailures.dat: cannot write %1").arg(tmpPath));
            return false;
        }

        file.writeUInt8(kHashFailureFileVersion);
        file.writeUInt32(static_cast<uint32>(records.size()));
        for (const HashFailureRecord& rec : records) {
            file.writeString(rec.directory, UTF8Mode::Raw);
            file.writeString(rec.filename, UTF8Mode::Raw);
            file.writeUInt64(rec.size);
            file.writeUInt64(static_cast<uint64>(rec.mtime));
        }
        commitAndReplace(file, tmpPath, path, theApp.commitFilesNow());
    } catch (const std::exception& e) {
        logError(QStringLiteral("hashfailures.dat: save failed (%1)")
                     .arg(QString::fromUtf8(e.what())));
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

} // namespace eMule::HashFailureFile
