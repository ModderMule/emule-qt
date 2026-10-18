/// @file tst_LegacyConfigImport.cpp
/// @brief Tests for prefs/LegacyConfigImport — installer-requested import of an MFC config dir.

#include "TestHelpers.h"
#include "prefs/DownloadCategory.h"
#include "prefs/LegacyConfigImport.h"
#include "prefs/Preferences.h"

#include <QDir>
#include <QFile>
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

/// Fresh eMule Qt profile plus the installer's request pointing at @p source.
struct Profile {
    TempDir dir;
    QString request;
    Preferences prefs;

    explicit Profile(const QString& source)
        : request(dir.filePath(QStringLiteral("request/mfc-import.yml")))
    {
        prefs.load(dir.filePath(QStringLiteral("preferences.yml")));
        QVERIFY(prefs.save());
        // Single-quoted, as the installer writes it
        writeFile(request, "source: '" + source.toUtf8() + "'\n");
    }

    [[nodiscard]] QString file(const QString& name) const { return dir.filePath(name); }

    [[nodiscard]] LegacyConfigImport::Outcome run()
    {
        return LegacyConfigImport::run(request, dir.path(), prefs);
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
    void import_carriesSchedulesOnly();
    void import_runsOnlyOnce();
    void import_expandsEnvironmentInTheRequest();
    void usedProfile_isLeftAlone();
    void missingSource_isRecordedAndSkipped();
    void pendingTempDirs_areHandedOutOnce();
};

void tst_LegacyConfigImport::noRequest_doesNothing()
{
    TempDir tmp;
    Preferences prefs;
    prefs.load(tmp.filePath(QStringLiteral("preferences.yml")));

    const auto out = LegacyConfigImport::run(tmp.filePath(QStringLiteral("mfc-import.yml")),
                                             tmp.path(), prefs);
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
    QCOMPARE(readFile(profile.file(QStringLiteral("known.met"))), QByteArrayLiteral("known"));
    QCOMPARE(readFile(profile.file(QStringLiteral("clients.met"))), QByteArrayLiteral("credits"));
    QCOMPARE(readFile(profile.file(QStringLiteral("server.met"))), QByteArrayLiteral("mfc servers"));
    QVERIFY(!QFile::exists(profile.file(QStringLiteral("statistics.ini"))));

    const Preferences& p = profile.prefs;
    QCOMPARE(p.nick(), QStringLiteral("OldMule"));
    QCOMPARE(p.port(), uint16{41234});
    QCOMPARE(p.udpPort(), uint16{41235});
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
    QCOMPARE(profile.prefs.udpPort(), uint16{0});     // UDP off is a setting
    QCOMPARE(profile.prefs.port(), portBefore);       // 0 = "pick one", ours stays
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

void tst_LegacyConfigImport::import_runsOnlyOnce()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));

    Profile profile(mfc.config);
    QCOMPARE(profile.run().result, LegacyConfigImport::Result::Imported);

    // The request sits in Program Files and outlives the import
    profile.prefs.setNick(QStringLiteral("renamed since"));
    QCOMPARE(profile.run().result, LegacyConfigImport::Result::AlreadyDone);
    QCOMPARE(profile.prefs.nick(), QStringLiteral("renamed since"));
}

void tst_LegacyConfigImport::import_expandsEnvironmentInTheRequest()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));
    qputenv("EMULE_TST_APPDATA", mfc.root.path().toUtf8());

    Profile profile(QStringLiteral("%EMULE_TST_APPDATA%/config"));
    // Setup saves the request as UTF-8 with BOM
    writeFile(profile.request, "\xEF\xBB\xBF# Written by setup\r\n"
                               "source: '%EMULE_TST_APPDATA%/config'\r\n");
    const auto out = profile.run();
    qunsetenv("EMULE_TST_APPDATA");

    QCOMPARE(out.result, LegacyConfigImport::Result::Imported);
    QCOMPARE(out.sourceDir, mfc.config);
}

void tst_LegacyConfigImport::usedProfile_isLeftAlone()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));
    writeFile(mfc.file(QStringLiteral("known.met")), "mfc known");

    Profile profile(mfc.config);
    writeFile(profile.file(QStringLiteral("known.met")), "ours");
    const QString nick = profile.prefs.nick();

    QCOMPARE(profile.run().result, LegacyConfigImport::Result::ProfileInUse);
    QCOMPARE(readFile(profile.file(QStringLiteral("known.met"))), QByteArrayLiteral("ours"));
    QCOMPARE(profile.prefs.nick(), nick);
    // Decided once, not re-examined at every start
    QCOMPARE(profile.run().result, LegacyConfigImport::Result::AlreadyDone);
}

void tst_LegacyConfigImport::missingSource_isRecordedAndSkipped()
{
    TempDir empty;
    Profile profile(empty.path());
    const QString nick = profile.prefs.nick();

    QCOMPARE(profile.run().result, LegacyConfigImport::Result::SourceMissing);
    QCOMPARE(profile.prefs.nick(), nick);
    QCOMPARE(profile.run().result, LegacyConfigImport::Result::AlreadyDone);
}

void tst_LegacyConfigImport::pendingTempDirs_areHandedOutOnce()
{
    MfcInstall mfc;
    writeFile(mfc.file(QStringLiteral("preferences.ini")), utf16(mfc.prefsIni()));

    Profile profile(mfc.config);
    QCOMPARE(profile.run().result, LegacyConfigImport::Result::Imported);

    QCOMPARE(LegacyConfigImport::takePendingTempDirs(profile.dir.path()), QStringList{mfc.temp});
    QVERIFY(LegacyConfigImport::takePendingTempDirs(profile.dir.path()).isEmpty());
    // Still marked done
    QCOMPARE(profile.run().result, LegacyConfigImport::Result::AlreadyDone);
}

QTEST_MAIN(tst_LegacyConfigImport)
#include "tst_LegacyConfigImport.moc"
