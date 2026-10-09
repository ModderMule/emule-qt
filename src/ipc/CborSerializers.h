#pragma once

/// @file CborSerializers.h
/// @brief Header-only CBOR serializers for core entity types.
///
/// Mirrors JsonSerializers.h but produces QCborMap instead of QJsonObject.
/// Include only from daemon code that has access to core types.

#include "client/ClientCredits.h"
#include "client/UpDownClient.h"
#include "files/AbstractFile.h"
#include "utils/Opcodes.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "friends/Friend.h"
#include "geo/IP2Country.h"
#include "net/BindAddress.h"
#include "search/SearchFile.h"
#include "server/Server.h"
#include "server/ServerList.h"
#include "utils/OtherFunctions.h"
#include "utils/TimeUtils.h"

#include <QCborArray>
#include <QCborMap>

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace eMule::Ipc {

// ---------------------------------------------------------------------------
// Status / priority to string helpers (reuse from JsonSerializers)
// ---------------------------------------------------------------------------

[[nodiscard]] inline QString statusToString(PartFileStatus s)
{
    switch (s) {
    case PartFileStatus::Ready:        return QStringLiteral("ready");
    case PartFileStatus::Empty:        return QStringLiteral("empty");
    case PartFileStatus::WaitingForHash: return QStringLiteral("waitingforhash");
    case PartFileStatus::Hashing:      return QStringLiteral("hashing");
    case PartFileStatus::Error:        return QStringLiteral("error");
    case PartFileStatus::Insufficient: return QStringLiteral("insufficient");
    case PartFileStatus::Paused:       return QStringLiteral("paused");
    case PartFileStatus::Completing:   return QStringLiteral("completing");
    case PartFileStatus::Complete:     return QStringLiteral("complete");
    default:                           return QStringLiteral("unknown");
    }
}

[[nodiscard]] inline QString priorityToString(uint8_t prio)
{
    switch (prio) {
    case 4:  return QStringLiteral("veryLow");
    case 0:  return QStringLiteral("low");
    case 1:  return QStringLiteral("normal");
    case 2:  return QStringLiteral("high");
    case 3:  return QStringLiteral("veryHigh");
    default: return QStringLiteral("auto");
    }
}

// ---------------------------------------------------------------------------
// Entity serializers — QCborMap output
// ---------------------------------------------------------------------------

[[nodiscard]] inline QCborArray buildPartMap(const PartFile& f)
{
    const uint16 parts = f.partCount();
    if (parts == 0) return {};

    const auto& freq = f.srcPartFrequency();

    // Mark parts with active download requests
    std::vector<bool> requested(parts, false);
    for (const auto* blk : f.requestedBlockList()) {
        uint32 startPart = static_cast<uint32>(blk->startOffset / PARTSIZE);
        uint32 endPart   = static_cast<uint32>(blk->endOffset / PARTSIZE);
        for (uint32 p = startPart; p <= endPart && p < parts; ++p)
            requested[p] = true;
    }

    // Encode: 0=complete, 1=gap/no-sources, 2..254=gap/sources(freq), 255=downloading
    QCborArray arr;
    for (uint16 p = 0; p < parts; ++p) {
        if (f.isComplete(p)) {
            arr.append(0);
        } else if (requested[p]) {
            arr.append(255);
        } else {
            uint16 avail = (p < freq.size()) ? freq[p] : 0;
            arr.append(avail == 0 ? 1 : std::clamp<int>(avail + 1, 2, 254));
        }
    }
    return arr;
}

/// Most byte ranges one bar list carries; a bar is a few hundred pixels wide.
inline constexpr qsizetype kMaxBarRanges = 1024;

/// Byte ranges [start, end] (inclusive, sorted) as a flat [s0, e0, s1, e1, ...]
/// array. Past kMaxBarRanges the closest neighbours merge, so the outline and
/// the first/last byte survive while the push stays small.
[[nodiscard]] inline QCborArray packBarRanges(std::vector<std::pair<uint64, uint64>> ranges)
{
    while (static_cast<qsizetype>(ranges.size()) > kMaxBarRanges) {
        // Merge across the smallest distances; one pass drops exactly the excess
        std::vector<uint64> distances;
        distances.reserve(ranges.size() - 1);
        for (size_t i = 1; i < ranges.size(); ++i)
            distances.push_back(ranges[i].first - ranges[i - 1].second);
        const size_t excess = ranges.size() - static_cast<size_t>(kMaxBarRanges);
        std::vector<uint64> sorted = distances;
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(excess - 1),
                         sorted.end());
        const uint64 threshold = sorted[excess - 1];

        std::vector<std::pair<uint64, uint64>> merged;
        merged.reserve(ranges.size());
        merged.push_back(ranges.front());
        size_t budget = excess;
        for (size_t i = 1; i < ranges.size(); ++i) {
            if (budget > 0 && distances[i - 1] <= threshold) {
                merged.back().second = std::max(merged.back().second, ranges[i].second);
                --budget;
            } else {
                merged.push_back(ranges[i]);
            }
        }
        ranges = std::move(merged);
    }

    QCborArray arr;
    for (const auto& [start, end] : ranges) {
        arr.append(static_cast<qint64>(start));
        arr.append(static_cast<qint64>(end));
    }
    return arr;
}

