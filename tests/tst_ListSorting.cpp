/// @file tst_ListSorting.cpp
/// @brief Columns that hold numbers must sort like numbers.
///
/// Qt orders an item-backed list by its *display string*, and five dialogs in
/// this GUI tried to opt out of that by writing the raw value to Qt::UserRole
/// with a comment saying "sort numerically, not as text". Nothing read it. The
/// symptom is quiet — the list does sort, just into the wrong order — so every
/// fixture here is chosen so the **text order is the reverse of the right one**.
/// A test whose sizes happen to sort the same either way proves nothing, which
/// is exactly the trap the archive dialog's own test fell into.
///
/// Two halves: the shared item classes in controls/SortableItems.h, then the
/// models whose Qt::UserRole answers feed a QSortFilterProxyModel — plus the MFC
/// colour cues those same models carry in Qt::ForegroundRole.

#include "controls/ClientListModel.h"
#include "controls/DownloadListModel.h"
#include "controls/KnownTypeStyle.h"
#include "controls/SearchResultsModel.h"
#include "controls/ServerListModel.h"
#include "controls/SortableItems.h"
#include "controls/UsenetQueueModel.h"
#include "prefs/Preferences.h"
#include "utils/ColorUtils.h"

#include <QCborArray>
#include <QCborMap>
#include <QDateTime>
#include <QGuiApplication>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTest>
#include <QTreeWidget>

using namespace eMule;

namespace {

/// A queue row with only the fields a sort reads.
UsenetItemRow release(const QString& id, qint64 totalBytes, int percent,
                      qint64 speed, int health, UsenetRowStatus status)
{
    UsenetItemRow r;
    r.id = id;
    r.name = id;
    r.status = status;
    r.statusText = id;
    r.totalBytes = totalBytes;
    r.decodedBytes = totalBytes * percent / 100;
    r.percent = percent;
    r.speed = speed;
    r.healthPercent = health;
    return r;
}

QCborMap server(const QString& name, const QString& address, int port, int preference)
{
    return QCborMap{
        {QStringLiteral("name"), name},
        {QStringLiteral("address"), address},
        {QStringLiteral("port"), port},
        {QStringLiteral("preference"), preference},
    };
}

/// The proxy the panels put over these models, configured as they configure it.
void sortThrough(QSortFilterProxyModel& proxy, QAbstractItemModel* model,
                 int column, Qt::SortOrder order)
{
    proxy.setSourceModel(model);
    proxy.setSortRole(Qt::UserRole);
    proxy.sort(column, order);
}

} // namespace

class tst_ListSorting : public QObject {
    Q_OBJECT

private slots:
    // --- controls/SortableItems.h ------------------------------------------
    void aTreeColumnSortsByMagnitudeNotByLeadingDigit();
    void aColumnWithNoSortKeyStillSortsAsText();
    void aUserRolePayloadDoesNotHijackTheOrder();
    void aDateColumnSortsChronologically();
    void aStandardItemModelSortsOnTheSameRole();

    // --- the models ---------------------------------------------------------
    void theUsenetQueueSortsByMagnitude();
    void anUnassessedReleaseSortsApartFromARuinedOne();
    void theUsenetStatusColumnSortsByProgressNotByWireOrder();
    void usenetFileRowsKeepNzbOrderUnderAnItemColumn();
    void serverPreferenceSortsByStrengthNotByName();
    void serverAddressesSortByOctetValue();
    void downloadPrioritySortsByRankNotByName();
    void downloadingClientsShowRateAndSessionTotals();

    // --- colour cues --------------------------------------------------------
    void searchResultsShadeByAvailability();
    void failingServersAreDimmed();
};

// ---------------------------------------------------------------------------
// controls/SortableItems.h
// ---------------------------------------------------------------------------

