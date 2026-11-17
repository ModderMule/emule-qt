/// @file tst_HosterCombo.cpp
/// @brief The site box of the Add account dialogs (HosterCombo, HosterIcons): the core's
/// account plugins per kind, typing, names, favicons. No daemon: the catalog comes in as the
/// IpcClient's PushHosterCatalog signal. EMULE_GRAB_DIR=<dir> saves a picture of the popup.

#include "app/IpcClient.h"
#include "controls/HosterCombo.h"
#include "utils/HosterIcons.h"
#include "IpcMessage.h"

#include <QAbstractItemView>
#include <QBuffer>
#include <QCborArray>
#include <QCborMap>
#include <QCompleter>
#include <QDir>
#include <QLineEdit>
#include <QPainter>
#include <QPixmap>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

void pushCatalog(IpcClient& ipc, const QCborMap& m)
{
    IpcMessage msg(IpcMsgType::PushHosterCatalog, 0);
    msg.append(m);
    emit ipc.hosterCatalogChanged(msg);
}

QCborMap host(const QString& h, bool multi = false, bool solver = false, const QString& name = {})
{
    QCborMap m{{QStringLiteral("host"), h}};
    if (multi)
        m.insert(QStringLiteral("multi"), true);
    if (solver)
        m.insert(QStringLiteral("solver"), true);
    if (!name.isEmpty())
        m.insert(QStringLiteral("name"), name);
    return m;
}

QByteArray png(const QColor& c)
{
    QPixmap pm(16, 16);
    pm.fill(c);
    QByteArray out;
    QBuffer b(&out);
    b.open(QIODevice::WriteOnly);
    pm.save(&b, "PNG");
    return out;
}

QStringList rows(const QComboBox& c)
{
    QStringList out;
    for (int i = 0; i < c.count(); ++i)
        out << c.itemText(i);
    return out;
}

} // namespace

class TestHosterCombo : public QObject {
    Q_OBJECT

private slots:
    void abbreviation_data();
    void abbreviation();
    void kinds_takeTheirPlugins();
    void host_typedNameAndWww();
    void icons_standInThenFavicon();
    void completer_filtersByPart();
};

void TestHosterCombo::abbreviation_data()
{
    QTest::addColumn<QString>("host");
    QTest::addColumn<QString>("abbr");
    QTest::newRow("rapidgator") << QStringLiteral("rapidgator.net") << QStringLiteral("RG");
    QTest::newRow("digits") << QStringLiteral("1fichier.com") << QStringLiteral("FH");
    QTest::newRow("vowels only") << QStringLiteral("aio.com") << QStringLiteral("AI");
}

void TestHosterCombo::abbreviation()
{
    QFETCH(QString, host);
    QFETCH(QString, abbr);
    QCOMPARE(HosterIcons::abbreviation(host), abbr);
}

void TestHosterCombo::kinds_takeTheirPlugins()
{
    IpcClient ipc;
    HosterCombo hosters(&ipc, HosterCombo::Kind::Hosters, {});
    HosterCombo solvers(&ipc, HosterCombo::Kind::Solvers, {{QStringLiteral("2captcha.com"), QStringLiteral("2Captcha")}});
    HosterCombo multi(&ipc, HosterCombo::Kind::MultiHosters, {});
    QVERIFY(rows(hosters).isEmpty());
    QCOMPARE(rows(solvers), QStringList{QStringLiteral("2captcha.com")});   // shipped list before the core's
    pushCatalog(ipc, {{QStringLiteral("hosts"),
                       QCborArray{host(QStringLiteral("rapidgator.net")), host(QStringLiteral("1fichier.com")),
                                  host(QStringLiteral("real-debrid.com"), true),
                                  host(QStringLiteral("anti-captcha.com"), false, true)}}});
    QCOMPARE(rows(hosters), (QStringList{QStringLiteral("1fichier.com"), QStringLiteral("rapidgator.net")}));
    QCOMPARE(rows(multi), QStringList{QStringLiteral("real-debrid.com")});
    QCOMPARE(rows(solvers), (QStringList{QStringLiteral("2captcha.com"), QStringLiteral("anti-captcha.com")}));
}

void TestHosterCombo::host_typedNameAndWww()
{
    IpcClient ipc;
    HosterCombo c(&ipc, HosterCombo::Kind::MultiHosters, {{QStringLiteral("real-debrid.com"), QStringLiteral("Real-Debrid")}});
    QSignalSpy changed(&c, &HosterCombo::hostChanged);
    c.setEditText(QStringLiteral("realdebrid"));
    QCOMPARE(c.host(), QStringLiteral("realdebrid"));   // not a name, not a listed host
    c.setEditText(QStringLiteral("real-DEBRID"));
    QCOMPARE(c.host(), QStringLiteral("real-debrid.com"));   // its name, any case
    c.setEditText(QStringLiteral(" WWW.Example.org "));
    QCOMPARE(c.host(), QStringLiteral("example.org"));   // any domain can be typed
    QVERIFY(changed.size() >= 3);
    c.setHost(QStringLiteral("real-debrid.com"));
    QCOMPARE(c.currentIndex(), 0);
    // an editor's text survives the list coming in
    c.setEditText(QStringLiteral("typed.example"));
    pushCatalog(ipc, {{QStringLiteral("hosts"), QCborArray{host(QStringLiteral("alldebrid.com"), true)}}});
    QCOMPARE(c.currentText(), QStringLiteral("typed.example"));
    QCOMPARE(c.count(), 2);
}

