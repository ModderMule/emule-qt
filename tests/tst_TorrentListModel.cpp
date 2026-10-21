/// @file tst_TorrentListModel.cpp
/// @brief The Torrents tab's row model: decode, upsert/remove, sort, state names.

#include "controls/SortableItems.h"
#include "controls/TorrentListModel.h"
#include "controls/TorrentProgressDelegate.h"

#include <QCborMap>
#include <QSet>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;

namespace {

QCborMap row(const QString& id, const QString& name, qint64 size, double progress, int state,
             qint64 rateDown = 0)
{
    return QCborMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("size"), size},
        {QStringLiteral("done"), qint64(double(size) * progress)},
        {QStringLiteral("progress"), progress},
        {QStringLiteral("state"), state},
        {QStringLiteral("rateDown"), rateDown},
        {QStringLiteral("hasMetadata"), true},
        {QStringLiteral("hashV1"), QStringLiteral("0123456789abcdef0123456789abcdef01234567")},
        {QStringLiteral("pieceMap"), QByteArray("\x00\x80\xff", 3)},
        {QStringLiteral("eta"), qint64(-1)},
    };
}

QString sortedColumn(QSortFilterProxyModel& proxy, int column, Qt::SortOrder order)
{
    proxy.sort(column, order);
    QStringList names;
    for (int r = 0; r < proxy.rowCount(); ++r)
        names << proxy.index(r, TorrentListModel::ColName).data().toString();
    return names.join(QLatin1Char(','));
}

} // namespace

class tst_TorrentListModel : public QObject {
    Q_OBJECT

private slots:
    void decodesRow();
    void missingNameFallsBack();
    void upsertAndRemove();
    void setRowsMergesByKey();
    void sortsByValueNotText();
    void stateNames();
    void barRoles();
    void bucketColours();
};

void tst_TorrentListModel::decodesRow()
{
    const TorrentRow r = torrentRowFromCbor(row(QStringLiteral("bt:v1:AA"), QStringLiteral("Ubuntu"),
                                                1000, 0.5, 3, 2048));
    QCOMPARE(r.id, QStringLiteral("bt:v1:AA"));
    QCOMPARE(r.name, QStringLiteral("Ubuntu"));
    QCOMPARE(r.size, 1000);
    QCOMPARE(r.done, 500);
    QCOMPARE(r.state, TorrentState::Downloading);
    QCOMPARE(r.rateDown, 2048);
    QCOMPARE(r.pieceMap.size(), 3);
    // absent keys keep their "unknown" defaults
    QCOMPARE(r.peersTotal, -1);
    QCOMPARE(r.queuePosition, -1);
    QCOMPARE(r.eta, -1);
}

void tst_TorrentListModel::missingNameFallsBack()
{
    QCborMap m = row(QStringLiteral("bt:v1:AA"), {}, 0, 0, 2);
    QCOMPARE(torrentRowFromCbor(m).name, QStringLiteral("0123456789abcdef0123456789abcdef01234567"));
    // an out-of-range state is clamped, not cast blindly
    m.insert(QStringLiteral("state"), 99);
    QCOMPARE(torrentRowFromCbor(m).state, TorrentState::Moving);
}

void tst_TorrentListModel::upsertAndRemove()
{
    TorrentListModel model;
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);

    model.upsertRow(torrentRowFromCbor(row(QStringLiteral("a"), QStringLiteral("A"), 10, 0, 3)));
    model.upsertRow(torrentRowFromCbor(row(QStringLiteral("b"), QStringLiteral("B"), 20, 0, 3)));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(inserted.count(), 2);

    // same id: in place, no new row
    model.upsertRow(torrentRowFromCbor(row(QStringLiteral("a"), QStringLiteral("A2"), 10, 1, 5)));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.findById(QStringLiteral("a"))->name, QStringLiteral("A2"));

    model.removeRow(QStringLiteral("a"));
    model.removeRow(QStringLiteral("nope"));   // unknown id: ignored
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.idAt(0), QStringLiteral("b"));
    QCOMPARE(reset.count(), 0);   // never a reset: the view keeps its selection

    // an empty id is not a row
    model.upsertRow(TorrentRow{});
    QCOMPARE(model.rowCount(), 1);
}

void tst_TorrentListModel::setRowsMergesByKey()
{
    TorrentListModel model;
    model.setRows({torrentRowFromCbor(row(QStringLiteral("a"), QStringLiteral("A"), 1, 0, 3)),
                   torrentRowFromCbor(row(QStringLiteral("b"), QStringLiteral("B"), 1, 0, 3))});
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);

    model.setRows({torrentRowFromCbor(row(QStringLiteral("b"), QStringLiteral("B"), 1, 0, 5)),
                   torrentRowFromCbor(row(QStringLiteral("c"), QStringLiteral("C"), 1, 0, 3))});
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(reset.count(), 0);
    QVERIFY(!model.findById(QStringLiteral("a")));
    QCOMPARE(model.findById(QStringLiteral("b"))->state, TorrentState::Seeding);
    QVERIFY(model.findById(QStringLiteral("c")));
}

