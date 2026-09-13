/// @file tst_UsenetHistory.cpp
/// @brief What a release leaves behind when it stops being a queue item.
///
/// Kept apart from tst_UsenetQueue for the reason tst_UsenetUsage is: none of it
/// needs a socket, a thread or a download, and the questions it answers are ones
/// a download test cannot see — does clearing a finished row read back as giving
/// up, and does turning a preference off actually forget.

#include "TestHelpers.h"

#include "nzb/NzbFile.h"
#include "nzb/NzbInfo.h"
#include "prefs/Preferences.h"
#include "queue/UsenetHealth.h"
#include "queue/UsenetHistory.h"
#include "queue/UsenetQueueItem.h"
#include "queue/UsenetQueueStore.h"

#include <QFile>
#include <QTest>

using namespace eMule;
using namespace eMule::usenet;

namespace {

/// One release, with @p segments articles whose message-ids are derived from
/// @p stem — so two stems are two different releases and one stem twice is one.
QByteArray makeNzb(const QString& stem, int segments)
{
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n"
        "  <file poster=\"p@example.com\" date=\"1700000000\" "
        "subject=\"&quot;%1&quot; yEnc (1/%2)\">\n"
        "    <groups><group>alt.binaries.test</group></groups>\n"
        "    <segments>\n").arg(stem).arg(segments);
    for (int i = 1; i <= segments; ++i) {
        xml += QStringLiteral("      <segment bytes=\"1000\" number=\"%1\">"
                              "%2-%1@example.com</segment>\n").arg(i).arg(stem);
    }
    xml += QStringLiteral("    </segments>\n  </file>\n</nzb>\n");
    return xml.toUtf8();
}

/// A queue item as UsenetHistory sees one: a name and a parsed NZB.
UsenetQueueItem itemFor(const QString& stem, int segments = 3)
{
    UsenetQueueItem item;
    item.id = stem;
    item.name = stem;
    QString error;
    [[maybe_unused]] const bool ok = NzbFile::parse(makeNzb(stem, segments), item.nzb, error);
    Q_ASSERT(ok);
    item.articleDigest = nzbArticleDigest(item.nzb);
    return item;
}

void rememberEverything()
{
    thePrefs.setRememberDownloadedFiles(true);
    thePrefs.setRememberCancelledFiles(true);
}

} // namespace

class tst_UsenetHistory : public QObject {
    Q_OBJECT

private slots:
    void aCompletedReleaseIsRememberedAfterItsRowIsCleared();
    void aRemovedUnfinishedReleaseIsRememberedAsCancelled();
    void clearingTheRowOfACompletedReleaseDoesNotDowngradeItToCancelled();
    void theHistorySurvivesARestart();
    void theHistoryIsForgottenWhenTheUserAsksUsNotToRemember();
    void theHistoryEvictsItsOldestEntries();
    void anNzbWithNoMessageIdsIsNotRecorded();
    void aReleaseIsFoundByItsNameAsWellAsItsArticles();
};

void tst_UsenetHistory::aCompletedReleaseIsRememberedAfterItsRowIsCleared()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    rememberEverything();

    const UsenetQueueItem item = itemFor(QStringLiteral("done"));

    UsenetHistory history;
    QVERIFY(!history.findByDigest(item.articleDigest));
    history.record(item, UsenetHistoryState::Downloaded);

    const UsenetHistoryEntry* found = history.findByDigest(item.articleDigest);
    QVERIFY(found);
    QCOMPARE(found->state, UsenetHistoryState::Downloaded);
    QCOMPARE(found->name, QStringLiteral("done"));
    QCOMPARE(history.count(), 1);
}

void tst_UsenetHistory::aRemovedUnfinishedReleaseIsRememberedAsCancelled()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    rememberEverything();

    const UsenetQueueItem item = itemFor(QStringLiteral("gaveup"));

    UsenetHistory history;
    history.record(item, UsenetHistoryState::Cancelled);

    const UsenetHistoryEntry* found = history.findByDigest(item.articleDigest);
    QVERIFY(found);
    QCOMPARE(found->state, UsenetHistoryState::Cancelled);
}

void tst_UsenetHistory::clearingTheRowOfACompletedReleaseDoesNotDowngradeItToCancelled()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    rememberEverything();

    const UsenetQueueItem item = itemFor(QStringLiteral("finished"));

    UsenetHistory history;
    // What actually happens: onPostFinished() records the completion, and the user
    // clears the row later, which records again. The second call must not undo the
    // first, or "you already downloaded this" becomes "you gave up on this".
    history.record(item, UsenetHistoryState::Downloaded);
    history.record(item, UsenetHistoryState::Cancelled);

    const UsenetHistoryEntry* found = history.findByDigest(item.articleDigest);
    QVERIFY(found);
    QCOMPARE(found->state, UsenetHistoryState::Downloaded);
    QCOMPARE(history.count(), 1);   // recorded twice, remembered once

    // The other direction is not monotone: something abandoned and later
    // downloaded for real is downloaded.
    const UsenetQueueItem second = itemFor(QStringLiteral("retried"));
    history.record(second, UsenetHistoryState::Cancelled);
    history.record(second, UsenetHistoryState::Downloaded);
    QCOMPARE(history.findByDigest(second.articleDigest)->state,
             UsenetHistoryState::Downloaded);
}

