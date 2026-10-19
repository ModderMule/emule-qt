/// @file tst_MetaSearchGui.cpp
/// @brief eNode torrent/Usenet/web rows in the search list, the account dialog
/// and the web links dialog.
///
/// Set EMULE_TEST_SHOTS=<dir> to also write PNGs of the rendered list and dialog.

#include "controls/FilterEdit.h"
#include "prefs/Preferences.h"
#include "controls/SearchResultsModel.h"
#include "controls/SearchResultsProxy.h"
#include "dialogs/MetaAccountDialog.h"
#include "dialogs/WebLinksDialog.h"
#include "utils/ViewSelection.h"
#include "IpcProtocol.h"

#include <QApplication>
#include <QClipboard>
#include <QCborArray>
#include <QCborMap>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSortFilterProxyModel>
#include <QTest>
#include <QTreeView>
#include <QTreeWidget>
#include <QUrl>

using namespace eMule;
using Ipc::MetaStatus;

namespace {

SearchResultRow row(const QString& name, int metaKind, const QString& type = QStringLiteral("Video"))
{
    SearchResultRow r;
    r.hash = QStringLiteral("ED2B0110FFFF1E1B3620ADE2AF9D6E90");
    r.fileName = name;
    r.fileType = type;
    r.fileSize = 1'754'017'281;
    r.sourceCount = metaKind == 3 ? 0 : 4;
    r.metaKind = metaKind;
    return r;
}

void shoot(QWidget* w, const QString& name)
{
    const QString dir = qEnvironmentVariable("EMULE_TEST_SHOTS");
    if (dir.isEmpty())
        return;
    w->show();
    QTest::qWait(50);
    w->grab().save(QDir(dir).filePath(name));
}

QCborMap authRequired()
{
    return QCborMap{
        {QStringLiteral("status"), static_cast<int>(MetaStatus::AuthRequired)},
        {QStringLiteral("serverAddr"), QStringLiteral("127.0.0.1:5555")},
        {QStringLiteral("serverName"), QStringLiteral("eNode-go")},
        {QStringLiteral("authMode"), 2},
        {QStringLiteral("registrationUrl"), QStringLiteral("http://127.0.0.1:4672/account/register")},
        {QStringLiteral("accountUrl"), QStringLiteral("http://127.0.0.1:4672/account")},
    };
}

template <class T>
T* visibleChild(QWidget* parent, auto pred)
{
    for (auto* w : parent->findChildren<T*>())
        if (w->isVisibleTo(parent) && pred(w))
            return w;
    return nullptr;
}

} // namespace

class tst_MetaSearchGui : public QObject {
    Q_OBJECT

private slots:
    void networkBadgeFollowsFileType();
    void kadOriginGetsKadBadge();
    void ed2kLinkRefusedForMetaRows();
    void magnetLinkForMetaRows();
    void multiSelectionSurvivesReset();
    void networkFilterHidesForeignRows();
    void webAndContainerRows();
    void webLinksDialog_listsAndCopies();
    void webLinksDialog_listsOnionLinks();
    void alternativeNamesAreChildRows();
    void refreshKeepsExpansionAndSelection();
    void mergeTouchesOnlyTheChangedRows();
    void namesStayUnderTheirFileAndSpamStaysLast();
    void filterBoxFollowsMfc();
    void searchCellsFollowMfc();
    void loginDialog_authRequired();
    void loginDialog_inactiveShowsSteps();
    void loginDialog_rejectsNonWebLinks();
    void loginDialog_acceptsWhenActive();
};

void tst_MetaSearchGui::networkBadgeFollowsFileType()
{
    SearchResultsModel model;
    model.setResults({row(QStringLiteral("Ubuntu.Server.iso"), 0, QStringLiteral("Iso")),
                      row(QStringLiteral("UBUNTU Linux Server"), 1),
                      row(QStringLiteral("Some.Release.1080p"), 3)});

    const auto icon = [&](int r) {
        return model.data(model.index(r, SearchResultsModel::ColFileName), Qt::DecorationRole).value<QIcon>();
    };
    QVERIFY(!icon(1).isNull());
    QVERIFY(!icon(2).isNull());
    // Type icon + own badge slot; the eD2K row keeps a blank one so names line up
    const QList<QSize> wide{QSize(68, 32)};
    QCOMPARE(icon(0).availableSizes(), wide);
    QCOMPARE(icon(1).availableSizes(), wide);
    QCOMPARE(icon(2).availableSizes(), wide);
    const QImage ed2k = icon(0).pixmap(QSize(34, 16), 2).toImage();
    const QImage torrent = icon(1).pixmap(QSize(34, 16), 2).toImage();
    const QImage usenet = icon(2).pixmap(QSize(34, 16), 2).toImage();
    QVERIFY(torrent != usenet);
    QVERIFY(torrent != ed2k);

    // Without meta rows an eD2K row is back to the plain type icon
    model.removeRow(2);
    model.removeRow(1);
    QVERIFY(icon(0).availableSizes() != wide);

    QTreeView view;
    view.setRootIsDecorated(false);
    view.setModel(&model);
    view.header()->resizeSection(0, 260);
    view.resize(560, 110);
    shoot(&view, QStringLiteral("meta_results.png"));
}