/// The file's gaps, byte-exact, for MFC's DrawStatusBar.
[[nodiscard]] inline QCborArray buildGapRanges(const PartFile& f)
{
    std::vector<std::pair<uint64, uint64>> ranges;
    ranges.reserve(f.gapList().size());
    for (const Gap& gap : f.gapList())
        ranges.emplace_back(gap.start, gap.end);
    std::ranges::sort(ranges);
    return packBarRanges(std::move(ranges));
}

/// The not-yet-received remainder of each requested block (MFC's yellow).
[[nodiscard]] inline QCborArray buildPendingRanges(const PartFile& f)
{
    std::vector<std::pair<uint64, uint64>> ranges;
    ranges.reserve(f.requestedBlockList().size());
    for (const auto* blk : f.requestedBlockList()) {
        const uint64 start = blk->startOffset + blk->transferredByClient;
        if (start <= blk->endOffset)
            ranges.emplace_back(start, blk->endOffset);
    }
    std::ranges::sort(ranges);
    return packBarRanges(std::move(ranges));
}

/// Raw source count per part; partMap's 255 (requested) hides it.
[[nodiscard]] inline QCborArray buildPartFrequency(const PartFile& f)
{
    QCborArray arr;
    for (const uint16 freq : f.srcPartFrequency())
        arr.append(freq);
    return arr;
}

/// Fake-file verdict as the three keys every row carries it under.
inline void insertFakeVerdict(QCborMap& m, const FakeFileVerdict& verdict)
{
    m.insert(QStringLiteral("fakeScore"),   verdict.score);
    m.insert(QStringLiteral("confidence"),  confidenceId(verdict.band));
    m.insert(QStringLiteral("fakeReasons"), QCborArray::fromStringList(verdict.reasonIds()));
}

[[nodiscard]] inline QCborMap toCbor(const PartFile& f)
{
    const ContainerCheck& cc = f.containerCheck();
    QCborMap m{
        {QStringLiteral("hash"),                 md4str(f.fileHash())},
        {QStringLiteral("fileName"),             f.fileName()},
        {QStringLiteral("fileSize"),             static_cast<qint64>(f.fileSize())},
        {QStringLiteral("completedSize"),        static_cast<qint64>(f.completedSize())},
        {QStringLiteral("percentCompleted"),     static_cast<double>(f.percentCompleted())},
        {QStringLiteral("status"),               statusToString(f.status())},
        {QStringLiteral("datarate"),             f.status() == PartFileStatus::Complete ? 0 : static_cast<qint64>(f.datarate())},
        {QStringLiteral("sourceCount"),          f.sourceCount()},
        {QStringLiteral("transferringSrcCount"), f.transferringSrcCount()},
        // MFC's Sources column: available (on queue + downloading) of total, plus A4AF.
        {QStringLiteral("availableSrcCount"),    f.availableSourceCount()},
        {QStringLiteral("a4afSrcCount"),         f.a4afSourceCount()},
        {QStringLiteral("downPriority"),        priorityToString(f.downPriority())},
        {QStringLiteral("isAutoDownPriority"),   f.isAutoDownPriority()},
        {QStringLiteral("isPaused"),             f.isPaused()},
        {QStringLiteral("isStopped"),            f.isStopped()},
        // Both feed the GUI's status column, which reproduces MFC's getPartfileStatus():
        // a file op relabels "Completing", and a completion error relabels "Error".
        {QStringLiteral("fileOp"),               static_cast<int>(f.fileOp())},
        {QStringLiteral("completionError"),      f.completionError()},
        {QStringLiteral("category"),             static_cast<qint64>(f.category())},
        {QStringLiteral("lastSeenComplete"),    static_cast<qint64>(f.lastSeenComplete())},
        // MFC appends the complete-source range to Seen Complete
        {QStringLiteral("completeSourcesLo"),   static_cast<int>(f.completeSourcesCountLo())},
        {QStringLiteral("completeSourcesHi"),   static_cast<int>(f.completeSourcesCountHi())},
        {QStringLiteral("timeRemaining"),       static_cast<qint64>(f.timeRemaining())},
        // Bytes received for this file; "upTransferred" below is the upload statistic
        {QStringLiteral("downTransferred"),     static_cast<qint64>(f.transferred())},
        // Advanced-mode per-file settings (MFC Source Handling / Preview submenus)
        {QStringLiteral("privateMaxSources"),   static_cast<qint64>(f.privateMaxSources())},
        {QStringLiteral("previewPrio"),         f.previewPrio()},
        {QStringLiteral("pauseOnPreview"),      f.isPausingOnPreview()},
        {QStringLiteral("hashsetNeeded"),       f.partCount() > 1 && f.isMD4HashsetNeeded()},
        {QStringLiteral("lastReception"),       static_cast<qint64>(f.lastReceptionDate())},
        {QStringLiteral("addedOn"),             static_cast<qint64>(f.createdDate())},
        {QStringLiteral("fileType"),            f.fileType()},
        {QStringLiteral("requests"),            static_cast<qint64>(f.statistic.allTimeRequests())},
        {QStringLiteral("acceptedReqs"),        static_cast<qint64>(f.statistic.allTimeAccepts())},
        {QStringLiteral("upTransferred"),       static_cast<qint64>(f.statistic.allTimeTransferred())},
        {QStringLiteral("partMap"),             buildPartMap(f)},
        // Byte-exact bar data: the progress bar draws gaps and pending blocks where they sit
        {QStringLiteral("gaps"),                buildGapRanges(f)},
        {QStringLiteral("pending"),             buildPendingRanges(f)},
        {QStringLiteral("partFreq"),            buildPartFrequency(f)},
        {QStringLiteral("isPreviewPossible"),  f.isPreviewPossible()},
        // Comment/rating marks. userRating(true) folds in MFC's pseudo-rating 6,
        // "a Kad note lookup is running", so the GUI rebuilds the whole predicate
        // from these two.
        {QStringLiteral("hasComment"),          f.hasComment()},
        {QStringLiteral("userRating"),          static_cast<int>(f.userRating(true))},
        // The container check, bound once: asking three times costs three gap-list
        // scans (and three opens) per poll while the first part is still missing,
        // because an unreadable head is deliberately not cached. Downloads are the
        // one list small enough to read here; the share is warmed in the background
        // by SharedFileList::warmContainerChecks(), and a part file is the same
        // object in both lists, so whichever gets there first settles it for both.
        {QStringLiteral("containerSuspect"),    cc.isSuspect()},
        {QStringLiteral("containerExpected"),   cc.expected},
        {QStringLiteral("containerActual"),     cc.actual},
    };
    insertFakeVerdict(m, f.fakeVerdict());
    return m;
}

