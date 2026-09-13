#include "pch.h"
/// @file WebServer.cpp
/// @brief JSON REST API + template web server — implementation.

#include "webserver/WebServer.h"
#include "webserver/JsonSerializers.h"
#include "webserver/WebSessionManager.h"
#include "webserver/WebTemplateEngine.h"

#include "app/AppConfig.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "friends/Friend.h"
#include "friends/FriendList.h"
#include "media/ContainerSniffer.h"
#include "prefs/Preferences.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "search/SearchParams.h"
#include "server/Server.h"
#include "server/ServerConnect.h"
#include "server/ServerList.h"
#include "stats/Statistics.h"
#include "stats/StatsHistory.h"
#include "transfer/DownloadQueue.h"
#include "transfer/UploadQueue.h"
#include "kademlia/Kademlia.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/StringUtils.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponder>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QPromise>
#include <QRegularExpression>
#include <QSet>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QTcpServer>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

#include <algorithm>
#include <chrono>

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

/// Ceiling on one preview response body.
///
/// The body is built in memory, and a player opens a stream with
/// `Range: bytes=0-` — "send me the lot". Uncapped, that is an allocation the
/// size of the file, which for a Usenet release is tens of gigabytes and for a
/// large ED2K download is several. Answering with less than was asked for is
/// ordinary HTTP; the player re-requests from where the Content-Range left off.
///
/// 4 MiB is roughly a player's own read-ahead, so it does not add round trips
/// that matter.
constexpr qint64 kPreviewChunkBytes = 4 * 1024 * 1024;

/// How long a Usenet preview request waits for the bytes it asked for before
/// giving up. Playback catching up with the write head is normal and recovers
/// in a second or two; a seek far past it never will, and this is what turns
/// that into a prompt failure instead of a hung player.
constexpr int kStreamWaitMs = 15000;

/// Poll period while waiting. Matched to UsenetQueue's own tick, so a wait never
/// costs more wake-ups than the scheduler it is waiting on.
constexpr int kStreamPollMs = 250;

/// Parse a single-range `bytes=N-[M]` header.
///
/// Deliberately narrow: multiple ranges and suffix ranges (`bytes=-500`) are
/// refused rather than half-supported, because a media player never sends
/// either and a partly-correct multipart response is worse than a 416.
/// @p end is -1 when the header left the last byte open.
[[nodiscard]] bool parseRange(const QByteArray& header, qint64& start, qint64& end)
{
    static const QRegularExpression rx(QStringLiteral("^bytes=(\\d+)-(\\d*)$"));
    const auto m = rx.match(QString::fromLatin1(header));
    if (!m.hasMatch())
        return false;
    start = m.captured(1).toLongLong();
    end = m.captured(2).isEmpty() ? -1 : m.captured(2).toLongLong();
    return true;
}

/// First byte a Range header asks for, or 0 when there is no usable header.
/// Used to decide whether a Usenet file has downloaded far enough to answer at
/// all — a question that has to be settled before opening the file.
[[nodiscard]] qint64 parseRangeStart(const QByteArray& header)
{
    qint64 start = 0;
    qint64 end = -1;
    if (header.isEmpty() || !parseRange(header, start, end))
        return 0;
    return start;
}

[[nodiscard]] QHttpServerResponse rangeNotSatisfiable(qint64 fileSize)
{
    QHttpServerResponse err(QByteArrayLiteral("text/plain"),
        QByteArrayLiteral("Range Not Satisfiable"),
        static_cast<QHttpServerResponse::StatusCode>(416));
    auto h = err.headers();
    h.append(QByteArrayLiteral("Content-Range"),
             QStringLiteral("bytes */%1").arg(qMax(qint64(0), fileSize)));
    err.setHeaders(std::move(h));
    return err;
}

/// A read-only window [start, start+length) onto a file.
///
/// What lets the incoming stream route answer a Range without assembling the
/// body in memory the way serveRange has to. QHttpServerResponder writes a
/// device to its end and takes Content-Length off size(), so the window has to
/// live inside the device: a bare QFile seeked to the offset would over-report
/// both.
class RangeFileDevice : public QIODevice     // no Q_OBJECT: adds no signals
{
public:
    RangeFileDevice(const QString& path, qint64 start, qint64 length, QObject* parent)
        : QIODevice(parent), m_file(path), m_start(start), m_length(length)
    {
    }

    bool open(OpenMode mode) override
    {
        if (!m_file.open(QIODevice::ReadOnly) || !m_file.seek(m_start)) {
            m_file.close();
            return false;
        }
        return QIODevice::open(mode);
    }

    void close() override
    {
        QIODevice::close();
        m_file.close();
    }

    /// The window, not the file. Everything else follows from this: QIODevice
    /// derives bytesAvailable() and atEnd() from it, and the responder sends it
    /// as Content-Length.
    qint64 size() const override { return m_length; }

    bool isSequential() const override { return false; }

    bool seek(qint64 pos) override
    {
        if (pos < 0 || pos > m_length || !m_file.seek(m_start + pos))
            return false;
        return QIODevice::seek(pos);
    }

protected:
    qint64 readData(char* data, qint64 maxSize) override
    {
        // pos() is where this read starts -- QIODevice advances it afterwards.
        const qint64 left = m_length - pos();
        if (left <= 0)
            return 0;
        return m_file.read(data, qMin(maxSize, left));
    }

