/// @file tst_LegacyConfigImport.cpp
/// @brief Tests for prefs/LegacyConfigImport — wizard-requested import of an eMule / aMule config dir.

#include "TestHelpers.h"
#include "prefs/DownloadCategory.h"
#include "prefs/LegacyConfigImport.h"
#include "prefs/Preferences.h"

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTest>

using namespace eMule;
using namespace eMule::testing;

namespace {

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(f.write(data), data.size());
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/// The way the Win32 profile API stores a Unicode INI: UTF-16LE, BOM, CRLF.
QByteArray utf16(const QString& text)
{
    QString crlf = text;
    crlf.replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    QByteArray out("\xFF\xFE", 2);
    out.append(reinterpret_cast<const char*>(crlf.utf16()), crlf.size() * 2);
    return out;
}

/// An MFC install laid out in a temp dir: config/, Incoming/, Temp/, a shared folder.
struct MfcInstall {
    TempDir root;
    QString config;
    QString incoming;
    QString temp;
    QString shared;
    QString catDir;
    QString missing;      ///< never created
    QString uncreatable;  ///< below a regular file, so mkpath fails on every OS

    MfcInstall()
        : config(root.filePath(QStringLiteral("config")))
        , incoming(root.filePath(QStringLiteral("Incoming")))
        , temp(root.filePath(QStringLiteral("Temp")))
        , shared(root.filePath(QStringLiteral("Müsic")))
        , catDir(root.filePath(QStringLiteral("Films")))
        , missing(root.filePath(QStringLiteral("Gone")))
        , uncreatable(root.filePath(QStringLiteral("blocker/dir")))
    {
        for (const QString& dir : {config, incoming, temp, shared, catDir})
            QDir().mkpath(dir);
        // Not a fake absolute path: Windows lets a user create C:/no/such/dir
        QFile blocker(root.filePath(QStringLiteral("blocker")));
        if (blocker.open(QIODevice::WriteOnly))
            blocker.write("file, not a dir");
    }

    [[nodiscard]] QString file(const QString& name) const { return config + u'/' + name; }

    /// MFC writes folders with a trailing separator.
    [[nodiscard]] QString prefsIni() const
    {
        return QStringLiteral(
                   "[eMule]\n"
                   "AppVersion=0.50a\n"
                   "Nick=OldMule\n"
                   "IncomingDir=%1/\n"
                   "TempDir=%2/\n"
                   "TempDirs=%2/|%3/\n"
                   "DownloadCapacity=2000\n"
                   "UploadCapacityNew=500\n"
                   "MaxUpload=80\n"
                   "MaxDownload=-1\n"
                   "MaxConnections=450\n"
                   "MaxSourcesPerFile=600\n"
                   "Port=41234\n"
                   "UDPPort=41235\n"
                   "Autoconnect=1\n"
                   "Reconnect=0\n"
                   "NetworkKademlia=1\n"
                   "NetworkED2K=0\n"
                   "CryptLayerRequested=0\n"
                   "CryptLayerSupported=0\n"
                   "CryptLayerRequired=0\n"
                   "\n"
                   "[Scheduler]\n"
                   "Count=1\n"
                   "[Schedule#0]\n"
                   "Title=night\n"
                   "[WebServer]\n"
                   "Port=4711\n")
            .arg(incoming, temp, missing);
    }
};

/// Fresh eMule Qt profile plus a request pointing at @p source.
struct Profile {
    TempDir dir;
    QString request;
    Preferences prefs;

    explicit Profile(const QString& source)
        : request(LegacyConfigImport::requestFilePath(dir.path()))
    {
        prefs.load(dir.filePath(QStringLiteral("preferences.yml")));
        QVERIFY(prefs.save());
        // Hand-written form: kind and parts default to eMule, on
        writeFile(request, "source: '" + source.toUtf8() + "'\n");
    }

    [[nodiscard]] QString file(const QString& name) const { return dir.filePath(name); }

