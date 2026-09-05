#pragma once

/// @file WebServer.h
/// @brief JSON REST API web server — replaces MFC WebServer.cpp/h.
///
/// Uses Qt 6's QHttpServer to serve a JSON REST API for controlling eMule.
/// Provides CRUD access to downloads, uploads, servers, search, shared files,
/// friends, statistics, and preferences.

#include "utils/Types.h"

#include <QFuture>
#include <QHttpHeaders>
#include <QHttpServerResponse>
#include <QJsonObject>
#include <QList>
#include <QObject>

#include <functional>
#include <memory>
#include <vector>

class QHttpServer;
class QHttpServerRequest;
class QSslServer;
class QTcpServer;
class QUrlQuery;

namespace eMule {

class DownloadQueue;
class FriendList;
class Preferences;
class SearchList;
class ServerConnect;
class ServerList;
class SharedFileList;
class Statistics;
class StatsHistory;
class UploadQueue;
struct StatsGraphSample;

// ---------------------------------------------------------------------------
// WebServerConfig — startup configuration
// ---------------------------------------------------------------------------

struct WebServerConfig {
    uint16 port = 4711;
    QString listenAddress;
    QString apiKey;
    bool enabled = false;         ///< Run the HTTP server at all (also serves the GUI preview stream).
    bool webUiEnabled = false;    ///< Serve the template web UI (/, login, static assets). Independent of restApiEnabled.
    bool restApiEnabled = false;  ///< Serve the JSON REST API (/api/v1/*). Independent of webUiEnabled.
    bool gzipEnabled = true;
    QString templatePath;
    int sessionTimeout = 5;
    bool httpsEnabled = false;
    QString certPath;
    QString keyPath;
    QString adminPasswordHash;
    bool adminAllowHiLevFunc = false;
    bool guestEnabled = false;
    QString guestPasswordHash;
};

// ---------------------------------------------------------------------------
// UsenetStreamSource — what a Usenet preview request resolves to
// ---------------------------------------------------------------------------

/// Declared here, not included from the Usenet module: `core -> usenet` must
/// never happen, or the dependency direction that keeps eMule::Usenet optional
/// is gone. `DaemonApp` — which links both — installs a resolver that fills this
/// in from `UsenetQueue::requestStream()`. Same shape as the `setLogProvider`
/// seam, and for the same reason.
/// One contiguous run of the logical file, and the file on disk holding it.
///
/// A raw-posted release is one piece. A stored RAR set is one piece per volume,
/// because the thing being played is the `.mkv` inside them — that is phase 6b,
/// and it is why a preview source stopped being a single path.
struct UsenetStreamPiece {
    QString path;
    qint64 virtualOffset = 0;  ///< where it starts in the logical file
    qint64 fileOffset = 0;     ///< where it starts inside `path`
    qint64 length = 0;         ///< 0 on a lone piece means "to the end of the file"
};

struct UsenetStreamSource {
    bool found = false;
    QString fileName;

    /// Final length of the logical file. Zero while it is still unknown, which
    /// is a normal state for Usenet: only the article's own `=ybegin size=` says
    /// it, so nothing is known until the first article of that file lands.
    qint64 totalSize = 0;

    /// End of the readable run containing the requested offset. The rest of the
    /// file exists — it is preallocated — but reading it would return zeros, so
    /// it must not be served as content.
    qint64 availableEnd = 0;

    bool complete = false;

    QList<UsenetStreamPiece> pieces;

    /// Set when the release can never be streamed: a compressed, solid or
    /// encrypted archive. Answered immediately rather than waited out, so the
    /// user learns why instead of watching a player time out.
    QString notSeekableReason;
};

// ---------------------------------------------------------------------------
// WebServer — JSON REST API server
// ---------------------------------------------------------------------------

class WebServer : public QObject {
    Q_OBJECT

public:
    explicit WebServer(QObject* parent = nullptr);
    ~WebServer() override;

