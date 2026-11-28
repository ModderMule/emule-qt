/// @file tst_Ed3kDownloadRows.cpp
/// @brief The ed3k rows of the Transfers download list (controls/Ed3kDownloadRows) from
/// hand-built GetEd3kDownloads CBOR: the block runs (utils/BlockRuns) as the eD2K bar's gaps,
/// pending blocks and sources per 4 MiB piece; the bar painted by eD2K's painter.

#include "controls/DownloadListModel.h"
#include "controls/DownloadProgressDelegate.h"
#include "controls/Ed3kDownloadRows.h"
#include "utils/BlockRuns.h"

#include <QCborArray>
#include <QCborMap>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QTest>

using namespace eMule;

namespace {

constexpr qint64 kBlock = 256 * 1024;
constexpr qint64 kPiece = 16 * kBlock;

std::vector<bool> bits(std::initializer_list<int> v)
{
    std::vector<bool> out;
    for (const int b : v)
        out.push_back(b != 0);
    return out;
}

} // namespace

class tst_Ed3kDownloadRows : public QObject {
    Q_OBJECT

private slots:
    void runsRoundTrip();
    void damagedRunsAreRefused();
    void aRowCarriesTheBlocks();
    void anOlderCoreDrawsOneRun();
    void theBarIsPaintedByPiece();
    void aNetworkJobRow();
    void aCompletingRowShowsItsStep();
    void networksColumn();
};

void tst_Ed3kDownloadRows::aCompletingRowShowsItsStep()
{
    QCborMap m;
    m.insert(QStringLiteral("root"), QStringLiteral("ab"));
    m.insert(QStringLiteral("size"), qint64(1000));
    m.insert(QStringLiteral("done"), qint64(1000));
    m.insert(QStringLiteral("state"), 2);
    m.insert(QStringLiteral("completionPermille"), 425);
    QCborMap list;
    list.insert(QStringLiteral("downloads"), QCborArray{m});
    QCOMPARE(ed3kDownloadRows(list).at(0).fileOp, 1);   // hashing
    QCOMPARE(ed3kDownloadRows(list).at(0).completionPermille, 425);

    m.insert(QStringLiteral("copying"), true);
    list.insert(QStringLiteral("downloads"), QCborArray{m});
    QCOMPARE(ed3kDownloadRows(list).at(0).fileOp, 2);

    m.insert(QStringLiteral("state"), 3);   // complete: no step left
    list.insert(QStringLiteral("downloads"), QCborArray{m});
    QCOMPARE(ed3kDownloadRows(list).at(0).completionPermille, -1);
}

void tst_Ed3kDownloadRows::runsRoundTrip()
{
    const std::vector<std::vector<bool>> cases = {
        {},
        bits({1}),
        bits({0}),
        bits({1, 1, 0, 0, 0, 1}),
        bits({0, 1, 0, 1, 0, 1, 1}),
        std::vector<bool>(1000, true),
        std::vector<bool>(70000, false),   // a run over two varint bytes
    };
    for (const auto& c : cases) {
        const QByteArray runs = encodeBlockRuns(c);
        const auto back = decodeBlockRuns(runs, c.size());
        QVERIFY(back.has_value());
        QVERIFY(*back == c);
    }
    // in order, half done: a few bytes only
    std::vector<bool> half(4000, false);
    std::fill(half.begin(), half.begin() + 2000, true);
    QCOMPARE(encodeBlockRuns(half).size(), 4);
    // first block clear: a set run of 0 first
    QCOMPARE(encodeBlockRuns(bits({0, 1})), QByteArray("\x00\x01\x01", 3));
}

void tst_Ed3kDownloadRows::damagedRunsAreRefused()
{
    QVERIFY(!decodeBlockRuns(QByteArray("\x02", 1), 3));          // short of the count
    QVERIFY(!decodeBlockRuns(QByteArray("\x02\x02", 2), 3));      // over it
    QVERIFY(!decodeBlockRuns(QByteArray("\x81", 1), 3));          // cut off varint
    QVERIFY(!decodeBlockRuns(QByteArray(), kMaxBlockRunBits + 1));
    QVERIFY(decodeBlockRuns(QByteArray(), 0).has_value());
    QCOMPARE(blockRanges(bits({1, 0, 0, 1, 0}), false, 10, 45), (QList<qint64>{10, 29, 40, 44}));
    QCOMPARE(blockRanges(bits({0, 0}), false, 10, 15), (QList<qint64>{0, 14}));
}

