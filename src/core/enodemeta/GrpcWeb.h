#pragma once

/// @file GrpcWeb.h
/// @brief Minimal unary gRPC-Web framing (application/grpc-web+proto).
///
/// Messages come from the shared .proto (QtProtobuf); only the framing is
/// hand-written, so no QtGrpc/HTTP2 stack is needed. Both eNode Meta API
/// listeners accept gRPC-Web over HTTP/1.1.
///
///   frame   := flag(1) length(4, big endian) payload(length)
///   flag    := 0x00 data (protobuf message) | 0x80 trailers
///   trailers:= "grpc-status: N\r\ngrpc-message: ...\r\n..."
///
/// A trailers-only error carries grpc-status in the HTTP headers, no body.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

#include <expected>
#include <optional>

namespace eMule::enodemeta::grpcweb {

inline constexpr auto kContentType = "application/grpc-web+proto";

/// gRPC status codes used by the Meta API.
enum StatusCode : int {
    Ok                = 0,
    Cancelled         = 1,
    Unknown           = 2,
    InvalidArgument   = 3,
    DeadlineExceeded  = 4,
    NotFound          = 5,
    PermissionDenied  = 7,
    ResourceExhausted = 8,
    Unimplemented     = 12,
    Internal          = 13,
    Unavailable       = 14,
    DataLoss          = 15,
    Unauthenticated   = 16,
};

/// Wrap one serialized message into a data frame.
[[nodiscard]] QByteArray frame(const QByteArray& message);

/// A parsed unary response.
struct Response {
    int status = -1;                 ///< grpc-status; -1 = none seen
    QString message;                 ///< grpc-message, percent-decoded
    std::optional<QByteArray> data;  ///< the one data frame's payload
    QByteArray statusDetails;        ///< decoded grpc-status-details-bin (google.rpc.Status)
};

using HeaderList = QList<QPair<QByteArray, QByteArray>>;

/// Parse a response body; @p headers are the HTTP headers (trailers-only case).
[[nodiscard]] std::expected<Response, QString> parseResponse(const QByteArray& body,
                                                             const HeaderList& headers);

/// Payload of the first google.protobuf.Any in a google.rpc.Status whose
/// type_url ends in @p typeName (e.g. "enode.meta.v1.ErrorInfo").
[[nodiscard]] std::optional<QByteArray> statusDetail(const QByteArray& rpcStatus,
                                                     QByteArrayView typeName);

/// Short English text for a status code.
[[nodiscard]] QString statusName(int code);

} // namespace eMule::enodemeta::grpcweb
