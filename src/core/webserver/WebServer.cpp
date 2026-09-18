#include "pch.h"
/// @file WebServer.cpp
/// @brief JSON REST API + template web server — implementation.

#include "webserver/WebServer.h"
#include "webserver/JsonSerializers.h"
#include "webserver/WebSessionManager.h"
#include "webserver/WebTemplateEngine.h"

#include "app/AppConfig.h"
#include "app/TranslationRouter.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "friends/Friend.h"
#include "friends/FriendList.h"
#include "media/ContainerSniffer.h"
#include "prefs/Preferences.h"
#include "protocol/ED2KLink.h"
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
#include "utils/UsenetDisplay.h"

#include <QCborArray>
#include <QCoreApplication>
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
#include <QLocale>
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
#include <cstring>

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

/// An already-answered future, for routes that sometimes have to wait.
QFuture<QHttpServerResponse> finishedResponse(QHttpServerResponse&& response)
{
    QPromise<QHttpServerResponse> promise;
    QFuture<QHttpServerResponse> future = promise.future();
    promise.start();
    promise.addResult(std::move(response));
    promise.finish();
    return future;
}

/// A value for a template: element text or a quoted attribute. The engine never
/// scans a value again, so escaping is all it takes.
[[nodiscard]] QString htmlText(const QString& s)
{
    return WebTemplateEngine::htmlEscape(s);
}

/// `lang=` for an <html> element: "de-DE", or "en" for the source language.
[[nodiscard]] QString htmlLangCode(const QString& code)
{
    return code.isEmpty() ? QStringLiteral("en")
                          : QString(code).replace(QLatin1Char('_'), QLatin1Char('-'));
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

        // The Usenet page talks JSON over its session rather than GET-and-render
        // like the Transfer page, so a reload never repeats an action and a
        // passphrase never lands in a URL.
        m_server->route(QStringLiteral("/usenet/action"), QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& req) {
                return handleWebUsenetAction(req);
            });

        m_server->route(QStringLiteral("/usenet/add"), QHttpServerRequest::Method::Post,
            [this](const QHttpServerRequest& req) -> QFuture<QHttpServerResponse> {
                const WebSessionCheck ses = webSession(QUrlQuery(req.query()));
                const TranslationRouter::Scope language(m_translations, webLanguage(ses.id));
                if (!ses.valid)
                    return finishedResponse(jsonError(401, tr("Session expired — log in again")));
                if (!ses.admin)
                    return finishedResponse(jsonError(403, tr("Guests cannot add downloads")));
                return handleUsenetAdd(req, /*restApi*/ false);
            });

        m_server->route(QStringLiteral("/usenet/entries"), QHttpServerRequest::Method::Get,
            [this](const QHttpServerRequest& req) {
                return handleWebUsenetEntries(req);
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

    // --- Usenet queue ---
    // Literal paths before the <arg> ones, so "stats" is never taken for an item id.
    m_server->route(QStringLiteral("/api/v1/usenet/stats"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleRestUsenetStats();
        });

    m_server->route(QStringLiteral("/api/v1/usenet/pause"), QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleUsenetEngineOp(true);
        });

    m_server->route(QStringLiteral("/api/v1/usenet/resume"), QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleUsenetEngineOp(false);
        });

    m_server->route(QStringLiteral("/api/v1/usenet/categories/<arg>/<arg>"), QHttpServerRequest::Method::Post,
        [this](const QString& category, const QString& op, const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleRestUsenetCategoryOp(category, op);
        });

    m_server->route(QStringLiteral("/api/v1/usenet"), QHttpServerRequest::Method::Get,
        [this](const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleRestUsenetList(req);
        });

    m_server->route(QStringLiteral("/api/v1/usenet"), QHttpServerRequest::Method::Post,
        [this](const QHttpServerRequest& req) -> QFuture<QHttpServerResponse> {
            if (auto r = checkAuth(req.headers()); !r.ok)
                return finishedResponse(std::move(r.response));
            return handleUsenetAdd(req, /*restApi*/ true);
        });

    m_server->route(QStringLiteral("/api/v1/usenet/<arg>"), QHttpServerRequest::Method::Get,
        [this](const QString& id, const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleRestUsenetItem(id);
        });

    m_server->route(QStringLiteral("/api/v1/usenet/<arg>"), QHttpServerRequest::Method::Patch,
        [this](const QString& id, const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleRestUsenetPatch(id, req);
        });

    m_server->route(QStringLiteral("/api/v1/usenet/<arg>"), QHttpServerRequest::Method::Delete,
        [this](const QString& id, const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleRestUsenetDelete(id, req);
        });

    m_server->route(QStringLiteral("/api/v1/usenet/<arg>/<arg>/entries"), QHttpServerRequest::Method::Get,
        [this](const QString& id, const QString& fileIndex, const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleRestUsenetEntries(id, fileIndex);
        });

    m_server->route(QStringLiteral("/api/v1/usenet/<arg>/<arg>"), QHttpServerRequest::Method::Post,
        [this](const QString& id, const QString& op, const QHttpServerRequest& req) {
            if (auto r = checkAuth(req.headers()); !r.ok) return std::move(r.response);
            return handleRestUsenetItemOp(id, op);
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
    // Same gate as the ED2K route: a per-process random token, not the REST API
    // key. Preview is served even with both web surfaces switched off, so it
    // cannot lean on either one's authentication.
    const QUrlQuery query(req.query());
    const QString token = query.queryItemValue(QStringLiteral("token"));
    if (token.isEmpty() || token != m_streamToken) {
        logWarning(QStringLiteral("Usenet preview: 401 — invalid or missing stream token"));
        return finishedResponse(jsonError(401, QStringLiteral("Invalid or missing stream token")));
    }

    if (!m_usenetStreamResolver)
        return finishedResponse(jsonError(503, QStringLiteral("Usenet engine unavailable")));

    bool indexOk = false;
    const int fileIndex = fileIndexText.toInt(&indexOk);
    if (!indexOk || fileIndex < 0)
        return finishedResponse(jsonError(400, QStringLiteral("Invalid file index")));

    // Which file inside the archive set. Absent means "the first playable one",
    // which is what every URL predating the chooser carries. A malformed value
    // is rejected here rather than passed on: a bad ordinal is a bad request,
    // not a lookup that happens to miss.
    int entryOrdinal = -1;
    if (query.hasQueryItem(QStringLiteral("entry"))) {
        bool entryOk = false;
        entryOrdinal = query.queryItemValue(QStringLiteral("entry")).toInt(&entryOk);
        if (!entryOk || entryOrdinal < 0)
            return finishedResponse(jsonError(400, QStringLiteral("Invalid archive entry")));
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
        return finishedResponse(jsonError(404, QStringLiteral("File not found")));
    }

    // A compressed, solid or encrypted archive will never be mappable. Waiting
    // out the poll would end in a 416 that reads like "not yet" — 406 with the
    // reason says "not ever", which is what the user needs to know.
    if (!first.notSeekableReason.isEmpty()) {
        logWarning(QStringLiteral("Usenet preview: 406 — %1").arg(first.notSeekableReason));
        return finishedResponse(jsonError(406, first.notSeekableReason));
    }

    if (first.availableEnd > wantStart) {
        return finishedResponse(serveRange(first.pieces, first.fileName, first.totalSize,
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
QString incomingHref(const QString& path, const QString& token, const QString& lang,
                     const QString& key, const QString& value)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("token"), token);
    if (!lang.isEmpty())
        query.addQueryItem(QStringLiteral("lang"), lang);
    if (!value.isEmpty())
        query.addQueryItem(key, value);

    const QString href = path + QLatin1Char('?') + query.toString(QUrl::FullyEncoded);
    return htmlText(href);
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
        return WebServer::tr("Looking for comments on Kad");
    if (rating >= 1 && rating <= 5)
        return WebServer::tr("Rating: %1").arg(ratingLabel(static_cast<int>(rating)));
    return f.hasComment() ? WebServer::tr("Has comments") : QString{};
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
        "%2</style></head><body>").arg(htmlText(title), extraCss);
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

    // No session on a token route, so the web UI's pages name their language. Kept
    // only when usable, and handed on to every link the page draws.
    QString lang = query.queryItemValue(QStringLiteral("lang"));
    if (!m_translations || !m_translations->isUsable(lang))
        lang.clear();
    const TranslationRouter::Scope language(m_translations, webLanguage({}, lang));

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
                                   renderIncomingPlayer(play, QFileInfo(abs).fileName(), token, lang));
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
                               renderIncomingListing(abs, rel, token, lang));
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
                                            const QString& token, const QString& lang) const
{
    // A category root is addressed as "!N/...", which is an implementation
    // detail the breadcrumb should not show — name the category instead.
    const IncomingRoot selected = splitIncomingPath(relPath);
    const QString rootLabel =
        selected.categoryIndex > 0 && m_preferences
            ? tr("Incoming") + QLatin1Char('/') + m_preferences->category(selected.categoryIndex).title
            : tr("Incoming");
    const QString here = selected.remainder.isEmpty()
                             ? rootLabel
                             : rootLabel + QLatin1Char('/') + selected.remainder;
    const QString spriteCss = ratingSpriteCss();
    QString html = pageHead(here, spriteCss);

    html += QStringLiteral("<h1>%1</h1>").arg(htmlText(here));

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
        html += QStringLiteral("<p class=\"empty\">%1</p>")
                    .arg(htmlText(tr("Nothing has finished downloading yet.")));
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

    html += QStringLiteral("<table><tr><th>%1</th><th>%2</th><th>%3</th><th></th></tr>")
                .arg(htmlText(tr("Name")), htmlText(tr("Size")), htmlText(tr("Modified")));

    for (const auto& [index, title] : categoryRoots) {
        html += QStringLiteral("<tr><td><a href=\"%1\">%2/</a></td><td class=\"n\"></td>"
                               "<td class=\"n\"></td><td class=\"a\"></td></tr>")
                    .arg(incomingHref(QStringLiteral("/api/v1/incoming"), token, lang,
                                      QStringLiteral("path"),
                                      QStringLiteral("!%1").arg(index)),
                         htmlText(title));
    }

    if (!relPath.isEmpty()) {
        // From "!1" this yields "", i.e. the virtual root that lists the
        // category folders — which is where the user came from.
        const qsizetype cut = relPath.lastIndexOf(QLatin1Char('/'));
        const QString up = cut < 0 ? QString{} : relPath.left(cut);
        html += QStringLiteral("<tr><td><a href=\"%1\">../</a></td><td></td><td></td>"
                               "<td></td></tr>")
                    .arg(incomingHref(QStringLiteral("/api/v1/incoming"), token, lang,
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
                        .arg(incomingHref(QStringLiteral("/api/v1/incoming"), token, lang,
                                          QStringLiteral("path"), rel),
                             htmlText(name), modified);
            continue;
        }

        QString actions = QStringLiteral("<a href=\"%1\">%2</a>")
                              .arg(incomingHref(QStringLiteral("/api/v1/incoming/download"), token, lang,
                                                QStringLiteral("file"), rel),
                                   htmlText(tr("Download")));

        // Video and audio get a second link, and it is always the player page --
        // never the raw stream URL. Whether the browser can decode the container
        // is a question the player page answers; a link here that turns into a
        // download the moment it is clicked is not a link the listing should
        // offer at all.
        const ED2KFileType type = getED2KFileTypeID(name);
        const bool media = type == ED2KFileType::Video || type == ED2KFileType::Audio;
        if (media) {
            actions += QStringLiteral("<a href=\"%1\">%2</a>")
                           .arg(incomingHref(QStringLiteral("/api/v1/incoming"), token, lang,
                                             QStringLiteral("play"), rel),
                                htmlText(tr("Play")));
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
            const QString why = htmlText(containerWarningText(check, name));
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
                                  htmlText(webRatingTitle(*known)));
            }
        }

        html += QStringLiteral("<tr><td>%1%2%3</td><td class=\"n\">%4</td><td class=\"n\">%5</td>"
                               "<td class=\"a\">%6</td></tr>")
                    .arg(marker, rating, htmlText(name), formatByteSize(fi.size()), modified,
                         actions);
    }

    html += QStringLiteral("</table></body></html>");
    return html.toUtf8();
}

