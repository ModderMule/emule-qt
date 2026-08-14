/// @file tst_ViewNavigation.cpp
/// @brief Prev/Next walking over a QTreeView — eMule::ViewNav.
///
/// The walker is what makes the detail dialogs' ▲/▼ buttons step through a list,
/// so the properties it must hold are the ones MFC's CListCtrlItemWalk holds:
/// visual order (which is the *sorted* order, not the model's), no wrap-around at
/// either end, row filtering that lets download rows and source rows be walked
/// separately, and a real selection move as the side effect.

#include "utils/ViewNavigation.h"

#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTest>
#include <QTreeView>

using namespace eMule;

namespace {

/// A tree shaped like the Transfers download list: `parents` top-level rows, each
/// with `childrenPer` source children. Item text is "P<i>" / "P<i>C<j>".
QStandardItemModel* makeTree(int parents, int childrenPer, QObject* owner)
{
    auto* model = new QStandardItemModel(owner);
    for (int p = 0; p < parents; ++p) {
        auto* parent = new QStandardItem(QStringLiteral("P%1").arg(p));
        for (int c = 0; c < childrenPer; ++c)
            parent->appendRow(new QStandardItem(QStringLiteral("P%1C%2").arg(p).arg(c)));
        model->appendRow(parent);
    }
    return model;
}

QString textOf(const QModelIndex& index)
{
    return index.isValid() ? index.data(Qt::DisplayRole).toString() : QString{};
}

} // namespace

class TestViewNavigation : public QObject
{
    Q_OBJECT

private slots:
    void toSourceUnwrapsProxyChain_data();
    void toSourceUnwrapsProxyChain();
    void fromSourceRoundTrips();
    void fromSourceRejectsFilteredRow();
    void stepsFlatListInBothDirections();
    void doesNotWrapAtEitherEnd();
    void topLevelFilterSkipsExpandedChildren();
    void childFilterCrossesIntoNextParent();
    void childFilterStopsAtLastChildOverall();
    void collapsedChildrenAreNotVisited();
    void followsProxySortOrderNotSourceOrder();
    void stepMovesSelectionAndCurrent();
    void zeroDeltaAndInvalidOriginAreNoOps();
};

// ---------------------------------------------------------------------------
// Proxy-chain mapping
// ---------------------------------------------------------------------------

void TestViewNavigation::toSourceUnwrapsProxyChain_data()
{
    QTest::addColumn<int>("proxyCount");
    QTest::newRow("no proxy")  << 0;
    QTest::newRow("one proxy") << 1;
    QTest::newRow("two proxies — the Transfers chain") << 2;
}

void TestViewNavigation::toSourceUnwrapsProxyChain()
{
    QFETCH(int, proxyCount);

    QObject owner;
    auto* model = makeTree(3, 2, &owner);

    QTreeView view;
    QAbstractItemModel* top = model;
    for (int i = 0; i < proxyCount; ++i) {
        auto* proxy = new QSortFilterProxyModel(&owner);
        proxy->setSourceModel(top);
        top = proxy;
    }
    view.setModel(top);
    view.expandAll();

    // A top-level row and a child row must both survive the round trip.
    const QModelIndex srcParent = model->index(1, 0);
    const QModelIndex srcChild  = model->index(0, 0, srcParent);

    for (const QModelIndex& src : {srcParent, srcChild}) {
        const QModelIndex viewIdx = ViewNav::fromSource(&view, src);
        QVERIFY(viewIdx.isValid());
        QCOMPARE(ViewNav::toSource(viewIdx), src);
    }
}

void TestViewNavigation::fromSourceRoundTrips()
{
    QObject owner;
    auto* model = makeTree(2, 2, &owner);
    auto* proxy = new QSortFilterProxyModel(&owner);
    proxy->setSourceModel(model);

    QTreeView view;
    view.setModel(proxy);

    const QModelIndex src = model->index(1, 0);
    QCOMPARE(textOf(ViewNav::fromSource(&view, src)), QStringLiteral("P1"));
    QVERIFY(!ViewNav::fromSource(&view, QModelIndex{}).isValid());
    QVERIFY(!ViewNav::fromSource(nullptr, src).isValid());
}

void TestViewNavigation::fromSourceRejectsFilteredRow()
{
    QObject owner;
    auto* model = makeTree(3, 0, &owner);
    auto* proxy = new QSortFilterProxyModel(&owner);
    proxy->setSourceModel(model);
    proxy->setFilterFixedString(QStringLiteral("P1"));   // hides P0 and P2

    QTreeView view;
    view.setModel(proxy);

    QVERIFY(ViewNav::fromSource(&view, model->index(1, 0)).isValid());
    QVERIFY(!ViewNav::fromSource(&view, model->index(0, 0)).isValid());
}

// ---------------------------------------------------------------------------
// Stepping
// ---------------------------------------------------------------------------

void TestViewNavigation::stepsFlatListInBothDirections()
{
    QObject owner;
    auto* model = makeTree(3, 0, &owner);
    QTreeView view;
    view.setModel(model);

    const QModelIndex middle = model->index(1, 0);
    QCOMPARE(textOf(ViewNav::peekStep(&view, middle, +1)), QStringLiteral("P2"));
    QCOMPARE(textOf(ViewNav::peekStep(&view, middle, -1)), QStringLiteral("P0"));
}

