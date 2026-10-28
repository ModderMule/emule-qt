/// @file tst_Ed3kSetsDialog.cpp
/// @brief "ed3k Sets" (src/gui/dialogs/Ed3kSetsDialog) driven with hand-built GetEd3kSets /
/// GetEd3kLive maps, no daemon: the list, the member tree with its boxes, the ticks kept over
/// a refresh, the live list, a paid set (Buy Pass, the pass in the info), a mount's waiting
/// version, a broadcast of ours. EMULE_SCREENSHOT_DIR set: a picture of each tab is saved there.

#include "dialogs/Ed3kSetsDialog.h"

#include <QCborArray>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QTest>
#include <QTreeWidget>

using namespace eMule;

namespace {

QCborMap member(const QString& path, qint64 size, int state, bool skipped = false)
{
    return QCborMap{{QStringLiteral("path"), path},       {QStringLiteral("root"), QString(64, QLatin1Char('a'))},
                    {QStringLiteral("size"), size},       {QStringLiteral("skipped"), skipped},
                    {QStringLiteral("state"), state},     {QStringLiteral("done"), state == 3 ? size : 0}};
}

QCborMap sets(qint64 pending)
{
    const QCborArray members{member(QStringLiteral("Episode 01.mkv"), 734003200, 3),
                             member(QStringLiteral("Episode 02.mkv"), 716177408, 2),
                             member(QStringLiteral("Extras/Making of.mkv"), 209715200, 0, true),
                             member(QStringLiteral("Extras/Poster.jpg"), 524288, 3)};
    const QCborMap one{{QStringLiteral("id"), 1},
                       {QStringLiteral("link"), QStringLiteral("ed3k:set?key=abc&id=def")},
                       {QStringLiteral("title"), QStringLiteral("Season one")},
                       {QStringLiteral("author"), QString(QStringLiteral("884b8857") + QString(56, QLatin1Char('0')))},
                       {QStringLiteral("hidden"), false},
                       {QStringLiteral("followed"), true},
                       {QStringLiteral("policy"), 1},
                       {QStringLiteral("seq"), 2},
                       {QStringLiteral("pendingSeq"), pending},
                       {QStringLiteral("added"), QCborArray{QStringLiteral("Episode 03.mkv")}},
                       {QStringLiteral("dir"), QStringLiteral("/Users/me/Incoming/Season one")},
                       {QStringLiteral("totalSize"), 1660420096},
                       {QStringLiteral("members"), members}};
    const QCborMap two{{QStringLiteral("id"), 2},
                       {QStringLiteral("title"), QStringLiteral("Private photos")},
                       {QStringLiteral("hidden"), true},
                       {QStringLiteral("followed"), true},
                       {QStringLiteral("policy"), 0},
                       {QStringLiteral("seq"), -1},
                       {QStringLiteral("pendingSeq"), -1}};
    return QCborMap{{QStringLiteral("running"), true}, {QStringLiteral("sets"), QCborArray{one, two}}};
}

void shoot(QWidget& w, const QString& name)
{
    const QString dir = qEnvironmentVariable("EMULE_SCREENSHOT_DIR");
    if (!dir.isEmpty())
        w.grab().save(QDir(dir).filePath(name));
}

} // namespace

class tst_Ed3kSetsDialog : public QObject {
    Q_OBJECT

private slots:
    void listAndMemberTree();
    void liveStreams();
    void aPaidSetAndAWaitingMount();
};

void tst_Ed3kSetsDialog::listAndMemberTree()
{
    Ed3kSetsDialog dlg(nullptr);
    dlg.fillSets(sets(-1));
    dlg.show();
    auto* list = dlg.findChild<QTreeWidget*>(QStringLiteral("ed3kSets"));
    auto* tree = dlg.findChild<QTreeWidget*>(QStringLiteral("ed3kMembers"));
    QCOMPARE(list->topLevelItemCount(), 2);
    QCOMPARE(list->topLevelItem(0)->text(0), QStringLiteral("Season one"));
    QCOMPARE(list->topLevelItem(0)->text(1), QStringLiteral("884b8857"));
    QCOMPARE(list->topLevelItem(0)->text(2), QStringLiteral("2"));
    QCOMPARE(list->topLevelItem(1)->text(1), Ed3kSetsDialog::tr("(hidden)"));
    QCOMPARE(list->topLevelItem(0)->text(5), Ed3kSetsDialog::policyText(1));
    // three top items: two files and the folder "Extras" with its two
    QCOMPARE(tree->topLevelItemCount(), 3);
    QTreeWidgetItem* extras = tree->topLevelItem(2);
    QCOMPARE(extras->text(0), QStringLiteral("Extras"));
    QCOMPARE(extras->childCount(), 2);
    QCOMPARE(extras->child(0)->checkState(0), Qt::Unchecked);   // skipped
    QCOMPARE(extras->checkState(0), Qt::PartiallyChecked);
    QCOMPARE(tree->topLevelItem(0)->text(2), Ed3kSetsDialog::memberStateText(3));
    shoot(dlg, QStringLiteral("ed3k-sets.png"));

    // the user's ticks stay over a refresh of the same version; a new version waiting shows
    tree->topLevelItem(1)->setCheckState(0, Qt::Unchecked);
    dlg.fillSets(sets(-1));
    QCOMPARE(tree->topLevelItem(1)->checkState(0), Qt::Unchecked);
    dlg.fillSets(sets(3));
    QCOMPARE(list->topLevelItem(0)->text(2), QStringLiteral("2 → 3"));
    QCOMPARE(tree->topLevelItem(1)->checkState(0), Qt::Checked);   // another version: rebuilt
}

