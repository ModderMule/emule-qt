/// @file DaemonApp.cpp
/// @brief Orchestrator for the headless core daemon — implementation.

#include "DaemonApp.h"
#include "CoreNotifierBridge.h"
#include "IpcServer.h"

#include "IpcMessage.h"

#include "app/AppConfig.h"
#include "app/AppContext.h"
#include "app/CoreSession.h"
#include "net/HttpDefaults.h"
#include "prefs/Preferences.h"
#include "stats/Statistics.h"
#include "stats/StatsSnapshot.h"
#include "UsenetSession.h"
#include "IndexerSearchList.h"
#include "IndexerResult.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"
#include "ipc/PushCoalescer.h"
#include "webserver/WebServer.h"
#include "utils/Log.h"

#include <QDateTime>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUuid>

#include <openssl/rand.h>


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

    // Resolve and log public IP (non-blocking, opt-in)
    if (thePrefs.logPublicIP()) {
        auto* nam = new QNetworkAccessManager(this);
        // curl -6 ifconfig.me <- for v6 detection later
        QNetworkRequest req = Http::makeRequest(QUrl(QStringLiteral("https://api.ipify.org")));
        req.setTransferTimeout(5000);
        auto* reply = nam->get(req);
        connect(reply, &QNetworkReply::finished, this, [reply, nam]() {
            if (reply->error() == QNetworkReply::NoError) {
                const QString ip = QString::fromUtf8(reply->readAll()).trimmed();
                if (!ip.isEmpty())
                    logInfo(QStringLiteral("Public IP address: %1").arg(ip));
            }
            reply->deleteLater();
            nam->deleteLater();
        });
    }

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
    connect(m_ipcServer.get(), &IpcServer::usenetConfigChanged,
            this, &DaemonApp::applyUsenetServers);
    connect(m_ipcServer.get(), &IpcServer::indexerConfigChanged,
            this, &DaemonApp::applyIndexerConfig);

    // Start web server if enabled
    startWebServer();

    // Usenet. Always constructed; usenetEnabled() gates only the auto-start, so
    // the switch takes effect without a daemon restart.
    m_usenetSession = std::make_unique<usenet::UsenetSession>();
    usenet::theUsenetSession = m_usenetSession.get();
    connectUsenetPushes();
    if (thePrefs.usenetEnabled())
        m_usenetSession->start();

    // Indexer search. No enable switch: an account list that is empty is the off
    // state, and there is nothing running to gate — a search only ever happens
    // because the user asked for one.
    m_indexerSearches = std::make_unique<indexer::IndexerSearchList>();
    indexer::theIndexerSearchList = m_indexerSearches.get();
    connectIndexerPushes();

    m_running = true;
    logInfo(QStringLiteral("Daemon started — IPC server on %1:%2")
                .arg(addr.toString()).arg(port));
    return true;
}

