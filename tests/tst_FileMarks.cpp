/// @file tst_FileMarks.cpp
/// @brief The marks column 0 draws — eMule's comment/rating icon and the
///        red exclamation for a file whose bytes contradict its name.
///
/// Two signals share one cell and they are not the same kind of claim. The
/// rating is what other users said; the exclamation is what the file's own
/// bytes say. Only the first is an opinion, and only the first is what the
/// "indicate ratings" preference is about.

#include "controls/DownloadListModel.h"
#include "controls/SharedFilesModel.h"
#include "prefs/Preferences.h"
#include "utils/RatingIcons.h"

#include <QIcon>
#include <QImage>
#include <QTest>

using namespace eMule;

class tst_FileMarks : public QObject {
    Q_OBJECT

private slots:
    void init();
    void ratingMarkFollowsMfcPredicate();
    void aKadNoteLookupHasItsOwnMark();
    void thePreferenceGovernsRatingsOnly();
    void aSuspectRowLooksDifferentFromACleanOne();
    void ourOwnCommentIsMarkedApartFromEveryoneElses();
    void theModelDecoratesOnlyTheNameColumn();
    void theTooltipTellsTheTwoKindsOfWrongApart();
    void theSharedListDrawsTheSameFakeMarkAsTheDownloadList();

private:
    [[nodiscard]] static QImage renderOf(const QIcon& icon);
    [[nodiscard]] static DownloadRow rowWith(bool suspect, const QString& actual);
};

QImage tst_FileMarks::renderOf(const QIcon& icon)
{
    return icon.pixmap(QSize(64, 16), 2).toImage();
}

DownloadRow tst_FileMarks::rowWith(bool suspect, const QString& actual)
{
    DownloadRow row;
    row.hash = QStringLiteral("00112233445566778899AABBCCDDEEFF");
    row.fileName = QStringLiteral("Some Movie.wmv");
    row.fileType = QStringLiteral("Video");
    row.status = QStringLiteral("ready");
    row.containerSuspect = suspect;
    row.containerExpected = QStringLiteral("ASF");
    row.containerActual = actual;
    return row;
}

void tst_FileMarks::init()
{
    thePrefs.setIndicateRatings(true);
}

void tst_FileMarks::ratingMarkFollowsMfcPredicate()
{
    // MFC: HasComment() || HasRating() || IsKadCommentSearchRunning().
    // Nothing at all means no mark, not a grey "not rated" one.
    QCOMPARE(ratingMark(/*hasComment*/ false, /*userRating*/ 0), FileMark::None);

    // Rated, so the mark is the rating.
    QCOMPARE(ratingMark(false, 1), FileMark::Fake);
    QCOMPARE(ratingMark(false, 2), FileMark::Poor);
    QCOMPARE(ratingMark(false, 3), FileMark::Fair);
    QCOMPARE(ratingMark(false, 4), FileMark::Good);
    QCOMPARE(ratingMark(false, 5), FileMark::Excellent);

    // Commented but unrated still earns a mark — the point is that there is
    // something to read, and MFC uses the "not rated" art to say so.
    QCOMPARE(ratingMark(true, 0), FileMark::NotRated);
}

void tst_FileMarks::aKadNoteLookupHasItsOwnMark()
{
    // userRating() folds the "searching" state in as 6 rather than adding a
    // second field, exactly as MFC does. It must not read as a rating of 6.
    QCOMPARE(ratingMark(false, 6), FileMark::KadSearching);
}

void tst_FileMarks::thePreferenceGovernsRatingsOnly()
{
    thePrefs.setIndicateRatings(false);
    QCOMPARE(ratingMark(true, 5), FileMark::None);

    // ...but a provable container mismatch is not an opinion, so the checkbox
    // does not silence it. Turning off "indicate ratings" must not hide a fake.
    const QIcon marked = fileMarksIcon(QStringLiteral("Video"), /*suspect*/ true,
                                       /*ownComment*/ false, ratingMark(true, 5));
    const QIcon plain = fileMarksIcon(QStringLiteral("Video"), /*suspect*/ false,
                                      /*ownComment*/ false, ratingMark(true, 5));
    QVERIFY(renderOf(marked) != renderOf(plain));
}

void tst_FileMarks::aSuspectRowLooksDifferentFromACleanOne()
{
    const QIcon clean = fileMarksIcon(QStringLiteral("Video"), false, false, FileMark::None);
    const QIcon suspect = fileMarksIcon(QStringLiteral("Video"), true, false, FileMark::None);
    const QIcon rated = fileMarksIcon(QStringLiteral("Video"), false, false, FileMark::Fake);

    QVERIFY(!renderOf(clean).isNull());
    QVERIFY(renderOf(clean) != renderOf(suspect));
    QVERIFY(renderOf(clean) != renderOf(rated));

    // The two are independent signals, so a file that is both must not collapse
    // into looking like either one alone.
    const QIcon both = fileMarksIcon(QStringLiteral("Video"), true, false, FileMark::Fake);
    QVERIFY(renderOf(both) != renderOf(suspect));
    QVERIFY(renderOf(both) != renderOf(rated));
}