void tst_UsenetHistory::theHistorySurvivesARestart()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    rememberEverything();

    const UsenetQueueItem done = itemFor(QStringLiteral("kept"));
    const UsenetQueueItem gone = itemFor(QStringLiteral("dropped"));

    {
        UsenetHistory history;
        history.record(done, UsenetHistoryState::Downloaded);
        history.record(gone, UsenetHistoryState::Cancelled);
        QVERIFY(history.save());
    }

    QVERIFY(QFile::exists(UsenetHistory::historyPath()));

    UsenetHistory reloaded;
    reloaded.load();
    QCOMPARE(reloaded.count(), 2);
    QCOMPARE(reloaded.findByDigest(done.articleDigest)->state,
             UsenetHistoryState::Downloaded);
    QCOMPARE(reloaded.findByDigest(gone.articleDigest)->state,
             UsenetHistoryState::Cancelled);
}

void tst_UsenetHistory::theHistoryIsForgottenWhenTheUserAsksUsNotToRemember()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    rememberEverything();

    const UsenetQueueItem done = itemFor(QStringLiteral("d"));
    const UsenetQueueItem cancelled = itemFor(QStringLiteral("c"));

    {
        UsenetHistory history;
        history.record(done, UsenetHistoryState::Downloaded);
        history.record(cancelled, UsenetHistoryState::Cancelled);
        QVERIFY(history.save());
    }

    // Off means forget, which is what cancelled.met has always done. Half of the
    // file goes; the other half, governed by the other preference, stays.
    thePrefs.setRememberDownloadedFiles(false);
    {
        UsenetHistory history;
        history.load();
        QCOMPARE(history.count(), 1);
        QVERIFY(!history.findByDigest(done.articleDigest));
        QVERIFY(history.findByDigest(cancelled.articleDigest));
        QVERIFY(history.save());
    }

    // And nothing new is written while it is off.
    {
        UsenetHistory history;
        history.record(itemFor(QStringLiteral("later")), UsenetHistoryState::Downloaded);
        QCOMPARE(history.count(), 1);
    }

    // Turning it back on does not resurrect what the save above dropped.
    thePrefs.setRememberDownloadedFiles(true);
    UsenetHistory back;
    back.load();
    QCOMPARE(back.count(), 1);
    QVERIFY(!back.findByDigest(done.articleDigest));
}

void tst_UsenetHistory::theHistoryEvictsItsOldestEntries()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    rememberEverything();

    UsenetHistory history;
    // One past the cap. Nothing below this catches an evicted release, so the cap
    // is the point at which the guard silently stops being able to see something.
    for (int i = 0; i < UsenetHistory::kMaxEntries + 1; ++i)
        history.record(itemFor(QStringLiteral("r%1").arg(i), 1), UsenetHistoryState::Downloaded);

    QCOMPARE(history.count(), UsenetHistory::kMaxEntries);
    // The newest is always kept — evicting what just happened would be the one
    // unambiguously wrong answer.
    const UsenetQueueItem newest = itemFor(QStringLiteral("r%1").arg(UsenetHistory::kMaxEntries), 1);
    QVERIFY(history.findByDigest(newest.articleDigest));
}

void tst_UsenetHistory::anNzbWithNoMessageIdsIsNotRecorded()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    rememberEverything();

    UsenetQueueItem empty;
    empty.name = QStringLiteral("nothing");

    UsenetHistory history;
    history.record(empty, UsenetHistoryState::Downloaded);
    // One empty-digest entry would answer for every future release that has none.
    QCOMPARE(history.count(), 0);
    QVERIFY(!history.findByDigest(QString()));
}

void tst_UsenetHistory::aReleaseIsFoundByItsNameAsWellAsItsArticles()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    rememberEverything();

    UsenetQueueItem item = itemFor(QStringLiteral("release"));
    item.name = QStringLiteral("Some.Release.Name-GROUP");

    UsenetHistory history;
    history.record(item, UsenetHistoryState::Downloaded);

    // The join a search row has to use: an indexer result carries a title, not a
    // set of message-ids, so this is the only way to mark it.
    QVERIFY(history.findByName(QStringLiteral("Some.Release.Name-GROUP")));
    QVERIFY(history.findByName(QStringLiteral("some_release_name-group.nzb")));
    QVERIFY(!history.findByName(QStringLiteral("Another.Release-GROUP")));
    QVERIFY(!history.findByName(QString()));
}

QTEST_MAIN(tst_UsenetHistory)
#include "tst_UsenetHistory.moc"
