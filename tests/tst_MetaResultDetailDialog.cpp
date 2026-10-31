/// @file tst_MetaResultDetailDialog.cpp
/// @brief A torrent/Usenet search result's Details, off hand-built inspection maps.
///        No daemon: the dialog is fed what FetchMetaFile / InspectIndexerResult return.

#include "dialogs/MetaResultDetailDialog.h"

#include "controls/UsenetFileCheckList.h"

#include <QApplication>
#include <QCborArray>
#include <QMap>
#include <QMessageBox>
#include <QTimer>

#include <algorithm>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTest>
#include <QTreeWidget>

using namespace eMule;

class tst_MetaResultDetailDialog : public QObject {
    Q_OBJECT

private slots:
    void fetchingUntilFilled();
    void torrentShowsTreeAndTrackers();
    void nzbShowsFilesAndFacts();
    void errorShowsMessage();
    void downloadEmits();
    void nzbChecksAllByDefault();
    void nzbChecksRowFileSetOnly();
    void nzbDownloadSendsUnchecked();
    void nzbNoneCheckedBlocks();
};

namespace {

QCborMap torrentDetails()
{
    const QCborArray files{
        QCborMap{{QStringLiteral("index"), 0}, {QStringLiteral("path"), QStringLiteral("demo/dir/a.txt")},
                 {QStringLiteral("size"), 10}, {QStringLiteral("pad"), false}},
        QCborMap{{QStringLiteral("index"), 1}, {QStringLiteral("path"), QStringLiteral("demo/.pad/5")},
                 {QStringLiteral("size"), 5}, {QStringLiteral("pad"), true}},
        QCborMap{{QStringLiteral("index"), 2}, {QStringLiteral("path"), QStringLiteral("demo/b.bin")},
                 {QStringLiteral("size"), 20}, {QStringLiteral("pad"), false}},
    };
    const QCborMap inspection{
        {QStringLiteral("name"), QStringLiteral("demo")},
        {QStringLiteral("infoHashV1"), QString(40, u'a')},
        {QStringLiteral("totalSize"), 35},
        {QStringLiteral("pieceLength"), 16384},
        {QStringLiteral("pieceCount"), 1},
        {QStringLiteral("files"), files},
        {QStringLiteral("trackers"), QCborArray{QCborMap{{QStringLiteral("url"), QStringLiteral("http://t.example/a")},
                                                         {QStringLiteral("tier"), 0}}}},
        {QStringLiteral("webSeeds"), QCborArray{QStringLiteral("http://seed.example/")}},
    };
    return {{QStringLiteral("kind"), QStringLiteral("torrent")}, {QStringLiteral("inspection"), inspection}};
}

QTreeWidgetItem* childNamed(QTreeWidgetItem* parent, const QString& name)
{
    for (int i = 0; i < parent->childCount(); ++i)
        if (parent->child(i)->text(0) == name)
            return parent->child(i);
    return nullptr;
}

/// a.part1.rar + a.part2.rar (set "a"), b.mkv, c.par2
QCborMap nzbChooseDetails(int rowFileIndex)
{
    const auto file = [](int index, const char* name, const char* setKey, bool par2) {
        return QCborMap{{QStringLiteral("index"), index}, {QStringLiteral("name"), QLatin1StringView(name)},
                        {QStringLiteral("size"), 100}, {QStringLiteral("segments"), 1},
                        {QStringLiteral("isPar2"), par2}, {QStringLiteral("setKey"), QLatin1StringView(setKey)}};
    };
    const QCborArray files{file(0, "a.part1.rar", "a", false), file(1, "a.part2.rar", "a", false),
                           file(2, "b.mkv", "", false), file(3, "c.par2", "", true)};
    return {{QStringLiteral("kind"), QStringLiteral("nzb")},
            {QStringLiteral("rowFileIndex"), rowFileIndex},
            {QStringLiteral("inspection"), QCborMap{{QStringLiteral("name"), QStringLiteral("R")},
                                                    {QStringLiteral("files"), files}}}};
}

/// File index → checked, from the Files tree.
QMap<int, bool> checkStates(const MetaResultDetailDialog& dlg)
{
    QMap<int, bool> out;
    QTreeWidget* tree = dlg.filesTree();
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = tree->topLevelItem(i);
        const QString name = item->text(MetaResultDetailDialog::NzbName);
        const int index = name == QLatin1String("a.part1.rar") ? 0
                          : name == QLatin1String("a.part2.rar") ? 1
                          : name == QLatin1String("b.mkv")       ? 2
                                                                 : 3;
        out.insert(index, item->checkState(MetaResultDetailDialog::NzbName) == Qt::Checked);
    }
    return out;
}