    qint64 writeData(const char*, qint64) override { return -1; }

private:
    QFile  m_file;
    qint64 m_start;
    qint64 m_length;
};

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
    , m_streamToken(QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')))
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

    // A custom template may ship its own assets flat beside the .tmpl; the URL space
    // is flat too, so they resolve by bare name. Custom path only -- the default
    // template lives in configDir, which holds preferences and .dat files and must
    // never become an asset root. Canonical compare, so Config/../Config is caught.
    m_webAssetOverrideDir.clear();
    if (!m_config.templatePath.isEmpty()) {
        const QString dir = QFileInfo(m_config.templatePath).canonicalPath();
        if (!dir.isEmpty() && dir != QFileInfo(configDir).canonicalFilePath())
            m_webAssetOverrideDir = dir;
    }

    // Keyed on the two roots just set, and templatePath is user-editable -- a theme
    // switch comes back through restartWebServer() -> start(), so the memo cannot be
    // allowed to survive. Reset here and not in stop(): stop() early-returns when
    // m_server is null, and ratingSpriteCss() is reachable on a stream token even
    // with the web UI off.
    m_ratingSpriteCss.reset();

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
                    if (body.size() > 256 && resp.statusCode() == QHttpServerResponse::StatusCode::Ok) {
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
void WebServer::setStatsHistory(StatsHistory* history) { m_statsHistory = history; }
void WebServer::setPreferences(Preferences* prefs)    { m_preferences = prefs; }

// ---------------------------------------------------------------------------
// Route registration
// ---------------------------------------------------------------------------

void WebServer::registerRoutes()
{
    // OPTIONS catch-all for CORS preflight — always registered.
    m_server->route(QStringLiteral("/<arg>"), QHttpServerRequest::Method::Options,
        [](const QUrl&) {
            return QHttpServerResponse(QHttpServerResponse::StatusCode::NoContent);
        });

    // --- Template web interface routes (only if the web UI is enabled) ---
    // Independent of the REST API: the UI is fully server-rendered and does not
    // call /api/v1/*. Gated so "web server disabled" actually serves no UI.
    if (m_config.webUiEnabled) {
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
            [this](const QString& file, const QHttpServerRequest& /*req*/) {
                if (file.startsWith(QStringLiteral("api/")))
                    return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
                // Only serve known static file extensions
                if (file.endsWith(QStringLiteral(".gif")) || file.endsWith(QStringLiteral(".jpg")) ||
                    file.endsWith(QStringLiteral(".png")) || file.endsWith(QStringLiteral(".ico")) ||
                    file.endsWith(QStringLiteral(".css")) || file.endsWith(QStringLiteral(".js"))) {
                    return handleStaticFile(file);
                }
                return QHttpServerResponse(QHttpServerResponse::StatusCode::NotFound);
            });
    }

    // --- Preview streaming (always available, uses stream token auth) ---
    // Neither web UI nor REST API — the GUI's preview feature relies on it, so it
    // is served regardless of either flag.
    m_server->route(QStringLiteral("/api/v1/downloads/<arg>/preview"), QHttpServerRequest::Method::Get,
        [this](const QString& hash, const QHttpServerRequest& req) {
            return handlePreviewStream(hash, req);
        });

    // --- Incoming folder browsing (always available, stream-token auth) ---
    // What the GUI opens instead of the file manager when its core runs on
    // another machine. Same gate and same unconditional registration as preview:
    // a remote GUI is exactly the case where both web surfaces may be off.
    m_server->route(QStringLiteral("/api/v1/incoming"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) {
            return handleIncomingListing(req);
        });

    // The two routes in the daemon that answer through the responder rather than
    // by returning a response, for the same reason: every QHttpServerResponse
    // holds its body in memory, and these two hand over a whole file.
    m_server->route(QStringLiteral("/api/v1/incoming/stream"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req, QHttpServerResponder& responder) {
            handleIncomingStream(req, responder);
        });

    m_server->route(QStringLiteral("/api/v1/incoming/download"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req, QHttpServerResponder& responder) {
            handleIncomingDownload(req, responder);
        });

    // Usenet preview. A separate route, not a widening of the one above: that
    // one keys on a 32-hex ED2K hash, and a Usenet file is named by an item UUID
    // plus its index within the NZB. It also has to be able to answer "not yet"
    // by waiting, which is why it returns a future.
    m_server->route(QStringLiteral("/api/v1/usenet/<arg>/<arg>/preview"),
        QHttpServerRequest::Method::Get,
        [this](const QString& itemId, const QString& fileIndex, const QHttpServerRequest& req) {
            return handleUsenetPreviewStream(itemId, fileIndex, req);
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
// Preview streaming handler
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::handlePreviewStream(const QString& hash, const QHttpServerRequest& req)
{
    const bool dbg = m_preferences && m_preferences->logWebServer();

    if (dbg) {
        logDebug(QStringLiteral("Preview: GET %1").arg(req.url().toString()));
        const auto reqHeaders = req.headers();
        for (qsizetype i = 0; i < reqHeaders.size(); ++i) {
            logDebug(QStringLiteral("Preview request header: %1: %2")
                .arg(QString::fromLatin1(reqHeaders.nameAt(i)),
                     QString::fromLatin1(reqHeaders.valueAt(i))));
        }
    }

    // Authenticate via stream token query parameter
    const QUrlQuery query(req.query());
    const QString token = query.queryItemValue(QStringLiteral("token"));
    if (token.isEmpty() || token != m_streamToken) {
        logWarning(QStringLiteral("Preview: 401 — invalid or missing stream token"));
        return jsonError(401, QStringLiteral("Invalid or missing stream token"));
    }

    if (!m_downloadQueue) {
        logWarning(QStringLiteral("Preview: 500 — download queue not available"));
        return jsonError(500, QStringLiteral("Download queue not available"));
    }

    std::array<uint8, 16> hashBytes{};
    if (hash.size() != 32 || decodeBase16(hash, hashBytes.data(), 16) != 16) {
        logWarning(QStringLiteral("Preview: 400 — invalid hash format: %1").arg(hash));
        return jsonError(400, QStringLiteral("Invalid hash format"));
    }

    // The hash may name an active download or any other shared file. A finished
    // download leaves the queue — KnownFileList owns it from then on — and a file that
    // came from a shared directory was never in it, so asking the queue alone would
    // answer 404 for nearly everything the Shared Files window can offer to open.
    QString path;
    QString fileName;
    if (const auto* download = m_downloadQueue->fileByID(hashBytes.data())) {
        fileName = download->fileName();

        // For an in-progress download fullName() is the .part.met metadata path — strip
        // .met to get the .part data file. A completed download has been moved to the
        // incoming dir and fullName() still points at the now-deleted .part, so use the
        // final path recorded in filePath() instead. This lets the endpoint back both
        // live preview and completed-file open (remote core).
        if (download->status() == PartFileStatus::Complete && !download->filePath().isEmpty()) {
            path = download->filePath();
        } else {
            path = download->fullName();
            if (path.endsWith(QStringLiteral(".met")))
                path.chop(4);
        }
    } else if (m_sharedFiles) {
        if (const auto* shared = m_sharedFiles->getFileByID(hashBytes.data())) {
            fileName = shared->fileName();
            path = shared->filePath();
        }
    }

    if (path.isEmpty()) {
        logWarning(QStringLiteral("Preview: 404 — no download or shared file for hash %1").arg(hash));
        return jsonError(404, QStringLiteral("File not found"));
    }
    if (!QFileInfo::exists(path)) {
        logWarning(QStringLiteral("Preview: 404 — file not available: %1").arg(path));
        return jsonError(404, QStringLiteral("File not available"));
    }

    // The file is resolved fresh on every request; serveRange does the rest,
    // and is also where the response-size cap lives.
    return serveRange({{path, 0, 0, 0}}, fileName, /*totalSize*/ 0, /*availableEnd*/ 0,
                      req.headers().combinedValue(QByteArrayLiteral("Range")));
}

QFuture<QHttpServerResponse> WebServer::handleUsenetPreviewStream(
    const QString& itemId, const QString& fileIndexText, const QHttpServerRequest& req)
{
    const auto ready = [](QHttpServerResponse&& r) {
        QPromise<QHttpServerResponse> p;
        QFuture<QHttpServerResponse> f = p.future();
        p.start();
        p.addResult(std::move(r));
        p.finish();
        return f;
    };

    // Same gate as the ED2K route: a per-process random token, not the REST API
    // key. Preview is served even with both web surfaces switched off, so it
    // cannot lean on either one's authentication.
    const QUrlQuery query(req.query());
    const QString token = query.queryItemValue(QStringLiteral("token"));
    if (token.isEmpty() || token != m_streamToken) {
        logWarning(QStringLiteral("Usenet preview: 401 — invalid or missing stream token"));
        return ready(jsonError(401, QStringLiteral("Invalid or missing stream token")));
    }

    if (!m_usenetStreamResolver)
        return ready(jsonError(503, QStringLiteral("Usenet engine unavailable")));

    bool indexOk = false;
    const int fileIndex = fileIndexText.toInt(&indexOk);
    if (!indexOk || fileIndex < 0)
        return ready(jsonError(400, QStringLiteral("Invalid file index")));

    // Which file inside the archive set. Absent means "the first playable one",
    // which is what every URL predating the chooser carries. A malformed value
    // is rejected here rather than passed on: a bad ordinal is a bad request,
    // not a lookup that happens to miss.
    int entryOrdinal = -1;
    if (query.hasQueryItem(QStringLiteral("entry"))) {
        bool entryOk = false;
        entryOrdinal = query.queryItemValue(QStringLiteral("entry")).toInt(&entryOk);
        if (!entryOk || entryOrdinal < 0)
            return ready(jsonError(400, QStringLiteral("Invalid archive entry")));
    }

    // The request object does not outlive this call, so everything the deferred
    // path needs is copied out now.
    const QByteArray rangeHeader = req.headers().combinedValue(QByteArrayLiteral("Range"));
    const qint64 wantStart = parseRangeStart(rangeHeader);

    // What to ask the queue to fetch. The response is capped at one chunk
    // anyway, so asking for more than that would promote articles the client is
    // not going to read yet.
    constexpr qint64 kWantLength = kPreviewChunkBytes;

    UsenetStreamRequest ask;
    ask.itemId = itemId;
    ask.fileIndex = fileIndex;
    ask.wantOffset = wantStart;
    ask.wantLength = kWantLength;
    ask.entryOrdinal = entryOrdinal;

    // Resolving is not a pure lookup — it also tells the queue somebody is
    // watching this item, which is what puts its articles at the front of the
    // schedule. That is why the poll below calls it again rather than caching:
    // the boost has to be refreshed for as long as a player is really reading.
    const UsenetStreamSource first = m_usenetStreamResolver(ask);
    if (!first.found) {
        logWarning(QStringLiteral("Usenet preview: 404 — no file %1 of item %2")
                       .arg(fileIndex).arg(itemId));
        return ready(jsonError(404, QStringLiteral("File not found")));
    }

    // A compressed, solid or encrypted archive will never be mappable. Waiting
    // out the poll would end in a 416 that reads like "not yet" — 406 with the
    // reason says "not ever", which is what the user needs to know.
    if (!first.notSeekableReason.isEmpty()) {
        logWarning(QStringLiteral("Usenet preview: 406 — %1").arg(first.notSeekableReason));
        return ready(jsonError(406, first.notSeekableReason));
    }

    if (first.availableEnd > wantStart) {
        return ready(serveRange(first.pieces, first.fileName, first.totalSize,
                                first.availableEnd, rangeHeader));
    }

    // Playback has caught up with the write head. Hold the request rather than
    // answer it: a zero-byte 206 reads as end-of-stream and every player stops.
    //
    // This runs on the daemon's event loop — the same thread as UsenetQueue — so
    // sleeping here would stall the very downloads being waited for. A deferred
    // QFuture is the way out: the handler returns at once and the response goes
    // when the promise is fulfilled.
    auto promise = std::make_shared<QPromise<QHttpServerResponse>>();
    promise->start();
    QFuture<QHttpServerResponse> future = promise->future();

    auto* timer = new QTimer(this);
    timer->setInterval(kStreamPollMs);

    QDeadlineTimer deadline(kStreamWaitMs);

    connect(timer, &QTimer::timeout, this,
            [this, timer, promise, ask, rangeHeader, wantStart, deadline] {
        const UsenetStreamSource now =
            m_usenetStreamResolver ? m_usenetStreamResolver(ask) : UsenetStreamSource{};

        // Four ways out: the bytes turned up, the release turned out to be
        // unmappable, the item went away, or the wait ran out. Only the first is
        // a success, and forgetting the middle two means a removed or solid
        // release holds a player for the whole timeout.
        const bool arrived = now.found && now.availableEnd > wantStart;
        const bool gone = !now.found;
        const bool refused = now.found && !now.notSeekableReason.isEmpty();
        if (!arrived && !gone && !refused && !deadline.hasExpired())
            return;

        timer->stop();
        timer->deleteLater();

        QHttpServerResponse resp = [&]() -> QHttpServerResponse {
            if (!now.found)
                return jsonError(404, QStringLiteral("File not found"));
            if (refused) {
                logWarning(QStringLiteral("Usenet preview: 406 — %1").arg(now.notSeekableReason));
                return jsonError(406, now.notSeekableReason);
            }
            if (!arrived) {
                // The articles covering this offset were asked for and have not
                // come back inside the window — a slow provider, or a hole no
                // server holds. A prompt 416 beats a player hanging on a request
                // that might take the rest of the download.
                logWarning(QStringLiteral("Usenet preview: 416 — timed out waiting for byte "
                                          "%1 of \"%2\" (have %3)")
                               .arg(wantStart).arg(now.fileName).arg(now.availableEnd));
                return rangeNotSatisfiable(now.totalSize > 0 ? now.totalSize : now.availableEnd);
            }
            return serveRange(now.pieces, now.fileName, now.totalSize, now.availableEnd,
                              rangeHeader);
        }();

        promise->addResult(std::move(resp));
        promise->finish();
    });

    timer->start();
    return future;
}

QHttpServerResponse WebServer::serveRange(const QList<UsenetStreamPiece>& piecesIn,
                                          const QString& fileName,
                                          qint64 totalSize, qint64 availableEnd,
                                          const QByteArray& rangeHeader)
{
    const bool dbg = m_preferences && m_preferences->logWebServer();

    if (piecesIn.isEmpty()) {
        logWarning(QStringLiteral("Preview: 404 — nothing to serve for %1").arg(fileName));
        return jsonError(404, QStringLiteral("File not available"));
    }

    // A lone piece with no length means "whatever is on disk" — the ED2K route,
    // whose .part file is already preallocated to its final length.
    QList<UsenetStreamPiece> pieces = piecesIn;
    const bool lengthFromDisk = pieces.size() == 1 && pieces.first().length <= 0;
    if (lengthFromDisk) {
        const qint64 onDisk = QFileInfo(pieces.first().path).size();
        pieces[0].length = qMax<qint64>(0, onDisk - pieces.first().fileOffset);
    }

    // totalSize is what the *finished* file will be, and it is what Content-Range
    // must report: a player derives its duration and seek bar from it, and a
    // total that grows underneath makes both jump.
    qint64 fileSize = totalSize;
    if (fileSize <= 0) {
        fileSize = 0;
        for (const UsenetStreamPiece& p : std::as_const(pieces))
            fileSize += p.length;
    }
    if (fileSize <= 0) {
        logWarning(QStringLiteral("Preview: 404 — nothing readable yet: %1")
                       .arg(pieces.first().path));
        return jsonError(404, QStringLiteral("File not available"));
    }

    // Where the *content* stops. Zero means "the caller does not track holes",
    // which is the ED2K case: its .part file is preallocated and its gaps have
    // always read back as zeros.
    const qint64 contentEnd = availableEnd > 0 ? qMin(availableEnd, fileSize) : fileSize;

    // Derive MIME type from the original filename
    QMimeDatabase mimeDb;
    const QMimeType mime = mimeDb.mimeTypeForFile(fileName, QMimeDatabase::MatchExtension);
    const QByteArray mimeType = mime.name().toUtf8();

    if (dbg)
        logDebug(QStringLiteral("Preview: file=%1  pieces=%2  size=%3  available=%4  mime=%5")
            .arg(fileName).arg(pieces.size()).arg(fileSize).arg(contentEnd)
            .arg(QString::fromUtf8(mimeType)));

    // --- Range, for seeking support (required by VLC et al.) ---
    qint64 rangeStart = 0;
    qint64 rangeEnd   = fileSize - 1;
    bool   hasRange   = false;

    if (!rangeHeader.isEmpty()) {
        if (dbg)
            logDebug(QStringLiteral("Preview: Range header: %1").arg(QString::fromLatin1(rangeHeader)));

        qint64 parsedEnd = -1;
        if (!parseRange(rangeHeader, rangeStart, parsedEnd)) {
            logWarning(QStringLiteral("Preview: 416 — malformed Range header: %1")
                .arg(QString::fromLatin1(rangeHeader)));
            return rangeNotSatisfiable(fileSize);
        }
        rangeEnd = parsedEnd < 0 ? fileSize - 1 : parsedEnd;
        // RFC 7233 §2.1: clamp last-byte-pos to file size
        if (rangeEnd >= fileSize)
            rangeEnd = fileSize - 1;
        if (rangeStart >= fileSize || rangeStart > rangeEnd) {
            logWarning(QStringLiteral("Preview: 416 — out of bounds: start=%1 end=%2 fileSize=%3")
                .arg(rangeStart).arg(rangeEnd).arg(fileSize));
            return rangeNotSatisfiable(fileSize);
        }
        hasRange = true;
    } else {
        if (dbg)
            logDebug(QStringLiteral("Preview: no Range header — serving from byte 0"));
    }

    // Nothing at that offset yet. Whoever called has already waited as long as
    // it was willing to; a zero-byte 206 reads as end-of-stream and stops
    // playback, so say plainly that the range cannot be satisfied instead.
    if (rangeStart >= contentEnd) {
        logWarning(QStringLiteral("Preview: 416 — byte %1 is not downloaded yet (have %2)")
            .arg(rangeStart).arg(contentEnd));
        return rangeNotSatisfiable(fileSize);
    }

    // Two ceilings, both load-bearing:
    //   - contentEnd, so the preallocated tail of a half-downloaded file is
    //     never handed over as content;
    //   - kPreviewChunkBytes, because the body is assembled in memory. Without
    //     it `Range: bytes=0-` on a 40 GB release asks the daemon to allocate
    //     40 GB — and that is the request a player opens with.
    // Answering with fewer bytes than were asked for is ordinary HTTP:
    // Content-Range states what was actually sent and the client comes back for
    // the next window.
    rangeEnd = qMin(rangeEnd, contentEnd - 1);
    rangeEnd = qMin(rangeEnd, rangeStart + kPreviewChunkBytes - 1);

    const qint64 contentLength = rangeEnd - rangeStart + 1;

    // Walk the pieces the window touches. Usually one — a 4 MiB window inside a
    // volume of tens of megabytes — but a window landing on a volume boundary
    // spans two files, and stitching them here is what makes a RAR set look like
    // one continuous file to the player.
    QByteArray data;
    data.reserve(contentLength);
    for (const UsenetStreamPiece& p : std::as_const(pieces)) {
        const qint64 pos = rangeStart + data.size();
        if (pos > rangeEnd)
            break;
        if (p.virtualOffset + p.length <= pos)
            continue;
        if (p.virtualOffset > pos)
            break;                  // a gap in the map; serve what we have

        QFile file(p.path);
        if (!file.open(QIODevice::ReadOnly)) {
            logWarning(QStringLiteral("Preview: cannot open %1").arg(p.path));
            break;
        }
        const qint64 into = pos - p.virtualOffset;
        if (!file.seek(p.fileOffset + into))
            break;

        const qint64 want = qMin(rangeEnd - pos + 1, p.length - into);
        const QByteArray chunk = file.read(want);
        if (chunk.isEmpty())
            break;
        data.append(chunk);
    }

    if (data.isEmpty()) {
        logWarning(QStringLiteral("Preview: 416 — byte %1 could not be read").arg(rangeStart));
        return rangeNotSatisfiable(fileSize);
    }
    rangeEnd = rangeStart + data.size() - 1;

    if (dbg)
        logDebug(QStringLiteral("Preview: serving %1 bytes [%2-%3/%4]  hasRange=%5  pieces=%6")
            .arg(data.size()).arg(rangeStart).arg(rangeEnd).arg(fileSize)
            .arg(hasRange).arg(pieces.size()));

    // Pieces of a stated length, and no total: whoever built them knows how far
    // the file goes but not how far it will go — an extraction still writing it
    // out. Its current length is not its length, so say `/*` rather than invent
    // a number the player will latch onto as the duration.
    //
    // A lone piece with no length is the opposite case: that length *came* from
    // the file on disk, which the ED2K route preallocates to its final size, so
    // there the size on disk is the answer.
    const bool totalUnknown = totalSize <= 0 && !lengthFromDisk;

    // A capped answer to an uncapped request is still a *partial* one, so it has
    // to be 206 with a Content-Range even when the client sent no Range header.
    // Replying 200 would claim the truncated body is the whole file.
    const bool partial = hasRange || data.size() < fileSize || totalUnknown;

    const auto statusCode = partial
        ? static_cast<QHttpServerResponse::StatusCode>(206)
        : QHttpServerResponse::StatusCode::Ok;

    QHttpServerResponse resp(mimeType, data, statusCode);
    auto headers = resp.headers();
    headers.append(QByteArrayLiteral("Accept-Ranges"), QStringLiteral("bytes"));
    if (partial) {
        headers.append(QByteArrayLiteral("Content-Range"),
                       QStringLiteral("bytes %1-%2/%3")
                           .arg(rangeStart).arg(rangeEnd)
                           .arg(totalUnknown ? QStringLiteral("*") : QString::number(fileSize)));
    }
    headers.append(QHttpHeaders::WellKnownHeader::ContentDisposition,
                   QStringLiteral("inline; filename=\"%1\"").arg(fileName));
    resp.setHeaders(std::move(headers));

    if (dbg) {
        const auto respHeaders = resp.headers();
        for (qsizetype i = 0; i < respHeaders.size(); ++i) {
            logDebug(QStringLiteral("Preview response header: %1: %2")
                .arg(QString::fromLatin1(respHeaders.nameAt(i)),
                     QString::fromLatin1(respHeaders.valueAt(i))));
        }
        logDebug(QStringLiteral("Preview: responding %1  body=%2 bytes")
            .arg(static_cast<int>(statusCode)).arg(data.size()));
    }

    return resp;
}

// ---------------------------------------------------------------------------
// Incoming folder browsing
// ---------------------------------------------------------------------------

namespace {

/// Containers a browser plays by itself.
///
/// It does not decide *which* link a file gets -- every video and audio file
/// gets the player page, because a link in a page that silently starts a
/// download instead is the worse surprise. It decides whether that page warns
/// and offers the raw URL for VLC, so what the user sees when the black box
/// stays black is an explanation rather than nothing.
bool isBrowserPlayable(const QString& fileName)
{
    static const QStringList kExt = {
        QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("webm"),
        QStringLiteral("ogv"), QStringLiteral("ogg"), QStringLiteral("mp3"),
        QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("flac"),
        QStringLiteral("wav"),
    };
    return kExt.contains(QFileInfo(fileName).suffix().toLower());
}

/// One link back into these routes, with every value percent-encoded.
QString incomingHref(const QString& path, const QString& token,
                     const QString& key, const QString& value)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("token"), token);
    if (!value.isEmpty())
        query.addQueryItem(key, value);

    const QString href = path + QLatin1Char('?') + query.toString(QUrl::FullyEncoded);
    return href.toHtmlEscaped();
}

/// The `filetype_*` sprite suffix for a name — MFC _GetWebImageNameForFileType
/// (srchybrid/WebServer.cpp:4157-4188). Note IMAGE maps to "picture", not
/// "image": that is what the shipped sprite is called.
QString webFileTypeToken(const QString& fileName)
{
    switch (getED2KFileTypeID(fileName)) {
    case ED2KFileType::Audio:           return QStringLiteral("audio");
    case ED2KFileType::Video:           return QStringLiteral("video");
    case ED2KFileType::Image:           return QStringLiteral("picture");
    case ED2KFileType::Program:         return QStringLiteral("program");
    case ED2KFileType::Document:        return QStringLiteral("document");
    case ED2KFileType::Archive:         return QStringLiteral("archive");
    case ED2KFileType::CDImage:         return QStringLiteral("cdimage");
    case ED2KFileType::EmuleCollection: return QStringLiteral("emulecollection");
    default:                            return QStringLiteral("other");
    }
}

/// The `is_*` sprite suffix. Three states, exactly MFC's iComment
/// (srchybrid/WebServer.cpp:1881-1884, 2184-2196): nothing, something to read,
/// or a bad rating — which is rating 1, "Invalid / Corrupt / Fake".
///
/// The blank is is_none, not is_halfnone: the "half" blank is 8px wide because
/// MFC pairs it with the 8px getflc icon in one cell, and using it alone would
/// shift the filename on every row that has no comment.
QString webCommentToken(const AbstractFile& f)
{
    if (f.hasBadRating())
        return QStringLiteral("halfcmtbad");
    if (f.hasComment() || f.hasRating())
        return QStringLiteral("halfcmtgood");
    return QStringLiteral("none");
}

/// The `rating_*` sprite suffix. MFC's web UI stops at the three-state comment
/// icon above; the GUI shows all six, and so does this.
QString webRatingToken(const AbstractFile& f, bool indicateRatings)
{
    // Same predicate the GUI uses, and the same preference governs it.
    if (!indicateRatings
        || !(f.hasComment() || f.hasRating() || f.isKadCommentSearchRunning())) {
        return QStringLiteral("none");
    }
    const uint32 rating = f.userRating(true);
    if (rating == 6)
        return QStringLiteral("search");
    return QString::number(rating <= 5 ? rating : 0);
}

/// What those two icons mean, for their title=. Same either/or the Qt lists make in
/// fileMarksTooltip(): the rating when there is one, otherwise only that there is
/// something to read. Empty when the row says nothing, so the span stays silent.
QString webRatingTitle(const AbstractFile& f)
{
    const uint32 rating = f.userRating(true);
    if (rating == 6)
        return QStringLiteral("Looking for comments on Kad");
    if (rating >= 1 && rating <= 5)
        return QStringLiteral("Rating: %1").arg(ratingLabel(static_cast<int>(rating)));
    return f.hasComment() ? QStringLiteral("Has comments") : QString{};
}

/// Shared head of both pages. Inline everything: with the web UI disabled there
/// is no stylesheet route to link to. @p extraCss is appended inside the same
/// <style>, which is how the rating sprite sheet gets in (see ratingSpriteCss()).
QString pageHead(const QString& title, const QString& extraCss = {})
{
    return QStringLiteral(
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>%1</title><style>"
        "body{font:14px -apple-system,Segoe UI,Roboto,sans-serif;margin:0;padding:24px;"
        "background:#f6f7f9;color:#1c1e21}"
        "h1{font-size:18px;margin:0 0 16px}"
        "a{color:#1268c3;text-decoration:none}a:hover{text-decoration:underline}"
        "table{border-collapse:collapse;width:100%;background:#fff;border:1px solid #dcdfe3;"
        "border-radius:6px;overflow:hidden}"
        "th,td{text-align:left;padding:8px 12px;border-bottom:1px solid #eceef0}"
        "th{background:#f0f2f5;font-weight:600;font-size:12px;text-transform:uppercase;"
        "letter-spacing:.04em;color:#606770}"
        "tr:last-child td{border-bottom:none}"
        "td.n{white-space:nowrap;color:#606770}"
        "td.a{white-space:nowrap}td.a a{margin-right:12px}"
        "video,audio{width:100%;max-width:960px;background:#000;border-radius:6px}"
        "p.empty{color:#606770}"
        "div.warn{max-width:960px;margin:16px 0;padding:12px 14px;background:#fff8e1;"
        "border:1px solid #f0d999;border-radius:6px}"
        "div.warn p{margin:0 0 8px}"
        "div.warn form{display:flex;gap:8px}"
        "div.warn input{flex:1;min-width:0;font:12px ui-monospace,Menlo,Consolas,monospace;"
        "padding:6px 8px;border:1px solid #dcdfe3;border-radius:4px;background:#fff;"
        "color:#1c1e21}"
        "div.warn button{padding:6px 12px;border:1px solid #dcdfe3;border-radius:4px;"
        "background:#f0f2f5;color:#1c1e21;cursor:pointer}"
        // The fallback mark, drawn in CSS: the static asset route only exists while
        // the web UI is enabled, and this page is reachable on a stream token with it
        // off. Used when the sprite sheet cannot be read — see ratingSpriteCss().
        "span.bad{display:inline-block;width:15px;height:15px;line-height:15px;"
        "text-align:center;border-radius:50%;background:#d93025;color:#fff;"
        "font-weight:700;font-size:11px;margin-right:6px;cursor:help}"
        "%2</style></head><body>").arg(title.toHtmlEscaped(), extraCss);
}

} // namespace

QHttpServerResponse WebServer::handleIncomingListing(const QHttpServerRequest& req)
{
    if (!hasStreamToken(req)) {
        logWarning(QStringLiteral("Incoming: 401 — invalid or missing stream token"));
        return jsonError(401, QStringLiteral("Invalid or missing stream token"));
    }

    const QUrlQuery query(req.query());
    const QString token = query.queryItemValue(QStringLiteral("token"));

    // ?play= is the same route on purpose: the player page is one <video> tag and
    // a back link, not a surface of its own.
    const QString play = query.queryItemValue(QStringLiteral("play"), QUrl::FullyDecoded);
    if (!play.isEmpty()) {
        const QString abs = resolveIncomingPath(play);
        if (abs.isEmpty() || !QFileInfo(abs).isFile()) {
            logWarning(QStringLiteral("Incoming: 404 — cannot play %1").arg(play));
            return jsonError(404, QStringLiteral("File not found"));
        }
        return QHttpServerResponse(QByteArrayLiteral("text/html; charset=utf-8"),
                                   renderIncomingPlayer(play, QFileInfo(abs).fileName(), token));
    }

    const QString rel = query.queryItemValue(QStringLiteral("path"), QUrl::FullyDecoded);
    const QString abs = resolveIncomingPath(rel);
    if (abs.isEmpty()) {
        logWarning(QStringLiteral("Incoming: 404 — no such folder: %1")
                       .arg(rel.isEmpty() ? QStringLiteral("<incoming>") : rel));
        return jsonError(404, QStringLiteral("Folder not found"));
    }
    if (!QFileInfo(abs).isDir()) {
        logWarning(QStringLiteral("Incoming: 400 — not a folder: %1").arg(rel));
        return jsonError(400, QStringLiteral("Not a folder"));
    }

    return QHttpServerResponse(QByteArrayLiteral("text/html; charset=utf-8"),
                               renderIncomingListing(abs, rel, token));
}

void WebServer::handleIncomingStream(const QHttpServerRequest& req,
                                    QHttpServerResponder& responder)
{
    if (!hasStreamToken(req)) {
        logWarning(QStringLiteral("Incoming: 401 — invalid or missing stream token"));
        responder.sendResponse(jsonError(401, QStringLiteral("Invalid or missing stream token")));
        return;
    }

    const QUrlQuery query(req.query());
    const QString rel = query.queryItemValue(QStringLiteral("file"), QUrl::FullyDecoded);
    const QString abs = resolveIncomingPath(rel);
    const QFileInfo info(abs);
    if (abs.isEmpty() || !info.isFile()) {
        logWarning(QStringLiteral("Incoming: 404 — cannot stream %1").arg(rel));
        responder.sendResponse(jsonError(404, QStringLiteral("File not found")));
        return;
    }

    const qint64 fileSize = info.size();
    if (fileSize <= 0) {
        logWarning(QStringLiteral("Incoming: 404 — nothing to stream in %1").arg(rel));
        responder.sendResponse(jsonError(404, QStringLiteral("File not available")));
        return;
    }

    // Deliberately not serveRange(). That one assembles its body in memory,
    // because a preview is stitched out of a release that is still arriving, and
    // its 4 MiB cap makes every answer a 206 — including the answer to a request
    // that carried no Range at all, which a browser then saves as a truncated
    // download. A finished file is one contiguous file on disk with no holes, so
    // it streams straight off the device and a range-less GET gets its 200.
    const QByteArray rangeHeader = req.headers().combinedValue(QByteArrayLiteral("Range"));

    qint64 start = 0;
    qint64 end   = fileSize - 1;
    const bool hasRange = !rangeHeader.isEmpty();

    if (hasRange) {
        qint64 parsedEnd = -1;
        if (!parseRange(rangeHeader, start, parsedEnd)) {
            logWarning(QStringLiteral("Incoming: 416 — malformed Range header: %1")
                           .arg(QString::fromLatin1(rangeHeader)));
            responder.sendResponse(rangeNotSatisfiable(fileSize));
            return;
        }
        end = parsedEnd < 0 ? fileSize - 1 : parsedEnd;
        // RFC 7233 §2.1: clamp last-byte-pos to file size
        if (end >= fileSize)
            end = fileSize - 1;
        if (start >= fileSize || start > end) {
            logWarning(QStringLiteral("Incoming: 416 — out of bounds: start=%1 end=%2 size=%3")
                           .arg(start).arg(end).arg(fileSize));
            responder.sendResponse(rangeNotSatisfiable(fileSize));
            return;
        }
    }

    // Parented rather than owned outright, same as the download route: the
    // responder documents that it takes the device, and a parent makes the other
    // reading harmless instead of a leak per request.
    auto* device = new RangeFileDevice(abs, start, end - start + 1, this);
    if (!device->open(QIODevice::ReadOnly)) {
        logWarning(QStringLiteral("Incoming: 500 — cannot open %1").arg(abs));
        delete device;
        responder.sendResponse(jsonError(500, QStringLiteral("Cannot open file")));
        return;
    }

    const QString name = info.fileName();
    QMimeDatabase mimeDb;

    // The extension decides the type, not the content: QMimeDatabase content
    // matching demotes every real .mp4 to video/quicktime, which some browsers
    // then refuse to play. The one exception is a file whose magic positively
    // contradicts its name -- an incoming folder is full of those, and sending
    // the type it really is turns a player stuck at 0:00 into one that plays.
    QString mimeName = mimeDb.mimeTypeForFile(name, QMimeDatabase::MatchExtension).name();
    if (const ContainerCheck real = checkFile(abs, name); real.isSuspect()) {
        // Only a container we positively identified may change the type. Knowing
        // the file is not what it claims is not the same as knowing what it is,
        // and guessing there would just trade one wrong type for another.
        logWarning(QStringLiteral("Incoming: %1 is named .%2 but contains %3")
                       .arg(name, info.suffix().toLower(),
                            real.actual.isEmpty() ? QStringLiteral("no recognised container")
                                                  : real.actual));
        if (!real.mimeType.isEmpty())
            mimeName = real.mimeType;
    }

    QHttpHeaders headers;
    headers.append(QHttpHeaders::WellKnownHeader::ContentType, mimeName);
    // Unlike the download route this one keeps the promise, so it may make it.
    headers.append(QByteArrayLiteral("Accept-Ranges"), QStringLiteral("bytes"));
    if (hasRange) {
        headers.append(QByteArrayLiteral("Content-Range"),
                       QStringLiteral("bytes %1-%2/%3").arg(start).arg(end).arg(fileSize));
    }
    // inline, not attachment: this route exists to be played. Quoted the same way
    // the download route quotes it — a name carrying a quote would otherwise end
    // the header value early.
    headers.append(QHttpHeaders::WellKnownHeader::ContentDisposition,
                   QStringLiteral("inline; filename=\"%1\"; filename*=UTF-8''%2")
                       .arg(QString(name).remove(QLatin1Char('"')).remove(QLatin1Char('\\')),
                            QString::fromLatin1(QUrl::toPercentEncoding(name))));

    responder.write(device, headers,
                    hasRange ? static_cast<QHttpServerResponse::StatusCode>(206)
                             : QHttpServerResponse::StatusCode::Ok);
}

void WebServer::handleIncomingDownload(const QHttpServerRequest& req,
                                       QHttpServerResponder& responder)
{
    if (!hasStreamToken(req)) {
        logWarning(QStringLiteral("Incoming: 401 — invalid or missing stream token"));
        responder.sendResponse(jsonError(401, QStringLiteral("Invalid or missing stream token")));
        return;
    }

    const QUrlQuery query(req.query());
    const QString rel = query.queryItemValue(QStringLiteral("file"), QUrl::FullyDecoded);
    const QString abs = resolveIncomingPath(rel);
    if (abs.isEmpty() || !QFileInfo(abs).isFile()) {
        logWarning(QStringLiteral("Incoming: 404 — cannot download %1").arg(rel));
        responder.sendResponse(jsonError(404, QStringLiteral("File not found")));
        return;
    }

    // Parented rather than owned outright: the responder documents that it takes
    // the device, and a parent makes the other reading harmless instead of a leak
    // per download.
    auto* file = new QFile(abs, this);
    if (!file->open(QIODevice::ReadOnly)) {
        logWarning(QStringLiteral("Incoming: 500 — cannot open %1").arg(abs));
        delete file;
        responder.sendResponse(jsonError(500, QStringLiteral("Cannot open file")));
        return;
    }

    const QString name = QFileInfo(abs).fileName();
    QMimeDatabase mimeDb;

    QHttpHeaders headers;
    headers.append(QHttpHeaders::WellKnownHeader::ContentType,
                   mimeDb.mimeTypeForFile(name, QMimeDatabase::MatchExtension).name());
    // Deliberately no Accept-Ranges: this route always answers with the whole
    // file, so advertising resumability would be a promise it does not keep. A
    // client that wants ranges has /api/v1/incoming/stream.
    headers.append(QHttpHeaders::WellKnownHeader::ContentDisposition,
                   QStringLiteral("attachment; filename=\"%1\"; filename*=UTF-8''%2")
                       .arg(QString(name).remove(QLatin1Char('"')).remove(QLatin1Char('\\')),
                            QString::fromLatin1(QUrl::toPercentEncoding(name))));

    responder.write(file, headers, QHttpServerResponse::StatusCode::Ok);
}

bool WebServer::hasStreamToken(const QHttpServerRequest& req) const
{
    const QUrlQuery query(req.query());
    const QString token = query.queryItemValue(QStringLiteral("token"));
    return !token.isEmpty() && token == m_streamToken;
}

QString WebServer::incomingRoot() const
{
    if (!m_preferences)
        return {};

    const QString dir = m_preferences->incomingDir();
    return dir.isEmpty() ? QString{} : QFileInfo(dir).canonicalFilePath();
}

WebServer::IncomingRoot WebServer::splitIncomingPath(const QString& relPath) const
{
    // Windows clients send backslashes; normalise before anything is inspected,
    // or "..\\.." walks straight past the component check in the caller.
    QString rel = relPath;
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));

    if (!rel.startsWith(QLatin1Char('!')))
        return {incomingRoot(), rel, -1};

    const qsizetype cut = rel.indexOf(QLatin1Char('/'));
    const QStringView token = cut < 0 ? QStringView{rel}.mid(1)
                                      : QStringView{rel}.mid(1, cut - 1);
    bool ok = false;
    const int index = token.toInt(&ok);
    if (!ok || index <= 0 || !m_preferences)
        return {};

    // Only a category with a folder of its own is addressable this way. One that
    // falls back to the global dir is already reachable at the plain root, and
    // giving it a second address would let the same file appear twice.
    const QString dir = m_preferences->category(index).incomingPath;
    if (dir.isEmpty())
        return {};

    return {QFileInfo(m_preferences->incomingDirForCategory(index)).canonicalFilePath(),
            cut < 0 ? QString{} : rel.mid(cut + 1), index};
}