void DaemonApp::stop()
{
    if (!m_running)
        return;

    // Bank this session's statistics before tearing down. The periodic flush in
    // CoreSession has probably already done it; the flush is idempotent, so this
    // only picks up whatever happened since.
    if (theApp.statistics) {
        flushCumulativeStats(thePrefs);
        thePrefs.save();
    }

    stopWebServer();

    // Before the IPC server goes away: a Test-button reply in flight holds a
    // QPointer to its handler, and the engine's own sockets must be torn down
    // while the event loop is still turning. UsenetSession::stop() is also where
    // the worker threads are joined, so by the time it returns nothing is left
    // running that could reach a half-destroyed daemon.
    indexer::theIndexerSearchList = nullptr;
    m_indexerSearches.reset();

    usenet::theUsenetSession = nullptr;
    if (m_usenetSession)
        m_usenetSession->stop();
    m_usenetSession.reset();

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
    m_webServer->setStatsHistory(theApp.statsHistory);
    m_webServer->setPreferences(&thePrefs);

    // Usenet preview. Injected as a callback rather than a pointer: WebServer
    // lives in eMule::Core and `core -> usenet` must never happen. DaemonApp is
    // the one object that links both, which is exactly the seam setLogProvider
    // below already uses.
    m_webServer->setUsenetStreamResolver(
        [](const QString& itemId, int fileIndex, qint64 wantOffset,
           qint64 wantLength) -> UsenetStreamSource {
            UsenetStreamSource out;
            if (!usenet::theUsenetSession || !usenet::theUsenetSession->queue())
                return out;

            const auto info = usenet::theUsenetSession->queue()->requestStream(
                itemId, fileIndex, wantOffset, wantLength);
            out.found             = info.found;
            out.fileName          = info.fileName;
            out.totalSize         = info.totalSize;
            out.availableEnd      = info.availableEnd;
            out.complete          = info.complete;
            out.notSeekableReason = info.notSeekableReason;

            // A stored RAR set resolves to one piece per volume; a raw post to
            // one. Either way the web server only ever sees paths and offsets.
            for (const auto& piece : info.pieces) {
                out.pieces.append({piece.path, piece.virtualOffset,
                                   piece.fileOffset, piece.length});
            }
            return out;
        });

    m_webServer->setLogProvider([] {
        auto entries = DaemonApp::logsSince(0);
        QString text;
        for (const auto& e : entries) {
            text += QDateTime::fromSecsSinceEpoch(e.timestamp).toString(QStringLiteral("HH:mm:ss"));
            text += QLatin1Char(' ');
            text += e.message;
            text += QLatin1Char('\n');
        }
        return text;
    });

    // The web UI and the REST API are two independent surfaces — either can be
    // enabled without the other. The HTTP server always runs (config.enabled)
    // because the GUI's preview stream needs it even when both surfaces are off.
    WebServerConfig config;
    config.enabled        = true;
    config.port           = thePrefs.webServerPort();
    config.webUiEnabled   = thePrefs.webServerEnabled();
    config.restApiEnabled = thePrefs.webServerRestApiEnabled();

    if (config.webUiEnabled || config.restApiEnabled) {
        // Either surface needs the shared server + auth settings. The REST API
        // authenticates with apiKey; the web UI uses session login. Populate all
        // of it so REST works even with the UI off, and vice versa.
        config.listenAddress       = thePrefs.webServerListenAddress();
        config.apiKey              = thePrefs.webServerApiKey();
        config.gzipEnabled         = thePrefs.webServerGzipEnabled();
        config.templatePath        = thePrefs.webServerTemplatePath();
        config.sessionTimeout      = thePrefs.webServerSessionTimeout();
        config.httpsEnabled        = thePrefs.webServerHttpsEnabled();
        config.certPath            = thePrefs.webServerCertPath();
        config.keyPath             = thePrefs.webServerKeyPath();
        config.adminPasswordHash   = thePrefs.webServerAdminPassword();
        config.adminAllowHiLevFunc = thePrefs.webServerAdminAllowHiLevFunc();
        config.guestEnabled        = thePrefs.webServerGuestEnabled();
        config.guestPasswordHash   = thePrefs.webServerGuestPassword();
    } else {
        // Preview-only — the server runs solely for the GUI's preview stream, so
        // keep it on localhost and expose neither surface.
        config.listenAddress = QStringLiteral("127.0.0.1");
        config.guestEnabled  = false;
    }

    m_webServer->start(config);

    // The web port only needs forwarding while the server is actually up, and
    // only when the user asked for it. This is what finally gives the
    // webServerUPnP preference an effect — the old UPnPManager exposed
    // enableWebServerPort() but nothing ever called it.
    if (m_coreSession)
        m_coreSession->updatePortMappings();
}

namespace {

/// Matches CoreNotifierBridge's kPushWindowMs: below the GUI's 500 ms poll, so a
/// push still beats the poll to the data, while per-segment progress on a large
/// release collapses into a handful of sends.
constexpr int kUsenetPushWindowMs = 250;

} // namespace

/// Defined in IpcClientHandler.cpp, beside the GetUsenetQueue row it must match.
QCborMap usenetQueueItemToCbor(const usenet::UsenetQueueItem& item);

/// Likewise: the push and the StartIndexerSearch reply must carry the same row.
QCborMap indexerResultToCbor(const indexer::IndexerResult& result);

void DaemonApp::connectUsenetPushes()
{
    if (!m_usenetSession || !m_ipcServer)
        return;

    auto* coalescer = new Ipc::PushCoalescer(this);
    connect(coalescer, &Ipc::PushCoalescer::ready, this, [this](const IpcMessage& msg) {
        m_ipcServer->broadcast(msg);
    });

    auto pushItem = [this, coalescer](const QString& id) {
        // Keyed on the item, so a 10 000-article release cannot suppress pushes
        // for a second NZB queued beside it. PushCoalescer's subKey is 32 bits and
        // the id is a UUID string, so hash it down.
        const quint32 subKey = qHash(id);
        coalescer->post(IpcMsgType::PushUsenetQueueItem, [this, id] {
            IpcMessage msg(IpcMsgType::PushUsenetQueueItem, 0);
            if (auto* session = m_usenetSession.get(); session && session->queue()) {
                if (const auto* item = session->queue()->findItem(id))
                    msg.append(usenetQueueItemToCbor(*item));
            }
            return msg;
        }, kUsenetPushWindowMs, subKey);
    };

    connect(m_usenetSession.get(), &usenet::UsenetSession::itemChanged, this, pushItem);
    connect(m_usenetSession.get(), &usenet::UsenetSession::itemAdded, this, pushItem);

    connect(m_usenetSession.get(), &usenet::UsenetSession::itemRemoved, this,
            [this](const QString& id) {
        // Not coalesced: a removal that arrives after a later change for the same
        // key would be dropped, and the GUI would keep showing a row for an item
        // that no longer exists.
        IpcMessage msg(IpcMsgType::PushUsenetItemRemoved, 0);
        msg.append(id);
        m_ipcServer->broadcast(msg);
    });

    connect(m_usenetSession.get(), &usenet::UsenetSession::itemFinished, this,
            [this](const QString& id, bool success, const QString& message) {
        // Also uncoalesced, and for the reason onServerStateChanged is: this is a
        // transition rather than a latest value. A completion collapsed inside a
        // window is a notification the user asked for and never got.
        IpcMessage msg(IpcMsgType::PushUsenetItemFinished, 0);
        msg.append(id);
        msg.append(success);
        msg.append(message);
        m_ipcServer->broadcast(msg);
    });
}

