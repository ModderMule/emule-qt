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
};

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

    // what the progress cell hands the painter
    DownloadListModel model;
    model.setDownloads(rows);
    const auto bar = model.index(0, DownloadListModel::ColProgress).data(DownloadListModel::BarDataRole).value<DownloadBarData>();
    QCOMPARE(bar.fileSize, size);
    QCOMPARE(bar.gaps, r.gaps);
    QCOMPARE(bar.pending, r.pending);
    QCOMPARE(bar.freqUnit, kPiece);
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

QTEST_MAIN(tst_Ed3kDownloadRows)
#include "tst_Ed3kDownloadRows.moc"
