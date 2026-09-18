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
#include "controls/KadContactsModel.h"
#include "controls/KnownTypeStyle.h"
#include "controls/SearchResultsModel.h"
#include "controls/ServerListModel.h"
#include "controls/SortableItems.h"
#include "controls/UploadStatusDelegate.h"
#include "controls/UsenetQueueModel.h"
#include "prefs/Preferences.h"
#include "utils/ColorUtils.h"
#include "utils/Opcodes.h"
#include "utils/PriorityText.h"

#include <QCborArray>
#include <QCborMap>
#include <QDateTime>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
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

    // --- MFC column text ----------------------------------------------------
    void downloadSourcesReadAvailableOfTotal();
    void sourceRowsFollowMfcColumns();
    void aSourceIndexSurvivesAnEarlierDownloadLeaving();
    void completeSourcesShowPercentOrUnknown();
    void uploadPriorityLabelsMatchMfc();
    void serverCountsHideZeroAndCompact();
    void onQueueColumnsFollowMfc();
    void uploadStatusBarFollowsMfc();
    void kadContactImageFollowsMfc();
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
             QStringLiteral("192.168.1.10 : 4661"));
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

// ---------------------------------------------------------------------------
// MFC column text
// ---------------------------------------------------------------------------

void tst_ListSorting::downloadSourcesReadAvailableOfTotal()
{
    // MFC DownloadListCtrl.cpp:2019-2036. The port led with the transferring count, so
    // "2 / 12" read as two usable sources where nine were queued or sending.
    thePrefs.setShowExtControls(true);
    DownloadRow busy;
    busy.hash = QStringLiteral("busy");
    busy.status = QStringLiteral("ready");
    busy.sourceCount = 12;
    busy.availableSrcCount = 9;
    busy.transferringSrcCount = 2;
    busy.a4afSrcCount = 3;

    DownloadRow allUsable;
    allUsable.hash = QStringLiteral("usable");
    allUsable.status = QStringLiteral("ready");
    allUsable.sourceCount = 4;
    allUsable.availableSrcCount = 4;

    DownloadRow paused;
    paused.hash = QStringLiteral("paused");
    paused.status = QStringLiteral("paused");

    DownloadListModel model;
    model.setDownloads({busy, allUsable, paused});
    const auto text = [&model](int row) {
        return model.index(row, DownloadListModel::ColSources).data().toString();
    };
    QCOMPARE(text(0), QStringLiteral("9/12+3 (2)"));
    QCOMPARE(text(1), QStringLiteral("4"));
    QCOMPARE(text(2), QString());   // paused with no sources: blank

    thePrefs.setShowExtControls(false);   // A4AF is an extended control
    QCOMPARE(text(0), QStringLiteral("9/12 (2)"));
    thePrefs.setShowExtControls(true);
}

void tst_ListSorting::sourceRowsFollowMfcColumns()
{
    // MFC GetSourceItemDisplayText (DownloadListCtrl.cpp:508-519): software under Sources,
    // the queue rank under Priority. They sat under Last Reception and Sources.
    DownloadRow file;
    file.hash = QStringLiteral("file");
    file.status = QStringLiteral("ready");
    file.sourceCount = 3;

    SourceRow queued;
    queued.userHash = QStringLiteral("q");
    queued.userName = QStringLiteral("queued");
    queued.software = QStringLiteral("eMule v0.70b");
    queued.downloadState = QStringLiteral("OnQueue");
    queued.remoteQueueRank = 42;
    queued.partCount = 10;
    queued.availPartCount = 4;
    SourceRow full = queued;
    full.userHash = QStringLiteral("f");
    full.remoteQueueFull = true;
    SourceRow sending = queued;
    sending.userHash = QStringLiteral("s");
    sending.downloadState = QStringLiteral("Downloading");

    DownloadListModel model;
    model.setDownloads({file});
    model.setSources(QStringLiteral("file"), {queued, full, sending});
    const QModelIndex parent = model.index(0, 0);
    const auto text = [&](int row, int column) {
        return model.index(row, column, parent).data().toString();
    };

    QCOMPARE(text(0, DownloadListModel::ColSources), QStringLiteral("eMule v0.70b"));
    QCOMPARE(text(0, DownloadListModel::ColPriority), QStringLiteral("QR: 42"));
    QCOMPARE(text(1, DownloadListModel::ColPriority), QStringLiteral("Queue Full"));
    QCOMPARE(text(2, DownloadListModel::ColPriority), QString());   // only on queue
    QCOMPARE(text(0, DownloadListModel::ColLastReception), QString());
    QCOMPARE(text(0, DownloadListModel::ColSeenComplete), QString());
}