void tst_Ed3kSetsDialog::liveStreams()
{
    Ed3kSetsDialog dlg(nullptr);
    const QCborMap s{{QStringLiteral("id"), QString(32, QLatin1Char('c'))},
                     {QStringLiteral("title"), QStringLiteral("Test TV")},
                     {QStringLiteral("media"), QStringLiteral("video/mp4")},
                     {QStringLiteral("state"), 1},
                     {QStringLiteral("neighbours"), 3},
                     {QStringLiteral("segments"), 42},
                     {QStringLiteral("bytes"), 44040192},
                     {QStringLiteral("viewers"), 1},
                     {QStringLiteral("url"), QStringLiteral("http://live-cc.localhost:1/")}};
    dlg.fillLive(QCborMap{{QStringLiteral("streams"), QCborArray{s}}});
    dlg.findChild<QTabWidget*>()->setCurrentIndex(Ed3kSetsDialog::TabLive);
    dlg.show();
    auto* live = dlg.findChild<QTreeWidget*>(QStringLiteral("ed3kLive"));
    QCOMPARE(live->topLevelItemCount(), 1);
    QCOMPARE(live->topLevelItem(0)->text(0), QStringLiteral("Test TV"));
    QCOMPARE(live->topLevelItem(0)->text(1), Ed3kSetsDialog::liveStateText(1));
    QCOMPARE(live->topLevelItem(0)->text(2), QStringLiteral("3"));
    shoot(dlg, QStringLiteral("ed3k-live.png"));
}

void tst_Ed3kSetsDialog::aPaidSetAndAWaitingMount()
{
    Ed3kSetsDialog dlg(nullptr);
    const QString scope(32, QLatin1Char('5'));
    QCborMap all = sets(-1);
    QCborArray rows = all.value(QStringLiteral("sets")).toArray();
    QCborMap two = rows.at(1).toMap();
    two.insert(QStringLiteral("paidScope"), scope);
    rows[1] = two;
    QCborMap one = rows.at(0).toMap();
    one.insert(QStringLiteral("pendingMounts"), QCborArray{QStringLiteral("Extras")});
    rows[0] = one;
    all.insert(QStringLiteral("sets"), rows);
    dlg.fillSets(all);
    dlg.show();
    auto* list = dlg.findChild<QTreeWidget*>(QStringLiteral("ed3kSets"));
    auto* buy = dlg.findChild<QPushButton*>(QStringLiteral("ed3kBuyPass"));
    QVERIFY(list && buy);
    // the mount's new version: offered, as a version
    QCOMPARE(list->topLevelItem(0)->text(6), QStringLiteral("New version"));
    QVERIFY(buy->isHidden());
    // the paid set: a pass is needed, the price once the offer is known
    QCOMPARE(list->topLevelItem(1)->text(6), Ed3kSetsDialog::paidStateText(0));
    dlg.fillPaid(QCborMap{{QStringLiteral("reports"), false},
                          {QStringLiteral("subs"), QCborArray{QCborMap{{QStringLiteral("scope"), scope},
                                                                       {QStringLiteral("title"), QStringLiteral("Private photos")},
                                                                       {QStringLiteral("state"), 2},
                                                                       {QStringLiteral("until"), QDateTime::currentSecsSinceEpoch() + 86400},
                                                                       {QStringLiteral("renewing"), true},
                                                                       {QStringLiteral("hasOffer"), true},
                                                                       {QStringLiteral("price"), QStringLiteral("4.99 EUR / month")}}}}});
    list->setCurrentItem(list->topLevelItem(1));
    QVERIFY(!buy->isHidden());
    QVERIFY(!buy->isEnabled());   // a pass is held
    const QString info = dlg.findChild<QLabel*>(QStringLiteral("ed3kSetInfo"))->text();
    QVERIFY2(info.contains(QStringLiteral("4.99 EUR / month")), qPrintable(info));
    QVERIFY(info.contains(QStringLiteral("48")));
    auto* reports = dlg.findChild<QCheckBox*>();
    QVERIFY(reports && !reports->isChecked());
    shoot(dlg, QStringLiteral("ed3k-sets-paid.png"));
    // a broadcast of ours in the live list
    const QCborMap b{{QStringLiteral("id"), QString(32, QLatin1Char('d'))}, {QStringLiteral("title"), QStringLiteral("My TV")},
                     {QStringLiteral("state"), 1},  {QStringLiteral("broadcast"), true},
                     {QStringLiteral("link"), QStringLiteral("ed3k:live?key=a&id=b")}, {QStringLiteral("segments"), 3}};
    dlg.fillLive(QCborMap{{QStringLiteral("streams"), QCborArray{b}}});
    auto* live = dlg.findChild<QTreeWidget*>(QStringLiteral("ed3kLive"));
    QCOMPARE(live->topLevelItem(0)->text(1), QStringLiteral("On air"));
}

QTEST_MAIN(tst_Ed3kSetsDialog)
#include "tst_Ed3kSetsDialog.moc"
