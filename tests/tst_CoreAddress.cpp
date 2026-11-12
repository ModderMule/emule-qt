/// @file tst_CoreAddress.cpp
/// @brief CoreAttachment::isRemoteAddress: is the configured core this machine or another?
/// Also where a name listen value binds and is dialed (Preferences::ipcBindAddressesOf).
///
/// Local = the GUI finds, launches and owns the core; remote = it only connects.
/// Taking a local core as remote let GUI exit write stale prefs over the core's
/// (#120); taking a remote hostname as local made the GUI launch a core of its own.
/// DNS goes through the test seam, so no case needs a network.

#include "TestHelpers.h"

#include "app/CoreAttachment.h"
#include "net/OwnAddress.h"
#include "prefs/Preferences.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QTest>

using eMule::CoreAttachment;
using eMule::Preferences;
namespace OwnAddress = eMule::OwnAddress;

class tst_CoreAddress : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void wildcardsAndEmptyAreLocal_data();
    void wildcardsAndEmptyAreLocal();
    void loopbackLiteralsAreLocal();
    void ownInterfaceAddressIsLocal();
    void foreignLiteralIsRemote();
    void localhostNamesSkipDns();
    void ownHostNameSkipsDns();
    void nameResolvingToOwnAddressesIsLocal();
    void nameResolvingToForeignAddressIsRemote();
    void nameWithMixedAddressesIsRemote();
    void unresolvableNameIsRemote();
    void timedOutLookupIsRemoteAndRetried();
    void resolvedNameIsCached();
    void nameIsCaseAndTrailingDotInsensitive();
    void dialHostOfLocalNameIsLoopback();
    void dialHostOfRemoteNameKeepsName();
    void dialHostOfLiterals_data();
    void dialHostOfLiterals();
    void dialHostResolvesOnce();
    void bindOfNameIsLoopbackPlusOwnPerFamily();
    void bindOfNameDropsForeignAndLoopback();
    void bindOfForeignOrUnresolvedNameIsLoopback();
    void bindSkipsLinkLocal();
    void dialHostOfLanNameIsLoopback();
    void refreshReplacesCachedAnswer();
    void failedRefreshKeepsCachedAnswer();

private:
    /// DNS answer per name; a missing name times out
    QHash<QString, QList<QHostAddress>> m_dns;
    QStringList m_asked;   ///< names looked up, in order
};

void tst_CoreAddress::init()
{
    m_dns.clear();
    m_asked.clear();
    OwnAddress::setLookup(
        [this](const QString& name, int) -> std::optional<QList<QHostAddress>> {
            m_asked << name;
            if (const auto it = m_dns.constFind(name); it != m_dns.cend())
                return *it;
            return std::nullopt;
        });
}

void tst_CoreAddress::cleanup()
{
    OwnAddress::setLookup({});
}

void tst_CoreAddress::wildcardsAndEmptyAreLocal_data()
{
    QTest::addColumn<QString>("host");
    QTest::newRow("empty") << QString();
    QTest::newRow("blank") << QStringLiteral("  ");
    QTest::newRow("star") << QStringLiteral("*");
    QTest::newRow("any4") << QStringLiteral("0.0.0.0");
    QTest::newRow("any6") << QStringLiteral("::");
}

void tst_CoreAddress::wildcardsAndEmptyAreLocal()
{
    QFETCH(QString, host);
    QVERIFY(!CoreAttachment::isRemoteAddress(host));
    QVERIFY(m_asked.isEmpty());
}

void tst_CoreAddress::loopbackLiteralsAreLocal()
{
    QVERIFY(!CoreAttachment::isRemoteAddress(QStringLiteral("127.0.0.1")));
    QVERIFY(!CoreAttachment::isRemoteAddress(QStringLiteral("127.0.1.1")));
    QVERIFY(!CoreAttachment::isRemoteAddress(QStringLiteral("::1")));
    QVERIFY(!CoreAttachment::isRemoteAddress(QStringLiteral(" ::1 ")));
    QVERIFY(m_asked.isEmpty());
}

void tst_CoreAddress::ownInterfaceAddressIsLocal()
{
    const auto own = QNetworkInterface::allAddresses();
    for (const QHostAddress& addr : own) {
        if (addr.isLoopback())
            continue;
        QVERIFY2(!CoreAttachment::isRemoteAddress(addr.toString()), qPrintable(addr.toString()));
        return;
    }
    QSKIP("no non-loopback interface address");
}

