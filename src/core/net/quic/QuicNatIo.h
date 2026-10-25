#pragma once
/// @file QuicNatIo.h
/// @brief What the QUIC NAT-T endpoint needs from the app: the client UDP socket and policy.
///
/// AppQuicNatIo is the real one; tests wire two endpoints together.

#include "net/Address.h"
#include "net/quic/QuicNatProtocol.h"

namespace eMule::quic {

class QuicNatIo {
public:
    virtual ~QuicNatIo() = default;

    [[nodiscard]] virtual Hash16 ourHash() const = 0;
    /// Monotonic milliseconds.
    [[nodiscard]] virtual uint64 nowMs() const = 0;

    /// Send `0xB2 <type> <payload>` from the client UDP port, now (not throttled).
    /// @p cryptHash obfuscates it for that user hash, with random padding; nullptr = plain.
    virtual void sendFrame(uint8 type, const uint8* payload, size_t len, const Endpoint& to,
                           const Hash16* cryptHash) = 0;
    /// Obfuscation towards @p to can work: crypt layer on, our public IP of its family known.
    [[nodiscard]] virtual bool canObfuscate(const Endpoint& to) const = 0;
    /// Never send a plain 0xB2 frame (preference).
    [[nodiscard]] virtual bool strictObfuscation() const = 0;
    /// Not filtered, not banned.
    [[nodiscard]] virtual bool admit(const Address& addr) const = 0;
    /// Our CONNECT_OPT crypt bits for caps frames (the QUIC bit is added).
    [[nodiscard]] virtual uint8 connectOptions() const = 0;
    /// Our address as the UDP socket sees it, for the QUIC path towards @p peer.
    [[nodiscard]] virtual Endpoint localEndpoint(const Endpoint& peer) const = 0;
};

} // namespace eMule::quic