/// The bound-interface state every connection snapshot carries: `netBlocked` while the
/// selected interface is missing (nothing connects), with the reason and the interface.
inline void insertBindState(QCborMap& info)
{
    const BindAddress::Resolution r = BindAddress::current();
    info.insert(QStringLiteral("netBlocked"), r.state == BindAddress::State::Blocked);
    info.insert(QStringLiteral("netBlockReason"), r.reason);
    info.insert(QStringLiteral("boundInterface"),
                r.state == BindAddress::State::Bound ? r.name : QString());
}

[[nodiscard]] inline QCborMap toCbor(const Server& s)
{
    return QCborMap{
        {QStringLiteral("name"),        s.name()},
        {QStringLiteral("address"),     s.address()},
        {QStringLiteral("ip"),          static_cast<qint64>(s.ipAddress().toNetworkUint32())},
        // "address" may be a dynIP hostname; "addr" is always the literal we dialed, and is
        // the only field that survives an IPv6 server ("ip" is 0 for those).
        {QStringLiteral("addr"),        s.ipAddress().toString()},
        // A dual-stack server's IPv6 next to its IPv4 "addr" (empty otherwise).
        {QStringLiteral("addr6"),       s.hasBothFamilies() ? s.ipv6Address().toString() : QString()},
        {QStringLiteral("cc"),          countryCodeOf(s.ipAddress())},   // empty until resolved
        {QStringLiteral("port"),        s.port()},
        {QStringLiteral("description"), s.description()},
        {QStringLiteral("version"),     s.version()},
        {QStringLiteral("users"),       static_cast<qint64>(s.users())},
        {QStringLiteral("maxUsers"),    static_cast<qint64>(s.maxUsers())},
        {QStringLiteral("files"),       static_cast<qint64>(s.files())},
        {QStringLiteral("ping"),        static_cast<qint64>(s.ping())},
        {QStringLiteral("failedCount"), static_cast<qint64>(s.failedCount())},
        {QStringLiteral("preference"),  static_cast<int>(s.preference())},
        {QStringLiteral("isStatic"),    s.isStaticMember()},
        {QStringLiteral("disabled"),    s.isDisabled()},
        {QStringLiteral("softFiles"),   static_cast<qint64>(s.softFiles())},
        {QStringLiteral("hardFiles"),   static_cast<qint64>(s.hardFiles())},
        {QStringLiteral("lowIDUsers"),  static_cast<qint64>(s.lowIDUsers())},
        {QStringLiteral("obfuscation"), s.supportsObfuscationTCP()},
        {QStringLiteral("serverId"),    static_cast<qint64>(s.serverId())},
        {QStringLiteral("hasMetaApi"),  s.hasMetaApi()},
        {QStringLiteral("addrPort"),    s.addressWithPort()},   // key of the Meta API requests
    };
}

[[nodiscard]] inline QCborMap toCbor(const Friend& f)
{
    return QCborMap{
        {QStringLiteral("hash"),        f.hasUserhash() ? md4str(f.userHash().data()) : QString()},
        {QStringLiteral("name"),        f.name()},
        {QStringLiteral("ip"),          static_cast<qint64>(f.lastUsedAddress().toNetworkUint32())},
        {QStringLiteral("addr"),        f.lastUsedAddress().toString()},   // IPv6-capable form
        {QStringLiteral("cc"),          countryCodeOf(f.lastUsedAddress())},
        {QStringLiteral("port"),        f.lastUsedPort()},
        {QStringLiteral("lastSeen"),    static_cast<qint64>(f.lastSeen())},
        {QStringLiteral("lastChatted"), static_cast<qint64>(f.lastChatted())},
        {QStringLiteral("friendSlot"),  f.friendSlot()},
        {QStringLiteral("kadID"),       md4str(f.kadID().data())},
    };
}