QByteArray WebServer::renderIncomingPlayer(const QString& relPath, const QString& fileName,
                                           const QString& token, const QString& lang) const
{
    const qsizetype cut = relPath.lastIndexOf(QLatin1Char('/'));
    const QString folder = cut < 0 ? QString{} : relPath.left(cut);
    const QString src = incomingHref(QStringLiteral("/api/v1/incoming/stream"), token, lang,
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
                .arg(incomingHref(QStringLiteral("/api/v1/incoming"), token, lang,
                                  QStringLiteral("path"), folder),
                     htmlText(fileName));
    html += QStringLiteral("<%1 controls autoplay src=\"%2\"></%1>")
                .arg(audio ? QStringLiteral("audio") : QStringLiteral("video"), src);

    // The element above is offered either way -- browsers differ, and one that
    // does decode this container should not be talked out of it. The notice
    // below says why it might stay black, and the two reasons are worth telling
    // apart: a container the browser has no decoder for is fixed by opening VLC,
    // a file whose bytes are not what its name claims is fixed by nothing, and
    // sending someone to VLC for that one just wastes their time twice.
    const QString ext = QFileInfo(fileName).suffix().toLower();
    // The sentences are translated and escaped whole; the markup goes in as arguments.
    const auto strong = [](const QString& text) {
        return QStringLiteral("<strong>%1</strong>").arg(htmlText(text));
    };
    QString notice;
    QString callToAction = htmlText(tr("Open this URL in VLC or another player:"));
    if (real.verdict == ContainerVerdict::WrongContainer) {
        notice = htmlText(tr("This file is named %1 but its contents are %2. The name is wrong "
                             "— common for files off the ed2k network — so a player that "
                             "trusts it finds no %3 and sits at 0:00. It is being served as "
                             "its real type, so it may still play above."))
                     .arg(strong(QLatin1Char('.') + ext), strong(real.actual),
                          htmlText(real.expected));
    } else if (real.verdict == ContainerVerdict::NoKnownContainer) {
        // The signature its extension requires is missing and nothing else
        // matches, which is what a fake usually looks like from here. Say that
        // plainly: pointing this one at VLC only wastes the trip twice.
        notice = htmlText(tr("This file is named %1 but does not start with the %2 signature "
                             "every one of them has, and its contents match no media container "
                             "we recognise. It is very likely a fake or a corrupt download — no "
                             "player will get anything out of it."))
                     .arg(strong(QLatin1Char('.') + ext), htmlText(real.expected));
        // Not "open this in VLC": we just said nothing will play it, and sending
        // someone off to prove that for themselves is how this bug got reported.
        callToAction = htmlText(tr("The raw URL, if you want to look for yourself:"));
    } else if (!isBrowserPlayable(fileName)) {
        notice = htmlText(tr("Your browser probably cannot decode %1."))
                     .arg(strong(QLatin1Char('.') + ext));
    }

    if (!notice.isEmpty()) {
        html += QStringLiteral(
            "<div class=\"warn\"><p>%1 %3</p>"
            "<form onsubmit=\"return false\">"
            "<input id=\"u\" readonly value=\"%2\">"
            "<button id=\"c\">%4</button></form></div>"
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
            "this.textContent='%5';"
            "};"
            "</script>").arg(notice, src, callToAction, htmlText(tr("Copy")),
                             WebTemplateEngine::jsEscape(tr("Copied")));
    }

    html += QStringLiteral("<p><a href=\"%1\">%2</a></p>")
                .arg(incomingHref(QStringLiteral("/api/v1/incoming/download"), token, lang,
                                  QStringLiteral("file"), relPath),
                     htmlText(tr("Download this file")));
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

    // No session yet, so the login page speaks the app's language.
    const TranslationRouter::Scope language(m_translations, webLanguage({}));

    // Parse form body: w=password&p=<password>
    const QUrlQuery query(QString::fromUtf8(request.body()));
    const QString password = query.queryItemValue(QStringLiteral("p"));

    // No admin password configured — deny login with clear message
    if (m_config.adminPasswordHash.isEmpty()
        && !(m_config.guestEnabled && !m_config.guestPasswordHash.isEmpty())) {
        return loginPage(QStringLiteral("<p class=\"failed\">%1</p>").arg(htmlText(
            tr("Access denied — no password configured. Set a password in Options → Web Interface."))));
    }

    if (password.isEmpty())
        return loginPage({});

    // Hash the password and attempt login
    const QByteArray passwordHash = QCryptographicHash::hash(
        password.toUtf8(), QCryptographicHash::Sha256).toHex();

    const QString sessionId = m_sessionManager->login(
        QString::fromLatin1(passwordHash),
        m_config.adminPasswordHash,
        m_config.guestPasswordHash,
        m_config.guestEnabled);

    if (sessionId.isEmpty()) {
        return loginPage(
            QStringLiteral("<p class=\"failed\">%1</p>").arg(htmlText(tr("Login failed"))));
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
    // Until a session says otherwise, the app's language.
    const TranslationRouter::Scope appLanguage(m_translations, webLanguage({}));

    if (!m_templateEngine || !m_templateEngine->isValid() || !m_sessionManager)
        return loginPage({});

    const QUrlQuery query(request.url());
    const QString ses = query.queryItemValue(QStringLiteral("ses"));
    const QString page = query.queryItemValue(QStringLiteral("w"));

    // Check for logout
    if (page == QStringLiteral("logout")) {
        if (!ses.isEmpty())
            m_sessionManager->logout(ses);
        return loginPage({});
    }

    // Validate session
    if (ses.isEmpty() || !m_sessionManager->isValid(ses))
        return loginPage({});

    // The header's language menu, guests included. Idempotent, so a reload that
    // repeats it is harmless; empty follows the app again.
    if (query.hasQueryItem(QStringLiteral("setlang"))) {
        const QString code = query.queryItemValue(QStringLiteral("setlang"));
        if (code.isEmpty() || (m_translations && m_translations->isUsable(code)))
            m_sessionManager->setLanguage(ses, code);
    }
    const TranslationRouter::Scope language(m_translations, webLanguage(ses));

    // Dispatch actions before rendering (admin only)
    const QString activePage = page.isEmpty() ? QStringLiteral("transfer") : page;
    if (m_sessionManager->isAdmin(ses))
        dispatchActions(query, activePage);

    // The Usenet page polls these instead of reloading, which keeps its
    // selection, expanded rows and scroll position.
    if (activePage == QStringLiteral("usenet")) {
        const QString part = query.queryItemValue(QStringLiteral("part"));
        if (part == QStringLiteral("list")) {
            return QHttpServerResponse(QByteArrayLiteral("text/html"),
                                       buildUsenetList(ses, query).toUtf8());
        }
        if (part == QStringLiteral("details")) {
            return QHttpServerResponse(
                QByteArrayLiteral("text/html"),
                buildUsenetDetails(ses, query.queryItemValue(QStringLiteral("id"), QUrl::FullyDecoded))
                    .toUtf8());
        }
    }

    return renderPage(activePage, ses, query);
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

QHttpServerResponse WebServer::renderPage(const QString& page, const QString& sessionId,
                                          const QUrlQuery& query)
{
    const bool isAdmin = m_sessionManager->isAdmin(sessionId);

    // Build header vars
    QHash<QString, QString> headerVars;
    headerVars[QStringLiteral("CharSet")] = QStringLiteral("UTF-8");
    headerVars[QStringLiteral("eMuleAppName")] = QStringLiteral("eMule");
    headerVars[QStringLiteral("version")] = QString(kAppVersion);
    headerVars[QStringLiteral("WebControl")] = htmlText(tr("Web Control Panel"));
    headerVars[QStringLiteral("Session")] = sessionId;
    headerVars[QStringLiteral("ses")] = sessionId;
    headerVars[QStringLiteral("HtmlLang")] = htmlText(htmlLangCode(webLanguage(sessionId)));
    headerVars[QStringLiteral("LanguageOptions")] = languageOptions(sessionId);

    // Connection status
    if (m_serverConnect) {
        headerVars[QStringLiteral("ServerName")] = htmlText(m_serverConnect->currentServer()
            ? m_serverConnect->currentServer()->name() : tr("Not connected"));
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
        QStringLiteral("debuglog"), QStringLiteral("kad"), QStringLiteral("myinfo"),
        QStringLiteral("usenet")
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
    else if (page == QStringLiteral("usenet"))
        content = buildUsenetPage(isAdmin, sessionId, query);
    else
        content = buildTransferPage(isAdmin, sessionId);

    // Inject stylesheet into header via [StyleSheet] variable, then assemble page
    headerVars[QStringLiteral("StyleSheet")] =
        m_templateEngine->section(QStringLiteral("HEADER_STYLESHEET"));
    const QString header = WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("HEADER")), headerVars);
    const QString footer = WebTemplateEngine::substitute(m_templateEngine->section(QStringLiteral("FOOTER")));

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
            lineVars[QStringLiteral("DownloadFileName")] = htmlText(file->fileName());
            lineVars[QStringLiteral("DownloadFileType")] = webFileTypeToken(file->fileName());
            lineVars[QStringLiteral("DownloadCommentIcon")] = webCommentToken(*file);
            lineVars[QStringLiteral("DownloadRating")] =
                webRatingToken(*file, m_preferences && m_preferences->indicateRatings());
            lineVars[QStringLiteral("DownloadRatingTitle")] =
                htmlText(webRatingTitle(*file));
            // The download list is tens of files and the verdict is cached after the
            // first look, so this one may read. The share cannot — see the shared page.
            const ContainerCheck& cc = file->containerCheck();
            lineVars[QStringLiteral("DownloadFake")] =
                cc.isSuspect() ? QStringLiteral("fake") : QStringLiteral("none");
            lineVars[QStringLiteral("DownloadFakeTitle")] =
                htmlText(containerWarningText(cc, file->fileName()));
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
            lineVars[QStringLiteral("1")] = htmlText(client->userName());
            lineVars[QStringLiteral("ClientSoftV")] = htmlText(client->clientSoftwareStr());
            lineVars[QStringLiteral("2")] = client->uploadFile()
                ? htmlText(client->uploadFile()->fileName()) : QStringLiteral("?");

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
                ? htmlText(client->uploadFile()->fileName()) : QString();
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
            // All three come from the servers themselves.
            lineVars[QStringLiteral("ServerName")] = htmlText(srv->name());
            lineVars[QStringLiteral("ServerAddr")] = htmlText(srv->address());
            lineVars[QStringLiteral("ServerPort")] = QString::number(srv->port());
            lineVars[QStringLiteral("ServerDescription")] = htmlText(srv->description());
            lineVars[QStringLiteral("ServerPing")] = QString::number(srv->ping());
            lineVars[QStringLiteral("ServerUsers")] = QString::number(srv->users());
            lineVars[QStringLiteral("ServerFiles")] = QString::number(srv->files());

            bool isConnected = m_serverConnect && m_serverConnect->currentServer() == srv.get();
            lineVars[QStringLiteral("ServerStatus")] = isConnected
                ? QStringLiteral("connected") : QStringLiteral("disconnected");
            lineVars[QStringLiteral("ServerStatusText")] =
                htmlText(isConnected ? tr("Connected") : tr("Disconnected"));

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
            lineVars[QStringLiteral("SharedFileName")] = htmlText(file->fileName());
            lineVars[QStringLiteral("SharedFileType")] = webFileTypeToken(file->fileName());
            lineVars[QStringLiteral("SharedCommentIcon")] = webCommentToken(*file);
            lineVars[QStringLiteral("SharedRating")] =
                webRatingToken(*file, m_preferences && m_preferences->indicateRatings());
            lineVars[QStringLiteral("SharedRatingTitle")] =
                htmlText(webRatingTitle(*file));
            // Only what the daemon's background sweep has settled: this walks the whole
            // share, so it may not open files (SharedFileList::warmContainerChecks).
            const ContainerCheck& cc = file->containerCheckIfResolved();
            lineVars[QStringLiteral("SharedFake")] =
                cc.isSuspect() ? QStringLiteral("fake") : QStringLiteral("none");
            lineVars[QStringLiteral("SharedFakeTitle")] =
                htmlText(containerWarningText(cc, file->fileName()));
            lineVars[QStringLiteral("SharedFileSize")] = formatByteSize(file->fileSize());
            lineVars[QStringLiteral("SharedFileHash")] = md4str(file->fileHash());
            lineVars[QStringLiteral("SharedRequests")] = QString::number(file->statistic.requests());
            lineVars[QStringLiteral("SharedAccepted")] = QString::number(file->statistic.accepts());
            lineVars[QStringLiteral("SharedTransferred")] = formatByteSize(file->statistic.transferred());
            lineVars[QStringLiteral("SharedPriority")] = QString::number(file->upPriority());
            // For Copy ED2K Link. A data attribute, so the name never lands in a
            // script literal, and the core's own builder encodes it.
            ED2KFileLink link;
            link.name = file->fileName();
            link.size = file->fileSize();
            std::memcpy(link.hash.data(), file->fileHash(), link.hash.size());
            lineVars[QStringLiteral("SharedED2kLink")] = htmlText(link.toLink());
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
        vars[QStringLiteral("Uptime")] = formatDuration(std::chrono::seconds(m_statistics->uptimeSecs()));
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

    vars[QStringLiteral("TxtDownload")]    = htmlText(tr("Downloads"));
    vars[QStringLiteral("TxtUpload")]      = htmlText(tr("Uploads"));
    vars[QStringLiteral("TxtConnections")] = htmlText(tr("Active Connections"));
    vars[QStringLiteral("TxtTime")]        = htmlText(tr("Time"));
    vars[QStringLiteral("KByteSec")]       = htmlText(tr("KB/s"));

    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("GRAPHS")), vars);
}