QStringList formLabels(QWidget* page)
{
    QStringList out;
    for (auto* label : page->findChildren<QLabel*>())
        out.append(label->text());
    return out;
}

} // namespace

void tst_MetaResultDetailDialog::fetchingUntilFilled()
{
    MetaResultDetailDialog dlg({.name = QStringLiteral("demo"), .torrent = true});
    QCOMPARE(dlg.findChild<QStackedWidget*>()->currentWidget(), dlg.noteLabel());
    QVERIFY(!dlg.downloadButton()->isEnabled());
}

void tst_MetaResultDetailDialog::torrentShowsTreeAndTrackers()
{
    MetaResultDetailDialog dlg({.name = QStringLiteral("demo"), .torrent = true, .source = QStringLiteral("Idx")});
    dlg.setDetails(torrentDetails());

    QCOMPARE(dlg.findChild<QStackedWidget*>()->currentWidget(), dlg.tabs());
    QVERIFY(dlg.downloadButton()->isEnabled());
    QCOMPARE(dlg.tabs()->tabText(MetaResultDetailDialog::TabFiles), QStringLiteral("Files (2)"));
    QVERIFY(dlg.tabs()->isTabVisible(MetaResultDetailDialog::TabTrackers));

    // demo/ { dir/ { a.txt }, b.bin }, no pad file
    QTreeWidget* tree = dlg.filesTree();
    QCOMPARE(tree->topLevelItemCount(), 1);
    QTreeWidgetItem* root = tree->topLevelItem(0);
    QCOMPARE(root->text(0), QStringLiteral("demo"));
    QCOMPARE(root->childCount(), 2);
    QVERIFY(childNamed(root, QStringLiteral(".pad")) == nullptr);
    QTreeWidgetItem* dir = childNamed(root, QStringLiteral("dir"));
    QVERIFY(dir);
    QVERIFY(childNamed(dir, QStringLiteral("a.txt")));
    QVERIFY(childNamed(root, QStringLiteral("b.bin")));
    // folder size: the real files only
    QCOMPARE(root->data(MetaResultDetailDialog::TorrentSize, Qt::UserRole + 100).toLongLong(), 30);

    QCOMPARE(dlg.trackersTree()->topLevelItemCount(), 2);   // tracker + web seed
    QVERIFY(formLabels(dlg.tabs()->widget(MetaResultDetailDialog::TabGeneral)).contains(QStringLiteral("Idx")));
}

void tst_MetaResultDetailDialog::nzbShowsFilesAndFacts()
{
    const QCborArray files{
        QCborMap{{QStringLiteral("index"), 0}, {QStringLiteral("name"), QStringLiteral("a.rar")},
                 {QStringLiteral("size"), 1000}, {QStringLiteral("segments"), 3},
                 {QStringLiteral("missingSegments"), 1}, {QStringLiteral("isPar2"), false}},
        QCborMap{{QStringLiteral("index"), 1}, {QStringLiteral("name"), QStringLiteral("a.par2")},
                 {QStringLiteral("size"), 10}, {QStringLiteral("segments"), 1}, {QStringLiteral("isPar2"), true}},
    };
    const QCborMap inspection{
        {QStringLiteral("name"), QStringLiteral("Release")},
        {QStringLiteral("files"), files},
        {QStringLiteral("poster"), QStringLiteral("poster@example")},
        {QStringLiteral("groups"), QCborArray{QStringLiteral("alt.binaries.test")}},
        {QStringLiteral("articles"), 4},
        {QStringLiteral("totalSize"), 1010},
    };
    MetaResultDetailDialog dlg({.name = QStringLiteral("Release")});
    dlg.setDetails({{QStringLiteral("kind"), QStringLiteral("nzb")}, {QStringLiteral("inspection"), inspection}});

    QVERIFY(!dlg.tabs()->isTabVisible(MetaResultDetailDialog::TabTrackers));
    QTreeWidget* tree = dlg.filesTree();
    QCOMPARE(tree->topLevelItemCount(), 2);
    tree->sortByColumn(MetaResultDetailDialog::NzbName, Qt::AscendingOrder);
    QCOMPARE(tree->topLevelItem(0)->text(MetaResultDetailDialog::NzbName), QStringLiteral("a.par2"));
    QCOMPARE(tree->topLevelItem(0)->text(MetaResultDetailDialog::NzbType), QStringLiteral("PAR2"));
    QVERIFY(tree->topLevelItem(1)->text(MetaResultDetailDialog::NzbArticles).contains(QStringLiteral("1 missing")));

    const QStringList labels = formLabels(dlg.tabs()->widget(MetaResultDetailDialog::TabGeneral));
    QVERIFY(labels.contains(QStringLiteral("poster@example")));
    QVERIFY(labels.contains(QStringLiteral("alt.binaries.test")));
}