    [[nodiscard]] LegacyConfigImport::Outcome run()
    {
        return LegacyConfigImport::run(dir.path(), prefs);
    }
};

/// An aMule config dir: amule.conf, Temp/ inside it, an incoming and a shared dir.
struct AmuleInstall {
    TempDir root;
    QString config;
    QString incoming;
    QString temp;
    QString shared;

    AmuleInstall()
        : config(root.filePath(QStringLiteral(".aMule")))
        , incoming(root.filePath(QStringLiteral("Downloads")))
        , temp(root.filePath(QStringLiteral(".aMule/Temp")))
        , shared(root.filePath(QStringLiteral("Bücher")))
    {
        for (const QString& dir : {config, incoming, temp, shared})
            QDir().mkpath(dir);
    }

    [[nodiscard]] QString file(const QString& name) const { return config + u'/' + name; }

    /// wxFileConfig: UTF-8, backslash escaped, the default category not written.
    [[nodiscard]] QByteArray conf() const
    {
        return QStringLiteral(
                   "[eMule]\n"
                   "Nick=Back\\\\slash \\\"Mule\\\"\n"
                   "Port=4662\n"
                   "UDPPort=4672\n"
                   "UDPEnable=0\n"
                   "MaxUpload=0\n"
                   "MaxDownload=120\n"
                   "DownloadCapacity=3000\n"
                   "UploadCapacity=400\n"
                   "MaxConnections=300\n"
                   "MaxSourcesPerFile=250\n"
                   "Autoconnect=1\n"
                   "Reconnect=1\n"
                   "ConnectToKad=0\n"
                   "ConnectToED2K=1\n"
                   "IncomingDir=%1\n"
                   "TempDir=%2\n"
                   "[General]\n"
                   "Count=2\n"
                   "[Cat#1]\n"
                   "Title=Bücher\n"
                   "Incoming=%3\n"
                   "Comment=ebooks\n"
                   "[Cat#2]\n"
                   "Title=\n")
            .arg(incoming, temp, shared)
            .toUtf8();
    }
};

} // namespace

class tst_LegacyConfigImport : public QObject {
    Q_OBJECT

private slots:
    void noRequest_doesNothing();
    void import_copiesDataFilesAndMapsPreferences();
    void import_leavesEncryptionAlone();
    void import_readsAnsiIniWithUtf8NickAndUnlimited();
    void import_categoriesAndSharedDirs();
    void import_identityNeedsBothFiles();
    void import_noHashTakesDerivedOne();
    void import_carriesSchedulesOnly();
    void import_requestIsConsumed();
    void import_expandsEnvironmentInTheRequest();
    void import_skipsMfcDefaultNick();
    void usedProfile_isBackedUp();
    void missingSource_isRecordedAndSkipped();
    void pendingTempDirs_areHandedOutOnce();
    void request_withoutParts_queuesNothing();
    void request_chosenTempDirsWin();
    void amule_confCategoriesAndSharedDirs();
    void discover_windowsLayout();
    void discover_amuleAndWine();
    void probe_acceptsParentAndPrefix();
};

void tst_LegacyConfigImport::noRequest_doesNothing()
{
    TempDir tmp;
    Preferences prefs;
    prefs.load(tmp.filePath(QStringLiteral("preferences.yml")));

    const auto out = LegacyConfigImport::run(tmp.path(), prefs);
    QCOMPARE(out.result, LegacyConfigImport::Result::NoRequest);
    QVERIFY(!QFile::exists(tmp.filePath(QLatin1StringView(LegacyConfigImport::kStateFileName))));
    QVERIFY(LegacyConfigImport::takePendingTempDirs(tmp.path()).isEmpty());
}

