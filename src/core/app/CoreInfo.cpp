#include "pch.h"
/// @file CoreInfo.cpp
/// @brief Status snapshots of the running core — implementation.

#include "app/CoreInfo.h"

#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "geo/IP2Country.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadIndexed.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadUDPListener.h"
#include "kademlia/Kademlia.h"
#include "net/BindAddress.h"
#include "portmap/PortMapper.h"
#include "prefs/Preferences.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "utils/OtherFunctions.h"

namespace eMule::ops {

void insertBindState(QCborMap& info)
{
    const BindAddress::Resolution r = BindAddress::current();
    info.insert(QStringLiteral("netBlocked"), r.state == BindAddress::State::Blocked);
    info.insert(QStringLiteral("netBlockReason"), r.reason);
    info.insert(QStringLiteral("boundInterface"),
                r.state == BindAddress::State::Bound ? r.name : QString());
}

QCborMap networkInfo()
{
    QCborMap info;

    // -- Client section -------------------------------------------------------
    QCborMap client;
    client.insert(QStringLiteral("nick"), thePrefs.nick());
    const auto hash = thePrefs.userHash();
    client.insert(QStringLiteral("hash"), md4str(hash.data()));
    client.insert(QStringLiteral("tcpPort"), theApp.advertisedTcpPort());
    client.insert(QStringLiteral("udpPort"), theApp.advertisedUdpPort());
    insertBindState(client);
    info.insert(QStringLiteral("client"), client);

    // -- eD2K section ---------------------------------------------------------
    QCborMap ed2k;
    // ED2K-only: this feeds the "ed2k" section, reported separately from "kad" below.
    const bool ed2kConnected = theApp.serverConnect && theApp.serverConnect->isConnected();
    const bool ed2kConnecting = theApp.serverConnect && theApp.serverConnect->isConnecting();
    const bool ed2kFirewalled = theApp.isFirewalled();
    ed2k.insert(QStringLiteral("connected"), ed2kConnected);
    ed2k.insert(QStringLiteral("connecting"), ed2kConnecting);
    ed2k.insert(QStringLiteral("firewalled"), ed2kFirewalled);

    // Reported whether or not a server session exists: this comes from LocalIPv6 scanning the
    // local interfaces at startup, not from the server handshake. Gating it on ed2kConnected
    // would withhold it exactly when someone is diagnosing why they cannot connect. Empty when
    // the host has no usable public IPv6.
    ed2k.insert(QStringLiteral("publicIPv6"), theApp.publicIPv6().toString());
    // Server's ST_IPV6_STATUS verdict on that address (IPV6ST_* bits; 0 = no verdict).
    ed2k.insert(QStringLiteral("ipv6Status"), static_cast<qint64>(theApp.publicIPv6Status()));
    // Dotted-quad form of publicIP, so callers that just need a literal (the port test URL) do
    // not each reimplement the ED2K byte order. Empty until a server tells us our IPv4.
    ed2k.insert(QStringLiteral("publicIPv4"),
                theApp.publicIP() != 0 ? ipstr(theApp.publicIP()) : QString());

    if (ed2kConnected && theApp.serverConnect) {
        ed2k.insert(QStringLiteral("clientID"),
                     static_cast<qint64>(theApp.serverConnect->clientID()));
        ed2k.insert(QStringLiteral("lowID"), theApp.serverConnect->isLowID());
        ed2k.insert(QStringLiteral("publicIP"),
                     static_cast<qint64>(theApp.publicIP()));

        // Total users/files across all servers
        if (theApp.serverList) {
            uint32 totalUsers = 0, totalFiles = 0;
            for (const auto& srv : theApp.serverList->servers()) {
                totalUsers += srv->users();
                totalFiles += srv->files();
            }
            ed2k.insert(QStringLiteral("totalUsers"), static_cast<qint64>(totalUsers));
            ed2k.insert(QStringLiteral("totalFiles"), static_cast<qint64>(totalFiles));
        }

        // Current server details
        const auto* srv = theApp.serverConnect->currentServer();
        if (srv) {
            QCborMap server;
            server.insert(QStringLiteral("name"), srv->name());
            server.insert(QStringLiteral("description"), srv->description());
            server.insert(QStringLiteral("address"), srv->address());
            server.insert(QStringLiteral("addr"), srv->ipAddress().toString());   // IPv6-capable
            server.insert(QStringLiteral("port"), srv->port());
            server.insert(QStringLiteral("version"), srv->version());
            server.insert(QStringLiteral("users"), static_cast<qint64>(srv->users()));
            server.insert(QStringLiteral("files"), static_cast<qint64>(srv->files()));
            server.insert(QStringLiteral("obfuscated"),
                          theApp.serverConnect->isConnectedObfuscated());
            server.insert(QStringLiteral("lowIDUsers"),
                          static_cast<qint64>(srv->lowIDUsers()));
            server.insert(QStringLiteral("ping"), static_cast<qint64>(srv->ping()));
            server.insert(QStringLiteral("softFiles"),
                          static_cast<qint64>(srv->softFiles()));
            server.insert(QStringLiteral("hardFiles"),
                          static_cast<qint64>(srv->hardFiles()));
            server.insert(QStringLiteral("tcpFlags"),
                          static_cast<qint64>(srv->tcpFlags()));
            server.insert(QStringLiteral("udpFlags"),
                          static_cast<qint64>(srv->udpFlags()));
            ed2k.insert(QStringLiteral("server"), server);
        }
    }
    info.insert(QStringLiteral("ed2k"), ed2k);

    // -- Kad section ----------------------------------------------------------
    QCborMap kadInfo;
    auto* kad = kad::Kademlia::instance();
    const bool kadRunning = kad && kad->isRunning();
    const bool kadConnected = kad && kad->isConnected();
    const bool kadFirewalled = kad && kad->isFirewalled();

    kadInfo.insert(QStringLiteral("running"), kadRunning);
    kadInfo.insert(QStringLiteral("connected"), kadConnected);
    kadInfo.insert(QStringLiteral("firewalled"), kadFirewalled);

    if (kadConnected && kad) {
        kadInfo.insert(QStringLiteral("udpFirewalled"),
                       kad::UDPFirewallTester::isFirewalledUDP(true));
        kadInfo.insert(QStringLiteral("udpVerified"),
                       kad::UDPFirewallTester::isVerified());

        // Buddy (firewall traversal): 0 none, 1 connecting, 2 connected (BuddyStatus)
        if (theApp.clientList) {
            const BuddyStatus bs = theApp.clientList->buddyStatus();
            kadInfo.insert(QStringLiteral("buddyStatus"), static_cast<int>(bs));
            const UpDownClient* buddy = theApp.clientList->getBuddy();
            if (bs == BuddyStatus::Connected && buddy) {
                kadInfo.insert(QStringLiteral("buddyName"), buddy->userName());
                if (!buddy->userAddress().isNull())
                    kadInfo.insert(QStringLiteral("buddyAddress"), buddy->userAddress().toString());
                kadInfo.insert(QStringLiteral("buddyPort"), buddy->userPort());
            }
        }

        auto* prefs = kad->getPrefs();
        if (prefs) {
            kadInfo.insert(QStringLiteral("ip"),
                           static_cast<qint64>(prefs->ipAddress()));
            kadInfo.insert(QStringLiteral("id"),
                           static_cast<qint64>(prefs->ipAddress()));
            kadInfo.insert(QStringLiteral("hash"),
                           prefs->kadId().toHexString());
            kadInfo.insert(QStringLiteral("internPort"), prefs->internKadPort());
            kadInfo.insert(QStringLiteral("externPort"),
                           prefs->useExternKadPort()
                               ? prefs->externalKadPort() : 0);
        }

        kadInfo.insert(QStringLiteral("users"),
                       static_cast<qint64>(kad->getKademliaUsers()));
        kadInfo.insert(QStringLiteral("usersExperimental"),
                       static_cast<qint64>(kad->getKademliaUsers(true)));
        kadInfo.insert(QStringLiteral("files"),
                       static_cast<qint64>(kad->getKademliaFiles()));

        auto* indexed = kad->getIndexed();
        if (indexed) {
            QCborMap idx;
            idx.insert(QStringLiteral("source"),
                       static_cast<qint64>(indexed->m_totalIndexSource));
            idx.insert(QStringLiteral("keyword"),
                       static_cast<qint64>(indexed->m_totalIndexKeyword));
            idx.insert(QStringLiteral("notes"),
                       static_cast<qint64>(indexed->m_totalIndexNotes));
            idx.insert(QStringLiteral("load"),
                       static_cast<qint64>(indexed->m_totalIndexLoad));
            kadInfo.insert(QStringLiteral("indexed"), idx);
        }
    }
    info.insert(QStringLiteral("kad"), kadInfo);

    // Port mapping — reported alongside the firewall state because they answer
    // the same user question, and because a Degraded mapping is precisely the
    // case where "port forwarded" and "still firewalled" are both true.
    QCborMap portMapInfo;
    if (theApp.portMapper != nullptr) {
        const PortMapper* mapper = theApp.portMapper;
        portMapInfo.insert(QStringLiteral("status"), static_cast<int>(mapper->status()));
        portMapInfo.insert(QStringLiteral("statusText"), portMapStatusName(mapper->status()));
        portMapInfo.insert(QStringLiteral("method"), static_cast<int>(mapper->activeMethod()));
        portMapInfo.insert(QStringLiteral("methodText"),
                           portMapMethodName(mapper->activeMethod()));
        portMapInfo.insert(QStringLiteral("externalAddress"),
                           mapper->externalAddress().toString());

        QCborArray mappings;
        for (const PortMapping& mapping : mapper->mappings()) {
            QCborMap entry;
            entry.insert(QStringLiteral("purpose"),
                         portMapPurposeName(mapping.request.purpose));
            entry.insert(QStringLiteral("protocol"),
                         mapping.request.protocol == PortMapProtocol::Udp
                             ? QStringLiteral("UDP") : QStringLiteral("TCP"));
            entry.insert(QStringLiteral("family"),
                         mapping.request.family == PortMapFamily::IPv6 ? 6 : 4);
            entry.insert(QStringLiteral("internalPort"), mapping.request.internalPort);
            entry.insert(QStringLiteral("externalPort"), mapping.externalPort);
            entry.insert(QStringLiteral("lifetime"),
                         static_cast<qint64>(mapping.lifetimeSecs));
            entry.insert(QStringLiteral("usable"), mapping.isUsable());
            mappings.append(entry);
        }
        portMapInfo.insert(QStringLiteral("mappings"), mappings);
    } else {
        portMapInfo.insert(QStringLiteral("status"),
                           static_cast<int>(PortMapStatus::Disabled));
        portMapInfo.insert(QStringLiteral("statusText"),
                           portMapStatusName(PortMapStatus::Disabled));
    }
    info.insert(QStringLiteral("portmap"), portMapInfo);

    return info;
}

QCborArray kadContacts()
{
    QCborArray contacts;
    auto* kad = kad::Kademlia::instance();
    if (kad && kad->isRunning()) {
        auto* zone = kad->getRoutingZone();
        if (zone) {
            kad::ContactArray allContacts;
            zone->getAllEntries(allContacts);
            for (const auto* c : allContacts) {
                QCborMap m;
                m.insert(QStringLiteral("clientId"), c->getClientID().toHexString());
                m.insert(QStringLiteral("distance"), c->getDistance().toBinaryString());
                m.insert(QStringLiteral("ip"), static_cast<qint64>(c->address().toUint32()));
                m.insert(QStringLiteral("addr"), c->address().toString());   // IPv6-capable form
                m.insert(QStringLiteral("cc"), countryCodeOf(c->address()));
                m.insert(QStringLiteral("udpPort"), c->getUDPPort());
                m.insert(QStringLiteral("tcpPort"), c->getTCPPort());
                m.insert(QStringLiteral("version"), c->getVersion());
                m.insert(QStringLiteral("type"), c->getType());
                // The contact icon (MFC KadContactListCtrl.cpp:117-124)
                m.insert(QStringLiteral("ipVerified"), c->isIpVerified());
                m.insert(QStringLiteral("bootstrap"), c->isBootstrapContact());
                contacts.append(m);
            }
        }
    }
    return contacts;
}

QCborMap kadStatus()
{
    QCborMap status;
    auto* kad = kad::Kademlia::instance();
    status.insert(QStringLiteral("running"), kad && kad->isRunning());
    status.insert(QStringLiteral("connected"), kad && kad->isConnected());
    status.insert(QStringLiteral("firewalled"), kad && kad->isFirewalled());

    if (kad && kad->isRunning()) {
        auto* zone = kad->getRoutingZone();
        if (zone) {
            kad::ContactArray allContacts;
            zone->getAllEntries(allContacts);
            status.insert(QStringLiteral("contactCount"),
                          static_cast<qint64>(allContacts.size()));
        }
        auto* udp = kad->getUDPListener();
        if (udp) {
            status.insert(QStringLiteral("hellosSent"),
                          static_cast<qint64>(udp->totalHellosSent()));
            status.insert(QStringLiteral("hellosReceived"),
                          static_cast<qint64>(udp->totalHellosReceived()));
        }
        status.insert(QStringLiteral("users"),
                      static_cast<qint64>(kad->getKademliaUsers()));
        status.insert(QStringLiteral("usersExperimental"),
                      static_cast<qint64>(kad->getKademliaUsers(true)));
        status.insert(QStringLiteral("files"),
                      static_cast<qint64>(kad->getKademliaFiles()));
    }
    if (kad && kad->isConnected()) {
        status.insert(QStringLiteral("udpFirewalled"),
                      kad::UDPFirewallTester::isFirewalledUDP(true));
        status.insert(QStringLiteral("udpVerified"),
                      kad::UDPFirewallTester::isVerified());
        auto* prefs = kad->getPrefs();
        if (prefs) {
            status.insert(QStringLiteral("ip"),
                          static_cast<qint64>(prefs->ipAddress()));
            // ID is the IP in eD2K byte order (first octet in LSB)
            const uint32_t kadIp = prefs->ipAddress();
            const uint32_t ed2kId = ((kadIp & 0xFF) << 24) | ((kadIp & 0xFF00) << 8)
                                  | ((kadIp >> 8) & 0xFF00) | ((kadIp >> 24) & 0xFF);
            status.insert(QStringLiteral("id"),
                          static_cast<qint64>(ed2kId));
            status.insert(QStringLiteral("internPort"), prefs->internKadPort());
            status.insert(QStringLiteral("externPort"),
                          prefs->useExternKadPort()
                              ? prefs->externalKadPort() : 0);
        }
    }
    return status;
}

} // namespace eMule::ops
