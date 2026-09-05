/// @file tst_WebServer.cpp
/// @brief Unit tests for the JSON REST API WebServer (Module 19).

#include "webserver/WebServer.h"

#include "friends/FriendList.h"
#include "prefs/Preferences.h"
#include "search/SearchList.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "stats/Statistics.h"
#include "stats/StatsHistory.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace eMule;

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class tst_WebServer : public QObject {
    Q_OBJECT

public:
    tst_WebServer() = default;

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Auth tests
    void authMissingKey();
    void authWrongKey();
    void authValidKey();

    // Stats tests
    void getStats();

    // Downloads tests
    void getDownloadsEmpty();
    void getDownloadBadHash();

    // Servers tests
    void getServers();

    // Connection tests
    void getConnection();

    // Friends tests
    void friendsLifecycle();

    // Shared files tests
    void getSharedFiles();

    // Preferences tests
    void getPreferences();
    void patchPreferences();

    // CORS tests
    void corsOptionsRequest();

    // Error tests
    void invalidEndpoint();

    // Web UI / REST API independence
    void restApiWithoutWebUi();
    void webUiWithoutRestApi();

    // Preview streaming — the channel behind the GUI's Preview action, and the
    // one route served whether or not either web surface is enabled.
    void previewRejectsAMissingOrWrongStreamToken();
    void previewRejectsABadFileIndex();
    void previewCarriesTheArchiveEntryFromTheQuery();
    void previewRejectsABadArchiveEntry();
    void previewWithoutATotalMustNotAnswerTheOpeningRequestWith200();
    void previewCapsTheResponseBody();
    void previewStopsAtWhatHasDownloaded();
    void previewWaitsForBytesThatHaveNotArrivedYet();
    void previewGivesUpWhenTheItemDisappears();
    void previewStitchesAReadAcrossTwoFiles();
    void previewRefusesAnUnstreamableReleaseAtOnce();
    void previewTakesAPieceWithNoLengthFromTheFileOnDisk();

    // Graphs page data
    // Incoming folder browsing — the remote core's stand-in for the file manager
    void incomingRejectsAMissingOrWrongStreamToken();
    void incomingListingShowsFilesAndFolders();
    void incomingRefusesToEscapeTheIncomingDir();
    void incomingDownloadSendsTheWholeFileAsAnAttachment();
    void incomingStreamHonoursARangeRequest();

    void graphVars_carryTheSeriesOldestFirst();
    void graphVars_ratesAreBytesPerSecond();
    void graphVars_pointsStayInsideTheViewBox();
    void graphVars_withNoSamplesAreEmpty();

private:
    // Helper: send HTTP request and block until response
    struct Response {
        int statusCode = 0;
        QJsonDocument json;
        QByteArray rawBody;
        QMap<QString, QString> headers;
    };

    Response sendRequest(const QByteArray& method, const QString& path,
                         const QByteArray& body = {},
                         bool includeAuth = true);

    /// GET with an optional Range header and no X-Api-Key. Preview authenticates
    /// with the stream token instead, so sending the API key would prove nothing.
    Response sendRanged(const QString& path, const QByteArray& range);

    QString baseUrl() const;

    // Blocking GET against an arbitrary port; returns the HTTP status code.
    // Used by the independence tests, which spin up their own servers.
    int rawGetStatus(uint16 port, const QString& path, bool withKey);

    // Start a throwaway WebServer with the given UI/REST flags on a random port,
    // wired to the shared fixture dependencies. Caller owns and must stop it.
    std::unique_ptr<WebServer> startServer(bool webUiEnabled, bool restApiEnabled);

    std::unique_ptr<WebServer>     m_webServer;
    std::unique_ptr<Statistics>    m_stats;
    std::unique_ptr<FriendList>    m_friendList;
    std::unique_ptr<ServerList>    m_serverList;
    std::unique_ptr<ServerConnect> m_serverConnect;
    std::unique_ptr<DownloadQueue> m_downloadQueue;
    std::unique_ptr<UploadQueue>   m_uploadQueue;
    std::unique_ptr<KnownFileList>  m_knownFiles;
    std::unique_ptr<SharedFileList> m_sharedFiles;
    std::unique_ptr<SearchList>    m_searchList;
    std::unique_ptr<Preferences>   m_preferences;

    /// A throwaway Incoming folder for the browse routes, with one of everything
    /// they distinguish: a subfolder, a browser-playable video, one that is not,
    /// a plain file, and a file too large for the preview cap.
    void buildIncomingTree();
    QString incomingUrl(const QString& path, const QString& key = {},
                        const QString& value = {}) const;

    QNetworkAccessManager m_nam;
    QString m_apiKey = QStringLiteral("test-secret-key-12345");
    uint16 m_port = 0;

    std::unique_ptr<QTemporaryDir> m_incoming;
    QByteArray m_bigFileBytes;
};

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

void tst_WebServer::initTestCase()
{
    m_stats = std::make_unique<Statistics>();
    m_friendList = std::make_unique<FriendList>();
    m_serverList = std::make_unique<ServerList>();
    m_serverConnect = std::make_unique<ServerConnect>(*m_serverList);
    m_downloadQueue = std::make_unique<DownloadQueue>();
    m_uploadQueue = std::make_unique<UploadQueue>();
    m_knownFiles = std::make_unique<KnownFileList>();
    m_sharedFiles = std::make_unique<SharedFileList>(m_knownFiles.get());
    m_searchList = std::make_unique<SearchList>();
    m_preferences = std::make_unique<Preferences>();

    m_webServer = std::make_unique<WebServer>();
    m_webServer->setStatistics(m_stats.get());
    m_webServer->setFriendList(m_friendList.get());
    m_webServer->setServerList(m_serverList.get());
    m_webServer->setServerConnect(m_serverConnect.get());
    m_webServer->setDownloadQueue(m_downloadQueue.get());
    m_webServer->setUploadQueue(m_uploadQueue.get());
    m_webServer->setSharedFileList(m_sharedFiles.get());
    m_webServer->setSearchList(m_searchList.get());
    m_webServer->setPreferences(m_preferences.get());

    buildIncomingTree();

    WebServerConfig config;
    config.enabled = true;
    config.restApiEnabled = true;
    config.port = 0;
    config.apiKey = m_apiKey;

    QSignalSpy spy(m_webServer.get(), &WebServer::started);
    QVERIFY(m_webServer->start(config));
    QVERIFY(m_webServer->isRunning());

    m_port = m_webServer->port();
    QVERIFY(m_port > 0);
}

