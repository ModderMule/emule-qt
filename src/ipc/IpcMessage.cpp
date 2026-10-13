/// @file IpcMessage.cpp
/// @brief Type-safe QCborArray wrapper for IPC messages — implementation.

#include "IpcMessage.h"

namespace eMule::Ipc {

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

IpcMessage::IpcMessage(IpcMsgType type, int seqId)
{
    m_array.append(static_cast<int>(type));
    m_array.append(seqId);
}

IpcMessage::IpcMessage(QCborArray array)
    : m_array(std::move(array))
{
}

// ---------------------------------------------------------------------------
// Header access
// ---------------------------------------------------------------------------

IpcMsgType IpcMessage::type() const
{
    if (m_array.size() < 2)
        return static_cast<IpcMsgType>(-1);
    return static_cast<IpcMsgType>(m_array.at(0).toInteger(-1));
}

int IpcMessage::seqId() const
{
    if (m_array.size() < 2)
        return 0;
    return static_cast<int>(m_array.at(1).toInteger(0));
}

// ---------------------------------------------------------------------------
// Payload field access
// ---------------------------------------------------------------------------

int IpcMessage::fieldCount() const
{
    return std::max(0, static_cast<int>(m_array.size()) - 2);
}

QCborValue IpcMessage::field(int index) const
{
    const int arrayIndex = index + 2;
    if (arrayIndex < 0 || arrayIndex >= m_array.size())
        return {};
    return m_array.at(arrayIndex);
}

QString IpcMessage::fieldString(int index) const
{
    return field(index).toString();
}

int64_t IpcMessage::fieldInt(int index) const
{
    return field(index).toInteger();
}

bool IpcMessage::fieldBool(int index) const
{
    return field(index).toBool();
}

QCborMap IpcMessage::fieldMap(int index) const
{
    return field(index).toMap();
}

QCborArray IpcMessage::fieldArray(int index) const
{
    return field(index).toArray();
}

// ---------------------------------------------------------------------------
// Payload building
// ---------------------------------------------------------------------------

IpcMessage& IpcMessage::append(const QCborValue& value)
{
    m_array.append(value);
    return *this;
}

IpcMessage& IpcMessage::append(const QString& value)
{
    m_array.append(value);
    return *this;
}

IpcMessage& IpcMessage::append(qint64 value)
{
    m_array.append(value);
    return *this;
}

IpcMessage& IpcMessage::append(bool value)
{
    m_array.append(value);
    return *this;
}

IpcMessage& IpcMessage::append(const QCborMap& value)
{
    m_array.append(value);
    return *this;
}

IpcMessage& IpcMessage::append(const QCborArray& value)
{
    m_array.append(QCborValue(value));
    return *this;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

QByteArray IpcMessage::toFrame() const
{
    return encodeFrame(m_array);
}

const QCborArray& IpcMessage::toArray() const
{
    return m_array;
}

bool IpcMessage::isValid() const
{
    if (m_array.size() < 2)
        return false;
    const auto t = static_cast<int>(m_array.at(0).toInteger(-1));
    return t >= 100;  // All valid types are >= 100
}

// ---------------------------------------------------------------------------
// Factory methods
// ---------------------------------------------------------------------------

IpcMessage IpcMessage::makeResult(int seqId, bool success, const QCborValue& data)
{
    IpcMessage msg(IpcMsgType::Result, seqId);
    msg.append(success);
    if (!data.isNull() && !data.isUndefined())
        msg.append(data);
    return msg;
}

IpcMessage IpcMessage::makeError(int seqId, int code, const QString& message)
{
    IpcMessage msg(IpcMsgType::Error, seqId);
    msg.append(static_cast<qint64>(code));
    msg.append(message);
    return msg;
}

quint64 snapshotPushKey(const IpcMessage& msg)
{
    if (msg.seqId() != 0)
        return 0;   // a reply, never a push

    quint32 subKey = 0;
    switch (msg.type()) {
    case IpcMsgType::PushStatsUpdate:
    case IpcMsgType::PushDownloadUpdate:
    case IpcMsgType::PushDownloadAdded:
    case IpcMsgType::PushDownloadRemoved:
    case IpcMsgType::PushServerState:
    case IpcMsgType::PushUploadUpdate:
    case IpcMsgType::PushKadUpdate:
    case IpcMsgType::PushKadSearchesChanged:
    case IpcMsgType::PushKnownClientsChanged:
    case IpcMsgType::PushFriendListChanged:
    case IpcMsgType::PushPortMapStatus:
    case IpcMsgType::PushCategoriesChanged:
    case IpcMsgType::PushCoreLoading:
        break;
    // One state per search: field 0 is the search id
    case IpcMsgType::PushSearchResult:
    case IpcMsgType::PushGlobalSearchProgress:
    case IpcMsgType::PushIndexerProgress:
        subKey = static_cast<quint32>(msg.fieldInt(0));
        break;
    default:
        return 0;
    }
    return (static_cast<quint64>(static_cast<quint32>(msg.type())) << 32) | subKey;
}

bool isReadOnlyRequest(IpcMsgType type)
{
    switch (type) {
    case IpcMsgType::GetDownloads:
    case IpcMsgType::GetDownload:
    case IpcMsgType::GetDownloadSources:
    case IpcMsgType::GetUploads:
    case IpcMsgType::GetDownloadClients:
    case IpcMsgType::GetKnownClients:
    case IpcMsgType::GetServers:
    case IpcMsgType::GetConnection:
    case IpcMsgType::GetSearchResults:
    case IpcMsgType::GetKnownTypes:
    case IpcMsgType::GetSharedFiles:
    case IpcMsgType::GetFriends:
    case IpcMsgType::GetStats:
    case IpcMsgType::GetPreferences:
    case IpcMsgType::GetKadContacts:
    case IpcMsgType::GetKadStatus:
    case IpcMsgType::GetKadSearches:
    case IpcMsgType::GetKadLookupHistory:
    case IpcMsgType::GetNetworkInfo:
    case IpcMsgType::GetSchedules:
    case IpcMsgType::GetNetworkInterfaces:
    case IpcMsgType::GetConvertJobs:
    case IpcMsgType::GetDownloadDetails:
    case IpcMsgType::GetClientDetails:
    case IpcMsgType::GetSharedFileDetails:
    case IpcMsgType::GetServerState:
    case IpcMsgType::GetServerMessages:
    case IpcMsgType::GetSearchResultDetails:
    case IpcMsgType::GetKadStats:
    case IpcMsgType::GetClientStats:
    case IpcMsgType::GetGeoIpStatus:
    case IpcMsgType::GetSpeedHistory:
    case IpcMsgType::GetStatsHistory:
    case IpcMsgType::GetCategories:
    case IpcMsgType::GetSharedDirState:
    case IpcMsgType::GetKnownFiles:
    case IpcMsgType::GetIndexers:
    case IpcMsgType::GetIndexerFeeds:
    case IpcMsgType::GetNewsServers:
    case IpcMsgType::GetUsenetQueue:
    case IpcMsgType::GetUsenetKnownTypes:
    case IpcMsgType::GetUsenetItemDetails:
    case IpcMsgType::GetUsenetStats:
        return true;
    default:
        return false;
    }
}

QByteArray requestIdentity(const IpcMessage& msg)
{
    QCborArray arr = msg.toArray();
    if (arr.size() >= 2)
        arr[1] = 0;
    return arr.toCborValue().toCbor();
}

} // namespace eMule::Ipc
