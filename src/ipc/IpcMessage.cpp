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

IpcMessage& IpcMessage::append(int64_t value)
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
    msg.append(static_cast<int64_t>(code));
    msg.append(message);
    return msg;
}

} // namespace eMule::Ipc
