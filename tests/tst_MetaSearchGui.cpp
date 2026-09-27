/// @file tst_MetaSearchGui.cpp
/// @brief eNode torrent/Usenet rows in the search list, and the account dialog.
///
/// Set EMULE_TEST_SHOTS=<dir> to also write PNGs of the rendered list and dialog.

#include "controls/SearchResultsModel.h"
#include "dialogs/MetaAccountDialog.h"
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
    void networkIconReplacesFileType();
    void ed2kLinkRefusedForMetaRows();
    void magnetLinkForMetaRows();
    void loginDialog_authRequired();
    void loginDialog_inactiveShowsSteps();
    void loginDialog_rejectsNonWebLinks();
    void loginDialog_acceptsWhenActive();
};

void tst_MetaSearchGui::networkIconReplacesFileType()
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
    const QImage ed2k = icon(0).pixmap(16).toImage();
    const QImage torrent = icon(1).pixmap(16).toImage();
    const QImage usenet = icon(2).pixmap(16).toImage();
    QVERIFY(torrent != usenet);
    QVERIFY(torrent != ed2k);
    QCOMPARE(usenet, QIcon(QStringLiteral(":/icons/Usenet.ico")).pixmap(16).toImage());

    QTreeView view;
    view.setRootIsDecorated(false);
    view.setModel(&model);
    view.header()->resizeSection(0, 260);
    view.resize(560, 110);
    shoot(&view, QStringLiteral("meta_results.png"));
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
