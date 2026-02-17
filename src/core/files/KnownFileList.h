#pragma once

/// @file KnownFileList.h
/// @brief Known file database — port of MFC CKnownFileList.
///
/// Manages known.met and cancelled.met persistence, hash-based lookup
/// of completed files. Thread-safe for read operations.

#include "utils/Types.h"

#include <QString>

#include <array>
#include <ctime>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace eMule {

class KnownFile;
class FileDataIO;

// ---------------------------------------------------------------------------
// MD4Key — lightweight 16-byte hash wrapper for use as unordered_map key
// ---------------------------------------------------------------------------

struct MD4Key {
    std::array<uint8, 16> data{};

    MD4Key() = default;

    explicit MD4Key(const uint8* hash)
    {
        std::memcpy(data.data(), hash, 16);
    }

    friend bool operator==(const MD4Key& a, const MD4Key& b)
    {
        return a.data == b.data;
    }
};

} // namespace eMule

template <>
struct std::hash<eMule::MD4Key> {
    std::size_t operator()(const eMule::MD4Key& key) const noexcept
    {
        // MD4 hashes are already well-distributed; use first 8 bytes
        uint64_t val = 0;
        std::memcpy(&val, key.data.data(), sizeof(val));
        return static_cast<std::size_t>(val);
    }
};

namespace eMule {

// ---------------------------------------------------------------------------
// KnownFileList
// ---------------------------------------------------------------------------

class KnownFileList {
public:
    KnownFileList();
    ~KnownFileList();

    bool init(const QString& configDir);
    void save();
    void clear();
    void process();

    bool safeAddKFile(KnownFile* file);
    KnownFile* findKnownFile(const QString& filename, time_t date, uint64 size) const;
    KnownFile* findKnownFileByID(const uint8* hash) const;
    KnownFile* findKnownFileByPath(const QString& path) const;
    bool isKnownFile(const KnownFile* file) const;
    bool isFilePtrInList(const KnownFile* file) const;

    void addCancelledFileID(const uint8* hash);
    bool isCancelledFileByID(const uint8* hash) const;

    [[nodiscard]] size_t count() const { return m_filesMap.size(); }

    uint64 totalTransferred = 0;
    uint32 totalRequested = 0;
    uint32 totalAccepted = 0;

private:
    bool loadKnownFiles();
    bool loadCancelledFiles();
    void saveCancelledFiles();
    MD4Key makeCancelledKey(const uint8* hash) const;

    std::unordered_map<MD4Key, KnownFile*> m_filesMap;
    std::unordered_set<MD4Key> m_cancelledFiles;
    uint32 m_cancelledSeed = 0;
    uint32 m_lastSaveTime = 0;
    QString m_configDir;
};

} // namespace eMule