void tst_MetaSearchGui::kadOriginGetsKadBadge()
{
    // A file the server found on Kad: an eD2K row with the Kad icon after its type icon
    SearchResultRow kad = row(QStringLiteral("Night.Of.The.Living.Dead.avi"), 0);
    kad.hash = QStringLiteral("4B4B4B4B4B4B4B4B4B4B4B4B4B4B4B4B");
    kad.kadOrigin = true;
    SearchResultRow kadComplete = kad;
    kadComplete.completeSourceCount = 2;
    // the same file found on a web page: the web eD2K badge instead of Kad's
    SearchResultRow web = kad;
    web.hash = QStringLiteral("57575757575757575757575757575757");
    web.webOrigin = true;

    SearchResultsModel model;
    model.setResults({row(QStringLiteral("Ubuntu.Server.iso"), 0, QStringLiteral("Iso")),
                      kad,
                      row(QStringLiteral("UBUNTU Linux Server"), 1),
                      kadComplete,
                      web});

    const auto icon = [&](int r) {
        return model.data(model.index(r, SearchResultsModel::ColFileName), Qt::DecorationRole).value<QIcon>();
    };
    const auto complete = [&](int r) {
        return model.data(model.index(r, SearchResultsModel::ColComplete), Qt::DisplayRole).toString();
    };
    const QList<QSize> wide{QSize(68, 32)};
    QCOMPARE(icon(1).availableSizes(), wide);
    const QImage ed2kImg = icon(0).pixmap(QSize(34, 16), 2).toImage();
    const QImage kadImg = icon(1).pixmap(QSize(34, 16), 2).toImage();
    const QImage torrentImg = icon(2).pixmap(QSize(34, 16), 2).toImage();
    QVERIFY(kadImg != torrentImg);
    // same type as the torrent row ("Video"), so only the badge can differ; and the
    // eD2K row's slot is blank where the Kad row's is drawn
    QVERIFY(kadImg.copy(34, 0, 34, 32) != ed2kImg.copy(34, 0, 34, 32));
    const QImage webImg = icon(4).pixmap(QSize(34, 16), 2).toImage();
    QCOMPARE(icon(4).availableSizes(), wide);
    QVERIFY(webImg.copy(34, 0, 34, 32) != kadImg.copy(34, 0, 34, 32));
    QVERIFY(webImg.copy(34, 0, 34, 32) != ed2kImg.copy(34, 0, 34, 32));
    QVERIFY(model.resultAt(4)->ed2kLink().startsWith(QStringLiteral("ed2k://|file|")));

    // still an eD2K file: a link, and "?" where Kad reported no complete sources
    QVERIFY(model.resultAt(1)->ed2kLink().startsWith(QStringLiteral("ed2k://|file|")));
    QCOMPARE(complete(1), QStringLiteral("?"));
    QVERIFY(complete(3) != QStringLiteral("?"));
    QVERIFY(!model.data(model.index(1, SearchResultsModel::ColComplete), Qt::ForegroundRole).isValid()
            || model.data(model.index(1, SearchResultsModel::ColComplete), Qt::ForegroundRole).value<QColor>()
                   != QColor(255, 0, 0));

    // Kad rows alone reserve the slot too: the eD2K row beside them stays aligned
    model.removeRow(2);
    QCOMPARE(icon(0).availableSizes(), wide);
    model.removeRow(2);
    model.removeRow(1);
    QCOMPARE(icon(0).availableSizes(), wide);   // the web page's file holds it alone
    model.removeRow(1);
    QVERIFY(icon(0).availableSizes() != wide);

    web.fileName = QStringLiteral("Night.Of.The.Living.Dead.Web.avi");
    model.setResults({row(QStringLiteral("Ubuntu.Server.iso"), 0, QStringLiteral("Iso")), kad, kadComplete, web});
    QTreeView view;
    view.setRootIsDecorated(false);
    view.setModel(&model);
    view.header()->resizeSection(0, 260);
    view.resize(560, 130);
    shoot(&view, QStringLiteral("kad_results.png"));
}

void tst_MetaSearchGui::ed2kLinkRefusedForMetaRows()
{
    // phase 6: a meta hash is no MD4 — never mint ed2k:// for it
    QVERIFY(row(QStringLiteral("Torrent.Release"), 1).ed2kLink().isEmpty());
    QVERIFY(row(QStringLiteral("Torrent.V2.Release"), 2).ed2kLink().isEmpty());
    QVERIFY(row(QStringLiteral("Usenet.Release"), 3).ed2kLink().isEmpty());

    SearchResultRow plain = row(QStringLiteral("plain.iso"), 0);
    plain.hash = QStringLiteral("0123456789abcdef0123456789abcdef");
    QCOMPARE(plain.ed2kLink(),
             QStringLiteral("ed2k://|file|plain.iso|1754017281|0123456789abcdef0123456789abcdef|/"));

    // a '|' or '%' in the name must not corrupt the link
    plain.fileName = QStringLiteral("a|b 50%.iso");
    QCOMPARE(plain.ed2kLink(),
             QStringLiteral("ed2k://|file|a_b%2050%25.iso|1754017281|0123456789abcdef0123456789abcdef|/"));
}

