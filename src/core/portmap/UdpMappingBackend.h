#pragma once

/// @file UdpMappingBackend.h
/// @brief Shared machinery for the two UDP/5351 protocols, PCP and NAT-PMP.
///
/// Both speak to the default gateway on the same port with the same
/// request/retransmit/validate shape, so the socket handling, gateway lookup,
/// transaction table and retransmit ladder live here and the subclasses supply
/// only encode/decode.
///
/// Four things here are driven by measurements against a real FRITZ!Box rather
/// than by the RFCs:
///
///  * The socket is connect()ed. That enforces the 5-tuple against off-path
///    spoofing, yields the source address the PCP client-IP field must carry
///    (RFC 6887 section 16.4), and turns an ICMP port-unreachable into an
///    immediate error instead of a silent timeout.
///  * For IPv6 the socket is bound to our GUA first. The v6 default route is via
///    a link-local next hop, so the kernel would otherwise pick a link-local
///    source — and the router then answers NOT_AUTHORIZED, even to ANNOUNCE.
///  * **One socket per attempt.** Over IPv6 the FRITZ!Box answers exactly one
///    PCP request per source port and silently ignores every later one from the
///    same port — measured, and true for any combination of ANNOUNCE and MAP.
///    Reusing a socket therefore makes the first request work and all the rest
///    time out, which reads exactly like "the router does not support IPv6".
///    A fresh ephemeral source port per attempt sidesteps it; responses are
///    matched by nonce and port, never by source port, so nothing is lost.
///  * Discovery uses a short bounded ladder, not the RFC schedules. RFC 6886
///    takes ~128 s to give up and RFC 6887 with MRC=0 retries forever; neither
///    is usable while a client is waiting to come online.

#include "net/DefaultGateway.h"
#include "portmap/PortMapBackend.h"

#include <QByteArray>
#include <QTimer>
#include <QUdpSocket>

#include <memory>
#include <span>
#include <vector>

namespace eMule {

class UdpMappingBackend : public PortMapBackend {
    Q_OBJECT

public:
    explicit UdpMappingBackend(QObject* parent = nullptr);
    ~UdpMappingBackend() override;

    void probe(int timeoutMs) override;
    void requestMapping(const PortMapRequest& request, uint32 lifetimeSecs) override;
    void releaseMapping(const PortMapping& mapping) override;
    void shutdown() override;

protected:
    /// Where to reach the gateway for one address family, and which source
    /// address to use. Holds no socket — see the one-socket-per-attempt note.
    struct Channel {
        GatewayCandidate gateway;
        Address localAddress;      ///< source address, i.e. the PCP client IP
        bool open = false;
    };

    /// An in-flight request. Owns the socket for its current attempt.
    struct Transaction {
        int             id = 0;
        PortMapFamily   family = PortMapFamily::IPv4;
        PortMapRequest  request;
        QByteArray      payload;
        QByteArray      nonce;         ///< PCP mapping nonce; unused by NAT-PMP
        uint32          lifetimeSecs = 0;
        int             attempt = 0;
        int             maxAttempts = 3;
        bool            isProbe = false;
        std::unique_ptr<QUdpSocket> socket;
        std::unique_ptr<QTimer> timer;
    };

    // -- Subclass hooks ----------------------------------------------------

    /// A cheap request that proves a server is there without changing its state.
    [[nodiscard]] virtual QByteArray encodeProbe(const Channel& channel) = 0;

    [[nodiscard]] virtual QByteArray encodeMap(const Channel& channel,
                                               const PortMapRequest& request,
                                               uint32 lifetimeSecs,
                                               Transaction& transaction) = 0;

    [[nodiscard]] virtual QByteArray encodeDelete(const Channel& channel,
                                                  const PortMapping& mapping) = 0;

    /// Interpret a datagram in the context of @p transaction.
    /// @return true when the datagram belongs to this transaction and the
    ///         transaction is now complete.
    [[nodiscard]] virtual bool decodeReply(std::span<const uint8> datagram,
                                           const Transaction& transaction,
                                           PortMapping& mapping, bool& ok,
                                           QString& error) = 0;

    /// Does @p datagram indicate the server speaks a different protocol version?
    /// Used to fail a probe fast rather than waiting out the ladder.
    [[nodiscard]] virtual bool isVersionMismatch(std::span<const uint8> datagram) const = 0;

    /// Optional look at a probe reply before it is discarded. NAT-PMP learns the
    /// external address from it and PCP seeds its epoch tracker.
    virtual void inspectProbeReply(std::span<const uint8> datagram) { Q_UNUSED(datagram) }

    [[nodiscard]] Channel* channelFor(PortMapFamily family);

    // Private helpers follow the protected interface, per the project standard.
private:
    [[nodiscard]] bool openChannel(Channel& channel, PortMapFamily family);
    void closeChannels();
    /// Create a socket with the right source address for @p family, connected to
    /// that family's gateway. Returns null when the channel is not usable.
    [[nodiscard]] std::unique_ptr<QUdpSocket> makeSocket(PortMapFamily family);
    void onTransactionReadyRead(int transactionId);
    void onTransactionError(int transactionId);
    [[nodiscard]] Transaction* findTransaction(int id);
    Transaction* startTransaction(PortMapFamily family, QByteArray payload, int maxAttempts);
    void transmit(Transaction& transaction);
    void finishProbe(bool supported, const QString& detail);
    void failTransaction(Transaction& transaction, const QString& error);
    void eraseTransaction(int id);
    /// Detach a transaction's socket and timer for deferred deletion.
    void retire(Transaction& transaction);
    /// The socket half of retire(). Also used by transmit() between attempts, where the
    /// transaction lives on but its socket is replaced — that swap must defer the old
    /// socket's deletion just the same, since it can still have a read notification pending.
    void retireSocket(Transaction& transaction);

    Channel m_v4;
    Channel m_v6;
    std::vector<std::unique_ptr<Transaction>> m_transactions;
    int  m_nextTransactionId = 1;
    bool m_probing = false;
    bool m_probeAnswered = false;
    int  m_probesOutstanding = 0;
};

} // namespace eMule
