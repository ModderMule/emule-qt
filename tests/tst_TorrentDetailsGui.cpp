/// @file tst_TorrentDetailsGui.cpp
/// @brief The Torrents tab's state filter, the Peers/Trackers/Files pane and the
/// Info/Activity/Options inspector, off hand-built rows and GetTorrentDetails maps.

#include "controls/TorrentFilterProxy.h"
#include "controls/TorrentListModel.h"
#include "controls/TorrentListsPane.h"
#include "controls/CheckComboBox.h"
#include "controls/TorrentPieceGrid.h"
#include "controls/TorrentOptionsForm.h"
#include "dialogs/EditTrackersDialog.h"
#include "dialogs/TorrentCreatorDialog.h"
#include "dialogs/TorrentOptionsDialog.h"
#include "dialogs/TorrentDetailsDialog.h"
#include "prefs/Preferences.h"

#include <QAbstractItemView>
#include <QCborArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QTimer>
#include <QPlainTextEdit>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTest>
#include <QTreeWidget>
#include <QUrl>

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

QCborMap file(int index, const QString& path, qint64 size, qint64 done, int priority, bool pad = false,
              double availability = -1.0)
{
    return QCborMap{{QStringLiteral("index"), index}, {QStringLiteral("path"), path},
                    {QStringLiteral("size"), size},   {QStringLiteral("done"), done},
                    {QStringLiteral("priority"), priority}, {QStringLiteral("pad"), pad},
                    {QStringLiteral("availability"), availability}};
}

