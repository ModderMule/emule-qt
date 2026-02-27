/// @file IpcProtocol.cpp
/// @brief IPC wire protocol framing implementation.

#include "IpcProtocol.h"

#include <QCborStreamReader>
#include <QCborStreamWriter>
#include <QCborValue>
#include <QtEndian>

namespace eMule::Ipc {

// ---------------------------------------------------------------------------
// encodeFrame
// ---------------------------------------------------------------------------

QByteArray encodeFrame(const QByteArray& cborPayload)
{
    QByteArray frame;
    frame.reserve(FrameHeaderSize + cborPayload.size());

    // Write big-endian uint32 length prefix
    const auto len = static_cast<uint32_t>(cborPayload.size());
    uint8_t header[FrameHeaderSize];
    qToBigEndian(len, header);
    frame.append(reinterpret_cast<const char*>(header), FrameHeaderSize);

    frame.append(cborPayload);
    return frame;
}

QByteArray encodeFrame(const QCborArray& message)
{
    return encodeFrame(message.toCborValue().toCbor());
}

// ---------------------------------------------------------------------------
// tryDecodeFrame
// ---------------------------------------------------------------------------

std::optional<DecodeResult> tryDecodeFrame(const QByteArray& buffer)
{
    if (buffer.size() < FrameHeaderSize)
        return std::nullopt;

    // Read big-endian uint32 payload length
    const auto payloadLen = qFromBigEndian<uint32_t>(
        reinterpret_cast<const uint8_t*>(buffer.constData()));

    if (payloadLen > MaxPayloadSize)
        return std::nullopt;  // Oversized — caller should disconnect

    const int totalLen = FrameHeaderSize + static_cast<int>(payloadLen);
    if (buffer.size() < totalLen)
        return std::nullopt;  // Incomplete frame

    // Extract and decode CBOR payload
    const QByteArray payload = buffer.mid(FrameHeaderSize, static_cast<int>(payloadLen));
    QCborValue value = QCborValue::fromCbor(payload);

    if (!value.isArray())
        return std::nullopt;  // Protocol violation

    return DecodeResult{value.toArray(), totalLen};
}

} // namespace eMule::Ipc
