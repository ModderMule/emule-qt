/// @file WebServer.cpp
/// @brief JSON REST API web server — implementation.

#include "webserver/WebServer.h"
#include "webserver/JsonSerializers.h"

#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "friends/Friend.h"
#include "friends/FriendList.h"
#include "prefs/Preferences.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "search/SearchParams.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "stats/Statistics.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>

namespace eMule {

// ---------------------------------------------------------------------------
// Auth check result
// ---------------------------------------------------------------------------

struct WebServer::AuthResult {
    bool ok = false;
    QHttpServerResponse response{QHttpServerResponse::StatusCode::Ok};
};

// ---------------------------------------------------------------------------
// JSON error / success helpers
// ---------------------------------------------------------------------------

namespace {

QHttpServerResponse jsonError(int code, const QString& message)
{
    QJsonObject errObj{
        {QStringLiteral("code"),    code},
        {QStringLiteral("message"), message},
    };
    QJsonObject root{{QStringLiteral("error"), errObj}};

    return QHttpServerResponse(root,
        static_cast<QHttpServerResponse::StatusCode>(code));
}

QHttpServerResponse jsonSuccess(const QJsonObject& data)
{
    return QHttpServerResponse(data);
}

QHttpServerResponse jsonSuccess(const QJsonArray& data)
{
    return QHttpServerResponse(data);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

WebServer::WebServer(QObject* parent)
    : QObject(parent)
{
}

WebServer::~WebServer()
{
    stop();
}

bool WebServer::start(const WebServerConfig& config)
{
    if (m_server)
        stop();

    m_config = config;

    if (!m_config.enabled)
        return false;

    m_server = std::make_unique<QHttpServer>(this);

    registerRoutes();

    // CORS: add response headers to every response
    m_server->addAfterRequestHandler(this,
        [](const QHttpServerRequest&, QHttpServerResponse& resp) {
            auto hdrs = resp.headers();
            hdrs.append(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin,
                        QStringLiteral("*"));
            hdrs.append(QHttpHeaders::WellKnownHeader::AccessControlAllowHeaders,
                        QStringLiteral("X-Api-Key, Content-Type"));
            hdrs.append(QHttpHeaders::WellKnownHeader::AccessControlAllowMethods,
                        QStringLiteral("GET, POST, PATCH, DELETE, OPTIONS"));
            resp.setHeaders(std::move(hdrs));
        });

    // Bind to a QTcpServer
    m_tcpServer = new QTcpServer(m_server.get());
    const QHostAddress addr = m_config.listenAddress.isEmpty()
        ? QHostAddress::Any
        : QHostAddress(m_config.listenAddress);

    if (!m_tcpServer->listen(addr, m_config.port)) {
        logError(QStringLiteral("WebServer: failed to listen on port %1: %2")
                     .arg(m_config.port)
                     .arg(m_tcpServer->errorString()));
        m_server.reset();
        m_tcpServer = nullptr;
        return false;
    }

    m_server->bind(m_tcpServer);

    const auto actualPort = m_tcpServer->serverPort();
    logInfo(QStringLiteral("WebServer: listening on port %1").arg(actualPort));

    emit started(actualPort);
    return true;
}

void WebServer::stop()
{
    if (!m_server)
        return;

    m_server.reset();   // Destroys QHttpServer + owned QTcpServer
    m_tcpServer = nullptr;

    logInfo(QStringLiteral("WebServer: stopped"));
    emit stopped();
}

bool WebServer::isRunning() const
{
    return m_server != nullptr && m_tcpServer != nullptr && m_tcpServer->isListening();
}

uint16 WebServer::port() const
{
    if (m_tcpServer && m_tcpServer->isListening())
        return m_tcpServer->serverPort();
    return 0;
}

// ---------------------------------------------------------------------------
// Dependency injection
// ---------------------------------------------------------------------------

void WebServer::setDownloadQueue(DownloadQueue* dq)  { m_downloadQueue = dq; }
void WebServer::setUploadQueue(UploadQueue* uq)      { m_uploadQueue = uq; }
void WebServer::setServerList(ServerList* sl)         { m_serverList = sl; }
void WebServer::setServerConnect(ServerConnect* sc)   { m_serverConnect = sc; }
void WebServer::setSearchList(SearchList* search)     { m_searchList = search; }
void WebServer::setSharedFileList(SharedFileList* sf) { m_sharedFiles = sf; }
void WebServer::setFriendList(FriendList* fl)         { m_friendList = fl; }
void WebServer::setStatistics(Statistics* stats)      { m_statistics = stats; }
void WebServer::setPreferences(Preferences* prefs)    { m_preferences = prefs; }

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

void WebServer::registerRoutes()
{
    // OPTIONS catch-all for CORS preflight
    m_server->route(QStringLiteral("/<arg>"), QHttpServerRequest::Method::Options,
        [](const QUrl&) {
            return QHttpServerResponse(QHttpServerResponse::StatusCode::NoContent);
        });

    // --- Downloads ---
    m_server->route(QStringLiteral("/api/v1/downloads"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleGetDownloads();
        });

    m_server->route(QStringLiteral("/api/v1/downloads/<arg>"), QHttpServerRequest::Method::Get,
        [this](const QString& hash, const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleGetDownload(hash);
        });

    m_server->route(QStringLiteral("/api/v1/downloads/<arg>/pause"), QHttpServerRequest::Method::Post,
        [this](const QString& hash, const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handlePauseDownload(hash);
        });

    m_server->route(QStringLiteral("/api/v1/downloads/<arg>/resume"), QHttpServerRequest::Method::Post,
        [this](const QString& hash, const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleResumeDownload(hash);
        });

    m_server->route(QStringLiteral("/api/v1/downloads/<arg>/cancel"), QHttpServerRequest::Method::Post,
        [this](const QString& hash, const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleCancelDownload(hash);
        });

    // --- Uploads ---
    m_server->route(QStringLiteral("/api/v1/uploads"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleGetUploads();
        });

    // --- Servers ---
    m_server->route(QStringLiteral("/api/v1/servers"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleGetServers();
        });

    // --- Connection ---
    m_server->route(QStringLiteral("/api/v1/connection"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleGetConnection();
        });

    m_server->route(QStringLiteral("/api/v1/connection/connect"), QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handlePostConnect();
        });

    m_server->route(QStringLiteral("/api/v1/connection/disconnect"), QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handlePostDisconnect();
        });

