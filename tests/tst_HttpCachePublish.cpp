/// @file tst_HttpCachePublish.cpp
/// @brief HttpCachePublisher against a cache server that refuses the upload.
///
/// A POST that fails is not one outcome but several, and they want different
/// answers. A 500 will greet the next chunk exactly the same way, so nothing
/// should be uploaded for a while; an unreadable part file says nothing about
/// the server and must not stop the other candidates; a rejected API key will
/// not fix itself no matter how patiently we escalate. These tests drive a real
/// HttpCachePublisher — real QNetworkAccessManager, real socket — against a fake
/// origin that answers with each of those, and then pin the retry policy the
/// manager derives from them.

#include "FakeCacheServer.h"
#include "TestHelpers.h"
#include "httpcache/HttpCacheManager.h"
#include "httpcache/HttpCachePublisher.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace eMule;
using namespace eMule::testing;

namespace {

/// Far smaller than a real part: none of this exercises the encryption, which
/// tst_AesCbc already covers, so the read-and-encrypt pass just has to happen.
constexpr qint64 kPartLength = 64 * 1024;

/// Shorthand for a failure of the given shape, for the policy cases.
HttpCachePublishResult failure(HttpCachePublishStage stage, int status = 0, int retryAfter = 0)
{
    HttpCachePublishResult result;
    result.ok = false;
    result.stage = stage;
    result.httpStatus = status;
    result.retryAfterSeconds = retryAfter;
    return result;
}

} // namespace

class tst_HttpCachePublish : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // -- what the publisher reports --------------------------------------
    void successCarriesTheOffer();
    void serverErrorIsAServerFailure();
    void serverErrorKeepsTheServersOwnMessage();
    void unauthorizedIsReportedWithItsStatus();
    void retryAfterIsCarriedBack();
    void garbageOnSuccessIsAServerFailure();
    void unreadablePartIsALocalFailure();
    void abortedExchangeIsATransportFailure();

    // -- what the manager does with it -----------------------------------
    void backoffScopes_data();
    void backoffScopes();
    void serverBackoffEscalatesAndSaturates();
    void refusalsDoNotEscalate();
    void retryAfterOnlyEverExtends();

    // -- which server the next chunk goes to ------------------------------
    void rotationVisitsEveryHealthyServer();
    void unusableServersAreSkipped();
    void backoffTakesAServerOutAndGivesItBack();
    void aRekeyedServerNeedNotWaitOutItsPause();

private:
    /// Run one publish to completion and hand back what finished() carried.
    [[nodiscard]] HttpCachePublishResult publish(FakeCacheServer& server,
                                                 const QString& dataFilePath) const;

    QTemporaryDir m_dir;
    QString m_partPath;
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

void tst_HttpCachePublish::initTestCase()
{
    QVERIFY(m_dir.isValid());

    thePrefs.load(m_dir.filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_dir.path());

    m_partPath = m_dir.filePath(QStringLiteral("chunk.dat"));

    QFile file(m_partPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QByteArray payload(kPartLength, Qt::Uninitialized);
    for (qsizetype i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>((i * 97 + 13) & 0xFF);
    QCOMPARE(file.write(payload), qint64{kPartLength});
    file.close();
}

HttpCachePublishResult tst_HttpCachePublish::publish(FakeCacheServer& server,
                                                     const QString& dataFilePath) const
{
    HttpCachePublisher::Request request;
    request.baseUrl = server.baseUrl();
    request.apiKey = QStringLiteral("test-key");
    request.ttlSeconds = 3600;
    request.rateBytesPerSecond = 0;   // unthrottled: the throttle is not under test
    request.partIndex = 0;
    for (std::size_t i = 0; i < request.fileHash.size(); ++i)
        request.fileHash[i] = static_cast<uint8>(i + 1);

    request.dataFilePath = dataFilePath;
    request.partOffset = 0;
    request.partLength = kPartLength;

    auto* publisher = new HttpCachePublisher();
    QSignalSpy spy(publisher, &HttpCachePublisher::finished);
    publisher->start(request);

    if (spy.isEmpty() && !spy.wait(15'000))
        return {};

    return spy.at(0).at(0).value<HttpCachePublishResult>();
}

// ---------------------------------------------------------------------------
// What the publisher reports
// ---------------------------------------------------------------------------

void tst_HttpCachePublish::successCarriesTheOffer()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    const auto result = publish(server, m_partPath);

    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.stage, HttpCachePublishStage::None);
    QCOMPARE(server.requestCount(), 1);

    // The whole part plus its pad block actually went out — a publisher that
    // reported success without sending the bytes would pass everything else here.
    QCOMPARE(server.bodyBytesReceived(),
             static_cast<qint64>(AesCbcEncryptor::cipherLengthFor(kPartLength)));
}

void tst_HttpCachePublish::serverErrorIsAServerFailure()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setReply({.status = 500});

    const auto result = publish(server, m_partPath);

    QVERIFY(!result.ok);
    QCOMPARE(result.stage, HttpCachePublishStage::Server);
    QCOMPARE(result.httpStatus, 500);
}

void tst_HttpCachePublish::serverErrorKeepsTheServersOwnMessage()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setReply({.status = 500,
                     .body = R"({"error":"cannot commit chunk","status":500})"});

    const auto result = publish(server, m_partPath);

    QVERIFY(!result.ok);

    // The status alone does not distinguish a full disk from a crashed handler.
    QVERIFY2(result.error.contains(QStringLiteral("cannot commit chunk")),
             qPrintable(result.error));
    QVERIFY(result.error.contains(QStringLiteral("500")));
}

