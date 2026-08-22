/// @file tst_HttpCacheConfigLink.cpp
/// @brief The `/v1/info` handshake behind an `ed2k://|httpcache|` link.
///
/// Parsing the link is tst_ED2KLink's job. This is the step after it, and the one
/// that carries the weight: a configuration link is a URL somebody else chose, and
/// the handshake is the only thing standing between it and this client's uploads.
/// So the cases here are mostly the refusals — a host that is a web server and
/// nothing more, one that answers with something else's JSON, one that never
/// answers at all — plus the assertion that the probe itself carries no credential.

#include "FakeCacheServer.h"

#include "app/AppConfig.h"
#include "httpcache/HttpCacheServerProbe.h"
#include "protocol/ED2KLink.h"

#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using eMule::testing::FakeCacheReply;
using eMule::testing::FakeCacheServer;

class tst_HttpCacheConfigLink : public QObject {
    Q_OBJECT

private slots:
    void probeAcceptsACache();
    void probeSendsNoCredential();
    void probeRejectsAnotherService();
    void probeRejectsAnHtmlPage();
    void probeRejectsNotFound();
    void probeRejectsAFutureVersion();
    void probeRejectsAnOversizedBody();
    void probeRejectsAnUnreachableHost();
    void probeRejectsAnUnusableAddress();
    void probeSurvivesItsCallerDying();

private:
    /// Run one probe to completion and hand back the verdict.
    [[nodiscard]] static HttpCacheServerInfo probeSync(const QString& baseUrl);
};

HttpCacheServerInfo tst_HttpCacheConfigLink::probeSync(const QString& baseUrl)
{
    HttpCacheServerInfo result;
    bool done = false;

    HttpCacheServerProbe::probe(baseUrl, nullptr,
                                [&result, &done](const HttpCacheServerInfo& info) {
        result = info;
        done = true;
    });

    if (!QTest::qWaitFor([&done] { return done; }, 20'000))
        result.error = QStringLiteral("the probe never called back");
    return result;
}

// ---------------------------------------------------------------------------

void tst_HttpCacheConfigLink::probeAcceptsACache()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    const HttpCacheServerInfo info = probeSync(server.baseUrl());

    QVERIFY2(info.ok, qPrintable(info.error));
    QCOMPARE(info.service, QStringLiteral("emule-http-cache"));
    QCOMPARE(info.version, HttpCacheServerProbe::kSupportedVersion);
    QCOMPARE(info.implementation, QStringLiteral("fake"));
    QCOMPARE(info.uploadRequiresAuth, true);
    QCOMPARE(info.maxChunkSize, Q_UINT64_C(10485760));
    QCOMPARE(info.httpStatus, 200);
    QCOMPARE(server.infoCount(), 1);

    // A trailing slash is a link somebody typed, not a different server.
    const HttpCacheServerInfo again = probeSync(server.baseUrl() + QStringLiteral("/"));
    QVERIFY2(again.ok, qPrintable(again.error));
}

void tst_HttpCacheConfigLink::probeSendsNoCredential()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QVERIFY(probeSync(server.baseUrl()).ok);
    QCOMPARE(server.infoCount(), 1);

    // The point of the handshake is that the host has not proved anything yet.
    // Sending the key with it would hand the credential to whatever the link
    // named, which is exactly what the handshake exists to prevent.
    const QByteArray headers = server.lastInfoHeaders().toLower();
    QVERIFY2(!headers.contains("authorization"), server.lastInfoHeaders().constData());
    QVERIFY2(!headers.contains("x-api-key"), server.lastInfoHeaders().constData());

    // It does say who is asking, though. Left to Qt this would read "Mozilla/5.0",
    // which defeats the `eMule*` allow-list an operator behind a WAF has to write.
    const QByteArray agentLine = QByteArray("\r\nUser-Agent: ") + kUserAgent.toLatin1() + "\r\n";
    QVERIFY2(server.lastInfoHeaders().contains(agentLine),
             server.lastInfoHeaders().constData());
}

