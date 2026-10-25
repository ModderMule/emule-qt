#pragma once
/// @file AppNatEnv.h
/// @brief NatEnv backed by the running core (theApp, thePrefs, sockets).

#include "net/natt/NatEnv.h"

#include <QPointer>

#include <map>
#include <memory>

namespace eMule::quic {
class QuicNatEndpoint;
}

namespace eMule::natt {

class AppQuicNatIo;

class AppNatEnv final : public NatEnv {
public:
    AppNatEnv();
    ~AppNatEnv() override;

    [[nodiscard]] uint64 now() const override;

    [[nodiscard]] Hash16 ourHash() const override;
    [[nodiscard]] uint32 ourClientID() const override;
    [[nodiscard]] uint16 ourUdpPort() const override;
    [[nodiscard]] uint16 ourTcpPort() const override;
    [[nodiscard]] uint8 ourConnectOptions() const override;
    [[nodiscard]] uint32 ourPublicIPv4() const override;
    [[nodiscard]] Address ourPublicIPv6() const override;
    [[nodiscard]] bool firewalled() const override;
    [[nodiscard]] bool kadConnected() const override;
    [[nodiscard]] bool enabled() const override;

    [[nodiscard]] const Server* currentServer() const override;
    [[nodiscard]] Address serverSessionAddress() const override;
    [[nodiscard]] ForeignServer foreignServer(const Address& addr, uint16 tcpPort, bool probe) override;
    void refreshForeignServerKey(const Address& addr, uint16 tcpPort) override;

    [[nodiscard]] bool admit(const Address& addr) const override;

    void sendPeerUdp(std::unique_ptr<Packet> packet, const Endpoint& to,
                     const uint8* cryptHash) override;
    void sendServerUdp(std::unique_ptr<Packet> packet, const Endpoint& to, uint32 serverKey) override;
    [[nodiscard]] ServerUdpCrypt serverUdpCrypt() const override;
    void refreshServerKey() override;
    void sendRawUdp(const QByteArray& data, const Endpoint& to) override;
    bool sendServerTcp(std::unique_ptr<Packet> packet) override;

    void dialPeer(const Attempt& attempt, const Address& ipv6) override;
    [[nodiscard]] bool peerConnected(const Attempt& attempt) const override;
    void attemptFailed(const Attempt& attempt) override;

    [[nodiscard]] bool quicAvailable() const override;
    void armQuic(const Attempt& attempt) override;
    void dialPeerQuic(const Attempt& attempt) override;
    [[nodiscard]] bool quicActive(const Attempt& attempt) const override;
    bool handleQuicFrame(uint8 type, const uint8* data, uint32 size, const Endpoint& from,
                         bool obfuscated) override;

private:
    /// The QUIC endpoint, built on first use (needs the client UDP socket).
    [[nodiscard]] quic::QuicNatEndpoint* quic() const;

    [[nodiscard]] static UpDownClient* findClient(const Attempt& attempt);

    /// QUIC dials in flight: peer address -> the source that wants the link
    mutable std::map<Address, QPointer<UpDownClient>> m_quicClients;
    mutable std::unique_ptr<AppQuicNatIo> m_quicIo;
    mutable std::unique_ptr<quic::QuicNatEndpoint> m_quic;
};

} // namespace eMule::natt
