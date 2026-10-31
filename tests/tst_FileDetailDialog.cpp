/// @file tst_FileDetailDialog.cpp
/// @brief File Details dialog on a small screen, and the ED2K Link page's HTML format.
///
/// Issue #36: on an 800x600 monitor the dialog could not be shrunk to fit, and with
/// "HTML Format" ticked the link box rendered the anchor -- so it showed, and copied,
/// only the file name. Runs on an 800x600 offscreen screen set up in main().

#include "dialogs/FileDetailDialog.h"
#include "dialogs/FileDetailsMerge.h"
#include "prefs/Preferences.h"

#include <QApplication>
#include <QCheckBox>
#include <QCborMap>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QScreen>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>

using namespace eMule;

namespace {

constexpr int kScreenWidth  = 800;
constexpr int kScreenHeight = 600;

QCborMap fileDetails()
{
    const QString hash = QStringLiteral("0123456789ABCDEF0123456789ABCDEF");
    return QCborMap{
        {QStringLiteral("fileName"), QStringLiteral("Some.Linux.Distribution-x86_64.iso")},
        {QStringLiteral("hash"), hash},
        {QStringLiteral("fileSize"), 734003200},
        {QStringLiteral("completedSize"), 367001600},
        {QStringLiteral("percentCompleted"), 50.0},
        {QStringLiteral("status"), QStringLiteral("Downloading")},
        {QStringLiteral("fileType"), QStringLiteral("Iso")},   // the 7th tab too
        {QStringLiteral("ed2kLink"),
         QStringLiteral("ed2k://|file|Some.Linux.Distribution-x86_64.iso|734003200|%1|/")
             .arg(hash)}};
}

/// A download as GetDownloadDetails sends it: downTransferred marks one
QCborMap downloadDetails()
{
    QCborMap d = fileDetails();
    d.insert(QStringLiteral("fileType"), QStringLiteral("Video"));
    d.insert(QStringLiteral("fileName"), QStringLiteral("Some.Movie.mkv"));
    d.insert(QStringLiteral("dataPath"), QStringLiteral("/tmp/Temp/001.part"));
    d.insert(QStringLiteral("downTransferred"), 400000000);
    d.insert(QStringLiteral("sizeOnDisk"), 367001600);
    d.insert(QStringLiteral("partCount"), 77);
    d.insert(QStringLiteral("availablePartCount"), 40);
    d.insert(QStringLiteral("md4HashsetAvailable"), true);
    d.insert(QStringLiteral("aichHashsetAvailable"), false);
    d.insert(QStringLiteral("sourceCount"), 12);
    d.insert(QStringLiteral("validSourceCount"), 9);
    d.insert(QStringLiteral("nnpSourceCount"), 2);
    d.insert(QStringLiteral("a4afSourceCount"), 1);
    d.insert(QStringLiteral("transferringSrcCount"), 3);
    d.insert(QStringLiteral("containerVerdict"), 2);   // WrongContainer
    d.insert(QStringLiteral("containerActual"), QStringLiteral("MP4"));
    d.insert(QStringLiteral("addedOn"), 1'700'000'000);
    d.insert(QStringLiteral("dlActiveTime"), 3700);
    return d;
}

/// The captions of the General page, without the colon
QStringList generalCaptions(FileDetailDialog& dlg)
{
    QStringList out;
    auto* tabs = dlg.findChild<QTabWidget*>();
    for (auto* label : tabs->widget(0)->findChildren<QLabel*>())
        if (label->text().endsWith(QLatin1Char(':')))
            out << label->text().chopped(1);
    return out;
}

/// The General page's label showing exactly @p text; null: none
QLabel* generalLabel(FileDetailDialog& dlg, const QString& text)
{
    for (auto* label : dlg.findChild<QTabWidget*>()->widget(0)->findChildren<QLabel*>())
        if (label->text() == text)
            return label;
    return nullptr;
}

/// The download joined from ed3k (Ed3kBridge::appendDownloadInfo)
QCborMap ed3kDownloadDetails()
{
    QCborMap d = downloadDetails();
    d.insert(QStringLiteral("ed3kEnabled"), true);
    d.insert(QStringLiteral("ed3kTracked"), true);
    d.insert(QStringLiteral("ed3kRoot"), QStringLiteral("ab").repeated(32));
    d.insert(QStringLiteral("ed3kPair"), 1);
    d.insert(QStringLiteral("ed3kBytes"), 73400320);
    d.insert(QStringLiteral("ed3kSources"), 3);
    d.insert(QStringLiteral("ed3kTransferring"), 1);
    return d;
}

} // namespace

class TestFileDetailDialog : public QObject {
    Q_OBJECT

private slots:
    void fitsOnAn800x600Screen();
    void htmlLinkShowsAndCopiesTheMarkup();
    void downloadGeneralIsGroupedAndFits();
    void extControlsGateTheAdvancedRows();
    void severalDownloadsAddUp();
    void liveRefreshUpdatesTheShownLabels();
    void liveRefreshRebuildsWhenRowsChange();
    void liveRefreshSkipsOtherPages();
    void severalDownloadsRefresh();
    void ed3kGroupShowsTheJoin();
    void sharedFileShowsItsBlake3Root();
};

/// The window manager may not let the user drag a window smaller than its minimum, so
/// the minimum plus the title bar has to fit in the available screen height.
void TestFileDetailDialog::fitsOnAn800x600Screen()
{
    const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    QCOMPARE(avail.height(), kScreenHeight);

    for (int tab = FileDetailDialog::General; tab <= FileDetailDialog::Ed2kLink; ++tab) {
        FileDetailDialog dlg(fileDetails(), FileDetailDialog::Tab(tab));
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        QTest::qWait(10);   // the deferred re-fit

        const QString where = QStringLiteral("tab %1: min %2x%3, size %4x%5")
                                  .arg(tab)
                                  .arg(dlg.minimumWidth()).arg(dlg.minimumHeight())
                                  .arg(dlg.width()).arg(dlg.height());
        QVERIFY2(dlg.minimumHeight() <= avail.height() - 60, qPrintable(where));
        QVERIFY2(dlg.minimumWidth() <= avail.width() - 40, qPrintable(where));
        QVERIFY2(dlg.height() <= avail.height() - 60, qPrintable(where));

        // Not the preferred height: Qt's for a tab widget is its tallest preferred page,
        // and the link box and the lists ask for far more than they need
        QVERIFY2(dlg.minimumHeight() < dlg.layout()->totalSizeHint().height(),
                 qPrintable(where + QStringLiteral(", hint %1")
                                        .arg(dlg.layout()->totalSizeHint().height())));
    }
}

/// MFC shows the HTML source in the edit box; that text is what the button copies.
void TestFileDetailDialog::htmlLinkShowsAndCopiesTheMarkup()
{
    FileDetailDialog dlg(fileDetails(), FileDetailDialog::Ed2kLink);
    auto* edit = dlg.findChild<QTextEdit*>();
    QVERIFY(edit);
    auto* html = [&dlg]() -> QCheckBox* {
        for (auto* box : dlg.findChildren<QCheckBox*>())
            if (box->text() == FileDetailDialog::tr("HTML Format"))
                return box;
        return nullptr;
    }();
    QVERIFY(html);

    const QString markup = QStringLiteral(
        "<a href=\"ed2k://|file|Some.Linux.Distribution-x86_64.iso|734003200|"
        "0123456789ABCDEF0123456789ABCDEF|/\">Some.Linux.Distribution-x86_64.iso</a>");
    html->setChecked(true);
    dlg.applyEd2kLink(markup, false);   // the daemon's answer for the HTML variant
    QCOMPARE(edit->toPlainText(), markup);

    QPushButton* copy = nullptr;
    for (auto* button : dlg.findChildren<QPushButton*>())
        if (button->text() == FileDetailDialog::tr("Copy to Clipboard"))
            copy = button;
    QVERIFY(copy);
    copy->click();
    QCOMPARE(QGuiApplication::clipboard()->text(), markup);
}

/// Issue #97: MFC's General / Transfer / Dates boxes, still on an 800x600 screen
void TestFileDetailDialog::downloadGeneralIsGroupedAndFits()
{
    thePrefs.setShowExtControls(true);
    FileDetailDialog dlg(downloadDetails(), FileDetailDialog::General);
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    QTest::qWait(10);

    QStringList groups;
    for (auto* box : dlg.findChild<QTabWidget*>()->widget(0)->findChildren<QGroupBox*>())
        groups << box->title();
    QCOMPARE(groups, (QStringList{FileDetailDialog::tr("General"), FileDetailDialog::tr("Transfer"),
                                  FileDetailDialog::tr("Dates")}));

    const QRect avail = QGuiApplication::primaryScreen()->availableGeometry();
    QVERIFY2(dlg.minimumHeight() <= avail.height() - 60, qPrintable(QString::number(dlg.minimumHeight())));
    QVERIFY2(dlg.minimumWidth() <= avail.width() - 40, qPrintable(QString::number(dlg.minimumWidth())));

    // The values MFC formats this way
    QStringList texts;
    for (auto* label : dlg.findChildren<QLabel*>())
        texts << label->text();
    QVERIFY(texts.contains(QStringLiteral("77; available: 40 (51.9%)")));
    QVERIFY(texts.contains(QStringLiteral("12 (Useful: 9, NNP: 2); A4AF: 1")));
    QVERIFY(texts.contains(QStringLiteral("Yes (eD2K)")));
    QVERIFY(texts.contains(QStringLiteral("MP4 (Verified) - Invalid file extension: MKV")));
    QVERIFY(texts.contains(QStringLiteral("Downloading (from 3 source(s))")));
    QVERIFY(texts.contains(QStringLiteral("/tmp/Temp/001.part")));   // the .part, issue #56
    QVERIFY(texts.contains(FileDetailDialog::tr("Never")));          // seen complete
}

void TestFileDetailDialog::extControlsGateTheAdvancedRows()
{
    const QStringList advanced{FileDetailDialog::tr("Parts"), FileDetailDialog::tr("Hashset available"),
                               FileDetailDialog::tr("Compression"), FileDetailDialog::tr("Corrupted"),
                               FileDetailDialog::tr("Recovered")};

    thePrefs.setShowExtControls(true);
    {
        FileDetailDialog dlg(downloadDetails(), FileDetailDialog::General);
        const QStringList captions = generalCaptions(dlg);
        for (const QString& row : advanced)
            QVERIFY2(captions.contains(row), qPrintable(row));
    }

    thePrefs.setShowExtControls(false);
    FileDetailDialog dlg(downloadDetails(), FileDetailDialog::General);
    const QStringList captions = generalCaptions(dlg);
    for (const QString& row : advanced)
        QVERIFY2(!captions.contains(row), qPrintable(row));
    QVERIFY(captions.contains(FileDetailDialog::tr("Sources")));
    QStringList texts;
    for (auto* label : dlg.findChildren<QLabel*>())
        texts << label->text();
    QVERIFY(texts.contains(QStringLiteral("12")));   // the plain count
    thePrefs.setShowExtControls(true);
}

/// MFC FileDetailDialogInfo.cpp:228-327: per-file rows read "-", the rest add up
void TestFileDetailDialog::severalDownloadsAddUp()
{
    thePrefs.setShowExtControls(true);
    QCborMap second = downloadDetails();
    second.insert(QStringLiteral("hash"), QStringLiteral("FEDCBA9876543210FEDCBA9876543210"));
    second.insert(QStringLiteral("aichHashsetAvailable"), true);
    FileDetailDialog dlg(QList<QCborMap>{downloadDetails(), second}, FileDetailDialog::General);

    QCOMPARE(dlg.findChild<QTabWidget*>()->widget(0)->findChildren<QGroupBox*>().size(), 3);
    QStringList texts;
    for (auto* label : dlg.findChildren<QLabel*>())
        texts << label->text();
    QVERIFY(texts.contains(QStringLiteral("24 (Useful: 18, NNP: 4); A4AF: 2")));
    QVERIFY(texts.contains(QStringLiteral("-")));
    QVERIFY(!texts.contains(QStringLiteral("Yes (eD2K + AICH)")));   // one file lacks AICH
}

/// MFC's 5 s timer refreshes the page in place: same labels, new texts
void TestFileDetailDialog::liveRefreshUpdatesTheShownLabels()
{
    thePrefs.setShowExtControls(true);
    FileDetailDialog dlg(downloadDetails(), FileDetailDialog::General);
    QWidget* page = dlg.findChild<QTabWidget*>()->widget(0);
    QLabel* sources = generalLabel(dlg, QStringLiteral("12 (Useful: 9, NNP: 2); A4AF: 1"));
    QLabel* type = generalLabel(dlg, QStringLiteral("MP4 (Verified) - Invalid file extension: MKV"));
    QVERIFY(sources && type);
    QVERIFY(type->testAttribute(Qt::WA_SetPalette));   // red

    QCborMap next = downloadDetails();
    next.insert(QStringLiteral("sourceCount"), 15);
    next.insert(QStringLiteral("containerVerdict"), 1);   // now it matches
    dlg.refreshLive(next);

    QCOMPARE(dlg.findChild<QTabWidget*>()->widget(0), page);
    QCOMPARE(sources->text(), QStringLiteral("15 (Useful: 9, NNP: 2); A4AF: 1"));
    QCOMPARE(type->text(), QStringLiteral("MP4 (Verified)"));
    QVERIFY(!type->testAttribute(Qt::WA_SetPalette));
}

void TestFileDetailDialog::liveRefreshRebuildsWhenRowsChange()
{
    thePrefs.setShowExtControls(true);
    FileDetailDialog dlg(downloadDetails(), FileDetailDialog::General);
    auto* tabs = dlg.findChild<QTabWidget*>();
    const int count = tabs->count();
    QVERIFY(!generalCaptions(dlg).contains(FileDetailDialog::tr("BLAKE3 Hash")));

    dlg.refreshLive(ed3kDownloadDetails());   // the ed3k box comes

    QCOMPARE(tabs->count(), count);
    QCOMPARE(tabs->currentIndex(), 0);
    QCOMPARE(tabs->tabText(0), FileDetailDialog::tr("General"));
    QVERIFY(generalCaptions(dlg).contains(FileDetailDialog::tr("BLAKE3 Hash")));
}

/// The timer is the info page's: another page on top is left alone
void TestFileDetailDialog::liveRefreshSkipsOtherPages()
{
    FileDetailDialog dlg(downloadDetails(), FileDetailDialog::FileNames);
    QLabel* sources = generalLabel(dlg, QStringLiteral("12 (Useful: 9, NNP: 2); A4AF: 1"));
    QVERIFY(sources);
    QCborMap next = downloadDetails();
    next.insert(QStringLiteral("sourceCount"), 15);
    dlg.refreshLive(next);
    QCOMPARE(sources->text(), QStringLiteral("12 (Useful: 9, NNP: 2); A4AF: 1"));
}

void TestFileDetailDialog::severalDownloadsRefresh()
{
    thePrefs.setShowExtControls(true);
    QCborMap second = downloadDetails();
    second.insert(QStringLiteral("hash"), QStringLiteral("FEDCBA9876543210FEDCBA9876543210"));
    FileDetailDialog dlg(QList<QCborMap>{downloadDetails(), second}, FileDetailDialog::General);
    QLabel* sources = generalLabel(dlg, QStringLiteral("24 (Useful: 18, NNP: 4); A4AF: 2"));
    QVERIFY(sources);

    second.insert(QStringLiteral("sourceCount"), 20);
    dlg.refreshLive(mergeFileDetails({downloadDetails(), second}));
    QCOMPARE(sources->text(), QStringLiteral("32 (Useful: 18, NNP: 4); A4AF: 2"));
}

void TestFileDetailDialog::ed3kGroupShowsTheJoin()
{
    thePrefs.setShowExtControls(false);   // not one of the advanced rows
    FileDetailDialog dlg(ed3kDownloadDetails(), FileDetailDialog::General);
    QStringList groups;
    for (auto* box : dlg.findChild<QTabWidget*>()->widget(0)->findChildren<QGroupBox*>())
        groups << box->title();
    QCOMPARE(groups.last(), FileDetailDialog::tr("ed3k"));
    QVERIFY(generalLabel(dlg, QStringLiteral("ab").repeated(32) + QStringLiteral(" (Trusted)")));
    QVERIFY(generalLabel(dlg, QStringLiteral("3 (transferring 1)")));
    const auto labels = dlg.findChild<QTabWidget*>()->widget(0)->findChildren<QLabel*>();
    QVERIFY(std::ranges::any_of(labels, [](const QLabel* l) {   // 70 MB of 700
        return l->text().startsWith(QLatin1String("70")) && l->text().endsWith(QLatin1String(" (10.0%)"));
    }));

    // still searching for the root
    QCborMap searching = ed3kDownloadDetails();
    searching.remove(QStringLiteral("ed3kRoot"));
    searching.insert(QStringLiteral("ed3kSearching"), true);
    FileDetailDialog dlg2(searching, FileDetailDialog::General);
    QVERIFY(generalLabel(dlg2, FileDetailDialog::tr("Searching...")));

    // several: summed, the root reads "-"
    QCborMap second = ed3kDownloadDetails();
    second.insert(QStringLiteral("hash"), QStringLiteral("FEDCBA9876543210FEDCBA9876543210"));
    FileDetailDialog multi(QList<QCborMap>{ed3kDownloadDetails(), second}, FileDetailDialog::General);
    QVERIFY(generalLabel(multi, QStringLiteral("6 (transferring 2)")));
    thePrefs.setShowExtControls(true);
}

void TestFileDetailDialog::sharedFileShowsItsBlake3Root()
{
    QCborMap shared = fileDetails();
    shared.insert(QStringLiteral("ed3kEnabled"), true);
    shared.insert(QStringLiteral("ed3kRoot"), QStringLiteral("cd").repeated(32));
    shared.insert(QStringLiteral("ed3kAliasPublished"), true);
    FileDetailDialog dlg(shared, FileDetailDialog::General);
    QStringList texts;
    for (auto* label : dlg.findChild<QTabWidget*>()->widget(0)->findChildren<QLabel*>())
        texts << label->text();
    QVERIFY(texts.contains(QStringLiteral("cd").repeated(32)));
    QVERIFY(texts.contains(FileDetailDialog::tr("eD2K alias")));

    // without ed3k: no row
    FileDetailDialog plain(fileDetails(), FileDetailDialog::General);
    for (auto* label : plain.findChild<QTabWidget*>()->widget(0)->findChildren<QLabel*>())
        QVERIFY(!label->text().contains(FileDetailDialog::tr("BLAKE3 Hash")));
}

int main(int argc, char* argv[])
{
    // An 800x600 screen for the offscreen platform
    QTemporaryDir dir;
    const QString config = dir.filePath(QStringLiteral("screen.json"));
    QFile file(config);
    if (!file.open(QIODevice::WriteOnly))
        return 1;
    file.write(QStringLiteral(R"({"screens":[{"name":"small","x":0,"y":0,"width":%1,"height":%2,)"
                              R"("logicalDpi":96,"logicalBaseDpi":96,"dpr":1}]})")
                   .arg(kScreenWidth).arg(kScreenHeight).toUtf8());
    file.close();
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen:configfile=%1").arg(config).toUtf8());

    QApplication app(argc, argv);
    TestFileDetailDialog test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_FileDetailDialog.moc"