void tst_LegacyConfigImport::import_copiesDataFilesAndMapsPreferences()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));
    writeFile(mfc.file(QStringLiteral("known.met")), "known");
    writeFile(mfc.file(QStringLiteral("clients.met")), "credits");
    writeFile(mfc.file(QStringLiteral("server.met")), "mfc servers");
    writeFile(mfc.file(QStringLiteral("emfriends.met")), "friends");
    writeFile(mfc.file(QStringLiteral("statistics.ini")), "not imported");

    Profile profile(mfc.config);
    // What bundle seeding put there before the daemon got this far
    writeFile(profile.file(QStringLiteral("server.met")), "seeded servers");

    const auto out = profile.run();
    QCOMPARE(out.result, LegacyConfigImport::Result::Imported);
    QCOMPARE(out.filesCopied, 4);
    QCOMPARE(out.filesBackedUp, 1);   // the seeded server.met
    QVERIFY(!QFile::exists(profile.request));
    QCOMPARE(readFile(profile.file(QStringLiteral("known.met"))), QByteArrayLiteral("known"));
    QCOMPARE(readFile(profile.file(QStringLiteral("clients.met"))), QByteArrayLiteral("credits"));
    QCOMPARE(readFile(profile.file(QStringLiteral("server.met"))), QByteArrayLiteral("mfc servers"));
    QVERIFY(!QFile::exists(profile.file(QStringLiteral("statistics.ini"))));

    const Preferences& p = profile.prefs;
    QCOMPARE(p.nick(), QStringLiteral("OldMule"));
    QCOMPARE(p.port(), uint16{41234});
    QCOMPARE(p.udpPort(), uint16{41234});   // one port: UDPPort dropped
    QCOMPARE(p.ed3kUdpPort(), uint16{41234});
    QCOMPARE(p.maxGraphDownloadRate(), 2000u);
    QCOMPARE(p.maxGraphUploadRate(), 500u);
    QCOMPARE(p.maxUpload(), 80u);
    QCOMPARE(p.maxDownload(), 0u);   // MFC UNLIMITED
    QCOMPARE(p.maxConnections(), uint16{450});
    QCOMPARE(p.maxSourcesPerFile(), uint16{600});
    QVERIFY(p.autoConnect());
    QVERIFY(!p.reconnect());
    QVERIFY(p.kadEnabled());
    QVERIFY(!p.networkED2K());
    QCOMPARE(p.incomingDir(), mfc.incoming);

    // The old temp dir is queued for the converter, never adopted
    QCOMPARE(out.pendingTempDirs, QStringList{mfc.temp});
    QVERIFY(!p.tempDirs().contains(mfc.temp));

    // Saved, not only set
    Preferences reloaded;
    QVERIFY(reloaded.load(profile.file(QStringLiteral("preferences.yml"))));
    QCOMPARE(reloaded.nick(), QStringLiteral("OldMule"));
    QCOMPARE(reloaded.port(), uint16{41234});

    // The source is only read
    QCOMPARE(readFile(mfc.file(QStringLiteral("known.met"))), QByteArrayLiteral("known"));
    QCOMPARE(readFile(mfc.file(QStringLiteral("preferences.ini"))), utf16(mfc.prefsIni()));
}

void tst_LegacyConfigImport::import_leavesEncryptionAlone()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));

    Profile profile(mfc.config);
    const bool supported = profile.prefs.cryptLayerSupported();
    const bool requested = profile.prefs.cryptLayerRequested();
    const bool required = profile.prefs.cryptLayerRequired();

    QCOMPARE(profile.run().result, LegacyConfigImport::Result::Imported);
    QCOMPARE(profile.prefs.cryptLayerSupported(), supported);
    QCOMPARE(profile.prefs.cryptLayerRequested(), requested);
    QCOMPARE(profile.prefs.cryptLayerRequired(), required);
}

