/// @file tst_KadRoutingZone.cpp
/// @brief Tests for KadRoutingZone — Kademlia routing table tree.

#include "TestHelpers.h"

#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadRoutingBin.h"
#include "kademlia/KadUInt128.h"
#include "app/AppContext.h"
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
    void addContact_udpKeyGatesSameAddressUpdate();
    void addContact_legacyKad2MayOnlyRefreshTimer();
    void addContact_neverClearsVerifiedFlag();
    void isAcceptableContact_rejectsPreKad2Version();
    void isAcceptableContact_rejectsKadIdHijack();
    void isAcceptableContact_allowsDuplicateOfUnverifiedContact();
    void isAcceptableContact_enforcesGlobalIPLimit();
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
    void writeFile_usesOfficialHeader();
    void readFile_acceptsOfficialHeader();
    void readFile_acceptsLegacyPortHeader();
    void readFile_verifiesAllWhenFileDeclaresNoVerifiedContact();
    void readFile_keepsUnverifiedWhenFileDeclaresAVerifiedContact();
    void readFile_verifiesAllForVersion1File();
    void randomLookupTarget_isInOwnZone();

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

void tst_KadRoutingZone::addContact_udpKeyGatesSameAddressUpdate()
{
    // Every >= 0.49a contact carries a UDP sender key, and only the host that
    // actually received our key can echo it back. The key check therefore has to
    // gate *every* update, not just ones that move the IP: a spoofed packet from
    // the contact's own address still gets to rewrite version, ports and the key
    // itself, which is enough to take the entry over. MFC RoutingZone.cpp:497-560
    // applies the check before any other update branch is considered.
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    const UInt128 id = makeId(42);
    const uint32 ip = makePublicIP(1);
    const uint32 myIP = theApp.publicIP();
    const KadUDPKey goodKey(0x1234ABCDu, myIP);
    const KadUDPKey attackerKey(0x0BADF00Du, myIP);

    QVERIFY(zone.add(id, ip, 4672, 4662, KADEMLIA_VERSION, goodKey,
                     true, false, false, false));

    // Same IP, same UDP port — only the key is wrong. Must not be applied.
    zone.add(id, ip, 4672, 5555, KADEMLIA_VERSION, attackerKey,
             true, true, false, false);

    Contact* contact = zone.getContact(id);
    QVERIFY(contact != nullptr);
    QCOMPARE(contact->getTCPPort(), uint16{4662});
    QCOMPARE(contact->getUDPKey().getKeyValue(myIP), uint32{0x1234ABCD});

    // Control: the very same update carrying the correct key must go through,
    // otherwise the test above would pass even if add() rejected everything.
    zone.add(id, ip, 4672, 5555, KADEMLIA_VERSION, goodKey,
             true, true, false, false);
    QCOMPARE(contact->getTCPPort(), uint16{5555});
}

void tst_KadRoutingZone::addContact_legacyKad2MayOnlyRefreshTimer()
{
    // Kad2 clients older than 0.49a have no sender key to authenticate with, so
    // once we have completed a HELLO with one there is nothing left to prove a
    // later packet really came from it. MFC RoutingZone.cpp:513-529 lets such an
    // entry refresh its liveness timer and nothing else — any packet claiming a
    // different address, port or version is dropped outright, so an attacker
    // cannot walk an established legacy entry over to a host they control.
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    const UInt128 id = makeId(42);
    const uint32 ip = makePublicIP(1);
    // fromHello = true → getReceivedHelloPacket(), which arms the legacy branch.
    QVERIFY(zone.add(id, ip, 4672, 4662, KADEMLIA_VERSION5_48a, KadUDPKey(),
                     true, false, true, false));

    Contact* contact = zone.getContact(id);
    QVERIFY(contact != nullptr);

    // Accept and reject both return false from add() (the contact is not *new*),
    // so contactUpdated is the only signal that separates them.
    QSignalSpy updated(&zone, &RoutingZone::contactUpdated);

    // Identical values → liveness refresh is allowed.
    zone.add(id, ip, 4672, 4662, KADEMLIA_VERSION5_48a, KadUDPKey(),
             true, true, false, false);
    QCOMPARE(updated.count(), 1);

    // Each of the four identifying fields on its own must veto the update.
    zone.add(id, makePublicIP(2), 4672, 4662, KADEMLIA_VERSION5_48a, KadUDPKey(),
             true, true, false, false);
    zone.add(id, ip, 4672, 5555, KADEMLIA_VERSION5_48a, KadUDPKey(),
             true, true, false, false);
    zone.add(id, ip, 5555, 4662, KADEMLIA_VERSION5_48a, KadUDPKey(),
             true, true, false, false);
    zone.add(id, ip, 4672, 4662, KADEMLIA_VERSION6_49aBETA, KadUDPKey(),
             true, true, false, false);

    QCOMPARE(updated.count(), 1);
    QCOMPARE(contact->address().toUint32(), ip);
    QCOMPARE(contact->getTCPPort(), uint16{4662});
    QCOMPARE(contact->getUDPPort(), uint16{4672});
    QCOMPARE(contact->getVersion(), uint8{KADEMLIA_VERSION5_48a});
}

