/// @file tst_I2pRouter.cpp
/// @brief The own I2P router's supervisor (I2pRouter): args, ready, crash restart, stop;
/// and its download (I2pdInstaller).
/// A shell script stands in for i2pd ($EMULE_I2PD): it opens the SAM port it was given.
/// EMULE_I2PD_LIVE=1 also fetches the pinned release from GitHub.

#include "net/I2pRouter.h"
#include "net/I2pdInstaller.h"

#include <QCryptographicHash>
#include <QProcess>
#include <QUrl>

#include <QFile>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace eMule;

namespace {

/// A fake i2pd. @p mode: "serve" opens the SAM port, "exit" ends at once with code 3.
QString writeFakeRouter(const QTemporaryDir& dir, const QByteArray& mode)
{
    const QString path = dir.filePath(QStringLiteral("emuleqt-i2pd"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write("#!/bin/sh\n"
            "echo \"$@\" > \"$(dirname \"$0\")/args.txt\"\n"
            "echo '12:00:00@1/warn - fake router up'\n");
    if (mode == "serve") {
        f.write("for a in \"$@\"; do case \"$a\" in --sam.port=*) PORT=\"${a#--sam.port=}\";; esac; done\n"
                "exec python3 -c 'import socket,sys,time\n"
                "s=socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)\n"
                "s.bind((\"127.0.0.1\", int(sys.argv[1]))); s.listen(4)\n"
                "while True:\n"
                "    c,_=s.accept(); c.close()' \"$PORT\"\n");
    } else {
        f.write("exit 3\n");
    }
    f.close();
    f.setPermissions(f.permissions() | QFileDevice::ExeOwner);
    return path;
}

I2pRouter::Config configIn(const QTemporaryDir& dir)
{
    I2pRouter::Config c;
    c.dataDir = dir.filePath(QStringLiteral("I2P"));
    c.routerPort = 23456;
    c.share = 40;
    c.address4 = QStringLiteral("10.8.0.2");
    return c;
}

/// A release-like tar.gz in @p dir: emuleqt-i2pd (no exec bit), certificates/, LICENSE.
I2pdInstaller::Asset writeFakeRelease(const QTemporaryDir& dir)
{
    const QString src = dir.filePath(QStringLiteral("src"));
    QDir().mkpath(src + QStringLiteral("/certificates/reseed"));
    for (const auto& [name, text] : {std::pair{"emuleqt-i2pd", "#!/bin/sh\nexit 0\n"},
                                     std::pair{"LICENSE", "BSD\n"},
                                     std::pair{"certificates/reseed/a.crt", "cert\n"}}) {
        QFile f(src + QLatin1Char('/') + QLatin1StringView(name));
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write(text);
    }
    const QString archive = dir.filePath(QStringLiteral("i2pd-test.tar.gz"));
    if (QProcess::execute(QStringLiteral("tar"), {QStringLiteral("-czf"), archive, QStringLiteral("-C"), src,
                                                  QStringLiteral(".")}) != 0)
        return {};
    QFile f(archive);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QString sha = QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
    return {QStringLiteral("i2pd-test.tar.gz"), sha, QUrl::fromLocalFile(archive), I2pdInstaller::pinnedAsset().release};
}

} // namespace

class tst_I2pRouter : public QObject {
    Q_OBJECT

private slots:
    void arguments();
    void readyOnceSamAnswers();
    void crashRestartsWithPause();
    void stopEndsIt();
    void installerUnpacksAndStamps();
    void installerBadHashKeepsOld();
    void installerLive();
};

void tst_I2pRouter::arguments()
{
    I2pRouter::Config c;
    c.dataDir = QStringLiteral("/cfg/I2P");
    c.routerPort = 12345;
    c.share = 150;
    c.address6 = QStringLiteral("fd00::2");
    const QStringList a = I2pRouter::arguments(c, 40000, QStringLiteral("/app/i2pd-certificates"));
    QVERIFY(a.contains(QStringLiteral("--sam.enabled=true")));
    QVERIFY(a.contains(QStringLiteral("--sam.address=127.0.0.1")));
    QVERIFY(a.contains(QStringLiteral("--sam.port=40000")));
    QVERIFY(a.contains(QStringLiteral("--port=12345")));
    QVERIFY(a.contains(QStringLiteral("--share=100")));
    // nothing else listens, and UPnP is the app's
    QVERIFY(a.contains(QStringLiteral("--http.enabled=false")));
    QVERIFY(a.contains(QStringLiteral("--httpproxy.enabled=false")));
    QVERIFY(a.contains(QStringLiteral("--socksproxy.enabled=false")));
    QVERIFY(a.contains(QStringLiteral("--upnp.enabled=false")));
    QVERIFY(a.contains(QStringLiteral("--address6=fd00::2")));
    QVERIFY(!a.join(QLatin1Char(' ')).contains(QStringLiteral("--address4")));
    QVERIFY(a.join(QLatin1Char(' ')).contains(QStringLiteral("--certsdir=")));
}

void tst_I2pRouter::readyOnceSamAnswers()
{
#ifdef Q_OS_WIN
    QSKIP("the fake router is a shell script");
#endif
    QTemporaryDir dir;
    const QString bin = writeFakeRouter(dir, "serve");
    qputenv("EMULE_I2PD", bin.toLocal8Bit());
    I2pRouter router;
    QVERIFY(router.available());
    QSignalSpy spy(&router, &I2pRouter::stateChanged);
    router.start(configIn(dir));
    QCOMPARE(router.state(), I2pRouter::State::Starting);
    QVERIFY(router.samPort() != 0);
    QTRY_COMPARE_WITH_TIMEOUT(router.state(), I2pRouter::State::Ready, 15000);
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("I2P/i2pd.conf"))));
    QVERIFY(QFile::exists(dir.filePath(QStringLiteral("I2P/tunnels.conf"))));
    QFile args(dir.filePath(QStringLiteral("args.txt")));
    QVERIFY(args.open(QIODevice::ReadOnly));
    const QByteArray line = args.readAll();
    QVERIFY(line.contains("--address4=10.8.0.2"));
    QVERIFY(line.contains("--port=23456"));
    // same config again: no restart
    const int changes = int(spy.count());
    router.start(configIn(dir));
    QCOMPARE(int(spy.count()), changes);
    router.stop();
    QCOMPARE(router.state(), I2pRouter::State::Stopped);
    qunsetenv("EMULE_I2PD");
}

