/// @file KadPrefs.cpp
/// @brief Kademlia runtime preferences implementation.

#include "kademlia/KadPrefs.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadRoutingZone.h"
#include "client/ClientList.h"
#include "crypto/MD5Hash.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <cstring>
#include <ctime>

namespace eMule::kad {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

KadPrefs::KadPrefs(const QString& configDir)
{
    m_filename = configDir + QStringLiteral("/preferencesKad.dat");

    // Generate random KadID
    m_clientId.setValueRandom();

    // Init client hash from global preferences user hash
    auto userHash = thePrefs.userHash();
    m_clientHash.setValueBE(userHash.data());

    m_lastContact = 0;

    readFile();
}

KadPrefs::~KadPrefs()
{
    writeFile();
}

// ---------------------------------------------------------------------------
// Public methods — Identity
// ---------------------------------------------------------------------------

UInt128 KadPrefs::kadId() const
{
    return m_clientId;
}

void KadPrefs::setKadId(const UInt128& id)
{
    m_clientId = id;
}

UInt128 KadPrefs::clientHash() const
{
    return m_clientHash;
}

void KadPrefs::setClientHash(const UInt128& hash)
{
    m_clientHash = hash;
}

// ---------------------------------------------------------------------------
// Public methods — Network
// ---------------------------------------------------------------------------

uint32 KadPrefs::ipAddress() const
{
    return m_ip;
}

void KadPrefs::setIPAddress(uint32 ip)
{
    // Two-step verification: IP must match twice before being set
    if (ip == 0 || m_ipLast == 0) {
        m_ipLast = ip;
    }
    if (ip == m_ipLast) {
        m_ip = ip;
    } else {
        m_ipLast = ip;
    }
}

// ---------------------------------------------------------------------------
// Public methods — Recheck IP
// ---------------------------------------------------------------------------

bool KadPrefs::recheckIP() const
{
    return m_recheckIp < KADEMLIAFIREWALLCHECKS;
}

void KadPrefs::setRecheckIP()
{
    m_recheckIp = 0;
    m_firewallCounter = 0;
}

void KadPrefs::incRecheckIP()
{
    ++m_recheckIp;
}

// ---------------------------------------------------------------------------
// Public methods — Connection
// ---------------------------------------------------------------------------

bool KadPrefs::hasHadContact() const
{
    if (m_lastContact != 0)
        return (time(nullptr) - m_lastContact) < KADEMLIADISCONNECTDELAY;
    return false;
}

bool KadPrefs::hasLostConnection() const
{
    if (m_lastContact != 0)
        return !hasHadContact();
    return false;
}

void KadPrefs::setLastContact()
{
    m_lastContact = time(nullptr);
}

time_t KadPrefs::lastContact() const
{
    return m_lastContact;
}

// ---------------------------------------------------------------------------
// Public methods — Firewall
// ---------------------------------------------------------------------------

bool KadPrefs::firewalled() const
{
    if (m_firewallCounter < 2) {
        // Not enough checks yet — use recheck + last known state
        if (!recheckIP())
            return m_lastFirewallState;
        return true;  // Assume firewalled during recheck
    }
    return false;
}

void KadPrefs::setFirewalled()
{
    // Snapshot current state and reset counter
    m_lastFirewallState = firewalled();
    m_firewallCounter = 0;
}

void KadPrefs::incFirewalled()
{
    ++m_firewallCounter;
}

// ---------------------------------------------------------------------------
// Public methods — Storage limits
// ---------------------------------------------------------------------------

uint8 KadPrefs::totalFile() const { return m_totalFile; }
void KadPrefs::setTotalFile(uint8 val) { m_totalFile = val; }

uint8 KadPrefs::totalStoreSrc() const { return m_totalStoreSrc; }
void KadPrefs::setTotalStoreSrc(uint8 val) { m_totalStoreSrc = val; }

uint8 KadPrefs::totalStoreKey() const { return m_totalStoreKey; }
void KadPrefs::setTotalStoreKey(uint8 val) { m_totalStoreKey = val; }

uint8 KadPrefs::totalSource() const { return m_totalSource; }
void KadPrefs::setTotalSource(uint8 val) { m_totalSource = val; }

uint8 KadPrefs::totalNotes() const { return m_totalNotes; }
void KadPrefs::setTotalNotes(uint8 val) { m_totalNotes = val; }

uint8 KadPrefs::totalStoreNotes() const { return m_totalStoreNotes; }
void KadPrefs::setTotalStoreNotes(uint8 val) { m_totalStoreNotes = val; }

// ---------------------------------------------------------------------------
// Public methods — Stats
// ---------------------------------------------------------------------------

uint32 KadPrefs::kademliaUsers() const
{
    return m_kademliaUsers;
}

void KadPrefs::setKademliaUsers(uint32 val)
{
    m_kademliaUsers = val;
}

uint32 KadPrefs::kademliaFiles() const
{
    return m_kademliaFiles;
}

void KadPrefs::setKademliaFiles(uint32 averageFileCount)
{
    m_kademliaFiles = std::max(averageFileCount, uint32{108}) * m_kademliaUsers;
}

// ---------------------------------------------------------------------------
// Public methods — Publish / buddy
// ---------------------------------------------------------------------------

bool KadPrefs::publish() const
{
    return m_publish;
}

void KadPrefs::setPublish(bool val)
{
    m_publish = val;
}

bool KadPrefs::findBuddy()
{
    if (m_findBuddy) {
        m_findBuddy = false;
        return true;
    }
    return false;
}

void KadPrefs::setFindBuddy(bool val)
{
    m_findBuddy = val;
}

// ---------------------------------------------------------------------------
// Public methods — External port
// ---------------------------------------------------------------------------

bool KadPrefs::useExternKadPort() const
{
    if (Kademlia::instance() && Kademlia::instance()->isRunningInLANMode())
        return false;
    return m_useExternKadPort && m_externKadPort != 0;
}

void KadPrefs::setUseExternKadPort(bool val)
{
    m_useExternKadPort = val;
}

uint16 KadPrefs::externalKadPort() const
{
    return m_externKadPort;
}

void KadPrefs::setExternKadPort(uint16 port, uint32 fromIP)
{
    // Consensus check: need 2 of kExternalPortAskIPs (3) agreeing on same port
    // from different IPs
    for (size_t i = 0; i < m_externPortIPs.size(); ++i) {
        if (m_externPortIPs[i] == fromIP) {
            // Already have a response from this IP — update it
            m_externPorts[i] = port;
            return;
        }
    }

    m_externPortIPs.push_back(fromIP);
    m_externPorts.push_back(port);

    // Check for consensus (2 of 3 agreeing)
    if (m_externPorts.size() >= 2) {
        for (size_t i = 0; i < m_externPorts.size(); ++i) {
            uint32 count = 0;
            for (size_t j = 0; j < m_externPorts.size(); ++j) {
                if (m_externPorts[j] == m_externPorts[i])
                    ++count;
            }
            if (count >= 2) {
                m_externKadPort = m_externPorts[i];
                return;
            }
        }
    }
}

bool KadPrefs::findExternKadPort(bool reset)
{
    if (reset) {
        m_externPortIPs.clear();
        m_externPorts.clear();
    }
    return m_externPortIPs.size() < kExternalPortAskIPs;
}

uint16 KadPrefs::internKadPort() const
{
    return thePrefs.udpPort();
}

// ---------------------------------------------------------------------------
// Public methods — Connect options
// ---------------------------------------------------------------------------

uint8 KadPrefs::myConnectOptions() const
{
    // Bit 0: TCP open (not firewalled)
    // Bit 1: UDP open (not UDP firewalled)
    // Bit 2: has buddy
    uint8 options = 0;
    if (!firewalled())
        options |= 0x01;
    if (!UDPFirewallTester::isFirewalledUDP(true))
        options |= 0x02;
    if (auto* clientList = Kademlia::getClientList()) {
        if (clientList->buddyStatus() == eMule::BuddyStatus::Connected)
            options |= 0x04;
    }
    return options;
}

// ---------------------------------------------------------------------------
// Public methods — Firewall stats
// ---------------------------------------------------------------------------

void KadPrefs::statsIncUDPFirewalledNodes(bool fw)
{
    if (fw)
        ++m_statsUDPFirewalledNodes;
    else
        ++m_statsUDPOpenNodes;
}

void KadPrefs::statsIncTCPFirewalledNodes(bool fw)
{
    if (fw)
        ++m_statsTCPFirewalledNodes;
    else
        ++m_statsTCPOpenNodes;
}

float KadPrefs::statsGetFirewalledRatio(bool udp) const
{
    uint32 open = udp ? m_statsUDPOpenNodes : m_statsTCPOpenNodes;
    uint32 fw = udp ? m_statsUDPFirewalledNodes : m_statsTCPFirewalledNodes;
    uint32 total = open + fw;
    if (total > 0)
        return static_cast<float>(fw) / static_cast<float>(total);
    return 0.0f;
}

float KadPrefs::statsGetKadV8Ratio()
{
    // Cache result for 60 seconds
    if (m_statsKadV8LastChecked != 0 && (time(nullptr) - m_statsKadV8LastChecked) < 60)
        return m_kadV8Ratio;

    m_statsKadV8LastChecked = time(nullptr);

    if (m_routingZone) {
        uint32 v8Contacts = 0;
        uint32 nonV8Contacts = 0;
        m_routingZone->getNumContacts(v8Contacts, nonV8Contacts, KADEMLIA_VERSION8_49b);
        uint32 total = v8Contacts + nonV8Contacts;
        if (total > 0)
            m_kadV8Ratio = static_cast<float>(v8Contacts) / static_cast<float>(total);
        else
            m_kadV8Ratio = 0.0f;
    }

    return m_kadV8Ratio;
}

// ---------------------------------------------------------------------------
// Public methods — UDP verify key
// ---------------------------------------------------------------------------

uint32 KadPrefs::getUDPVerifyKey(uint32 targetIP)
{
    uint64 buffer = (static_cast<uint64>(thePrefs.kadUDPKey()) << 32) | targetIP;
    eMule::MD5Hasher md5(reinterpret_cast<const uint8*>(&buffer), sizeof(buffer));
    const uint8* rawHash = md5.getRawHash();

    // XOR-fold 4 chunks of 4 bytes → single uint32
    uint32 result = 0;
    for (int i = 0; i < 4; ++i) {
        uint32 chunk;
        std::memcpy(&chunk, rawHash + i * 4, 4);
        result ^= chunk;
    }
    // Ensure non-zero: result = (result % 0xFFFFFFFE) + 1
    return (result % 0xFFFFFFFEu) + 1;
}

// ---------------------------------------------------------------------------
// Public methods — RoutingZone link
// ---------------------------------------------------------------------------

void KadPrefs::setRoutingZone(RoutingZone* zone)
{
    m_routingZone = zone;
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

void KadPrefs::readFile()
{
    QFile file(m_filename);
    if (!file.exists())
        return;

    if (!file.open(QIODevice::ReadOnly)) {
        logKad(QStringLiteral("Failed to open Kad preferences file: %1").arg(m_filename));
        return;
    }

    QDataStream in(&file);
    in.setByteOrder(QDataStream::LittleEndian);

    // Binary format: uint32 IP | uint16 reserved | 16-byte KadID | uint8 tagCount(0)
    if (file.size() < 23) {
        logKad(QStringLiteral("Kad preferences file too small: %1").arg(m_filename));
        return;
    }

    uint32 ip = 0;
    uint16 reserved = 0;
    in >> ip >> reserved;

    // MFC uses ReadUInt128 → GetDataPtr() (raw host-order bytes).
    m_clientId = UInt128();
    if (in.readRawData(reinterpret_cast<char*>(m_clientId.getDataPtr()), 16) != 16) {
        logKad(QStringLiteral("Failed to read KadID from: %1").arg(m_filename));
        return;
    }

    uint8 tagCount = 0;
    in >> tagCount;

    m_ip = ip;
    m_ipLast = ip;
}

void KadPrefs::writeFile()
{
    QSaveFile file(m_filename);
    if (!file.open(QIODevice::WriteOnly)) {
        logKad(QStringLiteral("Failed to write Kad preferences file: %1").arg(m_filename));
        return;
    }

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);

    out << m_ip << uint16{0};

    // MFC uses WriteUInt128 → GetData() (raw host-order bytes).
    out.writeRawData(reinterpret_cast<const char*>(m_clientId.getData()), 16);

    out << uint8{0}; // tag count

    if (!file.commit()) {
        logKad(QStringLiteral("Failed to commit Kad preferences file: %1").arg(m_filename));
    }
}

} // namespace eMule::kad
