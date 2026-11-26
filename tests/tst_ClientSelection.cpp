/// @file tst_ClientSelection.cpp
/// @brief Client list selection across a poll refresh (issue #253).
///
/// A client leaving the Uploading list made Qt's SingleSelection view select the
/// neighbour row — also when the leaving row was only current, not selected. MFC
/// selects nothing (UploadListCtrl.cpp:447 RemoveClient). Runs the panel's exact
/// refresh: save → setClients (keyed row removal) → restore.

#include "controls/ClientListModel.h"
#include "utils/ClientSelection.h"

#include <QSortFilterProxyModel>
#include <QTest>
#include <QTreeView>

using namespace eMule;
using namespace Qt::StringLiterals;

namespace {

ClientRow client(const QString& hash)
{
    ClientRow r;
    r.userHash = hash;
    r.userName = hash;
    return r;
}

std::vector<ClientRow> clients(const QStringList& hashes)
{
    std::vector<ClientRow> rows;
    for (const QString& h : hashes)
        rows.push_back(client(h));
    return rows;
}

/// One client list as TransferPanel builds it: model, sort proxy, row view.
struct ClientList {
    ClientListModel model{ClientListMode::Uploading};
    QSortFilterProxyModel proxy;
    QTreeView view;

    explicit ClientList(QAbstractItemView::SelectionMode mode = QAbstractItemView::SingleSelection)
    {
        model.setClients(clients({u"A"_s, u"B"_s, u"C"_s, u"D"_s}));
        proxy.setSourceModel(&model);
        view.setModel(&proxy);
        view.setSelectionMode(mode);
        view.setSelectionBehavior(QAbstractItemView::SelectRows);
    }

    QModelIndex row(int r) const { return proxy.index(r, 0); }

    /// TransferPanel::applyClients without the scroll bars
    void refresh(const QStringList& hashes)
    {
        const QStringList keys = ClientSelection::save(&view, &model);
        model.setClients(clients(hashes));
        ClientSelection::restore(&view, &model, keys);
    }

    QStringList selected() const
    {
        QStringList out;
        for (const QModelIndex& idx : view.selectionModel()->selectedRows(0))
            out << model.clientAt(proxy.mapToSource(idx).row())->userHash;
        out.sort();
        return out;
    }
};

} // namespace

class tst_ClientSelection : public QObject {
    Q_OBJECT

private slots:
    void selectedRowLeaves_selectsNothing()
    {
        ClientList list;
        list.view.setCurrentIndex(list.row(1));   // click B
        QCOMPARE(list.selected(), QStringList{u"B"_s});

        list.refresh({u"A"_s, u"C"_s, u"D"_s});
        QCOMPARE(list.selected(), QStringList{});
    }

    void unselectedCurrentRowLeaves_selectsNothing()
    {
        // current without selection: focus, or a Ctrl+click deselect
        ClientList list;
        list.view.selectionModel()->setCurrentIndex(list.row(1), QItemSelectionModel::NoUpdate);
        QCOMPARE(list.selected(), QStringList{});

        list.refresh({u"A"_s, u"C"_s, u"D"_s});
        QCOMPARE(list.selected(), QStringList{});
    }

    void otherRowLeaves_selectionStays()
    {
        ClientList list;
        list.view.setCurrentIndex(list.row(1));

        list.refresh({u"A"_s, u"B"_s, u"D"_s});
        QCOMPARE(list.selected(), QStringList{u"B"_s});
        QCOMPARE(list.view.currentIndex().row(), 1);
    }

    void nothingSelected_rowLeaves_staysEmpty()
    {
        ClientList list;
        list.refresh({u"B"_s, u"C"_s, u"D"_s});
        QCOMPARE(list.selected(), QStringList{});
    }

    void multiSelection_keepsSurvivorsOnly()
    {
        // Known Clients is ExtendedSelection
        ClientList list(QAbstractItemView::ExtendedSelection);
        auto* sel = list.view.selectionModel();
        sel->setCurrentIndex(list.row(0), QItemSelectionModel::NoUpdate);
        sel->select(list.row(0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
        sel->select(list.row(2), QItemSelectionModel::Select | QItemSelectionModel::Rows);

        list.refresh({u"B"_s, u"C"_s, u"D"_s});
        QCOMPARE(list.selected(), QStringList{u"C"_s});
    }
};

QTEST_MAIN(tst_ClientSelection)
#include "tst_ClientSelection.moc"
