/// @file tst_AutoUpdater.cpp
/// @brief emulecored --autoupdate: argument rewrite, scheduling, download + hand-over.

#include "AutoUpdater.h"

#include "app/VersionManifest.h"
#include "prefs/Preferences.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;
using namespace Qt::StringLiterals;

namespace {

/// GET-only HTTP server answering from a path -> body map.
class TinyHttpServer : public QTcpServer {
public:
    QHash<QByteArray, QByteArray> files;

    [[nodiscard]] QString url(const QString& path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path);
    }

protected:
    void incomingConnection(qintptr handle) override
    {
        auto* s = new QTcpSocket(this);
        s->setSocketDescriptor(handle);
        connect(s, &QTcpSocket::readyRead, s, [this, s] {
            const QByteArray req = s->readAll();
            const QByteArray path = req.split(' ').value(1);
            const bool found = files.contains(path);
            const QByteArray body = files.value(path, QByteArrayLiteral("not found"));
            s->write((found ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n")
                     + QByteArrayLiteral("Content-Length: ") + QByteArray::number(body.size())
                     + "\r\nConnection: close\r\n\r\n" + body);
            s->disconnectFromHost();
        });
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    }
};

QByteArray manifestFor(const QString& key, const QString& url, const QByteArray& pkg, const QString& sha = {})
{
    const QString hash = sha.isEmpty()
        ? QString::fromLatin1(QCryptographicHash::hash(pkg, QCryptographicHash::Sha256).toHex())
        : sha;
    const QJsonObject entry{{QStringLiteral("url"), url}, {QStringLiteral("sha256"), hash},
                            {QStringLiteral("size"), qint64(pkg.size())}};
    const QJsonObject root{{QStringLiteral("latest"), QStringLiteral("9.0.0")},
                           {QStringLiteral("downloads"), QJsonObject{{key, entry}}}};
    return QJsonDocument(root).toJson();
}

} // namespace

class TestAutoUpdater : public QObject {
    Q_OBJECT

private slots:
    void init();
    void normalizeArgs_data();
    void normalizeArgs();
    void relaunchArgsDropHoldConnect();
    void clampHours();
    void isDue();
    void packageKeyAndFormat();
    void installRoot();
    void configKeepArgs();
    void downloadsVerifiesAndHandsOver();
    void hashMismatchLaunchesNothing();
    void repeatedFailureSkipsVersion();
    void resultFileClearsOrCountsFailure();

private:
    struct Rig {
        QTemporaryDir dir;
        TinyHttpServer http;
        QString stateDir;
        QString appDir;
        QString launchedProgram;
        QStringList launchedArgs;
        QString announced;
        bool quitCalled = false;
        int guiCalls = 0;
    };
    std::unique_ptr<AutoUpdater> makeUpdater(Rig& rig);
    void serve(Rig& rig, const QByteArray& pkg, const QString& sha = {});
};

void TestAutoUpdater::init()
{
    qunsetenv("EMULEQT_VERSION_MANIFEST_URL");
}

void TestAutoUpdater::normalizeArgs_data()
{
    QTest::addColumn<QStringList>("in");
    QTest::addColumn<QStringList>("out");
    const QString exe = QStringLiteral("emulecored");
    QTest::newRow("plain") << QStringList{exe, u"--autoupdate"_s} << QStringList{exe, u"--autoupdate"_s};
    QTest::newRow("equals") << QStringList{exe, u"--autoupdate=12"_s}
                            << QStringList{exe, u"--autoupdate"_s, u"--autoupdate-hours"_s, u"12"_s};
    QTest::newRow("space") << QStringList{exe, u"--autoupdate"_s, u"12"_s, u"--port"_s, u"4712"_s}
                           << QStringList{exe, u"--autoupdate"_s, u"--autoupdate-hours"_s, u"12"_s, u"--port"_s, u"4712"_s};
    QTest::newRow("next is option") << QStringList{exe, u"--autoupdate"_s, u"--port"_s, u"4712"_s}
                                    << QStringList{exe, u"--autoupdate"_s, u"--port"_s, u"4712"_s};
    QTest::newRow("absent") << QStringList{exe, u"--port"_s, u"12"_s} << QStringList{exe, u"--port"_s, u"12"_s};
}

