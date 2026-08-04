#pragma once

/// @file PcpMessages.h
/// @brief PCP (RFC 6887) message codec.
///
/// Pure apart from generateNonce(): no sockets, no Qt objects, no clock.
///
/// Two layout details are easy to get wrong and expensive to debug:
///
///  * The response result code is **8 bits at offset 3**; offset 2 is 8 reserved
///    bits. It is not a 16-bit code at offset 2 (that is NAT-PMP's layout).
///  * decodeHeader() must be usable *before* the 24-octet minimum is enforced,
///    because section 8.3 checks UNSUPP_VERSION first and the reply that
///    identifies a NAT-PMP-only router is 8 bytes — or, on real FRITZ!OS, 2.

#include "portmap/PortMapWire.h"
#include "utils/Types.h"

#include <QByteArray>

#include <array>
#include <optional>
#include <span>

namespace eMule::pcp {

using portmap::kDefaultLifetimeSecs;

inline constexpr uint8  kVersion = 2;
inline constexpr uint16 kServerPort = 5351;
inline constexpr uint16 kClientPort = 5350;      ///< multicast ANNOUNCE listener only
inline constexpr uint8  kResponseBit = 0x80;

inline constexpr usize kHeaderSize = 24;
inline constexpr usize kMapDataSize = 36;
inline constexpr usize kMapPacketSize = 60;      ///< header + MAP opcode data
inline constexpr usize kNonceSize = 12;
inline constexpr usize kMaxMessageSize = 1100;   ///< section 8.2/8.3, both directions

enum class Opcode : uint8 { Announce = 0, Map = 1, Peer = 2 };

enum class OptionCode : uint8 { ThirdParty = 1, PreferFailure = 2, Filter = 3 };

/// IANA protocol numbers, as PCP puts them on the wire.
enum class IpProtocol : uint8 { All = 0, Tcp = 6, Udp = 17 };

enum class Result : uint8 {
    Success = 0,
    UnsuppVersion = 1,
    NotAuthorized = 2,
    MalformedRequest = 3,
    UnsuppOpcode = 4,
    UnsuppOption = 5,
    MalformedOption = 6,
    NetworkFailure = 7,
    NoResources = 8,
    UnsuppProtocol = 9,
    UserExQuota = 10,
    CannotProvideExternal = 11,
    AddressMismatch = 12,
    ExcessiveRemotePeers = 13,
};

using Nonce = std::array<uint8, kNonceSize>;

/// Human-readable name; codes 14..255 are legal-but-unknown and render as
/// "unknown (N)" rather than being enum-cast.
[[nodiscard]] QString resultName(uint8 code);

/// 96 unguessable bits (section 11.2, RFC 4086). Uses the OpenSSL CSPRNG.
///
/// The nonce is the capability that owns the mapping: re-requesting the same
/// internal port with a *different* nonce earns NOT_AUTHORIZED until the old
/// mapping expires (section 11.3). Generate one per mapping, reuse it for every
/// retransmission and renewal, and never persist it.
[[nodiscard]] Nonce generateNonce();

struct MapRequest {
    Nonce      nonce{};
    IpProtocol protocol = IpProtocol::Tcp;
    uint16     internalPort = 0;
    uint16     suggestedExternalPort = 0;   ///< 0 first time; granted port on renewal
    Address    suggestedExternalAddress;    ///< null -> family-specific all-zeros
    Address    clientAddress;               ///< source address of *this* datagram
    uint32     lifetimeSecs = kDefaultLifetimeSecs;   ///< 0 = delete
    bool       preferFailure = false;       ///< appends the 4-byte option
    /// Selects the external family when suggestedExternalAddress is null:
    /// ::ffff:0:0 for IPv4, :: for IPv6. Section 10 uses this field's family to
    /// choose the external family, so a dual-stack client sends two MAP
    /// requests with two nonces rather than one combined request.
    bool       externalFamilyIsIPv4 = true;
};

/// Header-only view. Succeeds on any datagram of 2 octets or more so the caller
/// can act on a version mismatch before the 24-octet minimum applies.
struct ResponseHeader {
    uint8  version = 0;
    bool   isResponse = false;
    uint8  opcode = 0;
    uint8  resultCode = 0;
    uint32 lifetimeSecs = 0;    ///< only meaningful when hasFullHeader
    uint32 epochTime = 0;       ///< only meaningful when hasFullHeader
    bool   hasResultCode = false;   ///< datagram was >= 4 octets
    bool   hasFullHeader = false;   ///< datagram was >= 24 octets

    [[nodiscard]] bool isVersionMismatch() const noexcept
    {
        return version != kVersion
               || (hasResultCode && resultCode == static_cast<uint8>(Result::UnsuppVersion));
    }
};

struct MapResponse {
    uint8      version = 0;
    Opcode     opcode = Opcode::Map;
    uint8      resultCode = 0;
    uint32     lifetimeSecs = 0;
    uint32     epochTime = 0;
    Nonce      nonce{};
    IpProtocol protocol = IpProtocol::Tcp;
    uint16     internalPort = 0;
    uint16     assignedExternalPort = 0;
    Address    assignedExternalAddress;

    [[nodiscard]] bool isSuccess() const noexcept
    {
        return resultCode == static_cast<uint8>(Result::Success);
    }
};

[[nodiscard]] QByteArray encodeMapRequest(const MapRequest& request);

/// Build the delete form: section 15.1 requires lifetime 0 and *zeroed*
/// suggested external port and address.
[[nodiscard]] QByteArray encodeDeleteRequest(const MapRequest& request);

/// ANNOUNCE (section 14.1.2) — idempotent, allocates nothing, and its reply
/// seeds the epoch tracker. The cheapest way to ask "is a PCP server there?".
[[nodiscard]] QByteArray encodeAnnounceRequest(const Address& clientAddress);

[[nodiscard]] std::optional<ResponseHeader> decodeHeader(std::span<const uint8> datagram);
[[nodiscard]] std::optional<MapResponse> decodeMapResponse(std::span<const uint8> datagram);

/// Section 11.4 response matching. Compares only the four fields the client
/// controls — the rest are set by the server and must not be compared.
[[nodiscard]] bool matchesRequest(const MapResponse& response, const MapRequest& request,
                                  const Address& datagramDestination);

} // namespace eMule::pcp
