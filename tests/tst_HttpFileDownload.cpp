/// @file tst_HttpFileDownload.cpp
/// @brief Tests for net/HttpFileDownload — the shared config-file downloader.
///
/// Covers fetch() / fetchBlocking(): unwrapping, errors, manual redirects, conditional
/// requests and abort. Served from an in-process QTcpServer so there is no external
/// dependency and nothing to be flaky about.

#include "FakeHttpServer.h"
#include "TestHelpers.h"
#include "net/BindAddress.h"
#include "net/HttpFileDownload.h"
#include "prefs/Preferences.h"

#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <zlib.h>

using namespace eMule;

namespace {

/// gzip-compress @p data (windowBits 16+MAX_WBITS selects the gzip wrapper).
QByteArray gzipCompress(const QByteArray& data)
{
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) {
        return {};
    }
    QByteArray out(static_cast<qsizetype>(deflateBound(&zs, static_cast<uLong>(data.size()))) + 32,
                   Qt::Uninitialized);
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
    zs.avail_in = static_cast<uInt>(data.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());

    const int rc = deflate(&zs, Z_FINISH);
    const auto written = static_cast<qsizetype>(zs.total_out);
    deflateEnd(&zs);
    if (rc != Z_STREAM_END)
        return {};
    out.truncate(written);
    return out;
}

/// Drive the event loop until the async callback fires (or we give up).
struct Capture {
    bool called = false;
    int calls = 0;
    HttpFileDownload::Result r;

    [[nodiscard]] HttpFileDownload::Callback cb()
    {
        return [this](const HttpFileDownload::Result& res) {
            called = true;
            ++calls;
            r = res;
        };
    }
};

FakeHttpServer::Response ok(QByteArray body,
                            QList<std::pair<QByteArray, QByteArray>> headers = {})
{
    return {200, std::move(body), std::move(headers)};
}

} // namespace

class tst_HttpFileDownload : public QObject {
    Q_OBJECT

private slots:
    void fetch_unwrapsGzippedPayload();
    void fetch_passesPlainPayloadThrough();
    void fetch_reportsHttpError();
    void fetch_redactUrlsHidesUrl();
    void fetch_sendsUserAgent();
    void fetch_heldWhileBoundInterfaceMissing();
    void fetch_returnsValidators();
    void fetch_sendsValidatorsAndReports304();
    void fetch_304WithoutConditionIsError();
    void fetch_followsRedirect();
    void fetch_dropsSameHostHeadersOffHost();
    void fetch_redirectLimit();
    void fetch_refusesNonHttpRedirect();
    void fetch_abortStillCallsBack();
    void fetch_contextDestroyedNoCallback();
    void fetchBlocking_returnsData();
};

// ---------------------------------------------------------------------------

void tst_HttpFileDownload::fetch_unwrapsGzippedPayload()
{
    // A list mirror serving ipfilter.dat.gz must reach the caller as text.
    const QByteArray plain = "# Level 1\n1.2.3.0 - 1.2.3.255 , 0 , blocked\n";
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.response = ok(gzipCompress(plain));

    QObject ctx;
    Capture cap;
    HttpFileDownload::Options opts;
    opts.preferredNames = {QStringLiteral("ipfilter.dat")};
    HttpFileDownload::fetch(&ctx, server.url(), opts, cap.cb());

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY2(cap.r.ok, qPrintable(cap.r.error));
    QCOMPARE(cap.r.data, plain);
    QCOMPARE(cap.r.httpStatus, 200);
}

void tst_HttpFileDownload::fetch_passesPlainPayloadThrough()
{
    const QByteArray plain = "# Level 1\n1.2.3.0 - 1.2.3.255 , 0 , blocked\n";
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.response = ok(plain);

    QObject ctx;
    Capture cap;
    HttpFileDownload::fetch(&ctx, server.url(), {}, cap.cb());

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY2(cap.r.ok, qPrintable(cap.r.error));
    QCOMPARE(cap.r.data, plain);
    QVERIFY(cap.r.entryName.isEmpty());   // nothing was unpacked
    QVERIFY(!cap.r.notModified);
}

void tst_HttpFileDownload::fetch_reportsHttpError()
{
    // A 404 must surface as a failure, not as an empty-but-successful download that
    // then overwrites a good config file with nothing.
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.response = {404, "not found"};

    QObject ctx;
    Capture cap;
    HttpFileDownload::fetch(&ctx, server.url(), {}, cap.cb());

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY(!cap.r.ok);
    QCOMPARE(cap.r.httpStatus, 404);
    QVERIFY(!cap.r.error.isEmpty());
    QVERIFY(cap.r.data.isEmpty());
}