void tst_ListSorting::aTreeColumnSortsByMagnitudeNotByLeadingDigit()
{
    QTreeWidget tree;
    tree.setColumnCount(2);
    tree.setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Size")});

    // 9.90 MB and 10.00 GB: as text the megabyte row wins on the leading '9',
    // which is the whole bug.
    auto* small = new SortableTreeItem(&tree);
    small->setText(0, QStringLiteral("small"));
    small->setText(1, QStringLiteral("9.90 MB"));
    small->setData(1, SortRole, qint64(10380902));

    auto* big = new SortableTreeItem(&tree);
    big->setText(0, QStringLiteral("big"));
    big->setText(1, QStringLiteral("10.00 GB"));
    big->setData(1, SortRole, qint64(10737418240LL));

    tree.setSortingEnabled(true);
    tree.sortByColumn(1, Qt::AscendingOrder);
    QCOMPARE(tree.topLevelItem(0)->text(0), QStringLiteral("small"));
    QCOMPARE(tree.topLevelItem(1)->text(0), QStringLiteral("big"));

    tree.sortByColumn(1, Qt::DescendingOrder);
    QCOMPARE(tree.topLevelItem(0)->text(0), QStringLiteral("big"));
}

void tst_ListSorting::aColumnWithNoSortKeyStillSortsAsText()
{
    // The reason a mixed table needs the role only on its numeric columns: a
    // Name column must keep sorting as a name, with no key at all.
    QTreeWidget tree;
    tree.setColumnCount(2);
    tree.setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Size")});

    for (const auto& name : {QStringLiteral("zulu"), QStringLiteral("alpha")}) {
        auto* item = new SortableTreeItem(&tree);
        item->setText(0, name);
        item->setData(1, SortRole, qint64(1));
    }

    tree.setSortingEnabled(true);
    tree.sortByColumn(0, Qt::AscendingOrder);
    QCOMPARE(tree.topLevelItem(0)->text(0), QStringLiteral("alpha"));
}

void tst_ListSorting::aUserRolePayloadDoesNotHijackTheOrder()
{
    // Why SortRole is not Qt::UserRole. Every one of these lists already spends
    // UserRole on something the double-click handler reads back — a path, an
    // archive ordinal, a hash. A comparator reading UserRole would sort the Name
    // column by that payload; when the payload is a string, toLongLong() is 0 on
    // both sides, every pair compares equal, and the column stops sorting
    // entirely. That is exactly what happened to the Usenet details dialog.
    QTreeWidget tree;
    tree.setColumnCount(1);
    tree.setHeaderLabels({QStringLiteral("Name")});

    auto* zulu = new SortableTreeItem(&tree);
    zulu->setText(0, QStringLiteral("zulu"));
    zulu->setData(0, Qt::UserRole, QStringLiteral("/incoming/zulu.mkv"));

    auto* alpha = new SortableTreeItem(&tree);
    alpha->setText(0, QStringLiteral("alpha"));
    alpha->setData(0, Qt::UserRole, QStringLiteral("/incoming/alpha.mkv"));

    tree.setSortingEnabled(true);
    tree.sortByColumn(0, Qt::AscendingOrder);
    QCOMPARE(tree.topLevelItem(0)->text(0), QStringLiteral("alpha"));

    // And an ordinal payload must not reorder it either — that one *is* numeric,
    // so a UserRole comparator would silently sort by insertion order instead.
    zulu->setData(0, Qt::UserRole, 0);
    alpha->setData(0, Qt::UserRole, 1);
    tree.sortByColumn(0, Qt::AscendingOrder);
    QCOMPARE(tree.topLevelItem(0)->text(0), QStringLiteral("alpha"));
}

void tst_ListSorting::aDateColumnSortsChronologically()
{
    // A locale short-format date is the worst text sort in the set: "1/2/26"
    // leads "9/1/25" on every field.
    QTreeWidget tree;
    tree.setColumnCount(1);
    tree.setHeaderLabels({QStringLiteral("Modified")});

    const QDateTime older(QDate(2025, 9, 1), QTime(10, 0));
    const QDateTime newer(QDate(2026, 1, 2), QTime(10, 0));

    auto* a = new SortableTreeItem(&tree);
    a->setText(0, QStringLiteral("newer"));
    a->setData(0, SortRole, newer);

    auto* b = new SortableTreeItem(&tree);
    b->setText(0, QStringLiteral("older"));
    b->setData(0, SortRole, older);

    tree.setSortingEnabled(true);
    tree.sortByColumn(0, Qt::AscendingOrder);
    QCOMPARE(tree.topLevelItem(0)->text(0), QStringLiteral("older"));
}