void tst_I2pRouter::crashRestartsWithPause()
{
#ifdef Q_OS_WIN
    QSKIP("the fake router is a shell script");
#endif
    QTemporaryDir dir;
    qputenv("EMULE_I2PD", writeFakeRouter(dir, "exit").toLocal8Bit());
    I2pRouter router;
    router.start(configIn(dir));
    QTRY_COMPARE_WITH_TIMEOUT(router.state(), I2pRouter::State::Failed, 5000);
    QVERIFY(router.lastError().contains(QStringLiteral("code 3")));
    QCOMPARE(router.samPort(), quint16(0));
    // the next try comes after kFirstRestartMs, not at once
    QTest::qWait(1000);
    QCOMPARE(router.state(), I2pRouter::State::Failed);
    router.stop();
    QCOMPARE(router.state(), I2pRouter::State::Stopped);
    qunsetenv("EMULE_I2PD");
}

void tst_I2pRouter::stopEndsIt()
{
#ifdef Q_OS_WIN
    QSKIP("the fake router is a shell script");
#endif
    QTemporaryDir dir;
    qputenv("EMULE_I2PD", writeFakeRouter(dir, "serve").toLocal8Bit());
    auto router = std::make_unique<I2pRouter>();
    router->start(configIn(dir));
    QTRY_COMPARE_WITH_TIMEOUT(router->state(), I2pRouter::State::Ready, 15000);
    const quint16 port = router->samPort();
    // destroyed while running: the child goes too, its port is free again
    router.reset();
    const auto portFree = [port] {
        QTcpServer probe;
        return probe.listen(QHostAddress::LocalHost, port);
    };
    QTRY_VERIFY_WITH_TIMEOUT(portFree(), 5000);
    qunsetenv("EMULE_I2PD");
}

