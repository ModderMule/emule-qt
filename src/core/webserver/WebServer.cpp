/// @file WebServer.cpp
/// @brief JSON REST API + template web server — implementation.

#include "webserver/WebServer.h"
#include "webserver/JsonSerializers.h"
#include "webserver/WebSessionManager.h"
#include "webserver/WebTemplateEngine.h"

#include "client/UpDownClient.h"
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

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QTcpServer>
#include <QUrlQuery>

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

    // Initialize session manager
    m_sessionManager = std::make_unique<WebSessionManager>(m_config.sessionTimeout);

    // Initialize template engine
    m_templateEngine = std::make_unique<WebTemplateEngine>();

    // Resolve web data directory (config/webserver/)
    const QString configDir = m_preferences ? m_preferences->configDir() : QString();
    if (!configDir.isEmpty())
        m_webDataDir = configDir + QStringLiteral("/webserver");

    // Load template (config/eMule.tmpl by default)
    QString tmplPath = m_config.templatePath;
    if (tmplPath.isEmpty() && !configDir.isEmpty())
        tmplPath = configDir + QStringLiteral("/eMule.tmpl");
    if (QFile::exists(tmplPath))
        m_templateEngine->loadTemplate(tmplPath);

    m_server = std::make_unique<QHttpServer>(this);

    registerRoutes();

    // After-request handler: CORS + Gzip
    const bool gzipEnabled = m_config.gzipEnabled;
    m_server->addAfterRequestHandler(this,
        [gzipEnabled](const QHttpServerRequest& req, QHttpServerResponse& resp) {
            auto hdrs = resp.headers();
            // CORS
            hdrs.append(QHttpHeaders::WellKnownHeader::AccessControlAllowOrigin,
                        QStringLiteral("*"));
            hdrs.append(QHttpHeaders::WellKnownHeader::AccessControlAllowHeaders,
                        QStringLiteral("X-Api-Key, Content-Type"));
            hdrs.append(QHttpHeaders::WellKnownHeader::AccessControlAllowMethods,
                        QStringLiteral("GET, POST, PATCH, DELETE, OPTIONS"));

            // Gzip compression for text responses
            if (gzipEnabled) {
                const auto accept = req.headers().combinedValue(
                    QByteArrayLiteral("Accept-Encoding"));
                if (accept.contains("gzip")) {
                    auto body = resp.data();
                    if (body.size() > 256) {
                        auto compressed = gzipCompress(body);
                        if (!compressed.isEmpty() && compressed.size() < body.size()) {
                            auto ct = hdrs.value(QHttpHeaders::WellKnownHeader::ContentType);
                            resp = QHttpServerResponse(QByteArray(ct.data(), ct.size()), compressed, resp.statusCode());
                            hdrs = resp.headers();
                            hdrs.append(QHttpHeaders::WellKnownHeader::ContentEncoding,
                                        QStringLiteral("gzip"));
                        }
                    }
                }
            }
            resp.setHeaders(std::move(hdrs));
        });

    // Bind to TCP or SSL server
    const QHostAddress addr = m_config.listenAddress.isEmpty()
        ? QHostAddress::Any
        : QHostAddress(m_config.listenAddress);

    bool useSsl = false;
    if (m_config.httpsEnabled && !m_config.certPath.isEmpty() && !m_config.keyPath.isEmpty()) {
        QFile certFile(m_config.certPath);
        QFile keyFile(m_config.keyPath);
        if (certFile.open(QIODevice::ReadOnly) && keyFile.open(QIODevice::ReadOnly)) {
            auto* sslServer = new QSslServer(m_server.get());
            QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
            sslConfig.setLocalCertificate(QSslCertificate(certFile.readAll(), QSsl::Pem));
            sslConfig.setPrivateKey(QSslKey(keyFile.readAll(), QSsl::Rsa));
            sslServer->setSslConfiguration(sslConfig);

            if (sslServer->listen(addr, m_config.port)) {
                m_server->bind(sslServer);
                m_tcpServer = sslServer;
                useSsl = true;
            } else {
                logError(QStringLiteral("WebServer: HTTPS failed to listen on port %1: %2")
                             .arg(m_config.port).arg(sslServer->errorString()));
                delete sslServer;
            }
        } else {
            logError(QStringLiteral("WebServer: failed to open cert/key files, falling back to HTTP"));
        }
    }

    if (!useSsl) {
        m_tcpServer = new QTcpServer(m_server.get());

        if (!m_tcpServer->listen(addr, m_config.port)) {
            logError(QStringLiteral("WebServer: failed to listen on port %1: %2")
                         .arg(m_config.port)
                         .arg(m_tcpServer->errorString()));
            m_server.reset();
            m_tcpServer = nullptr;
            return false;
        }

        m_server->bind(m_tcpServer);
    }

    const auto actualPort = m_tcpServer->serverPort();
    logInfo(QStringLiteral("WebServer: listening on port %1%2")
                .arg(actualPort)
                .arg(m_config.httpsEnabled ? QStringLiteral(" (HTTPS)") : QString()));

    emit started(actualPort);
    return true;
}