void tst_MetaSearchGui::magnetLinkForMetaRows()
{
    SearchResultRow torrent = row(QStringLiteral("Torrent.Release"), 1);
    torrent.magnet = QStringLiteral("magnet:?xt=urn:btih:1111111111111111111111111111111111111111");
    QCOMPARE(torrent.magnetLink(), torrent.magnet);   // the server's, never urn:ed2k
    QVERIFY(row(QStringLiteral("Torrent.NoMagnet"), 1).magnetLink().isEmpty());
    QVERIFY(row(QStringLiteral("Usenet.Release"), 3).magnetLink().isEmpty());

    SearchResultRow plain = row(QStringLiteral("a b.iso"), 0);
    plain.hash = QStringLiteral("0123456789abcdef0123456789abcdef");
    QCOMPARE(plain.magnetLink(),
             QStringLiteral("magnet:?xt=urn:ed2k:0123456789ABCDEF0123456789ABCDEF&xl=1754017281&dn=a%20b.iso"));
}

void tst_MetaSearchGui::multiSelectionSurvivesReset()
{
    // a result push resets the model; a multi-select must not shrink to one row
    const auto rows = [](bool reversed) {
        std::vector<SearchResultRow> v;
        for (int i = 0; i < 5; ++i) {
            SearchResultRow r = row(QStringLiteral("Release.%1").arg(i), 1);
            r.hash = QStringLiteral("%1").arg(i, 32, 16, QLatin1Char('A'));
            v.push_back(std::move(r));
        }
        if (reversed)
            std::ranges::reverse(v);
        return v;
    };

    SearchResultsModel model;
    model.setResults(rows(false));
    QSortFilterProxyModel proxy;
    proxy.setSourceModel(&model);
    QTreeView view;
    view.setModel(&proxy);
    view.setSelectionMode(QAbstractItemView::ExtendedSelection);
    view.setSelectionBehavior(QAbstractItemView::SelectRows);
    view.setColumnHidden(SearchResultsModel::ColCodec, true);

    const auto keyOf = [&](int viewRow) {
        return model.hashAt(proxy.mapToSource(proxy.index(viewRow, 0)).row());
    };
    const auto selectedKeys = [&] {
        QStringList keys;
        for (const QModelIndex& idx : view.selectionModel()->selectedRows())
            keys.append(keyOf(idx.row()));
        keys.sort();
        return keys;
    };

    for (const int r : {1, 2, 4})
        view.selectionModel()->select(proxy.index(r, 0),
                                      QItemSelectionModel::Select | QItemSelectionModel::Rows);
    view.selectionModel()->setCurrentIndex(proxy.index(2, 0), QItemSelectionModel::NoUpdate);
    const QStringList before = selectedKeys();
    QCOMPARE(before.size(), 3);

    const ViewSelection saved = captureViewSelection(&view, keyOf);
    QCOMPARE(saved.keys.size(), 3);
    model.setResults(rows(true));   // reset + rows moved
    view.selectionModel()->clearSelection();
    restoreViewSelection(&view, saved, keyOf);

    QCOMPARE(selectedKeys(), before);
    QCOMPARE(keyOf(view.selectionModel()->currentIndex().row()), saved.currentKey);
}

void tst_MetaSearchGui::loginDialog_authRequired()
{
    MetaAccountDialog dlg(nullptr, authRequired(), MetaAccountDialog::Purpose::Download);
    dlg.setAttribute(Qt::WA_DeleteOnClose, false);
    dlg.show();

    auto* pass = visibleChild<QLineEdit>(&dlg, [](QLineEdit* e) { return e->echoMode() == QLineEdit::Password; });
    QVERIFY(pass);
    auto* reg = visibleChild<QLabel>(&dlg, [](QLabel* l) { return l->text().contains(u"account/register"); });
    QVERIFY2(reg, "registration link missing");
    QVERIFY(reg->openExternalLinks());
    QVERIFY(visibleChild<QPushButton>(&dlg, [](QPushButton* b) { return b->text() == MetaAccountDialog::tr("Log In"); }));
    shoot(&dlg, QStringLiteral("meta_login.png"));
}

