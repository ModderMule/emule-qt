/// @file tst_SharedFilesModel.cpp
/// @brief The shared-files model takes a new list and single rows without a reset.

#include "controls/SharedFilesModel.h"
#include "dialogs/FileDetailsMerge.h"
#include "utils/SharedDirState.h"

#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;

class tst_SharedFilesModel : public QObject {
    Q_OBJECT

private slots:
    void mergeFileDetails_sumsAgreesAndLists();
    void altSort_sessionFiguresAndKadFirst();
    void sharedDirState_matchesWholeFoldersOnly();
    void sharedDirState_unshareWithSubdirs();
    void sharedDirState_nestsUnderNearestSharedParent();
    void firstListIsAReset();
    void sameListAgainTouchesNothing();
    void newListKeepsSurvivorsInPlace();
    void upsertUpdatesOneRowOrAppends();
    void removeFileDropsTheRowAndKeepsTheIndexRight();
    void hashesMatchInEitherCase();
    void browsedRowsWithoutAHashFallBackToAReset();
    void aLargeListIsDiffedQuickly();
    void completeSourcesReadAsARange();
    void hiddenColumnsCarryMfcsText();
    void sharedNetworksCellIsIconsOnly();
    void rowsWithoutAValueSortLastBothWays();
    void textFilterMatchesTheCellText();
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

// MFC CSharedFilesCtrl::GetItemDisplayText case 10 (SharedFilesCtrl.cpp:641-648)
void tst_SharedFilesModel::completeSourcesReadAsARange()
{
    SharedFilesModel model;
    SharedFileRow same = row(1, "same"), below = row(2, "below"), range = row(3, "range"),
                  none = row(4, "none");
    same.completeSourcesLo = same.completeSourcesHi = 7;
    below.completeSourcesHi = 5;
    range.completeSourcesLo = 3;
    range.completeSourcesHi = 9;
    model.setFiles({same, below, range, none});

    const auto text = [&](int r) {
        return model.index(r, SharedFilesModel::ColCompleteSources).data().toString();
    };
    QCOMPARE(text(0), QStringLiteral("7"));
    QCOMPARE(text(1), QStringLiteral("< 5"));
    QCOMPARE(text(2), QStringLiteral("3 - 9"));
    QCOMPARE(text(3), QStringLiteral("0"));   // lo == hi wins over lo == 0 here
}

void tst_SharedFilesModel::hiddenColumnsCarryMfcsText()
{
    SharedFilesModel model;
    SharedFileRow r = row(0xAB, "song.mp3");
    r.acceptedUploads = 2;
    r.allTimeAccepted = 11;
    r.artist = QStringLiteral("Artist");
    r.album = QStringLiteral("Album");
    r.title = QStringLiteral("Title");
    r.length = 3725;
    r.bitrate = 192;
    r.codec = QStringLiteral("MP3");
    model.setFiles({r, row(2, "bare.bin")});

    const auto text = [&](int rowNo, int col) { return model.index(rowNo, col).data().toString(); };
    QCOMPARE(text(0, SharedFilesModel::ColFileId), r.hash);
    QCOMPARE(text(0, SharedFilesModel::ColAccepted), QStringLiteral("2 (11)"));
    QCOMPARE(text(0, SharedFilesModel::ColArtist), QStringLiteral("Artist"));
    QCOMPARE(text(0, SharedFilesModel::ColAlbum), QStringLiteral("Album"));
    QCOMPARE(text(0, SharedFilesModel::ColTitle), QStringLiteral("Title"));
    QCOMPARE(text(0, SharedFilesModel::ColLength), QStringLiteral("1:02:05"));
    QCOMPARE(text(0, SharedFilesModel::ColBitrate), QStringLiteral("192 Kbit/s"));
    QCOMPARE(text(0, SharedFilesModel::ColCodec), QStringLiteral("MP3"));
    // no tag, no text
    QCOMPARE(text(1, SharedFilesModel::ColLength), QString());
    QCOMPARE(text(1, SharedFilesModel::ColBitrate), QString());

    QCOMPARE(model.headerData(SharedFilesModel::ColAccepted, Qt::Horizontal).toString(),
             QStringLiteral("Accepted Requests"));
}

// MFC paints two icons and no text (SharedFilesCtrl.cpp:585-595); "Yes|No" is the tip
void tst_SharedFilesModel::sharedNetworksCellIsIconsOnly()
{
    SharedFilesModel model;
    SharedFileRow both = row(1, "both"), kad = row(2, "kad"), none = row(3, "none");
    both.publishedED2K = both.kadPublished = true;
    kad.kadPublished = true;
    model.setFiles({both, kad, none});

    const auto cell = [&](int r) { return model.index(r, SharedFilesModel::ColSharedNetworks); };
    QCOMPARE(model.headerData(SharedFilesModel::ColSharedNetworks, Qt::Horizontal).toString(),
             QStringLiteral("Shared eD2K|Kad"));
    for (int r = 0; r < 3; ++r)
        QCOMPARE(cell(r).data().toString(), QString());
    QVERIFY(cell(0).data(Qt::DecorationRole).isValid());
    QVERIFY(cell(1).data(Qt::DecorationRole).isValid());
    QVERIFY(!cell(2).data(Qt::DecorationRole).isValid());
    QVERIFY(cell(0).data(Qt::ToolTipRole).toString().contains(QStringLiteral("Yes|Yes")));
    QVERIFY(cell(1).data(Qt::ToolTipRole).toString().contains(QStringLiteral("No|Yes")));
    // eD2K is the first sort key, Kad the second
    QVERIFY(cell(0).data(Qt::UserRole).toInt() > cell(1).data(Qt::UserRole).toInt());
    QVERIFY(cell(1).data(Qt::UserRole).toInt() > cell(2).data(Qt::UserRole).toInt());
}

// MFC's "undefined at bottom" comparers (SharedFilesCtrl.cpp:1211-1228)
void tst_SharedFilesModel::rowsWithoutAValueSortLastBothWays()
{
    SharedFilesModel model;
    SharedFileRow a = row(1, "a"), b = row(2, "b"), bare = row(3, "bare");
    a.artist = QStringLiteral("Abba");
    a.length = 100;
    b.artist = QStringLiteral("Zappa");
    b.length = 300;
    model.setFiles({bare, b, a});

    SharedFilesSortProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setSortRole(Qt::UserRole);
    const auto order = [&] {
        QStringList out;
        for (int i = 0; i < proxy.rowCount(); ++i)
            out << proxy.index(i, 0).data().toString();
        return out.join(QLatin1Char(','));
    };
    for (const int column : {SharedFilesModel::ColArtist, SharedFilesModel::ColLength}) {
        proxy.sort(column, Qt::AscendingOrder);
        QCOMPARE(order(), QStringLiteral("a,b,bare"));
        proxy.sort(column, Qt::DescendingOrder);
        QCOMPARE(order(), QStringLiteral("b,a,bare"));
    }
}

// MFC CSharedFilesCtrl::IsFilteredOut (SharedFilesCtrl.cpp:1450-1470)
void tst_SharedFilesModel::textFilterMatchesTheCellText()
{
    SharedFilesModel model;
    SharedFileRow a = row(1, "Linux Distro.iso"), b = row(2, "linux notes.txt"),
                  c = row(3, "holiday.avi");
    b.completeSourcesHi = 5;
    model.setFiles({a, b, c});

    SharedFilesSortProxy proxy;
    proxy.setSourceModel(&model);
    QCOMPARE(proxy.rowCount(), 3);

    proxy.setTextFilter({QStringLiteral("LINUX")}, SharedFilesModel::ColFileName);
    QCOMPARE(proxy.rowCount(), 2);
    proxy.setTextFilter({QStringLiteral("linux"), QStringLiteral("-notes")},
                        SharedFilesModel::ColFileName);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data().toString(), a.fileName);