    // --- Search ---
    m_server->route(QStringLiteral("/api/v1/search"), QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            auto doc = QJsonDocument::fromJson(req.body());
            if (!doc.isObject())
                return jsonError(400, QStringLiteral("Invalid JSON body"));
            return handlePostSearch(doc.object());
        });

    m_server->route(QStringLiteral("/api/v1/search/<arg>/results"), QHttpServerRequest::Method::Get,
        [this](uint32 searchID, const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleGetSearchResults(searchID);
        });

    // --- Shared files ---
    m_server->route(QStringLiteral("/api/v1/shared"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleGetSharedFiles();
        });

    // --- Friends ---
    m_server->route(QStringLiteral("/api/v1/friends"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleGetFriends();
        });

    m_server->route(QStringLiteral("/api/v1/friends"), QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            auto doc = QJsonDocument::fromJson(req.body());
            if (!doc.isObject())
                return jsonError(400, QStringLiteral("Invalid JSON body"));
            return handlePostFriend(doc.object());
        });

    m_server->route(QStringLiteral("/api/v1/friends/<arg>"), QHttpServerRequest::Method::Delete,
        [this](const QString& hash, const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleDeleteFriend(hash);
        });

    // --- Statistics ---
    m_server->route(QStringLiteral("/api/v1/stats"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleGetStats();
        });

    // --- Preferences ---
    m_server->route(QStringLiteral("/api/v1/preferences"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleGetPreferences();
        });

    m_server->route(QStringLiteral("/api/v1/preferences"), QHttpServerRequest::Method::Patch,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            auto doc = QJsonDocument::fromJson(req.body());
            if (!doc.isObject())
                return jsonError(400, QStringLiteral("Invalid JSON body"));
            return handlePatchPreferences(doc.object());
        });
}

// ---------------------------------------------------------------------------
// Auth
// ---------------------------------------------------------------------------

WebServer::AuthResult WebServer::checkAuth(const QHttpHeaders& headers) const
{
    if (m_config.apiKey.isEmpty())
        return {true, QHttpServerResponse(QHttpServerResponse::StatusCode::Ok)};

    const auto key = headers.combinedValue(QByteArrayLiteral("X-Api-Key"));
    if (key.isEmpty() || QString::fromUtf8(key) != m_config.apiKey)
        return {false, jsonError(401, QStringLiteral("Unauthorized: invalid or missing API key"))};

    return {true, QHttpServerResponse(QHttpServerResponse::StatusCode::Ok)};
}

