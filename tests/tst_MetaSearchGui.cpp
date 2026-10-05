/// @file tst_MetaSearchGui.cpp
/// @brief eNode torrent/Usenet rows in the search list, and the account dialog.
///
/// Set EMULE_TEST_SHOTS=<dir> to also write PNGs of the rendered list and dialog.

#include "controls/SearchResultsModel.h"
#include "dialogs/MetaAccountDialog.h"
#include "utils/ViewSelection.h"
#include "IpcProtocol.h"

#include <QApplication>
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

    SearchResultsModel model;
    model.setResults({row(QStringLiteral("Ubuntu.Server.iso"), 0, QStringLiteral("Iso")),
                      kad,
                      row(QStringLiteral("UBUNTU Linux Server"), 1),
                      kadComplete});

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

    // still an eD2K file: a link, and "?" where Kad reported no complete sources
    QVERIFY(model.rowAt(1)->ed2kLink().startsWith(QStringLiteral("ed2k://|file|")));
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
    QVERIFY(icon(0).availableSizes() != wide);

    model.setResults({row(QStringLiteral("Ubuntu.Server.iso"), 0, QStringLiteral("Iso")), kad, kadComplete});
    QTreeView view;
    view.setRootIsDecorated(false);
    view.setModel(&model);
    view.header()->resizeSection(0, 260);
    view.resize(560, 110);
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

QTEST_MAIN(tst_MetaSearchGui)
#include "tst_MetaSearchGui.moc"
