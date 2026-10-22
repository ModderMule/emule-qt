/// @file tst_TorrentDetailsGui.cpp
/// @brief The Torrents tab's state filter, the Peers/Trackers/Files pane and the
/// Info/Activity/Options inspector, off hand-built rows and GetTorrentDetails maps.

#include "controls/TorrentFilterProxy.h"
#include "controls/TorrentListModel.h"
#include "controls/TorrentListsPane.h"
#include "controls/TorrentPieceGrid.h"
#include "dialogs/TorrentDetailsDialog.h"

#include <QCborArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QTest>
#include <QTreeWidget>

using namespace eMule;

namespace {

TorrentRow makeRow(const QString& id, TorrentState state)
{
    TorrentRow r;
    r.id = id;
    r.name = id;
    r.state = state;
    r.size = 1000;
    r.done = state == TorrentState::Seeding || state == TorrentState::Finished ? 1000 : 10;
    r.paused = state == TorrentState::Paused;
    return r;
}

QCborMap file(int index, const QString& path, qint64 size, qint64 done, int priority, bool pad = false)
{
    return QCborMap{{QStringLiteral("index"), index}, {QStringLiteral("path"), path},
                    {QStringLiteral("size"), size},   {QStringLiteral("done"), done},
                    {QStringLiteral("priority"), priority}, {QStringLiteral("pad"), pad}};
}

QCborMap details()
{
    QCborArray files{file(0, QStringLiteral("Show/S01/e1.mkv"), 300, 300, 4),
                     file(1, QStringLiteral("Show/S01/e2.mkv"), 100, 0, 0),
                     file(2, QStringLiteral("Show/.pad/0"), 7, 0, 4, true),
                     file(3, QStringLiteral("Show/readme.txt"), 50, 25, 7)};
    QCborArray peers{QCborMap{{QStringLiteral("address"), QStringLiteral("10.1.2.3:6881")},
                              {QStringLiteral("client"), QStringLiteral("Transmission 4.0")},
                              {QStringLiteral("progress"), 0.5},
                              {QStringLiteral("rateDown"), 2048},
                              {QStringLiteral("rateUp"), 0},
                              {QStringLiteral("encrypted"), true},
                              {QStringLiteral("source"), QStringLiteral("dht")},
                              {QStringLiteral("flags"), QStringLiteral("TDEH")}}};
    QCborArray trackers{
        QCborMap{{QStringLiteral("url"), QStringLiteral("udp://a.example/announce")},
                 {QStringLiteral("tier"), 0}, {QStringLiteral("working"), true},
                 {QStringLiteral("seeds"), 12}, {QStringLiteral("peers"), 3},
                 {QStringLiteral("downloaded"), 99}, {QStringLiteral("nextAnnounce"), 600}},
        QCborMap{{QStringLiteral("url"), QStringLiteral("http://b.example/announce")},
                 {QStringLiteral("tier"), 1}, {QStringLiteral("hasError"), true},
                 {QStringLiteral("message"), QStringLiteral("timed out")}}};
    return QCborMap{{QStringLiteral("files"), files},
                    {QStringLiteral("peers"), peers},
                    {QStringLiteral("trackers"), trackers},
                    {QStringLiteral("availabilityMap"), QByteArray("\x01\x02\x00", 3)},
                    {QStringLiteral("options"),
                     QCborMap{{QStringLiteral("downloadLimitKb"), 0},
                              {QStringLiteral("uploadLimitKb"), 50},
                              {QStringLiteral("maxPeers"), 30},
                              {QStringLiteral("ratioMode"), 2},
                              {QStringLiteral("ratioPercent"), 150},
                              {QStringLiteral("seedTimeMode"), 1},
                              {QStringLiteral("seedTimeMinutes"), 90}}}};
}

bool anyLabel(const QWidget* root, const QString& text)
{
    for (const QLabel* l : root->findChildren<QLabel*>())
        if (l->text() == text)
            return true;
    return false;
}

} // namespace

class tst_TorrentDetailsGui : public QObject {
    Q_OBJECT

private slots:
    void filterBuckets();
    void filterProxyHidesRows();
    void filesTree();
    void filesChecksToPriorities();
    void peersAndFlags();
    void trackersBackupToggle();
    void inspectorInfoAndActivity();
    void inspectorOptions();
    void pieceGridColours();
};