void tst_CoreAddress::foreignLiteralIsRemote()
{
    // TEST-NET, never assigned to a host
    QVERIFY(CoreAttachment::isRemoteAddress(QStringLiteral("192.0.2.10")));
    QVERIFY(CoreAttachment::isRemoteAddress(QStringLiteral("2001:db8::10")));
    QVERIFY(m_asked.isEmpty());
}

void tst_CoreAddress::localhostNamesSkipDns()
{
    QVERIFY(!CoreAttachment::isRemoteAddress(QStringLiteral("localhost")));
    QVERIFY(!CoreAttachment::isRemoteAddress(QStringLiteral("LocalHost.")));
    QVERIFY(!CoreAttachment::isRemoteAddress(QStringLiteral("core.localhost")));
    QVERIFY(m_asked.isEmpty());
}

void tst_CoreAddress::ownHostNameSkipsDns()
{
    const QString self = QHostInfo::localHostName();
    if (self.isEmpty())
        QSKIP("no local host name");
    QVERIFY(!CoreAttachment::isRemoteAddress(self));
    QVERIFY(!CoreAttachment::isRemoteAddress(self.toUpper()));
    QVERIFY(!CoreAttachment::isRemoteAddress(self + QStringLiteral(".local")));
    QVERIFY(!CoreAttachment::isRemoteAddress(self.section(u'.', 0, 0)));
    QVERIFY(m_asked.isEmpty());
}

void tst_CoreAddress::nameResolvingToOwnAddressesIsLocal()
{
    m_dns.insert(QStringLiteral("me.example"),
                 {QHostAddress(QStringLiteral("127.0.0.1")), QHostAddress(QStringLiteral("::1"))});
    QVERIFY(!CoreAttachment::isRemoteAddress(QStringLiteral("me.example")));
    QCOMPARE(m_asked, QStringList{QStringLiteral("me.example")});
}

void tst_CoreAddress::nameResolvingToForeignAddressIsRemote()
{
    m_dns.insert(QStringLiteral("nas.example"), {QHostAddress(QStringLiteral("192.0.2.20"))});
    QVERIFY(CoreAttachment::isRemoteAddress(QStringLiteral("nas.example")));
}

void tst_CoreAddress::nameWithMixedAddressesIsRemote()
{
    // the dial may land on the foreign one
    m_dns.insert(QStringLiteral("mixed.example"),
                 {QHostAddress(QStringLiteral("127.0.0.1")),
                  QHostAddress(QStringLiteral("198.51.100.7"))});
    QVERIFY(CoreAttachment::isRemoteAddress(QStringLiteral("mixed.example")));
}

void tst_CoreAddress::unresolvableNameIsRemote()
{
    m_dns.insert(QStringLiteral("gone.example"), {});
    QVERIFY(CoreAttachment::isRemoteAddress(QStringLiteral("gone.example")));
}

void tst_CoreAddress::timedOutLookupIsRemoteAndRetried()
{
    QVERIFY(CoreAttachment::isRemoteAddress(QStringLiteral("slow.example")));
    // not cached: once DNS answers, the name is judged again
    m_dns.insert(QStringLiteral("slow.example"), {QHostAddress(QStringLiteral("127.0.0.1"))});
    QVERIFY(!CoreAttachment::isRemoteAddress(QStringLiteral("slow.example")));
    QCOMPARE(m_asked.size(), 2);
}

void tst_CoreAddress::resolvedNameIsCached()
{
    m_dns.insert(QStringLiteral("nas.example"), {QHostAddress(QStringLiteral("192.0.2.20"))});
    QVERIFY(CoreAttachment::isRemoteAddress(QStringLiteral("nas.example")));
    QVERIFY(CoreAttachment::isRemoteAddress(QStringLiteral("nas.example")));
    QCOMPARE(m_asked.size(), 1);

    // the seam reset drops the cache
    OwnAddress::setLookup([this](const QString& name, int) {
        m_asked << name;
        return std::optional{QList<QHostAddress>{QHostAddress(QStringLiteral("::1"))}};
    });
    QVERIFY(!CoreAttachment::isRemoteAddress(QStringLiteral("nas.example")));
    QCOMPARE(m_asked.size(), 2);
}

