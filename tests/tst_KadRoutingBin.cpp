/// @file tst_KadRoutingBin.cpp
/// @brief Tests for KadRoutingBin.h — Kademlia K-bucket.

#include "TestHelpers.h"

#include "kademlia/KadRoutingBin.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadUInt128.h"

#include <QTest>

#include <cstring>

using namespace eMule;
using namespace eMule::kad;

// Helper to create a contact with a unique ID from a seed value.
// IP is derived from seed to ensure uniqueness across different subnets.
static Contact* makeContact(uint32 seed, uint32 ip = 0)
{
    UInt128 clientId(seed);
    if (ip == 0)
        ip = 0x0A000000 | (seed & 0xFFFF); // 10.0.xx.xx — spread across subnets
    UInt128 target(uint32{0});
    return new Contact(clientId, ip, static_cast<uint16>(4672 + seed), static_cast<uint16>(4662 + seed),
                       target, 8, KadUDPKey(), false);
}

class tst_KadRoutingBin : public QObject {
    Q_OBJECT

private slots:
    void init();       // Reset global tracking before each test
    void cleanup();

    void addContact_basic();
    void addContact_full();
    void addContact_duplicateId();
    void removeContact_basic();
    void getContact_byId();
    void getContact_byIP();
    void getOldest();
    void setAlive_movesToBottom();
    void getClosestTo_ordering();
    void getClosestTo_excludesUnverified();
    void getEntries();
    void getRandomContact();
    void globalIPLimits();
    void subnetLimits();
    void replaceWeakest_takesTheExpiredOrClearlyWeakerOne();
    void replaceWeakest_respectsTheLimitsAndRemovesNothingOnRefusal();
    void changeContactIPAddress();
    void hasOnlyLANNodes();
    void getNumContacts_filtering();
};

void tst_KadRoutingBin::init()
{
    RoutingBin::resetGlobalTracking();
}

void tst_KadRoutingBin::cleanup()
{
    RoutingBin::resetGlobalTracking();
}

// ---------------------------------------------------------------------------
// Basic add / remove
// ---------------------------------------------------------------------------

void tst_KadRoutingBin::addContact_basic()
{
    RoutingBin bin;
    // Bin owns the contact — will delete it in destructor
    auto* c = makeContact(1);
    QVERIFY(bin.addContact(c));
    QCOMPARE(bin.getSize(), uint32{1});
}

void tst_KadRoutingBin::addContact_full()
{
    RoutingBin bin;
    // Bin owns all added contacts

    // Fill bin to K capacity, each in a different /24 subnet
    for (uint32 i = 0; i < kK; ++i) {
        auto* c = makeContact(i + 1, 0x0A000000 | ((i + 1) << 8) | 1);
        QVERIFY(bin.addContact(c));
    }
    QCOMPARE(bin.getSize(), uint32{kK});

    // One more should fail — we must delete the rejected contact ourselves
    auto* extra = makeContact(100, 0x0B000001);
    QVERIFY(!bin.addContact(extra));
    delete extra;
}

void tst_KadRoutingBin::addContact_duplicateId()
{
    RoutingBin bin;
    // Bin owns c1 (accepted)
    auto* c1 = makeContact(1, 0x0A000101);
    auto* c2 = makeContact(1, 0x0B000101); // same ID, different IP — will be rejected
    QVERIFY(bin.addContact(c1));
    QVERIFY(!bin.addContact(c2));
    delete c2; // rejected, we own it
}

void tst_KadRoutingBin::removeContact_basic()
{
    RoutingBin bin;
    auto* c = makeContact(1);
    bin.addContact(c);
    QCOMPARE(bin.getSize(), uint32{1});

    bin.removeContact(c);
    QCOMPARE(bin.getSize(), uint32{0});
    delete c; // removed from bin, we own it now
}

// ---------------------------------------------------------------------------
// Lookup
// ---------------------------------------------------------------------------

void tst_KadRoutingBin::getContact_byId()
{
    RoutingBin bin;
    auto* c1 = makeContact(1, 0x0A000101);
    auto* c2 = makeContact(2, 0x0A010101);
    bin.addContact(c1);
    bin.addContact(c2);

    UInt128 id(uint32{2});
    QCOMPARE(bin.getContact(id), c2);

    UInt128 missing(uint32{999});
    QCOMPARE(bin.getContact(missing), nullptr);
}

