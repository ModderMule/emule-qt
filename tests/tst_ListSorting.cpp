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

#include "controls/BarShader.h"
#include "controls/ClientListModel.h"
#include "controls/DownloadListModel.h"
#include "controls/DownloadProgressDelegate.h"
#include "controls/KadContactsModel.h"
#include "controls/KadSearchesModel.h"
#include "controls/KnownTypeStyle.h"
#include "controls/SearchResultsModel.h"
#include "controls/ServerListModel.h"
#include "controls/SortableItems.h"
#include "controls/UploadStatusDelegate.h"
#include "controls/UsenetQueueModel.h"
#include "ipc/CborSerializers.h"
#include "prefs/Preferences.h"
#include "utils/ColorUtils.h"
#include "utils/Opcodes.h"
#include "utils/PriorityText.h"
#include "utils/StringUtils.h"

#include <QCborArray>
#include <QCborMap>
#include <QDateTime>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QSignalSpy>
#include <QTest>
#include <QTreeWidget>

#include <functional>

using namespace eMule;
using namespace Qt::StringLiterals;

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
    r.progressBytes = totalBytes * percent / 100;
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
    void searchResultsSortByConfidence();
    void failingServersAreDimmed();

    // --- MFC column text ----------------------------------------------------
    void downloadSourcesReadAvailableOfTotal();
    void sourceRowsFollowMfcColumns();
    void sourceStatusFollowsMfc();
    void a4afSourcesStayBelowAvailableOnes();
    void sourcesSortByQueueRankWithDownloadingFirst();
    void remainingAndSeenCompleteFollowMfc();
    void knownClientsFollowMfc();
    void uploadStateIsWordedAndSortedByState();
    void removingASearchNameKeepsTheFile();
    void downloadingClientsCarryThePartBar();
    void aSourceIndexSurvivesAnEarlierDownloadLeaving();
    void completeSourcesShowPercentOrUnknown();
    void uploadPriorityLabelsMatchMfc();
    void downloadPriorityLabelsMatchMfc();
    void serverCountsHideZeroAndCompact();
    void onQueueColumnsFollowMfc();
    void uploadStatusBarFollowsMfc();
    void kadContactImageFollowsMfc();
    void kadContactColumnsFollowMfc();
    void kadSearchRowsFollowMfc();

    // --- progress bars (BarShader, MFC CBarShader) --------------------------
    void barShaderShadesRoundBars();
    void barShaderBlendsSubPixelSpans();
    void downloadBarDrawsDataWhereItSits();
    void barRangesAreCappedKeepingTheEnds();
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

void tst_ListSorting::searchResultsSortByConfidence()
{
    SearchResultsModel model;
    std::vector<SearchResultRow> rows(6);
    const auto set = [&rows](size_t i, const char* band, int score) {
        rows[i].fileName = QString::fromLatin1(band);
        rows[i].confidence = QString::fromLatin1(band);
        rows[i].fakeScore = score;
    };
    set(0, "looks_good", 0);
    set(1, "caution", 30);
    set(2, "genuine", 0);
    set(3, "likely_fake", 90);
    set(4, "caution", 45);
    set(5, "", 0);            // a torrent row: nothing judged
    rows[1].fakeReasons = {QStringLiteral("multiple_names")};
    model.setResults(std::move(rows));

    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    proxy.setSortRole(Qt::UserRole);
    proxy.sort(SearchResultsModel::ColConfidence, Qt::AscendingOrder);
    QStringList order;
    for (int row = 0; row < proxy.rowCount(); ++row) {
        order.push_back(QStringLiteral("%1:%2").arg(
            proxy.index(row, SearchResultsModel::ColFileName).data().toString(),
            proxy.index(row, SearchResultsModel::ColConfidence).data().toString()));
    }
    // Worst first; inside a band the higher score is worse
    QCOMPARE(order, (QStringList{QStringLiteral(":"), QStringLiteral("likely_fake:Likely fake"),
                                 QStringLiteral("caution:Caution: 45%"),
                                 QStringLiteral("caution:Caution: 30%"),
                                 QStringLiteral("looks_good:Looks good"),
                                 QStringLiteral("genuine:Genuine")}));

    // The cell says why, and only that cell takes the colour
    const QModelIndex caution = model.index(1, SearchResultsModel::ColConfidence);
    QVERIFY(caution.data(Qt::ToolTipRole).toString().contains(QStringLiteral("different content")));
    QVERIFY(caution.data(Qt::ForegroundRole).value<QColor>().isValid());
    QVERIFY(!model.index(0, SearchResultsModel::ColConfidence).data(Qt::ForegroundRole).isValid());
    QCOMPARE(model.headerData(SearchResultsModel::ColConfidence, Qt::Horizontal).toString(),
             QStringLiteral("Confidence"));
}

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