void DaemonApp::applyUsenetServers()
{
    if (!m_usenetSession)
        return;

    m_usenetSession->applyPreferences();

    // The enable switch can flip in the same save that changed the servers.
    if (thePrefs.usenetEnabled() && !m_usenetSession->isRunning())
        m_usenetSession->start();
    else if (!thePrefs.usenetEnabled() && m_usenetSession->isRunning())
        m_usenetSession->stop();
}

void DaemonApp::applyIndexerConfig()
{
    if (m_indexerSearches)
        m_indexerSearches->applyPreferences();
}

void DaemonApp::connectIndexerPushes()
{
    if (!m_indexerSearches || !m_ipcServer)
        return;

    connect(m_indexerSearches.get(), &indexer::IndexerSearchList::resultsReady, this,
            [this](quint32 searchId, const QList<indexer::IndexerResult>& rows) {
        // Not coalesced. Every push carries a *different* batch of rows — this is
        // an append, not a latest value — so a coalescing window would not merge
        // them, it would throw all but the last away.
        IpcMessage msg(IpcMsgType::PushIndexerResults, 0);
        msg.append(static_cast<qint64>(searchId));
        QCborArray out;
        for (const auto& row : rows)
            out.append(indexerResultToCbor(row));
        msg.append(out);
        m_ipcServer->broadcast(msg);
    });

    connect(m_indexerSearches.get(), &indexer::IndexerSearchList::searchProgress, this,
            [this](quint32 searchId, int done, int total) {
        IpcMessage msg(IpcMsgType::PushIndexerProgress, 0);
        msg.append(static_cast<qint64>(searchId));
        msg.append(static_cast<qint64>(done));
        msg.append(static_cast<qint64>(total));
        m_ipcServer->broadcast(msg);
    });

    connect(m_indexerSearches.get(), &indexer::IndexerSearchList::searchFinished, this,
            [this](quint32 searchId, const QString& error) {
        IpcMessage msg(IpcMsgType::PushIndexerSearchDone, 0);
        msg.append(static_cast<qint64>(searchId));
        msg.append(error);
        m_ipcServer->broadcast(msg);
    });
}

void DaemonApp::stopWebServer()
{
    if (m_webServer) {
        m_webServer->stop();
        m_webServer.reset();
    }
    if (m_coreSession)
        m_coreSession->updatePortMappings();
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
        closeLogFileSink();
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
    qint64 logId;
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
    push.append(static_cast<qint64>(logId));
    push.append(QString::fromUtf8(cat));
    push.append(static_cast<qint64>(type));
    push.append(msg);
    push.append(ts);
    s_instance->m_ipcServer->broadcast(push);
}

void DaemonApp::applyLogFilterRules()
{
    // Refresh the emit-site gate for the dedicated server-verbose channel.
    setServerVerboseLogging(thePrefs.serverVerboseLog());

    // Compose category filter rules. Rules are evaluated top-to-bottom, so the
    // per-subsystem overrides below win over the "all off" baseline. Enabling a
    // subsystem's *.debug just lets its qCDebug lines reach the handler; the
    // actual show/hide is still gated by the pref (verbose) or the emit-site
    // bool (kad / server-verbose), so this never leaks one channel into another.
    QStringList rules;
    rules << QStringLiteral("emule.*.debug=false");
    if (thePrefs.verbose())
        rules << QStringLiteral("emule.*.debug=true");
    if (thePrefs.kadVerboseLog())
        rules << QStringLiteral("emule.kad.debug=true");
    if (thePrefs.serverVerboseLog())
        rules << QStringLiteral("emule.serverv.debug=true");

    QLoggingCategory::setFilterRules(rules.join(QLatin1Char('\n')));
}

void DaemonApp::applyLogFileSettings()
{
    applyLogFileSink(AppConfig::configDir(), QStringLiteral("emulecored"),
                     thePrefs.logToDiskCore(), thePrefs.maxLogFileSize());
}

} // namespace eMule
