#pragma once

/// @file KadRoutingZone.h
/// @brief Kademlia routing table tree (ported from kademlia/routing/RoutingZone.h).
///
/// The routing table is a binary tree of zones; each leaf holds a RoutingBin
/// (K-bucket).  The tree splits when a bin overflows and the zone is close
/// enough to the local node ID.

#include "kademlia/KadTypes.h"
#include "kademlia/KadUDPKey.h"
#include "kademlia/KadUInt128.h"
#include "utils/Types.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <ctime>

namespace eMule { class SafeFile; }

namespace eMule::kad {

class Contact;
class RoutingBin;

/// A single zone in the Kademlia routing tree.
class RoutingZone : public QObject {
    Q_OBJECT

public:
    /// Construct the root zone.
    RoutingZone(const UInt128& localKadId, const QString& nodesFilePath,
                QObject* parent = nullptr);

    ~RoutingZone() override;

    RoutingZone(const RoutingZone&) = delete;
    RoutingZone& operator=(const RoutingZone&) = delete;

    // -- Contact management ---------------------------------------------------

    /// Validate IP and add a contact (main entry point).
    bool add(const UInt128& id, uint32 ip, uint16 udpPort, uint16 tcpPort,
             uint8 version, const KadUDPKey& udpKey, bool ipVerified,
             bool update, bool fromHello, bool fromNodesDat);

    /// Add without IP validation (already checked). Returns true if contact was added.
    bool addUnfiltered(const UInt128& id, uint32 ip, uint16 udpPort, uint16 tcpPort,
                       uint8 version, const KadUDPKey& udpKey, bool ipVerified,
                       bool update, bool fromHello, bool fromNodesDat);

    /// Tree-walk add of a pre-built contact.
    bool add(Contact* contact, bool update, bool& ipVerified);

    // -- Queries --------------------------------------------------------------

    [[nodiscard]] Contact* getContact(const UInt128& id) const;
    [[nodiscard]] Contact* getContact(uint32 ip, uint16 port, bool tcpPort) const;

    [[nodiscard]] Contact* getRandomContact(uint32 maxType, uint32 minVersion) const;

    [[nodiscard]] uint32 getNumContacts() const;
    void getNumContacts(uint32& inOutContacts, uint32& inOutFilteredContacts,
                        uint8 minVersion) const;

    [[nodiscard]] static bool isAcceptableContact(const Contact* contact);

    // -- Bulk operations ------------------------------------------------------

    void getAllEntries(ContactArray& result, bool emptyFirst = true) const;

    void getClosestTo(uint32 maxType, const UInt128& target,
                      const UInt128& distance, uint32 maxRequired,
                      ContactMap& result, bool emptyFirst = true,
                      bool setInUse = false) const;

    void getBootstrapContacts(ContactArray& result, uint32 maxRequired) const;

    /// Convenience method: create a Contact from params and add/update it.
    bool addOrUpdateContact(const UInt128& id, uint32 ip, uint16 udpPort, uint16 tcpPort,
                            uint8 version, const KadUDPKey& udpKey, bool ipVerified);

    // -- Maintenance ----------------------------------------------------------

    void consolidate();
    void onBigTimer();
    void onSmallTimer();

    [[nodiscard]] uint32 estimateCount() const;

    bool verifyContact(const UInt128& id, uint32 ip);
    [[nodiscard]] bool hasOnlyLANNodes() const;

    // -- File I/O -------------------------------------------------------------

    void readFile(const QString& specialNodesdat = {});
    void writeFile();

    // -- Static access --------------------------------------------------------

    [[nodiscard]] static const UInt128& localKadId() { return s_localKadId; }
    [[nodiscard]] static const QString& nodesFilename() { return s_nodesFilename; }

signals:
    void contactAdded(Contact* contact);
    void contactUpdated(Contact* contact);
    void contactRemoved(Contact* contact);

private:
    /// Internal child zone constructor.
    RoutingZone(RoutingZone* superZone, uint32 level, const UInt128& zoneIndex);

    void init(RoutingZone* superZone, uint32 level, const UInt128& zoneIndex);

    [[nodiscard]] bool isLeaf() const;
    [[nodiscard]] bool canSplit() const;
    void split();
    [[nodiscard]] RoutingZone* genSubZone(int side);

    [[nodiscard]] uint32 topDepth() const;
    [[nodiscard]] uint32 getMaxDepth() const;
    [[nodiscard]] RoutingBin* randomBin() const;

    void readBootstrapNodesDat(SafeFile& sf);
    void randomLookup();
    void setAllContactsVerified();
    void startTimer();
    void stopTimer();
    void onTimerTick();

    RoutingZone* m_subZones[2] = {nullptr, nullptr};
    RoutingZone* m_superZone = nullptr;
    RoutingBin* m_bin = nullptr;
    UInt128 m_zoneIndex;
    uint32 m_level = 0;
    time_t m_nextSmallTimer = 0;
    time_t m_nextBigTimer = 0;
    QTimer* m_timer = nullptr;

    static UInt128 s_localKadId;
    static QString s_nodesFilename;
};

} // namespace eMule::kad
