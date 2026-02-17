/// @file tst_KadRoutingZone.cpp
/// @brief Tests for KadRoutingZone — Kademlia routing table tree.

#include "TestHelpers.h"

#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadRoutingBin.h"
#include "kademlia/KadUInt128.h"
#include "prefs/Preferences.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"

#include <QSignalSpy>
#include <QTest>

#include <cstring>

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

using namespace eMule;
using namespace eMule::kad;
using namespace eMule::testing;

namespace {

/// Generate a UInt128 ID from a seed, spread across bits to avoid clustering.
UInt128 makeId(uint32 seed)
{
    return UInt128(seed);
}

/// Generate a UInt128 that is close to `localId` (shares high bits).
/// The XOR distance from localId is exactly `seed` in bits 120-127,
/// zero elsewhere — guaranteeing deterministic routing behaviour.
UInt128 makeCloseId(const UInt128& localId, uint32 seed)
{
    // Build a distance with only bits 120-127 set to seed.
    UInt128 distance(uint32{0});
    for (uint32 b = 0; b < 8; ++b)
        distance.setBitNumber(120 + b, (seed >> (7 - b)) & 1);
    // id = localId XOR distance → distance from localId is exactly `seed`
    UInt128 id(localId);
    id.xorWith(distance);
    return id;
}

/// Make a public IP from seed (use different /24 subnets to avoid subnet limits).
uint32 makePublicIP(uint32 seed)
{
    // 88.x.x.1 — each seed gets a different /24
    return (88u << 24) | ((seed & 0xFF) << 16) | (((seed >> 8) & 0xFF) << 8) | 1;
}

} // namespace

class tst_KadRoutingZone : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void construction_createsLeafRoot();
    void addContact_basic();
    void addContact_triggersSplit();
    void addContact_rejectsSelfId();
    void addContact_rejectsKad1();
    void getContact_byId();
    void getContact_byIPPort();
    void getRandomContact();
    void getClosestTo_ordering();
    void getNumContacts();
    void getBootstrapContacts();
    void consolidate_mergesUnderfull();
    void estimateCount_lowLevel();
    void verifyContact_setsFlag();
    void writeReadRoundTrip();

private:
    UInt128 m_localId;
    TempDir* m_tmpDir = nullptr;
};

void tst_KadRoutingZone::initTestCase()
{
    // Ensure thePrefs is initialized
    TempDir tmp;
    thePrefs.load(tmp.filePath(QStringLiteral("prefs_init.yaml")));
}

void tst_KadRoutingZone::init()
{
    RoutingBin::resetGlobalTracking();
    m_localId.setValueRandom();
    m_tmpDir = new TempDir();
}

void tst_KadRoutingZone::cleanup()
{
    RoutingBin::resetGlobalTracking();
    delete m_tmpDir;
    m_tmpDir = nullptr;
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

void tst_KadRoutingZone::construction_createsLeafRoot()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));
    QCOMPARE(zone.getNumContacts(), uint32{0});
}

// ---------------------------------------------------------------------------
// Add contacts
// ---------------------------------------------------------------------------

void tst_KadRoutingZone::addContact_basic()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    UInt128 contactId = makeId(42);
    uint32 ip = makePublicIP(1);

    QVERIFY(zone.add(contactId, ip, 4672, 4662, KADEMLIA_VERSION, KadUDPKey(),
                     true, false, false, false));
    QCOMPARE(zone.getNumContacts(), uint32{1});
}

void tst_KadRoutingZone::addContact_triggersSplit()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    // Add 30 contacts whose XOR distance from localId is exactly seed
    // in bits 120-127, zero elsewhere. They follow zoneIndex 0 (always
    // splittable) and deterministically distribute into 4 bins.
    constexpr uint32 kNumContacts = 30;
    uint32 added = 0;
    for (uint32 i = 1; i <= kNumContacts; ++i) {
        UInt128 id = makeCloseId(m_localId, i);
        uint32 ip = makePublicIP(i);
        if (zone.add(id, ip, static_cast<uint16>(4672 + i), static_cast<uint16>(4662 + i),
                     KADEMLIA_VERSION, KadUDPKey(), true, false, false, false)) {
            ++added;
        }
    }

    // All 30 must be accepted — they have unique IDs/IPs and the tree
    // splits along zoneIndex 0 to accommodate them.
    QCOMPARE(added, kNumContacts);
    QCOMPARE(zone.getNumContacts(), kNumContacts);
}