void tst_Ed3kDownloadRows::aRowCarriesTheBlocks()
{
    // 2 pieces + 1 block: blocks 0-1 held, 2-3 asked for, the rest missing; sources 3 and 0
    const qint64 size = 2 * kPiece + kBlock - 100;
    std::vector<bool> held(33, false);
    held[0] = held[1] = true;
    std::vector<bool> pending(33, false);
    pending[2] = pending[3] = true;
    QCborMap m{{QStringLiteral("root"), QStringLiteral("ab").repeated(32)},
               {QStringLiteral("name"), QStringLiteral("film.mkv")},
               {QStringLiteral("size"), size},
               {QStringLiteral("done"), 2 * kBlock},
               {QStringLiteral("state"), 0},
               {QStringLiteral("priority"), 2},
               {QStringLiteral("sources"), 3},
               {QStringLiteral("transferring"), 1},
               {QStringLiteral("blockSize"), kBlock},
               {QStringLiteral("blocks"), encodeBlockRuns(held)},
               {QStringLiteral("pending"), encodeBlockRuns(pending)},
               {QStringLiteral("pieceSize"), kPiece},
               {QStringLiteral("avail"), QByteArray("\x03\x00\x01", 3)}};
    const auto rows = ed3kDownloadRows(QCborMap{{QStringLiteral("downloads"), QCborArray{m}}});
    QCOMPARE(rows.size(), std::size_t(1));
    const DownloadRow& r = rows.front();
    QVERIFY(r.isEd3k());
    QCOMPARE(r.priority, QStringLiteral("high"));
    QCOMPARE(r.status, QStringLiteral("ready"));
    QVERIFY(r.hasBarRanges);
    QCOMPARE(r.gaps, (QList<qint64>{2 * kBlock, size - 1}));
    QCOMPARE(r.pending, (QList<qint64>{2 * kBlock, 4 * kBlock - 1}));
    QCOMPARE(r.partFreq, (QList<quint16>{3, 0, 1}));
    QCOMPARE(r.partFreqUnit, kPiece);
    // part detail: piece 0 started, the others empty
    QCOMPARE(r.partMap, QByteArray("\x01\x00\x00", 3));

    // what the progress cell hands the painter
    DownloadListModel model;
    model.setDownloads(rows);
    const auto bar = model.index(0, DownloadListModel::ColProgress).data(DownloadListModel::BarDataRole).value<DownloadBarData>();
    QCOMPARE(bar.fileSize, size);
    QCOMPARE(bar.gaps, r.gaps);
    QCOMPARE(bar.pending, r.pending);
    QCOMPARE(bar.freqUnit, kPiece);
    QCOMPARE(bar.partMap, r.partMap);
}

void tst_Ed3kDownloadRows::anOlderCoreDrawsOneRun()
{
    QCborMap m{{QStringLiteral("size"), 1000}, {QStringLiteral("done"), 400}, {QStringLiteral("state"), 1}};
    const DownloadRow r = ed3kDownloadRow(m);
    QVERIFY(r.isPaused);
    QCOMPARE(r.gaps, (QList<qint64>{400, 999}));
    QVERIFY(r.pending.isEmpty());
    // complete: no gaps at all
    QCborMap done{{QStringLiteral("size"), 1000}, {QStringLiteral("done"), 1000}, {QStringLiteral("state"), 3}};
    QVERIFY(ed3kDownloadRow(done).gaps.isEmpty());
}

void tst_Ed3kDownloadRows::theBarIsPaintedByPiece()
{
    // piece 0 missing with 1 source (blue), piece 1 missing with none (red), piece 2 held
    const qint64 size = 3 * kPiece;
    DownloadBarData bar;
    bar.fileSize = size;
    bar.gaps = {0, 2 * kPiece - 1};
    bar.partFreq = {1, 0, 0};
    bar.freqUnit = kPiece;
    QImage img(300, 10, QImage::Format_RGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        paintDownloadBar(p, img.rect(), bar, 33.0, false, false);
    }
    const QColor first = img.pixelColor(50, 6);
    const QColor second = img.pixelColor(150, 6);
    const QColor third = img.pixelColor(250, 6);
    QVERIFY2(first.blue() > 200 && first.red() < 60, qPrintable(first.name()));   // a source: blue
    QVERIFY2(second.red() > 200 && second.blue() < 60, qPrintable(second.name())); // none: red
    QVERIFY2(third.red() < 140 && third.blue() < 140, qPrintable(third.name()));   // held: dark
}