void tst_WebServer::cleanupTestCase()
{
    m_webServer->stop();
    QVERIFY(!m_webServer->isRunning());
}

// ---------------------------------------------------------------------------
// Helper
// ---------------------------------------------------------------------------

QString tst_WebServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(m_port);
}

tst_WebServer::Response tst_WebServer::sendRequest(
    const QByteArray& method, const QString& path,
    const QByteArray& body, bool includeAuth)
{
    QNetworkRequest req(QUrl(baseUrl() + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
    if (includeAuth)
        req.setRawHeader(QByteArrayLiteral("X-Api-Key"), m_apiKey.toUtf8());

    QNetworkReply* reply = nullptr;
    if (method == QByteArrayLiteral("GET"))
        reply = m_nam.get(req);
    else if (method == QByteArrayLiteral("POST"))
        reply = m_nam.post(req, body);
    else if (method == QByteArrayLiteral("PATCH"))
        reply = m_nam.sendCustomRequest(req, QByteArrayLiteral("PATCH"), body);
    else if (method == QByteArrayLiteral("DELETE"))
        reply = m_nam.deleteResource(req);
    else if (method == QByteArrayLiteral("OPTIONS"))
        reply = m_nam.sendCustomRequest(req, QByteArrayLiteral("OPTIONS"));
    else
        reply = m_nam.sendCustomRequest(req, method);

    // Block until finished (with timeout)
    if (!reply->isFinished()) {
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
    }

    Response resp;
    resp.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    resp.rawBody = reply->readAll();
    resp.json = QJsonDocument::fromJson(resp.rawBody);

    for (const auto& header : reply->rawHeaderList())
        resp.headers[QString::fromUtf8(header)] = QString::fromUtf8(reply->rawHeader(header));

    reply->deleteLater();
    return resp;
}

tst_WebServer::Response tst_WebServer::sendRanged(const QString& path, const QByteArray& range)
{
    QNetworkRequest req(QUrl(baseUrl() + path));
    if (!range.isEmpty())
        req.setRawHeader(QByteArrayLiteral("Range"), range);

    QNetworkReply* reply = m_nam.get(req);
    if (!reply->isFinished()) {
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(30000, &loop, &QEventLoop::quit);
        loop.exec();
    }

    Response resp;
    resp.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    resp.rawBody = reply->readAll();
    // Lower-cased keys: HTTP header names are case-insensitive and Qt does not
    // promise the casing it hands back, so matching on "Content-Range" silently
    // finds nothing.
    for (const auto& header : reply->rawHeaderList()) {
        resp.headers[QString::fromUtf8(header).toLower()] =
            QString::fromUtf8(reply->rawHeader(header));
    }

    reply->deleteLater();
    return resp;
}

// ---------------------------------------------------------------------------
// Auth tests
// ---------------------------------------------------------------------------

void tst_WebServer::authMissingKey()
{
    auto resp = sendRequest(QByteArrayLiteral("GET"),
                            QStringLiteral("/api/v1/stats"), {}, false);
    QCOMPARE(resp.statusCode, 401);
    QVERIFY(resp.json.object().contains(QStringLiteral("error")));
}

void tst_WebServer::authWrongKey()
{
    QNetworkRequest req(QUrl(baseUrl() + QStringLiteral("/api/v1/stats")));
    req.setRawHeader(QByteArrayLiteral("X-Api-Key"), QByteArrayLiteral("wrong-key"));

    auto* reply = m_nam.get(req);
    QSignalSpy finished(reply, &QNetworkReply::finished);
    if (!reply->isFinished())
        QVERIFY(finished.wait(5000));

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QCOMPARE(status, 401);
    reply->deleteLater();
}

void tst_WebServer::authValidKey()
{
    auto resp = sendRequest(QByteArrayLiteral("GET"),
                            QStringLiteral("/api/v1/stats"));
    QCOMPARE(resp.statusCode, 200);
    QVERIFY(resp.json.isObject());
}

// ---------------------------------------------------------------------------
// Stats tests
// ---------------------------------------------------------------------------

void tst_WebServer::getStats()
{
    auto resp = sendRequest(QByteArrayLiteral("GET"),
                            QStringLiteral("/api/v1/stats"));
    QCOMPARE(resp.statusCode, 200);

    auto obj = resp.json.object();
    QVERIFY(obj.contains(QStringLiteral("rateDown")));
    QVERIFY(obj.contains(QStringLiteral("rateUp")));
    QVERIFY(obj.contains(QStringLiteral("sessionReceivedBytes")));
    QVERIFY(obj.contains(QStringLiteral("sessionSentBytes")));
}

// ---------------------------------------------------------------------------
// Downloads tests
// ---------------------------------------------------------------------------

void tst_WebServer::getDownloadsEmpty()
{
    auto resp = sendRequest(QByteArrayLiteral("GET"),
                            QStringLiteral("/api/v1/downloads"));
    QCOMPARE(resp.statusCode, 200);
    QVERIFY(resp.json.isArray());
    QCOMPARE(resp.json.array().size(), 0);
}

void tst_WebServer::getDownloadBadHash()
{
    auto resp = sendRequest(QByteArrayLiteral("GET"),
                            QStringLiteral("/api/v1/downloads/badhash"));
    QCOMPARE(resp.statusCode, 400);
}

// ---------------------------------------------------------------------------
// Servers tests
// ---------------------------------------------------------------------------

void tst_WebServer::getServers()
{
    auto resp = sendRequest(QByteArrayLiteral("GET"),
                            QStringLiteral("/api/v1/servers"));
    QCOMPARE(resp.statusCode, 200);
    QVERIFY(resp.json.isArray());
}

// ---------------------------------------------------------------------------
// Connection tests
// ---------------------------------------------------------------------------

void tst_WebServer::getConnection()
{
    auto resp = sendRequest(QByteArrayLiteral("GET"),
                            QStringLiteral("/api/v1/connection"));
    QCOMPARE(resp.statusCode, 200);

    auto obj = resp.json.object();
    QVERIFY(obj.contains(QStringLiteral("isConnected")));
    QVERIFY(obj.contains(QStringLiteral("isConnecting")));
}

// ---------------------------------------------------------------------------
// Friends tests
// ---------------------------------------------------------------------------

void tst_WebServer::friendsLifecycle()
{
    // GET — initially empty
    auto resp = sendRequest(QByteArrayLiteral("GET"),
                            QStringLiteral("/api/v1/friends"));
    QCOMPARE(resp.statusCode, 200);
    QVERIFY(resp.json.isArray());
    const auto initialCount = resp.json.array().size();

    // POST — add a friend
    QJsonObject friendObj{
        {QStringLiteral("name"), QStringLiteral("TestFriend")},
        {QStringLiteral("ip"),   0x7F000001},  // 127.0.0.1
        {QStringLiteral("port"), 4662},
    };
    resp = sendRequest(QByteArrayLiteral("POST"),
                       QStringLiteral("/api/v1/friends"),
                       QJsonDocument(friendObj).toJson(QJsonDocument::Compact));
    // Should either succeed (200) or fail gracefully (400)
    QVERIFY(resp.statusCode == 200 || resp.statusCode == 400);

    if (resp.statusCode == 200) {
        auto addedFriend = resp.json.object();
        QVERIFY(addedFriend.contains(QStringLiteral("name")));

        resp = sendRequest(QByteArrayLiteral("GET"),
                           QStringLiteral("/api/v1/friends"));
        QCOMPARE(resp.statusCode, 200);
        QCOMPARE(resp.json.array().size(), initialCount + 1);
    }
}

// ---------------------------------------------------------------------------
// Shared files tests
// ---------------------------------------------------------------------------

void tst_WebServer::getSharedFiles()
{
    auto resp = sendRequest(QByteArrayLiteral("GET"),
                            QStringLiteral("/api/v1/shared"));
    QCOMPARE(resp.statusCode, 200);
    QVERIFY(resp.json.isArray());
}

// ---------------------------------------------------------------------------
// Preferences tests
// ---------------------------------------------------------------------------

void tst_WebServer::getPreferences()
{
    auto resp = sendRequest(QByteArrayLiteral("GET"),
                            QStringLiteral("/api/v1/preferences"));
    QCOMPARE(resp.statusCode, 200);

    auto obj = resp.json.object();
    QVERIFY(obj.contains(QStringLiteral("nick")));
    QVERIFY(obj.contains(QStringLiteral("maxUpload")));
    QVERIFY(obj.contains(QStringLiteral("maxDownload")));
}

void tst_WebServer::patchPreferences()
{
    QJsonObject patch{
        {QStringLiteral("nick"), QStringLiteral("WebTestNick")},
    };
    auto resp = sendRequest(QByteArrayLiteral("PATCH"),
                            QStringLiteral("/api/v1/preferences"),
                            QJsonDocument(patch).toJson(QJsonDocument::Compact));
    QCOMPARE(resp.statusCode, 200);

    auto obj = resp.json.object();
    QCOMPARE(obj[QStringLiteral("nick")].toString(), QStringLiteral("WebTestNick"));

    QCOMPARE(m_preferences->nick(), QStringLiteral("WebTestNick"));
}

// ---------------------------------------------------------------------------
// CORS tests
// ---------------------------------------------------------------------------

void tst_WebServer::corsOptionsRequest()
{
    auto resp = sendRequest(QByteArrayLiteral("OPTIONS"),
                            QStringLiteral("/api/v1/stats"), {}, false);
    // OPTIONS should return 204 No Content
    QCOMPARE(resp.statusCode, 204);

    // Check CORS headers — header names may be lowercase in the response
    bool found = false;
    for (auto it = resp.headers.cbegin(); it != resp.headers.cend(); ++it) {
        if (it.key().compare(QStringLiteral("access-control-allow-origin"),
                             Qt::CaseInsensitive) == 0) {
            QCOMPARE(it.value(), QStringLiteral("*"));
            found = true;
            break;
        }
    }
    QVERIFY(found);
}

// ---------------------------------------------------------------------------
// Error tests
// ---------------------------------------------------------------------------

void tst_WebServer::invalidEndpoint()
{
    auto resp = sendRequest(QByteArrayLiteral("GET"),
                            QStringLiteral("/api/v1/nonexistent"));
    // QHttpServer returns 404 for unregistered routes
    QCOMPARE(resp.statusCode, 404);
}

// ---------------------------------------------------------------------------
// Web UI / REST API independence
// ---------------------------------------------------------------------------

int tst_WebServer::rawGetStatus(uint16 port, const QString& path, bool withKey)
{
    QNetworkRequest req(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(port).arg(path)));
    if (withKey)
        req.setRawHeader(QByteArrayLiteral("X-Api-Key"), m_apiKey.toUtf8());

    QNetworkReply* reply = m_nam.get(req);
    if (!reply->isFinished()) {
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
    }
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();
    return code;
}

std::unique_ptr<WebServer> tst_WebServer::startServer(bool webUiEnabled, bool restApiEnabled)
{
    auto server = std::make_unique<WebServer>();
    server->setStatistics(m_stats.get());
    server->setFriendList(m_friendList.get());
    server->setServerList(m_serverList.get());
    server->setServerConnect(m_serverConnect.get());
    server->setDownloadQueue(m_downloadQueue.get());
    server->setUploadQueue(m_uploadQueue.get());
    server->setSharedFileList(m_sharedFiles.get());
    server->setSearchList(m_searchList.get());
    server->setPreferences(m_preferences.get());

    WebServerConfig config;
    config.enabled = true;
    config.webUiEnabled = webUiEnabled;
    config.restApiEnabled = restApiEnabled;
    config.port = 0;
    config.apiKey = m_apiKey;
    config.templatePath = QString();  // no template file — page render is not under test here

    server->start(config);
    return server;
}

// REST enabled, web UI disabled: /api/v1/* is served, the UI 404s.
void tst_WebServer::restApiWithoutWebUi()
{
    auto server = startServer(/*webUiEnabled*/ false, /*restApiEnabled*/ true);
    QVERIFY(server->isRunning());
    const uint16 port = server->port();
    QVERIFY(port > 0);

    QCOMPARE(rawGetStatus(port, QStringLiteral("/api/v1/stats"), /*withKey*/ true), 200);
    QCOMPARE(rawGetStatus(port, QStringLiteral("/api/v1/stats"), /*withKey*/ false), 401);
    QCOMPARE(rawGetStatus(port, QStringLiteral("/"), /*withKey*/ false), 404);

    server->stop();
}

// Web UI enabled, REST disabled: the UI root is served, /api/v1/* 404s.
void tst_WebServer::webUiWithoutRestApi()
{
    auto server = startServer(/*webUiEnabled*/ true, /*restApiEnabled*/ false);
    QVERIFY(server->isRunning());
    const uint16 port = server->port();
    QVERIFY(port > 0);

    // The UI root is registered (renders the login page even without a template).
    QVERIFY(rawGetStatus(port, QStringLiteral("/"), /*withKey*/ false) != 404);
    // REST is not registered — even with a valid key it 404s.
    QCOMPARE(rawGetStatus(port, QStringLiteral("/api/v1/stats"), /*withKey*/ true), 404);

    server->stop();
}

// ---------------------------------------------------------------------------
// Preview streaming
//
// The one route registered whether or not the web UI and the REST API are on,
// because the GUI's Preview action rides on it. It has its own gate — a
// per-process random token — so none of these send an API key.
// ---------------------------------------------------------------------------

namespace {

/// A file of @p size bytes whose content encodes its own offset, so a wrong
/// seek shows up as wrong *bytes* and not merely as a wrong length.
QString writePattern(QTemporaryDir& dir, const QString& name, qint64 size)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};

    QByteArray block(64 * 1024, Qt::Uninitialized);
    qint64 written = 0;
    while (written < size) {
        for (qsizetype i = 0; i < block.size(); ++i)
            block[i] = char((written + i) & 0xFF);
        const qint64 chunk = qMin<qint64>(block.size(), size - written);
        f.write(block.constData(), chunk);
        written += chunk;
    }
    f.close();
    return path;
}

} // namespace

