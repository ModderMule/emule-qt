/// @file tst_KadNodesData.cpp
/// @brief Integration test — load real nodes.dat files into Kademlia routing table.
///
/// Tests both the v2 format (data/nodes.dat) and the v3 bootstrap format
/// (data/nodes-bootstrap.dat). Copies each file into a temporary directory
/// (simulating the eMule config dir) and verifies that RoutingZone can
/// parse the contacts and populate the routing table.

#include "TestHelpers.h"

#include "kademlia/KadRoutingZone.h"
#include "utils/Log.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadRoutingBin.h"
#include "kademlia/KadUInt128.h"
#include "prefs/Preferences.h"
#include "utils/SafeFile.h"

#include <QFile>
#include <QTest>

using namespace eMule;
using namespace eMule::kad;
using namespace eMule::testing;

class tst_KadNodesData : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void loadNodesDat_fromProjectData();
    void nodesDat_contactsHaveValidProperties();
    void nodesDat_bootstrapFileFormat();

    void loadBootstrapDat_v3_fromProjectData();
    void bootstrapDat_v3_contactsHaveValidProperties();
    void bootstrapDat_v3_fileFormat();

private:
    TempDir* m_tmpDir = nullptr;
};

void tst_KadNodesData::initTestCase()
{
    // Ensure thePrefs is initialized (needed by RoutingZone for IP filter level)
    TempDir tmp;
    thePrefs.load(tmp.filePath(QStringLiteral("prefs_init.yaml")));
}

void tst_KadNodesData::init()
{
    RoutingBin::resetGlobalTracking();
    m_tmpDir = new TempDir();
}

void tst_KadNodesData::cleanup()
{
    RoutingBin::resetGlobalTracking();
    delete m_tmpDir;
    m_tmpDir = nullptr;
}

// ---------------------------------------------------------------------------
// Test: copy data/nodes.dat to temp dir and load it via RoutingZone
// ---------------------------------------------------------------------------

void tst_KadNodesData::loadNodesDat_fromProjectData()
{
    const QString srcPath = projectDataDir() + QStringLiteral("/nodes.dat");
    QVERIFY2(QFile::exists(srcPath),
             qPrintable(QStringLiteral("Missing test fixture: %1").arg(srcPath)));

    const QString dstPath = m_tmpDir->filePath(QStringLiteral("nodes.dat"));
    QVERIFY(QFile::copy(srcPath, dstPath));

    // Create a random local Kad ID — ensures we don't reject contacts matching our ID
    UInt128 localId;
    localId.setValueRandom();

    // RoutingZone constructor reads nodes.dat automatically
    RoutingZone zone(localId, dstPath);

    const uint32 numContacts = zone.getNumContacts();
    logDebug(QStringLiteral("Loaded %1 Kad contacts from nodes.dat").arg(numContacts));

    // The bootstrap nodes.dat contains ~1200 contacts, but the routing table
    // can only hold a limited subset (K-buckets have finite capacity).
    // We expect at least some contacts to survive IP validation and be added.
    QVERIFY2(numContacts > 0,
             qPrintable(QStringLiteral("Expected contacts from nodes.dat, got %1")
                            .arg(numContacts)));
}

// ---------------------------------------------------------------------------
// Test: verify loaded contacts have valid properties
// ---------------------------------------------------------------------------

void tst_KadNodesData::nodesDat_contactsHaveValidProperties()
{
    const QString srcPath = projectDataDir() + QStringLiteral("/nodes.dat");
    const QString dstPath = m_tmpDir->filePath(QStringLiteral("nodes.dat"));
    QVERIFY(QFile::copy(srcPath, dstPath));

    UInt128 localId;
    localId.setValueRandom();
    RoutingZone zone(localId, dstPath);

    ContactArray contacts;
    zone.getAllEntries(contacts);

    QVERIFY(!contacts.empty());

    for (const Contact* c : contacts) {
        QVERIFY(c != nullptr);

        // Every contact must have a non-zero IP
        QVERIFY2(c->getIPAddress() != 0,
                 "Contact has zero IP address");

        // UDP port must be non-zero
        QVERIFY2(c->getUDPPort() != 0,
                 "Contact has zero UDP port");

        // Contact version must be > Kad1 (rejected during loading)
        QVERIFY2(c->getVersion() > KADEMLIA_VERSION1_46c,
                 qPrintable(QStringLiteral("Contact has too-old version: %1")
                                .arg(c->getVersion())));

        // Client ID must not be all-zeros
        QVERIFY(!(c->getClientID() == UInt128(uint32{0})));
    }

    logDebug(QStringLiteral("Validated %1 contacts").arg(contacts.size()));
}

// ---------------------------------------------------------------------------
// Test: verify the raw binary header matches the bootstrap format
// ---------------------------------------------------------------------------