void tst_KadRoutingBin::getContact_byIP()
{
    RoutingBin bin;
    auto* c = makeContact(1, 0x0A000101);
    bin.addContact(c);

    // By UDP port
    QCOMPARE(bin.getContact(0x0A000101, c->getUDPPort(), false), c);
    // By TCP port
    QCOMPARE(bin.getContact(0x0A000101, c->getTCPPort(), true), c);
    // Port 0 = any
    QCOMPARE(bin.getContact(0x0A000101, 0, false), c);
    // Wrong IP
    QCOMPARE(bin.getContact(0x0B000101, c->getUDPPort(), false), nullptr);
}

void tst_KadRoutingBin::getOldest()
{
    RoutingBin bin;
    QCOMPARE(bin.getOldest(), nullptr);

    auto* c1 = makeContact(1, 0x0A000101);
    auto* c2 = makeContact(2, 0x0A010101);
    bin.addContact(c1);
    bin.addContact(c2);

    QCOMPARE(bin.getOldest(), c1);
}

// ---------------------------------------------------------------------------
// SetAlive / PushToBottom
// ---------------------------------------------------------------------------

void tst_KadRoutingBin::setAlive_movesToBottom()
{
    RoutingBin bin;
    auto* c1 = makeContact(1, 0x0A000101);
    auto* c2 = makeContact(2, 0x0A010101);
    bin.addContact(c1);
    bin.addContact(c2);

    QCOMPARE(bin.getOldest(), c1);

    // Mark c1 alive → moves to bottom
    bin.setAlive(c1);
    QCOMPARE(bin.getOldest(), c2);
}

// ---------------------------------------------------------------------------
// GetClosestTo
// ---------------------------------------------------------------------------

void tst_KadRoutingBin::getClosestTo_ordering()
{
    RoutingBin bin;
    // Create 3 contacts with IDs 1, 2, 3 — all IP-verified
    auto* c1 = makeContact(1, 0x0A000101);
    auto* c2 = makeContact(2, 0x0A010101);
    auto* c3 = makeContact(3, 0x0A020101);
    c1->setIpVerified(true);
    c2->setIpVerified(true);
    c3->setIpVerified(true);
    bin.addContact(c1);
    bin.addContact(c2);
    bin.addContact(c3);

    // Search closest to target = UInt128(2)
    UInt128 target(uint32{2});
    ContactMap result;
    bin.getClosestTo(3, target, 2, result);

    // Should return 2 closest (by XOR distance to target)
    QCOMPARE(static_cast<uint32>(result.size()), uint32{2});

    // The closest to target=2 by XOR: ID=2 (dist=0), ID=3 (dist=1), ID=1 (dist=3)
    auto it = result.begin();
    QCOMPARE(it->second, c2); // distance 0
    ++it;
    QCOMPARE(it->second, c3); // distance 1
}

void tst_KadRoutingBin::getClosestTo_excludesUnverified()
{
    // getClosestTo() feeds both our own searches and the KADEMLIA2_RES answers
    // we serve to remote peers, so an unverified contact must never appear —
    // otherwise this node propagates spoofable addresses into the network.
    RoutingBin bin;
    auto* verified = makeContact(2, 0x0A010101);
    auto* unverified = makeContact(3, 0x0A020101);
    verified->setIpVerified(true);
    unverified->setIpVerified(false);
    bin.addContact(verified);
    bin.addContact(unverified);

    UInt128 target(uint32{2});
    ContactMap result;
    bin.getClosestTo(3, target, 10, result);

    QCOMPARE(static_cast<uint32>(result.size()), uint32{1});
    QCOMPARE(result.begin()->second, verified);

    // Once verified it becomes eligible — proving the exclusion is the flag and
    // not something else about the contact.
    unverified->setIpVerified(true);
    ContactMap after;
    bin.getClosestTo(3, target, 10, after);
    QCOMPARE(static_cast<uint32>(after.size()), uint32{2});
}

// ---------------------------------------------------------------------------
// GetEntries
// ---------------------------------------------------------------------------

void tst_KadRoutingBin::getEntries()
{
    RoutingBin bin;
    auto* c1 = makeContact(1, 0x0A000101);
    auto* c2 = makeContact(2, 0x0A010101);
    bin.addContact(c1);
    bin.addContact(c2);

    ContactArray result;
    bin.getEntries(result);
    QCOMPARE(static_cast<uint32>(result.size()), uint32{2});

    // Append mode
    bin.getEntries(result, false);
    QCOMPARE(static_cast<uint32>(result.size()), uint32{4});
}