void tst_HttpCacheConfigLink::probeRejectsAnotherService()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setInfoReply({200, R"({"service":"some-other-thing","version":1})", {}, false});

    const HttpCacheServerInfo info = probeSync(server.baseUrl());

    QVERIFY(!info.ok);
    QCOMPARE(info.service, QStringLiteral("some-other-thing"));
    QVERIFY(info.error.contains(QStringLiteral("some-other-thing")));
}

void tst_HttpCacheConfigLink::probeRejectsAnHtmlPage()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setInfoReply({200, "<html><body>It works!</body></html>", {}, false});

    const HttpCacheServerInfo info = probeSync(server.baseUrl());

    QVERIFY(!info.ok);
    QVERIFY(!info.error.isEmpty());
}

void tst_HttpCacheConfigLink::probeRejectsNotFound()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setInfoReply({404, R"({"error":"nope","status":404})", {}, false});

    const HttpCacheServerInfo info = probeSync(server.baseUrl());

    QVERIFY(!info.ok);
    QCOMPARE(info.httpStatus, 404);
}

void tst_HttpCacheConfigLink::probeRejectsAFutureVersion()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setInfoReply({200, R"({"service":"emule-http-cache","version":99})", {}, false});

    const HttpCacheServerInfo info = probeSync(server.baseUrl());

    // Right service, wrong dialect. Refusing beats storing a credential for a
    // contract this client cannot hold up its end of.
    QVERIFY(!info.ok);
    QCOMPARE(info.version, 99);
    QVERIFY(info.error.contains(QStringLiteral("99")));
}

void tst_HttpCacheConfigLink::probeRejectsAnOversizedBody()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    // An /v1/info is a few hundred bytes. A megabyte of it means the URL is not
    // one, and the read stops rather than buffering whatever arrives.
    QByteArray huge = R"({"service":"emule-http-cache","version":1,"pad":")";
    huge += QByteArray(1024 * 1024, 'x');
    huge += R"("})";
    server.setInfoReply({200, huge, {}, false});

    const HttpCacheServerInfo info = probeSync(server.baseUrl());

    QVERIFY(!info.ok);
    QVERIFY(!info.error.isEmpty());
}

void tst_HttpCacheConfigLink::probeRejectsAnUnreachableHost()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setInfoReply({200, {}, {}, /*closeWithoutAnswering=*/true});

    const HttpCacheServerInfo info = probeSync(server.baseUrl());

    QVERIFY(!info.ok);
    QCOMPARE(info.httpStatus, 0);
    QVERIFY(!info.error.isEmpty());
}

void tst_HttpCacheConfigLink::probeRejectsAnUnusableAddress()
{
    // Refused without a socket ever opening, and — the part that matters for a
    // caller — still through the callback, so no flow can silently stall on one.
    for (const QString& bad : {QStringLiteral(""),
                               QStringLiteral("not a url"),
                               QStringLiteral("ftp://example.com"),
                               QStringLiteral("/relative")}) {
        const HttpCacheServerInfo info = probeSync(bad);
        QVERIFY2(!info.ok, qPrintable(bad));
        QVERIFY2(!info.error.isEmpty(), qPrintable(bad));
    }
}

void tst_HttpCacheConfigLink::probeSurvivesItsCallerDying()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    auto* context = new QObject;
    bool called = false;
    HttpCacheServerProbe::probe(server.baseUrl(), context,
                                [&called](const HttpCacheServerInfo&) { called = true; });

    // The GUI dialog that started a probe can be closed while it is in flight.
    // The callback must not run into the wreckage — and the request must still
    // finish and clean itself up rather than leaking a manager per abandoned probe.
    delete context;

    QTest::qWait(1500);
    QVERIFY(!called);
    QCOMPARE(server.infoCount(), 1);
}

QTEST_MAIN(tst_HttpCacheConfigLink)
#include "tst_HttpCacheConfigLink.moc"
