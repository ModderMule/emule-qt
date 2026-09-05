/// @file tst_UsenetDirectUnpack.cpp
/// @brief Extraction driven while the volumes are still arriving.
///
/// The interesting property is not that it extracts — ArchiveReader already
/// does that — but that it *waits* for a volume that has not landed yet and
/// carries on when it does, without the caller ever assembling the set.

#include "RarFixtures.h"
#include "TestHelpers.h"

#include "post/UsenetDirectUnpack.h"
#include "queue/UsenetQueue.h"

#include "FakeNntpServer.h"
#include "UsenetPostingHarness.h"
#include "prefs/Preferences.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

using namespace eMule;
using namespace eMule::usenet;
using namespace eMule::testing;
using namespace eMule::testing::usenet;

namespace {

QByteArray patterned(qsizetype size)
{
    QByteArray out(size, '\0');
    for (qsizetype i = 0; i < size; ++i)
        out[i] = char('a' + (i % 26));
    return out;
}

/// Write one volume of a crafted stored set and return its path.
QString writeVolume(const QString& dir, int index, const QByteArray& bytes)
{
    const QString path = QDir(dir).filePath(
        index == 0 ? QStringLiteral("rel.rar")
                   : QStringLiteral("rel.r%1").arg(index - 1, 2, 10, QLatin1Char('0')));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(bytes);
    f.close();
    return path;
}

constexpr qint64 kVolumePayload = 3000;
constexpr int kVolumeCount = 5;
constexpr int kArticleSize = 1600;

/// Post a stored multi-volume set and return the NZB indexing it.
QByteArray postStoredSet(FakeNntpServer& server, QByteArray& innerOut)
{
    innerOut = patterned(kVolumePayload * kVolumeCount);
    const QList<QByteArray> volumes =
        eMule::testing::rar::makeStoredRarSet("Some.Release.mkv", innerOut, kVolumePayload);

    QList<PostedFile> files;
    for (int i = 0; i < volumes.size(); ++i) {
        files.append({QStringLiteral("Some.Release.part%1.rar")
                          .arg(i + 1, 2, 10, QLatin1Char('0')),
                      volumes.at(i)});
    }
    return postFiles(server, files, kArticleSize);
}

/// Run a release to completion and report whether the payload was already
/// extracted at the instant post-processing began.
struct RunOutcome {
    bool finishedOk = false;
    bool payloadReadyBeforePostProcessing = false;
    QByteArray published;
};

RunOutcome runRelease(bool directUnpack)
{
    RunOutcome outcome;

    FakeNntpServer server;
    if (!server.listen())
        return outcome;
    server.addGroup(QStringLiteral("alt.binaries.test"), 1, 1, 1);

    QByteArray inner;
    const QByteArray nzb = postStoredSet(server, inner);

    UsenetQueue queue;
    queue.applyServers({serverConfig(server.serverPort(), 2)}, 60);
    queue.setPostProcessingOptions({.par2 = false, .rename = false, .unpack = true,
                                    .cleanup = true, .directUnpack = directUnpack});
    queue.start();

    QString error;
    const QString id = queue.addNzb(nzb, QStringLiteral("release"), error);
    if (id.isEmpty())
        return outcome;

    // Sample the moment the item leaves Downloading. Direct unpack has to have
    // finished by then — checkItemCompletion() holds the item open until every
    // run has answered — so the payload is either there or it never was.
    const QString expected = QDir(QDir(thePrefs.usenetTempDir()).filePath(id))
                                 .filePath(QStringLiteral("_unpacked/Some.Release.mkv"));
    bool sampled = false;
    QObject::connect(&queue, &UsenetQueue::itemChanged, &queue, [&](const QString& changed) {
        if (sampled || changed != id)
            return;
        const auto* item = queue.findItem(id);
        if (!item || !item->isPostProcessing())
            return;
        sampled = true;
        outcome.payloadReadyBeforePostProcessing =
            QFileInfo(expected).size() == inner.size();
    });

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    if (!finished.wait(60000))
        return outcome;
    outcome.finishedOk = finished.first().at(1).toBool();

    QFile out(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("Some.Release.mkv")));
    if (out.open(QIODevice::ReadOnly))
        outcome.published = out.readAll();

    queue.stop();
    return outcome;
}

} // namespace

