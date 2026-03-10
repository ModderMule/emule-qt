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
#include "search/SearchFile.h"
#include "server/Server.h"
#include "server/ServerList.h"
#include "utils/OtherFunctions.h"

#include <QCborArray>
#include <QCborMap>

#include <set>

namespace eMule::Ipc {

// ---------------------------------------------------------------------------
// Status / priority to string helpers (reuse from JsonSerializers)
// ---------------------------------------------------------------------------

[[nodiscard]] inline QString statusToString(PartFileStatus s)
{
    switch (s) {
    case PartFileStatus::Ready:        return QStringLiteral("ready");
    case PartFileStatus::Empty:        return QStringLiteral("empty");
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

[[nodiscard]] inline QCborMap toCbor(const PartFile& f)
{
    return QCborMap{
        {QStringLiteral("hash"),                 md4str(f.fileHash())},
        {QStringLiteral("fileName"),             f.fileName()},
        {QStringLiteral("fileSize"),             static_cast<qint64>(f.fileSize())},
        {QStringLiteral("completedSize"),        static_cast<qint64>(f.completedSize())},
        {QStringLiteral("percentCompleted"),     static_cast<double>(f.percentCompleted())},
        {QStringLiteral("status"),               statusToString(f.status())},
        {QStringLiteral("datarate"),             static_cast<qint64>(f.datarate())},
        {QStringLiteral("sourceCount"),          f.sourceCount()},
        {QStringLiteral("transferringSrcCount"), f.transferringSrcCount()},
        {QStringLiteral("downPriority"),         priorityToString(f.downPriority())},
        {QStringLiteral("isAutoDownPriority"),   f.isAutoDownPriority()},
        {QStringLiteral("isPaused"),             f.isPaused()},
        {QStringLiteral("isStopped"),            f.isStopped()},
        {QStringLiteral("category"),             static_cast<qint64>(f.category())},
        {QStringLiteral("lastSeenComplete"),    static_cast<qint64>(f.completeSourcesTime())},
        {QStringLiteral("lastReception"),       static_cast<qint64>(f.lastReceptionDate())},
        {QStringLiteral("addedOn"),             static_cast<qint64>(f.createdDate())},
        {QStringLiteral("fileType"),            f.fileType()},
        {QStringLiteral("requests"),            static_cast<qint64>(f.statistic.allTimeRequests())},
        {QStringLiteral("acceptedReqs"),        static_cast<qint64>(f.statistic.allTimeAccepts())},
        {QStringLiteral("transferredData"),     static_cast<qint64>(f.statistic.allTimeTransferred())},
        {QStringLiteral("partMap"),             buildPartMap(f)},
        {QStringLiteral("isPreviewPossible"),  f.isPreviewPossible()},
    };
}

[[nodiscard]] inline QCborMap toCbor(const Server& s)
{
    return QCborMap{
        {QStringLiteral("name"),        s.name()},
        {QStringLiteral("address"),     s.address()},
        {QStringLiteral("ip"),          static_cast<qint64>(s.ip())},
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
        {QStringLiteral("softFiles"),   static_cast<qint64>(s.softFiles())},
        {QStringLiteral("lowIDUsers"),  static_cast<qint64>(s.lowIDUsers())},
        {QStringLiteral("obfuscation"), s.supportsObfuscationTCP()},
        {QStringLiteral("serverId"),    static_cast<qint64>(s.serverId())},
    };
}

[[nodiscard]] inline QCborMap toCbor(const Friend& f)
{
    return QCborMap{
        {QStringLiteral("hash"),        f.hasUserhash() ? md4str(f.userHash().data()) : QString()},
        {QStringLiteral("name"),        f.name()},
        {QStringLiteral("ip"),          static_cast<qint64>(f.lastUsedIP())},
        {QStringLiteral("port"),        f.lastUsedPort()},
        {QStringLiteral("lastSeen"),    static_cast<qint64>(f.lastSeen())},
        {QStringLiteral("lastChatted"), static_cast<qint64>(f.lastChatted())},
        {QStringLiteral("friendSlot"),  f.friendSlot()},
        {QStringLiteral("kadID"),       md4str(f.kadID().data())},
    };
}

[[nodiscard]] inline QCborMap toCbor(const SearchFile& f)
{
    QCborMap m;
    m.insert(QStringLiteral("hash"),                md4str(f.fileHash()));
    m.insert(QStringLiteral("fileName"),            f.fileName());
    m.insert(QStringLiteral("fileSize"),            static_cast<qint64>(f.fileSize()));
    m.insert(QStringLiteral("sourceCount"),         static_cast<qint64>(f.sourceCount()));
    m.insert(QStringLiteral("completeSourceCount"), static_cast<qint64>(f.completeSourceCount()));
    m.insert(QStringLiteral("fileType"),            f.fileType());
    m.insert(QStringLiteral("searchID"),            static_cast<qint64>(f.searchID()));
    m.insert(QStringLiteral("knownType"),           static_cast<int>(f.knownType()));
    m.insert(QStringLiteral("isSpam"),              f.isConsideredSpam());
    // Media metadata from ED2K tags
    m.insert(QStringLiteral("artist"),  f.getStrTagValue(FT_MEDIA_ARTIST));
    m.insert(QStringLiteral("album"),   f.getStrTagValue(FT_MEDIA_ALBUM));
    m.insert(QStringLiteral("title"),   f.getStrTagValue(FT_MEDIA_TITLE));
    m.insert(QStringLiteral("length"),  static_cast<qint64>(f.getIntTagValue(FT_MEDIA_LENGTH)));
    m.insert(QStringLiteral("bitrate"), static_cast<qint64>(f.getIntTagValue(FT_MEDIA_BITRATE)));
    m.insert(QStringLiteral("codec"),   f.getStrTagValue(FT_MEDIA_CODEC));
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
    if (c.isDownloading() && c.sessionDown() > 0 && c.lastBlockOffset() != UINT64_MAX)
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
    m.insert(QStringLiteral("uploadState"),     c.uploadStateDisplayString());
    m.insert(QStringLiteral("downloadState"),   c.downloadStateDisplayString());
    m.insert(QStringLiteral("sourceFrom"),      static_cast<int>(c.sourceFrom()));
    // Upload fields
    m.insert(QStringLiteral("transferredUp"),   static_cast<qint64>(c.transferredUp()));
    m.insert(QStringLiteral("sessionUp"),       static_cast<qint64>(c.sessionUp()));
    m.insert(QStringLiteral("askedCount"),      static_cast<qint64>(c.askedCount()));
    m.insert(QStringLiteral("waitStartTime"),   static_cast<qint64>(c.waitStartTime()));
    m.insert(QStringLiteral("isBanned"),        c.isBanned());
    // Download fields
    m.insert(QStringLiteral("transferredDown"), static_cast<qint64>(c.transferredDown()));
    m.insert(QStringLiteral("sessionDown"),     static_cast<qint64>(c.sessionDown()));
    m.insert(QStringLiteral("datarate"),        static_cast<qint64>(c.downDatarate()));
    m.insert(QStringLiteral("partCount"),       c.partCount());
    m.insert(QStringLiteral("fileName"),        c.clientFilename());
    m.insert(QStringLiteral("remoteQueueRank"), static_cast<qint64>(c.remoteQueueRank()));
    m.insert(QStringLiteral("availPartCount"),  c.availablePartCount());
    // Client software identification
    m.insert(QStringLiteral("softwareId"), static_cast<int>(c.clientSoft()));
    m.insert(QStringLiteral("hasCredit"),  c.credits() ? (c.credits()->scoreRatio(c.connectIP()) > 1.0f) : false);
    m.insert(QStringLiteral("isFriend"),   c.friendPtr() != nullptr);
    // Network address
    m.insert(QStringLiteral("ip"),   static_cast<qint64>(c.connectIP()));
    m.insert(QStringLiteral("port"), static_cast<qint64>(c.userPort()));
    // Upload timing and connection state
    m.insert(QStringLiteral("uploadStartDelay"), static_cast<qint64>(c.getUpStartTimeDelay()));
    m.insert(QStringLiteral("fileRating"), static_cast<int>(c.fileRating()));
    m.insert(QStringLiteral("isConnected"), c.socket() != nullptr);
    // File info
    if (c.reqFile()) {
        m.insert(QStringLiteral("reqFileName"), c.reqFile()->fileName());
        m.insert(QStringLiteral("filePriority"), static_cast<int>(c.reqFile()->downPriority()));
        m.insert(QStringLiteral("isAutoPriority"), c.reqFile()->isAutoDownPriority());
    }
    if (c.uploadFile())
        m.insert(QStringLiteral("uploadFileName"), c.uploadFile()->fileName());
    if (auto spm = buildSourcePartMap(c); !spm.isEmpty())
        m.insert(QStringLiteral("sourcePartMap"), std::move(spm));
    return m;
}

// ---------------------------------------------------------------------------
// Extended client serializer for detail dialog
// ---------------------------------------------------------------------------

} // namespace eMule::Ipc

#include "app/AppContext.h"

namespace eMule::Ipc {

[[nodiscard]] inline QCborMap toCborDetailed(const UpDownClient& c, AppContext& app)
{
    QCborMap m = toCbor(c);

    // Low / High ID
    m.insert(QStringLiteral("hasLowID"), c.hasLowID());

    // Server info
    m.insert(QStringLiteral("serverIP"),   static_cast<qint64>(c.serverIP()));
    m.insert(QStringLiteral("serverPort"), static_cast<qint64>(c.serverPort()));
    if (c.serverIP() != 0 && app.serverList) {
        if (auto* srv = app.serverList->findByIPTcp(c.serverIP(), c.serverPort()))
            m.insert(QStringLiteral("serverName"), srv->name());
    }

    // Kad
    m.insert(QStringLiteral("kadConnected"), c.kadPort() != 0);

    // Obfuscation
    QString obfuStr;
    if (c.isObfuscatedConnectionEstablished())
        obfuStr = QStringLiteral("Enabled");
    else if (c.supportsCryptLayer())
        obfuStr = c.requestsCryptLayer() ? QStringLiteral("Supported (preferred)")
                                          : QStringLiteral("Supported");
    else
        obfuStr = QStringLiteral("Not supported");
    m.insert(QStringLiteral("obfuscation"), obfuStr);

    // Identification (credits)
    if (c.credits()) {
        const auto identState = c.credits()->currentIdentState(c.connectIP());
        QString identStr;
        switch (identState) {
        case IdentState::Identified:   identStr = QStringLiteral("Verified (secure)"); break;
        case IdentState::IdNeeded:     identStr = QStringLiteral("Not yet checked"); break;
        case IdentState::IdFailed:     identStr = QStringLiteral("Failed"); break;
        case IdentState::IdBadGuy:     identStr = QStringLiteral("Bad guy / fake"); break;
        default:                       identStr = QStringLiteral("Not available"); break;
        }
        m.insert(QStringLiteral("identification"), identStr);

        // Credit totals
        m.insert(QStringLiteral("downloadedTotal"), static_cast<qint64>(c.credits()->downloadedTotal()));
        m.insert(QStringLiteral("uploadedTotal"),   static_cast<qint64>(c.credits()->uploadedTotal()));
        m.insert(QStringLiteral("scoreRatio"),      static_cast<double>(c.credits()->scoreRatio(c.connectIP())));
    } else {
        m.insert(QStringLiteral("identification"), QStringLiteral("Not available"));
        m.insert(QStringLiteral("downloadedTotal"), 0);
        m.insert(QStringLiteral("uploadedTotal"),   0);
        m.insert(QStringLiteral("scoreRatio"),      1.0);
    }

    // Queue score
    m.insert(QStringLiteral("score"),  static_cast<qint64>(c.score(false, c.isDownloading(), false)));
    m.insert(QStringLiteral("rating"), static_cast<qint64>(c.score(false, c.isDownloading(), true)));

    // Friend slot
    m.insert(QStringLiteral("friendSlot"), c.friendSlot());

    return m;
}

} // namespace eMule::Ipc
