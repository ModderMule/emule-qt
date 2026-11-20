/// @file tst_FileDetailDialog.cpp
/// @brief File Details dialog on a small screen, and the ED2K Link page's HTML format.
///
/// Issue #36: on an 800x600 monitor the dialog could not be shrunk to fit, and with
/// "HTML Format" ticked the link box rendered the anchor -- so it showed, and copied,
/// only the file name. Runs on an 800x600 offscreen screen set up in main().

#include "controls/ContentScrollArea.h"
#include "controls/ArchiveContentsView.h"
#include "dialogs/ArchivePreviewPanel.h"
#include "dialogs/FileDetailDialog.h"
#include "dialogs/FileDetailsMerge.h"
#include "prefs/Preferences.h"
#include "utils/DialogSizing.h"

#include <QApplication>
#include <QCheckBox>
#include <QCborMap>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QFormLayout>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QLayout>
#include <QPushButton>
#include <QCborArray>
#include <QCborMap>
#include <QSignalSpy>
#include <QScreen>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>
#include <QTreeWidget>

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
    void ed3kDownloadHasNamesAndMetadataFromKad();
    void sharedFileShowsItsBlake3Root();
    void shrinksAndKeepsItsSizeAfterARename();
    void scrollFitShrinksContentThatFits();
    void archiveViewOffersTakeFromDisk();
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

/// Issue #181: the minimum was the whole content, so the window could not be dragged
/// to fit a screen less its panels, and a rename (a rebuild) grew it back.
void TestFileDetailDialog::shrinksAndKeepsItsSizeAfterARename()
{
    for (const QCborMap& details : {fileDetails(), downloadDetails()}) {
        FileDetailDialog dlg(details, FileDetailDialog::General);
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        QTest::qWait(10);

        const QString where = QStringLiteral("%1: min %2, size %3")
                                  .arg(details.value(QStringLiteral("fileName")).toString())
                                  .arg(dlg.minimumHeight()).arg(dlg.height());
        QVERIFY2(dlg.minimumHeight() < dlg.height() - 50, qPrintable(where));

        // The user drags it smaller, then renames: the dialog is rebuilt from new details
        const QSize chosen(dlg.minimumWidth(), dlg.minimumHeight());
        dlg.resize(chosen);
        QTest::qWait(10);
        QCborMap renamed = details;
        renamed.insert(QStringLiteral("fileName"), QStringLiteral("Renamed.file.name.iso"));
        dlg.setDetails(renamed);
        QTest::qWait(10);
        QCOMPARE(dlg.size(), chosen);
    }
}

