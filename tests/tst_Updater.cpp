/// @file tst_Updater.cpp
/// @brief emuleqt-updater: archive overlay, rollback, pid wait, dmg swap, usage.

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QTest>

#define UPDATER_STRINGIFY_(x) #x
#define UPDATER_STRINGIFY(x) UPDATER_STRINGIFY_(x)

class tst_Updater : public QObject {
    Q_OBJECT

private slots:
    void init();
    void archiveOverlay();
    void corruptPackageLeavesTargetAndLaunches();
    void failedCopyRollsBack();
    void waitPidTimeout();
    void waitPidThenInstall();
    void dmgSwap();
    void badUsage_data();
    void badUsage();

private:
    QString path(const QString& rel) const { return m_tmp->filePath(rel); }
    static void writeFile(const QString& file, const QByteArray& data,
                          QFile::Permissions perms = QFile::Permissions());
    static QByteArray readFile(const QString& file);
    void makeTarget();
    void makePackage();
    int runUpdater(const QStringList& args) const;
    QJsonObject result() const;
    QStringList baseArgs() const;
    QStringList markerLaunch() const;

    std::unique_ptr<QTemporaryDir> m_tmp;
};

namespace {

QString updaterExe()
{
    const QString env = qEnvironmentVariable("EMULEQT_UPDATER");
    // the VS projects pass a path relative to the run dir
    return QFileInfo(env.isEmpty() ? QStringLiteral(UPDATER_STRINGIFY(EMULEQT_UPDATER_PATH)) : env)
        .absoluteFilePath();
}

constexpr QFile::Permissions kExec = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                     | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther;

} // namespace

void tst_Updater::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
    QVERIFY2(QFileInfo(updaterExe()).isExecutable(), qPrintable(updaterExe()));
}

// new core replaced (exec bit), user config kept, shipped extras added, stale files left
void tst_Updater::archiveOverlay()
{
    makeTarget();
    makePackage();
    const int rc = runUpdater(baseArgs() + QStringList{QStringLiteral("--keep-existing"), QStringLiteral("config")}
                              + markerLaunch());
    QCOMPARE(rc, 0);

    const QString t = path(QStringLiteral("app/"));
    QCOMPARE(readFile(t + QStringLiteral("emulecored")), QByteArray("new core"));
    QCOMPARE(readFile(t + QStringLiteral("config/preferences.yml")), QByteArray("user prefs"));
    QCOMPARE(readFile(t + QStringLiteral("config/new.dat")), QByteArray("new dat"));
    QCOMPARE(readFile(t + QStringLiteral("newfile")), QByteArray("added"));
    QCOMPARE(readFile(t + QStringLiteral("lib/old.so")), QByteArray("old lib"));
    QVERIFY(!QFileInfo::exists(t + QStringLiteral(".update-backup")));
#ifndef Q_OS_WIN
    QVERIFY(QFileInfo(t + QStringLiteral("emulecored")).isExecutable());
    const QFileInfo link(t + QStringLiteral("lib/libfoo.so"));
    QVERIFY(link.isSymLink());
    QCOMPARE(link.symLinkTarget(), QFileInfo(t + QStringLiteral("lib/libfoo.so.1")).absoluteFilePath());
#endif
    // staging next to the package is gone
    QCOMPARE(QDir(path(QStringLiteral("update"))).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot),
             QStringList{QStringLiteral("pkg.tar.gz")});

    const QJsonObject r = result();
    QCOMPARE(r.value(QStringLiteral("ok")).toBool(), true);
    QCOMPARE(r.value(QStringLiteral("version")).toString(), QStringLiteral("1.2.3"));
    QCOMPARE(r.value(QStringLiteral("rolledBack")).toBool(), false);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(path(QStringLiteral("marker"))), 5000);
    QVERIFY(QFileInfo(path(QStringLiteral("updater.log"))).size() > 0);
}

