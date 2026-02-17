#pragma once

/// @file KadPrefs.h
/// @brief Kademlia runtime preferences (ported from kademlia/kademlia/Prefs.h).
///
/// Manages KadID, firewall state, storage limits, stats, and binary persistence
/// to preferencesKad.dat.  Global YAML settings (kadEnabled, kadUDPKey) live in
/// Preferences; this class handles the per-session runtime state.

#include "kademlia/KadUInt128.h"
#include "utils/Types.h"

#include <QString>

#include <cstdint>
#include <ctime>
#include <vector>

namespace eMule::kad {

class RoutingZone;

/// Runtime Kademlia preferences — not thread-safe (Kad thread only).
class KadPrefs {
public:
    explicit KadPrefs(const QString& configDir);
    ~KadPrefs();

    KadPrefs(const KadPrefs&) = delete;
    KadPrefs& operator=(const KadPrefs&) = delete;

    // -- Identity -------------------------------------------------------------

    [[nodiscard]] UInt128 kadId() const;
    void setKadId(const UInt128& id);

    [[nodiscard]] UInt128 clientHash() const;
    void setClientHash(const UInt128& hash);

    // -- Network --------------------------------------------------------------

    [[nodiscard]] uint32 ipAddress() const;
    void setIPAddress(uint32 ip);

    // -- Recheck IP -----------------------------------------------------------

    [[nodiscard]] bool recheckIP() const;
    void setRecheckIP();
    void incRecheckIP();

    // -- Connection -----------------------------------------------------------

    [[nodiscard]] bool hasHadContact() const;
    [[nodiscard]] bool hasLostConnection() const;
    void setLastContact();
    [[nodiscard]] time_t lastContact() const;

    // -- Firewall -------------------------------------------------------------

    [[nodiscard]] bool firewalled() const;
    void setFirewalled();
    void incFirewalled();

    // -- Storage limits -------------------------------------------------------

    [[nodiscard]] uint8 totalFile() const;
    void setTotalFile(uint8 val);

    [[nodiscard]] uint8 totalStoreSrc() const;
    void setTotalStoreSrc(uint8 val);

    [[nodiscard]] uint8 totalStoreKey() const;
    void setTotalStoreKey(uint8 val);

    [[nodiscard]] uint8 totalSource() const;
    void setTotalSource(uint8 val);

    [[nodiscard]] uint8 totalNotes() const;
    void setTotalNotes(uint8 val);

    [[nodiscard]] uint8 totalStoreNotes() const;
    void setTotalStoreNotes(uint8 val);

    // -- Stats ----------------------------------------------------------------

    [[nodiscard]] uint32 kademliaUsers() const;
    void setKademliaUsers(uint32 val);

    [[nodiscard]] uint32 kademliaFiles() const;
    void setKademliaFiles(uint32 averageFileCount);

    // -- Publish / buddy ------------------------------------------------------

    [[nodiscard]] bool publish() const;
    void setPublish(bool val);

    [[nodiscard]] bool findBuddy();
    void setFindBuddy(bool val);

    // -- External port --------------------------------------------------------

    [[nodiscard]] bool useExternKadPort() const;
    void setUseExternKadPort(bool val);

    [[nodiscard]] uint16 externalKadPort() const;
    void setExternKadPort(uint16 port, uint32 fromIP);

    [[nodiscard]] bool findExternKadPort(bool reset);

    [[nodiscard]] uint16 internKadPort() const;

    // -- Connect options ------------------------------------------------------

    [[nodiscard]] uint8 myConnectOptions() const;

    // -- Firewall stats -------------------------------------------------------

    void statsIncUDPFirewalledNodes(bool firewalled);
    void statsIncTCPFirewalledNodes(bool firewalled);
    [[nodiscard]] float statsGetFirewalledRatio(bool udp) const;
    [[nodiscard]] float statsGetKadV8Ratio();

    // -- UDP verify key -------------------------------------------------------

    [[nodiscard]] static uint32 getUDPVerifyKey(uint32 targetIP);

    // -- RoutingZone link (for stats) -----------------------------------------

    void setRoutingZone(RoutingZone* zone);

private:
    void readFile();
    void writeFile();

    QString m_filename;
    time_t m_lastContact = 0;
    UInt128 m_clientId;
    UInt128 m_clientHash;
    uint32 m_ip = 0;
    uint32 m_ipLast = 0;
    uint32 m_recheckIp = 0;
    uint32 m_firewallCounter = 0;
    uint32 m_kademliaUsers = 0;
    uint32 m_kademliaFiles = 0;
    uint8 m_totalFile = 0;
    uint8 m_totalStoreSrc = 0;
    uint8 m_totalStoreKey = 0;
    uint8 m_totalSource = 0;
    uint8 m_totalNotes = 0;
    uint8 m_totalStoreNotes = 0;
    bool m_publish = false;
    bool m_findBuddy = false;
    bool m_lastFirewallState = true;
    bool m_useExternKadPort = true;
    uint16 m_externKadPort = 0;
    std::vector<uint32> m_externPortIPs;
    std::vector<uint16> m_externPorts;
    uint32 m_statsUDPOpenNodes = 0;
    uint32 m_statsUDPFirewalledNodes = 0;
    uint32 m_statsTCPOpenNodes = 0;
    uint32 m_statsTCPFirewalledNodes = 0;
    time_t m_statsKadV8LastChecked = 0;
    float m_kadV8Ratio = 0.0f;
    RoutingZone* m_routingZone = nullptr;
};

} // namespace eMule::kad
