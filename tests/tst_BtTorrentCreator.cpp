/// @file tst_BtTorrentCreator.cpp
/// @brief Torrent Creator (#191): v1/v2/hybrid, a folder subset, dot files, fields, cancel,
/// seeding what was made.

#include "BtSession.h"
#include "create/BtTorrentCreator.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule::bt;

namespace {

void writeFile(const QString& path, qint64 size, char fill)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QVERIFY(f.write(QByteArray(size, fill)) == size);
}

/// bytes of the real files, padding left out (v2 pads the tail)
qint64 contentSize(const BtTorrentPreview& t)
{
    qint64 n = 0;
    for (const auto& f : t.files)
        if (!f.pad)
            n += f.size;
    return n;
}

BtCreateResult create(const BtCreateParams& p)
{
    BtTorrentCreator c(p);
    QSignalSpy done(&c, &BtTorrentCreator::finished);
    c.run();   // on this thread: the signal is direct
    if (done.count() != 1)
        return {};
    return done.at(0).at(0).value<BtCreateResult>();
}

} // namespace

class tst_BtTorrentCreator : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void singleFileFormats_data();
    void singleFileFormats();
    void folderNaturalOrderSkipsDotfiles();
    void onlySelectedFilesOfAFolder();
    void fieldsAndTiers();
    void refusesEscapingNames();
    void cancelStops();
    void seedsWhatWasMade();
    void savedTorrentsWithoutASession();
    void exportsWhatIsSeeded_data();
    void exportsWhatIsSeeded();
    void seedSurvivesARestart_data();
    void seedSurvivesARestart();

private:
    QTemporaryDir m_dir;
};

void tst_BtTorrentCreator::initTestCase()
{
    qRegisterMetaType<BtCreateResult>();
    QVERIFY(m_dir.isValid());
    QDir(m_dir.path()).mkpath(QStringLiteral("set/sub"));
    writeFile(m_dir.filePath(QStringLiteral("one.bin")), 300 * 1024 + 7, 'a');
    writeFile(m_dir.filePath(QStringLiteral("set/file10.txt")), 1000, 'b');
    writeFile(m_dir.filePath(QStringLiteral("set/file2.txt")), 2000, 'c');
    writeFile(m_dir.filePath(QStringLiteral("set/.hidden")), 10, 'd');
    writeFile(m_dir.filePath(QStringLiteral("set/sub/deep.dat")), 70000, 'e');
}

void tst_BtTorrentCreator::singleFileFormats_data()
{
    QTest::addColumn<int>("format");
    QTest::addColumn<bool>("v1");
    QTest::addColumn<bool>("v2");
    QTest::newRow("v1") << int(BtTorrentFormat::V1) << true << false;
    QTest::newRow("v2") << int(BtTorrentFormat::V2) << false << true;
    QTest::newRow("hybrid") << int(BtTorrentFormat::Hybrid) << true << true;
}

void tst_BtTorrentCreator::singleFileFormats()
{
    QFETCH(int, format);
    QFETCH(bool, v1);
    QFETCH(bool, v2);
    BtCreateParams p;
    p.path = m_dir.filePath(QStringLiteral("one.bin"));
    p.format = BtTorrentFormat(format);
    p.pieceSize = 64 * 1024;
    const BtCreateResult r = create(p);
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.name, QStringLiteral("one.bin"));
    QCOMPARE(r.savePath, m_dir.path());
    QCOMPARE(r.pieces, 5);
    QCOMPARE(BtTorrentCreator::countPieces(p).value_or(-1), 5);
    const auto preview = BtSession::inspectTorrentFile(r.data);
    QVERIFY2(preview, qPrintable(preview ? QString() : preview.error()));
    QCOMPARE(!preview->hashV1.isEmpty(), v1);
    QCOMPARE(!preview->hashV2.isEmpty(), v2);
    QCOMPARE(contentSize(*preview), 300 * 1024 + 7);
    QCOMPARE(preview->pieceLength, 64 * 1024);
}