void tst_Updater::corruptPackageLeavesTargetAndLaunches()
{
    makeTarget();
    QByteArray junk(4096, '\0');
    for (auto& c : junk)
        c = static_cast<char>(QRandomGenerator::global()->bounded(256));
    writeFile(path(QStringLiteral("update/pkg.tar.gz")), junk);

    QCOMPARE(runUpdater(baseArgs() + markerLaunch()), 1);
    QCOMPARE(readFile(path(QStringLiteral("app/emulecored"))), QByteArray("old core"));
    QCOMPARE(readFile(path(QStringLiteral("app/config/preferences.yml"))), QByteArray("user prefs"));
    QVERIFY(!QFileInfo::exists(path(QStringLiteral("app/newfile"))));
    const QJsonObject r = result();
    QCOMPARE(r.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(!r.value(QStringLiteral("error")).toString().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(path(QStringLiteral("marker"))), 5000);
}

// a directory where the package has a file: the whole overlay is undone
void tst_Updater::failedCopyRollsBack()
{
    makeTarget();
    makePackage();
    writeFile(path(QStringLiteral("app/newfile/inside")), "blocker");

    QCOMPARE(runUpdater(baseArgs()), 1);
    QCOMPARE(readFile(path(QStringLiteral("app/emulecored"))), QByteArray("old core"));
    QCOMPARE(readFile(path(QStringLiteral("app/config/preferences.yml"))), QByteArray("user prefs"));
    QCOMPARE(readFile(path(QStringLiteral("app/newfile/inside"))), QByteArray("blocker"));
    QVERIFY(!QFileInfo::exists(path(QStringLiteral("app/config/new.dat"))));
    QVERIFY(!QFileInfo::exists(path(QStringLiteral("app/lib/libfoo.so.1"))));
    QVERIFY(!QFileInfo::exists(path(QStringLiteral("app/.update-backup"))));
    QCOMPARE(result().value(QStringLiteral("ok")).toBool(), false);
}

void tst_Updater::waitPidTimeout()
{
    makeTarget();
    makePackage();
    QProcess sleeper;
#ifdef Q_OS_WIN
    sleeper.start(QStringLiteral("ping"), {QStringLiteral("-n"), QStringLiteral("31"), QStringLiteral("127.0.0.1")});
#else
    sleeper.start(QStringLiteral("/bin/sleep"), {QStringLiteral("30")});
#endif
    QVERIFY(sleeper.waitForStarted());

    const int rc = runUpdater(baseArgs() + QStringList{QStringLiteral("--wait-pid"),
                                                       QString::number(sleeper.processId()),
                                                       QStringLiteral("--wait-timeout"), QStringLiteral("1")}
                              + markerLaunch());
    QCOMPARE(sleeper.state(), QProcess::Running); // not killed
    sleeper.kill();
    sleeper.waitForFinished();

    QCOMPARE(rc, 1);
    QCOMPARE(readFile(path(QStringLiteral("app/emulecored"))), QByteArray("old core"));
    QVERIFY(!QFileInfo::exists(path(QStringLiteral("app/newfile"))));
    const QJsonObject r = result();
    QCOMPARE(r.value(QStringLiteral("ok")).toBool(), false);
    QVERIFY(r.value(QStringLiteral("error")).toString().contains(QStringLiteral("timed out")));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(path(QStringLiteral("marker"))), 5000);
}

void tst_Updater::waitPidThenInstall()
{
    makeTarget();
    makePackage();
    QProcess sleeper;
#ifdef Q_OS_WIN
    sleeper.start(QStringLiteral("ping"), {QStringLiteral("-n"), QStringLiteral("2"), QStringLiteral("127.0.0.1")});
#else
    sleeper.start(QStringLiteral("/bin/sleep"), {QStringLiteral("1")});
#endif
    QVERIFY(sleeper.waitForStarted());

    // run async: the sleeper must be reaped here, or it stays a zombie
    QProcess updater;
    updater.start(updaterExe(), baseArgs() + QStringList{QStringLiteral("--wait-pid"),
                                                         QString::number(sleeper.processId()),
                                                         QStringLiteral("--wait-timeout"), QStringLiteral("20")});
    QVERIFY(updater.waitForStarted());
    QVERIFY(sleeper.waitForFinished(10000));
    QVERIFY(updater.waitForFinished(20000));
    QCOMPARE(updater.exitCode(), 0);
    QCOMPARE(readFile(path(QStringLiteral("app/emulecored"))), QByteArray("new core"));
    QCOMPARE(result().value(QStringLiteral("ok")).toBool(), true);
}

