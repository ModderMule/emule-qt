/// @file tst_OptionsTree.cpp
/// @brief The MFC-style settings tree: number rows show "Label: value", clamp, edit in
/// place and signal changes; check rows too; setters stay quiet.

#include "controls/OptionsTree.h"

#include <QAbstractItemDelegate>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTest>

using eMule::OptionsTree;

class tst_OptionsTree : public QObject {
    Q_OBJECT

private slots:
    void numberRowsShowClampAndEdit();
    void checkRowsSignal();
};

void tst_OptionsTree::numberRowsShowClampAndEdit()
{
    OptionsTree tree;
    QSignalSpy spy(&tree, &OptionsTree::changed);
    auto* group = tree.addGroup(QStringLiteral("Hashing"), {});
    QVERIFY(group->isExpanded());
    auto* ram = tree.addSpin(group, QStringLiteral("RAM"), 0, 100, QStringLiteral("Auto"), QStringLiteral("MiB"));
    QCOMPARE(ram->text(0), QStringLiteral("RAM: Auto"));
    tree.setValue(ram, 40);
    QCOMPARE(tree.value(ram), 40);
    QCOMPARE(ram->text(0), QStringLiteral("RAM: 40 MiB"));
    tree.setValue(ram, 500);
    QCOMPARE(tree.value(ram), 100);
    QCOMPARE(spy.count(), 0);

    // edited in place: a spin box, the value written back, one change
    tree.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tree));
    tree.setCurrentItem(ram);
    tree.editItem(ram);
    auto* spin = tree.findChild<QSpinBox*>();
    QVERIFY(spin);
    QCOMPARE(spin->value(), 100);
    QCOMPARE(spin->maximum(), 100);
    spin->setValue(7);
    QTest::keyClick(spin, Qt::Key_Return);
    QTRY_COMPARE(tree.value(ram), 7);
    QCOMPARE(ram->text(0), QStringLiteral("RAM: 7 MiB"));
    QCOMPARE(spy.count(), 1);
}

void tst_OptionsTree::checkRowsSignal()
{
    OptionsTree tree;
    QSignalSpy spy(&tree, &OptionsTree::changed);
    auto* group = tree.addGroup(QStringLiteral("G"), {});
    auto* box = tree.addCheck(group, QStringLiteral("On"));
    QVERIFY(!tree.isChecked(box));
    tree.setChecked(box, true);
    QVERIFY(tree.isChecked(box));
    QCOMPARE(spy.count(), 0);
    box->setCheckState(0, Qt::Unchecked);   // as a click does
    QCOMPARE(spy.count(), 1);
    QVERIFY(!tree.isChecked(box));
}

QTEST_MAIN(tst_OptionsTree)
#include "tst_OptionsTree.moc"
