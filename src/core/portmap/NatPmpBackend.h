#pragma once

/// @file NatPmpBackend.h
/// @brief NAT-PMP (RFC 6886) backend.
///
/// IPv4-only by specification: the protocol's sole address field is the 32-bit
/// "External IPv4 Address" of the opcode-128 response, and there is no IPv6
/// message at all.
///
/// Kept despite the dev FRITZ!Box rejecting it (it answers UNSUPP_VERSION and
/// points at PCP): it shares all of its machinery with PcpBackend, so it is
/// nearly free, and it is what OpenWrt/miniupnpd and older Apple gear speak.

#include "portmap/NatPmpMessages.h"
#include "portmap/PortMapWire.h"
#include "portmap/UdpMappingBackend.h"

#include <QElapsedTimer>

namespace eMule {

class NatPmpBackend : public UdpMappingBackend {
    Q_OBJECT

public:
    explicit NatPmpBackend(QObject* parent = nullptr);

    [[nodiscard]] PortMapMethod method() const override { return PortMapMethod::NatPmp; }
    [[nodiscard]] bool supports(PortMapFamily family) const override
    {
        return family == PortMapFamily::IPv4;
    }

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
    void checkEpoch(uint32 secondsSinceEpoch);

    uint32 m_lastSssoe = 0;
    qint64 m_lastSssoeAtSecs = 0;
    bool   m_haveSssoe = false;
    QElapsedTimer m_clock;
};

} // namespace eMule
