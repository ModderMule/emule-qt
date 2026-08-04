#include "pch.h"
/// @file IPFilter.cpp
/// @brief IP range filter implementation — replaces MFC CIPFilter.

#include "ipfilter/IPFilter.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/TimeUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>



namespace eMule {

namespace {

// ---------------------------------------------------------------------------
// Range-key helpers — let one merge algorithm serve both families
// ---------------------------------------------------------------------------

using V6Key = std::array<uint8, 16>;

/// std::array compares lexicographically, which for network-order bytes is numeric
/// order, so the keys need no conversion — only successor/predecessor.
[[nodiscard]] uint32 keyNext(uint32 k) { return k + 1; }
[[nodiscard]] uint32 keyPrev(uint32 k) { return k - 1; }
[[nodiscard]] bool keyIsMax(uint32 k) { return k == UINT32_MAX; }
[[nodiscard]] bool keyIsMin(uint32 k) { return k == 0; }

[[nodiscard]] V6Key keyNext(V6Key k)
{
    for (int i = 15; i >= 0; --i) {
        if (++k[static_cast<size_t>(i)] != 0)
            break;      // no carry out of this byte
    }
    return k;
}

[[nodiscard]] V6Key keyPrev(V6Key k)
{
    for (int i = 15; i >= 0; --i) {
        if (k[static_cast<size_t>(i)]-- != 0)
            break;      // no borrow out of this byte
    }
    return k;
}

[[nodiscard]] bool keyIsMax(const V6Key& k)
{
    return std::all_of(k.begin(), k.end(), [](uint8 b) { return b == 0xFF; });
}

[[nodiscard]] bool keyIsMin(const V6Key& k)
{
    return std::all_of(k.begin(), k.end(), [](uint8 b) { return b == 0; });
}

// ---------------------------------------------------------------------------
// sortAndMergeRanges — shared by the IPv4 and IPv6 tables
// ---------------------------------------------------------------------------
//
// Sorts by start (then by level, stricter first) and merges. Overlaps between entries
// of *different* levels are split into segments rather than collapsed: the lookup only
// ever inspects the single entry with the largest start <= the address, so a range left
// nested inside another would never be found.
template <typename Entry>
void sortAndMergeRanges(std::vector<Entry>& entries, bool& modified)
{
    if (entries.size() < 2)
        return;

    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.start != b.start)
            return a.start < b.start;
        return a.level < b.level;
    });

    std::vector<Entry> merged;
    merged.reserve(entries.size());
    merged.push_back(std::move(entries[0]));

    for (size_t i = 1; i < entries.size(); ++i) {
        auto& cur = entries[i];
        auto& prev = merged.back();

        const bool overlapping = cur.start >= prev.start && cur.start <= prev.end;
        const bool adjacent = !keyIsMax(prev.end) && cur.start == keyNext(prev.end)
                              && cur.level == prev.level;

        if (!overlapping && !adjacent) {
            merged.push_back(std::move(cur));
            continue;
        }

        if (cur.start == prev.start && cur.end == prev.end) {
            // Duplicate range: keep the lowest (strictest) level
            if (cur.level < prev.level)
                prev.level = cur.level;
        } else if (prev.level == cur.level) {
            if (cur.end > prev.end)
                prev.end = cur.end;
        } else if (overlapping) {
            // prev = [A..B, levelP], cur = [C..D, levelC], with A <= C <= B.
            const auto a = prev.start;
            const auto b = prev.end;
            const auto c = cur.start;
            const auto d = cur.end;
            const uint32 levelP = prev.level;
            const uint32 levelC = cur.level;
            const uint32 minLevel = std::min(levelP, levelC);
            const std::string descP = prev.desc;

            merged.pop_back();

            if (a < c && !keyIsMin(c))
                merged.push_back(Entry{a, keyPrev(c), levelP, 0, descP});

            merged.push_back(Entry{c, std::min(b, d), minLevel, 0, descP});

            if (b > d && !keyIsMax(d))
                merged.push_back(Entry{keyNext(d), b, levelP, 0, descP});
            else if (d > b && !keyIsMax(b))
                merged.push_back(Entry{keyNext(b), d, levelC, 0, cur.desc});
        }
        modified = true;
    }

    entries = std::move(merged);
}

