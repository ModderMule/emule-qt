/// @file KadUDPListener.cpp
/// @brief Kademlia UDP packet handler implementation.

#include "kademlia/KadUDPListener.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadEntry.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadIndexed.h"
#include "kademlia/KadMiscUtils.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearch.h"
#include "kademlia/KadSearchManager.h"
#include "client/ClientList.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <QHostInfo>

#include <cstring>
#include <ctime>

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
    // Resolve hostname and bootstrap
    QHostInfo::lookupHost(host, this, [this, udpPort](const QHostInfo& info) {
        if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            logWarning(QStringLiteral("Kad: Failed to resolve bootstrap host: %1")
                           .arg(info.errorString()));
            return;
        }
        uint32 ip = info.addresses().first().toIPv4Address();
        bootstrap(ip, udpPort);
    });
}

void KademliaUDPListener::bootstrap(uint32 ip, uint16 udpPort, uint8 /*kadVersion*/,
                                     const UInt128* cryptTargetID)
{
    SafeMemFile packet;
    sendMyDetails(KADEMLIA2_HELLO_REQ, ip, udpPort, KADEMLIA_VERSION,
                  KadUDPKey(0), cryptTargetID, true);
}

void KademliaUDPListener::firewalledCheck(uint32 ip, uint16 udpPort,
                                           const KadUDPKey& senderKey, uint8 /*kadVersion*/)
{
    sendNullPacket(KADEMLIA_FIREWALLED_REQ, ip, udpPort, senderKey, nullptr);
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

    // Write TCP port from preferences
    uint16 tcpPort = 0;
    if (auto* prefs = Kademlia::getInstancePrefs())
        tcpPort = prefs->internKadPort();
    packet.writeUInt16(tcpPort);

    // Write version
    packet.writeUInt8(KADEMLIA_VERSION);

    // Write tags count (0 for minimal hello)
    packet.writeUInt8(0);

    sendPacket(packet, opcode, ip, udpPort, targetKey, cryptTargetID);
    addTrackedOutPacket(ip, opcode);
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

    sendPacket(packet, KADEMLIA2_PUBLISH_SOURCE_REQ,
               contact->getIPAddress(), contact->getUDPPort(),
               contact->getUDPKey(), nullptr);
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

    uint8 opcode = data[0];
    const uint8* payload = data + 1;
    uint32 payloadLen = len - 1;

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
        process_KADEMLIA2_HELLO_RES_ACK(payload, payloadLen, ip, validReceiverKey);
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
    case KADEMLIA_FIREWALLED_REQ:
        process_KADEMLIA_FIREWALLED_REQ(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA_FIREWALLED2_REQ:
        process_KADEMLIA_FIREWALLED2_REQ(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA_FIREWALLED_RES:
        process_KADEMLIA_FIREWALLED_RES(payload, payloadLen, ip, senderKey);
        break;
    case KADEMLIA_FIREWALLED_ACK_RES:
        process_KADEMLIA_FIREWALLED_ACK_RES(payloadLen);
        break;
    case KADEMLIA_FINDBUDDY_REQ:
        process_KADEMLIA_FINDBUDDY_REQ(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA_FINDBUDDY_RES:
        process_KADEMLIA_FINDBUDDY_RES(payload, payloadLen, ip, udpPort, senderKey);
        break;
    case KADEMLIA_CALLBACK_REQ:
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
        logDebug(QStringLiteral("Kad: Unknown opcode 0x%1 from %2:%3")
                     .arg(opcode, 2, 16, QChar(u'0'))
                     .arg(ipToString(ip)).arg(udpPort));
        break;
    }
}

void KademliaUDPListener::sendPacket(const uint8* data, uint32 len, uint32 destIP,
                                      uint16 destPort, const KadUDPKey& /*targetKey*/,
                                      const UInt128* /*cryptTargetID*/)
{
    QByteArray packetData(reinterpret_cast<const char*>(data), static_cast<qsizetype>(len));
    emit packetToSend(std::move(packetData), destIP, destPort);
}

void KademliaUDPListener::sendPacket(const uint8* data, uint32 len, uint8 opcode,
                                      uint32 destIP, uint16 destPort,
                                      const KadUDPKey& targetKey, const UInt128* cryptTargetID)
{
    // Prepend opcode
    QByteArray fullPacket;
    fullPacket.reserve(static_cast<qsizetype>(len + 1));
    fullPacket.append(static_cast<char>(opcode));
    if (len > 0)
        fullPacket.append(reinterpret_cast<const char*>(data), static_cast<qsizetype>(len));
    emit packetToSend(std::move(fullPacket), destIP, destPort);
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
        if (it->requester == expireImmediately || it->expire < now)
            it = m_fetchNodeIDRequests.erase(it);
        else
            ++it;
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
    if (len < 25) // 16 (ID) + 2 (TCP port) + 1 (version) + minimum
        return false;

    SafeMemFile io(data, len);
    UInt128 contactID = io::readUInt128(io);
    uint16 tcpPort = io.readUInt16();
    uint8 version = io.readUInt8();

    if (outVersion)
        *outVersion = version;
    if (outContactID)
        *outContactID = contactID;

    // Read tags (if any remaining data)
    uint8 tagCount = 0;
    if (io.position() < io.length())
        tagCount = io.readUInt8();

    bool reqACK = false;
    for (uint8 i = 0; i < tagCount; ++i) {
        try {
            Tag tag = io::readKadTag(io);
            // Check for misc options tag
            if (tag.isInt() && tag.nameId() == 0xF7) { // TAG_KADMISCOPTIONS
                reqACK = (tag.intValue() & 0x01) != 0;
            }
        } catch (...) {
            break;
        }
    }
    if (outRequestsACK)
        *outRequestsACK = reqACK;

    // Add to routing table
    if (auto* rz = Kademlia::getInstanceRoutingZone()) {
        if (update)
            rz->addOrUpdateContact(contactID, ip, udpPort, tcpPort, version, udpKey, ipVerified);
    }

    return true;
}

void KademliaUDPListener::sendLegacyChallenge(uint32 ip, uint16 udpPort, const UInt128& contactID)
{
    UInt128 challenge;
    challenge.setValueRandom();
    addLegacyChallenge(contactID, challenge, ip, KADEMLIA2_HELLO_RES);

    SafeMemFile packet;
    io::writeUInt128(packet, challenge);
    sendPacket(packet, KADEMLIA2_PING, ip, udpPort, KadUDPKey(0), nullptr);
}

std::unique_ptr<SearchTerm> KademliaUDPListener::createSearchExpressionTree(SafeMemFile& io, int level)
{
    // Prevent excessive recursion
    if (level > 24)
        return nullptr;

    if (io.position() >= io.length())
        return nullptr;

    uint8 op = io.readUInt8();
    auto term = std::make_unique<SearchTerm>();

    switch (op) {
    case 0x00: // AND
        term->type = SearchTerm::Type::AND;
        term->left = createSearchExpressionTree(io, level + 1);
        term->right = createSearchExpressionTree(io, level + 1);
        if (!term->left || !term->right)
            return nullptr;
        break;
    case 0x01: // OR
        term->type = SearchTerm::Type::OR;
        term->left = createSearchExpressionTree(io, level + 1);
        term->right = createSearchExpressionTree(io, level + 1);
        if (!term->left || !term->right)
            return nullptr;
        break;
    case 0x02: // NOT
        term->type = SearchTerm::Type::NOT;
        term->left = createSearchExpressionTree(io, level + 1);
        term->right = createSearchExpressionTree(io, level + 1);
        if (!term->left || !term->right)
            return nullptr;
        break;
    case 0x03: { // String
        term->type = SearchTerm::Type::String;
        QString str = io::readStringUTF8(io);
        if (!str.isEmpty())
            term->strings.push_back(str.toLower());
        break;
    }
    case 0x04: { // MetaTag (string)
        term->type = SearchTerm::Type::MetaTag;
        QString val = io::readStringUTF8(io);
        uint16 nameLen = io.readUInt16();
        QByteArray name(nameLen, Qt::Uninitialized);
        if (nameLen > 0)
            io.read(name.data(), nameLen);
        term->tag = Tag(std::move(name), val);
        break;
    }
    case 0x05:   // >=
    case 0x06:   // <=
    case 0x07:   // >
    case 0x08:   // <
    case 0x09:   // =
    case 0x0A: { // !=
        uint64 val = io.readUInt64();
        uint16 nameLen = io.readUInt16();
        QByteArray name(nameLen, Qt::Uninitialized);
        if (nameLen > 0)
            io.read(name.data(), nameLen);
        term->tag = Tag(std::move(name), val);
        switch (op) {
        case 0x05: term->type = SearchTerm::Type::OpGreaterEqual; break;
        case 0x06: term->type = SearchTerm::Type::OpLessEqual; break;
        case 0x07: term->type = SearchTerm::Type::OpGreater; break;
        case 0x08: term->type = SearchTerm::Type::OpLess; break;
        case 0x09: term->type = SearchTerm::Type::OpEqual; break;
        case 0x0A: term->type = SearchTerm::Type::OpNotEqual; break;
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
    auto* prefs = Kademlia::getInstancePrefs();
    packet.writeUInt16(prefs ? prefs->internKadPort() : uint16{0});
    packet.writeUInt8(KADEMLIA_VERSION);

    // Write contact list
    packet.writeUInt16(static_cast<uint16>(contacts.size()));
    for (auto* contact : contacts) {
        io::writeUInt128(packet, contact->getClientID());
        packet.writeUInt32(contact->getIPAddress());
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

    logDebug(QStringLiteral("Kad: BOOTSTRAP_RES from %1:%2, %3 bytes")
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
        rz->addOrUpdateContact(bootstrapID, ip, udpPort, bootstrapTCP,
                               bootstrapVersion, senderKey, validReceiverKey);

        // Add all received contacts to routing zone
        for (uint16 i = 0; i < numContacts && io.position() < io.length(); ++i) {
            UInt128 contactID = io::readUInt128(io);
            uint32 contactIP = io.readUInt32();
            uint16 contactUDP = io.readUInt16();
            uint16 contactTCP = io.readUInt16();
            uint8 contactVersion = io.readUInt8();

            rz->add(contactID, contactIP, contactUDP, contactTCP,
                    contactVersion, KadUDPKey(0), false,
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
    bool ipVerified = false;
    bool requestsACK = false;
    UInt128 contactID;

    if (!addContact_KADEMLIA2(data, len, ip, udpPort, &version, senderKey,
                              ipVerified, true, true, &requestsACK, &contactID))
        return;

    // Send hello response
    sendMyDetails(KADEMLIA2_HELLO_RES, ip, udpPort, version,
                  senderKey, &contactID, false);

    if (requestsACK && version >= KADEMLIA_VERSION8_49b) {
        SafeMemFile ackPacket;
        io::writeUInt128(ackPacket, contactID);
        sendPacket(ackPacket, KADEMLIA2_HELLO_RES_ACK, ip, udpPort, senderKey, &contactID);
    }
}

void KademliaUDPListener::process_KADEMLIA2_HELLO_RES(const uint8* data, uint32 len,
                                                       uint32 ip, uint16 udpPort,
                                                       const KadUDPKey& senderKey,
                                                       bool validReceiverKey)
{
    if (!isOnOutTrackList(ip, KADEMLIA2_HELLO_REQ))
        return;

    uint8 version = 0;
    bool ipVerified = false;
    UInt128 contactID;

    addContact_KADEMLIA2(data, len, ip, udpPort, &version, senderKey,
                         ipVerified, true, false, nullptr, &contactID);
}

void KademliaUDPListener::process_KADEMLIA2_HELLO_RES_ACK(const uint8* data, uint32 len,
                                                            uint32 ip, bool /*validReceiverKey*/)
{
    if (len < 16)
        return;

    SafeMemFile io(data, len);
    UInt128 remoteID = io::readUInt128(io);

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
    UInt128 target = io::readUInt128(io);
    UInt128 receiver = io::readUInt128(io);

    // Compute distance for lookup
    UInt128 distance(RoutingZone::localKadId());
    distance.xorWith(target);

    // Get closest contacts
    ContactMap results;
    rz->getClosestTo(type, target, distance, kK * 2, results, true, false);

    // Build response packet
    SafeMemFile resPacket;
    io::writeUInt128(resPacket, target);
    resPacket.writeUInt8(static_cast<uint8>(std::min(results.size(), size_t{kK * 2})));

    uint32 count = 0;
    for (auto& [dist, contact] : results) {
        if (count >= kK * 2)
            break;
        io::writeUInt128(resPacket, contact->getClientID());
        resPacket.writeUInt32(contact->getIPAddress());
        resPacket.writeUInt16(contact->getUDPPort());
        resPacket.writeUInt16(contact->getTCPPort());
        resPacket.writeUInt8(contact->getVersion());
        ++count;
    }

    sendPacket(resPacket, KADEMLIA2_RES, ip, udpPort, senderKey, nullptr);
}

void KademliaUDPListener::process_KADEMLIA2_RES(const uint8* data, uint32 len, uint32 ip,
                                                  uint16 udpPort, const KadUDPKey& senderKey)
{
    if (!isOnOutTrackList(ip, KADEMLIA2_REQ))
        return;

    if (len < 17) // 16 (target) + 1 (count)
        return;

    SafeMemFile io(data, len);
    UInt128 target = io::readUInt128(io);
    uint8 numContacts = io.readUInt8();

    ContactArray results;
    for (uint8 i = 0; i < numContacts && io.position() < io.length(); ++i) {
        UInt128 contactID = io::readUInt128(io);
        uint32 contactIP = io.readUInt32();
        uint16 contactUDP = io.readUInt16();
        uint16 contactTCP = io.readUInt16();
        uint8 contactVersion = io.readUInt8();

        auto* contact = new Contact(contactID, contactIP, contactUDP, contactTCP,
                                     target, contactVersion, KadUDPKey(0), false);
        results.push_back(contact);
    }

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
    uint16 startPos = (io.position() < io.length()) ? io.readUInt16() : 0;

    // Parse search expression if present
    std::unique_ptr<SearchTerm> searchTerms;
    if (io.position() < io.length())
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
                                                        const KadUDPKey& senderKey,
                                                        uint32 ip, uint16 udpPort)
{
    if (len < 17)
        return;

    SafeMemFile io(data, len);
    UInt128 target = io::readUInt128(io);
    uint16 count = io.readUInt16();

    for (uint16 i = 0; i < count && io.position() < io.length(); ++i) {
        UInt128 answer = io::readUInt128(io);
        TagList tags = io::readKadTagList(io);
        SearchManager::processResult(target, answer, tags, ip, udpPort);
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
    uint16 count = io.readUInt16();

    auto* indexed = Kademlia::getInstanceIndexed();
    uint8 totalLoad = 0;

    for (uint16 i = 0; i < count && io.position() < io.length(); ++i) {
        UInt128 sourceID = io::readUInt128(io);
        TagList tags = io::readKadTagList(io);

        if (indexed) {
            auto* entry = new KeyEntry();
            entry->m_keyID = keyID;
            entry->m_sourceID = sourceID;
            entry->m_ip = ip;
            for (auto& tag : tags)
                entry->addTag(std::move(tag));

            uint8 load = 0;
            if (!indexed->addKeyword(keyID, sourceID, entry, load))
                delete entry;
            totalLoad = std::max(totalLoad, load);
        }
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
    UInt128 keyID = io::readUInt128(io);
    UInt128 sourceID = io::readUInt128(io);
    TagList tags = io::readKadTagList(io);

    uint8 load = 0;
    if (auto* indexed = Kademlia::getInstanceIndexed()) {
        auto* entry = new Entry();
        entry->m_keyID = keyID;
        entry->m_sourceID = sourceID;
        entry->m_ip = ip;
        for (auto& tag : tags)
            entry->addTag(std::move(tag));
        if (!indexed->addSources(keyID, sourceID, entry, load))
            delete entry;
    }

    // Send publish response with load
    SafeMemFile resPacket;
    io::writeUInt128(resPacket, keyID);
    resPacket.writeUInt8(load);
    sendPacket(resPacket, KADEMLIA2_PUBLISH_RES, ip, udpPort, senderKey, nullptr);
}

void KademliaUDPListener::process_KADEMLIA2_PUBLISH_RES(const uint8* data, uint32 len,
                                                         uint32 ip, uint16 udpPort,
                                                         const KadUDPKey& senderKey)
{
    if (len < 17)
        return;

    SafeMemFile io(data, len);
    UInt128 target = io::readUInt128(io);
    uint8 load = io.readUInt8();

    SearchManager::processPublishResult(target, load, true);
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
    UInt128 keyID = io::readUInt128(io);
    UInt128 sourceID = io::readUInt128(io);
    TagList tags = io::readKadTagList(io);

    uint8 load = 0;
    if (auto* indexed = Kademlia::getInstanceIndexed()) {
        auto* entry = new Entry();
        entry->m_keyID = keyID;
        entry->m_sourceID = sourceID;
        entry->m_ip = ip;
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
}

// ---------------------------------------------------------------------------
// Process handlers — Firewall
// ---------------------------------------------------------------------------

void KademliaUDPListener::process_KADEMLIA_FIREWALLED_REQ(const uint8* data, uint32 len,
                                                           uint32 ip, uint16 udpPort,
                                                           const KadUDPKey& senderKey)
{
    if (len < 2)
        return;

    SafeMemFile io(data, len);
    [[maybe_unused]] uint16 tcpPort = io.readUInt16();

    // Respond with their external IP so they can determine their public address.
    // Full TCP verification (connecting to their TCP port) requires outbound TCP
    // socket support, which is deferred. The IP response is still useful.
    SafeMemFile resPacket;
    resPacket.writeUInt32(ip);
    sendPacket(resPacket, KADEMLIA_FIREWALLED_RES, ip, udpPort, senderKey, nullptr);

    logDebug(QStringLiteral("Kad: FIREWALLED_REQ from %1:%2, responded with their IP")
                 .arg(ipToString(ip)).arg(udpPort));
}

void KademliaUDPListener::process_KADEMLIA_FIREWALLED2_REQ(const uint8* data, uint32 len,
                                                            uint32 ip, uint16 udpPort,
                                                            const KadUDPKey& senderKey)
{
    if (len < 19) // 2 (TCP) + 16 (ID) + 1 (options)
        return;

    SafeMemFile io(data, len);
    [[maybe_unused]] uint16 tcpPort = io.readUInt16();
    UInt128 contactID = io::readUInt128(io);
    [[maybe_unused]] uint8 options = io.readUInt8();

    // Respond with their external IP (same as FIREWALLED_REQ).
    // Full TCP verification is deferred.
    SafeMemFile resPacket;
    resPacket.writeUInt32(ip);
    sendPacket(resPacket, KADEMLIA_FIREWALLED_RES, ip, udpPort, senderKey, &contactID);

    logDebug(QStringLiteral("Kad: FIREWALLED2_REQ from %1:%2, responded with their IP")
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

    logDebug(QStringLiteral("Kad: FIREWALLED_RES from %1 — external IP: %2")
                 .arg(ipToString(ip)).arg(ipToString(externalIP)));
}

void KademliaUDPListener::process_KADEMLIA_FIREWALLED_ACK_RES(uint32 len)
{
    // No payload expected
    logDebug(QStringLiteral("Kad: FIREWALLED_ACK_RES received"));
}

// ---------------------------------------------------------------------------
// Process handlers — Buddy
// ---------------------------------------------------------------------------

void KademliaUDPListener::process_KADEMLIA_FINDBUDDY_REQ(const uint8* data, uint32 len,
                                                          uint32 ip, uint16 udpPort,
                                                          const KadUDPKey& senderKey)
{
    if (len < 34) // 16 + 16 + 2
        return;

    SafeMemFile io(data, len);
    [[maybe_unused]] UInt128 checkID = io::readUInt128(io);
    UInt128 contactID = io::readUInt128(io);
    uint16 tcpPort = io.readUInt16();

    auto* prefs = Kademlia::getInstancePrefs();
    auto* clientList = Kademlia::getClientList();
    if (!clientList || !prefs)
        return;

    // Accept the buddy request if we don't already have a buddy
    if (clientList->buddyStatus() == eMule::BuddyStatus::Connected) {
        logDebug(QStringLiteral("Kad: FINDBUDDY_REQ from %1:%2 — already have a buddy")
                     .arg(ipToString(ip)).arg(udpPort));
        return;
    }

    // Send FINDBUDDY_RES back with our info
    SafeMemFile resPacket;
    io::writeUInt128(resPacket, prefs->kadId());
    io::writeUInt128(resPacket, contactID);
    resPacket.writeUInt16(prefs->internKadPort());
    sendPacket(resPacket, KADEMLIA_FINDBUDDY_RES, ip, udpPort, senderKey, nullptr);

    // Attempt to accept this as our buddy
    uint8 buddyIDBytes[16];
    contactID.toByteArray(buddyIDBytes);
    clientList->incomingBuddy(ip, tcpPort, buddyIDBytes);

    logDebug(QStringLiteral("Kad: FINDBUDDY_REQ from %1:%2 — accepted, sent response")
                 .arg(ipToString(ip)).arg(udpPort));
}

void KademliaUDPListener::process_KADEMLIA_FINDBUDDY_RES(const uint8* data, uint32 len,
                                                          uint32 ip, uint16 udpPort,
                                                          const KadUDPKey& senderKey)
{
    if (len < 34)
        return;

    SafeMemFile io(data, len);
    [[maybe_unused]] UInt128 checkID = io::readUInt128(io);
    [[maybe_unused]] UInt128 contactID = io::readUInt128(io);
    uint16 tcpPort = io.readUInt16();

    auto* clientList = Kademlia::getClientList();
    if (!clientList)
        return;

    // We sent a FindBuddy search and this node responded — try to connect
    // as our buddy via TCP. For now, initiate the buddy request.
    auto* prefs = Kademlia::getInstancePrefs();
    uint8 connectOptions = prefs ? prefs->myConnectOptions() : 0;
    clientList->requestBuddy(ip, tcpPort, connectOptions);

    logDebug(QStringLiteral("Kad: FINDBUDDY_RES from %1:%2 — requesting buddy connection")
                 .arg(ipToString(ip)).arg(udpPort));
}

void KademliaUDPListener::process_KADEMLIA_CALLBACK_REQ(const uint8* data, uint32 len,
                                                         uint32 ip, const KadUDPKey& senderKey)
{
    if (len < 34)
        return;

    SafeMemFile io(data, len);
    [[maybe_unused]] UInt128 checkID = io::readUInt128(io);
    UInt128 contactID = io::readUInt128(io);
    uint16 tcpPort = io.readUInt16();

    auto* clientList = Kademlia::getClientList();
    if (!clientList)
        return;

    // A callback request is sent through our buddy to reach a firewalled client.
    // We need to have a buddy and the request must come from our buddy's IP.
    auto* buddy = clientList->getBuddy();
    if (!buddy || clientList->buddyStatus() != eMule::BuddyStatus::Connected) {
        logDebug(QStringLiteral("Kad: CALLBACK_REQ from %1 — no buddy, ignoring")
                     .arg(ipToString(ip)));
        return;
    }

    // Relay: create a connection to the firewalled client at the given IP:port.
    // Full implementation requires TCP client connection support.
    logDebug(QStringLiteral("Kad: CALLBACK_REQ from %1 for %2:%3 — relay deferred (needs TCP)")
                 .arg(ipToString(ip)).arg(ipToString(contactID.get32BitChunk(0))).arg(tcpPort));
}

// ---------------------------------------------------------------------------
// Process handlers — Ping/Pong
// ---------------------------------------------------------------------------

void KademliaUDPListener::process_KADEMLIA2_PING(uint32 ip, uint16 udpPort,
                                                  const KadUDPKey& senderKey)
{
    // Respond with pong + our external UDP port
    SafeMemFile packet;
    uint16 externPort = 0;
    if (auto* prefs = Kademlia::getInstancePrefs()) {
        if (prefs->useExternKadPort())
            externPort = prefs->externalKadPort();
        else
            externPort = prefs->internKadPort();
    }
    packet.writeUInt16(externPort);
    sendPacket(packet, KADEMLIA2_PONG, ip, udpPort, senderKey, nullptr);
}

void KademliaUDPListener::process_KADEMLIA2_PONG(const uint8* data, uint32 len,
                                                  uint32 ip, uint16 udpPort,
                                                  const KadUDPKey& senderKey)
{
    if (!isOnOutTrackList(ip, KADEMLIA2_PING))
        return;

    if (len < 2)
        return;

    SafeMemFile io(data, len);
    uint16 externalPort = io.readUInt16();

    // Feed external port to preferences for consensus
    if (auto* prefs = Kademlia::getInstancePrefs())
        prefs->setExternKadPort(externalPort, ip);
}

void KademliaUDPListener::process_KADEMLIA2_FIREWALLUDP(const uint8* data, uint32 len,
                                                         uint32 ip, const KadUDPKey& senderKey)
{
    if (len < 3) // 1 (error code) + 2 (port)
        return;

    SafeMemFile io(data, len);
    uint8 errorCode = io.readUInt8();
    uint16 incomingPort = io.readUInt16();

    UDPFirewallTester::setUDPFWCheckResult(errorCode == 0, false, ip, incomingPort);
}

} // namespace eMule::kad
