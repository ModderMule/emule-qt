#include "pch.h"
/// @file AppNatEnv.cpp
/// @brief NatEnv backed by the running core.

#include "net/natt/AppNatEnv.h"

#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "ipfilter/IPFilter.h"
#include "kademlia/Kademlia.h"
#include "net/ClientUDPSocket.h"
#include "net/EMSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "utils/Opcodes.h"
#include "utils/TimeUtils.h"

namespace eMule::natt {

uint64 AppNatEnv::now() const
{
    return getTickCount();
}

Hash16 AppNatEnv::ourHash() const
{
    return thePrefs.userHash();
}

uint32 AppNatEnv::ourClientID() const
{
    auto* sc = theApp.serverConnect;
    return sc && sc->isConnected() ? sc->clientID() : 0;
}

uint16 AppNatEnv::ourUdpPort() const
{
    return theApp.clientUDP ? theApp.advertisedUdpPort() : uint16{0};
}

uint16 AppNatEnv::ourTcpPort() const
{
    return theApp.advertisedTcpPort();
}

uint8 AppNatEnv::ourConnectOptions() const
{
    return static_cast<uint8>((thePrefs.cryptLayerSupported() ? CONNECT_OPT_CRYPT_SUPPORTED : 0)
                              | (thePrefs.cryptLayerRequested() ? CONNECT_OPT_CRYPT_REQUESTED : 0)
                              | (thePrefs.cryptLayerRequired() ? CONNECT_OPT_CRYPT_REQUIRED : 0));
}

uint32 AppNatEnv::ourPublicIPv4() const
{
    return theApp.publicIP();
}

Address AppNatEnv::ourPublicIPv6() const
{
    return theApp.hasConfidentPublicIPv6() ? theApp.publicIPv6() : Address();
}

bool AppNatEnv::firewalled() const
{
    return theApp.isFirewalled();
}

bool AppNatEnv::kadConnected() const
{
    auto* kad = kad::Kademlia::instance();
    return kad && kad->isConnected();
}

bool AppNatEnv::enabled() const
{
    return thePrefs.natTraversal();
}

const Server* AppNatEnv::currentServer() const
{
    auto* sc = theApp.serverConnect;
    return sc && sc->isConnected() ? sc->currentServer() : nullptr;
}

Address AppNatEnv::serverSessionAddress() const
{
    return theApp.serverConnect ? theApp.serverConnect->sessionAddress() : Address();
}

bool AppNatEnv::admit(const Address& addr) const
{
    if (!isGoodIP(addr))
        return false;
    if (theApp.ipFilter && theApp.ipFilter->isFiltered(addr, thePrefs.ipFilterLevel()))
        return false;
    return !(theApp.clientList && theApp.clientList->isBannedClient(addr));
}

void AppNatEnv::sendPeerUdp(std::unique_ptr<Packet> packet, const Endpoint& to, const uint8* cryptHash)
{
    if (!theApp.clientUDP)
        return;
    // The client key mixes in our public IP of the destination's family.
    const bool haveIP = to.address().isIPv6() ? !theApp.publicIPv6().isNull() : theApp.publicIP() != 0;
    const bool encrypt = cryptHash && thePrefs.cryptLayerSupported() && haveIP;
    theApp.clientUDP->sendPacket(std::move(packet), to, encrypt, cryptHash, false, 0);
}

void AppNatEnv::sendServerUdp(std::unique_ptr<Packet> packet, const Endpoint& to, uint32 serverKey)
{
    if (theApp.clientUDP)
        theApp.clientUDP->sendServerPacket(std::move(packet), to,
                                           thePrefs.cryptLayerSupported() ? serverKey : 0u);
}

ServerUdpCrypt AppNatEnv::serverUdpCrypt() const
{
    const Server* cur = currentServer();
    if (!cur)
        return {};
    const Server* listed = theApp.serverList
        ? theApp.serverList->findByIPTcp(cur->ipAddress(), cur->port()) : nullptr;
    const Server& s = listed ? *listed : *cur;
    return {s.serverKeyUDP(), s.obfuscationPortUDP()};
}

void AppNatEnv::refreshServerKey()
{
    if (const Server* server = currentServer(); server && theApp.serverList)
        theApp.serverList->requestStatPing(*server);
}

void AppNatEnv::sendRawUdp(const QByteArray& data, const Endpoint& to)
{
    if (theApp.clientUDP)
        theApp.clientUDP->sendRawDatagram(data, to);
}

bool AppNatEnv::sendServerTcp(std::unique_ptr<Packet> packet)
{
    auto* sc = theApp.serverConnect;
    return sc && sc->isConnected() && sc->sendPacket(std::move(packet));
}

UpDownClient* AppNatEnv::findClient(const Attempt& attempt)
{
    if (attempt.client)
        return attempt.client.data();
    if (attempt.peer.hashKnown && theApp.clientList)
        return theApp.clientList->findByUserHash(attempt.peer.hash.data());
    return nullptr;
}

void AppNatEnv::dialPeer(const Attempt& attempt, const Address& ipv6)
{
    if (!theApp.clientList)
        return;
    UpDownClient* client = findClient(attempt);
    if (!client) {
        if (attempt.role == Role::Requester || !attempt.peer.hashKnown)
            return;   // the source is gone
        // Target: the requester is new to us, as after a server callback (clientID 1 = LowID).
        client = new UpDownClient(attempt.peerTcpPort, 1, 0, 0, nullptr);
        client->setUserHash(attempt.peer.hash.data());
        theApp.clientList->addClient(client);
    }
    if (attempt.peerTcpPort != 0)
        client->setUserPort(attempt.peerTcpPort);
    client->setUserIPv6(ipv6);
    client->setOpenIPv6(true);
    client->setConnectAddress(ipv6);
    if (client->connectingState() == ConnectingState::NatRendezvous)
        client->setConnectingState(ConnectingState::None);
    // tryToConnect takes the direct branch for an IPv6 target.
    client->tryToConnect(true);
}

bool AppNatEnv::peerConnected(const Attempt& attempt) const
{
    const UpDownClient* client = findClient(attempt);
    return client && client->socket() && client->socket()->isConnected();
}

void AppNatEnv::attemptFailed(const Attempt& attempt)
{
    UpDownClient* client = findClient(attempt);
    if (!client || client->connectingState() != ConnectingState::NatRendezvous)
        return;
    // Kept as a source; PartFile::process() will not revive a failed rendezvous peer.
    client->setConnectingState(ConnectingState::None);
    if (client->downloadState() == DownloadState::Connecting)
        client->setDownloadState(DownloadState::LowToLowIP);
}

} // namespace eMule::natt
