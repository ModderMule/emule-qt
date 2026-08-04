#include "pch.h"
/// @file PcpMessages.cpp
/// @brief PCP (RFC 6887) message codec.

#include "portmap/PcpMessages.h"
#include "utils/Log.h"

#include <openssl/rand.h>

#include <algorithm>

namespace eMule::pcp {

using portmap::appendBytes;
using portmap::appendU16;
using portmap::appendU32;
using portmap::appendZeros;
using portmap::decodeAddr128;
using portmap::encodeAddr128;
using portmap::readU32;

namespace {

/// Offsets into a MAP packet, so the field layout is stated once.
constexpr usize kOffNonce = 24;
constexpr usize kOffProtocol = 36;
constexpr usize kOffInternalPort = 40;
constexpr usize kOffExternalPort = 42;
constexpr usize kOffExternalAddr = 44;

/// Write the 24-byte common request header.
void appendRequestHeader(QByteArray& out, Opcode opcode, uint32 lifetimeSecs,
                         const Address& clientAddress)
{
    out.append(static_cast<char>(kVersion));
    out.append(static_cast<char>(opcode));           // R = 0 for a request
    appendZeros(out, 2);                             // reserved
    appendU32(out, lifetimeSecs);
    // The client address must be the source address the server actually sees,
    // or it answers ADDRESS_MISMATCH (section 7.4). Callers take it from the
    // connected socket's localAddress() rather than guessing.
    const auto client = encodeAddr128(clientAddress, !clientAddress.isIPv6());
    appendBytes(out, client);
}

/// Write the 36-byte MAP opcode data.
void appendMapData(QByteArray& out, const MapRequest& request,
                   uint16 suggestedPort, const Address& suggestedAddress)
{
    appendBytes(out, request.nonce);
    out.append(static_cast<char>(request.protocol));
    appendZeros(out, 3);                             // reserved
    appendU16(out, request.internalPort);
    appendU16(out, suggestedPort);
    const auto external = encodeAddr128(suggestedAddress, request.externalFamilyIsIPv4);
    appendBytes(out, external);
}

} // namespace

QString resultName(uint8 code)
{
    switch (code) {
    case 0:  return QStringLiteral("SUCCESS");
    case 1:  return QStringLiteral("UNSUPP_VERSION");
    case 2:  return QStringLiteral("NOT_AUTHORIZED");
    case 3:  return QStringLiteral("MALFORMED_REQUEST");
    case 4:  return QStringLiteral("UNSUPP_OPCODE");
    case 5:  return QStringLiteral("UNSUPP_OPTION");
    case 6:  return QStringLiteral("MALFORMED_OPTION");
    case 7:  return QStringLiteral("NETWORK_FAILURE");
    case 8:  return QStringLiteral("NO_RESOURCES");
    case 9:  return QStringLiteral("UNSUPP_PROTOCOL");
    case 10: return QStringLiteral("USER_EX_QUOTA");
    case 11: return QStringLiteral("CANNOT_PROVIDE_EXTERNAL");
    case 12: return QStringLiteral("ADDRESS_MISMATCH");
    case 13: return QStringLiteral("EXCESSIVE_REMOTE_PEERS");
    default: return QStringLiteral("unknown (%1)").arg(code);
    }
}

Nonce generateNonce()
{
    Nonce nonce{};
    if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
        // Never fall back to a predictable source: the nonce is what stops
        // another host on this LAN from stealing or deleting our mapping.
        logError(QStringLiteral("PCP: CSPRNG failed, cannot generate mapping nonce"));
        nonce.fill(0);
    }
    return nonce;
}

QByteArray encodeMapRequest(const MapRequest& request)
{
    QByteArray out;
    out.reserve(static_cast<qsizetype>(kMapPacketSize + 4));
    appendRequestHeader(out, Opcode::Map, request.lifetimeSecs, request.clientAddress);
    appendMapData(out, request, request.suggestedExternalPort, request.suggestedExternalAddress);

    if (request.preferFailure) {
        // Option TLV: code, reserved, 16-bit semantic length (0), no payload.
        out.append(static_cast<char>(OptionCode::PreferFailure));
        out.append('\0');
        appendU16(out, 0);
    }
    return out;
}