QString WebServer::buildPreferencesPage(bool /*isAdmin*/)
{
    QHash<QString, QString> vars;
    if (m_preferences) {
        vars[QStringLiteral("Nick")] = htmlText(m_preferences->nick());
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

    QString info;
    if (m_serverConnect && m_serverConnect->isConnected()) {
        if (const Server* srv = m_serverConnect->currentServer()) {
            // One arg() per line: a server name holding "%2" has to stay text.
            info += tr("Connected to: %1 (%2:%3)")
                        .arg(srv->name(), srv->address(), QString::number(srv->port()))
                    + QLatin1Char('\n');
            info += tr("Client ID: %1 (%2)")
                        .arg(QString::number(m_serverConnect->clientID()),
                             m_serverConnect->isLowID() ? tr("LowID") : tr("HighID"))
                    + QLatin1Char('\n');
            info += tr("Users: %1 | Files: %2")
                        .arg(QString::number(srv->users()), QString::number(srv->files()))
                    + QLatin1Char('\n');
            if (!srv->description().isEmpty())
                info += tr("Description: %1").arg(srv->description()) + QLatin1Char('\n');
            if (srv->ping() > 0)
                info += tr("Ping: %1 ms").arg(QString::number(srv->ping())) + QLatin1Char('\n');
        }
    } else {
        info = m_serverConnect && m_serverConnect->isConnecting()
            ? tr("Connecting...")
            : tr("Not connected to any server");
    }
    // Server-supplied text, into a <pre>.
    vars[QStringLiteral("ServerInfo")] = htmlText(info);

    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("SERVERINFO")), vars);
}

QString WebServer::buildLogPage()
{
    QHash<QString, QString> vars;
    // Log lines quote peers, servers and file names.
    vars[QStringLiteral("Log")] = htmlText(m_logProvider ? m_logProvider() : QString());
    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("LOG")), vars);
}

QString WebServer::buildDebugLogPage()
{
    QHash<QString, QString> vars;
    vars[QStringLiteral("DebugLog")] = htmlText(m_logProvider ? m_logProvider() : QString());
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
            kadStatus = tr("Running");
        else
            kadStatus = tr("Disconnected");
    } else {
        kadStatus = tr("Not available");
    }
    vars[QStringLiteral("KadStatus")] = htmlText(kadStatus);

    return WebTemplateEngine::substitute(
        m_templateEngine->section(QStringLiteral("KADDLG")), vars);
}

QString WebServer::buildMyInfoPage()
{
    QHash<QString, QString> vars;
    if (m_preferences) {
        vars[QStringLiteral("Nick")] = htmlText(m_preferences->nick());
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

// ---------------------------------------------------------------------------
// Usenet queue — shared by the REST API and the web UI
//
// Read through UsenetWebBackend, which DaemonApp installs: core may not name the
// Usenet module. Rows are the IPC's own CBOR maps, so all three surfaces share
// one field vocabulary.
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] bool queryFlag(const QString& v)
{
    return v == QLatin1String("1") || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
        || v.compare(QLatin1String("on"), Qt::CaseInsensitive) == 0;
}

/// A form body as QUrlQuery reads it: that class leaves '+' alone.
[[nodiscard]] QUrlQuery formBody(const QByteArray& body)
{
    return QUrlQuery(QString::fromUtf8(body).replace(QLatin1Char('+'), QStringLiteral("%20")));
}

/// 409 is a question ("already downloaded — force?"), 429 a moment's wait, 400 a
/// bad NZB or link, 500 a disk that refused.
[[nodiscard]] QHttpServerResponse usenetAddReply(const UsenetWebAddResult& r)
{
    if (r.ok()) {
        return jsonSuccess(QJsonObject{{QStringLiteral("id"), r.itemId},
                                       {QStringLiteral("outcome"), int(r.outcome)}});
    }

    int code = 400;
    if (r.busy)
        code = 429;
    else if (r.outcome == UsenetWebAddOutcome::AlreadyDownloaded
             || r.outcome == UsenetWebAddOutcome::Duplicate)
        code = 409;
    else if (r.outcome == UsenetWebAddOutcome::Failed)
        code = 500;

    const QJsonObject root{
        {QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code},
                                              {QStringLiteral("message"), r.error}}},
        {QStringLiteral("outcome"), int(r.outcome)},
    };
    return QHttpServerResponse(root, static_cast<QHttpServerResponse::StatusCode>(code));
}

[[nodiscard]] int usenetRowStatus(const QCborMap& row)
{
    return int(row.value(QStringLiteral("status")).toInteger());
}

/// What the Progress column shows: a post-processing stage moves no segments.
[[nodiscard]] qint64 usenetShownPercent(const QCborMap& row)
{
    return usenetStatusIsPostProcessing(usenetRowStatus(row))
               ? row.value(QStringLiteral("postPercent")).toInteger()
               : row.value(QStringLiteral("percent")).toInteger();
}

/// The Qt window's status words. The row's own statusText comes from the daemon,
/// which translates nothing.
[[nodiscard]] QString usenetStatusName(int status)
{
    switch (status) {
    case UsenetWireStatus::Queued:      return WebServer::tr("Queued");
    case UsenetWireStatus::Downloading: return WebServer::tr("Downloading");
    case UsenetWireStatus::Paused:      return WebServer::tr("Paused");
    case UsenetWireStatus::Complete:    return WebServer::tr("Complete");
    case UsenetWireStatus::Failed:      return WebServer::tr("Failed");
    case UsenetWireStatus::Verifying:   return WebServer::tr("Verifying");
    case UsenetWireStatus::Repairing:   return WebServer::tr("Repairing");
    case UsenetWireStatus::Unpacking:   return WebServer::tr("Unpacking");
    case UsenetWireStatus::Checking:    return WebServer::tr("Checking");
    default:                            return WebServer::tr("Unknown");
    }
}