QCborMap details()
{
    QCborArray files{file(0, QStringLiteral("Show/S01/e1.mkv"), 300, 300, 4, false, 1.0),
                     file(1, QStringLiteral("Show/S01/e2.mkv"), 100, 0, 0, false, 0.25),
                     file(2, QStringLiteral("Show/.pad/0"), 7, 0, 4, true),
                     file(3, QStringLiteral("Show/readme.txt"), 50, 25, 7)};
    QCborArray peers{QCborMap{{QStringLiteral("address"), QStringLiteral("10.1.2.3:6881")},
                              {QStringLiteral("client"), QStringLiteral("Transmission 4.0")},
                              {QStringLiteral("progress"), 0.5},
                              {QStringLiteral("rateDown"), 2048},
                              {QStringLiteral("rateUp"), 0},
                              {QStringLiteral("encrypted"), true},
                              {QStringLiteral("source"), QStringLiteral("dht")},
                              {QStringLiteral("flags"), QStringLiteral("TDEH")},
                              {QStringLiteral("cc"), QStringLiteral("DE")}}};
    QCborArray trackers{
        QCborMap{{QStringLiteral("url"), QStringLiteral("udp://a.example/announce")},
                 {QStringLiteral("tier"), 0}, {QStringLiteral("working"), true},
                 {QStringLiteral("seeds"), 12}, {QStringLiteral("peers"), 3},
                 {QStringLiteral("downloaded"), 99}, {QStringLiteral("nextAnnounce"), 600},
                 {QStringLiteral("cc"), QStringLiteral("FR")}},
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

/// Polls for the next message box and clicks @p button (topLevelWidgets: no focus needed).
void answerNextBox(QMessageBox::StandardButton button)
{
    auto* poll = new QTimer;
    poll->setInterval(10);
    QObject::connect(poll, &QTimer::timeout, poll, [poll, button] {
        for (QWidget* w : QApplication::topLevelWidgets())
            if (auto* box = qobject_cast<QMessageBox*>(w); box && box->isVisible()) {
                box->button(button)->click();
                poll->deleteLater();
                return;
            }
    });
    poll->start();
}

} // namespace

class tst_TorrentDetailsGui : public QObject {
    Q_OBJECT

private slots:
    void torrentCreatorDialog();
    void torrentCreatorPrivateAndDrop();
    void filterBuckets();
    void filterProxyHidesRows();
    void filesTree();
    void filesTreeDropsGoneFolders();
    void filesChecksToPriorities();
    void peersAndFlags();
    void trackersBackupToggle();
    void editTrackersForMany();
    void inspectorInfoAndActivity();
    void inspectorOptions();
    void optionsForMany();
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
    // Availability as qBittorrent: per file, folders size-weighted, N/A without counts
    QCOMPARE(tree->headerItem()->text(TorrentListsPane::FileAvailability), QStringLiteral("Availability"));
    QTreeWidgetItem* e2 = nullptr;
    QTreeWidgetItem* readme = nullptr;
    for (int i = 0; i < s01->childCount(); ++i)
        if (s01->child(i)->text(TorrentListsPane::FileName) == QLatin1String("e2.mkv"))
            e2 = s01->child(i);
    for (int i = 0; i < show->childCount(); ++i)
        if (show->child(i)->text(TorrentListsPane::FileName) == QLatin1String("readme.txt"))
            readme = show->child(i);
    QVERIFY(e2 && readme);
    QCOMPARE(e2->text(TorrentListsPane::FileAvailability), QStringLiteral("25.0%"));
    QCOMPARE(readme->text(TorrentListsPane::FileAvailability), QStringLiteral("N/A"));
    QCOMPARE(s01->text(TorrentListsPane::FileAvailability), QStringLiteral("81.3%"));   // (300 + 25) / 400
    QCOMPARE(show->text(TorrentListsPane::FileAvailability), QStringLiteral("81.3%"));   // readme has none

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

void tst_TorrentDetailsGui::filesTreeDropsGoneFolders()
{
    // nested folders gone at once: the subfolder dies with its parent, never read
    // after (report 46). Alternated so the hash order varies
    TorrentListsPane pane;
    QCborMap flat = details();
    flat.insert(QStringLiteral("files"),
                QCborArray{QCborMap{{QStringLiteral("index"), 0},
                                    {QStringLiteral("path"), QStringLiteral("other.bin")},
                                    {QStringLiteral("size"), 10}}});
    QTreeWidget* tree = pane.filesTree();
    for (int round = 0; round < 20; ++round) {
        pane.applyDetails(details());
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(TorrentListsPane::FileName), QStringLiteral("Show"));
        pane.applyDetails(flat);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(TorrentListsPane::FileName), QStringLiteral("other.bin"));
        QCOMPARE(tree->topLevelItem(0)->childCount(), 0);
    }
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
    // country of the address: tool tip (the flag icon needs resources)
    QCOMPARE(p->toolTip(TorrentListsPane::PeerAddress), QStringLiteral("Germany (DE)"));
    // in place on the next poll
    pane.applyDetails(details());
    QCOMPARE(peers->topLevelItem(0), p);
    // I2P peer: I2P icon instead of a flag, no country in the tool tip
    QCborMap d = details();
    d[QStringLiteral("peers")] = QCborArray{
        QCborMap{{QStringLiteral("address"), QStringLiteral("abcd.b32.i2p")},
                 {QStringLiteral("i2p"), true}}};
    pane.applyDetails(d);
    QTreeWidgetItem* ip = peers->topLevelItem(0);
    QCOMPARE(ip->data(TorrentListsPane::PeerAddress, Qt::UserRole + 40).toString(), QStringLiteral("i2p"));
    QCOMPARE(ip->toolTip(TorrentListsPane::PeerAddress), ip->toolTip(TorrentListsPane::PeerLock));
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
    // tracker country under the url; none known: just the url
    QCOMPARE(backup->toolTip(TorrentListsPane::TrackerUrl), QStringLiteral("http://b.example/announce"));
    QTreeWidgetItem* primary = trackers->topLevelItem(trackers->topLevelItem(0) == backup ? 1 : 0);
    QCOMPARE(primary->toolTip(TorrentListsPane::TrackerUrl),
             QStringLiteral("udp://a.example/announce\nFrance (FR)"));
    QVERIFY(!backup->isHidden());
    auto* toggle = pane.findChild<QCheckBox*>();
    QVERIFY(toggle);
    toggle->setChecked(false);
    QVERIFY(backup->isHidden());
}