void tst_WebServer::previewRejectsAMissingOrWrongStreamToken()
{
    m_webServer->setUsenetStreamResolver([](const UsenetStreamRequest&) {
        UsenetStreamSource src;
        src.found = true;
        src.pieces = {{QStringLiteral("/nonexistent"), 0, 0, 0}};
        return src;
    });

    // No token at all.
    QCOMPARE(sendRanged(QStringLiteral("/api/v1/usenet/some-id/0/preview"), {}).statusCode, 401);

    // A wrong one — also what an old GUI sends after the daemon restarts and
    // mints a new token, so it has to be a clean 401 that gives nothing away.
    const auto wrong = sendRanged(
        QStringLiteral("/api/v1/usenet/some-id/0/preview?token=not-the-token"), {});
    QCOMPARE(wrong.statusCode, 401);
    QVERIFY(!wrong.rawBody.contains(m_webServer->streamToken().toUtf8()));

    m_webServer->setUsenetStreamResolver({});
}

void tst_WebServer::previewRejectsABadFileIndex()
{
    bool resolverCalled = false;
    m_webServer->setUsenetStreamResolver([&resolverCalled](const UsenetStreamRequest&) {
        resolverCalled = true;
        return UsenetStreamSource{};
    });

    const QString token = m_webServer->streamToken();
    const auto resp = sendRanged(
        QStringLiteral("/api/v1/usenet/some-id/notanumber/preview?token=%1").arg(token), {});

    QCOMPARE(resp.statusCode, 400);
    // Rejected before the queue is touched: a malformed URL is not a lookup.
    QVERIFY(!resolverCalled);

    m_webServer->setUsenetStreamResolver({});
}

