#pragma once

/// @file NatPmpMessages.h
/// @brief NAT-PMP (RFC 6886) message codec.
///
/// Pure: no sockets, no Qt objects, no clock. NAT-PMP is IPv4-only — its sole
/// address field is the 32-bit "External IPv4 Address (a.b.c.d)" of the
/// opcode-128 response — so nothing here takes a family parameter.

#include "portmap/PortMapWire.h"
#include "utils/Types.h"

#include <QByteArray>

#include <optional>
#include <span>

namespace eMule::natpmp {

using portmap::kDefaultLifetimeSecs;

inline constexpr uint8  kVersion = 0;
inline constexpr uint16 kServerPort = 5351;
inline constexpr uint16 kAnnouncePort = 5350;

inline constexpr usize kExtAddrRequestSize = 2;
inline constexpr usize kMapRequestSize = 12;
inline constexpr usize kExtAddrResponseSize = 12;
inline constexpr usize kMapResponseSize = 16;
inline constexpr usize kVersionErrorSize = 8;
inline constexpr usize kMaxDatagramSize = 16;

enum class Opcode : uint8 { ExternalAddress = 0, MapUdp = 1, MapTcp = 2 };

enum class Result : uint16 {
    Success = 0,
    UnsupportedVersion = 1,
    NotAuthorized = 2,
    NetworkFailure = 3,
    OutOfResources = 4,
    UnsupportedOpcode = 5,
};

/// Human-readable name; unknown codes render as "unknown (N)" rather than
/// being enum-cast, since section 3.5 requires clients to tolerate them.
[[nodiscard]] QString resultName(uint16 code);

struct MapRequest {
    Opcode opcode = Opcode::MapTcp;    ///< MapUdp or MapTcp only
    uint16 internalPort = 0;
    uint16 suggestedExternalPort = 0;  ///< 0 = any; on renewal, echo what was granted
    uint32 lifetimeSecs = kDefaultLifetimeSecs;   ///< 0 = delete
};

/// What kind of reply arrived, decided by the version and opcode bytes — never
/// by datagram length, which real firmware gets wrong (see decode()).
enum class ReplyKind : uint8 { ExternalAddress, Map, VersionError, Unknown };

struct Response {
    ReplyKind kind = ReplyKind::Unknown;
    uint8     version = 0;        ///< highest version the server supports
    uint8     rawOpcode = 0;
    uint16    resultCode = 0;     ///< raw; never enum-cast an unknown value
    uint32    secondsSinceEpoch = 0;
    Address   externalAddress;    ///< ExternalAddress replies only
    uint16    internalPort = 0;   ///< Map replies only
    uint16    mappedExternalPort = 0;
    uint32    lifetimeSecs = 0;

    /// True when the server told us to speak a newer protocol (i.e. PCP).
    [[nodiscard]] bool suggestsPcp() const noexcept
    {
        return kind == ReplyKind::VersionError && version >= 2;
    }
};

[[nodiscard]] QByteArray encodeExternalAddressRequest();
[[nodiscard]] QByteArray encode(const MapRequest& request);

/// Build the delete form of @p request: RFC 6886 section 3.4 requires the
/// suggested external port to be 0 and the lifetime to be 0.
[[nodiscard]] QByteArray encodeDelete(Opcode opcode, uint16 internalPort);

/// Decode a datagram. Returns nullopt for anything unusable.
///
/// Deliberately tolerant of short replies. A FRITZ!Box answers our 2-byte
/// external-address request with just `02 80`, where section 3.5 mandates 8
/// bytes — and byte 0 alone (the version) is all the version handshake needs. A
/// decoder that insists on 4 bytes here silently fails to detect the router.
[[nodiscard]] std::optional<Response> decode(std::span<const uint8> datagram);

} // namespace eMule::natpmp