/// Source clients, and the AICH root the row and its names agree on with who stands
/// behind it. Capped: a restored row needs a start, not the whole list.
inline void insertDownloadSeed(QCborMap& m, const SearchFile& f)
{
    static constexpr qsizetype kMaxSeedEntries = 16;

    std::vector<const SearchFile*> rows{&f};
    for (const SearchFile* child : f.listChildren())
        rows.push_back(child);

    QCborArray clients;
    std::vector<SearchFile::SClient> seen;
    const AICHHash* agreed = nullptr;
    bool conflict = false;
    bool vouched = false;
    QCborArray voters;
    for (const SearchFile* row : rows) {
        for (const SearchFile::SClient& client : row->clients()) {
            if (clients.size() >= kMaxSeedEntries || std::ranges::find(seen, client) != seen.end())
                continue;
            seen.push_back(client);
            clients.append(QCborArray{static_cast<qint64>(client.ip), client.port,
                                      static_cast<qint64>(client.serverIP), client.serverPort});
        }
        if (!row->fileIdentifier().hasAICHHash())
            continue;
        const AICHHash& hash = row->fileIdentifier().getAICHHash();
        if (!agreed)
            agreed = &hash;
        else if (*agreed != hash)
            conflict = true;
        vouched |= !row->isKadResult() || row->isAICHVouchedDirectly();
        for (const Address& voter : row->aichVoters()) {
            if (voters.size() < kMaxSeedEntries)
                voters.append(voter.toString());
        }
    }
    if (!clients.isEmpty())
        m.insert(QStringLiteral("clients"), clients);
    if (agreed && !conflict) {
        m.insert(QStringLiteral("aichSeed"), agreed->getString());
        m.insert(QStringLiteral("aichVouched"), vouched);
        if (!vouched && !voters.isEmpty())
            m.insert(QStringLiteral("aichVoters"), voters);
    }
}

