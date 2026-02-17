#pragma once

/// @file IPFilter.h
/// @brief IP range filter — replaces MFC CIPFilter.
///
/// Loads IP filter lists from disk (FilterDat, PeerGuardian text,
/// PeerGuardian2 binary), stores sorted ranges for O(log n) lookup,
/// and merges overlapping/adjacent ranges.

#include "utils/Types.h"

#include <QObject>
#include <QString>

#include <string>
#include <vector>

namespace eMule {

// ---------------------------------------------------------------------------
// IPFilterEntry — one blocked IP range (value type, replaces SIPFilter*)
// ---------------------------------------------------------------------------

struct IPFilterEntry {
    uint32 start = 0;       // Start IP (host byte order)
    uint32 end   = 0;       // End IP (host byte order)
    uint32 level = 100;     // Filter level (lower = more restrictive)
    mutable uint32 hits = 0; // Hit counter (logically mutable during lookup)
    std::string desc;       // ASCII description
};

inline constexpr uint32 kDefaultFilterLevel = 100;
inline constexpr auto kDefaultIPFilterFilename = "ipfilter.dat";

// ---------------------------------------------------------------------------
// IPFilter — QObject-based IP filter (replaces MFC CIPFilter)
// ---------------------------------------------------------------------------

class IPFilter : public QObject {
    Q_OBJECT

public:
    explicit IPFilter(QObject* parent = nullptr);
    ~IPFilter() override = default;

    // -- Loading & persistence ------------------------------------------------

    /// Load filter entries from a file. Returns number of entries loaded.
    /// Supports FilterDat (.dat/.prefix), PeerGuardian text (.p2p),
    /// and PeerGuardian2 binary formats.
    int loadFromFile(const QString& filePath);

    /// Clear and reload from the default ipfilter.dat in configDir.
    int loadFromDefaultFile(const QString& configDir);

    /// Save current filter list to a file in FilterDat format.
    bool saveToFile(const QString& filePath) const;

    // -- Filtering ------------------------------------------------------------

    /// Check if an IP (network byte order) is filtered at the given level.
    /// Returns true if the IP falls in a range with level < filterLevel.
    [[nodiscard]] bool isFiltered(uint32 ip, uint32 filterLevel) const;

    /// Convenience: check using default level (100).
    [[nodiscard]] bool isFiltered(uint32 ip) const;

    // -- Modification ---------------------------------------------------------

    /// Add a single IP range (host byte order).
    void addIPRange(uint32 start, uint32 end, uint32 level,
                    const std::string& desc);

    /// Remove a specific filter entry by index. Returns true on success.
    bool removeFilter(int index);

    /// Remove all filter entries.
    void removeAllFilters();

    // -- Accessors ------------------------------------------------------------

    [[nodiscard]] int entryCount() const { return static_cast<int>(m_entries.size()); }
    [[nodiscard]] bool isEmpty() const { return m_entries.empty(); }
    [[nodiscard]] bool isModified() const { return m_modified; }

    [[nodiscard]] const std::vector<IPFilterEntry>& entries() const { return m_entries; }

    [[nodiscard]] QString lastHitDescription() const;

    /// Sort by start IP and merge overlapping/adjacent ranges.
    void sortAndMerge();

signals:
    /// Emitted after a filter file is loaded.
    void filterLoaded(int count);

    /// Emitted when an IP is blocked (network byte order IP, description).
    void ipBlocked(uint32 ip, const QString& description);

private:
    static bool parseFilterDatLine(const std::string& line, uint32& ip1,
                                   uint32& ip2, uint32& level, std::string& desc);
    static bool parsePeerGuardianLine(const std::string& line, uint32& ip1,
                                      uint32& ip2, uint32& level, std::string& desc);

    std::vector<IPFilterEntry> m_entries;
    mutable const IPFilterEntry* m_lastHit = nullptr;
    bool m_modified = false;
};

} // namespace eMule