/// Status plus the first reason worth reading — UsenetQueueModel's cascade.
[[nodiscard]] QString usenetStatusText(const QCborMap& row)
{
    const QString text = usenetStatusName(usenetRowStatus(row));
    const QString sep = QStringLiteral(" — ");
    if (const QString e = row.value(QStringLiteral("error")).toString(); !e.isEmpty())
        return text + sep + e;
    const QString detail = row.value(QStringLiteral("postDetail")).toString();
    if (usenetStatusIsPostProcessing(usenetRowStatus(row)) && !detail.isEmpty())
        return text + sep + detail;
    if (const QString s = row.value(QStringLiteral("stalledReason")).toString(); !s.isEmpty())
        return text + sep + s;
    return text;
}

[[nodiscard]] QString usenetHealthText(const QCborMap& row)
{
    // A dash, not "100%": -1 means nothing was ever asked.
    const qint64 h = row.value(QStringLiteral("healthPercent")).toInteger(-1);
    return h < 0 ? QStringLiteral("—") : QStringLiteral("%1%").arg(qMin(h, qint64(100)));
}

[[nodiscard]] QString usenetHealthTitle(const QCborMap& row)
{
    const qint64 h = row.value(QStringLiteral("healthPercent")).toInteger(-1);
    if (h < 0)
        return WebServer::tr("Not checked.");
    QString note = row.value(QStringLiteral("healthProbed")).toBool()
        ? WebServer::tr("%1% of this release looks obtainable.").arg(h)
        : WebServer::tr("%1% by the NZB's own article counts. No server was asked.").arg(h);
    const qint64 missing = row.value(QStringLiteral("healthMissingBytes")).toInteger();
    const qint64 recovery = row.value(QStringLiteral("healthRecoveryBytes")).toInteger();
    if (missing > 0 && recovery >= missing)
        note += QLatin1Char('\n') + WebServer::tr("The PAR2 recovery volumes should cover the shortfall.");
    return note;
}

[[nodiscard]] QString usenetNameTitle(const QCborMap& row)
{
    const QString name = row.value(QStringLiteral("name")).toString();
    const bool has = row.value(QStringLiteral("hasPassword")).toBool();
    // The Qt model's own sentences, so they share its translations.
    if (row.value(QStringLiteral("passwordRequired")).toBool()) {
        return has ? WebServer::tr("%1\nThe password for this release did not work. "
                                   "Right-click to set a different one.").arg(name)
                   : WebServer::tr("%1\nThis release is password-protected. "
                                   "Right-click to set its password.").arg(name);
    }
    if (const qint64 m = row.value(QStringLiteral("missingSegments")).toInteger(); m > 0) {
        return WebServer::tr("%1\n%n article(s) could not be found on any server", nullptr, int(m))
                   .arg(name);
    }
    if (has)
        return WebServer::tr("%1\nA password is set for this release.").arg(name);
    return name;
}

/// The `t_*` sprite the Transfer page shows for the nearest state.
[[nodiscard]] QString usenetStatusIcon(int status)
{
    switch (status) {
    case UsenetWireStatus::Downloading: return QStringLiteral("t_downloading");
    case UsenetWireStatus::Paused:      return QStringLiteral("t_paused");
    case UsenetWireStatus::Complete:    return QStringLiteral("t_complete");
    case UsenetWireStatus::Failed:      return QStringLiteral("t_error");
    case UsenetWireStatus::Verifying:
    case UsenetWireStatus::Repairing:
    case UsenetWireStatus::Unpacking:   return QStringLiteral("t_completing");
    case UsenetWireStatus::Checking:    return QStringLiteral("t_connecting");
    default:                            return QStringLiteral("t_waiting");
    }
}

/// What the page script keys its menu and double-click on.
[[nodiscard]] QString usenetStatusKey(int status)
{
    switch (status) {
    case UsenetWireStatus::Queued:      return QStringLiteral("queued");
    case UsenetWireStatus::Downloading: return QStringLiteral("downloading");
    case UsenetWireStatus::Paused:      return QStringLiteral("paused");
    case UsenetWireStatus::Complete:    return QStringLiteral("complete");
    case UsenetWireStatus::Failed:      return QStringLiteral("failed");
    case UsenetWireStatus::Checking:    return QStringLiteral("checking");
    default:                            return QStringLiteral("post");
    }
}

/// Bold while working, red failed, grey paused, blue post-processing.
[[nodiscard]] QString usenetRowClass(int status)
{
    if (status == UsenetWireStatus::Failed)
        return QStringLiteral("un-failed");
    if (status == UsenetWireStatus::Paused)
        return QStringLiteral("un-paused");
    if (usenetStatusIsPostProcessing(status))
        return QStringLiteral("un-post");
    if (status == UsenetWireStatus::Downloading)
        return QStringLiteral("un-active");
    return {};
}

/// The release's payload: the biggest published file that is not a recovery
/// volume. From publishedFiles, never files[].finalPath — unpacking leaves that
/// empty on purpose, which is the Qt window's Open Folder defect.
[[nodiscard]] QCborMap usenetPayload(const QCborMap& row)
{
    QCborMap best;
    for (const auto& v : row.value(QStringLiteral("publishedFiles")).toArray()) {
        const QCborMap pf = v.toMap();
        if (pf.value(QStringLiteral("name")).toString().endsWith(QLatin1String(".par2"),
                                                                Qt::CaseInsensitive))
            continue;
        if (best.isEmpty()
            || pf.value(QStringLiteral("size")).toInteger()
                   > best.value(QStringLiteral("size")).toInteger())
            best = pf;
    }
    return best;
}

/// The published entry for one NZB file, matched by path.
[[nodiscard]] QCborMap usenetPublishedFor(const QCborMap& row, const QString& finalPath)
{
    if (finalPath.isEmpty())
        return {};
    for (const auto& v : row.value(QStringLiteral("publishedFiles")).toArray()) {
        const QCborMap pf = v.toMap();
        if (pf.value(QStringLiteral("path")).toString() == finalPath)
            return pf;
    }
    return {};
}

/// The largest previewable file, or -1 and the first note saying why not.
[[nodiscard]] std::pair<int, QString> usenetPreviewTarget(const QCborMap& row)
{
    int bestIndex = -1;
    qint64 bestSize = -1;
    QString note;
    for (const auto& v : row.value(QStringLiteral("files")).toArray()) {
        const QCborMap f = v.toMap();
        if (note.isEmpty())
            note = f.value(QStringLiteral("previewNote")).toString();
        if (!f.value(QStringLiteral("previewable")).toBool())
            continue;
        if (const qint64 size = f.value(QStringLiteral("size")).toInteger(); size > bestSize) {
            bestSize = size;
            bestIndex = int(f.value(QStringLiteral("index")).toInteger(-1));
        }
    }
    return {bestIndex, bestIndex >= 0 ? QString() : note};
}

/// Whether Open plays rather than downloads — the incoming route's own split.
[[nodiscard]] QString usenetPlayFlag(const QString& name)
{
    const QString type = webFileTypeToken(name);
    return (type == QLatin1String("video") || type == QLatin1String("audio"))
               ? QStringLiteral("1") : QStringLiteral("0");
}

/// MFC colours the tab text (SetTabTextColor), and so does CategoryTabBar.
[[nodiscard]] QString categoryCssColor(quint32 color)
{
    if (color == kCategoryColorAuto)
        return {};
    return QStringLiteral("color:#%1").arg(color & 0xFFFFFFu, 6, 16, QLatin1Char('0'));
}

/// One value, or "first (and N other(s))" when a release's files disagree.
[[nodiscard]] QString collapseValues(const QStringList& values)
{
    QStringList distinct;
    for (const QString& v : values) {
        if (!v.isEmpty() && !distinct.contains(v))
            distinct << v;
    }
    if (distinct.size() <= 1)
        return distinct.value(0);
    return WebServer::tr("%1 (and %2 other(s))")
               .arg(distinct.first(), QString::number(distinct.size() - 1));
}

/// A JS string literal that is also safe inside a template and a <script>.
[[nodiscard]] QString jsString(const QString& s)
{
    QString json = QString::fromUtf8(
        QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact));
    json = json.mid(1, json.size() - 2);
    return json.replace(QLatin1Char('['), QStringLiteral("\\u005b"))
               .replace(QLatin1Char('<'), QStringLiteral("\\u003c"));
}

/// Raw values where the column is a number, like the Qt model's UserRole.
[[nodiscard]] bool usenetLess(const QCborMap& a, const QCborMap& b, const QString& column)
{
    const auto num = [](const QCborMap& m, QLatin1String key) {
        return m.value(key).toInteger();
    };
    if (column == QLatin1String("size"))
        return num(a, QLatin1String("totalBytes")) < num(b, QLatin1String("totalBytes"));
    if (column == QLatin1String("progress"))
        return usenetShownPercent(a) < usenetShownPercent(b);
    if (column == QLatin1String("status"))
        return usenetStatusRank(usenetRowStatus(a)) < usenetStatusRank(usenetRowStatus(b));
    if (column == QLatin1String("speed"))
        return num(a, QLatin1String("speed")) < num(b, QLatin1String("speed"));
    if (column == QLatin1String("remaining")) {
        return num(a, QLatin1String("totalBytes")) - num(a, QLatin1String("decodedBytes"))
             < num(b, QLatin1String("totalBytes")) - num(b, QLatin1String("decodedBytes"));
    }
    if (column == QLatin1String("priority"))
        return num(a, QLatin1String("priority")) < num(b, QLatin1String("priority"));
    if (column == QLatin1String("health")) {
        return a.value(QStringLiteral("healthPercent")).toInteger(-1)
             < b.value(QStringLiteral("healthPercent")).toInteger(-1);
    }
    if (column == QLatin1String("category"))
        return num(a, QLatin1String("category")) < num(b, QLatin1String("category"));
    return a.value(QStringLiteral("name")).toString().compare(
               b.value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
}

[[nodiscard]] QJsonObject rowJsonById(const QList<QCborMap>& rows, const QString& id)
{
    for (const QCborMap& row : rows) {
        if (row.value(QStringLiteral("id")).toString() == id)
            return row.toJsonObject();
    }
    return QJsonObject{{QStringLiteral("id"), id}};
}

[[nodiscard]] QString usenetUnavailableText()
{
    return WebServer::tr("Usenet engine unavailable");
}

[[nodiscard]] QString usenetNotFoundText()
{
    return WebServer::tr("Usenet item not found");
}

/// The Qt window's priority names.
[[nodiscard]] QString usenetPriorityName(int level)
{
    switch (level) {
    case 2:  return WebServer::tr("Very high");
    case 1:  return WebServer::tr("High");
    case -1: return WebServer::tr("Low");
    case -2: return WebServer::tr("Very low");
    default: return WebServer::tr("Normal");
    }
}

} // namespace