void tst_TorrentDetailsGui::filterBuckets()
{
    using F = TorrentFilterProxy;
    const TorrentRow dl = makeRow(QStringLiteral("a"), TorrentState::Downloading);
    const TorrentRow seed = makeRow(QStringLiteral("b"), TorrentState::Seeding);
    const TorrentRow paused = makeRow(QStringLiteral("c"), TorrentState::Paused);
    TorrentRow broken = makeRow(QStringLiteral("d"), TorrentState::Error);
    broken.error = QStringLiteral("disk full");
    const TorrentRow queued = makeRow(QStringLiteral("e"), TorrentState::Queued);
    TorrentRow busySeed = seed;
    busySeed.rateUp = 100;

    QVERIFY(F::matches(dl, F::Downloading));
    QVERIFY(F::matches(queued, F::Downloading));
    QVERIFY(!F::matches(seed, F::Downloading));
    QVERIFY(F::matches(seed, F::Seeding));
    QVERIFY(!F::matches(paused, F::Seeding));
    QVERIFY(F::matches(paused, F::Paused));
    QVERIFY(F::matches(broken, F::Error));
    QVERIFY(!F::matches(seed, F::Active));
    QVERIFY(F::matches(busySeed, F::Active));
    for (const TorrentRow* r : std::initializer_list<const TorrentRow*>{&dl, &seed, &paused, &broken, &queued})
        QVERIFY(F::matches(*r, F::All));
}

void tst_TorrentDetailsGui::filterProxyHidesRows()
{
    TorrentListModel model;
    model.setRows({makeRow(QStringLiteral("a"), TorrentState::Downloading),
                   makeRow(QStringLiteral("b"), TorrentState::Seeding),
                   makeRow(QStringLiteral("c"), TorrentState::Seeding)});
    TorrentFilterProxy proxy;
    proxy.setSourceModel(&model);
    QCOMPARE(proxy.rowCount(), 3);
    proxy.setFilter(TorrentFilterProxy::Seeding);
    QCOMPARE(proxy.rowCount(), 2);
    proxy.setFilter(TorrentFilterProxy::Downloading);
    QCOMPARE(proxy.rowCount(), 1);
    // a row that starts seeding leaves the Downloading view
    TorrentRow done = makeRow(QStringLiteral("a"), TorrentState::Seeding);
    model.upsertRow(done);
    QCOMPARE(proxy.rowCount(), 0);
}

void tst_TorrentDetailsGui::filesTree()
{
    TorrentListsPane pane;
    pane.applyDetails(details());
    QTreeWidget* tree = pane.filesTree();
    // one top folder; the pad file is not shown
    QCOMPARE(tree->topLevelItemCount(), 1);
    QTreeWidgetItem* show = tree->topLevelItem(0);
    QCOMPARE(show->text(TorrentListsPane::FileName), QStringLiteral("Show"));
    QCOMPARE(show->childCount(), 2);   // S01 + readme.txt
    QCOMPARE(show->data(TorrentListsPane::FileSize, Qt::UserRole + 100).toLongLong(), 450);
    QTreeWidgetItem* s01 = nullptr;
    for (int i = 0; i < show->childCount(); ++i)
        if (show->child(i)->text(TorrentListsPane::FileName) == QLatin1String("S01"))
            s01 = show->child(i);
    QVERIFY(s01);
    QCOMPARE(s01->childCount(), 2);
    QCOMPARE(s01->data(TorrentListsPane::FileSize, Qt::UserRole + 100).toLongLong(), 400);
    // one wanted, one skipped: the folder is partly checked
    QCOMPARE(s01->checkState(TorrentListsPane::FileDownload), Qt::PartiallyChecked);
    QCOMPARE(pane.tabs()->tabText(TorrentListsPane::TabFiles), QStringLiteral("Files (3)"));

    // a second poll updates in place: same items
    QTreeWidgetItem* before = s01->child(0);
    pane.applyDetails(details());
    QCOMPARE(tree->topLevelItem(0)->childCount(), 2);
    bool same = false;
    for (int i = 0; i < tree->topLevelItem(0)->childCount(); ++i)
        for (int j = 0; j < tree->topLevelItem(0)->child(i)->childCount(); ++j)
            same = same || tree->topLevelItem(0)->child(i)->child(j) == before;
    QVERIFY(same);
}