void tst_LegacyConfigImport::import_readsAnsiIniWithUtf8NickAndUnlimited()
{
    // Raw bytes, not QSettings: the nick is UTF-8 inside an otherwise ANSI file,
    // keys differ in case, and one value is quoted.
    MfcInstall mfc;
    const QByteArray ini = "; written by an old build\r\n"
                           "[EMULE]\r\n"
                           "nick=J\xC3\xBCrgen, der Esel\r\n"
                           "MAXUPLOAD=65535\r\n"
                           "MaxDownload=\"300\"\r\n"
                           "UDPPort=0\r\n"
                           "Port=0\r\n";
    writeFile(mfc.file(QStringLiteral("preferences.ini")), ini);

    Profile profile(mfc.config);
    const uint16 portBefore = profile.prefs.port();

    QCOMPARE(profile.run().result, LegacyConfigImport::Result::Imported);
    QCOMPARE(profile.prefs.nick(), QStringLiteral("Jürgen, der Esel"));
    QCOMPARE(profile.prefs.maxUpload(), 0u);          // old 0xFFFF UNLIMITED
    QCOMPARE(profile.prefs.maxDownload(), 300u);
    QCOMPARE(profile.prefs.port(), portBefore);       // 0 = "pick one", ours stays
    QCOMPARE(profile.prefs.udpPort(), portBefore);    // one port, never UDP off
}

void tst_LegacyConfigImport::import_categoriesAndSharedDirs()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));
    // Category.ini goes through the ANSI API: UTF-8 values in a byte file
    writeFile(mfc.file(QStringLiteral("Category.ini")),
              QStringLiteral("[General]\r\nCount=2\r\n"
                             "[Cat#0]\r\nTitle=\r\nFilter=3\r\n"
                             "[Cat#1]\r\nTitle=Filme für alle\r\nIncoming=%1/\r\n"
                             "Comment=hd, uncut\r\nColor=255\r\na4afPriority=2\r\n"
                             "AutoCat=*.mkv|*.avi\r\nCare4All=1\r\n"
                             "[Cat#2]\r\nTitle=Music\r\nIncoming=%2/\r\nColor=-1\r\n")
                  .arg(mfc.catDir, mfc.uncreatable).toUtf8());
    writeFile(mfc.file(QStringLiteral("shareddir.dat")),
              utf16(mfc.shared + QStringLiteral("/\n") + mfc.incoming
                    + QStringLiteral("/\n") + mfc.missing + QStringLiteral("/\n") + mfc.shared + QStringLiteral("\n")));

    Profile profile(mfc.config);
    const auto out = profile.run();
    QCOMPARE(out.result, LegacyConfigImport::Result::Imported);

    QCOMPARE(out.categories, 2);
    const auto cats = profile.prefs.categories();
    QCOMPARE(cats.size(), 3);
    QCOMPARE(cats[0].filter, 3);
    QVERIFY(cats[0].incomingPath.isEmpty());
    QCOMPARE(cats[1].title, QStringLiteral("Filme für alle"));
    QCOMPARE(cats[1].incomingPath, mfc.catDir);
    QCOMPARE(cats[1].comment, QStringLiteral("hd, uncut"));
    QCOMPARE(cats[1].color, quint32{255});
    QCOMPARE(cats[1].prio, quint8{2});
    QCOMPARE(cats[1].autocat, QStringLiteral("*.mkv|*.avi"));
    QVERIFY(cats[1].care4all);
    QCOMPARE(cats[2].title, QStringLiteral("Music"));
    QCOMPARE(cats[2].color, kCategoryColorAuto);
    // Its folder is gone: back to the global incoming dir
    QCOMPARE(profile.prefs.incomingDirForCategory(2), mfc.incoming);

    // Missing folders, the incoming dir and the duplicate are dropped
    QCOMPARE(out.sharedDirs, 1);
    QCOMPARE(profile.prefs.sharedDirs(), QStringList{mfc.shared});
}