[[nodiscard]] inline QCborMap toCbor(const SearchFile& f)
{
    QCborMap m;
    m.insert(QStringLiteral("hash"),                md4str(f.fileHash()));
    m.insert(QStringLiteral("fileName"),            f.fileName());
    m.insert(QStringLiteral("fileSize"),            static_cast<qint64>(f.fileSize()));
    m.insert(QStringLiteral("sourceCount"),         static_cast<qint64>(f.sourceCount()));
    m.insert(QStringLiteral("completeSourceCount"), static_cast<qint64>(f.completeSourceCount()));
    // MFC CSearchFile::IsComplete(): neither a Kad hit nor a browsed file carries
    // complete-source information, so the GUI shows "?" instead of 0%.
    m.insert(QStringLiteral("isKad"),               f.isKadResult());
    // a server result the server found on Kad (FT_META_NETWORK): gets the Kad badge
    m.insert(QStringLiteral("kadOrigin"),           f.isKadOrigin());
    m.insert(QStringLiteral("inDirectory"),         !f.directory().isEmpty());
    m.insert(QStringLiteral("fileType"),            f.fileType());
    m.insert(QStringLiteral("searchID"),            static_cast<qint64>(f.searchID()));
    m.insert(QStringLiteral("knownType"),           static_cast<int>(f.knownType()));
    // From the seen-files index: met before this search, under how many names, since when.
    m.insert(QStringLiteral("seenBefore"),          f.seenBefore());
    m.insert(QStringLiteral("seenNames"),           f.seenNames());
    m.insert(QStringLiteral("firstSeen"),           f.firstSeen());
    m.insert(QStringLiteral("isSpam"),              f.isConsideredSpam());
    m.insert(QStringLiteral("hasComment"),          f.hasComment());
    m.insert(QStringLiteral("userRating"),          static_cast<int>(f.userRating(true)));
    // a browsed peer's file it can send preview frames of (RequestSearchPreview)
    m.insert(QStringLiteral("previewPossible"),     f.isPreviewPossible());
    // Media metadata from ED2K tags
    m.insert(QStringLiteral("artist"),  f.getStrTagValue(FT_MEDIA_ARTIST));
    m.insert(QStringLiteral("album"),   f.getStrTagValue(FT_MEDIA_ALBUM));
    m.insert(QStringLiteral("title"),   f.getStrTagValue(FT_MEDIA_TITLE));
    m.insert(QStringLiteral("length"),  static_cast<qint64>(f.getIntTagValue(FT_MEDIA_LENGTH)));
    m.insert(QStringLiteral("bitrate"), static_cast<qint64>(f.getIntTagValue(FT_MEDIA_BITRATE)));
    m.insert(QStringLiteral("codec"),   f.getStrTagValue(FT_MEDIA_CODEC));
    // Hidden-by-default columns of MFC's list: Folder and AICH Hash
    m.insert(QStringLiteral("directory"), f.directory());
    if (f.fileIdentifier().hasAICHHash())
        m.insert(QStringLiteral("aichHash"), f.fileIdentifier().getAICHHash().getString());
    // The advanced-mode detail behind Availability (MFC SearchListCtrl.cpp:1555-1567)
    m.insert(QStringLiteral("kadPublishers"), static_cast<int>((f.kadPublishInfo() >> 16) & 0xFF));
    // ... and the rest of the publish info: different names, trust x100 (ibid. :1569-1575)
    m.insert(QStringLiteral("kadNames"),      static_cast<int>((f.kadPublishInfo() >> 24) & 0xFF));
    m.insert(QStringLiteral("kadTrust"),      static_cast<int>(f.kadPublishInfo() & 0xFFFF));
    m.insert(QStringLiteral("clientCount"),   static_cast<int>(f.clientsCount()));
    // What a download takes from the result (DownloadQueue::seedFromSearchResult): the
    // GUI keeps it with the row, for a tab restored after the search is gone.
    insertDownloadSeed(m, f);
    // The names this file was found under, once there is more than one (MFC's child rows)
    if (f.listChildCount() > 1) {
        QCborArray children;
        for (const SearchFile* child : f.listChildren()) {
            QCborMap c;
            c.insert(QStringLiteral("fileName"),    child->fileName());
            c.insert(QStringLiteral("sourceCount"), static_cast<qint64>(child->sourceCount()));
            c.insert(QStringLiteral("directory"),   child->directory());
            if (child->fileIdentifier().hasAICHHash())
                c.insert(QStringLiteral("aichHash"), child->fileIdentifier().getAICHHash().getString());
            c.insert(QStringLiteral("artist"),  child->getStrTagValue(FT_MEDIA_ARTIST));
            c.insert(QStringLiteral("album"),   child->getStrTagValue(FT_MEDIA_ALBUM));
            c.insert(QStringLiteral("title"),   child->getStrTagValue(FT_MEDIA_TITLE));
            c.insert(QStringLiteral("length"),  static_cast<qint64>(child->getIntTagValue(FT_MEDIA_LENGTH)));
            c.insert(QStringLiteral("bitrate"), static_cast<qint64>(child->getIntTagValue(FT_MEDIA_BITRATE)));
            c.insert(QStringLiteral("codec"),   child->getStrTagValue(FT_MEDIA_CODEC));
            children.append(c);
        }
        m.insert(QStringLiteral("children"), children);
    }
    // Absent for a row nothing was judged on (a child row)
    if (f.hasFakeVerdict())
        insertFakeVerdict(m, f.fakeVerdict());
    // eNode meta row: the network comes from the hash (0 = plain eD2K)
    const auto& meta = f.meta();
    m.insert(QStringLiteral("metaKind"), static_cast<int>(meta.kind));
    if (f.isMetaResult()) {
        m.insert(QStringLiteral("magnet"),      meta.magnet);
        m.insert(QStringLiteral("metaSeeders"), static_cast<qint64>(meta.seeders));
        m.insert(QStringLiteral("metaPeers"),   static_cast<qint64>(meta.peers));
        m.insert(QStringLiteral("metaAge"),     static_cast<qint64>(meta.ageDays));
        m.insert(QStringLiteral("metaIndexer"), meta.indexer);
        m.insert(QStringLiteral("metaFlags"),   static_cast<qint64>(meta.flags));
        // what FetchMetaFile/DownloadMetaResult need once the live search is gone
        m.insert(QStringLiteral("metaCatalogId"), meta.catalogId);
        QCborArray servers;
        for (const auto& s : f.servers())
            servers.append(QCborArray{static_cast<qint64>(s.ip), static_cast<qint64>(s.port)});
        m.insert(QStringLiteral("metaServers"), servers);
    }
    return m;
}

[[nodiscard]] inline QCborArray buildSourcePartMap(const UpDownClient& c)
{
    const auto& partStatus = c.partStatus();
    const uint16 parts = c.partCount();
    const auto* reqFile = c.reqFile();

    // Collect parts with pending blocks
    std::set<uint32> pendingParts;
    for (const auto* blk : c.pendingBlocks()) {
        if (blk && blk->block)
            pendingParts.insert(static_cast<uint32>(blk->block->startOffset / PARTSIZE));
    }

    // Determine actively downloading part
    uint32 activePart = UINT32_MAX;
    if (c.isDownloadingFromPeer() && c.sessionDown() > 0 && c.lastBlockOffset() != UINT64_MAX)
        activePart = static_cast<uint32>(c.lastBlockOffset() / PARTSIZE);

    // For complete sources, partStatus is empty but they have all parts
    if (c.completeSource() && reqFile) {
        const uint16 fileParts = reqFile->partCount();
        if (fileParts == 0) return {};
        QCborArray arr;
        for (uint16 i = 0; i < fileParts; ++i) {
            if (reqFile->isComplete(i)) {
                arr.append(1);  // both have it
            } else if (i == activePart) {
                arr.append(4);  // currently receiving
            } else if (pendingParts.count(i)) {
                arr.append(3);  // pending block queued
            } else {
                arr.append(2);  // client has, we need
            }
        }
        return arr;
    }

    if (parts == 0 || partStatus.empty() || !reqFile)
        return {};

    QCborArray arr;
    for (uint16 i = 0; i < parts; ++i) {
        if (i >= partStatus.size() || !partStatus[i]) {
            arr.append(0);  // client doesn't have this part
        } else if (reqFile->isComplete(i)) {
            arr.append(1);  // both have it
        } else if (i == activePart) {
            arr.append(4);  // currently receiving
        } else if (pendingParts.count(i)) {
            arr.append(3);  // pending block queued
        } else {
            arr.append(2);  // client has, we need
        }
    }
    return arr;
}

