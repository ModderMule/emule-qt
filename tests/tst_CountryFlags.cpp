/// @file tst_CountryFlags.cpp
/// @brief GUI half of IP2Country: flag icons, the Country column and its default
///        visibility, the models' "cc" plumbing, and header layouts surviving the extra column.

#include "controls/AbstractListView.h"
#include "controls/ClientListModel.h"
#include "controls/KadContactsModel.h"
#include "controls/ServerListModel.h"
#include "utils/AddressSortKey.h"
#include "utils/CountryFlags.h"

#include <QCborArray>
#include <QCborMap>
#include <QDirIterator>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;

class tst_CountryFlags : public QObject {
    Q_OBJECT

private slots:
    void init();

    void flag_knownAndUnknownCodes();
    void flag_everyIsoTerritoryHasOne();
    void withFlag_widthAndOffSwitch();
    void columnText_isLongName();
    void clientModel_countryColumnAndDecoration();
    void serverModel_parsesCc();
    void kadModel_countryColumn();
    void countryColumn_hiddenByDefaultSavedLayoutWins();
    void sharedListColumn_hiddenByDefaultAndExtOnly();
    void clientModel_transferAndRatioColumns();
    void clientModel_peerIp();
    void clientModel_hostnameEligibility();
    void clientModel_hostnameSurvivesRefresh();
    void restoreHeader_keepsLayoutWhenColumnAppended();
    void delegate_widensComposedDecoration();
};

void tst_CountryFlags::init()
{
    CountryFlags::setSettings(true);
}

void tst_CountryFlags::flag_knownAndUnknownCodes()
{
    QVERIFY(!CountryFlags::flag(QStringLiteral("DE")).isNull());
    QVERIFY(!CountryFlags::flag(QStringLiteral("de")).isNull());
    QVERIFY(!CountryFlags::flag(QStringLiteral("GB")).isNull());
    QVERIFY(!CountryFlags::flag(QStringLiteral("UK")).isNull());   // alias of GB
    QVERIFY(!CountryFlags::flag(QStringLiteral("XK")).isNull());
    QVERIFY(!CountryFlags::flag(QStringLiteral("YU")).isNull());   // old MorphXT-only code
    // HiDPI variants
    const QList<QSize> sizes = CountryFlags::flag(QStringLiteral("DE")).availableSizes();
    QVERIFY(sizes.contains(QSize(18, 16)));
    QVERIFY(sizes.contains(QSize(36, 32)));
    QVERIFY(sizes.contains(QSize(54, 48)));
    QVERIFY(CountryFlags::flag(QStringLiteral("QQ")).isNull());
    QVERIFY(CountryFlags::flag(QString()).isNull());
    QVERIFY(CountryFlags::flag(QStringLiteral("DEU")).isNull());
}

void tst_CountryFlags::flag_everyIsoTerritoryHasOne()
{
    // Every territory Qt knows by a two-letter code; a few Qt-only pseudo codes
    // and territories MaxMind never returns are allowed to lack a flag.
    const QSet<QString> allowedMissing = {
        QStringLiteral("AC"), QStringLiteral("CP"), QStringLiteral("DG"), QStringLiteral("EA"),
        QStringLiteral("IC"), QStringLiteral("TA"), QStringLiteral("QO"),
        QStringLiteral("EZ"), QStringLiteral("CQ"), QStringLiteral("419"),
        QStringLiteral("150"), QStringLiteral("001")};
    QStringList missing;
    for (int t = QLocale::Afghanistan; t <= QLocale::LastTerritory; ++t) {
        const QString code = QLocale::territoryToCode(static_cast<QLocale::Territory>(t));
        if (code.size() != 2 || allowedMissing.contains(code))
            continue;
        if (CountryFlags::flag(code).isNull())
            missing << code;
    }
    QVERIFY2(missing.size() <= 3, qPrintable(missing.join(u' ')));
}

void tst_CountryFlags::withFlag_widthAndOffSwitch()
{
    const QIcon base(QStringLiteral(":/icons/ClientDefault.ico"));
    QVERIFY(!base.isNull());

    const QIcon composed = CountryFlags::withFlag(base, QStringLiteral("SE"));
    const QSize s = composed.availableSizes().value(0);
    QVERIFY(s.isValid());
    QCOMPARE(s.width() * 16, 36 * s.height());   // 16 + 2 + 18 wide, 16 high

    // Unknown country still reserves the slot, so names stay aligned
    const QSize blank = CountryFlags::withFlag(base, QString()).availableSizes().value(0);
    QCOMPARE(blank.width() * 16, 36 * blank.height());

    // No base icon: just the flag slot
    const QSize alone = CountryFlags::withFlag(QIcon(), QStringLiteral("SE")).availableSizes().value(0);
    QCOMPARE(alone.width() * 16, 18 * alone.height());

    CountryFlags::setSettings(false);
    QCOMPARE(CountryFlags::withFlag(base, QStringLiteral("SE")).cacheKey(), base.cacheKey());
}