void WebServer::reloadTemplate()
{
    if (m_templateEngine)
        m_templateEngine->reload();
}

void WebServer::stop()
{
    if (!m_server)
        return;

    m_server.reset();   // Destroys QHttpServer + owned QTcpServer
    m_tcpServer = nullptr;
    m_sessionManager.reset();
    m_templateEngine.reset();

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

    // --- Template web interface routes ---

    // Login form submission (POST /)
    m_server->route(QStringLiteral("/"), QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            return handleLogin(req);
        });

    // Main page dispatch (GET /)
    m_server->route(QStringLiteral("/"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) {
            return handlePage(req);
        });

    // Favicon
    m_server->route(QStringLiteral("/favicon.ico"), QHttpServerRequest::Method::Get,
        [this]() {
            return handleStaticFile(QStringLiteral("favicon.ico"));
        });

    // Static files (images, CSS)
    m_server->route(QStringLiteral("/<arg>"), QHttpServerRequest::Method::Get,
        [this](const QUrl& url, const QHttpServerRequest& req) {
            const QString path = url.path();
            // Serve REST API only if enabled
            if (path.startsWith(QStringLiteral("/api/"))) {
                // Let the specific API routes handle it below — this is a fallback
                return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
            }
            // Strip leading slash
            const QString file = path.mid(1);
            // Only serve known static file extensions
            if (file.endsWith(QStringLiteral(".gif")) || file.endsWith(QStringLiteral(".jpg")) ||
                file.endsWith(QStringLiteral(".png")) || file.endsWith(QStringLiteral(".ico")) ||
                file.endsWith(QStringLiteral(".css")) || file.endsWith(QStringLiteral(".js"))) {
                return handleStaticFile(file);
            }
            return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
        });

    // --- REST API routes (only if REST API is enabled) ---
    if (!m_config.restApiEnabled)
        return;

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

// ---------------------------------------------------------------------------
// Template web interface — Login
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handleLogin(const QHttpServerRequest& request)
{
    if (!m_sessionManager || !m_templateEngine || !m_templateEngine->isValid())
        return QHttpServerResponse(QByteArrayLiteral("text/plain"),
            QByteArrayLiteral("Web interface not configured"),
            QHttpServerResponse::StatusCode::InternalServerError);

    // Parse form body: w=password&p=<password>
    const QUrlQuery query(QString::fromUtf8(request.body()));
    const QString password = query.queryItemValue(QStringLiteral("p"));

    if (password.isEmpty()) {
        // Show login page with no error
        QHash<QString, QString> vars;
        vars[QStringLiteral("CharSet")] = QStringLiteral("UTF-8");
        vars[QStringLiteral("eMuleAppName")] = QStringLiteral("eMule");
        vars[QStringLiteral("version")] = QStringLiteral("0.70b");
        vars[QStringLiteral("WebControl")] = QStringLiteral("Web Control Panel");
        vars[QStringLiteral("FailedLogin")] = QString();
        const auto html = WebTemplateEngine::substitute(
            m_templateEngine->section(QStringLiteral("LOGIN")), vars);
        return QHttpServerResponse(QByteArrayLiteral("text/html"), html.toUtf8());
    }

    // Hash the password and attempt login
    const QByteArray passwordHash = QCryptographicHash::hash(
        password.toUtf8(), QCryptographicHash::Sha256).toHex();

    const QString sessionId = m_sessionManager->login(
        QString::fromLatin1(passwordHash),
        m_config.adminPasswordHash,
        m_config.guestPasswordHash,
        m_config.guestEnabled);

    if (sessionId.isEmpty()) {
        // Failed login — show login page with error
        QHash<QString, QString> vars;
        vars[QStringLiteral("CharSet")] = QStringLiteral("UTF-8");
        vars[QStringLiteral("eMuleAppName")] = QStringLiteral("eMule");
        vars[QStringLiteral("version")] = QStringLiteral("0.70b");
        vars[QStringLiteral("WebControl")] = QStringLiteral("Web Control Panel");
        vars[QStringLiteral("FailedLogin")] = QStringLiteral("<p class=\"failed\">Login failed</p>");
        const auto html = WebTemplateEngine::substitute(
            m_templateEngine->section(QStringLiteral("LOGIN")), vars);
        return QHttpServerResponse(QByteArrayLiteral("text/html"), html.toUtf8());
    }

    // Redirect to main page with session
    return QHttpServerResponse(QByteArrayLiteral("text/html"),
        QStringLiteral("<html><head><meta http-equiv=\"refresh\" content=\"0; url=/?ses=%1&w=transfer\"></head></html>")
            .arg(sessionId).toUtf8());
}