void tst_ListSorting::sourceStatusFollowsMfc()
{
    // MFC GetDownloadStateDisplayString (BaseClient.cpp:2434-2478); the cells showed the
    // wire tokens "OnQueue", "NoNeededParts", "LowToLowIp".
    DownloadRow file;
    file.hash = QStringLiteral("file");
    file.status = QStringLiteral("ready");

    const auto source = [](const char* hash, const char* state) {
        SourceRow s;
        s.userHash = QString::fromLatin1(hash);
        s.downloadState = QString::fromLatin1(state);
        return s;
    };
    SourceRow full = source("b", "OnQueue");
    full.remoteQueueFull = true;
    SourceRow cache = source("g", "Downloading");
    cache.sourceFrom = 8;   // SourceFrom::HttpCache
    SourceRow other = source("h", "OnQueue");
    other.a4af = true;
    other.noNeededHere = true;
    other.hasOtherRequests = true;
    other.otherFileName = QStringLiteral("other.avi");
    other.transferredDown = 5000;
    other.datarate = 100;
    other.partMap = QByteArray(3, '\2');

    DownloadListModel model;
    model.setDownloads({file});
    model.setSources(QStringLiteral("file"),
                     {source("a", "OnQueue"), full, source("c", "Downloading"),
                      source("d", "NoNeededParts"), source("e", "LowToLowIp"),
                      source("f", "None"), cache, other});
    const QModelIndex parent = model.index(0, 0);
    const auto text = [&](int row, int column = DownloadListModel::ColStatus) {
        return model.index(row, column, parent).data().toString();
    };

    thePrefs.setShowExtControls(false);
    QCOMPARE(text(0), QStringLiteral("On Queue"));
    QCOMPARE(text(1), QStringLiteral("Queue Full"));
    QCOMPARE(text(2), QStringLiteral("Transferring"));
    QCOMPARE(text(3), QStringLiteral("No needed parts"));
    QCOMPARE(text(4), QStringLiteral("Cannot connect LowID to LowID"));
    QCOMPARE(text(5), QString());
    QCOMPARE(text(6), QStringLiteral("Transferring (HTTP Cache)"));
    QCOMPARE(text(7), QStringLiteral("Asked for another file"));
    QCOMPARE(text(0, DownloadListModel::ColFileName), QStringLiteral("(Unknown)"));

    // The A4AF row: no figures, and the bare grey bar
    QCOMPARE(text(7, DownloadListModel::ColCompleted), QString());
    QCOMPARE(text(7, DownloadListModel::ColSpeed), QString());
    QVERIFY(model.index(7, DownloadListModel::ColProgress, parent)
                .data(DownloadListModel::PartMapRole).toByteArray().isEmpty());

    thePrefs.setShowExtControls(true);
    QCOMPARE(text(7), QStringLiteral("Asked for another file (No needed parts): \"other.avi\"*"));
}