void tst_WebServer::previewCarriesTheArchiveEntryFromTheQuery()
{
    int seenEntry = -99;
    m_webServer->setUsenetStreamResolver([&seenEntry](const UsenetStreamRequest& ask) {
        seenEntry = ask.entryOrdinal;
        return UsenetStreamSource{};
    });

    const QString token = m_webServer->streamToken();

    (void)sendRanged(QStringLiteral("/api/v1/usenet/some-id/0/preview?token=%1&entry=2")
                         .arg(token), {});
    QCOMPARE(seenEntry, 2);

    // No `entry=` means the first playable file, which is what every URL
    // predating the chooser carries — the back-compat guarantee, asserted.
    seenEntry = -99;
    (void)sendRanged(QStringLiteral("/api/v1/usenet/some-id/0/preview?token=%1").arg(token), {});
    QCOMPARE(seenEntry, -1);

    m_webServer->setUsenetStreamResolver({});
}

void tst_WebServer::previewRejectsABadArchiveEntry()
{
    bool resolverCalled = false;
    m_webServer->setUsenetStreamResolver([&resolverCalled](const UsenetStreamRequest&) {
        resolverCalled = true;
        return UsenetStreamSource{};
    });

    const QString token = m_webServer->streamToken();

    for (const QString& bad : {QStringLiteral("abc"), QStringLiteral("-3")}) {
        const auto resp = sendRanged(
            QStringLiteral("/api/v1/usenet/some-id/0/preview?token=%1&entry=%2")
                .arg(token, bad), {});
        QCOMPARE(resp.statusCode, 400);
        // Same rule as a bad file index: a malformed URL is not a lookup.
        QVERIFY2(!resolverCalled, qPrintable(bad));
    }

    m_webServer->setUsenetStreamResolver({});
}