void TestAutoUpdater::normalizeArgs()
{
    QFETCH(QStringList, in);
    QFETCH(QStringList, out);
    QCOMPARE(AutoUpdater::normalizeArgs(in), out);
}

void TestAutoUpdater::relaunchArgsDropHoldConnect()
{
    const QStringList argv{u"/x/emulecored"_s, u"--autoupdate=6"_s, u"--hold-connect"_s, u"--config"_s, u"/c"_s};
    QCOMPARE(AutoUpdater::relaunchArgs(argv), (QStringList{u"--autoupdate=6"_s, u"--config"_s, u"/c"_s}));
}

void TestAutoUpdater::clampHours()
{
    QCOMPARE(AutoUpdater::clampHours(0), 0);
    QCOMPARE(AutoUpdater::clampHours(1), 1);
    QCOMPARE(AutoUpdater::clampHours(720), 720);
    QCOMPARE(AutoUpdater::clampHours(721), 0);
    QCOMPARE(AutoUpdater::clampHours(-3), 0);
}

void TestAutoUpdater::isDue()
{
    QVERIFY(AutoUpdater::isDue(0, 1000, 3600));            // never checked
    QVERIFY(!AutoUpdater::isDue(1000, 2000, 3600));
    QVERIFY(AutoUpdater::isDue(1000, 4600, 3600));
    QVERIFY(AutoUpdater::isDue(5000, 1000, 3600));         // clock went back
}

void TestAutoUpdater::packageKeyAndFormat()
{
    QCOMPARE(AutoUpdater::packageKey(u"windows-x64"_s, true), QStringLiteral("windows-x64-installer"));
    QCOMPARE(AutoUpdater::packageKey(u"windows-x64"_s, false), QStringLiteral("windows-x64"));
    QCOMPARE(AutoUpdater::packageKey(u"mac-arm64"_s, true), QStringLiteral("mac-arm64"));
    QCOMPARE(AutoUpdater::packageKey({}, false), QString());
    QCOMPARE(AutoUpdater::packageFormat(u"eMuleQt-v1.0.0-macOS-arm64.dmg"_s), QStringLiteral("dmg"));
    QCOMPARE(AutoUpdater::packageFormat(u"eMuleQt-v1.0.0-win64-setup.exe"_s), QStringLiteral("inno"));
    QCOMPARE(AutoUpdater::packageFormat(u"eMuleQt-v1.0.0-win64.zip"_s), QStringLiteral("archive"));
    QCOMPARE(AutoUpdater::packageFormat(u"eMuleQt-v1.0.0-linux-x86_64.tar.gz"_s), QStringLiteral("archive"));
    QCOMPARE(AutoUpdater::packageFormat(u"readme.txt"_s), QString());
}

void TestAutoUpdater::installRoot()
{
#ifdef Q_OS_MACOS
    QCOMPARE(AutoUpdater::installRoot(u"/Applications/eMule Qt.app/Contents/MacOS"_s),
             QStringLiteral("/Applications/eMule Qt.app"));
#endif
    QCOMPARE(AutoUpdater::installRoot(u"/opt/eMuleQt"_s), QStringLiteral("/opt/eMuleQt"));
}

void TestAutoUpdater::configKeepArgs()
{
    const QStringList user{u"server.met"_s, u"preferences.yml"_s};
    // portable: bundle config follows the release unless edited, user data kept
    QCOMPARE(AutoUpdater::configKeepArgs(u"/opt/e"_s, {u"/opt/e/config"_s, u"/opt/e/config/"_s}, user),
             (QStringList{u"--keep-edited"_s, u"config"_s, u"--keep-existing"_s, u"config/server.met"_s,
                          u"--keep-existing"_s, u"config/preferences.yml"_s}));
    // stale path from a moved folder plus the real one: the real one still protected
    QCOMPARE(AutoUpdater::configKeepArgs(u"/opt/e"_s, {u"/old/place/config"_s, u"/opt/e/config"_s}, {}),
             (QStringList{u"--keep-edited"_s, u"config"_s}));
    // another dir inside the install: kept whole; outside or the root itself: nothing
    QCOMPARE(AutoUpdater::configKeepArgs(u"/opt/e"_s, {u"/opt/e/my cfg"_s, u"/home/u/cfg"_s, u"/opt/e"_s}, user),
             (QStringList{u"--keep-existing"_s, u"my cfg"_s}));
}