void tst_Updater::dmgSwap()
{
#ifndef Q_OS_MACOS
    QSKIP("dmg is macOS only");
#else
    // image root: Test.app + the Applications symlink a release dmg has
    writeFile(path(QStringLiteral("dmgsrc/Test.app/Contents/MacOS/emuleqt")), "new gui", kExec);
    QVERIFY(QFile::link(QStringLiteral("/Applications"), path(QStringLiteral("dmgsrc/Applications"))));
    QDir().mkpath(path(QStringLiteral("update")));
    QProcess mk;
    mk.start(QStringLiteral("/usr/bin/hdiutil"),
             {QStringLiteral("create"), QStringLiteral("-quiet"), QStringLiteral("-srcfolder"),
              path(QStringLiteral("dmgsrc")), QStringLiteral("-volname"), QStringLiteral("eMuleQt Test"),
              QStringLiteral("-format"), QStringLiteral("UDZO"), QStringLiteral("-ov"),
              path(QStringLiteral("update/pkg.dmg"))});
    if (!mk.waitForFinished(60000) || mk.exitCode() != 0)
        QSKIP("hdiutil create unavailable");

    const QString target = path(QStringLiteral("Apps/eMuleQt.app"));
    writeFile(target + QStringLiteral("/Contents/MacOS/emuleqt"), "old gui", kExec);
    writeFile(target + QStringLiteral("/Contents/MacOS/stale"), "stale");

    const int rc = runUpdater({QStringLiteral("--package"), path(QStringLiteral("update/pkg.dmg")),
                               QStringLiteral("--format"), QStringLiteral("dmg"),
                               QStringLiteral("--target"), target,
                               QStringLiteral("--version"), QStringLiteral("9.9.9"),
                               QStringLiteral("--log"), path(QStringLiteral("updater.log")),
                               QStringLiteral("--result"), path(QStringLiteral("result.json"))});
    QCOMPARE(rc, 0);
    QCOMPARE(readFile(target + QStringLiteral("/Contents/MacOS/emuleqt")), QByteArray("new gui"));
    QVERIFY(QFileInfo(target + QStringLiteral("/Contents/MacOS/emuleqt")).isExecutable());
    QVERIFY(!QFileInfo::exists(target + QStringLiteral("/Contents/MacOS/stale")));
    QVERIFY(!QFileInfo::exists(target + QStringLiteral(".old")));
    QVERIFY(!QFileInfo::exists(target + QStringLiteral(".new")));
    QCOMPARE(result().value(QStringLiteral("ok")).toBool(), true);
#endif
}

void tst_Updater::badUsage_data()
{
    QTest::addColumn<QStringList>("args");
    const QStringList ok{QStringLiteral("--package"), QStringLiteral("p"), QStringLiteral("--format"),
                         QStringLiteral("archive"), QStringLiteral("--target"), QStringLiteral("t")};
    QTest::newRow("unknown") << (ok + QStringList{QStringLiteral("--bogus")});
    QTest::newRow("no package") << ok.mid(2);
    QTest::newRow("bad format") << QStringList{QStringLiteral("--package"), QStringLiteral("p"),
                                               QStringLiteral("--format"), QStringLiteral("rpm"),
                                               QStringLiteral("--target"), QStringLiteral("t")};
    QTest::newRow("launch no end") << (ok + QStringList{QStringLiteral("--launch"), QStringLiteral("x")});
    QTest::newRow("bad pid") << (ok + QStringList{QStringLiteral("--wait-pid"), QStringLiteral("abc")});
    QTest::newRow("missing value") << (ok + QStringList{QStringLiteral("--result")});
}