void TestHosterCombo::icons_standInThenFavicon()
{
    IpcClient ipc;
    HosterIcons* icons = HosterIcons::of(&ipc);
    QCOMPARE(HosterIcons::of(&ipc), icons);   // one per client
    const QIcon standIn = icons->icon(QStringLiteral("rapidgator.net"));
    QVERIFY(!standIn.isNull());
    HosterCombo c(&ipc, HosterCombo::Kind::Hosters, {});
    pushCatalog(ipc, {{QStringLiteral("hosts"), QCborArray{host(QStringLiteral("rapidgator.net"))}}});
    QSignalSpy changed(icons, &HosterIcons::iconChanged);
    pushCatalog(ipc, {{QStringLiteral("icons"), QCborMap{{QStringLiteral("rapidgator.net"), QCborValue(png(Qt::red))}}}});
    QCOMPARE(changed.size(), 1);
    const QImage got = icons->icon(QStringLiteral("rapidgator.net")).pixmap(16, 16).toImage();
    QCOMPARE(QColor(got.pixel(8, 8)), QColor(Qt::red));
    QVERIFY(c.itemIcon(0).isNull());   // the delegate draws it: never twice in the field
    // the server country: a flag after the favicon, in the tool tip
    changed.clear();
    pushCatalog(ipc, {{QStringLiteral("countries"), QCborMap{{QStringLiteral("rapidgator.net"), QStringLiteral("DE")}}}});
    QCOMPARE(changed.size(), 1);
    QCOMPARE(icons->country(QStringLiteral("rapidgator.net")), QStringLiteral("de"));
    QVERIFY(c.itemData(0, Qt::ToolTipRole).toString().contains(QStringLiteral("(DE)")));
    const QSize wide = icons->iconWithFlag(QStringLiteral("rapidgator.net")).availableSizes().value(0);
    QCOMPARE(wide.width(), wide.height() * 36 / 16);   // 36x16 at any pixel ratio
    // a site without one keeps its stand-in
    pushCatalog(ipc, {{QStringLiteral("icons"), QCborMap{{QStringLiteral("none.example"), QCborValue(QByteArray())}}}});
    QVERIFY(!icons->icon(QStringLiteral("none.example")).isNull());
}

void TestHosterCombo::completer_filtersByPart()
{
    IpcClient ipc;
    HosterCombo c(&ipc, HosterCombo::Kind::Hosters, {});
    QCborArray list;
    for (const char* h : {"rapidgator.net", "1fichier.com", "uploaded.net", "ddownload.com", "katfile.com", "nitroflare.com",
                          "turbobit.net", "mega.nz", "filefactory.com", "uptobox.com"})
        list.append(host(QString::fromLatin1(h)));
    pushCatalog(ipc, {{QStringLiteral("hosts"), list}});
    const QByteArray colors[] = {png(QColor(0xE0, 0x40, 0x30)), png(QColor(0x30, 0x90, 0xE0))};
    pushCatalog(ipc, {{QStringLiteral("icons"), QCborMap{{QStringLiteral("rapidgator.net"), QCborValue(colors[0])},
                                                          {QStringLiteral("uploaded.net"), QCborValue(colors[1])}}},
                      {QStringLiteral("countries"), QCborMap{{QStringLiteral("rapidgator.net"), QStringLiteral("ru")},
                                                             {QStringLiteral("uploaded.net"), QStringLiteral("nl")},
                                                             {QStringLiteral("katfile.com"), QStringLiteral("de")}}}});
    c.resize(320, c.sizeHint().height());
    c.show();
    QVERIFY(QTest::qWaitForWindowExposed(&c));
    // anywhere in the name, as JD's search box
    QCompleter* comp = c.completer();
    comp->setCompletionPrefix(QStringLiteral("file"));
    QStringList hits;
    for (int i = 0; comp->setCurrentRow(i); ++i)
        hits << comp->currentCompletion();
    QCOMPARE(hits, (QStringList{QStringLiteral("filefactory.com"), QStringLiteral("katfile.com")}));
    if (const QString dir = qEnvironmentVariable("EMULE_GRAB_DIR"); !dir.isEmpty()) {
        // the typed search, then the whole list
        c.setEditText(QStringLiteral("t"));
        comp->setCompletionPrefix(QStringLiteral("t"));
        comp->complete();
        QTest::qWait(300);
        const QPixmap typed = comp->popup()->grab();
        comp->popup()->hide();
        c.setHost(QStringLiteral("rapidgator.net"));
        c.showPopup();
        QTest::qWait(300);
        const QPixmap list = c.view()->window()->grab();
        QPixmap shot(std::max(c.width(), list.width()) * 2 + 20, c.height() + std::max(typed.height(), list.height()));
        shot.fill(Qt::white);
        QPainter p(&shot);
        p.drawPixmap(0, 0, c.grab());
        p.drawPixmap(0, c.height(), list);
        p.drawPixmap(shot.width() / 2 + 10, c.height(), typed);
        p.end();
        shot.save(QDir(dir).filePath(QStringLiteral("hostercombo.png")));
        c.hidePopup();
    }
}

QTEST_MAIN(TestHosterCombo)
#include "tst_HosterCombo.moc"