void tst_KadRoutingZone::addContact_neverClearsVerifiedFlag()
{
    // A KADEMLIA2_RES routing answer always reports its contacts with
    // ipVerified = false, because the responder cannot vouch for them. If such an
    // update were allowed to write that flag back, any peer could strip the
    // verification off every contact we hold — and getClosestTo() only hands out
    // verified contacts, so that alone would stall all our searches. The flag is
    // therefore only ever set, never cleared (clearing is changeContactIPAddress'
    // job on a genuine IP change).
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    const UInt128 id = makeId(42);
    const uint32 ip = makePublicIP(1);
    QVERIFY(zone.add(id, ip, 4672, 4662, KADEMLIA_VERSION, KadUDPKey(),
                     true, false, false, false));

    Contact* contact = zone.getContact(id);
    QVERIFY(contact != nullptr);
    QVERIFY(contact->isIpVerified());

    zone.add(id, ip, 4672, 5555, KADEMLIA_VERSION, KadUDPKey(),
             false /*ipVerified*/, true, false, false);

    // The TCP port proves the full-update branch really ran — without it the
    // verified assertion below would hold vacuously.
    QCOMPARE(contact->getTCPPort(), uint16{5555});
    QVERIFY(contact->isIpVerified());
}

// ---------------------------------------------------------------------------
// isAcceptableContact — vetting of contacts arriving in a KADEMLIA2_RES
// ---------------------------------------------------------------------------

void tst_KadRoutingZone::isAcceptableContact_rejectsPreKad2Version()
{
    // Kad1 nodes speak an incompatible, unauthenticated protocol; feeding them
    // into a live search only wastes lookup slots. MFC RoutingZone.cpp:928-948.
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    const Contact kad1(makeId(42), makePublicIP(1), 4672, 4662,
                       KADEMLIA_VERSION1_46c, KadUDPKey(), false, m_localId);
    QVERIFY(!zone.isAcceptableContact(&kad1));

    // Control: the first accepted version, same contact otherwise.
    const Contact kad2(makeId(42), makePublicIP(1), 4672, 4662,
                       KADEMLIA_VERSION2_47a, KadUDPKey(), false, m_localId);
    QVERIFY(zone.isAcceptableContact(&kad2));

    QVERIFY(!zone.isAcceptableContact(nullptr));
}

void tst_KadRoutingZone::isAcceptableContact_rejectsKadIdHijack()
{
    // A peer answering our KADEMLIA2_REQ can name any endpoint it likes for a
    // KadID. If we already proved that KadID lives at a specific address, a
    // response relocating it is an attempt to displace a known-good node from
    // other peers' searches — the classic route to eclipsing a keyword.
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    const UInt128 id = makeId(42);
    const uint32 ip = makePublicIP(1);
    QVERIFY(zone.add(id, ip, 4672, 4662, KADEMLIA_VERSION, KadUDPKey(),
                     true /*ipVerified*/, false, false, false));

    // Matching IP + UDP port: just a node we already know. The TCP port is not
    // part of a contact's identity here and may legitimately differ.
    const Contact same(id, ip, 4672, 9999, KADEMLIA_VERSION, KadUDPKey(),
                       false, m_localId);
    QVERIFY(zone.isAcceptableContact(&same));

    const Contact movedIP(id, makePublicIP(2), 4672, 4662, KADEMLIA_VERSION,
                          KadUDPKey(), false, m_localId);
    QVERIFY(!zone.isAcceptableContact(&movedIP));

    const Contact movedPort(id, ip, 9999, 4662, KADEMLIA_VERSION,
                            KadUDPKey(), false, m_localId);
    QVERIFY(!zone.isAcceptableContact(&movedPort));
}