void tst_HttpCachePublish::unauthorizedIsReportedWithItsStatus()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setReply({.status = 401});

    const auto result = publish(server, m_partPath);

    QVERIFY(!result.ok);
    QCOMPARE(result.stage, HttpCachePublishStage::Server);
    QCOMPARE(result.httpStatus, 401);
}

void tst_HttpCachePublish::retryAfterIsCarriedBack()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setReply({.status = 503, .extraHeaders = "Retry-After: 120\r\n"});

    const auto result = publish(server, m_partPath);

    QVERIFY(!result.ok);
    QCOMPARE(result.httpStatus, 503);
    QCOMPARE(result.retryAfterSeconds, 120);
}

void tst_HttpCachePublish::garbageOnSuccessIsAServerFailure()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setReply({.status = 201, .body = "<html>gateway rewrote your response</html>"});

    const auto result = publish(server, m_partPath);

    QVERIFY(!result.ok);

    // A 201 we cannot parse is still the server's fault, and the next chunk would
    // meet exactly the same proxy — so it must back the server off, not the part.
    QCOMPARE(result.stage, HttpCachePublishStage::Server);
    QCOMPARE(result.httpStatus, 201);
}

void tst_HttpCachePublish::unreadablePartIsALocalFailure()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    const auto result = publish(server, m_dir.filePath(QStringLiteral("no-such-file.dat")));

    QVERIFY(!result.ok);
    QCOMPARE(result.stage, HttpCachePublishStage::Local);
    QCOMPARE(result.httpStatus, 0);

    // Nothing was sent, so the server has no opinion to offer about it.
    QCOMPARE(server.requestCount(), 0);
}

void tst_HttpCachePublish::abortedExchangeIsATransportFailure()
{
    FakeCacheServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setReply({.closeWithoutAnswering = true});

    const auto result = publish(server, m_partPath);

    QVERIFY(!result.ok);
    QCOMPARE(result.stage, HttpCachePublishStage::Transport);
    QCOMPARE(result.httpStatus, 0);
}

// ---------------------------------------------------------------------------
// What the manager does with it
// ---------------------------------------------------------------------------

