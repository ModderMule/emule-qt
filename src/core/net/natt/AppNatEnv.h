#pragma once
/// @file AppNatEnv.h
/// @brief NatEnv backed by the running core (theApp, thePrefs, sockets).

#include "net/natt/NatEnv.h"

namespace eMule::natt {

class AppNatEnv final : public NatEnv {
public:
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

private:
    [[nodiscard]] static UpDownClient* findClient(const Attempt& attempt);
};

} // namespace eMule::natt
