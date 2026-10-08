/// @file tst_ListHeaderMenu.cpp
/// @brief AbstractListView header menu (MFC CMuleListCtrl column menu): entries,
///        hide/show with width restore, default-hidden columns and persistence.

#include "controls/AbstractListView.h"

#include <QMenu>
#include <QStandardItemModel>
#include <QTest>

#include <functional>

using namespace eMule;
using namespace Qt::StringLiterals;

class tst_ListHeaderMenu : public QObject {
    Q_OBJECT

private slots:
    void menu_listsEveryColumnButFirst();
    void menu_togglesAndPersists();
    void show_restoresWidthOfColumnHiddenByDefault();
    void defaultHidden_savedLayoutWins();
    void lockedColumns_leftOutAndClearedOnRebind();
    void sortClick_rule();
    void headerClicks_followMfc();

private:
    /// Open the header menu, hand it to @p onMenu, trigger @p pick, then close it.
    static void openMenu(ListTreeView& view, const std::function<void(QMenu*)>& onMenu,
                         const QString& pick = {});
    static QStandardItemModel* makeModel(QObject* parent);
};

void tst_ListHeaderMenu::menu_listsEveryColumnButFirst()
{
    ListTreeView view;
    view.setModel(makeModel(&view));
    view.bindColumns(QStringLiteral("tst_headerMenu_list"), {}, {2});

    QStringList texts;
    QList<bool> checked;
    openMenu(view, [&](QMenu* menu) {
        for (const QAction* a : menu->actions()) {
            texts << a->text();
            checked << a->isChecked();
        }
    });
    QCOMPARE(texts, (QStringList{u"B"_s, u"C"_s, u"D"_s}));
    QCOMPARE(checked, (QList<bool>{true, false, true}));
}

void tst_ListHeaderMenu::menu_togglesAndPersists()
{
    const QString key = QStringLiteral("tst_headerMenu_toggle");
    ListTreeView view;
    view.setModel(makeModel(&view));
    view.bindColumns(key, {100, 80, 90, 70});

    openMenu(view, {}, QStringLiteral("C"));
    QVERIFY(view.header()->isSectionHidden(2));

    // Captured without any resize signal: a fresh bind restores it hidden
    ListTreeView other;
    other.setModel(makeModel(&other));
    other.bindColumns(key);
    QVERIFY(other.header()->isSectionHidden(2));

    openMenu(view, {}, QStringLiteral("C"));
    QVERIFY(!view.header()->isSectionHidden(2));
    QCOMPARE(view.header()->sectionSize(2), 90);
}

void tst_ListHeaderMenu::show_restoresWidthOfColumnHiddenByDefault()
{
    ListTreeView view;
    view.setModel(makeModel(&view));
    view.bindColumns(QStringLiteral("tst_headerMenu_width"), {100, 80, 130, 70}, {2});
    QVERIFY(view.header()->isSectionHidden(2));

    openMenu(view, {}, QStringLiteral("C"));
    QVERIFY(!view.header()->isSectionHidden(2));
    QVERIFY(view.header()->sectionSize(2) >= view.header()->minimumSectionSize());
}

void tst_ListHeaderMenu::defaultHidden_savedLayoutWins()
{
    const QString key = QStringLiteral("tst_headerMenu_saved");
    {
        ListTreeView view;
        view.setModel(makeModel(&view));
        view.bindColumns(key);
        QVERIFY(!view.header()->isSectionHidden(3));
    }
    ListTreeView view;
    view.setModel(makeModel(&view));
    view.bindColumns(key, {}, {3});
    QVERIFY(!view.header()->isSectionHidden(3));
}

void tst_ListHeaderMenu::lockedColumns_leftOutAndClearedOnRebind()
{
    ListTreeView view;
    view.setModel(makeModel(&view));
    view.bindColumns(QStringLiteral("tst_headerMenu_locked"));
    view.setLockedColumns({1, 3});

    QStringList texts;
    openMenu(view, [&](QMenu* menu) {
        for (const QAction* a : menu->actions())
            texts << a->text();
    });
    QCOMPARE(texts, QStringList{u"C"_s});

    view.bindColumns(QStringLiteral("tst_headerMenu_locked2"));
    texts.clear();
    openMenu(view, [&](QMenu* menu) {
        for (const QAction* a : menu->actions())
            texts << a->text();
    });
    QCOMPARE(texts, (QStringList{u"B"_s, u"C"_s, u"D"_s}));
}

