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

QTEST_MAIN(tst_ListHeaderMenu)
#include "tst_ListHeaderMenu.moc"
