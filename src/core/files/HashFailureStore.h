#pragma once

/// @file HashFailureStore.h
/// @brief Files that could not be hashed, remembered across restarts.
///
/// A file we gave up on (unreadable, locked for good) used to be retried
/// through the whole wait ladder after every start. `<ConfigDir>/hashfailures.dat`
/// keeps path, size and date; the file is left alone until one of them changes.
/// `<ConfigDir>/duplicates.dat` does the same for copies of a shared file.

#include "utils/Types.h"

#include <QByteArray>
#include <QString>

#include <ctime>
#include <vector>

namespace eMule {

inline constexpr auto kHashFailureFileName = "hashfailures.dat";
/// Same format, written by AICHSyncThread: files whose AICH hashset could not be built.
inline constexpr auto kAICHFailureFileName = "aichfailures.dat";
inline constexpr uint8 kHashFailureFileVersion = 1;
inline constexpr uint32 kHashFailureMaxRecords = 100'000;

struct HashFailureRecord {
    QString directory;
    QString filename;
    uint64 size = 0;
    time_t mtime = 0;
};

namespace HashFailureFile {

/// A missing, truncated or foreign file reads as empty.
[[nodiscard]] std::vector<HashFailureRecord> read(const QString& path);
bool write(const QString& path, const std::vector<HashFailureRecord>& records);

} // namespace HashFailureFile

/// Copies of a shared file that were not shared (same content, other path). Kept so a
/// restart does not hash each copy again (#130).
inline constexpr auto kDuplicateFileName = "duplicates.dat";
inline constexpr uint8 kDuplicateFileVersion = 1;

struct DuplicateRecord {
    QString path;
    uint64 size = 0;
    time_t mtime = 0;
    QByteArray fileHash;   // MD4 of the shared original, 16 bytes
};

namespace DuplicateFileStore {

/// A missing, truncated or foreign file reads as empty.
[[nodiscard]] std::vector<DuplicateRecord> read(const QString& path);
bool write(const QString& path, const std::vector<DuplicateRecord>& records);

} // namespace DuplicateFileStore

} // namespace eMule