// ---------------------------------------------------------------------------
// Template web interface — Page dispatch
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handlePage(const QHttpServerRequest& request)
{
    if (!m_templateEngine || !m_templateEngine->isValid() || !m_sessionManager) {
        // No template loaded — show simple login or error
        QHash<QString, QString> vars;
        vars[QStringLiteral("CharSet")] = QStringLiteral("UTF-8");
        vars[QStringLiteral("eMuleAppName")] = QStringLiteral("eMule");
        vars[QStringLiteral("version")] = QStringLiteral("0.70b");
        vars[QStringLiteral("WebControl")] = QStringLiteral("Web Control Panel");
        vars[QStringLiteral("FailedLogin")] = QString();
        const QString loginTmpl = m_templateEngine ? m_templateEngine->section(QStringLiteral("LOGIN")) : QString();
        if (loginTmpl.isEmpty())
            return QHttpServerResponse(QByteArrayLiteral("text/html"),
                QStringLiteral("<html><body><h1>eMule Web Interface</h1><p>Template not loaded.</p></body></html>").toUtf8());
        const auto html = WebTemplateEngine::substitute(loginTmpl, vars);
        return QHttpServerResponse(QByteArrayLiteral("text/html"), html.toUtf8());
    }

    const QUrlQuery query(request.url());
    const QString ses = query.queryItemValue(QStringLiteral("ses"));
    const QString page = query.queryItemValue(QStringLiteral("w"));

    // Check for logout
    if (page == QStringLiteral("logout")) {
        if (!ses.isEmpty())
            m_sessionManager->logout(ses);
        QHash<QString, QString> vars;
        vars[QStringLiteral("CharSet")] = QStringLiteral("UTF-8");
        vars[QStringLiteral("eMuleAppName")] = QStringLiteral("eMule");
        vars[QStringLiteral("version")] = QStringLiteral("0.70b");
        vars[QStringLiteral("WebControl")] = QStringLiteral("Web Control Panel");
        vars[QStringLiteral("FailedLogin")] = QString();
        const auto html = WebTemplateEngine::substitute(
            m_templateEngine->section(QStringLiteral("LOGIN")), vars);
        return QHttpServerResponse(QByteArrayLiteral("text/html"), html.toUtf8());
    }

    // Validate session
    if (ses.isEmpty() || !m_sessionManager->isValid(ses)) {
        QHash<QString, QString> vars;
        vars[QStringLiteral("CharSet")] = QStringLiteral("UTF-8");
        vars[QStringLiteral("eMuleAppName")] = QStringLiteral("eMule");
        vars[QStringLiteral("version")] = QStringLiteral("0.70b");
        vars[QStringLiteral("WebControl")] = QStringLiteral("Web Control Panel");
        vars[QStringLiteral("FailedLogin")] = QString();
        const auto html = WebTemplateEngine::substitute(
            m_templateEngine->section(QStringLiteral("LOGIN")), vars);
        return QHttpServerResponse(QByteArrayLiteral("text/html"), html.toUtf8());
    }

    return renderPage(page.isEmpty() ? QStringLiteral("transfer") : page, ses);
}