// ---------------------------------------------------------------------------
// Download handlers
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handleGetDownloads()
{
    if (!m_downloadQueue)
        return jsonError(500, QStringLiteral("Download queue not available"));

    QJsonArray arr;
    for (const auto* file : m_downloadQueue->files())
        arr.append(toJson(*file));

    return jsonSuccess(arr);
}

QHttpServerResponse WebServer::handleGetDownload(const QString& hash)
{
    if (!m_downloadQueue)
        return jsonError(500, QStringLiteral("Download queue not available"));

    std::array<uint8, 16> hashBytes{};
    if (hash.size() != 32 || decodeBase16(hash, hashBytes.data(), 16) != 16)
        return jsonError(400, QStringLiteral("Invalid hash format"));

    auto* file = m_downloadQueue->fileByID(hashBytes.data());
    if (!file)
        return jsonError(404, QStringLiteral("Download not found"));

    return jsonSuccess(toJson(*file));
}

QHttpServerResponse WebServer::handlePauseDownload(const QString& hash)
{
    if (!m_downloadQueue)
        return jsonError(500, QStringLiteral("Download queue not available"));

    std::array<uint8, 16> hashBytes{};
    if (hash.size() != 32 || decodeBase16(hash, hashBytes.data(), 16) != 16)
        return jsonError(400, QStringLiteral("Invalid hash format"));

    auto* file = m_downloadQueue->fileByID(hashBytes.data());
    if (!file)
        return jsonError(404, QStringLiteral("Download not found"));

    file->pauseFile();
    return jsonSuccess(toJson(*file));
}

QHttpServerResponse WebServer::handleResumeDownload(const QString& hash)
{
    if (!m_downloadQueue)
        return jsonError(500, QStringLiteral("Download queue not available"));

    std::array<uint8, 16> hashBytes{};
    if (hash.size() != 32 || decodeBase16(hash, hashBytes.data(), 16) != 16)
        return jsonError(400, QStringLiteral("Invalid hash format"));

    auto* file = m_downloadQueue->fileByID(hashBytes.data());
    if (!file)
        return jsonError(404, QStringLiteral("Download not found"));

    file->resumeFile();
    return jsonSuccess(toJson(*file));
}

QHttpServerResponse WebServer::handleCancelDownload(const QString& hash)
{
    if (!m_downloadQueue)
        return jsonError(500, QStringLiteral("Download queue not available"));

    std::array<uint8, 16> hashBytes{};
    if (hash.size() != 32 || decodeBase16(hash, hashBytes.data(), 16) != 16)
        return jsonError(400, QStringLiteral("Invalid hash format"));

    auto* file = m_downloadQueue->fileByID(hashBytes.data());
    if (!file)
        return jsonError(404, QStringLiteral("Download not found"));

    file->stopFile(/*cancel=*/true);
    return jsonSuccess(QJsonObject{{QStringLiteral("cancelled"), true}});
}

// ---------------------------------------------------------------------------
// Upload handlers
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handleGetUploads()
{
    if (!m_uploadQueue)
        return jsonError(500, QStringLiteral("Upload queue not available"));

    QJsonObject obj{
        {QStringLiteral("datarate"),           static_cast<qint64>(m_uploadQueue->datarate())},
        {QStringLiteral("uploadQueueLength"),  m_uploadQueue->uploadQueueLength()},
        {QStringLiteral("waitingUserCount"),   m_uploadQueue->waitingUserCount()},
        {QStringLiteral("successfulUploads"),  static_cast<qint64>(m_uploadQueue->successfulUploadCount())},
        {QStringLiteral("failedUploads"),      static_cast<qint64>(m_uploadQueue->failedUploadCount())},
    };
    return jsonSuccess(obj);
}

// ---------------------------------------------------------------------------
// Server handlers
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handleGetServers()
{
    if (!m_serverList)
        return jsonError(500, QStringLiteral("Server list not available"));

    QJsonArray arr;
    for (const auto& srv : m_serverList->servers())
        arr.append(toJson(*srv));

    return jsonSuccess(arr);
}

// ---------------------------------------------------------------------------
// Connection handlers
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handleGetConnection()
{
    if (!m_serverConnect)
        return jsonError(500, QStringLiteral("Server connection not available"));

    const auto* current = m_serverConnect->currentServer();
    QJsonObject obj{
        {QStringLiteral("isConnected"),  m_serverConnect->isConnected()},
        {QStringLiteral("isConnecting"), m_serverConnect->isConnecting()},
        {QStringLiteral("isLowID"),      m_serverConnect->isLowID()},
        {QStringLiteral("clientID"),     static_cast<qint64>(m_serverConnect->clientID())},
    };

    if (current) {
        obj[QStringLiteral("currentServer")] = QJsonObject{
            {QStringLiteral("name"),    current->name()},
            {QStringLiteral("address"), current->address()},
            {QStringLiteral("port"),    current->port()},
        };
    }

    return jsonSuccess(obj);
}