void tst_ListSorting::a4afSourcesStayBelowAvailableOnes()
{
    // MFC SortProc returns on the item type before the direction is applied
    // (DownloadListCtrl.cpp:1636-1637).
    DownloadRow file;
    file.hash = QStringLiteral("file");
    file.status = QStringLiteral("ready");

    SourceRow a4af;
    a4af.userHash = QStringLiteral("x");
    a4af.userName = QStringLiteral("aaa");
    a4af.a4af = true;
    SourceRow early;
    early.userHash = QStringLiteral("y");
    early.userName = QStringLiteral("bbb");
    SourceRow late;
    late.userHash = QStringLiteral("z");
    late.userName = QStringLiteral("ccc");

    DownloadListModel model;
    model.setDownloads({file});
    model.setSources(QStringLiteral("file"), {a4af, late, early});

    DownloadSortProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setSortRole(Qt::UserRole);
    const auto names = [&proxy] {
        const QModelIndex parent = proxy.index(0, 0);
        QStringList out;
        for (int row = 0; row < proxy.rowCount(parent); ++row)
            out << proxy.index(row, 0, parent).data().toString();
        return out;
    };

    proxy.sort(DownloadListModel::ColFileName, Qt::AscendingOrder);
    QCOMPARE(names(), (QStringList{u"bbb"_s, u"ccc"_s, u"aaa"_s}));
    proxy.sort(DownloadListModel::ColFileName, Qt::DescendingOrder);
    QCOMPARE(names(), (QStringList{u"ccc"_s, u"bbb"_s, u"aaa"_s}));
}

void tst_ListSorting::sourcesSortByQueueRankWithDownloadingFirst()
{
    // MFC Compare case 7 (DownloadListCtrl.cpp:1794-1807): transferring sources
    // ahead of QR 1, Queue Full after the highest rank, A4AF last either way.
    DownloadRow file;
    file.hash = QStringLiteral("file");
    file.status = QStringLiteral("ready");

    const auto source = [](const QString& name, const QString& state, int64_t rank = 0) {
        SourceRow s;
        s.userHash = name;
        s.userName = name;
        s.downloadState = state;
        s.remoteQueueRank = rank;
        return s;
    };
    SourceRow full = source(u"full"_s, u"OnQueue"_s);
    full.remoteQueueFull = true;
    SourceRow a4af = source(u"a4af"_s, u"Downloading"_s);
    a4af.a4af = true;

    DownloadListModel model;
    model.setDownloads({file});
    model.setSources(QStringLiteral("file"),
                     {a4af, source(u"qr7"_s, u"OnQueue"_s, 7), source(u"noparts"_s, u"NoNeededParts"_s),
                      full, source(u"downloading"_s, u"Downloading"_s),
                      source(u"qr1"_s, u"OnQueue"_s, 1)});

    DownloadSortProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setSortRole(Qt::UserRole);
    const auto names = [&proxy] {
        const QModelIndex parent = proxy.index(0, 0);
        QStringList out;
        for (int row = 0; row < proxy.rowCount(parent); ++row)
            out << proxy.index(row, 0, parent).data().toString();
        return out;
    };

    proxy.sort(DownloadListModel::ColPriority, Qt::AscendingOrder);
    QCOMPARE(names(), (QStringList{u"downloading"_s, u"qr1"_s, u"qr7"_s, u"full"_s, u"noparts"_s,
                                   u"a4af"_s}));
    proxy.sort(DownloadListModel::ColPriority, Qt::DescendingOrder);
    QCOMPARE(names(), (QStringList{u"noparts"_s, u"full"_s, u"qr7"_s, u"qr1"_s, u"downloading"_s,
                                   u"a4af"_s}));
}