void TestAutoUpdater::downloadsVerifiesAndHandsOver()
{
    if (VersionManifest::platformKey().isEmpty())
        QSKIP("no manifest key for this platform");
    Rig rig;
    const QByteArray pkg(4096, 'p');
    serve(rig, pkg);
    auto up = makeUpdater(rig);
    QSignalSpy stages(up.get(), &AutoUpdater::stageChanged);

    up->checkNow();
    QTRY_VERIFY_WITH_TIMEOUT(rig.quitCalled, 10'000);

    QCOMPARE(rig.announced, QStringLiteral("9.0.0"));
    QCOMPARE(rig.launchedProgram, QDir(rig.stateDir).filePath(AutoUpdater::helperName()));
    QVERIFY(QFile::exists(rig.launchedProgram));   // the copy, not the original

    const QStringList& a = rig.launchedArgs;
    const QString pkgPath = a.value(a.indexOf(u"--package"_s) + 1);
    QFile f(pkgPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), pkg);
    QCOMPARE(a.value(a.indexOf(u"--format"_s) + 1), QStringLiteral("archive"));
    QCOMPARE(a.value(a.indexOf(u"--version"_s) + 1), QStringLiteral("9.0.0"));
    QVERIFY(a.contains(u"4242"_s));   // the GUI's pid is waited for

    // core first, keeping its flags, then the GUI
    const qsizetype core = a.indexOf(u"--launch"_s);
    QVERIFY(core > 0);
    QVERIFY(a.at(core + 1).endsWith(QStringLiteral("emulecored")) || a.at(core + 1).endsWith(QStringLiteral("emulecored.exe")));
    QCOMPARE(a.at(core + 2), QStringLiteral("--autoupdate=2"));
    QCOMPARE(a.at(core + 3), QStringLiteral("--end"));
    QCOMPARE(a.at(core + 4), QStringLiteral("--launch"));
    QCOMPARE(a.at(core + 5), QStringLiteral("/gui/emuleqt"));
    QCOMPARE(a.at(core + 6), QStringLiteral("--end"));
}