void tst_LegacyConfigImport::import_identityNeedsBothFiles()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));
    writeFile(mfc.file(QStringLiteral("cryptkey.dat")), "mfc key");

    // A key without its hash would only replace ours for nothing
    Profile lonelyKey(mfc.config);
    QCOMPARE(lonelyKey.run().result, LegacyConfigImport::Result::Imported);
    QVERIFY(!QFile::exists(lonelyKey.file(QStringLiteral("cryptkey.dat"))));

    writeFile(mfc.file(QStringLiteral("preferences.dat")), "mfc hash");
    Profile both(mfc.config);
    QCOMPARE(both.run().result, LegacyConfigImport::Result::Imported);
    QCOMPARE(readFile(both.file(QStringLiteral("cryptkey.dat"))), QByteArrayLiteral("mfc key"));
    QCOMPARE(readFile(both.file(QStringLiteral("preferences.dat"))), QByteArrayLiteral("mfc hash"));
}

void tst_LegacyConfigImport::import_noHashTakesDerivedOne()
{
    std::array<uint8, 16> derived{};
    derived.fill(0x42);
    Preferences::setUserHashSource([derived] { return std::optional(derived); });
    const auto reset = qScopeGuard([] { Preferences::setUserHashSource({}); });

    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));

    // No hash in the source: ours becomes the ed3k-derived one (eD2K compat)
    Profile none(mfc.config);
    none.prefs.setUserHash(Preferences::generateUserHash());
    const auto outNone = none.run();
    QVERIFY(outNone.userHashDerived);
    QVERIFY(none.prefs.userHash() == derived);

    // A hash pair in the source: left to LegacyIdentityImport (credits)
    writeFile(mfc.file(QStringLiteral("preferences.dat")), "mfc hash");
    writeFile(mfc.file(QStringLiteral("cryptkey.dat")), "mfc key");
    Profile both(mfc.config);
    const auto before = Preferences::generateUserHash();
    both.prefs.setUserHash(before);
    QVERIFY(!both.run().userHashDerived);
    QVERIFY(both.prefs.userHash() == before);
}

void tst_LegacyConfigImport::import_carriesSchedulesOnly()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));

    Profile profile(mfc.config);
    QCOMPARE(profile.run().result, LegacyConfigImport::Result::Imported);

    // Passwords and the rest of the MFC ini stay behind
    const QByteArray ini = readFile(profile.file(QStringLiteral("preferences.ini")));
    QVERIFY(ini.contains("[Scheduler]"));
    QVERIFY(ini.contains("[Schedule#0]"));
    QVERIFY(ini.contains("Title=night"));
    QVERIFY(!ini.contains("[eMule]"));
    QVERIFY(!ini.contains("[WebServer]"));
}

void tst_LegacyConfigImport::import_requestIsConsumed()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));

    Profile profile(mfc.config);
    QCOMPARE(profile.run().result, LegacyConfigImport::Result::Imported);

    // Next start: nothing asked for, nothing changed
    profile.prefs.setNick(QStringLiteral("renamed since"));
    QCOMPARE(profile.run().result, LegacyConfigImport::Result::NoRequest);
    QCOMPARE(profile.prefs.nick(), QStringLiteral("renamed since"));
}

void tst_LegacyConfigImport::import_expandsEnvironmentInTheRequest()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));
    qputenv("EMULE_TST_APPDATA", mfc.root.path().toUtf8());

    Profile profile(QStringLiteral("%EMULE_TST_APPDATA%/config"));
    // Hand-written in Notepad: UTF-8 with BOM, CRLF
    writeFile(profile.request, "\xEF\xBB\xBF# Written by hand\r\n"
                               "source: '%EMULE_TST_APPDATA%/config'\r\n");
    const auto out = profile.run();
    qunsetenv("EMULE_TST_APPDATA");

    QCOMPARE(out.result, LegacyConfigImport::Result::Imported);
    QCOMPARE(out.sourceDir, mfc.config);
}