void tst_KadRoutingZone::isAcceptableContact_allowsDuplicateOfUnverifiedContact()
{
    // If we never proved where the KadID lives — e.g. it came straight out of
    // nodes.dat — we have no claim worth defending, and rejecting the newcomer
    // would let one unverified entry permanently shadow the real node.
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    const UInt128 id = makeId(42);
    QVERIFY(zone.add(id, makePublicIP(1), 4672, 4662, KADEMLIA_VERSION, KadUDPKey(),
                     false /*ipVerified*/, false, false, false));
    QVERIFY(!zone.getContact(id)->isIpVerified());

    const Contact elsewhere(id, makePublicIP(2), 9999, 4662, KADEMLIA_VERSION,
                            KadUDPKey(), false, m_localId);
    QVERIFY(zone.isAcceptableContact(&elsewhere));
}

void tst_KadRoutingZone::isAcceptableContact_enforcesGlobalIPLimit()
{
    // For a KadID we have never seen, the only defence left is the address
    // budget: 1 contact per IP and 10 per /24 across the whole routing table.
    // Without it a single host could answer every routing request with sybils
    // under fresh KadIDs and own the search.
    RoutingZone zone(m_localId, m_tmpDir->filePath(QStringLiteral("nodes.dat")));

    const uint32 takenIP = makePublicIP(1);
    QVERIFY(zone.add(makeId(42), takenIP, 4672, 4662, KADEMLIA_VERSION, KadUDPKey(),
                     true, false, false, false));

    // Unknown KadID, but the IP already hosts a routing contact (limit is 1).
    const Contact sybil(makeId(43), takenIP, 4672, 4662, KADEMLIA_VERSION,
                        KadUDPKey(), false, m_localId);
    QVERIFY(!zone.isAcceptableContact(&sybil));

    // Control: same unknown KadID on an address nobody occupies.
    const Contact fresh(makeId(43), makePublicIP(2), 4672, 4662, KADEMLIA_VERSION,
                        KadUDPKey(), false, m_localId);
    QVERIFY(zone.isAcceptableContact(&fresh));
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

// ---------------------------------------------------------------------------
// nodes.dat interoperability with official eMule
// ---------------------------------------------------------------------------

namespace {

/// One contact record in a nodes.dat fixture.
struct NodeRecord {
    UInt128 id;
    uint32  ip;
    bool    verified; // only written for record version >= 2
};

/// Write a nodes.dat with an arbitrary header, followed by `records`.
/// @param recordVersion 2 → full record (UDP key + verified byte); 1 → the older
///                      layout that stops after the Kad version byte.
void writeNodesFileRecords(const QString& path, const QList<uint32>& header,
                           const QList<NodeRecord>& records, int recordVersion = 2)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    for (uint32 v : header)
        ds << v;

    for (const NodeRecord& rec : records) {
        uint8 idBytes[16];
        std::memcpy(idBytes, rec.id.getData(), 16);
        f.write(reinterpret_cast<const char*>(idBytes), 16);

        ds << rec.ip;                    // IP (host order, as written by writeFile)
        ds << static_cast<uint16>(4672); // UDP port
        ds << static_cast<uint16>(4662); // TCP port
        ds << static_cast<uint8>(KADEMLIA_VERSION);
        if (recordVersion >= 2) {
            ds << static_cast<uint32>(0); // UDP key
            ds << static_cast<uint32>(0); // UDP key IP
            ds << static_cast<uint8>(rec.verified ? 1 : 0);
        }
    }
    f.close();
}

/// Write a nodes.dat with an arbitrary header, followed by one v2 contact record.
void writeNodesFileWithHeader(const QString& path, const QList<uint32>& header,
                              const UInt128& contactId, uint32 ip)
{
    writeNodesFileRecords(path, header, {NodeRecord{contactId, ip, true}});
}

} // namespace

