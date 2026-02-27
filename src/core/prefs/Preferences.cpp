/// @file Preferences.cpp
/// @brief Central preferences with YAML persistence — implementation.

#include "prefs/Preferences.h"

#include "net/EMSocket.h"
#include "net/EncryptedStreamSocket.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

#include <yaml-cpp/yaml.h>

#include <random>

namespace eMule {

// ---------------------------------------------------------------------------
// Data struct — all settings with default values
// ---------------------------------------------------------------------------

struct Preferences::Data {
    // General
    QString nick = QStringLiteral("eMule User");
    std::array<uint8, 16> userHash{};
    bool autoConnect = false;
    bool reconnect = true;
    bool filterLANIPs = true;

    // Server connection
    bool safeServerConnect = true;      // Limit to 1 concurrent connection attempt
    bool autoConnectStaticOnly = false; // Only connect to static servers
    bool useServerPriorities = true;    // Sort servers by priority before connecting
    bool addServersFromServer = true;   // Request server list from connected server
    uint32 serverKeepAliveTimeout = 0;  // Keep-alive interval (ms), 0 = disabled

    // Network
    uint16 port = 0;              // 0 = random
    uint16 udpPort = 0;
    uint16 serverUDPPort = 65535; // 65535 = random, 0 = disabled
    uint16 maxConnections = 500;
    uint16 maxHalfConnections = 9;
    QString bindAddress;

    // Bandwidth (KB/s)
    uint32 maxUpload = 80;
    uint32 maxDownload = 90;
    uint32 minUpload = 1;
    uint32 maxGraphUploadRate = 100;
    uint32 maxGraphDownloadRate = 100;

    // Encryption
    bool cryptLayerSupported = true;
    bool cryptLayerRequested = true;
    bool cryptLayerRequired = false;
    uint8 cryptTCPPaddingLength = 128;

    // Proxy
    int proxyType = 0;  // PROXYTYPE_NOPROXY
    QString proxyHost;
    uint16 proxyPort = 1080;
    bool proxyEnablePassword = false;
    QString proxyUser;
    QString proxyPassword;

    // Directories
    QString incomingDir;
    QStringList tempDirs;
    QString configDir;
    QString fileCommentsFilePath;

    // UPnP
    bool enableUPnP = true;
    bool skipWANIPSetup = false;
    bool skipWANPPPSetup = false;
    bool closeUPnPOnExit = true;

    // Logging
    bool logToDisk = false;
    uint32 maxLogFileSize = 1048576; // 1 MB
    bool verbose = false;
    bool kadVerboseLog = true;
    uint32 maxLogLines = 5000;  // Max lines kept per log tab in the GUI

    // Files
    uint16 maxSourcesPerFile = 400;
    bool useICH = true;

    // Transfer
    uint32 fileBufferSize = 245760;     // 240 KB
    uint32 fileBufferTimeLimit = 60;    // seconds

    // Statistics (cumulative cross-session rates, KB/s)
    float connMaxDownRate = 0.0f;
    float connAvgDownRate = 0.0f;
    float connMaxAvgDownRate = 0.0f;
    float connAvgUpRate = 0.0f;
    float connMaxAvgUpRate = 0.0f;
    float connMaxUpRate = 0.0f;
    uint32 statsAverageMinutes = 5;  // averaging window for rate history

    // Security
    uint32 ipFilterLevel = 100;  // DFLT_FILTER_LEVEL — lower = more restrictive

    // IRC
    QString ircServer = QStringLiteral("irc.emule-project.net:6667");
    QString ircNick;
    bool ircEnableUTF8 = true;
    bool ircUsePerform = false;
    QString ircPerformString;

    // IPC Daemon
    bool ipcEnabled = true;
    uint16 ipcPort = 4712;
    QString ipcListenAddress = QStringLiteral("127.0.0.1");
    QString ipcDaemonPath;  // Empty = auto-detect next to GUI binary

    // Web Server
    bool webServerEnabled = false;
    uint16 webServerPort = 4711;        // Classic eMule web server port
    QString webServerApiKey;
    QString webServerListenAddress;     // Empty = any

    // Kademlia
    bool kadEnabled = true;
    uint32 kadUDPKey = 0;  // 0 = generate random on first run

    // Connection
    uint16 maxConsPerFive = 20;  // MAXCONPER5SEC — max connections per 5 seconds

    // Server management (extended)
    bool addServersFromClients = true;  // Accept server list from other clients
    bool filterServerByIP = false;      // Apply IP filter to server addresses

    // Network modes
    bool networkED2K = true;  // ED2K protocol enabled

    // Chat / Messages
    bool msgOnlyFriends = false;   // Only accept messages from friends
    bool msgSecure = false;        // Only accept messages from secure-identified clients
    bool useChatCaptchas = false;  // Require captcha for first messages
    bool enableSpamFilter = false; // Enable keyword-based spam filter

    // Security (extended)
    bool useSecureIdent = true;  // Enable secure identity (RSA key exchange)
    int viewSharedFilesAccess = 1;  // 0=nobody, 1=friends only, 2=everybody

    // Download behavior
    bool autoDownloadPriority = true;  // Auto-adjust download priority by source count
    bool addNewFilesPaused = false;    // Pause newly added download files

    // Disk space
    bool checkDiskspace = true;          // Monitor free disk space
    uint64 minFreeDiskSpace = 20971520;  // 20 MB minimum free space

    // Search
    bool enableSearchResultFilter = true;  // Filter search result spam

    // Network detection
    uint32 publicIP = 0;  // Our detected public IP (set by server/peers)