void tst_ListSorting::aStandardItemModelSortsOnTheSameRole()
{
    // The other half of the fix: a QStandardItemModel bound straight to a view
    // sorts through the virtual QStandardItem::operator<, and its sortRole is
    // Qt::DisplayRole with no proxy in the way to change it.
    QStandardItemModel model;
    model.setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Size")});

    auto* smallName = new SortableStandardItem(QStringLiteral("small"));
    auto* smallSize = new SortableStandardItem(QStringLiteral("9.90 MB"));
    smallSize->setData(qint64(10380902), SortRole);
    model.appendRow({smallName, smallSize});

    auto* bigName = new SortableStandardItem(QStringLiteral("big"));
    auto* bigSize = new SortableStandardItem(QStringLiteral("10.00 GB"));
    bigSize->setData(qint64(10737418240LL), SortRole);
    model.appendRow({bigName, bigSize});

    model.sort(1, Qt::AscendingOrder);
    QCOMPARE(model.item(0, 0)->text(), QStringLiteral("small"));

    // The Name column has no key and must still sort as a name.
    model.sort(0, Qt::AscendingOrder);
    QCOMPARE(model.item(0, 0)->text(), QStringLiteral("big"));
}

// ---------------------------------------------------------------------------
// The models
// ---------------------------------------------------------------------------

void tst_ListSorting::theUsenetQueueSortsByMagnitude()
{
    UsenetQueueModel model;
    model.setItems({
        // "9.90 MB" / "7%" / "900 B/s" all lead their bigger counterpart as text.
        release(QStringLiteral("small"), 10380902, 7, 900, 20, UsenetRowStatus::Downloading),
        release(QStringLiteral("big"), 10737418240LL, 100, 1048576, 90, UsenetRowStatus::Downloading),
    });

    QSortFilterProxyModel proxy;

    for (const int column : {int(UsenetQueueModel::ColSize),
                             int(UsenetQueueModel::ColProgress),
                             int(UsenetQueueModel::ColSpeed)}) {
        sortThrough(proxy, &model, column, Qt::AscendingOrder);
        QCOMPARE(proxy.index(0, UsenetQueueModel::ColName).data().toString(),
                 QStringLiteral("small"));
        QCOMPARE(proxy.index(1, UsenetQueueModel::ColName).data().toString(),
                 QStringLiteral("big"));
    }

    // Remaining runs the other way: the small release has fewer bytes left.
    sortThrough(proxy, &model, UsenetQueueModel::ColRemaining, Qt::AscendingOrder);
    QCOMPARE(proxy.index(0, UsenetQueueModel::ColName).data().toString(),
             QStringLiteral("big"));
}

void tst_ListSorting::anUnassessedReleaseSortsApartFromARuinedOne()
{
    // -1 is "never checked", not "0% obtainable", and the column shows it as an
    // em dash — which as text sorts after every digit, putting unchecked
    // releases at the healthy end.
    // Inserted in an order that is neither the answer nor its reverse, so a
    // model answering no sort key at all cannot pass on the stable sort alone.
    UsenetQueueModel model;
    model.setItems({
        release(QStringLiteral("healthy"), 1000, 0, 0, 99, UsenetRowStatus::Queued),
        release(QStringLiteral("unchecked"), 1000, 0, 0, -1, UsenetRowStatus::Queued),
        release(QStringLiteral("ruined"), 1000, 0, 0, 3, UsenetRowStatus::Queued),
    });

    QSortFilterProxyModel proxy;
    sortThrough(proxy, &model, UsenetQueueModel::ColHealth, Qt::AscendingOrder);

    QCOMPARE(proxy.index(0, UsenetQueueModel::ColName).data().toString(),
             QStringLiteral("unchecked"));
    QCOMPARE(proxy.index(1, UsenetQueueModel::ColName).data().toString(),
             QStringLiteral("ruined"));
    QCOMPARE(proxy.index(2, UsenetQueueModel::ColName).data().toString(),
             QStringLiteral("healthy"));
}