void tst_WebServer::previewWithoutATotalMustNotAnswerTheOpeningRequestWith200()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // What a still-extracting release looks like early on: a small prefix of a
    // large file, and a caller that did not say how large.
    constexpr qint64 kSoFar = 64 * 1024;
    const QString path = writePattern(dir, QStringLiteral("movie.mkv"), kSoFar);
    QVERIFY(!path.isEmpty());

    m_webServer->setUsenetStreamResolver([path](const UsenetStreamRequest&) {
        UsenetStreamSource src;
        src.found = true;
        src.pieces = {{path, 0, 0, kSoFar}};
        src.fileName = QStringLiteral("movie.mkv");
        src.totalSize = 0;            // "I do not know" — the trap
        src.availableEnd = kSoFar;
        return src;
    });

    const QString token = m_webServer->streamToken();
    const QString url = QStringLiteral("/api/v1/usenet/item/0/preview?token=%1").arg(token);

    // A player opens with no Range header at all. With no total the route can
    // only take the file's current length for the whole thing, and then
    // `data.size() == fileSize` makes the answer a **200 OK** carrying "the
    // complete movie, 64 KiB long". The client latches that and never asks for
    // more. Whoever supplies these pieces must therefore always supply a real
    // total — see UsenetQueue::streamFromExtraction, which waits rather than
    // answer without one.
    const auto resp = sendRanged(url, {});
    QVERIFY2(resp.statusCode != 200,
             "a growing file with no declared total was served as a complete one");
    QCOMPARE(resp.statusCode, 206);

    // RFC 9110 §14.4: `*` is how a complete length that is not yet known is
    // written. Any number here would be the wrong one.
    QCOMPARE(resp.headers.value(QStringLiteral("content-range")),
             QStringLiteral("bytes 0-%1/*").arg(kSoFar - 1));

    m_webServer->setUsenetStreamResolver({});
}

void tst_WebServer::previewCapsTheResponseBody()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    constexpr qint64 kSize = 6 * 1024 * 1024;   // comfortably over the 4 MiB cap
    constexpr qint64 kCap  = 4 * 1024 * 1024;
    const QString path = writePattern(dir, QStringLiteral("movie.mkv"), kSize);
    QVERIFY(!path.isEmpty());

    m_webServer->setUsenetStreamResolver([path](const UsenetStreamRequest&) {
        UsenetStreamSource src;
        src.found = true;
        src.pieces = {{path, 0, 0, kSize}};
        src.fileName = QStringLiteral("movie.mkv");
        src.totalSize = kSize;
        src.availableEnd = kSize;
        src.complete = true;
        return src;
    });

    const QString token = m_webServer->streamToken();
    const QString url = QStringLiteral("/api/v1/usenet/item/0/preview?token=%1").arg(token);

    // `bytes=0-` is what a player opens with: "send me the whole file". Before
    // the cap that allocated the entire file in the daemon's address space,
    // which on a real release is tens of gigabytes.
    const auto resp = sendRanged(url, QByteArrayLiteral("bytes=0-"));
    QCOMPARE(resp.statusCode, 206);
    QCOMPARE(qint64(resp.rawBody.size()), kCap);

    // The total in Content-Range is the whole file, not the slice — a player
    // takes its duration and its seek bar from that number.
    QCOMPARE(resp.headers.value(QStringLiteral("content-range")),
             QStringLiteral("bytes 0-%1/%2").arg(kCap - 1).arg(kSize));
    QCOMPARE(resp.headers.value(QStringLiteral("accept-ranges")), QStringLiteral("bytes"));

    // The next window starts exactly where the last stopped, with the right
    // bytes in it. An off-by-one here desyncs the stream and nothing says so.
    const auto next = sendRanged(url, QByteArrayLiteral("bytes=4194304-4194403"));
    QCOMPARE(next.statusCode, 206);
    QCOMPARE(next.rawBody.size(), qsizetype(100));
    QCOMPARE(quint8(next.rawBody.at(0)), quint8(kCap & 0xFF));

    m_webServer->setUsenetStreamResolver({});
}

void tst_WebServer::previewStopsAtWhatHasDownloaded()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // The file is already its final length on disk — ArticleWriter::reserve()
    // preallocates it — so its size says nothing about how much has arrived.
    constexpr qint64 kSize = 1024 * 1024;
    constexpr qint64 kHave = 300 * 1024;
    const QString path = writePattern(dir, QStringLiteral("movie.mkv"), kSize);
    QVERIFY(!path.isEmpty());

    m_webServer->setUsenetStreamResolver([path](const UsenetStreamRequest&) {
        UsenetStreamSource src;
        src.found = true;
        src.pieces = {{path, 0, 0, kSize}};
        src.fileName = QStringLiteral("movie.mkv");
        src.totalSize = kSize;
        src.availableEnd = kHave;
        return src;
    });

    const QString token = m_webServer->streamToken();
    const auto resp = sendRanged(
        QStringLiteral("/api/v1/usenet/item/0/preview?token=%1").arg(token),
        QByteArrayLiteral("bytes=0-"));

    QCOMPARE(resp.statusCode, 206);
    // Exactly the downloaded prefix. Going further hands the player the
    // preallocated tail, which is zeros — and that does not look like an error,
    // it looks like a corrupt file.
    QCOMPARE(qint64(resp.rawBody.size()), kHave);
    QCOMPARE(resp.headers.value(QStringLiteral("content-range")),
             QStringLiteral("bytes 0-%1/%2").arg(kHave - 1).arg(kSize));

    m_webServer->setUsenetStreamResolver({});
}

void tst_WebServer::previewWaitsForBytesThatHaveNotArrivedYet()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    constexpr qint64 kSize = 200 * 1024;
    constexpr qint64 kLate = 100 * 1024;
    const QString path = writePattern(dir, QStringLiteral("movie.mkv"), kSize);
    QVERIFY(!path.isEmpty());

    // Nothing has landed when the request arrives; the first 100 KiB shows up a
    // moment later. This is playback catching up with the write head, which is
    // normal rather than an error — and the wait must not block the event loop,
    // or the very downloads being waited for would stop.
    QElapsedTimer since;
    since.start();
    m_webServer->setUsenetStreamResolver([path, &since](const UsenetStreamRequest&) {
        UsenetStreamSource src;
        src.found = true;
        src.pieces = {{path, 0, 0, kSize}};
        src.fileName = QStringLiteral("movie.mkv");
        src.totalSize = kSize;
        src.availableEnd = since.elapsed() > 400 ? kLate : 0;
        return src;
    });

    const QString token = m_webServer->streamToken();
    const auto resp = sendRanged(
        QStringLiteral("/api/v1/usenet/item/0/preview?token=%1").arg(token),
        QByteArrayLiteral("bytes=0-"));

    // Answered, not refused. A zero-byte 206 reads as end-of-stream and every
    // player stops there.
    QCOMPARE(resp.statusCode, 206);
    QCOMPARE(qint64(resp.rawBody.size()), kLate);

    m_webServer->setUsenetStreamResolver({});
}

