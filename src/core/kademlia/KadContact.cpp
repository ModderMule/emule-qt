#include "pch.h"
/// @file KadContact.cpp
/// @brief DHT node contact implementation.

#include "kademlia/KadContact.h"
#include "utils/Opcodes.h"

#include <ctime>

#include "utils/ByteOrder.h"

namespace eMule::kad {

// ---------------------------------------------------------------------------
// Constructors
// ---------------------------------------------------------------------------

Contact::Contact()
{
    initContact();
}

Contact::Contact(const UInt128& clientId, uint32 ip, uint16 udpPort, uint16 tcpPort,
                 uint8 version, const KadUDPKey& udpKey, bool ipVerified,
                 const UInt128& localKadId)
    : m_clientId(clientId)
    , m_udpKey(udpKey)
    , m_ip(ip)
    , m_netIp(htonl(ip))
    , m_tcpPort(tcpPort)
    , m_udpPort(udpPort)
    , m_version(version)
    , m_type(3)
    , m_ipVerified(ipVerified)
{
    m_distance.setValue(localKadId);
    m_distance.xorWith(clientId);
    initContact();
}

Contact::Contact(const UInt128& clientId, uint32 ip, uint16 udpPort, uint16 tcpPort,
                 const UInt128& target, uint8 version, const KadUDPKey& udpKey,
                 bool ipVerified)
    : m_clientId(clientId)
    , m_udpKey(udpKey)
    , m_ip(ip)
    , m_netIp(htonl(ip))
    , m_tcpPort(tcpPort)
    , m_udpPort(udpPort)
    , m_version(version)
    , m_type(3)
    , m_ipVerified(ipVerified)
{
    m_distance.setValue(target);
    m_distance.xorWith(clientId);
    initContact();
}

Contact::Contact(const Contact& other)
{
    copy(other);
}

Contact& Contact::operator=(const Contact& other)
{
    if (this != &other)
        copy(other);
    return *this;
}

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

void Contact::setClientID(const UInt128& clientId, const UInt128& localKadId)
{
    m_clientId = clientId;
    m_distance.setValue(localKadId);
    m_distance.xorWith(clientId);
}

void Contact::setIPAddress(uint32 ip)
{
    if (m_ip != ip) {
        setIpVerified(false);
        m_ip = ip;
        m_netIp = htonl(m_ip);
    }
}

void Contact::updateType()
{
    time_t now = time(nullptr);
    switch ((now - m_created) / HR2S(1)) {
    case 0:
        m_type = 2;
        m_expires = now + HR2S(1);
        break;
    case 1:
        m_type = 1;
        m_expires = now + static_cast<time_t>(HR2S(1.5));
        break;
    default:
        m_type = 0;
        m_expires = now + HR2S(2);
        break;
    }
}

void Contact::checkingType()
{
    if (time(nullptr) - m_lastTypeSet >= 10 && m_type < 4) {
        m_lastTypeSet = time(nullptr);
        m_expires = m_lastTypeSet + MIN2S(2);
        ++m_type;
    }
}

time_t Contact::getLastSeen() const
{
    if (m_expires > 0) {
        switch (m_type) {
        case 2:
            return m_expires - HR2S(1);
        case 1:
            return m_expires - static_cast<time_t>(HR2S(1.5));
        case 0:
            return m_expires - HR2S(2);
        }
    }
    return 0;
}

void Contact::expire()
{
    m_type = 4;
    m_expires = 1;
}

void Contact::decUse()
{
    if (m_inUse)
        --m_inUse;
    else
        Q_ASSERT(false);
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

void Contact::initContact()
{
    m_created = m_lastTypeSet = time(nullptr);
}

void Contact::copy(const Contact& from)
{
    m_clientId           = from.m_clientId;
    m_distance           = from.m_distance;
    m_udpKey             = from.m_udpKey;
    m_lastTypeSet        = from.m_lastTypeSet;
    m_expires            = from.m_expires;
    m_created            = from.m_created;
    m_inUse              = from.m_inUse;
    m_ip                 = from.m_ip;
    m_netIp              = from.m_netIp;
    m_tcpPort            = from.m_tcpPort;
    m_udpPort            = from.m_udpPort;
    m_version            = from.m_version;
    m_type               = from.m_type;
    m_ipVerified         = from.m_ipVerified;
    m_receivedHelloPacket = from.m_receivedHelloPacket;
    m_bootstrapContact   = from.m_bootstrapContact;
}

} // namespace eMule::kad