void tst_TorrentListModel::sortsByValueNotText()
{
    TorrentListModel model;
    // "9.77 KB" sorts after "1.00 MB" as text; the raw size must win
    model.setRows({
        torrentRowFromCbor(row(QStringLiteral("a"), QStringLiteral("Alpha"), 10'000, 0.07, 3, 900)),
        torrentRowFromCbor(row(QStringLiteral("b"), QStringLiteral("Bravo"), 1'048'576, 1.0, 5, 0)),
        torrentRowFromCbor(row(QStringLiteral("c"), QStringLiteral("Charlie"), 500, 0.5, 8, 50'000)),
    });
    SortValueProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setSortRole(Qt::UserRole);

    QCOMPARE(sortedColumn(proxy, TorrentListModel::ColSize, Qt::AscendingOrder),
             QStringLiteral("Charlie,Alpha,Bravo"));
    QCOMPARE(sortedColumn(proxy, TorrentListModel::ColProgress, Qt::DescendingOrder),
             QStringLiteral("Bravo,Charlie,Alpha"));
    QCOMPARE(sortedColumn(proxy, TorrentListModel::ColDownSpeed, Qt::DescendingOrder),
             QStringLiteral("Charlie,Alpha,Bravo"));
    // busy first, errors last
    QCOMPARE(sortedColumn(proxy, TorrentListModel::ColStatus, Qt::AscendingOrder),
             QStringLiteral("Alpha,Bravo,Charlie"));
    QCOMPARE(sortedColumn(proxy, TorrentListModel::ColName, Qt::DescendingOrder),
             QStringLiteral("Charlie,Bravo,Alpha"));
}

void tst_TorrentListModel::stateNames()
{
    QSet<QString> seen;
    for (int s = 0; s <= int(TorrentState::Moving); ++s) {
        const QString name = torrentStateName(static_cast<TorrentState>(s));
        QVERIFY2(!name.isEmpty(), qPrintable(QString::number(s)));
        QVERIFY2(!seen.contains(name), qPrintable(name));
        seen.insert(name);
    }
    QCOMPARE(torrentStateName(TorrentState::Seeding), QStringLiteral("Seeding"));
    QVERIFY(torrentStateActive(TorrentState::Downloading));
    QVERIFY(!torrentStateActive(TorrentState::Paused));
    QVERIFY(!torrentStateActive(TorrentState::Seeding));

    // the Status column names the error
    TorrentListModel model;
    QCborMap m = row(QStringLiteral("e"), QStringLiteral("E"), 1, 0, 8);
    m.insert(QStringLiteral("error"), QStringLiteral("disk full"));
    model.upsertRow(torrentRowFromCbor(m));
    QVERIFY(model.index(0, TorrentListModel::ColStatus).data().toString().contains(
        QStringLiteral("disk full")));
}

void tst_TorrentListModel::barRoles()
{
    TorrentListModel model;
    model.upsertRow(torrentRowFromCbor(row(QStringLiteral("a"), QStringLiteral("A"), 100, 0.25, 7)));
    model.upsertRow(torrentRowFromCbor(row(QStringLiteral("b"), QStringLiteral("B"), 100, 1.0, 5)));
    const QModelIndex a = model.index(0, TorrentListModel::ColProgress);
    const QModelIndex b = model.index(1, TorrentListModel::ColProgress);
    QCOMPARE(a.data(kTorrentBarRole).toByteArray().size(), 3);
    QCOMPARE(a.data(kTorrentBarPercentRole).toDouble(), 25.0);
    QVERIFY(a.data(kTorrentBarPausedRole).toBool());
    QVERIFY(!a.data(kTorrentBarCompleteRole).toBool());
    QVERIFY(b.data(kTorrentBarCompleteRole).toBool());
    // bar roles belong to the Progress column only
    QVERIFY(!model.index(0, TorrentListModel::ColName).data(kTorrentBarRole).isValid());
}

void tst_TorrentListModel::bucketColours()
{
    // empty: blue with peers, red without; full: grey; half: in between
    const QColor blue = TorrentProgressDelegate::bucketColor(0, true, false);
    const QColor red = TorrentProgressDelegate::bucketColor(0, false, false);
    const QColor have = TorrentProgressDelegate::bucketColor(255, true, false);
    QVERIFY(blue.blue() > blue.red());
    QVERIFY(red.red() > red.blue());
    QCOMPARE(have.red(), have.blue());
    const QColor half = TorrentProgressDelegate::bucketColor(128, true, false);
    QVERIFY(half != blue && half != have);
}

QTEST_MAIN(tst_TorrentListModel)
#include "tst_TorrentListModel.moc"