void TestViewNavigation::doesNotWrapAtEitherEnd()
{
    QObject owner;
    auto* model = makeTree(3, 0, &owner);
    QTreeView view;
    view.setModel(model);

    // MFC beeps here rather than wrapping (ListViewWalkerPropertySheet.cpp:191).
    QVERIFY(!ViewNav::peekStep(&view, model->index(0, 0), -1).isValid());
    QVERIFY(!ViewNav::peekStep(&view, model->index(2, 0), +1).isValid());
}

void TestViewNavigation::topLevelFilterSkipsExpandedChildren()
{
    QObject owner;
    auto* model = makeTree(3, 2, &owner);
    QTreeView view;
    view.setModel(model);
    view.expandAll();

    // Visually P0, P0C0, P0C1, P1, ... — the file walk must jump the sources.
    const QModelIndex from = model->index(0, 0);
    QCOMPARE(textOf(ViewNav::peekStep(&view, from, +1, &ViewNav::isTopLevel)),
             QStringLiteral("P1"));

    const QModelIndex last = model->index(2, 0);
    QCOMPARE(textOf(ViewNav::peekStep(&view, last, -1, &ViewNav::isTopLevel)),
             QStringLiteral("P1"));
}

void TestViewNavigation::childFilterCrossesIntoNextParent()
{
    QObject owner;
    auto* model = makeTree(2, 2, &owner);
    QTreeView view;
    view.setModel(model);
    view.expandAll();

    // MFC's AVAILABLE_SOURCE walk runs over the flat list, so the last source of
    // one download continues into the first source of the next.
    const QModelIndex lastChildOfP0 = model->index(1, 0, model->index(0, 0));
    QCOMPARE(textOf(ViewNav::peekStep(&view, lastChildOfP0, +1, &ViewNav::isChild)),
             QStringLiteral("P1C0"));

    // ...and back again, skipping the P1 file row in between.
    const QModelIndex firstChildOfP1 = model->index(0, 0, model->index(1, 0));
    QCOMPARE(textOf(ViewNav::peekStep(&view, firstChildOfP1, -1, &ViewNav::isChild)),
             QStringLiteral("P0C1"));
}

void TestViewNavigation::childFilterStopsAtLastChildOverall()
{
    QObject owner;
    auto* model = makeTree(2, 2, &owner);
    QTreeView view;
    view.setModel(model);
    view.expandAll();

    const QModelIndex lastChild = model->index(1, 0, model->index(1, 0));
    QVERIFY(!ViewNav::peekStep(&view, lastChild, +1, &ViewNav::isChild).isValid());

    const QModelIndex firstChild = model->index(0, 0, model->index(0, 0));
    QVERIFY(!ViewNav::peekStep(&view, firstChild, -1, &ViewNav::isChild).isValid());
}

void TestViewNavigation::collapsedChildrenAreNotVisited()
{
    QObject owner;
    auto* model = makeTree(2, 2, &owner);
    QTreeView view;
    view.setModel(model);
    view.collapseAll();

    // Nothing is expanded, so the visual order is just P0, P1.
    QCOMPARE(textOf(ViewNav::peekStep(&view, model->index(0, 0), +1)), QStringLiteral("P1"));
    QVERIFY(!ViewNav::peekStep(&view, model->index(0, 0), +1, &ViewNav::isChild).isValid());
}

void TestViewNavigation::followsProxySortOrderNotSourceOrder()
{
    QObject owner;
    auto* model = makeTree(3, 0, &owner);
    auto* proxy = new QSortFilterProxyModel(&owner);
    proxy->setSourceModel(model);
    proxy->sort(0, Qt::DescendingOrder);          // view order becomes P2, P1, P0

    QTreeView view;
    view.setModel(proxy);

    const QModelIndex viewP2 = ViewNav::fromSource(&view, model->index(2, 0));
    QCOMPARE(textOf(ViewNav::peekStep(&view, viewP2, +1)), QStringLiteral("P1"));
    QVERIFY(!ViewNav::peekStep(&view, viewP2, -1).isValid());   // P2 is now the first row
}

void TestViewNavigation::stepMovesSelectionAndCurrent()
{
    QObject owner;
    auto* model = makeTree(3, 0, &owner);
    QTreeView view;
    view.setModel(model);

    const QModelIndex from = model->index(0, 0);
    view.setCurrentIndex(from);

    const QModelIndex to = ViewNav::step(&view, from, +1);
    QCOMPARE(textOf(to), QStringLiteral("P1"));
    QCOMPARE(view.currentIndex(), to);
    QVERIFY(view.selectionModel()->isSelected(to));
    QVERIFY(!view.selectionModel()->isSelected(from));   // the old row was deselected
}

void TestViewNavigation::zeroDeltaAndInvalidOriginAreNoOps()
{
    QObject owner;
    auto* model = makeTree(3, 0, &owner);
    QTreeView view;
    view.setModel(model);

    QVERIFY(!ViewNav::peekStep(&view, model->index(1, 0), 0).isValid());
    QVERIFY(!ViewNav::peekStep(&view, QModelIndex{}, +1).isValid());
    QVERIFY(!ViewNav::peekStep(nullptr, model->index(1, 0), +1).isValid());
    QVERIFY(!ViewNav::step(&view, QModelIndex{}, +1).isValid());
}

QTEST_MAIN(TestViewNavigation)
#include "tst_ViewNavigation.moc"