void tst_MetaSearchGui::loginDialog_inactiveShowsSteps()
{
    QCborMap meta = authRequired();
    meta.insert(QStringLiteral("status"), static_cast<int>(MetaStatus::AccountInactive));
    meta.insert(QStringLiteral("loggedIn"), true);
    meta.insert(QStringLiteral("username"), QStringLiteral("alice"));
    meta.insert(QStringLiteral("state"), 1);
    meta.insert(QStringLiteral("pendingSteps"), QCborArray{QCborMap{
        {QStringLiteral("title"), QStringLiteral("Membership (30 days)")},
        {QStringLiteral("kind"), 1},
        {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:4672/account/step/payment")}}});

    MetaAccountDialog dlg(nullptr, meta, MetaAccountDialog::Purpose::Download);
    dlg.setAttribute(Qt::WA_DeleteOnClose, false);
    dlg.show();
    QVERIFY(visibleChild<QLabel>(&dlg, [](QLabel* l) { return l->text().contains(u"step/payment"); }));
    QVERIFY(!visibleChild<QLineEdit>(&dlg, [](QLineEdit*) { return true; }));   // no login form
    QVERIFY(visibleChild<QPushButton>(&dlg, [](QPushButton* b) { return b->text() == MetaAccountDialog::tr("Check Again"); }));
    shoot(&dlg, QStringLiteral("meta_inactive.png"));
}

void tst_MetaSearchGui::loginDialog_rejectsNonWebLinks()
{
    QCborMap meta = authRequired();
    meta.insert(QStringLiteral("registrationUrl"), QStringLiteral("file:///etc/passwd"));
    MetaAccountDialog dlg(nullptr, meta, MetaAccountDialog::Purpose::Download);
    dlg.setAttribute(Qt::WA_DeleteOnClose, false);
    for (auto* l : dlg.findChildren<QLabel*>())
        QVERIFY(!l->text().contains(u"href=\"file:"));
}

void tst_MetaSearchGui::loginDialog_acceptsWhenActive()
{
    MetaAccountDialog dlg(nullptr, authRequired(), MetaAccountDialog::Purpose::Download);
    dlg.setAttribute(Qt::WA_DeleteOnClose, false);
    QSignalSpy accepted(&dlg, &QDialog::accepted);
    QCborMap ok = authRequired();
    ok.insert(QStringLiteral("status"), static_cast<int>(MetaStatus::Ok));
    ok.insert(QStringLiteral("loggedIn"), true);
    ok.insert(QStringLiteral("state"), 2);
    dlg.updateMeta(ok);
    QCOMPARE(accepted.count(), 1);
}

void tst_MetaSearchGui::networkFilterHidesForeignRows()
{
    const auto named = [](const QString& name, int metaKind, int n) {
        SearchResultRow r = row(name, metaKind);
        r.hash = QStringLiteral("%1").arg(n, 32, 16, QLatin1Char('A'));
        return r;
    };
    std::vector<SearchResultRow> rows;
    rows.push_back(named(QStringLiteral("ed2k"), 0, 0));
    rows.push_back(named(QStringLiteral("kadOrigin"), 0, 1));
    rows.back().kadOrigin = true;
    rows.push_back(named(QStringLiteral("ownKad"), 0, 2));
    rows.back().isKad = true;   // our own Kad search, not the server's
    rows.push_back(named(QStringLiteral("btv1"), 1, 3));
    rows.push_back(named(QStringLiteral("btv2"), 2, 4));
    rows.push_back(named(QStringLiteral("nzb"), 3, 5));

    SearchResultsModel model;
    model.setResults(rows);
    SearchResultsProxy proxy;
    proxy.setSourceModel(&model);

    const auto shown = [&] {
        QStringList names;
        for (int r = 0; r < proxy.rowCount(); ++r)
            names.append(model.resultAt(proxy.mapToSource(proxy.index(r, 0)).row())->fileName);
        names.sort();
        return names;
    };
    using Filter = SearchResultsProxy::NetworkFilter;
    using namespace Qt::StringLiterals;

    QCOMPARE(proxy.rowCount(), 6);
    QCOMPARE(proxy.hiddenCount(), 0);

    proxy.setNetworkFilter(Filter{.usenet = false});
    QCOMPARE(shown(), QStringList({u"btv1"_s, u"btv2"_s, u"ed2k"_s, u"kadOrigin"_s, u"ownKad"_s}));
    QCOMPARE(proxy.hiddenCount(), 1);

    proxy.setNetworkFilter(Filter{.kad = false});
    QCOMPARE(shown(), QStringList({u"btv1"_s, u"btv2"_s, u"ed2k"_s, u"nzb"_s, u"ownKad"_s}));

    proxy.setNetworkFilter(Filter{.torrent = false});
    QCOMPARE(shown(), QStringList({u"ed2k"_s, u"kadOrigin"_s, u"nzb"_s, u"ownKad"_s}));
    QCOMPARE(proxy.hiddenCount(), 2);

    proxy.setNetworkFilter(Filter{.usenet = false, .kad = false, .torrent = false});
    QCOMPARE(shown(), QStringList({u"ed2k"_s, u"ownKad"_s}));

    // a refresh while filtered: new foreign rows stay hidden
    rows.push_back(named(QStringLiteral("nzb2"), 3, 6));
    rows.push_back(named(QStringLiteral("ed2k2"), 0, 7));
    model.setResults(rows);
    QCOMPARE(shown(), QStringList({u"ed2k"_s, u"ed2k2"_s, u"ownKad"_s}));
    QCOMPARE(proxy.hiddenCount(), 5);

    proxy.setNetworkFilter(Filter{});
    QCOMPARE(proxy.rowCount(), 8);
    QCOMPARE(proxy.hiddenCount(), 0);
}

// Web link sets and link containers: their own badge, filter and size text.
void tst_MetaSearchGui::webAndContainerRows()
{
    std::vector<SearchResultRow> rows;
    rows.push_back(row(QStringLiteral("torrent"), 1));
    rows.push_back(row(QStringLiteral("web"), 5));
    rows.back().hash = QStringLiteral("ED2B0152FFFF1E1B3620ADE2AF9D6E90");
    rows.back().fileSize = 0;
    rows.push_back(row(QStringLiteral("container"), 6));
    rows.back().hash = QStringLiteral("ED2B01600000C0AD674404C49C4C52AC");
    rows.back().fileSize = 0;
    rows.push_back(row(QStringLiteral("magnet"), 1));
    rows.back().hash = QStringLiteral("ED2B01100000AAAD674404C49C4C52AC");
    rows.back().metaFlags = 0x08;
    rows.back().magnet = QStringLiteral("magnet:?xt=urn:btih:abc");
    // an eD2K file a web page offered (FT_META_NETWORK 5): a web row for the filter
    rows.push_back(row(QStringLiteral("webEd2k"), 0));
    rows.back().hash = QStringLiteral("57575757575757575757575757575757");
    rows.back().kadOrigin = true;
    rows.back().webOrigin = true;

    SearchResultsModel model;
    model.setResults(rows);
    const auto icon = [&](int r) {
        return model.data(model.index(r, SearchResultsModel::ColFileName), Qt::DecorationRole)
            .value<QIcon>().pixmap(QSize(34, 16), 2).toImage();
    };
    QVERIFY(icon(1) != icon(0));   // Web.ico, not the torrent badge
    QVERIFY(icon(2) != icon(1));   // the container's own icon
    QVERIFY(icon(3) == icon(0));   // a magnet is a torrent
    QVERIFY(!QIcon(QStringLiteral(":/icons/Container.ico")).isNull());
    QVERIFY(!QIcon(QStringLiteral(":/icons/WebEd2k.ico")).isNull());
    QVERIFY(icon(4) != icon(1));   // not the link set's plain globe

    const auto size = [&](int r) { return model.index(r, SearchResultsModel::ColSize).data().toString(); };
    QCOMPARE(size(1), SearchResultsModel::tr("Stream"));
    QCOMPARE(size(2), QString());
    QVERIFY(!size(0).isEmpty());

    QVERIFY(model.resultAt(1)->isWeb() && model.resultAt(2)->isContainer());
    QVERIFY(model.resultAt(3)->isMagnetOnly() && !model.resultAt(0)->isMagnetOnly());
    QCOMPARE(model.resultAt(3)->magnetLink(), QStringLiteral("magnet:?xt=urn:btih:abc"));
    QVERIFY(model.resultAt(1)->ed2kLink().isEmpty());

    SearchResultsProxy proxy;
    proxy.setSourceModel(&model);
    QCOMPARE(proxy.rowCount(), 5);
    proxy.setNetworkFilter(SearchResultsProxy::NetworkFilter{.web = false});
    QCOMPARE(proxy.rowCount(), 2);   // the torrent and the magnet stay
    proxy.setNetworkFilter(SearchResultsProxy::NetworkFilter{.kad = false});
    QCOMPARE(proxy.rowCount(), 5);   // the web page's file is the web's, not Kad's
    proxy.setNetworkFilter(SearchResultsProxy::NetworkFilter{.torrent = false});
    QCOMPARE(proxy.rowCount(), 3);
}

void tst_MetaSearchGui::webLinksDialog_listsAndCopies()
{
    const QCborArray links{
        QCborMap{{QStringLiteral("url"), QStringLiteral("https://a.test/f/1")}, {QStringLiteral("file"), 0},
                 {QStringLiteral("name"), QStringLiteral("part1.rar")}, {QStringLiteral("size"), 1048576},
                 {QStringLiteral("hoster"), QStringLiteral("a")}},
        QCborMap{{QStringLiteral("url"), QStringLiteral("https://b.test/f/1")}, {QStringLiteral("file"), 0},
                 {QStringLiteral("hoster"), QStringLiteral("b")}},
    };
    const QCborMap map{{QStringLiteral("title"), QStringLiteral("Some Release")},
                       {QStringLiteral("protected"), true},
                       {QStringLiteral("links"), links}};
    WebLinksDialog dlg(QStringLiteral("row"), map, "https://a.test/f/1\r\n", QStringLiteral("row.txt"));
    QVERIFY(dlg.windowTitle().contains(QStringLiteral("Some Release")));
    QVERIFY(visibleChild<QLabel>(&dlg, [](QLabel* l) { return l->text().contains(u"password"); }));

    auto* list = dlg.findChild<QTreeWidget*>();
    QVERIFY(list);
    QCOMPARE(list->topLevelItemCount(), 2);
    QCOMPARE(list->topLevelItem(0)->text(4), QStringLiteral("https://a.test/f/1"));
    QCOMPARE(list->topLevelItem(0)->text(0), QStringLiteral("1"));   // file numbers count from 1
    QVERIFY(!dlg.findChild<QLabel*>(QStringLiteral("onionNote")));   // no onion link, no Tor hint
    QVERIFY(list->topLevelItem(1)->text(2).isEmpty());                // no size hint

    auto* copy = visibleChild<QPushButton>(&dlg, [](QPushButton* b) { return b->text() == WebLinksDialog::tr("Copy Links"); });
    QVERIFY(copy);
    copy->click();   // nothing selected: all of them
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("https://a.test/f/1\nhttps://b.test/f/1"));
    list->topLevelItem(1)->setSelected(true);
    copy->click();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("https://b.test/f/1"));
    shoot(&dlg, QStringLiteral("web-links.png"));
}

void tst_MetaSearchGui::webLinksDialog_listsOnionLinks()
{
    const QString onion = QStringLiteral("http://abcdefghijklmnopqrstuvwxyz234567abcdefghijklmnopqrstuvwx.onion/f/ccc1");
    const QCborMap map{{QStringLiteral("title"), QStringLiteral("Hidden Release")},
                       {QStringLiteral("links"), QCborArray{QCborMap{{QStringLiteral("url"), onion},
                                                                     {QStringLiteral("file"), 0},
                                                                     {QStringLiteral("hoster"), QStringLiteral("onionhost")}}}}};
    WebLinksDialog dlg(QStringLiteral("row"), map, onion.toUtf8() + "\r\n", QStringLiteral("row.txt"));

    auto* list = dlg.findChild<QTreeWidget*>();
    QVERIFY(list);
    QCOMPARE(list->topLevelItemCount(), 1);
    QCOMPARE(list->topLevelItem(0)->text(4), onion);
    QCOMPARE(list->topLevelItem(0)->text(3), QStringLiteral("onionhost"));
    QVERIFY(QUrl(onion, QUrl::StrictMode).isValid());   // what Open in Browser hands on
    auto* note = dlg.findChild<QLabel*>(QStringLiteral("onionNote"));
    QVERIFY(note);
    QVERIFY(note->text().contains(u"Tor Browser"));

    auto* copy = visibleChild<QPushButton>(&dlg, [](QPushButton* b) { return b->text() == WebLinksDialog::tr("Copy Links"); });
    QVERIFY(copy);
    copy->click();
    QCOMPARE(QApplication::clipboard()->text(), onion);
    shoot(&dlg, QStringLiteral("web-links-onion.png"));
}

namespace {

/// An eD2K file found under two names.
SearchResultRow twoNames(const QString& hash, const QString& best, const QString& other)
{
    SearchResultRow r;
    r.hash = hash;
    r.fileName = best;
    r.fileSize = 1000;
    r.sourceCount = 12;
    SearchChildRow a;
    a.fileName = best;
    a.sourceCount = 9;
    a.aichHash = QStringLiteral("AICHA");
    SearchChildRow b;
    b.fileName = other;
    b.sourceCount = 3;
    b.directory = QStringLiteral("dir");
    r.children = {a, b};
    return r;
}

} // namespace

void tst_MetaSearchGui::alternativeNamesAreChildRows()
{
    // MFC SearchListCtrl.cpp:1151-1210, 1541-1654. The list used to be flat: the
    // other names a file goes by were dropped on the way to the GUI.
    SearchResultsModel model;
    model.setResults({twoNames(QStringLiteral("AAAA"), QStringLiteral("good.avi"),
                               QStringLiteral("other name.avi"))});

    const QModelIndex file = model.index(0, 0);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.rowCount(file), 2);
    QCOMPARE(model.resultCount(), 1);   // names are not results

    const auto cell = [&](int row, int column) {
        return model.index(row, column, file).data().toString();
    };
    QCOMPARE(cell(1, SearchResultsModel::ColFileName), QStringLiteral("other name.avi"));
    QCOMPARE(cell(1, SearchResultsModel::ColAvailability), QStringLiteral("3"));
    QCOMPARE(cell(1, SearchResultsModel::ColFolder), QStringLiteral("dir"));
    QCOMPARE(cell(0, SearchResultsModel::ColAichHash), QStringLiteral("AICHA"));
    // MFC leaves these blank on a name row
    QCOMPARE(cell(1, SearchResultsModel::ColSize), QString());
    QCOMPARE(cell(1, SearchResultsModel::ColComplete), QString());
    QCOMPARE(cell(1, SearchResultsModel::ColType), QString());
    QCOMPARE(cell(1, SearchResultsModel::ColFileID), QString());

    // A name row acts on its file, under that name
    const SearchResultRef ref = model.resultAt(model.index(1, 0, file));
    QVERIFY(ref);
    QCOMPARE(ref.row->hash, QStringLiteral("AAAA"));
    QCOMPARE(ref.fileName(), QStringLiteral("other name.avi"));
    QVERIFY(ref.row->ed2kLink(ref.fileName()).contains(QStringLiteral("other%20name.avi")));
    QVERIFY(model.index(1, 0, file).data(SearchResultsModel::ChildRole).toBool());
    QVERIFY(!file.data(SearchResultsModel::ChildRole).toBool());
    QCOMPARE(model.parent(model.index(1, 0, file)), file);
}

void tst_MetaSearchGui::refreshKeepsExpansionAndSelection()
{
    // The list is refetched while a search runs. A reset per refetch would collapse
    // every expanded file and drop the selection under the user's hand.
    const auto snapshot = [](int extraSources) {
        std::vector<SearchResultRow> v;
        v.push_back(twoNames(QStringLiteral("AAAA"), QStringLiteral("a.avi"), QStringLiteral("a2.avi")));
        v.push_back(twoNames(QStringLiteral("BBBB"), QStringLiteral("b.avi"), QStringLiteral("b2.avi")));
        v[1].sourceCount += extraSources;
        return v;
    };

    SearchResultsModel model;
    model.setResults(snapshot(0));
    SearchResultsProxy proxy;
    proxy.setSourceModel(&model);
    QTreeView view;
    view.setModel(&proxy);
    view.setSelectionBehavior(QAbstractItemView::SelectRows);

    const QModelIndex fileB = proxy.mapFromSource(model.index(1, 0));
    view.expand(fileB);
    const QModelIndex name = proxy.index(1, 0, fileB);
    view.selectionModel()->select(name, QItemSelectionModel::Select | QItemSelectionModel::Rows);

    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    auto next = snapshot(5);
    SearchResultRow added;
    added.hash = QStringLiteral("CCCC");
    added.fileName = QStringLiteral("c.avi");
    next.insert(next.begin(), added);   // arrives ahead of the others
    model.setResults(std::move(next));

    QCOMPARE(resets.count(), 0);
    QCOMPARE(model.rowCount(), 3);
    const QModelIndex fileBNow = proxy.mapFromSource(model.index(1, 0));
    QCOMPARE(model.resultAt(1)->sourceCount, qint64(17));   // updated in place
    QVERIFY(view.isExpanded(fileBNow));
    const QModelIndexList selected = view.selectionModel()->selectedRows();
    QCOMPARE(selected.size(), 1);
    QCOMPARE(selected.first().data().toString(), QStringLiteral("b2.avi"));

    // A name that is gone leaves; the file stays
    auto fewer = snapshot(0);
    fewer[1].children.pop_back();
    model.setResults(std::move(fewer));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.rowCount(model.index(1, 0)), 1);
}

