/// @file tst_IPFilterDialog.cpp
/// @brief IP Filter dialog column layout: default widths, movable, saved, header menu.

#include "app/UiState.h"
#include "controls/AbstractListView.h"
#include "dialogs/IPFilterDialog.h"

#include <QHeaderView>
#include <QMenu>
#include <QTableView>
#include <QTest>

using namespace eMule;

class tst_IPFilterDialog : public QObject {
    Q_OBJECT

private slots:
    void columnsUseTableBase();
    void keepsColumnLayout();
};

void tst_IPFilterDialog::columnsUseTableBase()
{
    IPFilterDialog dlg(nullptr);
    auto* view = dlg.findChild<QTableView*>();
    QVERIFY(dynamic_cast<ListTableView*>(view));
    const QHeaderView* h = view->horizontalHeader();
    QVERIFY(h->sectionsMovable());
    QCOMPARE(h->contextMenuPolicy(), Qt::CustomContextMenu);   // column menu
    QCOMPARE(h->sectionSize(IpFilterModel::ColLevel), 50);
}

void tst_IPFilterDialog::keepsColumnLayout()
{
    {
        IPFilterDialog dlg(nullptr);
        auto* view = dlg.findChild<QTableView*>();
        QHeaderView* h = view->horizontalHeader();
        h->resizeSection(IpFilterModel::ColStart, 111);
        h->moveSection(h->visualIndex(IpFilterModel::ColHits), 1);
        // Hide through the header menu, as a user does
        emit h->customContextMenuRequested(QPoint(5, 5));
        auto* menu = view->findChild<QMenu*>();
        QVERIFY(menu);
        const QList<QAction*> actions = menu->actions();
        QCOMPARE(actions.size(), IpFilterModel::ColCount - 1);   // first column can't hide
        actions.at(IpFilterModel::ColLevel - 1)->trigger();
        menu->close();
    }
    IPFilterDialog dlg(nullptr);
    const QHeaderView* h = dlg.findChild<QTableView*>()->horizontalHeader();
    QCOMPARE(h->sectionSize(IpFilterModel::ColStart), 111);
    QCOMPARE(h->visualIndex(IpFilterModel::ColHits), 1);
    QVERIFY(h->isSectionHidden(IpFilterModel::ColLevel));
}

QTEST_MAIN(tst_IPFilterDialog)
#include "tst_IPFilterDialog.moc"