QString WebServer::resolveIncomingPath(const QString& relPath) const
{
    const IncomingRoot selected = splitIncomingPath(relPath);
    const QString root = selected.root;
    if (root.isEmpty())
        return {};
    if (selected.remainder.isEmpty())
        return root;

    const QString rel = selected.remainder;
    if (QDir::isAbsolutePath(rel))
        return {};

    // Component-wise, not a substring search: a release directory may legitimately
    // contain ".." inside a name.
    const QStringList parts = rel.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        if (part == QLatin1String(".."))
            return {};
    }

    // canonicalFilePath resolves symlinks, so a link inside the folder that points
    // outside it fails the containment test rather than passing it. It is also
    // empty for anything that does not exist, which is the 404 the callers want.
    const QString abs = QFileInfo(root + QLatin1Char('/') + parts.join(QLatin1Char('/')))
                            .canonicalFilePath();
    if (abs.isEmpty())
        return {};
    if (abs != root && !abs.startsWith(root + QLatin1Char('/')))
        return {};

    return abs;
}

QString WebServer::resolveWebAsset(const QString& fileName) const
{
    // One segment, always: the /<arg> route captures [^/]+ and the favicon route
    // passes a literal. A separator here is malformed, not merely suspicious, so
    // this rejects outright rather than walking components the way the sibling
    // resolveIncomingPath() has to.
    if (fileName.isEmpty()
        || fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\'))
        || fileName == QLatin1String(".") || fileName == QLatin1String(".."))
        return {};

    // Template dir first so a theme can override any single file; the seeded assets
    // behind it so a theme only has to ship what it changes.
    for (const QString& root : {m_webAssetOverrideDir, m_webDataDir}) {
        if (root.isEmpty())
            continue;
        // Canonicalised per request, not once in start(): config/webserver/ may not
        // exist yet when start() runs (seeding is a separate pass) and
        // canonicalFilePath() is empty for a missing path, so caching that would
        // disable asset serving for the life of the process. Two extra stats per
        // hit, against responses already carrying Cache-Control: max-age=3600.
        const QString base = QFileInfo(root).canonicalFilePath();
        if (base.isEmpty())
            continue;
        // Resolves symlinks, so a link in a theme dir pointing out of it fails
        // containment instead of passing it. Empty for anything missing -- which is
        // exactly the per-file fall-through to the next root.
        const QString abs = QFileInfo(base + QLatin1Char('/') + fileName).canonicalFilePath();
        if (abs.isEmpty() || !abs.startsWith(base + QLatin1Char('/')))
            continue;
        return abs;
    }
    return {};
}

