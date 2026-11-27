/// @file tst_ConfigBackup.cpp
/// @brief ConfigBackup (#268) and BackgroundFileWriter's .bak keeping (#264).

#include "TestHelpers.h"

#include "app/ConfigBackup.h"
#include "utils/BackgroundFileWriter.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;

namespace {

void put(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

QByteArray slurp(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class tst_ConfigBackup : public QObject {
    Q_OBJECT

private slots:
    void copiesTheProfileWithoutLogsAndCaches();
    void refusesAFolderInsideTheConfig();
    void refusesAMissingFolder();
    void skipRules();
    void saveKeepsThePreviousFileAsBak();
    void removeWithBakDropsBoth();
};

void tst_ConfigBackup::copiesTheProfileWithoutLogsAndCaches()
{
    QTemporaryDir cfg;
    QTemporaryDir dest;
    put(cfg.filePath(QStringLiteral("preferences.yml")), "a: 1\n");
    put(cfg.filePath(QStringLiteral("BT/resume/x.fastresume")), "d4:abcde");
    put(cfg.filePath(QStringLiteral("ed3k/node.key")), QByteArray(32, 'k'));
    put(cfg.filePath(QStringLiteral("emulecored.log")), "log");
    put(cfg.filePath(QStringLiteral("emulecored.lock")), "1");
    put(cfg.filePath(QStringLiteral("I2P/netDb/r1.dat")), "x");
    put(cfg.filePath(QStringLiteral("crashes/a.dmp")), "x");

    const auto out = ConfigBackup::run(cfg.path(), dest.path());
    QVERIFY2(out.has_value(), qPrintable(out ? QString() : out.error()));
    QCOMPARE(out->files, 3);
    QCOMPARE(out->failed, 0);
    const QDir b(out->folder);
    QVERIFY(QFileInfo(out->folder).fileName().startsWith(QStringLiteral("eMuleQt-config-")));
    QCOMPARE(slurp(b.filePath(QStringLiteral("preferences.yml"))), QByteArray("a: 1\n"));
    QCOMPARE(slurp(b.filePath(QStringLiteral("BT/resume/x.fastresume"))), QByteArray("d4:abcde"));
    QCOMPARE(slurp(b.filePath(QStringLiteral("ed3k/node.key"))), QByteArray(32, 'k'));
    QVERIFY(!QFileInfo::exists(b.filePath(QStringLiteral("emulecored.log"))));
    QVERIFY(!QFileInfo::exists(b.filePath(QStringLiteral("I2P/netDb"))));
    QVERIFY(!QFileInfo::exists(b.filePath(QStringLiteral("crashes"))));

    // a second one the same second: its own folder
    const auto again = ConfigBackup::run(cfg.path(), dest.path());
    QVERIFY(again.has_value());
    QVERIFY(again->folder != out->folder);
}

void tst_ConfigBackup::refusesAFolderInsideTheConfig()
{
    QTemporaryDir cfg;
    put(cfg.filePath(QStringLiteral("preferences.yml")), "a: 1\n");
    QDir(cfg.path()).mkpath(QStringLiteral("backups"));
    QVERIFY(!ConfigBackup::run(cfg.path(), cfg.filePath(QStringLiteral("backups"))).has_value());
    QVERIFY(!ConfigBackup::run(cfg.path(), cfg.path()).has_value());
}

void tst_ConfigBackup::refusesAMissingFolder()
{
    QTemporaryDir cfg;
    put(cfg.filePath(QStringLiteral("preferences.yml")), "a: 1\n");
    QVERIFY(!ConfigBackup::run(cfg.path(), cfg.filePath(QStringLiteral("../no-such-dir-268"))).has_value());
}

void tst_ConfigBackup::skipRules()
{
    QVERIFY(ConfigBackup::skipped(QStringLiteral("emulecored.log")));
    QVERIFY(ConfigBackup::skipped(QStringLiteral("emulecored.log.1")));
    QVERIFY(ConfigBackup::skipped(QStringLiteral("Usenet/x.nzbstate.backup")));
    QVERIFY(ConfigBackup::skipped(QStringLiteral("ed3k/resolver/a.jar")));
    QVERIFY(!ConfigBackup::skipped(QStringLiteral("clients.met.bak")));
    QVERIFY(!ConfigBackup::skipped(QStringLiteral("logins.yml")));
    QVERIFY(!ConfigBackup::skipped(QStringLiteral("ed3k/resolver.json")));
}

void tst_ConfigBackup::saveKeepsThePreviousFileAsBak()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.json"));
    QVERIFY(BackgroundFileWriter::saveFileKeepBak(path, "one"));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".bak")));   // nothing before
    QVERIFY(BackgroundFileWriter::saveFileKeepBak(path, "two"));
    QCOMPARE(slurp(path), QByteArray("two"));
    QCOMPARE(slurp(path + QStringLiteral(".bak")), QByteArray("one"));
    // an empty file is no backup: the good .bak stays
    put(path, {});
    QVERIFY(BackgroundFileWriter::saveFileKeepBak(path, "three"));
    QCOMPARE(slurp(path + QStringLiteral(".bak")), QByteArray("one"));
}

void tst_ConfigBackup::removeWithBakDropsBoth()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("t.json"));
    QVERIFY(BackgroundFileWriter::saveFileKeepBak(path, "one"));
    QVERIFY(BackgroundFileWriter::saveFileKeepBak(path, "two"));
    BackgroundFileWriter::instance().removeWithBak(path);
    QVERIFY(!QFileInfo::exists(path));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".bak")));
}

QTEST_MAIN(tst_ConfigBackup)
#include "tst_ConfigBackup.moc"