// New install from an MFC profile: its homepage-URL nick is never taken over
void tst_LegacyConfigImport::import_skipsMfcDefaultNick()
{
    for (const QString& mfcNick : {QStringLiteral("https://emule-project.net"),
                                   QStringLiteral("http://emule-project.net"),
                                   QStringLiteral("https://www12.emule-project.net"),
                                   QStringLiteral("https://emule.sf.net")}) {
        MfcInstall mfc;
        QString ini = mfc.prefsIni();
        ini.replace(QStringLiteral("Nick=OldMule"), QStringLiteral("Nick=") + mfcNick);
        writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(ini));

        Profile profile(mfc.config);
        QCOMPARE(profile.run().result, LegacyConfigImport::Result::Imported);
        QCOMPARE(profile.prefs.nick(), Preferences::defaultNick());
    }
}

// Asked for on a used profile (wizard reopened from Tools): ours is kept aside
void tst_LegacyConfigImport::usedProfile_isBackedUp()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));
    writeFile(mfc.file(QStringLiteral("known.met")), "mfc known");

    Profile profile(mfc.config);
    writeFile(profile.file(QStringLiteral("known.met")), "ours");
    writeFile(profile.file(QStringLiteral("clients.met")), "our credits");
    writeFile(profile.file(QStringLiteral("preferences.yml")), "our prefs");
    writeFile(profile.file(QStringLiteral("uistate.yml")), "our layout");

    const auto out = profile.run();
    QCOMPARE(out.result, LegacyConfigImport::Result::Imported);
    QCOMPARE(readFile(profile.file(QStringLiteral("known.met"))), QByteArrayLiteral("mfc known"));
    QCOMPARE(out.filesBackedUp, 1);
    QCOMPARE(readFile(QDir(out.backupDir).filePath(QStringLiteral("known.met"))),
             QByteArrayLiteral("ours"));
    // Not in the source: left where it is
    QCOMPARE(readFile(profile.file(QStringLiteral("clients.met"))), QByteArrayLiteral("our credits"));
    // Settings as they were before the import: copied, the import then edits ours
    QCOMPARE(out.settingsBackedUp, 2);
    QCOMPARE(readFile(QDir(out.backupDir).filePath(QStringLiteral("preferences.yml"))),
             QByteArrayLiteral("our prefs"));
    QCOMPARE(readFile(QDir(out.backupDir).filePath(QStringLiteral("uistate.yml"))),
             QByteArrayLiteral("our layout"));
    QCOMPARE(readFile(profile.file(QStringLiteral("uistate.yml"))), QByteArrayLiteral("our layout"));
}

void tst_LegacyConfigImport::missingSource_isRecordedAndSkipped()
{
    TempDir empty;
    Profile profile(empty.path());
    const QString nick = profile.prefs.nick();

    QCOMPARE(profile.run().result, LegacyConfigImport::Result::SourceMissing);
    QCOMPARE(profile.prefs.nick(), nick);
    QCOMPARE(profile.run().result, LegacyConfigImport::Result::NoRequest);
}

void tst_LegacyConfigImport::pendingTempDirs_areHandedOutOnce()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));

    Profile profile(mfc.config);
    QCOMPARE(profile.run().result, LegacyConfigImport::Result::Imported);

    QCOMPARE(LegacyConfigImport::takePendingTempDirs(profile.dir.path()), QStringList{mfc.temp});
    QVERIFY(LegacyConfigImport::takePendingTempDirs(profile.dir.path()).isEmpty());
    QCOMPARE(profile.run().result, LegacyConfigImport::Result::NoRequest);
}

void tst_LegacyConfigImport::request_withoutParts_queuesNothing()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));
    const auto src = LegacyConfigImport::probe(mfc.config);
    QVERIFY(src);

    Profile profile(mfc.config);
    QVERIFY(LegacyConfigImport::writeRequest(profile.dir.path(), *src, /*parts=*/false));
    const auto out = profile.run();
    QCOMPARE(out.result, LegacyConfigImport::Result::Imported);
    QVERIFY(out.pendingTempDirs.isEmpty());
    QCOMPARE(profile.prefs.nick(), QStringLiteral("OldMule"));
    QVERIFY(LegacyConfigImport::takePendingTempDirs(profile.dir.path()).isEmpty());
}

