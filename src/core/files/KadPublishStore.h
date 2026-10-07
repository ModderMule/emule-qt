#pragma once

/// @file KadPublishStore.h
/// @brief Remembers when each Kad keyword is next due, across restarts.
///
/// Keyword publish times used to live in memory only, so every start published
/// every keyword again. `<ConfigDir>/kadpublish.dat` keeps, per keyword Kad id,
/// the next due time and a fingerprint of the files published under it. A
/// stored time is honoured only while the fingerprint still matches: a file
/// added or removed while we were offline makes the keyword due at once.

#include "kademlia/KadUInt128.h"
#include "utils/Types.h"

#include <QString>

#include <array>
#include <ctime>
#include <map>
#include <optional>

namespace eMule {

inline constexpr auto kKadPublishFileName = "kadpublish.dat";
inline constexpr uint8 kKadPublishFileVersion = 1;
/// Resave cadence while something changed.
inline constexpr time_t kKadPublishResaveSecs = 600;
/// More keywords than any share produces; a larger file is not ours.
inline constexpr uint32 kKadPublishMaxRecords = 500'000;

class KadPublishStore {
public:
    using Fingerprint = std::array<uint8, 16>;

    /// Read the file; a missing, truncated or foreign file leaves the store empty.
    void load(const QString& path);

    /// Write the records still due after @p now. Returns false when the write failed.
    bool save(const QString& path, time_t now);

    /// The stored due time, if there is one for exactly this file set.
    [[nodiscard]] std::optional<time_t> dueTime(const kad::UInt128& keywordID,
                                                const Fingerprint& files) const;

    void note(const kad::UInt128& keywordID, time_t due, const Fingerprint& files);

    [[nodiscard]] bool isDirty() const { return m_dirty; }
    [[nodiscard]] size_t count() const { return m_records.size(); }

    /// Fold one file hash into a fingerprint (order-independent).
    static void mix(Fingerprint& fingerprint, const uint8* fileHash);

private:
    struct Record {
        time_t due = 0;
        Fingerprint files{};
    };

    std::map<kad::UInt128, Record> m_records;
    bool m_dirty = false;
};

} // namespace eMule