    // Dependency injection
    void setDownloadQueue(DownloadQueue* dq);
    void setUploadQueue(UploadQueue* uq);
    void setServerList(ServerList* sl);
    void setServerConnect(ServerConnect* sc);
    void setSearchList(SearchList* search);
    void setSharedFileList(SharedFileList* shared);
    void setFriendList(FriendList* fl);
    void setStatistics(Statistics* stats);
    /// Sample history behind the Graphs page. MFC fills its own 500-point ring from the
    /// statistics dialog (CWebServer::AddStatsLine, srchybrid/WebServer.cpp:321) — a
    /// GUI-process hook a headless daemon does not have, so we read core's history
    /// instead. Optional: without it the page renders empty axes.
    void setStatsHistory(StatsHistory* history);
    void setPreferences(Preferences* prefs);
    void setLogProvider(std::function<QString()> provider) { m_logProvider = std::move(provider); }

    /// Resolve one file of one Usenet queue item for streaming. Optional:
    /// without it the Usenet preview route answers 503 and everything else is
    /// unaffected, which is what a build with EMULE_USENET off gets.
    ///
    /// The call is not a pure lookup — it also registers that somebody is
    /// watching, so the queue can put that item's articles first. See
    /// UsenetQueue::requestStream().
    using UsenetStreamResolver =
        std::function<UsenetStreamSource(const QString&, int, qint64, qint64)>;
    void setUsenetStreamResolver(UsenetStreamResolver resolver)
    {
        m_usenetStreamResolver = std::move(resolver);
    }

    bool start(const WebServerConfig& config);
    void stop();
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] uint16 port() const;

    /// Random token for authenticating preview streaming requests.
    /// Regenerated every time the web server starts.
    [[nodiscard]] const QString& streamToken() const { return m_streamToken; }

    /// Reload the template file (called from "Reload" button in options).
    void reloadTemplate();

    /// Template variables for the Graphs page, from @p samples (oldest first).
    ///
    /// Emits MFC's own variables so a stock eMule template keeps working —
    /// [GraphDownload], [GraphUpload] and [GraphConnections] as comma-separated
    /// values, the two rates in bytes/s exactly as _GetGraphs writes them
    /// (srchybrid/WebServer.cpp:3038-3043) — plus [GraphDownloadPts] and friends,
    /// which are the same series already scaled to a @p viewW x @p viewH SVG viewBox
    /// so a template can draw them without scripting.
    ///
    /// Public and static because it is the whole of the page worth testing; the page
    /// itself only adds labels around it.
    [[nodiscard]] static QHash<QString, QString> graphVars(
        const std::vector<StatsGraphSample>& samples,
        uint32 maxDown, uint32 maxUp, uint32 maxConn, int viewW, int viewH);

signals:
    void started(uint16 port);
    void stopped();

private:
    void registerRoutes();

    // Auth & helpers
    struct AuthResult;
    [[nodiscard]] AuthResult checkAuth(const QHttpHeaders& headers) const;

    // Endpoint handlers — Downloads
    QHttpServerResponse handleGetDownloads();
    QHttpServerResponse handleGetDownload(const QString& hash);
    QHttpServerResponse handlePauseDownload(const QString& hash);
    QHttpServerResponse handleResumeDownload(const QString& hash);
    QHttpServerResponse handleCancelDownload(const QString& hash);
    QHttpServerResponse handlePreviewStream(const QString& hash, const QHttpServerRequest& req);

    // Endpoint handlers — Usenet preview
    QFuture<QHttpServerResponse> handleUsenetPreviewStream(const QString& itemId,
                                                           const QString& fileIndex,
                                                           const QHttpServerRequest& req);

    /// Answer a Range request for @p path, serving nothing past @p availableEnd.
    ///
    /// Shared by both preview routes. Two properties matter and neither is
    /// obvious from the signature:
    ///
    ///  - the body is capped at kPreviewChunkBytes, so an open-ended
    ///    `Range: bytes=0-` cannot materialise a whole release in the daemon's
    ///    address space. A short 206 is ordinary HTTP and every player asks for
    ///    the next window;
    ///  - @p availableEnd is where the *content* stops, which for a file still
    ///    downloading is short of its size. Serving past it would hand a player
    ///    the preallocated zeros.
    ///
    /// Takes the raw `Range` header rather than the request, because the Usenet
    /// route can answer long after the QHttpServerRequest is gone.
    /// The one implementation of HTTP Range in the daemon, shared by the ED2K
    /// and Usenet preview routes. @p pieces are in ascending virtualOffset order
    /// and may span several files; the ED2K route passes exactly one.
    [[nodiscard]] QHttpServerResponse serveRange(const QList<UsenetStreamPiece>& pieces,
                                                 const QString& fileName,
                                                 qint64 totalSize,
                                                 qint64 availableEnd,
                                                 const QByteArray& rangeHeader);