void tst_MetaSearchGui::mergeTouchesOnlyTheChangedRows()
{
    // While a search runs only the changed files arrive. A dataChanged over the
    // whole list per refresh is what froze the window during a Kad search.
    SearchResultsModel model;
    std::vector<SearchResultRow> rows;
    rows.push_back(twoNames(QStringLiteral("AAAA"), QStringLiteral("a.avi"), QStringLiteral("a2.avi")));
    rows.push_back(twoNames(QStringLiteral("BBBB"), QStringLiteral("b.avi"), QStringLiteral("b2.avi")));
    rows.push_back(twoNames(QStringLiteral("CCCC"), QStringLiteral("c.avi"), QStringLiteral("c2.avi")));
    model.setResults(std::move(rows));
    const qint64 sourcesA = model.resultAt(0)->sourceCount;

    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);

    SearchResultRow update = twoNames(QStringLiteral("BBBB"), QStringLiteral("b.avi"), QStringLiteral("b2.avi"));
    update.sourceCount += 7;
    SearchResultRow fresh;
    fresh.hash = QStringLiteral("DDDD");
    fresh.fileName = QStringLiteral("d.avi");
    const qint64 sourcesB = update.sourceCount;
    QVERIFY(model.mergeResults({update, fresh}));

    QCOMPARE(resets.count(), 0);
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.resultAt(1)->sourceCount, sourcesB);
    QCOMPARE(model.resultAt(0)->sourceCount, sourcesA);
    QCOMPARE(model.hashAt(3), QStringLiteral("DDDD"));
    QCOMPARE(model.rowCount(model.index(1, 0)), 2);   // its names are still there

    // No top-level dataChanged reaches a row other than BBBB's
    for (const auto& call : changed) {
        const auto topLeft = call.at(0).toModelIndex();
        const auto bottomRight = call.at(1).toModelIndex();
        if (topLeft.parent().isValid())
            QCOMPARE(topLeft.parent().row(), 1);
        else {
            QCOMPARE(topLeft.row(), 1);
            QCOMPARE(bottomRight.row(), 1);
        }
    }
    QCOMPARE(inserted.count(), 1);

    // Nothing changed: nothing is signalled
    changed.clear();
    QVERIFY(model.mergeResults({}));
    QCOMPARE(changed.count(), 0);
}

