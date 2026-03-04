/// @file DaemonApp.cpp
/// @brief Orchestrator for the headless core daemon — implementation.

#include "DaemonApp.h"
#include "CoreNotifierBridge.h"
#include "IpcClientHandler.h"
#include "IpcServer.h"

#include "IpcMessage.h"

#include "app/AppContext.h"
#include "app/CoreSession.h"
#include "prefs/Preferences.h"
#include "webserver/WebServer.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QUuid>

#include <openssl/rand.h>

#include <cstring>

namespace eMule {

using namespace Ipc;

DaemonApp* DaemonApp::s_instance = nullptr;
QtMessageHandler DaemonApp::s_previousHandler = nullptr;
int64_t DaemonApp::s_nextLogId = 1;
std::deque<LogEntry> DaemonApp::s_logBuffer;
std::mutex DaemonApp::s_logMutex;
QString DaemonApp::s_sessionToken;

DaemonApp::DaemonApp(QObject* parent)
    : QObject(parent)
{
}

DaemonApp::~DaemonApp()
{
    stop();
}

bool DaemonApp::start()
{
    if (m_running)
        return true;

    // Install log forwarder first so all startup messages (including Kad start)
    // are captured in the buffer and visible to the GUI when it connects.
    installLogForwarder();

    // Start core session
    m_coreSession = std::make_unique<CoreSession>(this);
    m_coreSession->start();

    // Generate IPC auth token on first run
    auto tokens = thePrefs.ipcTokens();
    if (tokens.isEmpty()) {
        QByteArray raw(16, Qt::Uninitialized);
        RAND_bytes(reinterpret_cast<unsigned char*>(raw.data()), 16);
        QString token = QString::fromLatin1(raw.toHex());  // 32 hex chars
        tokens.append(token);
        thePrefs.setIpcTokens(tokens);
        thePrefs.save();
    }
    logInfo(QStringLiteral("IPC auth token: %1").arg(tokens.first()));

    // Start IPC server
    m_ipcServer = std::make_unique<IpcServer>(this);

    const QHostAddress addr = thePrefs.ipcListenAddress().isEmpty()
        ? QHostAddress::LocalHost
        : QHostAddress(thePrefs.ipcListenAddress());
    const uint16 port = thePrefs.ipcPort();

    if (!m_ipcServer->start(addr, port)) {
        logError(QStringLiteral("Failed to start IPC server on %1:%2")
                     .arg(addr.toString()).arg(port));
        m_coreSession->stop();
        m_coreSession.reset();
        removeLogForwarder();
        return false;
    }

    // Connect core signals to IPC push events
    m_notifierBridge = std::make_unique<CoreNotifierBridge>(m_ipcServer.get(), this);
    m_notifierBridge->connectAll();

    // Connect web server config changes from any IPC client
    connect(m_ipcServer.get(), &IpcServer::webServerConfigChanged,
            this, &DaemonApp::restartWebServer);

    // Start web server if enabled
    startWebServer();

    m_running = true;
    logInfo(QStringLiteral("Daemon started — IPC server on %1:%2")
                .arg(addr.toString()).arg(port));
    return true;
}

void DaemonApp::stop()
{
    if (!m_running)
        return;

    stopWebServer();

    removeLogForwarder();
    m_notifierBridge.reset();

    if (m_ipcServer)
        m_ipcServer->stop();
    m_ipcServer.reset();

    if (m_coreSession)
        m_coreSession->stop();
    m_coreSession.reset();

    m_running = false;
    logInfo(QStringLiteral("Daemon stopped."));
}

bool DaemonApp::isRunning() const
{
    return m_running;
}

// ---------------------------------------------------------------------------
// Web server management
// ---------------------------------------------------------------------------

void DaemonApp::startWebServer()
{
    if (!thePrefs.webServerEnabled())
        return;

    m_webServer = std::make_unique<WebServer>(this);

    // Inject dependencies from core
    m_webServer->setDownloadQueue(theApp.downloadQueue);
    m_webServer->setUploadQueue(theApp.uploadQueue);
    m_webServer->setServerList(theApp.serverList);
    m_webServer->setServerConnect(theApp.serverConnect);
    m_webServer->setSearchList(theApp.searchList);
    m_webServer->setSharedFileList(theApp.sharedFileList);
    m_webServer->setFriendList(theApp.friendList);
    m_webServer->setStatistics(theApp.statistics);
    m_webServer->setPreferences(&thePrefs);

    WebServerConfig config;
    config.enabled            = true;
    config.port               = thePrefs.webServerPort();
    config.listenAddress      = thePrefs.webServerListenAddress();
    config.apiKey             = thePrefs.webServerApiKey();
    config.restApiEnabled     = thePrefs.webServerRestApiEnabled();
    config.gzipEnabled        = thePrefs.webServerGzipEnabled();
    config.templatePath       = thePrefs.webServerTemplatePath();
    config.sessionTimeout     = thePrefs.webServerSessionTimeout();
    config.httpsEnabled       = thePrefs.webServerHttpsEnabled();
    config.certPath           = thePrefs.webServerCertPath();
    config.keyPath            = thePrefs.webServerKeyPath();
    config.adminPasswordHash  = thePrefs.webServerAdminPassword();
    config.adminAllowHiLevFunc = thePrefs.webServerAdminAllowHiLevFunc();
    config.guestEnabled       = thePrefs.webServerGuestEnabled();
    config.guestPasswordHash  = thePrefs.webServerGuestPassword();

    m_webServer->start(config);
}

void DaemonApp::stopWebServer()
{
    if (m_webServer) {
        m_webServer->stop();
        m_webServer.reset();
    }
}

void DaemonApp::restartWebServer()
{
    stopWebServer();
    startWebServer();
}

// ---------------------------------------------------------------------------
// Log forwarding to IPC clients
// ---------------------------------------------------------------------------

void DaemonApp::installLogForwarder()
{
    s_instance = this;
    if (s_sessionToken.isEmpty())
        s_sessionToken = QUuid::createUuid().toString(QUuid::WithoutBraces);
    s_previousHandler = qInstallMessageHandler(logMessageHandler);
}

void DaemonApp::removeLogForwarder()
{
    if (s_instance == this) {
        qInstallMessageHandler(s_previousHandler);
        s_previousHandler = nullptr;
        s_instance = nullptr;
    }
}

std::vector<LogEntry> DaemonApp::logsSince(int64_t lastLogId)
{
    std::lock_guard lock(s_logMutex);
    std::vector<LogEntry> result;
    for (const auto& entry : s_logBuffer) {
        if (entry.id > lastLogId)
            result.push_back(entry);
    }
    return result;
}

QString DaemonApp::sessionToken()
{
    return s_sessionToken;
}

void DaemonApp::logMessageHandler(QtMsgType type, const QMessageLogContext& context,
                                  const QString& msg)
{
    // Always chain to previous handler (console output)
    if (s_previousHandler)
        s_previousHandler(type, context, msg);

    const char* cat = context.category ? context.category : "";
    if (std::strncmp(cat, "emule.", 6) != 0)
        return;

    // Assign incremental ID and buffer the entry
    const qint64 ts = QDateTime::currentSecsSinceEpoch();
    int64_t logId;
    {
        std::lock_guard lock(s_logMutex);
        logId = s_nextLogId++;
        s_logBuffer.push_back({logId, QString::fromUtf8(cat), type, msg, ts});
        while (static_cast<int>(s_logBuffer.size()) > MaxLogBuffer)
            s_logBuffer.pop_front();
    }

    // Broadcast to connected GUI clients (with log ID and timestamp)
    if (!s_instance || !s_instance->m_ipcServer)
        return;

    IpcMessage push(IpcMsgType::PushLogMessage, 0);
    push.append(logId);
    push.append(QString::fromUtf8(cat));
    push.append(int64_t(type));
    push.append(msg);
    push.append(ts);
    s_instance->m_ipcServer->broadcast(push);
}

} // namespace eMule