void tst_CoreAddress::nameIsCaseAndTrailingDotInsensitive()
{
    m_dns.insert(QStringLiteral("nas.example"), {QHostAddress(QStringLiteral("192.0.2.20"))});
    QVERIFY(CoreAttachment::isRemoteAddress(QStringLiteral("NAS.Example.")));
    QCOMPARE(m_asked, QStringList{QStringLiteral("nas.example")});
}

void tst_CoreAddress::dialHostOfLocalNameIsLoopback()
{
    // a name core listens on loopback too: stable while its LAN address changes
    m_dns.insert(QStringLiteral("me.example"), {QHostAddress(QStringLiteral("::1"))});
    QCOMPARE(CoreAttachment::dialHost(QStringLiteral("me.example")), QStringLiteral("127.0.0.1"));
    QCOMPARE(CoreAttachment::dialHost(QStringLiteral("localhost")), QStringLiteral("127.0.0.1"));
}

void tst_CoreAddress::dialHostOfRemoteNameKeepsName()
{
    m_dns.insert(QStringLiteral("nas.example"), {QHostAddress(QStringLiteral("192.0.2.20"))});
    QCOMPARE(CoreAttachment::dialHost(QStringLiteral(" nas.example ")), QStringLiteral("nas.example"));
    // timed out = remote: dialed by name, QTcpSocket resolves again
    QCOMPARE(CoreAttachment::dialHost(QStringLiteral("slow.example")), QStringLiteral("slow.example"));
}

void tst_CoreAddress::dialHostOfLiterals_data()
{
    QTest::addColumn<QString>("host");
    QTest::addColumn<QString>("dial");
    QTest::newRow("empty") << QString() << QStringLiteral("127.0.0.1");
    QTest::newRow("star") << QStringLiteral("*") << QStringLiteral("127.0.0.1");
    QTest::newRow("any4") << QStringLiteral("0.0.0.0") << QStringLiteral("127.0.0.1");
    QTest::newRow("any6") << QStringLiteral("::") << QStringLiteral("::1");
    QTest::newRow("loop6") << QStringLiteral("::1") << QStringLiteral("::1");
    QTest::newRow("foreign") << QStringLiteral("192.0.2.10") << QStringLiteral("192.0.2.10");
}

void tst_CoreAddress::dialHostOfLiterals()
{
    QFETCH(QString, host);
    QFETCH(QString, dial);
    QCOMPARE(CoreAttachment::dialHost(host), dial);
    QVERIFY(m_asked.isEmpty());
}

void tst_CoreAddress::dialHostResolvesOnce()
{
    m_dns.insert(QStringLiteral("nas.example"), {QHostAddress(QStringLiteral("192.0.2.20"))});
    QVERIFY(CoreAttachment::isRemoteAddress(QStringLiteral("nas.example")));
    QCOMPARE(CoreAttachment::dialHost(QStringLiteral("nas.example")), QStringLiteral("nas.example"));
    QCOMPARE(m_asked.size(), 1);
}

void tst_CoreAddress::bindOfNameIsLoopbackPlusOwnPerFamily()
{
    QHostAddress own4, own6;
    for (const QHostAddress& own : QNetworkInterface::allAddresses()) {
        if (own.isLoopback() || own.isLinkLocal())
            continue;
        QHostAddress& slot = own.protocol() == QAbstractSocket::IPv4Protocol ? own4 : own6;
        if (slot.isNull())
            slot = own;
    }
    if (own4.isNull() && own6.isNull())
        QSKIP("no routable interface address");
    // loopback first, as macOS answers for its own .local name; each family once
    QList<QHostAddress> answer{QHostAddress(QStringLiteral("::1"))};
    QList<QHostAddress> expected{QHostAddress(QHostAddress::LocalHost)};
    for (const QHostAddress& own : {own4, own6}) {
        if (own.isNull())
            continue;
        answer << own << own;
        expected << own;
    }
    m_dns.insert(QStringLiteral("me.example"), answer);
    const auto bind = Preferences::ipcBindAddressesOf(QStringLiteral(" Me.Example. "));
    QVERIFY(bind);
    QCOMPARE(*bind, expected);
    QCOMPARE(m_asked, QStringList{QStringLiteral("me.example")});
}

void tst_CoreAddress::bindOfNameDropsForeignAndLoopback()
{
    m_dns.insert(QStringLiteral("me.example"),
                 {QHostAddress(QStringLiteral("198.51.100.7")), QHostAddress(QStringLiteral("::1"))});
    const QList<QHostAddress> loop{QHostAddress(QHostAddress::LocalHost)};
    QCOMPARE(Preferences::ipcBindAddressesOf(QStringLiteral("me.example")), loop);
    // localhost needs no DNS
    QCOMPARE(Preferences::ipcBindAddressesOf(QStringLiteral("localhost")), loop);
    QCOMPARE(m_asked, QStringList{QStringLiteral("me.example")});
}

