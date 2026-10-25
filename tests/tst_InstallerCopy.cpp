/// @file tst_InstallerCopy.cpp
/// @brief Tests for app/InstallerCopy — the Windows setup shared from the Incoming dir.

#include "TestHelpers.h"
#include "app/InstallerCopy.h"

#include <QDir>
#include <QFile>
#include <QTest>

using namespace eMule;
using namespace eMule::testing;

namespace {

const QString kSetupV1 = QStringLiteral("eMuleQt-v0.6.30-win64-setup.exe");
const QString kSetupV2 = QStringLiteral("eMuleQt-v0.6.31-win64-setup.exe");

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

/// Install dir with setup/, a user config dir and an Incoming dir not yet created
struct Layout {
    TempDir root;
    QString staged = root.filePath(QStringLiteral("app/setup"));
    QString config = root.filePath(QStringLiteral("config"));
    QString incoming = root.filePath(QStringLiteral("user/Incoming"));

    Layout() { QDir().mkpath(config); }

    void stage(const QString& name, const QByteArray& data)
    {
        QDir(staged).removeRecursively();   // what [InstallDelete] does
        writeFile(staged + u'/' + name, data);
    }

    [[nodiscard]] InstallerCopy::Outcome run() const
    {
        return InstallerCopy::run(staged, config, incoming);
    }

    [[nodiscard]] QString inIncoming(const QString& name) const { return incoming + u'/' + name; }
};

} // namespace

class tst_InstallerCopy : public QObject {
    Q_OBJECT

private slots:
    void nothingStaged();
    void copiesIntoNewIncomingDir();
    void secondRunIsDone();
    void deletedCopyStaysDeleted();
    void newVersionCopiedOldKept();
    void existingFileNotOverwritten();
    void otherExeIgnored();
};

void tst_InstallerCopy::nothingStaged()
{
    Layout l;
    QCOMPARE(l.run().result, InstallerCopy::Result::NoInstaller);
    QVERIFY(!QDir(l.incoming).exists());
    QVERIFY(!QFile::exists(QDir(l.config).filePath(QLatin1StringView(InstallerCopy::kStateFileName))));
}

void tst_InstallerCopy::copiesIntoNewIncomingDir()
{
    Layout l;
    l.stage(kSetupV1, "setup-v1");
    const auto out = l.run();
    QCOMPARE(out.result, InstallerCopy::Result::Copied);
    QCOMPARE(out.fileName, kSetupV1);
    QCOMPARE(readFile(l.inIncoming(kSetupV1)), QByteArray("setup-v1"));
    QVERIFY(!QFile::exists(l.inIncoming(kSetupV1) + QStringLiteral(".part")));
    QCOMPARE(QDir(l.incoming).entryList(QDir::Files), QStringList{kSetupV1});
    // Staged copy stays for the other users of this install
    QVERIFY(QFile::exists(l.staged + u'/' + kSetupV1));
}

void tst_InstallerCopy::secondRunIsDone()
{
    Layout l;
    l.stage(kSetupV1, "setup-v1");
    QCOMPARE(l.run().result, InstallerCopy::Result::Copied);
    QCOMPARE(l.run().result, InstallerCopy::Result::AlreadyDone);
}

void tst_InstallerCopy::deletedCopyStaysDeleted()
{
    Layout l;
    l.stage(kSetupV1, "setup-v1");
    QCOMPARE(l.run().result, InstallerCopy::Result::Copied);
    QVERIFY(QFile::remove(l.inIncoming(kSetupV1)));
    QCOMPARE(l.run().result, InstallerCopy::Result::AlreadyDone);
    QVERIFY(!QFile::exists(l.inIncoming(kSetupV1)));
}

void tst_InstallerCopy::newVersionCopiedOldKept()
{
    Layout l;
    l.stage(kSetupV1, "setup-v1");
    QCOMPARE(l.run().result, InstallerCopy::Result::Copied);
    l.stage(kSetupV2, "setup-v2");
    QCOMPARE(l.run().result, InstallerCopy::Result::Copied);
    QCOMPARE(readFile(l.inIncoming(kSetupV1)), QByteArray("setup-v1"));
    QCOMPARE(readFile(l.inIncoming(kSetupV2)), QByteArray("setup-v2"));
}

void tst_InstallerCopy::existingFileNotOverwritten()
{
    Layout l;
    l.stage(kSetupV1, "setup-v1");
    writeFile(l.inIncoming(kSetupV1), "downloaded");
    QCOMPARE(l.run().result, InstallerCopy::Result::AlreadyThere);
    QCOMPARE(readFile(l.inIncoming(kSetupV1)), QByteArray("downloaded"));
    QCOMPARE(l.run().result, InstallerCopy::Result::AlreadyDone);
}

void tst_InstallerCopy::otherExeIgnored()
{
    Layout l;
    writeFile(l.staged + QStringLiteral("/setup.exe"), "renamed");
    QCOMPARE(l.run().result, InstallerCopy::Result::NoInstaller);
}

QTEST_MAIN(tst_InstallerCopy)
#include "tst_InstallerCopy.moc"