void tst_WebServer::previewGivesUpWhenTheItemDisappears()
{
    // Found on the first call, gone on the next: the user removed the item, or
    // post-processing moved the file, while a player still held the stream open.
    // The wait has to end there rather than run out its full timeout.
    int calls = 0;
    m_webServer->setUsenetStreamResolver([&calls](const UsenetStreamRequest&) {
        UsenetStreamSource src;
        if (calls++ == 0) {
            src.found = true;
            src.fileName = QStringLiteral("movie.mkv");
            src.totalSize = 1024;
            src.availableEnd = 0;      // nothing readable yet -> wait
        }
        return src;
    });

    QElapsedTimer elapsed;
    elapsed.start();

    const QString token = m_webServer->streamToken();
    const auto resp = sendRanged(
        QStringLiteral("/api/v1/usenet/item/0/preview?token=%1").arg(token), {});

    QCOMPARE(resp.statusCode, 404);
    QVERIFY2(elapsed.elapsed() < 5000, "the wait should end with the item, not time out");

    m_webServer->setUsenetStreamResolver({});
}

void tst_WebServer::previewStitchesAReadAcrossTwoFiles()
{
    // Phase 6b: the logical file is the `.mkv` inside a stored RAR set, so it
    // lives in several volume files at an offset. A window landing on a volume
    // boundary has to come back as one continuous run of the right bytes —
    // this is the case an off-by-one in the extent walk survives everywhere else.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    constexpr qint64 kHeader = 96;      // stands in for the archive header
    constexpr qint64 kPayload = 4096;

    // Two volumes, each `kHeader` bytes of junk followed by its slice of the
    // logical file. Content encodes its own logical offset, so a wrong seek
    // shows up as wrong bytes rather than merely a wrong length.
    QByteArray logical(2 * kPayload, Qt::Uninitialized);
    for (qsizetype i = 0; i < logical.size(); ++i)
        logical[i] = char((i * 7 + 3) & 0xFF);

    QStringList paths;
    for (int v = 0; v < 2; ++v) {
        const QString path = dir.filePath(QStringLiteral("vol%1.rar").arg(v));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray(kHeader, '#'));
        f.write(logical.mid(int(v * kPayload), int(kPayload)));
        f.close();
        paths << path;
    }

    m_webServer->setUsenetStreamResolver([paths](const UsenetStreamRequest&) {
        UsenetStreamSource src;
        src.found = true;
        src.fileName = QStringLiteral("movie.mkv");
        src.totalSize = 2 * kPayload;
        src.availableEnd = 2 * kPayload;
        src.pieces = {{paths.at(0), 0, kHeader, kPayload},
                      {paths.at(1), kPayload, kHeader, kPayload}};
        return src;
    });

    const QString token = m_webServer->streamToken();
    const QString url = QStringLiteral("/api/v1/usenet/item/0/preview?token=%1").arg(token);

    // 200 bytes centred on the seam.
    const auto straddle = sendRanged(url, QByteArrayLiteral("bytes=3996-4195"));
    QCOMPARE(straddle.statusCode, 206);
    QCOMPARE(straddle.rawBody, logical.mid(3996, 200));

    // And the whole logical file in one go, which also proves the header bytes
    // of each volume are skipped rather than served.
    const auto all = sendRanged(url, QByteArrayLiteral("bytes=0-"));
    QCOMPARE(all.statusCode, 206);
    QCOMPARE(all.rawBody, logical);
    QCOMPARE(all.headers.value(QStringLiteral("content-range")),
             QStringLiteral("bytes 0-%1/%2").arg(2 * kPayload - 1).arg(2 * kPayload));

    m_webServer->setUsenetStreamResolver({});
}

void tst_WebServer::previewRefusesAnUnstreamableReleaseAtOnce()
{
    // A compressed, solid or encrypted archive will never become readable.
    // Waiting out the poll would end in a 416, which reads as "not yet"; 406
    // with the reason says "not ever", and says it before a player opens.
    m_webServer->setUsenetStreamResolver([](const UsenetStreamRequest&) {
        UsenetStreamSource src;
        src.found = true;
        src.fileName = QStringLiteral("Some.Release.part01.rar");
        src.notSeekableReason = QStringLiteral("Solid archive — cannot seek without decompressing");
        return src;
    });

    QElapsedTimer elapsed;
    elapsed.start();

    const QString token = m_webServer->streamToken();
    const auto resp = sendRanged(
        QStringLiteral("/api/v1/usenet/item/0/preview?token=%1").arg(token), {});

    QCOMPARE(resp.statusCode, 406);
    QVERIFY2(resp.rawBody.contains("Solid archive"),
             "the reason has to reach the caller, not just the log");
    QVERIFY2(elapsed.elapsed() < 5000, "an impossible request must not wait out the poll");

    m_webServer->setUsenetStreamResolver({});
}

void tst_WebServer::previewTakesAPieceWithNoLengthFromTheFileOnDisk()
{
    // The shape the ED2K route passes: one piece, no declared length, no total.
    // Its .part file is already preallocated to its final size, so the size on
    // disk is the answer — and this is the only place that branch is exercised,
    // since every Usenet source states its lengths.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    constexpr qint64 kSize = 5000;
    const QString path = writePattern(dir, QStringLiteral("movie.mkv"), kSize);
    QVERIFY(!path.isEmpty());

    m_webServer->setUsenetStreamResolver([path](const UsenetStreamRequest&) {
        UsenetStreamSource src;
        src.found = true;
        src.fileName = QStringLiteral("movie.mkv");
        src.availableEnd = kSize;
        src.pieces = {{path, 0, 0, 0}};       // length 0 — "whatever is on disk"
        return src;
    });

    const QString token = m_webServer->streamToken();
    const auto resp = sendRanged(
        QStringLiteral("/api/v1/usenet/item/0/preview?token=%1").arg(token),
        QByteArrayLiteral("bytes=1000-1099"));

    QCOMPARE(resp.statusCode, 206);
    QCOMPARE(resp.rawBody.size(), qsizetype(100));
    QCOMPARE(quint8(resp.rawBody.at(0)), quint8(1000 & 0xFF));
    QCOMPARE(resp.headers.value(QStringLiteral("content-range")),
             QStringLiteral("bytes 1000-1099/%1").arg(kSize));

    m_webServer->setUsenetStreamResolver({});
}