QByteArray encodeDeleteRequest(const MapRequest& request)
{
    QByteArray out;
    out.reserve(static_cast<qsizetype>(kMapPacketSize));
    appendRequestHeader(out, Opcode::Map, 0, request.clientAddress);
    // Section 15.1: suggested external port and address MUST be zero on a
    // delete. PREFER_FAILURE with lifetime 0 is MALFORMED_OPTION, so it is
    // never emitted here.
    appendMapData(out, request, 0, Address{});
    return out;
}

QByteArray encodeAnnounceRequest(const Address& clientAddress)
{
    QByteArray out;
    out.reserve(static_cast<qsizetype>(kHeaderSize));
    appendRequestHeader(out, Opcode::Announce, 0, clientAddress);
    return out;
}

std::optional<ResponseHeader> decodeHeader(std::span<const uint8> datagram)
{
    // Two octets, not four: section 8.3 orders the UNSUPP_VERSION check ahead of
    // the length checks, and a NAT-PMP-only router's rejection can be as short
    // as two bytes on real firmware.
    if (datagram.size() < 2 || datagram.size() > kMaxMessageSize)
        return std::nullopt;

    ResponseHeader header;
    header.version = datagram[0];
    header.isResponse = (datagram[1] & kResponseBit) != 0;
    header.opcode = datagram[1] & 0x7F;

    if (datagram.size() >= 4) {
        header.hasResultCode = true;
        // 8-bit result at offset 3; offset 2 is reserved.
        header.resultCode = datagram[3];
    }

    if (datagram.size() >= kHeaderSize) {
        header.hasFullHeader = true;
        header.lifetimeSecs = readU32(datagram, 4);
        header.epochTime = readU32(datagram, 8);
    }

    // Section 8.3 says to drop a datagram with the R bit clear. The exception is
    // a version-0 rejection: a NAT-PMP box that copied the pre-errata section
    // 3.5 figure sends OP=0, and discarding it would hide the one reply that
    // tells us to downgrade.
    if (!header.isResponse && header.version == kVersion)
        return std::nullopt;

    return header;
}

std::optional<MapResponse> decodeMapResponse(std::span<const uint8> datagram)
{
    const auto header = decodeHeader(datagram);
    if (!header.has_value())
        return std::nullopt;
    if (!header->hasFullHeader || datagram.size() < kMapPacketSize)
        return std::nullopt;
    if (datagram.size() % 4 != 0)
        return std::nullopt;
    if (header->version != kVersion || !header->isResponse)
        return std::nullopt;
    if (header->opcode != static_cast<uint8>(Opcode::Map))
        return std::nullopt;

    MapResponse response;
    response.version = header->version;
    response.opcode = Opcode::Map;
    response.resultCode = header->resultCode;
    response.lifetimeSecs = header->lifetimeSecs;
    response.epochTime = header->epochTime;
    std::copy_n(datagram.begin() + kOffNonce, kNonceSize, response.nonce.begin());
    response.protocol = static_cast<IpProtocol>(datagram[kOffProtocol]);
    response.internalPort = portmap::readU16(datagram, kOffInternalPort);

    // On an error the server echoes the request's opcode data (section 8.2), so
    // the port and address here are what we *asked* for, not an assignment.
    // Reporting them as assigned would publish a port that was never mapped.
    if (response.isSuccess()) {
        response.assignedExternalPort = portmap::readU16(datagram, kOffExternalPort);
        response.assignedExternalAddress =
            decodeAddr128(datagram.subspan<kOffExternalAddr, 16>());
    }

    return response;
}

bool matchesRequest(const MapResponse& response, const MapRequest& request,
                    const Address& datagramDestination)
{
    // Section 11.4 lists exactly these. Other fields are set by the server, so
    // comparing them would reject valid responses.
    return datagramDestination == request.clientAddress
           && response.protocol == request.protocol
           && response.internalPort == request.internalPort
           && response.nonce == request.nonce;
}

} // namespace eMule::pcp