/// Issue #181 on a screen tall enough for all of it: Fit::Content pins the minimum to
/// the content, Fit::Scroll opens just as tall but lets the scroll area give way.
/// Issue #234: Content whose whole page scrolls is Scroll, floor included.
void TestFileDetailDialog::scrollFitShrinksContentThatFits()
{
    const auto measure = [](DialogSizing::Fit fit, bool scroll, QSize floor = {}) {
        QDialog dlg;
        auto* layout = new QVBoxLayout(&dlg);
        auto* page = new QWidget;
        auto* form = new QFormLayout(page);
        for (int row = 0; row < 8; ++row)
            form->addRow(QStringLiteral("Row %1:").arg(row), new QLabel(QStringLiteral("value")));
        if (scroll) {
            auto* area = new ContentScrollArea;
            area->setWidget(page);
            layout->addWidget(area);
        } else {
            layout->addWidget(page);
        }
        DialogSizing::applySize(&dlg, floor, {}, fit);
        return std::pair{dlg.minimumSize(), dlg.size()};
    };

    const auto [contentMin, contentSize] = measure(DialogSizing::Fit::Content, false);
    const auto [scrollMin, scrollSize] = measure(DialogSizing::Fit::Scroll, true);
    const auto [wholeMin, wholeSize] = measure(DialogSizing::Fit::Content, true);
    QVERIFY2(contentSize.height() < kScreenHeight - 120, qPrintable(QString::number(contentSize.height())));
    QCOMPARE(contentMin.height(), contentSize.height());
    QCOMPARE(scrollSize.height(), contentSize.height());
    QVERIFY2(scrollMin.height() < scrollSize.height() - 50,
             qPrintable(QStringLiteral("%1 of %2").arg(scrollMin.height()).arg(scrollSize.height())));
    QCOMPARE(wholeMin, scrollMin);
    QCOMPARE(wholeSize, scrollSize);

    // the hand-picked floor (scaled to the 800x600 screen) opens it, no longer locks it
    const QSize floor(500, 400);
    const auto [floorMin, floorSize] = measure(DialogSizing::Fit::Content, true, floor);
    QVERIFY2(floorSize.width() >= floorMin.width() + 150 && floorSize.height() >= floorMin.height() + 100,
             qPrintable(QStringLiteral("min %1x%2, size %3x%4").arg(floorMin.width()).arg(floorMin.height())
                            .arg(floorSize.width()).arg(floorSize.height())));
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
    // Relative: Qt splits the platform spec on ':', so "C:/..." would break on Windows
    qputenv("QT_QPA_PLATFORM", QStringLiteral("offscreen:configfile=%1")
                                   .arg(QDir::current().relativeFilePath(config)).toUtf8());

    QApplication app(argc, argv);
    TestFileDetailDialog test;
    return QTest::qExec(&test, argc, argv);
}

void TestFileDetailDialog::ed3kDownloadHasNamesAndMetadataFromKad()
{
    QCborMap d;
    d.insert(QStringLiteral("network"), QStringLiteral("ed3k"));
    d.insert(QStringLiteral("hash"), QStringLiteral("ab").repeated(32));
    d.insert(QStringLiteral("fileName"), QStringLiteral("release.rar"));
    d.insert(QStringLiteral("fileSize"), qint64(1000));
    d.insert(QStringLiteral("sourceNames"), QCborArray{QCborMap{{QStringLiteral("name"), QStringLiteral("other.rar")},
                                                                {QStringLiteral("count"), 2}}});
    FileDetailDialog dlg(d, FileDetailDialog::FileNames);
    auto* tabs = dlg.findChild<QTabWidget*>();
    QStringList titles;
    for (int i = 0; i < tabs->count(); ++i)
        titles << tabs->tabText(i);
    QCOMPARE(titles, (QStringList{FileDetailDialog::tr("General"), FileDetailDialog::tr("File Names"),
                                  FileDetailDialog::tr("Comments"), FileDetailDialog::tr("Media Info"),
                                  FileDetailDialog::tr("Metadata"), FileDetailDialog::tr("ed3k Link"),
                                  FileDetailDialog::tr("Archive"), FileDetailDialog::tr("Archive Preview")}));
    QCOMPARE(tabs->currentIndex(), 1);
    auto* names = tabs->widget(1)->findChild<QTreeWidget*>();
    QVERIFY(names);
    QCOMPARE(names->topLevelItemCount(), 1);
    QCOMPARE(names->topLevelItem(0)->text(0), QStringLiteral("other.rar"));

    // the tags of a later answer replace the Metadata page
    QVERIFY(!tabs->widget(4)->findChild<QTreeWidget*>());
    d.insert(QStringLiteral("tags"), QCborArray{QCborMap{{QStringLiteral("name"), QStringLiteral("Artist")},
                                                         {QStringLiteral("type"), 2},
                                                         {QStringLiteral("strValue"), QStringLiteral("Band")}}});
    dlg.applyDetails(d);
    auto* tags = tabs->widget(4)->findChild<QTreeWidget*>();
    QVERIFY(tags);
    QCOMPARE(tags->topLevelItem(0)->text(2), QStringLiteral("Band"));

    // no table of contents in the data: the Kad record's answer instead
    auto* preview = tabs->widget(7)->findChild<ArchivePreviewPanel*>();
    if (!preview)
        preview = qobject_cast<ArchivePreviewPanel*>(tabs->widget(7));
    QVERIFY(preview);
    auto* archive = qobject_cast<ArchiveContentsView*>(tabs->widget(6));
    QVERIFY(archive);
    emit preview->contentsNotFound();
    const auto status = [preview] {
        QStringList texts;
        for (const auto* l : preview->findChildren<QLabel*>())
            texts << l->text();
        return texts;
    };
    QVERIFY(status().contains(FileDetailDialog::tr("Looking up in Kad...")));
    emit archive->lookupFinished(0);
    QVERIFY(status().contains(FileDetailDialog::tr("Nothing found in Kad.")));
    QVERIFY(!status().contains(ArchivePreviewPanel::tr("Table of contents not found.")));
}