bool WebServer::usenetAvailable() const
{
    return m_usenetBackend && m_usenetBackend->available();
}

WebServer::WebSessionCheck WebServer::webSession(const QUrlQuery& query)
{
    WebSessionCheck s;
    s.id = query.queryItemValue(QStringLiteral("ses"));
    if (!m_sessionManager || s.id.isEmpty() || !m_sessionManager->isValid(s.id))
        return s;
    s.valid = true;
    s.admin = m_sessionManager->isAdmin(s.id);
    return s;
}

QList<QCborMap> WebServer::usenetRows(int category)
{
    QList<QCborMap> rows;
    if (!usenetAvailable())
        return rows;

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    QSet<QString> present;
    for (const auto& v : m_usenetBackend->queue()) {
        QCborMap row = v.toMap();
        const QString id = row.value(QStringLiteral("id")).toString();
        present.insert(id);

        // Sampled on every request, whichever browser or script asked; the
        // sampler ignores readings closer together than its window.
        const qint64 rate = m_usenetRates[id].update(
            row.value(QStringLiteral("decodedBytes")).toInteger(), nowMs);
        row.insert(QStringLiteral("speed"),
                   usenetRowStatus(row) == UsenetWireStatus::Downloading ? rate : qint64(0));

        if (category > 0 && row.value(QStringLiteral("category")).toInteger() != category)
            continue;
        rows.append(row);
    }

    for (auto it = m_usenetRates.begin(); it != m_usenetRates.end();) {
        if (present.contains(it.key()))
            ++it;
        else
            it = m_usenetRates.erase(it);
    }
    return rows;
}

QJsonObject WebServer::usenetStatsJson(const QList<QCborMap>& rows) const
{
    int active = 0;
    qint64 total = 0;
    qint64 done = 0;
    QString stalled;
    for (const QCborMap& row : rows) {
        const int status = usenetRowStatus(row);
        // Post-processing counts as active: "0 active" during a repair reads as a stall.
        if (status == UsenetWireStatus::Downloading || status == UsenetWireStatus::Queued
            || usenetStatusIsPostProcessing(status)) {
            ++active;
        }
        total += row.value(QStringLiteral("totalBytes")).toInteger();
        done += row.value(QStringLiteral("decodedBytes")).toInteger();
        // Queue-level, so every stalled row carries the same sentence.
        if (stalled.isEmpty())
            stalled = row.value(QStringLiteral("stalledReason")).toString();
    }

    QJsonObject out = m_usenetBackend ? m_usenetBackend->downloadSplit().toJsonObject()
                                      : QJsonObject{};
    out.insert(QStringLiteral("count"), int(rows.size()));
    out.insert(QStringLiteral("active"), active);
    out.insert(QStringLiteral("totalBytes"), total);
    out.insert(QStringLiteral("decodedBytes"), done);
    out.insert(QStringLiteral("percent"), total > 0 ? int(done * 100 / total) : 0);
    out.insert(QStringLiteral("stalledReason"), stalled);
    return out;
}

WebServer::UsenetOpResult WebServer::applyUsenetItemOp(const QString& op, const QString& id,
                                                       const QString& value)
{
    if (!usenetAvailable())
        return {503, usenetUnavailableText()};
    if (!m_usenetBackend->contains(id))
        return {404, usenetNotFoundText()};

    if (op == QLatin1String("pause")) {
        if (!m_usenetBackend->pause(id))
            return {409, tr("Only a queued or downloading release can be paused")};
    } else if (op == QLatin1String("resume")) {
        if (!m_usenetBackend->resume(id))
            return {409, tr("Only a paused or failed release can be resumed")};
    } else if (op == QLatin1String("check")) {
        if (const QString why = m_usenetBackend->recheck(id); !why.isEmpty())
            return {409, why};
    } else if (op == QLatin1String("remove") || op == QLatin1String("removedelete")) {
        if (!m_usenetBackend->remove(id, op == QLatin1String("removedelete")))
            return {404, usenetNotFoundText()};
    } else if (op == QLatin1String("priority")) {
        bool ok = false;
        const int priority = value.toInt(&ok);
        if (!ok || priority < -2 || priority > 2)
            return {400, tr("priority must be a number from -2 to 2")};
        m_usenetBackend->setPriority(id, priority);
    } else if (op == QLatin1String("category")) {
        bool ok = false;
        const int category = value.toInt(&ok);
        if (!ok || !m_usenetBackend->categoryExists(category))
            return {400, tr("Unknown category")};
        m_usenetBackend->setCategory(id, category);
    } else if (op == QLatin1String("password")) {
        // Not trimmed: a passphrase is opaque, and empty is a deliberate clear.
        m_usenetBackend->setPassword(id, value);
    } else if (op == QLatin1String("skip") || op == QLatin1String("unskip")) {
        // File indices, comma-separated; the backend widens them to archive sets.
        QList<int> files;
        for (const QString& part : value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            bool ok = false;
            const int index = part.trimmed().toInt(&ok);
            if (!ok || index < 0)
                return {400, tr("files must be a list of file numbers")};
            files.append(index);
        }
        if (files.isEmpty())
            return {400, tr("files must be a list of file numbers")};
        if (const QString why =
                m_usenetBackend->setFilesSkipped(id, files, op == QLatin1String("skip"));
            !why.isEmpty()) {
            return {409, why};
        }
    } else {
        return {400, tr("Unknown action")};
    }
    return {};
}

// --- REST ------------------------------------------------------------------

QHttpServerResponse WebServer::handleRestUsenetList(const QHttpServerRequest& req)
{
    if (!usenetAvailable())
        return jsonError(503, usenetUnavailableText());

    const int category =
        QUrlQuery(req.query()).queryItemValue(QStringLiteral("category")).toInt();
    QJsonArray arr;
    for (const QCborMap& row : usenetRows(category))
        arr.append(row.toJsonObject());
    return jsonSuccess(arr);
}

QHttpServerResponse WebServer::handleRestUsenetStats()
{
    if (!usenetAvailable())
        return jsonError(503, usenetUnavailableText());
    return jsonSuccess(usenetStatsJson(usenetRows()));
}

QHttpServerResponse WebServer::handleRestUsenetItem(const QString& id)
{
    if (!usenetAvailable())
        return jsonError(503, usenetUnavailableText());

    QCborMap details = m_usenetBackend->details(id);
    if (details.isEmpty())
        return jsonError(404, usenetNotFoundText());

    // A rate only exists across requests; report the last one sampled.
    const auto it = m_usenetRates.constFind(id);
    const bool downloading = usenetRowStatus(details) == UsenetWireStatus::Downloading;
    details.insert(QStringLiteral("speed"),
                   it != m_usenetRates.cend() && downloading ? it->rate : qint64(0));
    return jsonSuccess(details.toJsonObject());
}

QHttpServerResponse WebServer::handleRestUsenetEntries(const QString& id,
                                                       const QString& fileIndexText)
{
    if (!usenetAvailable())
        return jsonError(503, usenetUnavailableText());

    bool ok = false;
    const int fileIndex = fileIndexText.toInt(&ok);
    if (!ok || fileIndex < 0)
        return jsonError(400, QStringLiteral("Invalid file index"));
    if (!m_usenetBackend->contains(id))
        return jsonError(404, usenetNotFoundText());
    return jsonSuccess(m_usenetBackend->archiveEntries(id, fileIndex).toJsonObject());
}

QHttpServerResponse WebServer::handleRestUsenetItemOp(const QString& id, const QString& op)
{
    if (op != QLatin1String("pause") && op != QLatin1String("resume")
        && op != QLatin1String("check"))
        return jsonError(404, QStringLiteral("Unknown Usenet action"));

    const UsenetOpResult r = applyUsenetItemOp(op, id, {});
    if (r.code != 200)
        return jsonError(r.code, r.message);
    return jsonSuccess(rowJsonById(usenetRows(), id));
}

QHttpServerResponse WebServer::handleRestUsenetPatch(const QString& id,
                                                     const QHttpServerRequest& req)
{
    if (!usenetAvailable())
        return jsonError(503, usenetUnavailableText());

    const QJsonDocument doc = QJsonDocument::fromJson(req.body());
    if (!doc.isObject())
        return jsonError(400, QStringLiteral("Invalid JSON body"));
    if (!m_usenetBackend->contains(id))
        return jsonError(404, usenetNotFoundText());

    // Validate every field before changing any, so a bad one leaves the release
    // exactly as it was.
    const QJsonObject body = doc.object();
    QList<std::pair<QString, QString>> ops;
    if (body.contains(QStringLiteral("priority"))) {
        const QJsonValue v = body.value(QStringLiteral("priority"));
        const int p = v.toInt(99);
        if (!v.isDouble() || p < -2 || p > 2)
            return jsonError(400, tr("priority must be a number from -2 to 2"));
        ops.append({QStringLiteral("priority"), QString::number(p)});
    }
    if (body.contains(QStringLiteral("category"))) {
        const QJsonValue v = body.value(QStringLiteral("category"));
        if (!v.isDouble() || !m_usenetBackend->categoryExists(v.toInt(-1)))
            return jsonError(400, tr("Unknown category"));
        ops.append({QStringLiteral("category"), QString::number(v.toInt())});
    }
    if (body.contains(QStringLiteral("password"))) {
        const QJsonValue v = body.value(QStringLiteral("password"));
        if (!v.isString())
            return jsonError(400, QStringLiteral("password must be a string"));
        ops.append({QStringLiteral("password"), v.toString()});
    }
    for (const auto& [field, op] : {std::pair{QStringLiteral("skipFiles"), QStringLiteral("skip")},
                                    std::pair{QStringLiteral("unskipFiles"),
                                              QStringLiteral("unskip")}}) {
        if (!body.contains(field))
            continue;
        const QJsonValue v = body.value(field);
        const QJsonArray indices = v.toArray();
        bool valid = v.isArray() && !indices.isEmpty();
        QStringList parts;
        for (const QJsonValue& e : indices) {
            if (!e.isDouble() || e.toInt(-1) < 0 || e.toDouble() != double(e.toInt(-1))) {
                valid = false;
                break;
            }
            parts.append(QString::number(e.toInt()));
        }
        if (!valid)
            return jsonError(400, QStringLiteral("%1 must be a non-empty array of file indices")
                                      .arg(field));
        ops.append({op, parts.join(QLatin1Char(','))});
    }
    if (ops.isEmpty())
        return jsonError(400, QStringLiteral("Nothing to change: send priority, category, "
                                             "password, skipFiles or unskipFiles"));

    for (const auto& [op, value] : std::as_const(ops)) {
        if (const UsenetOpResult r = applyUsenetItemOp(op, id, value); r.code != 200)
            return jsonError(r.code, r.message);
    }
    return jsonSuccess(rowJsonById(usenetRows(), id));
}

