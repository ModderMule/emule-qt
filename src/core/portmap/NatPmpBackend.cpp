#include "pch.h"
/// @file NatPmpBackend.cpp
/// @brief NAT-PMP (RFC 6886) backend.

#include "portmap/NatPmpBackend.h"
#include "utils/Log.h"

namespace eMule {

namespace {

[[nodiscard]] natpmp::Opcode toOpcode(PortMapProtocol protocol)
{
    return protocol == PortMapProtocol::Udp ? natpmp::Opcode::MapUdp
                                            : natpmp::Opcode::MapTcp;
}

} // namespace

NatPmpBackend::NatPmpBackend(QObject* parent)
    : UdpMappingBackend(parent)
{
    m_clock.start();
}

// ---------------------------------------------------------------------------
// Encode
// ---------------------------------------------------------------------------

QByteArray NatPmpBackend::encodeProbe(const Channel& channel)
{
    Q_UNUSED(channel)
    // The external-address request creates nothing on the router, and its reply
    // gives us the WAN address for free.
    return natpmp::encodeExternalAddressRequest();
}

QByteArray NatPmpBackend::encodeMap(const Channel& channel, const PortMapRequest& request,
                                    uint32 lifetimeSecs, Transaction& transaction)
{
    Q_UNUSED(channel)
    Q_UNUSED(transaction)

    natpmp::MapRequest message;
    message.opcode = toOpcode(request.protocol);
    message.internalPort = request.internalPort;
    // Ask for the same external port; a different one cannot be advertised.
    message.suggestedExternalPort = request.internalPort;
    message.lifetimeSecs = lifetimeSecs;
    return natpmp::encode(message);
}

QByteArray NatPmpBackend::encodeDelete(const Channel& channel, const PortMapping& mapping)
{
    Q_UNUSED(channel)
    // Section 3.4: internal port plus protocol, suggested port and lifetime zero.
    // There is no per-mapping handle, so this removes every mapping the router
    // holds for that internal port — which is exactly what we want here.
    return natpmp::encodeDelete(toOpcode(mapping.request.protocol),
                                mapping.request.internalPort);
}

// ---------------------------------------------------------------------------
// Decode
// ---------------------------------------------------------------------------

bool NatPmpBackend::decodeReply(std::span<const uint8> datagram, const Transaction& transaction,
                                PortMapping& mapping, bool& ok, QString& error)
{
    const auto response = natpmp::decode(datagram);
    if (!response.has_value())
        return false;

    if (response->kind == natpmp::ReplyKind::ExternalAddress) {
        if (!response->externalAddress.isNull())
            emit externalAddressLearned(response->externalAddress);
        return false;   // not an answer to a mapping request
    }
    if (response->kind != natpmp::ReplyKind::Map)
        return false;

    // NAT-PMP has no nonce; the echoed internal port is its only correlator,
    // which is why the connected-socket source check above carries the weight.
    if (response->internalPort != transaction.request.internalPort)
        return false;
    if ((response->rawOpcode & 0x7F) != static_cast<uint8>(toOpcode(transaction.request.protocol)))
        return false;

    checkEpoch(response->secondsSinceEpoch);

    if (response->resultCode != static_cast<uint16>(natpmp::Result::Success)) {
        ok = false;
        error = natpmp::resultName(response->resultCode);
        return true;
    }

    ok = true;
    mapping.externalPort = response->mappedExternalPort;
    mapping.lifetimeSecs = portmap::clampGrantedLifetime(response->lifetimeSecs, false);
    return true;
}

bool NatPmpBackend::isVersionMismatch(std::span<const uint8> datagram) const
{
    const auto response = natpmp::decode(datagram);
    return response.has_value() && response->kind == natpmp::ReplyKind::VersionError;
}

void NatPmpBackend::inspectProbeReply(std::span<const uint8> datagram)
{
    const auto response = natpmp::decode(datagram);
    if (!response.has_value())
        return;
    if (!response->externalAddress.isNull())
        emit externalAddressLearned(response->externalAddress);
    checkEpoch(response->secondsSinceEpoch);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void NatPmpBackend::checkEpoch(uint32 secondsSinceEpoch)
{
    const qint64 nowSecs = m_clock.elapsed() / 1000;
    if (!m_haveSssoe) {
        m_haveSssoe = true;
        m_lastSssoe = secondsSinceEpoch;
        m_lastSssoeAtSecs = nowSecs;
        return;
    }

    // Section 3.6 uses different arithmetic from PCP's epoch rule, so it gets
    // its own implementation rather than sharing EpochTracker.
    const bool rebooted = portmap::natPmpEpochIndicatesReboot(
        m_lastSssoe, secondsSinceEpoch, nowSecs - m_lastSssoeAtSecs);
    m_lastSssoe = secondsSinceEpoch;
    m_lastSssoeAtSecs = nowSecs;

    if (rebooted) {
        logWarning(QStringLiteral("NAT-PMP: gateway restarted — re-adding mappings"));
        emit mappingsInvalidated(QStringLiteral("NAT-PMP epoch reset"));
    }
}

} // namespace eMule
