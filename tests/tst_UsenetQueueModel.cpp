/// @file tst_UsenetQueueModel.cpp
/// @brief The Usenet queue's progress bars and file checkboxes, off hand-built rows.
///
/// Two ways these could be wrong and still look fine: a release bar that lets
/// the held recovery volumes swamp it, and a checkbox that flips on click before
/// the daemon has agreed — which a refusal would then leave lying.

#include "controls/UsenetQueueModel.h"

#include <QCborArray>
#include <QCborMap>
#include <QSignalSpy>
#include <QTest>

#include <algorithm>

using namespace eMule;

namespace {

QCborMap fileMap(const QString& name, qint64 size, UsenetFileRowState state, bool isPar2 = false,
                 bool skipped = false, int index = 0)
{
    return QCborMap{
        {QStringLiteral("name"),    name},
        {QStringLiteral("size"),    size},
        {QStringLiteral("state"),   int(state)},
        {QStringLiteral("isPar2"),  isPar2},
        {QStringLiteral("skipped"), skipped},
        {QStringLiteral("index"),   index},
    };
}

QCborMap itemMap(int status, const QCborArray& files)
{
    return QCborMap{
        {QStringLiteral("id"),     QStringLiteral("rel")},
        {QStringLiteral("name"),   QStringLiteral("Some.Release")},
        {QStringLiteral("status"), status},
        {QStringLiteral("files"),  files},
    };
}

int countOf(const QByteArray& bar, quint8 code)
{
    return int(std::count(bar.cbegin(), bar.cend(), char(code)));
}

} // namespace

class tst_UsenetQueueModel : public QObject {
    Q_OBJECT

private slots:
    void aFileWithoutAMapIsDrawnFromItsState();
    void aReleaseBarLaysItsFilesOutBySize();
    void aCheckboxClickAsksAndChangesNothing();
};

void tst_UsenetQueueModel::aFileWithoutAMapIsDrawnFromItsState()
{
    UsenetFileRow f;
    f.state = UsenetFileRowState::Complete;
    QCOMPARE(usenetFileBar(f), QByteArray(1, char(kUsenetBarDone)));
    f.state = UsenetFileRowState::Skipped;
    QCOMPARE(usenetFileBar(f), QByteArray(1, char(kUsenetBarSkipped)));
    f.state = UsenetFileRowState::Queued;
    QCOMPARE(usenetFileBar(f), QByteArray(1, char(kUsenetBarQueued)));

    // The daemon's map wins whenever there is one.
    f.state = UsenetFileRowState::Partial;
    f.segmentMap = QByteArray::fromHex("01020300");
    QCOMPARE(usenetFileBar(f), f.segmentMap);
}

void tst_UsenetQueueModel::aReleaseBarLaysItsFilesOutBySize()
{
    const UsenetItemRow item = usenetRowFromCbor(itemMap(1, {
        fileMap(QStringLiteral("a.rar"), 3000, UsenetFileRowState::Complete),
        fileMap(QStringLiteral("b.rar"), 1000, UsenetFileRowState::Missing),
        // Held back and a hundred times larger: drawn, it would be the whole bar.
        fileMap(QStringLiteral("r.vol00+64.par2"), 300000, UsenetFileRowState::Held, true),
    }));

    QCOMPARE(item.bar.size(), 128);
    QCOMPARE(countOf(item.bar, kUsenetBarSkipped), 0);
    QCOMPARE(countOf(item.bar, kUsenetBarDone), 96);
    QCOMPARE(countOf(item.bar, kUsenetBarMissing), 32);
    // In file order: the finished archive first, the short one after it.
    QCOMPARE(quint8(item.bar.at(0)), kUsenetBarDone);
    QCOMPARE(quint8(item.bar.at(127)), kUsenetBarMissing);
}

void tst_UsenetQueueModel::aCheckboxClickAsksAndChangesNothing()
{
    UsenetQueueModel model;
    model.setItems({usenetRowFromCbor(itemMap(1, {
        fileMap(QStringLiteral("movie.mkv"), 5000, UsenetFileRowState::Partial, false, false, 0),
        fileMap(QStringLiteral("rel.par2"), 100, UsenetFileRowState::Queued, true, false, 1),
    }))});

    const QModelIndex release = model.index(0, 0);
    const QModelIndex movie = model.index(0, UsenetQueueModel::ColName, release);
    const QModelIndex par2 = model.index(1, UsenetQueueModel::ColName, release);

    QVERIFY(model.flags(movie) & Qt::ItemIsUserCheckable);
    QCOMPARE(movie.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));
    QVERIFY(!(model.flags(par2) & Qt::ItemIsUserCheckable));
    QVERIFY(!par2.data(Qt::CheckStateRole).isValid());

    QSignalSpy asked(&model, &UsenetQueueModel::fileSkipRequested);
    QVERIFY(!model.setData(movie, int(Qt::Unchecked), Qt::CheckStateRole));
    QCOMPARE(asked.count(), 1);
    QCOMPARE(asked.at(0).at(0).toString(), QStringLiteral("rel"));
    QCOMPARE(asked.at(0).at(1).toInt(), 0);
    QVERIFY(asked.at(0).at(2).toBool());
    // Unchanged until the daemon's push says otherwise.
    QCOMPARE(movie.data(Qt::CheckStateRole).toInt(), int(Qt::Checked));

    // A finished release is past choosing.
    model.upsertItem(usenetRowFromCbor(itemMap(3, {
        fileMap(QStringLiteral("movie.mkv"), 5000, UsenetFileRowState::Complete, false, false, 0),
        fileMap(QStringLiteral("rel.par2"), 100, UsenetFileRowState::Held, true, false, 1),
    })));
    QVERIFY(!(model.flags(model.index(0, UsenetQueueModel::ColName, model.index(0, 0)))
              & Qt::ItemIsUserCheckable));
}

QTEST_MAIN(tst_UsenetQueueModel)
#include "tst_UsenetQueueModel.moc"