void tst_FileMarks::ourOwnCommentIsMarkedApartFromEveryoneElses()
{
    // MFC draws two unrelated things: the rating mark for what other people said,
    // and an overlay on the type icon for the fact that *we* commented
    // (srchybrid/SharedFilesCtrl.cpp:561-568). They must not be interchangeable.
    const QIcon plain = fileMarksIcon(QStringLiteral("Video"), false, false, FileMark::None);
    const QIcon mine  = fileMarksIcon(QStringLiteral("Video"), false, true, FileMark::None);
    const QIcon theirs = fileMarksIcon(QStringLiteral("Video"), false, false, FileMark::Good);
    const QIcon both  = fileMarksIcon(QStringLiteral("Video"), false, true, FileMark::Good);

    QVERIFY(!renderOf(mine).isNull());
    QVERIFY(renderOf(mine) != renderOf(plain));
    QVERIFY(renderOf(mine) != renderOf(theirs));
    QVERIFY(renderOf(both) != renderOf(mine));
    QVERIFY(renderOf(both) != renderOf(theirs));

    // And it is our own note, not an opinion, so the ratings checkbox leaves it be.
    thePrefs.setIndicateRatings(false);
    const QIcon mineUnindicated =
        fileMarksIcon(QStringLiteral("Video"), false, true, ratingMark(true, 4));
    QVERIFY(renderOf(mineUnindicated) != renderOf(plain));
}

void tst_FileMarks::theModelDecoratesOnlyTheNameColumn()
{
    DownloadListModel model;
    model.setDownloads({rowWith(true, QString{})});

    const QVariant name = model.data(model.index(0, DownloadListModel::ColFileName, {}),
                                     Qt::DecorationRole);
    QVERIFY(name.canConvert<QIcon>());
    QVERIFY(!name.value<QIcon>().isNull());

    // Every other column keeps its text; an icon in the size or status cell
    // would just shove the numbers around.
    QVERIFY(model.data(model.index(0, DownloadListModel::ColSize, {}),
                       Qt::DecorationRole).isNull());
    QVERIFY(model.data(model.index(0, DownloadListModel::ColStatus, {}),
                       Qt::DecorationRole).isNull());
}

void tst_FileMarks::theTooltipTellsTheTwoKindsOfWrongApart()
{
    DownloadListModel model;

    // Known container: say what it actually is, because that one still plays.
    model.setDownloads({rowWith(true, QStringLiteral("MP4"))});
    const QString named = model.data(model.index(0, DownloadListModel::ColFileName, {}),
                                     Qt::ToolTipRole).toString();
    QVERIFY(named.contains(QStringLiteral("contents are MP4")));

    // Unrecognised: do not invent a container. "Very likely a fake" is the whole of
    // what is known, and pointing the user at a player would waste the trip. The
    // wording is core's containerWarningText(), shared with both web listings.
    model.setDownloads({rowWith(true, QString{})});
    const QString fake = model.data(model.index(0, DownloadListModel::ColFileName, {}),
                                    Qt::ToolTipRole).toString();
    QVERIFY(fake.contains(QStringLiteral("very likely a fake")));
    QVERIFY(!fake.contains(QStringLiteral("contents are")));

    // An honest file says nothing extra at all.
    model.setDownloads({rowWith(false, QString{})});
    const QString clean = model.data(model.index(0, DownloadListModel::ColFileName, {}),
                                     Qt::ToolTipRole).toString();
    QVERIFY(!clean.contains(QStringLiteral("likely fake")));
    QVERIFY(!clean.contains(QStringLiteral("Named .")));
}

void tst_FileMarks::theSharedListDrawsTheSameFakeMarkAsTheDownloadList()
{
    // The two lists draw the same cell and must read the same. The shared list used
    // to hard-code the fake mark off, so a file that had finished downloading lost
    // its red exclamation the moment it moved from Transfers to Shared Files —
    // which is exactly when the user is deciding whether to keep it.
    SharedFileRow row;
    row.hash = QStringLiteral("00112233445566778899AABBCCDDEEFF");
    row.fileName = QStringLiteral("Some Movie.wmv");
    row.fileType = QStringLiteral("Video");
    row.containerSuspect = true;
    row.containerExpected = QStringLiteral("ASF");
    row.containerActual = QStringLiteral("MP4");

    SharedFilesModel shared;
    shared.setRows({row});
    const QModelIndex name = shared.index(0, SharedFilesModel::ColFileName, {});

    SharedFileRow clean = row;
    clean.containerSuspect = false;
    clean.containerActual.clear();
    SharedFilesModel other;
    other.setRows({clean});

    QVERIFY(renderOf(shared.data(name, Qt::DecorationRole).value<QIcon>())
            != renderOf(other.data(other.index(0, SharedFilesModel::ColFileName, {}),
                                   Qt::DecorationRole).value<QIcon>()));

    // ...and it says the same thing on hover as the download list does, word for
    // word, because both go through fileMarksTooltip().
    DownloadListModel downloads;
    downloads.setDownloads({rowWith(true, QStringLiteral("MP4"))});
    const QString fromDownloads =
        downloads.data(downloads.index(0, DownloadListModel::ColFileName, {}),
                       Qt::ToolTipRole).toString();
    const QString fromShared = shared.data(name, Qt::ToolTipRole).toString();

    QVERIFY(fromShared.contains(QStringLiteral("contents are MP4")));
    QVERIFY(fromDownloads.contains(QStringLiteral("contents are MP4")));
    QCOMPARE(fromShared.section(QStringLiteral("\n\n"), 1),
             fromDownloads.section(QStringLiteral("\n\n"), 1));
}

QTEST_MAIN(tst_FileMarks)
#include "tst_FileMarks.moc"