QHttpServerResponse WebServer::renderPage(const QString& page, const QString& sessionId)
{
    const bool isAdmin = m_sessionManager->isAdmin(sessionId);

    // Build header vars
    QHash<QString, QString> headerVars;
    headerVars[QStringLiteral("CharSet")] = QStringLiteral("UTF-8");
    headerVars[QStringLiteral("eMuleAppName")] = QStringLiteral("eMule");
    headerVars[QStringLiteral("version")] = QStringLiteral("0.70b");
    headerVars[QStringLiteral("WebControl")] = QStringLiteral("Web Control Panel");
    headerVars[QStringLiteral("Session")] = sessionId;
    headerVars[QStringLiteral("ses")] = sessionId;

    // Connection status
    if (m_serverConnect) {
        headerVars[QStringLiteral("ServerName")] = m_serverConnect->currentServer()
            ? m_serverConnect->currentServer()->name() : QStringLiteral("Not connected");
        headerVars[QStringLiteral("Connected")] = m_serverConnect->isConnected()
            ? QStringLiteral("1") : QStringLiteral("0");
    }

    // Speed info
    if (m_statistics) {
        headerVars[QStringLiteral("Speed")] = QStringLiteral("%1 / %2")
            .arg(QString::number(m_statistics->rateDown(), 'f', 1),
                 QString::number(m_statistics->rateUp(), 'f', 1));
        headerVars[QStringLiteral("SpeedDown")] = QString::number(m_statistics->rateDown(), 'f', 1);
        headerVars[QStringLiteral("SpeedUp")] = QString::number(m_statistics->rateUp(), 'f', 1);
    }

    // Transfer count
    if (m_downloadQueue)
        headerVars[QStringLiteral("TransferCount")] = QString::number(m_downloadQueue->fileCount());

    // Admin controls
    headerVars[QStringLiteral("IsAdmin")] = isAdmin ? QStringLiteral("1") : QStringLiteral("0");
    headerVars[QStringLiteral("AdminAllowHiLevFunc")] = (isAdmin && m_config.adminAllowHiLevFunc)
        ? QStringLiteral("1") : QStringLiteral("0");

    // Active page highlighting
    static const QStringList pages = {
        QStringLiteral("transfer"), QStringLiteral("server"), QStringLiteral("search"),
        QStringLiteral("shared"), QStringLiteral("stats"), QStringLiteral("graphs"),
        QStringLiteral("options"), QStringLiteral("sinfo"), QStringLiteral("log"),
        QStringLiteral("debuglog"), QStringLiteral("kad"), QStringLiteral("myinfo")
    };
    for (const auto& p : pages) {
        headerVars[QStringLiteral("Page_") + p] = (p == page)
            ? QStringLiteral("active") : QString();
    }

    // Build the page content
    QString content;
    if (page == QStringLiteral("transfer"))
        content = buildTransferPage(isAdmin);
    else if (page == QStringLiteral("server"))
        content = buildServerListPage(isAdmin);
    else if (page == QStringLiteral("search"))
        content = buildSearchPage(isAdmin);
    else if (page == QStringLiteral("shared"))
        content = buildSharedFilesPage(isAdmin);
    else if (page == QStringLiteral("stats"))
        content = buildStatisticsPage();
    else if (page == QStringLiteral("graphs"))
        content = buildGraphsPage();
    else if (page == QStringLiteral("options"))
        content = buildPreferencesPage(isAdmin);
    else if (page == QStringLiteral("sinfo"))
        content = buildServerInfoPage();
    else if (page == QStringLiteral("log"))
        content = buildLogPage();
    else if (page == QStringLiteral("debuglog"))
        content = buildDebugLogPage();
    else if (page == QStringLiteral("kad"))
        content = buildKadPage();
    else if (page == QStringLiteral("myinfo"))
        content = buildMyInfoPage();
    else
        content = buildTransferPage(isAdmin);

    // Inject stylesheet into header via [StyleSheet] variable, then assemble page
    headerVars[QStringLiteral("StyleSheet")] =
        m_templateEngine->section(QStringLiteral("HEADER_STYLESHEET"));
    const QString header = WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("HEADER")), headerVars);
    const QString footer = m_templateEngine->section(QStringLiteral("FOOTER"));

    const QString fullPage = header + content + footer;
    return QHttpServerResponse(QByteArrayLiteral("text/html"), fullPage.toUtf8());
}

