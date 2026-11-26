/// @file tst_NoNeighbourSelect.cpp
/// @brief Removing a list's current row selects nothing else (MFC DeleteItem).
///
/// Plain Qt SingleSelection views select the next row when the current row goes,
/// selected or not. Covers the mixin AbstractListView derives from and the
/// friends / IRC nick QListViews use.

#include "controls/NoNeighbourSelect.h"

#include <QListView>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTest>
#include <QTreeView>

using namespace eMule;

namespace {

template<class View>
struct List {
    QStandardItemModel model;
    QSortFilterProxyModel proxy;
    NoNeighbourSelect<View> view;

    explicit List(QAbstractItemView::SelectionMode mode = QAbstractItemView::SingleSelection)
    {
        for (const char* name : {"A", "B", "C", "D"})
            model.appendRow(new QStandardItem(QString::fromLatin1(name)));
        proxy.setSourceModel(&model);
        view.setModel(&proxy);
        view.setSelectionMode(mode);
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
    }

    QModelIndex row(int r) const { return proxy.index(r, 0); }

    QString selected() const
    {
        QStringList out;
        for (const QModelIndex& idx : view.selectionModel()->selectedRows())
            out << idx.data().toString();
        out.sort();
        return out.join(QString());
    }
};

} // namespace

class tst_NoNeighbourSelect : public QObject {
    Q_OBJECT

private slots:
    void selectedRowRemoved_selectsNothing()
    {
        List<QTreeView> list;
        list.view.setCurrentIndex(list.row(1));
        QCOMPARE(list.selected(), QStringLiteral("B"));

        list.model.removeRow(1);
        QCOMPARE(list.selected(), QString());
        QVERIFY(list.view.currentIndex().isValid());   // keys still have a row to start from
    }

    void unselectedCurrentRowRemoved_selectsNothing()
    {
        List<QTreeView> list;
        list.view.selectionModel()->setCurrentIndex(list.row(1), QItemSelectionModel::NoUpdate);

        list.model.removeRow(1);
        QCOMPARE(list.selected(), QString());
    }

    void otherRowRemoved_selectionStays()
    {
        List<QTreeView> list;
        list.view.setCurrentIndex(list.row(1));

        list.model.removeRow(2);
        QCOMPARE(list.selected(), QStringLiteral("B"));
    }

    void listView_selectedRowRemoved_selectsNothing()
    {
        List<QListView> list;
        list.view.setCurrentIndex(list.row(1));

        list.model.removeRow(1);
        QCOMPARE(list.selected(), QString());
    }

    void clickAfterRemoval_stillSelects()
    {
        List<QTreeView> list;
        list.view.setCurrentIndex(list.row(1));
        list.model.removeRow(1);

        list.view.setCurrentIndex(list.row(2));
        QCOMPARE(list.selected(), QStringLiteral("D"));
    }

    void multiSelection_keepsSurvivors()
    {
        List<QTreeView> list(QAbstractItemView::ExtendedSelection);
        auto* sel = list.view.selectionModel();
        sel->setCurrentIndex(list.row(1), QItemSelectionModel::NoUpdate);
        sel->select(list.row(1), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        sel->select(list.row(3), QItemSelectionModel::Select | QItemSelectionModel::Rows);

        list.model.removeRow(1);
        QCOMPARE(list.selected(), QStringLiteral("D"));
    }
};

QTEST_MAIN(tst_NoNeighbourSelect)
#include "tst_NoNeighbourSelect.moc"
