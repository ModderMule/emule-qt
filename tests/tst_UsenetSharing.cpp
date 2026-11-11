/// @file tst_UsenetSharing.cpp
/// @brief Usenet scratch must never reach the ED2K network; completed files must.
///
/// Two requirements that pull in opposite directions, and the failure mode of the
/// first is silent: nothing errors if a half-written article gets published, the
/// client just quietly advertises garbage to every peer that asks. So this covers
/// each guard independently rather than trusting that one of them holds.
///
///   1. The share scan does not recurse, so a Usenet folder nested under a shared
///      directory is never walked.
///   2. `.usenetpart` is skipped by name, which is what covers the two cases (1)
///      does not: a user who shares the Usenet temp directory itself, and the
///      cross-volume completion where a file is briefly *inside* the incoming
///      directory while it copies.
///   3. shouldBeShared() refuses the Usenet tree ahead of the incoming-directory
///      rule, which otherwise returns true unconditionally.
///   4. A file that has completed into the incoming directory is shared normally.
///   5. usenetShareFiles off (#177): landed files leave the share, a rescan of
///      incoming does not bring them back, and on shares them again.

#include "TestHelpers.h"

#include "LandedFiles.h"

#include "files/KnownFile.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"
#include "prefs/Preferences.h"

#include <QDir>
#include <QFile>
#include <QTest>

using namespace eMule;

namespace {

QString writeFile(const QString& dir, const QString& name, const QByteArray& content)
{
    QDir().mkpath(dir);
    QFile f(QDir(dir).filePath(name));
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(content);
    f.close();
    return QDir(dir).filePath(name);
}

bool isShared(const SharedFileList& shared, const QString& name)
{
    bool found = false;
    shared.forEachFile([&](KnownFile* f) {
        if (f->fileName() == name)
            found = true;
    });
    return found;
}

} // namespace

class tst_UsenetSharing : public QObject {
    Q_OBJECT

private slots:
    void tempDirIsDerivedFromTheFirstTempDir();
    void siblingDirectoryIsNotMistakenForTheUsenetTree();
    void scratchIsRefusedEvenInsideTheIncomingDirectory();
    void partSuffixIsSkippedByTheShareScan();
    void completedFileInIncomingIsShared();
    void inProgressFileCannotBeSharedByHand();
    void shareSwitchKeepsLandedFilesOutUntilSwitchedBackOn();
};

void tst_UsenetSharing::tempDirIsDerivedFromTheFirstTempDir()
{
    eMule::testing::TempDir tmp;
    const QString temp = tmp.filePath(QStringLiteral("temp"));

    thePrefs.setTempDirs({temp});
    QCOMPARE(thePrefs.usenetTempDir(), QDir(temp).filePath(QStringLiteral("Usenet")));

    // No temp directory configured is not a crash and not a match — it must not
    // collapse to something that accidentally covers the whole filesystem.
    thePrefs.setTempDirs({});
    QVERIFY(thePrefs.usenetTempDir().isEmpty());
    QVERIFY(!thePrefs.isUsenetTempPath(QStringLiteral("/anything")));
}

void tst_UsenetSharing::siblingDirectoryIsNotMistakenForTheUsenetTree()
{
    eMule::testing::TempDir tmp;
    const QString temp = tmp.filePath(QStringLiteral("temp"));
    thePrefs.setTempDirs({temp});

    const QString root = QDir(temp).filePath(QStringLiteral("Usenet"));

    QVERIFY(thePrefs.isUsenetTempPath(root));
    QVERIFY(thePrefs.isUsenetTempPath(QDir(root).filePath(QStringLiteral("a/b.usenetpart"))));

    // A prefix match without a separator would swallow this one, and it is an
    // ordinary directory the user may well be sharing.
    QVERIFY(!thePrefs.isUsenetTempPath(QDir(temp).filePath(QStringLiteral("Usenet Archive"))));
    QVERIFY(!thePrefs.isUsenetTempPath(QDir(temp).filePath(QStringLiteral("other/file.bin"))));
}

void tst_UsenetSharing::scratchIsRefusedEvenInsideTheIncomingDirectory()
{
    eMule::testing::TempDir tmp;
    const QString incoming = tmp.filePath(QStringLiteral("incoming"));

    // The pathological configuration: temp inside incoming. shouldBeShared()
    // returns true unconditionally for the incoming directory, so if the Usenet
    // check ran after it every in-progress article would be advertised.
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(incoming);
    thePrefs.setTempDirs({incoming});
    thePrefs.setSharedDirs({});

    const QString usenetDir = QDir(incoming).filePath(QStringLiteral("Usenet"));
    const QString scratch = writeFile(usenetDir, QStringLiteral("x.usenetpart"),
                                      QByteArray(256, 'u'));
    QVERIFY(!scratch.isEmpty());

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    QVERIFY2(!shared.shouldBeShared(usenetDir, scratch, false),
             "Usenet scratch must be refused ahead of the incoming-directory rule");
    QVERIFY2(!shared.shouldBeShared(usenetDir, scratch, /*mustBeShared=*/true),
             "not even the forced-on path may share it");
}