void tst_TorrentDetailsGui::editTrackersForMany()
{
    using D = EditTrackersDialog;
    const QString a = QStringLiteral("udp://a/announce");
    const QString b = QStringLiteral("udp://b/announce");
    const QString c = QStringLiteral("udp://c/announce");
    const QString d = QStringLiteral("udp://d/announce");
    QCOMPARE(D::commonTrackers({{a, b, c}, {c, a}, {a, c, d}}), (QStringList{a, c}));
    QVERIFY(D::commonTrackers({}).isEmpty());

    // box held a, c: c removed, d added; b (only some) untouched
    const D::Edit e = D::editFor({a, c}, {a, d}, false);
    QCOMPARE(e.add, QStringList{d});
    QCOMPARE(e.remove, QStringList{c});
    QVERIFY(!e.exact);
    QVERIFY(D::editFor({a, c}, {a, c}, false).isEmpty());
    const D::Edit x = D::editFor({a, c}, {a, c}, true);
    QVERIFY(x.exact && !x.isEmpty());
    QCOMPARE(x.add, (QStringList{a, c}));

    // the box: common ones; "remove others" only when some differ
    D one({{a, b}});
    QCOMPARE(one.findChild<QPlainTextEdit*>()->toPlainText(), a + u'\n' + b);
    QVERIFY(one.findChild<QCheckBox*>()->isHidden());
    D many({{a, b}, {a, c}});
    QCOMPARE(many.findChild<QPlainTextEdit*>()->toPlainText(), a);
    QVERIFY(!many.findChild<QCheckBox*>()->isHidden());
    many.findChild<QPlainTextEdit*>()->setPlainText(a + QStringLiteral("\n ") + d + QStringLiteral("\n\n") + d);
    QCOMPARE(many.edit().add, QStringList{d});
    QVERIFY(many.edit().remove.isEmpty());
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
    QCOMPARE(dialog.tabs()->count(), 4);   // Info, Activity, Options, Archive
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

void tst_TorrentDetailsGui::optionsForMany()
{
    const QCborMap a{{QStringLiteral("downloadLimitKb"), 0}, {QStringLiteral("uploadLimitKb"), 50},
                     {QStringLiteral("maxPeers"), 30}, {QStringLiteral("ratioMode"), 2},
                     {QStringLiteral("ratioPercent"), 150}, {QStringLiteral("seedTimeMode"), 0},
                     {QStringLiteral("seedTimeMinutes"), 60}};
    QCborMap b = a;
    b.insert(QStringLiteral("uploadLimitKb"), 80);
    b.insert(QStringLiteral("ratioPercent"), 200);
    TorrentOptionsDialog dlg({a, b});
    TorrentOptionsForm* form = dlg.form();
    QVERIFY(!form->varies(TorrentOptionsForm::FieldDown));
    QVERIFY(form->varies(TorrentOptionsForm::FieldUp));
    QVERIFY(form->varies(TorrentOptionsForm::FieldRatio));
    QVERIFY(!form->varies(TorrentOptionsForm::FieldMaxPeers));
    // nothing touched: nothing sent
    QVERIFY(dlg.changedOptions().isEmpty());

    // one setting changed: only its keys go out, for every torrent
    QSpinBox* maxPeers = nullptr;
    for (QSpinBox* s : dlg.findChildren<QSpinBox*>())
        if (s->specialValueText() == QLatin1String("Default"))
            maxPeers = s;
    QVERIFY(maxPeers);
    maxPeers->setValue(70);
    const QCborMap e = dlg.changedOptions();
    QCOMPARE(e.size(), 1);
    QCOMPARE(e.value(QStringLiteral("maxPeers")).toInteger(), 70);

    // the ratio pair goes together
    auto* ratioMode = dlg.findChildren<QComboBox*>().first();
    ratioMode->setCurrentIndex(ratioMode->findData(1));
    QCOMPARE(dlg.changedOptions().size(), 3);
    QCOMPARE(dlg.changedOptions().value(QStringLiteral("ratioMode")).toInteger(), 1);
    QVERIFY(dlg.changedOptions().contains(QStringLiteral("ratioPercent")));
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

// #191: the fields reach CreateTorrent's map; a folder subset shows its note
void tst_TorrentDetailsGui::torrentCreatorPrivateAndDrop()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString a = dir.filePath(QStringLiteral("a.bin"));
    const QString b = dir.filePath(QStringLiteral("b.bin"));
    for (const QString& p : {a, b}) {
        QFile f(p);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    TorrentCreatorDialog dlg(nullptr, QStringLiteral("/data/set"), {});
    QLineEdit* path = nullptr;
    for (auto* e : dlg.findChildren<QLineEdit*>())
        if (!e->placeholderText().isEmpty() && e->text() == QDir::toNativeSeparators(QStringLiteral("/data/set")))
            path = e;
    QVERIFY(path);
    QCheckBox* priv = nullptr;
    for (auto* c : dlg.findChildren<QCheckBox*>())
        if (c->text().startsWith(QStringLiteral("Private")))
            priv = c;
    QVERIFY(priv);
    auto* trackers = dlg.findChild<CheckComboBox*>();
    QVERIFY(trackers);

    // private: remembered picks stay but are off
    priv->setChecked(false);
    trackers->setCheckedValues({trackers->itemData(0).toString()});
    QVERIFY(trackers->isEnabled());
    priv->setChecked(true);
    QVERIFY(!trackers->isEnabled());
    QCOMPARE(trackers->checkedValues().size(), 1);
    priv->setChecked(false);
    QVERIFY(trackers->isEnabled());
    trackers->setCheckedValues({});

    const auto dropOn = [&](const QList<QUrl>& urls) {
        QMimeData mime;
        mime.setUrls(urls);
        QDropEvent ev(QPointF(1, 1), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        // sendEvent routes a drop only during a real drag
        return static_cast<QObject&>(dlg).eventFilter(path, &ev) && ev.isAccepted();
    };
    QVERIFY(dropOn({QUrl::fromLocalFile(a)}));
    QCOMPARE(QDir::fromNativeSeparators(path->text()), a);
    QVERIFY(dropOn({QUrl::fromLocalFile(a), QUrl::fromLocalFile(b)}));
    QCOMPARE(QFileInfo(QDir::fromNativeSeparators(path->text())).canonicalFilePath(), QFileInfo(dir.path()).canonicalFilePath());
    // a web link is no source
    const QString before = path->text();
    dropOn({QUrl(QStringLiteral("https://example.com/a.bin"))});
    QCOMPARE(path->text(), before);
}

void tst_TorrentDetailsGui::torrentCreatorDialog()
{
    TorrentCreatorDialog dlg(nullptr, QStringLiteral("/data/set"), {QStringLiteral("a.bin"), QStringLiteral("b.bin")});
    auto* format = dlg.findChild<QComboBox*>();
    QVERIFY(format);
    auto combos = dlg.findChildren<QComboBox*>();
    // #212: public trackers, a multi-select: none checked by default
    const auto checks = dlg.findChildren<CheckComboBox*>();
    QCOMPARE(checks.size(), 1);
    QVERIFY(checks.at(0)->count() >= 10);
    QVERIFY(checks.at(0)->checkedValues().isEmpty());
    checks.at(0)->setCheckedValues({checks.at(0)->itemData(0).toString(), checks.at(0)->itemData(2).toString()});
    QCOMPARE(checks.at(0)->checkedValues().size(), 2);
    checks.at(0)->setCheckedValues({});
    // a click in the open list toggles the item and keeps the list open
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    checks.at(0)->showPopup();
    QAbstractItemView* view = checks.at(0)->view();
    QTRY_VERIFY(view->isVisible());
    const QPoint at = view->visualRect(view->model()->index(1, 0)).center();
    QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, at);
    QCOMPARE(checks.at(0)->checkedValues(), QStringList{checks.at(0)->itemData(1).toString()});
    QVERIFY(view->isVisible());
    // Del removes the highlighted entry (and its check) from the list
    const int before = checks.at(0)->count();
    const QString gone = checks.at(0)->itemData(1).toString();
    QSignalSpy removed(checks.at(0), &CheckComboBox::itemRemoved);
    view->setCurrentIndex(view->model()->index(1, 0));
    QTest::keyClick(view, Qt::Key_Delete);
    QCOMPARE(removed.size(), 1);
    QCOMPARE(removed.at(0).at(0).toString(), gone);
    QCOMPARE(checks.at(0)->count(), before - 1);
    QVERIFY(!checks.at(0)->values().contains(gone));
    QVERIFY(checks.at(0)->checkedValues().isEmpty());
    QTest::keyClick(view, Qt::Key_Backspace);
    QCOMPARE(checks.at(0)->count(), before - 2);
    // last row resets the list; Del leaves it alone
    const QModelIndex resetRow = view->model()->index(view->model()->rowCount() - 1, 0);
    view->setCurrentIndex(resetRow);
    QTest::keyClick(view, Qt::Key_Delete);
    QCOMPARE(checks.at(0)->count(), before - 2);
    // asks first: No keeps the list
    answerNextBox(QMessageBox::No);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, view->visualRect(resetRow).center());
    QCOMPARE(checks.at(0)->count(), before - 2);
    checks.at(0)->showPopup();
    QTRY_VERIFY(view->isVisible());
    answerNextBox(QMessageBox::Yes);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, view->visualRect(resetRow).center());
    QCOMPARE(checks.at(0)->values(), Preferences::defaultBtPublicTrackers());
    QCOMPARE(thePrefs.btPublicTrackers(), Preferences::defaultBtPublicTrackers());
    QVERIFY(!checks.at(0)->values().contains(QStringLiteral("Reset to defaults")));
    checks.at(0)->hidePopup();
    checks.at(0)->setCheckedValues({});
    combos.removeAll(checks.at(0));
    QCOMPARE(combos.size(), 2);
    QCOMPARE(combos.at(0)->currentData().toInt(), 2);   // hybrid by default
    QCOMPARE(combos.at(1)->count(), 15);                 // Auto + 16 KiB..128 MiB
    QCOMPARE(combos.at(1)->itemData(14).toInt(), 128 * 1024 * 1024);
    const auto labels = dlg.findChildren<QLabel*>();
    QVERIFY(std::ranges::any_of(labels, [](const QLabel* l) { return l->text().startsWith(QStringLiteral("2 selected file")); }));
    auto edits = dlg.findChildren<QPlainTextEdit*>();
    QCOMPARE(edits.size(), 3);
    if (const QString shot = qEnvironmentVariable("EMULE_TEST_SHOT"); !shot.isEmpty()) {
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        dlg.grab().save(shot);
    }
}

// a native NSAlert runs its own modal loop the tests cannot close
int main(int argc, char** argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);
    tst_TorrentDetailsGui tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}
#include "tst_TorrentDetailsGui.moc"