void tst_MetaSearchGui::namesStayUnderTheirFileAndSpamStaysLast()
{
    // MFC CompareChild / Compare (SearchListCtrl.cpp:604-629)
    std::vector<SearchResultRow> rows;
    rows.push_back(twoNames(QStringLiteral("AAAA"), QStringLiteral("mmm.avi"), QStringLiteral("zzz.avi")));
    SearchResultRow spam;
    spam.hash = QStringLiteral("BBBB");
    spam.fileName = QStringLiteral("aaa spam.avi");
    spam.sourceCount = 500;
    spam.isSpam = true;
    rows.push_back(spam);
    SearchResultRow plain;
    plain.hash = QStringLiteral("CCCC");
    plain.fileName = QStringLiteral("bbb.avi");
    plain.sourceCount = 1;
    rows.push_back(plain);

    const bool hadFilter = thePrefs.enableSearchResultFilter();
    thePrefs.setEnableSearchResultFilter(true);

    SearchResultsModel model;
    model.setResults(rows);
    SearchResultsProxy proxy;
    proxy.setSourceModel(&model);
    proxy.setSortRole(Qt::UserRole);
    const auto top = [&proxy] {
        QStringList out;
        for (int r = 0; r < proxy.rowCount(); ++r)
            out << proxy.index(r, 0).data().toString();
        return out;
    };
    using namespace Qt::StringLiterals;

    proxy.sort(SearchResultsModel::ColFileName, Qt::AscendingOrder);
    QCOMPARE(top(), QStringList({u"bbb.avi"_s, u"mmm.avi"_s, u"aaa spam.avi"_s}));
    proxy.sort(SearchResultsModel::ColFileName, Qt::DescendingOrder);
    QCOMPARE(top(), QStringList({u"mmm.avi"_s, u"bbb.avi"_s, u"aaa spam.avi"_s}));

    // Names: most available first unless the column is the name
    proxy.sort(SearchResultsModel::ColSize, Qt::AscendingOrder);
    const QModelIndex file = proxy.mapFromSource(model.index(0, 0));
    QCOMPARE(proxy.rowCount(file), 2);
    QCOMPARE(proxy.index(0, 0, file).data().toString(), QStringLiteral("mmm.avi"));   // 9 sources
    proxy.sort(SearchResultsModel::ColFileName, Qt::DescendingOrder);
    QCOMPARE(proxy.index(0, 0, proxy.mapFromSource(model.index(0, 0))).data().toString(),
             QStringLiteral("zzz.avi"));

    thePrefs.setEnableSearchResultFilter(hadFilter);
}