void tst_BtTorrentCreator::folderNaturalOrderSkipsDotfiles()
{
    BtCreateParams p;
    p.path = m_dir.filePath(QStringLiteral("set"));
    p.format = BtTorrentFormat::V1;
    const BtCreateResult r = create(p);
    QVERIFY2(r.ok, qPrintable(r.error));
    const auto preview = BtSession::inspectTorrentFile(r.data);
    QVERIFY(preview);
    QStringList paths;
    for (const auto& f : preview->files)
        if (!f.pad)
            paths << f.path;
    // file2 before file10; the folder's own files before sub/; .hidden left out
    QCOMPARE(paths, (QStringList{QStringLiteral("set/file2.txt"), QStringLiteral("set/file10.txt"),
                                 QStringLiteral("set/sub/deep.dat")}));

    p.ignoreDotfiles = false;
    const BtCreateResult all = create(p);
    QVERIFY(all.ok);
    QCOMPARE(contentSize(*BtSession::inspectTorrentFile(all.data)), 1000 + 2000 + 10 + 70000);
}

void tst_BtTorrentCreator::onlySelectedFilesOfAFolder()
{
    BtCreateParams p;
    p.path = m_dir.filePath(QStringLiteral("set"));
    p.onlyFiles = {QStringLiteral("file10.txt"), QStringLiteral("sub/deep.dat")};
    p.format = BtTorrentFormat::Hybrid;
    const BtCreateResult r = create(p);
    QVERIFY2(r.ok, qPrintable(r.error));
    const auto preview = BtSession::inspectTorrentFile(r.data);
    QVERIFY(preview);
    QCOMPARE(preview->name, QStringLiteral("set"));
    QCOMPARE(contentSize(*preview), 1000 + 70000);
}

void tst_BtTorrentCreator::fieldsAndTiers()
{
    BtCreateParams p;
    p.path = m_dir.filePath(QStringLiteral("one.bin"));
    p.isPrivate = true;
    p.trackers = {QStringLiteral("http://tracker.example/a"), QStringLiteral("http://tracker.example/b"), QString(),
                  QStringLiteral("udp://tracker.example:6969")};
    p.webSeeds = {QStringLiteral("http://seed.example/one.bin")};
    p.comment = QStringLiteral("made for a test");
    p.source = QStringLiteral("SRC");
    p.creator = QStringLiteral("eMuleQt/test");
    const BtCreateResult r = create(p);
    QVERIFY2(r.ok, qPrintable(r.error));
    QVERIFY(r.data.contains("6:source3:SRC"));
    const auto preview = BtSession::inspectTorrentFile(r.data);
    QVERIFY(preview);
    QVERIFY(preview->isPrivate);
    QCOMPARE(preview->comment, p.comment);
    QCOMPARE(preview->creator, p.creator);
    QCOMPARE(preview->webSeeds, QList<QString>{p.webSeeds.constFirst()});
    QCOMPARE(preview->trackers.size(), 3);
    QCOMPARE(preview->trackers.at(0).tier, 0);
    QCOMPARE(preview->trackers.at(1).tier, 0);
    QCOMPARE(preview->trackers.at(2).tier, 1);
}

void tst_BtTorrentCreator::refusesEscapingNames()
{
    BtCreateParams p;
    p.path = m_dir.filePath(QStringLiteral("set"));
    p.onlyFiles = {QStringLiteral("../one.bin")};
    const BtCreateResult r = create(p);
    QVERIFY(!r.ok);
    QVERIFY(!r.cancelled);

    p.onlyFiles.clear();
    p.path = m_dir.filePath(QStringLiteral("missing"));
    QVERIFY(!create(p).ok);
}