QString WebServer::ratingSpriteCss() const
{
    if (m_ratingSpriteCss)
        return *m_ratingSpriteCss;

    // Carry the sheet rather than link it: the static asset route only exists while
    // the web UI is on, and this page is reachable on a stream token with it off.
    // At 410 bytes that is cheaper than the request it saves, and it keeps the marks
    // pixel-identical to the ones the web UI and the Qt lists draw.
    // Same two-tier resolution as the static route: a theme shipping its own
    // sprite-rating.png gets its marks inlined here too. Empty path -> open() fails.
    QFile sheet(resolveWebAsset(QStringLiteral("sprite-rating.png")));
    if (!sheet.open(QIODevice::ReadOnly)) {
        m_ratingSpriteCss.emplace();
        return *m_ratingSpriteCss;
    }

    // Cell order and offsets are generate_sprites.py's, the same ones
    // config/webserver/sprite-rating.css uses: none, 0-5, search, fake.
    QString css = QStringLiteral(
        ".rm{display:inline-block;width:16px;height:16px;vertical-align:middle;"
        "background-image:url(data:image/png;base64,%1)}")
        .arg(QString::fromLatin1(sheet.readAll().toBase64()));
    for (int cell = 0; cell < 9; ++cell) {
        css += QStringLiteral(".rm%1{background-position:-%2px 0}")
                   .arg(cell).arg(cell * 16);
    }

    m_ratingSpriteCss = css;
    return *m_ratingSpriteCss;
}