void tst_KadRoutingZone::writeFile_usesOfficialHeader()
{
    // Official eMule writes [0][2][count]; the leading zero stops pre-0.48a
    // clients from parsing the file. Writing [2][count] made an official client
    // read "2" as a legacy contact count and consume garbage.
    const QString nodesFile = m_tmpDir->filePath(QStringLiteral("nodes.dat"));
    {
        RoutingZone zone(m_localId, nodesFile);
        zone.add(makeId(100), makePublicIP(1), 4672, 4662,
                 KADEMLIA_VERSION, KadUDPKey(), true, false, false, false);
        QCOMPARE(zone.getNumContacts(), uint32{1});
    }

    QFile f(nodesFile);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    uint32 sentinel = 0xFFFFFFFF;
    uint32 version = 0xFFFFFFFF;
    uint32 count = 0xFFFFFFFF;
    ds >> sentinel >> version >> count;

    QCOMPARE(sentinel, uint32{0});
    QCOMPARE(version, uint32{2});
    QCOMPARE(count, uint32{1});
}

void tst_KadRoutingZone::readFile_acceptsOfficialHeader()
{
    const QString nodesFile = m_tmpDir->filePath(QStringLiteral("official.dat"));
    const UInt128 id = makeId(4242);
    writeNodesFileWithHeader(nodesFile, {0u, 2u, 1u}, id, makePublicIP(7));

    RoutingBin::resetGlobalTracking();
    RoutingZone zone(m_localId, nodesFile);
    QCOMPARE(zone.getNumContacts(), uint32{1});
    QVERIFY(zone.getContact(id) != nullptr);
}

void tst_KadRoutingZone::readFile_acceptsLegacyPortHeader()
{
    // Older builds of this port wrote [2][count] with no sentinel. Existing
    // installs must not lose their routing table on upgrade.
    const QString nodesFile = m_tmpDir->filePath(QStringLiteral("legacy.dat"));
    const UInt128 id = makeId(5353);
    writeNodesFileWithHeader(nodesFile, {2u, 1u}, id, makePublicIP(8));

    RoutingBin::resetGlobalTracking();
    RoutingZone zone(m_localId, nodesFile);
    QCOMPARE(zone.getNumContacts(), uint32{1});
    QVERIFY(zone.getContact(id) != nullptr);
}

// ---------------------------------------------------------------------------
// nodes.dat bootstrap verification fallback
// ---------------------------------------------------------------------------

void tst_KadRoutingZone::readFile_verifiesAllWhenFileDeclaresNoVerifiedContact()
{
    // getClosestTo() only hands out IP-verified contacts, but a nodes.dat written
    // by an old client carries no verified flags at all. Without the fallback
    // every loaded contact is unusable, no search can start, and Kad never
    // bootstraps. MFC RoutingZone.cpp:252-255 trusts them once for the session.
    const QString nodesFile = m_tmpDir->filePath(QStringLiteral("unverified.dat"));
    const UInt128 idA = makeId(1111);
    const UInt128 idB = makeId(2222);
    writeNodesFileRecords(nodesFile, {0u, 2u, 2u},
                          {NodeRecord{idA, makePublicIP(3), false},
                           NodeRecord{idB, makePublicIP(4), false}});

    RoutingBin::resetGlobalTracking();
    RoutingZone zone(m_localId, nodesFile);
    QCOMPARE(zone.getNumContacts(), uint32{2});

    QVERIFY(zone.getContact(idA) != nullptr);
    QVERIFY(zone.getContact(idA)->isIpVerified());
    QVERIFY(zone.getContact(idB) != nullptr);
    QVERIFY(zone.getContact(idB)->isIpVerified());
}