// #224: a queued download whose archive's every file is on disk here: marked, and "Take From
// Disk" hands that archive (name, parts, entries) on; one entry missing: not offered
void TestFileDetailDialog::archiveViewOffersTakeFromDisk()
{
    ArchiveContentsView view(nullptr, false);
    view.setQueued(true);
    QVERIFY(!view.takeFromDiskButton()->isHidden());
    const auto entry = [](const QString& path, bool local) {
        QCborMap e{{QStringLiteral("path"), path}, {QStringLiteral("size"), 10}, {QStringLiteral("root"), QString(64, u'b')}};
        if (local)
            e.insert(QStringLiteral("local"), true);
        return e;
    };
    QCborMap all{{QStringLiteral("name"), QStringLiteral("show.part1.rar")},
                 {QStringLiteral("state"), QStringLiteral("found")},
                 {QStringLiteral("parts"), QCborArray{QStringLiteral("show.part1.rar"), QStringLiteral("show.part2.rar")}},
                 {QStringLiteral("allLocal"), true},
                 {QStringLiteral("entries"), QCborArray{entry(QStringLiteral("a.mkv"), true), entry(QStringLiteral("b.nfo"), true)}}};
    QCborMap some{{QStringLiteral("name"), QStringLiteral("extra.rar")},
                  {QStringLiteral("state"), QStringLiteral("found")},
                  {QStringLiteral("entries"), QCborArray{entry(QStringLiteral("c.mkv"), true), entry(QStringLiteral("d.mkv"), false)}}};
    view.setResult(QCborMap{{QStringLiteral("archives"), QCborArray{all, some}}, {QStringLiteral("editable"), true}});
    QCOMPARE(view.tree()->topLevelItem(0)->child(0)->text(ArchiveContentsView::ColStatus), ArchiveContentsView::tr("On disk"));
    QVERIFY(view.tree()->topLevelItem(1)->child(1)->text(ArchiveContentsView::ColStatus).isEmpty());
    QVERIFY(view.takeFromDiskButton()->isEnabled());
    QSignalSpy spy(&view, &ArchiveContentsView::takeFromDiskRequested);
    view.takeFromDiskButton()->click();
    QCOMPARE(spy.count(), 1);
    const QCborArray archives = spy.at(0).at(0).value<QCborArray>();
    QCOMPARE(archives.size(), 1);
    const QCborMap a = archives.at(0).toMap();
    QCOMPARE(a.value(QStringLiteral("name")).toString(), QStringLiteral("show.part1.rar"));
    QCOMPARE(a.value(QStringLiteral("parts")).toArray().size(), 2);
    QCOMPARE(a.value(QStringLiteral("entries")).toArray().size(), 2);

    // unpacked already: nothing changes any more
    view.setResult(QCborMap{{QStringLiteral("archives"), QCborArray{all}}, {QStringLiteral("editable"), false}});
    QVERIFY(!view.takeFromDiskButton()->isEnabled());
    // only some on disk: the archive is still needed
    view.setResult(QCborMap{{QStringLiteral("archives"), QCborArray{some}}, {QStringLiteral("editable"), true}});
    QVERIFY(!view.takeFromDiskButton()->isEnabled());
}

#include "tst_FileDetailDialog.moc"
