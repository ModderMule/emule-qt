/// @file tst_BtShareGuard.cpp
/// @brief Unfinished torrents stay out of the eD2K / ed3k share (#124); with sharing
/// switched off finished torrents and hoster downloads do too (#177).

#include "utils/BackgroundFileWriter.h"
#include "BtShareGuard.h"
#include "LandedFiles.h"
#include "files/SharedFileList.h"

#include "TestHelpers.h"

#include <QDir>
#include <QFile>
#include <QTest>

using namespace eMule;
using namespace eMule::bt;

class tst_BtShareGuard : public QObject {
    Q_OBJECT

private slots:
    void unfinished_blocksTheWholeContent();
    void done_releasesTheTreeAndKeepsSkippedFilesOut();
    void removed_keepsItsBlocks();
    void finished_listsCompleteFilesOnce();
    void restart_completedStaysSharedWhileChecked();
    void shareFinishedOff_blocksDoneTorrents();
    void shareModePublic_blocksPrivateAndI2p();
    void landedFiles_blockOnlyWhileOff();
};

namespace {

BtTorrentRow row(const QString& savePath, const QString& name, double progress, BtState state)
{
    BtTorrentRow r;
    r.id = QStringLiteral("bt:v1:AA");
    r.savePath = savePath;
    r.name = name;
    r.progress = progress;
    r.state = state;
    return r;
}

} // namespace

void tst_BtShareGuard::unfinished_blocksTheWholeContent()
{
    BtShareGuard guard;
    int asked = 0;
    const auto files = [&asked] { ++asked; return QList<BtFileRow>{}; };
    const auto c = guard.update(QStringLiteral("t"), row(QStringLiteral("/in"), QStringLiteral("Album"), 0.5, BtState::Downloading), files);
    QVERIFY(c.blockedMore);
    QVERIFY(c.released.isEmpty());
    QCOMPARE(asked, 0);
    QVERIFY(guard.blocks(QStringLiteral("/in/Album/CD1/01.mp3")));
    QVERIFY(guard.blocks(QStringLiteral("/IN/album/02.mp3")));   // any spelling (pathKey)
    QVERIFY(!guard.blocks(QStringLiteral("/in/Album 2/01.mp3")));
    QVERIFY(!guard.blocks(QStringLiteral("/in/other.mkv")));
    // a done torrent whose data is being checked is not done
    guard.update(QStringLiteral("t"), row(QStringLiteral("/in"), QStringLiteral("Album"), 1.0, BtState::CheckingFiles), files);
    QVERIFY(guard.blocks(QStringLiteral("/in/Album/02.mp3")));
    QCOMPARE(asked, 0);
}

void tst_BtShareGuard::done_releasesTheTreeAndKeepsSkippedFilesOut()
{
    eMule::testing::TempDir tmp;
    const QString save = tmp.path();
    QVERIFY(QDir().mkpath(save + QStringLiteral("/Album/CD1")));

    BtShareGuard guard;
    int asked = 0;
    const auto files = [&asked] {
        ++asked;
        BtFileRow done;
        done.path = QStringLiteral("Album/CD1/01.mp3");
        done.size = done.done = 100;
        BtFileRow skipped;
        skipped.path = QStringLiteral("Album/extras.zip");
        skipped.size = 100;
        skipped.done = 30;
        skipped.priority = 0;
        BtFileRow pad;
        pad.path = QStringLiteral(".pad/50");
        pad.size = 50;
        pad.pad = true;
        return QList<BtFileRow>{done, skipped, pad};
    };
    guard.update(QStringLiteral("t"), row(save, QStringLiteral("Album"), 0.9, BtState::Downloading), files);
    const auto c = guard.update(QStringLiteral("t"), row(save, QStringLiteral("Album"), 1.0, BtState::Seeding), files);
    QCOMPARE(asked, 1);
    QVERIFY(c.released.contains(QDir::cleanPath(save + QStringLiteral("/Album"))));
    QVERIFY2(c.released.contains(QDir::cleanPath(save)), "a single-file torrent's folder");
    QVERIFY(!guard.blocks(save + QStringLiteral("/Album/CD1/01.mp3")));
    QVERIFY(guard.blocks(save + QStringLiteral("/Album/extras.zip")));

    // seeding ticks: no file list each time
    const auto again = guard.update(QStringLiteral("t"), row(save, QStringLiteral("Album"), 1.0, BtState::Seeding), files);
    QCOMPARE(asked, 1);
    QVERIFY(again.released.isEmpty() && !again.blockedMore);

    // a skipped file picked again: the whole torrent is out once more
    const auto back = guard.update(QStringLiteral("t"), row(save, QStringLiteral("Album"), 0.95, BtState::Downloading), files);
    QVERIFY(back.blockedMore);
    QVERIFY(guard.blocks(save + QStringLiteral("/Album/CD1/01.mp3")));
}

