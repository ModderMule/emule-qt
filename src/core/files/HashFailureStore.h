#pragma once

/// @file HashFailureStore.h
/// @brief Files that could not be hashed, remembered across restarts.
///
/// A file we gave up on (unreadable, locked for good) used to be retried
/// through the whole wait ladder after every start. `<ConfigDir>/hashfailures.dat`
/// keeps path, size and date; the file is left alone until one of them changes.

#include "utils/Types.h"

#include <QString>

#include <ctime>
#include <vector>

namespace eMule {

inline constexpr auto kHashFailureFileName = "hashfailures.dat";
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

} // namespace eMule