void tst_I2pRouter::installerUnpacksAndStamps()
{
#ifdef Q_OS_WIN
    QSKIP("the fake release is a shell script");
#endif
    QTemporaryDir dir;
    const I2pdInstaller::Asset asset = writeFakeRelease(dir);
    QVERIFY(!asset.name.isEmpty());
    I2pdInstaller inst;
    inst.setDir(I2pRouter::installDir(dir.filePath(QStringLiteral("I2P"))));
    QVERIFY(inst.binary().isEmpty());
    QVERIFY(!inst.current());
    QSignalSpy done(&inst, &I2pdInstaller::finished);
    QVERIFY(inst.install(asset));
    QVERIFY(inst.running());
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
    QVERIFY2(done.first().first().toBool(), qPrintable(inst.error()));
    QVERIFY(!inst.running());
    const QString bin = inst.binary();
    QVERIFY(!bin.isEmpty());
    QVERIFY(QFileInfo(bin).isExecutable());
    // the stamp names the pin
    QCOMPARE(inst.current(), I2pdInstaller::supported());
    QCOMPARE(I2pRouter::findCertificates(bin), QDir::cleanPath(inst.dir() + QStringLiteral("/certificates")));
    // no archive or temp folder left
    QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("I2P/i2pd-test.tar.gz"))));
    QVERIFY(!QFileInfo::exists(inst.dir() + QStringLiteral(".tmp")));
}

void tst_I2pRouter::installerBadHashKeepsOld()
{
#ifdef Q_OS_WIN
    QSKIP("the fake release is a shell script");
#endif
    QTemporaryDir dir;
    I2pdInstaller::Asset asset = writeFakeRelease(dir);
    QVERIFY(!asset.name.isEmpty());
    I2pdInstaller inst;
    inst.setDir(I2pRouter::installDir(dir.filePath(QStringLiteral("I2P"))));
    QSignalSpy done(&inst, &I2pdInstaller::finished);
    QVERIFY(inst.install(asset));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
    QVERIFY(done.takeFirst().first().toBool());
    asset.sha256 = QString(64, QLatin1Char('0'));
    asset.release = QStringLiteral("other 00");
    QVERIFY(inst.install(asset));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 10000);
    QVERIFY(!done.first().first().toBool());
    QVERIFY(inst.error().contains(QStringLiteral("SHA-256")));
    QVERIFY(!inst.binary().isEmpty());
}

void tst_I2pRouter::installerLive()
{
    if (qEnvironmentVariableIntValue("EMULE_I2PD_LIVE") != 1)
        QSKIP("EMULE_I2PD_LIVE=1 downloads the pinned release");
    if (!I2pdInstaller::supported())
        QSKIP("no release for this system");
    QTemporaryDir dir;
    I2pdInstaller inst;
    inst.setDir(I2pRouter::installDir(dir.path()));
    QSignalSpy done(&inst, &I2pdInstaller::finished);
    QVERIFY(inst.install());
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 120000);
    QVERIFY2(done.first().first().toBool(), qPrintable(inst.error()));
    QVERIFY(inst.current());
    QVERIFY(!I2pRouter::findCertificates(inst.binary()).isEmpty());
    QProcess p;
    p.start(inst.binary(), {QStringLiteral("--version")});
    QVERIFY(p.waitForFinished(10000));
    QVERIFY(p.readAll().contains("i2pd"));
}

QTEST_GUILESS_MAIN(tst_I2pRouter)
#include "tst_I2pRouter.moc"