void tst_BtTorrentCreator::cancelStops()
{
    BtCreateParams p;
    p.path = m_dir.filePath(QStringLiteral("one.bin"));
    p.pieceSize = 16 * 1024;
    BtTorrentCreator c(p);
    QSignalSpy done(&c, &BtTorrentCreator::finished);
    connect(&c, &BtTorrentCreator::progress, &c, [&c](int pct) {
        if (pct > 0)
            c.cancel();
    });
    c.run();
    QCOMPARE(done.count(), 1);
    const auto r = done.at(0).at(0).value<BtCreateResult>();
    QVERIFY(!r.ok);
    QVERIFY(r.cancelled);
}

void tst_BtTorrentCreator::seedsWhatWasMade()
{
    BtCreateParams p;
    p.path = m_dir.filePath(QStringLiteral("set"));
    const BtCreateResult r = create(p);
    QVERIFY(r.ok);

    QTemporaryDir state;
    BtSettings s;
    s.listenPort = 0;
    s.dht = false;
    s.lsd = false;
    s.stateDir = state.filePath(QStringLiteral("BT"));
    s.downloadDir = state.filePath(QStringLiteral("Incoming"));
    BtSession session;
    session.start(s);
    const BtAddResult added = session.addCreatedTorrent(r.data, r.savePath);
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    // seed mode: complete without a download or a recheck
    QTRY_VERIFY_WITH_TIMEOUT(session.torrent(added.id) && session.torrent(added.id)->haveBytes > 0
                                 && session.torrent(added.id)->haveBytes == session.torrent(added.id)->totalSize,
                             10000);
    QCOMPARE(session.addCreatedTorrent(r.data, r.savePath).outcome, BtAddOutcome::Duplicate);
    session.stop();
}

// BT off: the share guard reads the torrents of the last run from their resume data
void tst_BtTorrentCreator::savedTorrentsWithoutASession()
{
    BtCreateParams p;
    p.path = m_dir.filePath(QStringLiteral("set"));
    p.isPrivate = true;
    const BtCreateResult r = create(p);
    QVERIFY(r.ok);

    QTemporaryDir state;
    BtSettings s;
    s.listenPort = 0;
    s.dht = false;
    s.lsd = false;
    s.stateDir = state.filePath(QStringLiteral("BT"));
    s.downloadDir = state.filePath(QStringLiteral("Incoming"));
    QString id;
    {
        BtSession session;
        session.start(s);
        const BtAddResult added = session.addCreatedTorrent(r.data, r.savePath);
        QCOMPARE(added.outcome, BtAddOutcome::Added);
        id = added.id;
        QTRY_VERIFY_WITH_TIMEOUT(session.torrent(id) && session.torrent(id)->totalSize > 0
                                     && session.torrent(id)->haveBytes == session.torrent(id)->totalSize,
                                 10000);
        session.stop();
    }
    const QList<BtSavedTorrent> saved = BtSession::savedTorrents(s.stateDir, s.downloadDir);
    QCOMPARE(saved.size(), 1);
    const BtTorrentRow& row = saved.first().row;
    QCOMPARE(row.id, id);
    QCOMPARE(row.name, QStringLiteral("set"));
    QCOMPARE(QDir::cleanPath(row.savePath), QDir::cleanPath(r.savePath));
    QVERIFY(row.isPrivate);
    QCOMPARE(row.progress, 1.0);
    QVERIFY(row.totalSize > 0 && row.haveBytes == row.totalSize);
    for (const BtFileRow& f : saved.first().files)
        QCOMPARE(f.done, f.size);
    QVERIFY(BtSession::savedTorrents(state.filePath(QStringLiteral("none")), s.downloadDir).isEmpty());
}

void tst_BtTorrentCreator::exportsWhatIsSeeded_data()
{
    QTest::addColumn<int>("format");
    QTest::newRow("v1") << int(BtTorrentFormat::V1);
    QTest::newRow("v2") << int(BtTorrentFormat::V2);
    QTest::newRow("hybrid") << int(BtTorrentFormat::Hybrid);
}