    // the formatted cell, not the raw value
    proxy.setTextFilter({QStringLiteral("<")}, SharedFilesModel::ColCompleteSources);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data().toString(), b.fileName);

    // on top of the folder filter
    proxy.setTextFilter({QStringLiteral("linux")}, SharedFilesModel::ColFileName);
    proxy.setFolderFilter(SharedFilterType::Incomplete);
    QCOMPARE(proxy.rowCount(), 0);

    proxy.setFolderFilter(SharedFilterType::AllShared);
    proxy.setTextFilter({}, SharedFilesModel::ColFileName);
    QCOMPARE(proxy.rowCount(), 3);
}

// Issue #8: the folder tree marks a shared folder and bolds the ones above it. The
// checks run on the separator, or "/data/musicvideos" would bold "/data/music".
void tst_SharedFilesModel::sharedDirState_matchesWholeFoldersOnly()
{
    using namespace SharedDirState;
    const QStringList dirs{QStringLiteral("/data/music/albums/"),
                           QStringLiteral("/data/musicvideos")};

    QVERIFY(isSharedDir(dirs, QStringLiteral("/data/music/albums")));   // trailing separator
    QVERIFY(isSharedDir(dirs, QStringLiteral("/Data/MusicVideos/")));   // case, as the core
    QVERIFY(!isSharedDir(dirs, QStringLiteral("/data/music")));
    QVERIFY(!isSharedDir(dirs, QString()));

    QVERIFY(hasSharedSubdir(dirs, QStringLiteral("/data/music")));
    QVERIFY(hasSharedSubdir(dirs, QStringLiteral("/data/")));
    QVERIFY(hasSharedSubdir(dirs, QStringLiteral("/")));
    QVERIFY2(!hasSharedSubdir(dirs, QStringLiteral("/data/mus")),
             "a name prefix is not a parent folder");
    QVERIFY2(!hasSharedSubdir(dirs, QStringLiteral("/data/music/albums")),
             "a folder is not below itself");
    QVERIFY(!hasSharedSubdir(dirs, QStringLiteral("/data/music/albums/live")));
    QVERIFY(!hasSharedSubdir({}, QStringLiteral("/data")));

    QVERIFY(hasDirAtOrBelow(dirs, QStringLiteral("/data/musicvideos")));
    QVERIFY(hasDirAtOrBelow(dirs, QStringLiteral("/data")));
    QVERIFY(!hasDirAtOrBelow(dirs, QStringLiteral("/data/photos")));
}