void tst_HttpFileDownload::fetch_redactUrlsHidesUrl()
{
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.response = {404, "not found"};

    QObject ctx;
    Capture cap;
    HttpFileDownload::Options opts;
    opts.redactUrls = true;
    HttpFileDownload::fetch(&ctx, server.url(QStringLiteral("/x?key=SeCrEt")), opts, cap.cb());

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY(!cap.r.ok);
    QCOMPARE(cap.r.error, QStringLiteral("HTTP 404"));
}

void tst_HttpFileDownload::fetch_sendsUserAgent()
{
    // Only one of the four original call sites identified itself; the shared helper must
    // do it for all of them, since some mirrors reject an empty User-Agent.
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.response = ok("data");

    QObject ctx;
    Capture cap;
    HttpFileDownload::fetch(&ctx, server.url(), {}, cap.cb());

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY(server.header("User-Agent").startsWith("eMuleQt/"));
}

// HTTP cannot be pinned to an interface, so with the selected one missing nothing is sent.
void tst_HttpFileDownload::fetch_heldWhileBoundInterfaceMissing()
{
    QTcpServer server;   // would accept the connection if one were made
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const QUrl url(QStringLiteral("http://127.0.0.1:%1/server.met").arg(server.serverPort()));

    const QString before = thePrefs.bindAddress();
    thePrefs.setBindAddress(QStringLiteral("vpn-not-there0"));
    const auto restore = qScopeGuard([&] {
        thePrefs.setBindAddress(before);
        BindAddress::refresh();
    });

    QObject ctx;
    Capture cap;
    HttpFileDownload::fetch(&ctx, url, {}, cap.cb());
    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 3000);
    QVERIFY(!cap.r.ok);
    QVERIFY2(cap.r.error.contains(QStringLiteral("vpn-not-there0")), qPrintable(cap.r.error));

    const HttpFileDownload::Result blocking = HttpFileDownload::fetchBlocking(url, {});
    QVERIFY(!blocking.ok);
    QVERIFY(!blocking.error.isEmpty());

    QVERIFY(!server.hasPendingConnections());
}

void tst_HttpFileDownload::fetch_returnsValidators()
{
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.response = ok("data", {{"ETag", "\"6ae623cb-2d1503\""},
                                  {"Last-Modified", "Sat, 31 Oct 2026 17:22:19 GMT"}});

    QObject ctx;
    Capture cap;
    HttpFileDownload::fetch(&ctx, server.url(), {}, cap.cb());

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY2(cap.r.ok, qPrintable(cap.r.error));
    QCOMPARE(cap.r.validators.etag, QByteArray("\"6ae623cb-2d1503\""));
    QCOMPARE(cap.r.validators.lastModified, QByteArray("Sat, 31 Oct 2026 17:22:19 GMT"));
    // Nothing was sent: an unconditional request
    QVERIFY(server.header("If-None-Match").isEmpty());
    QVERIFY(server.header("If-Modified-Since").isEmpty());
}

void tst_HttpFileDownload::fetch_sendsValidatorsAndReports304()
{
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.response = {304, {}, {{"ETag", "\"abc\""}}};

    QObject ctx;
    Capture cap;
    HttpFileDownload::Options opts;
    opts.validators = {"\"abc\"", "Sat, 31 Oct 2026 17:22:19 GMT"};
    HttpFileDownload::fetch(&ctx, server.url(), opts, cap.cb());

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY2(cap.r.ok, qPrintable(cap.r.error));
    QVERIFY(cap.r.notModified);
    QCOMPARE(cap.r.httpStatus, 304);
    QVERIFY(cap.r.data.isEmpty());
    QCOMPARE(server.header("If-None-Match"), QByteArray("\"abc\""));
    QCOMPARE(server.header("If-Modified-Since"), QByteArray("Sat, 31 Oct 2026 17:22:19 GMT"));
}

void tst_HttpFileDownload::fetch_304WithoutConditionIsError()
{
    // A 304 to a plain request has no copy to fall back on
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.response = {304, {}};

    QObject ctx;
    Capture cap;
    HttpFileDownload::fetch(&ctx, server.url(), {}, cap.cb());

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY(!cap.r.ok);
    QVERIFY(!cap.r.notModified);
}

