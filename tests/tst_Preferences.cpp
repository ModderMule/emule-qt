/// @file tst_Preferences.cpp
/// @brief Unit tests for Preferences class (Module 16).

#include "TestHelpers.h"

#include "prefs/Preferences.h"
#include "net/EMSocket.h"
#include "net/EncryptedStreamSocket.h"
#include "utils/OtherFunctions.h"

#include <QFile>
#include <QTest>
#include <QTextStream>

using namespace eMule;
using namespace eMule::testing;

class tst_Preferences : public QObject {
    Q_OBJECT

private slots:
    // -- Defaults -------------------------------------------------------------

    void defaults_general()
    {
        Preferences prefs;
        QCOMPARE(prefs.nick(), QStringLiteral("https://emule-qt.org"));
        QCOMPARE(prefs.autoConnect(), false);
        QCOMPARE(prefs.reconnect(), true);
        QCOMPARE(prefs.filterLANIPs(), true);
    }

    void defaults_network()
    {
        Preferences prefs;
        QCOMPARE(prefs.maxConnections(), static_cast<uint16>(500));
        QCOMPARE(prefs.maxHalfConnections(), static_cast<uint16>(9));
    }

    void defaults_bandwidth()
    {
        Preferences prefs;
        QCOMPARE(prefs.maxUpload(), 250u);
        QCOMPARE(prefs.maxDownload(), 500u);
        QCOMPARE(prefs.minUpload(), 1u);
    }

    void defaults_encryption()
    {
        Preferences prefs;
        QCOMPARE(prefs.cryptLayerSupported(), true);
        QCOMPARE(prefs.cryptLayerRequested(), true);
        QCOMPARE(prefs.cryptLayerRequired(), false);
        QCOMPARE(prefs.cryptTCPPaddingLength(), static_cast<uint8>(128));
    }

    void defaults_display()
    {
        Preferences prefs;
        QCOMPARE(prefs.useOriginalIcons(), false);
    }

    void defaults_upnp()
    {
        Preferences prefs;
        QCOMPARE(prefs.enableUPnP(), true);
        QCOMPARE(prefs.closeUPnPOnExit(), true);
    }

    // -- Load / Save ----------------------------------------------------------