// Download folders edited in the wizard replace the detected ones
void tst_LegacyConfigImport::request_chosenTempDirsWin()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));
    auto src = LegacyConfigImport::probe(mfc.config);
    QVERIFY(src);
    src->tempDirs = {mfc.shared, mfc.missing};

    Profile profile(mfc.config);
    QVERIFY(LegacyConfigImport::writeRequest(profile.dir.path(), *src, /*parts=*/true));
    const auto out = profile.run();
    QCOMPARE(out.result, LegacyConfigImport::Result::Imported);
    // A folder gone by now is dropped
    QCOMPARE(out.pendingTempDirs, QStringList{mfc.shared});
}

void tst_LegacyConfigImport::amule_confCategoriesAndSharedDirs()
{
    AmuleInstall am;
    writeFile(am.file(QStringLiteral("amule.conf")), am.conf());
    writeFile(am.file(QStringLiteral("shareddir.dat")), (am.shared + QStringLiteral("\n")).toUtf8());
    writeFile(am.file(QStringLiteral("known.met")), "amule known");
    writeFile(am.file(QStringLiteral("preferences.dat")), "amule hash");
    writeFile(am.file(QStringLiteral("cryptkey.dat")), "amule key");
    writeFile(am.temp + QStringLiteral("/001.part.met"), "part");
    writeFile(am.temp + QStringLiteral("/001.part"), "data");

    const auto src = LegacyConfigImport::probe(am.config);
    QVERIFY(src);
    QCOMPARE(src->kind, LegacyConfigImport::SourceKind::Amule);
    QCOMPARE(src->tempDirs, QStringList{am.temp});
    QCOMPARE(src->partFiles, 1);

    Profile profile(QString{});
    QVERIFY(LegacyConfigImport::writeRequest(profile.dir.path(), *src, /*parts=*/true));
    const auto out = profile.run();
    QCOMPARE(out.result, LegacyConfigImport::Result::Imported);
    QCOMPARE(out.filesCopied, 3);
    QCOMPARE(readFile(profile.file(QStringLiteral("known.met"))), QByteArrayLiteral("amule known"));

    const Preferences& p = profile.prefs;
    QCOMPARE(p.nick(), QStringLiteral("Back\\slash \"Mule\""));
    QCOMPARE(p.port(), uint16{4662});
    QCOMPARE(p.udpPort(), uint16{4662});   // UDPEnable=0 dropped: one port
    QCOMPARE(p.maxUpload(), 0u);
    QCOMPARE(p.maxDownload(), 120u);
    QCOMPARE(p.maxGraphDownloadRate(), 3000u);
    QCOMPARE(p.maxGraphUploadRate(), 400u);
    QCOMPARE(p.maxConnections(), uint16{300});
    QVERIFY(!p.kadEnabled());
    QVERIFY(p.networkED2K());
    QCOMPARE(p.incomingDir(), am.incoming);

    // Cat#2 has no title: aMule skips it as well
    QCOMPARE(out.categories, 1);
    const auto cats = p.categories();
    QCOMPARE(cats.size(), 2);
    QCOMPARE(cats[1].title, QStringLiteral("Bücher"));
    QCOMPARE(cats[1].incomingPath, am.shared);
    QCOMPARE(cats[1].comment, QStringLiteral("ebooks"));

    QCOMPARE(out.pendingTempDirs, QStringList{am.temp});
}