QByteArray WebServer::renderIncomingListing(const QString& absDir, const QString& relPath,
                                            const QString& token) const
{
    // A category root is addressed as "!N/...", which is an implementation
    // detail the breadcrumb should not show — name the category instead.
    const IncomingRoot selected = splitIncomingPath(relPath);
    const QString rootLabel =
        selected.categoryIndex > 0 && m_preferences
            ? QStringLiteral("Incoming/") + m_preferences->category(selected.categoryIndex).title
            : QStringLiteral("Incoming");
    const QString here = selected.remainder.isEmpty()
                             ? rootLabel
                             : rootLabel + QLatin1Char('/') + selected.remainder;
    const QString spriteCss = ratingSpriteCss();
    QString html = pageHead(here, spriteCss);

    html += QStringLiteral("<h1>%1</h1>").arg(here.toHtmlEscaped());

    const QFileInfoList entries = QDir(absDir).entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);

    // Every category with a folder of its own shows at the top level as a
    // directory. They are not *inside* the global incoming folder, so nothing
    // relative could reach them — the "!N" address is what makes them
    // browsable at all.
    QList<QPair<int, QString>> categoryRoots;
    if (relPath.isEmpty() && m_preferences) {
        const auto categories = m_preferences->categories();
        for (int i = 1; i < categories.size(); ++i) {
            if (!categories.at(i).incomingPath.isEmpty())
                categoryRoots.append({i, categories.at(i).displayName()});
        }
    }

    if (entries.isEmpty() && categoryRoots.isEmpty() && relPath.isEmpty()) {
        html += QStringLiteral("<p class=\"empty\">Nothing has finished downloading yet.</p>");
        html += QStringLiteral("</body></html>");
        return html.toUtf8();
    }

    // One pass over the share, not one lookup per entry: forEachFile() holds the map
    // lock for its duration, and a finished-downloads folder can hold thousands of
    // files. Same trick, for the same reason, as handleBrowseDirectory().
    //
    // Matched on the canonical path, because the two sides spell it differently: the
    // share stores the path as configured, incomingRoot() hands back the canonical
    // form, and on macOS a temp or symlinked folder is /var here and /private/var
    // there. canonicalFilePath() is a syscall, so it runs only for shared files whose
    // *name* occurs in this directory — bounded by the listing, not by the share.
    QSet<QString> namesHere;
    for (const QFileInfo& fi : entries) {
        if (!fi.isDir())
            namesHere.insert(fi.fileName().toLower());
    }
    QHash<QString, KnownFile*> sharedByPath;
    if (m_sharedFiles && !namesHere.isEmpty()) {
        m_sharedFiles->forEachFile([&](KnownFile* file) {
            if (!file || file->filePath().isEmpty()
                || !namesHere.contains(file->fileName().toLower()))
                return;
            const QString canonical = QFileInfo(file->filePath()).canonicalFilePath();
            if (!canonical.isEmpty())
                sharedByPath.insert(canonical.toLower(), file);
        });
    }

    html += QStringLiteral("<table><tr><th>Name</th><th>Size</th><th>Modified</th>"
                           "<th></th></tr>");

    for (const auto& [index, title] : categoryRoots) {
        html += QStringLiteral("<tr><td><a href=\"%1\">%2/</a></td><td class=\"n\"></td>"
                               "<td class=\"n\"></td><td class=\"a\"></td></tr>")
                    .arg(incomingHref(QStringLiteral("/api/v1/incoming"), token,
                                      QStringLiteral("path"),
                                      QStringLiteral("!%1").arg(index)),
                         title.toHtmlEscaped());
    }

    if (!relPath.isEmpty()) {
        // From "!1" this yields "", i.e. the virtual root that lists the
        // category folders — which is where the user came from.
        const qsizetype cut = relPath.lastIndexOf(QLatin1Char('/'));
        const QString up = cut < 0 ? QString{} : relPath.left(cut);
        html += QStringLiteral("<tr><td><a href=\"%1\">../</a></td><td></td><td></td>"
                               "<td></td></tr>")
                    .arg(incomingHref(QStringLiteral("/api/v1/incoming"), token,
                                      QStringLiteral("path"), up));
    }

    for (const QFileInfo& fi : entries) {
        const QString name = fi.fileName();
        const QString rel = relPath.isEmpty() ? name
                                              : relPath + QLatin1Char('/') + name;
        const QString modified = fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm"));

        if (fi.isDir()) {
            html += QStringLiteral("<tr><td><a href=\"%1\">%2/</a></td><td class=\"n\"></td>"
                                   "<td class=\"n\">%3</td><td class=\"a\"></td></tr>")
                        .arg(incomingHref(QStringLiteral("/api/v1/incoming"), token,
                                          QStringLiteral("path"), rel),
                             name.toHtmlEscaped(), modified);
            continue;
        }

        QString actions = QStringLiteral("<a href=\"%1\">Download</a>")
                              .arg(incomingHref(QStringLiteral("/api/v1/incoming/download"),
                                                token, QStringLiteral("file"), rel));

        // Video and audio get a second link, and it is always the player page --
        // never the raw stream URL. Whether the browser can decode the container
        // is a question the player page answers; a link here that turns into a
        // download the moment it is clicked is not a link the listing should
        // offer at all.
        const ED2KFileType type = getED2KFileTypeID(name);
        const bool media = type == ED2KFileType::Video || type == ED2KFileType::Audio;
        if (media) {
            actions += QStringLiteral("<a href=\"%1\">Play</a>")
                           .arg(incomingHref(QStringLiteral("/api/v1/incoming"), token,
                                             QStringLiteral("play"), rel));
        }

        // Say it here rather than only on the player page. Finding out that a file is
        // not what it claims after clicking Play and watching a black rectangle is how
        // this got reported in the first place. No file-type gate: checkFile() already
        // answers Unchecked for every extension we promise nothing about, and gating
        // again only made this list narrower than the other three.
        //
        // Prefer the shared file's own verdict where there is one — the daemon's sweep
        // has usually settled it already, and it is the same answer either way.
        const KnownFile* known = sharedByPath.value(fi.canonicalFilePath().toLower(), nullptr);
        ContainerCheck check;
        if (known)
            check = known->containerCheckIfResolved();
        // Nothing has settled it yet — read it. One directory is bounded, and going
        // quiet until the sweep gets round to it would lose a mark this page has
        // always drawn. checkFile() costs nothing for an extension we promise
        // nothing about, which is what Unchecked also means here.
        if (check.verdict == ContainerVerdict::Unchecked)
            check = checkFile(fi.absoluteFilePath(), name);
        QString marker;
        if (check.isSuspect()) {
            const QString why = containerWarningText(check, name).toHtmlEscaped();
            marker = spriteCss.isEmpty()
                         // No sheet: fall back to the mark drawn in the page's own CSS.
                         ? QStringLiteral("<span class=\"bad\" title=\"%1\">!</span>").arg(why)
                         : QStringLiteral("<span class=\"rm rm8\" title=\"%1\"></span>").arg(why);
        }

        // And what other users said, for the files the share knows — the same rating
        // mark, from the same sheet, as the Qt lists and the web UI draw.
        QString rating;
        if (known && !spriteCss.isEmpty()) {
            const QString cell = webRatingToken(*known, m_preferences
                                                        && m_preferences->indicateRatings());
            if (cell != QLatin1String("none")) {
                rating = QStringLiteral("<span class=\"rm rm%1\" title=\"%2\"></span>")
                             .arg(cell == QLatin1String("search")
                                      ? QStringLiteral("7") : QString::number(cell.toInt() + 1),
                                  webRatingTitle(*known).toHtmlEscaped());
            }
        }

        html += QStringLiteral("<tr><td>%1%2%3</td><td class=\"n\">%4</td><td class=\"n\">%5</td>"
                               "<td class=\"a\">%6</td></tr>")
                    .arg(marker, rating, name.toHtmlEscaped(), formatByteSize(fi.size()), modified,
                         actions);
    }

    html += QStringLiteral("</table></body></html>");
    return html.toUtf8();
}