// ---------------------------------------------------------------------------
// Template web interface — Static file serving
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handleStaticFile(const QString& path)
{
    // Security: prevent directory traversal
    if (path.contains(QStringLiteral("..")) || path.contains(QStringLiteral("//")))
        return QHttpServerResponse(QHttpServerResponse::StatusCode::Forbidden);

    const QString filePath = m_webDataDir + QStringLiteral("/") + path;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
    }

    const QByteArray data = file.readAll();
    const QMimeDatabase mimeDb;
    const auto mimeType = mimeDb.mimeTypeForFile(filePath);

    QHttpServerResponse resp(mimeType.name().toUtf8(), data);

    // Cache static assets for 1 hour
    auto hdrs = resp.headers();
    hdrs.append(QHttpHeaders::WellKnownHeader::CacheControl,
                QStringLiteral("public, max-age=3600"));
    resp.setHeaders(std::move(hdrs));

    return resp;
}

// ---------------------------------------------------------------------------
// Template page builders
// ---------------------------------------------------------------------------

QString WebServer::buildTransferPage(bool isAdmin)
{
    QHash<QString, QString> vars;
    vars[QStringLiteral("Session")] = QStringLiteral("");  // will be set in renderPage via header

    // Build download list
    QString downLines;
    if (m_downloadQueue) {
        const QString lineTmpl = m_templateEngine->section(QStringLiteral("TRANSFER_DOWN_LINE"));
        int index = 0;
        for (const auto* file : m_downloadQueue->files()) {
            QHash<QString, QString> lineVars;
            lineVars[QStringLiteral("DownloadFileName")] = file->fileName();
            lineVars[QStringLiteral("DownloadFileSize")] = QString::number(file->fileSize());
            lineVars[QStringLiteral("DownloadFileHash")] = md4str(file->fileHash());
            lineVars[QStringLiteral("DownloadCompleted")] = QString::number(file->completedSize());
            lineVars[QStringLiteral("DownloadSpeed")] = QString::number(file->datarate(), 'f', 1);
            lineVars[QStringLiteral("DownloadSources")] = QString::number(file->sourceCount());
            lineVars[QStringLiteral("DownloadPriority")] = QString::number(file->downPriority());

            double progress = file->fileSize() > 0
                ? static_cast<double>(file->completedSize()) * 100.0 / static_cast<double>(file->fileSize())
                : 0.0;
            lineVars[QStringLiteral("DownloadPercent")] = QString::number(progress, 'f', 1);

            // Status icon (CSS sprite class name, no .gif suffix)
            switch (file->status()) {
            case PartFileStatus::Ready:
            case PartFileStatus::Empty:
                lineVars[QStringLiteral("DownloadStatus")] = QStringLiteral("t_downloading");
                break;
            case PartFileStatus::Paused:
                lineVars[QStringLiteral("DownloadStatus")] = QStringLiteral("t_paused");
                break;
            case PartFileStatus::Complete:
                lineVars[QStringLiteral("DownloadStatus")] = QStringLiteral("t_complete");
                break;
            case PartFileStatus::Error:
                lineVars[QStringLiteral("DownloadStatus")] = QStringLiteral("t_error");
                break;
            default:
                lineVars[QStringLiteral("DownloadStatus")] = QStringLiteral("t_waiting");
                break;
            }
            lineVars[QStringLiteral("DownloadIndex")] = QString::number(index++);

            downLines += WebTemplateEngine::substitute(lineTmpl, lineVars);
        }
    }

    // Build upload list
    QString upLines;
    uint32 totalUpSpeed = 0;
    uint64 totalUpTransferred = 0;
    if (m_uploadQueue) {
        const QString lineTmpl = m_templateEngine->section(QStringLiteral("TRANSFER_UP_LINE"));
        int upIndex = 0;
        m_uploadQueue->forEachUploading([&](UpDownClient* client) {
            QHash<QString, QString> lineVars;
            lineVars[QStringLiteral("1")] = client->userName().toHtmlEscaped();
            lineVars[QStringLiteral("ClientSoftV")] = client->clientSoftwareStr().toHtmlEscaped();
            lineVars[QStringLiteral("2")] = client->uploadFile()
                ? client->uploadFile()->fileName().toHtmlEscaped() : QStringLiteral("?");

            const uint64 transferred = client->sessionUp();
            const uint32 delay = client->getUpStartTimeDelay();
            const double speed = delay > 0
                ? static_cast<double>(transferred) * 1000.0 / static_cast<double>(delay)
                : 0.0;

            lineVars[QStringLiteral("3")] = QString::number(transferred);
            lineVars[QStringLiteral("4")] = QString::number(speed, 'f', 1);

            auto softToIcon = [](ClientSoftware soft) -> QString {
                switch (soft) {
                case ClientSoftware::eMule:        return QStringLiteral("0");
                case ClientSoftware::eDonkeyHybrid: return QStringLiteral("h");
                case ClientSoftware::eDonkey:      return QStringLiteral("0");
                case ClientSoftware::aMule:        return QStringLiteral("a");
                case ClientSoftware::MLDonkey:     return QStringLiteral("m");
                case ClientSoftware::Shareaza:     return QStringLiteral("s");
                case ClientSoftware::lphant:       return QStringLiteral("l");
                default:                           return QStringLiteral("u");
                }
            };

            lineVars[QStringLiteral("ClientState")] = QStringLiteral("uploading");
            lineVars[QStringLiteral("ClientSoft")] = softToIcon(client->clientSoft());
            lineVars[QStringLiteral("ClientExtra")] = QStringLiteral("none");
            lineVars[QStringLiteral("UserHash")] = md4str(client->userHash());
            lineVars[QStringLiteral("FileInfo")] = client->uploadFile()
                ? client->uploadFile()->fileName().toHtmlEscaped() : QString();
            lineVars[QStringLiteral("admin")] = isAdmin ? QStringLiteral("admin") : QString();
            lineVars[QStringLiteral("UploadIndex")] = QString::number(upIndex++);

            upLines += WebTemplateEngine::substitute(lineTmpl, lineVars);
            totalUpTransferred += transferred;
        });
        totalUpSpeed = m_uploadQueue->datarate();
    }

    QHash<QString, QString> transferVars;
    transferVars[QStringLiteral("DownloadFilesList")] = downLines;
    transferVars[QStringLiteral("UploadFilesList")] = upLines;
    transferVars[QStringLiteral("DownloadCount")] = m_downloadQueue
        ? QString::number(m_downloadQueue->fileCount()) : QStringLiteral("0");
    transferVars[QStringLiteral("TotalUpTransferred")] = QString::number(totalUpTransferred);
    transferVars[QStringLiteral("TotalUpSpeed")] = QString::number(totalUpSpeed);

    const QString downHeader = WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("TRANSFER_DOWN_HEADER")), transferVars);
    const QString downFooter = m_templateEngine->section(QStringLiteral("TRANSFER_DOWN_FOOTER"));
    const QString upHeader = WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("TRANSFER_UP_HEADER")), transferVars);
    const QString upFooter = m_templateEngine->section(QStringLiteral("TRANSFER_UP_FOOTER"));

    const QString transferList = WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("TRANSFER_LIST")), transferVars);

    return downHeader + downLines + downFooter + upHeader + upLines + upFooter;
}