QHttpServerResponse WebServer::handleRestUsenetDelete(const QString& id,
                                                      const QHttpServerRequest& req)
{
    const bool deleteFiles =
        queryFlag(QUrlQuery(req.query()).queryItemValue(QStringLiteral("deleteFiles")));
    const UsenetOpResult r = applyUsenetItemOp(
        deleteFiles ? QStringLiteral("removedelete") : QStringLiteral("remove"), id, {});
    if (r.code != 200)
        return jsonError(r.code, r.message);
    return jsonSuccess(QJsonObject{{QStringLiteral("removed"), true},
                                   {QStringLiteral("deletedFiles"), deleteFiles}});
}

QHttpServerResponse WebServer::handleRestUsenetCategoryOp(const QString& categoryText,
                                                          const QString& op)
{
    if (!usenetAvailable())
        return jsonError(503, usenetUnavailableText());

    UsenetWebCategoryAction action = UsenetWebCategoryAction::Pause;
    if (op == QLatin1String("pause"))
        action = UsenetWebCategoryAction::Pause;
    else if (op == QLatin1String("resume"))
        action = UsenetWebCategoryAction::Resume;
    else if (op == QLatin1String("cancel"))
        action = UsenetWebCategoryAction::Cancel;
    else
        return jsonError(404, QStringLiteral("Unknown category action"));

    bool ok = false;
    const int category = categoryText.toInt(&ok);
    const int acted = ok ? m_usenetBackend->applyCategoryAction(category, action) : -1;
    if (acted < 0)
        return jsonError(400, tr("Unknown category"));
    return jsonSuccess(QJsonObject{{QStringLiteral("affected"), acted}});
}

QHttpServerResponse WebServer::handleUsenetEngineOp(bool paused)
{
    if (!usenetAvailable())
        return jsonError(503, usenetUnavailableText());
    m_usenetBackend->setEnginePaused(paused);
    return jsonSuccess(QJsonObject{{QStringLiteral("paused"), paused}});
}

QFuture<QHttpServerResponse> WebServer::handleUsenetAdd(const QHttpServerRequest& req,
                                                        bool restApi)
{
    if (!usenetAvailable())
        return finishedResponse(jsonError(503, usenetUnavailableText()));

    const QByteArray contentType =
        req.headers().combinedValue(QByteArrayLiteral("Content-Type")).toLower();
    const QByteArray body = req.body();

    // Options arrive three ways: a JSON body (REST, for a URL), a form body (the
    // page's URL adds, so a passphrase stays out of the URL), or the query string
    // beside a raw .nzb body.
    UsenetWebAddOptions options;
    QString url;
    QString name;
    QString categoryText;
    QString priorityText;

    if (restApi && contentType.startsWith("application/json")) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject())
            return finishedResponse(jsonError(400, QStringLiteral("Invalid JSON body")));
        const QJsonObject o = doc.object();
        url = o.value(QStringLiteral("url")).toString();
        name = o.value(QStringLiteral("name")).toString();
        options.force = o.value(QStringLiteral("force")).toBool();
        options.password = o.value(QStringLiteral("password")).toString();
        options.paused = o.value(QStringLiteral("paused")).toBool();
        if (o.contains(QStringLiteral("category")))
            categoryText = QString::number(o.value(QStringLiteral("category")).toInt(-1));
        if (o.contains(QStringLiteral("priority")))
            priorityText = QString::number(o.value(QStringLiteral("priority")).toInt(99));
        if (url.isEmpty()) {
            return finishedResponse(jsonError(400, QStringLiteral(
                "A JSON body must carry \"url\"; post the .nzb itself as the body to add a file")));
        }
    } else {
        const bool form = contentType.startsWith("application/x-www-form-urlencoded");
        const QUrlQuery fields = form ? formBody(body) : QUrlQuery(req.query());
        const auto field = [&fields](const char* key) {
            return fields.queryItemValue(QLatin1String(key), QUrl::FullyDecoded);
        };
        url = field("url");
        name = field("name");
        options.force = queryFlag(field("force"));
        options.password = field("password");
        options.paused = queryFlag(field("paused"));
        categoryText = field("category");
        priorityText = field("priority");

        // Beside a raw body the passphrase rides a header, not the URL.
        const QByteArray pwHeader = req.headers().combinedValue(QByteArrayLiteral("X-Nzb-Password"));
        if (options.password.isEmpty() && !pwHeader.isEmpty())
            options.password = QUrl::fromPercentEncoding(pwHeader);

        if (url.isEmpty() && (form || body.isEmpty())) {
            return finishedResponse(
                jsonError(400, tr("Post the .nzb as the request body, or give a url")));
        }
    }

    if (!categoryText.isEmpty()) {
        bool ok = false;
        options.category = categoryText.toInt(&ok);
        if (!ok || !m_usenetBackend->categoryExists(options.category))
            return finishedResponse(jsonError(400, tr("Unknown category")));
    }
    if (!priorityText.isEmpty()) {
        bool ok = false;
        options.priority = priorityText.toInt(&ok);
        if (!ok || options.priority < -2 || options.priority > 2) {
            return finishedResponse(
                jsonError(400, tr("priority must be a number from -2 to 2")));
        }
    }

    if (url.isEmpty())
        return finishedResponse(usenetAddReply(m_usenetBackend->addNzb(body, name, options)));

    // The fetch takes seconds and the reply goes when it lands. The callback owns
    // only the promise, so a web-server restart in the meantime is harmless.
    auto promise = std::make_shared<QPromise<QHttpServerResponse>>();
    promise->start();
    QFuture<QHttpServerResponse> future = promise->future();
    m_usenetBackend->addNzbUrl(url, options, [promise](const UsenetWebAddResult& r) {
        promise->addResult(usenetAddReply(r));
        promise->finish();
    });
    return future;
}

// --- Web UI (session) --------------------------------------------------------

QHttpServerResponse WebServer::handleWebUsenetAction(const QHttpServerRequest& req)
{
    const WebSessionCheck ses = webSession(QUrlQuery(req.query()));
    const TranslationRouter::Scope language(m_translations, webLanguage(ses.id));
    if (!ses.valid)
        return jsonError(401, tr("Session expired — log in again"));
    if (!ses.admin)
        return jsonError(403, tr("Guests cannot change downloads"));
    if (!usenetAvailable())
        return jsonError(503, usenetUnavailableText());

    const QUrlQuery form = formBody(req.body());
    const QString op = form.queryItemValue(QStringLiteral("op"));
    const QString value = form.queryItemValue(QStringLiteral("v"), QUrl::FullyDecoded);

    if (op.startsWith(QLatin1String("cat"))) {
        // catpause / catresume / catcancel, with the category index in `v`.
        return handleRestUsenetCategoryOp(value, op.mid(3));
    }
    if (op == QLatin1String("enginepause") || op == QLatin1String("engineresume"))
        return handleUsenetEngineOp(op == QLatin1String("enginepause"));

    const QStringList ids = form.allQueryItemValues(QStringLiteral("id"), QUrl::FullyDecoded);
    if (ids.isEmpty())
        return jsonError(400, tr("Nothing selected"));

    int done = 0;
    UsenetOpResult firstFailure;
    for (const QString& id : ids) {
        const UsenetOpResult r = applyUsenetItemOp(op, id, value);
        if (r.code == 200)
            ++done;
        else if (firstFailure.code == 200)
            firstFailure = r;
    }

    // A batch where some rows refused — pausing a finished release beside a
    // running one — still did what it could, the way the Qt toolbar does.
    if (done == 0)
        return jsonError(firstFailure.code, firstFailure.message);
    return jsonSuccess(QJsonObject{{QStringLiteral("done"), done},
                                   {QStringLiteral("message"), firstFailure.message}});
}

QHttpServerResponse WebServer::handleWebUsenetEntries(const QHttpServerRequest& req)
{
    const QUrlQuery query(req.query());
    const WebSessionCheck ses = webSession(query);
    const TranslationRouter::Scope language(m_translations, webLanguage(ses.id));
    if (!ses.valid)
        return jsonError(401, tr("Session expired — log in again"));
    return handleRestUsenetEntries(query.queryItemValue(QStringLiteral("id"), QUrl::FullyDecoded),
                                   query.queryItemValue(QStringLiteral("file")));
}

// --- Page ------------------------------------------------------------------------