void tst_SharedFilesModel::sharedDirState_unshareWithSubdirs()
{
    using namespace SharedDirState;
    const QStringList dirs{QStringLiteral("/data/music"), QStringLiteral("/data/music/albums"),
                           QStringLiteral("/data/music/albums/live"),
                           QStringLiteral("/data/musicvideos")};

    QCOMPARE(withoutDir(dirs, QStringLiteral("/data/music/albums/"), false),
             (QStringList{QStringLiteral("/data/music"), QStringLiteral("/data/music/albums/live"),
                          QStringLiteral("/data/musicvideos")}));
    QCOMPARE(withoutDir(dirs, QStringLiteral("/data/music"), true),
             QStringList{QStringLiteral("/data/musicvideos")});
    // From a folder that is not shared itself: only what is below it goes.
    QCOMPARE(withoutDir(dirs, QStringLiteral("/data/music/albums"), true),
             (QStringList{QStringLiteral("/data/music"), QStringLiteral("/data/musicvideos")}));
    QCOMPARE(withoutDir(dirs, QStringLiteral("/data/photos"), true), dirs);
}

void tst_SharedFilesModel::sharedDirState_nestsUnderNearestSharedParent()
{
    using namespace SharedDirState;
    const QString music = QStringLiteral("/data/music");
    const QString live = QStringLiteral("/data/music/albums/live");
    const QString bootleg = QStringLiteral("/data/music/albums/live/bootleg");
    const QString videos = QStringLiteral("/data/musicvideos");

    const auto parents = nearestSharedParents({music, live, bootleg, videos});
    QCOMPARE(parents.value(music), QString());
    QCOMPARE(parents.value(videos), QString());
    QCOMPARE(parents.value(live), music);       // "albums" between them is not shared
    QCOMPARE(parents.value(bootleg), live);     // the nearest one, not the topmost
}