void tst_ListSorting::aSourceIndexSurvivesAnEarlierDownloadLeaving()
{
    // A source index used to carry its download's *row*. When a download above left,
    // a selected source under a lower one pointed past the end — or at another file.
    std::vector<DownloadRow> rows;
    for (const auto* hash : {"a", "b", "c"}) {
        DownloadRow row;
        row.hash = QString::fromLatin1(hash);
        row.fileName = row.hash;
        row.status = QStringLiteral("ready");
        row.sourceCount = 1;
        rows.push_back(row);
    }
    DownloadListModel model;
    model.setDownloads(rows);

    SourceRow peer;
    peer.userHash = QStringLiteral("peer");
    peer.userName = QStringLiteral("peer of c");
    model.setSources(QStringLiteral("c"), {peer});

    const QPersistentModelIndex source = model.index(0, 0, model.index(2, 0));
    QVERIFY(source.isValid());

    rows.erase(rows.begin() + 1);   // "b" goes; "c" moves up to row 1
    model.setDownloads(rows);

    QVERIFY(source.isValid());
    QCOMPARE(source.parent().row(), 1);
    QCOMPARE(source.parent().data().toString(), QStringLiteral("c"));
    const SourceRow* resolved = model.sourceAt(source);
    QVERIFY(resolved);
    QCOMPARE(resolved->userName, QStringLiteral("peer of c"));
}

void tst_ListSorting::completeSourcesShowPercentOrUnknown()
{
    // MFC SearchListCtrl.cpp:444-482 and 1452-1459. The port showed the raw count and
    // never the red that marks a file nobody has complete.
    thePrefs.setShowExtControls(false);
    std::vector<SearchResultRow> rows(4);
    rows[0].sourceCount = 8;
    rows[0].completeSourceCount = 2;
    rows[0].fileSize = 700'000'000;
    rows[1].sourceCount = 5;
    rows[1].fileSize = 700'000'000;
    rows[2].sourceCount = 5;
    rows[2].isKad = true;               // Kad knows no complete sources
    rows[2].fileSize = 700'000'000;
    rows[3].sourceCount = 1;
    rows[3].isKad = true;
    rows[3].fileSize = 1'000'000;       // one part: complete wherever it is

    SearchResultsModel model;
    model.setResults(std::move(rows));
    const auto text = [&model](int row) {
        return model.index(row, SearchResultsModel::ColComplete).data().toString();
    };
    const auto colour = [&model](int row, int column) {
        return model.index(row, column).data(Qt::ForegroundRole).value<QColor>();
    };

    QCOMPARE(text(0), QStringLiteral("25%"));
    QCOMPARE(text(1), QStringLiteral("0%"));
    QCOMPARE(text(2), QStringLiteral("?"));
    QCOMPARE(text(3), QStringLiteral("Yes"));

    const QColor red(255, 0, 0);
    QCOMPARE(colour(1, SearchResultsModel::ColComplete), red);
    QVERIFY(colour(2, SearchResultsModel::ColComplete) != red);   // unknown is not incomplete
    QVERIFY(colour(1, SearchResultsModel::ColFileName) != red);   // that cell only

    thePrefs.setShowExtControls(true);
    QCOMPARE(text(0), QStringLiteral("25% (2)"));
}