void TestAutoUpdater::hashMismatchLaunchesNothing()
{
    if (VersionManifest::platformKey().isEmpty())
        QSKIP("no manifest key for this platform");
    Rig rig;
    serve(rig, QByteArray(2048, 'q'), QString(64, QLatin1Char('0')));
    auto up = makeUpdater(rig);
    QSignalSpy stages(up.get(), &AutoUpdater::stageChanged);

    up->checkNow();
    QTRY_VERIFY_WITH_TIMEOUT(!stages.isEmpty() && stages.last().at(0).toString() == QStringLiteral("failed"), 10'000);
    QVERIFY(rig.launchedProgram.isEmpty());
    QVERIFY(!rig.quitCalled);
    QVERIFY(QDir(rig.stateDir).entryList({u"*.tar.gz*"_s}, QDir::Files).isEmpty());
}

void TestAutoUpdater::repeatedFailureSkipsVersion()
{
    if (VersionManifest::platformKey().isEmpty())
        QSKIP("no manifest key for this platform");
    Rig rig;
    serve(rig, QByteArray(1024, 'r'), QString(64, QLatin1Char('0')));
    for (int i = 0; i < 3; ++i) {
        auto up = makeUpdater(rig);
        QSignalSpy stages(up.get(), &AutoUpdater::stageChanged);
        up->start();
        up->checkNow();
        QTRY_VERIFY_WITH_TIMEOUT(!stages.isEmpty() && stages.last().at(0).toString() == QStringLiteral("failed"), 10'000);
    }
    auto up = makeUpdater(rig);
    QSignalSpy stages(up.get(), &AutoUpdater::stageChanged);
    up->start();
    up->checkNow();
    QTRY_VERIFY_WITH_TIMEOUT(!stages.isEmpty() && stages.last().at(0).toString() == QStringLiteral("skipped"), 10'000);
}

void TestAutoUpdater::resultFileClearsOrCountsFailure()
{
    Rig rig;
    QVERIFY(rig.dir.isValid());
    rig.stateDir = rig.dir.filePath(u"update"_s);
    QDir().mkpath(rig.stateDir);
    auto writeResult = [&](bool ok, const QString& version) {
        QFile f(QDir(rig.stateDir).filePath(u"result.json"_s));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(QJsonObject{{u"ok"_s, ok}, {u"version"_s, version}, {u"error"_s, ok ? u""_s : u"boom"_s}}).toJson());
    };
    auto readState = [&] {
        QFile f(QDir(rig.stateDir).filePath(u"autoupdate.json"_s));
        return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject{};
    };

    writeResult(false, u"9.0.0"_s);
    {
        AutoUpdater up(rig.stateDir, 2);
        up.setCurrentVersion(u"1.0.0"_s);
        up.setFirstCheckDelayMs(3'600'000);
        up.start();
    }
    QCOMPARE(readState().value(u"failedVersion"_s).toString(), QStringLiteral("9.0.0"));
    QCOMPARE(readState().value(u"failedCount"_s).toInt(), 1);
    QVERIFY(!QFile::exists(QDir(rig.stateDir).filePath(u"result.json"_s)));

    writeResult(true, u"9.0.0"_s);
    {
        AutoUpdater up(rig.stateDir, 2);
        up.setCurrentVersion(u"9.0.0"_s);
        up.setFirstCheckDelayMs(3'600'000);
        up.start();
    }
    QCOMPARE(readState().value(u"failedCount"_s).toInt(), 0);
}

// ---------------------------------------------------------------------------

std::unique_ptr<AutoUpdater> TestAutoUpdater::makeUpdater(Rig& rig)
{
    rig.stateDir = rig.dir.filePath(u"update"_s);
    rig.appDir = rig.dir.filePath(u"app"_s);
    QDir().mkpath(rig.appDir);
    QFile helper(QDir(rig.appDir).filePath(AutoUpdater::helperName()));
    if (!helper.exists() && helper.open(QIODevice::WriteOnly))
        helper.write("fake helper");
    helper.close();

    auto up = std::make_unique<AutoUpdater>(rig.stateDir, 2);
    up->setCurrentVersion(u"1.0.0"_s);
    up->setAppDir(rig.appDir);
    up->setCoreArguments({u"/x/emulecored"_s, u"--autoupdate=2"_s, u"--hold-connect"_s});
    up->setFirstCheckDelayMs(3'600'000);   // tests drive checkNow()
    AutoUpdater::Hooks hooks;
    // the GUI is there until the push, then gone
    hooks.localGuis = [&rig] {
        std::vector<AutoUpdater::Gui> out;
        if (rig.announced.isEmpty())
            out.push_back({4242, {u"/gui/emuleqt"_s}});
        ++rig.guiCalls;
        return out;
    };
    hooks.announce = [&rig](const QString& v) { rig.announced = v; };
    hooks.launch = [&rig](const QString& p, const QStringList& a) {
        rig.launchedProgram = p;
        rig.launchedArgs = a;
        return true;
    };
    hooks.quit = [&rig] { rig.quitCalled = true; };
    up->setHooks(std::move(hooks));
    return up;
}

void TestAutoUpdater::serve(Rig& rig, const QByteArray& pkg, const QString& sha)
{
    QVERIFY(rig.dir.isValid());
    QVERIFY(rig.http.listen(QHostAddress::LocalHost, 0));
    rig.http.files.insert("/pkg/eMuleQt-v9.0.0.tar.gz", pkg);
    rig.http.files.insert("/manifest.json",
                          manifestFor(VersionManifest::platformKey(), rig.http.url(u"/pkg/eMuleQt-v9.0.0.tar.gz"_s), pkg, sha));
    qputenv("EMULEQT_VERSION_MANIFEST_URL", rig.http.url(u"/manifest.json"_s).toUtf8());
}

QTEST_GUILESS_MAIN(TestAutoUpdater)
#include "tst_AutoUpdater.moc"
