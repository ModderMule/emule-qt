/// @file tst_KadIncomingGraph.cpp
/// @brief Tests for KadIncomingGraph.h — the graph of one incoming Kad request.
/// EMULE_GRAPH_PNG=<file> saves the picture.

#include "controls/KadIncomingGraph.h"

#include <QCborArray>
#include <QCborMap>
#include <QImage>
#include <QSet>
#include <QTest>

using namespace eMule;

namespace {

const QString kTarget = QStringLiteral("F0000000000000000000000000000000");

/// An id sharing @p bits leading bits with kTarget.
QString idSharing(int bits, int salt = 0)
{
    // F0.. ^ (top bit after the prefix set) keeps the prefix, flips the next bit
    QString hex = kTarget;
    const int nibble = bits / 4;
    const int bit = 3 - bits % 4;
    const int v = hex.mid(nibble, 1).toInt(nullptr, 16) ^ (1 << bit);
    hex[nibble] = QString::number(v, 16).toUpper().at(0);
    if (salt != 0 && nibble + 2 < 32)
        hex[31] = QString::number(salt % 16, 16).toUpper().at(0);
    return hex;
}

QCborMap node(const QString& id, const QString& addr)
{
    return QCborMap{{QStringLiteral("id"), id}, {QStringLiteral("addr"), addr}, {QStringLiteral("version"), 9}};
}

} // namespace

class tst_KadIncomingGraph : public QObject {
    Q_OBJECT

private slots:
    void handedCloserContactsSitBelowOurLine();
    void unknownAskerAndNoTarget();
};

void tst_KadIncomingGraph::handedCloserContactsSitBelowOurLine()
{
    const QCborMap row{{QStringLiteral("id"), 7},
                       {QStringLiteral("type"), 1},
                       {QStringLiteral("target"), kTarget},
                       {QStringLiteral("addr"), QStringLiteral("198.51.100.4:4672")},
                       {QStringLiteral("contacts"), 4},
                       {QStringLiteral("results"), -1},
                       {QStringLiteral("records"), -1}};
    QCborArray handed;
    handed.append(node(idSharing(14, 1), QStringLiteral("192.0.2.1:4672")));
    handed.append(node(idSharing(17, 2), QStringLiteral("192.0.2.2:4672")));
    handed.append(node(idSharing(19, 3), QStringLiteral("192.0.2.3:4672")));
    handed.append(node(idSharing(9, 4), QStringLiteral("192.0.2.4:4672")));   // not closer than us
    const QCborMap graph{{QStringLiteral("found"), true},
                         {QStringLiteral("wanted"), 11},
                         {QStringLiteral("asker"), node(idSharing(6), QStringLiteral("198.51.100.4:4672"))},
                         {QStringLiteral("own"), QCborMap{{QStringLiteral("id"), idSharing(12)}}},
                         {QStringLiteral("handed"), handed}};

    KadIncomingGraph g;
    g.resize(720, 360);
    g.setRequest(row, graph, QStringLiteral("Node Lookup: F000…"), QStringLiteral("4 contact(s)"));
    QCOMPARE(g.requestId(), quint64{7});

    const QImage img = g.grab().toImage();
    QVERIFY(!img.isNull());
    if (const QByteArray out = qgetenv("EMULE_GRAPH_PNG"); !out.isEmpty())
        img.save(QString::fromLocal8Bit(out));

    // a picture, not the hint: more than the background and a few lines of text
    QSet<QRgb> colors;
    for (int y = 0; y < img.height(); y += 2)
        for (int x = 0; x < img.width(); x += 2)
            colors.insert(img.pixel(x, y));
    QVERIFY2(colors.size() > 20, qPrintable(QString::number(colors.size())));
}

void tst_KadIncomingGraph::unknownAskerAndNoTarget()
{
    KadIncomingGraph g;
    g.resize(400, 240);
    // eD2K REQs carry no sender ID: an asker not in our table has none
    const QCborMap row{{QStringLiteral("id"), 3}, {QStringLiteral("target"), kTarget}};
    const QCborMap graph{{QStringLiteral("found"), true},
                         {QStringLiteral("asker"), QCborMap{{QStringLiteral("id"), QString()}}},
                         {QStringLiteral("own"), QCborMap{{QStringLiteral("id"), idSharing(8)}}},
                         {QStringLiteral("handed"), QCborArray{node(idSharing(10), QStringLiteral("192.0.2.9:1"))}}};
    g.setRequest(row, graph, QStringLiteral("Node Lookup"), {});
    QVERIFY(!g.grab().toImage().isNull());

    // a bootstrap has no target: the hint, no crash
    g.setRequest(QCborMap{{QStringLiteral("id"), 4}}, graph, QStringLiteral("Bootstrap"), {});
    QCOMPARE(g.requestId(), quint64{4});
    QVERIFY(!g.grab().toImage().isNull());
    g.clear();
    QCOMPARE(g.requestId(), quint64{0});
}

QTEST_MAIN(tst_KadIncomingGraph)
#include "tst_KadIncomingGraph.moc"