class tst_UsenetDirectUnpack : public QObject {
    Q_OBJECT

private slots:
    void volumesOfferedOneAtATimeExtractInFull();
    void aRunWaitsForAVolumeThatHasNotLanded();
    void cancelMidSetLeavesNoPartialOutput();
    void aSetThatEndsShortFailsRatherThanHanging();
    void aDownloadedSetIsAlreadyUnpackedWhenPostProcessingStarts();
    void withTheOptionOffTheSetIsUnpackedAtTheEndAsBefore();

private:
    /// Start a worker on its own thread and hand back both, already running.
    static void start(UsenetDirectUnpack*& worker, QThread*& thread,
                      const UsenetDirectUnpackJob& job);
    static void stop(UsenetDirectUnpack* worker, QThread* thread);
};

void tst_UsenetDirectUnpack::volumesOfferedOneAtATimeExtractInFull()
{
    eMule::testing::TempDir tmp;
    const QByteArray payload = patterned(40000);
    const QList<QByteArray> vols =
        eMule::testing::rar::makeStoredRarSet("movie.mkv", payload, 9000);
    QCOMPARE(vols.size(), 5);

    const QString dest = QDir(tmp.path()).filePath(QStringLiteral("_unpacked"));
    UsenetDirectUnpack* worker = nullptr;
    QThread* thread = nullptr;
    start(worker, thread, {QStringLiteral("item"), QStringLiteral("rel"), dest, {}});
    QSignalSpy spy(worker, &UsenetDirectUnpack::finished);

    for (int i = 0; i < vols.size(); ++i) {
        worker->offerVolume(i, writeVolume(tmp.path(), i, vols.at(i)));
        QTest::qWait(20);
    }
    worker->endOfSet();

    QVERIFY(spy.wait(10000));
    const auto result = spy.first().at(0).value<UsenetDirectUnpackResult>();
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.extracted.size(), 1);
    QCOMPARE(result.consumed.size(), 5);
    // Names the set for post-processing's skip list. Empty here means the set is
    // unpacked a second time at the end, silently undoing the whole feature.
    QVERIFY(result.firstVolume.endsWith(QStringLiteral("rel.rar")));

    QFile out(QDir(dest).filePath(QStringLiteral("movie.mkv")));
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.readAll(), payload);

    stop(worker, thread);
}

// The whole point: the run must block on a volume that is not there yet rather
// than treating its absence as the end of the archive.
void tst_UsenetDirectUnpack::aRunWaitsForAVolumeThatHasNotLanded()
{
    eMule::testing::TempDir tmp;
    const QByteArray payload = patterned(40000);
    const QList<QByteArray> vols =
        eMule::testing::rar::makeStoredRarSet("movie.mkv", payload, 9000);

    const QString dest = QDir(tmp.path()).filePath(QStringLiteral("_unpacked"));
    UsenetDirectUnpack* worker = nullptr;
    QThread* thread = nullptr;
    start(worker, thread, {QStringLiteral("item"), QStringLiteral("rel"), dest, {}});
    QSignalSpy spy(worker, &UsenetDirectUnpack::finished);

    for (int i = 0; i < vols.size() - 1; ++i)
        worker->offerVolume(i, writeVolume(tmp.path(), i, vols.at(i)));

    // Everything but the last volume is present. Nothing may be reported yet.
    QTest::qWait(300);
    QCOMPARE(spy.count(), 0);
    QVERIFY(!QFile::exists(QDir(dest).filePath(QStringLiteral("movie.mkv")))
            || QFileInfo(QDir(dest).filePath(QStringLiteral("movie.mkv"))).size()
                   < payload.size());

    const int last = int(vols.size()) - 1;
    worker->offerVolume(last, writeVolume(tmp.path(), last, vols.at(last)));
    worker->endOfSet();

    QVERIFY(spy.wait(10000));
    QVERIFY(spy.first().at(0).value<UsenetDirectUnpackResult>().ok);

    QFile out(QDir(dest).filePath(QStringLiteral("movie.mkv")));
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.readAll(), payload);

    stop(worker, thread);
}

