#pragma once

/// @file KadUDPListener.h
/// @brief Kademlia UDP packet handler (ported from kademlia/net/KademliaUDPListener.h).
///
/// Handles all Kad UDP protocol messages. Uses a packetToSend signal for
/// decoupling from the actual UDP socket implementation.

#include "kademlia/KadPacketTracking.h"
#include "kademlia/KadSearchDefs.h"
#include "kademlia/KadTypes.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUInt128.h"
#include "utils/SafeFile.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <cstdint>
#include <list>
#include <memory>

namespace eMule::kad {

class Contact;
class KadClientSearcher;

/// Handles all Kademlia UDP protocol messages.
class KademliaUDPListener : public QObject, public PacketTracking {
    Q_OBJECT

public:
    explicit KademliaUDPListener(QObject* parent = nullptr);
    ~KademliaUDPListener() override;

    void bootstrap(const QString& host, uint16 udpPort);
    void bootstrap(uint32 ip, uint16 udpPort, uint8 kadVersion = 0,
                   const UInt128* cryptTargetID = nullptr);
    void firewalledCheck(uint32 ip, uint16 udpPort, const KadUDPKey& senderKey, uint8 kadVersion);
    void sendMyDetails(uint8 opcode, uint32 ip, uint16 udpPort, uint8 kadVersion,
                       const KadUDPKey& targetKey, const UInt128* cryptTargetID, bool requestAck);
    void sendPublishSourcePacket(Contact* contact, const UInt128& targetID,
                                 const UInt128& contactID, const TagList& tags);
    void sendNullPacket(uint8 opcode, uint32 ip, uint16 udpPort,
                        const KadUDPKey& targetKey, const UInt128* cryptTargetID);
    void processPacket(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                       bool validReceiverKey, const KadUDPKey& senderKey);
    void sendPacket(const uint8* data, uint32 len, uint32 destIP, uint16 destPort,
                    const KadUDPKey& targetKey, const UInt128* cryptTargetID);
    void sendPacket(const uint8* data, uint32 len, uint8 opcode, uint32 destIP,
                    uint16 destPort, const KadUDPKey& targetKey, const UInt128* cryptTargetID);
    void sendPacket(SafeMemFile& data, uint8 opcode, uint32 destIP, uint16 destPort,
                    const KadUDPKey& targetKey, const UInt128* cryptTargetID);

    bool findNodeIDByIP(KadClientSearcher* requester, uint32 ip, uint16 tcpPort, uint16 udpPort);
    void expireClientSearch(const KadClientSearcher* expireImmediately = nullptr);

signals:
    void packetToSend(QByteArray data, uint32 destIP, uint16 destPort,
                      KadUDPKey targetKey, UInt128 cryptTargetID);

private:
    // Packet handlers
    bool addContact_KADEMLIA2(const uint8* data, uint32 len, uint32 ip, uint16& udpPort,
                              uint8* outVersion, const KadUDPKey& udpKey, bool& ipVerified,
                              bool update, bool fromHelloReq, bool* outRequestsACK,
                              UInt128* outContactID);
    void sendLegacyChallenge(uint32 ip, uint16 udpPort, const UInt128& contactID);
    static std::unique_ptr<SearchTerm> createSearchExpressionTree(SafeMemFile& io, int level);

    void process_KADEMLIA2_BOOTSTRAP_REQ(uint32 ip, uint16 udpPort, const KadUDPKey& senderKey);
    void process_KADEMLIA2_BOOTSTRAP_RES(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                          const KadUDPKey& senderKey, bool validReceiverKey);
    void process_KADEMLIA2_HELLO_REQ(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                      const KadUDPKey& senderKey, bool validReceiverKey);
    void process_KADEMLIA2_HELLO_RES(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                      const KadUDPKey& senderKey, bool validReceiverKey);
    void process_KADEMLIA2_HELLO_RES_ACK(const uint8* data, uint32 len, uint32 ip, bool validReceiverKey);
    void process_KADEMLIA2_REQ(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                const KadUDPKey& senderKey);
    void process_KADEMLIA2_RES(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                const KadUDPKey& senderKey);
    void process_KADEMLIA2_SEARCH_KEY_REQ(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                           const KadUDPKey& senderKey);
    void process_KADEMLIA2_SEARCH_SOURCE_REQ(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                              const KadUDPKey& senderKey);
    void process_KADEMLIA2_SEARCH_RES(const uint8* data, uint32 len, const KadUDPKey& senderKey,
                                       uint32 ip, uint16 udpPort);
    void process_KADEMLIA2_PUBLISH_KEY_REQ(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                            const KadUDPKey& senderKey);
    void process_KADEMLIA2_PUBLISH_SOURCE_REQ(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                               const KadUDPKey& senderKey);
    void process_KADEMLIA2_PUBLISH_RES(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                        const KadUDPKey& senderKey);
    void process_KADEMLIA2_SEARCH_NOTES_REQ(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                             const KadUDPKey& senderKey);
    void process_KADEMLIA2_PUBLISH_NOTES_REQ(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                              const KadUDPKey& senderKey);
    void process_KADEMLIA_FIREWALLED_REQ(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                          const KadUDPKey& senderKey);
    void process_KADEMLIA_FIREWALLED2_REQ(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                           const KadUDPKey& senderKey);
    void process_KADEMLIA_FIREWALLED_RES(const uint8* data, uint32 len, uint32 ip,
                                          const KadUDPKey& senderKey);
    void process_KADEMLIA_FIREWALLED_ACK_RES(uint32 len);
    void process_KADEMLIA_FINDBUDDY_REQ(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                         const KadUDPKey& senderKey);
    void process_KADEMLIA_FINDBUDDY_RES(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                         const KadUDPKey& senderKey);
    void process_KADEMLIA_CALLBACK_REQ(const uint8* data, uint32 len, uint32 ip,
                                        const KadUDPKey& senderKey);
    void process_KADEMLIA2_PING(uint32 ip, uint16 udpPort, const KadUDPKey& senderKey);
    void process_KADEMLIA2_PONG(const uint8* data, uint32 len, uint32 ip, uint16 udpPort,
                                 const KadUDPKey& senderKey);
    void process_KADEMLIA2_FIREWALLUDP(const uint8* data, uint32 len, uint32 ip,
                                        const KadUDPKey& senderKey);

    struct FetchNodeIDRequest {
        uint32 ip = 0;
        uint32 tcpPort = 0;
        uint32 expire = 0;
        KadClientSearcher* requester = nullptr;
    };
    std::list<FetchNodeIDRequest> m_fetchNodeIDRequests;
};

} // namespace eMule::kad