void tst_MetaResultDetailDialog::errorShowsMessage()
{
    MetaResultDetailDialog dlg({.name = QStringLiteral("demo"), .torrent = true});
    dlg.setDetails({{QStringLiteral("error"), QStringLiteral("Only a magnet link")}});
    QCOMPARE(dlg.findChild<QStackedWidget*>()->currentWidget(), dlg.noteLabel());
    QCOMPARE(dlg.noteLabel()->text(), QStringLiteral("Only a magnet link"));
    QVERIFY(!dlg.downloadButton()->isEnabled());
}

void tst_MetaResultDetailDialog::downloadEmits()
{
    MetaResultDetailDialog dlg({.name = QStringLiteral("demo"), .torrent = true});
    dlg.setDetails(torrentDetails());
    QSignalSpy spy(&dlg, &MetaResultDetailDialog::downloadRequested);
    dlg.downloadButton()->click();
    QCOMPARE(spy.count(), 1);
}

void tst_MetaResultDetailDialog::nzbChecksAllByDefault()
{
    // option off: a file row still opens with everything checked
    MetaResultDetailDialog dlg({.name = QStringLiteral("R"), .chooseFiles = true});
    dlg.setDetails(nzbChooseDetails(2));
    QVERIFY(dlg.fileChecks());
    const auto states = checkStates(dlg);
    QCOMPARE(states.size(), 4);
    for (const bool checked : states)
        QVERIFY(checked);
}

void tst_MetaResultDetailDialog::nzbChecksRowFileSetOnly()
{
    MetaResultDetailDialog dlg({.name = QStringLiteral("R"), .chooseFiles = true, .selectedFileOnly = true});
    dlg.setDetails(nzbChooseDetails(1));   // a.part2.rar: its whole set
    const auto states = checkStates(dlg);
    QVERIFY(states.value(0));
    QVERIFY(states.value(1));
    QVERIFY(!states.value(2));
    QVERIFY(states.value(3));   // par2: locked, checked

    // whole-set row (-1): everything
    MetaResultDetailDialog whole({.name = QStringLiteral("R"), .chooseFiles = true, .selectedFileOnly = true});
    whole.setDetails(nzbChooseDetails(-1));
    for (const bool checked : checkStates(whole))
        QVERIFY(checked);
}

void tst_MetaResultDetailDialog::nzbDownloadSendsUnchecked()
{
    MetaResultDetailDialog dlg({.name = QStringLiteral("R"), .chooseFiles = true, .selectedFileOnly = true});
    dlg.setDetails(nzbChooseDetails(2));   // b.mkv only
    QSignalSpy spy(&dlg, &MetaResultDetailDialog::downloadRequested);
    dlg.downloadButton()->click();
    QCOMPARE(spy.count(), 1);
    auto unchecked = spy.at(0).at(0).value<QList<int>>();
    std::ranges::sort(unchecked);
    QCOMPARE(unchecked, (QList<int>{0, 1}));

    // all checked: empty, i.e. the whole NZB
    MetaResultDetailDialog all({.name = QStringLiteral("R"), .chooseFiles = true});
    all.setDetails(nzbChooseDetails(2));
    QSignalSpy allSpy(&all, &MetaResultDetailDialog::downloadRequested);
    all.downloadButton()->click();
    QCOMPARE(allSpy.count(), 1);
    QVERIFY(allSpy.at(0).at(0).value<QList<int>>().isEmpty());
}

void tst_MetaResultDetailDialog::nzbNoneCheckedBlocks()
{
    MetaResultDetailDialog dlg({.name = QStringLiteral("R"), .chooseFiles = true});
    dlg.setDetails(nzbChooseDetails(-1));
    QTreeWidget* tree = dlg.filesTree();
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
        tree->topLevelItem(i)->setCheckState(MetaResultDetailDialog::NzbName, Qt::Unchecked);
    QVERIFY(!dlg.fileChecks()->anyUnlockedChecked());

    QSignalSpy spy(&dlg, &MetaResultDetailDialog::downloadRequested);
    // the warning is modal: answer it as soon as it shows
    QTimer::singleShot(0, [] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->accept();
    });
    dlg.downloadButton()->click();
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(tst_MetaResultDetailDialog)
#include "tst_MetaResultDetailDialog.moc"