void tst_UsenetDirectUnpack::cancelMidSetLeavesNoPartialOutput()
{
    eMule::testing::TempDir tmp;
    const QByteArray payload = patterned(40000);
    const QList<QByteArray> vols =
        eMule::testing::rar::makeStoredRarSet("movie.mkv", payload, 9000);

    const QString dest = QDir(tmp.path()).filePath(QStringLiteral("_unpacked"));
    UsenetDirectUnpack* worker = nullptr;
    QThread* thread = nullptr;
    start(worker, thread, {QStringLiteral("item"), QStringLiteral("rel"), dest, {}});
    QSignalSpy spy(worker, &UsenetDirectUnpack::finished);

    worker->offerVolume(0, writeVolume(tmp.path(), 0, vols.at(0)));
    worker->offerVolume(1, writeVolume(tmp.path(), 1, vols.at(1)));
    QTest::qWait(100);

    QElapsedTimer clock;
    clock.start();
    worker->cancel();
    QVERIFY(spy.wait(5000));
    QVERIFY2(clock.elapsed() < 3000, "cancel must unwind promptly, not on a timeout");

    const auto result = spy.first().at(0).value<UsenetDirectUnpackResult>();
    QVERIFY(!result.ok);
    QVERIFY(!QFile::exists(QDir(dest).filePath(QStringLiteral("movie.mkv"))));

    stop(worker, thread);
}

void tst_UsenetDirectUnpack::aSetThatEndsShortFailsRatherThanHanging()
{
    eMule::testing::TempDir tmp;
    const QByteArray payload = patterned(40000);
    const QList<QByteArray> vols =
        eMule::testing::rar::makeStoredRarSet("movie.mkv", payload, 9000);

    const QString dest = QDir(tmp.path()).filePath(QStringLiteral("_unpacked"));
    UsenetDirectUnpack* worker = nullptr;
    QThread* thread = nullptr;
    start(worker, thread, {QStringLiteral("item"), QStringLiteral("rel"), dest, {}});
    QSignalSpy spy(worker, &UsenetDirectUnpack::finished);

    // Two volumes of five, then the set is declared over — which is what a
    // failed or removed download looks like from here.
    worker->offerVolume(0, writeVolume(tmp.path(), 0, vols.at(0)));
    worker->offerVolume(1, writeVolume(tmp.path(), 1, vols.at(1)));
    worker->endOfSet();

    QVERIFY(spy.wait(10000));
    const auto result = spy.first().at(0).value<UsenetDirectUnpackResult>();
    QVERIFY(!result.ok);
    QVERIFY(!QFile::exists(QDir(dest).filePath(QStringLiteral("movie.mkv"))));

    stop(worker, thread);
}

// The point of the whole feature: by the time the last article lands, the
// release is already extracted, so post-processing has nothing left to unpack.
void tst_UsenetDirectUnpack::aDownloadedSetIsAlreadyUnpackedWhenPostProcessingStarts()
{
    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    const RunOutcome outcome = runRelease(/*directUnpack*/ true);
    QVERIFY(outcome.finishedOk);
    QVERIFY2(outcome.payloadReadyBeforePostProcessing,
             "the payload must be complete before post-processing starts");
    QCOMPARE(outcome.published, patterned(kVolumePayload * kVolumeCount));
}

// And with it off, nothing changes about the result — only about when the work
// happened. This is the fallback every refusal in the feature lands on.
void tst_UsenetDirectUnpack::withTheOptionOffTheSetIsUnpackedAtTheEndAsBefore()
{
    eMule::testing::TempDir tmp;
    useTempPrefs(tmp);

    const RunOutcome outcome = runRelease(/*directUnpack*/ false);
    QVERIFY(outcome.finishedOk);
    QVERIFY(!outcome.payloadReadyBeforePostProcessing);
    QCOMPARE(outcome.published, patterned(kVolumePayload * kVolumeCount));
}

// --- helpers ---------------------------------------------------------------

void tst_UsenetDirectUnpack::start(UsenetDirectUnpack*& worker, QThread*& thread,
                                   const UsenetDirectUnpackJob& job)
{
    qRegisterMetaType<UsenetDirectUnpackJob>();
    qRegisterMetaType<UsenetDirectUnpackResult>();

    worker = new UsenetDirectUnpack;
    thread = new QThread;
    worker->moveToThread(thread);
    thread->start();
    QMetaObject::invokeMethod(worker, "run", Qt::QueuedConnection,
                              Q_ARG(eMule::usenet::UsenetDirectUnpackJob, job));
}

void tst_UsenetDirectUnpack::stop(UsenetDirectUnpack* worker, QThread* thread)
{
    worker->cancel();
    thread->quit();
    QVERIFY(thread->wait(5000));
    delete worker;
    delete thread;
}

QTEST_MAIN(tst_UsenetDirectUnpack)
#include "tst_UsenetDirectUnpack.moc"