/// Zero every bit after @p prefixLen, giving the first address of the prefix.
[[nodiscard]] V6Key prefixFloor(V6Key addr, int prefixLen)
{
    for (int bit = prefixLen; bit < 128; ++bit)
        addr[static_cast<size_t>(bit / 8)] &= static_cast<uint8>(~(0x80u >> (bit % 8)));
    return addr;
}

/// Set every bit after @p prefixLen, giving the last address of the prefix.
[[nodiscard]] V6Key prefixCeiling(V6Key addr, int prefixLen)
{
    for (int bit = prefixLen; bit < 128; ++bit)
        addr[static_cast<size_t>(bit / 8)] |= static_cast<uint8>(0x80u >> (bit % 8));
    return addr;
}

/// Parse an IPv6 literal into 16 network-order bytes. Rejects IPv4 and hostnames.
[[nodiscard]] bool parseV6Literal(const std::string& text, V6Key& out)
{
    const Address addr = Address::fromString(
        QString::fromLatin1(text.c_str(), static_cast<qsizetype>(text.size())).trimmed());
    if (!addr.isIPv6())
        return false;
    out = addr.ipv6Bytes();
    return true;
}

[[nodiscard]] std::string trimmed(std::string s)
{
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.erase(s.begin());
    return s;
}

