#include "pch.h"
/// @file IPFilter.cpp
/// @brief IP range filter implementation — replaces MFC CIPFilter.

#include "ipfilter/IPFilter.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/TimeUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "utils/ByteOrder.h"

namespace eMule {

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

            // Skip comments and short lines
            if (line.empty() || line[0] == '#' || line[0] == '/' || line.size() < 15)
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

    if (!file.commit()) {
        logError(QStringLiteral("Failed to commit IP filter file: %1").arg(filePath));
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Filtering
// ---------------------------------------------------------------------------

bool IPFilter::isFiltered(uint32 ip) const
{
    return isFiltered(ip, kDefaultFilterLevel);
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
                ip, QString::fromStdString(it->desc));
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
    m_lastHit = nullptr;
    m_modified = false;
}

QString IPFilter::lastHitDescription() const
{
    if (m_lastHit)
        return QString::fromStdString(m_lastHit->desc);
    return QStringLiteral("Not available");
}

// ---------------------------------------------------------------------------
// Sort & merge
// ---------------------------------------------------------------------------

void IPFilter::sortAndMerge()
{
    if (m_entries.size() < 2)
        return;

    // Sort by start IP, then by level (lower/stricter first)
    std::sort(m_entries.begin(), m_entries.end(),
              [](const IPFilterEntry& a, const IPFilterEntry& b) {
                  if (a.start != b.start)
                      return a.start < b.start;
                  return a.level < b.level;
              });

    // Merge overlapping/adjacent ranges, splitting when levels differ
    std::vector<IPFilterEntry> merged;
    merged.reserve(m_entries.size());
    merged.push_back(std::move(m_entries[0]));

    for (size_t i = 1; i < m_entries.size(); ++i) {
        auto& cur = m_entries[i];
        auto& prev = merged.back();

        const bool overlapping = cur.start >= prev.start && cur.start <= prev.end;
        const bool adjacent = cur.start == prev.end + 1 && cur.level == prev.level;

        if (overlapping || adjacent) {
            if (cur.start == prev.start && cur.end == prev.end) {
                // Duplicate range: keep lowest (strictest) level
                if (cur.level < prev.level)
                    prev.level = cur.level;
            } else if (prev.level == cur.level) {
                // Same level: simply extend
                if (cur.end > prev.end)
                    prev.end = cur.end;
            } else if (overlapping) {
                // Overlapping entries with different levels — split into segments.
                // prev = [A..B, level_p], cur = [C..D, level_c], where A <= C <= B.
                const uint32 A = prev.start;
                const uint32 B = prev.end;
                const uint32 C = cur.start;
                const uint32 D = cur.end;
                const uint32 levelP = prev.level;
                const uint32 levelC = cur.level;
                const uint32 minLevel = std::min(levelP, levelC);

                // Remove prev — we'll re-add the split segments
                merged.pop_back();

                // Segment 1: [A..C-1] at prev's level (non-overlapping left part)
                if (A < C) {
                    merged.push_back({A, C - 1, levelP, 0, prev.desc});
                }

                // Segment 2: [C..min(B,D)] at the stricter (lower) level
                const uint32 overlapEnd = std::min(B, D);
                merged.push_back({C, overlapEnd, minLevel, 0, prev.desc});

                // Segment 3: the remainder beyond the overlap
                if (B > D) {
                    // prev extends beyond cur: [D+1..B] at prev's level
                    merged.push_back({D + 1, B, levelP, 0, prev.desc});
                } else if (D > B) {
                    // cur extends beyond prev: [B+1..D] at cur's level
                    merged.push_back({B + 1, D, levelC, 0, cur.desc});
                }
                // else B == D: no remainder
            }
            m_modified = true;
        } else {
            merged.push_back(std::move(cur));
        }
    }

    m_entries = std::move(merged);
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