void tst_ListSorting::theUsenetStatusColumnSortsByProgressNotByWireOrder()
{
    // The enum's own values are wire order, where Paused (2) sits between
    // Downloading (1) and Complete (3). The column ranks by how finished a
    // release is, so one click puts what needs attention at the bottom.
    UsenetQueueModel model;
    model.setItems({
        release(QStringLiteral("failed"), 1000, 0, 0, -1, UsenetRowStatus::Failed),
        release(QStringLiteral("done"), 1000, 100, 0, -1, UsenetRowStatus::Complete),
        release(QStringLiteral("paused"), 1000, 40, 0, -1, UsenetRowStatus::Paused),
        release(QStringLiteral("busy"), 1000, 40, 0, -1, UsenetRowStatus::Downloading),
    });

    QSortFilterProxyModel proxy;
    sortThrough(proxy, &model, UsenetQueueModel::ColStatus, Qt::AscendingOrder);

    const QStringList order = {
        proxy.index(0, UsenetQueueModel::ColName).data().toString(),
        proxy.index(1, UsenetQueueModel::ColName).data().toString(),
        proxy.index(2, UsenetQueueModel::ColName).data().toString(),
        proxy.index(3, UsenetQueueModel::ColName).data().toString(),
    };
    QCOMPARE(order, (QStringList{QStringLiteral("done"), QStringLiteral("busy"),
                                 QStringLiteral("paused"), QStringLiteral("failed")}));
}

void tst_ListSorting::usenetFileRowsKeepNzbOrderUnderAnItemColumn()
{
    // Speed, Priority, Health and Category are item-level columns a file row
    // leaves blank. Answering nothing there would make every child equal and let
    // the sort scatter a release's parts; the NZB position keeps them in posting
    // order instead.
    UsenetItemRow item = release(QStringLiteral("rel"), 3000, 0, 0, -1,
                                 UsenetRowStatus::Downloading);
    for (int i = 0; i < 3; ++i) {
        UsenetFileRow f;
        f.name = QStringLiteral("rel.part%1.rar").arg(3 - i);   // reverse of NZB order
        f.index = i;
        f.size = 1000;
        item.files.append(f);
    }

    UsenetQueueModel model;
    model.setItems({item});

    QSortFilterProxyModel proxy;
    sortThrough(proxy, &model, UsenetQueueModel::ColSpeed, Qt::AscendingOrder);

    const QModelIndex parent = proxy.index(0, 0);
    QCOMPARE(proxy.rowCount(parent), 3);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(proxy.index(i, UsenetQueueModel::ColName, parent).data().toString(),
                 QStringLiteral("rel.part%1.rar").arg(3 - i));
    }

    // A real key rather than "no answer": the children reverse with the sort
    // instead of relying on the proxy happening to be stable.
    sortThrough(proxy, &model, UsenetQueueModel::ColSpeed, Qt::DescendingOrder);
    const QModelIndex reversed = proxy.index(0, 0);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(proxy.index(i, UsenetQueueModel::ColName, reversed).data().toString(),
                 QStringLiteral("rel.part%1.rar").arg(i + 1));
    }
}

void tst_ListSorting::serverPreferenceSortsByStrengthNotByName()
{
    // The wire value is 0 Normal, 1 High, 2 Low — neither alphabetical nor a
    // strength order, and the column used to show the name and sort by it.
    ServerListModel model;
    model.refreshFromCborArray({
        server(QStringLiteral("n"), QStringLiteral("1.1.1.1"), 4661, 0),
        server(QStringLiteral("h"), QStringLiteral("1.1.1.2"), 4661, 1),
        server(QStringLiteral("l"), QStringLiteral("1.1.1.3"), 4661, 2),
    });

    QSortFilterProxyModel proxy;
    sortThrough(proxy, &model, ServerListModel::ColPreference, Qt::AscendingOrder);

    QCOMPARE(proxy.index(0, ServerListModel::ColName).data().toString(), QStringLiteral("l"));
    QCOMPARE(proxy.index(1, ServerListModel::ColName).data().toString(), QStringLiteral("n"));
    QCOMPARE(proxy.index(2, ServerListModel::ColName).data().toString(), QStringLiteral("h"));

    // The cell still reads as a word.
    QCOMPARE(proxy.index(2, ServerListModel::ColPreference).data().toString(),
             QStringLiteral("High"));
}

