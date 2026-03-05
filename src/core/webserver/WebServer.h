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

class QHttpServer;
class QHttpServerRequest;
class QSslServer;
class QTcpServer;

namespace eMule {

class DownloadQueue;
class FriendList;
class Preferences;
class SearchList;
class ServerConnect;
class ServerList;
class SharedFileList;
class Statistics;
class UploadQueue;

// ---------------------------------------------------------------------------
// WebServerConfig — startup configuration
// ---------------------------------------------------------------------------

struct WebServerConfig {
    uint16 port = 4711;
    QString listenAddress;
    QString apiKey;
    bool enabled = false;
    bool restApiEnabled = false;
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
    void setPreferences(Preferences* prefs);
    void setLogProvider(std::function<QString()> provider) { m_logProvider = std::move(provider); }

    bool start(const WebServerConfig& config);
    void stop();
    [[nodiscard]] bool isRunning() const;
    [[nodiscard]] uint16 port() const;

    /// Reload the template file (called from "Reload" button in options).
    void reloadTemplate();

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

    // Template page builders
    [[nodiscard]] QString buildTransferPage(bool isAdmin);
    [[nodiscard]] QString buildServerListPage(bool isAdmin);
    [[nodiscard]] QString buildSearchPage(bool isAdmin);
    [[nodiscard]] QString buildSharedFilesPage(bool isAdmin);
    [[nodiscard]] QString buildStatisticsPage();
    [[nodiscard]] QString buildPreferencesPage(bool isAdmin);
    [[nodiscard]] QString buildServerInfoPage();
    [[nodiscard]] QString buildLogPage();
    [[nodiscard]] QString buildDebugLogPage();
    [[nodiscard]] QString buildKadPage();
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
    Preferences*    m_preferences   = nullptr;

    // Log provider callback (injected by DaemonApp)
    std::function<QString()> m_logProvider;
};

} // namespace eMule