void tst_TorrentDetailsGui::filesChecksToPriorities()
{
    TorrentListsPane pane;
    pane.applyDetails(details());
    // as reported: 4, 0, pad 4, 7
    QCOMPARE(pane.prioritiesFromChecks(), (QList<int>{4, 0, 4, 7}));
    QTreeWidgetItem* show = pane.filesTree()->topLevelItem(0);
    // unchecking the whole folder skips every file in it, the pad keeps its value
    show->setCheckState(TorrentListsPane::FileDownload, Qt::Unchecked);
    QCOMPARE(pane.prioritiesFromChecks(), (QList<int>{0, 0, 4, 0}));
    // checking it again: skipped files come back at Normal, others keep theirs
    show->setCheckState(TorrentListsPane::FileDownload, Qt::Checked);
    QCOMPARE(pane.prioritiesFromChecks(), (QList<int>{4, 4, 4, 7}));
}

void tst_TorrentDetailsGui::peersAndFlags()
{
    TorrentListsPane pane;
    pane.applyDetails(details());
    QTreeWidget* peers = pane.peersTree();
    QCOMPARE(peers->topLevelItemCount(), 1);
    QTreeWidgetItem* p = peers->topLevelItem(0);
    QCOMPARE(p->text(TorrentListsPane::PeerStatus), QStringLiteral("TDEH"));
    QCOMPARE(p->text(TorrentListsPane::PeerUp), QString());   // zero rate stays blank
    QVERIFY(!p->text(TorrentListsPane::PeerDown).isEmpty());
    // the icon needs the app's resources; the sort key and tooltip do not
    QCOMPARE(p->data(TorrentListsPane::PeerLock, Qt::UserRole + 100).toInt(), 1);
    QVERIFY(!p->toolTip(TorrentListsPane::PeerLock).isEmpty());
    const QStringList tip = p->toolTip(TorrentListsPane::PeerStatus).split(QLatin1Char('\n'));
    QCOMPARE(tip.size(), 4);
    QVERIFY(tip.at(0).startsWith(QStringLiteral("T: ")));
    QVERIFY(tip.at(3).startsWith(QStringLiteral("H: ")));
    QCOMPARE(torrentPeerFlagsTooltip(QStringLiteral("Z")), QString());
    // in place on the next poll
    pane.applyDetails(details());
    QCOMPARE(peers->topLevelItem(0), p);
}

void tst_TorrentDetailsGui::trackersBackupToggle()
{
    TorrentListsPane pane;
    pane.applyDetails(details());
    QTreeWidget* trackers = pane.trackersTree();
    QCOMPARE(trackers->topLevelItemCount(), 2);
    QTreeWidgetItem* backup = nullptr;
    for (int i = 0; i < 2; ++i)
        if (trackers->topLevelItem(i)->text(TorrentListsPane::TrackerUrl).startsWith(QStringLiteral("http://b")))
            backup = trackers->topLevelItem(i);
    QVERIFY(backup);
    QVERIFY(backup->text(TorrentListsPane::TrackerStatus).contains(QStringLiteral("timed out")));
    QVERIFY(!backup->isHidden());
    auto* toggle = pane.findChild<QCheckBox*>();
    QVERIFY(toggle);
    toggle->setChecked(false);
    QVERIFY(backup->isHidden());
}

