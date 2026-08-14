#pragma once

/// @file WebServer.h
/// @brief JSON REST API web server — replaces MFC WebServer.cpp/h.
///
/// Uses Qt 6's QHttpServer to serve a JSON REST API for controlling eMule.
/// Provides CRUD access to downloads, uploads, servers, search, shared files,
/// friends, statistics, and preferences.

#include "utils/Types.h"

#include <QHttpHeaders>
#include <QHttpServerResponse>
#include <QJsonObject>
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

    // Random token for preview streaming authentication
    QString m_streamToken;
};

} // namespace eMule