void tst_UsenetSharing::partSuffixIsSkippedByTheShareScan()
{
    eMule::testing::TempDir tmp;
    const QString shareDir = tmp.filePath(QStringLiteral("share"));

    // The user has shared the directory the scratch file sits in. The scan must
    // still skip it, on the suffix alone.
    const QString scratch = writeFile(shareDir, QStringLiteral("half.usenetpart"),
                                      QByteArray(4096, 'p'));
    const QString normal = writeFile(shareDir, QStringLiteral("real.bin"),
                                     QByteArray(4096, 'r'));
    QVERIFY(!scratch.isEmpty() && !normal.isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setSharedDirs({shareDir});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    shared.reload();

    // The scan queues files for hashing; wait for the ordinary one to land, which
    // is also proof the scan ran at all rather than the assertion passing because
    // nothing happened.
    QTRY_VERIFY_WITH_TIMEOUT(shared.getCount() >= 1, 15000);

    bool sawScratch = false;
    bool sawNormal = false;
    shared.forEachFile([&](KnownFile* f) {
        if (f->fileName().endsWith(QStringLiteral(".usenetpart")))
            sawScratch = true;
        if (f->fileName() == QStringLiteral("real.bin"))
            sawNormal = true;
    });

    QVERIFY2(sawNormal, "the scan must have run for this test to mean anything");
    QVERIFY2(!sawScratch, "a .usenetpart file must never be shared");
}

void tst_UsenetSharing::completedFileInIncomingIsShared()
{
    eMule::testing::TempDir tmp;
    const QString incoming = tmp.filePath(QStringLiteral("incoming"));
    const QString done = writeFile(incoming, QStringLiteral("release.mkv"),
                                   QByteArray(4096, 'd'));
    QVERIFY(!done.isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(incoming);
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setSharedDirs({});

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    // This is the other half of the requirement: once it lands in incoming it is
    // an ordinary file, and the incoming directory is always shared.
    QVERIFY(shared.shouldBeShared(incoming, done, false));
    QVERIFY2(shared.addFileInSharedLocation(done),
             "the queue calls this on completion instead of a full rescan");
    QVERIFY2(!shared.addSingleSharedFile(done),
             "addSingleSharedFile refuses the incoming directory — "
             "isShareableDirectory() excludes it, which is why the queue "
             "must not use it");
}

void tst_UsenetSharing::inProgressFileCannotBeSharedByHand()
{
    eMule::testing::TempDir tmp;
    const QString temp = tmp.filePath(QStringLiteral("temp"));
    const QString usenetDir = QDir(temp).filePath(QStringLiteral("Usenet"));
    const QString scratch = writeFile(usenetDir, QStringLiteral("wip.usenetpart"),
                                      QByteArray(256, 'w'));
    QVERIFY(!scratch.isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setTempDirs({temp});
    thePrefs.setSharedDirs({usenetDir});   // the user asked for it explicitly

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);

    // Refused outright rather than silently: without the explicit check the entry
    // would land in sharedfiles.dat, persist, and share nothing.
    QVERIFY2(!shared.addSingleSharedFile(scratch),
             "an in-progress Usenet file must be refused, not silently ignored");
}

/// The daemon wiring (DaemonApp: m_usenetLanded + setNeverShared), switched both
/// ways. Incoming is always shared, so without the block the next scan would
/// bring a finished Usenet file straight back.
void tst_UsenetSharing::shareSwitchKeepsLandedFilesOutUntilSwitchedBackOn()
{
    eMule::testing::TempDir tmp;
    const QString incoming = tmp.filePath(QStringLiteral("incoming"));
    const QString usenetFile = writeFile(incoming, QStringLiteral("usenet.mkv"),
                                         QByteArray(4096, 'n'));
    const QString otherFile = writeFile(incoming, QStringLiteral("other.bin"),
                                        QByteArray(4096, 'o'));
    QVERIFY(!usenetFile.isEmpty() && !otherFile.isEmpty());

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setIncomingDir(incoming);
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setSharedDirs({});

    const QString listPath = tmp.filePath(QStringLiteral("Usenet/landed.txt"));
    LandedFiles landed(listPath);
    landed.load();
    landed.add({usenetFile});   // UsenetQueue::filesPublished, recorded with sharing on too

    KnownFileList knownFiles;
    SharedFileList shared(&knownFiles);
    shared.setNeverShared([&landed](const QString& path) { return landed.blocks(path); });

    // on (default): both shared
    shared.reload();
    QTRY_VERIFY_WITH_TIMEOUT(isShared(shared, QStringLiteral("usenet.mkv"))
                             && isShared(shared, QStringLiteral("other.bin")), 15000);

    // off: dropped at once, the other file stays
    QVERIFY(landed.setBlocking(true));
    QVERIFY2(!landed.setBlocking(true), "unchanged reports false, so the daemon skips the rescan");
    shared.dropNeverShared();
    QVERIFY2(!isShared(shared, QStringLiteral("usenet.mkv")), "switching off must unshare now");
    QVERIFY(isShared(shared, QStringLiteral("other.bin")));

    // a rescan of incoming does not bring it back
    shared.reload();
    QTRY_VERIFY_WITH_TIMEOUT(isShared(shared, QStringLiteral("other.bin")), 15000);
    QTest::qWait(200);
    QVERIFY2(!isShared(shared, QStringLiteral("usenet.mkv")), "the incoming scan must skip it");
    QVERIFY2(!shared.addFileInSharedLocation(usenetFile), "nor may a single add share it");

    // after a restart the list still names it
    LandedFiles reloaded(listPath);
    reloaded.load();
    QVERIFY(reloaded.contains(usenetFile));
    reloaded.setBlocking(true);
    QVERIFY(reloaded.blocks(usenetFile));
    QVERIFY(!reloaded.blocks(otherFile));

    // on again: rescan shares it
    QVERIFY(landed.setBlocking(false));
    shared.reload();
    QTRY_VERIFY_WITH_TIMEOUT(isShared(shared, QStringLiteral("usenet.mkv")), 15000);
    QVERIFY(isShared(shared, QStringLiteral("other.bin")));
}

QTEST_MAIN(tst_UsenetSharing)
#include "tst_UsenetSharing.moc"