QByteArray WebServer::renderIncomingPlayer(const QString& relPath, const QString& fileName,
                                           const QString& token) const
{
    const qsizetype cut = relPath.lastIndexOf(QLatin1Char('/'));
    const QString folder = cut < 0 ? QString{} : relPath.left(cut);
    const QString src = incomingHref(QStringLiteral("/api/v1/incoming/stream"), token,
                                     QStringLiteral("file"), relPath);
    // A file whose magic contradicts its name decides its own element -- the fake
    // .wmv that prompted this is an MP3, and a <video> tag can only ever show a
    // black rectangle for it.
    const QString absPath = resolveIncomingPath(relPath);
    const ContainerCheck real = checkFile(absPath, fileName);
    const QString realMime = real.mimeType;
    const bool audio = realMime.isEmpty()
                           ? getED2KFileTypeID(fileName) == ED2KFileType::Audio
                           : realMime.startsWith(QLatin1String("audio/"));

    QString html = pageHead(fileName);
    html += QStringLiteral("<h1><a href=\"%1\">&larr;</a> %2</h1>")
                .arg(incomingHref(QStringLiteral("/api/v1/incoming"), token,
                                  QStringLiteral("path"), folder),
                     fileName.toHtmlEscaped());
    html += QStringLiteral("<%1 controls autoplay src=\"%2\"></%1>")
                .arg(audio ? QStringLiteral("audio") : QStringLiteral("video"), src);

    // The element above is offered either way -- browsers differ, and one that
    // does decode this container should not be talked out of it. The notice
    // below says why it might stay black, and the two reasons are worth telling
    // apart: a container the browser has no decoder for is fixed by opening VLC,
    // a file whose bytes are not what its name claims is fixed by nothing, and
    // sending someone to VLC for that one just wastes their time twice.
    const QString ext = QFileInfo(fileName).suffix().toLower();
    QString notice;
    QString callToAction = QStringLiteral("Open this URL in VLC or another player:");
    if (real.verdict == ContainerVerdict::WrongContainer) {
        notice = QStringLiteral(
                     "This file is named <strong>.%1</strong> but its contents are "
                     "<strong>%2</strong>. The name is wrong — common for files off "
                     "the ed2k network — so a player that trusts it finds no %3 and "
                     "sits at 0:00. It is being served as its real type, so it may "
                     "still play above.")
                     .arg(ext.toHtmlEscaped(), real.actual.toHtmlEscaped(),
                          real.expected.toHtmlEscaped());
    } else if (real.verdict == ContainerVerdict::NoKnownContainer) {
        // The signature its extension requires is missing and nothing else
        // matches, which is what a fake usually looks like from here. Say that
        // plainly: pointing this one at VLC only wastes the trip twice.
        notice = QStringLiteral(
                     "This file is named <strong>.%1</strong> but does not start with "
                     "the %2 signature every one of them has, and its contents match no "
                     "media container we recognise. It is very likely a fake or a "
                     "corrupt download — no player will get anything out of it.")
                     .arg(ext.toHtmlEscaped(), real.expected.toHtmlEscaped());
        // Not "open this in VLC": we just said nothing will play it, and sending
        // someone off to prove that for themselves is how this bug got reported.
        callToAction = QStringLiteral("The raw URL, if you want to look for yourself:");
    } else if (!isBrowserPlayable(fileName)) {
        notice = QStringLiteral("Your browser probably cannot decode <strong>.%1</strong>.")
                     .arg(ext.toHtmlEscaped());
    }

    if (!notice.isEmpty()) {
        html += QStringLiteral(
            "<div class=\"warn\"><p>%1 %3</p>"
            "<form onsubmit=\"return false\">"
            "<input id=\"u\" readonly value=\"%2\">"
            "<button id=\"c\">Copy</button></form></div>"
            // location.origin rather than a URL built on the server: the daemon
            // does not reliably know the scheme, host or port it was reached
            // through, and the browser does.
            "<script>"
            "var i=document.getElementById('u');"
            "i.value=location.origin+i.value;"
            "document.getElementById('c').onclick=function(){"
            "i.select();"
            // navigator.clipboard is undefined over plain HTTP to anything but
            // localhost, which is the common case for a core on the LAN.
            "if(navigator.clipboard)navigator.clipboard.writeText(i.value);"
            "else document.execCommand('copy');"
            "this.textContent='Copied';"
            "};"
            "</script>").arg(notice, src, callToAction);
    }

    html += QStringLiteral("<p><a href=\"%1\">Download this file</a></p>")
                .arg(incomingHref(QStringLiteral("/api/v1/incoming/download"), token,
                                  QStringLiteral("file"), relPath));
    html += QStringLiteral("</body></html>");
    return html.toUtf8();
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

    // "addr" is the IPv6-capable form the GET side emits; "ip" remains accepted for
    // IPv4-only callers, which is all the numeric field can express.
    Address friendAddr = Address::fromString(body[QStringLiteral("addr")].toString());
    if (friendAddr.isNull())
        friendAddr = Address::fromNetworkOrder(ip);

    bool hasHash = !hashStr.isEmpty() && hashStr.size() == 32;
    std::array<uint8, 16> hashBytes{};

    if (hasHash)
        hasHash = decodeBase16(hashStr, hashBytes.data(), 16) > 0;

    auto* f = m_friendList->addFriend(hasHash ? hashBytes.data() : nullptr,
                                       friendAddr, friendPort, name, hasHash);
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
        {QStringLiteral("uptimeSeconds"),        static_cast<qint64>(m_statistics->uptimeSecs())},
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

    // No admin password configured — deny login with clear message
    if (m_config.adminPasswordHash.isEmpty()
        && !(m_config.guestEnabled && !m_config.guestPasswordHash.isEmpty())) {
        QHash<QString, QString> vars;
        vars[QStringLiteral("CharSet")] = QStringLiteral("UTF-8");
        vars[QStringLiteral("eMuleAppName")] = QStringLiteral("eMule");
        vars[QStringLiteral("version")] = QString(kAppVersion);
        vars[QStringLiteral("WebControl")] = QStringLiteral("Web Control Panel");
        vars[QStringLiteral("FailedLogin")] = QStringLiteral(
            "<p class=\"failed\">Access denied &mdash; no password configured. "
            "Set a password in Options &rarr; Web Interface.</p>");
        const auto html = WebTemplateEngine::substitute(
            m_templateEngine->section(QStringLiteral("LOGIN")), vars);
        return QHttpServerResponse(QByteArrayLiteral("text/html"), html.toUtf8());
    }

    if (password.isEmpty()) {
        // Show login page with no error
        QHash<QString, QString> vars;
        vars[QStringLiteral("CharSet")] = QStringLiteral("UTF-8");
        vars[QStringLiteral("eMuleAppName")] = QStringLiteral("eMule");
        vars[QStringLiteral("version")] = QString(kAppVersion);
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
        vars[QStringLiteral("version")] = QString(kAppVersion);
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
        vars[QStringLiteral("version")] = QString(kAppVersion);
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
        vars[QStringLiteral("version")] = QString(kAppVersion);
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
        vars[QStringLiteral("version")] = QString(kAppVersion);
        vars[QStringLiteral("WebControl")] = QStringLiteral("Web Control Panel");
        vars[QStringLiteral("FailedLogin")] = QString();
        const auto html = WebTemplateEngine::substitute(
            m_templateEngine->section(QStringLiteral("LOGIN")), vars);
        return QHttpServerResponse(QByteArrayLiteral("text/html"), html.toUtf8());
    }

    // Dispatch actions before rendering (admin only)
    const QString activePage = page.isEmpty() ? QStringLiteral("transfer") : page;
    if (m_sessionManager->isAdmin(ses))
        dispatchActions(query, activePage);

    return renderPage(activePage, ses);
}

