/// @file tst_CategoryFilterProxy.cpp
/// @brief The category tab's filter — the one behavioural change in extracting
///        the category UI out of TransferPanel.
///
/// This proxy was file-local to TransferPanel and reached the category by
/// casting `sourceModel()` to `DownloadListModel*` through a second proxy. Its
/// own comment records what that cost: when the cast failed the fallback
/// accepted **every** row, so the tab bar looked like it worked and filtered
/// nothing. That is the failure this file exists to make impossible — it is
/// invisible on the screen unless you count rows.
///
/// Two arrangements matter and only one of them was ever exercised:
///
///   - the proxy directly on a model, and
///   - the proxy stacked on a **sort proxy**, which is how both panels use it
///     and the arrangement the old cast could not see through.
///
/// A tiny model stands in for the real ones. What is under test is the proxy's
/// contract with `kCategoryRole`, not any model's data.

#include "controls/CategoryFilterProxy.h"

#include <QAbstractItemModel>
#include <QSortFilterProxyModel>
#include <QTest>

using namespace eMule;

namespace {

/// Flat list of (name, category) rows answering kCategoryRole, plus one child
/// per row so the "child rows are never filtered" rule has something to act on.
class ToyModel : public QAbstractItemModel {
public:
    struct Row {
        QString name;
        int category = 0;
    };

    explicit ToyModel(QList<Row> rows) : m_rows(std::move(rows)) {}

    /// Rows answer the role; a model that does not is covered separately.
    void setAnswersRole(bool on) { m_answersRole = on; }

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override
    {
        if (!hasIndex(row, column, parent))
            return {};
        if (parent.isValid())
            return createIndex(row, column, quintptr(parent.row() + 1));
        return createIndex(row, column, quintptr(0));
    }

    QModelIndex parent(const QModelIndex& idx) const override
    {
        if (!idx.isValid() || idx.internalId() == 0)
            return {};
        return createIndex(int(idx.internalId()) - 1, 0, quintptr(0));
    }

    int rowCount(const QModelIndex& parent = {}) const override
    {
        if (!parent.isValid())
            return int(m_rows.size());
        return parent.internalId() == 0 ? 1 : 0;   // one child each
    }

    int columnCount(const QModelIndex& = {}) const override { return 1; }

    QVariant data(const QModelIndex& idx, int role = Qt::DisplayRole) const override
    {
        if (!idx.isValid())
            return {};
        if (idx.internalId() != 0)
            return role == Qt::DisplayRole ? QVariant(QStringLiteral("child")) : QVariant{};

        const Row& r = m_rows.at(idx.row());
        if (role == Qt::DisplayRole)
            return r.name;
        if (role == kCategoryRole && m_answersRole)
            return r.category;
        return {};
    }

private:
    QList<Row> m_rows;
    bool m_answersRole = true;
};

QStringList namesOf(const QAbstractItemModel& model)
{
    QStringList out;
    for (int i = 0; i < model.rowCount(); ++i)
        out << model.index(i, 0).data(Qt::DisplayRole).toString();
    return out;
}

} // namespace

class tst_CategoryFilterProxy : public QObject {
    Q_OBJECT

private slots:
    void theAllTabShowsEverything();
    void aCategoryFilterShowsOnlyItsOwnRows();
    void aCategoryFilterStackedOnASortProxyStillFilters();
    void childRowsAreNeverFiltered();
    void aModelThatDoesNotAnswerTheRoleIsHiddenNotShown();
};

void tst_CategoryFilterProxy::theAllTabShowsEverything()
{
    ToyModel model({{QStringLiteral("a"), 0},
                    {QStringLiteral("b"), 1},
                    {QStringLiteral("c"), 2}});
    CategoryFilterProxy proxy;
    proxy.setSourceModel(&model);

    QCOMPARE(proxy.categoryFilter(), 0);
    QCOMPARE(proxy.rowCount(), 3);
}

void tst_CategoryFilterProxy::aCategoryFilterShowsOnlyItsOwnRows()
{
    ToyModel model({{QStringLiteral("a"), 0},
                    {QStringLiteral("b"), 1},
                    {QStringLiteral("c"), 2},
                    {QStringLiteral("d"), 1}});
    CategoryFilterProxy proxy;
    proxy.setSourceModel(&model);

    proxy.setCategoryFilter(1);
    QCOMPARE(namesOf(proxy), QStringList({QStringLiteral("b"), QStringLiteral("d")}));

    proxy.setCategoryFilter(2);
    QCOMPARE(namesOf(proxy), QStringList({QStringLiteral("c")}));

    // An uncategorised row belongs to no tab but "All" — it is not swept into
    // the first real one.
    proxy.setCategoryFilter(3);
    QCOMPARE(proxy.rowCount(), 0);

    proxy.setCategoryFilter(0);
    QCOMPARE(proxy.rowCount(), 4);
}

void tst_CategoryFilterProxy::aCategoryFilterStackedOnASortProxyStillFilters()
{
    // The arrangement both panels actually build, and the one the old
    // cast-to-model version could not see through: it accepted every row and the
    // tab bar silently filtered nothing.
    ToyModel model({{QStringLiteral("c"), 1},
                    {QStringLiteral("a"), 2},
                    {QStringLiteral("b"), 1}});

    QSortFilterProxyModel sort;
    sort.setSourceModel(&model);
    sort.sort(0, Qt::AscendingOrder);

    CategoryFilterProxy proxy;
    proxy.setSourceModel(&sort);
    proxy.setCategoryFilter(1);

    // Filtered *and* still in the sort proxy's order — b before c, not the
    // model's c before b.
    QCOMPARE(namesOf(proxy), QStringList({QStringLiteral("b"), QStringLiteral("c")}));

    proxy.setCategoryFilter(2);
    QCOMPARE(namesOf(proxy), QStringList({QStringLiteral("a")}));
}

void tst_CategoryFilterProxy::childRowsAreNeverFiltered()
{
    // A source under an ED2K download, a file inside an NZB: it belongs to
    // whichever parent survived and carries no category of its own. Filtering it
    // on a role it does not answer would empty every expanded row.
    ToyModel model({{QStringLiteral("a"), 1}});
    CategoryFilterProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setCategoryFilter(1);

    QCOMPARE(proxy.rowCount(), 1);
    const QModelIndex parent = proxy.index(0, 0);
    QCOMPARE(proxy.rowCount(parent), 1);
    QCOMPARE(parent.model()->index(0, 0, parent).data().toString(),
             QStringLiteral("child"));
}

void tst_CategoryFilterProxy::aModelThatDoesNotAnswerTheRoleIsHiddenNotShown()
{
    // The safe direction. An unanswered role reads as 0 — uncategorised — so a
    // model wired up wrong shows nothing under a real category rather than
    // showing everything under all of them, which is what the old fallback did
    // and what made the bug invisible.
    ToyModel model({{QStringLiteral("a"), 1}, {QStringLiteral("b"), 2}});
    model.setAnswersRole(false);

    CategoryFilterProxy proxy;
    proxy.setSourceModel(&model);

    proxy.setCategoryFilter(1);
    QCOMPARE(proxy.rowCount(), 0);

    // "All" still shows them, so the list is never unreachable.
    proxy.setCategoryFilter(0);
    QCOMPARE(proxy.rowCount(), 2);
}

QTEST_MAIN(tst_CategoryFilterProxy)
#include "tst_CategoryFilterProxy.moc"