void tst_KadNodesData::nodesDat_bootstrapFileFormat()
{
    const QString srcPath = projectDataDir() + QStringLiteral("/nodes.dat");
    QVERIFY(QFile::exists(srcPath));

    SafeFile sf;
    QVERIFY(sf.open(srcPath, QIODevice::ReadOnly));

    // The data/nodes.dat uses the eMule "newer client" version 2 format:
    //   uint32: 0            (marker — prevents old clients from reading)
    //   uint32: 2            (version)
    //   uint32: numContacts
    //   followed by numContacts * 34 bytes of contact data:
    //     16 bytes KadID + 4 bytes IP + 2 bytes UDP port + 2 bytes TCP port
    //     + 1 byte contact version + 8 bytes KadUDPKey + 1 byte ipVerified

    const uint32 marker = sf.readUInt32();
    QCOMPARE(marker, uint32{0});

    const uint32 version = sf.readUInt32();
    QCOMPARE(version, uint32{2});

    const uint32 numContacts = sf.readUInt32();
    QVERIFY2(numContacts > 0,
             qPrintable(QStringLiteral("Expected contacts in header, got %1")
                            .arg(numContacts)));

    // Verify the file size matches: 12-byte header + numContacts * 34 bytes
    const qint64 expectedSize = 12 + static_cast<qint64>(numContacts) * 34;
    QCOMPARE(sf.length(), expectedSize);

    logDebug(QStringLiteral("nodes.dat v2: %1 contacts, %2 bytes").arg(numContacts).arg(sf.length()));
}

// ---------------------------------------------------------------------------
// Test: load v3 bootstrap nodes-bootstrap.dat via RoutingZone
// ---------------------------------------------------------------------------

void tst_KadNodesData::loadBootstrapDat_v3_fromProjectData()
{
    const QString srcPath = projectDataDir() + QStringLiteral("/nodes-bootstrap.dat");
    QVERIFY2(QFile::exists(srcPath),
             qPrintable(QStringLiteral("Missing test fixture: %1").arg(srcPath)));

    // RoutingZone reads "nodes.dat" — copy the bootstrap file under that name
    const QString dstPath = m_tmpDir->filePath(QStringLiteral("nodes.dat"));
    QVERIFY(QFile::copy(srcPath, dstPath));

    UInt128 localId;
    localId.setValueRandom();

    RoutingZone zone(localId, dstPath);

    const uint32 numContacts = zone.getNumContacts();
    logDebug(QStringLiteral("Loaded %1 Kad contacts from nodes-bootstrap.dat (v3)")
                 .arg(numContacts));

    QVERIFY2(numContacts > 0,
             qPrintable(QStringLiteral("Expected contacts from v3 bootstrap, got %1")
                            .arg(numContacts)));
}

// ---------------------------------------------------------------------------
// Test: verify v3 bootstrap contacts have valid properties
// ---------------------------------------------------------------------------

void tst_KadNodesData::bootstrapDat_v3_contactsHaveValidProperties()
{
    const QString srcPath = projectDataDir() + QStringLiteral("/nodes-bootstrap.dat");
    const QString dstPath = m_tmpDir->filePath(QStringLiteral("nodes.dat"));
    QVERIFY(QFile::copy(srcPath, dstPath));

    UInt128 localId;
    localId.setValueRandom();
    RoutingZone zone(localId, dstPath);

    ContactArray contacts;
    zone.getAllEntries(contacts);

    QVERIFY(!contacts.empty());

    for (const Contact* c : contacts) {
        QVERIFY(c != nullptr);

        QVERIFY2(c->getIPAddress() != 0,
                 "Bootstrap contact has zero IP address");

        QVERIFY2(c->getUDPPort() != 0,
                 "Bootstrap contact has zero UDP port");

        QVERIFY2(c->getVersion() > KADEMLIA_VERSION1_46c,
                 qPrintable(QStringLiteral("Bootstrap contact has too-old version: %1")
                                .arg(c->getVersion())));

        QVERIFY(!(c->getClientID() == UInt128(uint32{0})));
    }

    logDebug(QStringLiteral("Validated %1 v3 bootstrap contacts").arg(contacts.size()));
}

// ---------------------------------------------------------------------------
// Test: verify the raw binary header matches v3 bootstrap format
// ---------------------------------------------------------------------------

void tst_KadNodesData::bootstrapDat_v3_fileFormat()
{
    const QString srcPath = projectDataDir() + QStringLiteral("/nodes-bootstrap.dat");
    QVERIFY(QFile::exists(srcPath));

    SafeFile sf;
    QVERIFY(sf.open(srcPath, QIODevice::ReadOnly));

    // The v3 bootstrap format (edition 1):
    //   uint32: 0            (marker — prevents old clients from reading)
    //   uint32: 3            (version)
    //   uint32: 1            (bootstrap edition)
    //   uint32: numContacts
    //   followed by numContacts * 25 bytes of contact data:
    //     16 bytes KadID + 4 bytes IP + 2 bytes UDP port + 2 bytes TCP port
    //     + 1 byte contact version (no KadUDPKey or ipVerified)

    const uint32 marker = sf.readUInt32();
    QCOMPARE(marker, uint32{0});

    const uint32 version = sf.readUInt32();
    QCOMPARE(version, uint32{3});

    const uint32 edition = sf.readUInt32();
    QCOMPARE(edition, uint32{1});

    const uint32 numContacts = sf.readUInt32();
    QVERIFY2(numContacts > 0,
             qPrintable(QStringLiteral("Expected contacts in v3 header, got %1")
                            .arg(numContacts)));

    // Verify the file size matches: 16-byte header + numContacts * 25 bytes
    const qint64 expectedSize = 16 + static_cast<qint64>(numContacts) * 25;
    QCOMPARE(sf.length(), expectedSize);

    logDebug(QStringLiteral("nodes-bootstrap.dat v3: %1 contacts, %2 bytes")
                 .arg(numContacts).arg(sf.length()));
}

QTEST_MAIN(tst_KadNodesData)
#include "tst_KadNodesData.moc"