QHttpServerResponse WebServer::handlePostConnect()
{
    if (!m_serverConnect)
        return jsonError(500, QStringLiteral("Server connection not available"));

    m_serverConnect->connectToAnyServer();
    return jsonSuccess(QJsonObject{{QStringLiteral("connecting"), true}});
}

QHttpServerResponse WebServer::handlePostDisconnect()
{
    if (!m_serverConnect)
        return jsonError(500, QStringLiteral("Server connection not available"));

    m_serverConnect->disconnect();
    return jsonSuccess(QJsonObject{{QStringLiteral("disconnected"), true}});
}

// ---------------------------------------------------------------------------
// Search handlers
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handlePostSearch(const QJsonObject& body)
{
    if (!m_searchList)
        return jsonError(500, QStringLiteral("Search list not available"));

    const auto expression = body[QStringLiteral("expression")].toString();
    if (expression.isEmpty())
        return jsonError(400, QStringLiteral("Missing 'expression' field"));

    SearchParams params;
    params.expression = expression;
    params.keyword = expression;
    params.searchTitle = expression;

    if (body.contains(QStringLiteral("fileType")))
        params.fileType = body[QStringLiteral("fileType")].toString();
    if (body.contains(QStringLiteral("minSize")))
        params.minSize = static_cast<uint64>(body[QStringLiteral("minSize")].toDouble());
    if (body.contains(QStringLiteral("maxSize")))
        params.maxSize = static_cast<uint64>(body[QStringLiteral("maxSize")].toDouble());

    const auto typeStr = body[QStringLiteral("type")].toString(QStringLiteral("ed2kServer"));
    if (typeStr == QStringLiteral("kad"))
        params.type = SearchType::Kademlia;
    else if (typeStr == QStringLiteral("ed2kGlobal"))
        params.type = SearchType::Ed2kGlobal;
    else
        params.type = SearchType::Ed2kServer;

    const auto searchID = m_searchList->newSearch(params.fileType, params);

    return jsonSuccess(QJsonObject{
        {QStringLiteral("searchID"), static_cast<qint64>(searchID)},
    });
}

QHttpServerResponse WebServer::handleGetSearchResults(uint32 searchID)
{
    if (!m_searchList)
        return jsonError(500, QStringLiteral("Search list not available"));

    QJsonArray files;
    const bool found = m_searchList->forEachResult(searchID,
        [&files](const SearchFile* file) {
            files.append(toJson(*file));
        });

    if (!found)
        return jsonSuccess(QJsonArray{});

    QJsonObject result{
        {QStringLiteral("searchID"),     static_cast<qint64>(searchID)},
        {QStringLiteral("resultCount"),  static_cast<qint64>(m_searchList->resultCount(searchID))},
        {QStringLiteral("foundFiles"),   static_cast<qint64>(m_searchList->foundFiles(searchID))},
        {QStringLiteral("foundSources"), static_cast<qint64>(m_searchList->foundSources(searchID))},
        {QStringLiteral("results"),      files},
    };

    return jsonSuccess(result);
}

// ---------------------------------------------------------------------------
// Shared file handlers
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handleGetSharedFiles()
{
    if (!m_sharedFiles)
        return jsonError(500, QStringLiteral("Shared file list not available"));

    QJsonArray arr;
    m_sharedFiles->forEachFile([&arr](KnownFile* file) {
        arr.append(QJsonObject{
            {QStringLiteral("hash"),     md4str(file->fileHash())},
            {QStringLiteral("fileName"), file->fileName()},
            {QStringLiteral("fileSize"), static_cast<qint64>(file->fileSize())},
        });
    });

    return jsonSuccess(arr);
}

// ---------------------------------------------------------------------------
// Friend handlers
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handleGetFriends()
{
    if (!m_friendList)
        return jsonError(500, QStringLiteral("Friend list not available"));

    QJsonArray arr;
    for (const auto& f : m_friendList->friends())
        arr.append(toJson(*f));

    return jsonSuccess(arr);
}