void tst_BtShareGuard::removed_keepsItsBlocks()
{
    BtShareGuard guard;
    guard.update(QStringLiteral("t"), row(QStringLiteral("/in"), QStringLiteral("movie.mkv"), 0.2, BtState::Paused), {});
    QVERIFY(guard.blocks(QStringLiteral("/in/movie.mkv")));
    const auto c = guard.update(QStringLiteral("t"), std::nullopt, {});
    QVERIFY(c.released.isEmpty());
    QVERIFY2(guard.blocks(QStringLiteral("/in/movie.mkv")), "its partial file stays out");
}

// #131: finished files are listed for one-by-one sharing, once, and only after a download
void tst_BtShareGuard::finished_listsCompleteFilesOnce()
{
    const auto files = [] {
        BtFileRow done;
        done.path = QStringLiteral("Album/01.mp3");
        done.size = done.done = 100;
        BtFileRow skipped;
        skipped.path = QStringLiteral("Album/extras.zip");
        skipped.size = 100;
        BtFileRow pad;
        pad.path = QStringLiteral(".pad/50");
        pad.size = pad.done = 50;
        pad.pad = true;
        return QList<BtFileRow>{done, skipped, pad};
    };
    const QString id = QStringLiteral("t");
    const QString save = QStringLiteral("/bt");
    const QString album = QStringLiteral("Album");

    BtShareGuard guard;
    QVERIFY(guard.update(id, row(save, album, 0.5, BtState::Downloading), files).finished.isEmpty());
    // a move between done and seeding keeps it "downloaded this session"
    QVERIFY(guard.update(id, row(save, album, 1.0, BtState::Moving), files).finished.isEmpty());
    const auto c = guard.update(id, row(save, album, 1.0, BtState::Seeding), files);
    QCOMPARE(c.finished, QStringList{QStringLiteral("/bt/Album/01.mp3")});
    QVERIFY(guard.update(id, row(save, album, 1.0, BtState::Seeding), files).finished.isEmpty());

    // loaded complete (a restart): nothing listed, an unshared file stays unshared
    BtShareGuard restart;
    QVERIFY(restart.update(id, row(save, album, 0.0, BtState::CheckingResume), files).finished.isEmpty());
    QVERIFY(restart.update(id, row(save, album, 1.0, BtState::Seeding), files).finished.isEmpty());
}

// #122: a torrent completed before is not blocked while its resume data is checked
void tst_BtShareGuard::restart_completedStaysSharedWhileChecked()
{
    const auto files = [] {
        BtFileRow done;
        done.path = QStringLiteral("Album/01.mp3");
        done.size = done.done = 100;
        BtFileRow skipped;
        skipped.path = QStringLiteral("Album/extras.zip");
        skipped.size = 100;
        return QList<BtFileRow>{done, skipped};
    };
    const QString id = QStringLiteral("t");
    BtTorrentRow checking = row(QStringLiteral("/bt"), QStringLiteral("Album"), 0.0, BtState::CheckingResume);
    checking.completedAt = 1'700'000'000;

    BtShareGuard guard;
    const auto c = guard.update(id, checking, files);
    QVERIFY(!c.blockedMore);
    QVERIFY(!guard.blocks(QStringLiteral("/bt/Album/01.mp3")));
    checking.state = BtState::CheckingFiles;
    QVERIFY(!guard.update(id, checking, files).blockedMore);

    // settled: only the skipped file goes out, nothing counts as just finished
    BtTorrentRow seeding = checking;
    seeding.state = BtState::Seeding;
    seeding.progress = 1.0;
    const auto s = guard.update(id, seeding, files);
    QVERIFY(s.blockedMore);
    QVERIFY(s.finished.isEmpty());
    QVERIFY(!guard.blocks(QStringLiteral("/bt/Album/01.mp3")));
    QVERIFY(guard.blocks(QStringLiteral("/bt/Album/extras.zip")));

    // never completed: blocked from the start, as before
    BtShareGuard fresh;
    QVERIFY(fresh.update(id, row(QStringLiteral("/bt"), QStringLiteral("Album"), 0.0, BtState::CheckingResume), files).blockedMore);
    QVERIFY(fresh.blocks(QStringLiteral("/bt/Album/01.mp3")));
}