    // Endpoint handlers — Uploads
    QHttpServerResponse handleGetUploads();

    // Endpoint handlers — Servers
    QHttpServerResponse handleGetServers();

    // Endpoint handlers — Connection
    QHttpServerResponse handleGetConnection();
    QHttpServerResponse handlePostConnect();
    QHttpServerResponse handlePostDisconnect();

    // Endpoint handlers — Search
    QHttpServerResponse handlePostSearch(const QJsonObject& body);
    QHttpServerResponse handleGetSearchResults(uint32 searchID);

    // Endpoint handlers — Shared files
    QHttpServerResponse handleGetSharedFiles();

    // Endpoint handlers — Friends
    QHttpServerResponse handleGetFriends();
    QHttpServerResponse handlePostFriend(const QJsonObject& body);
    QHttpServerResponse handleDeleteFriend(const QString& hash);

    // Endpoint handlers — Statistics
    QHttpServerResponse handleGetStats();

    // Endpoint handlers — Preferences
    QHttpServerResponse handleGetPreferences();
    QHttpServerResponse handlePatchPreferences(const QJsonObject& body);

    // Template web interface handlers
    QHttpServerResponse handleLogin(const QHttpServerRequest& request);
    QHttpServerResponse handlePage(const QHttpServerRequest& request);
    QHttpServerResponse handleStaticFile(const QString& path);
    QHttpServerResponse renderPage(const QString& page, const QString& sessionId);
    void dispatchActions(const QUrlQuery& query, const QString& page);

    // Template page builders
    [[nodiscard]] QString buildTransferPage(bool isAdmin, const QString& sessionId);
    [[nodiscard]] QString buildServerListPage(bool isAdmin, const QString& sessionId);
    [[nodiscard]] QString buildSearchPage(bool isAdmin);
    [[nodiscard]] QString buildSharedFilesPage(bool isAdmin, const QString& sessionId);
    [[nodiscard]] QString buildStatisticsPage();
    [[nodiscard]] QString buildPreferencesPage(bool isAdmin);
    [[nodiscard]] QString buildServerInfoPage();
    [[nodiscard]] QString buildLogPage();
    [[nodiscard]] QString buildDebugLogPage();
    [[nodiscard]] QString buildKadPage(const QString& sessionId);
    [[nodiscard]] QString buildMyInfoPage();
    [[nodiscard]] QString buildGraphsPage();

    // Gzip compression helper
    static QByteArray gzipCompress(const QByteArray& data);

    // Members
    std::unique_ptr<QHttpServer> m_server;
    QTcpServer* m_tcpServer = nullptr;  // Owned by m_server after listen()
    WebServerConfig m_config;

    // Template engine & session manager
    std::unique_ptr<class WebTemplateEngine> m_templateEngine;
    std::unique_ptr<class WebSessionManager> m_sessionManager;
    QString m_webDataDir;  // path to config/webserver/ assets

    // Manager pointers (not owned)
    DownloadQueue*  m_downloadQueue = nullptr;
    UploadQueue*    m_uploadQueue   = nullptr;
    ServerList*     m_serverList    = nullptr;
    ServerConnect*  m_serverConnect = nullptr;
    SearchList*     m_searchList    = nullptr;
    SharedFileList* m_sharedFiles   = nullptr;
    FriendList*     m_friendList    = nullptr;
    Statistics*     m_statistics    = nullptr;
    StatsHistory*   m_statsHistory  = nullptr;
    Preferences*    m_preferences   = nullptr;

    // Log provider callback (injected by DaemonApp)
    std::function<QString()> m_logProvider;

    // Usenet stream resolver (injected by DaemonApp; null in a core-only build)
    UsenetStreamResolver m_usenetStreamResolver;

    // Random token for preview streaming authentication
    QString m_streamToken;
};

} // namespace eMule