void tst_HttpCachePublish::backoffScopes_data()
{
    QTest::addColumn<int>("stage");
    QTest::addColumn<int>("status");
    QTest::addColumn<bool>("serverWide");

    const int local = static_cast<int>(HttpCachePublishStage::Local);
    const int transport = static_cast<int>(HttpCachePublishStage::Transport);
    const int server = static_cast<int>(HttpCachePublishStage::Server);

    QTest::newRow("unreadable part") << local << 0 << false;
    QTest::newRow("unreachable server") << transport << 0 << true;

    // 400 is the one server status that is about this attempt: the PHP backend
    // sends it when the body it received was shorter than Content-Length, i.e.
    // our upload was cut off on the way out.
    QTest::newRow("short body") << server << 400 << false;

    QTest::newRow("bad api key") << server << 401 << true;
    QTest::newRow("forbidden") << server << 403 << true;
    QTest::newRow("no content-length") << server << 411 << true;
    QTest::newRow("chunk too large") << server << 413 << true;
    QTest::newRow("quota spent") << server << 429 << true;
    QTest::newRow("internal error") << server << 500 << true;
    QTest::newRow("bad gateway") << server << 502 << true;
    QTest::newRow("unavailable") << server << 503 << true;
    QTest::newRow("out of storage") << server << 507 << true;

    // A 2xx we could not use: the response was the server's, so it is the server
    // that stands down.
    QTest::newRow("unusable 201") << server << 201 << true;
}

void tst_HttpCachePublish::backoffScopes()
{
    QFETCH(int, stage);
    QFETCH(int, status);
    QFETCH(bool, serverWide);

    const auto backoff = HttpCacheManager::backoffFor(
        failure(static_cast<HttpCachePublishStage>(stage), status), 0);

    QCOMPARE(backoff.serverWide, serverWide);
    QVERIFY(backoff.seconds > 0);

    if (!serverWide)
        QCOMPARE(backoff.seconds, HttpCacheManager::kChunkCooldownSeconds);
}

void tst_HttpCachePublish::serverBackoffEscalatesAndSaturates()
{
    const auto pause = [](int priorFailures) {
        return HttpCacheManager::backoffFor(failure(HttpCachePublishStage::Server, 500),
                                            priorFailures)
            .seconds;
    };

    const auto& table = HttpCacheManager::kServerBackoffSeconds;
    const int steps = static_cast<int>(std::size(table));

    for (int i = 0; i < steps; ++i)
        QCOMPARE(pause(i), table[i]);

    // Past the end of the table it holds, rather than wrapping back to a minute
    // or walking off the array.
    QCOMPARE(pause(steps), table[steps - 1]);
    QCOMPARE(pause(500), table[steps - 1]);

    // Each step is longer than the one before: the point is that a server that
    // stays broken is asked less and less often.
    for (int i = 1; i < steps; ++i)
        QVERIFY(table[i] > table[i - 1]);
}

void tst_HttpCachePublish::refusalsDoNotEscalate()
{
    // A rejected key is not a flake, so escalating would be theatre — it is the
    // same slow re-probe whether it is the first refusal or the tenth.
    for (const int status : {401, 403, 411, 413, 429}) {
        for (const int prior : {0, 1, 7}) {
            const auto backoff =
                HttpCacheManager::backoffFor(failure(HttpCachePublishStage::Server, status), prior);

            QVERIFY(backoff.serverWide);
            QCOMPARE(backoff.seconds, HttpCacheManager::kServerRefusalBackoffSeconds);
        }
    }
}

void tst_HttpCachePublish::retryAfterOnlyEverExtends()
{
    const qint64 firstStep = HttpCacheManager::kServerBackoffSeconds[0];

    // Longer than our own pause: the server knows better, so it wins.
    QCOMPARE(HttpCacheManager::backoffFor(
                 failure(HttpCachePublishStage::Server, 503, static_cast<int>(firstStep) + 300), 0)
                 .seconds,
             firstStep + 300);

    // Shorter: ignored. Otherwise a server under load could ask to be hammered,
    // which is precisely the situation the backoff exists for.
    QCOMPARE(HttpCacheManager::backoffFor(failure(HttpCachePublishStage::Server, 503, 1), 0).seconds,
             firstStep);
}

// ---------------------------------------------------------------------------
// Which server the next chunk goes to
//
// chooseServer() is where round-robin and failover are the same mechanism: the
// walk spreads the chunks, and skipping a server that is sick is what makes one
// dying cost nothing but its turn. Pure, so none of this needs a queue, a socket
// or a peer.
// ---------------------------------------------------------------------------

namespace {

HttpCacheServerConfig server(const QString& host, bool enabled = true,
                             const QString& key = QStringLiteral("k"))
{
    return {{}, QStringLiteral("https://%1.example").arg(host), key, {}, enabled};
}

constexpr qint64 kNow = 1'700'000'000;

} // namespace