QString WebServer::buildUsenetPage(bool isAdmin, const QString& sessionId, const QUrlQuery& query)
{
    if (!usenetAvailable())
        return WebTemplateEngine::substitute(m_templateEngine->section(QStringLiteral("USENET_UNAVAILABLE")));

    const int currentCat = query.queryItemValue(QStringLiteral("cat")).toInt();
    const QList<DownloadCategory> cats =
        m_preferences ? m_preferences->categories() : QList<DownloadCategory>{};
    const int catCount = qMax(1, int(cats.size()));

    QString tabs;
    QString catOptions;
    QStringList catNames;
    for (int i = 0; i < catCount; ++i) {
        const bool known = i < cats.size();
        // Index 0 is "All" on a tab, and "No category" as a thing to assign.
        const QString title = i == 0 ? tr("All") : cats.at(i).displayName();
        const QString assign = i == 0 ? tr("No category") : title;
        tabs += QStringLiteral("<a class=\"un-cattab%1\" href=\"?ses=%2&amp;w=usenet&amp;cat=%3\" "
                               "data-cat=\"%3\" title=\"%4\" style=\"%5\">%6</a>")
                    .arg(i == currentCat ? QStringLiteral(" active") : QString(), sessionId)
                    .arg(i)
                    .arg(known ? htmlText(cats.at(i).comment) : QString(),
                         known && i > 0 ? categoryCssColor(cats.at(i).color) : QString(),
                         htmlText(title));
        catOptions += QStringLiteral("<option value=\"%1\">%2</option>").arg(i).arg(htmlText(assign));
        catNames << jsString(assign);
    }

    QString prioOptions;
    QStringList levels;
    for (const int level : kUsenetPriorityLevels) {
        prioOptions += QStringLiteral("<option value=\"%1\"%2>%3</option>")
                           .arg(level)
                           .arg(level == 0 ? QStringLiteral(" selected") : QString(),
                                htmlText(usenetPriorityName(level)));
        levels << QStringLiteral("{p:%1,n:%2}").arg(level).arg(jsString(usenetPriorityName(level)));
    }

    // Whitelisted, because it lands inside a script literal.
    QString sort = query.queryItemValue(QStringLiteral("sort"));
    static const QRegularExpression word(QStringLiteral("^[a-z]{0,12}$"));
    if (!word.match(sort).hasMatch())
        sort.clear();

    QHash<QString, QString> vars;
    vars[QStringLiteral("Session")] = sessionId;
    vars[QStringLiteral("StreamToken")] = m_streamToken;
    // Passed on to the incoming pages, which have no session of their own.
    vars[QStringLiteral("WebLang")] = WebTemplateEngine::jsEscape(webLanguage(sessionId));
    // The page script formats archive entry sizes itself, in formatByteSize's units.
    vars[QStringLiteral("UsenetUnitsJson")] = QStringLiteral("{\"list\":[%1]}").arg(QStringList{
        jsString(QCoreApplication::translate("Units", "Bytes")),
        jsString(QCoreApplication::translate("Units", "KB")),
        jsString(QCoreApplication::translate("Units", "MB")),
        jsString(QCoreApplication::translate("Units", "GB")),
        jsString(QCoreApplication::translate("Units", "TB")),
    }.join(QLatin1Char(',')));
    vars[QStringLiteral("IsAdmin")] = isAdmin ? QStringLiteral("1") : QStringLiteral("0");
    vars[QStringLiteral("UsenetCategoryTabs")] = tabs;
    vars[QStringLiteral("UsenetCategoryOptions")] = catOptions;
    vars[QStringLiteral("UsenetPriorityOptions")] = prioOptions;
    vars[QStringLiteral("UsenetCategoriesJson")] = QStringLiteral("{\"list\":") + QLatin1Char('[')
        + catNames.join(QLatin1Char(',')) + QStringLiteral("]}");
    vars[QStringLiteral("UsenetLevelsJson")] = QStringLiteral("{\"list\":") + QLatin1Char('[')
        + levels.join(QLatin1Char(',')) + QStringLiteral("]}");
    vars[QStringLiteral("UsenetCat")] = QString::number(currentCat);
    vars[QStringLiteral("UsenetSort")] = sort;
    vars[QStringLiteral("UsenetDesc")] =
        query.queryItemValue(QStringLiteral("desc")) == QLatin1String("1") ? QStringLiteral("1")
                                                                           : QStringLiteral("0");
    // AddNzbUrlDialog::kMaxUrls — the same cap on a pasted list.
    vars[QStringLiteral("UsenetMaxUrls")] = QStringLiteral("20");
    vars[QStringLiteral("UsenetList")] = buildUsenetList(sessionId, query);
    return WebTemplateEngine::substitute(m_templateEngine->section(QStringLiteral("USENET")), vars);
}

QString WebServer::buildUsenetList(const QString& sessionId, const QUrlQuery& query)
{
    if (!usenetAvailable())
        return QStringLiteral("<!--usenet-list--><div class=\"message\">%1</div>")
                   .arg(htmlText(usenetUnavailableText()));

    const int category = query.queryItemValue(QStringLiteral("cat")).toInt();
    const QString sort = query.queryItemValue(QStringLiteral("sort"));
    const bool desc = query.queryItemValue(QStringLiteral("desc")) == QLatin1String("1");

    // All of them for the summary, which the Qt window also counts over every tab.
    const QList<QCborMap> all = usenetRows();
    QList<QCborMap> rows;
    for (const QCborMap& row : all) {
        if (category <= 0 || row.value(QStringLiteral("category")).toInteger() == category)
            rows.append(row);
    }
    if (!sort.isEmpty()) {
        std::stable_sort(rows.begin(), rows.end(), [&sort, desc](const QCborMap& a, const QCborMap& b) {
            return desc ? usenetLess(b, a, sort) : usenetLess(a, b, sort);
        });
    }

    const QList<DownloadCategory> cats =
        m_preferences ? m_preferences->categories() : QList<DownloadCategory>{};
    const QString lineTmpl = m_templateEngine->section(QStringLiteral("USENET_LINE"));
    const QString fileTmpl = m_templateEngine->section(QStringLiteral("USENET_FILE_LINE"));

    QString lines;
    for (const QCborMap& row : std::as_const(rows)) {
        const QString id = row.value(QStringLiteral("id")).toString();
        const int status = usenetRowStatus(row);
        const qint64 total = row.value(QStringLiteral("totalBytes")).toInteger();
        const qint64 decoded = row.value(QStringLiteral("decodedBytes")).toInteger();
        const auto [previewFile, previewNote] = usenetPreviewTarget(row);
        const QCborMap payload = usenetPayload(row);
        const QCborArray files = row.value(QStringLiteral("files")).toArray();

        QString fileLines;
        for (const auto& fv : files) {
            const QCborMap f = fv.toMap();
            const QString fname = f.value(QStringLiteral("name")).toString();
            const QString finalPath = f.value(QStringLiteral("finalPath")).toString();
            const qint64 missing = f.value(QStringLiteral("missingSegments")).toInteger();
            const qint64 pct = f.value(QStringLiteral("percent")).toInteger();

            QHash<QString, QString> v;
            v[QStringLiteral("UsenetId")] = htmlText(id);
            v[QStringLiteral("UsenetFileIndex")] =
                QString::number(f.value(QStringLiteral("index")).toInteger(-1));
            v[QStringLiteral("UsenetFileName")] = htmlText(fname);
            v[QStringLiteral("UsenetFileType")] = webFileTypeToken(fname);
            v[QStringLiteral("UsenetFileSize")] = formatByteSize(f.value(QStringLiteral("size")).toInteger());
            v[QStringLiteral("UsenetFilePercent")] = QString::number(pct);
            v[QStringLiteral("UsenetFileStatus")] =
                missing > 0 ? htmlText(tr("%n article(s) missing", nullptr, int(missing)))
                            : (pct >= 100 ? htmlText(tr("Complete")) : QString());
            v[QStringLiteral("UsenetFileClass")] =
                f.value(QStringLiteral("isPar2")).toBool() ? QStringLiteral("un-par2") : QString();
            v[QStringLiteral("UsenetFileTitle")] = htmlText(finalPath);
            v[QStringLiteral("UsenetFilePreview")] =
                f.value(QStringLiteral("previewable")).toBool() ? QStringLiteral("1") : QStringLiteral("0");
            v[QStringLiteral("UsenetFileNote")] = htmlText(f.value(QStringLiteral("previewNote")).toString());
            v[QStringLiteral("UsenetFileOpen")] =
                htmlText(usenetPublishedFor(row, finalPath).value(QStringLiteral("relPath")).toString());
            v[QStringLiteral("UsenetFileHasFinal")] =
                finalPath.isEmpty() ? QStringLiteral("0") : QStringLiteral("1");
            v[QStringLiteral("UsenetFilePlay")] = usenetPlayFlag(fname);
            fileLines += WebTemplateEngine::substitute(fileTmpl, v);
        }

        const int cat = int(row.value(QStringLiteral("category")).toInteger());
        const bool hasPw = row.value(QStringLiteral("hasPassword")).toBool();
        const qint64 speed = row.value(QStringLiteral("speed")).toInteger();

        QHash<QString, QString> v;
        v[QStringLiteral("UsenetId")] = htmlText(id);
        v[QStringLiteral("UsenetName")] = htmlText(row.value(QStringLiteral("name")).toString());
        v[QStringLiteral("UsenetNameTitle")] = htmlText(usenetNameTitle(row));
        v[QStringLiteral("UsenetLock")] =
            (hasPw || row.value(QStringLiteral("passwordRequired")).toBool())
                ? QStringLiteral("<span class=\"un-lock\">&#128274;</span>") : QString();
        v[QStringLiteral("UsenetStatusIcon")] = usenetStatusIcon(status);
        v[QStringLiteral("UsenetStatusKey")] = usenetStatusKey(status);
        v[QStringLiteral("UsenetRowClass")] = usenetRowClass(status);
        v[QStringLiteral("UsenetSize")] = formatByteSize(total);
        v[QStringLiteral("UsenetPercent")] =
            QString::number(qBound(qint64(0), usenetShownPercent(row), qint64(100)));
        v[QStringLiteral("UsenetStatus")] = htmlText(usenetStatusText(row));
        v[QStringLiteral("UsenetSpeed")] = speed > 0 ? formatByteRate(speed) : QString();
        v[QStringLiteral("UsenetRemaining")] = formatByteSize(qMax(qint64(0), total - decoded));
        v[QStringLiteral("UsenetPriority")] =
            htmlText(usenetPriorityName(int(row.value(QStringLiteral("priority")).toInteger())));
        v[QStringLiteral("UsenetHealth")] = usenetHealthText(row);
        v[QStringLiteral("UsenetHealthTitle")] = htmlText(usenetHealthTitle(row));
        // Blank for index 0, the absence of a category; the bare number for one
        // the list no longer holds.
        v[QStringLiteral("UsenetCategory")] =
            cat <= 0 ? QString()
                     : (cat < cats.size() ? htmlText(cats.at(cat).displayName()) : QString::number(cat));
        v[QStringLiteral("UsenetPreviewFile")] = QString::number(previewFile);
        v[QStringLiteral("UsenetPreviewNote")] = htmlText(previewNote);
        v[QStringLiteral("UsenetOpen")] = htmlText(payload.value(QStringLiteral("relPath")).toString());
        v[QStringLiteral("UsenetHasPayload")] = payload.isEmpty() ? QStringLiteral("0") : QStringLiteral("1");
        v[QStringLiteral("UsenetOpenPlay")] = usenetPlayFlag(payload.value(QStringLiteral("name")).toString());
        v[QStringLiteral("UsenetHasPassword")] = hasPw ? QStringLiteral("1") : QStringLiteral("0");
        v[QStringLiteral("UsenetFileCount")] = QString::number(files.size());
        v[QStringLiteral("UsenetFiles")] = fileLines;
        lines += WebTemplateEngine::substitute(lineTmpl, v);
    }

    // The Qt window's summary line, plus the engine's rate: the web UI has no
    // status bar to show it in.
    const QJsonObject stats = usenetStatsJson(all);
    QString summary;
    QString summaryTitle;
    const int count = stats.value(QStringLiteral("count")).toInt();
    // UsenetPanel::updateSummary()'s sentences, so they share its translations.
    if (count == 0) {
        summary = tr("No Usenet downloads. Use \"Add NZB…\" to queue one.");
    } else {
        const int active = stats.value(QStringLiteral("active")).toInt();
        summary = tr("%1 download(s), %2 active — %3% complete")
                      .arg(QString::number(count), QString::number(active),
                           QString::number(stats.value(QStringLiteral("percent")).toInt()));
        if (const QString stalled = stats.value(QStringLiteral("stalledReason")).toString();
            !stalled.isEmpty())
            summary += tr(" — %1").arg(stalled);
        if (const qint64 rate = stats.value(QStringLiteral("rate")).toInteger(); rate > 0)
            summary += tr(" — %1").arg(formatByteRate(rate));

        const qint64 maxKb = stats.value(QStringLiteral("maxDownloadKb")).toInteger();
        const qint64 usenetKb = stats.value(QStringLiteral("usenetLimitKb")).toInteger();
        const qint64 ed2kKb = stats.value(QStringLiteral("ed2kBudgetKb")).toInteger();
        if (active > 0 && maxKb > 0) {
            if (usenetKb < maxKb)
                summary += tr(" — limited to %1 KB/s while eD2K downloads").arg(usenetKb);
            summaryTitle = tr("Download limit %1 KB/s: Usenet up to %2 KB/s, eD2K up to %3 KB/s.\n"
                              "Whichever network is idle lends its share to the other.")
                               .arg(QString::number(maxKb), QString::number(usenetKb),
                                    QString::number(ed2kKb));
        }
    }

    QHash<QString, QString> listVars;
    for (const char* col : {"name", "size", "progress", "status", "speed", "remaining",
                            "priority", "health", "category"}) {
        const QString c = QLatin1String(col);
        listVars[QStringLiteral("SortMark_") + c] =
            sort == c ? (desc ? QStringLiteral(" &#9660;") : QStringLiteral(" &#9650;")) : QString();
    }
    listVars[QStringLiteral("Session")] = sessionId;
    listVars[QStringLiteral("UsenetCount")] = QString::number(rows.size());
    listVars[QStringLiteral("UsenetSummary")] = htmlText(summary);
    listVars[QStringLiteral("UsenetSummaryTitle")] = htmlText(summaryTitle);
    listVars[QStringLiteral("UsenetEnginePaused")] =
        stats.value(QStringLiteral("paused")).toBool() ? QStringLiteral("1") : QStringLiteral("0");
    listVars[QStringLiteral("UsenetEmptyRow")] = rows.isEmpty()
        ? QStringLiteral("<tr><td class=\"left\" colspan=\"10\">%1</td></tr>")
              .arg(htmlText(tr("No Usenet downloads here.")))
        : QString();
    listVars[QStringLiteral("UsenetRows")] = lines;
    return WebTemplateEngine::substitute(m_templateEngine->section(QStringLiteral("USENET_LIST")),
                                         listVars);
}