// #196: Save Torrent File gives back the same info hash, the v2 piece layers and trackers
void tst_BtTorrentCreator::exportsWhatIsSeeded()
{
    QFETCH(int, format);
    BtCreateParams p;
    p.path = m_dir.filePath(QStringLiteral("set"));
    p.format = BtTorrentFormat(format);
    p.pieceSize = 16 * 1024;   // deep.dat spans pieces: a v2 piece layer
    p.trackers = {QStringLiteral("http://tracker.example/a"), QString(), QStringLiteral("http://tracker.example/b")};
    p.webSeeds = {QStringLiteral("http://seed.example/")};
    p.comment = QStringLiteral("exported");
    const BtCreateResult r = create(p);
    QVERIFY2(r.ok, qPrintable(r.error));

    QTemporaryDir state;
    BtSettings s;
    s.listenPort = 0;
    s.dht = false;
    s.lsd = false;
    s.stateDir = state.filePath(QStringLiteral("BT"));
    s.downloadDir = state.filePath(QStringLiteral("Incoming"));
    BtSession session;
    session.start(s);
    QVERIFY(!session.exportTorrent(QStringLiteral("bt:v1:unknown")));
    const BtAddResult added = session.addCreatedTorrent(r.data, r.savePath);
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    QTRY_VERIFY_WITH_TIMEOUT(session.exportTorrent(added.id).has_value(), 10000);
    const QByteArray out = *session.exportTorrent(added.id);

    const auto made = BtSession::inspectTorrentFile(r.data);
    const auto back = BtSession::inspectTorrentFile(out);
    QVERIFY2(back, qPrintable(back ? QString() : back.error()));
    QCOMPARE(back->hashV1, made->hashV1);
    QCOMPARE(back->hashV2, made->hashV2);
    QCOMPARE(back->comment, p.comment);
    QCOMPARE(back->webSeeds, made->webSeeds);
    QCOMPARE(back->trackers.size(), 2);
    QCOMPARE(back->trackers.at(1).tier, 1);
    if (format != int(BtTorrentFormat::V1))
        QVERIFY(out.contains("12:piece layers"));
    // a re-add of the export is the same torrent
    QCOMPARE(session.addTorrentFile(out, r.savePath).outcome, BtAddOutcome::Duplicate);
    session.stop();
}

void tst_BtTorrentCreator::seedSurvivesARestart_data()
{
    exportsWhatIsSeeded_data();
}

// #212: a created torrent that is seeded is still listed, complete, after a restart
void tst_BtTorrentCreator::seedSurvivesARestart()
{
    QFETCH(int, format);
    BtCreateParams p;
    p.path = m_dir.filePath(QStringLiteral("one.bin"));
    p.format = BtTorrentFormat(format);
    const BtCreateResult r = create(p);
    QVERIFY2(r.ok, qPrintable(r.error));

    QTemporaryDir state;
    BtSettings s;
    s.listenPort = 0;
    s.dht = false;
    s.lsd = false;
    s.stateDir = state.filePath(QStringLiteral("BT"));
    s.downloadDir = state.filePath(QStringLiteral("Incoming"));
    QString id;
    {
        BtSession session;
        session.start(s);
        const BtAddResult added = session.addCreatedTorrent(r.data, r.savePath);
        QCOMPARE(added.outcome, BtAddOutcome::Added);
        id = added.id;
        QTRY_VERIFY_WITH_TIMEOUT(session.torrent(id) && session.torrent(id)->totalSize > 0
                                     && session.torrent(id)->haveBytes == session.torrent(id)->totalSize,
                                 10000);
        session.stop();
    }
    BtSession again;
    again.start(s);
    QTRY_VERIFY_WITH_TIMEOUT(again.torrent(id).has_value(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(again.torrent(id)->totalSize > 0 && again.torrent(id)->haveBytes == again.torrent(id)->totalSize,
                             10000);
    again.stop();
}

QTEST_GUILESS_MAIN(tst_BtTorrentCreator)
#include "tst_BtTorrentCreator.moc"