void tst_ListSorting::remainingAndSeenCompleteFollowMfc()
{
    // MFC DownloadListCtrl.cpp:2062-2100
    DownloadRow known;
    known.hash = QStringLiteral("known");
    known.status = QStringLiteral("ready");
    known.fileSize = 3 * 1024 * 1024;
    known.completedSize = 1024 * 1024;
    known.timeRemaining = 303;
    known.completeSourcesLo = 2;
    known.completeSourcesHi = 5;
    known.downTransferred = 2048;
    DownloadRow unknown = known;
    unknown.hash = QStringLiteral("unknown");
    unknown.timeRemaining = -1;
    unknown.completeSourcesLo = 0;
    DownloadRow done = known;
    done.hash = QStringLiteral("done");
    done.status = QStringLiteral("complete");
    done.completeSourcesLo = 5;

    DownloadListModel model;
    model.setDownloads({known, unknown, done});
    const auto text = [&model](int row, int column) {
        return model.index(row, column).data().toString();
    };

    QCOMPARE(text(0, DownloadListModel::ColRemaining),
             QStringLiteral("5:03 mins (%1)").arg(formatByteSize(2 * 1024 * 1024)));
    QVERIFY(text(1, DownloadListModel::ColRemaining).startsWith(QStringLiteral("? (")));
    QCOMPARE(text(2, DownloadListModel::ColRemaining), QString());

    QCOMPARE(text(0, DownloadListModel::ColSeenComplete), QStringLiteral("Never (2 - 5)"));
    QCOMPARE(text(1, DownloadListModel::ColSeenComplete), QStringLiteral("Never (< 5)"));
    QCOMPARE(text(2, DownloadListModel::ColSeenComplete), QStringLiteral("Never (5)"));
    QCOMPARE(text(0, DownloadListModel::ColLastReception), QStringLiteral("Never"));
    QCOMPARE(text(0, DownloadListModel::ColAddedOn), QStringLiteral("?"));
    QCOMPARE(text(0, DownloadListModel::ColTransferred), formatByteSize(2048));

    // An unknown time sorts as the longest
    const auto key = [&model](int row) {
        return model.index(row, DownloadListModel::ColRemaining).data(Qt::UserRole).toLongLong();
    };
    QVERIFY(key(1) > key(0));
}

void tst_ListSorting::knownClientsFollowMfc()
{
    // MFC ClientListCtrl.cpp:166-200
    ClientRow c;
    c.userHash = QStringLiteral("a");
    c.downloadState = QStringLiteral("OnQueue");
    c.transferredUp = 111;       // this session's counters are not what the list shows
    c.transferredDown = 222;
    c.uploadedTotal = 4096;

    ClientListModel model(ClientListMode::KnownClients);
    model.setClients({c});
    const auto text = [&model](int column) { return model.index(0, column).data().toString(); };

    QCOMPARE(model.headerData(2, Qt::Horizontal).toString(), QStringLiteral("Transferred Up"));
    QCOMPARE(text(0), QStringLiteral("(Unknown)"));
    QCOMPARE(text(2), formatByteSize(4096));
    QCOMPARE(text(3), QStringLiteral("On Queue"));
    QCOMPARE(text(4), QString());                 // no credits: blank
    QCOMPARE(text(5), QStringLiteral("Unknown"));
    QCOMPARE(text(6), QStringLiteral("No"));
}