    void loadSave_roundTrip()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("prefs.yaml"));

        // Create prefs with non-default values
        {
            Preferences p1;
            p1.load(tmp.filePath(QStringLiteral("nonexistent.yaml"))); // get defaults + generated hash

            p1.setNick(QStringLiteral("TestUser"));
            p1.setAutoConnect(true);
            p1.setReconnect(false);
            p1.setFilterLANIPs(false);
            p1.setMaxConnections(200);
            p1.setMaxHalfConnections(15);
            p1.setMaxUpload(50);
            p1.setMaxDownload(100);
            p1.setMinUpload(5);
            p1.setCryptLayerSupported(false);
            p1.setCryptLayerRequested(false);
            p1.setCryptLayerRequired(true);
            p1.setCryptTCPPaddingLength(64);
            p1.setProxyType(3);
            p1.setProxyHost(QStringLiteral("proxy.example.com"));
            p1.setProxyPort(9050);
            p1.setProxyEnablePassword(true);
            p1.setProxyUser(QStringLiteral("user1"));
            p1.setProxyPassword(QStringLiteral("pass1"));
            p1.setEnableUPnP(false);
            p1.setCloseUPnPOnExit(false);
            p1.setLogToDisk(true);
            p1.setMaxLogFileSize(2048);
            p1.setVerbose(true);
            p1.setMaxSourcesPerFile(1000);
            p1.setUseICH(false);
            p1.setIncomingDir(QStringLiteral("/tmp/incoming"));
            p1.setTempDirs({QStringLiteral("/tmp/t1"), QStringLiteral("/tmp/t2")});
            p1.setBindAddress(QStringLiteral("192.168.1.100"));

            QVERIFY(p1.saveTo(file));
        }

        // Load into new instance and verify
        Preferences p2;
        QVERIFY(p2.load(file));

        QCOMPARE(p2.nick(), QStringLiteral("TestUser"));
        QCOMPARE(p2.autoConnect(), true);
        QCOMPARE(p2.reconnect(), false);
        QCOMPARE(p2.filterLANIPs(), false);
        QCOMPARE(p2.maxConnections(), static_cast<uint16>(200));
        QCOMPARE(p2.maxHalfConnections(), static_cast<uint16>(15));
        QCOMPARE(p2.maxUpload(), 50u);
        QCOMPARE(p2.maxDownload(), 100u);
        QCOMPARE(p2.minUpload(), 5u);
        QCOMPARE(p2.cryptLayerSupported(), false);
        QCOMPARE(p2.cryptLayerRequested(), false);
        QCOMPARE(p2.cryptLayerRequired(), true);
        QCOMPARE(p2.cryptTCPPaddingLength(), static_cast<uint8>(64));
        QCOMPARE(p2.proxyType(), 3);
        QCOMPARE(p2.proxyHost(), QStringLiteral("proxy.example.com"));
        QCOMPARE(p2.proxyPort(), static_cast<uint16>(9050));
        QCOMPARE(p2.proxyEnablePassword(), true);
        QCOMPARE(p2.proxyUser(), QStringLiteral("user1"));
        QCOMPARE(p2.proxyPassword(), QStringLiteral("pass1"));
        QCOMPARE(p2.enableUPnP(), false);
        QCOMPARE(p2.closeUPnPOnExit(), false);
        QCOMPARE(p2.logToDisk(), true);
        QCOMPARE(p2.maxLogFileSize(), 2048u);
        QCOMPARE(p2.verbose(), true);
        QCOMPARE(p2.maxSourcesPerFile(), static_cast<uint16>(1000));
        QCOMPARE(p2.useICH(), false);
        QCOMPARE(p2.incomingDir(), QStringLiteral("/tmp/incoming"));
        QCOMPARE(p2.tempDirs(), QStringList({QStringLiteral("/tmp/t1"), QStringLiteral("/tmp/t2")}));
        QCOMPARE(p2.bindAddress(), QStringLiteral("192.168.1.100"));
    }

    void load_missingFile()
    {
        TempDir tmp;
        Preferences prefs;
        // Non-existent file → defaults applied, returns true (first run)
        QVERIFY(prefs.load(tmp.filePath(QStringLiteral("does_not_exist.yaml"))));
        QCOMPARE(prefs.nick(), QStringLiteral("https://emule-qt.org"));
        QCOMPARE(prefs.maxConnections(), static_cast<uint16>(500));
        // User hash should be generated
        auto hash = prefs.userHash();
        QVERIFY(!isnulmd4(hash.data()));
    }

    void load_malformedYaml()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("bad.yaml"));

        // Write malformed YAML
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("general:\n  nick: [unterminated\n  bad:: yaml::: {\n");
            f.close();
        }

        Preferences prefs;
        QVERIFY(!prefs.load(file));
        // Should fall back to defaults
        QCOMPARE(prefs.nick(), QStringLiteral("https://emule-qt.org"));
    }

    void load_partialYaml()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("partial.yaml"));

        // Write only general.nick
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("general:\n  nick: \"CustomNick\"\n");
            f.close();
        }

        Preferences prefs;
        QVERIFY(prefs.load(file));
        QCOMPARE(prefs.nick(), QStringLiteral("CustomNick"));
        // Everything else should be defaults
        QCOMPARE(prefs.maxConnections(), static_cast<uint16>(500));
        QCOMPARE(prefs.cryptLayerSupported(), true);
        QCOMPARE(prefs.enableUPnP(), true);
    }

    // -- Validation -----------------------------------------------------------

    void validate_nickTruncation()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("longNick.yaml"));

        // Write a 100-char nick
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QString longNick(100, QLatin1Char('A'));
            f.write(QStringLiteral("general:\n  nick: \"%1\"\n").arg(longNick).toUtf8());
            f.close();
        }

        Preferences prefs;
        QVERIFY(prefs.load(file));
        QCOMPARE(prefs.nick().size(), 50);
    }

    void validate_minUploadClamped()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("v.yaml"));
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("bandwidth:\n  minUpload: 0\n");
            f.close();
        }

        Preferences prefs;
        QVERIFY(prefs.load(file));
        QCOMPARE(prefs.minUpload(), 1u);
    }

    void validate_cryptPaddingClamped()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("v.yaml"));
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("encryption:\n  cryptTCPPaddingLength: 255\n");
            f.close();
        }

        Preferences prefs;
        QVERIFY(prefs.load(file));
        QCOMPARE(prefs.cryptTCPPaddingLength(), static_cast<uint8>(254));
    }

    void validate_proxyTypeClamped()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("v.yaml"));
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("proxy:\n  type: 99\n");
            f.close();
        }

        Preferences prefs;
        QVERIFY(prefs.load(file));
        QCOMPARE(prefs.proxyType(), 0);
    }

    void validate_maxSourcesClamped()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("v.yaml"));
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("files:\n  maxSourcesPerFile: 99999\n");
            f.close();
        }

        Preferences prefs;
        QVERIFY(prefs.load(file));
        QCOMPARE(prefs.maxSourcesPerFile(), static_cast<uint16>(5000));
    }

    // -- Factory methods ------------------------------------------------------

    void factory_obfuscationConfig()
    {
        Preferences prefs;
        prefs.setCryptLayerSupported(true);
        prefs.setCryptLayerRequired(true);
        prefs.setCryptTCPPaddingLength(200);
        auto hash = Preferences::generateUserHash();
        prefs.setUserHash(hash);

        auto cfg = prefs.obfuscationConfig();
        QCOMPARE(cfg.cryptLayerEnabled, true);
        QCOMPARE(cfg.cryptLayerRequired, true);
        QCOMPARE(cfg.cryptLayerRequiredStrict, true);
        QCOMPARE(cfg.userHash, hash);
        QCOMPARE(cfg.cryptTCPPaddingLength, static_cast<uint8>(200));
    }

    void factory_proxySettings()
    {
        Preferences prefs;
        prefs.setProxyType(3);
        prefs.setProxyHost(QStringLiteral("socks.example.com"));
        prefs.setProxyPort(1080);
        prefs.setProxyEnablePassword(true);
        prefs.setProxyUser(QStringLiteral("u"));
        prefs.setProxyPassword(QStringLiteral("p"));

        auto ps = prefs.proxySettings();
        QCOMPARE(ps.useProxy, true);
        QCOMPARE(ps.type, 3);
        QCOMPARE(ps.host, QStringLiteral("socks.example.com"));
        QCOMPARE(ps.port, static_cast<uint16>(1080));
        QCOMPARE(ps.enablePassword, true);
        QCOMPARE(ps.user, QStringLiteral("u"));
        QCOMPARE(ps.password, QStringLiteral("p"));
    }

    void factory_proxySettings_noProxy()
    {
        Preferences prefs;
        auto ps = prefs.proxySettings();
        QCOMPARE(ps.useProxy, false);
        QCOMPARE(ps.type, 0);
    }

    // -- Static utilities -----------------------------------------------------

    void randomPort_inRange()
    {
        for (int i = 0; i < 100; ++i) {
            auto p = Preferences::randomTCPPort();
            QVERIFY2(p >= 4096 && p <= 65095,
                      qPrintable(QStringLiteral("Port %1 out of range").arg(p)));
        }
    }

    void generateUserHash_markers()
    {
        auto hash = Preferences::generateUserHash();
        QCOMPARE(hash[5], static_cast<uint8>(14));
        QCOMPARE(hash[14], static_cast<uint8>(111));
    }

    void userHash_hexRoundTrip()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("hash_rt.yaml"));

        auto origHash = Preferences::generateUserHash();

        {
            Preferences p1;
            p1.setUserHash(origHash);
            QVERIFY(p1.saveTo(file));
        }

        Preferences p2;
        QVERIFY(p2.load(file));
        QCOMPARE(p2.userHash(), origHash);
    }

    // -- Kademlia section -----------------------------------------------------

    void kadEnabled_defaultTrue()
    {
        Preferences prefs;
        QCOMPARE(prefs.kadEnabled(), true);
    }

    void kadUDPKey_generatedOnFirstRun()
    {
        TempDir tmp;
        Preferences prefs;
        prefs.load(tmp.filePath(QStringLiteral("nonexistent_kad.yaml")));
        // After loading a non-existent file, kadUDPKey should be generated (non-zero)
        QVERIFY(prefs.kadUDPKey() != 0);
    }

    void kademlia_roundTrip()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("kad_rt.yaml"));

        {
            Preferences p1;
            p1.load(tmp.filePath(QStringLiteral("nonexistent_for_init.yaml")));
            p1.setKadEnabled(false);
            p1.setKadUDPKey(12345);
            QVERIFY(p1.saveTo(file));
        }

        Preferences p2;
        QVERIFY(p2.load(file));
        QCOMPARE(p2.kadEnabled(), false);
        QCOMPARE(p2.kadUDPKey(), uint32{12345});
    }

    // -- New core settings defaults -------------------------------------------

    void defaults_connection()
    {
        Preferences prefs;
        QCOMPARE(prefs.maxConsPerFive(), static_cast<uint16>(20));
    }

    void defaults_serverExtended()
    {
        Preferences prefs;
        QCOMPARE(prefs.addServersFromClients(), true);
        QCOMPARE(prefs.filterServerByIP(), false);
    }

    void defaults_networkModes()
    {
        Preferences prefs;
        QCOMPARE(prefs.networkED2K(), false);
    }

    void defaults_chat()
    {
        Preferences prefs;
        QCOMPARE(prefs.msgOnlyFriends(), false);
        QCOMPARE(prefs.msgSecure(), false);
        QCOMPARE(prefs.useChatCaptchas(), true);
        QCOMPARE(prefs.enableSpamFilter(), true);
    }

    void defaults_securityExtended()
    {
        Preferences prefs;
        QCOMPARE(prefs.useSecureIdent(), true);
    }

    void defaults_downloadBehavior()
    {
        Preferences prefs;
        QCOMPARE(prefs.autoDownloadPriority(), true);
        QCOMPARE(prefs.addNewFilesPaused(), false);
    }

    void defaults_diskSpace()
    {
        Preferences prefs;
        QCOMPARE(prefs.checkDiskspace(), true);
        QCOMPARE(prefs.minFreeDiskSpace(), uint64{20971520}); // 20 MB
    }

    void defaults_search()
    {
        Preferences prefs;
        QCOMPARE(prefs.enableSearchResultFilter(), true);
    }

    void defaults_publicIP()
    {
        Preferences prefs;
        QCOMPARE(prefs.publicIP(), 0u);
    }

    // -- New core settings round-trip -----------------------------------------

    void newSettings_roundTrip()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("new_prefs_rt.yaml"));

        {
            Preferences p1;
            p1.load(tmp.filePath(QStringLiteral("nonexistent_new.yaml")));

            p1.setMaxConsPerFive(30);
            p1.setAddServersFromClients(false);
            p1.setFilterServerByIP(true);
            p1.setNetworkED2K(false);
            p1.setMsgOnlyFriends(true);
            p1.setMsgSecure(true);
            p1.setUseChatCaptchas(true);
            p1.setEnableSpamFilter(true);
            p1.setUseSecureIdent(false);
            p1.setAutoDownloadPriority(false);
            p1.setAddNewFilesPaused(true);
            p1.setCheckDiskspace(false);
            p1.setMinFreeDiskSpace(104857600); // 100 MB
            p1.setEnableSearchResultFilter(false);
            p1.setPublicIP(0xC0A80101); // 192.168.1.1

            QVERIFY(p1.saveTo(file));
        }

        Preferences p2;
        QVERIFY(p2.load(file));

        QCOMPARE(p2.maxConsPerFive(), static_cast<uint16>(30));
        QCOMPARE(p2.addServersFromClients(), false);
        QCOMPARE(p2.filterServerByIP(), true);
        QCOMPARE(p2.networkED2K(), false);
        QCOMPARE(p2.msgOnlyFriends(), true);
        QCOMPARE(p2.msgSecure(), true);
        QCOMPARE(p2.useChatCaptchas(), true);
        QCOMPARE(p2.enableSpamFilter(), true);
        QCOMPARE(p2.useSecureIdent(), false);
        QCOMPARE(p2.autoDownloadPriority(), false);
        QCOMPARE(p2.addNewFilesPaused(), true);
        QCOMPARE(p2.checkDiskspace(), false);
        QCOMPARE(p2.minFreeDiskSpace(), uint64{104857600});
        QCOMPARE(p2.enableSearchResultFilter(), false);
        QCOMPARE(p2.publicIP(), uint32{0xC0A80101});
    }

    // -- Validation for new settings ------------------------------------------

    void validate_maxConsPerFiveClamped()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("v_con5.yaml"));
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("network:\n  maxConsPerFive: 999\n");
            f.close();
        }

        Preferences prefs;
        QVERIFY(prefs.load(file));
        QCOMPARE(prefs.maxConsPerFive(), static_cast<uint16>(50));
    }

    void validate_maxConsPerFiveMinClamped()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("v_con5min.yaml"));
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("network:\n  maxConsPerFive: 0\n");
            f.close();
        }

        Preferences prefs;
        QVERIFY(prefs.load(file));
        QCOMPARE(prefs.maxConsPerFive(), static_cast<uint16>(1));
    }
};

QTEST_MAIN(tst_Preferences)
#include "tst_Preferences.moc"
