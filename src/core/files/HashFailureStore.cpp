/// @file HashFailureStore.cpp
/// @brief Hash-failure memory on disk — implementation.

#include "pch.h"

#include "files/HashFailureStore.h"

#include "app/AppContext.h"
#include "utils/Log.h"
#include "utils/SafeFile.h"

#include <QFile>
#include <QFileInfo>

namespace eMule {

namespace {

/// version byte, uint32 count, then @p readOne per record
template <typename Record, typename ReadOne>
std::vector<Record> readRecords(const QString& path, uint8 version, ReadOne readOne)
{
    if (!QFile::exists(path))
        return {};

    std::vector<Record> records;
    try {
        SafeFile file;
        if (!file.open(path, QIODevice::ReadOnly))
            return {};
        if (file.readUInt8() != version)
            return {};

        const uint32 count = file.readUInt32();
        if (count > kHashFailureMaxRecords)
            return {};

        records.reserve(count);
        for (uint32 i = 0; i < count; ++i)
            records.push_back(readOne(file));
    } catch (const std::exception& e) {
        logWarning(QStringLiteral("%1: read failed (%2) — ignoring the file")
                       .arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what())));
        return {};
    }
    return records;
}

template <typename Record, typename WriteOne>
bool writeRecords(const QString& path, uint8 version, const std::vector<Record>& records,
                  WriteOne writeOne)
{
    const QString name = QFileInfo(path).fileName();
    const QString tmpPath = path + QStringLiteral(".tmp");
    try {
        QFile::remove(tmpPath);

        SafeFile file;
        if (!file.open(tmpPath, QIODevice::WriteOnly)) {
            logWarning(QStringLiteral("%1: cannot write %2").arg(name, tmpPath));
            return false;
        }

        file.writeUInt8(version);
        file.writeUInt32(static_cast<uint32>(records.size()));
        for (const Record& rec : records)
            writeOne(file, rec);
        commitAndReplace(file, tmpPath, path, theApp.commitFilesNow());
    } catch (const std::exception& e) {
        logError(QStringLiteral("%1: save failed (%2)").arg(name, QString::fromUtf8(e.what())));
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

} // namespace

namespace HashFailureFile {

std::vector<HashFailureRecord> read(const QString& path)
{
    return readRecords<HashFailureRecord>(path, kHashFailureFileVersion, [](SafeFile& file) {
        HashFailureRecord rec;
        rec.directory = file.readString(true);
        rec.filename = file.readString(true);
        rec.size = file.readUInt64();
        rec.mtime = static_cast<time_t>(file.readUInt64());
        return rec;
    });
}

bool write(const QString& path, const std::vector<HashFailureRecord>& records)
{
    return writeRecords(path, kHashFailureFileVersion, records,
                        [](SafeFile& file, const HashFailureRecord& rec) {
        file.writeString(rec.directory, UTF8Mode::Raw);
        file.writeString(rec.filename, UTF8Mode::Raw);
        file.writeUInt64(rec.size);
        file.writeUInt64(static_cast<uint64>(rec.mtime));
    });
}

} // namespace HashFailureFile

namespace DuplicateFileStore {

std::vector<DuplicateRecord> read(const QString& path)
{
    return readRecords<DuplicateRecord>(path, kDuplicateFileVersion, [](SafeFile& file) {
        DuplicateRecord rec;
        rec.path = file.readString(true);
        rec.size = file.readUInt64();
        rec.mtime = static_cast<time_t>(file.readUInt64());
        uint8 hash[16];
        file.readHash16(hash);
        rec.fileHash = QByteArray(reinterpret_cast<const char*>(hash), 16);
        return rec;
    });
}

bool write(const QString& path, const std::vector<DuplicateRecord>& records)
{
    return writeRecords(path, kDuplicateFileVersion, records,
                        [](SafeFile& file, const DuplicateRecord& rec) {
        file.writeString(rec.path, UTF8Mode::Raw);
        file.writeUInt64(rec.size);
        file.writeUInt64(static_cast<uint64>(rec.mtime));
        file.writeHash16(reinterpret_cast<const uint8*>(rec.fileHash.constData()));
    });
}

} // namespace DuplicateFileStore

} // namespace eMule
