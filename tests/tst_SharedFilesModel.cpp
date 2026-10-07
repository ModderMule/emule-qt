/// @file tst_SharedFilesModel.cpp
/// @brief The shared-files model takes a new list and single rows without a reset.

#include "controls/SharedFilesModel.h"

#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;

class tst_SharedFilesModel : public QObject {
    Q_OBJECT

private slots:
    void firstListIsAReset();
    void sameListAgainTouchesNothing();
    void newListKeepsSurvivorsInPlace();
    void upsertUpdatesOneRowOrAppends();
    void removeFileDropsTheRowAndKeepsTheIndexRight();
    void hashesMatchInEitherCase();
    void browsedRowsWithoutAHashFallBackToAReset();
    void aLargeListIsDiffedQuickly();
};

namespace {

SharedFileRow row(int id, const char* name, int64_t requests = 0)
{
    SharedFileRow r;
    r.hash = QStringLiteral("%1").arg(id, 32, 16, QLatin1Char('0')).toUpper();
    r.fileName = QString::fromLatin1(name);
    r.fileSize = 1000 + id;
    r.requests = requests;
    return r;
}

QString names(const SharedFilesModel& model)
{
    QStringList out;
    for (int i = 0; i < model.fileCount(); ++i)
        out << model.fileAt(i)->fileName;
    return out.join(QLatin1Char(','));
}

} // namespace

void tst_SharedFilesModel::firstListIsAReset()
{
    SharedFilesModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QVERIFY(model.setFiles({row(1, "a"), row(2, "b")}));
    QCOMPARE(reset.count(), 1);
    QCOMPARE(model.fileCount(), 2);
    QVERIFY(model.containsHash(row(2, "b").hash));
}

void tst_SharedFilesModel::sameListAgainTouchesNothing()
{
    SharedFilesModel model;
    model.setFiles({row(1, "a"), row(2, "b"), row(3, "c")});

    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    // the daemon's map order is not stable; the list order here must be
    QVERIFY(!model.setFiles({row(3, "c"), row(1, "a"), row(2, "b")}));
    QCOMPARE(reset.count() + changed.count() + inserted.count() + removed.count(), 0);
    QCOMPARE(names(model), QStringLiteral("a,b,c"));
}

void tst_SharedFilesModel::newListKeepsSurvivorsInPlace()
{
    SharedFilesModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setFiles({row(1, "a"), row(2, "b"), row(3, "c"), row(4, "d"), row(5, "e")});
    const QPersistentModelIndex keepC(model.index(2, 0));
    const QPersistentModelIndex keepE(model.index(4, 0));

    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);

    // a and d left (b too: two runs), c changed, f and g are new
    QVERIFY(!model.setFiles({row(7, "g"), row(5, "e"), row(3, "c", 9), row(6, "f")}));

    QCOMPARE(reset.count(), 0);
    QCOMPARE(removed.count(), 2);     // [d] and [a, b]
    QCOMPARE(changed.count(), 1);     // only c
    QCOMPARE(inserted.count(), 1);    // one batch
    QCOMPARE(names(model), QStringLiteral("c,e,g,f"));
    QVERIFY(keepC.isValid() && keepE.isValid());
    QCOMPARE(keepC.row(), 0);
    QCOMPARE(keepE.row(), 1);
    QCOMPARE(model.findByHash(row(3, "c").hash)->requests, 9);
    QVERIFY(!model.containsHash(row(1, "a").hash));
    QCOMPARE(model.findByHash(row(6, "f").hash)->fileName, QStringLiteral("f"));
}

void tst_SharedFilesModel::upsertUpdatesOneRowOrAppends()
{
    SharedFilesModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setFiles({row(1, "a"), row(2, "b")});

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);

    model.upsertFiles({row(2, "b", 5)});
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.first().at(0).toModelIndex().row(), 1);
    QCOMPARE(inserted.count(), 0);

    model.upsertFiles({row(2, "b", 5)});   // nothing new in it
    QCOMPARE(changed.count(), 1);

    // one known, two new, one of the new ones twice
    model.upsertFiles({row(1, "a", 3), row(3, "c"), row(4, "d"), row(3, "c", 8)});
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(names(model), QStringLiteral("a,b,c,d"));
    QCOMPARE(model.findByHash(row(3, "c").hash)->requests, 8);
    QCOMPARE(model.findByHash(row(1, "a").hash)->requests, 3);
}

