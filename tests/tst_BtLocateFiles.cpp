/// @file tst_BtLocateFiles.cpp
/// @brief #224 Locate Files for torrents: found files proved by the torrent's hashes, used in
/// place; the originals survive a remove with files.

#include "BtSession.h"
#include "session/BtLocateFiles.h"

#include <libtorrent/create_torrent.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/torrent_info.hpp>

#include <QBitArray>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule::bt;
namespace lt = libtorrent;

namespace {

constexpr int kPiece = 32 * 1024;
constexpr int kTimeoutMs = 30000;

QByteArray noise(int size, quint32 seed)
{
    QByteArray out(size, Qt::Uninitialized);
    QRandomGenerator gen(seed);
    gen.fillRange(reinterpret_cast<quint32*>(out.data()), size / int(sizeof(quint32)));
    return out;
}

void put(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QCOMPARE(f.write(data), data.size());
}

QByteArray get(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QByteArray makeTorrent(const QString& content, const QString& root, lt::create_flags_t flags)
{
    lt::create_torrent t(lt::list_files(content.toStdString()), kPiece, flags);
    lt::set_piece_hashes(t, root.toStdString());
    const std::vector<char> buf = t.generate_buf();
    return {buf.data(), qsizetype(buf.size())};
}

std::shared_ptr<const lt::torrent_info> infoOf(const QByteArray& bytes)
{
    return lt::load_torrent_buffer(lt::span<char const>(bytes.constData(), std::ptrdiff_t(bytes.size()))).ti;
}

BtSettings sessionSettings(const QString& stateDir, const QString& downloads)
{
    BtSettings s;
    s.stateDir = stateDir;
    s.downloadDir = downloads;
    s.listenPort = 0;
    s.bindAddresses = {QStringLiteral("127.0.0.1")};
    s.dht = false;
    s.lsd = false;
    s.seedRatioPercent = 0;
    s.userAgent = QStringLiteral("eMuleQt-test/0.0.0");
    return s;
}

bool checked(const BtSession& session, const QString& id)
{
    const auto row = session.torrent(id);
    return row && row->state != BtState::CheckingFiles && row->state != BtState::CheckingResume;
}

std::optional<BtLocateReport> locate(BtSession& session, const QString& id, const QString& dir)
{
    std::optional<BtLocateReport> out;
    bool ended = false;
    session.locateFiles(id, {dir}, [&](std::optional<BtLocateReport> r) {
        out = r;
        ended = true;
    });
    if (!QTest::qWaitFor([&] { return ended; }, kTimeoutMs))
        return std::nullopt;
    return out;
}

/// The pieces of file @p f no other real file touches.
std::vector<int> wholePieces(const lt::torrent_info& ti, int f)
{
    const lt::file_storage& fs = ti.layout();
    std::vector<int> out;
    for (const auto p : ti.piece_range()) {
        const auto slices = fs.map_block(p, 0, fs.piece_size(p));
        const bool own = std::ranges::all_of(slices, [&](const lt::file_slice& sl) { return int(sl.file_index) == f || fs.pad_file_at(sl.file_index); });
        if (own && std::ranges::any_of(slices, [&](const lt::file_slice& sl) { return int(sl.file_index) == f; }))
            out.push_back(int(p));
    }
    return out;
}

int fileOfSize(const lt::torrent_info& ti, qint64 size)
{
    const lt::file_storage& fs = ti.layout();
    for (const auto f : fs.file_range())
        if (!fs.pad_file_at(f) && fs.file_size(f) == size)
            return int(f);
    return -1;
}

} // namespace

class tst_BtLocateFiles : public QObject {
    Q_OBJECT

private slots:
    void scanProvesAWholeFileV1();
    void scanProvesAFileV2();
    void scanRefusesSameSizeOtherContent();
    void edgePieceNeedsItsNeighbour();
    void linksInPlaceAndCompletes();
    void copiesProvedPiecesOfAnUnprovableFile();
    void removeWithFilesKeepsTheOriginal();
    void linkSurvivesARestart();
};

void tst_BtLocateFiles::scanProvesAWholeFileV1()
{
    QTemporaryDir dir;
    const QByteArray data = noise(kPiece * 5 + 123, 1);
    put(dir.filePath(QStringLiteral("src/a.bin")), data);
    const auto ti = infoOf(makeTorrent(dir.filePath(QStringLiteral("src/a.bin")), dir.filePath(QStringLiteral("src")), lt::create_torrent::v1_only));
    put(dir.filePath(QStringLiteral("found/renamed.bin")), data);
    const auto scan = locateTorrentFiles(*ti, {dir.filePath(QStringLiteral("found"))}, {0}, {{0, dir.filePath(QStringLiteral("save/a.bin"))}});
    QCOMPARE(scan.found.size(), std::size_t(1));
    QVERIFY(scan.found.at(0).endsWith(QStringLiteral("renamed.bin")));
}

void tst_BtLocateFiles::scanProvesAFileV2()
{
    QTemporaryDir dir;
    const QByteArray data = noise(kPiece * 3 + 77, 2);
    put(dir.filePath(QStringLiteral("src/a.bin")), data);
    const auto ti = infoOf(makeTorrent(dir.filePath(QStringLiteral("src/a.bin")), dir.filePath(QStringLiteral("src")), lt::create_torrent::v2_only));
    put(dir.filePath(QStringLiteral("found/x.bin")), data);
    QByteArray other = data;
    other[kPiece + 5] = char(other[kPiece + 5] ^ 1);
    put(dir.filePath(QStringLiteral("found/bad.bin")), other);
    const auto scan = locateTorrentFiles(*ti, {dir.filePath(QStringLiteral("found"))}, {0}, {{0, dir.filePath(QStringLiteral("save/a.bin"))}});
    QCOMPARE(scan.found.size(), std::size_t(1));
    QVERIFY(scan.found.at(0).endsWith(QStringLiteral("x.bin")));
}

void tst_BtLocateFiles::scanRefusesSameSizeOtherContent()
{
    QTemporaryDir dir;
    const QByteArray data = noise(kPiece * 4, 3);
    put(dir.filePath(QStringLiteral("src/a.bin")), data);
    const auto ti = infoOf(makeTorrent(dir.filePath(QStringLiteral("src/a.bin")), dir.filePath(QStringLiteral("src")), lt::create_torrent::v1_only));
    put(dir.filePath(QStringLiteral("found/other.bin")), noise(kPiece * 4, 4));
    const auto scan = locateTorrentFiles(*ti, {dir.filePath(QStringLiteral("found"))}, {0}, {{0, dir.filePath(QStringLiteral("save/a.bin"))}});
    QVERIFY(scan.found.empty());
    QCOMPARE(scan.partial, 1);
}

void tst_BtLocateFiles::edgePieceNeedsItsNeighbour()
{
    QTemporaryDir dir;
    const QByteArray a = noise(kPiece * 3 + 1000, 5);
    const QByteArray b = noise(kPiece * 3 + 2000, 6);
    put(dir.filePath(QStringLiteral("src/set/a.bin")), a);
    put(dir.filePath(QStringLiteral("src/set/b.bin")), b);
    const auto ti = infoOf(makeTorrent(dir.filePath(QStringLiteral("src/set")), dir.filePath(QStringLiteral("src")), lt::create_torrent::v1_only));
    const lt::file_storage& fs = ti->layout();
    int ia = -1, ib = -1;
    for (const auto f : fs.file_range()) {
        if (fs.pad_file_at(f))
            continue;
        (fs.file_size(f) == a.size() ? ia : ib) = int(f);
    }
    QVERIFY(ia >= 0 && ib >= 0);
    const std::map<int, QString> current{{ia, dir.filePath(QStringLiteral("save/set/a.bin"))}, {ib, dir.filePath(QStringLiteral("save/set/b.bin"))}};
    // alone: the piece it shares cannot be checked, so it is not used; its whole pieces are
    put(dir.filePath(QStringLiteral("one/a.bin")), a);
    auto scan = locateTorrentFiles(*ti, {dir.filePath(QStringLiteral("one"))}, {ia, ib}, current);
    QVERIFY(scan.found.empty());
    QCOMPARE(scan.partial, 0);
    QCOMPARE(scan.copies.size(), std::size_t(1));
    const std::vector<int> own = wholePieces(*ti, ia);
    QCOMPARE(own.size(), std::size_t(3));
    QCOMPARE(scan.copies.at(ia).pieces, own);
    const qint64 start = qint64(own.front()) * kPiece - fs.file_offset(lt::file_index_t(ia));
    const std::vector<char> piece = readLocatedPiece(*ti, own.front(), ia, scan.copies.at(ia).path);
    QCOMPARE(QByteArray(piece.data(), qsizetype(piece.size())), a.mid(start, kPiece));
    // both there: the shared piece passes
    put(dir.filePath(QStringLiteral("one/b.bin")), b);
    scan = locateTorrentFiles(*ti, {dir.filePath(QStringLiteral("one"))}, {ia, ib}, current);
    QCOMPARE(scan.found.size(), std::size_t(2));
}

void tst_BtLocateFiles::linksInPlaceAndCompletes()
{
    QTemporaryDir dir;
    const QByteArray data = noise(kPiece * 6 + 11, 7);
    put(dir.filePath(QStringLiteral("src/a.bin")), data);
    const QByteArray bytes = makeTorrent(dir.filePath(QStringLiteral("src/a.bin")), dir.filePath(QStringLiteral("src")), lt::create_torrent::v1_only);
    const QString original = dir.filePath(QStringLiteral("library/kept.bin"));
    put(original, data);
    // a partial at the torrent's own path must not stop the link or reach the original
    put(dir.filePath(QStringLiteral("save/a.bin")), QByteArray(kPiece, 'x'));

    BtSession session;
    session.start(sessionSettings(dir.filePath(QStringLiteral("state")), dir.filePath(QStringLiteral("save"))));
    const BtAddResult added = session.addTorrentFile(bytes, dir.filePath(QStringLiteral("save")));
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    const QString id = added.id;
    QTRY_VERIFY_WITH_TIMEOUT(session.torrent(id) && !session.files(id).isEmpty(), kTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(checked(session, id), kTimeoutMs);

    const auto report = locate(session, id, dir.filePath(QStringLiteral("library")));
    QVERIFY(report.has_value());
    QCOMPARE(report->linked, 1);
    QCOMPARE(report->failed, 0);
    QTRY_VERIFY_WITH_TIMEOUT(session.torrent(id)->progress >= 1.0, kTimeoutMs);
    QCOMPARE(get(original), data);
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("save/a.bin"))));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("save/a.bin.located-old"))));
    QCOMPARE(session.locatedFiles(id).size(), std::size_t(1));
    session.stop();
}