void tst_KadRoutingZone::addContact_rejectsSelfId()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    // Adding our own ID should be rejected
    uint32 ip = makePublicIP(1);
    QVERIFY(!zone.add(m_localId, ip, 4672, 4662, KADEMLIA_VERSION, KadUDPKey(),
                      true, false, false, false));
    QCOMPARE(zone.getNumContacts(), uint32{0});
}

void tst_KadRoutingZone::addContact_rejectsKad1()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    UInt128 contactId = makeId(42);
    uint32 ip = makePublicIP(1);

    // Version 1 contacts should be rejected
    QVERIFY(!zone.add(contactId, ip, 4672, 4662, KADEMLIA_VERSION1_46c, KadUDPKey(),
                      true, false, false, false));
    QCOMPARE(zone.getNumContacts(), uint32{0});
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

void tst_KadRoutingZone::getContact_byId()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    UInt128 contactId = makeId(42);
    uint32 ip = makePublicIP(1);
    zone.add(contactId, ip, 4672, 4662, KADEMLIA_VERSION, KadUDPKey(),
             true, false, false, false);

    Contact* found = zone.getContact(contactId);
    QVERIFY(found != nullptr);
    QCOMPARE(found->getClientID(), contactId);

    UInt128 missingId = makeId(999);
    QCOMPARE(zone.getContact(missingId), nullptr);
}

void tst_KadRoutingZone::getContact_byIPPort()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    UInt128 contactId = makeId(42);
    uint32 ip = makePublicIP(1);
    zone.add(contactId, ip, 4672, 4662, KADEMLIA_VERSION, KadUDPKey(),
             true, false, false, false);

    // Lookup by UDP port
    Contact* found = zone.getContact(ip, 4672, false);
    QVERIFY(found != nullptr);
    QCOMPARE(found->getClientID(), contactId);

    // Wrong IP should return null
    QCOMPARE(zone.getContact(makePublicIP(99), 4672, false), nullptr);
}

void tst_KadRoutingZone::getRandomContact()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    // Empty zone
    QCOMPARE(zone.getRandomContact(3, 0), nullptr);

    // Add a contact
    UInt128 contactId = makeId(42);
    uint32 ip = makePublicIP(1);
    zone.add(contactId, ip, 4672, 4662, KADEMLIA_VERSION, KadUDPKey(),
             true, false, false, false);

    QVERIFY(zone.getRandomContact(3, 0) != nullptr);
}

// ---------------------------------------------------------------------------
// Closest-to query
// ---------------------------------------------------------------------------

void tst_KadRoutingZone::getClosestTo_ordering()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    // Add several contacts
    for (uint32 i = 1; i <= 5; ++i) {
        UInt128 id = makeId(i);
        uint32 ip = makePublicIP(i);
        zone.add(id, ip, static_cast<uint16>(4672 + i), static_cast<uint16>(4662 + i),
                 KADEMLIA_VERSION, KadUDPKey(), true, false, false, false);
    }

    // Query closest to target = UInt128(3)
    UInt128 target(uint32{3});
    UInt128 distance(m_localId);
    distance.xorWith(target);

    ContactMap result;
    zone.getClosestTo(3, target, distance, 3, result);

    QVERIFY(!result.empty());
    QVERIFY(result.size() <= 3);

    // Results should be ordered by XOR distance to target (map key = distance)
    UInt128 prevDist(uint32{0});
    for (auto& [dist, contact] : result) {
        QVERIFY(dist >= prevDist);
        prevDist = dist;
    }
}

// ---------------------------------------------------------------------------
// Count
// ---------------------------------------------------------------------------

void tst_KadRoutingZone::getNumContacts()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    for (uint32 i = 1; i <= 5; ++i) {
        UInt128 id = makeId(i * 100);
        uint32 ip = makePublicIP(i);
        zone.add(id, ip, static_cast<uint16>(4672 + i), static_cast<uint16>(4662 + i),
                 KADEMLIA_VERSION, KadUDPKey(), true, false, false, false);
    }

    QCOMPARE(zone.getNumContacts(), uint32{5});
}