// MFC GetUploadStateDisplayString (BaseClient.cpp:2480-2510): the daemon sent English
// text, which no translation could reach, and the column sorted by that text.
void tst_ListSorting::uploadStateIsWordedAndSortedByState()
{
    const bool ext = thePrefs.showExtControls();
    const auto restore = qScopeGuard([ext] { thePrefs.setShowExtControls(ext); });

    std::vector<ClientRow> rows(5);
    const char* tokens[] = {"OnQueue", "Transferring", "Standby", "Banned", "Connecting"};
    for (std::size_t i = 0; i < rows.size(); ++i) {
        rows[i].userHash = QString::number(i);
        rows[i].uploadState = QString::fromLatin1(tokens[i]);
    }
    rows[1].uploadStalled = true;

    ClientListModel model(ClientListMode::KnownClients);
    model.setClients(rows);
    const auto text = [&model](int row) { return model.index(row, 1).data().toString(); };

    thePrefs.setShowExtControls(false);
    QCOMPARE(text(0), QStringLiteral("On Queue"));
    QCOMPARE(text(1), QStringLiteral("Transferring"));   // stalled shows in advanced mode only
    QCOMPARE(text(2), QStringLiteral("Standby"));
    QCOMPARE(text(3), QStringLiteral("Banned"));
    QCOMPARE(text(4), QStringLiteral("Connecting"));
    thePrefs.setShowExtControls(true);
    QCOMPARE(text(1), QStringLiteral("Stalled! Waiting for block request."));

    // uploading, queued, connecting, banned — not alphabetical
    const auto rank = [&model](int row) { return model.index(row, 1).data(Qt::UserRole).toInt(); };
    QVERIFY(rank(1) < rank(0));
    QVERIFY(rank(0) < rank(4));
    QVERIFY(rank(4) < rank(3));
    QCOMPARE(rank(1), rank(2));
}

// MFC CSearchList::RemoveResult takes one list entry: a name row goes alone.
void tst_ListSorting::removingASearchNameKeepsTheFile()
{
    SearchResultRow file;
    file.hash = QStringLiteral("aa");
    file.fileName = QStringLiteral("best name.avi");
    for (const char* name : {"best name.avi", "other name.avi", "third.avi"}) {
        SearchChildRow child;
        child.fileName = QString::fromLatin1(name);
        file.children.push_back(child);
    }
    SearchResultsModel model;
    model.setResults({file});
    const QModelIndex parent = model.index(0, 0);
    QCOMPARE(model.rowCount(parent), 3);

    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    model.removeChild(0, 1);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.rowCount(parent), 2);
    QCOMPARE(model.resultAt(0)->children[1].fileName, QStringLiteral("third.avi"));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.at(0).at(0).toModelIndex(), parent);

    model.removeChild(0, 7);   // out of range: nothing happens
    model.removeChild(3, 0);
    QCOMPARE(model.rowCount(parent), 2);
}