void tst_ListSorting::uploadPriorityLabelsMatchMfc()
{
    // MFC KnownFile.cpp:1703-1726: PR_VERYHIGH is "Release", and neither end of the
    // scale is ever shown as auto.
    QCOMPARE(uploadPriorityText(3, false), QStringLiteral("Release"));
    QCOMPARE(uploadPriorityText(3, true), QStringLiteral("Release"));
    QCOMPARE(uploadPriorityText(4, true), QStringLiteral("Very Low"));
    QCOMPARE(uploadPriorityText(0, true), QStringLiteral("Auto [Lo]"));
    QCOMPARE(uploadPriorityText(1, true), QStringLiteral("Auto [No]"));
    QCOMPARE(uploadPriorityText(2, false), QStringLiteral("High"));
}

void tst_ListSorting::serverCountsHideZeroAndCompact()
{
    // MFC ServerListCtrl.cpp:117-190: unknown counts are blank, big ones CastItoIShort'd,
    // Max Users waits for Users, and there is a Files column.
    QCborMap busy = server(QStringLiteral("busy"), QStringLiteral("1.1.1.1"), 4661, 0);
    busy.insert(QStringLiteral("users"), 1'234'567);
    busy.insert(QStringLiteral("maxUsers"), 2'000'000);
    busy.insert(QStringLiteral("files"), 900);
    busy.insert(QStringLiteral("ping"), 35);
    QCborMap fresh = server(QStringLiteral("fresh"), QStringLiteral("1.1.1.2"), 4661, 0);
    fresh.insert(QStringLiteral("maxUsers"), 5000);
    QCborMap mid = server(QStringLiteral("mid"), QStringLiteral("1.1.1.3"), 4661, 0);
    mid.insert(QStringLiteral("users"), 5);
    mid.insert(QStringLiteral("files"), 120'000);   // "120.00 k" leads "900" as text

    ServerListModel model;
    model.refreshFromCborArray({busy, fresh, mid});
    const auto text = [&model](int row, int column) {
        return model.index(row, column).data().toString();
    };

    QCOMPARE(model.headerData(ServerListModel::ColFiles, Qt::Horizontal).toString(),
             QStringLiteral("Files"));
    QCOMPARE(text(0, ServerListModel::ColUsers), QStringLiteral("1.23 M"));
    QCOMPARE(text(0, ServerListModel::ColMaxUsers), QStringLiteral("2.00 M"));
    QCOMPARE(text(0, ServerListModel::ColFiles), QStringLiteral("900"));
    QCOMPARE(text(0, ServerListModel::ColPing), QStringLiteral("35"));
    QCOMPARE(text(1, ServerListModel::ColUsers), QString());
    QCOMPARE(text(1, ServerListModel::ColMaxUsers), QString());
    QCOMPARE(text(1, ServerListModel::ColFiles), QString());
    QCOMPARE(text(1, ServerListModel::ColPing), QString());

    QSortFilterProxyModel proxy;
    sortThrough(proxy, &model, ServerListModel::ColFiles, Qt::DescendingOrder);
    QCOMPARE(proxy.index(0, ServerListModel::ColName).data().toString(), QStringLiteral("mid"));
}

void tst_ListSorting::onQueueColumnsFollowMfc()
{
    // MFC QueueListCtrl.cpp:185-260. Score showed the remote queue rank, Last Seen repeated
    // Entered Queue, and File Priority read the requested download's priority.
    ClientListModel model(ClientListMode::OnQueue);
    std::vector<ClientRow> rows(3);
    rows[0].userName = QStringLiteral("high");
    rows[0].queueRating = 100;
    rows[0].queueScore = 9;
    rows[0].lastUpRequestDelay = 65'000;
    rows[0].waitStartTime = 3'700'000;
    rows[0].uploadFilePriority = 3;
    rows[1].userName = QStringLiteral("low");
    rows[1].queueScore = 10;                 // "10" sorts before "9" as text
    rows[1].hasLowID = true;
    rows[2].userName = QStringLiteral("next");
    rows[2].queueScore = 5;
    rows[2].hasLowID = true;
    rows[2].addNextConnect = true;
    rows[2].isBanned = true;
    model.setClients(std::move(rows));

    const auto text = [&](int row, int column) { return model.index(row, column).data().toString(); };
    QCOMPARE(text(0, 2), QStringLiteral("Release"));
    QCOMPARE(text(0, 3), QStringLiteral("100"));
    QCOMPARE(text(0, 4), QStringLiteral("9"));
    QCOMPARE(text(1, 4), QStringLiteral("10 (Low ID)"));
    QCOMPARE(text(2, 4), QStringLiteral("5 ****"));
    QCOMPARE(text(0, 5), QStringLiteral("0"));
    QCOMPARE(text(0, 6), QStringLiteral("1:05 mins"));
    QCOMPARE(text(0, 7), QStringLiteral("1:01 h"));
    QCOMPARE(text(0, 8), QStringLiteral("No"));
    QCOMPARE(text(2, 8), QStringLiteral("Yes"));
    QCOMPARE(text(0, 9), QString());

    QSortFilterProxyModel proxy;
    sortThrough(proxy, &model, 4, Qt::AscendingOrder);
    QCOMPARE(proxy.index(0, 0).data().toString(), QStringLiteral("next"));
    QCOMPARE(proxy.index(1, 0).data().toString(), QStringLiteral("high"));
    QCOMPARE(proxy.index(2, 0).data().toString(), QStringLiteral("low"));

    // The Uploading list shares the time format (UploadListCtrl.cpp:205-213)
    ClientListModel uploads(ClientListMode::Uploading);
    std::vector<ClientRow> slot(1);
    slot[0].waitStartTime = 125'000;
    slot[0].uploadStartDelay = 30'000;
    slot[0].hasLowID = true;
    uploads.setClients(std::move(slot));
    QCOMPARE(uploads.index(0, 4).data().toString(), QStringLiteral("2:05 mins (Low ID)"));
    QCOMPARE(uploads.index(0, 5).data().toString(), QStringLiteral("30 secs"));
}

void tst_ListSorting::uploadStatusBarFollowsMfc()
{
    // MFC CUpDownClient::DrawUpStatusBar: had parts black, the next part yellow, sent bytes
    // green, the rest light grey — and a paler set for a slot past the active count.
    UpStatusBar bar;
    bar.fileSize = 3 * static_cast<int64_t>(PARTSIZE);
    bar.parts = QByteArray("\x01\x00\x00", 3);
    bar.nextParts = {1};
    const auto part = static_cast<int64_t>(PARTSIZE);
    bar.sentRanges = {{2 * part, 2 * part + part / 2 - 1}};

    const auto render = [](const UpStatusBar& b) {
        QImage image(300, 10, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        paintUpStatusBar(painter, image.rect(), b);
        return image;
    };

    QImage image = render(bar);
    QCOMPARE(image.pixelColor(50, 5), QColor(0, 0, 0));
    QCOMPARE(image.pixelColor(150, 5), QColor(255, 208, 0));
    QCOMPARE(image.pixelColor(220, 5), QColor(0, 150, 0));
    QCOMPARE(image.pixelColor(290, 5), QColor(224, 224, 224));

    bar.greyed = true;
    image = render(bar);
    QCOMPARE(image.pixelColor(50, 5), QColor(191, 191, 191));
    QCOMPARE(image.pixelColor(290, 5), QColor(248, 248, 248));

    // Nothing to draw without a size
    image = render(UpStatusBar{});
    QCOMPARE(image.pixelColor(50, 5), QColor(Qt::white));
}

void tst_ListSorting::kadContactImageFollowsMfc()
{
    // MFC KadContactListCtrl.cpp:117-124: an active contact that isn't IP-verified, or a
    // bootstrap one, shows SrcUnknown (5).
    KadContactRow c;
    c.type = 1;
    QCOMPARE(KadContactsModel::contactImage(c), 5);
    c.ipVerified = true;
    QCOMPARE(KadContactsModel::contactImage(c), 1);
    c.bootstrap = true;
    QCOMPARE(KadContactsModel::contactImage(c), 5);

    KadContactRow old;
    old.type = 3;
    QCOMPARE(KadContactsModel::contactImage(old), 3);
    old.type = 7;
    QCOMPARE(KadContactsModel::contactImage(old), 4);
}

QTEST_MAIN(tst_ListSorting)
#include "tst_ListSorting.moc"
