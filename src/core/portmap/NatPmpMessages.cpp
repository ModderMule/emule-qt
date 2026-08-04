#include "pch.h"
/// @file NatPmpMessages.cpp
/// @brief NAT-PMP (RFC 6886) message codec.

#include "portmap/NatPmpMessages.h"

namespace eMule::natpmp {

using portmap::appendU16;
using portmap::appendU32;
using portmap::appendZeros;
using portmap::readU16;
using portmap::readU32;

namespace {

constexpr uint8 kResponseBit = 0x80;

} // namespace

QString resultName(uint16 code)
{
    switch (code) {
    case 0: return QStringLiteral("Success");
    case 1: return QStringLiteral("UnsupportedVersion");
    case 2: return QStringLiteral("NotAuthorized");
    case 3: return QStringLiteral("NetworkFailure");
    case 4: return QStringLiteral("OutOfResources");
    case 5: return QStringLiteral("UnsupportedOpcode");
    default: return QStringLiteral("unknown (%1)").arg(code);
    }
}

QByteArray encodeExternalAddressRequest()
{
    QByteArray out;
    out.reserve(static_cast<qsizetype>(kExtAddrRequestSize));
    out.append(static_cast<char>(kVersion));
    out.append(static_cast<char>(Opcode::ExternalAddress));
    return out;
}

QByteArray encode(const MapRequest& request)
{
    QByteArray out;
    out.reserve(static_cast<qsizetype>(kMapRequestSize));
    out.append(static_cast<char>(kVersion));
    out.append(static_cast<char>(request.opcode));
    appendZeros(out, 2);                            // reserved
    appendU16(out, request.internalPort);
    appendU16(out, request.suggestedExternalPort);
    appendU32(out, request.lifetimeSecs);
    return out;
}

QByteArray encodeDelete(Opcode opcode, uint16 internalPort)
{
    MapRequest request;
    request.opcode = opcode;
    request.internalPort = internalPort;
    request.suggestedExternalPort = 0;   // MUST be 0 (section 3.4)
    request.lifetimeSecs = 0;            // 0 = delete
    return encode(request);
}

std::optional<Response> decode(std::span<const uint8> datagram)
{
    if (datagram.size() < 2)
        return std::nullopt;

    Response response;
    response.version = datagram[0];
    response.rawOpcode = datagram[1];

    // A NAT-PMP server MUST answer with version 0. Anything else is a version
    // mismatch however short the datagram is, which is what makes the 2-byte
    // FRITZ!Box reply usable. Handling this before the 4-byte minimum is the
    // whole point: the reply that identifies a PCP router is the shortest one
    // either protocol ever sends.
    if (response.version != kVersion) {
        response.kind = ReplyKind::VersionError;
        response.resultCode = static_cast<uint16>(Result::UnsupportedVersion);
        // Offsets 4..7 are the *PCP* Lifetime field in this reply, echoed from
        // our request — not an epoch. Leave secondsSinceEpoch at 0 so it can
        // never reach epoch tracking.
        return response;
    }

    if (datagram.size() < 4)
        return std::nullopt;
    response.resultCode = readU16(datagram, 2);

    const bool isReply = (response.rawOpcode & kResponseBit) != 0;
    const bool versionError = response.resultCode
                              == static_cast<uint16>(Result::UnsupportedVersion);
    // Section 3.5's published figure shows OP=0 for the version-mismatch reply;
    // erratum 3618 corrects it to 128. Shipped firmware does both, so accept a
    // clear response bit only in that one case.
    if (!isReply && !versionError)
        return std::nullopt;

    if (datagram.size() >= kVersionErrorSize)
        response.secondsSinceEpoch = readU32(datagram, 4);

    if (versionError) {
        response.kind = ReplyKind::VersionError;
        return response;
    }

    switch (response.rawOpcode & 0x7F) {
    case static_cast<uint8>(Opcode::ExternalAddress):
        if (datagram.size() < kExtAddrResponseSize)
            return std::nullopt;
        response.kind = ReplyKind::ExternalAddress;
        // Section 3: non-numeric quantities are not byte-swapped, so bytes 8..11
        // are a.b.c.d in order — which is exactly a big-endian read.
        response.externalAddress = Address::fromHostOrder(readU32(datagram, 8));
        return response;

    case static_cast<uint8>(Opcode::MapUdp):
    case static_cast<uint8>(Opcode::MapTcp):
        if (datagram.size() < kMapResponseSize)
            return std::nullopt;
        response.kind = ReplyKind::Map;
        response.internalPort = readU16(datagram, 8);
        response.mappedExternalPort = readU16(datagram, 10);
        response.lifetimeSecs = readU32(datagram, 12);
        return response;

    default:
        response.kind = ReplyKind::Unknown;
        return response;
    }
}

} // namespace eMule::natpmp