void tst_Updater::badUsage()
{
    QFETCH(QStringList, args);
    QCOMPARE(runUpdater(args), 2);
}

// ---------------------------------------------------------------------------

void tst_Updater::writeFile(const QString& file, const QByteArray& data, QFile::Permissions perms)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(data);
    f.close();
    if (perms)
        QVERIFY(f.setPermissions(perms));
}

QByteArray tst_Updater::readFile(const QString& file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("<missing>");
}

void tst_Updater::makeTarget()
{
    writeFile(path(QStringLiteral("app/emulecored")), "old core");
    writeFile(path(QStringLiteral("app/config/preferences.yml")), "user prefs");
    writeFile(path(QStringLiteral("app/lib/old.so")), "old lib");
}

// tarball with a top eMuleQt/ like the Linux release
void tst_Updater::makePackage()
{
    const QString root = path(QStringLiteral("pkgsrc/eMuleQt/"));
    writeFile(root + QStringLiteral("emulecored"), "new core", kExec);
    writeFile(root + QStringLiteral("config/preferences.yml"), "shipped prefs");
    writeFile(root + QStringLiteral("config/new.dat"), "new dat");
    writeFile(root + QStringLiteral("newfile"), "added");
    writeFile(root + QStringLiteral("lib/libfoo.so.1"), "foo");
#ifndef Q_OS_WIN
    QVERIFY(QFile::link(QStringLiteral("libfoo.so.1"), root + QStringLiteral("lib/libfoo.so")));
#endif
    QDir().mkpath(path(QStringLiteral("update")));
    QProcess tar;
    tar.start(QStringLiteral("tar"), {QStringLiteral("-czf"), path(QStringLiteral("update/pkg.tar.gz")),
                                      QStringLiteral("-C"), path(QStringLiteral("pkgsrc")), QStringLiteral("eMuleQt")});
    QVERIFY(tar.waitForFinished(30000));
    QCOMPARE(tar.exitCode(), 0);
}

int tst_Updater::runUpdater(const QStringList& args) const
{
    QProcess p;
    p.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    p.start(updaterExe(), args);
    if (!p.waitForStarted() || !p.waitForFinished(60000))
        return -1;
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

QJsonObject tst_Updater::result() const
{
    return QJsonDocument::fromJson(readFile(path(QStringLiteral("result.json")))).object();
}

QStringList tst_Updater::baseArgs() const
{
    return {QStringLiteral("--package"), path(QStringLiteral("update/pkg.tar.gz")),
            QStringLiteral("--format"), QStringLiteral("archive"),
            QStringLiteral("--target"), path(QStringLiteral("app")),
            QStringLiteral("--version"), QStringLiteral("1.2.3"),
            QStringLiteral("--log"), path(QStringLiteral("updater.log")),
            QStringLiteral("--result"), path(QStringLiteral("result.json"))};
}

QStringList tst_Updater::markerLaunch() const
{
#ifdef Q_OS_WIN
    return {QStringLiteral("--launch"), QStringLiteral("C:\\Windows\\System32\\cmd.exe"), QStringLiteral("/c"),
            QStringLiteral("echo"), QStringLiteral("ok>"), QDir::toNativeSeparators(path(QStringLiteral("marker"))),
            QStringLiteral("--end")};
#else
    return {QStringLiteral("--launch"), QStringLiteral("/bin/sh"), QStringLiteral("-c"),
            QStringLiteral("echo ok > '") + path(QStringLiteral("marker")) + QStringLiteral("'"),
            QStringLiteral("--end")};
#endif
}

QTEST_GUILESS_MAIN(tst_Updater)
#include "tst_Updater.moc"
