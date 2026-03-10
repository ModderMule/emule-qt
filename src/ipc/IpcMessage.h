#pragma once

/// @file IpcMessage.h
/// @brief Type-safe wrapper around QCborArray for IPC messages.
///
/// Provides convenient access to the standard fields:
///   [0] msgType (IpcMsgType)
///   [1] seqId   (int, 0 for push events)
///   [2+] payload fields

#include "IpcProtocol.h"

#include <QCborMap>
#include <QCborValue>
#include <QString>

namespace eMule::Ipc {

class IpcMessage {
public:
    /// Construct an empty (invalid) message.
    IpcMessage() = default;

    /// Construct a message with the given type and sequence ID.
    explicit IpcMessage(IpcMsgType type, int seqId = 0);

    /// Construct from a raw QCborArray (e.g. from tryDecodeFrame).
    explicit IpcMessage(QCborArray array);

    // -- Header access --------------------------------------------------------

    /// Message type (index 0). Returns -1 cast to IpcMsgType if invalid.
    [[nodiscard]] IpcMsgType type() const;

    /// Sequence ID (index 1). Returns 0 if invalid.
    [[nodiscard]] int seqId() const;

    // -- Payload field access (index 2+) -------------------------------------

    /// Number of payload fields (total array size minus 2 header fields).
    [[nodiscard]] int fieldCount() const;

    /// Get payload field at @p index (0-based, maps to array index+2).
    [[nodiscard]] QCborValue field(int index) const;

    /// Get payload field as string.
    [[nodiscard]] QString fieldString(int index) const;

    /// Get payload field as integer.
    [[nodiscard]] int64_t fieldInt(int index) const;

    /// Get payload field as bool.
    [[nodiscard]] bool fieldBool(int index) const;

    /// Get payload field as QCborMap.
    [[nodiscard]] QCborMap fieldMap(int index) const;

    /// Get payload field as QCborArray.
    [[nodiscard]] QCborArray fieldArray(int index) const;

    // -- Payload building -----------------------------------------------------

    /// Append a value to the message payload.
    IpcMessage& append(const QCborValue& value);
    IpcMessage& append(const QString& value);
    IpcMessage& append(qint64 value);
    IpcMessage& append(bool value);
    IpcMessage& append(const QCborMap& value);
    IpcMessage& append(const QCborArray& value);

    // -- Serialization --------------------------------------------------------

    /// Encode to a length-prefixed frame ready for transmission.
    [[nodiscard]] QByteArray toFrame() const;

    /// Access the underlying QCborArray.
    [[nodiscard]] const QCborArray& toArray() const;

    /// Returns true if the message has a valid type and seqId.
    [[nodiscard]] bool isValid() const;

    // -- Factory methods ------------------------------------------------------

    /// Create a Result response.
    [[nodiscard]] static IpcMessage makeResult(int seqId, bool success, const QCborValue& data = {});

    /// Create an Error response.
    [[nodiscard]] static IpcMessage makeError(int seqId, int code, const QString& message);

private:
    QCborArray m_array;
};

} // namespace eMule::Ipc