void tst_KadRoutingZone::readFile_keepsUnverifiedWhenFileDeclaresAVerifiedContact()
{
    // The fallback is a bootstrap crutch, not a blanket amnesty. Once the file
    // proves it understands the verified byte, its unverified entries are a
    // deliberate statement and must stay unverified — otherwise every restart
    // would launder unproven contacts into search-eligible ones.
    const QString nodesFile = m_tmpDir->filePath(QStringLiteral("mixed.dat"));
    const UInt128 idVerified = makeId(3333);
    const UInt128 idUnverified = makeId(4444);
    writeNodesFileRecords(nodesFile, {0u, 2u, 2u},
                          {NodeRecord{idVerified, makePublicIP(5), true},
                           NodeRecord{idUnverified, makePublicIP(6), false}});

    RoutingBin::resetGlobalTracking();
    RoutingZone zone(m_localId, nodesFile);
    QCOMPARE(zone.getNumContacts(), uint32{2});

    QVERIFY(zone.getContact(idVerified) != nullptr);
    QVERIFY(zone.getContact(idVerified)->isIpVerified());
    QVERIFY(zone.getContact(idUnverified) != nullptr);
    QVERIFY(!zone.getContact(idUnverified)->isIpVerified());
}

void tst_KadRoutingZone::readFile_verifiesAllForVersion1File()
{
    // Only nodes.dat version >= 2 carries a verified byte at all, so a v1 file can
    // never declare a verified contact and must always take the fallback. This is
    // the case the fallback exists for: upgrading from an old install.
    const QString nodesFile = m_tmpDir->filePath(QStringLiteral("v1.dat"));
    const UInt128 id = makeId(5555);
    writeNodesFileRecords(nodesFile, {0u, 1u, 1u},
                          {NodeRecord{id, makePublicIP(9), false}}, 1 /*recordVersion*/);

    RoutingBin::resetGlobalTracking();
    RoutingZone zone(m_localId, nodesFile);
    QCOMPARE(zone.getNumContacts(), uint32{1});
    QVERIFY(zone.getContact(id) != nullptr);
    QVERIFY(zone.getContact(id)->isIpVerified());
}

// ---------------------------------------------------------------------------
// randomLookup target
// ---------------------------------------------------------------------------

void tst_KadRoutingZone::randomLookupTarget_isInOwnZone()
{
    // A zone refresh must probe *its own* region of the keyspace. Two bugs used
    // to break that: zoneIndex bits were copied from the wrong end
    // (getBitNumber(0) is the MSB while zoneIndex accumulates in the low bits,
    // so the prefix was always zero), and the result was never XORed with the
    // local ID — so every zone at every depth probed the same wrong region.
    //
    // Invariant (MFC RoutingZone.cpp:857-865): target XOR localKadId must start
    // with the zone's index in its top `level` bits.
    for (uint32 level = 1; level <= 16; ++level) {
        const UInt128 zoneIndex(level * 37u + 5u); // arbitrary, fits in `level` bits below
        const UInt128 target =
            RoutingZone::makeRandomLookupTarget(zoneIndex, level, m_localId);

        UInt128 distance(target);
        distance.xorWith(m_localId);

        UInt128 expectedPrefix(zoneIndex);
        expectedPrefix.shiftLeft(128 - level);

        for (uint32 bit = 0; bit < level; ++bit) {
            QCOMPARE(distance.getBitNumber(bit), expectedPrefix.getBitNumber(bit));
        }
    }

    // The low bits must actually be randomised, otherwise a zone would probe a
    // single fixed point forever.
    const UInt128 zoneIndex(uint32{1});
    const UInt128 first = RoutingZone::makeRandomLookupTarget(zoneIndex, 4, m_localId);
    bool differs = false;
    for (int i = 0; i < 16 && !differs; ++i)
        differs = (RoutingZone::makeRandomLookupTarget(zoneIndex, 4, m_localId) != first);
    QVERIFY2(differs, "randomLookupTarget must not return a constant");
}

QTEST_MAIN(tst_KadRoutingZone)
#include "tst_KadRoutingZone.moc"