// single transfer mode: a torrent joined with ed3k as a Transfers row (SingleTransfer::transferRow)
void tst_Ed3kDownloadRows::aNetworkJobRow()
{
    const QCborMap m{{QStringLiteral("root"), QStringLiteral("torrent|bt:v1:AB")},
                     {QStringLiteral("network"), QStringLiteral("torrent")},
                     {QStringLiteral("name"), QStringLiteral("Big Buck Bunny")},
                     {QStringLiteral("size"), 1000}, {QStringLiteral("done"), 250},
                     {QStringLiteral("rate"), 50}, {QStringLiteral("sources"), 4}, {QStringLiteral("transferring"), 4},
                     {QStringLiteral("state"), 0}};
    const DownloadRow r = ed3kDownloadRow(m);
    QVERIFY(r.isEd3k());
    QVERIFY(r.isNetworkJob());
    QCOMPARE(r.hash, QStringLiteral("torrent|bt:v1:AB"));
    QCOMPARE(r.ed3kNetwork, QStringLiteral("torrent"));
    QCOMPARE(r.status, QStringLiteral("ready"));
    QCOMPARE(r.timeRemaining, qint64(15));

    DownloadListModel model;
    model.setDownloads({r});
    // #261: the name stays plain, the Networks column says BitTorrent + ed3k
    QCOMPARE(model.data(model.index(0, DownloadListModel::ColFileName), Qt::DisplayRole).toString(),
             QStringLiteral("Big Buck Bunny"));
    QCOMPARE(model.data(model.index(0, DownloadListModel::ColNetworks), Qt::ToolTipRole).toString(),
             QStringLiteral("BitTorrent, ed3k"));
    const QString tip = model.data(model.index(0, DownloadListModel::ColFileName), Qt::ToolTipRole).toString();
    QVERIFY2(tip.contains(QStringLiteral("single transfer mode")), qPrintable(tip));
    // a plain ed3k root is no network job
    QVERIFY(!ed3kDownloadRow(QCborMap{{QStringLiteral("root"), QString(64, u'a')}}).isNetworkJob());
}

// #261: own network first, then the joined ones; icons side by side
void tst_Ed3kDownloadRows::networksColumn()
{
    const auto nets = [](const DownloadRow& r) -> QString {
        DownloadListModel model;
        model.setDownloads({r});
        const QModelIndex i = model.index(0, DownloadListModel::ColNetworks);
        const auto icon = model.data(i, Qt::DecorationRole).value<QIcon>();
        const QString text = model.data(i, Qt::ToolTipRole).toString();
        // one 16 px icon per network, 2 px apart (the delegate reads the aspect ratio)
        const int count = int(text.count(QStringLiteral(", "))) + 1;
        const QList<QSize> sizes = icon.availableSizes();
        if (sizes.isEmpty() || sizes.constFirst().width() * 16 != sizes.constFirst().height() * (16 + (count - 1) * 18))
            return QStringLiteral("bad icon");
        return text;
    };
    DownloadRow ed2k;
    ed2k.hash = QStringLiteral("00112233445566778899aabbccddeeff");
    QCOMPARE(nets(ed2k), QStringLiteral("eD2K"));
    ed2k.jointEd3k = true;
    QCOMPARE(nets(ed2k), QStringLiteral("eD2K, ed3k"));

    // an ed3k root known as a torrent and an eD2K file, under the anonymous key
    const QCborMap m{{QStringLiteral("root"), QString(64, u'b')},
                     {QStringLiteral("key"), QStringLiteral("Anonymous")},
                     {QStringLiteral("aliases"), QCborArray{
                          QCborMap{{QStringLiteral("network"), QStringLiteral("torrent")}, {QStringLiteral("name"), QStringLiteral("t")}},
                          QCborMap{{QStringLiteral("network"), QStringLiteral("ed2k")}, {QStringLiteral("id"), QStringLiteral("x")}},
                          QCborMap{{QStringLiteral("network"), QStringLiteral("torrent")}, {QStringLiteral("name"), QStringLiteral("t2")}}}}};
    const DownloadRow root = ed3kDownloadRow(m);
    QCOMPARE(root.ed3kAliasNets, (QStringList{QStringLiteral("torrent"), QStringLiteral("ed2k")}));
    QCOMPARE(nets(root), QStringLiteral("ed3k (anonymous key), eD2K, BitTorrent"));
    QCOMPARE(nets(ed3kDownloadRow(QCborMap{{QStringLiteral("root"), QString(64, u'c')}})), QStringLiteral("ed3k"));

    // single transfer mode, not joined: its own network only
    DownloadRow job = ed3kDownloadRow(QCborMap{{QStringLiteral("root"), QStringLiteral("usenet|7")},
                                               {QStringLiteral("network"), QStringLiteral("usenet")},
                                               {QStringLiteral("joined"), false}});
    QCOMPARE(nets(job), QStringLiteral("Usenet"));
}

QTEST_MAIN(tst_Ed3kDownloadRows)
#include "tst_Ed3kDownloadRows.moc"