void tst_CoreAddress::bindOfForeignOrUnresolvedNameIsLoopback()
{
    m_dns.insert(QStringLiteral("nas.example"), {QHostAddress(QStringLiteral("192.0.2.20"))});
    m_dns.insert(QStringLiteral("gone.example"), {});
    const QHostAddress loop(QHostAddress::LocalHost);
    // the first address (probes, local dial) asks no DNS
    QCOMPARE(Preferences::ipcBindAddressOf(QStringLiteral("nas.example")), loop);
    QVERIFY(m_asked.isEmpty());
    QCOMPARE(Preferences::ipcBindAddressesOf(QStringLiteral("nas.example")), QList{loop});
    QCOMPARE(Preferences::ipcBindAddressesOf(QStringLiteral("gone.example")), QList{loop});
    QVERIFY(!Preferences::ipcBindAddressesOf(QStringLiteral("slow.example")));
}

void tst_CoreAddress::bindSkipsLinkLocal()
{
    // fe80:: cannot bind without a scope id
    QHostAddress linkLocal;
    for (const QHostAddress& own : QNetworkInterface::allAddresses()) {
        if (own.isLinkLocal() && own.protocol() == QAbstractSocket::IPv6Protocol) {
            linkLocal = own;
            break;
        }
    }
    if (linkLocal.isNull())
        QSKIP("no link-local address");
    m_dns.insert(QStringLiteral("me.example"), {linkLocal});
    QCOMPARE(Preferences::ipcBindAddressesOf(QStringLiteral("me.example")),
             QList{QHostAddress(QHostAddress::LocalHost)});
}

void tst_CoreAddress::dialHostOfLanNameIsLoopback()
{
    QHostAddress lan;
    for (const QHostAddress& own : QNetworkInterface::allAddresses()) {
        if (!own.isLoopback() && !own.isLinkLocal()) {
            lan = own;
            break;
        }
    }
    if (lan.isNull())
        QSKIP("no routable interface address");
    // local, and the core listens on loopback too: no token, survives a new LAN address
    m_dns.insert(QStringLiteral("me.example"), {lan});
    QVERIFY(!CoreAttachment::isRemoteAddress(QStringLiteral("me.example")));
    QCOMPARE(CoreAttachment::dialHost(QStringLiteral("me.example")), QStringLiteral("127.0.0.1"));
    QCOMPARE(m_asked.size(), 1);
}

void tst_CoreAddress::refreshReplacesCachedAnswer()
{
    const QHostAddress a(QStringLiteral("192.0.2.1")), b(QStringLiteral("192.0.2.2"));
    m_dns.insert(QStringLiteral("nas.example"), {a});
    QCOMPARE(OwnAddress::resolve(QStringLiteral("nas.example")), QList{a});
    m_dns.insert(QStringLiteral("nas.example"), {b});
    QCOMPARE(OwnAddress::resolve(QStringLiteral("nas.example")), QList{a});   // cached
    bool answered = false;
    OwnAddress::refresh(QStringLiteral("NAS.example"), this, [&](bool ok) { answered = ok; });
    QVERIFY(answered);
    QCOMPARE(OwnAddress::resolve(QStringLiteral("nas.example")), QList{b});
    QCOMPARE(m_asked.size(), 2);
}

void tst_CoreAddress::failedRefreshKeepsCachedAnswer()
{
    // DNS down: the core keeps listening where it does
    const QHostAddress a(QStringLiteral("192.0.2.1"));
    m_dns.insert(QStringLiteral("nas.example"), {a});
    QCOMPARE(OwnAddress::resolve(QStringLiteral("nas.example")), QList{a});
    m_dns.remove(QStringLiteral("nas.example"));
    bool called = false, answered = true;
    OwnAddress::refresh(QStringLiteral("nas.example"), this, [&](bool ok) {
        called = true;
        answered = ok;
    });
    QVERIFY(called);
    QVERIFY(!answered);
    QCOMPARE(OwnAddress::resolve(QStringLiteral("nas.example")), QList{a});
}

QTEST_GUILESS_MAIN(tst_CoreAddress)
#include "tst_CoreAddress.moc"