void tst_HttpFileDownload::fetch_followsRedirect()
{
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.handler = [](const QByteArray& path, const QByteArray&) -> FakeHttpServer::Response {
        if (path == "/old")
            return {302, {}, {{"Location", "/new"}}};
        return ok("moved data");
    };

    QObject ctx;
    Capture cap;
    HttpFileDownload::Options opts;
    opts.validators = {"\"v1\"", {}};
    HttpFileDownload::fetch(&ctx, server.url(QStringLiteral("/old")), opts, cap.cb());

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY2(cap.r.ok, qPrintable(cap.r.error));
    QCOMPARE(cap.r.data, QByteArray("moved data"));
    QCOMPARE(server.requestCount(), 2);
    // The validators describe the file, so they follow it
    QCOMPARE(server.header("If-None-Match", 1), QByteArray("\"v1\""));
}

void tst_HttpFileDownload::fetch_dropsSameHostHeadersOffHost()
{
    // A presigned storage URL rejects any Authorization header, so it stays behind
    FakeHttpServer storage(QHostAddress::Any);
    QVERIFY(storage.isListening());
    storage.response = ok("file");
    FakeHttpServer origin;
    QVERIFY(origin.isListening());
    origin.response = {302, {}, {{"Location",
        storage.url(QStringLiteral("/bucket?X-Amz-Signature=abc"), QStringLiteral("localhost"))
            .toEncoded()}}};

    QObject ctx;
    Capture cap;
    HttpFileDownload::Options opts;
    opts.sameHostHeaders = {{"Authorization", "Basic c2VjcmV0"}};
    opts.headers = {{"X-Every-Hop", "1"}};
    HttpFileDownload::fetch(&ctx, origin.url(), opts, cap.cb());

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY2(cap.r.ok, qPrintable(cap.r.error));
    QCOMPARE(origin.header("Authorization"), QByteArray("Basic c2VjcmV0"));
    QVERIFY(storage.header("Authorization").isEmpty());
    QCOMPARE(storage.header("X-Every-Hop"), QByteArray("1"));
}

void tst_HttpFileDownload::fetch_redirectLimit()
{
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.response = {302, {}, {{"Location", "/list.dat"}}};

    QObject ctx;
    Capture cap;
    HttpFileDownload::fetch(&ctx, server.url(), {}, cap.cb());

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY(!cap.r.ok);
    QCOMPARE(server.requestCount(), kMaxRedirects + 1);
}

void tst_HttpFileDownload::fetch_refusesNonHttpRedirect()
{
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.response = {302, {}, {{"Location", "file:///etc/passwd"}}};

    QObject ctx;
    Capture cap;
    HttpFileDownload::fetch(&ctx, server.url(), {}, cap.cb());

    QTRY_VERIFY_WITH_TIMEOUT(cap.called, 10000);
    QVERIFY(!cap.r.ok);
    QCOMPARE(server.requestCount(), 1);
}

void tst_HttpFileDownload::fetch_abortStillCallsBack()
{
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.response.silent = true;

    QObject ctx;
    Capture cap;
    const QPointer<HttpDownloadJob> job = HttpFileDownload::fetch(&ctx, server.url(), {}, cap.cb());
    QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 10000);
    QVERIFY(job);
    job->abort();

    QVERIFY(cap.called);
    QVERIFY(!cap.r.ok);
    QCOMPARE(cap.r.error, QStringLiteral("canceled"));
    job->abort();   // a second abort is a no-op
    QCOMPARE(cap.calls, 1);
    QTRY_VERIFY(!job);   // deletes itself
}

void tst_HttpFileDownload::fetch_contextDestroyedNoCallback()
{
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.response.silent = true;

    Capture cap;
    {
        QObject ctx;
        HttpFileDownload::fetch(&ctx, server.url(), {}, cap.cb());
        QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(), 1, 10000);
    }
    QTest::qWait(50);
    QVERIFY(!cap.called);
}

void tst_HttpFileDownload::fetchBlocking_returnsData()
{
    FakeHttpServer server;
    QVERIFY(server.isListening());
    server.response = ok("blocking data", {{"ETag", "\"b\""}});

    const HttpFileDownload::Result r = HttpFileDownload::fetchBlocking(server.url(), {});
    QVERIFY2(r.ok, qPrintable(r.error));
    QCOMPARE(r.data, QByteArray("blocking data"));
    QCOMPARE(r.validators.etag, QByteArray("\"b\""));
}

QTEST_MAIN(tst_HttpFileDownload)
#include "tst_HttpFileDownload.moc"