void tst_SharedFilesModel::removeFileDropsTheRowAndKeepsTheIndexRight()
{
    SharedFilesModel model;
    QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
    model.setFiles({row(1, "a"), row(2, "b"), row(3, "c")});

    QVERIFY(model.removeFile(row(1, "a").hash));
    QVERIFY(!model.removeFile(row(1, "a").hash));
    QCOMPARE(names(model), QStringLiteral("b,c"));
    // the rows behind it moved: lookups and updates must land on the right one
    QCOMPARE(model.findByHash(row(3, "c").hash)->fileName, QStringLiteral("c"));
    model.upsertFiles({row(3, "c", 4)});
    QCOMPARE(model.fileAt(1)->requests, 4);

    model.clear();
    QVERIFY(!model.containsHash(row(2, "b").hash));
    model.upsertFiles({row(2, "b")});
    QCOMPARE(model.fileCount(), 1);
}

void tst_SharedFilesModel::hashesMatchInEitherCase()
{
    SharedFilesModel model;
    SharedFileRow upper = row(0xABCDEF, "x");
    model.setFiles({upper});
    QVERIFY(model.containsHash(upper.hash.toLower()));
    SharedFileRow lower = upper;
    lower.hash = upper.hash.toLower();
    lower.requests = 2;
    model.upsertFiles({lower});
    QCOMPARE(model.fileCount(), 1);
    QVERIFY(model.removeFile(upper.hash.toLower()));
}

void tst_SharedFilesModel::browsedRowsWithoutAHashFallBackToAReset()
{
    SharedFilesModel model;
    SharedFileRow unshared;
    unshared.fileName = QStringLiteral("not shared.bin");
    model.resetFiles({unshared, unshared, row(1, "a")});
    QCOMPARE(model.fileCount(), 3);
    QVERIFY(model.containsHash(row(1, "a").hash));

    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QVERIFY(model.setFiles({row(1, "a"), row(2, "b")}));   // back to the shared list
    QCOMPARE(reset.count(), 1);
    QCOMPARE(names(model), QStringLiteral("a,b"));
}

void tst_SharedFilesModel::aLargeListIsDiffedQuickly()
{
    constexpr int kFiles = 50000;
    std::vector<SharedFileRow> rows;
    rows.reserve(kFiles);
    for (int i = 0; i < kFiles; ++i)
        rows.push_back(row(i + 1, qPrintable(QStringLiteral("file %1").arg(i))));

    SharedFilesModel model;
    model.setFiles(rows);

    // every 100th file changed, every 1000th left, a few are new
    std::vector<SharedFileRow> next;
    next.reserve(kFiles);
    for (int i = 0; i < kFiles; ++i) {
        if (i % 1000 == 999)
            continue;
        next.push_back(rows[static_cast<size_t>(i)]);
        if (i % 100 == 0)
            next.back().requests = 1;
    }
    for (int i = 0; i < 10; ++i)
        next.push_back(row(kFiles + 1 + i, qPrintable(QStringLiteral("new %1").arg(i))));

    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!model.setFiles(std::move(next)));
    const qint64 ms = timer.elapsed();
    qInfo("diff of %d rows: %lld ms", kFiles, static_cast<long long>(ms));
    QCOMPARE(model.fileCount(), kFiles - 50 + 10);
    QCOMPARE(changed.count(), 500);
    // MSVC debug (checked iterators, debug heap) lands right at 2 s on a quiet machine
#if defined(_MSC_VER) && defined(_DEBUG)
    constexpr qint64 kBudgetMs = 10000;
#else
    constexpr qint64 kBudgetMs = 2000;
#endif
    QVERIFY2(ms < kBudgetMs, "the diff is no longer linear");
}

QTEST_MAIN(tst_SharedFilesModel)
#include "tst_SharedFilesModel.moc"
