/// @file tst_Preferences.cpp
/// @brief Unit tests for Preferences class (Module 16).

#include "TestHelpers.h"

#include "app/AppConfig.h"
#include "prefs/Preferences.h"
#include "net/EMSocket.h"
#include "net/EncryptedStreamSocket.h"
#include "utils/Opcodes.h"
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
        QCOMPARE(prefs.autoConnect(), true);
        QCOMPARE(prefs.reconnect(), true);
        QCOMPARE(prefs.filterLANIPs(), true);
    }

    void defaults_network()
    {
        Preferences prefs;
        QCOMPARE(prefs.maxConnections(), static_cast<uint16>(500));
        QCOMPARE(prefs.maxHalfConnections(), static_cast<uint16>(50)); // eMule 2026 bandwidth default
        // Default ON: IPv6 peers are a small population and would otherwise be outbid
        // on upload score by the IPv4 majority indefinitely.
        QCOMPARE(prefs.separateIPv6Queue(), true);
    }

    void defaults_bandwidth()
    {
        Preferences prefs;
        QCOMPARE(prefs.maxUpload(), 250u);
        QCOMPARE(prefs.maxDownload(), 500u);
        QCOMPARE(prefs.minUpload(), 1u);
    }

    /// maxUpload() stores "no limit" as 0; every bandwidth path ported from MFC compares
    /// against UNLIMITED instead (srchybrid normalises in SetMaxUpload). maxUploadLimit()
    /// is the bridge, and the whole upload pipeline depends on it: with the raw 0 the
    /// throttler computes a zero byte budget and UploadQueue derives a zero slot cap.
    void maxUploadLimit_mapsZeroToUnlimited()
    {
        Preferences prefs;

        prefs.setMaxUpload(0);
        QCOMPARE(prefs.maxUpload(), 0u);            // raw pref keeps the storage form
        QCOMPARE(prefs.maxUploadLimit(), UNLIMITED); // callers see MFC's sentinel

        prefs.setMaxUpload(250);
        QCOMPARE(prefs.maxUploadLimit(), 250u);      // a real limit passes through as-is

        // Already-UNLIMITED input must survive untouched, so the two spellings of
        // "no limit" converge rather than one of them wrapping to a tiny number.
        prefs.setMaxUpload(UNLIMITED);
        QCOMPARE(prefs.maxUploadLimit(), UNLIMITED);
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
        QCOMPARE(prefs.useOriginalIcons(), true);
    }

    void defaults_upnp()
    {
        Preferences prefs;
        QCOMPARE(prefs.enableUPnP(), true);
        QCOMPARE(prefs.closeUPnPOnExit(), true);
    }

    // -- Config directory override (--config) ---------------------------------

    // --config must redirect every configDir() consumer (server.met, nodes.dat,
    // known.met, ...), not just the preferences.yml lookup in main(). Crucially
    // it must NOT be persisted, or a sandboxed test run would rewrite the user's
    // real preferences.yml to point at the sandbox.
    void configDir_honoursAppConfigOverride()
    {
        TempDir tmp;
        const auto file = tmp.filePath(QStringLiteral("prefs.yaml"));
        const auto realDir = QStringLiteral("/tmp/emuleqt-real-config");
        const auto sandbox = tmp.filePath(QStringLiteral("sandbox"));

        Preferences p;
        p.load(file);
        p.setConfigDir(realDir);
        QCOMPARE(p.configDir(), realDir);

        AppConfig::setConfigDirOverride(sandbox);
        QCOMPARE(p.configDir(), sandbox);   // every consumer now redirected

        p.save();
        AppConfig::setConfigDirOverride(QString());  // process-global static

        // The saved file must still name the user's real directory.
        Preferences p2;
        p2.load(file);
        QCOMPARE(p2.configDir(), realDir);
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
            p1.setSeparateIPv6Queue(false);   // non-default, so a lost key would show
            // One switch per process — both must survive the round trip
            p1.setLogToDiskCore(true);
            p1.setLogToDiskGui(true);
            p1.setMaxLogFileSize(2048);
            p1.setVerbose(true);
            p1.setMaxSourcesPerFile(1000);
            p1.setUseICH(false);
            p1.setStatsSaveInterval(120);
            p1.setStatsLastReset(1700000000);
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
        QCOMPARE(p2.separateIPv6Queue(), false);
        QCOMPARE(p2.logToDiskCore(), true);
        QCOMPARE(p2.logToDiskGui(), true);
        QCOMPARE(p2.maxLogFileSize(), 2048u);
        QCOMPARE(p2.verbose(), true);
        QCOMPARE(p2.maxSourcesPerFile(), static_cast<uint16>(1000));
        QCOMPARE(p2.useICH(), false);
        QCOMPARE(p2.statsSaveInterval(), 120u);
        QCOMPARE(p2.statsLastReset(), uint64{1700000000});
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
        // Strict is an independent hidden option (MFC CryptLayerRequiredStrict), default false —
        // requiring encryption alone must NOT imply strict (that would reject server test callbacks).
        QCOMPARE(cfg.cryptLayerRequiredStrict, false);
        QCOMPARE(cfg.userHash, hash);
        QCOMPARE(cfg.cryptTCPPaddingLength, static_cast<uint8>(200));

        // Strict propagates only when explicitly set.
        prefs.setCryptLayerRequiredStrict(true);
        QCOMPARE(prefs.obfuscationConfig().cryptLayerRequiredStrict, true);
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
        QCOMPARE(prefs.maxConsPerFive(), static_cast<uint16>(40)); // eMule 2026 bandwidth default
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

    // publicIP is no longer a preference — it moved to AppContext as session
    // state that must be re-derived each run. Covered by tst_AppContext.

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