// MFC SharedFilesCtrl SortProc: cases 5-7 against 105-107, 11 against 111.
void tst_SharedFilesModel::altSort_sessionFiguresAndKadFirst()
{
    SharedFilesModel model;
    SharedFileRow a, b;
    a.hash = QStringLiteral("aa");
    a.fileName = QStringLiteral("a");
    a.requests = 1;            // little today,
    a.allTimeRequests = 900;   // a lot over time
    a.publishedED2K = true;
    b.hash = QStringLiteral("bb");
    b.fileName = QStringLiteral("b");
    b.requests = 5;
    b.allTimeRequests = 10;
    b.kadPublished = true;
    model.setFiles({a, b});

    SharedFilesSortProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setSortRole(Qt::UserRole);
    const auto top = [&] { return proxy.index(0, 0).data().toString(); };

    proxy.sort(SharedFilesModel::ColRequests, Qt::DescendingOrder);
    QCOMPARE(top(), QStringLiteral("a"));     // all-time
    proxy.setAltSort(SharedFilesModel::ColRequests, true);
    QCOMPARE(top(), QStringLiteral("b"));     // this session

    proxy.sort(SharedFilesModel::ColSharedNetworks, Qt::DescendingOrder);
    QCOMPARE(top(), QStringLiteral("a"));     // on eD2K
    proxy.setAltSort(SharedFilesModel::ColSharedNetworks, true);
    QCOMPARE(top(), QStringLiteral("b"));     // on Kad
}

// The combined detail sheet of a multi-selection (MFC CFileDetailDialog with several files).
void tst_SharedFilesModel::mergeFileDetails_sumsAgreesAndLists()
{
    const auto file = [](qint64 size, qint64 done, const QString& artist, qint64 bitrate,
                         const QString& link, const QString& comment) {
        QCborArray comments;
        if (!comment.isEmpty())
            comments.append(QCborMap{{QStringLiteral("comment"), comment}});
        return QCborMap{
            {QStringLiteral("fileSize"), size}, {QStringLiteral("completedSize"), done},
            {QStringLiteral("sourceCount"), 3}, {QStringLiteral("mediaArtist"), artist},
            {QStringLiteral("mediaAlbum"), QStringLiteral("Same Album")},
            {QStringLiteral("mediaBitrate"), bitrate}, {QStringLiteral("mediaLength"), 60},
            {QStringLiteral("ed2kLink"), link}, {QStringLiteral("comments"), comments},
            {QStringLiteral("canComment"), true}, {QStringLiteral("hash"), QStringLiteral("ab")}};
    };
    const QCborMap merged = mergeFileDetails({
        file(1000, 250, QStringLiteral("A"), 128, QStringLiteral("ed2k://1"), QStringLiteral("good")),
        file(3000, 750, QStringLiteral("B"), 128, QStringLiteral("ed2k://2"), QString()),
        file(0, 0, QStringLiteral("A"), 128, QString(), QStringLiteral("fake")),
    });
    const auto value = [&merged](const char* key) { return merged.value(QLatin1StringView(key)); };

    QCOMPARE(value("multiCount").toInteger(), 3);
    QCOMPARE(value("fileSize").toInteger(), 4000);
    QCOMPARE(value("completedSize").toInteger(), 1000);
    QCOMPARE(value("percentCompleted").toDouble(), 25.0);
    QCOMPARE(value("sourceCount").toInteger(), 9);
    QCOMPARE(value("mediaLength").toInteger(), 180);
    QCOMPARE(value("mediaArtist").toString(), QString());                    // they differ
    QCOMPARE(value("mediaAlbum").toString(), QStringLiteral("Same Album"));
    QCOMPARE(value("mediaBitrate").toInteger(), 128);
    QCOMPARE(value("comments").toArray().size(), 2);
    QCOMPARE(value("ed2kLink").toString(), QStringLiteral("ed2k://1\ned2k://2"));
    QVERIFY(!value("canComment").toBool());
    QVERIFY(!merged.contains(QLatin1StringView("hash")));
}

QTEST_MAIN(tst_SharedFilesModel)
#include "tst_SharedFilesModel.moc"
