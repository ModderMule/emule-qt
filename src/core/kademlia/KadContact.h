#pragma once

/// @file KadContact.h
/// @brief DHT node contact (ported from kademlia/routing/Contact.h).
///
/// GUI callbacks (ContactRef/ContactRem) have been removed.
/// Distance computation requires an explicit localKadId parameter.

#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUInt128.h"
#include "net/Address.h"
#include "utils/Types.h"

#include <cstdint>
#include <ctime>

namespace eMule::kad {

/// A Kademlia DHT contact (peer node).
class Contact {
public:
    ~Contact();
    Contact();

    /// Construct with a known local Kad ID (distance = localKadId XOR clientId).
    Contact(const UInt128& clientId, uint32 ip, uint16 udpPort, uint16 tcpPort,
            uint8 version, const KadUDPKey& udpKey, bool ipVerified,
            const UInt128& localKadId);

    /// Construct with an explicit distance target (distance = target XOR clientId).
    Contact(const UInt128& clientId, uint32 ip, uint16 udpPort, uint16 tcpPort,
            const UInt128& target, uint8 version, const KadUDPKey& udpKey,
            bool ipVerified);

    Contact(const Contact& other);
    Contact& operator=(const Contact& other);

    // -- ID / distance -------------------------------------------------------
    [[nodiscard]] UInt128  getClientID() const       { return m_clientId; }
    [[nodiscard]] UInt128  getDistance() const        { return m_distance; }
    void setClientID(const UInt128& clientId, const UInt128& localKadId); // different from userHash (UpDownClient)

    // -- Network -------------------------------------------------------------
    [[nodiscard]] const Address& address() const     { return m_address; }
    void setAddress(const Address& addr);

    [[nodiscard]] uint16   getTCPPort() const        { return m_tcpPort; }
    void setTCPPort(uint16 port)                     { m_tcpPort = port; }

    [[nodiscard]] uint16   getUDPPort() const        { return m_udpPort; }
    void setUDPPort(uint16 port)                     { m_udpPort = port; }

    // -- Type management -----------------------------------------------------
    [[nodiscard]] uint8    getType() const           { return m_type; }
    void updateType();
    void checkingType();
    void expire();

    // -- Reference counting --------------------------------------------------
    [[nodiscard]] bool     inUse() const             { return m_inUse > 0; }
    [[nodiscard]] uint32   useCount() const          { return m_inUse; }
    void incUse()                                    { ++m_inUse; }
    void decUse();

    /// Number of Contact objects currently alive. Used by tests to assert that
    /// searches free every contact they were handed (see KadSearch's m_deleteList).
    [[nodiscard]] static uint64 liveInstanceCount();

    // -- Version / timing ----------------------------------------------------
    [[nodiscard]] uint8    getVersion() const        { return m_version; }
    void setVersion(uint8 version)                   { m_version = version; }

    [[nodiscard]] time_t   getCreatedTime() const    { return m_created; }
    [[nodiscard]] time_t   getExpireTime() const     { return m_expires; }
    void                   setExpireTime(time_t t)   { m_expires = t; }
    [[nodiscard]] time_t   getLastTypeSet() const    { return m_lastTypeSet; }
    [[nodiscard]] time_t   getLastSeen() const;

    // -- Hello packet --------------------------------------------------------
    [[nodiscard]] bool     getReceivedHelloPacket() const { return m_receivedHelloPacket; }
    void setReceivedHelloPacket()                    { m_receivedHelloPacket = true; }

    // -- UDP key -------------------------------------------------------------
    [[nodiscard]] KadUDPKey getUDPKey() const        { return m_udpKey; }
    void setUDPKey(const KadUDPKey& key)             { m_udpKey = key; }

    // -- IP verification -----------------------------------------------------
    [[nodiscard]] bool     isIpVerified() const      { return m_ipVerified; }
    void setIpVerified(bool verified)                { m_ipVerified = verified; }

    // -- Crypto / connect options --------------------------------------------
    [[nodiscard]] uint8    connectOptions() const     { return m_connectOptions; }
    void setConnectOptions(uint8 opts)                { m_connectOptions = opts; }

    [[nodiscard]] UInt128  clientHash() const          { return m_clientHash; }
    void setClientHash(const UInt128& hash)            { m_clientHash = hash; }

    // -- Bootstrap -----------------------------------------------------------
    [[nodiscard]] bool     isBootstrapContact() const { return m_bootstrapContact; }
    void setBootstrapContact()                       { m_bootstrapContact = true; }

private:
    void initContact();
    void copy(const Contact& from);

    UInt128    m_clientId;
    UInt128    m_distance;
    KadUDPKey  m_udpKey;
    time_t     m_lastTypeSet = 0;
    time_t     m_expires     = 0;
    time_t     m_created     = 0;
    uint32     m_inUse       = 0;
    Address    m_address;
    uint16     m_tcpPort     = 0;
    uint16     m_udpPort     = 0;
    UInt128    m_clientHash;
    uint8      m_version     = 0;
    uint8      m_type        = 3;
    uint8      m_connectOptions = 0;
    bool       m_ipVerified  = false;
    bool       m_receivedHelloPacket = false;
    bool       m_bootstrapContact   = false;
};

} // namespace eMule::kad
