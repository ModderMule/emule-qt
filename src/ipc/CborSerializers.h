#pragma once

/// @file CborSerializers.h
/// @brief Header-only CBOR serializers for core entity types.
///
/// Mirrors JsonSerializers.h but produces QCborMap instead of QJsonObject.
/// Include only from daemon code that has access to core types.

#include "client/UpDownClient.h"
#include "files/AbstractFile.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "friends/Friend.h"
#include "search/SearchFile.h"
#include "server/Server.h"
#include "utils/OtherFunctions.h"

#include <QCborArray>
#include <QCborMap>

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
        {QStringLiteral("files"),       static_cast<qint64>(s.files())},
        {QStringLiteral("ping"),        static_cast<qint64>(s.ping())},
        {QStringLiteral("failedCount"), static_cast<qint64>(s.failedCount())},
        {QStringLiteral("preference"),  static_cast<int>(s.preference())},
    };
}

[[nodiscard]] inline QCborMap toCbor(const Friend& f)
{
    return QCborMap{
        {QStringLiteral("hash"),     f.hasUserhash() ? md4str(f.userHash().data()) : QString()},
        {QStringLiteral("name"),     f.name()},
        {QStringLiteral("ip"),       static_cast<qint64>(f.lastUsedIP())},
        {QStringLiteral("port"),     f.lastUsedPort()},
        {QStringLiteral("lastSeen"), static_cast<qint64>(f.lastSeen())},
    };
}

[[nodiscard]] inline QCborMap toCbor(const SearchFile& f)
{
    return QCborMap{
        {QStringLiteral("hash"),        md4str(f.fileHash())},
        {QStringLiteral("fileName"),    f.fileName()},
        {QStringLiteral("fileSize"),    static_cast<qint64>(f.fileSize())},
        {QStringLiteral("sourceCount"), static_cast<qint64>(f.sourceCount())},
    };
}

[[nodiscard]] inline QCborMap toCbor(const UpDownClient& c)
{
    QCborMap m;
    m.insert(QStringLiteral("userName"),        c.userName());
    m.insert(QStringLiteral("userHash"),        md4str(c.userHash()));
    m.insert(QStringLiteral("software"),        c.clientSoftwareStr());
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
    m.insert(QStringLiteral("partCount"),       c.partCount());
    m.insert(QStringLiteral("fileName"),        c.clientFilename());
    m.insert(QStringLiteral("remoteQueueRank"), static_cast<qint64>(c.remoteQueueRank()));
    m.insert(QStringLiteral("availPartCount"),  c.availablePartCount());
    // File info
    if (c.reqFile())
        m.insert(QStringLiteral("reqFileName"), c.reqFile()->fileName());
    if (c.uploadFile())
        m.insert(QStringLiteral("uploadFileName"), c.uploadFile()->fileName());
    return m;
}

} // namespace eMule::Ipc