/// "2001:db8::/32", "a - b", or a bare literal → an inclusive range.
[[nodiscard]] bool parseV6RangeSpec(const std::string& spec, V6Key& start, V6Key& end)
{
    const std::string s = trimmed(spec);
    if (s.empty())
        return false;

    if (const auto slash = s.find('/'); slash != std::string::npos) {
        V6Key addr{};
        if (!parseV6Literal(s.substr(0, slash), addr))
            return false;
        const std::string lenText = trimmed(s.substr(slash + 1));
        if (lenText.empty()
            || lenText.find_first_not_of("0123456789") != std::string::npos)
            return false;
        const int prefixLen = std::atoi(lenText.c_str());
        if (prefixLen < 0 || prefixLen > 128)
            return false;
        start = prefixFloor(addr, prefixLen);
        end = prefixCeiling(addr, prefixLen);
        return true;
    }

    // "a - b". Only a dash flanked by whitespace can be a separator: a bare '-' is not
    // valid inside an IPv6 literal, but requiring the spaces keeps the intent explicit
    // and matches how every list in the wild writes it.
    if (const auto dash = s.find(" - "); dash != std::string::npos) {
        if (!parseV6Literal(s.substr(0, dash), start))
            return false;
        if (!parseV6Literal(s.substr(dash + 3), end))
            return false;
        return start <= end;
    }

    if (!parseV6Literal(s, start))
        return false;
    end = start;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

IPFilter::IPFilter(QObject* parent)
    : QObject(parent)
{
}

// ---------------------------------------------------------------------------
// Loading — public API
// ---------------------------------------------------------------------------

int IPFilter::loadFromDefaultFile(const QString& configDir)
{
    removeAllFilters();
    m_modified = false;
    const QString path = QDir(configDir).filePath(
        QString::fromLatin1(kDefaultIPFilterFilename));
    return loadFromFile(path);
}

int IPFilter::loadFromFile(const QString& filePath)
{
    const auto startTime = getTickCount();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }

    enum class FileType { Unknown, FilterDat, PeerGuardian, PeerGuardian2 };
    FileType fileType = FileType::Unknown;

    // Detect format by extension
    const QFileInfo fi(filePath);
    const QString ext = fi.suffix().toLower();
    const QString baseName = fi.completeBaseName().toLower();

    if (ext == u"p2p" || (baseName == u"guarding.p2p" && ext == u"txt")) {
        fileType = FileType::PeerGuardian;
    } else if (ext == u"prefix") {
        fileType = FileType::FilterDat;
    } else {
        // Check for PeerGuardian2 binary header: 0xFF 0xFF 0xFF 0xFF 'P' '2' 'B'
        static constexpr uint8 p2bHeader[] = { 0xFF, 0xFF, 0xFF, 0xFF, 'P', '2', 'B' };
        char header[sizeof(p2bHeader)];
        if (file.peek(header, sizeof(header)) == sizeof(header)
            && std::memcmp(header, p2bHeader, sizeof(p2bHeader)) == 0) {
            fileType = FileType::PeerGuardian2;
            file.skip(sizeof(p2bHeader)); // consume header
        }
    }

    int foundRanges = 0;
    int lineCount = 0;

    if (fileType == FileType::PeerGuardian2) {
        // Read version byte
        uint8 version = 0;
        if (file.read(reinterpret_cast<char*>(&version), 1) != 1
            || (version != 1 && version != 2)) {
            return 0;
        }
        // PeerGuardian2 has no standardised IPv6 record — both published versions store a
        // pair of 32-bit bounds. A v6 list therefore has to arrive in one of the text
        // formats; say so rather than silently filtering nothing.
        logInfo(QStringLiteral("IP filter: \"%1\" is PeerGuardian2 binary (IPv4 only) — "
                               "IPv6 ranges must come from a text list")
                    .arg(filePath));

        while (!file.atEnd()) {
            // Read null-terminated name
            std::string name;
            char ch;
            while (file.read(&ch, 1) == 1) {
                if (ch == '\0')
                    break;
                name += ch;
            }

            uint32 uStart = 0;
            uint32 uEnd = 0;
            if (file.read(reinterpret_cast<char*>(&uStart), 4) != 4)
                break;
            if (file.read(reinterpret_cast<char*>(&uEnd), 4) != 4)
                break;

            uStart = ntohl(uStart);
            uEnd = ntohl(uEnd);

            ++lineCount;
            addIPRange(uStart, uEnd, kDefaultFilterLevel, name);
            ++foundRanges;
        }
    } else {
        // Text-based formats
        while (!file.atEnd()) {
            const QByteArray rawLine = file.readLine(4096);
            ++lineCount;

            std::string line = rawLine.toStdString();
            // Trim whitespace
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'
                   || line.back() == ' ' || line.back() == '\t'))
                line.pop_back();
            while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
                line.erase(line.begin());

            // Skip comments
            if (line.empty() || line[0] == '#' || line[0] == '/')
                continue;

            // IPv6 entries are recognised on their own, before the IPv4 length guard and
            // outside the v4 format-detection state machine below: "2001:db8::/32" is only
            // 13 characters, and a mixed-family list must not have its v4 detection thrown
            // off by a v6 line it cannot parse.
            {
                std::array<uint8, 16> v6Start{};
                std::array<uint8, 16> v6End{};
                uint32 v6Level = kDefaultFilterLevel;
                std::string v6Desc;
                if (parseIPv6Line(line, v6Start, v6End, v6Level, v6Desc)) {
                    addIPRange6(v6Start, v6End, v6Level, v6Desc);
                    ++foundRanges;
                    continue;
                }
            }

            // Skip lines too short to hold an IPv4 range
            if (line.size() < 15)
                continue;

            // Auto-detect format if unknown — keep trying on each line until detected
            if (fileType == FileType::Unknown) {
                // Strip HTML tags if present
                auto gt = line.find('>');
                auto lt = line.find('<');
                if (gt != std::string::npos && lt != std::string::npos) {
                    auto lastGt = line.rfind('>');
                    if (lastGt != std::string::npos)
                        line.erase(0, lastGt + 1);
                }

                unsigned u1, u2, u3, u4, u5, u6, u7, u8;
                if (std::sscanf(line.c_str(),
                        "%3u.%3u.%3u.%3u - %3u.%3u.%3u.%3u",
                        &u1, &u2, &u3, &u4, &u5, &u6, &u7, &u8) == 8) {
                    fileType = FileType::FilterDat;
                } else {
                    auto colon = line.find(':');
                    if (colon != std::string::npos) {
                        if (std::sscanf(line.c_str() + colon + 1,
                                "%3u.%3u.%3u.%3u - %3u.%3u.%3u.%3u",
                                &u1, &u2, &u3, &u4, &u5, &u6, &u7, &u8) == 8) {
                            fileType = FileType::PeerGuardian;
                        }
                    }
                }
                // Format still unknown — skip this line and try detection on the next
                if (fileType == FileType::Unknown)
                    continue;
            }

            uint32 start = 0, end = 0, level = 0;
            std::string desc;
            bool valid = false;

            if (fileType == FileType::FilterDat)
                valid = parseFilterDatLine(line, start, end, level, desc);
            else if (fileType == FileType::PeerGuardian)
                valid = parsePeerGuardianLine(line, start, end, level, desc);

            // If extension-based format fails on the first qualifying line,
            // reset to Unknown so auto-detection retries on the next line
            if (!valid && foundRanges == 0 && fileType != FileType::Unknown) {
                fileType = FileType::Unknown;
                continue;
            }

            if (valid) {
                addIPRange(start, end, level, desc);
                ++foundRanges;
            }
        }
    }

    sortAndMerge();

    const auto elapsed = getTickCount() - startTime;
    logInfo(QStringLiteral("Loaded %1 IP filter entries from \"%2\" (%3 ranges found, %4 lines, %5 ms)")
                .arg(entryCount())
                .arg(filePath)
                .arg(foundRanges)
                .arg(lineCount)
                .arg(elapsed));

    emit filterLoaded(entryCount());
    return entryCount();
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