void tst_MetaSearchGui::filterBoxFollowsMfc()
{
    // MFC CSearchListCtrl::IsFilteredOut (SearchListCtrl.cpp:1721-1744)
    using namespace Qt::StringLiterals;
    QCOMPARE(FilterEdit::tokens(u"  linux  -  -beta iso "_s), QStringList({u"linux"_s, u"-beta"_s, u"iso"_s}));
    QVERIFY(FilterEdit::matches({u"LINUX"_s, u"iso"_s}, u"Some.Linux.ISO"_s));
    QVERIFY(!FilterEdit::matches({u"linux"_s, u"-iso"_s}, u"Some.Linux.ISO"_s));
    QVERIFY(FilterEdit::matches({u"-beta"_s}, u"Some.Linux.ISO"_s));
    QVERIFY(FilterEdit::matches({}, u"anything"_s));

    std::vector<SearchResultRow> rows;
    rows.push_back(twoNames(QStringLiteral("AAAA"), QStringLiteral("linux.iso"), QStringLiteral("holiday.iso")));
    SearchResultRow other;
    other.hash = QStringLiteral("BBBB");
    other.fileName = QStringLiteral("holiday.avi");
    other.fileType = QStringLiteral("Video");
    rows.push_back(other);

    SearchResultsModel model;
    model.setResults(rows);
    SearchResultsProxy proxy;
    proxy.setSourceModel(&model);

    proxy.setTextFilter({u"linux"_s}, SearchResultsModel::ColFileName);
    QCOMPARE(proxy.rowCount(), 1);
    // Files only: the one that stays keeps every name, also the one not matching
    QCOMPARE(proxy.rowCount(proxy.index(0, 0)), 2);

    // The column's text as shown, so "Video" and not a token
    proxy.setTextFilter({u"video"_s}, SearchResultsModel::ColType);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data().toString(), QStringLiteral("holiday.avi"));

    proxy.setTextFilter({}, 0);
    QCOMPARE(proxy.rowCount(), 2);
}