// ---------------------------------------------------------------------------
// GetRandomContact
// ---------------------------------------------------------------------------

void tst_KadRoutingBin::getRandomContact()
{
    RoutingBin bin;
    QCOMPARE(bin.getRandomContact(3, 0), nullptr);

    auto* c = makeContact(1, 0x0A000101);
    bin.addContact(c);

    QVERIFY(bin.getRandomContact(3, 0) != nullptr);
    // With minKadVersion matching contact's version → should match (version=8)
    QVERIFY(bin.getRandomContact(3, 8) != nullptr);
    // Version too high → no match
    QCOMPARE(bin.getRandomContact(3, 9), nullptr);
}

// ---------------------------------------------------------------------------
// Global IP limits
// ---------------------------------------------------------------------------

void tst_KadRoutingBin::globalIPLimits()
{
    RoutingBin bin;
    // Add a contact with IP 10.0.1.1
    auto* c1 = makeContact(1, 0x0A000101);
    QVERIFY(bin.addContact(c1));

    // Second contact with same IP but different ID should be rejected (max 1 per IP)
    auto* c2 = makeContact(2, 0x0A000101);
    QVERIFY(!bin.addContact(c2));
    delete c2; // rejected
}

void tst_KadRoutingBin::subnetLimits()
{
    RoutingBin bin;
    // Use public IPs (not LAN) from same /24 — max 2 per bin from same /24
    // 88.1.1.x → host byte order 0x58010101, 0x58010102, 0x58010103
    auto* c1 = makeContact(1, 0x58010101);
    auto* c2 = makeContact(2, 0x58010102);
    auto* c3 = makeContact(3, 0x58010103);

    QVERIFY(bin.addContact(c1));
    QVERIFY(bin.addContact(c2));
    // Third from same /24 should be rejected (not LAN, so subnet limit applies)
    QVERIFY(!bin.addContact(c3));
    delete c3; // rejected
}

namespace {

/// A bin filled with kK plain contacts, one public /24 each (88.<i>.1.1), ids 1..kK.
void fillBin(RoutingBin& bin)
{
    for (uint32 i = 1; i <= kK; ++i)
        QVERIFY(bin.addContact(makeContact(i, 0x58000101u | (i << 16))));
}

} // namespace

// A full bin that cannot split dropped every newcomer unless the *front* contact
// happened to be expired.
void tst_KadRoutingBin::replaceWeakest_takesTheExpiredOrClearlyWeakerOne()
{
    RoutingBin bin;
    fillBin(bin);

    // Nobody expired, newcomer no better than anyone: it is the one dropped.
    auto* plain = makeContact(50, 0x59000101u);
    QVERIFY(bin.replaceWeakest(plain) == nullptr);
    QCOMPARE(bin.getSize(), uint32{kK});

    // An expired contact goes first, wherever it sits in the list.
    Contact* fifth = bin.getContact(UInt128(uint32{5}));
    QVERIFY(fifth != nullptr);
    fifth->expire();
    Contact* gone = bin.replaceWeakest(plain);
    QCOMPARE(gone, fifth);
    delete gone;
    QCOMPARE(bin.getSize(), uint32{kK});
    QCOMPARE(bin.getContact(UInt128(uint32{50})), plain);

    // A verified contact that said hello outranks one that never proved anything.
    auto* proven = makeContact(51, 0x5A000101u);
    proven->setIpVerified(true);
    proven->setReceivedHelloPacket();
    QVERIFY(proven->strength() >= bin.getOldest()->strength() + RoutingBin::kReplaceMargin);
    Contact* oldest = bin.getOldest();
    gone = bin.replaceWeakest(proven);
    QCOMPARE(gone, oldest);            // equal strength: the longest silent one
    delete gone;

    // ...but not one that is in use by a running lookup.
    ContactArray all;
    bin.getEntries(all);
    for (auto* c : all)
        c->incUse();
    auto* another = makeContact(52, 0x5B000101u);
    another->setIpVerified(true);
    another->setReceivedHelloPacket();
    QVERIFY(bin.replaceWeakest(another) == nullptr);
    delete another;
    for (auto* c : all)
        c->decUse();
}