QString WebServer::buildServerListPage(bool isAdmin)
{
    QHash<QString, QString> vars;
    QString serverLines;

    if (m_serverList) {
        const QString lineTmpl = m_templateEngine->section(QStringLiteral("SERVER_LINE"));
        for (const auto& srv : m_serverList->servers()) {
            QHash<QString, QString> lineVars;
            lineVars[QStringLiteral("ServerName")] = srv->name();
            lineVars[QStringLiteral("ServerAddr")] = srv->address();
            lineVars[QStringLiteral("ServerPort")] = QString::number(srv->port());
            lineVars[QStringLiteral("ServerDescription")] = srv->description();
            lineVars[QStringLiteral("ServerPing")] = QString::number(srv->ping());
            lineVars[QStringLiteral("ServerUsers")] = QString::number(srv->users());
            lineVars[QStringLiteral("ServerFiles")] = QString::number(srv->files());

            bool isConnected = m_serverConnect && m_serverConnect->currentServer() == srv.get();
            lineVars[QStringLiteral("ServerStatus")] = isConnected
                ? QStringLiteral("connected") : QStringLiteral("disconnected");

            serverLines += WebTemplateEngine::substitute(lineTmpl, lineVars);
        }
    }

    vars[QStringLiteral("ServerList")] = serverLines;
    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("SERVER_LIST")), vars);
}