// ---------------------------------------------------------------------------
// Action dispatch — execute URL param actions before rendering
// ---------------------------------------------------------------------------

void WebServer::dispatchActions(const QUrlQuery& query, const QString& page)
{
    // --- Transfer actions (page=transfer, param "op") ---
    if (page == QStringLiteral("transfer")) {
        const QString op = query.queryItemValue(QStringLiteral("op"));
        if (op.isEmpty())
            return;

        if (op == QStringLiteral("clearcompleted")) {
            if (m_downloadQueue) {
                QList<PartFile*> toRemove;
                for (auto* file : m_downloadQueue->files()) {
                    if (file->status() == PartFileStatus::Complete)
                        toRemove.append(file);
                }
                for (auto* file : toRemove)
                    m_downloadQueue->removeFile(file);
            }
            return;
        }

        // All other transfer ops require a file hash
        const QString hashStr = query.queryItemValue(QStringLiteral("file"));
        if (hashStr.size() != 32 || !m_downloadQueue)
            return;
        std::array<uint8, 16> hash{};
        if (decodeBase16(hashStr, hash.data(), 16) != 16)
            return;
        auto* file = m_downloadQueue->fileByID(hash.data());
        if (!file)
            return;

        if (op == QStringLiteral("stop"))
            file->stopFile();
        else if (op == QStringLiteral("pause"))
            file->pauseFile();
        else if (op == QStringLiteral("resume"))
            file->resumeFile();
        else if (op == QStringLiteral("cancel"))
            file->stopFile(true);
        else if (op == QStringLiteral("priolow")) {
            file->setAutoDownPriority(false);
            file->setDownPriority(kPrLow);
        } else if (op == QStringLiteral("prionormal")) {
            file->setAutoDownPriority(false);
            file->setDownPriority(kPrNormal);
        } else if (op == QStringLiteral("priohigh")) {
            file->setAutoDownPriority(false);
            file->setDownPriority(kPrHigh);
        } else if (op == QStringLiteral("prioauto")) {
            file->setAutoDownPriority(true);
            file->setDownPriority(kPrHigh);
        }
        return;
    }

    // --- Server actions (page=server, param "c") ---
    if (page == QStringLiteral("server")) {
        const QString cmd = query.queryItemValue(QStringLiteral("c"));
        if (cmd.isEmpty())
            return;

        if (cmd == QStringLiteral("connect")) {
            const QString ip = query.queryItemValue(QStringLiteral("ip"));
            const uint16 port = query.queryItemValue(QStringLiteral("port")).toUShort();
            if (!ip.isEmpty() && port > 0 && m_serverList && m_serverConnect) {
                if (auto* srv = m_serverList->findByAddress(ip, port))
                    m_serverConnect->connectToServer(srv);
            } else if (m_serverConnect) {
                m_serverConnect->connectToAnyServer();
            }
        } else if (cmd == QStringLiteral("disconnect")) {
            if (m_serverConnect)
                m_serverConnect->disconnect();
        } else {
            // Commands that require a specific server
            const QString ip = query.queryItemValue(QStringLiteral("ip"));
            const uint16 port = query.queryItemValue(QStringLiteral("port")).toUShort();
            if (ip.isEmpty() || port == 0 || !m_serverList)
                return;
            auto* srv = m_serverList->findByAddress(ip, port);
            if (!srv)
                return;

            if (cmd == QStringLiteral("remove"))
                m_serverList->removeServer(srv);
            else if (cmd == QStringLiteral("addtostatic"))
                srv->setStaticMember(true);
            else if (cmd == QStringLiteral("removefromstatic"))
                srv->setStaticMember(false);
            else if (cmd == QStringLiteral("priolow"))
                srv->setPreference(ServerPriority::Low);
            else if (cmd == QStringLiteral("prionormal"))
                srv->setPreference(ServerPriority::Normal);
            else if (cmd == QStringLiteral("priohigh"))
                srv->setPreference(ServerPriority::High);
        }
        return;
    }

    // --- Kad actions (page=kad, param "c") ---
    if (page == QStringLiteral("kad")) {
        const QString cmd = query.queryItemValue(QStringLiteral("c"));
        if (cmd == QStringLiteral("connect")) {
            if (auto* kadInst = kad::Kademlia::instance())
                kadInst->start();
        } else if (cmd == QStringLiteral("disconnect")) {
            if (auto* kadInst = kad::Kademlia::instance())
                kadInst->stop();
        } else if (cmd == QStringLiteral("rcfirewall")) {
            if (auto* kadInst = kad::Kademlia::instance())
                (void)kadInst->recheckFirewalled();
        }
        return;
    }

    // --- ED2K link addition (any page, param "ed2k") ---
    const QString ed2k = query.queryItemValue(QStringLiteral("ed2k"));
    if (!ed2k.isEmpty() && m_downloadQueue)
        m_downloadQueue->addDownloadFromED2KLink(ed2k, QString());
}