void tst_CountryFlags::columnText_isLongName()
{
    QCOMPARE(CountryFlags::columnText(QStringLiteral("se")), QStringLiteral("Sweden"));
    QCOMPARE(CountryFlags::columnText(QStringLiteral("SE")), QStringLiteral("Sweden"));
    QCOMPARE(CountryFlags::columnText(QString()), QString());
    QCOMPARE(CountryFlags::tooltip(QStringLiteral("SE")), QStringLiteral("Sweden (SE)"));
    QCOMPARE(CountryFlags::tooltip(QString()), QString());
}

void tst_CountryFlags::clientModel_countryColumnAndDecoration()
{
    CountryFlags::setSettings(true);
    for (const auto mode : {ClientListMode::Uploading, ClientListMode::Downloading,
                            ClientListMode::OnQueue, ClientListMode::KnownClients}) {
        ClientListModel model(mode);
        ClientRow row;
        row.userName = QStringLiteral("peer");
        row.cc = QStringLiteral("JP");
        model.setClients({row});

        const int col = model.countryColumn();
        QCOMPARE(col, model.sharedListColumn() - 1);
        QCOMPARE(model.headerData(col, Qt::Horizontal).toString(), QStringLiteral("Country"));
        QCOMPARE(model.data(model.index(0, col)).toString(), QStringLiteral("Japan"));
        QCOMPARE(model.data(model.index(0, col), Qt::UserRole).toString(), QStringLiteral("Japan"));
        const auto icon = model.data(model.index(0, 0), Qt::DecorationRole).value<QIcon>();
        const QSize s = icon.availableSizes().value(0);
        QVERIFY(s.width() > s.height());
        QCOMPARE(model.data(model.index(0, 0), Qt::ToolTipRole).toString(),
                 QStringLiteral("Japan (JP)"));
    }
}

void tst_CountryFlags::serverModel_parsesCc()
{
    CountryFlags::setSettings(true);
    ServerListModel model;
    QCborMap m;
    m.insert(QStringLiteral("name"), QStringLiteral("srv"));
    m.insert(QStringLiteral("address"), QStringLiteral("81.2.69.160"));
    m.insert(QStringLiteral("cc"), QStringLiteral("GB"));
    model.refreshFromCborArray(QCborArray{m});
    QCOMPARE(model.data(model.index(0, ServerListModel::ColCountry)).toString(), QStringLiteral("United Kingdom"));
    QVERIFY(!model.data(model.index(0, ServerListModel::ColName), Qt::DecorationRole)
                 .value<QIcon>().isNull());

    CountryFlags::setSettings(false);
    QVERIFY(!model.data(model.index(0, ServerListModel::ColName), Qt::DecorationRole).isValid());
}

void tst_CountryFlags::kadModel_countryColumn()
{
    CountryFlags::setSettings(true);
    KadContactsModel model;
    KadContactRow row;
    row.cc = QStringLiteral("SE");
    model.setContacts({row});
    QCOMPARE(model.columnCount(), int(KadContactsModel::ColCount));
    QCOMPARE(model.data(model.index(0, KadContactsModel::ColCountry)).toString(), QStringLiteral("Sweden"));
}

void tst_CountryFlags::countryColumn_hiddenByDefaultSavedLayoutWins()
{
    ClientListModel model(ClientListMode::KnownClients);
    ListTreeView view;
    view.setModel(&model);
    view.bindColumns(QStringLiteral("tst_countryFlags_known"), {}, {model.countryColumn()});
    QVERIFY(view.header()->isSectionHidden(model.countryColumn()));

    // Shown from the header menu, then saved: a re-bind must keep it shown
    view.header()->setSectionHidden(model.countryColumn(), false);
    theUiState.captureHeaderState(view.header(), QStringLiteral("tst_countryFlags_known"));

    ListTreeView other;
    other.setModel(&model);
    other.bindColumns(QStringLiteral("tst_countryFlags_known"), {}, {model.countryColumn()});
    QVERIFY(!other.header()->isSectionHidden(model.countryColumn()));
}