    // UI State (GUI-only, persisted across sessions)
    QList<int> serverSplitSizes;
    QList<int> kadSplitSizes;
    int windowWidth = 900;
    int windowHeight = 620;
    bool windowMaximized = false;
};

// ---------------------------------------------------------------------------
// Global instance
// ---------------------------------------------------------------------------

Preferences thePrefs;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Preferences::Preferences()
    : m_data(std::make_unique<Data>())
{
}

Preferences::~Preferences() = default;

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

void Preferences::setDefaults()
{
    QWriteLocker lock(&m_lock);
    m_data = std::make_unique<Data>();
}

// ---------------------------------------------------------------------------
// Getters / setters — General
// ---------------------------------------------------------------------------

QString Preferences::nick() const
{
    QReadLocker lock(&m_lock);
    return m_data->nick;
}

void Preferences::setNick(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->nick = val;
}

std::array<uint8, 16> Preferences::userHash() const
{
    QReadLocker lock(&m_lock);
    return m_data->userHash;
}

void Preferences::setUserHash(const std::array<uint8, 16>& val)
{
    QWriteLocker lock(&m_lock);
    m_data->userHash = val;
}

bool Preferences::autoConnect() const
{
    QReadLocker lock(&m_lock);
    return m_data->autoConnect;
}

void Preferences::setAutoConnect(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->autoConnect = val;
}

bool Preferences::reconnect() const
{
    QReadLocker lock(&m_lock);
    return m_data->reconnect;
}

void Preferences::setReconnect(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->reconnect = val;
}

bool Preferences::filterLANIPs() const
{
    QReadLocker lock(&m_lock);
    return m_data->filterLANIPs;
}

void Preferences::setFilterLANIPs(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->filterLANIPs = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Server connection
// ---------------------------------------------------------------------------

bool Preferences::safeServerConnect() const
{
    QReadLocker lock(&m_lock);
    return m_data->safeServerConnect;
}

void Preferences::setSafeServerConnect(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->safeServerConnect = val;
}

bool Preferences::autoConnectStaticOnly() const
{
    QReadLocker lock(&m_lock);
    return m_data->autoConnectStaticOnly;
}

void Preferences::setAutoConnectStaticOnly(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->autoConnectStaticOnly = val;
}

bool Preferences::useServerPriorities() const
{
    QReadLocker lock(&m_lock);
    return m_data->useServerPriorities;
}

void Preferences::setUseServerPriorities(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->useServerPriorities = val;
}

bool Preferences::addServersFromServer() const
{
    QReadLocker lock(&m_lock);
    return m_data->addServersFromServer;
}

void Preferences::setAddServersFromServer(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->addServersFromServer = val;
}

uint32 Preferences::serverKeepAliveTimeout() const
{
    QReadLocker lock(&m_lock);
    return m_data->serverKeepAliveTimeout;
}

void Preferences::setServerKeepAliveTimeout(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->serverKeepAliveTimeout = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Network
// ---------------------------------------------------------------------------

uint16 Preferences::port() const
{
    QReadLocker lock(&m_lock);
    return m_data->port;
}

void Preferences::setPort(uint16 val)
{
    QWriteLocker lock(&m_lock);
    m_data->port = val;
}

uint16 Preferences::udpPort() const
{
    QReadLocker lock(&m_lock);
    return m_data->udpPort;
}

void Preferences::setUdpPort(uint16 val)
{
    QWriteLocker lock(&m_lock);
    m_data->udpPort = val;
}

uint16 Preferences::serverUDPPort() const
{
    QReadLocker lock(&m_lock);
    return m_data->serverUDPPort;
}

void Preferences::setServerUDPPort(uint16 val)
{
    QWriteLocker lock(&m_lock);
    m_data->serverUDPPort = val;
}

uint16 Preferences::maxConnections() const
{
    QReadLocker lock(&m_lock);
    return m_data->maxConnections;
}

void Preferences::setMaxConnections(uint16 val)
{
    QWriteLocker lock(&m_lock);
    m_data->maxConnections = val;
}

uint16 Preferences::maxHalfConnections() const
{
    QReadLocker lock(&m_lock);
    return m_data->maxHalfConnections;
}

void Preferences::setMaxHalfConnections(uint16 val)
{
    QWriteLocker lock(&m_lock);
    m_data->maxHalfConnections = val;
}

QString Preferences::bindAddress() const
{
    QReadLocker lock(&m_lock);
    return m_data->bindAddress;
}

void Preferences::setBindAddress(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->bindAddress = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Bandwidth
// ---------------------------------------------------------------------------

uint32 Preferences::maxUpload() const
{
    QReadLocker lock(&m_lock);
    return m_data->maxUpload;
}

void Preferences::setMaxUpload(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->maxUpload = val;
}

uint32 Preferences::maxDownload() const
{
    QReadLocker lock(&m_lock);
    return m_data->maxDownload;
}

void Preferences::setMaxDownload(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->maxDownload = val;
}

uint32 Preferences::minUpload() const
{
    QReadLocker lock(&m_lock);
    return m_data->minUpload;
}

void Preferences::setMinUpload(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->minUpload = val;
}

uint32 Preferences::maxGraphUploadRate() const
{
    QReadLocker lock(&m_lock);
    return m_data->maxGraphUploadRate;
}

void Preferences::setMaxGraphUploadRate(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->maxGraphUploadRate = val;
}

uint32 Preferences::maxGraphDownloadRate() const
{
    QReadLocker lock(&m_lock);
    return m_data->maxGraphDownloadRate;
}

void Preferences::setMaxGraphDownloadRate(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->maxGraphDownloadRate = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Encryption
// ---------------------------------------------------------------------------

bool Preferences::cryptLayerSupported() const
{
    QReadLocker lock(&m_lock);
    return m_data->cryptLayerSupported;
}

void Preferences::setCryptLayerSupported(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->cryptLayerSupported = val;
}

bool Preferences::cryptLayerRequested() const
{
    QReadLocker lock(&m_lock);
    return m_data->cryptLayerRequested;
}

void Preferences::setCryptLayerRequested(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->cryptLayerRequested = val;
}

bool Preferences::cryptLayerRequired() const
{
    QReadLocker lock(&m_lock);
    return m_data->cryptLayerRequired;
}

void Preferences::setCryptLayerRequired(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->cryptLayerRequired = val;
}

uint8 Preferences::cryptTCPPaddingLength() const
{
    QReadLocker lock(&m_lock);
    return m_data->cryptTCPPaddingLength;
}

void Preferences::setCryptTCPPaddingLength(uint8 val)
{
    QWriteLocker lock(&m_lock);
    m_data->cryptTCPPaddingLength = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Proxy
// ---------------------------------------------------------------------------

int Preferences::proxyType() const
{
    QReadLocker lock(&m_lock);
    return m_data->proxyType;
}

void Preferences::setProxyType(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->proxyType = val;
}

QString Preferences::proxyHost() const
{
    QReadLocker lock(&m_lock);
    return m_data->proxyHost;
}

void Preferences::setProxyHost(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->proxyHost = val;
}

uint16 Preferences::proxyPort() const
{
    QReadLocker lock(&m_lock);
    return m_data->proxyPort;
}

void Preferences::setProxyPort(uint16 val)
{
    QWriteLocker lock(&m_lock);
    m_data->proxyPort = val;
}

bool Preferences::proxyEnablePassword() const
{
    QReadLocker lock(&m_lock);
    return m_data->proxyEnablePassword;
}

void Preferences::setProxyEnablePassword(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->proxyEnablePassword = val;
}

QString Preferences::proxyUser() const
{
    QReadLocker lock(&m_lock);
    return m_data->proxyUser;
}

void Preferences::setProxyUser(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->proxyUser = val;
}

QString Preferences::proxyPassword() const
{
    QReadLocker lock(&m_lock);
    return m_data->proxyPassword;
}

void Preferences::setProxyPassword(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->proxyPassword = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Directories
// ---------------------------------------------------------------------------

QString Preferences::incomingDir() const
{
    QReadLocker lock(&m_lock);
    return m_data->incomingDir;
}

void Preferences::setIncomingDir(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->incomingDir = val;
}

QStringList Preferences::tempDirs() const
{
    QReadLocker lock(&m_lock);
    return m_data->tempDirs;
}

void Preferences::setTempDirs(const QStringList& val)
{
    QWriteLocker lock(&m_lock);
    m_data->tempDirs = val;
}

QString Preferences::configDir() const
{
    QReadLocker lock(&m_lock);
    return m_data->configDir;
}

void Preferences::setConfigDir(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->configDir = val;
}

QString Preferences::fileCommentsFilePath() const
{
    QReadLocker lock(&m_lock);
    return m_data->fileCommentsFilePath;
}

void Preferences::setFileCommentsFilePath(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->fileCommentsFilePath = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — UPnP
// ---------------------------------------------------------------------------

bool Preferences::enableUPnP() const
{
    QReadLocker lock(&m_lock);
    return m_data->enableUPnP;
}

void Preferences::setEnableUPnP(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->enableUPnP = val;
}

bool Preferences::skipWANIPSetup() const
{
    QReadLocker lock(&m_lock);
    return m_data->skipWANIPSetup;
}

void Preferences::setSkipWANIPSetup(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->skipWANIPSetup = val;
}

bool Preferences::skipWANPPPSetup() const
{
    QReadLocker lock(&m_lock);
    return m_data->skipWANPPPSetup;
}

void Preferences::setSkipWANPPPSetup(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->skipWANPPPSetup = val;
}

bool Preferences::closeUPnPOnExit() const
{
    QReadLocker lock(&m_lock);
    return m_data->closeUPnPOnExit;
}

void Preferences::setCloseUPnPOnExit(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->closeUPnPOnExit = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Logging
// ---------------------------------------------------------------------------

bool Preferences::logToDisk() const
{
    QReadLocker lock(&m_lock);
    return m_data->logToDisk;
}

void Preferences::setLogToDisk(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->logToDisk = val;
}

uint32 Preferences::maxLogFileSize() const
{
    QReadLocker lock(&m_lock);
    return m_data->maxLogFileSize;
}

void Preferences::setMaxLogFileSize(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->maxLogFileSize = val;
}

bool Preferences::verbose() const
{
    QReadLocker lock(&m_lock);
    return m_data->verbose;
}

void Preferences::setVerbose(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->verbose = val;
}

bool Preferences::kadVerboseLog() const
{
    QReadLocker lock(&m_lock);
    return m_data->kadVerboseLog;
}

void Preferences::setKadVerboseLog(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->kadVerboseLog = val;
}

uint32 Preferences::maxLogLines() const
{
    QReadLocker lock(&m_lock);
    return m_data->maxLogLines;
}

void Preferences::setMaxLogLines(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->maxLogLines = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Files
// ---------------------------------------------------------------------------

uint16 Preferences::maxSourcesPerFile() const
{
    QReadLocker lock(&m_lock);
    return m_data->maxSourcesPerFile;
}

void Preferences::setMaxSourcesPerFile(uint16 val)
{
    QWriteLocker lock(&m_lock);
    m_data->maxSourcesPerFile = val;
}

bool Preferences::useICH() const
{
    QReadLocker lock(&m_lock);
    return m_data->useICH;
}

void Preferences::setUseICH(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->useICH = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Transfer
// ---------------------------------------------------------------------------

uint32 Preferences::fileBufferSize() const
{
    QReadLocker lock(&m_lock);
    return m_data->fileBufferSize;
}

void Preferences::setFileBufferSize(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->fileBufferSize = val;
}

uint32 Preferences::fileBufferTimeLimit() const
{
    QReadLocker lock(&m_lock);
    return m_data->fileBufferTimeLimit;
}

void Preferences::setFileBufferTimeLimit(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->fileBufferTimeLimit = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Statistics
// ---------------------------------------------------------------------------

float Preferences::connMaxDownRate() const
{
    QReadLocker lock(&m_lock);
    return m_data->connMaxDownRate;
}

void Preferences::setConnMaxDownRate(float val)
{
    QWriteLocker lock(&m_lock);
    m_data->connMaxDownRate = val;
}

float Preferences::connAvgDownRate() const
{
    QReadLocker lock(&m_lock);
    return m_data->connAvgDownRate;
}

void Preferences::setConnAvgDownRate(float val)
{
    QWriteLocker lock(&m_lock);
    m_data->connAvgDownRate = val;
}

float Preferences::connMaxAvgDownRate() const
{
    QReadLocker lock(&m_lock);
    return m_data->connMaxAvgDownRate;
}

void Preferences::setConnMaxAvgDownRate(float val)
{
    QWriteLocker lock(&m_lock);
    m_data->connMaxAvgDownRate = val;
}

float Preferences::connAvgUpRate() const
{
    QReadLocker lock(&m_lock);
    return m_data->connAvgUpRate;
}

void Preferences::setConnAvgUpRate(float val)
{
    QWriteLocker lock(&m_lock);
    m_data->connAvgUpRate = val;
}

float Preferences::connMaxAvgUpRate() const
{
    QReadLocker lock(&m_lock);
    return m_data->connMaxAvgUpRate;
}

void Preferences::setConnMaxAvgUpRate(float val)
{
    QWriteLocker lock(&m_lock);
    m_data->connMaxAvgUpRate = val;
}

float Preferences::connMaxUpRate() const
{
    QReadLocker lock(&m_lock);
    return m_data->connMaxUpRate;
}

void Preferences::setConnMaxUpRate(float val)
{
    QWriteLocker lock(&m_lock);
    m_data->connMaxUpRate = val;
}

uint32 Preferences::statsAverageMinutes() const
{
    QReadLocker lock(&m_lock);
    return m_data->statsAverageMinutes;
}

void Preferences::setStatsAverageMinutes(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->statsAverageMinutes = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Security
// ---------------------------------------------------------------------------

uint32 Preferences::ipFilterLevel() const
{
    QReadLocker lock(&m_lock);
    return m_data->ipFilterLevel;
}

void Preferences::setIpFilterLevel(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->ipFilterLevel = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — IRC
// ---------------------------------------------------------------------------

QString Preferences::ircServer() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircServer;
}

void Preferences::setIrcServer(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircServer = val;
}

QString Preferences::ircNick() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircNick;
}

void Preferences::setIrcNick(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircNick = val;
}

bool Preferences::ircEnableUTF8() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircEnableUTF8;
}

void Preferences::setIrcEnableUTF8(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircEnableUTF8 = val;
}

bool Preferences::ircUsePerform() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircUsePerform;
}

void Preferences::setIrcUsePerform(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircUsePerform = val;
}

QString Preferences::ircPerformString() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircPerformString;
}

void Preferences::setIrcPerformString(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircPerformString = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — IPC Daemon
// ---------------------------------------------------------------------------

bool Preferences::ipcEnabled() const
{
    QReadLocker lock(&m_lock);
    return m_data->ipcEnabled;
}

void Preferences::setIpcEnabled(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->ipcEnabled = val;
}

uint16 Preferences::ipcPort() const
{
    QReadLocker lock(&m_lock);
    return m_data->ipcPort;
}

void Preferences::setIpcPort(uint16 val)
{
    QWriteLocker lock(&m_lock);
    m_data->ipcPort = val;
}

QString Preferences::ipcListenAddress() const
{
    QReadLocker lock(&m_lock);
    return m_data->ipcListenAddress;
}

void Preferences::setIpcListenAddress(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->ipcListenAddress = val;
}

QString Preferences::ipcDaemonPath() const
{
    QReadLocker lock(&m_lock);
    return m_data->ipcDaemonPath;
}

void Preferences::setIpcDaemonPath(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->ipcDaemonPath = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Web Server
// ---------------------------------------------------------------------------

bool Preferences::webServerEnabled() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerEnabled;
}

void Preferences::setWebServerEnabled(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerEnabled = val;
}

uint16 Preferences::webServerPort() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerPort;
}

void Preferences::setWebServerPort(uint16 val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerPort = val;
}

QString Preferences::webServerApiKey() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerApiKey;
}

void Preferences::setWebServerApiKey(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerApiKey = val;
}

QString Preferences::webServerListenAddress() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerListenAddress;
}

void Preferences::setWebServerListenAddress(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerListenAddress = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Kademlia
// ---------------------------------------------------------------------------

bool Preferences::kadEnabled() const
{
    QReadLocker lock(&m_lock);
    return m_data->kadEnabled;
}

void Preferences::setKadEnabled(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->kadEnabled = val;
}

uint32 Preferences::kadUDPKey() const
{
    QReadLocker lock(&m_lock);
    return m_data->kadUDPKey;
}

void Preferences::setKadUDPKey(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->kadUDPKey = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Connection
// ---------------------------------------------------------------------------

uint16 Preferences::maxConsPerFive() const
{
    QReadLocker lock(&m_lock);
    return m_data->maxConsPerFive;
}

void Preferences::setMaxConsPerFive(uint16 val)
{
    QWriteLocker lock(&m_lock);
    m_data->maxConsPerFive = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Server management (extended)
// ---------------------------------------------------------------------------

bool Preferences::addServersFromClients() const
{
    QReadLocker lock(&m_lock);
    return m_data->addServersFromClients;
}

void Preferences::setAddServersFromClients(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->addServersFromClients = val;
}

bool Preferences::filterServerByIP() const
{
    QReadLocker lock(&m_lock);
    return m_data->filterServerByIP;
}

void Preferences::setFilterServerByIP(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->filterServerByIP = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Network modes
// ---------------------------------------------------------------------------

bool Preferences::networkED2K() const
{
    QReadLocker lock(&m_lock);
    return m_data->networkED2K;
}

void Preferences::setNetworkED2K(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->networkED2K = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Chat / Messages
// ---------------------------------------------------------------------------

bool Preferences::msgOnlyFriends() const
{
    QReadLocker lock(&m_lock);
    return m_data->msgOnlyFriends;
}

void Preferences::setMsgOnlyFriends(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->msgOnlyFriends = val;
}

bool Preferences::msgSecure() const
{
    QReadLocker lock(&m_lock);
    return m_data->msgSecure;
}

void Preferences::setMsgSecure(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->msgSecure = val;
}

bool Preferences::useChatCaptchas() const
{
    QReadLocker lock(&m_lock);
    return m_data->useChatCaptchas;
}

void Preferences::setUseChatCaptchas(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->useChatCaptchas = val;
}

bool Preferences::enableSpamFilter() const
{
    QReadLocker lock(&m_lock);
    return m_data->enableSpamFilter;
}

void Preferences::setEnableSpamFilter(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->enableSpamFilter = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Security (extended)
// ---------------------------------------------------------------------------

bool Preferences::useSecureIdent() const
{
    QReadLocker lock(&m_lock);
    return m_data->useSecureIdent;
}

void Preferences::setUseSecureIdent(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->useSecureIdent = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Shared file visibility
// ---------------------------------------------------------------------------

int Preferences::viewSharedFilesAccess() const
{
    QReadLocker lock(&m_lock);
    return m_data->viewSharedFilesAccess;
}

void Preferences::setViewSharedFilesAccess(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->viewSharedFilesAccess = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Download behavior
// ---------------------------------------------------------------------------

bool Preferences::autoDownloadPriority() const
{
    QReadLocker lock(&m_lock);
    return m_data->autoDownloadPriority;
}

void Preferences::setAutoDownloadPriority(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->autoDownloadPriority = val;
}

bool Preferences::addNewFilesPaused() const
{
    QReadLocker lock(&m_lock);
    return m_data->addNewFilesPaused;
}

void Preferences::setAddNewFilesPaused(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->addNewFilesPaused = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Disk space
// ---------------------------------------------------------------------------

bool Preferences::checkDiskspace() const
{
    QReadLocker lock(&m_lock);
    return m_data->checkDiskspace;
}

void Preferences::setCheckDiskspace(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->checkDiskspace = val;
}

uint64 Preferences::minFreeDiskSpace() const
{
    QReadLocker lock(&m_lock);
    return m_data->minFreeDiskSpace;
}

void Preferences::setMinFreeDiskSpace(uint64 val)
{
    QWriteLocker lock(&m_lock);
    m_data->minFreeDiskSpace = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Search
// ---------------------------------------------------------------------------

bool Preferences::enableSearchResultFilter() const
{
    QReadLocker lock(&m_lock);
    return m_data->enableSearchResultFilter;
}

void Preferences::setEnableSearchResultFilter(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->enableSearchResultFilter = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Network detection
// ---------------------------------------------------------------------------

uint32 Preferences::publicIP() const
{
    QReadLocker lock(&m_lock);
    return m_data->publicIP;
}

void Preferences::setPublicIP(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->publicIP = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — UI State
// ---------------------------------------------------------------------------

QList<int> Preferences::serverSplitSizes() const
{
    QReadLocker lock(&m_lock);
    return m_data->serverSplitSizes;
}

void Preferences::setServerSplitSizes(const QList<int>& val)
{
    QWriteLocker lock(&m_lock);
    m_data->serverSplitSizes = val;
}

QList<int> Preferences::kadSplitSizes() const
{
    QReadLocker lock(&m_lock);
    return m_data->kadSplitSizes;
}

void Preferences::setKadSplitSizes(const QList<int>& val)
{
    QWriteLocker lock(&m_lock);
    m_data->kadSplitSizes = val;
}

int Preferences::windowWidth() const
{
    QReadLocker lock(&m_lock);
    return m_data->windowWidth;
}

void Preferences::setWindowWidth(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->windowWidth = val;
}

int Preferences::windowHeight() const
{
    QReadLocker lock(&m_lock);
    return m_data->windowHeight;
}

void Preferences::setWindowHeight(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->windowHeight = val;
}

bool Preferences::windowMaximized() const
{
    QReadLocker lock(&m_lock);
    return m_data->windowMaximized;
}

void Preferences::setWindowMaximized(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->windowMaximized = val;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

void Preferences::validate()
{
    // nick: truncate to 50 chars
    if (m_data->nick.size() > 50)
        m_data->nick.truncate(50);

    // minUpload: clamp min=1
    if (m_data->minUpload < 1)
        m_data->minUpload = 1;

    // maxUpload: clamp ≤ maxGraphUploadRate (unless unlimited=0)
    if (m_data->maxUpload > 0 && m_data->maxGraphUploadRate > 0
        && m_data->maxUpload > m_data->maxGraphUploadRate)
        m_data->maxUpload = m_data->maxGraphUploadRate;

    // maxConnections: clamp 1–65535
    if (m_data->maxConnections < 1)
        m_data->maxConnections = 1;

    // maxHalfConnections: clamp 1–100
    m_data->maxHalfConnections = std::clamp<uint16>(m_data->maxHalfConnections, 1, 100);

    // cryptTCPPaddingLength: clamp 0–254
    if (m_data->cryptTCPPaddingLength > 254)
        m_data->cryptTCPPaddingLength = 254;

    // proxyType: clamp 0–5
    if (m_data->proxyType < 0 || m_data->proxyType > 5)
        m_data->proxyType = 0;

    // maxLogFileSize: min 1024
    if (m_data->maxLogFileSize < 1024)
        m_data->maxLogFileSize = 1024;

    // maxSourcesPerFile: clamp 1–5000
    m_data->maxSourcesPerFile = std::clamp<uint16>(m_data->maxSourcesPerFile, 1, 5000);

    // maxConsPerFive: clamp 1–50
    m_data->maxConsPerFive = std::clamp<uint16>(m_data->maxConsPerFive, 1, 50);

    // viewSharedFilesAccess: clamp 0–2
    m_data->viewSharedFilesAccess = std::clamp(m_data->viewSharedFilesAccess, 0, 2);

    // kadUDPKey: generate random if 0
    if (m_data->kadUDPKey == 0) {
        std::uniform_int_distribution<uint32> dist(1, UINT32_MAX);
        m_data->kadUDPKey = dist(randomEngine());
    }
}

// ---------------------------------------------------------------------------
// Directory resolution
// ---------------------------------------------------------------------------

void Preferences::resolveDefaultDirectories()
{
#ifdef Q_OS_MACOS
    const QString baseDir = QDir::homePath() + QStringLiteral("/eMuleQt");
#else
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
#endif

    if (m_data->incomingDir.isEmpty())
        m_data->incomingDir = baseDir + QStringLiteral("/Incoming");

    if (m_data->configDir.isEmpty())
        m_data->configDir = baseDir + QStringLiteral("/Config");

    if (m_data->tempDirs.isEmpty())
        m_data->tempDirs.append(baseDir + QStringLiteral("/Temp"));
}

// ---------------------------------------------------------------------------
// YAML persistence — load
// ---------------------------------------------------------------------------

bool Preferences::load(const QString& filePath)
{
    QWriteLocker lock(&m_lock);

    m_filePath = filePath;
    m_data = std::make_unique<Data>();

    if (!QFile::exists(filePath)) {
        // First run — generate user hash, resolve ports & directories
        m_data->userHash = generateUserHash();
        if (m_data->port == 0)
            m_data->port = randomTCPPort();
        if (m_data->udpPort == 0)
            m_data->udpPort = randomUDPPort();
        validate();
        resolveDefaultDirectories();

        // Create directories and persist initial preferences
        QDir().mkpath(m_data->configDir);
        QDir().mkpath(m_data->incomingDir);
        for (const auto& td : m_data->tempDirs)
            QDir().mkpath(td);
        saveImpl(filePath);
        return true;
    }

    try {
        const YAML::Node root = YAML::LoadFile(filePath.toStdString());

        // General
        if (auto g = root["general"]) {
            m_data->nick = QString::fromStdString(g["nick"].as<std::string>(m_data->nick.toStdString()));
            m_data->autoConnect = g["autoConnect"].as<bool>(m_data->autoConnect);
            m_data->reconnect = g["reconnect"].as<bool>(m_data->reconnect);
            m_data->filterLANIPs = g["filterLANIPs"].as<bool>(m_data->filterLANIPs);

            // userHash: decode from hex
            if (g["userHash"]) {
                auto hexStr = QString::fromStdString(g["userHash"].as<std::string>(""));
                if (hexStr.size() == 32) {
                    std::array<uint8, 16> hash{};
                    if (decodeBase16(hexStr, hash.data(), 16) == 16)
                        m_data->userHash = hash;
                }
            }
        }

        // Server connection
        if (auto s = root["serverConnection"]) {
            m_data->safeServerConnect = s["safeServerConnect"].as<bool>(m_data->safeServerConnect);
            m_data->autoConnectStaticOnly = s["autoConnectStaticOnly"].as<bool>(m_data->autoConnectStaticOnly);
            m_data->useServerPriorities = s["useServerPriorities"].as<bool>(m_data->useServerPriorities);
            m_data->addServersFromServer = s["addServersFromServer"].as<bool>(m_data->addServersFromServer);
            m_data->serverKeepAliveTimeout = s["serverKeepAliveTimeout"].as<uint32>(m_data->serverKeepAliveTimeout);
            m_data->addServersFromClients = s["addServersFromClients"].as<bool>(m_data->addServersFromClients);
            m_data->filterServerByIP = s["filterServerByIP"].as<bool>(m_data->filterServerByIP);
        }

        // Network
        if (auto n = root["network"]) {
            m_data->port = static_cast<uint16>(n["port"].as<int>(m_data->port));
            m_data->udpPort = static_cast<uint16>(n["udpPort"].as<int>(m_data->udpPort));
            m_data->serverUDPPort = static_cast<uint16>(n["serverUDPPort"].as<int>(m_data->serverUDPPort));
            m_data->maxConnections = static_cast<uint16>(n["maxConnections"].as<int>(m_data->maxConnections));
            m_data->maxHalfConnections = static_cast<uint16>(n["maxHalfConnections"].as<int>(m_data->maxHalfConnections));
            m_data->bindAddress = QString::fromStdString(n["bindAddress"].as<std::string>(m_data->bindAddress.toStdString()));
            m_data->maxConsPerFive = static_cast<uint16>(n["maxConsPerFive"].as<int>(m_data->maxConsPerFive));
            m_data->networkED2K = n["networkED2K"].as<bool>(m_data->networkED2K);
            m_data->publicIP = n["publicIP"].as<uint32>(m_data->publicIP);
        }

        // Bandwidth
        if (auto b = root["bandwidth"]) {
            m_data->maxUpload = b["maxUpload"].as<uint32>(m_data->maxUpload);
            m_data->maxDownload = b["maxDownload"].as<uint32>(m_data->maxDownload);
            m_data->minUpload = b["minUpload"].as<uint32>(m_data->minUpload);
            m_data->maxGraphUploadRate = b["maxGraphUploadRate"].as<uint32>(m_data->maxGraphUploadRate);
            m_data->maxGraphDownloadRate = b["maxGraphDownloadRate"].as<uint32>(m_data->maxGraphDownloadRate);
        }

        // Encryption
        if (auto e = root["encryption"]) {
            m_data->cryptLayerSupported = e["cryptLayerSupported"].as<bool>(m_data->cryptLayerSupported);
            m_data->cryptLayerRequested = e["cryptLayerRequested"].as<bool>(m_data->cryptLayerRequested);
            m_data->cryptLayerRequired = e["cryptLayerRequired"].as<bool>(m_data->cryptLayerRequired);
            m_data->cryptTCPPaddingLength = static_cast<uint8>(e["cryptTCPPaddingLength"].as<int>(m_data->cryptTCPPaddingLength));
        }

        // Proxy
        if (auto p = root["proxy"]) {
            m_data->proxyType = p["type"].as<int>(m_data->proxyType);
            m_data->proxyHost = QString::fromStdString(p["host"].as<std::string>(m_data->proxyHost.toStdString()));
            m_data->proxyPort = static_cast<uint16>(p["port"].as<int>(m_data->proxyPort));
            m_data->proxyEnablePassword = p["enablePassword"].as<bool>(m_data->proxyEnablePassword);
            m_data->proxyUser = QString::fromStdString(p["user"].as<std::string>(m_data->proxyUser.toStdString()));
            m_data->proxyPassword = QString::fromStdString(p["password"].as<std::string>(m_data->proxyPassword.toStdString()));
        }

        // Directories
        if (auto d = root["directories"]) {
            m_data->incomingDir = QString::fromStdString(d["incomingDir"].as<std::string>(m_data->incomingDir.toStdString()));
            m_data->configDir = QString::fromStdString(d["configDir"].as<std::string>(m_data->configDir.toStdString()));
            m_data->fileCommentsFilePath = QString::fromStdString(d["fileCommentsFilePath"].as<std::string>(m_data->fileCommentsFilePath.toStdString()));

            if (d["tempDirs"] && d["tempDirs"].IsSequence()) {
                m_data->tempDirs.clear();
                for (const auto& item : d["tempDirs"])
                    m_data->tempDirs.append(QString::fromStdString(item.as<std::string>("")));
            }
        }

        // UPnP
        if (auto u = root["upnp"]) {
            m_data->enableUPnP = u["enableUPnP"].as<bool>(m_data->enableUPnP);
            m_data->skipWANIPSetup = u["skipWANIPSetup"].as<bool>(m_data->skipWANIPSetup);
            m_data->skipWANPPPSetup = u["skipWANPPPSetup"].as<bool>(m_data->skipWANPPPSetup);
            m_data->closeUPnPOnExit = u["closeUPnPOnExit"].as<bool>(m_data->closeUPnPOnExit);
        }

        // Logging
        if (auto l = root["logging"]) {
            m_data->logToDisk = l["logToDisk"].as<bool>(m_data->logToDisk);
            m_data->maxLogFileSize = l["maxLogFileSize"].as<uint32>(m_data->maxLogFileSize);
            m_data->verbose = l["verbose"].as<bool>(m_data->verbose);
            m_data->kadVerboseLog = l["kadVerboseLog"].as<bool>(m_data->kadVerboseLog);
            m_data->maxLogLines = l["maxLogLines"].as<uint32>(m_data->maxLogLines);
        }

        // Files
        if (auto f = root["files"]) {
            m_data->maxSourcesPerFile = static_cast<uint16>(f["maxSourcesPerFile"].as<int>(m_data->maxSourcesPerFile));
            m_data->useICH = f["useICH"].as<bool>(m_data->useICH);
            m_data->checkDiskspace = f["checkDiskspace"].as<bool>(m_data->checkDiskspace);
            m_data->minFreeDiskSpace = f["minFreeDiskSpace"].as<uint64>(m_data->minFreeDiskSpace);
        }

        // Transfer
        if (auto t = root["transfer"]) {
            m_data->fileBufferSize = t["fileBufferSize"].as<uint32>(m_data->fileBufferSize);
            m_data->fileBufferTimeLimit = t["fileBufferTimeLimit"].as<uint32>(m_data->fileBufferTimeLimit);
            m_data->autoDownloadPriority = t["autoDownloadPriority"].as<bool>(m_data->autoDownloadPriority);
            m_data->addNewFilesPaused = t["addNewFilesPaused"].as<bool>(m_data->addNewFilesPaused);
        }

        // Statistics
        if (auto st = root["statistics"]) {
            m_data->connMaxDownRate = st["connMaxDownRate"].as<float>(m_data->connMaxDownRate);
            m_data->connAvgDownRate = st["connAvgDownRate"].as<float>(m_data->connAvgDownRate);
            m_data->connMaxAvgDownRate = st["connMaxAvgDownRate"].as<float>(m_data->connMaxAvgDownRate);
            m_data->connAvgUpRate = st["connAvgUpRate"].as<float>(m_data->connAvgUpRate);
            m_data->connMaxAvgUpRate = st["connMaxAvgUpRate"].as<float>(m_data->connMaxAvgUpRate);
            m_data->connMaxUpRate = st["connMaxUpRate"].as<float>(m_data->connMaxUpRate);
            m_data->statsAverageMinutes = st["statsAverageMinutes"].as<uint32>(m_data->statsAverageMinutes);
        }

        // Security
        if (auto sec = root["security"]) {
            m_data->ipFilterLevel = sec["ipFilterLevel"].as<uint32>(m_data->ipFilterLevel);
            m_data->useSecureIdent = sec["useSecureIdent"].as<bool>(m_data->useSecureIdent);
            m_data->viewSharedFilesAccess = sec["viewSharedFilesAccess"].as<int>(m_data->viewSharedFilesAccess);
        }

        // IRC
        if (auto irc = root["irc"]) {
            m_data->ircServer = QString::fromStdString(irc["server"].as<std::string>(m_data->ircServer.toStdString()));
            m_data->ircNick = QString::fromStdString(irc["nick"].as<std::string>(m_data->ircNick.toStdString()));
            m_data->ircEnableUTF8 = irc["enableUTF8"].as<bool>(m_data->ircEnableUTF8);
            m_data->ircUsePerform = irc["usePerform"].as<bool>(m_data->ircUsePerform);
            m_data->ircPerformString = QString::fromStdString(irc["performString"].as<std::string>(m_data->ircPerformString.toStdString()));
        }

        // Chat / Messages
        if (auto ch = root["chat"]) {
            m_data->msgOnlyFriends = ch["msgOnlyFriends"].as<bool>(m_data->msgOnlyFriends);
            m_data->msgSecure = ch["msgSecure"].as<bool>(m_data->msgSecure);
            m_data->useChatCaptchas = ch["useChatCaptchas"].as<bool>(m_data->useChatCaptchas);
            m_data->enableSpamFilter = ch["enableSpamFilter"].as<bool>(m_data->enableSpamFilter);
        }

        // Search
        if (auto sr = root["search"]) {
            m_data->enableSearchResultFilter = sr["enableSearchResultFilter"].as<bool>(m_data->enableSearchResultFilter);
        }

        // IPC Daemon
        if (auto ipc = root["ipc"]) {
            m_data->ipcEnabled = ipc["enabled"].as<bool>(m_data->ipcEnabled);
            m_data->ipcPort = static_cast<uint16>(ipc["port"].as<int>(m_data->ipcPort));
            m_data->ipcListenAddress = QString::fromStdString(ipc["listenAddress"].as<std::string>(m_data->ipcListenAddress.toStdString()));
            m_data->ipcDaemonPath = QString::fromStdString(ipc["daemonPath"].as<std::string>(m_data->ipcDaemonPath.toStdString()));
        }

        // Web Server
        if (auto ws = root["webserver"]) {
            m_data->webServerEnabled = ws["enabled"].as<bool>(m_data->webServerEnabled);
            m_data->webServerPort = static_cast<uint16>(ws["port"].as<int>(m_data->webServerPort));
            m_data->webServerApiKey = QString::fromStdString(ws["apiKey"].as<std::string>(m_data->webServerApiKey.toStdString()));
            m_data->webServerListenAddress = QString::fromStdString(ws["listenAddress"].as<std::string>(m_data->webServerListenAddress.toStdString()));
        }

        // Kademlia
        if (auto k = root["kademlia"]) {
            m_data->kadEnabled = k["enabled"].as<bool>(m_data->kadEnabled);
            m_data->kadUDPKey = k["udpKey"].as<uint32>(m_data->kadUDPKey);
        }

        // UI State
        if (auto ui = root["uistate"]) {
            if (ui["serverSplitSizes"] && ui["serverSplitSizes"].IsSequence()) {
                m_data->serverSplitSizes.clear();
                for (const auto& item : ui["serverSplitSizes"])
                    m_data->serverSplitSizes.append(item.as<int>(0));
            }
            if (ui["kadSplitSizes"] && ui["kadSplitSizes"].IsSequence()) {
                m_data->kadSplitSizes.clear();
                for (const auto& item : ui["kadSplitSizes"])
                    m_data->kadSplitSizes.append(item.as<int>(0));
            }
            m_data->windowWidth     = ui["windowWidth"].as<int>(m_data->windowWidth);
            m_data->windowHeight    = ui["windowHeight"].as<int>(m_data->windowHeight);
            m_data->windowMaximized = ui["windowMaximized"].as<bool>(m_data->windowMaximized);
        }

    } catch (const YAML::Exception& ex) {
        logWarning(QStringLiteral("Failed to parse preferences YAML: %1 — using defaults")
                       .arg(QString::fromStdString(ex.what())));
        m_data = std::make_unique<Data>();
        m_data->userHash = generateUserHash();
        if (m_data->port == 0)
            m_data->port = randomTCPPort();
        if (m_data->udpPort == 0)
            m_data->udpPort = randomUDPPort();
        validate();
        resolveDefaultDirectories();
        return false;
    }

    // Generate user hash if missing/invalid
    if (isnulmd4(m_data->userHash.data()))
        m_data->userHash = generateUserHash();

    // Resolve port=0 → random
    if (m_data->port == 0)
        m_data->port = randomTCPPort();
    if (m_data->udpPort == 0)
        m_data->udpPort = randomUDPPort();

    validate();
    resolveDefaultDirectories();
    return true;
}

// ---------------------------------------------------------------------------
// YAML persistence — save
// ---------------------------------------------------------------------------

bool Preferences::save() const
{
    QReadLocker lock(&m_lock);
    if (m_filePath.isEmpty())
        return false;
    return saveImpl(m_filePath);
}

bool Preferences::saveTo(const QString& filePath) const
{
    QReadLocker lock(&m_lock);
    return saveImpl(filePath);
}

// ---------------------------------------------------------------------------
// Factory methods
// ---------------------------------------------------------------------------

ObfuscationConfig Preferences::obfuscationConfig() const
{
    QReadLocker lock(&m_lock);
    ObfuscationConfig cfg;
    cfg.cryptLayerEnabled = m_data->cryptLayerSupported;
    cfg.cryptLayerRequired = m_data->cryptLayerRequired;
    cfg.cryptLayerRequiredStrict = m_data->cryptLayerRequired;
    cfg.userHash = m_data->userHash;
    cfg.cryptTCPPaddingLength = m_data->cryptTCPPaddingLength;
    return cfg;
}

ProxySettings Preferences::proxySettings() const
{
    QReadLocker lock(&m_lock);
    ProxySettings ps;
    ps.useProxy = (m_data->proxyType != 0);
    ps.type = m_data->proxyType;
    ps.host = m_data->proxyHost;
    ps.port = m_data->proxyPort;
    ps.enablePassword = m_data->proxyEnablePassword;
    ps.user = m_data->proxyUser;
    ps.password = m_data->proxyPassword;
    return ps;
}

// ---------------------------------------------------------------------------
// Static utilities
// ---------------------------------------------------------------------------

uint16 Preferences::randomTCPPort()
{
    std::uniform_int_distribution<int> dist(4096, 65095);
    return static_cast<uint16>(dist(randomEngine()));
}

uint16 Preferences::randomUDPPort()
{
    std::uniform_int_distribution<int> dist(4096, 65095);
    return static_cast<uint16>(dist(randomEngine()));
}

std::array<uint8, 16> Preferences::generateUserHash()
{
    std::array<uint8, 16> hash{};
    std::uniform_int_distribution<int> dist(0, 255);
    auto& rng = randomEngine();
    for (auto& byte : hash)
        byte = static_cast<uint8>(dist(rng));

    // eMule markers — MFC Preferences.cpp:CreateUserHash()
    // Byte[5]:  14 (0x0E) — eMule client marker
    // Byte[14]: 111 (0x6F) — eMule magic value
    // Servers check these exact values to verify the client is eMule.
    // Using any other value (e.g. 0x8E) triggers anti-leecher bans.
    hash[5] = 14;
    hash[14] = 111;
    return hash;
}

// ---------------------------------------------------------------------------
// Private: YAML emitter
// ---------------------------------------------------------------------------

bool Preferences::saveImpl(const QString& filePath) const
{
    YAML::Emitter out;
    out << YAML::Comment("eMule Qt Preferences");
    out << YAML::Newline;
    out << YAML::BeginMap;

    // General
    out << YAML::Key << "general" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "nick" << YAML::Value << m_data->nick.toStdString();
    out << YAML::Key << "userHash" << YAML::Value
        << encodeBase16(std::span<const uint8>(m_data->userHash.data(), 16)).toStdString();
    out << YAML::Key << "autoConnect" << YAML::Value << m_data->autoConnect;
    out << YAML::Key << "reconnect" << YAML::Value << m_data->reconnect;
    out << YAML::Key << "filterLANIPs" << YAML::Value << m_data->filterLANIPs;
    out << YAML::EndMap;

    // Server connection
    out << YAML::Key << "serverConnection" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "safeServerConnect" << YAML::Value << m_data->safeServerConnect;
    out << YAML::Key << "autoConnectStaticOnly" << YAML::Value << m_data->autoConnectStaticOnly;
    out << YAML::Key << "useServerPriorities" << YAML::Value << m_data->useServerPriorities;
    out << YAML::Key << "addServersFromServer" << YAML::Value << m_data->addServersFromServer;
    out << YAML::Key << "serverKeepAliveTimeout" << YAML::Value << m_data->serverKeepAliveTimeout;
    out << YAML::Key << "addServersFromClients" << YAML::Value << m_data->addServersFromClients;
    out << YAML::Key << "filterServerByIP" << YAML::Value << m_data->filterServerByIP;
    out << YAML::EndMap;

    // Network
    out << YAML::Key << "network" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "port" << YAML::Value << static_cast<int>(m_data->port);
    out << YAML::Key << "udpPort" << YAML::Value << static_cast<int>(m_data->udpPort);
    out << YAML::Key << "serverUDPPort" << YAML::Value << static_cast<int>(m_data->serverUDPPort);
    out << YAML::Key << "maxConnections" << YAML::Value << static_cast<int>(m_data->maxConnections);
    out << YAML::Key << "maxHalfConnections" << YAML::Value << static_cast<int>(m_data->maxHalfConnections);
    out << YAML::Key << "bindAddress" << YAML::Value << m_data->bindAddress.toStdString();
    out << YAML::Key << "maxConsPerFive" << YAML::Value << static_cast<int>(m_data->maxConsPerFive);
    out << YAML::Key << "networkED2K" << YAML::Value << m_data->networkED2K;
    out << YAML::Key << "publicIP" << YAML::Value << m_data->publicIP;
    out << YAML::EndMap;

    // Bandwidth
    out << YAML::Key << "bandwidth" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "maxUpload" << YAML::Value << m_data->maxUpload;
    out << YAML::Key << "maxDownload" << YAML::Value << m_data->maxDownload;
    out << YAML::Key << "minUpload" << YAML::Value << m_data->minUpload;
    out << YAML::Key << "maxGraphUploadRate" << YAML::Value << m_data->maxGraphUploadRate;
    out << YAML::Key << "maxGraphDownloadRate" << YAML::Value << m_data->maxGraphDownloadRate;
    out << YAML::EndMap;

    // Encryption
    out << YAML::Key << "encryption" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "cryptLayerSupported" << YAML::Value << m_data->cryptLayerSupported;
    out << YAML::Key << "cryptLayerRequested" << YAML::Value << m_data->cryptLayerRequested;
    out << YAML::Key << "cryptLayerRequired" << YAML::Value << m_data->cryptLayerRequired;
    out << YAML::Key << "cryptTCPPaddingLength" << YAML::Value << static_cast<int>(m_data->cryptTCPPaddingLength);
    out << YAML::EndMap;

    // Proxy
    out << YAML::Key << "proxy" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "type" << YAML::Value << m_data->proxyType;
    out << YAML::Key << "host" << YAML::Value << m_data->proxyHost.toStdString();
    out << YAML::Key << "port" << YAML::Value << static_cast<int>(m_data->proxyPort);
    out << YAML::Key << "enablePassword" << YAML::Value << m_data->proxyEnablePassword;
    out << YAML::Key << "user" << YAML::Value << m_data->proxyUser.toStdString();
    out << YAML::Key << "password" << YAML::Value << m_data->proxyPassword.toStdString();
    out << YAML::EndMap;

    // Directories
    out << YAML::Key << "directories" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "incomingDir" << YAML::Value << m_data->incomingDir.toStdString();
    out << YAML::Key << "tempDirs" << YAML::Value << YAML::BeginSeq;
    for (const auto& dir : m_data->tempDirs)
        out << dir.toStdString();
    out << YAML::EndSeq;
    out << YAML::Key << "configDir" << YAML::Value << m_data->configDir.toStdString();
    out << YAML::Key << "fileCommentsFilePath" << YAML::Value << m_data->fileCommentsFilePath.toStdString();
    out << YAML::EndMap;

    // UPnP
    out << YAML::Key << "upnp" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "enableUPnP" << YAML::Value << m_data->enableUPnP;
    out << YAML::Key << "skipWANIPSetup" << YAML::Value << m_data->skipWANIPSetup;
    out << YAML::Key << "skipWANPPPSetup" << YAML::Value << m_data->skipWANPPPSetup;
    out << YAML::Key << "closeUPnPOnExit" << YAML::Value << m_data->closeUPnPOnExit;
    out << YAML::EndMap;

    // Logging
    out << YAML::Key << "logging" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "logToDisk" << YAML::Value << m_data->logToDisk;
    out << YAML::Key << "maxLogFileSize" << YAML::Value << m_data->maxLogFileSize;
    out << YAML::Key << "verbose" << YAML::Value << m_data->verbose;
    out << YAML::Key << "kadVerboseLog" << YAML::Value << m_data->kadVerboseLog;
    out << YAML::Key << "maxLogLines" << YAML::Value << m_data->maxLogLines;
    out << YAML::EndMap;

    // Files
    out << YAML::Key << "files" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "maxSourcesPerFile" << YAML::Value << static_cast<int>(m_data->maxSourcesPerFile);
    out << YAML::Key << "useICH" << YAML::Value << m_data->useICH;
    out << YAML::Key << "checkDiskspace" << YAML::Value << m_data->checkDiskspace;
    out << YAML::Key << "minFreeDiskSpace" << YAML::Value << m_data->minFreeDiskSpace;
    out << YAML::EndMap;

    // Transfer
    out << YAML::Key << "transfer" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "fileBufferSize" << YAML::Value << m_data->fileBufferSize;
    out << YAML::Key << "fileBufferTimeLimit" << YAML::Value << m_data->fileBufferTimeLimit;
    out << YAML::Key << "autoDownloadPriority" << YAML::Value << m_data->autoDownloadPriority;
    out << YAML::Key << "addNewFilesPaused" << YAML::Value << m_data->addNewFilesPaused;
    out << YAML::EndMap;

    // Statistics
    out << YAML::Key << "statistics" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "connMaxDownRate" << YAML::Value << m_data->connMaxDownRate;
    out << YAML::Key << "connAvgDownRate" << YAML::Value << m_data->connAvgDownRate;
    out << YAML::Key << "connMaxAvgDownRate" << YAML::Value << m_data->connMaxAvgDownRate;
    out << YAML::Key << "connAvgUpRate" << YAML::Value << m_data->connAvgUpRate;
    out << YAML::Key << "connMaxAvgUpRate" << YAML::Value << m_data->connMaxAvgUpRate;
    out << YAML::Key << "connMaxUpRate" << YAML::Value << m_data->connMaxUpRate;
    out << YAML::Key << "statsAverageMinutes" << YAML::Value << m_data->statsAverageMinutes;
    out << YAML::EndMap;

    // Security
    out << YAML::Key << "security" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "ipFilterLevel" << YAML::Value << m_data->ipFilterLevel;
    out << YAML::Key << "useSecureIdent" << YAML::Value << m_data->useSecureIdent;
    out << YAML::Key << "viewSharedFilesAccess" << YAML::Value << m_data->viewSharedFilesAccess;
    out << YAML::EndMap;

    // IRC
    out << YAML::Key << "irc" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "server" << YAML::Value << m_data->ircServer.toStdString();
    out << YAML::Key << "nick" << YAML::Value << m_data->ircNick.toStdString();
    out << YAML::Key << "enableUTF8" << YAML::Value << m_data->ircEnableUTF8;
    out << YAML::Key << "usePerform" << YAML::Value << m_data->ircUsePerform;
    out << YAML::Key << "performString" << YAML::Value << m_data->ircPerformString.toStdString();
    out << YAML::EndMap;

    // Chat / Messages
    out << YAML::Key << "chat" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "msgOnlyFriends" << YAML::Value << m_data->msgOnlyFriends;
    out << YAML::Key << "msgSecure" << YAML::Value << m_data->msgSecure;
    out << YAML::Key << "useChatCaptchas" << YAML::Value << m_data->useChatCaptchas;
    out << YAML::Key << "enableSpamFilter" << YAML::Value << m_data->enableSpamFilter;
    out << YAML::EndMap;

    // Search
    out << YAML::Key << "search" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "enableSearchResultFilter" << YAML::Value << m_data->enableSearchResultFilter;
    out << YAML::EndMap;

    // IPC Daemon
    out << YAML::Key << "ipc" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "enabled" << YAML::Value << m_data->ipcEnabled;
    out << YAML::Key << "port" << YAML::Value << static_cast<int>(m_data->ipcPort);
    out << YAML::Key << "listenAddress" << YAML::Value << m_data->ipcListenAddress.toStdString();
    out << YAML::Key << "daemonPath" << YAML::Value << m_data->ipcDaemonPath.toStdString();
    out << YAML::EndMap;

    // Web Server
    out << YAML::Key << "webserver" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "enabled" << YAML::Value << m_data->webServerEnabled;
    out << YAML::Key << "port" << YAML::Value << static_cast<int>(m_data->webServerPort);
    out << YAML::Key << "apiKey" << YAML::Value << m_data->webServerApiKey.toStdString();
    out << YAML::Key << "listenAddress" << YAML::Value << m_data->webServerListenAddress.toStdString();
    out << YAML::EndMap;

    // Kademlia
    out << YAML::Key << "kademlia" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "enabled" << YAML::Value << m_data->kadEnabled;
    out << YAML::Key << "udpKey" << YAML::Value << m_data->kadUDPKey;
    out << YAML::EndMap;

    // UI State
    out << YAML::Key << "uistate" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "serverSplitSizes" << YAML::Value << YAML::Flow << YAML::BeginSeq;
    for (int sz : m_data->serverSplitSizes)
        out << sz;
    out << YAML::EndSeq;
    out << YAML::Key << "kadSplitSizes" << YAML::Value << YAML::Flow << YAML::BeginSeq;
    for (int sz : m_data->kadSplitSizes)
        out << sz;
    out << YAML::EndSeq;
    out << YAML::Key << "windowWidth"     << YAML::Value << m_data->windowWidth;
    out << YAML::Key << "windowHeight"    << YAML::Value << m_data->windowHeight;
    out << YAML::Key << "windowMaximized" << YAML::Value << m_data->windowMaximized;
    out << YAML::EndMap;

    out << YAML::EndMap;

    // Atomic write via QSaveFile (temp file + rename)
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logError(QStringLiteral("Failed to open preferences file for writing: %1").arg(filePath));
        return false;
    }

    file.write(out.c_str(), static_cast<qint64>(out.size()));
    file.write("\n", 1);

    if (!file.commit()) {
        logError(QStringLiteral("Failed to commit preferences file: %1").arg(filePath));
        return false;
    }

    return true;
}

} // namespace eMule