QString WebServer::buildSearchPage(bool isAdmin)
{
    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("SEARCH")), {});
}

QString WebServer::buildSharedFilesPage(bool isAdmin)
{
    QHash<QString, QString> vars;
    QString sharedLines;

    if (m_sharedFiles) {
        const QString lineTmpl = m_templateEngine->section(QStringLiteral("SHARED_LINE"));
        m_sharedFiles->forEachFile([&](KnownFile* file) {
            QHash<QString, QString> lineVars;
            lineVars[QStringLiteral("SharedFileName")] = file->fileName();
            lineVars[QStringLiteral("SharedFileSize")] = QString::number(file->fileSize());
            lineVars[QStringLiteral("SharedFileHash")] = md4str(file->fileHash());
            lineVars[QStringLiteral("SharedRequests")] = QString::number(file->statistic.requests());
            lineVars[QStringLiteral("SharedAccepted")] = QString::number(file->statistic.accepts());
            lineVars[QStringLiteral("SharedTransferred")] = QString::number(file->statistic.transferred());
            lineVars[QStringLiteral("SharedPriority")] = QString::number(file->upPriority());
            sharedLines += WebTemplateEngine::substitute(lineTmpl, lineVars);
        });
    }

    vars[QStringLiteral("SharedFilesList")] = sharedLines;
    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("SHARED_LIST")), vars);
}

QString WebServer::buildStatisticsPage()
{
    QHash<QString, QString> vars;
    if (m_statistics) {
        vars[QStringLiteral("SpeedDown")] = QString::number(m_statistics->rateDown(), 'f', 1);
        vars[QStringLiteral("SpeedUp")] = QString::number(m_statistics->rateUp(), 'f', 1);
        vars[QStringLiteral("MaxSpeedDown")] = QString::number(m_statistics->maxDown(), 'f', 1);
        vars[QStringLiteral("MaxSpeedUp")] = QString::number(m_statistics->maxUp(), 'f', 1);
        vars[QStringLiteral("SessionReceived")] = QString::number(m_statistics->sessionReceivedBytes());
        vars[QStringLiteral("SessionSent")] = QString::number(m_statistics->sessionSentBytes());
        vars[QStringLiteral("Reconnects")] = QString::number(m_statistics->reconnects());
        vars[QStringLiteral("Uptime")] = QString::number(m_statistics->transferTime());
    }
    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("STATS")), vars);
}

QString WebServer::buildGraphsPage()
{
    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("GRAPHS")), {});
}

QString WebServer::buildPreferencesPage(bool isAdmin)
{
    QHash<QString, QString> vars;
    if (m_preferences) {
        vars[QStringLiteral("Nick")] = m_preferences->nick();
        vars[QStringLiteral("MaxUpload")] = QString::number(m_preferences->maxUpload());
        vars[QStringLiteral("MaxDownload")] = QString::number(m_preferences->maxDownload());
        vars[QStringLiteral("Port")] = QString::number(m_preferences->port());
        vars[QStringLiteral("UDPPort")] = QString::number(m_preferences->udpPort());
    }
    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("PREFERENCES")), vars);
}

