#include "pch.h"
/// @file ClientAnalyzer.cpp
/// @brief Behaviour score per user hash, port of WiZaRd's ClientAnalyzer (eMuleFuture v1.5).

#include "client/ClientAnalyzer.h"

#include "client/ClientCredits.h"
#include "protocol/Tag.h"
#include "utils/BackgroundFileWriter.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QStringList>

#include <algorithm>
#include <cstring>
#include <ctime>

namespace eMule {

namespace {

// Score steps, ClientAnalyzer.h AT_*
constexpr float kBaseScore = 1.0f;
constexpr float kHighPunish = 0.5f;
constexpr float kMidPunish = 0.1f;
constexpr float kLowPunish = 0.025f;
constexpr float kMinScore = 0.01f;
constexpr float kMaxScore = 10.0f;
constexpr double kRoundOffset = 0.2;     // 80 % of a chunk counts as one
constexpr double kFullFileWeight = 1.5;  // full-file downloads from it count extra
constexpr double kPartFileWeight = 1.5;  // part-file uploads to it count extra
constexpr double kRareShareWeight = 1.0; // rare-file downloads from it count extra

constexpr uint8 kFileVersion = 0x01;

// Record tags
enum : uint8 {
    TagLastSeen = 1,
    TagBadSince,
    TagFlags,
    TagReasks,
    TagAvgReask,
    TagSpams,
    TagXsAsks,
    TagXsAnswers,
    TagFastXs,
    TagBadUl,
    TagBadDl,
    TagSent,
    TagSentPartial,
    TagGot,
    TagGotPartial,
    TagGotRare,
};

uint64 chunks(uint64 bytes)
{
    return static_cast<uint64>(static_cast<double>(bytes) / PARTSIZE + kRoundOffset);
}

int64 unixNow()
{
    return static_cast<int64>(std::time(nullptr));
}

} // namespace

// ===========================================================================
// AnalyzerRecord
// ===========================================================================

void AnalyzerRecord::addUploaded(uint64 bytes, bool partial)
{
    sent += bytes;
    if (partial)
        sentPartial += bytes;
}

void AnalyzerRecord::addDownloaded(uint64 bytes, bool partial, uint32 sourceCount)
{
    got += bytes;
    if (partial)
        gotPartial += bytes;
    if (sourceCount < kRareFile)
        gotRare += bytes;
}

void AnalyzerRecord::addUlSession(bool bad)
{
    if (!bad)
        badUlSessions = 0;
    else if (badUlSessions < 255)
        ++badUlSessions;
}

void AnalyzerRecord::addDlSession(bool bad)
{
    if (!bad)
        badDlSessions = 0;
    else if (badDlSessions < 255)
        ++badDlSessions;
}

bool AnalyzerRecord::noteReask(uint32 intervalMs, uint32 reaskTimeMs)
{
    const bool before = isFastAsker(reaskTimeMs);
    const uint64 sum = static_cast<uint64>(avgReaskMs) * reasks + intervalMs;
    ++reasks;
    avgReaskMs = static_cast<uint32>(sum / reasks);
    return !before && isFastAsker(reaskTimeMs);
}

bool AnalyzerRecord::incXsAsks()
{
    const bool before = isXsExploiter();
    ++xsAsks;
    return !before && isXsExploiter();
}

bool AnalyzerRecord::incXsAnswers()
{
    const bool before = isXsExploiter();
    ++xsAnswers;
    return before && !isXsExploiter();
}

bool AnalyzerRecord::setFlag(uint32 flag, int64 now)
{
    badSince = static_cast<uint32>(now);   // every hit restarts the hold
    if (flags & flag)
        return false;
    flags |= flag;
    return true;
}

bool AnalyzerRecord::clearFlag(uint32 flag, int64 now)
{
    // Held: a peer that saw it was caught cannot clear itself by a clean hello
    if (!(flags & flag) || isHeldBad(now))
        return false;
    flags &= ~flag;
    return true;
}

bool AnalyzerRecord::isFastAsker(uint32 reaskTimeMs) const
{
    return reasks > 2 && avgReaskMs != 0 && avgReaskMs + kReaskBufferMs < reaskTimeMs;
}

bool AnalyzerRecord::isXsExploiter() const
{
    return xsAsks > kXsExploitLevel && xsAsks > kXsExploitFactor * xsAnswers;
}

uint64 AnalyzerRecord::uploadedWeighted() const
{
    // Rare uploads are not weighted: their downloaders help spread them (ClientAnalyzer v1.3)
    return (sent - std::min(sentPartial, sent))
         + static_cast<uint64>(kPartFileWeight * static_cast<double>(sentPartial));
}

uint64 AnalyzerRecord::downloadedWeighted() const
{
    return static_cast<uint64>(kFullFileWeight * static_cast<double>(got - std::min(gotPartial, got)))
         + gotPartial
         + static_cast<uint64>(kRareShareWeight * static_cast<double>(gotRare));
}

float AnalyzerRecord::score(bool hasSomethingForUs, uint32 reaskTimeMs) const
{
    float s = kBaseScore;

    s -= static_cast<float>(spams) * kLowPunish;
    if (isXsSpammer())
        s -= static_cast<float>(fastXsAsks) * kLowPunish;
    if (isXsExploiter())
        s -= kHighPunish;
    // Who asks too fast often is punished harder (v1.5)
    if (isFastAsker(reaskTimeMs))
        s -= static_cast<float>(reasks) * kLowPunish;

    if (flags & kFlagDlp)
        s -= 3 * kMidPunish;   // = 3 chunks to balance
    if (flags & (kFlagFileFaker | kFlagUdpFnfFaker))
        s -= kHighPunish;      // = 5 chunks to balance

    // Grants a slot but sends nothing
    if (badDlSessions > 2)
        s -= static_cast<float>(badDlSessions) * kMidPunish;

    // A peer with nothing for us keeps an even score [SEPPL]
    if (hasSomethingForUs) {
        s += static_cast<float>(chunks(downloadedWeighted())) * kMidPunish;
        s -= static_cast<float>(chunks(uploadedWeighted())) * kMidPunish;
        // ClientAnalyzer adds +0.025 per week a peer keeps its user hash. Dropped on
        // purpose: an age bonus grows without bound and pays a hash merely for existing.
    }

    return std::clamp(s, kMinScore, kMaxScore);
}

QString AnalyzerRecord::reasons(bool hasSomethingForUs, uint32 reaskTimeMs) const
{
    QStringList out;
    if (flags & kFlagDlp)
        out << QStringLiteral("DLP verdict");
    if (flags & kFlagFileFaker)
        out << QStringLiteral("file faker");
    if (flags & kFlagUdpFnfFaker)
        out << QStringLiteral("UDP FNF faker");
    if (hasSomethingForUs && chunks(downloadedWeighted()) < chunks(uploadedWeighted()))
        out << QStringLiteral("UL/DL %1/%2 chunks").arg(chunks(uploadedWeighted())).arg(chunks(downloadedWeighted()));
    if (isFastAsker(reaskTimeMs))
        out << QStringLiteral("reask %1 × %2 s avg").arg(reasks).arg(avgReaskMs / 1000);
    if (spams > 0)
        out << QStringLiteral("spams %1").arg(spams);
    if (isXsExploiter())
        out << QStringLiteral("XS %1/%2").arg(xsAsks).arg(xsAnswers);
    if (isXsSpammer())
        out << QStringLiteral("fast XS %1").arg(fastXsAsks);
    if (badUlSessions || badDlSessions)
        out << QStringLiteral("failed UL/DL %1/%2").arg(badUlSessions).arg(badDlSessions);
    return out.join(QStringLiteral(", "));
}

bool AnalyzerRecord::isWorthKeeping(int64 now) const
{
    // Bad guys always; good ones once a chunk went either way
    return flags != 0 || isHeldBad(now) || spams > 0 || fastXsAsks > 0
        || xsAsks > xsAnswers || badUlSessions > 0 || badDlSessions > 0
        || isFastAsker(FILEREASKTIME)
        || sent >= PARTSIZE || got >= PARTSIZE;
}

// ===========================================================================
// ClientAnalyzer
// ===========================================================================

std::size_t ClientAnalyzer::KeyHash::operator()(const Key& k) const noexcept
{
    std::size_t h;
    std::memcpy(&h, k.data(), sizeof(h));   // an MD4: already uniform
    return h;
}

ClientAnalyzer::Key ClientAnalyzer::toKey(const uint8* hash)
{
    Key key;
    std::memcpy(key.data(), hash, key.size());
    return key;
}

AnalyzerRecord& ClientAnalyzer::record(const uint8* userHash, bool* created)
{
    const Key key = toKey(userHash);
    auto [it, inserted] = m_records.try_emplace(key);
    if (inserted)
        it->second.key = key;
    if (created)
        *created = inserted;
    it->second.lastSeen = static_cast<uint32>(unixNow());
    m_dirty = true;   // the caller is about to change it
    return it->second;
}

const AnalyzerRecord* ClientAnalyzer::find(const uint8* userHash) const
{
    const auto it = m_records.find(toKey(userHash));
    return it == m_records.end() ? nullptr : &it->second;
}

AnalyzerRecord ClientAnalyzer::snapshot(const uint8* userHash) const
{
    if (const AnalyzerRecord* r = find(userHash))
        return *r;
    AnalyzerRecord r;
    r.key = toKey(userHash);
    r.lastSeen = static_cast<uint32>(unixNow());
    return r;
}

void ClientAnalyzer::seedFromCredits(AnalyzerRecord& r, const ClientCredits& credits)
{
    // Taken as plain data: not partial, not rare
    if (r.sent == 0 && r.got == 0) {
        r.sent = credits.uploadedTotal();
        r.got = credits.downloadedTotal();
    }
}

void ClientAnalyzer::addCorruptSender(const uint8* userHash)
{
    m_corrupt[toKey(userHash)] = static_cast<uint32>(unixNow());
    m_dirty = true;
}

bool ClientAnalyzer::isCorruptSender(const uint8* userHash) const
{
    const auto it = m_corrupt.find(toKey(userHash));
    return it != m_corrupt.end() && unixNow() - it->second < kCorruptHoldS;
}

uint32 ClientAnalyzer::reaskTimeFor(ClientSoftware soft)
{
    switch (soft) {
    case ClientSoftware::eMule:     return FILEREASKTIME;
    case ClientSoftware::MLDonkey:  return MIN_REQUESTTIME;
    case ClientSoftware::Shareaza:  return MIN_REQUESTTIME * 2;
    case ClientSoftware::eDonkey:
    case ClientSoftware::OldEMule:  return MIN2MS(21);   // old eMule/eDonkey reask (eMule+ source)
    default:                        return MIN_REQUESTTIME;   // unknown: the lowest limit
    }
}

QByteArray ClientAnalyzer::serialize(int64 now) const
{
    SafeMemFile file;
    file.writeUInt8(kFileVersion);

    uint32 count = 0;
    for (const auto& [key, r] : m_records)
        if (r.lastSeen + kExpireS >= now && r.isWorthKeeping(now))
            ++count;
    file.writeUInt32(count);

    for (const auto& [key, r] : m_records) {
        if (r.lastSeen + kExpireS < now || !r.isWorthKeeping(now))
            continue;
        file.writeHash16(key.data());
        const qint64 countPos = file.position();
        uint32 tags = 0;
        file.writeUInt32(0);
        const auto put32 = [&](uint8 id, uint32 v) {
            if (v != 0 && Tag(id, v).writeTagToFile(file))
                ++tags;
        };
        const auto put64 = [&](uint8 id, uint64 v) {
            if (v != 0 && Tag(id, v).writeTagToFile(file))
                ++tags;
        };
        put32(TagLastSeen, r.lastSeen);
        put32(TagBadSince, r.badSince);
        put32(TagFlags, r.flags);
        put32(TagReasks, r.reasks);
        put32(TagAvgReask, r.avgReaskMs);
        put32(TagSpams, r.spams);
        put32(TagXsAsks, r.xsAsks);
        put32(TagXsAnswers, r.xsAnswers);
        put32(TagFastXs, r.fastXsAsks);
        put32(TagBadUl, r.badUlSessions);
        put32(TagBadDl, r.badDlSessions);
        put64(TagSent, r.sent);
        put64(TagSentPartial, r.sentPartial);
        put64(TagGot, r.got);
        put64(TagGotPartial, r.gotPartial);
        put64(TagGotRare, r.gotRare);
        const qint64 end = file.position();
        file.seek(countPos, 0);
        file.writeUInt32(tags);
        file.seek(end, 0);
    }

    uint32 corrupt = 0;
    for (const auto& [key, added] : m_corrupt)
        if (now - added < kCorruptHoldS)
            ++corrupt;
    file.writeUInt32(corrupt);
    for (const auto& [key, added] : m_corrupt) {
        if (now - added >= kCorruptHoldS)
            continue;
        file.writeHash16(key.data());
        file.writeUInt32(added);
    }
    return file.takeBuffer();
}

bool ClientAnalyzer::deserialize(const QByteArray& data, int64 now)
{
    m_records.clear();
    m_corrupt.clear();
    try {
        SafeMemFile file(data);
        if (file.readUInt8() != kFileVersion)
            return false;

        const uint32 count = file.readUInt32();
        for (uint32 i = 0; i < count; ++i) {
            AnalyzerRecord r;
            file.readHash16(r.key.data());
            const uint32 tags = file.readUInt32();
            for (uint32 t = 0; t < tags; ++t) {
                const Tag tag(file, false);
                if (!tag.isInt64(true))
                    continue;   // unknown kind: a newer version, skipped
                const uint64 v = tag.int64Value();
                const auto v32 = static_cast<uint32>(v);
                switch (tag.nameId()) {
                case TagLastSeen:    r.lastSeen = v32; break;
                case TagBadSince:    r.badSince = v32; break;
                case TagFlags:       r.flags = v32; break;
                case TagReasks:      r.reasks = v32; break;
                case TagAvgReask:    r.avgReaskMs = v32; break;
                case TagSpams:       r.spams = v32; break;
                case TagXsAsks:      r.xsAsks = v32; break;
                case TagXsAnswers:   r.xsAnswers = v32; break;
                case TagFastXs:      r.fastXsAsks = v32; break;
                case TagBadUl:       r.badUlSessions = static_cast<uint8>(std::min<uint32>(v32, 255)); break;
                case TagBadDl:       r.badDlSessions = static_cast<uint8>(std::min<uint32>(v32, 255)); break;
                case TagSent:        r.sent = v; break;
                case TagSentPartial: r.sentPartial = v; break;
                case TagGot:         r.got = v; break;
                case TagGotPartial:  r.gotPartial = v; break;
                case TagGotRare:     r.gotRare = v; break;
                default: break;
                }
            }
            if (r.avgReaskMs == 0)
                r.reasks = 0;   // ClientAnalyzer failsafe
            if (r.lastSeen + kExpireS >= now)
                m_records[r.key] = r;
        }

        // Older files may end here
        if (file.position() < file.length()) {
            const uint32 corrupt = file.readUInt32();
            for (uint32 i = 0; i < corrupt; ++i) {
                Key key;
                file.readHash16(key.data());
                const uint32 added = file.readUInt32();
                if (now - added < kCorruptHoldS)
                    m_corrupt[key] = added;
            }
        }
        return true;
    } catch (const FileException&) {
        return false;
    }
}

bool ClientAnalyzer::load(const QString& filePath)
{
    const auto data = BackgroundFileWriter::readFile(filePath);
    if (!data)
        return false;
    if (!deserialize(*data, unixNow())) {
        logWarning(QStringLiteral("Client analyzer file \"%1\" is corrupt or of an unknown version, starting empty")
                       .arg(filePath));
        m_records.clear();
        m_corrupt.clear();
        return false;
    }
    logInfo(QStringLiteral("Client analyzer file loaded, %1 clients and %2 corrupt data senders are known")
                .arg(m_records.size()).arg(m_corrupt.size()));
    return true;
}

void ClientAnalyzer::save(const QString& filePath)
{
    if (!m_dirty && !BackgroundFileWriter::instance().failed(filePath))
        return;
    m_dirty = false;
    // Expired entries leave the RAM too
    const int64 now = unixNow();
    std::erase_if(m_records, [now](const auto& kv) { return kv.second.lastSeen + kExpireS < now; });
    std::erase_if(m_corrupt, [now](const auto& kv) { return now - kv.second >= kCorruptHoldS; });
    BackgroundFileWriter::instance().write(filePath, serialize(now), &BackgroundFileWriter::saveFile);
}

void ClientAnalyzer::process(const QString& filePath, uint64 nowMs)
{
    if (m_lastSaved == 0) {
        m_lastSaved = nowMs;
        return;
    }
    if (nowMs - m_lastSaved < static_cast<uint64>(kSaveIntervalMs))
        return;
    m_lastSaved = nowMs;
    save(filePath);
}

} // namespace eMule