void tst_MetaSearchGui::searchCellsFollowMfc()
{
    SearchResultRow archive;
    archive.hash = QStringLiteral("0123456789ABCDEF0123456789ABCDEF");
    archive.fileName = QStringLiteral("stuff.zip");
    archive.fileType = QStringLiteral("Pro");     // archives are published as programs
    archive.sourceCount = 7;
    archive.clientCount = 2;
    archive.bitrate = 128;
    archive.directory = QStringLiteral("Shared/Stuff");
    archive.aichHash = QStringLiteral("AICHROOT");
    SearchResultRow picture;
    picture.hash = QStringLiteral("1123456789ABCDEF0123456789ABCDEF");
    picture.fileName = QStringLiteral("photo.jpg");
    picture.fileType = QStringLiteral("Image");
    picture.isKad = true;
    picture.kadPublishers = 4;
    SearchResultRow program;
    program.hash = QStringLiteral("2123456789ABCDEF0123456789ABCDEF");
    program.fileName = QStringLiteral("setup.exe");
    program.fileType = QStringLiteral("Pro");

    SearchResultsModel model;
    model.setResults({archive, picture, program});
    const auto cell = [&model](int row, int column) { return model.index(row, column).data().toString(); };

    // MFC GetFileTypeDisplayStrFromED2KFileType; the cells showed "Pro" and "Image"
    QCOMPARE(cell(0, SearchResultsModel::ColType), QStringLiteral("Archive"));
    QCOMPARE(cell(1, SearchResultsModel::ColType), QStringLiteral("Picture"));
    QCOMPARE(cell(2, SearchResultsModel::ColType), QStringLiteral("Program"));
    QCOMPARE(cell(0, SearchResultsModel::ColBitrate), QStringLiteral("128 Kbit/s"));

    // The three columns MFC keeps hidden by default
    QCOMPARE(cell(0, SearchResultsModel::ColFileID), archive.hash);
    QCOMPARE(cell(0, SearchResultsModel::ColFolder), QStringLiteral("Shared/Stuff"));
    QCOMPARE(cell(0, SearchResultsModel::ColAichHash), QStringLiteral("AICHROOT"));
    QCOMPARE(model.headerData(SearchResultsModel::ColFileID, Qt::Horizontal).toString(),
             QStringLiteral("File ID"));

    // Availability: the bare count, and what stands behind it in advanced mode
    const bool ext = thePrefs.showExtControls();
    thePrefs.setShowExtControls(false);
    QCOMPARE(cell(0, SearchResultsModel::ColAvailability), QStringLiteral("7"));
    QCOMPARE(cell(2, SearchResultsModel::ColAvailability), QStringLiteral("0"));
    thePrefs.setShowExtControls(true);
    QCOMPARE(cell(0, SearchResultsModel::ColAvailability), QStringLiteral("7 (2)"));   // clients
    QCOMPARE(cell(1, SearchResultsModel::ColAvailability), QStringLiteral("0 (4)"));   // publishers
    thePrefs.setShowExtControls(ext);
}

QTEST_MAIN(tst_MetaSearchGui)
#include "tst_MetaSearchGui.moc"