void tst_BtLocateFiles::copiesProvedPiecesOfAnUnprovableFile()
{
    QTemporaryDir dir;
    const QByteArray a = noise(kPiece * 3 + 1000, 8);
    const QByteArray b = noise(kPiece * 2 + 3000, 9);
    put(dir.filePath(QStringLiteral("src/set/a.bin")), a);
    put(dir.filePath(QStringLiteral("src/set/b.bin")), b);
    const QByteArray bytes = makeTorrent(dir.filePath(QStringLiteral("src/set")), dir.filePath(QStringLiteral("src")), lt::create_torrent::v1_only);
    const QString original = dir.filePath(QStringLiteral("library/a-copy.bin"));
    put(original, a);

    BtSession session;
    session.start(sessionSettings(dir.filePath(QStringLiteral("state")), dir.filePath(QStringLiteral("save"))));
    const BtAddResult added = session.addTorrentFile(bytes, dir.filePath(QStringLiteral("save")));
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    const QString id = added.id;
    QTRY_VERIFY_WITH_TIMEOUT(session.torrent(id) && !session.files(id).isEmpty(), kTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(checked(session, id), kTimeoutMs);

    const auto report = locate(session, id, dir.filePath(QStringLiteral("library")));
    QVERIFY(report.has_value());
    QCOMPARE(report->linked, 0);
    QCOMPARE(report->copied, 1);
    QCOMPARE(report->partial, 0);
    const auto ti = infoOf(bytes);
    const int ia = fileOfSize(*ti, a.size());
    const std::vector<int> own = wholePieces(*ti, ia);
    QCOMPARE(own.size(), std::size_t(3));
    const QBitArray have = session.havePieces(id);
    int count = 0;
    for (int p = 0; p < have.size(); ++p)
        count += have.testBit(p) ? 1 : 0;
    QCOMPARE(count, 3);   // the piece shared with b: still to download
    for (const int p : own)
        QVERIFY(have.testBit(p));
    QVERIFY(session.locatedFiles(id).empty());
    session.stop();
    QCOMPARE(get(original), a);
    const qint64 start = qint64(own.front()) * kPiece - ti->layout().file_offset(lt::file_index_t(ia));
    QCOMPARE(get(dir.filePath(QStringLiteral("save/set/a.bin"))).mid(start, kPiece * 3), a.mid(start, kPiece * 3));
}

void tst_BtLocateFiles::removeWithFilesKeepsTheOriginal()
{
    QTemporaryDir dir;
    const QByteArray a = noise(kPiece * 4, 8);
    const QByteArray b = noise(kPiece * 4, 9);
    put(dir.filePath(QStringLiteral("src/set/a.bin")), a);
    put(dir.filePath(QStringLiteral("src/set/b.bin")), b);
    const QByteArray bytes = makeTorrent(dir.filePath(QStringLiteral("src/set")), dir.filePath(QStringLiteral("src")), lt::create_torrent::v1_only);
    const QString original = dir.filePath(QStringLiteral("library/a-kept.bin"));
    put(original, a);
    put(dir.filePath(QStringLiteral("save/set/b.bin")), b);   // the other file: the torrent's own

    BtSession session;
    session.start(sessionSettings(dir.filePath(QStringLiteral("state")), dir.filePath(QStringLiteral("save"))));
    const BtAddResult added = session.addTorrentFile(bytes, dir.filePath(QStringLiteral("save")));
    QCOMPARE(added.outcome, BtAddOutcome::Added);
    const QString id = added.id;
    QTRY_VERIFY_WITH_TIMEOUT(session.torrent(id) && !session.files(id).isEmpty(), kTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(checked(session, id), kTimeoutMs);

    const auto report = locate(session, id, dir.filePath(QStringLiteral("library")));
    QVERIFY(report.has_value());
    QCOMPARE(report->linked, 1);
    QTRY_VERIFY_WITH_TIMEOUT(session.torrent(id)->progress >= 1.0, kTimeoutMs);

    QVERIFY(session.remove(id, true));
    QTRY_VERIFY_WITH_TIMEOUT(!QFile::exists(dir.filePath(QStringLiteral("save/set/b.bin"))), kTimeoutMs);
    QCOMPARE(get(original), a);
    session.stop();
}

void tst_BtLocateFiles::linkSurvivesARestart()
{
    QTemporaryDir dir;
    const QByteArray data = noise(kPiece * 5 + 9, 10);
    put(dir.filePath(QStringLiteral("src/a.bin")), data);
    const QByteArray bytes = makeTorrent(dir.filePath(QStringLiteral("src/a.bin")), dir.filePath(QStringLiteral("src")), lt::create_torrent::v1_only);
    const QString original = dir.filePath(QStringLiteral("library/kept.bin"));
    put(original, data);
    const BtSettings settings = sessionSettings(dir.filePath(QStringLiteral("state")), dir.filePath(QStringLiteral("save")));
    QString id;
    {
        BtSession session;
        session.start(settings);
        const BtAddResult added = session.addTorrentFile(bytes, dir.filePath(QStringLiteral("save")));
        QCOMPARE(added.outcome, BtAddOutcome::Added);
        id = added.id;
        QTRY_VERIFY_WITH_TIMEOUT(session.torrent(id) && !session.files(id).isEmpty(), kTimeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(checked(session, id), kTimeoutMs);
        const auto report = locate(session, id, dir.filePath(QStringLiteral("library")));
        QVERIFY(report.has_value());
        QCOMPARE(report->linked, 1);
        QTRY_VERIFY_WITH_TIMEOUT(session.torrent(id)->progress >= 1.0, kTimeoutMs);
        session.stop();
    }
    BtSession again;
    again.start(settings);
    QTRY_VERIFY_WITH_TIMEOUT(again.torrent(id).has_value(), kTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(checked(again, id) && again.torrent(id)->progress >= 1.0, kTimeoutMs);
    QCOMPARE(again.locatedFiles(id).size(), std::size_t(1));
    QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("save/a.bin"))));
    QVERIFY(again.remove(id, true));
    QTest::qWait(500);
    QCOMPARE(get(original), data);
    again.stop();
}

QTEST_MAIN(tst_BtLocateFiles)
#include "tst_BtLocateFiles.moc"