QString WebServer::buildUsenetDetails(const QString& /*sessionId*/, const QString& id)
{
    if (!usenetAvailable())
        return WebTemplateEngine::substitute(m_templateEngine->section(QStringLiteral("USENET_DETAILS_GONE")));

    const QCborMap d = m_usenetBackend->details(id);
    if (d.isEmpty())
        return WebTemplateEngine::substitute(m_templateEngine->section(QStringLiteral("USENET_DETAILS_GONE")));

    const QCborMap payload = usenetPayload(d);
    const QString fileTmpl = m_templateEngine->section(QStringLiteral("USENET_DETAILS_FILE_LINE"));
    const QCborArray files = d.value(QStringLiteral("files")).toArray();

    QStringList posters;
    QStringList groups;
    qint64 newest = 0;
    qint64 doneSegs = 0;
    qint64 totalSegs = 0;
    QString fileLines;
    for (const auto& fv : files) {
        const QCborMap f = fv.toMap();
        posters << f.value(QStringLiteral("poster")).toString();
        for (const auto& g : f.value(QStringLiteral("groups")).toArray())
            groups << g.toString();
        newest = qMax(newest, f.value(QStringLiteral("date")).toInteger());
        const qint64 done = f.value(QStringLiteral("doneSegments")).toInteger();
        const qint64 segs = f.value(QStringLiteral("segmentCount")).toInteger();
        doneSegs += done;
        totalSegs += segs;

        const QString fname = f.value(QStringLiteral("name")).toString();
        const QString finalPath = f.value(QStringLiteral("finalPath")).toString();
        const qint64 missing = f.value(QStringLiteral("missingSegments")).toInteger();
        const qint64 pct = f.value(QStringLiteral("percent")).toInteger();

        QString title = f.value(QStringLiteral("subject")).toString();
        if (!finalPath.isEmpty())
            title += QLatin1Char('\n') + finalPath;
        if (const qint64 nzbMissing = f.value(QStringLiteral("nzbMissingSegments")).toInteger();
            nzbMissing > 0) {
            title += QLatin1Char('\n')
                     + tr("%n article(s) were never listed in the NZB", nullptr, int(nzbMissing));
        }

        QHash<QString, QString> v;
        v[QStringLiteral("UsenetId")] = htmlText(id);
        v[QStringLiteral("UsenetFileName")] = htmlText(fname);
        v[QStringLiteral("UsenetFileType")] = webFileTypeToken(fname);
        v[QStringLiteral("UsenetFileSize")] = formatByteSize(f.value(QStringLiteral("size")).toInteger());
        v[QStringLiteral("UsenetFilePercent")] = QString::number(pct);
        v[QStringLiteral("UsenetFileArticles")] = QStringLiteral("%1/%2").arg(done).arg(segs);
        v[QStringLiteral("UsenetFileMissing")] = missing > 0 ? QString::number(missing) : QString();
        v[QStringLiteral("UsenetFileStatus")] =
            htmlText(missing > 0  ? tr("%n article(s) missing", nullptr, int(missing))
                     : pct >= 100 ? tr("Complete")
                     : done > 0   ? tr("Downloading")
                                  : tr("Queued"));
        v[QStringLiteral("UsenetFileClass")] =
            f.value(QStringLiteral("isPar2")).toBool() ? QStringLiteral("un-par2") : QString();
        v[QStringLiteral("UsenetFileTitle")] = htmlText(title);
        v[QStringLiteral("UsenetFileOpen")] =
            htmlText(usenetPublishedFor(d, finalPath).value(QStringLiteral("relPath")).toString());
        v[QStringLiteral("UsenetFileHasFinal")] =
            finalPath.isEmpty() ? QStringLiteral("0") : QStringLiteral("1");
        v[QStringLiteral("UsenetFilePlay")] = usenetPlayFlag(fname);
        fileLines += WebTemplateEngine::substitute(fileTmpl, v);
    }

    QHash<QString, QString> vars;
    vars[QStringLiteral("UsenetId")] = htmlText(id);
    vars[QStringLiteral("UsenetName")] = htmlText(d.value(QStringLiteral("name")).toString());
    vars[QStringLiteral("UsenetSize")] = formatByteSize(d.value(QStringLiteral("totalBytes")).toInteger());
    vars[QStringLiteral("UsenetDate")] = newest > 0
        ? QDateTime::fromSecsSinceEpoch(newest).toString(QStringLiteral("yyyy-MM-dd HH:mm"))
        : QString();
    vars[QStringLiteral("UsenetStatus")] = htmlText(usenetStatusText(d));
    vars[QStringLiteral("UsenetHealth")] = usenetHealthText(d);
    vars[QStringLiteral("UsenetHealthTitle")] = htmlText(usenetHealthTitle(d));
    vars[QStringLiteral("UsenetPoster")] = htmlText(collapseValues(posters));
    vars[QStringLiteral("UsenetGroups")] = htmlText(collapseValues(groups));
    vars[QStringLiteral("UsenetArticles")] =
        htmlText(tr("%1 of %2").arg(QString::number(doneSegs), QString::number(totalSegs)));
    vars[QStringLiteral("UsenetFileCount")] = QString::number(files.size());
    vars[QStringLiteral("UsenetOpen")] = htmlText(payload.value(QStringLiteral("relPath")).toString());
    vars[QStringLiteral("UsenetHasPayload")] = payload.isEmpty() ? QStringLiteral("0") : QStringLiteral("1");
    vars[QStringLiteral("UsenetDetailsFiles")] = fileLines;
    return WebTemplateEngine::substitute(m_templateEngine->section(QStringLiteral("USENET_DETAILS")),
                                         vars);
}

// ---------------------------------------------------------------------------
// Languages
// ---------------------------------------------------------------------------

QHttpServerResponse WebServer::loginPage(const QString& failedHtml)
{
    const QString tmpl =
        m_templateEngine ? m_templateEngine->section(QStringLiteral("LOGIN")) : QString();
    if (tmpl.isEmpty()) {
        return QHttpServerResponse(QByteArrayLiteral("text/html"),
            QByteArrayLiteral("<html><body><h1>eMule Web Interface</h1><p>Template not loaded.</p></body></html>"));
    }

    QHash<QString, QString> vars;
    vars[QStringLiteral("CharSet")] = QStringLiteral("UTF-8");
    vars[QStringLiteral("eMuleAppName")] = QStringLiteral("eMule");
    vars[QStringLiteral("version")] = QString(kAppVersion);
    vars[QStringLiteral("WebControl")] = htmlText(tr("Web Control Panel"));
    vars[QStringLiteral("HtmlLang")] = htmlText(htmlLangCode(webLanguage({})));
    vars[QStringLiteral("FailedLogin")] = failedHtml;
    return QHttpServerResponse(QByteArrayLiteral("text/html"),
                               WebTemplateEngine::substitute(tmpl, vars).toUtf8());
}

QString WebServer::webLanguage(const QString& sessionId, const QString& requested) const
{
    if (!m_translations)
        return {};
    QString chosen = requested;
    if (m_sessionManager && !sessionId.isEmpty()) {
        if (const WebSession* s = m_sessionManager->session(sessionId); s && !s->language.isEmpty())
            chosen = s->language;
    }
    return m_translations->resolve(chosen, m_preferences ? m_preferences->language() : QString());
}

QString WebServer::languageOptions(const QString& sessionId) const
{
    const WebSession* session = m_sessionManager ? m_sessionManager->session(sessionId) : nullptr;
    const QString chosen = session ? session->language : QString();

    const auto option = [&chosen](const QString& code, const QString& label) {
        return QStringLiteral("<option value=\"%1\"%2>%3</option>")
            .arg(htmlText(code), code == chosen ? QStringLiteral(" selected") : QString(),
                 htmlText(label));
    };

    // "App language" names what that currently is, so choosing it is not a guess.
    const QString app = m_translations
        ? m_translations->resolve({}, m_preferences ? m_preferences->language() : QString())
        : QStringLiteral("en_US");
    QString html = option({}, tr("App language (%1)").arg(QLocale(app).nativeLanguageName()));
    html += option(QStringLiteral("en_US"), QStringLiteral("English"));
    if (m_translations) {
        for (const AppLanguage& lang : m_translations->availableLanguages())
            html += option(lang.code, lang.label);
    }
    return html;
}

} // namespace eMule