// The old code removed the expired contact first and then tried the add, which a
// subnet limit could still refuse: a contact lost for nothing.
void tst_KadRoutingBin::replaceWeakest_respectsTheLimitsAndRemovesNothingOnRefusal()
{
    RoutingBin bin;
    // Two contacts share 88.1.1.x; the rest have a /24 each.
    QVERIFY(bin.addContact(makeContact(1, 0x58010101u)));
    QVERIFY(bin.addContact(makeContact(2, 0x58010102u)));
    for (uint32 i = 3; i <= kK; ++i)
        QVERIFY(bin.addContact(makeContact(i, 0x58000101u | (i << 16))));

    Contact* expired = bin.getContact(UInt128(uint32{7}));
    expired->expire();

    // A third from 88.1.1.x would break the two-per-bin rule.
    auto* third = makeContact(60, 0x58010103u);
    QVERIFY(bin.replaceWeakest(third) == nullptr);
    QCOMPARE(bin.getSize(), uint32{kK});
    QCOMPARE(bin.getContact(UInt128(uint32{7})), expired);
    delete third;

    // An address already in the table elsewhere is refused too.
    auto* sameIP = makeContact(61, 0x58010101u);
    QVERIFY(bin.replaceWeakest(sameIP) == nullptr);
    QCOMPARE(bin.getContact(UInt128(uint32{7})), expired);
    delete sameIP;

    // Replacing one of the two from that /24 keeps the count at two: allowed.
    bin.getContact(UInt128(uint32{2}))->expire();
    expired->updateType();             // no longer the first choice
    auto* swap = makeContact(62, 0x58010104u);
    Contact* gone = bin.replaceWeakest(swap);
    QVERIFY(gone != nullptr);
    QCOMPARE(gone->getClientID(), UInt128(uint32{2}));
    delete gone;

    // The global counts followed: the freed address can come back, a third cannot.
    QVERIFY(RoutingBin::checkGlobalIPLimits(0x58010102u, 4672, false));
    QVERIFY(!RoutingBin::checkGlobalIPLimits(0x58010104u, 4672, false));
}

// ---------------------------------------------------------------------------
// Change IP
// ---------------------------------------------------------------------------

void tst_KadRoutingBin::changeContactIPAddress()
{
    RoutingBin bin;
    auto* c = makeContact(1, 0x0A000101);
    bin.addContact(c);

    // Same IP → trivially accepted
    QVERIFY(bin.changeContactIPAddress(c, 0x0A000101));

    // Change to a new IP → should succeed
    QVERIFY(bin.changeContactIPAddress(c, 0x0B000101));
    QCOMPARE(c->address().toUint32(), uint32{0x0B000101});
}

// ---------------------------------------------------------------------------
// HasOnlyLANNodes
// ---------------------------------------------------------------------------

void tst_KadRoutingBin::hasOnlyLANNodes()
{
    RoutingBin bin;
    // Empty bin → technically true (vacuous truth)
    QVERIFY(bin.hasOnlyLANNodes());

    // 10.x.x.x is LAN. Contact stores m_netIp = htonl(m_ip).
    auto* c = makeContact(1, 0x0A000101);
    bin.addContact(c);
    QVERIFY(bin.hasOnlyLANNodes());

    // Add a public IP contact — 8.8.8.8 = 0x08080808
    auto* c2 = makeContact(2, 0x08080808);
    bin.addContact(c2);
    QVERIFY(!bin.hasOnlyLANNodes());
}

// ---------------------------------------------------------------------------
// GetNumContacts filtering
// ---------------------------------------------------------------------------

void tst_KadRoutingBin::getNumContacts_filtering()
{
    RoutingBin bin;
    auto* c1 = makeContact(1, 0x0A000101);
    c1->setVersion(6);
    auto* c2 = makeContact(2, 0x0A010101);
    c2->setVersion(8);
    bin.addContact(c1);
    bin.addContact(c2);

    uint32 matching = 0, filtered = 0;
    bin.getNumContacts(matching, filtered, 7);
    QCOMPARE(matching, uint32{1});  // c2 (version 8 >= 7)
    QCOMPARE(filtered, uint32{1});  // c1 (version 6 < 7)
}

QTEST_MAIN(tst_KadRoutingBin)
#include "tst_KadRoutingBin.moc"
