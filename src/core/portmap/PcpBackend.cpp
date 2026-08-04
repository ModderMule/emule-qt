#include "pch.h"
/// @file PcpBackend.cpp
/// @brief PCP (RFC 6887) backend.

#include "portmap/PcpBackend.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <openssl/rand.h>

#include <QCryptographicHash>

#include <cstring>

namespace eMule {

namespace {

[[nodiscard]] pcp::IpProtocol toPcpProtocol(PortMapProtocol protocol)
{
    return protocol == PortMapProtocol::Udp ? pcp::IpProtocol::Udp : pcp::IpProtocol::Tcp;
}

[[nodiscard]] QByteArray nonceToBytes(const pcp::Nonce& nonce)
{
    return {reinterpret_cast<const char*>(nonce.data()), qsizetype(nonce.size())};
}

} // namespace

PcpBackend::PcpBackend(QObject* parent)
    : UdpMappingBackend(parent)
{
    m_clock.start();
}

// ---------------------------------------------------------------------------
// Encode
// ---------------------------------------------------------------------------

QByteArray PcpBackend::encodeProbe(const Channel& channel)
{
    // ANNOUNCE allocates nothing and changes no state, so a probe is free even
    // if it is repeated (section 14.1.2).
    return pcp::encodeAnnounceRequest(channel.localAddress);
}

QByteArray PcpBackend::encodeMap(const Channel& channel, const PortMapRequest& request,
                                 uint32 lifetimeSecs, Transaction& transaction)
{
    pcp::MapRequest message;
    message.nonce = nonceFor(request);
    message.protocol = toPcpProtocol(request.protocol);
    message.internalPort = request.internalPort;
    // Ask for the same external port: eD2K advertises thePrefs.port() and has no
    // external-port tag, so anything else is a LowID even when it "succeeds".
    message.suggestedExternalPort = request.internalPort;
    message.lifetimeSecs = lifetimeSecs;
    message.externalFamilyIsIPv4 = request.family != PortMapFamily::IPv6;
    // Must be the address the server actually sees, or it answers
    // ADDRESS_MISMATCH. Taken from the connected socket, never guessed.
    message.clientAddress = channel.localAddress;

    transaction.nonce = nonceToBytes(message.nonce);
    return pcp::encodeMapRequest(message);
}

QByteArray PcpBackend::encodeDelete(const Channel& channel, const PortMapping& mapping)
{
    pcp::MapRequest message;
    message.nonce = nonceFor(mapping.request);
    message.protocol = toPcpProtocol(mapping.request.protocol);
    message.internalPort = mapping.request.internalPort;
    message.clientAddress = channel.localAddress;
    message.externalFamilyIsIPv4 = mapping.request.family != PortMapFamily::IPv6;
    return pcp::encodeDeleteRequest(message);
}

// ---------------------------------------------------------------------------
// Decode
// ---------------------------------------------------------------------------

bool PcpBackend::decodeReply(std::span<const uint8> datagram, const Transaction& transaction,
                             PortMapping& mapping, bool& ok, QString& error)
{
    const auto response = pcp::decodeMapResponse(datagram);
    if (!response.has_value())
        return false;

    // Section 11.4: match on nonce, protocol and internal port. The nonce is the
    // only field an off-path attacker cannot guess, so it is the real check.
    if (transaction.nonce.size() == qsizetype(pcp::kNonceSize)
        && std::memcmp(response->nonce.data(), transaction.nonce.constData(),
                       pcp::kNonceSize)
               != 0) {
        return false;
    }
    if (response->internalPort != transaction.request.internalPort)
        return false;
    if (response->protocol != toPcpProtocol(transaction.request.protocol))
        return false;

    checkEpoch(response->epochTime);

    if (!response->isSuccess()) {
        ok = false;
        error = pcp::resultName(response->resultCode);
        if (response->resultCode == static_cast<uint8>(pcp::Result::AddressMismatch)) {
            error += QStringLiteral(" (an unexpected NAT sits between us and the "
                                    "PCP server)");
        }
        return true;
    }

    ok = true;
    mapping.externalPort = response->assignedExternalPort;
    mapping.externalAddress = response->assignedExternalAddress;
    // Never assume the request was honoured (section 11.2) — publish what came
    // back, and clamp an absurd value rather than renewing in 136 years.
    mapping.lifetimeSecs = portmap::clampGrantedLifetime(response->lifetimeSecs, false);
    mapping.opaqueId = transaction.nonce;

    if (!mapping.externalAddress.isNull())
        emit externalAddressLearned(mapping.externalAddress);
    return true;
}

bool PcpBackend::isVersionMismatch(std::span<const uint8> datagram) const
{
    const auto header = pcp::decodeHeader(datagram);
    if (!header.has_value())
        return false;
    return header->isVersionMismatch();
}

void PcpBackend::inspectProbeReply(std::span<const uint8> datagram)
{
    const auto header = pcp::decodeHeader(datagram);
    if (header.has_value() && header->hasFullHeader)
        checkEpoch(header->epochTime);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

pcp::Nonce PcpBackend::nonceFor(const PortMapRequest& request)
{
    const auto key = std::make_tuple(static_cast<uint8>(request.family),
                                     static_cast<uint8>(request.protocol),
                                     request.internalPort);
    const auto it = m_nonces.find(key);
    if (it != m_nonces.end())
        return it->second;

    // Derived from a persisted secret rather than freshly random, because the
    // nonce is what *owns* the mapping. If we come back after a crash with a new
    // nonce, the router refuses to renew or delete the mapping we still hold —
    // the dev FRITZ!Box does not even answer NOT_AUTHORIZED as RFC 6887
    // section 11.3 requires, it simply drops the request. That would leave the
    // client firewalled for the rest of the lease with no diagnosable cause.
    //
    // Deriving keeps the property that matters (unguessable by other hosts on
    // this LAN) while making it reproducible for the same port on this install.
    QByteArray material = secret();
    material.append(static_cast<char>(request.family));
    material.append(static_cast<char>(request.protocol));
    material.append(static_cast<char>((request.internalPort >> 8) & 0xFF));
    material.append(static_cast<char>(request.internalPort & 0xFF));

    const QByteArray digest =
        QCryptographicHash::hash(material, QCryptographicHash::Sha256);

    pcp::Nonce nonce{};
    std::memcpy(nonce.data(), digest.constData(), pcp::kNonceSize);
    m_nonces.emplace(key, nonce);
    return nonce;
}

QByteArray PcpBackend::secret()
{
    QByteArray stored = QByteArray::fromHex(thePrefs.portMapSecret().toLatin1());
    if (stored.size() == kSecretBytes)
        return stored;

    // First run on this install: mint one and persist it.
    stored.resize(kSecretBytes);
    if (RAND_bytes(reinterpret_cast<uint8*>(stored.data()), kSecretBytes) != 1) {
        logError(QStringLiteral("PCP: CSPRNG failed, cannot derive mapping nonces"));
        return {};
    }
    thePrefs.setPortMapSecret(QString::fromLatin1(stored.toHex()));
    return stored;
}

void PcpBackend::checkEpoch(uint32 epochTime)
{
    if (m_epoch.validate(epochTime, m_clock.elapsed() / 1000))
        return;

    // Section 8.5: the server lost state, so every mapping it held is gone.
    // Re-adding now beats waiting out the lease with a dead mapping advertised.
    logWarning(QStringLiteral("PCP: server epoch went backwards — it restarted"));
    emit mappingsInvalidated(QStringLiteral("PCP epoch violation"));
}

} // namespace eMule