void tst_LegacyConfigImport::discover_windowsLayout()
{
    TempDir localAppData;   // %LOCALAPPDATA%/eMule/config
    MfcInstall program;     // Program Files/eMule, found through the ed2k handler
    AmuleInstall am;
    TempDir appData;        // %APPDATA%/aMule
    const QString local = localAppData.filePath(QStringLiteral("eMule/config"));
    writeFile(local + QStringLiteral("/preferences.ini"), utf16(QStringLiteral("[eMule]\nNick=Local\n")));
    writeFile(program.file(QStringLiteral("preferences.ini")), utf16(program.prefsIni()));
    writeFile(appData.filePath(QStringLiteral("aMule/amule.conf")), am.conf());

    LegacyConfigImport::SearchRoots roots;
    roots.windowsLayout = true;
    roots.localAppData = localAppData.path();
    roots.emuleExeDirs = {program.root.path()};
    roots.appData = appData.path();

    const auto found = LegacyConfigImport::discover(roots);
    QCOMPARE(found.size(), 3);
    QCOMPARE(found[0].kind, LegacyConfigImport::SourceKind::Emule);
    QCOMPARE(found[0].configDir, QFileInfo(local).canonicalFilePath());
    QCOMPARE(found[0].nick, QStringLiteral("Local"));
    QCOMPARE(found[1].configDir, QFileInfo(program.config).canonicalFilePath());
    QCOMPARE(found[1].nick, QStringLiteral("OldMule"));
    QCOMPARE(found[2].kind, LegacyConfigImport::SourceKind::Amule);
}

void tst_LegacyConfigImport::discover_amuleAndWine()
{
#ifdef Q_OS_WIN
    QSKIP("Unix layout");
#else
    TempDir home;
    // ~/.aMule
    const QString amule = home.filePath(QStringLiteral(".aMule"));
    writeFile(amule + QStringLiteral("/amule.conf"),
              QStringLiteral("[eMule]\nNick=Linux\nTempDir=%1/Temp\n").arg(amule).toUtf8());
    writeFile(amule + QStringLiteral("/Temp/1.part.met"), "x");

    // ~/.wine with eMule per user, C: through dosdevices
    const QString prefix = home.filePath(QStringLiteral(".wine"));
    const QString user = prefix + QStringLiteral("/drive_c/users/bob");
    QDir().mkpath(prefix + QStringLiteral("/dosdevices"));
    QVERIFY(QFile::link(QStringLiteral("../drive_c"), prefix + QStringLiteral("/dosdevices/c:")));
    writeFile(user + QStringLiteral("/AppData/Local/eMule/config/preferences.ini"),
              utf16(QStringLiteral("[eMule]\nNick=Wino\nTempDir=C:\\users\\bob\\Temp\\\n"
                                   "IncomingDir=C:\\users\\bob\\Incoming\\\n")));
    writeFile(user + QStringLiteral("/Temp/2.part.met"), "y");
    writeFile(user + QStringLiteral("/Temp/3.part.met"), "z");

    LegacyConfigImport::SearchRoots roots;
    roots.unixLayout = true;
    roots.home = home.path();
    const auto found = LegacyConfigImport::discover(roots);
    QCOMPARE(found.size(), 2);
    QCOMPARE(found[0].kind, LegacyConfigImport::SourceKind::Amule);
    QCOMPARE(found[0].partFiles, 1);
    QCOMPARE(found[1].kind, LegacyConfigImport::SourceKind::Emule);
    QCOMPARE(found[1].nick, QStringLiteral("Wino"));
    QCOMPARE(found[1].winePrefix, prefix);
    QCOMPARE(found[1].tempDirs,
             QStringList{QFileInfo(user + QStringLiteral("/Temp")).canonicalFilePath()});
    QCOMPARE(found[1].partFiles, 2);
#endif
}

void tst_LegacyConfigImport::probe_acceptsParentAndPrefix()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));
    // The install folder, not its config/
    const auto src = LegacyConfigImport::probe(mfc.root.path());
    QVERIFY(src);
    QCOMPARE(src->configDir, mfc.config);
    QCOMPARE(src->tempDirs, QStringList{mfc.temp});

    TempDir empty;
    QVERIFY(!LegacyConfigImport::probe(empty.path()));
}

QTEST_MAIN(tst_LegacyConfigImport)
#include "tst_LegacyConfigImport.moc"