void tst_TorrentDetailsGui::inspectorInfoAndActivity()
{
    TorrentListModel model;
    TorrentRow r = makeRow(QStringLiteral("bt:v1:AA"), TorrentState::Downloading);
    r.name = QStringLiteral("Tampopo");
    r.creator = QStringLiteral("mktorrent 1.1");
    r.createdAt = 0;
    r.numFiles = 6;
    r.hasMetadata = true;
    r.totalSize = 2000;
    r.haveBytes = 500;
    r.pieces = 8;
    r.pieceLength = 256;
    r.hashV1 = QStringLiteral("aabb");
    r.downloaded = 100;
    r.uploaded = 50;
    r.pieceMap = QByteArray("\xff\x00", 2);
    model.setRows({r});

    TorrentDetailsDialog dialog(nullptr, &model, r.id);
    QCOMPARE(dialog.torrentId(), r.id);
    QCOMPARE(dialog.tabs()->count(), 3);
    QVERIFY(anyLabel(&dialog, QStringLiteral("Tampopo")));
    QVERIFY(anyLabel(&dialog, QStringLiteral("Created by mktorrent 1.1")));
    QVERIFY(anyLabel(&dialog, QStringLiteral("25.0% (1.0% selected)")));
    QVERIFY(anyLabel(&dialog, QStringLiteral("0.50")));   // ratio
    QVERIFY(anyLabel(&dialog, QStringLiteral("aabb")));

    dialog.setDetails(details());
    TorrentPieceGrid* grid = dialog.pieceGrid();
    QCOMPARE(grid->mode(), TorrentPieceGrid::Mode::Progress);
    QList<QToolButton*> buttons = dialog.findChildren<QToolButton*>();
    QToolButton* available = nullptr;
    for (QToolButton* b : buttons)
        if (b->text() == QLatin1String("Available"))
            available = b;
    QVERIFY(available);
    available->click();
    QCOMPARE(grid->mode(), TorrentPieceGrid::Mode::Available);
}

void tst_TorrentDetailsGui::inspectorOptions()
{
    TorrentListModel model;
    model.setRows({makeRow(QStringLiteral("bt:v1:BB"), TorrentState::Seeding)});
    TorrentDetailsDialog dialog(nullptr, &model, QStringLiteral("bt:v1:BB"));
    dialog.setDetails(details());
    const QCborMap ui = dialog.optionsFromUi();
    QCOMPARE(ui.value(QStringLiteral("uploadLimitKb")).toInteger(), 50);
    QCOMPARE(ui.value(QStringLiteral("downloadLimitKb")).toInteger(), 0);
    QCOMPARE(ui.value(QStringLiteral("maxPeers")).toInteger(), 30);
    QCOMPARE(ui.value(QStringLiteral("ratioMode")).toInteger(), 2);
    QCOMPARE(ui.value(QStringLiteral("ratioPercent")).toInteger(), 150);
    QCOMPARE(ui.value(QStringLiteral("seedTimeMode")).toInteger(), 1);

    // an edit goes out after a short pause, once
    QSignalSpy sent(&dialog, &TorrentDetailsDialog::optionsSent);
    QSpinBox* maxPeers = nullptr;
    for (QSpinBox* s : dialog.findChildren<QSpinBox*>())
        if (s->specialValueText() == QLatin1String("Default"))
            maxPeers = s;
    QVERIFY(maxPeers);
    maxPeers->setValue(40);
    maxPeers->setValue(45);
    QVERIFY(sent.wait(3000));
    QCOMPARE(sent.count(), 1);
    QCOMPARE(sent.first().first().value<QCborMap>().value(QStringLiteral("maxPeers")).toInteger(), 45);
}

void tst_TorrentDetailsGui::pieceGridColours()
{
    QCOMPARE(TorrentPieceGrid::gridSide(0), 0);
    QCOMPARE(TorrentPieceGrid::gridSide(1), 1);
    QCOMPARE(TorrentPieceGrid::gridSide(256), 16);
    QCOMPARE(TorrentPieceGrid::gridSide(257), 17);
    TorrentPieceGrid grid;
    grid.setMode(TorrentPieceGrid::Mode::Available);
    QCOMPARE(grid.cellColor(0), QColor(220, 60, 60));   // nobody has it
    QVERIFY(grid.cellColor(1) != grid.cellColor(10));
    QCOMPARE(grid.cellColor(10), grid.cellColor(200));    // plenty is plenty
    grid.setMode(TorrentPieceGrid::Mode::Progress);
    QCOMPARE(grid.cellColor(0), grid.palette().color(QPalette::Active, QPalette::Base));
}

QTEST_MAIN(tst_TorrentDetailsGui)
#include "tst_TorrentDetailsGui.moc"