QString WebServer::buildServerInfoPage()
{
    QHash<QString, QString> vars;

    if (m_serverConnect && m_serverConnect->isConnected()) {
        Server* srv = m_serverConnect->currentServer();
        QString info;
        if (srv) {
            info += QStringLiteral("Connected to: %1 (%2:%3)\n")
                .arg(srv->name(), srv->address()).arg(srv->port());
            info += QStringLiteral("Client ID: %1 (%2)\n")
                .arg(m_serverConnect->clientID())
                .arg(m_serverConnect->isLowID() ? QStringLiteral("LowID") : QStringLiteral("HighID"));
            info += QStringLiteral("Users: %1 | Files: %2\n").arg(srv->users()).arg(srv->files());
            if (!srv->description().isEmpty())
                info += QStringLiteral("Description: %1\n").arg(srv->description());
            if (srv->ping() > 0)
                info += QStringLiteral("Ping: %1 ms\n").arg(srv->ping());
        }
        vars[QStringLiteral("ServerInfo")] = info;
    } else {
        vars[QStringLiteral("ServerInfo")] = m_serverConnect && m_serverConnect->isConnecting()
            ? QStringLiteral("Connecting...")
            : QStringLiteral("Not connected to any server");
    }

    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("SERVERINFO")), vars);
}

QString WebServer::buildLogPage()
{
    QHash<QString, QString> vars;
    vars[QStringLiteral("Log")] = m_logProvider ? m_logProvider() : QString();
    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("LOG")), vars);
}

QString WebServer::buildDebugLogPage()
{
    QHash<QString, QString> vars;
    vars[QStringLiteral("DebugLog")] = m_logProvider ? m_logProvider() : QString();
    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("DEBUGLOG")), vars);
}

QString WebServer::buildKadPage()
{
    QHash<QString, QString> vars;
    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("KADDLG")), vars);
}

QString WebServer::buildMyInfoPage()
{
    QHash<QString, QString> vars;
    if (m_preferences) {
        vars[QStringLiteral("Nick")] = m_preferences->nick();
    }
    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("MYINFO")), vars);
}

// ---------------------------------------------------------------------------
// Gzip compression
// ---------------------------------------------------------------------------

QByteArray WebServer::gzipCompress(const QByteArray& data)
{
    // Use qCompress which produces zlib format; strip the 4-byte length prefix
    // and 2-byte zlib header to get raw deflate, then wrap in gzip
    QByteArray compressed = qCompress(data, 6);
    if (compressed.size() < 10)
        return {};

    // qCompress format: 4 bytes BE uncompressed length + zlib stream
    // zlib stream: 2-byte header + deflate data + 4-byte adler32
    // gzip format: 10-byte header + deflate data + 4-byte CRC32 + 4-byte size

    // Extract raw deflate data (skip 4-byte length + 2-byte zlib header, remove 4-byte adler32)
    constexpr qsizetype zlibStart = 4;  // skip qCompress length prefix
    constexpr qsizetype zlibHeaderSize = 2;
    constexpr qsizetype adlerSize = 4;
    constexpr qsizetype deflateStart = zlibStart + zlibHeaderSize;
    const qsizetype deflateSize = compressed.size() - deflateStart - adlerSize;
    if (deflateSize <= 0)
        return {};

    // Build gzip output
    QByteArray gzip;
    gzip.reserve(10 + deflateSize + 8);

    // Gzip header
    gzip.append('\x1f');
    gzip.append('\x8b');
    gzip.append('\x08');  // deflate method
    gzip.append('\x00');  // flags
    gzip.append(4, '\x00');  // timestamp
    gzip.append('\x00');  // extra flags
    gzip.append('\xff');  // OS = unknown

    // Deflate data
    gzip.append(compressed.data() + deflateStart, deflateSize);

    // CRC32 + original size (little-endian)
    quint32 crc = 0;
    // Compute CRC32
    crc = ~crc;
    for (int i = 0; i < data.size(); ++i) {
        crc ^= static_cast<quint8>(data[i]);
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (0xEDB88320U & (-(crc & 1)));
    }
    crc = ~crc;

    auto appendLE32 = [&gzip](quint32 val) {
        gzip.append(static_cast<char>(val & 0xFF));
        gzip.append(static_cast<char>((val >> 8) & 0xFF));
        gzip.append(static_cast<char>((val >> 16) & 0xFF));
        gzip.append(static_cast<char>((val >> 24) & 0xFF));
    };

    appendLE32(crc);
    appendLE32(static_cast<quint32>(data.size()));

    return gzip;
}

} // namespace eMule
