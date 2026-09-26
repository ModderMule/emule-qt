#include "pch.h"
/// @file GrpcWeb.cpp
/// @brief Minimal unary gRPC-Web framing (application/grpc-web+proto).

#include "enodemeta/GrpcWeb.h"

#include <QUrl>

namespace eMule::enodemeta::grpcweb {

namespace {

// --- tiny protobuf wire reader, only for google.rpc.Status/Any ---------------

bool readVarint(const QByteArray& d, qsizetype& pos, quint64& out)
{
    out = 0;
    for (int shift = 0; shift < 64 && pos < d.size(); shift += 7) {
        const auto b = static_cast<uint8_t>(d.at(pos++));
        out |= static_cast<quint64>(b & 0x7F) << shift;
        if (!(b & 0x80))
            return true;
    }
    return false;
}

/// Next field; for wire type 2 @p bytes holds the payload. False at end/error.
bool nextField(const QByteArray& d, qsizetype& pos, quint32& field, QByteArray& bytes, bool& ok)
{
    ok = true;
    if (pos >= d.size())
        return false;
    quint64 key = 0;
    if (!readVarint(d, pos, key)) {
        ok = false;
        return false;
    }
    field = static_cast<quint32>(key >> 3);
    bytes.clear();
    switch (key & 7) {
    case 0: {
        quint64 v = 0;
        ok = readVarint(d, pos, v);
        break;
    }
    case 1: pos += 8; break;
    case 5: pos += 4; break;
    case 2: {
        quint64 len = 0;
        if (!readVarint(d, pos, len) || len > static_cast<quint64>(d.size() - pos)) {
            ok = false;
            break;
        }
        bytes = d.mid(pos, static_cast<qsizetype>(len));
        pos += static_cast<qsizetype>(len);
        break;
    }
    default: ok = false; break;
    }
    if (pos > d.size())
        ok = false;
    return ok;
}

void applyTrailerLine(Response& r, QByteArrayView keyRaw, QByteArrayView valueRaw)
{
    const QByteArray key = keyRaw.trimmed().toByteArray().toLower();
    const QByteArray value = valueRaw.trimmed().toByteArray();
    if (key == "grpc-status") {
        bool ok = false;
        const int s = value.toInt(&ok);
        if (ok)
            r.status = s;
    } else if (key == "grpc-message") {
        r.message = QUrl::fromPercentEncoding(value);
    } else if (key == "grpc-status-details-bin") {
        // unpadded base64 is the norm here
        r.statusDetails = QByteArray::fromBase64(value);
    }
}

} // namespace

QByteArray frame(const QByteArray& message)
{
    QByteArray out;
    out.reserve(5 + message.size());
    const auto n = static_cast<quint32>(message.size());
    out.append('\0');
    out.append(static_cast<char>((n >> 24) & 0xFF));
    out.append(static_cast<char>((n >> 16) & 0xFF));
    out.append(static_cast<char>((n >> 8) & 0xFF));
    out.append(static_cast<char>(n & 0xFF));
    out.append(message);
    return out;
}

std::expected<Response, QString> parseResponse(const QByteArray& body, const HeaderList& headers)
{
    Response r;
    for (const auto& [k, v] : headers)
        applyTrailerLine(r, k, v);

    qsizetype pos = 0;
    while (pos < body.size()) {
        if (body.size() - pos < 5)
            return std::unexpected(QStringLiteral("truncated gRPC-Web frame header"));
        const auto flag = static_cast<uint8_t>(body.at(pos));
        const quint32 len = (static_cast<quint32>(static_cast<uint8_t>(body.at(pos + 1))) << 24)
                          | (static_cast<quint32>(static_cast<uint8_t>(body.at(pos + 2))) << 16)
                          | (static_cast<quint32>(static_cast<uint8_t>(body.at(pos + 3))) << 8)
                          |  static_cast<quint32>(static_cast<uint8_t>(body.at(pos + 4)));
        pos += 5;
        if (len > static_cast<quint64>(body.size() - pos))
            return std::unexpected(QStringLiteral("truncated gRPC-Web frame"));
        const QByteArray payload = body.mid(pos, len);
        pos += len;

        if (flag & 0x80) {
            for (const QByteArray& line : payload.split('\n')) {
                const qsizetype colon = line.indexOf(':');
                if (colon > 0)
                    applyTrailerLine(r, QByteArrayView(line).left(colon),
                                     QByteArrayView(line).mid(colon + 1));
            }
        } else {
            if (flag & 0x01)
                return std::unexpected(QStringLiteral("compressed gRPC-Web frames are not supported"));
            if (r.data)
                return std::unexpected(QStringLiteral("more than one message in a unary reply"));
            r.data = payload;
        }
    }

    if (r.status < 0)
        return std::unexpected(QStringLiteral("gRPC-Web reply carries no status"));
    return r;
}

std::optional<QByteArray> statusDetail(const QByteArray& rpcStatus, QByteArrayView typeName)
{
    // google.rpc.Status { int32 code = 1; string message = 2; repeated Any details = 3; }
    // google.protobuf.Any { string type_url = 1; bytes value = 2; }
    qsizetype pos = 0;
    quint32 field = 0;
    QByteArray bytes;
    bool ok = true;
    while (nextField(rpcStatus, pos, field, bytes, ok)) {
        if (field != 3)
            continue;
        qsizetype apos = 0;
        quint32 afield = 0;
        QByteArray abytes;
        bool aok = true;
        QByteArray typeUrl;
        QByteArray value;
        while (nextField(bytes, apos, afield, abytes, aok)) {
            if (afield == 1)
                typeUrl = abytes;
            else if (afield == 2)
                value = abytes;
        }
        if (aok && typeUrl.endsWith(typeName))
            return value;
    }
    return std::nullopt;
}

QString statusName(int code)
{
    switch (code) {
    case Ok:                return QStringLiteral("ok");
    case Cancelled:         return QStringLiteral("cancelled");
    case InvalidArgument:   return QStringLiteral("invalid argument");
    case DeadlineExceeded:  return QStringLiteral("deadline exceeded");
    case NotFound:          return QStringLiteral("not found");
    case PermissionDenied:  return QStringLiteral("permission denied");
    case ResourceExhausted: return QStringLiteral("resource exhausted");
    case Unimplemented:     return QStringLiteral("unimplemented");
    case Internal:          return QStringLiteral("internal error");
    case Unavailable:       return QStringLiteral("unavailable");
    case DataLoss:          return QStringLiteral("data loss");
    case Unauthenticated:   return QStringLiteral("unauthenticated");
    default:                return QStringLiteral("error %1").arg(code);
    }
}

} // namespace eMule::enodemeta::grpcweb