void tst_ListSorting::serverAddressesSortByOctetValue()
{
    ServerListModel model;
    model.refreshFromCborArray({
        server(QStringLiteral("ten"), QStringLiteral("192.168.1.10"), 4661, 0),
        server(QStringLiteral("nine"), QStringLiteral("192.168.1.9"), 4661, 0),
        server(QStringLiteral("two"), QStringLiteral("192.168.1.2"), 16000, 0),
    });

    QSortFilterProxyModel proxy;
    sortThrough(proxy, &model, ServerListModel::ColIP, Qt::AscendingOrder);

    QCOMPARE(proxy.index(0, ServerListModel::ColName).data().toString(), QStringLiteral("two"));
    QCOMPARE(proxy.index(1, ServerListModel::ColName).data().toString(), QStringLiteral("nine"));
    QCOMPARE(proxy.index(2, ServerListModel::ColName).data().toString(), QStringLiteral("ten"));

    // The displayed form is untouched — only the key behind it is padded.
    QCOMPARE(proxy.index(2, ServerListModel::ColIP).data().toString(),
             QStringLiteral("192.168.1.10:4661"));
}

void tst_ListSorting::downloadPrioritySortsByRankNotByName()
{
    // The daemon sends a name (JsonSerializers.h priorityToString), and by name
    // the column comes out high, low, normal, veryHigh, veryLow.
    DownloadListModel model;
    std::vector<DownloadRow> rows;
    for (const auto& [hash, priority] : {
             std::pair{QStringLiteral("a"), QStringLiteral("veryHigh")},
             std::pair{QStringLiteral("b"), QStringLiteral("low")},
             std::pair{QStringLiteral("c"), QStringLiteral("normal")},
             std::pair{QStringLiteral("d"), QStringLiteral("veryLow")},
         }) {
        DownloadRow row;
        row.hash = hash;
        row.fileName = hash;
        row.status = QStringLiteral("paused");
        row.priority = priority;
        row.fileSize = 1000;
        rows.push_back(row);
    }
    model.setDownloads(std::move(rows));

    QSortFilterProxyModel proxy;
    sortThrough(proxy, &model, DownloadListModel::ColPriority, Qt::AscendingOrder);

    const QStringList order = {
        proxy.index(0, DownloadListModel::ColFileName).data().toString(),
        proxy.index(1, DownloadListModel::ColFileName).data().toString(),
        proxy.index(2, DownloadListModel::ColFileName).data().toString(),
        proxy.index(3, DownloadListModel::ColFileName).data().toString(),
    };
    QCOMPARE(order, (QStringList{QStringLiteral("d"), QStringLiteral("b"),
                                 QStringLiteral("c"), QStringLiteral("a")}));
}

void tst_ListSorting::downloadingClientsShowRateAndSessionTotals()
{
    // MFC DownloadClientsCtrl.cpp:181-198. Speed used to be the session byte count
    // reformatted as a rate, and the second Transferred column was lifetime download
    // where MFC shows session upload.
    ClientListModel model(ClientListMode::Downloading);
    std::vector<ClientRow> rows(2);
    rows[0].userName = QStringLiteral("slow");
    rows[0].downDatarate = 9 * 1024;
    rows[0].sessionDown = 5 * 1024 * 1024;
    rows[0].downloadedTotal = 10 * 1024 * 1024;   // earlier sessions too: shown in brackets
    rows[0].sessionUp = 1024 * 1024;
    rows[0].transferredDown = 99 * 1024 * 1024;   // no longer shown here
    rows[1].userName = QStringLiteral("fast");
    rows[1].downDatarate = 10 * 1024;             // "10.00" sorts before "9.00" as text
    model.setClients(std::move(rows));

    QCOMPARE(model.index(0, 3).data().toString(), QStringLiteral("9.00 KB/s"));
    QCOMPARE(model.index(0, 5).data().toString(), QStringLiteral("5.00 MB (10.00 MB)"));
    QCOMPARE(model.index(0, 6).data().toString(), QStringLiteral("1.00 MB"));
    QCOMPARE(model.headerData(5, Qt::Horizontal).toString(), QStringLiteral("Transferred Down"));
    QCOMPARE(model.headerData(6, Qt::Horizontal).toString(), QStringLiteral("Transferred Up"));

    QSortFilterProxyModel proxy;
    sortThrough(proxy, &model, 3, Qt::AscendingOrder);
    QCOMPARE(proxy.index(0, 0).data().toString(), QStringLiteral("slow"));
}