void tst_ListSorting::downloadingClientsCarryThePartBar()
{
    // MFC DownloadClientsCtrl.cpp:150-155 draws the bar; the cell held a number
    ClientRow c;
    c.userHash = QStringLiteral("a");
    c.availPartCount = 2;
    c.partMap = QByteArray::fromHex("010200");
    c.reqFileSize = 30'000'000;

    ClientListModel model(ClientListMode::Downloading);
    model.setClients({c});
    const QModelIndex cell = model.index(0, 4);
    QCOMPARE(cell.data().toString(), QString());
    QCOMPARE(cell.data(ClientListModel::PartMapRole).toByteArray(), c.partMap);
    QCOMPARE(cell.data(ClientListModel::FileSizeRole).toLongLong(), qint64(30'000'000));
    QCOMPARE(cell.data(Qt::UserRole).toInt(), 2);   // still sorts by part count
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

void tst_ListSorting::downloadPriorityLabelsMatchMfc()
{
    // The column printed the wire token: "veryHigh", "normal", "Auto [normal]".
    // MFC DownloadListCtrl.cpp:2038-2056 localises the label and has no auto form
    // for either end of the scale. Level 3 is "Very High" here, not the upload
    // side's "Release"; MFC blanks it, but the port's own menus offer it.
    QCOMPARE(downloadPriorityText(QStringLiteral("veryHigh"), false), QStringLiteral("Very High"));
    QCOMPARE(downloadPriorityText(QStringLiteral("veryHigh"), true), QStringLiteral("Very High"));
    QCOMPARE(downloadPriorityText(QStringLiteral("veryLow"), true), QStringLiteral("Very Low"));
    QCOMPARE(downloadPriorityText(QStringLiteral("low"), true), QStringLiteral("Auto [Lo]"));
    QCOMPARE(downloadPriorityText(QStringLiteral("normal"), true), QStringLiteral("Auto [No]"));
    QCOMPARE(downloadPriorityText(QStringLiteral("high"), true), QStringLiteral("Auto [Hi]"));
    QCOMPARE(downloadPriorityText(QStringLiteral("high"), false), QStringLiteral("High"));
    // The daemon resolves auto per file, so "auto" should never arrive -- but if it
    // does it must not reach the cell verbatim.
    QCOMPARE(downloadPriorityText(QStringLiteral("auto"), false), QStringLiteral("Normal"));

    // ... and the model asks for it, rather than formatting its own.
    DownloadListModel model;
    std::vector<DownloadRow> rows;
    DownloadRow row;
    row.hash = QStringLiteral("a");
    row.fileName = QStringLiteral("a");
    row.status = QStringLiteral("paused");
    row.priority = QStringLiteral("veryHigh");
    row.fileSize = 1000;
    rows.push_back(row);
    model.setDownloads(std::move(rows));
    QCOMPARE(model.index(0, DownloadListModel::ColPriority).data().toString(),
             QStringLiteral("Very High"));
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

    thePrefs.setDepth3D(0);   // flat palette and no shading
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

    // Round: had parts turn MFC's 3D grey
    thePrefs.setDepth3D(5);
    bar.greyed = false;
    image = render(bar);
    QCOMPARE(image.pixelColor(50, 5), QColor(104, 104, 104));
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

// MFC KadContactListCtrl.cpp:53-55, :95-125, :203-237
void tst_ListSorting::kadContactColumnsFollowMfc()
{
    KadContactsModel model;
    QCOMPARE(model.headerData(KadContactsModel::ColClientId, Qt::Horizontal).toString(),
             QStringLiteral("ID"));
    QCOMPARE(model.headerData(KadContactsModel::ColStatus, Qt::Horizontal).toString(),
             QStringLiteral("Type"));

    KadContactRow older, newer;
    older.clientId = QStringLiteral("AA");
    older.type = 2;
    older.version = 8;
    newer.clientId = QStringLiteral("BB");
    newer.type = 2;
    newer.version = 9;
    model.setContacts({older, newer});

    // deliberate: the icon stays on Type (MFC: ID), where the flag delegate widens it
    QVERIFY(model.index(0, KadContactsModel::ColStatus).data(Qt::DecorationRole).isValid());
    QVERIFY(!model.index(0, KadContactsModel::ColClientId).data(Qt::DecorationRole).isValid());
    QCOMPARE(model.index(0, KadContactsModel::ColStatus).data().toString(), QStringLiteral("2(8)"));
    // same type: the version breaks the tie
    QVERIFY(model.index(0, KadContactsModel::ColStatus).data(Qt::UserRole).toInt()
            < model.index(1, KadContactsModel::ColStatus).data(Qt::UserRole).toInt());
}

// MFC KadSearchListCtrl.cpp:124-151, CSearch::GetTypeName
void tst_ListSorting::kadSearchRowsFollowMfc()
{
    KadSearchesModel model;
    QCOMPARE(model.headerData(KadSearchesModel::ColNumber, Qt::Horizontal).toString(),
             QStringLiteral("Number"));

    QCOMPARE(KadSearchesModel::typeText(0), QStringLiteral("Node Lookup"));
    QCOMPARE(KadSearchesModel::typeText(11), QStringLiteral("Node Lookup"));
    QCOMPARE(KadSearchesModel::typeText(2), QStringLiteral("Search Sources"));
    QCOMPARE(KadSearchesModel::typeText(3), QStringLiteral("Search Keywords"));
    QCOMPARE(KadSearchesModel::typeText(6), QStringLiteral("Store Keyword"));
    QCOMPARE(KadSearchesModel::typeText(9), QStringLiteral("Unknown"));
    QCOMPARE(KadSearchesModel::typeIconName(5), QStringLiteral("KadStoreFile.ico"));
    QVERIFY(KadSearchesModel::typeIconName(8).isEmpty());   // Find Buddy has none

    KadSearchRow keyword, buddy;
    keyword.searchId = 1;
    keyword.typeId = 3;
    buddy.searchId = 2;
    buddy.typeId = 8;
    buddy.stopping = true;
    model.setSearches({keyword, buddy});

    const auto cell = [&](int row, int col) { return model.index(row, col); };
    QVERIFY(cell(0, KadSearchesModel::ColNumber).data(Qt::DecorationRole).isValid());
    QVERIFY(!cell(1, KadSearchesModel::ColNumber).data(Qt::DecorationRole).isValid());
    QCOMPARE(cell(0, KadSearchesModel::ColStatus).data().toString(), QStringLiteral("Active"));
    QCOMPARE(cell(1, KadSearchesModel::ColStatus).data().toString(), QStringLiteral("Stopping"));
    // Type sorts by the id, not by its name ("Find Buddy" < "Search Keywords" as text)
    QVERIFY(cell(0, KadSearchesModel::ColType).data(Qt::UserRole).toInt()
            < cell(1, KadSearchesModel::ColType).data(Qt::UserRole).toInt());
}

// ---------------------------------------------------------------------------
// controls/BarShader.h, DownloadProgressDelegate.h — MFC CBarShader / DrawStatusBar
// ---------------------------------------------------------------------------

namespace {

QImage renderBar(int width, int height, const std::function<void(QPainter&, const QRect&)>& paint)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    paint(painter, image.rect());
    return image;
}

} // namespace

void tst_ListSorting::barShaderShadesRoundBars()
{
    // MFC BuildModifiers: depth 5 starts at sin(0) (black edge), the middle is always 1
    const std::vector<float> mods = BarShader::shadeModifiers(16, 5);
    QCOMPARE(mods.size(), size_t(8));
    QCOMPARE(mods.front(), 0.0f);
    QCOMPARE(mods.back(), 1.0f);
    QVERIFY(BarShader::shadeModifiers(16, 1).front() > 0.8f);

    BarShader shader(100);
    shader.fill(QColor(0, 224, 0));
    QImage round = renderBar(100, 16, [&](QPainter& p, const QRect& r) { shader.draw(p, r, false, 5); });
    QCOMPARE(round.pixelColor(50, 0), QColor(0, 0, 0));
    QCOMPARE(round.pixelColor(50, 15), QColor(0, 0, 0));
    QCOMPARE(round.pixelColor(50, 7), QColor(0, 224, 0));
    QCOMPARE(round.pixelColor(50, 8), QColor(0, 224, 0));

    QImage flat = renderBar(100, 16, [&](QPainter& p, const QRect& r) { shader.draw(p, r, true, 5); });
    QCOMPARE(flat.pixelColor(50, 0), QColor(0, 224, 0));

    // Black is never shaded, and the preview-style level 0 is flat
    shader.fill(QColor(0, 0, 0));
    round = renderBar(100, 16, [&](QPainter& p, const QRect& r) { shader.draw(p, r, false, 5); });
    QCOMPARE(round.pixelColor(50, 7), QColor(0, 0, 0));
}

void tst_ListSorting::barShaderBlendsSubPixelSpans()
{
    // 100 bytes per pixel: half white, half black averages to mid grey
    BarShader shader(1000);
    shader.fill(Qt::black);
    shader.fillRange(0, 50, Qt::white);
    shader.fillRange(500, 1000, QColor(255, 0, 0));
    const QImage image = renderBar(10, 4, [&](QPainter& p, const QRect& r) { shader.draw(p, r, true, 0); });
    QCOMPARE(image.pixelColor(0, 1), QColor(128, 128, 128));
    QCOMPARE(image.pixelColor(3, 1), QColor(0, 0, 0));
    QCOMPARE(image.pixelColor(7, 1), QColor(255, 0, 0));

    // A later range overwrites, and the colour after it resumes
    shader.fillRange(600, 700, QColor(0, 0, 255));
    const QImage over = renderBar(10, 4, [&](QPainter& p, const QRect& r) { shader.draw(p, r, true, 0); });
    QCOMPARE(over.pixelColor(6, 1), QColor(0, 0, 255));
    QCOMPARE(over.pixelColor(7, 1), QColor(255, 0, 0));
}

void tst_ListSorting::downloadBarDrawsDataWhereItSits()
{
    // Part 0 complete; part 1 has its first half downloaded, its second half a gap
    // one source offers. 200 px, so each part is 100 px.
    const auto part = static_cast<qint64>(PARTSIZE);
    DownloadBarData bar;
    bar.fileSize = 2 * part;
    bar.partMap = QByteArray("\x00\x02", 2);
    bar.partFreq = {0, 1};
    bar.gaps = {part + part / 2, 2 * part - 1};

    thePrefs.setDepth3D(0);
    const auto render = [&](bool detail) {
        return renderBar(200, 16, [&](QPainter& p, const QRect& r) {
            paintDownloadBar(p, r, bar, 75.0, false, detail);
        });
    };

    // MFC: downloaded bytes are "have" black wherever they are
    QImage image = render(false);
    QCOMPARE(image.pixelColor(50, 8), QColor(0, 0, 0));
    QCOMPARE(image.pixelColor(125, 8), QColor(0, 0, 0));
    QCOMPARE(image.pixelColor(175, 8), QColor(0, 210, 255));
    QCOMPARE(image.pixelColor(10, 1), QColor(0, 150, 0));      // strip, flat green
    QCOMPARE(image.pixelColor(190, 1), QColor(224, 224, 224)); // flat track
    QCOMPARE(image.pixelColor(100, 1), QColor(0, 150, 0));     // no dots

    // MorphXT: the started part shows its data in grey, requested in green, plus a dot
    image = render(true);
    QCOMPARE(image.pixelColor(50, 8), QColor(0, 0, 0));
    QCOMPARE(image.pixelColor(125, 8), QColor(160, 160, 160));
    QCOMPARE(image.pixelColor(175, 8), QColor(0, 210, 255));
    QCOMPARE(image.pixelColor(100, 1), QColor(128, 128, 128));
    bar.partMap = QByteArray("\x00\xff", 2);
    image = render(true);
    QCOMPARE(image.pixelColor(125, 8), QColor(0, 224, 0));

    // Round: no grey track under the strip, the shaded bar shows through
    thePrefs.setDepth3D(5);
    image = render(false);
    QVERIFY(image.pixelColor(190, 1) != QColor(224, 224, 224));
    QCOMPARE(image.pixelColor(10, 1), QColor(0, 224, 0));
    thePrefs.setDepth3D(0);
}

void tst_ListSorting::barRangesAreCappedKeepingTheEnds()
{
    std::vector<std::pair<uint64, uint64>> ranges;
    for (uint64 i = 0; i < 1500; ++i)
        ranges.emplace_back(i * 10, i * 10 + 4);
    // one wide distance must survive the merge
    ranges.back() = {1'000'000, 1'000'004};

    const QCborArray packed = Ipc::packBarRanges(ranges);
    QCOMPARE(packed.size(), 2 * Ipc::kMaxBarRanges);
    QCOMPARE(packed.first().toInteger(), 0);
    QCOMPARE(packed.last().toInteger(), 1'000'004);
    QCOMPARE(packed.at(packed.size() - 2).toInteger(), 1'000'000);
    for (qsizetype i = 1; i < packed.size(); ++i)
        QVERIFY(packed.at(i - 1).toInteger() <= packed.at(i).toInteger());

    QCOMPARE(Ipc::packBarRanges({{5, 9}}).size(), 2);
}

QTEST_MAIN(tst_ListSorting)
#include "tst_ListSorting.moc"
