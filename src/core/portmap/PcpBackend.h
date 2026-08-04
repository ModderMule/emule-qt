#pragma once

/// @file PcpBackend.h
/// @brief PCP (RFC 6887) backend.
///
/// The preferred protocol: it handles both address families, returns the
/// assigned external address and port explicitly, and carries an epoch that
/// reveals a router reboot before the lease runs out.
///
/// For IPv6 a MAP is a pure firewall pinhole — no translation — so the external
/// port equals the internal port and the external address equals the client's
/// own GUA. On a CGNAT line that is the only path to real inbound reachability.

#include "portmap/PcpMessages.h"
#include "portmap/PortMapWire.h"
#include "portmap/UdpMappingBackend.h"

#include <QElapsedTimer>

#include <map>

namespace eMule {

class PcpBackend : public UdpMappingBackend {
    Q_OBJECT

public:
    explicit PcpBackend(QObject* parent = nullptr);

    [[nodiscard]] PortMapMethod method() const override { return PortMapMethod::Pcp; }
    [[nodiscard]] bool supports(PortMapFamily) const override { return true; }

protected:
    [[nodiscard]] QByteArray encodeProbe(const Channel& channel) override;
    [[nodiscard]] QByteArray encodeMap(const Channel& channel, const PortMapRequest& request,
                                       uint32 lifetimeSecs, Transaction& transaction) override;
    [[nodiscard]] QByteArray encodeDelete(const Channel& channel,
                                          const PortMapping& mapping) override;
    [[nodiscard]] bool decodeReply(std::span<const uint8> datagram,
                                   const Transaction& transaction, PortMapping& mapping,
                                   bool& ok, QString& error) override;
    [[nodiscard]] bool isVersionMismatch(std::span<const uint8> datagram) const override;
    void inspectProbeReply(std::span<const uint8> datagram) override;

private:
    /// Nonces are per (family, protocol, internal port) and must survive
    /// renewals: re-requesting the same mapping with a *different* nonce earns
    /// NOT_AUTHORIZED against our own mapping until it expires (section 11.3).
    /// Memory-only — persisting it would turn a capability into an on-disk
    /// secret for no benefit.
    [[nodiscard]] pcp::Nonce nonceFor(const PortMapRequest& request);

    /// The persisted per-install secret the nonces are derived from, minting it
    /// on first use.
    [[nodiscard]] QByteArray secret();

    void checkEpoch(uint32 epochTime);

    static constexpr int kSecretBytes = 32;

    std::map<std::tuple<uint8, uint8, uint16>, pcp::Nonce> m_nonces;
    portmap::EpochTracker m_epoch;
    QElapsedTimer m_clock;
};

} // namespace eMule