void tst_ListHeaderMenu::openMenu(ListTreeView& view, const std::function<void(QMenu*)>& onMenu,
                                  const QString& pick)
{
    emit view.header()->customContextMenuRequested(QPoint(5, 5));
    auto* menu = view.findChild<QMenu*>(QString(), Qt::FindDirectChildrenOnly);
    QVERIFY(menu);
    if (onMenu)
        onMenu(menu);
    for (QAction* a : menu->actions()) {
        if (!pick.isEmpty() && a->text() == pick)
            a->trigger();
    }
    delete menu;
}

QStandardItemModel* tst_ListHeaderMenu::makeModel(QObject* parent)
{
    auto* model = new QStandardItemModel(1, 4, parent);
    model->setHorizontalHeaderLabels({u"A"_s, u"B"_s, u"C"_s, u"D"_s});
    return model;
}

// MFC OnLvnColumnClick (e.g. SharedFilesCtrl.cpp:1088-1134).
void tst_ListHeaderMenu::sortClick_rule()
{
    // a new column takes its own first direction, whatever Qt chose
    QCOMPARE(resolveSortClick(false, Qt::AscendingOrder, false).order, Qt::AscendingOrder);
    QCOMPARE(resolveSortClick(false, Qt::AscendingOrder, true).order, Qt::DescendingOrder);
    QVERIFY(!resolveSortClick(false, Qt::AscendingOrder, true).switchValue);

    // the same column toggles; back at its first direction the other value is due
    QVERIFY(!resolveSortClick(true, Qt::AscendingOrder, true).switchValue);
    QVERIFY(resolveSortClick(true, Qt::DescendingOrder, true).switchValue);
    QVERIFY(resolveSortClick(true, Qt::AscendingOrder, false).switchValue);
    QCOMPARE(resolveSortClick(true, Qt::DescendingOrder, false).order, Qt::DescendingOrder);
}

void tst_ListHeaderMenu::headerClicks_followMfc()
{
    ListTreeView view;
    view.setModel(makeModel(&view));
    view.setSortingEnabled(true);
    view.resize(600, 200);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    QList<bool> applied;
    view.setDescendingFirst({1, 2});
    view.setSortValueColumn(2, [&applied](bool second) { applied << second; });
    QCOMPARE(applied, (QList<bool>{false}));   // told the starting value at once

    auto* hdr = view.header();
    const auto click = [&](int column) {
        const QPoint pos(hdr->sectionViewportPosition(column) + hdr->sectionSize(column) / 2,
                         hdr->height() / 2);
        QTest::mouseClick(hdr->viewport(), Qt::LeftButton, {}, pos);
    };
    const auto sorted = [&] { return qMakePair(hdr->sortIndicatorSection(), hdr->sortIndicatorOrder()); };
    const auto doubleArrow = [&] {
        return hdr->property(SortArrowStyle::kDoubleArrowProperty).toBool();
    };

    click(0);
    QCOMPARE(sorted(), qMakePair(0, Qt::AscendingOrder));
    click(1);                                   // numeric: starts descending
    QCOMPARE(sorted(), qMakePair(1, Qt::DescendingOrder));
    click(1);
    QCOMPARE(sorted(), qMakePair(1, Qt::AscendingOrder));
    click(1);
    QCOMPARE(sorted(), qMakePair(1, Qt::DescendingOrder));
    QCOMPARE(applied.size(), 1);                // no second value on this column

    // the two-value column: desc, asc, then desc again by the other value
    click(2);
    QCOMPARE(sorted(), qMakePair(2, Qt::DescendingOrder));
    QVERIFY(!doubleArrow());
    click(2);
    QCOMPARE(sorted(), qMakePair(2, Qt::AscendingOrder));
    QCOMPARE(applied.size(), 1);
    click(2);
    QCOMPARE(sorted(), qMakePair(2, Qt::DescendingOrder));
    QCOMPARE(applied, (QList<bool>{false, true}));
    QVERIFY(doubleArrow());

    // leaving and coming back keeps the value, and the arrow follows the sort column
    click(0);
    QVERIFY(!doubleArrow());
    click(2);
    QCOMPARE(sorted(), qMakePair(2, Qt::DescendingOrder));
    QCOMPARE(applied.size(), 2);
    QVERIFY(doubleArrow());

    // A sort set from code is nobody's click: it is left as given
    view.sortByColumn(1, Qt::AscendingOrder);
    QCOMPARE(sorted(), qMakePair(1, Qt::AscendingOrder));
}

QTEST_MAIN(tst_ListHeaderMenu)
#include "tst_ListHeaderMenu.moc"