void tst_CountryFlags::sharedListColumn_hiddenByDefaultAndExtOnly()
{
    ClientListModel model(ClientListMode::Downloading);
    ClientRow open, closed, http;
    closed.noViewShared = true;
    http.isUrl = true;
    model.setClients({open, closed, http});
    const int col = model.sharedListColumn();
    QCOMPARE(col, model.columnCount() - 2);
    QCOMPARE(model.headerData(col, Qt::Horizontal).toString(), QStringLiteral("Shared List"));
    QCOMPARE(model.data(model.index(0, col)).toString(), QStringLiteral("Yes"));
    QCOMPARE(model.data(model.index(1, col)).toString(), QStringLiteral("No"));
    QVERIFY(!model.data(model.index(2, col)).isValid());

    // A layout saved before the column existed: the column still starts hidden
    const QString key = QStringLiteral("tst_countryFlags_sharedList");
    {
        QStandardItemModel oldModel(0, col);
        QHeaderView oldHeader(Qt::Horizontal);
        oldHeader.setModel(&oldModel);
        oldHeader.resizeSection(1, 123);
        theUiState.captureHeaderState(&oldHeader, key);
    }
    QCOMPARE(theUiState.headerStateSectionCount(key), col);
    ListTreeView view;
    view.setModel(&model);
    view.bindColumns(key, {}, {model.countryColumn(), col});
    QCOMPARE(view.header()->sectionSize(1), 123);
    QVERIFY(view.header()->isSectionHidden(col));

    // Shown by the user, then "Show more controls" turned off: hidden again
    view.setExtColumns({col}, true);
    view.header()->setSectionHidden(col, false);
    view.applyExtColumns(false);
    QVERIFY(view.header()->isSectionHidden(col));
}

// Uploading and On Queue append Uploaded, Downloaded, Ratio, Session Ratio after
// Shared List, Uploading also Slot #; all are "Show more controls" columns. Software
// (MorphXT) comes last and shows always.
void tst_CountryFlags::clientModel_transferAndRatioColumns()
{
    for (const auto mode : {ClientListMode::Uploading, ClientListMode::OnQueue}) {
        ClientListModel model(mode);
        ClientRow row;
        row.uploadedTotal = 3 * 1024 * 1024;
        row.downloadedTotal = 0;
        row.uploadFileSize = 1000;
        row.uploadFileAllTimeTransferred = 2500;
        row.uploadFileTransferred = 500;
        model.setClients({row});

        const int first = model.sharedListColumn() + 1;
        const bool uploading = mode == ClientListMode::Uploading;
        QCOMPARE(model.columnCount(), first + (uploading ? 6 : 5) + 1);   // + Peer IP
        QCOMPARE(model.headerData(first, Qt::Horizontal).toString(), QStringLiteral("Uploaded"));
        QCOMPARE(model.headerData(first + 3, Qt::Horizontal).toString(), QStringLiteral("Session Ratio"));
        QCOMPARE(model.data(model.index(0, first)).toString(), QStringLiteral("3.00 MB"));
        QCOMPARE(model.data(model.index(0, first + 1)).toString(), QString());   // 0 stays blank
        QCOMPARE(model.data(model.index(0, first + 2)).toString(), QStringLiteral("2.5"));
        QCOMPARE(model.data(model.index(0, first + 3)).toString(), QStringLiteral("0.5"));
        QCOMPARE(model.data(model.index(0, first + 2), Qt::UserRole).toDouble(), 2.5);

        std::vector<int> expected{first - 1, first, first + 1, first + 2, first + 3};
        if (uploading) {
            // MorphXT's Slot #: ext-only, but shown on a fresh layout
            expected.push_back(first + 4);
            QCOMPARE(model.headerData(first + 4, Qt::Horizontal).toString(), QStringLiteral("Slot #"));
            QCOMPARE(model.data(model.index(0, first + 4)).toString(), QString());   // no slot
            ClientRow slotted = row;
            slotted.slotNumber = 3;
            model.setClients({slotted});
            QCOMPARE(model.data(model.index(0, first + 4)).toString(), QStringLiteral("3"));
            QCOMPARE(model.data(model.index(0, first + 4), Qt::UserRole).toInt(), 3);
            const auto hidden = model.defaultHiddenColumns();
            QVERIFY(std::ranges::find(hidden, first + 4) == hidden.end());
        }
        QCOMPARE(model.extColumns(), expected);

        const int software = model.columnCount() - 2;   // Peer IP is last
        QCOMPARE(model.headerData(software, Qt::Horizontal).toString(), QStringLiteral("Software"));
        QCOMPARE(model.data(model.index(0, software)).toString(), QStringLiteral("Unknown"));
        ClientRow named = row;
        named.software = QStringLiteral("eMule v0.50a");
        model.setClients({named});
        QCOMPARE(model.data(model.index(0, software)).toString(), QStringLiteral("eMule v0.50a"));
        const auto hidden = model.defaultHiddenColumns();
        QVERIFY(std::ranges::find(hidden, software) == hidden.end());
    }
    // The other two tabs append Peer IP immediately after Shared List.
    ClientListModel known(ClientListMode::KnownClients);
    QCOMPARE(known.sharedListColumn(), known.columnCount() - 2);
    QCOMPARE(known.extColumns(), std::vector<int>{known.sharedListColumn()});
}