[[nodiscard]] inline QCborMap toCbor(const UpDownClient& c)
{
    QCborMap m;
    m.insert(QStringLiteral("userName"),        c.userName());
    m.insert(QStringLiteral("userHash"),        md4str(c.userHash()));
    m.insert(QStringLiteral("software"),        c.dbgGetFullClientSoftVer());
    m.insert(QStringLiteral("uploadState"),     c.uploadStateToken());
    m.insert(QStringLiteral("uploadStalled"),   c.uploadStalled());
    m.insert(QStringLiteral("downloadState"),   c.downloadStateDisplayString());
    m.insert(QStringLiteral("sourceFrom"),      static_cast<int>(c.sourceFrom()));
    // Upload fields
    m.insert(QStringLiteral("transferredUp"),   static_cast<qint64>(c.transferredUp()));
    m.insert(QStringLiteral("sessionUp"),       static_cast<qint64>(c.sessionUp()));
    m.insert(QStringLiteral("queueSessionPayloadUp"),
             static_cast<qint64>(c.queueSessionPayloadUp()));
    m.insert(QStringLiteral("upDatarate"),     static_cast<qint64>(c.upDatarate()));
    m.insert(QStringLiteral("askedCount"),      static_cast<qint64>(c.askedCount()));
    m.insert(QStringLiteral("waitStartTime"),   static_cast<qint64>(c.getWaitTimeDelay()));
    m.insert(QStringLiteral("isBanned"),        c.isBanned());
    // Download fields
    m.insert(QStringLiteral("transferredDown"), static_cast<qint64>(c.transferredDown()));
    m.insert(QStringLiteral("sessionDown"),     static_cast<qint64>(c.sessionDown()));
    m.insert(QStringLiteral("datarate"),        static_cast<qint64>(c.downDatarate()));
    // Credit totals across sessions; the Downloading list shows them beside the session
    // figures (MFC DownloadClientsCtrl.cpp:187-198).
    const auto* cr = c.credits();
    m.insert(QStringLiteral("downloadedTotal"), cr ? static_cast<qint64>(cr->downloadedTotal()) : 0);
    m.insert(QStringLiteral("uploadedTotal"),   cr ? static_cast<qint64>(cr->uploadedTotal()) : 0);
    m.insert(QStringLiteral("partCount"),       c.partCount());
    m.insert(QStringLiteral("upPartCount"),    static_cast<int>(c.upPartCount()));
    m.insert(QStringLiteral("fileName"),        c.clientFilename());
    m.insert(QStringLiteral("remoteQueueRank"), static_cast<qint64>(c.remoteQueueRank()));
    m.insert(QStringLiteral("remoteQueueFull"), c.remoteQueueFull());
    m.insert(QStringLiteral("availPartCount"),  c.availablePartCount());
    // Client software identification
    m.insert(QStringLiteral("softwareId"), static_cast<int>(c.clientSoft()));
    // userAddress(), not connectAddress(): scoreRatio()'s ident gate keys off MFC's GetIP(),
    // and a connectAddress can hold an IPv6 we merely intend to dial — a key that never
    // matches m_identIP, so a securely identified peer reads back as IdBadGuy here while
    // scoring correctly in the queue. Same key
    // score() uses, for the reason spelled out at core UploadClient.cpp:50-54.
    m.insert(QStringLiteral("hasCredit"),  c.credits() ? (c.credits()->scoreRatio(c.userAddress()) > 1.0f) : false);
    m.insert(QStringLiteral("isFriend"),   c.friendPtr() != nullptr);
    // Network address. "ip" stays for compatibility but is 0 for an IPv6 peer — "addr"
    // carries both families, so anything that must survive IPv6 reads that instead.
    m.insert(QStringLiteral("ip"),   static_cast<qint64>(c.connectAddress().toNetworkUint32()));
    m.insert(QStringLiteral("addr"), c.connectAddress().toString());
    m.insert(QStringLiteral("cc"),   countryCodeOf(c.connectAddress().isNull() ? c.userAddress()
                                                                               : c.connectAddress()));
    m.insert(QStringLiteral("port"), static_cast<qint64>(c.userPort()));
    // Upload timing and connection state
    m.insert(QStringLiteral("uploadStartDelay"), static_cast<qint64>(c.getUpStartTimeDelay()));
    m.insert(QStringLiteral("fileRating"), static_cast<int>(c.fileRating()));
    m.insert(QStringLiteral("isConnected"), c.socket() != nullptr);
    // File info. File Priority on the queue list is the *upload* file's up priority
    // (MFC QueueListCtrl.cpp:204-226), not the requested download's.
    if (c.reqFile()) {
        m.insert(QStringLiteral("reqFileName"), c.reqFile()->fileName());
        m.insert(QStringLiteral("reqFileSize"), static_cast<qint64>(c.reqFile()->fileSize()));
    }
    m.insert(QStringLiteral("kadPort"),    static_cast<int>(c.kadPort()));
    m.insert(QStringLiteral("kadVersion"), static_cast<int>(c.kadVersion()));
    // A4AF mark: a source of this file that is asking another one
    m.insert(QStringLiteral("hasOtherRequests"), c.otherRequestCount() > 0);
    // Source tooltip and sorting (MFC DownloadListCtrl.cpp:2318-2372, 1790-1793)
    m.insert(QStringLiteral("clientVersion"), static_cast<qint64>(c.clientVersion()));
    m.insert(QStringLiteral("askedCountDown"), static_cast<qint64>(c.askedCountDown()));
    m.insert(QStringLiteral("srcServerAddr"), c.serverAddress().isNull() ? QString() : c.serverAddress().toString());
    m.insert(QStringLiteral("srcServerPort"), static_cast<qint64>(c.serverPort()));
    if (c.reqFile())
        m.insert(QStringLiteral("nextReaskSecs"), static_cast<qint64>(c.timeUntilReask(c.reqFile()) / 1000));
    if (!c.fileComment().isEmpty())
        m.insert(QStringLiteral("fileComment"), c.fileComment());
    if (!c.otherRequests().empty()) {
        QStringList names;
        for (const PartFile* other : c.otherRequests())
            names << other->fileName();
        names.sort(Qt::CaseInsensitive);
        m.insert(QStringLiteral("a4afFiles"), QCborArray::fromStringList(names));
    }
    m.insert(QStringLiteral("isUrl"), c.isUrlClient());
    if (const auto* uf = c.uploadFile()) {
        m.insert(QStringLiteral("uploadFileName"), uf->fileName());
        m.insert(QStringLiteral("uploadFilePriority"), static_cast<int>(uf->upPriority()));
        m.insert(QStringLiteral("uploadFileAutoPriority"), uf->isAutoUpPriority());
    }
    if (auto spm = buildSourcePartMap(c); !spm.isEmpty())
        m.insert(QStringLiteral("sourcePartMap"), std::move(spm));

    // On Queue columns, MFC QueueListCtrl.cpp:230-253. score() runs in ms; /1000 is MFC's
    // figure, as in toCborDetailed(). The list asks without isDownloading, like MFC.
    m.insert(QStringLiteral("queueRating"), static_cast<qint64>(c.score(false, false, true) / 1000));
    m.insert(QStringLiteral("queueScore"),  static_cast<qint64>(c.score(false) / 1000));
    const auto tick = getTickCount();
    const uint64 lastUpRequest = c.lastUpRequest();
    m.insert(QStringLiteral("lastUpRequestDelay"),
             lastUpRequest != 0 && tick >= lastUpRequest ? static_cast<qint64>(tick - lastUpRequest)
                                                         : qint64(0));
    m.insert(QStringLiteral("hasLowID"), c.hasLowID());
    m.insert(QStringLiteral("addNextConnect"), c.addNextConnect());

    // Obtained Parts bar, MFC CUpDownClient::DrawUpStatusBar (UploadClient.cpp:53-114).
    // Parts go bit-packed: every client of the waiting list carries them.
    const uint64 upFileSize = c.uploadFile() ? static_cast<uint64>(c.uploadFile()->fileSize())
                                             : static_cast<uint64>(PARTSIZE) * c.upPartCount();
    const bool holdsSlot = c.uploadState() == UploadState::Uploading
                        || c.uploadState() == UploadState::Connecting;
    if (upFileSize > 0 && (c.upPartCount() > 0 || holdsSlot)) {
        QCborMap bar;
        bar.insert(QStringLiteral("fileSize"), static_cast<qint64>(upFileSize));
        const auto& status = c.upPartStatus();
        QByteArray packed((static_cast<qsizetype>(status.size()) + 7) / 8, '\0');
        for (std::size_t i = 0; i < status.size(); ++i) {
            if (status[i])
                packed[static_cast<qsizetype>(i / 8)] |= static_cast<char>(1 << (i % 8));
        }
        bar.insert(QStringLiteral("parts"), packed);
        // Whole parts about to go out: the next request and the part of the latest block.
        QCborArray next;
        if (!c.blockRequests().empty() && c.blockRequests().front())
            next.append(static_cast<qint64>(c.blockRequests().front()->startOffset / PARTSIZE));
        if (!c.doneBlocks().empty() && c.doneBlocks().front())
            next.append(static_cast<qint64>(c.doneBlocks().front()->startOffset / PARTSIZE));
        bar.insert(QStringLiteral("next"), next);
        QCborArray sent;
        for (const auto* block : c.doneBlocks()) {
            if (sent.size() >= 64)
                break;
            if (block) {
                sent.append(QCborArray{static_cast<qint64>(block->startOffset),
                                       static_cast<qint64>(block->endOffset)});
            }
        }
        bar.insert(QStringLiteral("sent"), sent);
        m.insert(QStringLiteral("upStatus"), bar);
    }
    return m;
}