QHttpServerResponse WebServer::renderPage(const QString& page, const QString& sessionId)
{
    const bool isAdmin = m_sessionManager->isAdmin(sessionId);

    // Build header vars
    QHash<QString, QString> headerVars;
    headerVars[QStringLiteral("CharSet")] = QStringLiteral("UTF-8");
    headerVars[QStringLiteral("eMuleAppName")] = QStringLiteral("eMule");
    headerVars[QStringLiteral("version")] = QString(kAppVersion);
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
        content = buildTransferPage(isAdmin, sessionId);
    else if (page == QStringLiteral("server"))
        content = buildServerListPage(isAdmin, sessionId);
    else if (page == QStringLiteral("search"))
        content = buildSearchPage(isAdmin);
    else if (page == QStringLiteral("shared"))
        content = buildSharedFilesPage(isAdmin, sessionId);
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
        content = buildKadPage(sessionId);
    else if (page == QStringLiteral("myinfo"))
        content = buildMyInfoPage();
    else
        content = buildTransferPage(isAdmin, sessionId);

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
    // Traversal guard, theme override and existence check all live in the resolver.
    // A rejected path now 404s rather than 403s -- a 403 confirms the guard fired and
    // so leaks whether the path exists.
    const QString filePath = resolveWebAsset(path);
    QFile file(filePath);
    if (filePath.isEmpty() || !file.open(QIODevice::ReadOnly)) {
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

QString WebServer::buildTransferPage(bool isAdmin, const QString& sessionId)
{
    QHash<QString, QString> vars;

    // Build download list
    QString downLines;
    if (m_downloadQueue) {
        const QString lineTmpl = m_templateEngine->section(QStringLiteral("TRANSFER_DOWN_LINE"));
        int index = 0;
        for (const auto* file : m_downloadQueue->files()) {
            QHash<QString, QString> lineVars;
            lineVars[QStringLiteral("Session")] = sessionId;
            // The engine substitutes raw, so anything user-supplied has to be
            // escaped here or a file named with a "<" injects markup.
            lineVars[QStringLiteral("DownloadFileName")] = file->fileName().toHtmlEscaped();
            lineVars[QStringLiteral("DownloadFileType")] = webFileTypeToken(file->fileName());
            lineVars[QStringLiteral("DownloadCommentIcon")] = webCommentToken(*file);
            lineVars[QStringLiteral("DownloadRating")] =
                webRatingToken(*file, m_preferences && m_preferences->indicateRatings());
            lineVars[QStringLiteral("DownloadRatingTitle")] =
                webRatingTitle(*file).toHtmlEscaped();
            // The download list is tens of files and the verdict is cached after the
            // first look, so this one may read. The share cannot — see the shared page.
            const ContainerCheck& cc = file->containerCheck();
            lineVars[QStringLiteral("DownloadFake")] =
                cc.isSuspect() ? QStringLiteral("fake") : QStringLiteral("none");
            lineVars[QStringLiteral("DownloadFakeTitle")] =
                containerWarningText(cc, file->fileName()).toHtmlEscaped();
            lineVars[QStringLiteral("DownloadFileSize")] = formatByteSize(file->fileSize());
            lineVars[QStringLiteral("DownloadFileHash")] = md4str(file->fileHash());
            lineVars[QStringLiteral("DownloadCompleted")] = formatByteSize(file->completedSize());
            lineVars[QStringLiteral("DownloadSpeed")] = formatByteRate(file->datarate());
            lineVars[QStringLiteral("DownloadSources")] = QString::number(file->sourceCount());
            lineVars[QStringLiteral("DownloadPriority")] = QString::number(file->downPriority());

            double progress = file->fileSize() > 0
                ? static_cast<double>(file->completedSize()) * 100.0 / static_cast<double>(file->fileSize())
                : 0.0;
            lineVars[QStringLiteral("DownloadPercent")] = QString::number(progress, 'f', 1);

            // Status key for context menu JS (simple string for state-aware menu)
            QString statusKey;
            if (file->isStopped()) {
                lineVars[QStringLiteral("DownloadStatus")] = QStringLiteral("t_stopped");
                statusKey = QStringLiteral("stopped");
            } else {
                switch (file->status()) {
                case PartFileStatus::Ready:
                case PartFileStatus::Empty:
                    lineVars[QStringLiteral("DownloadStatus")] = QStringLiteral("t_downloading");
                    statusKey = QStringLiteral("downloading");
                    break;
                case PartFileStatus::Paused:
                    lineVars[QStringLiteral("DownloadStatus")] = QStringLiteral("t_paused");
                    statusKey = QStringLiteral("paused");
                    break;
                case PartFileStatus::Complete:
                    lineVars[QStringLiteral("DownloadStatus")] = QStringLiteral("t_complete");
                    statusKey = QStringLiteral("complete");
                    break;
                case PartFileStatus::Error:
                    lineVars[QStringLiteral("DownloadStatus")] = QStringLiteral("t_error");
                    statusKey = QStringLiteral("error");
                    break;
                case PartFileStatus::WaitingForHash:
                case PartFileStatus::Hashing:
                    lineVars[QStringLiteral("DownloadStatus")] = QStringLiteral("t_waitinghash");
                    statusKey = QStringLiteral("hashing");
                    break;
                default:
                    lineVars[QStringLiteral("DownloadStatus")] = QStringLiteral("t_waiting");
                    statusKey = QStringLiteral("waiting");
                    break;
                }
            }
            lineVars[QStringLiteral("DownloadStatusKey")] = statusKey;
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

            lineVars[QStringLiteral("3")] = formatByteSize(transferred);
            lineVars[QStringLiteral("4")] = formatByteRate(speed);

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
    transferVars[QStringLiteral("TotalUpTransferred")] = formatByteSize(totalUpTransferred);
    transferVars[QStringLiteral("TotalUpSpeed")] = formatByteRate(totalUpSpeed);

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

QString WebServer::buildServerListPage(bool /*isAdmin*/, const QString& sessionId)
{
    QHash<QString, QString> vars;
    QString serverLines;

    if (m_serverList) {
        const QString lineTmpl = m_templateEngine->section(QStringLiteral("SERVER_LINE"));
        for (const auto& srv : m_serverList->servers()) {
            QHash<QString, QString> lineVars;
            lineVars[QStringLiteral("Session")] = sessionId;
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

QString WebServer::buildSearchPage(bool /*isAdmin*/)
{
    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("SEARCH")), {});
}

QString WebServer::buildSharedFilesPage(bool /*isAdmin*/, const QString& sessionId)
{
    QHash<QString, QString> vars;
    QString sharedLines;

    if (m_sharedFiles) {
        const QString lineTmpl = m_templateEngine->section(QStringLiteral("SHARED_LINE"));
        m_sharedFiles->forEachFile([&](KnownFile* file) {
            QHash<QString, QString> lineVars;
            lineVars[QStringLiteral("Session")] = sessionId;
            lineVars[QStringLiteral("SharedFileName")] = file->fileName().toHtmlEscaped();
            lineVars[QStringLiteral("SharedFileType")] = webFileTypeToken(file->fileName());
            lineVars[QStringLiteral("SharedCommentIcon")] = webCommentToken(*file);
            lineVars[QStringLiteral("SharedRating")] =
                webRatingToken(*file, m_preferences && m_preferences->indicateRatings());
            lineVars[QStringLiteral("SharedRatingTitle")] =
                webRatingTitle(*file).toHtmlEscaped();
            // Only what the daemon's background sweep has settled: this walks the whole
            // share, so it may not open files (SharedFileList::warmContainerChecks).
            const ContainerCheck& cc = file->containerCheckIfResolved();
            lineVars[QStringLiteral("SharedFake")] =
                cc.isSuspect() ? QStringLiteral("fake") : QStringLiteral("none");
            lineVars[QStringLiteral("SharedFakeTitle")] =
                containerWarningText(cc, file->fileName()).toHtmlEscaped();
            lineVars[QStringLiteral("SharedFileSize")] = formatByteSize(file->fileSize());
            lineVars[QStringLiteral("SharedFileHash")] = md4str(file->fileHash());
            lineVars[QStringLiteral("SharedRequests")] = QString::number(file->statistic.requests());
            lineVars[QStringLiteral("SharedAccepted")] = QString::number(file->statistic.accepts());
            lineVars[QStringLiteral("SharedTransferred")] = formatByteSize(file->statistic.transferred());
            lineVars[QStringLiteral("SharedPriority")] = QString::number(file->upPriority());
            // ED2K link for Copy ED2K Link context menu
            // Escape single quotes for JS string embedding in oncontextmenu attr
            QString safeName = file->fileName();
            safeName.replace(u'\'', QStringLiteral("\\'"));
            safeName.replace(u'&', QStringLiteral("&amp;"));
            lineVars[QStringLiteral("SharedED2kLink")] = QStringLiteral("ed2k://|file|%1|%2|%3|/")
                .arg(safeName)
                .arg(file->fileSize())
                .arg(md4str(file->fileHash()));
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
        vars[QStringLiteral("SessionReceived")] = formatByteSize(m_statistics->sessionReceivedBytes());
        vars[QStringLiteral("SessionSent")] = formatByteSize(m_statistics->sessionSentBytes());
        vars[QStringLiteral("Reconnects")] = QString::number(m_statistics->reconnects());
        vars[QStringLiteral("Uptime")] = QString::number(m_statistics->uptimeSecs());
    }
    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("STATS")), vars);
}

namespace {

/// Sample window the page shows — MFC's WEB_GRAPH_WIDTH (srchybrid/WebServer.h:8).
/// Core keeps 1024 samples, so the newest 500 are handed over.
constexpr size_t kWebGraphWidth = 500;

/// SVG viewBox the shipped template draws into. The width matches the sample window
/// one-to-one; the height is MFC's WEB_GRAPH_HEIGHT.
constexpr int kGraphViewWidth  = 500;
constexpr int kGraphViewHeight = 120;

/// One series as the template sees it.
struct GraphSeries {
    QString csv;      ///< comma-separated values, oldest first (MFC's variable)
    QString points;   ///< "x,y x,y …" for an SVG <polyline>
};

/// @param csvScale factor from @p values into the units the CSV variable carries —
///                 1024 for the rates, which MFC writes in bytes/s.
/// @param maxValue full-scale value for the Y axis, in the units of @p values.
GraphSeries buildSeries(const std::vector<double>& values, double maxValue,
                        double csvScale, int viewW, int viewH)
{
    GraphSeries out;
    if (values.empty())
        return out;   // an empty variable, not a stray placeholder

    const double top = maxValue > 0.0 ? maxValue : 1.0;
    const auto count = static_cast<int>(values.size());

    QStringList csv;
    QStringList points;
    csv.reserve(count);
    points.reserve(count);

    for (int i = 0; i < count; ++i) {
        const double value = values[static_cast<size_t>(i)];
        csv << QString::number(static_cast<uint64>(std::max(0.0, value) * csvScale));

        // A short history still spans the full width, so the page reads the same
        // whether the daemon started a minute or an hour ago.
        const double x = count > 1 ? (i * static_cast<double>(viewW - 1)) / (count - 1)
                                   : 0.0;
        const double y = viewH - std::clamp(value / top, 0.0, 1.0) * viewH;
        points << QStringLiteral("%1,%2").arg(QString::number(x, 'f', 1),
                                              QString::number(y, 'f', 1));
    }

    out.csv = csv.join(u',');
    out.points = points.join(u' ');
    return out;
}

} // namespace

QHash<QString, QString> WebServer::graphVars(const std::vector<StatsGraphSample>& samples,
                                             uint32 maxDown, uint32 maxUp, uint32 maxConn,
                                             int viewW, int viewH)
{
    std::vector<double> down;
    std::vector<double> up;
    std::vector<double> conn;
    down.reserve(samples.size());
    up.reserve(samples.size());
    conn.reserve(samples.size());
    for (const StatsGraphSample& s : samples) {
        // The three values MFC pushes into its web ring: the current rates and the
        // active connection count (srchybrid/StatisticsDlg.cpp:600-607).
        down.push_back(s.downCurrent);
        up.push_back(s.upCurrent);
        conn.push_back(s.connActive);
    }

    // The rates are KB/s here; MFC's CSV is bytes/s, so it multiplies by 1024
    // (srchybrid/WebServer.cpp:3038-3043). Connections are a plain count.
    const GraphSeries downSeries = buildSeries(down, maxDown, 1024.0, viewW, viewH);
    const GraphSeries upSeries   = buildSeries(up, maxUp, 1024.0, viewW, viewH);
    const GraphSeries connSeries = buildSeries(conn, maxConn, 1.0, viewW, viewH);

    QHash<QString, QString> vars;
    vars[QStringLiteral("GraphDownload")]       = downSeries.csv;
    vars[QStringLiteral("GraphUpload")]         = upSeries.csv;
    vars[QStringLiteral("GraphConnections")]    = connSeries.csv;
    vars[QStringLiteral("GraphDownloadPts")]    = downSeries.points;
    vars[QStringLiteral("GraphUploadPts")]      = upSeries.points;
    vars[QStringLiteral("GraphConnectionsPts")] = connSeries.points;
    vars[QStringLiteral("MaxDownload")]         = QString::number(maxDown);
    vars[QStringLiteral("MaxUpload")]           = QString::number(maxUp);
    vars[QStringLiteral("MaxConnections")]      = QString::number(maxConn);
    return vars;
}

QString WebServer::buildGraphsPage()
{
    std::vector<StatsGraphSample> samples;
    if (m_statsHistory) {
        samples = m_statsHistory->statsSince(0);
        if (samples.size() > kWebGraphWidth)
            samples.erase(samples.begin(), samples.end() - kWebGraphWidth);
    }

    // MFC pads its axis maxima the same way, so a trace at the limit still has air
    // above it (srchybrid/WebServer.cpp:3053-3059).
    const uint32 maxDown = (m_preferences ? m_preferences->maxGraphDownloadRate() : 0) + 4;
    const uint32 maxUp   = (m_preferences ? m_preferences->maxGraphUploadRate() : 0) + 4;
    const uint32 maxConn = (m_preferences ? m_preferences->maxConnections() : 0) + 20;

    // The desktop graphs' palette is GUI-only state in uistate.yml — the daemon cannot
    // see it, so the template paints in eMule's factory colours instead.
    QHash<QString, QString> vars =
        graphVars(samples, maxDown, maxUp, maxConn, kGraphViewWidth, kGraphViewHeight);

    const uint32 interval = m_preferences ? m_preferences->graphsUpdateSec() : 0;
    vars[QStringLiteral("ScaleTime")] = formatDuration(
        std::chrono::seconds(static_cast<int64_t>(interval > 0 ? interval : 3)
                             * static_cast<int64_t>(kWebGraphWidth)));

    vars[QStringLiteral("TxtDownload")]    = QStringLiteral("Downloads");
    vars[QStringLiteral("TxtUpload")]      = QStringLiteral("Uploads");
    vars[QStringLiteral("TxtConnections")] = QStringLiteral("Active Connections");
    vars[QStringLiteral("TxtTime")]        = QStringLiteral("Time");
    vars[QStringLiteral("KByteSec")]       = QStringLiteral("KB/s");

    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("GRAPHS")), vars);
}

QString WebServer::buildPreferencesPage(bool /*isAdmin*/)
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

QString WebServer::buildKadPage(const QString& sessionId)
{
    QHash<QString, QString> vars;
    vars[QStringLiteral("Session")] = sessionId;

    QString kadStatus;
    if (auto* kadInst = kad::Kademlia::instance()) {
        if (kadInst->isRunning())
            kadStatus = QStringLiteral("Running");
        else
            kadStatus = QStringLiteral("Disconnected");
    } else {
        kadStatus = QStringLiteral("Not available");
    }
    vars[QStringLiteral("KadStatus")] = kadStatus;

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