bool IPFilter::saveToFile(const QString& filePath) const
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logError(QStringLiteral("Failed to open IP filter file for writing: %1").arg(filePath));
        return false;
    }

    for (const auto& entry : m_entries) {
        // Convert host-order IPs to dotted decimal via network byte order
        const uint32 netStart = htonl(entry.start);
        const uint32 netEnd = htonl(entry.end);
        const QByteArray startStr = ipstr(netStart).toLatin1();
        const QByteArray endStr = ipstr(netEnd).toLatin1();

        char buf[256];
        std::snprintf(buf, sizeof(buf), "%-15s - %-15s , %3u , %s\n",
                      startStr.constData(), endStr.constData(),
                      entry.level, entry.desc.c_str());
        file.write(buf);
    }

    // IPv6 ranges in the same shape, so parseIPv6Line reads back what we wrote. Written
    // as explicit bounds rather than CIDR: a merged range is not necessarily a prefix.
    for (const auto& entry : m_entries6) {
        const QByteArray startStr =
            Address::fromIPv6Bytes(entry.start.data()).toString().toLatin1();
        const QByteArray endStr =
            Address::fromIPv6Bytes(entry.end.data()).toString().toLatin1();

        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s - %s , %3u , %s\n",
                      startStr.constData(), endStr.constData(),
                      entry.level, entry.desc.c_str());
        file.write(buf);
    }

    if (!file.commit()) {
        logError(QStringLiteral("Failed to commit IP filter file: %1").arg(filePath));
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Filtering
// ---------------------------------------------------------------------------

// The level-less overloads take the *user's* filter level, not kDefaultFilterLevel.
// The two constants are unrelated and must not be confused: kDefaultFilterLevel is the
// level assigned to a list entry that carries no level column, while the preference is
// the threshold those entries are tested against.  Feeding the entry default back in as
// the threshold makes the test `level < filterLevel` read `100 < 100` for every such
// entry — which is every bare-CIDR line, every PeerGuardian line and every .p2b record —
// so the whole list would load and then block nothing.  MFC delegates the same way
// (srchybrid/IPFilter.cpp:378-380).
bool IPFilter::isFiltered(uint32 ip) const
{
    return isFiltered(ip, thePrefs.ipFilterLevel());
}

bool IPFilter::isFiltered(const Address& addr, uint32 filterLevel) const
{
    if (addr.isIPv4())
        return isFiltered(addr.toNetworkUint32(), filterLevel);
    if (addr.isIPv6())
        return isFilteredV6(addr, filterLevel);
    return false;   // null address
}

bool IPFilter::isFilteredV6(const Address& addr, uint32 filterLevel) const
{
    if (m_entries6.empty())
        return false;

    const std::array<uint8, 16> key = addr.ipv6Bytes();

    // Same shape as the IPv4 lookup: find the first entry starting after the address,
    // step back one, and test that single candidate. sortAndMergeRanges guarantees no
    // entry is nested inside another, which is what makes one candidate sufficient.
    auto it = std::upper_bound(m_entries6.begin(), m_entries6.end(), key,
        [](const std::array<uint8, 16>& val, const IPFilterEntry6& entry) {
            return val < entry.start;
        });

    if (it != m_entries6.begin()) {
        --it;
        if (key >= it->start && key <= it->end && it->level < filterLevel) {
            it->hits++;
            m_lastHit6 = &(*it);
            emit const_cast<IPFilter*>(this)->ipBlocked(
                addr, QString::fromStdString(it->desc));
            return true;
        }
    }

    return false;
}

bool IPFilter::isFiltered(const Address& addr) const
{
    return isFiltered(addr, thePrefs.ipFilterLevel());
}

bool IPFilter::isFiltered(uint32 ip, uint32 filterLevel) const
{
    if (m_entries.empty() || ip == 0)
        return false;

    // Convert from network byte order to host byte order
    const uint32 hostIP = ntohl(ip);

    // Binary search: find the first entry whose start > hostIP, then check
    // the entry before it (or equal).
    auto it = std::upper_bound(m_entries.begin(), m_entries.end(), hostIP,
        [](uint32 val, const IPFilterEntry& entry) {
            return val < entry.start;
        });

    if (it != m_entries.begin()) {
        --it;
        if (hostIP >= it->start && hostIP <= it->end && it->level < filterLevel) {
            it->hits++;
            m_lastHit = &(*it);
            emit const_cast<IPFilter*>(this)->ipBlocked(
                Address::fromNetworkOrder(ip), QString::fromStdString(it->desc));
            return true;
        }
    }

    return false;
}

// ---------------------------------------------------------------------------
// Modification
// ---------------------------------------------------------------------------

void IPFilter::addIPRange(uint32 start, uint32 end, uint32 level,
                          const std::string& desc)
{
    m_entries.push_back(IPFilterEntry{start, end, level, 0, desc});
    m_modified = true;
}

void IPFilter::addIPRange6(const std::array<uint8, 16>& start,
                           const std::array<uint8, 16>& end, uint32 level,
                           const std::string& desc)
{
    if (end < start)
        return;
    m_entries6.push_back(IPFilterEntry6{start, end, level, 0, desc});
    m_modified = true;
}

bool IPFilter::removeFilter(int index)
{
    if (index < 0 || index >= static_cast<int>(m_entries.size()))
        return false;

    // If last hit points at this entry, clear it
    if (m_lastHit == &m_entries[static_cast<size_t>(index)])
        m_lastHit = nullptr;

    m_entries.erase(m_entries.begin() + index);
    m_modified = true;
    return true;
}

void IPFilter::removeAllFilters()
{
    m_entries.clear();
    m_entries6.clear();
    m_lastHit = nullptr;
    m_lastHit6 = nullptr;
    m_modified = false;
}

QString IPFilter::lastHitDescription() const
{
    if (m_lastHit)
        return QString::fromStdString(m_lastHit->desc);
    if (m_lastHit6)
        return QString::fromStdString(m_lastHit6->desc);
    return QStringLiteral("Not available");
}

// ---------------------------------------------------------------------------
// Sort & merge
// ---------------------------------------------------------------------------

void IPFilter::sortAndMerge()
{
    sortAndMergeRanges(m_entries, m_modified);
    sortAndMergeRanges(m_entries6, m_modified);
    // The tables are rebuilt, so any cached hit pointer now dangles.
    m_lastHit = nullptr;
    m_lastHit6 = nullptr;
}

// ---------------------------------------------------------------------------
// Line parsers (private, static)
// ---------------------------------------------------------------------------

bool IPFilter::parseFilterDatLine(const std::string& line, uint32& ip1,
                                  uint32& ip2, uint32& level, std::string& desc)
{
    unsigned u1, u2, u3, u4, u5, u6, u7, u8;
    unsigned uLevel = kDefaultFilterLevel;
    int descStart = 0;

    const int items = std::sscanf(line.c_str(),
        "%3u.%3u.%3u.%3u - %3u.%3u.%3u.%3u , %3u , %n",
        &u1, &u2, &u3, &u4, &u5, &u6, &u7, &u8, &uLevel, &descStart);

    if (items < 8)
        return false;

    // Store in host byte order (big-endian IP to uint32)
    ip1 = (u1 << 24) | (u2 << 16) | (u3 << 8) | u4;
    ip2 = (u5 << 24) | (u6 << 16) | (u7 << 8) | u8;

    if (items == 8) {
        level = kDefaultFilterLevel;
        return true;
    }

    level = uLevel;

    if (descStart > 0 && descStart < static_cast<int>(line.size())) {
        desc = line.substr(static_cast<size_t>(descStart));
        // Trim trailing control chars
        while (!desc.empty() && static_cast<unsigned char>(desc.back()) < ' ')
            desc.pop_back();
    }

    return true;
}

bool IPFilter::parseIPv6Line(const std::string& line, std::array<uint8, 16>& start,
                             std::array<uint8, 16>& end, uint32& level, std::string& desc)
{
    // Cheap reject first: this runs on every line of a v4 list, which never has a colon
    // in the address position. (A PeerGuardian v4 line does have a colon, but its address
    // half is dotted quad, so parseV6RangeSpec below rejects it.)
    if (line.find(':') == std::string::npos)
        return false;

    level = kDefaultFilterLevel;
    desc.clear();

    // Optional FilterDat-style tail: "<range> , <level> , <description>".
    std::string spec = line;
    if (const auto comma = spec.find(','); comma != std::string::npos) {
        const std::string tail = spec.substr(comma + 1);
        spec = spec.substr(0, comma);

        std::string levelText = tail;
        if (const auto comma2 = tail.find(','); comma2 != std::string::npos) {
            levelText = tail.substr(0, comma2);
            desc = trimmed(tail.substr(comma2 + 1));
        }
        levelText = trimmed(levelText);
        if (!levelText.empty()
            && levelText.find_first_not_of("0123456789") == std::string::npos) {
            level = static_cast<uint32>(std::atoi(levelText.c_str()));
        }
    }

    if (parseV6RangeSpec(spec, start, end))
        return true;

    // PeerGuardian style, "description:<range>". The description itself may contain
    // colons, and so does every IPv6 literal, so there is no unambiguous split — try the
    // whole string first (done above), then successively shorter suffixes. Trying longest
    // first matters: for a bare "2001:db8::/32" the suffix "db8::/32" is *also* a valid
    // address, and taking it would silently filter the wrong range.
    for (size_t colon = spec.find(':'); colon != std::string::npos;
         colon = spec.find(':', colon + 1)) {
        if (parseV6RangeSpec(spec.substr(colon + 1), start, end)) {
            std::string candidate = trimmed(spec.substr(0, colon));
            if (auto pos = candidate.find("PGIPDB"); pos != std::string::npos)
                candidate.erase(pos, 6);
            desc = trimmed(std::move(candidate));
            return true;
        }
    }

    return false;
}

bool IPFilter::parsePeerGuardianLine(const std::string& line, uint32& ip1,
                                     uint32& ip2, uint32& level, std::string& desc)
{
    // Format: "description:IP1 - IP2"
    const auto colon = line.rfind(':');
    if (colon == std::string::npos)
        return false;

    desc = line.substr(0, colon);
    // Remove "PGIPDB" marker if present
    if (auto pos = desc.find("PGIPDB"); pos != std::string::npos)
        desc.erase(pos, 6);
    // Trim whitespace
    while (!desc.empty() && (desc.back() == ' ' || desc.back() == '\t'))
        desc.pop_back();
    while (!desc.empty() && (desc.front() == ' ' || desc.front() == '\t'))
        desc.erase(desc.begin());

    unsigned u1, u2, u3, u4, u5, u6, u7, u8;
    if (std::sscanf(line.c_str() + colon + 1,
            "%3u.%3u.%3u.%3u - %3u.%3u.%3u.%3u",
            &u1, &u2, &u3, &u4, &u5, &u6, &u7, &u8) != 8) {
        return false;
    }

    ip1 = (u1 << 24) | (u2 << 16) | (u3 << 8) | u4;
    ip2 = (u5 << 24) | (u6 << 16) | (u7 << 8) | u8;
    level = kDefaultFilterLevel;

    return true;
}

} // namespace eMule