QHttpServerResponse WebServer::handlePostFriend(const QJsonObject& body)
{
    if (!m_friendList)
        return jsonError(500, QStringLiteral("Friend list not available"));

    const auto hashStr = body[QStringLiteral("hash")].toString();
    const auto name = body[QStringLiteral("name")].toString();
    const auto ip = static_cast<uint32>(body[QStringLiteral("ip")].toDouble());
    const auto friendPort = static_cast<uint16>(body[QStringLiteral("port")].toInt());

    bool hasHash = !hashStr.isEmpty() && hashStr.size() == 32;
    std::array<uint8, 16> hashBytes{};

    if (hasHash)
        decodeBase16(hashStr, hashBytes.data(), 16);

    auto* f = m_friendList->addFriend(hasHash ? hashBytes.data() : nullptr,
                                       ip, friendPort, name, hasHash);
    if (!f)
        return jsonError(400, QStringLiteral("Failed to add friend"));

    return jsonSuccess(toJson(*f));
}

QHttpServerResponse WebServer::handleDeleteFriend(const QString& hash)
{
    if (!m_friendList)
        return jsonError(500, QStringLiteral("Friend list not available"));

    std::array<uint8, 16> hashBytes{};
    if (hash.size() != 32 || decodeBase16(hash, hashBytes.data(), 16) != 16)
        return jsonError(400, QStringLiteral("Invalid hash format"));

    auto* f = m_friendList->searchFriend(hashBytes.data());
    if (!f)
        return jsonError(404, QStringLiteral("Friend not found"));

    m_friendList->removeFriend(f);
    return jsonSuccess(QJsonObject{{QStringLiteral("deleted"), true}});
}

// ---------------------------------------------------------------------------
// Statistics handlers
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handleGetStats()
{
    if (!m_statistics)
        return jsonError(500, QStringLiteral("Statistics not available"));

    QJsonObject obj{
        {QStringLiteral("rateDown"),             static_cast<double>(m_statistics->rateDown())},
        {QStringLiteral("rateUp"),               static_cast<double>(m_statistics->rateUp())},
        {QStringLiteral("maxDown"),              static_cast<double>(m_statistics->maxDown())},
        {QStringLiteral("maxUp"),                static_cast<double>(m_statistics->maxUp())},
        {QStringLiteral("sessionReceivedBytes"), static_cast<qint64>(m_statistics->sessionReceivedBytes())},
        {QStringLiteral("sessionSentBytes"),     static_cast<qint64>(m_statistics->sessionSentBytes())},
        {QStringLiteral("reconnects"),           m_statistics->reconnects()},
        {QStringLiteral("uptimeSeconds"),        static_cast<qint64>(m_statistics->transferTime())},
    };

    return jsonSuccess(obj);
}

// ---------------------------------------------------------------------------
// Preferences handlers
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handleGetPreferences()
{
    if (!m_preferences)
        return jsonError(500, QStringLiteral("Preferences not available"));

    QJsonObject obj{
        {QStringLiteral("nick"),         m_preferences->nick()},
        {QStringLiteral("maxUpload"),    static_cast<qint64>(m_preferences->maxUpload())},
        {QStringLiteral("maxDownload"),  static_cast<qint64>(m_preferences->maxDownload())},
        {QStringLiteral("port"),         m_preferences->port()},
        {QStringLiteral("udpPort"),      m_preferences->udpPort()},
        {QStringLiteral("autoConnect"),  m_preferences->autoConnect()},
        {QStringLiteral("kadEnabled"),   m_preferences->kadEnabled()},
        {QStringLiteral("incomingDir"),  m_preferences->incomingDir()},
    };

    return jsonSuccess(obj);
}

QHttpServerResponse WebServer::handlePatchPreferences(const QJsonObject& body)
{
    if (!m_preferences)
        return jsonError(500, QStringLiteral("Preferences not available"));

    if (body.contains(QStringLiteral("nick")))
        m_preferences->setNick(body[QStringLiteral("nick")].toString());
    if (body.contains(QStringLiteral("maxUpload")))
        m_preferences->setMaxUpload(static_cast<uint32>(body[QStringLiteral("maxUpload")].toDouble()));
    if (body.contains(QStringLiteral("maxDownload")))
        m_preferences->setMaxDownload(static_cast<uint32>(body[QStringLiteral("maxDownload")].toDouble()));
    if (body.contains(QStringLiteral("autoConnect")))
        m_preferences->setAutoConnect(body[QStringLiteral("autoConnect")].toBool());
    if (body.contains(QStringLiteral("kadEnabled")))
        m_preferences->setKadEnabled(body[QStringLiteral("kadEnabled")].toBool());

    return handleGetPreferences();
}

} // namespace eMule