void tst_HttpCachePublish::rotationVisitsEveryHealthyServer()
{
    const QList<HttpCacheServerConfig> servers = {server(QStringLiteral("a")),
                                                  server(QStringLiteral("b")),
                                                  server(QStringLiteral("c"))};
    const QHash<QString, HttpCacheManager::ServerHealth> health;

    // Three consecutive chunks, three different servers, then round again — and
    // the cursor is free to run past the end without the walk falling over.
    for (quint64 cursor = 0; cursor < 9; ++cursor) {
        QCOMPARE(HttpCacheManager::chooseServer(servers, health, cursor, kNow),
                 static_cast<int>(cursor % 3));
    }

    // No servers at all is a question with an answer, not a crash.
    QCOMPARE(HttpCacheManager::chooseServer({}, health, 0, kNow), -1);
}

void tst_HttpCachePublish::unusableServersAreSkipped()
{
    QList<HttpCacheServerConfig> servers = {
        server(QStringLiteral("disabled"), false),
        {{}, QStringLiteral("https://keyless.example"), {}, {}, true},
        server(QStringLiteral("good")),
    };
    const QHash<QString, HttpCacheManager::ServerHealth> health;

    // Whatever the cursor lands on first, only the third can be published to: a
    // switched-off entry and one with no credential are both unusable.
    for (quint64 cursor = 0; cursor < 3; ++cursor)
        QCOMPARE(HttpCacheManager::chooseServer(servers, health, cursor, kNow), 2);

    servers.removeLast();
    QCOMPARE(HttpCacheManager::chooseServer(servers, health, 0, kNow), -1);
}

void tst_HttpCachePublish::backoffTakesAServerOutAndGivesItBack()
{
    const QList<HttpCacheServerConfig> servers = {server(QStringLiteral("a")),
                                                  server(QStringLiteral("b"))};

    QHash<QString, HttpCacheManager::ServerHealth> health;
    health.insert(servers.at(0).baseUrl,
                  {kNow + 300, 1, HttpCacheManager::serverFingerprint(servers.at(0))});

    // A's turn goes to B, and B's stays B's.
    QCOMPARE(HttpCacheManager::chooseServer(servers, health, 0, kNow), 1);
    QCOMPARE(HttpCacheManager::chooseServer(servers, health, 1, kNow), 1);

    // Both sick: nothing to publish to, and no chunk is uploaded rather than one
    // being forced onto a server that just refused it.
    health.insert(servers.at(1).baseUrl,
                  {kNow + 300, 1, HttpCacheManager::serverFingerprint(servers.at(1))});
    QCOMPARE(HttpCacheManager::chooseServer(servers, health, 0, kNow), -1);

    // The pause is a pause, not a removal: once it lapses the rotation is whole
    // again, with no probe and nothing to reset by hand.
    QCOMPARE(HttpCacheManager::chooseServer(servers, health, 0, kNow + 301), 0);
    QCOMPARE(HttpCacheManager::chooseServer(servers, health, 1, kNow + 301), 1);
}

void tst_HttpCachePublish::aRekeyedServerNeedNotWaitOutItsPause()
{
    const QList<HttpCacheServerConfig> before = {server(QStringLiteral("a"), true,
                                                        QStringLiteral("rejected"))};

    QHash<QString, HttpCacheManager::ServerHealth> health;
    health.insert(before.at(0).baseUrl,
                  {kNow + 1800, 1, HttpCacheManager::serverFingerprint(before.at(0))});
    QCOMPARE(HttpCacheManager::chooseServer(before, health, 0, kNow), -1);

    // Re-applying the link with a working key is the operator answering the
    // "check your configuration" warning. Half an hour of silence after that
    // would look exactly like the fix not having worked.
    const QList<HttpCacheServerConfig> after = {server(QStringLiteral("a"), true,
                                                       QStringLiteral("rotated"))};
    QCOMPARE(HttpCacheManager::chooseServer(after, health, 0, kNow), 0);
}

QTEST_MAIN(tst_HttpCachePublish)
#include "tst_HttpCachePublish.moc"
