#pragma once

/// @file StreamTransport.h
/// @brief A reliable byte stream that is not a TCP socket, carried under EMSocket.
///
/// EncryptedStreamSocket stays a QTcpSocket for everything above it (peerAddress(),
/// state(), signals); with a transport attached its reads, writes and backlog go here
/// instead of a descriptor. Used by the QUIC NAT-T transport.
///
/// Threads: everything runs on the owner thread (the socket's) except write(),
/// pendingSend() and blocksWrite(), which the upload throttler calls from its own.

#include "net/Address.h"

#include <QAbstractSocket>
#include <QtGlobal>

namespace eMule {

/// What the socket is told. Always called on the owner thread.
class StreamTransportSink {
public:
    virtual ~StreamTransportSink() = default;
    /// Bytes are readable.
    virtual void transportReadable() = 0;
    /// @p bytes left the send queue (acked or handed over); room may have opened.
    virtual void transportWritten(qint64 bytes) = 0;
    /// The stream ended. Not called after close().
    virtual void transportLost(QAbstractSocket::SocketError error, const QString& reason) = 0;
};

class StreamTransport {
public:
    virtual ~StreamTransport() = default;

    virtual void setSink(StreamTransportSink* sink) = 0;

    // --- owner thread ---
    [[nodiscard]] virtual qint64 bytesAvailable() const = 0;
    /// Up to @p maxLen bytes; 0 when nothing is buffered, -1 once the stream is gone.
    virtual qint64 read(char* data, qint64 maxLen) = 0;
    /// Ends the stream. Twice is harmless; no transportLost() follows.
    virtual void close() = 0;
    [[nodiscard]] virtual Endpoint peerEndpoint() const = 0;
    [[nodiscard]] virtual Endpoint localEndpoint() const = 0;
    /// Raise the send queue limit (EMSocket::useBigSendBuffer). Returns the limit now in force.
    virtual qint64 setSendBufferLimit(qint64 bytes) = 0;

    // --- any thread ---
    /// Bytes accepted, all or part of @p len; 0 = queue full (try again), -1 = gone.
    virtual qint64 write(const char* data, qint64 len) = 0;
    /// Bytes accepted but not yet acknowledged by the peer.
    [[nodiscard]] virtual qint64 pendingSend() const = 0;
    /// The send queue is at its limit.
    [[nodiscard]] virtual bool blocksWrite() const = 0;
};

} // namespace eMule