// ---------------------------------------------------------------------------
// Colour cues
// ---------------------------------------------------------------------------

void tst_ListSorting::searchResultsShadeByAvailability()
{
    // MFC SearchListCtrl.cpp:1381-1417: known type first, then spam in grey, then
    // shades toward blue by source count. Spam used to be red and to win outright.
    thePrefs.setEnableSearchResultFilter(true);
    SearchResultsModel model;
    std::vector<SearchResultRow> rows(5);
    rows[0].sourceCount = 1;
    rows[1].sourceCount = 2;
    rows[2].sourceCount = 14;
    rows[3].sourceCount = 50;
    rows[3].isSpam = true;
    rows[4].sourceCount = 50;
    rows[4].isSpam = true;
    rows[4].knownType = 2;   // downloading
    model.setResults(std::move(rows));
    const auto fg = [&model](int row) {
        return model.index(row, SearchResultsModel::ColFileName).data(Qt::ForegroundRole);
    };

    QVERIFY(!fg(0).isValid());   // one source: plain text, MFC's shade 0

    const QColor text = QGuiApplication::palette().color(QPalette::Text);
    const QColor few = fg(1).value<QColor>();
    const QColor many = fg(2).value<QColor>();
    QVERIFY(few.isValid() && many.isValid());
    QVERIFY(few != text);
    QVERIFY(many != few);
    // Every step moves further from the text colour toward blue.
    QVERIFY(few.blueF() >= text.blueF() && many.blueF() >= few.blueF());
    QVERIFY(few.redF() <= text.redF() && many.redF() <= few.redF());

    QCOMPARE(fg(3).value<QColor>(), dimmedText(0.5));
    QCOMPARE(fg(4).value<QColor>(), knownTypeColor(2));

    // Without the spam filter a spam-rated result is just another result.
    thePrefs.setEnableSearchResultFilter(false);
    QCOMPARE(fg(3).value<QColor>(), many);
    thePrefs.setEnableSearchResultFilter(true);
}

void tst_ListSorting::failingServersAreDimmed()
{
    // MFC ServerListCtrl.cpp:209-214: the connected server wins, then light grey once
    // dead, grey from the second failure. The port parsed the count and never used it.
    thePrefs.setDeadServerRetries(5);
    const auto failing = [](const QString& name, const QString& address, int failed) {
        QCborMap m = server(name, address, 4661, 0);
        m.insert(QStringLiteral("failedCount"), failed);
        return m;
    };
    QCborMap connected = failing(QStringLiteral("connected"), QStringLiteral("1.1.1.4"), 7);
    connected.insert(QStringLiteral("serverId"), 42);

    ServerListModel model;
    model.refreshFromCborArray({
        failing(QStringLiteral("ok"), QStringLiteral("1.1.1.1"), 1),
        failing(QStringLiteral("failing"), QStringLiteral("1.1.1.2"), 2),
        failing(QStringLiteral("dead"), QStringLiteral("1.1.1.3"), 5),
        connected,
    });
    model.setConnectedServer(42);
    const auto fg = [&model](int row) {
        return model.index(row, ServerListModel::ColName).data(Qt::ForegroundRole);
    };

    QVERIFY(!fg(0).isValid());
    QCOMPARE(fg(1).value<QColor>(), dimmedText(0.5));
    QCOMPARE(fg(2).value<QColor>(), dimmedText(0.75));
    QCOMPARE(fg(3).value<QColor>(), QColor(0x33, 0x99, 0xFF));

    // 0 means "never remove", so no server is dead — but a failing one still greys.
    thePrefs.setDeadServerRetries(0);
    QCOMPARE(fg(2).value<QColor>(), dimmedText(0.5));
    thePrefs.setDeadServerRetries(20);
}

QTEST_MAIN(tst_ListSorting)
#include "tst_ListSorting.moc"
