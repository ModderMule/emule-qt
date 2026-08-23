#include "pch.h"
/// @file KadUDPListener.cpp
/// @brief Kademlia UDP packet handler implementation.

#include "kademlia/KadUDPListener.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadEntry.h"
#include "kademlia/KadClientSearcher.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadIndexed.h"
#include "kademlia/KadMiscUtils.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "prefs/Preferences.h"
#include "kademlia/KadSearchManager.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "ipfilter/IPFilter.h"
#include "net/HostResolver.h"
#include "net/Address.h"
#include "net/EMSocket.h"
#include "net/Packet.h"




namespace eMule::kad {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

KademliaUDPListener::KademliaUDPListener(QObject* parent)
    : QObject(parent)
{
}

KademliaUDPListener::~KademliaUDPListener() = default;

// ---------------------------------------------------------------------------
// Public methods — bootstrap
// ---------------------------------------------------------------------------

void KademliaUDPListener::bootstrap(const QString& host, uint16 udpPort)
{
    // Kad contacts are keyed by a 32-bit IPv4, so ask for A records only. Taking the
    // first address of either family (as this used to) turned an AAAA-first answer into
    // toIPv4Address() == 0 and bootstrapped against 0.0.0.0 without a word.
    if (!m_hostResolver)
        m_hostResolver = new HostResolver(this);

    m_hostResolver->resolve(host, HostResolver::Preference::IPv4Only, this,
        [this, host, udpPort](const HostResolver::Result& result) {
            if (!result.ok()) {
                logKad(QStringLiteral("Kad: Failed to resolve bootstrap host %1: %2")
                           .arg(host, result.errorString));
                return;
            }
            bootstrap(result.first().toUint32(), udpPort);
        });
}

void KademliaUDPListener::bootstrap(uint32 ip, uint16 udpPort, uint8 kadVersion,
                                     const UInt128* cryptTargetID)
{
    // sendPacket() tracks BOOTSTRAP_REQ for us — see isTrackedOutListRequestPacket().
    sendNullPacket(KADEMLIA2_BOOTSTRAP_REQ, ip, udpPort, KadUDPKey(0),
                   (kadVersion >= KADEMLIA_VERSION6_49aBETA) ? cryptTargetID : nullptr);
}

void KademliaUDPListener::firewalledCheck(uint32 ip, uint16 udpPort,
                                           const KadUDPKey& senderKey, uint8 kadVersion)
{
    auto* prefs = Kademlia::getInstancePrefs();
    // The remote node TCP-connects to this port to verify we're reachable.
    // Must be the TCP listener port, not the Kad UDP port.
    const uint16 tcpPort = thePrefs.port();

    if (kadVersion > KADEMLIA_VERSION6_49aBETA) {
        // Kad v2 (v7+): extended request with client hash + connect options for obfuscation support
        SafeMemFile packet;
        packet.writeUInt16(tcpPort);
        io::writeUInt128(packet, prefs ? prefs->clientHash() : UInt128());
        packet.writeUInt8(prefs ? prefs->myConnectOptions(true, false) : uint8{0});
        sendPacket(packet, KADEMLIA_FIREWALLED2_REQ, ip, udpPort, senderKey, nullptr);
        logKad(QStringLiteral("Kad: Sent FIREWALLED2_REQ (v2) to %1:%2, tcpPort=%3")
                   .arg(ipToString(ip)).arg(udpPort).arg(tcpPort));
    } else {
        // Kad v1 compat (v2–v6): legacy request with port only
        SafeMemFile packet;
        packet.writeUInt16(tcpPort);
        sendPacket(packet, KADEMLIA_FIREWALLED_REQ, ip, udpPort, senderKey, nullptr);
        logKad(QStringLiteral("Kad: Sent FIREWALLED_REQ (v1) to %1:%2, tcpPort=%3")
                   .arg(ip).arg(udpPort).arg(tcpPort));
    }

    // Track so we accept the KADEMLIA_FIREWALLED_RES reply from this IP
    addTrackedOutPacket(ip, KADEMLIA_FIREWALLED_REQ);

    // Also let EncryptedStreamSocket accept this node's unencrypted firewall-check callback
    // under require-encryption (and only count its ACK). `ip` is host order; store network
    // order. MFC: CClientList::AddKadFirewallRequest from FirewalledCheck
    // (srchybrid/kademlia/net/KademliaUDPListener.cpp:181).
    if (theApp.clientList)
        theApp.clientList->addKadFirewallRequest(htonl(ip));
}

// ---------------------------------------------------------------------------
// Public methods — send helpers
// ---------------------------------------------------------------------------

void KademliaUDPListener::sendMyDetails(uint8 opcode, uint32 ip, uint16 udpPort,
                                         uint8 kadVersion, const KadUDPKey& targetKey,
                                         const UInt128* cryptTargetID, bool requestAck)
{
    SafeMemFile packet;

    // Write our KadID
    io::writeUInt128(packet, RoutingZone::localKadId());

    // Write our ED2K TCP port
    packet.writeUInt16(thePrefs.port());

    // Write version
    packet.writeUInt8(KADEMLIA_VERSION);

    // Determine which tags to send (matching MFC SendMyDetails)
    auto* prefs = Kademlia::getInstancePrefs();
    bool sendSourceUPort = prefs && !prefs->useExternKadPort();
    bool sendMiscOptions = false;
    bool sendCrypto = false;
    if (kadVersion >= KADEMLIA_VERSION8_49b) {
        bool tcpFW = prefs ? prefs->firewalled() : false;
        bool udpFW = UDPFirewallTester::isFirewalledUDP(true);
        if (requestAck || tcpFW || udpFW)
            sendMiscOptions = true;
        // Send connect options + client hash so peers can encrypt TCP connections to us
        if (prefs && thePrefs.cryptLayerSupported())
            sendCrypto = true;
    }

    uint8 tagCount = (sendSourceUPort ? 1 : 0) + (sendMiscOptions ? 1 : 0)
                     + (sendCrypto ? 2 : 0);
    packet.writeUInt8(tagCount);

    if (sendSourceUPort) {
        // TAG_SOURCEUPORT — internal Kad UDP port when it differs from external
        Tag sourceUPort(FT_SOURCEUPORT, static_cast<uint32>(prefs->internKadPort()));
        io::writeKadTag(packet, sourceUPort);
    }

    if (sendMiscOptions) {
        // TAG_KADMISCOPTIONS — bit0=UDP-FW, bit1=TCP-FW, bit2=requestACK
        uint8 udpFW = UDPFirewallTester::isFirewalledUDP(true) ? 1 : 0;
        uint8 tcpFW = (prefs && prefs->firewalled()) ? 1 : 0;
        uint8 reqACK = requestAck ? 1 : 0;
        uint8 miscOptions = static_cast<uint8>((reqACK << 2) | (tcpFW << 1) | (udpFW << 0));
        Tag miscTag(FT_KADMISCOPTIONS, static_cast<uint32>(miscOptions));
        io::writeKadTag(packet, miscTag);
    }

    if (sendCrypto) {
        // TAG_ENCRYPTION — connect options: bit0=support, bit1=request, bit2=require
        Tag cryptTag(FT_ENCRYPTION,
                     static_cast<uint32>(prefs->myConnectOptions(true, false)));
        io::writeKadTag(packet, cryptTag);

        // Client hash (FT_USER_COUNT) — our ED2K user hash so peers can encrypt TCP to us
        uint8 hashBytes[16];
        prefs->clientHash().toByteArray(hashBytes);
        Tag hashTag(FT_USER_COUNT, hashBytes);
        io::writeKadTag(packet, hashTag);
    }

    if (opcode == KADEMLIA2_HELLO_REQ) {
        m_hellosSent.fetch_add(1, std::memory_order_relaxed);
        logKad(QStringLiteral("Kad: [diag] sending HELLO_REQ to %1:%2")
                   .arg(ipToString(ip)).arg(udpPort));
    }

    // sendPacket() tracks HELLO_REQ / HELLO_RES for us — see
    // isTrackedOutListRequestPacket(). Tracking again here would double-insert
    // and keep the out-track entry alive past its single intended consumption.
    sendPacket(packet, opcode, ip, udpPort, targetKey, cryptTargetID);
}

void KademliaUDPListener::sendPublishSourcePacket(Contact* contact, const UInt128& targetID,
                                                    const UInt128& contactID, const TagList& tags)
{
    if (!contact)
        return;

    SafeMemFile packet;
    io::writeUInt128(packet, targetID);
    io::writeUInt128(packet, contactID);
    io::writeKadTagList(packet, tags);

    UInt128 pubClientID = contact->getClientID();
    sendPacket(packet, KADEMLIA2_PUBLISH_SOURCE_REQ,
               contact->address().toUint32(), contact->getUDPPort(),
               contact->getUDPKey(), &pubClientID);
}

void KademliaUDPListener::sendNullPacket(uint8 opcode, uint32 ip, uint16 udpPort,
                                          const KadUDPKey& targetKey,
                                          const UInt128* cryptTargetID)
{
    SafeMemFile packet;
    sendPacket(packet, opcode, ip, udpPort, targetKey, cryptTargetID);
}

// ---------------------------------------------------------------------------
// Public methods — packet processing
// ---------------------------------------------------------------------------

void KademliaUDPListener::processPacket(const uint8* data, uint32 len, uint32 ip,
                                         uint16 udpPort, bool validReceiverKey,
                                         const KadUDPKey& senderKey)
{
    if (len < 1)
        return;

    // We do not accept unencrypted incoming packets from port 53 (DNS), to avoid
    // attacks based on DNS protocol confusion. MFC KademliaUDPListener.cpp:223-226.
    // The routing table already refuses port-53 contacts (RoutingZone::add); this
    // is the packet-acceptance half of the same mitigation.
    if (udpPort == 53 && senderKey.isEmpty())
        return;

    // Update connection state on every incoming packet (MFC KademliaUDPListener.cpp:229-231)
    auto* prefs = Kademlia::getInstancePrefs();
    if (prefs)
        prefs->setLastContact();
    //UDPFirewallTester::connected();

    uint8 opcode = data[0];
    const uint8* payload = data + 1;
    uint32 payloadLen = len - 1;

    // logKad(QStringLiteral("Kad: processPacket opcode=0x%1 from %2:%3 len=%4")
    //            .arg(opcode, 2, 16, QLatin1Char('0')).arg(ipToString(ip)).arg(udpPort).arg(len));

    // General incoming-request flood protection. MFC KademliaUDPListener.cpp:236-250.
    switch (inTrackListIsAllowedPacket(ip, opcode, validReceiverKey)) {
    case 2:
        // Massive flood — the limiter has already banned the IP. Also drop the
        // contact from the routing zone so we stop treating it as a peer; its
        // packets die at the socket while the ban lasts, so it would otherwise
        // linger as a dead entry.
        if (auto* rz = Kademlia::getInstanceRoutingZone()) {
            if (auto* contact = rz->getContact(ip, udpPort, false))
                contact->expire();
        }
        return;
    case 1:
        return;
    default:
        break;
    }

    switch (opcode) {
    case KADEMLIA2_BOOTSTRAP_REQ:
        process_KADEMLIA2_BOOTSTRAP_REQ(ip, udpPort, senderKey);
        break;
    case KADEMLIA2_BOOTSTRAP_RES:
        process_KADEMLIA2_BOOTSTRAP_RES(payload, payloadLen, ip, udpPort, senderKey, validReceiverKey);
        break;
    case KADEMLIA2_HELLO_REQ:
        process_KADEMLIA2_HELLO_REQ(payload, payloadLen, ip, udpPort, senderKey, validReceiverKey);
        break;
    case KADEMLIA2_HELLO_RES:
        process_KADEMLIA2_HELLO_RES(payload, payloadLen, ip, udpPort, senderKey, validReceiverKey);
        break;
    case KADEMLIA2_HELLO_RES_ACK:
        process_KADEMLIA2_HELLO_RES_ACK(payload, payloadLen, ip, udpPort, validReceiverKey);
        break;
    case KADEMLIA2_REQ:
        process_KADEMLIA2_REQ(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA2_RES:
        process_KADEMLIA2_RES(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA2_SEARCH_KEY_REQ:
        process_KADEMLIA2_SEARCH_KEY_REQ(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA2_SEARCH_SOURCE_REQ:
        process_KADEMLIA2_SEARCH_SOURCE_REQ(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA2_SEARCH_RES:
        process_KADEMLIA2_SEARCH_RES(payload, payloadLen, senderKey, ip, udpPort);
        break;
    case KADEMLIA2_PUBLISH_KEY_REQ:
        process_KADEMLIA2_PUBLISH_KEY_REQ(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA2_PUBLISH_SOURCE_REQ:
        process_KADEMLIA2_PUBLISH_SOURCE_REQ(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA2_PUBLISH_RES:
        process_KADEMLIA2_PUBLISH_RES(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA2_SEARCH_NOTES_REQ:
        process_KADEMLIA2_SEARCH_NOTES_REQ(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA2_PUBLISH_NOTES_REQ:
        process_KADEMLIA2_PUBLISH_NOTES_REQ(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA_FIREWALLED_REQ:   // Kad v1 compat: receive-only (we send KADEMLIA_FIREWALLED2_REQ)
        process_KADEMLIA_FIREWALLED_REQ(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA_FIREWALLED2_REQ:  // Kad v2
        process_KADEMLIA_FIREWALLED2_REQ(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA_FIREWALLED_RES:   // Response to both v1 and v2 requests
        process_KADEMLIA_FIREWALLED_RES(payload, payloadLen, ip, senderKey);
        break;
    case KADEMLIA_FIREWALLED_ACK_RES:
        process_KADEMLIA_FIREWALLED_ACK_RES(payloadLen);
        break;
    case KADEMLIA_FINDBUDDY_REQ:    // No v2 equivalent — part of modern buddy protocol
        process_KADEMLIA_FINDBUDDY_REQ(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA_FINDBUDDY_RES:    // No v2 equivalent
        process_KADEMLIA_FINDBUDDY_RES(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA_CALLBACK_REQ:     // No v2 equivalent
        process_KADEMLIA_CALLBACK_REQ(payload, payloadLen, ip, senderKey);
        break;
    case KADEMLIA2_PING:
        process_KADEMLIA2_PING(ip, udpPort, senderKey);
        break;
    case KADEMLIA2_PONG:
        process_KADEMLIA2_PONG(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA2_FIREWALLUDP:
        process_KADEMLIA2_FIREWALLUDP(payload, payloadLen, ip, senderKey);
        break;
    default:
        logKad(QStringLiteral("Kad: Unknown opcode 0x%1 from %2:%3")
                   .arg(opcode, 2, 16, QChar(u'0'))
                   .arg(ipToString(ip)).arg(udpPort));
        break;
    }
}

void KademliaUDPListener::sendPacket(const uint8* data, uint32 len, uint32 destIP,
                                      uint16 destPort, const KadUDPKey& targetKey,
                                      const UInt128* cryptTargetID)
{
    QByteArray packetData(reinterpret_cast<const char*>(data), static_cast<qsizetype>(len));
    emit packetToSend(std::move(packetData), destIP, destPort,
                      targetKey, cryptTargetID ? *cryptTargetID : UInt128());
}

void KademliaUDPListener::sendPacket(const uint8* data, uint32 len, uint8 opcode,
                                      uint32 destIP, uint16 destPort,
                                      const KadUDPKey& targetKey, const UInt128* cryptTargetID)
{
    // Prepend opcode then delegate to the base sendPacket for encryption + send.
    QByteArray fullPacket;
    fullPacket.reserve(static_cast<qsizetype>(len + 1));
    fullPacket.append(static_cast<char>(opcode));
    if (len > 0)
        fullPacket.append(reinterpret_cast<const char*>(data), static_cast<qsizetype>(len));

    // Track every outgoing request whose response handler gates on
    // isOnOutTrackList(). MFC does this from its SendPacket overloads
    // (KademliaUDPListener.cpp:1884,1900,1913) rather than per call site, so no
    // request can be forgotten. Previously only REQ and PING were tracked, which
    // meant a PUBLISH_RES out-track guard would have rejected every legitimate
    // publish response. firewalledCheck() still tracks its own opcode — it folds
    // FIREWALLED and FIREWALLED2 onto one key.
    if (isTrackedOutListRequestPacket(opcode))
        addTrackedOutPacket(destIP, opcode);

    sendPacket(reinterpret_cast<const uint8*>(fullPacket.constData()),
               static_cast<uint32>(fullPacket.size()),
               destIP, destPort, targetKey, cryptTargetID);
}

void KademliaUDPListener::sendPacket(SafeMemFile& data, uint8 opcode, uint32 destIP,
                                      uint16 destPort, const KadUDPKey& targetKey,
                                      const UInt128* cryptTargetID)
{
    const QByteArray& buf = data.buffer();
    sendPacket(reinterpret_cast<const uint8*>(buf.constData()),
               static_cast<uint32>(buf.size()), opcode, destIP, destPort,
               targetKey, cryptTargetID);
}

bool KademliaUDPListener::findNodeIDByIP(KadClientSearcher* requester, uint32 ip,
                                          uint16 tcpPort, uint16 udpPort)
{
    if (!requester)
        return false;

    FetchNodeIDRequest req;
    req.ip = ip;
    req.tcpPort = tcpPort;
    req.expire = static_cast<uint32>(time(nullptr)) + 60; // 1 minute expiry
    req.requester = requester;
    m_fetchNodeIDRequests.push_back(req);

    // Send HELLO_REQ to discover the node's Kad ID
    sendMyDetails(KADEMLIA2_HELLO_REQ, ip, udpPort, KADEMLIA_VERSION,
                  KadUDPKey(0), nullptr, true);
    return true;
}

void KademliaUDPListener::expireClientSearch(const KadClientSearcher* expireImmediately)
{
    uint32 now = static_cast<uint32>(time(nullptr));
    auto it = m_fetchNodeIDRequests.begin();
    while (it != m_fetchNodeIDRequests.end()) {
        if (it->requester == expireImmediately) {
            // Caller is destructing — remove silently without callback
            it = m_fetchNodeIDRequests.erase(it);
        } else if (it->expire < now) {
            // Erase before callback — the callback may re-enter m_fetchNodeIDRequests
            auto* requester = it->requester;
            it = m_fetchNodeIDRequests.erase(it);
            requester->kadSearchNodeIDByIPResult(KadClientSearchResult::Timeout, nullptr);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// Private methods — packet handlers
// ---------------------------------------------------------------------------

bool KademliaUDPListener::addContact_KADEMLIA2(const uint8* data, uint32 len, uint32 ip,
                                                 uint16& udpPort, uint8* outVersion,
                                                 const KadUDPKey& udpKey, bool& ipVerified,
                                                 bool update, bool fromHelloReq,
                                                 bool* outRequestsACK, UInt128* outContactID)
{
    if (len < 19) // 16 (ID) + 2 (TCP port) + 1 (version)
        return false;

    SafeMemFile io(data, len);
    UInt128 contactID = io::readUInt128(io);
    uint16 tcpPort = io.readUInt16();
    uint8 version = io.readUInt8();

    if (outVersion)
        *outVersion = version;
    if (outContactID)
        *outContactID = contactID;

    // Read tags (if any remaining data) — MFC AddContact_KADEMLIA2
    uint8 tagCount = 0;
    if (io.position() < io.length())
        tagCount = io.readUInt8();

    bool bUDPFirewalled = false;
    bool bTCPFirewalled = false;
    bool reqACK = false;
    uint8 peerConnectOptions = 0;
    UInt128 peerClientHash;
    for (uint8 i = 0; i < tagCount; ++i) {
        try {
            Tag tag = io::readKadTag(io);
            if (tag.isInt() && tag.nameId() == FT_SOURCEUPORT) {
                // TAG_SOURCEUPORT — update UDP port from internal port tag
                uint16 port = static_cast<uint16>(tag.intValue());
                if (port > 0)
                    udpPort = port;
            } else if (tag.isInt() && tag.nameId() == FT_KADMISCOPTIONS) {
                // TAG_KADMISCOPTIONS — bit0=UDP-FW, bit1=TCP-FW, bit2=requestACK
                uint32 val = tag.intValue();
                bUDPFirewalled = (val & 0x01) != 0;
                bTCPFirewalled = (val & 0x02) != 0;
                if ((val & 0x04) != 0 && version >= KADEMLIA_VERSION8_49b)
                    reqACK = true;
            } else if (tag.isInt() && tag.nameId() == FT_ENCRYPTION) {
                // TAG_ENCRYPTION — connect options: bit0=support, bit1=request, bit2=require
                peerConnectOptions = static_cast<uint8>(tag.intValue());
            } else if (tag.isHash() && tag.nameId() == FT_USER_COUNT) {
                // Client hash — peer's ED2K user hash for TCP encryption
                peerClientHash.setValueBE(tag.hashValue());
            }
        } catch (...) {
            break;
        }
    }
    if (outRequestsACK)
        *outRequestsACK = reqACK;

    // Firewall statistics from HELLO_REQ (MFC: lines 470-476)
    if (fromHelloReq && version >= KADEMLIA_VERSION8_49b) {
        if (auto* prefs = Kademlia::getInstancePrefs()) {
            prefs->statsIncUDPFirewalledNodes(bUDPFirewalled);
            prefs->statsIncTCPFirewalledNodes(bTCPFirewalled);
        }
    }

    // Do not add UDP-firewalled contacts to routing table (MFC: line 485)
    if (bUDPFirewalled)
        return false;

    // Add to routing table
    if (auto* rz = Kademlia::getInstanceRoutingZone()) {
        if (update)
            rz->addOrUpdateContact(contactID, ip, udpPort, tcpPort, version, udpKey, ipVerified);

        // Store crypto info on the contact (from TAG_ENCRYPTION / client hash tags)
        if (peerConnectOptions || !(peerClientHash == 0u)) {
            if (auto* c = rz->getContact(contactID)) {
                if (peerConnectOptions)
                    c->setConnectOptions(peerConnectOptions);
                if (!(peerClientHash == 0u))
                    c->setClientHash(peerClientHash);
            }
        }
    }

    return true;
}

void KademliaUDPListener::sendLegacyChallenge(uint32 ip, uint16 udpPort, const UInt128& contactID)
{
    // Verify that a pre-0.49a contact is real and not sent from a spoofed IP.
    // Those versions support no direct validation, so we send a KADEMLIA2_REQ
    // for a random target — the challenge. Only the true holder of that IP can
    // receive our packet and answer it, so any KADEMLIA2_RES that comes back
    // matching the challenge proves the address.
    // MFC KademliaUDPListener.cpp:2122-2155.
    if (hasActiveLegacyChallenge(ip))
        return; // never more than one challenge in flight per IP

    UInt128 challenge;
    challenge.setValueRandom();
    if (challenge == 0u)
        challenge = UInt128(uint32{1}); // a zero challenge is the PING wildcard

    SafeMemFile packet;
    packet.writeUInt8(KADEMLIA_FIND_VALUE);
    // The target we want is our challenge...
    io::writeUInt128(packet, challenge);
    // ...and the contact's ID lets the far end sanity-check the request.
    io::writeUInt128(packet, contactID);

    // The versions we send this to support neither encryption nor obfuscation.
    sendPacket(packet, KADEMLIA2_REQ, ip, udpPort, KadUDPKey(0), nullptr);
    addLegacyChallenge(contactID, challenge, ip, KADEMLIA2_REQ);
}

namespace {

/// Should we accept a publish request for @p keyID from @p ip?
///
/// Two guards, applied identically by all three PUBLISH_*_REQ handlers in MFC
/// (KademliaUDPListener.cpp:1183-1196, :1282-1293, :1513-1526):
///
///  1. Refuse while we are UDP-firewalled. We would happily index the entry but
///     could not be reached to serve it, so the publisher gets a false report of
///     having placed its data somewhere reachable.
///  2. Refuse keys that are not close to our own KadID. Publishes are supposed
///     to land on the nodes nearest the key; without this anyone can dump an
///     arbitrary index onto us regardless of where it belongs. LAN peers are
///     exempt so a local test network still works.
bool shouldAcceptPublish(const UInt128& keyID, uint32 ip)
{
    if (UDPFirewallTester::isFirewalledUDP(true))
        return false;

    auto* prefs = Kademlia::getInstancePrefs();
    if (!prefs)
        return false;

    UInt128 distance(prefs->kadId());
    distance.xorWith(keyID);
    return distance.get32BitChunk(0) <= kSearchTolerance
           || Address::fromHostOrder(ip).isLan();
}

/// Read a `<u16 namelen> <name…>` tag name and build a Tag around @p value.
///
/// A single-byte name is a numeric tag ID and must be normalized to Tag's
/// nameId, exactly as io::readKadTag does when parsing stored entries. Keeping
/// it as a raw byte-array name instead made every meta/numeric search term
/// compare against a name no entry ever carries, so no filter could ever match.
template <typename ValueT>
Tag makeTermTag(SafeMemFile& io, const ValueT& value)
{
    const uint16 nameLen = io.readUInt16();
    QByteArray name(nameLen, Qt::Uninitialized);
    if (nameLen > 0)
        io.read(name.data(), nameLen);

    if (nameLen == 1)
        return Tag(static_cast<uint8>(name[0]), value);
    return Tag(std::move(name), value);
}

} // namespace

std::unique_ptr<SearchTerm> KademliaUDPListener::createSearchExpressionTree(SafeMemFile& io, int level)
{
    // Prevent excessive recursion (MFC: depth limit 24)
    if (level >= 24)
        return nullptr;

    if (io.position() >= io.length())
        return nullptr;

    uint8 op = io.readUInt8();
    auto term = std::make_unique<SearchTerm>();

    switch (op) {
    case 0x00: {
        // Boolean operator — read sub-byte for operation type (MFC format)
        if (io.position() >= io.length())
            return nullptr;
        uint8 boolOp = io.readUInt8();
        switch (boolOp) {
        case 0x00: term->type = SearchTerm::Type::AND; break;
        case 0x01: term->type = SearchTerm::Type::OR; break;
        case 0x02: term->type = SearchTerm::Type::NOT; break;
        default:
            return nullptr;
        }
        term->left = createSearchExpressionTree(io, level + 1);
        term->right = createSearchExpressionTree(io, level + 1);
        if (!term->left || !term->right)
            return nullptr;
        break;
    }
    case 0x01: { // String
        term->type = SearchTerm::Type::String;
        QString str = kadTagStrToLower(io::readStringUTF8(io));
        // Pre-tokenize: a string term carries several words ("aaa bbb ccc") and
        // is matched as "aaa AND bbb AND ccc". Storing it unsplit meant a
        // multi-word term could never match. MFC KademliaUDPListener.cpp:977-978.
        getWords(str, term->strings);
        break;
    }
    case 0x02: { // MetaTag (string)
        term->type = SearchTerm::Type::MetaTag;
        // Lower case — the search code compares against lowercased entry data.
        QString val = kadTagStrToLower(io::readStringUTF8(io));
        term->tag = makeTermTag(io, val);
        break;
    }
    case 0x03:   // Numeric Relation 32-bit
    case 0x08: { // Numeric Relation 64-bit
        // MFC: value + mmop byte + tag name
        uint64 val = (op == 0x03) ? io.readUInt32() : io.readUInt64();
        uint8 mmop = io.readUInt8();
        term->tag = makeTermTag(io, val);
        // mmop: 0=Equal, 1=Greater, 2=Less, 3=GreaterEqual, 4=LessEqual, 5=NotEqual
        switch (mmop) {
        case 0x00: term->type = SearchTerm::Type::OpEqual; break;
        case 0x01: term->type = SearchTerm::Type::OpGreater; break;
        case 0x02: term->type = SearchTerm::Type::OpLess; break;
        case 0x03: term->type = SearchTerm::Type::OpGreaterEqual; break;
        case 0x04: term->type = SearchTerm::Type::OpLessEqual; break;
        case 0x05: term->type = SearchTerm::Type::OpNotEqual; break;
        default:   return nullptr;
        }
        break;
    }
    default:
        return nullptr;
    }

    return term;
}

// ---------------------------------------------------------------------------
// Process handlers — Bootstrap
// ---------------------------------------------------------------------------

void KademliaUDPListener::process_KADEMLIA2_BOOTSTRAP_REQ(uint32 ip, uint16 udpPort,
                                                           const KadUDPKey& senderKey)
{
    auto* rz = Kademlia::getInstanceRoutingZone();
    if (!rz)
        return;

    // Get up to 20 contacts for the bootstrap response
    ContactArray contacts;
    rz->getBootstrapContacts(contacts, 20);

    SafeMemFile packet;
    // Write our own contact info
    io::writeUInt128(packet, RoutingZone::localKadId());
    // ED2K TCP port (MFC: thePrefs.GetPort(), line 505)
    packet.writeUInt16(thePrefs.port());
    packet.writeUInt8(KADEMLIA_VERSION);

    // Write contact list
    packet.writeUInt16(static_cast<uint16>(contacts.size()));
    for (auto* contact : contacts) {
        io::writeUInt128(packet, contact->getClientID());
        packet.writeUInt32(contact->address().toUint32());
        packet.writeUInt16(contact->getUDPPort());
        packet.writeUInt16(contact->getTCPPort());
        packet.writeUInt8(contact->getVersion());
    }

    sendPacket(packet, KADEMLIA2_BOOTSTRAP_RES, ip, udpPort, senderKey, nullptr);
}

void KademliaUDPListener::process_KADEMLIA2_BOOTSTRAP_RES(const uint8* data, uint32 len,
                                                           uint32 ip, uint16 udpPort,
                                                           const KadUDPKey& senderKey,
                                                           bool validReceiverKey)
{
    if (!isOnOutTrackList(ip, KADEMLIA2_BOOTSTRAP_REQ))
        return;

    logKad(QStringLiteral("Kad: BOOTSTRAP_RES from %1:%2, %3 bytes")
               .arg(ipToString(ip)).arg(udpPort).arg(len));

    if (len < 23) // minimum: 16 (ID) + 2 (TCP) + 1 (version) + 2 (count) + 2
        return;

    SafeMemFile io(data, len);

    // Read bootstrapper's info
    UInt128 bootstrapID = io::readUInt128(io);
    uint16 bootstrapTCP = io.readUInt16();
    uint8 bootstrapVersion = io.readUInt8();

    // Read contact list
    uint16 numContacts = io.readUInt16();
    // Add bootstrapper itself to routing zone
    if (auto* rz = Kademlia::getInstanceRoutingZone()) {
        // If we know no contacts at all we are cold-starting, and getClosestTo()
        // hands out only IP-verified contacts — so without this we could never
        // issue the first lookup. Assume the bootstrap answer is genuine just
        // this once. MFC KademliaUDPListener.cpp:542-546: "the attack vectors to
        // exploit this are very small with no major effects, so that's a good
        // trade-off".
        const bool assumeVerified = rz->getNumContacts() == 0;

        // While still probing the shipped bootstrap list, don't fold each
        // bootstrap responder into the routing table — just harvest the contacts
        // it returns. Once the list is exhausted (normal operation) it is added.
        // MFC KademliaUDPListener.cpp:547.
        if (Kademlia::s_bootstrapList.empty()) {
            rz->addOrUpdateContact(bootstrapID, ip, udpPort, bootstrapTCP,
                                   bootstrapVersion, senderKey,
                                   validReceiverKey || assumeVerified);
        }

        // Add all received contacts to routing zone
        for (uint16 i = 0; i < numContacts && io.position() < io.length(); ++i) {
            UInt128 contactID = io::readUInt128(io);
            uint32 contactIP = io.readUInt32();
            uint16 contactUDP = io.readUInt16();
            uint16 contactTCP = io.readUInt16();
            uint8 contactVersion = io.readUInt8();

            rz->add(contactID, contactIP, contactUDP, contactTCP,
                    contactVersion, KadUDPKey(0), assumeVerified,
                    true /*update*/, false /*fromHello*/, false /*fromNodesDat*/);
        }
    }

    // Mark that we've had contact
    if (auto* prefs = Kademlia::getInstancePrefs())
        prefs->setLastContact();
}

// ---------------------------------------------------------------------------
// Process handlers — Hello
// ---------------------------------------------------------------------------

void KademliaUDPListener::process_KADEMLIA2_HELLO_REQ(const uint8* data, uint32 len,
                                                       uint32 ip, uint16 udpPort,
                                                       const KadUDPKey& senderKey,
                                                       bool validReceiverKey)
{
    uint8 version = 0;
    bool ipVerified = validReceiverKey;
    UInt128 contactID;

    const bool addedOrUpdated =
        addContact_KADEMLIA2(data, len, ip, udpPort, &version, senderKey,
                             ipVerified, true, true, nullptr, &contactID);

    if (ipVerified) {
        if (auto* rz = Kademlia::getInstanceRoutingZone())
            rz->verifyContact(contactID, ip);
    }

    // Answer, and — if this contact entered our routing table without already
    // having proved its IP — ask it for an ACK so we complete a three-way
    // handshake and learn it is not spoofing the source address.
    // MFC KademliaUDPListener.cpp:573.
    //
    // This is the side that *requests* the ACK. The port previously hardcoded
    // false here and instead sent a HELLO_RES_ACK from this handler carrying the
    // remote's ID, which is the wrong side of the exchange and the wrong payload,
    // so the handshake never functioned in either direction.
    sendMyDetails(KADEMLIA2_HELLO_RES, ip, udpPort, version,
                  senderKey, &contactID, addedOrUpdated && !validReceiverKey);

    if (!addedOrUpdated)
        return;

    // Peers too old for HELLO_RES_ACK still need verifying. The three version
    // bands below are disjoint and together cover everything we accept.
    if (!validReceiverKey && !hasActiveLegacyChallenge(ip)) {
        if (version == KADEMLIA_VERSION7_49a) {
            // v7 has sender/receiver keys but no HELLO_RES_ACK — a PING whose
            // PONG we can match proves the IP. A zero challenge is the wildcard
            // isLegacyChallenge() accepts for PING. MFC :578-581.
            addLegacyChallenge(contactID, UInt128(), ip, KADEMLIA2_PING);
            sendNullPacket(KADEMLIA2_PING, ip, udpPort, senderKey, nullptr);
        } else if (version < KADEMLIA_VERSION7_49a) {
            // Pre-v7 supports neither, so fall back to the KADEMLIA2_REQ
            // challenge. MFC :593-596.
            sendLegacyChallenge(ip, udpPort, contactID);
        } else if (version > KADEMLIA_VERSION5_48a) {
            // v8+ verify via HELLO_RES_ACK, not a challenge — so piggyback
            // external-port discovery on this exchange instead. Mutually
            // exclusive with the challenge branches above. MFC :590-591.
            if (auto* prefs = Kademlia::getInstancePrefs()) {
                if (prefs->findExternKadPort(false))
                    sendNullPacket(KADEMLIA2_PING, ip, udpPort, senderKey, nullptr);
            }
        }
    }

    // Check if firewalled — ask this node to report our external IP
    // Note: recheckIP() counts received responses, not sent requests.
    // Multiple HELLO_RES can arrive before any FIREWALLED_RES, causing
    // more than KADEMLIAFIREWALLCHECKS requests to be sent.
    // This matches original MFC eMule behavior.
    if (auto* prefs = Kademlia::getInstancePrefs()) {
        if (prefs->recheckIP() && !Kademlia::shouldSkipFirewallChecks()) {
            logKad(QStringLiteral("Kad: HELLO_REQ_ACK triggered firewall check to %1:%2")
                       .arg(ipToString(ip)).arg(udpPort));
            firewalledCheck(ip, udpPort, senderKey, version);
        }
    }
}

void KademliaUDPListener::process_KADEMLIA2_HELLO_RES(const uint8* data, uint32 len,
                                                       uint32 ip, uint16 udpPort,
                                                       const KadUDPKey& senderKey,
                                                       bool validReceiverKey)
{
    m_hellosReceived.fetch_add(1, std::memory_order_relaxed);

    if (!isOnOutTrackList(ip, KADEMLIA2_HELLO_REQ))
        return;

    uint8 version = 0;
    bool ipVerified = validReceiverKey;
    bool sendACK = false;
    UInt128 contactID;

    const bool addedOrUpdated =
        addContact_KADEMLIA2(data, len, ip, udpPort, &version, senderKey,
                             ipVerified, true, false, &sendACK, &contactID);

    if (sendACK) {
        // The remote asked us to prove we are not a spoofed contact. Reply with
        // *our own* KadID — MFC KademliaUDPListener.cpp:645-659.
        if (senderKey.isEmpty()) {
            // Without a sender key our reply could not be validated anyway;
            // most likely a bug in the remote client.
            logKad(QStringLiteral("Kad: HELLO_RES from %1 demands an ACK but sent no sender key")
                       .arg(ipToString(ip)));
        } else {
            auto* prefs = Kademlia::getInstancePrefs();
            SafeMemFile ackPacket;
            io::writeUInt128(ackPacket, prefs ? prefs->kadId() : UInt128());
            ackPacket.writeUInt8(0); // no tags at this time
            sendPacket(ackPacket, KADEMLIA2_HELLO_RES_ACK, ip, udpPort, senderKey, nullptr);
        }
    } else if (addedOrUpdated && !validReceiverKey && version < KADEMLIA_VERSION7_49a) {
        // Even though this is an answer to our own request, it can still be
        // spoofed by anyone who can guess we would send a HELLO_REQ, and these
        // versions support no keys. MFC :661-667.
        sendLegacyChallenge(ip, udpPort, contactID);
    }

    // Piggyback external-port discovery onto the HELLO exchange with v6+ peers.
    // MFC KademliaUDPListener.cpp:671-672.
    if (auto* prefs = Kademlia::getInstancePrefs()) {
        if (version > KADEMLIA_VERSION5_48a && prefs->findExternKadPort(false))
            sendNullPacket(KADEMLIA2_PING, ip, udpPort, senderKey, nullptr);
    }

    if (!addedOrUpdated)
        return;

    // If the contact's IP was verified (valid receiver key proves the remote
    // echoed back our UDP verify key), mark it in the routing table.
    if (ipVerified) {
        if (auto* rz = Kademlia::getInstanceRoutingZone())
            rz->verifyContact(contactID, ip);
    }

    // SafeKad: track verified node identity
    if (auto* sk = Kademlia::getInstanceSafeKad())
        sk->trackNode(ip, udpPort, contactID, true);

    // Deliver node-ID result to any pending FetchNodeIDRequest for this IP.
    // The contact data starts with 16 bytes KadID + 2 bytes TCP port.
    // Matches MFC KademliaUDPListener.cpp:461-474.
    if (len >= 18 && !m_fetchNodeIDRequests.empty()) {
        uint16 tcpPort = static_cast<uint16>(static_cast<uint16>(data[16]) | (static_cast<uint16>(data[17]) << 8));
        uint8 nodeIDBytes[16];
        contactID.toByteArray(nodeIDBytes);

        for (auto it = m_fetchNodeIDRequests.begin(); it != m_fetchNodeIDRequests.end(); ++it) {
            if (it->ip == ip && it->tcpPort == tcpPort) {
                // Erase before callback — the callback may re-enter m_fetchNodeIDRequests
                auto* requester = it->requester;
                m_fetchNodeIDRequests.erase(it);
                requester->kadSearchNodeIDByIPResult(KadClientSearchResult::Succeeded, nodeIDBytes);
                break;
            }
        }
    }

    // Mark that we've had contact — enables the staged bootstrap flow
    if (auto* prefs = Kademlia::getInstancePrefs())
        prefs->setLastContact();

    // Check if firewalled — ask this node to report our external IP
    // Note: recheckIP() counts received responses, not sent requests.
    // Multiple HELLO_RES can arrive before any FIREWALLED_RES, causing
    // more than KADEMLIAFIREWALLCHECKS requests to be sent.
    // This matches original MFC eMule behavior.
    if (auto* prefs = Kademlia::getInstancePrefs()) {
        if (prefs->recheckIP() && !Kademlia::shouldSkipFirewallChecks()) {
            logKad(QStringLiteral("Kad: HELLO_RES triggered firewall check to %1:%2")
                       .arg(ipToString(ip)).arg(udpPort));
            firewalledCheck(ip, udpPort, senderKey, version);
        }
    }
}

void KademliaUDPListener::process_KADEMLIA2_HELLO_RES_ACK(const uint8* data, uint32 len,
                                                            uint32 ip, uint16 udpPort,
                                                            bool validReceiverKey)
{
    // This packet completes the three-way handshake and is the sole basis for
    // marking the contact IP-verified, so all three of MFC's guards apply
    // (KademliaUDPListener.cpp:604-620). Without them any unsolicited datagram
    // could promote an arbitrary KadID to verified, which is precisely what the
    // ACK exists to prevent.
    if (len < 17) // 16 (KadID) + 1 (tag count)
        return;

    if (!isOnOutTrackList(ip, KADEMLIA2_HELLO_RES)) {
        logKad(QStringLiteral("Kad: unrequested HELLO_RES_ACK from %1 — dropped")
                   .arg(ipToString(ip)));
        return;
    }

    if (!validReceiverKey) {
        logKad(QStringLiteral("Kad: HELLO_RES_ACK from %1 has an invalid receiver key — dropped")
                   .arg(ipToString(ip)));
        return;
    }

    SafeMemFile io(data, len);
    UInt128 remoteID = io::readUInt128(io);

    // SafeKad: track verified node identity
    if (auto* sk = Kademlia::getInstanceSafeKad())
        sk->trackNode(ip, udpPort, remoteID, true);

    // Verify the contact's IP in the routing table
    if (auto* rz = Kademlia::getInstanceRoutingZone())
        rz->verifyContact(remoteID, ip);
}

// ---------------------------------------------------------------------------
// Process handlers — Routing requests
// ---------------------------------------------------------------------------

void KademliaUDPListener::process_KADEMLIA2_REQ(const uint8* data, uint32 len, uint32 ip,
                                                  uint16 udpPort, const KadUDPKey& senderKey)
{
    if (len < 33) // 1 (type) + 16 (target) + 16 (receiver)
        return;

    auto* rz = Kademlia::getInstanceRoutingZone();
    if (!rz)
        return;

    SafeMemFile io(data, len);
    uint8 type = io.readUInt8();
    type &= 0x1F;
    if (type == 0)
        return;

    UInt128 target = io::readUInt128(io);

    // Sanity check: the sender writes the contact's (our) Kad ID so we can
    // verify the request is actually intended for us.  Silently drop if the
    // check doesn't match — matches MFC Process_KADEMLIA2_REQ behaviour.
    UInt128 check = io::readUInt128(io);
    if (!(RoutingZone::localKadId() == check))
        return;

    // Compute distance for lookup
    UInt128 distance(RoutingZone::localKadId());
    distance.xorWith(target);

    // Get closest contacts — maxRequired=type (contact count requested by sender).
    // In LAN mode, include type-3 (uncontacted) contacts so newly discovered nodes
    // are shared immediately — otherwise they stay invisible until HELLO'd, preventing
    // routing table growth beyond the initial seed set.
    const uint32 maxType = (Kademlia::instance() && Kademlia::instance()->isRunningInLANMode()) ? 3 : 2;
    ContactMap results;
    rz->getClosestTo(maxType, target, distance, static_cast<uint32>(type), results, true, false);

    // Build response packet
    SafeMemFile resPacket;
    io::writeUInt128(resPacket, target);
    resPacket.writeUInt8(static_cast<uint8>(std::min(results.size(), size_t{kK * 2})));

    uint32 count = 0;
    for (auto& [dist, contact] : results) {
        if (count >= kK * 2)
            break;
        io::writeUInt128(resPacket, contact->getClientID());
        resPacket.writeUInt32(contact->address().toUint32());
        resPacket.writeUInt16(contact->getUDPPort());
        resPacket.writeUInt16(contact->getTCPPort());
        resPacket.writeUInt8(contact->getVersion());
        ++count;
    }

    sendPacket(resPacket, KADEMLIA2_RES, ip, udpPort, senderKey, nullptr);

    // In LAN mode with a small routing table, proactively discover the requester.
    // The REQ packet lacks the sender's KadID, so we send a HELLO_REQ to learn it.
    // The HELLO_RES handler will add them to our routing table automatically.
    if (Kademlia::instance() && Kademlia::instance()->isRunningInLANMode()
        && rz->getNumContacts() < 100 && !rz->getContact(ip, udpPort, false))
    {
        sendMyDetails(KADEMLIA2_HELLO_REQ, ip, udpPort,
                      KADEMLIA_VERSION, senderKey, nullptr, true);
    }
}

void KademliaUDPListener::process_KADEMLIA2_RES(const uint8* data, uint32 len, uint32 ip,
                                                  uint16 udpPort, const KadUDPKey& /*senderKey*/)
{
    if (!isOnOutTrackList(ip, KADEMLIA2_REQ)) {
        logKad(QStringLiteral("Kad: KADEMLIA2_RES from %1:%2 dropped — not on track list")
                   .arg(ipToString(ip)).arg(udpPort));
        return;
    }
    logKad(QStringLiteral("Kad: KADEMLIA2_RES from %1:%2, %3 bytes")
               .arg(ipToString(ip)).arg(udpPort).arg(len));

    if (len < 17) // 16 (target) + 1 (count)
        return;

    SafeMemFile io(data, len);
    UInt128 target = io::readUInt128(io);
    uint8 numContacts = io.readUInt8();

    // Is this the answer to one of our legacy (pre-0.49a) challenges? Those peers
    // support no direct verification, so we sent a KADEMLIA2_REQ with a random
    // target and treat any answer as proof the IP is not spoofed.
    // MFC KademliaUDPListener.cpp:759-767.
    UInt128 challengeContactID;
    if (isLegacyChallenge(target, ip, KADEMLIA2_REQ, challengeContactID)) {
        if (auto* rz = Kademlia::getInstanceRoutingZone()) {
            if (!rz->verifyContact(challengeContactID, ip)) {
                logKad(QStringLiteral("Kad: KADEMLIA2_RES: no valid sender in routing table for legacy challenge (%1)")
                           .arg(ipToString(ip)));
            }
        }
        return; // we do not care about the rest of its content
    }

    // Exact size — 16 (target) + 1 (count) + 25 per contact. MFC :770.
    // A short read used to silently yield a truncated contact list instead of
    // rejecting a malformed packet.
    if (len != 17u + 25u * numContacts) {
        logKad(QStringLiteral("Kad: KADEMLIA2_RES from %1 has wrong size %2 for %3 contacts — dropped")
                   .arg(ipToString(ip)).arg(len).arg(numContacts));
        return;
    }

    // Refuse answers for a search that already expired, or that carry more
    // contacts than we asked for. MFC :778-786. Returns 0 for an unknown target,
    // which is exactly the expired-search case.
    if (numContacts > SearchManager::getExpectedResponseContactCount(target)) {
        logKad(QStringLiteral("Kad: KADEMLIA2_RES from %1 — search expired or over-answered (%2 contacts) — dropped")
                   .arg(ipToString(ip)).arg(numContacts));
        return;
    }

    // SafeKad: track the responding node if we can identify it in the routing table
    if (auto* sk = Kademlia::getInstanceSafeKad()) {
        if (auto* rz = Kademlia::getInstanceRoutingZone()) {
            if (auto* sender = rz->getContact(ip, udpPort, false))
                sk->trackNode(ip, udpPort, sender->getClientID(), true);
        }
    }

    // MFC: firewall check searches skip routing table add — those contacts
    // must remain un-contacted for the UDP firewall test to be valid.
    // MFC :789 only treats the search as such while a check is actually running;
    // once it finished the answer is processed as an ordinary routing answer.
    const bool isFWCheckSearch = UDPFirewallTester::isFWCheckUDPRunning()
                                 && SearchManager::isFWCheckUDPSearch(target);

    // MFC :806 deliberately cripples a FW check search: the contacts go to the
    // tester only — never into the routing zone, and never back to the search
    // manager, which would UDP-ask them and destroy the "not contacted yet"
    // property the whole test rests on.
    // On a LAN-only network that leaves the tester with nothing: the routing
    // table holds a handful of nodes and every answer repeats the same ones, so
    // a crippled search never reaches an un-asked peer. There — and only there —
    // we keep delivering the answer to the search manager as well.
    const bool lanMode = Kademlia::instance() != nullptr
                         && Kademlia::instance()->isRunningInLANMode();

    auto* rz = Kademlia::getInstanceRoutingZone();
    auto* ipFilter = Kademlia::getIPFilter();
    uint32 ignoredCount = 0;
    uint32 fwFedCount = 0;

    ContactArray results;
    // Each contact entry: 16 (KadID) + 4 (IP) + 2 (UDP) + 2 (TCP) + 1 (ver) = 25 bytes
    for (uint8 i = 0; i < numContacts && io.length() - io.position() >= 25; ++i) {
        UInt128 contactID = io::readUInt128(io);
        uint32 contactIP = io.readUInt32();
        uint16 contactUDP = io.readUInt16();
        uint16 contactTCP = io.readUInt16();
        uint8 contactVersion = io.readUInt8();

        // Vet every contact before it can reach a live search. Previously only
        // the routing-table insert was filtered and its verdict was discarded, so
        // a hostile responder could seed m_possible with bogon, ipfiltered,
        // port-53 or Kad1 addresses that the search would then go and query.
        // MFC :793-806.
        if (contactVersion < KADEMLIA_VERSION2_47a) // Kad1 is no longer accepted
            continue;
        const uint32 hostIP = htonl(contactIP);
        if (!isGoodIPPort(hostIP, contactUDP)) {
            ++ignoredCount;
            continue;
        }
        if (ipFilter && ipFilter->isFiltered(hostIP, thePrefs.ipFilterLevel())) {
            ++ignoredCount;
            continue;
        }
        if (contactUDP == 53 && contactVersion <= KADEMLIA_VERSION5_48a) {
            ++ignoredCount; // no DNS port without encryption
            continue;
        }

        // Outside LAN mode a FW check contact stops here: straight to the tester,
        // no routing-zone entry, no Contact object, nothing for the search manager
        // to query. MFC :806.
        if (isFWCheckSearch && !lanMode) {
            if (UDPFirewallTester::needsMoreTestContacts()) {
                UDPFirewallTester::addPossibleTestContact(contactID, contactIP, contactUDP,
                                                          contactTCP, target, contactVersion,
                                                          KadUDPKey(0), false);
                ++fwFedCount;
            }
            continue;
        }

        // Add to routing table — matches MFC Process_KADEMLIA2_RES behavior.
        // Skip for firewall check searches (MFC: contacts must stay un-contacted).
        // Note update/fromHello are false here: a routing answer must not be able
        // to mutate existing entries or claim a completed HELLO handshake.
        bool wasAdded = false;
        if (!isFWCheckSearch && rz) {
            wasAdded = rz->addOrUpdateContact(contactID, contactIP, contactUDP, contactTCP,
                                              contactVersion, KadUDPKey(0), false,
                                              /*update=*/false, /*fromHello=*/false);
        }

        auto* contact = new Contact(contactID, contactIP, contactUDP, contactTCP,
                                     target, contactVersion, KadUDPKey(0), false);

        // If the contact made it into the routing table it is trustworthy enough;
        // otherwise it still has to pass the duplicate/hijack and IP-limit checks.
        if (wasAdded || !rz || rz->isAcceptableContact(contact)) {
            results.push_back(contact);
        } else {
            ++ignoredCount;
            delete contact;
        }
    }

    if (ignoredCount > 0) {
        logKad(QStringLiteral("Kad: ignored %1 bad contacts in routing answer from %2")
                   .arg(ignoredCount).arg(ipToString(ip)));
    }

    // In LAN mode the FW check contacts were kept, so feed them to the tester here
    // (skip entirely when the tester already has enough candidates).
    if (isFWCheckSearch && lanMode) {
        for (const auto* contact : results) {
            if (!UDPFirewallTester::needsMoreTestContacts())
                break;
            UDPFirewallTester::addPossibleTestContact(
                contact->getClientID(), contact->address().toUint32(),
                contact->getUDPPort(), contact->getTCPPort(),
                target, contact->getVersion(),
                contact->getUDPKey(), contact->isIpVerified(),
                contact->connectOptions(), contact->clientHash());
            ++fwFedCount;
        }
    }
    if (isFWCheckSearch) {
        logKad(QStringLiteral("Kad: FW check search response — feeding %1 contacts to UDP FW tester")
                   .arg(fwFedCount));
        if (fwFedCount > 0)
            UDPFirewallTester::queryNextClient();
    }

    // MFC :838 calls ProcessResponse unconditionally. Outside LAN mode `results`
    // is empty for a FW check search — that crippled search is exactly the point.
    SearchManager::processResponse(target, ip, udpPort, results);
}

// ---------------------------------------------------------------------------
// Process handlers — Search
// ---------------------------------------------------------------------------

void KademliaUDPListener::process_KADEMLIA2_SEARCH_KEY_REQ(const uint8* data, uint32 len,
                                                            uint32 ip, uint16 udpPort,
                                                            const KadUDPKey& senderKey)
{
    if (len < 16)
        return;

    SafeMemFile io(data, len);
    UInt128 target = io::readUInt128(io);
    uint16 startPos = (io.position() + 2 <= io.length()) ? io.readUInt16() : 0;

    // MFC: bit 15 of startPos signals a restrictive search with expression tree
    bool restrictive = (startPos & 0x8000) != 0;
    startPos &= static_cast<uint16>(~0x8000);

    // Parse search expression only if restrictive bit is set
    std::unique_ptr<SearchTerm> searchTerms;
    if (restrictive && io.position() < io.length())
        searchTerms = createSearchExpressionTree(io, 0);

    // Serve keyword results from local index
    if (auto* indexed = Kademlia::getInstanceIndexed()) {
        indexed->sendValidKeywordResult(target, searchTerms.get(),
                                        ip, udpPort, false, startPos, senderKey);
    }
}

void KademliaUDPListener::process_KADEMLIA2_SEARCH_SOURCE_REQ(const uint8* data, uint32 len,
                                                                uint32 ip, uint16 udpPort,
                                                                const KadUDPKey& senderKey)
{
    if (len < 32) // 16 (fileID) + 16 (some minimum)
        return;

    SafeMemFile io(data, len);
    UInt128 target = io::readUInt128(io);
    uint16 startPos = (io.position() + 2 <= io.length()) ? io.readUInt16() : 0;
    uint64 fileSize = (io.position() + 8 <= io.length()) ? io.readUInt64() : 0;

    // Serve source results from local index
    if (auto* indexed = Kademlia::getInstanceIndexed())
        indexed->sendValidSourceResult(target, ip, udpPort, startPos, fileSize, senderKey);
}

void KademliaUDPListener::process_KADEMLIA2_SEARCH_RES(const uint8* data, uint32 len,
                                                        const KadUDPKey& /*senderKey*/,
                                                        uint32 ip, uint16 udpPort)
{
    // Kad2 format: UInt128 source + UInt128 target + uint16 count + results
    if (len < 34)
        return;

    SafeMemFile io(data, len);
    [[maybe_unused]] UInt128 source = io::readUInt128(io);  // sender's node ID
    UInt128 target = io::readUInt128(io);
    uint16 count = io.readUInt16();

    logKad(QStringLiteral("Kad: SEARCH_RES from %1:%2, target=%3, count=%4")
               .arg(ipToString(ip)).arg(udpPort)
               .arg(target.toHexString()).arg(count));

    try {
        for (uint16 i = 0; i < count && io.position() < io.length(); ++i) {
            UInt128 answer = io::readUInt128(io);
            TagList tags = io::readKadTagList(io);
            SearchManager::processResult(target, answer, tags, ip, udpPort);
        }
    } catch (const FileException&) {
        logKad(QStringLiteral("Kad: SEARCH_RES from %1:%2 — truncated packet, parsed partial results")
                   .arg(ipToString(ip)).arg(udpPort));
    }
}

// ---------------------------------------------------------------------------
// Process handlers — Publish
// ---------------------------------------------------------------------------

void KademliaUDPListener::process_KADEMLIA2_PUBLISH_KEY_REQ(const uint8* data, uint32 len,
                                                             uint32 ip, uint16 udpPort,
                                                             const KadUDPKey& senderKey)
{
    if (len < 32)
        return;

    SafeMemFile io(data, len);
    UInt128 keyID = io::readUInt128(io);

    if (!shouldAcceptPublish(keyID, ip))
        return;

    uint16 count = io.readUInt16();

    auto* indexed = Kademlia::getInstanceIndexed();
    uint8 totalLoad = 0;

    try {
        for (uint16 i = 0; i < count && io.position() < io.length(); ++i) {
            UInt128 sourceID = io::readUInt128(io);
            TagList tags = io::readKadTagList(io);

            if (indexed) {
                auto* entry = new KeyEntry();
                entry->m_keyID = keyID;
                entry->m_sourceID = sourceID;
                entry->m_address = Address::fromHostOrder(ip);
                // Extract known tags to dedicated fields (MFC lines 1217-1249).
                // Filename → m_fileNames (for search term matching),
                // Filesize → m_size; remaining tags → m_tags.
                for (auto& tag : tags) {
                    if (tag.nameId() == FT_FILENAME && tag.isStr()) {
                        if (entry->getCommonFileName().isEmpty())
                            entry->setFileName(tag.strValue());
                    } else if (tag.nameId() == FT_FILESIZE) {
                        if (entry->m_size == 0)
                            entry->m_size = tag.isInt() ? tag.intValue()
                                          : tag.isInt64(false) ? tag.int64Value() : 0;
                    } else if (Entry::tagLookupKey(tag) == QByteArrayLiteral(TAG_KADAICHHASHPUB)) {
                        // The AICH hash is aggregated across publishers rather
                        // than stored as a plain tag — mergeIPsAndFilenames()
                        // reference-counts it and we re-serve the popular one as
                        // TAG_KADAICHHASHRESULT. MFC KademliaUDPListener.cpp:1227-1240.
                        // AICH hashes are SHA-1 digests (20 bytes).
                        constexpr qsizetype kAICHHashSize = 20;
                        // AICH is published as a BSOB (Kad has no BLOB); accept
                        // either in case a peer mislabels the payload.
                        const QByteArray hash = (tag.isBsob() || tag.isBlob()) ? tag.blobValue() : QByteArray();
                        if (hash.size() == kAICHHashSize) {
                            if (entry->aichHashCount() == 0)
                                entry->addRemoveAICHHash(hash, true);
                            else
                                logKad(QStringLiteral("Kad: multiple TAG_KADAICHHASHPUB tags for one file from %1")
                                           .arg(ipToString(ip)));
                        } else {
                            logKad(QStringLiteral("Kad: bad TAG_KADAICHHASHPUB from %1")
                                       .arg(ipToString(ip)));
                        }
                        // consumed, never stored in the tag list
                    } else {
                        entry->addTag(std::move(tag));
                    }
                }

                uint8 load = 0;
                if (!indexed->addKeyword(keyID, sourceID, entry, load))
                    delete entry;
                totalLoad = std::max(totalLoad, load);
            }
        }
    } catch (const FileException&) {
        logKad(QStringLiteral("Kad: PUBLISH_KEY_REQ from %1:%2 — truncated packet")
                   .arg(ipToString(ip)).arg(udpPort));
    }

    // Send publish response with load
    SafeMemFile resPacket;
    io::writeUInt128(resPacket, keyID);
    resPacket.writeUInt8(totalLoad);
    sendPacket(resPacket, KADEMLIA2_PUBLISH_RES, ip, udpPort, senderKey, nullptr);
}

void KademliaUDPListener::process_KADEMLIA2_PUBLISH_SOURCE_REQ(const uint8* data, uint32 len,
                                                                uint32 ip, uint16 udpPort,
                                                                const KadUDPKey& senderKey)
{
    if (len < 32)
        return;

    SafeMemFile io(data, len);
    uint8 load = 0;
    try {
        UInt128 keyID = io::readUInt128(io);

        if (!shouldAcceptPublish(keyID, ip))
            return;

        UInt128 sourceID = io::readUInt128(io);
        TagList tags = io::readKadTagList(io);

        if (auto* indexed = Kademlia::getInstanceIndexed()) {
            auto* entry = new Entry();
            entry->m_keyID = keyID;
            entry->m_sourceID = sourceID;
            entry->m_address = Address::fromHostOrder(ip);
            entry->m_udpPort = udpPort;
            entry->m_lifetime = time(nullptr) + KADEMLIAREPUBLISHTIMES;

            // Interpret the publisher's tags rather than storing them verbatim.
            // MFC KademliaUDPListener.cpp:1300-1380.
            bool addUDPPortTag = true;
            for (auto& tag : tags) {
                switch (tag.nameId()) {
                case FT_SOURCETYPE:
                    if (!entry->m_source) {
                        // A source result is useless without an address, and the
                        // publisher never sends its own — so we synthesise it
                        // from the address the packet actually came from. This
                        // also means a spoofed TAG_SOURCEIP cannot be stored.
                        entry->addTag(Tag(FT_SOURCEIP, entry->m_address.toUint32()));
                        entry->addTag(std::move(tag));
                        entry->m_source = true;
                    }
                    break;
                case FT_SOURCEPORT:
                    if (entry->m_tcpPort == 0) {
                        entry->m_tcpPort = static_cast<uint16>(tag.intValue());
                        entry->addTag(std::move(tag));
                    }
                    break;
                case FT_SOURCEUPORT:
                    if (addUDPPortTag && tag.isInt() && tag.intValue() != 0) {
                        entry->m_udpPort = static_cast<uint16>(tag.intValue());
                        entry->addTag(std::move(tag));
                        addUDPPortTag = false;
                    }
                    break;
                case FT_SERVERIP: {
                    // Drop lowID sources whose buddy is unreachable: a filtered
                    // or banned buddy IP makes the whole source unusable, and
                    // MFC clears m_bSource so the entry is discarded and no
                    // PUBLISH_RES is sent at all. MFC :1344-1369.
                    if (!tag.isInt())
                        break;
                    // Network order, and left that way: FT_SERVERIP is the exception to
                    // the host-order convention FT_SOURCEIP follows just above. This was
                    // the only one of the three sites that had it right — the publisher
                    // and the consumer have been corrected to match. MFC :1344-1369.
                    const uint32 buddyIP = tag.intValue();
                    const char* reason = nullptr;
                    auto* ipFilter = Kademlia::getIPFilter();
                    if (ipFilter && ipFilter->isFiltered(buddyIP, thePrefs.ipFilterLevel()))
                        reason = "IP-filtered";
                    else if (theApp.clientList
                             && theApp.clientList->isBannedClient(Address::fromNetworkOrder(buddyIP)))
                        reason = "banned";

                    if (reason == nullptr) {
                        entry->addTag(std::move(tag));
                    } else {
                        entry->m_source = false;
                        logKad(QStringLiteral("Kad: publish from source %1 with %2 buddy IP — rejected")
                                   .arg(ipToString(ip), QLatin1StringView(reason)));
                    }
                    break;
                }
                default:
                    entry->addTag(std::move(tag));
                    break;
                }
            }

            // If the publisher omitted its UDP port, fall back to the observed
            // one so the stored source still carries a usable port. MFC :1379.
            if (addUDPPortTag)
                entry->addTag(Tag(FT_SOURCEUPORT, static_cast<uint32>(entry->m_udpPort)));

            // Only a genuine source (TAG_SOURCETYPE seen, buddy not rejected)
            // gets indexed, and only a successful index earns a response.
            bool stored = false;
            if (entry->m_source)
                stored = indexed->addSources(keyID, sourceID, entry, load);
            if (!stored) {
                delete entry;
                return;
            }
        }

        // Send publish response with load
        SafeMemFile resPacket;
        io::writeUInt128(resPacket, keyID);
        resPacket.writeUInt8(load);
        sendPacket(resPacket, KADEMLIA2_PUBLISH_RES, ip, udpPort, senderKey, nullptr);
    } catch (const FileException&) {
        logKad(QStringLiteral("Kad: PUBLISH_SOURCE_REQ from %1:%2 — truncated packet")
                   .arg(ipToString(ip)).arg(udpPort));
    }
}

void KademliaUDPListener::process_KADEMLIA2_PUBLISH_RES(const uint8* data, uint32 len,
                                                         uint32 ip, uint16 udpPort,
                                                         const KadUDPKey& senderKey)
{
    // Only accept a publish response from a node we actually published to.
    // MFC KademliaUDPListener.cpp:1431-1437. This is only correct because
    // sendPacket() now registers all three PUBLISH_*_REQ opcodes on the
    // out-track list — previously nothing tracked them.
    if (!isOnOutTrackList(ip, KADEMLIA2_PUBLISH_KEY_REQ)
        && !isOnOutTrackList(ip, KADEMLIA2_PUBLISH_SOURCE_REQ)
        && !isOnOutTrackList(ip, KADEMLIA2_PUBLISH_NOTES_REQ)) {
        logKad(QStringLiteral("Kad: unrequested PUBLISH_RES from %1 — dropped")
                   .arg(ipToString(ip)));
        return;
    }

    if (len < 17)
        return;

    SafeMemFile io(data, len);
    UInt128 target = io::readUInt128(io);
    uint8 load = io.readUInt8();

    SearchManager::processPublishResult(target, load, true);

    // Check if the remote node requests an ACK
    if (io.position() < io.length()) {
        uint8 options = io.readUInt8();
        bool requestACK = (options & 0x01) != 0;
        if (requestACK && !senderKey.isEmpty()) {
            sendNullPacket(KADEMLIA2_PUBLISH_RES_ACK, ip, udpPort, senderKey, nullptr);
        }
    }
}

void KademliaUDPListener::process_KADEMLIA2_SEARCH_NOTES_REQ(const uint8* data, uint32 len,
                                                               uint32 ip, uint16 udpPort,
                                                               const KadUDPKey& senderKey)
{
    if (len < 32)
        return;

    SafeMemFile io(data, len);
    UInt128 target = io::readUInt128(io);
    uint64 fileSize = (io.position() + 8 <= io.length()) ? io.readUInt64() : 0;

    // Serve note results from local index
    if (auto* indexed = Kademlia::getInstanceIndexed())
        indexed->sendValidNoteResult(target, ip, udpPort, fileSize, senderKey);
}

void KademliaUDPListener::process_KADEMLIA2_PUBLISH_NOTES_REQ(const uint8* data, uint32 len,
                                                                uint32 ip, uint16 udpPort,
                                                                const KadUDPKey& senderKey)
{
    if (len < 32)
        return;

    SafeMemFile io(data, len);
    uint8 load = 0;
    try {
        UInt128 keyID = io::readUInt128(io);

        if (!shouldAcceptPublish(keyID, ip))
            return;

        UInt128 sourceID = io::readUInt128(io);
        TagList tags = io::readKadTagList(io);

        if (auto* indexed = Kademlia::getInstanceIndexed()) {
            auto* entry = new Entry();
            entry->m_keyID = keyID;
            entry->m_sourceID = sourceID;
            entry->m_address = Address::fromHostOrder(ip);
            for (auto& tag : tags)
                entry->addTag(std::move(tag));
            if (!indexed->addNotes(keyID, sourceID, entry, load))
                delete entry;
        }

        // Send publish response with load
        SafeMemFile resPacket;
        io::writeUInt128(resPacket, keyID);
        resPacket.writeUInt8(load);
        sendPacket(resPacket, KADEMLIA2_PUBLISH_RES, ip, udpPort, senderKey, nullptr);
    } catch (const FileException&) {
        logKad(QStringLiteral("Kad: PUBLISH_NOTES_REQ from %1:%2 — truncated packet")
                   .arg(ipToString(ip)).arg(udpPort));
    }
}

// ---------------------------------------------------------------------------
// Process handlers — Firewall
// ---------------------------------------------------------------------------

/// In LAN mode the routing table holds every peer, so we can propagate
/// their ED2K user hash and connect-options to enable encrypted TCP.
/// On the public internet the requesting node is almost never in our
/// routing zone, so this is a no-op there.
void KademliaUDPListener::propagateLanCryptoInfo(UpDownClient* client, const Contact* contact)
{
    if (!contact || !(Kademlia::instance() && Kademlia::instance()->isRunningInLANMode()))
        return;
    client->setConnectOptions(contact->connectOptions(), true, false);
    uint8 hashBytes[16];
    contact->clientHash().toByteArray(hashBytes);
    if (!isnulmd4(hashBytes))
        client->setUserHash(hashBytes);
}

/// Kad v1 compat: handles legacy KADEMLIA_FIREWALLED_REQ from older clients.
/// We always send KADEMLIA_FIREWALLED2_REQ for our own firewall checks.
void KademliaUDPListener::process_KADEMLIA_FIREWALLED_REQ(const uint8* data, uint32 len,
                                                           uint32 ip, uint16 udpPort,
                                                           const KadUDPKey& senderKey)
{
    if (len < 2)
        return;

    SafeMemFile io(data, len);
    uint16 tcpPort = io.readUInt16();

    // Respond with their external IP so they can determine their public address
    SafeMemFile resPacket;
    resPacket.writeUInt32(ip);
    sendPacket(resPacket, KADEMLIA_FIREWALLED_RES, ip, udpPort, senderKey, nullptr);

    // Attempt TCP verification: connect to their TCP port to verify it's open.
    // This is best-effort — failure is silently ignored.
    // The probe target's IP in the userId slot (host order, ed2kID false), as MFC builds
    // every Kad-originated client. With 0 there hasLowID() is true for a peer we can plainly
    // reach, and the dial only worked because QueuedFwCheck is separately allowed past
    // tryToConnect()'s Low-ID gate — the same latent shape that broke requestBuddy().
    auto* client = new UpDownClient(tcpPort, ip, 0, 0, nullptr);
    client->setConnectAddress(Address::fromHostOrder(ip));
    client->setKadState(KadState::QueuedFwCheck);
    if (auto* rz = Kademlia::getInstanceRoutingZone())
        propagateLanCryptoInfo(client, rz->getContact(ip, udpPort, false));
    // No dial here: ClientList::processKadList() owns the QueuedFwCheck transition, as MFC's
    // ProcessKadList does (srchybrid/ClientList.cpp:487-490). Dialling inline as well meant
    // one request produced two outgoing connections to the same peer.
    if (theApp.clientList)
        theApp.clientList->addClient(client);

    logKad(QStringLiteral("Kad: FIREWALLED_REQ from %1:%2, responded + TCP fw check")
               .arg(ipToString(ip)).arg(udpPort));
}

void KademliaUDPListener::process_KADEMLIA_FIREWALLED2_REQ(const uint8* data, uint32 len,
                                                            uint32 ip, uint16 udpPort,
                                                            const KadUDPKey& senderKey)
{
    if (len < 19) // 2 (TCP) + 16 (hash) + 1 (options)
        return;

    SafeMemFile io(data, len);
    uint16 tcpPort = io.readUInt16();
    UInt128 senderHash = io::readUInt128(io);  // sender's ED2K user hash for TCP encryption
    uint8 options = io.readUInt8();

    // Respond with their external IP (no crypto target — matches SrcHybrid)
    SafeMemFile resPacket;
    resPacket.writeUInt32(ip);
    sendPacket(resPacket, KADEMLIA_FIREWALLED_RES, ip, udpPort, senderKey, nullptr);

    // Attempt TCP verification: connect to their TCP port to verify it's open
    auto* client = new UpDownClient(tcpPort, ip, 0, 0, nullptr);
    client->setConnectAddress(Address::fromHostOrder(ip));
    client->setKadState(KadState::QueuedFwCheck);
    client->setConnectOptions(options, true, true);

    // Use sender's hash from packet directly for TCP encryption (matches SrcHybrid RequestTCP)
    uint8 hashBytes[16];
    senderHash.toByteArray(hashBytes);
    if (!isnulmd4(hashBytes))
        client->setUserHash(hashBytes);

    // See the note in process_KADEMLIA_FIREWALLED_REQ — processKadList() does the dialling.
    if (theApp.clientList)
        theApp.clientList->addClient(client);

    logKad(QStringLiteral("Kad: FIREWALLED2_REQ from %1:%2, responded + TCP fw check")
               .arg(ipToString(ip)).arg(udpPort));
}

void KademliaUDPListener::process_KADEMLIA_FIREWALLED_RES(const uint8* data, uint32 len,
                                                           uint32 ip,
                                                           const KadUDPKey& /*senderKey*/)
{
    if (len < 4)
        return;

    if (!isOnOutTrackList(ip, KADEMLIA_FIREWALLED_REQ))
        return;

    SafeMemFile io(data, len);
    uint32 externalIP = io.readUInt32();

    // Update our known external IP in KadPrefs
    if (auto* prefs = Kademlia::getInstancePrefs()) {
        if (prefs->ipAddress() != externalIP)
            prefs->setIPAddress(externalIP);
        prefs->incRecheckIP();
    }

    logKad(QStringLiteral("Kad: FIREWALLED_RES from %1 — external IP: %2")
               .arg(ipToString(ip)).arg(ipToString(externalIP)));
}

void KademliaUDPListener::process_KADEMLIA_FIREWALLED_ACK_RES(uint32 /*len*/)
{
    // The remote node successfully TCP-connected to our listen port,
    // confirming we are reachable.  Increment the firewall counter so
    // that KadPrefs::firewalled() eventually returns false.
    if (auto* prefs = Kademlia::getInstancePrefs())
        prefs->incFirewalled();
    logKad(QStringLiteral("Kad: FIREWALLED_ACK_RES received — incremented firewall counter"));
}

// ---------------------------------------------------------------------------
// Process handlers — Buddy
// ---------------------------------------------------------------------------

void KademliaUDPListener::process_KADEMLIA_FINDBUDDY_REQ(const uint8* data, uint32 len,
                                                          uint32 ip, uint16 udpPort,
                                                          const KadUDPKey& senderKey)
{
    // Matches MFC KademliaUDPListener.cpp:1681-1722
    if (len < 34) // 16 (buddyID) + 16 (clientHash) + 2 (tcpPort)
        return;

    auto* prefs = Kademlia::getInstancePrefs();
    auto* clientList = Kademlia::getClientList();
    if (!clientList || !prefs)
        return;

    // Reject if we ourselves are firewalled or UDP-unverified — we can't be a buddy.
    // Matches MFC line 1690.
    if (prefs->firewalled() || UDPFirewallTester::isFirewalledUDP(true)
        || !UDPFirewallTester::isVerified())
    {
        return;
    }

    // Already have a connected buddy — reject.
    // Matches MFC line 1693.
    if (clientList->buddyStatus() == eMule::BuddyStatus::Connected) {
        logKad(QStringLiteral("Kad: FINDBUDDY_REQ from %1:%2 — already have a buddy")
                   .arg(ipToString(ip)).arg(udpPort));
        return;
    }

    SafeMemFile bio(data, len);
    UInt128 buddyID = io::readUInt128(bio);
    UInt128 userID  = io::readUInt128(bio);
    uint16 tcpPort  = bio.readUInt16();

    // Try to accept this as our buddy first (MFC does incomingBuddy before sending RES).
    uint8 clientIDBytes[16];
    userID.toByteArray(clientIDBytes);
    uint8 buddyIDBytes[16];
    buddyID.toByteArray(buddyIDBytes);
    if (!clientList->incomingBuddy(ip, tcpPort, udpPort, clientIDBytes, buddyIDBytes))
        return; // cancelled — don't send a response

    // Send FINDBUDDY_RES back with our info.
    // Matches MFC line 1712-1717.
    SafeMemFile resPacket;
    io::writeUInt128(resPacket, buddyID);
    io::writeUInt128(resPacket, prefs->clientHash());
    resPacket.writeUInt16(thePrefs.port());  // ED2K TCP port
    if (!senderKey.isEmpty()) // connectOptions only sent with verified key (MFC line 1716)
        resPacket.writeUInt8(prefs->myConnectOptions());
    sendPacket(resPacket, KADEMLIA_FINDBUDDY_RES, ip, udpPort, senderKey, nullptr);

    logKad(QStringLiteral("Kad: FINDBUDDY_REQ from %1:%2 — accepted, sent response")
               .arg(ipToString(ip)).arg(udpPort));
}

void KademliaUDPListener::process_KADEMLIA_FINDBUDDY_RES(const uint8* data, uint32 len,
                                                          uint32 ip, uint16 udpPort,
                                                          const KadUDPKey& /*senderKey*/)
{
    // Matches MFC KademliaUDPListener.cpp:1725-1761
    if (len < 34) // 16 (checkID) + 16 (clientHash) + 2 (tcpPort)
        return;

    SafeMemFile bio(data, len);
    UInt128 checkID = io::readUInt128(bio);

    // Verify: checkID XOR ~0 should equal our KadID.
    // The responder echoes back our BuddyID (= ~kadID from the REQ).
    // MFC line 1743: uCheck.Xor(CUInt128(true)) → should == GetKadID()
    auto* prefs = Kademlia::getInstancePrefs();
    if (!prefs)
        return;
    checkID.xorWith(UInt128(true));
    if (checkID != prefs->kadId()) {
        logKad(QStringLiteral("Kad: FINDBUDDY_RES from %1:%2 — check ID mismatch, ignoring")
                   .arg(ipToString(ip)).arg(udpPort));
        return;
    }

    UInt128 clientHash = io::readUInt128(bio);
    uint16 tcpPort = bio.readUInt16();
    uint8 connectOptions = 0;
    if (len > 34)  // 0.49a+ sends connectOptions (MFC line 1749)
        connectOptions = bio.readUInt8();

    auto* clientList = Kademlia::getClientList();
    if (!clientList)
        return;

    // We sent a FindBuddy search and this node responded — try to connect
    // as our buddy via TCP.  Matches MFC line 1759.
    uint8 clientIDBytes[16];
    clientHash.toByteArray(clientIDBytes);
    clientList->requestBuddy(ip, tcpPort, udpPort, clientIDBytes, connectOptions);

    logKad(QStringLiteral("Kad: FINDBUDDY_RES from %1:%2 — requesting buddy connection")
               .arg(ipToString(ip)).arg(udpPort));
}

void KademliaUDPListener::process_KADEMLIA_CALLBACK_REQ(const uint8* data, uint32 len,
                                                         uint32 ip, const KadUDPKey& /*senderKey*/)
{
    // We are a buddy relay node. A firewalled client wants us to relay a
    // callback to our buddy (another firewalled client).
    // Matches MFC KademliaUDPListener.cpp:1764-1797.
    if (len < 34) // 16 (checkID) + 16 (fileID) + 2 (tcpPort)
        return;

    auto* clientList = Kademlia::getClientList();
    if (!clientList)
        return;

    auto* buddy = clientList->getBuddy();
    if (!buddy || clientList->buddyStatus() != eMule::BuddyStatus::Connected) {
        logKad(QStringLiteral("Kad: CALLBACK_REQ from %1 — no connected buddy, ignoring")
                   .arg(ipToString(ip)));
        return;
    }

    if (!buddy->socket() || !buddy->socket()->isConnected()) {
        logKad(QStringLiteral("Kad: CALLBACK_REQ from %1 — buddy has no active socket")
                   .arg(ipToString(ip)));
        return;
    }

    SafeMemFile bio(data, len);
    UInt128 checkID = io::readUInt128(bio);
    UInt128 fileID  = io::readUInt128(bio);
    uint16 tcpPort  = bio.readUInt16();

    // Build OP_CALLBACK TCP packet: original fields + sender's IP and port
    SafeMemFile relayPacket;
    io::writeUInt128(relayPacket, checkID);
    io::writeUInt128(relayPacket, fileID);
    relayPacket.writeUInt32(ip);       // sender's IP
    relayPacket.writeUInt16(tcpPort);  // sender's TCP port

    auto packet = std::make_unique<eMule::Packet>(relayPacket, OP_EMULEPROT, OP_CALLBACK);
    buddy->sendPacket(std::move(packet));

    logKad(QStringLiteral("Kad: CALLBACK_REQ from %1 — relayed to buddy via TCP")
               .arg(ipToString(ip)));
}

// ---------------------------------------------------------------------------
// Process handlers — Ping/Pong
// ---------------------------------------------------------------------------

void KademliaUDPListener::process_KADEMLIA2_PING(uint32 ip, uint16 udpPort,
                                                  const KadUDPKey& senderKey)
{
    // Echo back the sender's UDP port so they can discover their external port.
    // Matches MFC KademliaUDPListener.cpp:1799-1807.
    SafeMemFile packet;
    packet.writeUInt16(udpPort);
    sendPacket(packet, KADEMLIA2_PONG, ip, udpPort, senderKey, nullptr);
}

void KademliaUDPListener::process_KADEMLIA2_PONG(const uint8* data, uint32 len,
                                                  uint32 ip, uint16 /*udpPort*/,
                                                  const KadUDPKey& /*senderKey*/)
{
    if (!isOnOutTrackList(ip, KADEMLIA2_PING))
        return;

    if (len < 2)
        return;

    // Is this the answer to a v7 verification PING? Those are registered with a
    // zero challenge, which isLegacyChallenge() treats as a wildcard for PING.
    // Unlike the KADEMLIA2_RES case we fall through afterwards — the port echo
    // below is still useful. MFC KademliaUDPListener.cpp:1824-1834.
    UInt128 challengeContactID;
    if (isLegacyChallenge(UInt128(), ip, KADEMLIA2_PING, challengeContactID)) {
        if (auto* rz = Kademlia::getInstanceRoutingZone()) {
            if (!rz->verifyContact(challengeContactID, ip)) {
                logKad(QStringLiteral("Kad: PONG: no valid sender in routing table for legacy challenge (%1)")
                           .arg(ipToString(ip)));
            }
        }
    }

    SafeMemFile io(data, len);
    uint16 externalPort = io.readUInt16();

    // Only feed the reported external port into the consensus while we are still
    // trying to discover it, and once recorded, resume the UDP firewall check
    // (which was waiting on the port). MFC KademliaUDPListener.cpp:1833-1841.
    if (auto* prefs = Kademlia::getInstancePrefs()) {
        if (prefs->findExternKadPort(false)) {
            prefs->setExternKadPort(externalPort, ip);
            if (UDPFirewallTester::isFWCheckUDPRunning())
                UDPFirewallTester::queryNextClient();
        }
    }
}

void KademliaUDPListener::process_KADEMLIA2_FIREWALLUDP(const uint8* data, uint32 len,
                                                         uint32 ip, const KadUDPKey& /*senderKey*/)
{
    if (len < 3) // 1 (error code) + 2 (port)
        return;

    SafeMemFile io(data, len);
    uint8 errorCode = io.readUInt8();
    uint16 incomingPort = io.readUInt16();

    // The reported port decides our firewall verdict and which Kad port we then
    // advertise, so it cannot be taken on trust: accept it only if it is one of
    // the two ports we actually listen on. MFC KademliaUDPListener.cpp:1856-1866.
    //
    // Note both rejection paths report the test as *cancelled*, not failed — a
    // result we cannot interpret must not count towards the firewalled tally.
    // Previously a non-zero errorCode was reported as a counted firewalled
    // verdict, which MFC explicitly avoids ("ignoring result").
    auto* prefs = Kademlia::getInstancePrefs();
    const uint16 externPort = prefs ? prefs->externalKadPort() : uint16{0};
    const uint16 internPort = prefs ? prefs->internKadPort() : uint16{0};

    if (incomingPort == 0 || (incomingPort != externPort && incomingPort != internPort)) {
        logKad(QStringLiteral("Kad: FIREWALLUDP from %1 on unexpected incoming port %2 — ignoring result")
                   .arg(ipToString(ip)).arg(incomingPort));
        UDPFirewallTester::setUDPFWCheckResult(false, /*testCancelled=*/true, ip, 0);
    } else if (errorCode == 0) {
        logKad(QStringLiteral("Kad: FIREWALLUDP from %1 — incoming port %2")
                   .arg(ipToString(ip)).arg(incomingPort));
        UDPFirewallTester::setUDPFWCheckResult(true, false, ip, incomingPort);
    } else {
        logKad(QStringLiteral("Kad: FIREWALLUDP from %1 — remote error code %2 — ignoring result")
                   .arg(ipToString(ip)).arg(errorCode));
        UDPFirewallTester::setUDPFWCheckResult(false, /*testCancelled=*/true, ip, 0);
    }
}

} // namespace eMule::kad
