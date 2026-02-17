/// @file tst_WebServer.cpp
/// @brief Unit tests for the JSON REST API WebServer (Module 19).

#include "webserver/WebServer.h"

#include "friends/FriendList.h"
#include "prefs/Preferences.h"
#include "search/SearchList.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "stats/Statistics.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"
#include "files/KnownFileList.h"
#include "files/SharedFileList.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
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

    QString baseUrl() const;

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

    QNetworkAccessManager m_nam;
    QString m_apiKey = QStringLiteral("test-secret-key-12345");
    uint16 m_port = 0;
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

    WebServerConfig config;
    config.enabled = true;
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
// Main
// ---------------------------------------------------------------------------

QTEST_MAIN(tst_WebServer)
#include "tst_WebServer.moc"