void tst_BtShareGuard::shareFinishedOff_blocksDoneTorrents()
{
    const auto files = [] {
        BtFileRow f;
        f.path = QStringLiteral("Album/01.mp3");
        f.size = f.done = 100;
        return QList<BtFileRow>{f};
    };
    BtShareGuard guard;
    const QString id = QStringLiteral("t");
    guard.update(id, row(QStringLiteral("/in"), QStringLiteral("Album"), 0.5, BtState::Downloading), files);
    const auto done = guard.update(id, row(QStringLiteral("/in"), QStringLiteral("Album"), 1.0, BtState::Seeding), files);
    QCOMPARE(done.finished, QStringList{QStringLiteral("/in/Album/01.mp3")});
    QVERIFY(!guard.blocks(QStringLiteral("/in/Album/01.mp3")));

    QVERIFY(guard.setShareMode(BtShareMode::Off));
    QVERIFY(!guard.setShareMode(BtShareMode::Off));   // unchanged
    QVERIFY(guard.blocks(QStringLiteral("/in/Album/01.mp3")));
    QVERIFY(guard.blocks(QStringLiteral("/in/Album/new/02.mp3")));
    QVERIFY(!guard.blocks(QStringLiteral("/in/Album 2/01.mp3")));
    QVERIFY(!guard.blocks(QStringLiteral("/in/other.mkv")));

    // a single-file torrent, and one loaded complete at a restart (no entry: checking)
    BtTorrentRow checking = row(QStringLiteral("/in"), QStringLiteral("movie.mkv"), 0.0, BtState::CheckingResume);
    checking.completedAt = 1'700'000'000;
    guard.update(QStringLiteral("u"), checking, files);
    QVERIFY(guard.blocks(QStringLiteral("/in/movie.mkv")));
    // removed: still out for the session
    guard.update(id, std::nullopt, files);
    QVERIFY(guard.blocks(QStringLiteral("/in/Album/01.mp3")));

    QVERIFY(guard.setShareMode(BtShareMode::All));
    QVERIFY(!guard.blocks(QStringLiteral("/in/Album/01.mp3")));
    QVERIFY(!guard.blocks(QStringLiteral("/in/movie.mkv")));
}

void tst_BtShareGuard::shareModePublic_blocksPrivateAndI2p()
{
    const auto files = [] { return QList<BtFileRow>{}; };
    BtShareGuard guard;
    QVERIFY(guard.setShareMode(BtShareMode::Public));
    BtTorrentRow pub = row(QStringLiteral("/in"), QStringLiteral("Public"), 1.0, BtState::Seeding);
    BtTorrentRow priv = row(QStringLiteral("/in"), QStringLiteral("Private"), 1.0, BtState::Seeding);
    priv.isPrivate = true;
    BtTorrentRow i2p = row(QStringLiteral("/in"), QStringLiteral("Anon.mkv"), 1.0, BtState::Seeding);
    i2p.i2p = true;
    QVERIFY(!guard.update(QStringLiteral("a"), pub, files).blockedMore);
    QVERIFY(guard.update(QStringLiteral("b"), priv, files).blockedMore);   // shared before: drop now
    QVERIFY(guard.update(QStringLiteral("c"), i2p, files).blockedMore);
    QVERIFY(!guard.update(QStringLiteral("b"), priv, files).blockedMore);  // no news
    QVERIFY(!guard.blocks(QStringLiteral("/in/Public/01.mp3")));
    QVERIFY(guard.blocks(QStringLiteral("/in/Private/01.mp3")));
    QVERIFY(guard.blocks(QStringLiteral("/in/Anon.mkv")));

    QVERIFY(guard.setShareMode(BtShareMode::Off));
    QVERIFY(guard.blocks(QStringLiteral("/in/Public/01.mp3")));
    QVERIFY(guard.setShareMode(BtShareMode::All));
    QVERIFY(!guard.blocks(QStringLiteral("/in/Private/01.mp3")));
    QVERIFY(!guard.blocks(QStringLiteral("/in/Anon.mkv")));
}

void tst_BtShareGuard::landedFiles_blockOnlyWhileOff()
{
    eMule::testing::TempDir tmp;
    const QString list = tmp.path() + QStringLiteral("/ed3k/hoster/landed.txt");
    {
        LandedFiles landed(list);
        landed.load();   // no file yet
        QCOMPARE(landed.size(), 0);
        landed.add({QStringLiteral("/in/pkg/a.bin"), QStringLiteral("/in/b.bin"), QStringLiteral("/IN/B.BIN"), QString()});
        eMule::BackgroundFileWriter::instance().flush();   // saves are queued (report 43)
        QVERIFY(QFile::exists(list));
        QVERIFY(landed.contains(QStringLiteral("/in/b.bin")));
        QVERIFY(!landed.blocks(QStringLiteral("/in/b.bin")));   // sharing on by default
        QVERIFY(landed.setBlocking(true));
        QVERIFY(landed.blocks(QStringLiteral("/in/pkg/a.bin")));
        QVERIFY(!landed.blocks(QStringLiteral("/in/pkg/c.bin")));
    }
    LandedFiles again(list);
    again.load();
    QVERIFY(again.contains(QStringLiteral("/in/pkg/a.bin")));
    QVERIFY(again.contains(QStringLiteral("/in/b.bin")));
    QCOMPARE(again.size(), SharedFileList::pathKey(QStringLiteral("/in/b.bin")) == SharedFileList::pathKey(QStringLiteral("/IN/B.BIN")) ? 2 : 3);
}

QTEST_GUILESS_MAIN(tst_BtShareGuard)
#include "tst_BtShareGuard.moc"