void tst_CountryFlags::clientModel_peerIp()
{
    for (const auto mode : {ClientListMode::Uploading, ClientListMode::Downloading,
                            ClientListMode::OnQueue, ClientListMode::KnownClients}) {
        ClientListModel model(mode);
        ClientRow ipv4, ipv6, unknown, anonymous, hoster;
        ipv4.addr = QStringLiteral("192.0.2.1");
        ipv6.addr = QStringLiteral("2001:db8::1");
        anonymous.addr = QStringLiteral("192.0.2.2");
        anonymous.anonymous = true;
        hoster.network = ClientNetwork::Hoster;
        hoster.addr = QStringLiteral("example.com");
        model.setClients({ipv4, ipv6, unknown, anonymous, hoster});
        const int col = model.peerIpColumn();
        QCOMPARE(col, model.columnCount() - 1);
        QCOMPARE(model.headerData(col, Qt::Horizontal).toString(), QStringLiteral("Peer IP"));
        QCOMPARE(model.index(0, col).data().toString(), ipv4.addr);
        QCOMPARE(model.index(1, col).data().toString(), ipv6.addr);
        QCOMPARE(model.index(2, col).data().toString(), QStringLiteral("Unknown"));
        QCOMPARE(model.index(3, col).data().toString(), QStringLiteral("Hidden"));
        QCOMPARE(model.index(3, col).data(Qt::UserRole).toString(), QString());
        QVERIFY(!model.index(4, col).data().isValid());
        const auto hidden = model.defaultHiddenColumns();
        QVERIFY(std::ranges::find(hidden, col) == hidden.end());
        const auto ext = model.extColumns();
        QVERIFY(std::ranges::find(ext, col) == ext.end());
    }
}

void tst_CountryFlags::clientModel_hostnameEligibility()
{
    ClientListModel model(ClientListMode::KnownClients);
    ClientRow peer;
    QVERIFY(!model.canResolveHostname(peer));
    for (const auto* invalid : {"example.com", "0.0.0.0", "::", "239.1.2.3", "255.255.255.255"}) {
        peer.addr = QString::fromLatin1(invalid);
        QVERIFY(!model.canResolveHostname(peer));
    }
    for (const auto* address : {"192.0.2.1", "2001:db8::1"}) {
        peer.addr = QString::fromLatin1(address);
        QVERIFY(model.canResolveHostname(peer));
    }
    peer.anonymous = true;
    QVERIFY(!model.canResolveHostname(peer));
    peer.anonymous = false;
    peer.isUrl = true;
    QVERIFY(!model.canResolveHostname(peer));
    peer.isUrl = false;
    peer.network = ClientNetwork::Hoster;
    QVERIFY(!model.canResolveHostname(peer));
    peer.network = ClientNetwork::Usenet;
    QVERIFY(!model.canResolveHostname(peer));
    peer.network = ClientNetwork::Bt;
    QVERIFY(model.canResolveHostname(peer));
}

