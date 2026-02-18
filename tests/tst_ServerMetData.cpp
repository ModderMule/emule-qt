/// @file tst_ServerMetData.cpp
/// @brief Integration test — load real data/server.met into ServerList.
///
/// Copies the project-level data/server.met into a temporary directory
/// (simulating the eMule config dir) and verifies that ServerList can
/// parse the binary file and populate server entries.

#include "TestHelpers.h"
#include "server/ServerList.h"
#include "utils/Log.h"
#include "server/Server.h"

#include <QFile>
#include <QTest>

using namespace eMule;
using namespace eMule::testing;

class tst_ServerMetData : public QObject {
    Q_OBJECT

private slots:
    void loadServerMet_fromProjectData();
    void serverMet_hasExpectedServers();
    void serverMet_serverProperties();
};

// ---------------------------------------------------------------------------
// Test: copy data/server.met to temp dir and load it
// ---------------------------------------------------------------------------

void tst_ServerMetData::loadServerMet_fromProjectData()
{
    const QString srcPath = projectDataDir() + QStringLiteral("/server.met");
    QVERIFY2(QFile::exists(srcPath),
             qPrintable(QStringLiteral("Missing test fixture: %1").arg(srcPath)));

    TempDir configDir;
    const QString dstPath = configDir.filePath(QStringLiteral("server.met"));
    QVERIFY(QFile::copy(srcPath, dstPath));

    ServerList list;
    QVERIFY(list.loadServerMet(dstPath));
    QVERIFY(list.serverCount() > 0);

    logDebug(QStringLiteral("Loaded %1 servers from server.met").arg(list.serverCount()));
}

// ---------------------------------------------------------------------------
// Test: verify the file contains known server entries
// ---------------------------------------------------------------------------

void tst_ServerMetData::serverMet_hasExpectedServers()
{
    const QString srcPath = projectDataDir() + QStringLiteral("/server.met");
    TempDir configDir;
    const QString dstPath = configDir.filePath(QStringLiteral("server.met"));
    QVERIFY(QFile::copy(srcPath, dstPath));

    ServerList list;
    QVERIFY(list.loadServerMet(dstPath));

    // The data/server.met header says 10 servers (0x0A in bytes 1-4).
    // Some may be rejected due to bad IPs (private/reserved), but at least
    // one must survive — the file contains well-known public servers.
    QVERIFY2(list.serverCount() >= 1,
             qPrintable(QStringLiteral("Expected at least 1 server, got %1")
                            .arg(list.serverCount())));

    // Look for a known server name from the fixture file
    bool foundKnown = false;
    for (size_t i = 0; i < list.serverCount(); ++i) {
        const Server* srv = list.serverAt(i);
        QVERIFY(srv != nullptr);
        QVERIFY(srv->port() != 0);

        if (srv->name().contains(QStringLiteral("eMule Security"), Qt::CaseInsensitive)
            || srv->name().contains(QStringLiteral("Astra"), Qt::CaseInsensitive)
            || srv->name().contains(QStringLiteral("Sharing"), Qt::CaseInsensitive)) {
            foundKnown = true;
        }

        logDebug(QStringLiteral("  Server: %1 address: %2 port: %3").arg(srv->name(), srv->address()).arg(srv->port()));
    }
    QVERIFY2(foundKnown, "Expected to find at least one known server name");
}

// ---------------------------------------------------------------------------
// Test: verify parsed server properties are plausible
// ---------------------------------------------------------------------------

void tst_ServerMetData::serverMet_serverProperties()
{
    const QString srcPath = projectDataDir() + QStringLiteral("/server.met");
    TempDir configDir;
    const QString dstPath = configDir.filePath(QStringLiteral("server.met"));
    QVERIFY(QFile::copy(srcPath, dstPath));

    ServerList list;
    QVERIFY(list.loadServerMet(dstPath));

    for (size_t i = 0; i < list.serverCount(); ++i) {
        const Server* srv = list.serverAt(i);
        QVERIFY(srv != nullptr);

        // Every server must have a non-empty name (loadServerMet sets name
        // from address if the tag is missing)
        QVERIFY2(!srv->name().isEmpty(),
                 qPrintable(QStringLiteral("Server %1 has empty name").arg(i)));

        // Port must be non-zero
        QVERIFY(srv->port() > 0);

        // Either a numeric IP or a dynIP must be set
        QVERIFY(srv->ip() != 0 || srv->hasDynIP());
    }
}

QTEST_GUILESS_MAIN(tst_ServerMetData)
#include "tst_ServerMetData.moc"