// ---------------------------------------------------------------------------
// Graphs page — the variables the GRAPHS template section is substituted with.
//
// MFC fills a 500-point ring from its statistics dialog and joins it into
// [GraphDownload]/[GraphUpload]/[GraphConnections] (srchybrid/WebServer.cpp:3027).
// A headless daemon has no dialog, so the same numbers come out of core's StatsHistory
// — and the page also gets them pre-scaled as SVG points, because a template that has
// to run JavaScript to draw is one that shows nothing when JavaScript is off.
// ---------------------------------------------------------------------------

namespace {

/// Samples with distinct, hand-checkable values.
std::vector<StatsGraphSample> makeSamples(int count)
{
    std::vector<StatsGraphSample> samples;
    samples.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        StatsGraphSample s;
        s.seq = static_cast<uint32>(i + 1);
        s.downCurrent = static_cast<float>(i + 1);        // 1, 2, 3 … KB/s
        s.upCurrent = static_cast<float>((i + 1) * 2);
        s.connActive = static_cast<uint32>((i + 1) * 10);
        samples.push_back(s);
    }
    return samples;
}

} // namespace

void tst_WebServer::graphVars_carryTheSeriesOldestFirst()
{
    const auto vars = WebServer::graphVars(makeSamples(3), 100, 100, 500, 500, 120);

    // Oldest first, the order MFC writes and a graph reads left to right.
    QCOMPARE(vars.value(QStringLiteral("GraphConnections")), QStringLiteral("10,20,30"));
    QCOMPARE(vars.value(QStringLiteral("MaxConnections")), QStringLiteral("500"));
}

void tst_WebServer::graphVars_ratesAreBytesPerSecond()
{
    const auto vars = WebServer::graphVars(makeSamples(2), 100, 100, 500, 500, 120);

    // The samples are KB/s; MFC's variables are bytes/s (srchybrid/WebServer.cpp:3038).
    QCOMPARE(vars.value(QStringLiteral("GraphDownload")), QStringLiteral("1024,2048"));
    QCOMPARE(vars.value(QStringLiteral("GraphUpload")), QStringLiteral("2048,4096"));
}

void tst_WebServer::graphVars_pointsStayInsideTheViewBox()
{
    // A rate well over the axis maximum must clamp to the top of the box rather than
    // draw outside it — the capacity pref is a guess, not a limit.
    std::vector<StatsGraphSample> samples = makeSamples(2);
    samples[1].downCurrent = 10000.0f;

    const auto vars = WebServer::graphVars(samples, 100, 100, 500, 500, 120);
    const QStringList points =
        vars.value(QStringLiteral("GraphDownloadPts")).split(u' ', Qt::SkipEmptyParts);

    QCOMPARE(points.size(), 2);
    for (const QString& point : points) {
        const QStringList xy = point.split(u',');
        QCOMPARE(xy.size(), 2);
        const double x = xy[0].toDouble();
        const double y = xy[1].toDouble();
        QVERIFY(x >= 0.0 && x <= 499.0);
        QVERIFY(y >= 0.0 && y <= 120.0);
    }
    // Two samples span the full width, and the over-scale one sits on the ceiling.
    QCOMPARE(points.first(), QStringLiteral("0.0,118.8"));
    QCOMPARE(points.last(), QStringLiteral("499.0,0.0"));
}

void tst_WebServer::graphVars_withNoSamplesAreEmpty()
{
    // A daemon that has not sampled yet leaves the series empty — an empty variable
    // still substitutes, where a missing one would leave "[GraphDownload]" on the page.
    const auto vars = WebServer::graphVars({}, 100, 100, 500, 500, 120);

    QVERIFY(vars.contains(QStringLiteral("GraphDownload")));
    QVERIFY(vars.value(QStringLiteral("GraphDownload")).isEmpty());
    QVERIFY(vars.value(QStringLiteral("GraphDownloadPts")).isEmpty());
    QCOMPARE(vars.value(QStringLiteral("MaxDownload")), QStringLiteral("100"));
}

// ---------------------------------------------------------------------------
// Incoming folder browsing
//
// These three routes are what the GUI opens instead of the OS file manager when
// its core runs on another machine. They authenticate with the stream token, not
// with the API key or a web-UI session, so every case below deliberately sends
// no X-Api-Key.
// ---------------------------------------------------------------------------

void tst_WebServer::buildIncomingTree()
{
    m_incoming = std::make_unique<QTemporaryDir>();
    QVERIFY(m_incoming->isValid());

    const QDir root(m_incoming->path());
    QVERIFY(root.mkpath(QStringLiteral("Season 1")));

    const auto write = [&root](const QString& rel, const QByteArray& data) {
        QFile f(root.filePath(rel));
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(data), qint64(data.size()));
    };

    write(QStringLiteral("Season 1/episode.txt"), QByteArrayLiteral("nested"));
    write(QStringLiteral("clip.mp4"), QByteArrayLiteral("not really an mp4"));
    write(QStringLiteral("movie.mkv"), QByteArrayLiteral("not really an mkv"));
    write(QStringLiteral("notes.txt"), QByteArrayLiteral("plain"));

    // Larger than kPreviewChunkBytes, so a download served through the preview
    // path — which caps its body at 4 MiB — fails this fixture instead of
    // quietly handing browsers a truncated file.
    m_bigFileBytes.resize(5 * 1024 * 1024);
    for (qsizetype i = 0; i < m_bigFileBytes.size(); ++i)
        m_bigFileBytes[i] = char(i * 31 + (i >> 11));
    write(QStringLiteral("big.bin"), m_bigFileBytes);

    // A symlink out of the tree. Nothing but canonicalisation catches this one.
    QFile::link(QStringLiteral("/etc/hosts"), root.filePath(QStringLiteral("escape.txt")));

    m_preferences->setIncomingDir(m_incoming->path());
}

QString tst_WebServer::incomingUrl(const QString& path, const QString& key,
                                   const QString& value) const
{
    QString url = path + QStringLiteral("?token=") + m_webServer->streamToken();
    if (!key.isEmpty()) {
        url += QLatin1Char('&') + key + QLatin1Char('=')
             + QString::fromLatin1(QUrl::toPercentEncoding(value));
    }
    return url;
}