void tst_CountryFlags::clientModel_hostnameSurvivesRefresh()
{
    ClientListModel model(ClientListMode::KnownClients);
    ClientRow peer, other;
    peer.addr = QStringLiteral("127.0.0.1");
    other.addr = QStringLiteral("192.0.2.1");
    other.anonymous = true; // never query an external address in this test
    model.setClients({peer});
    QCOMPARE(model.index(0, model.peerIpColumn()).data().toString(), peer.addr);
    QVERIFY(model.canResolveHostname(peer)); // off by default: no lookup was started
    model.setResolveHostnames(true);
    QVERIFY(!model.canResolveHostname(peer)); // duplicate queries are suppressed
    QVERIFY(model.index(0, model.peerIpColumn()).data(Qt::ToolTipRole).toString().contains(QStringLiteral("resolving")));
    model.setClients({other, peer}); // a refresh moves the requested peer
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 10000);
    QCOMPARE(changed.last().at(0).value<QModelIndex>().row(), 1);
    QCOMPARE(model.index(0, model.peerIpColumn()).data().toString(), QStringLiteral("Hidden"));
    QVERIFY(model.index(1, model.peerIpColumn()).data(Qt::ToolTipRole).toString().contains(peer.addr));
    const QString resolved = model.index(1, model.peerIpColumn()).data().toString();
    // sorts by the name found, else numerically by the address
    QCOMPARE(model.index(1, model.peerIpColumn()).data(Qt::UserRole).toString(),
             resolved == peer.addr ? addressSortKey(peer.addr, peer.port) : resolved);
    QVERIFY(!model.canResolveHostname(peer)); // success and failure both cached
    model.setResolveHostnames(false);
    QCOMPARE(model.index(1, model.peerIpColumn()).data().toString(), peer.addr);
    QCOMPARE(model.headerData(model.peerIpColumn(), Qt::Horizontal).toString(), QStringLiteral("Peer IP"));
    model.setResolveHostnames(true);
    QCOMPARE(model.index(1, model.peerIpColumn()).data().toString(), resolved);
    QCOMPARE(model.headerData(model.peerIpColumn(), Qt::Horizontal).toString(), QStringLiteral("Hostname / IP"));

    ClientListModel cancelled(ClientListMode::Downloading);
    cancelled.setClients({peer});
    cancelled.setResolveHostnames(true);
    cancelled.setResolveHostnames(false);
    QTest::qWait(20);
    QCOMPARE(cancelled.index(0, cancelled.peerIpColumn()).data().toString(), peer.addr);
    QVERIFY(cancelled.canResolveHostname(peer)); // the cancelled callback cannot populate the cache
}

void tst_CountryFlags::restoreHeader_keepsLayoutWhenColumnAppended()
{
    // Every list gained a Country column. Qt restores a layout saved with fewer
    // columns (widths, order, sort), so nobody's saved layout resets — pin that.
    QStandardItemModel oldModel(0, 3);
    QHeaderView oldHeader(Qt::Horizontal);
    oldHeader.setModel(&oldModel);
    oldHeader.setSortIndicatorShown(true);
    oldHeader.resizeSection(0, 211);
    oldHeader.resizeSection(1, 73);
    oldHeader.resizeSection(2, 97);
    oldHeader.moveSection(2, 0);
    oldHeader.setSortIndicator(1, Qt::DescendingOrder);
    const QByteArray state = oldHeader.saveState();

    QStandardItemModel newModel(0, 4);
    QHeaderView header(Qt::Horizontal);
    header.setModel(&newModel);
    QVERIFY(header.restoreState(state));
    QCOMPARE(header.sectionSize(0), 211);
    QCOMPARE(header.sectionSize(1), 73);
    QCOMPARE(header.sectionSize(2), 97);
    QCOMPARE(header.logicalIndex(0), 2);
    QCOMPARE(header.visualIndex(3), 3);
    QCOMPARE(header.sortIndicatorSection(), 1);
    QCOMPARE(header.sortIndicatorOrder(), Qt::DescendingOrder);
}

void tst_CountryFlags::delegate_widensComposedDecoration()
{
    CountryFlags::setSettings(true);
    ClientListModel model(ClientListMode::Uploading);
    ClientRow row;
    row.userName = QStringLiteral("peer");
    row.cc = QStringLiteral("DE");
    model.setClients({row});

    ListTreeView view;
    view.setModel(&model);
    CountryFlags::bindFlagColumn(&view);
    auto* delegate = qobject_cast<FlagDecorationDelegate*>(view.itemDelegateForColumn(0));
    QVERIFY(delegate);

    QStyleOptionViewItem opt;
    opt.decorationSize = QSize(16, 16);
    struct Probe : FlagDecorationDelegate {
        using FlagDecorationDelegate::FlagDecorationDelegate;
        using FlagDecorationDelegate::initStyleOption;
    } probe;
    probe.initStyleOption(&opt, model.index(0, 0));
    QCOMPARE(opt.decorationSize, QSize(36, 16));
}

QTEST_MAIN(tst_CountryFlags)
#include "tst_CountryFlags.moc"