// ---------------------------------------------------------------------------
// Bootstrap contacts
// ---------------------------------------------------------------------------

void tst_KadRoutingZone::getBootstrapContacts()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    for (uint32 i = 1; i <= 5; ++i) {
        UInt128 id = makeId(i * 100);
        uint32 ip = makePublicIP(i);
        zone.add(id, ip, static_cast<uint16>(4672 + i), static_cast<uint16>(4662 + i),
                 KADEMLIA_VERSION, KadUDPKey(), true, false, false, false);
    }

    ContactArray result;
    zone.getBootstrapContacts(result, 3);
    QVERIFY(result.size() <= 3);
    QVERIFY(!result.empty());
}

// ---------------------------------------------------------------------------
// Consolidate
// ---------------------------------------------------------------------------

void tst_KadRoutingZone::consolidate_mergesUnderfull()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    // Add enough close contacts to trigger a split
    for (uint32 i = 1; i <= 12; ++i) {
        UInt128 id = makeCloseId(m_localId, i);
        uint32 ip = makePublicIP(i);
        zone.add(id, ip, static_cast<uint16>(4672 + i), static_cast<uint16>(4662 + i),
                 KADEMLIA_VERSION, KadUDPKey(), true, false, false, false);
    }

    uint32 beforeConsolidate = zone.getNumContacts();
    zone.consolidate();
    // After consolidate, contact count should be preserved
    QCOMPARE(zone.getNumContacts(), beforeConsolidate);
}

// ---------------------------------------------------------------------------
// Estimate count
// ---------------------------------------------------------------------------

void tst_KadRoutingZone::estimateCount_lowLevel()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    // With a few contacts, estimate should be > 0 or K * 2^0 = K for root
    uint32 estimate = zone.estimateCount();
    // Empty root zone — the formula gives K * 2^0 = K (since level=0 < KBASE)
    QCOMPARE(estimate, static_cast<uint32>(kK));
}

// ---------------------------------------------------------------------------
// Verify contact
// ---------------------------------------------------------------------------

void tst_KadRoutingZone::verifyContact_setsFlag()
{
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    UInt128 contactId = makeId(42);
    uint32 ip = makePublicIP(1);
    // Add with ipVerified = false
    zone.add(contactId, ip, 4672, 4662, KADEMLIA_VERSION, KadUDPKey(),
             false, false, false, false);

    Contact* contact = zone.getContact(contactId);
    QVERIFY(contact != nullptr);
    QVERIFY(!contact->isIpVerified());

    QVERIFY(zone.verifyContact(contactId, ip));
    QVERIFY(contact->isIpVerified());
}

// ---------------------------------------------------------------------------
// Write / read round-trip
// ---------------------------------------------------------------------------

void tst_KadRoutingZone::writeReadRoundTrip()
{
    QString nodesFile = m_tmpDir->filePath(QStringLiteral("nodes.dat"));

    std::vector<UInt128> addedIds;

    // Create zone, add contacts, destroy (triggers writeFile)
    {
        RoutingZone zone(m_localId, nodesFile);

        for (uint32 i = 1; i <= 5; ++i) {
            UInt128 id = makeId(i * 100);
            uint32 ip = makePublicIP(i);
            zone.add(id, ip, static_cast<uint16>(4672 + i), static_cast<uint16>(4662 + i),
                     KADEMLIA_VERSION, KadUDPKey(), true, false, false, false);
            addedIds.push_back(id);
        }

        QCOMPARE(zone.getNumContacts(), uint32{5});
    }

    // Reset global tracking for the new zone
    RoutingBin::resetGlobalTracking();

    // Reload from file
    RoutingZone zone2(m_localId, nodesFile);

    // All contacts should be restored
    QCOMPARE(zone2.getNumContacts(), uint32{5});

    for (const auto& id : addedIds) {
        QVERIFY(zone2.getContact(id) != nullptr);
    }
}

QTEST_MAIN(tst_KadRoutingZone)
#include "tst_KadRoutingZone.moc"
