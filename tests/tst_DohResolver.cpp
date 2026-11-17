/// @file tst_DohResolver.cpp
/// @brief DNS over HTTPS (net/DohResolver): wire query, answer parsing, a lookup against a
/// loopback DoH responder, and the DNS policy behind a proxy (flags, HostResolver).

#include "FakeHttpServer.h"
#include "FakeProxyServer.h"
#include "geo/HostCountryResolver.h"
#include "net/DohResolver.h"
#include "net/HostResolver.h"
#include "prefs/Preferences.h"

#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>

using namespace eMule;

namespace {

/// A response to @p query carrying @p v4 and @p v6 (name compressed to the question)
QByteArray answerFor(const QByteArray& query, const QList<QHostAddress>& addrs, quint16 rcode = 0)
{
    QByteArray m = query;
    m[2] = char(0x81);                 // QR + RD
    m[3] = char(0x80 | rcode);         // RA + rcode
    const quint16 qtype = quint16((quint8(query.at(query.size() - 4)) << 8) | quint8(query.at(query.size() - 3)));
    // a CNAME first, as public resolvers send for aliases
    QByteArray answers = QByteArray("\xC0\x0C\x00\x05\x00\x01\x00\x00\x00\x3C\x00\x02\xC0\x0C", 14);
    int count = 1;
    for (const QHostAddress& a : addrs) {
        const bool v4 = a.protocol() == QAbstractSocket::IPv4Protocol;
        if ((qtype == dns::kTypeA) != v4)
            continue;
        answers += QByteArray("\xC0\x0C", 2);
        answers += char(0);
        answers += char(v4 ? 1 : 28);
        answers += QByteArray("\x00\x01\x00\x00\x00\x3C", 6);
        answers += char(0);
        answers += char(v4 ? 4 : 16);
        if (v4) {
            const quint32 ip = a.toIPv4Address();
            answers += char(ip >> 24);
            answers += char(ip >> 16);
            answers += char(ip >> 8);
            answers += char(ip);
        } else {
            const Q_IPV6ADDR ip = a.toIPv6Address();
            answers += QByteArray(reinterpret_cast<const char*>(ip.c), 16);
        }
        ++count;
    }
    m[6] = char(count >> 8);
    m[7] = char(count & 0xFF);
    return m + answers;
}

/// TXT response to @p query: one record of @p strings each, TTL @p ttl
QByteArray txtAnswerFor(const QByteArray& query, const QList<QList<QByteArray>>& records, quint32 ttl, quint16 rcode = 0)
{
    QByteArray m = query;
    m[2] = char(0x81);
    m[3] = char(0x80 | rcode);
    m[6] = char(records.size() >> 8);
    m[7] = char(records.size() & 0xFF);
    for (const QList<QByteArray>& strings : records) {
        QByteArray rdata;
        for (const QByteArray& str : strings)
            rdata += char(str.size()) + str;
        m += QByteArray("\xC0\x0C\x00\x10\x00\x01", 6);
        m += char(ttl >> 24);
        m += char(ttl >> 16);
        m += char(ttl >> 8);
        m += char(ttl);
        m += char(rdata.size() >> 8);
        m += char(rdata.size() & 0xFF);
        m += rdata;
    }
    return m;
}

/// A loopback DoH responder answering every A/AAAA query with @p addrs
void serveAddresses(FakeHttpServer& server, const QList<QHostAddress>& addrs)
{
    server.handler = [addrs](const QByteArray& path, const QByteArray&) {
        const QUrlQuery q(QUrl(QString::fromLatin1(path)));
        const QByteArray msg = QByteArray::fromBase64(q.queryItemValue(QStringLiteral("dns")).toLatin1(),
                                                      QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        return FakeHttpServer::Response{200, answerFor(msg, addrs)};
    };
}

} // namespace

class tst_DohResolver : public QObject {
    Q_OBJECT

private slots:
    void buildQuery_layout();
    void buildQuery_rejectsBadNames();
    void parse_answersWithCnameAndCompression();
    void parse_errorAndGarbage();
    void parseTxt_recordsTtlAndErrors();
    void localNames();
    void validUrls();
    void providers_areValid();
    void lookup_againstLoopbackResponder();
    void lookup_failureGivesEmpty();
    void countryDns_offBehindProxyWithoutDoh();
    void hostResolver_usesDohBehindProxy();
    void prefs_cleanDohUrls();
    void live_throughProxy_data();
    void live_throughProxy();
};

void tst_DohResolver::buildQuery_layout()
{
    const QByteArray q = dns::buildQuery(QStringLiteral("www.Example.com."), dns::kTypeAAAA);
    QCOMPARE(q.left(12), QByteArray("\x00\x00\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00", 12));
    QCOMPARE(q.mid(12), QByteArray("\x03www\x07" "example\x03" "com\x00\x00\x1C\x00\x01", 21));
    // IDN goes out as ACE
    QVERIFY(dns::buildQuery(QStringLiteral("bücher.de"), dns::kTypeA).contains("xn--bcher-kva"));
}

void tst_DohResolver::buildQuery_rejectsBadNames()
{
    QVERIFY(dns::buildQuery(QString(), dns::kTypeA).isEmpty());
    QVERIFY(dns::buildQuery(QStringLiteral("a..b"), dns::kTypeA).isEmpty());
    QVERIFY(dns::buildQuery(QString(64, u'a') + QStringLiteral(".com"), dns::kTypeA).isEmpty());
}

void tst_DohResolver::parse_answersWithCnameAndCompression()
{
    const QByteArray q4 = dns::buildQuery(QStringLiteral("host.example"), dns::kTypeA);
    const QByteArray q6 = dns::buildQuery(QStringLiteral("host.example"), dns::kTypeAAAA);
    const QList<QHostAddress> addrs{QHostAddress(QStringLiteral("192.0.2.7")), QHostAddress(QStringLiteral("198.51.100.1")),
                                    QHostAddress(QStringLiteral("2001:db8::5"))};
    QCOMPARE(dns::parseAddresses(answerFor(q4, addrs)),
             (QList<QHostAddress>{addrs.at(0), addrs.at(1)}));
    QCOMPARE(dns::parseAddresses(answerFor(q6, addrs)), (QList<QHostAddress>{addrs.at(2)}));
}

void tst_DohResolver::parse_errorAndGarbage()
{
    const QByteArray q = dns::buildQuery(QStringLiteral("host.example"), dns::kTypeA);
    QVERIFY(dns::parseAddresses(answerFor(q, {QHostAddress(QStringLiteral("192.0.2.7"))}, 3)).isEmpty());   // NXDOMAIN
    QVERIFY(dns::parseAddresses(q).isEmpty());   // a query, not a response
    QVERIFY(dns::parseAddresses(QByteArray("\x00\x00\x81\x80", 4)).isEmpty());
    // truncated inside the answer: what came before stays, no overread
    const QByteArray full = answerFor(q, {QHostAddress(QStringLiteral("192.0.2.7"))});
    for (qsizetype n = 0; n < full.size(); ++n)
        (void)dns::parseAddresses(full.left(n));
    // a pointer loop is no name: label length with reserved bits
    QByteArray bad = full;
    bad[12] = char(0x80);
    QVERIFY(dns::parseAddresses(bad).isEmpty());
}

void tst_DohResolver::parseTxt_recordsTtlAndErrors()
{
    const QByteArray q = dns::buildQuery(QStringLiteral("_ed3k.alice.example"), dns::kTypeTXT);
    QCOMPARE(q.right(4), QByteArray("\x00\x10\x00\x01", 4));
    const auto txt = dns::parseTxt(txtAnswerFor(q, {{"ed3k=1 ", "key=abc"}, {"v=spf1"}}, 600));
    QVERIFY(txt);
    QCOMPARE(txt->records, (QList<QByteArray>{"ed3k=1 key=abc", "v=spf1"}));
    QCOMPARE(txt->ttl, 600u);

    const auto nx = dns::parseTxt(txtAnswerFor(q, {}, 0, 3));   // NXDOMAIN: an answer, no record
    QVERIFY(nx);
    QVERIFY(nx->records.isEmpty());
    QVERIFY(!dns::parseTxt(txtAnswerFor(q, {}, 0, 2)));   // SERVFAIL: no answer
    QVERIFY(!dns::parseTxt(q));
    // a string length past its record
    QByteArray bad = txtAnswerFor(q, {{"abc"}}, 60);
    bad[bad.size() - 4] = char(9);
    QVERIFY(!dns::parseTxt(bad));
    const QByteArray full = txtAnswerFor(q, {{"abc", "def"}}, 60);
    for (qsizetype n = 0; n < full.size(); ++n)
        (void)dns::parseTxt(full.left(n));
}

void tst_DohResolver::localNames()
{
    for (const char* n : {"localhost", "LOCALHOST.", "nas", "printer.local", "box.lan", "a.internal", "x.home.arpa", "foo.localhost"})
        QVERIFY2(isLocalName(QLatin1StringView(n)), n);
    for (const char* n : {"example.com", "dns.google", "localhost.example.com", "lan.example"})
        QVERIFY2(!isLocalName(QLatin1StringView(n)), n);
}

void tst_DohResolver::validUrls()
{
    QVERIFY(isValidDohUrl(QStringLiteral("https://dns.example/dns-query")));
    QVERIFY(isValidDohUrl(QStringLiteral("https://192.0.2.1/dns-query")));
    QVERIFY(isValidDohUrl(QStringLiteral("http://127.0.0.1:8053/dns-query")));   // local DoH proxy
    QVERIFY(!isValidDohUrl(QStringLiteral("http://dns.example/dns-query")));      // clear text out
    QVERIFY(!isValidDohUrl(QStringLiteral("https://user:pw@dns.example/")));
    QVERIFY(!isValidDohUrl(QStringLiteral("dns.example")));
    QVERIFY(!isValidDohUrl(QString()));
}

void tst_DohResolver::providers_areValid()
{
    QVERIFY(!dohProviders().isEmpty());
    for (const DohProvider& p : dohProviders())
        QVERIFY2(isValidDohUrl(p.url), qPrintable(p.url));
    QVERIFY(!QHostAddress(QUrl(defaultDohUrl()).host()).isNull());   // reachable without DNS
}

void tst_DohResolver::lookup_againstLoopbackResponder()
{
    FakeHttpServer server;
    QVERIFY(server.isListening());
    const QList<QHostAddress> addrs{QHostAddress(QStringLiteral("192.0.2.9")), QHostAddress(QStringLiteral("2001:db8::9"))};
    server.handler = [&addrs](const QByteArray& path, const QByteArray&) {
        const QUrlQuery q(QUrl(QString::fromLatin1(path)));
        const QByteArray msg = QByteArray::fromBase64(q.queryItemValue(QStringLiteral("dns")).toLatin1(),
                                                      QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
        return FakeHttpServer::Response{200, answerFor(msg, addrs)};
    };
    QList<QHostAddress> got;
    bool done = false;
    dohLookup(server.url(QStringLiteral("/dns-query")).toString(), QStringLiteral("files.example"), this,
              [&](QList<QHostAddress> a) { got = a; done = true; });
    QTRY_VERIFY(done);
    QCOMPARE(got.size(), 2);
    QVERIFY(got.contains(addrs.at(0)));
    QVERIFY(got.contains(addrs.at(1)));
    QCOMPARE(server.requestCount(), 2);
    QCOMPARE(server.header("Accept"), QByteArray("application/dns-message"));
}

void tst_DohResolver::lookup_failureGivesEmpty()
{
    FakeHttpServer server;
    server.response = {500, {}};
    bool done = false;
    QList<QHostAddress> got{QHostAddress::LocalHost};
    dohLookup(server.url(QStringLiteral("/dns-query")).toString(), QStringLiteral("files.example"), this,
              [&](QList<QHostAddress> a) { got = a; done = true; });
    QTRY_VERIFY(done);
    QVERIFY(got.isEmpty());

    // a silent server: the timeout answers
    server.response = {200, {}, {}, true};
    done = false;
    dohLookup(server.url(QStringLiteral("/dns-query")).toString(), QStringLiteral("files.example"), this,
              [&](QList<QHostAddress> a) { got = a; done = true; }, 300);
    QTRY_VERIFY_WITH_TIMEOUT(done, 3000);
    QVERIFY(got.isEmpty());

    // an invalid URL never sends
    done = false;
    dohLookup(QStringLiteral("ftp://x/"), QStringLiteral("files.example"), this, [&](QList<QHostAddress>) { done = true; });
    QTRY_VERIFY(done);
}

void tst_DohResolver::countryDns_offBehindProxyWithoutDoh()
{
    const int oldType = thePrefs.proxyType();
    const QString oldHost = thePrefs.proxyHost();
    const QString oldDoh = thePrefs.proxyDohUrl();
    auto restore = qScopeGuard([&] {
        thePrefs.setProxyType(oldType);
        thePrefs.setProxyHost(oldHost);
        thePrefs.setProxyDohUrl(oldDoh);
    });
    thePrefs.setProxyType(0);
    QVERIFY(countryDnsAvailable());

    thePrefs.setProxyType(3);   // SOCKS5
    thePrefs.setProxyHost(QStringLiteral("192.0.2.1"));
    thePrefs.setProxyDohUrl(QString());
    QVERIFY(!countryDnsAvailable());
    bool done = false;
    QList<QHostAddress> got{QHostAddress::LocalHost};
    countryDnsLookup(QStringLiteral("localhost"), this, [&](QList<QHostAddress> a) { got = a; done = true; });
    QTRY_VERIFY(done);
    QVERIFY(got.isEmpty());   // no local resolver behind the proxy
    QVERIFY(cachedHostCountry(QStringLiteral("never-looked-up.example")).isEmpty());

    thePrefs.setProxyDohUrl(QStringLiteral("not a url"));
    QCOMPARE(thePrefs.proxyDohUrl(), defaultDohUrl());   // invalid falls back
    QVERIFY(countryDnsAvailable());
}

void tst_DohResolver::hostResolver_usesDohBehindProxy()
{
    const int oldType = thePrefs.proxyType();
    const QString oldHost = thePrefs.proxyHost();
    const QString oldDoh = thePrefs.proxyDohUrl();
    auto restore = qScopeGuard([&] {
        thePrefs.setProxyType(oldType);
        thePrefs.setProxyHost(oldHost);
        thePrefs.setProxyDohUrl(oldDoh);
    });
    FakeHttpServer server;
    QVERIFY(server.isListening());
    serveAddresses(server, {QHostAddress(QStringLiteral("192.0.2.33")), QHostAddress(QStringLiteral("2001:db8::33"))});
    thePrefs.setProxyType(3);   // SOCKS5; the loopback DoH URL itself bypasses it
    thePrefs.setProxyHost(QStringLiteral("192.0.2.1"));
    thePrefs.setProxyDohUrl(server.url(QStringLiteral("/dns-query")).toString());
    QCOMPARE(activeDohUrl(), thePrefs.proxyDohUrl());

    HostResolver resolver;
    std::optional<HostResolver::Result> got;
    resolver.resolve(QStringLiteral("server.example"), HostResolver::Preference::PreferIPv6, this,
                     [&](const HostResolver::Result& r) { got = r; });
    QTRY_VERIFY(got);
    QVERIFY2(got->ok(), qPrintable(got->errorString));
    QCOMPARE(got->first().toString(), QStringLiteral("2001:db8::33"));
    QCOMPARE(got->firstIPv4().toString(), QStringLiteral("192.0.2.33"));
    QCOMPARE(server.requestCount(), 2);

    // a local name stays with the system resolver
    got.reset();
    resolver.resolve(QStringLiteral("localhost"), HostResolver::Preference::IPv4Only, this,
                     [&](const HostResolver::Result& r) { got = r; });
    QTRY_VERIFY(got);
    QCOMPARE(server.requestCount(), 2);

    // cancel drops a DoH lookup's late answer
    server.response = {200, {}, {}, true};
    server.handler = {};
    bool called = false;
    const int token = resolver.resolve(QStringLiteral("other.example"), HostResolver::Preference::Any, this,
                                       [&](const HostResolver::Result&) { called = true; });
    resolver.cancel(token);
    QTest::qWait(50);
    QVERIFY(!called);

    // DoH off: the system resolver again
    thePrefs.setProxyDohUrl(QString());
    QVERIFY(activeDohUrl().isEmpty());
    thePrefs.setProxyType(0);
    QVERIFY(activeDohUrl().isEmpty());
}

void tst_DohResolver::prefs_cleanDohUrls()
{
    QCOMPARE(Preferences::cleanDohUrls({QStringLiteral(" https://doh.example/q "), QStringLiteral("https://doh.example/q"),
                                        defaultDohUrl(), QStringLiteral("http://doh.example/q"), QString()}),
             QStringList{QStringLiteral("https://doh.example/q")});
}

void tst_DohResolver::live_throughProxy_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("url");
    for (const DohProvider& p : dohProviders()) {
        QTest::addRow("socks5 %s", qPrintable(p.name)) << int(testing::FakeProxyServer::Kind::Socks5) << p.url;
        QTest::addRow("http %s", qPrintable(p.name)) << int(testing::FakeProxyServer::Kind::HttpConnect) << p.url;
    }
}

/// Every built-in endpoint through a SOCKS5 and an HTTP CONNECT proxy. Needs the internet:
/// EMULE_LIVE_DOH=1.
void tst_DohResolver::live_throughProxy()
{
    if (!qEnvironmentVariableIsSet("EMULE_LIVE_DOH"))
        QSKIP("live: set EMULE_LIVE_DOH=1");
    QFETCH(int, kind);
    QFETCH(QString, url);
    testing::FakeProxyServer proxy{static_cast<testing::FakeProxyServer::Kind>(kind)};
    const quint16 port = proxy.start();
    QVERIFY(port != 0);
    const int oldType = thePrefs.proxyType();
    const QString oldHost = thePrefs.proxyHost();
    const uint16 oldPort = thePrefs.proxyPort();
    auto restore = qScopeGuard([&] {
        thePrefs.setProxyType(oldType);
        thePrefs.setProxyHost(oldHost);
        thePrefs.setProxyPort(oldPort);
    });
    thePrefs.setProxyType(kind == int(testing::FakeProxyServer::Kind::Socks5) ? 3 : 5);
    thePrefs.setProxyHost(QStringLiteral("127.0.0.1"));
    thePrefs.setProxyPort(port);

    bool done = false;
    QList<QHostAddress> got;
    dohLookup(url, QStringLiteral("example.com"), this, [&](QList<QHostAddress> a) { got = a; done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
    QVERIFY2(!got.isEmpty(), qPrintable(url));
    QVERIFY(!proxy.targets().isEmpty());   // went through the proxy
    QVERIFY(proxy.targets().first().endsWith(QLatin1String(":443")));

    // TXT, as ed3k names ask
    std::optional<QByteArray> body;
    done = false;
    dohQuery(url, QStringLiteral("example.com"), dns::kTypeTXT, this,
             [&](std::optional<QByteArray> b) { body = std::move(b); done = true; });
    QTRY_VERIFY_WITH_TIMEOUT(done, 15000);
    QVERIFY2(body, qPrintable(url));
    const auto txt = dns::parseTxt(*body);
    QVERIFY2(txt && !txt->records.isEmpty(), qPrintable(url));
}

QTEST_GUILESS_MAIN(tst_DohResolver)
#include "tst_DohResolver.moc"