/// The child rows of one download: its sources, then the ones currently asking
/// another file (MFC's UNAVAILABLE_SOURCE rows), marked "a4af".
[[nodiscard]] inline QCborArray downloadSourcesToCbor(const PartFile& file)
{
    QCborArray clients;
    for (const auto* c : file.srcList())
        clients.append(toCbor(*c));
    for (const auto* c : file.a4afSrcList()) {
        QCborMap m = toCbor(*c);
        m.insert(QStringLiteral("a4af"), true);
        m.insert(QStringLiteral("noNeededHere"), c->isInNoNeededList(&file));
        m.insert(QStringLiteral("swapSuspended"), c->reqFile() && c->isSwapSuspended(c->reqFile()));
        clients.append(m);
    }
    return clients;
}

// ---------------------------------------------------------------------------
// Extended client serializer for detail dialog
// ---------------------------------------------------------------------------

} // namespace eMule::Ipc

#include "app/AppContext.h"
#include "net/EMSocket.h"
#include "prefs/Preferences.h"

namespace eMule::Ipc {

[[nodiscard]] inline QCborMap toCborDetailed(const UpDownClient& c, AppContext& app)
{
    QCborMap m = toCbor(c);

    // Low / High ID
    m.insert(QStringLiteral("hasLowID"), c.hasLowID());

    // Server info
    m.insert(QStringLiteral("serverIP"),   static_cast<qint64>(c.serverAddress().toNetworkUint32()));
    m.insert(QStringLiteral("serverAddr"), c.serverAddress().toString());   // IPv6-capable form
    m.insert(QStringLiteral("serverPort"), static_cast<qint64>(c.serverPort()));
    if (!c.serverAddress().isNull() && app.serverList) {
        if (auto* srv = app.serverList->findByIPTcp(c.serverAddress().toNetworkUint32(), c.serverPort()))
            m.insert(QStringLiteral("serverName"), srv->name());
    }

    // Kad
    m.insert(QStringLiteral("kadConnected"), c.kadPort() != 0);

    // Tokens; the GUI has MFC's texts (srchybrid/ClientDetailDialog.cpp:93-150).
    // Obfuscation: "none" | "enabled" | "supported"
    const char* obfuscation = "none";
    if (c.supportsCryptLayer()) {
        const bool inUse = thePrefs.cryptLayerSupported()
            && (c.requestsCryptLayer() || thePrefs.cryptLayerRequested())
            && (c.isObfuscatedConnectionEstablished() || !c.socket() || !c.socket()->isConnected());
        obfuscation = inUse ? "enabled" : "supported";
    }
    m.insert(QStringLiteral("obfuscation"), QLatin1String(obfuscation));

    // Without credits MFC shows "?" for the totals, the modifier and the ident state
    m.insert(QStringLiteral("creditsKnown"), c.credits() != nullptr);
    // Identification: "ok" | "failed" | "none". userAddress(), the key scoreRatio() uses.
    if (c.credits()) {
        const char* ident = "none";
        if (app.clientCredits && app.clientCredits->cryptoAvailable()) {
            switch (c.credits()->currentIdentState(c.userAddress())) {
            case IdentState::Identified: ident = "ok"; break;
            case IdentState::IdNeeded:
            case IdentState::IdFailed:
            case IdentState::IdBadGuy:   ident = "failed"; break;
            default:                     break;
            }
        }
        m.insert(QStringLiteral("identification"), QLatin1String(ident));
        m.insert(QStringLiteral("scoreRatio"),      static_cast<double>(c.credits()->scoreRatio(c.userAddress())));
    } else {
        m.insert(QStringLiteral("identification"), QStringLiteral("none"));
        m.insert(QStringLiteral("scoreRatio"),      1.0);
    }
    m.insert(QStringLiteral("hasValidHash"), c.hasValidHash());
    m.insert(QStringLiteral("noViewSharedFiles"), !c.viewSharedFilesSupport());
    // MFC shows "-" for the queue score of a client that is not in our upload path
    m.insert(QStringLiteral("uploadIdle"), c.uploadState() == UploadState::None);

    // Queue score — MFC ClientDetailDialog.cpp:159,166 (IDC_DRATING / IDC_DSCORE).
    //
    // isUploadingToPeer() is MFC's IsDownloading(): "this peer is downloading from us". NOT
    // isDownloadingFromPeer(), which means the opposite and is what this used to pass.
    //
    // score() works in milliseconds where MFC's GetScore works in seconds (it divides by
    // SEC2MS(1.0f) at srchybrid/UploadClient.cpp:225). Both of these are display-only fields
    // shown verbatim in Client Details, so convert at the boundary rather than distorting the
    // queue's internal scale.
    const bool holdsSlot = c.isUploadingToPeer();
    m.insert(QStringLiteral("score"),  static_cast<qint64>(c.score(false, holdsSlot, false) / 1000));
    m.insert(QStringLiteral("rating"), static_cast<qint64>(c.score(false, holdsSlot, true) / 1000));

    // Friend slot
    m.insert(QStringLiteral("friendSlot"), c.friendSlot());

    return m;
}

} // namespace eMule::Ipc