void tst_WebServer::incomingRejectsAMissingOrWrongStreamToken()
{
    for (const QString& path : {QStringLiteral("/api/v1/incoming"),
                                QStringLiteral("/api/v1/incoming/stream?file=notes.txt"),
                                QStringLiteral("/api/v1/incoming/download?file=notes.txt")}) {
        QCOMPARE(sendRanged(path, {}).statusCode, 401);

        const QString sep = path.contains(QLatin1Char('?')) ? QStringLiteral("&")
                                                            : QStringLiteral("?");
        const auto wrong = sendRanged(path + sep + QStringLiteral("token=not-the-token"), {});
        QCOMPARE(wrong.statusCode, 401);
        // A 401 must not hand back the value it just rejected a guess against.
        QVERIFY(!wrong.rawBody.contains(m_webServer->streamToken().toUtf8()));
    }
}

void tst_WebServer::incomingListingShowsFilesAndFolders()
{
    const auto root = sendRanged(incomingUrl(QStringLiteral("/api/v1/incoming")), {});
    QCOMPARE(root.statusCode, 200);
    QVERIFY(root.headers.value(QStringLiteral("content-type")).startsWith(QStringLiteral("text/html")));

    const QString html = QString::fromUtf8(root.rawBody);
    QVERIFY(html.contains(QStringLiteral("Season 1")));
    QVERIFY(html.contains(QStringLiteral("notes.txt")));
    QVERIFY(html.contains(QStringLiteral("5.0 MB")));       // big.bin, humanised

    // A folder navigates; it is never offered as a download.
    QVERIFY(html.contains(QStringLiteral("path=Season%201")));

    // Video and audio get a second link, and which one depends on whether a
    // browser can play the container at all.
    QVERIFY(html.contains(QStringLiteral(">Play<")));
    QVERIFY(html.contains(QStringLiteral("play=clip.mp4")));
    QVERIFY(html.contains(QStringLiteral(">Stream<")));
    // &amp;, not &: the href is HTML-escaped, which is what keeps a release name
    // containing an ampersand from ending the attribute early.
    QVERIFY(html.contains(QStringLiteral("/api/v1/incoming/stream?token=%1&amp;file=movie.mkv")
                              .arg(m_webServer->streamToken())));

    // A plain file has exactly one action.
    QVERIFY(html.contains(QStringLiteral("file=notes.txt")));
    QVERIFY(!html.contains(QStringLiteral("play=notes.txt")));

    // Descending works, and the subfolder offers a way back up.
    const auto sub = sendRanged(
        incomingUrl(QStringLiteral("/api/v1/incoming"), QStringLiteral("path"),
                    QStringLiteral("Season 1")), {});
    QCOMPARE(sub.statusCode, 200);
    const QString subHtml = QString::fromUtf8(sub.rawBody);
    QVERIFY(subHtml.contains(QStringLiteral("episode.txt")));
    QVERIFY(subHtml.contains(QStringLiteral("../")));
}

void tst_WebServer::incomingRefusesToEscapeTheIncomingDir()
{
    const QStringList escapes = {
        QStringLiteral("../"),
        QStringLiteral("../../etc/passwd"),
        QStringLiteral("..\\..\\windows\\win.ini"),
        QStringLiteral("Season 1/../../etc/passwd"),
        QStringLiteral("/etc/passwd"),
        QStringLiteral("escape.txt"),          // a symlink pointing out of the tree
    };

    for (const QString& rel : escapes) {
        const auto listing = sendRanged(
            incomingUrl(QStringLiteral("/api/v1/incoming"), QStringLiteral("path"), rel), {});
        QVERIFY2(listing.statusCode == 404 || listing.statusCode == 400,
                 qPrintable(QStringLiteral("listing served %1 for %2")
                                .arg(listing.statusCode).arg(rel)));

        for (const QString& route : {QStringLiteral("/api/v1/incoming/stream"),
                                     QStringLiteral("/api/v1/incoming/download")}) {
            const auto bytes = sendRanged(
                incomingUrl(route, QStringLiteral("file"), rel), {});
            QCOMPARE(bytes.statusCode, 404);
            QVERIFY(!bytes.rawBody.contains(QByteArrayLiteral("root:")));
        }
    }
}

void tst_WebServer::incomingDownloadSendsTheWholeFileAsAnAttachment()
{
    const auto resp = sendRanged(
        incomingUrl(QStringLiteral("/api/v1/incoming/download"), QStringLiteral("file"),
                    QStringLiteral("big.bin")), {});

    QCOMPARE(resp.statusCode, 200);
    // The whole file, not one preview window: this is the assertion that a
    // download routed through serveRange would fail.
    QCOMPARE(resp.rawBody.size(), m_bigFileBytes.size());
    QCOMPARE(resp.rawBody, m_bigFileBytes);

    const QString disposition = resp.headers.value(QStringLiteral("content-disposition"));
    QVERIFY2(disposition.startsWith(QStringLiteral("attachment")), qPrintable(disposition));
    QVERIFY(disposition.contains(QStringLiteral("big.bin")));
}

void tst_WebServer::incomingStreamHonoursARangeRequest()
{
    const QString url = incomingUrl(QStringLiteral("/api/v1/incoming/stream"),
                                    QStringLiteral("file"), QStringLiteral("big.bin"));

    const auto ranged = sendRanged(url, QByteArrayLiteral("bytes=1000-1099"));
    QCOMPARE(ranged.statusCode, 206);
    QCOMPARE(ranged.rawBody, m_bigFileBytes.mid(1000, 100));
    QCOMPARE(ranged.headers.value(QStringLiteral("content-range")),
             QStringLiteral("bytes 1000-1099/%1").arg(m_bigFileBytes.size()));
    QCOMPARE(ranged.headers.value(QStringLiteral("accept-ranges")), QStringLiteral("bytes"));
    QVERIFY(ranged.headers.value(QStringLiteral("content-disposition"))
                .startsWith(QStringLiteral("inline")));

    // No Range at all still comes back partial, because the body is capped —
    // which is exactly why the download route cannot share this path.
    const auto whole = sendRanged(url, {});
    QCOMPARE(whole.statusCode, 206);
    QVERIFY(whole.rawBody.size() < m_bigFileBytes.size());
    QCOMPARE(whole.rawBody, m_bigFileBytes.left(whole.rawBody.size()));
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

QTEST_MAIN(tst_WebServer)
#include "tst_WebServer.moc"
