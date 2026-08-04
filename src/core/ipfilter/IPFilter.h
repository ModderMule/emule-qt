#pragma once

/// @file IPFilter.h
/// @brief IP range filter — replaces MFC CIPFilter.
///
/// Loads IP filter lists from disk (FilterDat, PeerGuardian text,
/// PeerGuardian2 binary), stores sorted ranges for O(log n) lookup,
/// and merges overlapping/adjacent ranges.

#include "net/Address.h"
#include "utils/Types.h"

#include <QObject>
#include <QString>

#include <array>
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

// ---------------------------------------------------------------------------
// IPFilterEntry6 — one blocked IPv6 range
// ---------------------------------------------------------------------------
//
// Kept in a table of its own rather than folded into IPFilterEntry through
// IPv4-mapped addresses: that would put v4 ranges under IPv6 comparison rules and
// silently change which IPv4 sources we accept, which the ipstr/isGoodIP split in
// net/Address.h already warns about. Two tables, one comparison rule each.
//
// Bounds are the 16 address bytes in network order, so the natural lexicographic
// ordering of std::array is numeric ordering — no conversion needed to sort or search.
struct IPFilterEntry6 {
    std::array<uint8, 16> start{};
    std::array<uint8, 16> end{};
    uint32 level = 100;
    mutable uint32 hits = 0;
    std::string desc;
};

/// Level given to a list entry whose line carries no level column (MFC
/// DFLT_FILTER_LEVEL, srchybrid/IPFilter.cpp:36).  This is an *entry* default, not the
/// threshold entries are compared against — that is thePrefs.ipFilterLevel().
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

    /// Convenience: check at the user's configured level, thePrefs.ipFilterLevel().
    /// Not kDefaultFilterLevel — see the note on the definition.
    [[nodiscard]] bool isFiltered(uint32 ip) const;

    /// Check if an Address is filtered at the given level. Dispatches to the per-family
    /// range table; both families are supported.
    [[nodiscard]] bool isFiltered(const Address& addr, uint32 filterLevel) const;

    /// Convenience: check Address at the user's configured level.
    [[nodiscard]] bool isFiltered(const Address& addr) const;

    // -- Modification ---------------------------------------------------------

    /// Add a single IP range (host byte order).
    void addIPRange(uint32 start, uint32 end, uint32 level,
                    const std::string& desc);

    /// Add a single IPv6 range. Bounds are 16 bytes in network order, @p start <= @p end.
    void addIPRange6(const std::array<uint8, 16>& start, const std::array<uint8, 16>& end,
                     uint32 level, const std::string& desc);

    /// Remove a specific filter entry by index. Returns true on success.
    bool removeFilter(int index);

    /// Remove all filter entries.
    void removeAllFilters();

    // -- Accessors ------------------------------------------------------------

    /// Total across both families — what the UI and the load log report.
    [[nodiscard]] int entryCount() const
    {
        return static_cast<int>(m_entries.size() + m_entries6.size());
    }
    [[nodiscard]] int entryCountV4() const { return static_cast<int>(m_entries.size()); }
    [[nodiscard]] int entryCountV6() const { return static_cast<int>(m_entries6.size()); }
    [[nodiscard]] bool isEmpty() const { return m_entries.empty() && m_entries6.empty(); }
    [[nodiscard]] bool isModified() const { return m_modified; }

    [[nodiscard]] const std::vector<IPFilterEntry>& entries() const { return m_entries; }
    [[nodiscard]] const std::vector<IPFilterEntry6>& entries6() const { return m_entries6; }

    [[nodiscard]] QString lastHitDescription() const;

    /// Sort by start IP and merge overlapping/adjacent ranges.
    void sortAndMerge();

signals:
    /// Emitted after a filter file is loaded.
    void filterLoaded(int count);

    /// Emitted when an IP is blocked. Address-typed so IPv6 hits can be reported too.
    void ipBlocked(const Address& addr, const QString& description);

private:
    static bool parseFilterDatLine(const std::string& line, uint32& ip1,
                                   uint32& ip2, uint32& level, std::string& desc);
    static bool parsePeerGuardianLine(const std::string& line, uint32& ip1,
                                      uint32& ip2, uint32& level, std::string& desc);
    /// Recognises an IPv6 entry in any of the shapes real lists use — `2001:db8::/32`,
    /// `start - end`, a bare literal, each optionally preceded by `description:` and
    /// followed by `, level , description`. Returns false for anything else, including
    /// every IPv4 line, so it can be tried first without disturbing v4 detection.
    static bool parseIPv6Line(const std::string& line, std::array<uint8, 16>& start,
                              std::array<uint8, 16>& end, uint32& level, std::string& desc);

    [[nodiscard]] bool isFilteredV6(const Address& addr, uint32 filterLevel) const;

    std::vector<IPFilterEntry> m_entries;
    std::vector<IPFilterEntry6> m_entries6;
    mutable const IPFilterEntry* m_lastHit = nullptr;
    mutable const IPFilterEntry6* m_lastHit6 = nullptr;
    bool m_modified = false;
};

} // namespace eMule
