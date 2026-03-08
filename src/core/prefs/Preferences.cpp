#include "pch.h"
/// @file Preferences.cpp
/// @brief Central preferences with YAML persistence — implementation.

#include "prefs/Preferences.h"

#include "app/AppConfig.h"
#include "net/EMSocket.h"
#include "net/EncryptedStreamSocket.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QCborArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

#include <yaml-cpp/yaml.h>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <random>

namespace eMule {

// ---------------------------------------------------------------------------
// AES-256-CBC helpers for SMTP password encryption in YAML
// ---------------------------------------------------------------------------

namespace {

QString aesEncrypt(const QString& plaintext, const QByteArray& key)
{
    if (plaintext.isEmpty() || key.size() != 32)
        return {};

    const QByteArray pt = plaintext.toUtf8();
    QByteArray iv(16, Qt::Uninitialized);
    RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), 16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    QByteArray ct(pt.size() + 16, Qt::Uninitialized); // room for padding
    int len = 0, totalLen = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                       reinterpret_cast<const unsigned char*>(key.constData()),
                       reinterpret_cast<const unsigned char*>(iv.constData()));
    EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(ct.data()), &len,
                      reinterpret_cast<const unsigned char*>(pt.constData()), static_cast<int>(pt.size()));
    totalLen = len;
    EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(ct.data()) + len, &len);
    totalLen += len;
    EVP_CIPHER_CTX_free(ctx);

    ct.resize(totalLen);
    QByteArray combined = iv;
    combined.append(ct);
    return QString::fromLatin1(combined.toBase64());
}

QString aesDecrypt(const QString& ciphertext, const QByteArray& key)
{
    if (ciphertext.isEmpty() || key.size() != 32)
        return {};

    const QByteArray raw = QByteArray::fromBase64(ciphertext.toLatin1());
    if (raw.size() < 17) return {}; // min: 16-byte IV + 1 byte

    const QByteArray iv = raw.left(16);
    const QByteArray ct = raw.mid(16);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return {};

    QByteArray pt(ct.size() + 16, Qt::Uninitialized);
    int len = 0, totalLen = 0;

    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr,
                       reinterpret_cast<const unsigned char*>(key.constData()),
                       reinterpret_cast<const unsigned char*>(iv.constData()));
    EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(pt.data()), &len,
                      reinterpret_cast<const unsigned char*>(ct.constData()), static_cast<int>(ct.size()));
    totalLen = len;
    if (!EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(pt.data()) + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    totalLen += len;
    EVP_CIPHER_CTX_free(ctx);

    pt.resize(totalLen);
    return QString::fromUtf8(pt);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Data struct — all settings with default values
// ---------------------------------------------------------------------------

struct Preferences::Data {
    // General
    QString nick = QStringLiteral("https://emule-qt.org");
    std::array<uint8, 16> userHash{};
    bool autoConnect = true;
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
    uint32 maxUpload = 250;
    uint32 maxDownload = 500;
    uint32 minUpload = 1;
    uint32 maxGraphUploadRate = 250;
    uint32 maxGraphDownloadRate = 500;

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
    QStringList sharedDirs;

    // UPnP
    bool enableUPnP = true;
    bool skipWANIPSetup = false;
    bool skipWANPPPSetup = false;
    bool closeUPnPOnExit = true;

    // Logging
    bool logToDisk = false;
    uint32 maxLogFileSize = 1048576; // 1 MB
    bool verbose = true;
    bool kadVerboseLog = true;
    uint32 maxLogLines = 5000;  // Max lines kept per log tab in the GUI
    int logLevel = 5;               // 0-5, higher = more verbose
    bool verboseLogToDisk = false;
    bool logSourceExchange = false;
    bool logBannedClients = true;
    bool logRatingDescReceived = true;
    bool logSecureIdent = true;
    bool logFilteredIPs = true;
    bool logFileSaving = false;
    bool logA4AF = false;
    bool logUlDlEvents = true;
    bool logRawSocketPackets = false;
    bool enableIpcLog = false;        // GUI-only: show IPC tab in LogWidget
    bool startCoreWithConsole = false; // GUI-only: launch daemon in terminal window

    // Files
    uint16 maxSourcesPerFile = 400;
    bool useICH = true;
    bool autoSharedFilesPriority = true;
    bool transferFullChunks = true;
    bool previewPrio = false;
    bool startNextPausedFile = false;
    bool startNextPausedFileSameCat = false;
    bool startNextPausedFileOnlySameCat = false;
    bool rememberDownloadedFiles = true;
    bool rememberCancelledFiles = true;

    // Transfer
    uint32 fileBufferSize = 4194304;    // 4 MB
    uint32 fileBufferTimeLimit = 60;    // seconds

    // Extended (PPgTweaks)
    bool useCreditSystem = true;     // Reward uploaders
    bool a4afSaveCpu = false;        // Skip A4AF swap checks
    bool autoArchivePreviewStart = true; // Auto-scan archive contents in file details
    QString ed2kHostname;            // Hostname for own eD2K links
    bool showExtControls = true;     // Show advanced mode controls in context menus
    int commitFiles = 1;             // 0=never, 1=on shutdown, 2=always
    int extractMetaData = 1;         // 0=never, 1=MediaInfo library
    uint32 queueSize = 5000;         // Upload queue size (2000-50000)

    // Upload SpeedSense (USS)
    bool dynUpEnabled = false;
    int dynUpPingTolerance = 500;        // % of lowest ping (min 100)
    int dynUpPingToleranceMs = 200;      // absolute tolerance in ms (min 1)
    bool dynUpUseMillisecondPingTolerance = false;
    int dynUpGoingUpDivider = 1000;      // speed increase slowness (min 1)
    int dynUpGoingDownDivider = 1000;    // speed decrease slowness (min 1)
    int dynUpNumberOfPings = 1;          // ring buffer size (min 1)

#ifdef Q_OS_WIN
    // Windows-only Extended (PPgTweaks)
    bool autotakeEd2kLinks = true;      // Register ed2k:// protocol handler
    bool openPortsOnWinFirewall = false; // Windows Firewall API
    bool sparsePartFiles = false;        // NTFS sparse file attribute
    bool allocFullFile = false;          // Pre-allocate disk space
    bool resolveShellLinks = false;      // Follow .lnk files in shared dirs
    int multiUserSharing = 2;            // 0=per-user, 1=shared, 2=program-dir
#endif

    // Statistics (cumulative cross-session rates, KB/s)
    float connMaxDownRate = 0.0f;
    float connAvgDownRate = 0.0f;
    float connMaxAvgDownRate = 0.0f;
    float connAvgUpRate = 0.0f;
    float connMaxAvgUpRate = 0.0f;
    float connMaxUpRate = 0.0f;
    uint32 statsAverageMinutes = 5;  // averaging window for rate history
    uint32 graphsUpdateSec = 3;      // Graph sampling interval (0=disabled)
    uint32 statsUpdateSec = 5;       // Statistics tree update interval (0=disabled)
    bool fillGraphs = false;         // Draw filled graphs
    uint32 statsConnectionsMax = 100;   // Connections graph Y-axis scale
    uint32 statsConnectionsRatio = 3;   // Active connections ratio (1,2,3,4,5,10,20)

    // Security
    uint32 ipFilterLevel = 100;  // DFLT_FILTER_LEVEL — lower = more restrictive
    bool warnUntrustedFiles = true;
    QString ipFilterUpdateUrl;

    // IRC
    QString ircServer = QStringLiteral("irc.mindforge.org:6667");
    QString ircNick;
    bool ircEnableUTF8 = true;
    bool ircUsePerform = false;
    QString ircPerformString;
    bool ircConnectHelpChannel = true;
    bool ircLoadChannelList = true;
    bool ircAddTimestamp = true;
    bool ircIgnoreMiscInfoMessages = false;
    bool ircIgnoreJoinMessages = true;
    bool ircIgnorePartMessages = true;
    bool ircIgnoreQuitMessages = true;
    bool ircUseChannelFilter = false;
    QString ircChannelFilter;

    // IPC Daemon
    bool ipcEnabled = true;
    uint16 ipcPort = 4712;
    QString ipcListenAddress = QStringLiteral("127.0.0.1");
    QString ipcDaemonPath;  // Empty = show dialog; "local" = connect to localhost
    int ipcRemotePollingMs = 1500;
    QStringList ipcTokens;

    // Web Server
    bool webServerEnabled = false;
    uint16 webServerPort = 4711;        // Classic eMule web server port
    QString webServerApiKey;
    QString webServerListenAddress;     // Empty = any
    bool webServerRestApiEnabled = false;
    bool webServerGzipEnabled = true;
    bool webServerUPnP = false;
    QString webServerTemplatePath;
    int webServerSessionTimeout = 5;    // minutes
    bool webServerHttpsEnabled = false;
    QString webServerCertPath;
    QString webServerKeyPath;
    QString webServerAdminPassword;     // SHA-256 hex hash
    bool webServerAdminAllowHiLevFunc = false;
    bool webServerGuestEnabled = false;
    QString webServerGuestPassword;     // SHA-256 hex hash

    // Scheduler
    bool schedulerEnabled = false;

    // Kademlia
    bool kadEnabled = true;
    uint32 kadUDPKey = 0;  // 0 = generate random on first run

    // Connection
    uint16 maxConsPerFive = 20;  // MAXCONPER5SEC — max connections per 5 seconds
    bool showOverhead = false;   // Show overhead bandwidth in status bar

    // Server management (extended)
    bool addServersFromClients = true;  // Accept server list from other clients
    bool filterServerByIP = false;      // Apply IP filter to server addresses
    uint32 deadServerRetries = 20;      // Remove dead servers after N failed attempts (0 = disabled)
    bool autoUpdateServerList = false;  // Auto-update server list from URL at startup
    QString serverListURL;              // URL for server.met download
    bool smartLowIdCheck = true;        // Try another server if we get a LowID
    bool manualServerHighPriority = false; // Set manually added servers to high priority

    // Network modes
    bool networkED2K = false;  // ED2K protocol disabled by default

    // Chat / Messages
    bool msgOnlyFriends = false;   // Only accept messages from friends
    bool msgSecure = false;        // Only accept messages from secure-identified clients
    bool useChatCaptchas = true;   // Require captcha for first messages
    bool enableSpamFilter = true;  // Enable keyword-based spam filter
    QString messageFilter = QStringLiteral("fastest download speed|fastest eMule");
    QString commentFilter = QStringLiteral("http://|https://|ftp://|www.|ftp.");
    bool showSmileys = true;
    bool indicateRatings = true;

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

    // GUI (General page)
    bool promptOnExit = true;
    bool startMinimized = false;
    bool showSplashScreen = true;
    QString language;             // Empty = system locale
    bool enableOnlineSignature = false;
    bool enableMiniMule = true;
    bool preventStandby = false;
    bool startWithOS = false;
    uint32 startVersion = 0;  // Migration counter: 0=first run, 1+=migrations applied
    bool versionCheckEnabled = true;
    int versionCheckDays = 5;          // Check interval in days (1-14)
    int64_t lastVersionCheck = 0;      // Epoch seconds of last check
    bool bringToFrontOnLinkClick = true;

    // GUI (Display page)
    int depth3D = 0;                        // 0=flat, 5=round
    int tooltipDelay = 1;                   // seconds
    bool minimizeToTray = true;
    bool transferDoubleClick = true;
    bool showDwlPercentage = false;
    bool showRatesInTitle = false;
    bool showCatTabInfos = false;
    bool autoRemoveFinishedDownloads = false;
    bool showTransToolbar = true;
    bool storeSearches = true;
    bool disableKnownClientList = false;
    bool disableQueueList = false;
    bool useAutoCompletion = true;
    bool useOriginalIcons = true;
    QString logFont;  // Empty = system default; QFont::toString() format

    // GUI (Files page)
    bool watchClipboard4ED2KLinks = false;
    bool useAdvancedCalcRemainingTime = true;
    QString videoPlayerCommand;
    QString videoPlayerArgs;
    bool createBackupToPreview = true;
    bool autoCleanupFilenames = false;

    // Notifications (GUI-side)
    int notifySoundType = 0;         // 0=noSound, 1=soundFile, 2=speech
    QString notifySoundFile;

    // Notifications (daemon-side)
    bool notifyOnLog = false;
    bool notifyOnChat = false;
    bool notifyOnChatMsg = false;
    bool notifyOnDownloadAdded = false;
    bool notifyOnDownloadFinished = false;
    bool notifyOnNewVersion = false;
    bool notifyOnUrgent = false;
    bool notifyEmailEnabled = false;
    QString notifyEmailSmtpServer;
    uint16 notifyEmailSmtpPort = 25;
    int notifyEmailSmtpAuth = 0;     // 0=none, 1=plain
    bool notifyEmailSmtpTls = false;
    QString notifyEmailSmtpUser;
    QString notifyEmailSmtpPassword; // plaintext in memory, AES-encrypted in YAML
    QString notifyEmailRecipient;
    QString notifyEmailSender;
    QByteArray notifyEmailEncKey;    // 32-byte AES key, not exposed via getter

    // UI State (GUI-only, persisted across sessions)
    QList<int> serverSplitSizes;
    QList<int> kadSplitSizes;
    QList<int> transferSplitSizes;
    QList<int> sharedHorzSplitSizes;
    QList<int> sharedVertSplitSizes;
    QList<int> messagesSplitSizes;
    QList<int> ircSplitSizes;
    QList<int> statsSplitSizes;
    int windowWidth = 900;
    int windowHeight = 620;
    bool windowMaximized = false;
    int optionsLastPage = 0;
    QList<int> toolbarButtonOrder;
    int toolbarButtonStyle = 3;   // Qt::ToolButtonTextUnderIcon
    QString toolbarSkinPath;
    QString skinProfilePath;
    QMap<QString, QByteArray> headerStates;
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

QStringList Preferences::sharedDirs() const
{
    QReadLocker lock(&m_lock);
    return m_data->sharedDirs;
}

void Preferences::setSharedDirs(const QStringList& val)
{
    QWriteLocker lock(&m_lock);
    m_data->sharedDirs = val;
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

int Preferences::logLevel() const
{
    QReadLocker lock(&m_lock);
    return m_data->logLevel;
}

void Preferences::setLogLevel(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->logLevel = val;
}

bool Preferences::verboseLogToDisk() const
{
    QReadLocker lock(&m_lock);
    return m_data->verboseLogToDisk;
}

void Preferences::setVerboseLogToDisk(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->verboseLogToDisk = val;
}

bool Preferences::logSourceExchange() const
{
    QReadLocker lock(&m_lock);
    return m_data->logSourceExchange;
}

void Preferences::setLogSourceExchange(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->logSourceExchange = val;
}

bool Preferences::logBannedClients() const
{
    QReadLocker lock(&m_lock);
    return m_data->logBannedClients;
}

void Preferences::setLogBannedClients(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->logBannedClients = val;
}

bool Preferences::logRatingDescReceived() const
{
    QReadLocker lock(&m_lock);
    return m_data->logRatingDescReceived;
}

void Preferences::setLogRatingDescReceived(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->logRatingDescReceived = val;
}

bool Preferences::logSecureIdent() const
{
    QReadLocker lock(&m_lock);
    return m_data->logSecureIdent;
}

void Preferences::setLogSecureIdent(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->logSecureIdent = val;
}

bool Preferences::logFilteredIPs() const
{
    QReadLocker lock(&m_lock);
    return m_data->logFilteredIPs;
}

void Preferences::setLogFilteredIPs(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->logFilteredIPs = val;
}

bool Preferences::logFileSaving() const
{
    QReadLocker lock(&m_lock);
    return m_data->logFileSaving;
}

void Preferences::setLogFileSaving(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->logFileSaving = val;
}

bool Preferences::logA4AF() const
{
    QReadLocker lock(&m_lock);
    return m_data->logA4AF;
}

void Preferences::setLogA4AF(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->logA4AF = val;
}

bool Preferences::logUlDlEvents() const
{
    QReadLocker lock(&m_lock);
    return m_data->logUlDlEvents;
}

void Preferences::setLogUlDlEvents(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->logUlDlEvents = val;
}

bool Preferences::logRawSocketPackets() const
{
    QReadLocker lock(&m_lock);
    return m_data->logRawSocketPackets;
}

void Preferences::setLogRawSocketPackets(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->logRawSocketPackets = val;
}

bool Preferences::enableIpcLog() const { QReadLocker lock(&m_lock); return m_data->enableIpcLog; }
void Preferences::setEnableIpcLog(bool val) { QWriteLocker lock(&m_lock); m_data->enableIpcLog = val; }
bool Preferences::startCoreWithConsole() const { QReadLocker lock(&m_lock); return m_data->startCoreWithConsole; }
void Preferences::setStartCoreWithConsole(bool val) { QWriteLocker lock(&m_lock); m_data->startCoreWithConsole = val; }

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
// Getters / setters — Extended (PPgTweaks)
// ---------------------------------------------------------------------------

bool Preferences::useCreditSystem() const
{
    QReadLocker lock(&m_lock);
    return m_data->useCreditSystem;
}

void Preferences::setUseCreditSystem(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->useCreditSystem = val;
}

bool Preferences::a4afSaveCpu() const
{
    QReadLocker lock(&m_lock);
    return m_data->a4afSaveCpu;
}

void Preferences::setA4afSaveCpu(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->a4afSaveCpu = val;
}

bool Preferences::autoArchivePreviewStart() const
{
    QReadLocker lock(&m_lock);
    return m_data->autoArchivePreviewStart;
}

void Preferences::setAutoArchivePreviewStart(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->autoArchivePreviewStart = val;
}

QString Preferences::ed2kHostname() const
{
    QReadLocker lock(&m_lock);
    return m_data->ed2kHostname;
}

void Preferences::setEd2kHostname(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->ed2kHostname = val;
}

bool Preferences::showExtControls() const
{
    QReadLocker lock(&m_lock);
    return m_data->showExtControls;
}

void Preferences::setShowExtControls(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->showExtControls = val;
}

int Preferences::commitFiles() const
{
    QReadLocker lock(&m_lock);
    return m_data->commitFiles;
}

void Preferences::setCommitFiles(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->commitFiles = val;
}

int Preferences::extractMetaData() const
{
    QReadLocker lock(&m_lock);
    return m_data->extractMetaData;
}

void Preferences::setExtractMetaData(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->extractMetaData = val;
}

uint32 Preferences::queueSize() const
{
    QReadLocker lock(&m_lock);
    return m_data->queueSize;
}

void Preferences::setQueueSize(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->queueSize = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Upload SpeedSense (USS)
// ---------------------------------------------------------------------------

bool Preferences::dynUpEnabled() const
{
    QReadLocker lock(&m_lock);
    return m_data->dynUpEnabled;
}

void Preferences::setDynUpEnabled(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->dynUpEnabled = val;
}

int Preferences::dynUpPingTolerance() const
{
    QReadLocker lock(&m_lock);
    return m_data->dynUpPingTolerance;
}

void Preferences::setDynUpPingTolerance(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->dynUpPingTolerance = val;
}

int Preferences::dynUpPingToleranceMs() const
{
    QReadLocker lock(&m_lock);
    return m_data->dynUpPingToleranceMs;
}

void Preferences::setDynUpPingToleranceMs(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->dynUpPingToleranceMs = val;
}

bool Preferences::dynUpUseMillisecondPingTolerance() const
{
    QReadLocker lock(&m_lock);
    return m_data->dynUpUseMillisecondPingTolerance;
}

void Preferences::setDynUpUseMillisecondPingTolerance(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->dynUpUseMillisecondPingTolerance = val;
}

int Preferences::dynUpGoingUpDivider() const
{
    QReadLocker lock(&m_lock);
    return m_data->dynUpGoingUpDivider;
}

void Preferences::setDynUpGoingUpDivider(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->dynUpGoingUpDivider = val;
}

int Preferences::dynUpGoingDownDivider() const
{
    QReadLocker lock(&m_lock);
    return m_data->dynUpGoingDownDivider;
}

void Preferences::setDynUpGoingDownDivider(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->dynUpGoingDownDivider = val;
}

int Preferences::dynUpNumberOfPings() const
{
    QReadLocker lock(&m_lock);
    return m_data->dynUpNumberOfPings;
}

void Preferences::setDynUpNumberOfPings(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->dynUpNumberOfPings = val;
}

#ifdef Q_OS_WIN

bool Preferences::autotakeEd2kLinks() const
{
    QReadLocker lock(&m_lock);
    return m_data->autotakeEd2kLinks;
}

void Preferences::setAutotakeEd2kLinks(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->autotakeEd2kLinks = val;
}

bool Preferences::openPortsOnWinFirewall() const
{
    QReadLocker lock(&m_lock);
    return m_data->openPortsOnWinFirewall;
}

void Preferences::setOpenPortsOnWinFirewall(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->openPortsOnWinFirewall = val;
}

bool Preferences::sparsePartFiles() const
{
    QReadLocker lock(&m_lock);
    return m_data->sparsePartFiles;
}

void Preferences::setSparsePartFiles(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->sparsePartFiles = val;
}

bool Preferences::allocFullFile() const
{
    QReadLocker lock(&m_lock);
    return m_data->allocFullFile;
}

void Preferences::setAllocFullFile(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->allocFullFile = val;
}

bool Preferences::resolveShellLinks() const
{
    QReadLocker lock(&m_lock);
    return m_data->resolveShellLinks;
}

void Preferences::setResolveShellLinks(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->resolveShellLinks = val;
}

int Preferences::multiUserSharing() const
{
    QReadLocker lock(&m_lock);
    return m_data->multiUserSharing;
}

void Preferences::setMultiUserSharing(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->multiUserSharing = val;
}

#endif // Q_OS_WIN

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

uint32 Preferences::graphsUpdateSec() const
{
    QReadLocker lock(&m_lock);
    return m_data->graphsUpdateSec;
}

void Preferences::setGraphsUpdateSec(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->graphsUpdateSec = val;
}

uint32 Preferences::statsUpdateSec() const
{
    QReadLocker lock(&m_lock);
    return m_data->statsUpdateSec;
}

void Preferences::setStatsUpdateSec(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->statsUpdateSec = val;
}

bool Preferences::fillGraphs() const
{
    QReadLocker lock(&m_lock);
    return m_data->fillGraphs;
}

void Preferences::setFillGraphs(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->fillGraphs = val;
}

uint32 Preferences::statsConnectionsMax() const
{
    QReadLocker lock(&m_lock);
    return m_data->statsConnectionsMax;
}

void Preferences::setStatsConnectionsMax(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->statsConnectionsMax = val;
}

uint32 Preferences::statsConnectionsRatio() const
{
    QReadLocker lock(&m_lock);
    return m_data->statsConnectionsRatio;
}

void Preferences::setStatsConnectionsRatio(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->statsConnectionsRatio = val;
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

bool Preferences::warnUntrustedFiles() const
{
    QReadLocker lock(&m_lock);
    return m_data->warnUntrustedFiles;
}

void Preferences::setWarnUntrustedFiles(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->warnUntrustedFiles = val;
}

QString Preferences::ipFilterUpdateUrl() const
{
    QReadLocker lock(&m_lock);
    return m_data->ipFilterUpdateUrl;
}

void Preferences::setIpFilterUpdateUrl(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->ipFilterUpdateUrl = val;
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

bool Preferences::ircConnectHelpChannel() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircConnectHelpChannel;
}

void Preferences::setIrcConnectHelpChannel(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircConnectHelpChannel = val;
}

bool Preferences::ircLoadChannelList() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircLoadChannelList;
}

void Preferences::setIrcLoadChannelList(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircLoadChannelList = val;
}

bool Preferences::ircAddTimestamp() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircAddTimestamp;
}

void Preferences::setIrcAddTimestamp(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircAddTimestamp = val;
}

bool Preferences::ircIgnoreMiscInfoMessages() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircIgnoreMiscInfoMessages;
}

void Preferences::setIrcIgnoreMiscInfoMessages(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircIgnoreMiscInfoMessages = val;
}

bool Preferences::ircIgnoreJoinMessages() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircIgnoreJoinMessages;
}

void Preferences::setIrcIgnoreJoinMessages(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircIgnoreJoinMessages = val;
}

bool Preferences::ircIgnorePartMessages() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircIgnorePartMessages;
}

void Preferences::setIrcIgnorePartMessages(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircIgnorePartMessages = val;
}

bool Preferences::ircIgnoreQuitMessages() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircIgnoreQuitMessages;
}

void Preferences::setIrcIgnoreQuitMessages(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircIgnoreQuitMessages = val;
}

bool Preferences::ircUseChannelFilter() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircUseChannelFilter;
}

void Preferences::setIrcUseChannelFilter(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircUseChannelFilter = val;
}

QString Preferences::ircChannelFilter() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircChannelFilter;
}

void Preferences::setIrcChannelFilter(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircChannelFilter = val;
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

int Preferences::ipcRemotePollingMs() const
{
    QReadLocker lock(&m_lock);
    return m_data->ipcRemotePollingMs;
}

void Preferences::setIpcRemotePollingMs(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->ipcRemotePollingMs = std::clamp(val, 200, 10000);
}

QStringList Preferences::ipcTokens() const
{
    QReadLocker lock(&m_lock);
    return m_data->ipcTokens;
}

void Preferences::setIpcTokens(const QStringList& val)
{
    QWriteLocker lock(&m_lock);
    m_data->ipcTokens = val;
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

bool Preferences::webServerRestApiEnabled() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerRestApiEnabled;
}

void Preferences::setWebServerRestApiEnabled(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerRestApiEnabled = val;
}

bool Preferences::webServerGzipEnabled() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerGzipEnabled;
}

void Preferences::setWebServerGzipEnabled(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerGzipEnabled = val;
}

bool Preferences::webServerUPnP() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerUPnP;
}

void Preferences::setWebServerUPnP(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerUPnP = val;
}

QString Preferences::webServerTemplatePath() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerTemplatePath;
}

void Preferences::setWebServerTemplatePath(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerTemplatePath = val;
}

int Preferences::webServerSessionTimeout() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerSessionTimeout;
}

void Preferences::setWebServerSessionTimeout(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerSessionTimeout = val;
}

bool Preferences::webServerHttpsEnabled() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerHttpsEnabled;
}

void Preferences::setWebServerHttpsEnabled(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerHttpsEnabled = val;
}

QString Preferences::webServerCertPath() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerCertPath;
}

void Preferences::setWebServerCertPath(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerCertPath = val;
}

QString Preferences::webServerKeyPath() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerKeyPath;
}

void Preferences::setWebServerKeyPath(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerKeyPath = val;
}

QString Preferences::webServerAdminPassword() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerAdminPassword;
}

void Preferences::setWebServerAdminPassword(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerAdminPassword = val;
}

bool Preferences::webServerAdminAllowHiLevFunc() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerAdminAllowHiLevFunc;
}

void Preferences::setWebServerAdminAllowHiLevFunc(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerAdminAllowHiLevFunc = val;
}

bool Preferences::webServerGuestEnabled() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerGuestEnabled;
}

void Preferences::setWebServerGuestEnabled(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerGuestEnabled = val;
}

QString Preferences::webServerGuestPassword() const
{
    QReadLocker lock(&m_lock);
    return m_data->webServerGuestPassword;
}

void Preferences::setWebServerGuestPassword(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->webServerGuestPassword = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Scheduler
// ---------------------------------------------------------------------------

bool Preferences::schedulerEnabled() const
{
    QReadLocker lock(&m_lock);
    return m_data->schedulerEnabled;
}

void Preferences::setSchedulerEnabled(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->schedulerEnabled = val;
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

bool Preferences::showOverhead() const
{
    QReadLocker lock(&m_lock);
    return m_data->showOverhead;
}

void Preferences::setShowOverhead(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->showOverhead = val;
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

uint32 Preferences::deadServerRetries() const
{
    QReadLocker lock(&m_lock);
    return m_data->deadServerRetries;
}

void Preferences::setDeadServerRetries(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->deadServerRetries = val;
}

bool Preferences::autoUpdateServerList() const
{
    QReadLocker lock(&m_lock);
    return m_data->autoUpdateServerList;
}

void Preferences::setAutoUpdateServerList(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->autoUpdateServerList = val;
}

QString Preferences::serverListURL() const
{
    QReadLocker lock(&m_lock);
    return m_data->serverListURL;
}

void Preferences::setServerListURL(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->serverListURL = val;
}

bool Preferences::smartLowIdCheck() const
{
    QReadLocker lock(&m_lock);
    return m_data->smartLowIdCheck;
}

void Preferences::setSmartLowIdCheck(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->smartLowIdCheck = val;
}

bool Preferences::manualServerHighPriority() const
{
    QReadLocker lock(&m_lock);
    return m_data->manualServerHighPriority;
}

void Preferences::setManualServerHighPriority(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->manualServerHighPriority = val;
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

QString Preferences::messageFilter() const
{
    QReadLocker lock(&m_lock);
    return m_data->messageFilter;
}

void Preferences::setMessageFilter(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->messageFilter = val;
}

QString Preferences::commentFilter() const
{
    QReadLocker lock(&m_lock);
    return m_data->commentFilter;
}

void Preferences::setCommentFilter(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->commentFilter = val;
}

bool Preferences::showSmileys() const
{
    QReadLocker lock(&m_lock);
    return m_data->showSmileys;
}

void Preferences::setShowSmileys(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->showSmileys = val;
}

bool Preferences::indicateRatings() const
{
    QReadLocker lock(&m_lock);
    return m_data->indicateRatings;
}

void Preferences::setIndicateRatings(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->indicateRatings = val;
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
// Getters / setters — Files (extended)
// ---------------------------------------------------------------------------

bool Preferences::autoSharedFilesPriority() const
{
    QReadLocker lock(&m_lock);
    return m_data->autoSharedFilesPriority;
}

void Preferences::setAutoSharedFilesPriority(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->autoSharedFilesPriority = val;
}

bool Preferences::transferFullChunks() const
{
    QReadLocker lock(&m_lock);
    return m_data->transferFullChunks;
}

void Preferences::setTransferFullChunks(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->transferFullChunks = val;
}

bool Preferences::previewPrio() const
{
    QReadLocker lock(&m_lock);
    return m_data->previewPrio;
}

void Preferences::setPreviewPrio(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->previewPrio = val;
}

bool Preferences::startNextPausedFile() const
{
    QReadLocker lock(&m_lock);
    return m_data->startNextPausedFile;
}

void Preferences::setStartNextPausedFile(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->startNextPausedFile = val;
}

bool Preferences::startNextPausedFileSameCat() const
{
    QReadLocker lock(&m_lock);
    return m_data->startNextPausedFileSameCat;
}

void Preferences::setStartNextPausedFileSameCat(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->startNextPausedFileSameCat = val;
}

bool Preferences::startNextPausedFileOnlySameCat() const
{
    QReadLocker lock(&m_lock);
    return m_data->startNextPausedFileOnlySameCat;
}

void Preferences::setStartNextPausedFileOnlySameCat(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->startNextPausedFileOnlySameCat = val;
}

bool Preferences::rememberDownloadedFiles() const
{
    QReadLocker lock(&m_lock);
    return m_data->rememberDownloadedFiles;
}

void Preferences::setRememberDownloadedFiles(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->rememberDownloadedFiles = val;
}

bool Preferences::rememberCancelledFiles() const
{
    QReadLocker lock(&m_lock);
    return m_data->rememberCancelledFiles;
}

void Preferences::setRememberCancelledFiles(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->rememberCancelledFiles = val;
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
// Getters / setters — GUI (General page)
// ---------------------------------------------------------------------------

bool Preferences::promptOnExit() const
{
    QReadLocker lock(&m_lock);
    return m_data->promptOnExit;
}

void Preferences::setPromptOnExit(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->promptOnExit = val;
}

bool Preferences::startMinimized() const
{
    QReadLocker lock(&m_lock);
    return m_data->startMinimized;
}

void Preferences::setStartMinimized(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->startMinimized = val;
}

bool Preferences::showSplashScreen() const
{
    QReadLocker lock(&m_lock);
    return m_data->showSplashScreen;
}

void Preferences::setShowSplashScreen(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->showSplashScreen = val;
}

QString Preferences::language() const
{
    QReadLocker lock(&m_lock);
    return m_data->language;
}

void Preferences::setLanguage(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->language = val;
}

bool Preferences::enableOnlineSignature() const
{
    QReadLocker lock(&m_lock);
    return m_data->enableOnlineSignature;
}

void Preferences::setEnableOnlineSignature(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->enableOnlineSignature = val;
}

bool Preferences::enableMiniMule() const
{
    QReadLocker lock(&m_lock);
    return m_data->enableMiniMule;
}

void Preferences::setEnableMiniMule(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->enableMiniMule = val;
}

bool Preferences::preventStandby() const
{
    QReadLocker lock(&m_lock);
    return m_data->preventStandby;
}

void Preferences::setPreventStandby(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->preventStandby = val;
}

bool Preferences::startWithOS() const
{
    QReadLocker lock(&m_lock);
    return m_data->startWithOS;
}

void Preferences::setStartWithOS(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->startWithOS = val;
}

uint32 Preferences::startVersion() const
{
    QReadLocker lock(&m_lock);
    return m_data->startVersion;
}

void Preferences::setStartVersion(uint32 val)
{
    QWriteLocker lock(&m_lock);
    m_data->startVersion = val;
}

bool Preferences::versionCheckEnabled() const
{
    QReadLocker lock(&m_lock);
    return m_data->versionCheckEnabled;
}

void Preferences::setVersionCheckEnabled(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->versionCheckEnabled = val;
}

int Preferences::versionCheckDays() const
{
    QReadLocker lock(&m_lock);
    return m_data->versionCheckDays;
}

void Preferences::setVersionCheckDays(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->versionCheckDays = std::clamp(val, 1, 14);
}

int64_t Preferences::lastVersionCheck() const
{
    QReadLocker lock(&m_lock);
    return m_data->lastVersionCheck;
}

void Preferences::setLastVersionCheck(int64_t val)
{
    QWriteLocker lock(&m_lock);
    m_data->lastVersionCheck = val;
}

bool Preferences::bringToFrontOnLinkClick() const
{
    QReadLocker lock(&m_lock);
    return m_data->bringToFrontOnLinkClick;
}

void Preferences::setBringToFrontOnLinkClick(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->bringToFrontOnLinkClick = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — GUI (Display page)
// ---------------------------------------------------------------------------

int Preferences::depth3D() const
{
    QReadLocker lock(&m_lock);
    return m_data->depth3D;
}

void Preferences::setDepth3D(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->depth3D = val;
}

int Preferences::tooltipDelay() const
{
    QReadLocker lock(&m_lock);
    return m_data->tooltipDelay;
}

void Preferences::setTooltipDelay(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->tooltipDelay = val;
}

bool Preferences::minimizeToTray() const
{
    QReadLocker lock(&m_lock);
    return m_data->minimizeToTray;
}

void Preferences::setMinimizeToTray(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->minimizeToTray = val;
}

bool Preferences::transferDoubleClick() const
{
    QReadLocker lock(&m_lock);
    return m_data->transferDoubleClick;
}

void Preferences::setTransferDoubleClick(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->transferDoubleClick = val;
}

bool Preferences::showDwlPercentage() const
{
    QReadLocker lock(&m_lock);
    return m_data->showDwlPercentage;
}

void Preferences::setShowDwlPercentage(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->showDwlPercentage = val;
}

bool Preferences::showRatesInTitle() const
{
    QReadLocker lock(&m_lock);
    return m_data->showRatesInTitle;
}

void Preferences::setShowRatesInTitle(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->showRatesInTitle = val;
}

bool Preferences::showCatTabInfos() const
{
    QReadLocker lock(&m_lock);
    return m_data->showCatTabInfos;
}

void Preferences::setShowCatTabInfos(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->showCatTabInfos = val;
}

bool Preferences::autoRemoveFinishedDownloads() const
{
    QReadLocker lock(&m_lock);
    return m_data->autoRemoveFinishedDownloads;
}

void Preferences::setAutoRemoveFinishedDownloads(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->autoRemoveFinishedDownloads = val;
}

bool Preferences::showTransToolbar() const
{
    QReadLocker lock(&m_lock);
    return m_data->showTransToolbar;
}

void Preferences::setShowTransToolbar(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->showTransToolbar = val;
}

bool Preferences::storeSearches() const
{
    QReadLocker lock(&m_lock);
    return m_data->storeSearches;
}

void Preferences::setStoreSearches(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->storeSearches = val;
}

bool Preferences::disableKnownClientList() const
{
    QReadLocker lock(&m_lock);
    return m_data->disableKnownClientList;
}

void Preferences::setDisableKnownClientList(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->disableKnownClientList = val;
}

bool Preferences::disableQueueList() const
{
    QReadLocker lock(&m_lock);
    return m_data->disableQueueList;
}

void Preferences::setDisableQueueList(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->disableQueueList = val;
}

bool Preferences::useAutoCompletion() const
{
    QReadLocker lock(&m_lock);
    return m_data->useAutoCompletion;
}

void Preferences::setUseAutoCompletion(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->useAutoCompletion = val;
}

bool Preferences::useOriginalIcons() const
{
    QReadLocker lock(&m_lock);
    return m_data->useOriginalIcons;
}

void Preferences::setUseOriginalIcons(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->useOriginalIcons = val;
}

QString Preferences::logFont() const
{
    QReadLocker lock(&m_lock);
    return m_data->logFont;
}

void Preferences::setLogFont(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->logFont = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — GUI (Files page)
// ---------------------------------------------------------------------------

bool Preferences::watchClipboard4ED2KLinks() const
{
    QReadLocker lock(&m_lock);
    return m_data->watchClipboard4ED2KLinks;
}

void Preferences::setWatchClipboard4ED2KLinks(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->watchClipboard4ED2KLinks = val;
}

bool Preferences::useAdvancedCalcRemainingTime() const
{
    QReadLocker lock(&m_lock);
    return m_data->useAdvancedCalcRemainingTime;
}

void Preferences::setUseAdvancedCalcRemainingTime(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->useAdvancedCalcRemainingTime = val;
}

QString Preferences::videoPlayerCommand() const
{
    QReadLocker lock(&m_lock);
    return m_data->videoPlayerCommand;
}

void Preferences::setVideoPlayerCommand(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->videoPlayerCommand = val;
}

QString Preferences::videoPlayerArgs() const
{
    QReadLocker lock(&m_lock);
    return m_data->videoPlayerArgs;
}

void Preferences::setVideoPlayerArgs(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->videoPlayerArgs = val;
}

bool Preferences::createBackupToPreview() const
{
    QReadLocker lock(&m_lock);
    return m_data->createBackupToPreview;
}

void Preferences::setCreateBackupToPreview(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->createBackupToPreview = val;
}

bool Preferences::autoCleanupFilenames() const
{
    QReadLocker lock(&m_lock);
    return m_data->autoCleanupFilenames;
}

void Preferences::setAutoCleanupFilenames(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->autoCleanupFilenames = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Notifications (GUI-side)
// ---------------------------------------------------------------------------

int Preferences::notifySoundType() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifySoundType;
}

void Preferences::setNotifySoundType(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifySoundType = val;
}

QString Preferences::notifySoundFile() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifySoundFile;
}

void Preferences::setNotifySoundFile(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifySoundFile = val;
}

// ---------------------------------------------------------------------------
// Getters / setters — Notifications (daemon-side)
// ---------------------------------------------------------------------------

bool Preferences::notifyOnLog() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyOnLog;
}

void Preferences::setNotifyOnLog(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyOnLog = val;
}

bool Preferences::notifyOnChat() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyOnChat;
}

void Preferences::setNotifyOnChat(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyOnChat = val;
}

bool Preferences::notifyOnChatMsg() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyOnChatMsg;
}

void Preferences::setNotifyOnChatMsg(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyOnChatMsg = val;
}

bool Preferences::notifyOnDownloadAdded() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyOnDownloadAdded;
}

void Preferences::setNotifyOnDownloadAdded(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyOnDownloadAdded = val;
}

bool Preferences::notifyOnDownloadFinished() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyOnDownloadFinished;
}

void Preferences::setNotifyOnDownloadFinished(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyOnDownloadFinished = val;
}

bool Preferences::notifyOnNewVersion() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyOnNewVersion;
}

void Preferences::setNotifyOnNewVersion(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyOnNewVersion = val;
}

bool Preferences::notifyOnUrgent() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyOnUrgent;
}

void Preferences::setNotifyOnUrgent(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyOnUrgent = val;
}

bool Preferences::notifyEmailEnabled() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyEmailEnabled;
}

void Preferences::setNotifyEmailEnabled(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyEmailEnabled = val;
}

QString Preferences::notifyEmailSmtpServer() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyEmailSmtpServer;
}

void Preferences::setNotifyEmailSmtpServer(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyEmailSmtpServer = val;
}

uint16 Preferences::notifyEmailSmtpPort() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyEmailSmtpPort;
}

void Preferences::setNotifyEmailSmtpPort(uint16 val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyEmailSmtpPort = val;
}

int Preferences::notifyEmailSmtpAuth() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyEmailSmtpAuth;
}

void Preferences::setNotifyEmailSmtpAuth(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyEmailSmtpAuth = val;
}

bool Preferences::notifyEmailSmtpTls() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyEmailSmtpTls;
}

void Preferences::setNotifyEmailSmtpTls(bool val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyEmailSmtpTls = val;
}

QString Preferences::notifyEmailSmtpUser() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyEmailSmtpUser;
}

void Preferences::setNotifyEmailSmtpUser(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyEmailSmtpUser = val;
}

QString Preferences::notifyEmailSmtpPassword() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyEmailSmtpPassword;
}

void Preferences::setNotifyEmailSmtpPassword(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyEmailSmtpPassword = val;
}

QString Preferences::notifyEmailRecipient() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyEmailRecipient;
}

void Preferences::setNotifyEmailRecipient(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyEmailRecipient = val;
}

QString Preferences::notifyEmailSender() const
{
    QReadLocker lock(&m_lock);
    return m_data->notifyEmailSender;
}

void Preferences::setNotifyEmailSender(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->notifyEmailSender = val;
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

QList<int> Preferences::transferSplitSizes() const
{
    QReadLocker lock(&m_lock);
    return m_data->transferSplitSizes;
}

void Preferences::setTransferSplitSizes(const QList<int>& val)
{
    QWriteLocker lock(&m_lock);
    m_data->transferSplitSizes = val;
}

QList<int> Preferences::sharedHorzSplitSizes() const
{
    QReadLocker lock(&m_lock);
    return m_data->sharedHorzSplitSizes;
}

void Preferences::setSharedHorzSplitSizes(const QList<int>& val)
{
    QWriteLocker lock(&m_lock);
    m_data->sharedHorzSplitSizes = val;
}

QList<int> Preferences::sharedVertSplitSizes() const
{
    QReadLocker lock(&m_lock);
    return m_data->sharedVertSplitSizes;
}

void Preferences::setSharedVertSplitSizes(const QList<int>& val)
{
    QWriteLocker lock(&m_lock);
    m_data->sharedVertSplitSizes = val;
}

QList<int> Preferences::messagesSplitSizes() const
{
    QReadLocker lock(&m_lock);
    return m_data->messagesSplitSizes;
}

void Preferences::setMessagesSplitSizes(const QList<int>& val)
{
    QWriteLocker lock(&m_lock);
    m_data->messagesSplitSizes = val;
}

QList<int> Preferences::ircSplitSizes() const
{
    QReadLocker lock(&m_lock);
    return m_data->ircSplitSizes;
}

void Preferences::setIrcSplitSizes(const QList<int>& val)
{
    QWriteLocker lock(&m_lock);
    m_data->ircSplitSizes = val;
}

QList<int> Preferences::statsSplitSizes() const
{
    QReadLocker lock(&m_lock);
    return m_data->statsSplitSizes;
}

void Preferences::setStatsSplitSizes(const QList<int>& val)
{
    QWriteLocker lock(&m_lock);
    m_data->statsSplitSizes = val;
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

int Preferences::optionsLastPage() const
{
    QReadLocker lock(&m_lock);
    return m_data->optionsLastPage;
}

void Preferences::setOptionsLastPage(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->optionsLastPage = val;
}

QList<int> Preferences::toolbarButtonOrder() const
{
    QReadLocker lock(&m_lock);
    return m_data->toolbarButtonOrder;
}

void Preferences::setToolbarButtonOrder(const QList<int>& val)
{
    QWriteLocker lock(&m_lock);
    m_data->toolbarButtonOrder = val;
}

int Preferences::toolbarButtonStyle() const
{
    QReadLocker lock(&m_lock);
    return m_data->toolbarButtonStyle;
}

void Preferences::setToolbarButtonStyle(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->toolbarButtonStyle = val;
}

QString Preferences::toolbarSkinPath() const
{
    QReadLocker lock(&m_lock);
    return m_data->toolbarSkinPath;
}

void Preferences::setToolbarSkinPath(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->toolbarSkinPath = val;
}

QString Preferences::skinProfilePath() const
{
    QReadLocker lock(&m_lock);
    return m_data->skinProfilePath;
}

void Preferences::setSkinProfilePath(const QString& val)
{
    QWriteLocker lock(&m_lock);
    m_data->skinProfilePath = val;
}

QByteArray Preferences::headerState(const QString& key) const
{
    QReadLocker lock(&m_lock);
    return m_data->headerStates.value(key);
}

void Preferences::setHeaderState(const QString& key, const QByteArray& val)
{
    QWriteLocker lock(&m_lock);
    m_data->headerStates[key] = val;
}

// ---------------------------------------------------------------------------
// IPC sync
// ---------------------------------------------------------------------------

void Preferences::updateFromCbor(const QCborMap& p)
{
    QWriteLocker lock(&m_lock);
    // Connection / General
    m_data->nick             = p.value(QStringLiteral("nick")).toString();
    m_data->port             = static_cast<uint16>(p.value(QStringLiteral("port")).toInteger());
    m_data->udpPort          = static_cast<uint16>(p.value(QStringLiteral("udpPort")).toInteger());
    m_data->maxUpload        = static_cast<uint32>(p.value(QStringLiteral("maxUpload")).toInteger());
    m_data->maxDownload      = static_cast<uint32>(p.value(QStringLiteral("maxDownload")).toInteger());
    m_data->maxGraphDownloadRate = static_cast<uint32>(p.value(QStringLiteral("maxGraphDownloadRate")).toInteger());
    m_data->maxGraphUploadRate   = static_cast<uint32>(p.value(QStringLiteral("maxGraphUploadRate")).toInteger());
    m_data->maxConnections   = static_cast<uint16>(p.value(QStringLiteral("maxConnections")).toInteger());
    m_data->maxSourcesPerFile = static_cast<uint16>(p.value(QStringLiteral("maxSourcesPerFile")).toInteger());
    m_data->autoConnect      = p.value(QStringLiteral("autoConnect")).toBool();
    m_data->reconnect        = p.value(QStringLiteral("reconnect")).toBool();
    m_data->showOverhead     = p.value(QStringLiteral("showOverhead")).toBool();
    m_data->networkED2K      = p.value(QStringLiteral("networkED2K")).toBool();
    m_data->kadEnabled       = p.value(QStringLiteral("kadEnabled")).toBool();
    m_data->schedulerEnabled = p.value(QStringLiteral("schedulerEnabled")).toBool();
    m_data->enableUPnP       = p.value(QStringLiteral("enableUPnP")).toBool();

    // Server
    m_data->safeServerConnect       = p.value(QStringLiteral("safeServerConnect")).toBool();
    m_data->autoConnectStaticOnly   = p.value(QStringLiteral("autoConnectStaticOnly")).toBool();
    m_data->useServerPriorities     = p.value(QStringLiteral("useServerPriorities")).toBool();
    m_data->addServersFromServer    = p.value(QStringLiteral("addServersFromServer")).toBool();
    m_data->addServersFromClients   = p.value(QStringLiteral("addServersFromClients")).toBool();
    m_data->deadServerRetries       = static_cast<uint32>(p.value(QStringLiteral("deadServerRetries")).toInteger());
    m_data->autoUpdateServerList    = p.value(QStringLiteral("autoUpdateServerList")).toBool();
    m_data->serverListURL           = p.value(QStringLiteral("serverListURL")).toString();
    m_data->smartLowIdCheck         = p.value(QStringLiteral("smartLowIdCheck")).toBool();
    m_data->manualServerHighPriority = p.value(QStringLiteral("manualServerHighPriority")).toBool();

    // Proxy
    m_data->proxyType           = static_cast<int>(p.value(QStringLiteral("proxyType")).toInteger());
    m_data->proxyHost           = p.value(QStringLiteral("proxyHost")).toString();
    m_data->proxyPort           = static_cast<uint16>(p.value(QStringLiteral("proxyPort")).toInteger());
    m_data->proxyEnablePassword = p.value(QStringLiteral("proxyEnablePassword")).toBool();
    m_data->proxyUser           = p.value(QStringLiteral("proxyUser")).toString();
    m_data->proxyPassword       = p.value(QStringLiteral("proxyPassword")).toString();

    // Files
    m_data->addNewFilesPaused             = p.value(QStringLiteral("addNewFilesPaused")).toBool();
    m_data->autoDownloadPriority          = p.value(QStringLiteral("autoDownloadPriority")).toBool();
    m_data->autoSharedFilesPriority       = p.value(QStringLiteral("autoSharedFilesPriority")).toBool();
    m_data->transferFullChunks            = p.value(QStringLiteral("transferFullChunks")).toBool();
    m_data->previewPrio                   = p.value(QStringLiteral("previewPrio")).toBool();
    m_data->startNextPausedFile           = p.value(QStringLiteral("startNextPausedFile")).toBool();
    m_data->startNextPausedFileSameCat    = p.value(QStringLiteral("startNextPausedFileSameCat")).toBool();
    m_data->startNextPausedFileOnlySameCat = p.value(QStringLiteral("startNextPausedFileOnlySameCat")).toBool();
    m_data->rememberDownloadedFiles       = p.value(QStringLiteral("rememberDownloadedFiles")).toBool();
    m_data->rememberCancelledFiles        = p.value(QStringLiteral("rememberCancelledFiles")).toBool();

    // Notifications
    m_data->notifyOnLog              = p.value(QStringLiteral("notifyOnLog")).toBool();
    m_data->notifyOnChat             = p.value(QStringLiteral("notifyOnChat")).toBool();
    m_data->notifyOnChatMsg          = p.value(QStringLiteral("notifyOnChatMsg")).toBool();
    m_data->notifyOnDownloadAdded    = p.value(QStringLiteral("notifyOnDownloadAdded")).toBool();
    m_data->notifyOnDownloadFinished = p.value(QStringLiteral("notifyOnDownloadFinished")).toBool();
    m_data->notifyOnNewVersion       = p.value(QStringLiteral("notifyOnNewVersion")).toBool();
    m_data->notifyOnUrgent           = p.value(QStringLiteral("notifyOnUrgent")).toBool();
    m_data->notifyEmailEnabled       = p.value(QStringLiteral("notifyEmailEnabled")).toBool();
    m_data->notifyEmailSmtpServer    = p.value(QStringLiteral("notifyEmailSmtpServer")).toString();
    m_data->notifyEmailSmtpPort      = static_cast<uint16>(p.value(QStringLiteral("notifyEmailSmtpPort")).toInteger());
    m_data->notifyEmailSmtpAuth      = static_cast<int>(p.value(QStringLiteral("notifyEmailSmtpAuth")).toInteger());
    m_data->notifyEmailSmtpTls       = p.value(QStringLiteral("notifyEmailSmtpTls")).toBool();
    m_data->notifyEmailSmtpUser      = p.value(QStringLiteral("notifyEmailSmtpUser")).toString();
    m_data->notifyEmailSmtpPassword  = p.value(QStringLiteral("notifyEmailSmtpPassword")).toString();
    m_data->notifyEmailRecipient     = p.value(QStringLiteral("notifyEmailRecipient")).toString();
    m_data->notifyEmailSender        = p.value(QStringLiteral("notifyEmailSender")).toString();

    // Messages and Comments
    m_data->msgOnlyFriends    = p.value(QStringLiteral("msgOnlyFriends")).toBool();
    m_data->enableSpamFilter  = p.value(QStringLiteral("enableSpamFilter")).toBool();
    m_data->useChatCaptchas   = p.value(QStringLiteral("useChatCaptchas")).toBool();
    m_data->messageFilter     = p.value(QStringLiteral("messageFilter")).toString();
    m_data->commentFilter     = p.value(QStringLiteral("commentFilter")).toString();

    // Security
    m_data->filterServerByIP          = p.value(QStringLiteral("filterServerByIP")).toBool();
    m_data->ipFilterLevel             = static_cast<uint32>(p.value(QStringLiteral("ipFilterLevel")).toInteger());
    m_data->viewSharedFilesAccess     = static_cast<int>(p.value(QStringLiteral("viewSharedFilesAccess")).toInteger());
    m_data->cryptLayerSupported       = p.value(QStringLiteral("cryptLayerSupported")).toBool();
    m_data->cryptLayerRequested       = p.value(QStringLiteral("cryptLayerRequested")).toBool();
    m_data->cryptLayerRequired        = p.value(QStringLiteral("cryptLayerRequired")).toBool();
    m_data->useSecureIdent            = p.value(QStringLiteral("useSecureIdent")).toBool();
    m_data->enableSearchResultFilter  = p.value(QStringLiteral("enableSearchResultFilter")).toBool();
    m_data->warnUntrustedFiles        = p.value(QStringLiteral("warnUntrustedFiles")).toBool();
    m_data->ipFilterUpdateUrl         = p.value(QStringLiteral("ipFilterUpdateUrl")).toString();

    // Statistics
    m_data->statsAverageMinutes   = static_cast<uint32>(p.value(QStringLiteral("statsAverageMinutes")).toInteger());
    m_data->graphsUpdateSec       = static_cast<uint32>(p.value(QStringLiteral("graphsUpdateSec")).toInteger());
    m_data->statsUpdateSec        = static_cast<uint32>(p.value(QStringLiteral("statsUpdateSec")).toInteger());
    m_data->fillGraphs            = p.value(QStringLiteral("fillGraphs")).toBool();
    m_data->statsConnectionsMax   = static_cast<uint32>(p.value(QStringLiteral("statsConnectionsMax")).toInteger());
    m_data->statsConnectionsRatio = static_cast<uint32>(p.value(QStringLiteral("statsConnectionsRatio")).toInteger());

    // Extended (PPgTweaks)
    m_data->maxConsPerFive              = static_cast<uint16>(p.value(QStringLiteral("maxConsPerFive")).toInteger());
    m_data->maxHalfConnections          = static_cast<uint16>(p.value(QStringLiteral("maxHalfConnections")).toInteger());
    m_data->serverKeepAliveTimeout      = static_cast<uint32>(p.value(QStringLiteral("serverKeepAliveTimeout")).toInteger());
    m_data->filterLANIPs                = p.value(QStringLiteral("filterLANIPs")).toBool();
    m_data->checkDiskspace              = p.value(QStringLiteral("checkDiskspace")).toBool();
    m_data->minFreeDiskSpace            = static_cast<uint64>(p.value(QStringLiteral("minFreeDiskSpace")).toInteger());
    m_data->logToDisk                   = p.value(QStringLiteral("logToDisk")).toBool();
    m_data->verbose                     = p.value(QStringLiteral("verbose")).toBool();
    m_data->closeUPnPOnExit             = p.value(QStringLiteral("closeUPnPOnExit")).toBool();
    m_data->skipWANIPSetup              = p.value(QStringLiteral("skipWANIPSetup")).toBool();
    m_data->skipWANPPPSetup             = p.value(QStringLiteral("skipWANPPPSetup")).toBool();
    m_data->fileBufferSize              = static_cast<uint32>(p.value(QStringLiteral("fileBufferSize")).toInteger());
    m_data->useCreditSystem             = p.value(QStringLiteral("useCreditSystem")).toBool();
    m_data->a4afSaveCpu                 = p.value(QStringLiteral("a4afSaveCpu")).toBool();
    m_data->autoArchivePreviewStart     = p.value(QStringLiteral("autoArchivePreviewStart")).toBool();
    m_data->ed2kHostname                = p.value(QStringLiteral("ed2kHostname")).toString();
    m_data->showExtControls             = p.value(QStringLiteral("showExtControls")).toBool();
    m_data->commitFiles                 = static_cast<int>(p.value(QStringLiteral("commitFiles")).toInteger());
    m_data->extractMetaData             = static_cast<int>(p.value(QStringLiteral("extractMetaData")).toInteger());
    m_data->logLevel                    = static_cast<int>(p.value(QStringLiteral("logLevel")).toInteger());
    m_data->verboseLogToDisk            = p.value(QStringLiteral("verboseLogToDisk")).toBool();
    m_data->logSourceExchange           = p.value(QStringLiteral("logSourceExchange")).toBool();
    m_data->logBannedClients            = p.value(QStringLiteral("logBannedClients")).toBool();
    m_data->logRatingDescReceived       = p.value(QStringLiteral("logRatingDescReceived")).toBool();
    m_data->logSecureIdent              = p.value(QStringLiteral("logSecureIdent")).toBool();
    m_data->logFilteredIPs              = p.value(QStringLiteral("logFilteredIPs")).toBool();
    m_data->logFileSaving               = p.value(QStringLiteral("logFileSaving")).toBool();
    m_data->logA4AF                     = p.value(QStringLiteral("logA4AF")).toBool();
    m_data->logUlDlEvents               = p.value(QStringLiteral("logUlDlEvents")).toBool();
    m_data->logRawSocketPackets         = p.value(QStringLiteral("logRawSocketPackets")).toBool();
    m_data->queueSize                   = static_cast<uint32>(p.value(QStringLiteral("queueSize")).toInteger());

    // USS
    m_data->dynUpEnabled                       = p.value(QStringLiteral("dynUpEnabled")).toBool();
    m_data->dynUpPingTolerance                 = static_cast<int>(p.value(QStringLiteral("dynUpPingTolerance")).toInteger());
    m_data->dynUpPingToleranceMs               = static_cast<int>(p.value(QStringLiteral("dynUpPingToleranceMs")).toInteger());
    m_data->dynUpUseMillisecondPingTolerance   = p.value(QStringLiteral("dynUpUseMillisecondPingTolerance")).toBool();
    m_data->dynUpGoingUpDivider                = static_cast<int>(p.value(QStringLiteral("dynUpGoingUpDivider")).toInteger());
    m_data->dynUpGoingDownDivider              = static_cast<int>(p.value(QStringLiteral("dynUpGoingDownDivider")).toInteger());
    m_data->dynUpNumberOfPings                 = static_cast<int>(p.value(QStringLiteral("dynUpNumberOfPings")).toInteger());

#ifdef Q_OS_WIN
    m_data->autotakeEd2kLinks     = p.value(QStringLiteral("autotakeEd2kLinks")).toBool();
    m_data->openPortsOnWinFirewall = p.value(QStringLiteral("openPortsOnWinFirewall")).toBool();
    m_data->sparsePartFiles       = p.value(QStringLiteral("sparsePartFiles")).toBool();
    m_data->allocFullFile         = p.value(QStringLiteral("allocFullFile")).toBool();
    m_data->resolveShellLinks     = p.value(QStringLiteral("resolveShellLinks")).toBool();
    m_data->multiUserSharing      = static_cast<int>(p.value(QStringLiteral("multiUserSharing")).toInteger());
#endif

    // Directories
    m_data->incomingDir = p.value(QStringLiteral("incomingDir")).toString();
    {
        QStringList temps;
        const QCborArray arr = p.value(QStringLiteral("tempDirs")).toArray();
        temps.reserve(static_cast<int>(arr.size()));
        for (const auto& v : arr)
            temps.append(v.toString());
        m_data->tempDirs = std::move(temps);
    }
    {
        QStringList shared;
        const QCborArray arr = p.value(QStringLiteral("sharedDirs")).toArray();
        shared.reserve(static_cast<int>(arr.size()));
        for (const auto& v : arr)
            shared.append(v.toString());
        m_data->sharedDirs = std::move(shared);
    }

    // Web Server
    m_data->webServerEnabled              = p.value(QStringLiteral("webServerEnabled")).toBool();
    m_data->webServerPort                 = static_cast<uint16>(p.value(QStringLiteral("webServerPort")).toInteger());
    m_data->webServerApiKey               = p.value(QStringLiteral("webServerApiKey")).toString();
    m_data->webServerListenAddress        = p.value(QStringLiteral("webServerListenAddress")).toString();
    m_data->webServerRestApiEnabled       = p.value(QStringLiteral("webServerRestApiEnabled")).toBool();
    m_data->webServerGzipEnabled          = p.value(QStringLiteral("webServerGzipEnabled")).toBool();
    m_data->webServerUPnP                 = p.value(QStringLiteral("webServerUPnP")).toBool();
    m_data->webServerTemplatePath         = p.value(QStringLiteral("webServerTemplatePath")).toString();
    m_data->webServerSessionTimeout       = static_cast<int>(p.value(QStringLiteral("webServerSessionTimeout")).toInteger());
    m_data->webServerHttpsEnabled         = p.value(QStringLiteral("webServerHttpsEnabled")).toBool();
    m_data->webServerCertPath             = p.value(QStringLiteral("webServerCertPath")).toString();
    m_data->webServerKeyPath              = p.value(QStringLiteral("webServerKeyPath")).toString();
    m_data->webServerAdminPassword        = p.value(QStringLiteral("webServerAdminPassword")).toString();
    m_data->webServerAdminAllowHiLevFunc  = p.value(QStringLiteral("webServerAdminAllowHiLevFunc")).toBool();
    m_data->webServerGuestEnabled         = p.value(QStringLiteral("webServerGuestEnabled")).toBool();
    m_data->webServerGuestPassword        = p.value(QStringLiteral("webServerGuestPassword")).toString();
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

void Preferences::validate()
{
    // nick: restore default if empty, truncate to 50 chars
    if (m_data->nick.isEmpty())
        m_data->nick = QStringLiteral("https://emule-qt.org");
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

    // USS: clamp minimums
    if (m_data->dynUpPingTolerance < 100)
        m_data->dynUpPingTolerance = 100;
    if (m_data->dynUpPingToleranceMs < 1)
        m_data->dynUpPingToleranceMs = 1;
    if (m_data->dynUpGoingUpDivider < 1)
        m_data->dynUpGoingUpDivider = 1;
    if (m_data->dynUpGoingDownDivider < 1)
        m_data->dynUpGoingDownDivider = 1;
    if (m_data->dynUpNumberOfPings < 1)
        m_data->dynUpNumberOfPings = 1;

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
#elif defined(Q_OS_WIN)
    QString baseDir;
    switch (AppConfig::multiUserSharingMode()) {
    case 0: // per-user
        baseDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        break;
    case 1: // all-users
        baseDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                  + QStringLiteral("/eMule/eMule Qt");
        break;
    default: // 2 = program-dir (portable)
        baseDir = QCoreApplication::applicationDirPath();
        break;
    }
#else
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
#endif

    if (m_data->incomingDir.isEmpty())
        m_data->incomingDir = baseDir + QStringLiteral("/Incoming");

    if (m_data->configDir.isEmpty()) {
#ifdef Q_OS_WIN
        // In program-dir mode use lowercase "config" to match the bundle layout
        if (AppConfig::multiUserSharingMode() == 2)
            m_data->configDir = baseDir + QStringLiteral("/config");
        else
#endif
        m_data->configDir = baseDir + QStringLiteral("/Config");
    }

    if (m_data->tempDirs.isEmpty())
        m_data->tempDirs.append(baseDir + QStringLiteral("/Temp"));
}

void Preferences::resolveDefaultVideoPlayer()
{
    if (!m_data->videoPlayerCommand.isEmpty())
        return;

    static constexpr const char* vlcPaths[] = {
#ifdef Q_OS_MACOS
        "/Applications/VLC.app/Contents/MacOS/VLC",
#elif defined(Q_OS_WIN)
        "C:\\Program Files\\VideoLAN\\VLC\\vlc.exe",
        "C:\\Program Files (x86)\\VideoLAN\\VLC\\vlc.exe",
#else
        "/usr/bin/vlc",
#endif
    };

    for (const char* path : vlcPaths) {
        if (QFile::exists(QString::fromLatin1(path))) {
            m_data->videoPlayerCommand = QString::fromLatin1(path);
            return;
        }
    }
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
        resolveDefaultVideoPlayer();
        m_data->startVersion = 1;
        m_data->webServerApiKey = generateApiKey();

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
            m_data->promptOnExit = g["promptOnExit"].as<bool>(m_data->promptOnExit);
            m_data->startMinimized = g["startMinimized"].as<bool>(m_data->startMinimized);
            m_data->showSplashScreen = g["showSplashScreen"].as<bool>(m_data->showSplashScreen);
            m_data->language = QString::fromStdString(g["language"].as<std::string>(m_data->language.toStdString()));
            m_data->enableOnlineSignature = g["enableOnlineSignature"].as<bool>(m_data->enableOnlineSignature);
            m_data->enableMiniMule = g["enableMiniMule"].as<bool>(m_data->enableMiniMule);
            m_data->preventStandby = g["preventStandby"].as<bool>(m_data->preventStandby);
            m_data->startWithOS = g["startWithOS"].as<bool>(m_data->startWithOS);
            m_data->startVersion = g["startVersion"].as<uint32>(m_data->startVersion);
            m_data->versionCheckEnabled = g["versionCheckEnabled"].as<bool>(m_data->versionCheckEnabled);
            m_data->versionCheckDays = std::clamp(g["versionCheckDays"].as<int>(m_data->versionCheckDays), 1, 14);
            m_data->lastVersionCheck = g["lastVersionCheck"].as<int64_t>(m_data->lastVersionCheck);
            m_data->bringToFrontOnLinkClick = g["bringToFrontOnLinkClick"].as<bool>(m_data->bringToFrontOnLinkClick);

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
            m_data->deadServerRetries = s["deadServerRetries"].as<uint32>(m_data->deadServerRetries);
            m_data->autoUpdateServerList = s["autoUpdateServerList"].as<bool>(m_data->autoUpdateServerList);
            if (s["serverListURL"])
                m_data->serverListURL = QString::fromStdString(s["serverListURL"].as<std::string>(""));
            m_data->smartLowIdCheck = s["smartLowIdCheck"].as<bool>(m_data->smartLowIdCheck);
            m_data->manualServerHighPriority = s["manualServerHighPriority"].as<bool>(m_data->manualServerHighPriority);
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
            m_data->showOverhead = n["showOverhead"].as<bool>(m_data->showOverhead);
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
            m_data->dynUpEnabled = b["dynUpEnabled"].as<bool>(m_data->dynUpEnabled);
            m_data->dynUpPingTolerance = b["dynUpPingTolerance"].as<int>(m_data->dynUpPingTolerance);
            m_data->dynUpPingToleranceMs = b["dynUpPingToleranceMs"].as<int>(m_data->dynUpPingToleranceMs);
            m_data->dynUpUseMillisecondPingTolerance = b["dynUpUseMillisecondPingTolerance"].as<bool>(m_data->dynUpUseMillisecondPingTolerance);
            m_data->dynUpGoingUpDivider = b["dynUpGoingUpDivider"].as<int>(m_data->dynUpGoingUpDivider);
            m_data->dynUpGoingDownDivider = b["dynUpGoingDownDivider"].as<int>(m_data->dynUpGoingDownDivider);
            m_data->dynUpNumberOfPings = b["dynUpNumberOfPings"].as<int>(m_data->dynUpNumberOfPings);
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

            if (d["sharedDirs"] && d["sharedDirs"].IsSequence()) {
                m_data->sharedDirs.clear();
                for (const auto& item : d["sharedDirs"])
                    m_data->sharedDirs.append(QString::fromStdString(item.as<std::string>("")));
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
            m_data->logLevel = l["logLevel"].as<int>(m_data->logLevel);
            m_data->verboseLogToDisk = l["verboseLogToDisk"].as<bool>(m_data->verboseLogToDisk);
            m_data->logSourceExchange = l["logSourceExchange"].as<bool>(m_data->logSourceExchange);
            m_data->logBannedClients = l["logBannedClients"].as<bool>(m_data->logBannedClients);
            m_data->logRatingDescReceived = l["logRatingDescReceived"].as<bool>(m_data->logRatingDescReceived);
            m_data->logSecureIdent = l["logSecureIdent"].as<bool>(m_data->logSecureIdent);
            m_data->logFilteredIPs = l["logFilteredIPs"].as<bool>(m_data->logFilteredIPs);
            m_data->logFileSaving = l["logFileSaving"].as<bool>(m_data->logFileSaving);
            m_data->logA4AF = l["logA4AF"].as<bool>(m_data->logA4AF);
            m_data->logUlDlEvents = l["logUlDlEvents"].as<bool>(m_data->logUlDlEvents);
            m_data->logRawSocketPackets = l["logRawSocketPackets"].as<bool>(m_data->logRawSocketPackets);
        }

        // Files
        if (auto f = root["files"]) {
            m_data->maxSourcesPerFile = static_cast<uint16>(f["maxSourcesPerFile"].as<int>(m_data->maxSourcesPerFile));
            m_data->useICH = f["useICH"].as<bool>(m_data->useICH);
            m_data->checkDiskspace = f["checkDiskspace"].as<bool>(m_data->checkDiskspace);
            m_data->minFreeDiskSpace = f["minFreeDiskSpace"].as<uint64>(m_data->minFreeDiskSpace);
            m_data->autoSharedFilesPriority = f["autoSharedFilesPriority"].as<bool>(m_data->autoSharedFilesPriority);
            m_data->transferFullChunks = f["transferFullChunks"].as<bool>(m_data->transferFullChunks);
            m_data->previewPrio = f["previewPrio"].as<bool>(m_data->previewPrio);
            m_data->startNextPausedFile = f["startNextPausedFile"].as<bool>(m_data->startNextPausedFile);
            m_data->startNextPausedFileSameCat = f["startNextPausedFileSameCat"].as<bool>(m_data->startNextPausedFileSameCat);
            m_data->startNextPausedFileOnlySameCat = f["startNextPausedFileOnlySameCat"].as<bool>(m_data->startNextPausedFileOnlySameCat);
            m_data->rememberDownloadedFiles = f["rememberDownloadedFiles"].as<bool>(m_data->rememberDownloadedFiles);
            m_data->rememberCancelledFiles = f["rememberCancelledFiles"].as<bool>(m_data->rememberCancelledFiles);
        }

        // Transfer
        if (auto t = root["transfer"]) {
            m_data->fileBufferSize = t["fileBufferSize"].as<uint32>(m_data->fileBufferSize);
            m_data->fileBufferTimeLimit = t["fileBufferTimeLimit"].as<uint32>(m_data->fileBufferTimeLimit);
            m_data->autoDownloadPriority = t["autoDownloadPriority"].as<bool>(m_data->autoDownloadPriority);
            m_data->addNewFilesPaused = t["addNewFilesPaused"].as<bool>(m_data->addNewFilesPaused);
            m_data->useCreditSystem = t["useCreditSystem"].as<bool>(m_data->useCreditSystem);
            m_data->a4afSaveCpu = t["a4afSaveCpu"].as<bool>(m_data->a4afSaveCpu);
            m_data->autoArchivePreviewStart = t["autoArchivePreviewStart"].as<bool>(m_data->autoArchivePreviewStart);
            m_data->ed2kHostname = QString::fromStdString(t["ed2kHostname"].as<std::string>(m_data->ed2kHostname.toStdString()));
            m_data->showExtControls = t["showExtControls"].as<bool>(m_data->showExtControls);
            m_data->commitFiles = t["commitFiles"].as<int>(m_data->commitFiles);
            m_data->extractMetaData = t["extractMetaData"].as<int>(m_data->extractMetaData);
            m_data->queueSize = t["queueSize"].as<uint32>(m_data->queueSize);
#ifdef Q_OS_WIN
            m_data->autotakeEd2kLinks = t["autotakeEd2kLinks"].as<bool>(m_data->autotakeEd2kLinks);
            m_data->openPortsOnWinFirewall = t["openPortsOnWinFirewall"].as<bool>(m_data->openPortsOnWinFirewall);
            m_data->sparsePartFiles = t["sparsePartFiles"].as<bool>(m_data->sparsePartFiles);
            m_data->allocFullFile = t["allocFullFile"].as<bool>(m_data->allocFullFile);
            m_data->resolveShellLinks = t["resolveShellLinks"].as<bool>(m_data->resolveShellLinks);
            m_data->multiUserSharing = t["multiUserSharing"].as<int>(m_data->multiUserSharing);
#endif
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
            m_data->graphsUpdateSec = st["graphsUpdateSec"].as<uint32>(m_data->graphsUpdateSec);
            m_data->statsUpdateSec = st["statsUpdateSec"].as<uint32>(m_data->statsUpdateSec);
            m_data->fillGraphs = st["fillGraphs"].as<bool>(m_data->fillGraphs);
            m_data->statsConnectionsMax = st["statsConnectionsMax"].as<uint32>(m_data->statsConnectionsMax);
            m_data->statsConnectionsRatio = st["statsConnectionsRatio"].as<uint32>(m_data->statsConnectionsRatio);
        }

        // Security
        if (auto sec = root["security"]) {
            m_data->ipFilterLevel = sec["ipFilterLevel"].as<uint32>(m_data->ipFilterLevel);
            m_data->useSecureIdent = sec["useSecureIdent"].as<bool>(m_data->useSecureIdent);
            m_data->viewSharedFilesAccess = sec["viewSharedFilesAccess"].as<int>(m_data->viewSharedFilesAccess);
            m_data->warnUntrustedFiles = sec["warnUntrustedFiles"].as<bool>(m_data->warnUntrustedFiles);
            if (sec["ipFilterUpdateUrl"])
                m_data->ipFilterUpdateUrl = QString::fromStdString(sec["ipFilterUpdateUrl"].as<std::string>());
        }

        // IRC
        if (auto irc = root["irc"]) {
            m_data->ircServer = QString::fromStdString(irc["server"].as<std::string>(m_data->ircServer.toStdString()));
            m_data->ircNick = QString::fromStdString(irc["nick"].as<std::string>(m_data->ircNick.toStdString()));
            m_data->ircEnableUTF8 = irc["enableUTF8"].as<bool>(m_data->ircEnableUTF8);
            m_data->ircUsePerform = irc["usePerform"].as<bool>(m_data->ircUsePerform);
            m_data->ircPerformString = QString::fromStdString(irc["performString"].as<std::string>(m_data->ircPerformString.toStdString()));
            m_data->ircConnectHelpChannel = irc["connectHelpChannel"].as<bool>(m_data->ircConnectHelpChannel);
            m_data->ircLoadChannelList = irc["loadChannelList"].as<bool>(m_data->ircLoadChannelList);
            m_data->ircAddTimestamp = irc["addTimestamp"].as<bool>(m_data->ircAddTimestamp);
            m_data->ircIgnoreMiscInfoMessages = irc["ignoreMiscInfoMessages"].as<bool>(m_data->ircIgnoreMiscInfoMessages);
            m_data->ircIgnoreJoinMessages = irc["ignoreJoinMessages"].as<bool>(m_data->ircIgnoreJoinMessages);
            m_data->ircIgnorePartMessages = irc["ignorePartMessages"].as<bool>(m_data->ircIgnorePartMessages);
            m_data->ircIgnoreQuitMessages = irc["ignoreQuitMessages"].as<bool>(m_data->ircIgnoreQuitMessages);
            m_data->ircUseChannelFilter = irc["useChannelFilter"].as<bool>(m_data->ircUseChannelFilter);
            m_data->ircChannelFilter = QString::fromStdString(irc["channelFilter"].as<std::string>(m_data->ircChannelFilter.toStdString()));
        }

        // Chat / Messages
        if (auto ch = root["chat"]) {
            m_data->msgOnlyFriends = ch["msgOnlyFriends"].as<bool>(m_data->msgOnlyFriends);
            m_data->msgSecure = ch["msgSecure"].as<bool>(m_data->msgSecure);
            m_data->useChatCaptchas = ch["useChatCaptchas"].as<bool>(m_data->useChatCaptchas);
            m_data->enableSpamFilter = ch["enableSpamFilter"].as<bool>(m_data->enableSpamFilter);
            if (ch["messageFilter"])
                m_data->messageFilter = QString::fromStdString(ch["messageFilter"].as<std::string>());
            if (ch["commentFilter"])
                m_data->commentFilter = QString::fromStdString(ch["commentFilter"].as<std::string>());
            m_data->showSmileys = ch["showSmileys"].as<bool>(m_data->showSmileys);
            m_data->indicateRatings = ch["indicateRatings"].as<bool>(m_data->indicateRatings);
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
            m_data->ipcRemotePollingMs = std::clamp(ipc["remotePollingMs"].as<int>(m_data->ipcRemotePollingMs), 200, 10000);
            if (auto tok = ipc["tokens"]) {
                m_data->ipcTokens.clear();
                for (std::size_t i = 0; i < tok.size(); ++i)
                    m_data->ipcTokens.append(QString::fromStdString(tok[i].as<std::string>()));
            }
        }

        // Web Server
        if (auto ws = root["webserver"]) {
            m_data->webServerEnabled = ws["enabled"].as<bool>(m_data->webServerEnabled);
            m_data->webServerPort = static_cast<uint16>(ws["port"].as<int>(m_data->webServerPort));
            m_data->webServerApiKey = QString::fromStdString(ws["apiKey"].as<std::string>(m_data->webServerApiKey.toStdString()));
            m_data->webServerListenAddress = QString::fromStdString(ws["listenAddress"].as<std::string>(m_data->webServerListenAddress.toStdString()));
            m_data->webServerRestApiEnabled = ws["restApiEnabled"].as<bool>(m_data->webServerRestApiEnabled);
            m_data->webServerGzipEnabled = ws["gzipEnabled"].as<bool>(m_data->webServerGzipEnabled);
            m_data->webServerUPnP = ws["upnp"].as<bool>(m_data->webServerUPnP);
            m_data->webServerTemplatePath = QString::fromStdString(ws["templatePath"].as<std::string>(m_data->webServerTemplatePath.toStdString()));
            m_data->webServerSessionTimeout = ws["sessionTimeout"].as<int>(m_data->webServerSessionTimeout);
            m_data->webServerHttpsEnabled = ws["httpsEnabled"].as<bool>(m_data->webServerHttpsEnabled);
            m_data->webServerCertPath = QString::fromStdString(ws["certPath"].as<std::string>(m_data->webServerCertPath.toStdString()));
            m_data->webServerKeyPath = QString::fromStdString(ws["keyPath"].as<std::string>(m_data->webServerKeyPath.toStdString()));
            m_data->webServerAdminPassword = QString::fromStdString(ws["adminPassword"].as<std::string>(m_data->webServerAdminPassword.toStdString()));
            m_data->webServerAdminAllowHiLevFunc = ws["adminAllowHiLevFunc"].as<bool>(m_data->webServerAdminAllowHiLevFunc);
            m_data->webServerGuestEnabled = ws["guestEnabled"].as<bool>(m_data->webServerGuestEnabled);
            m_data->webServerGuestPassword = QString::fromStdString(ws["guestPassword"].as<std::string>(m_data->webServerGuestPassword.toStdString()));
        }

        // Kademlia
        if (auto k = root["kademlia"]) {
            m_data->kadEnabled = k["enabled"].as<bool>(m_data->kadEnabled);
            m_data->kadUDPKey = k["udpKey"].as<uint32>(m_data->kadUDPKey);
        }

        // Scheduler
        if (auto s = root["scheduler"]) {
            m_data->schedulerEnabled = s["enabled"].as<bool>(m_data->schedulerEnabled);
        }

        // Display
        if (auto d = root["display"]) {
            m_data->depth3D = d["depth3D"].as<int>(m_data->depth3D);
            m_data->tooltipDelay = d["tooltipDelay"].as<int>(m_data->tooltipDelay);
            m_data->minimizeToTray = d["minimizeToTray"].as<bool>(m_data->minimizeToTray);
            m_data->transferDoubleClick = d["transferDoubleClick"].as<bool>(m_data->transferDoubleClick);
            m_data->showDwlPercentage = d["showDwlPercentage"].as<bool>(m_data->showDwlPercentage);
            m_data->showRatesInTitle = d["showRatesInTitle"].as<bool>(m_data->showRatesInTitle);
            m_data->showCatTabInfos = d["showCatTabInfos"].as<bool>(m_data->showCatTabInfos);
            m_data->autoRemoveFinishedDownloads = d["autoRemoveFinishedDownloads"].as<bool>(m_data->autoRemoveFinishedDownloads);
            m_data->showTransToolbar = d["showTransToolbar"].as<bool>(m_data->showTransToolbar);
            m_data->storeSearches = d["storeSearches"].as<bool>(m_data->storeSearches);
            m_data->disableKnownClientList = d["disableKnownClientList"].as<bool>(m_data->disableKnownClientList);
            m_data->disableQueueList = d["disableQueueList"].as<bool>(m_data->disableQueueList);
            m_data->useAutoCompletion = d["useAutoCompletion"].as<bool>(m_data->useAutoCompletion);
            m_data->useOriginalIcons = d["useOriginalIcons"].as<bool>(m_data->useOriginalIcons);
            m_data->enableIpcLog = d["enableIpcLog"].as<bool>(m_data->enableIpcLog);
            m_data->startCoreWithConsole = d["startCoreWithConsole"].as<bool>(m_data->startCoreWithConsole);
            m_data->logFont = QString::fromStdString(d["logFont"].as<std::string>(m_data->logFont.toStdString()));
            m_data->watchClipboard4ED2KLinks = d["watchClipboard4ED2KLinks"].as<bool>(m_data->watchClipboard4ED2KLinks);
            m_data->useAdvancedCalcRemainingTime = d["useAdvancedCalcRemainingTime"].as<bool>(m_data->useAdvancedCalcRemainingTime);
            m_data->videoPlayerCommand = QString::fromStdString(d["videoPlayerCommand"].as<std::string>(m_data->videoPlayerCommand.toStdString()));
            m_data->videoPlayerArgs = QString::fromStdString(d["videoPlayerArgs"].as<std::string>(m_data->videoPlayerArgs.toStdString()));
            m_data->createBackupToPreview = d["createBackupToPreview"].as<bool>(m_data->createBackupToPreview);
            m_data->autoCleanupFilenames = d["autoCleanupFilenames"].as<bool>(m_data->autoCleanupFilenames);
        }

        // Notifications
        if (auto n = root["notifications"]) {
            m_data->notifySoundType = n["soundType"].as<int>(m_data->notifySoundType);
            m_data->notifySoundFile = QString::fromStdString(n["soundFile"].as<std::string>(m_data->notifySoundFile.toStdString()));
            m_data->notifyOnLog = n["onLog"].as<bool>(m_data->notifyOnLog);
            m_data->notifyOnChat = n["onChat"].as<bool>(m_data->notifyOnChat);
            m_data->notifyOnChatMsg = n["onChatMsg"].as<bool>(m_data->notifyOnChatMsg);
            m_data->notifyOnDownloadAdded = n["onDownloadAdded"].as<bool>(m_data->notifyOnDownloadAdded);
            m_data->notifyOnDownloadFinished = n["onDownloadFinished"].as<bool>(m_data->notifyOnDownloadFinished);
            m_data->notifyOnNewVersion = n["onNewVersion"].as<bool>(m_data->notifyOnNewVersion);
            m_data->notifyOnUrgent = n["onUrgent"].as<bool>(m_data->notifyOnUrgent);
            m_data->notifyEmailEnabled = n["emailEnabled"].as<bool>(m_data->notifyEmailEnabled);
            m_data->notifyEmailSmtpServer = QString::fromStdString(n["emailSmtpServer"].as<std::string>(m_data->notifyEmailSmtpServer.toStdString()));
            m_data->notifyEmailSmtpPort = static_cast<uint16>(n["emailSmtpPort"].as<int>(m_data->notifyEmailSmtpPort));
            m_data->notifyEmailSmtpAuth = n["emailSmtpAuth"].as<int>(m_data->notifyEmailSmtpAuth);
            m_data->notifyEmailSmtpTls = n["emailSmtpTls"].as<bool>(m_data->notifyEmailSmtpTls);
            m_data->notifyEmailSmtpUser = QString::fromStdString(n["emailSmtpUser"].as<std::string>(m_data->notifyEmailSmtpUser.toStdString()));
            m_data->notifyEmailRecipient = QString::fromStdString(n["emailRecipient"].as<std::string>(m_data->notifyEmailRecipient.toStdString()));
            m_data->notifyEmailSender = QString::fromStdString(n["emailSender"].as<std::string>(m_data->notifyEmailSender.toStdString()));

            // AES encryption key for SMTP password
            if (n["emailEncryptionKey"]) {
                m_data->notifyEmailEncKey = QByteArray::fromHex(
                    QByteArray::fromStdString(n["emailEncryptionKey"].as<std::string>("")));
            }
            // Decrypt SMTP password
            if (n["emailSmtpPasswordEnc"] && !m_data->notifyEmailEncKey.isEmpty()) {
                auto enc = QString::fromStdString(n["emailSmtpPasswordEnc"].as<std::string>(""));
                m_data->notifyEmailSmtpPassword = aesDecrypt(enc, m_data->notifyEmailEncKey);
            }
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
            if (ui["transferSplitSizes"] && ui["transferSplitSizes"].IsSequence()) {
                m_data->transferSplitSizes.clear();
                for (const auto& item : ui["transferSplitSizes"])
                    m_data->transferSplitSizes.append(item.as<int>(0));
            }
            if (ui["sharedHorzSplitSizes"] && ui["sharedHorzSplitSizes"].IsSequence()) {
                m_data->sharedHorzSplitSizes.clear();
                for (const auto& item : ui["sharedHorzSplitSizes"])
                    m_data->sharedHorzSplitSizes.append(item.as<int>(0));
            }
            if (ui["sharedVertSplitSizes"] && ui["sharedVertSplitSizes"].IsSequence()) {
                m_data->sharedVertSplitSizes.clear();
                for (const auto& item : ui["sharedVertSplitSizes"])
                    m_data->sharedVertSplitSizes.append(item.as<int>(0));
            }
            if (ui["messagesSplitSizes"] && ui["messagesSplitSizes"].IsSequence()) {
                m_data->messagesSplitSizes.clear();
                for (const auto& item : ui["messagesSplitSizes"])
                    m_data->messagesSplitSizes.append(item.as<int>(0));
            }
            if (ui["ircSplitSizes"] && ui["ircSplitSizes"].IsSequence()) {
                m_data->ircSplitSizes.clear();
                for (const auto& item : ui["ircSplitSizes"])
                    m_data->ircSplitSizes.append(item.as<int>(0));
            }
            if (ui["statsSplitSizes"] && ui["statsSplitSizes"].IsSequence()) {
                m_data->statsSplitSizes.clear();
                for (const auto& item : ui["statsSplitSizes"])
                    m_data->statsSplitSizes.append(item.as<int>(0));
            }
            m_data->windowWidth     = ui["windowWidth"].as<int>(m_data->windowWidth);
            m_data->windowHeight    = ui["windowHeight"].as<int>(m_data->windowHeight);
            m_data->windowMaximized = ui["windowMaximized"].as<bool>(m_data->windowMaximized);
            m_data->optionsLastPage = ui["optionsLastPage"].as<int>(m_data->optionsLastPage);
            m_data->toolbarButtonStyle = ui["toolbarButtonStyle"].as<int>(m_data->toolbarButtonStyle);
            m_data->toolbarSkinPath = QString::fromStdString(
                ui["toolbarSkinPath"].as<std::string>(std::string{}));
            m_data->skinProfilePath = QString::fromStdString(
                ui["skinProfilePath"].as<std::string>(std::string{}));
            if (ui["toolbarButtonOrder"] && ui["toolbarButtonOrder"].IsSequence()) {
                m_data->toolbarButtonOrder.clear();
                for (const auto& item : ui["toolbarButtonOrder"])
                    m_data->toolbarButtonOrder.append(item.as<int>(0));
            }

            if (ui["headers"] && ui["headers"].IsMap()) {
                for (const auto& pair : ui["headers"]) {
                    auto key = QString::fromStdString(pair.first.as<std::string>());
                    auto val = QByteArray::fromBase64(
                        QByteArray::fromStdString(pair.second.as<std::string>()));
                    m_data->headerStates[key] = val;
                }
            }
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

    // Generate REST API key if missing (existing installs with empty key)
    if (m_data->webServerApiKey.isEmpty()) {
        m_data->webServerApiKey = generateApiKey();
        saveImpl(m_filePath);
    }

    // One-time migrations keyed by startVersion
    if (m_data->startVersion == 0) {
        resolveDefaultVideoPlayer();
        m_data->startVersion = 1;
    }

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

QString Preferences::generateApiKey()
{
    std::uniform_int_distribution<int> dist(0, 255);
    auto& rng = randomEngine();
    QByteArray bytes(16, Qt::Uninitialized);
    for (int i = 0; i < 16; ++i)
        bytes[i] = static_cast<char>(dist(rng));
    return QString::fromLatin1(bytes.toHex());
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
    out << YAML::Key << "promptOnExit" << YAML::Value << m_data->promptOnExit;
    out << YAML::Key << "startMinimized" << YAML::Value << m_data->startMinimized;
    out << YAML::Key << "showSplashScreen" << YAML::Value << m_data->showSplashScreen;
    if (!m_data->language.isEmpty())
        out << YAML::Key << "language" << YAML::Value << m_data->language.toStdString();
    out << YAML::Key << "enableOnlineSignature" << YAML::Value << m_data->enableOnlineSignature;
    out << YAML::Key << "enableMiniMule" << YAML::Value << m_data->enableMiniMule;
    out << YAML::Key << "preventStandby" << YAML::Value << m_data->preventStandby;
    out << YAML::Key << "startWithOS" << YAML::Value << m_data->startWithOS;
    out << YAML::Key << "startVersion" << YAML::Value << m_data->startVersion;
    out << YAML::Key << "versionCheckEnabled" << YAML::Value << m_data->versionCheckEnabled;
    out << YAML::Key << "versionCheckDays" << YAML::Value << m_data->versionCheckDays;
    out << YAML::Key << "lastVersionCheck" << YAML::Value << m_data->lastVersionCheck;
    out << YAML::Key << "bringToFrontOnLinkClick" << YAML::Value << m_data->bringToFrontOnLinkClick;
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
    out << YAML::Key << "deadServerRetries" << YAML::Value << m_data->deadServerRetries;
    out << YAML::Key << "autoUpdateServerList" << YAML::Value << m_data->autoUpdateServerList;
    if (!m_data->serverListURL.isEmpty())
        out << YAML::Key << "serverListURL" << YAML::Value << m_data->serverListURL.toStdString();
    out << YAML::Key << "smartLowIdCheck" << YAML::Value << m_data->smartLowIdCheck;
    out << YAML::Key << "manualServerHighPriority" << YAML::Value << m_data->manualServerHighPriority;
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
    out << YAML::Key << "showOverhead" << YAML::Value << m_data->showOverhead;
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
    out << YAML::Key << "dynUpEnabled" << YAML::Value << m_data->dynUpEnabled;
    out << YAML::Key << "dynUpPingTolerance" << YAML::Value << m_data->dynUpPingTolerance;
    out << YAML::Key << "dynUpPingToleranceMs" << YAML::Value << m_data->dynUpPingToleranceMs;
    out << YAML::Key << "dynUpUseMillisecondPingTolerance" << YAML::Value << m_data->dynUpUseMillisecondPingTolerance;
    out << YAML::Key << "dynUpGoingUpDivider" << YAML::Value << m_data->dynUpGoingUpDivider;
    out << YAML::Key << "dynUpGoingDownDivider" << YAML::Value << m_data->dynUpGoingDownDivider;
    out << YAML::Key << "dynUpNumberOfPings" << YAML::Value << m_data->dynUpNumberOfPings;
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
    out << YAML::Key << "sharedDirs" << YAML::Value << YAML::BeginSeq;
    for (const auto& dir : m_data->sharedDirs)
        out << dir.toStdString();
    out << YAML::EndSeq;
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
    out << YAML::Key << "logLevel" << YAML::Value << m_data->logLevel;
    out << YAML::Key << "verboseLogToDisk" << YAML::Value << m_data->verboseLogToDisk;
    out << YAML::Key << "logSourceExchange" << YAML::Value << m_data->logSourceExchange;
    out << YAML::Key << "logBannedClients" << YAML::Value << m_data->logBannedClients;
    out << YAML::Key << "logRatingDescReceived" << YAML::Value << m_data->logRatingDescReceived;
    out << YAML::Key << "logSecureIdent" << YAML::Value << m_data->logSecureIdent;
    out << YAML::Key << "logFilteredIPs" << YAML::Value << m_data->logFilteredIPs;
    out << YAML::Key << "logFileSaving" << YAML::Value << m_data->logFileSaving;
    out << YAML::Key << "logA4AF" << YAML::Value << m_data->logA4AF;
    out << YAML::Key << "logUlDlEvents" << YAML::Value << m_data->logUlDlEvents;
    out << YAML::Key << "logRawSocketPackets" << YAML::Value << m_data->logRawSocketPackets;
    out << YAML::EndMap;

    // Files
    out << YAML::Key << "files" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "maxSourcesPerFile" << YAML::Value << static_cast<int>(m_data->maxSourcesPerFile);
    out << YAML::Key << "useICH" << YAML::Value << m_data->useICH;
    out << YAML::Key << "checkDiskspace" << YAML::Value << m_data->checkDiskspace;
    out << YAML::Key << "minFreeDiskSpace" << YAML::Value << m_data->minFreeDiskSpace;
    out << YAML::Key << "autoSharedFilesPriority" << YAML::Value << m_data->autoSharedFilesPriority;
    out << YAML::Key << "transferFullChunks" << YAML::Value << m_data->transferFullChunks;
    out << YAML::Key << "previewPrio" << YAML::Value << m_data->previewPrio;
    out << YAML::Key << "startNextPausedFile" << YAML::Value << m_data->startNextPausedFile;
    out << YAML::Key << "startNextPausedFileSameCat" << YAML::Value << m_data->startNextPausedFileSameCat;
    out << YAML::Key << "startNextPausedFileOnlySameCat" << YAML::Value << m_data->startNextPausedFileOnlySameCat;
    out << YAML::Key << "rememberDownloadedFiles" << YAML::Value << m_data->rememberDownloadedFiles;
    out << YAML::Key << "rememberCancelledFiles" << YAML::Value << m_data->rememberCancelledFiles;
    out << YAML::EndMap;

    // Transfer
    out << YAML::Key << "transfer" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "fileBufferSize" << YAML::Value << m_data->fileBufferSize;
    out << YAML::Key << "fileBufferTimeLimit" << YAML::Value << m_data->fileBufferTimeLimit;
    out << YAML::Key << "autoDownloadPriority" << YAML::Value << m_data->autoDownloadPriority;
    out << YAML::Key << "addNewFilesPaused" << YAML::Value << m_data->addNewFilesPaused;
    out << YAML::Key << "useCreditSystem" << YAML::Value << m_data->useCreditSystem;
    out << YAML::Key << "a4afSaveCpu" << YAML::Value << m_data->a4afSaveCpu;
    out << YAML::Key << "autoArchivePreviewStart" << YAML::Value << m_data->autoArchivePreviewStart;
    out << YAML::Key << "ed2kHostname" << YAML::Value << m_data->ed2kHostname.toStdString();
    out << YAML::Key << "showExtControls" << YAML::Value << m_data->showExtControls;
    out << YAML::Key << "commitFiles" << YAML::Value << m_data->commitFiles;
    out << YAML::Key << "extractMetaData" << YAML::Value << m_data->extractMetaData;
    out << YAML::Key << "queueSize" << YAML::Value << m_data->queueSize;
#ifdef Q_OS_WIN
    out << YAML::Key << "autotakeEd2kLinks" << YAML::Value << m_data->autotakeEd2kLinks;
    out << YAML::Key << "openPortsOnWinFirewall" << YAML::Value << m_data->openPortsOnWinFirewall;
    out << YAML::Key << "sparsePartFiles" << YAML::Value << m_data->sparsePartFiles;
    out << YAML::Key << "allocFullFile" << YAML::Value << m_data->allocFullFile;
    out << YAML::Key << "resolveShellLinks" << YAML::Value << m_data->resolveShellLinks;
    out << YAML::Key << "multiUserSharing" << YAML::Value << m_data->multiUserSharing;
#endif
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
    out << YAML::Key << "graphsUpdateSec" << YAML::Value << m_data->graphsUpdateSec;
    out << YAML::Key << "statsUpdateSec" << YAML::Value << m_data->statsUpdateSec;
    out << YAML::Key << "fillGraphs" << YAML::Value << m_data->fillGraphs;
    out << YAML::Key << "statsConnectionsMax" << YAML::Value << m_data->statsConnectionsMax;
    out << YAML::Key << "statsConnectionsRatio" << YAML::Value << m_data->statsConnectionsRatio;
    out << YAML::EndMap;

    // Security
    out << YAML::Key << "security" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "ipFilterLevel" << YAML::Value << m_data->ipFilterLevel;
    out << YAML::Key << "useSecureIdent" << YAML::Value << m_data->useSecureIdent;
    out << YAML::Key << "viewSharedFilesAccess" << YAML::Value << m_data->viewSharedFilesAccess;
    out << YAML::Key << "warnUntrustedFiles" << YAML::Value << m_data->warnUntrustedFiles;
    if (!m_data->ipFilterUpdateUrl.isEmpty())
        out << YAML::Key << "ipFilterUpdateUrl" << YAML::Value << m_data->ipFilterUpdateUrl.toStdString();
    out << YAML::EndMap;

    // IRC
    out << YAML::Key << "irc" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "server" << YAML::Value << m_data->ircServer.toStdString();
    out << YAML::Key << "nick" << YAML::Value << m_data->ircNick.toStdString();
    out << YAML::Key << "enableUTF8" << YAML::Value << m_data->ircEnableUTF8;
    out << YAML::Key << "usePerform" << YAML::Value << m_data->ircUsePerform;
    out << YAML::Key << "performString" << YAML::Value << m_data->ircPerformString.toStdString();
    out << YAML::Key << "connectHelpChannel" << YAML::Value << m_data->ircConnectHelpChannel;
    out << YAML::Key << "loadChannelList" << YAML::Value << m_data->ircLoadChannelList;
    out << YAML::Key << "addTimestamp" << YAML::Value << m_data->ircAddTimestamp;
    out << YAML::Key << "ignoreMiscInfoMessages" << YAML::Value << m_data->ircIgnoreMiscInfoMessages;
    out << YAML::Key << "ignoreJoinMessages" << YAML::Value << m_data->ircIgnoreJoinMessages;
    out << YAML::Key << "ignorePartMessages" << YAML::Value << m_data->ircIgnorePartMessages;
    out << YAML::Key << "ignoreQuitMessages" << YAML::Value << m_data->ircIgnoreQuitMessages;
    out << YAML::Key << "useChannelFilter" << YAML::Value << m_data->ircUseChannelFilter;
    out << YAML::Key << "channelFilter" << YAML::Value << m_data->ircChannelFilter.toStdString();
    out << YAML::EndMap;

    // Chat / Messages
    out << YAML::Key << "chat" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "msgOnlyFriends" << YAML::Value << m_data->msgOnlyFriends;
    out << YAML::Key << "msgSecure" << YAML::Value << m_data->msgSecure;
    out << YAML::Key << "useChatCaptchas" << YAML::Value << m_data->useChatCaptchas;
    out << YAML::Key << "enableSpamFilter" << YAML::Value << m_data->enableSpamFilter;
    out << YAML::Key << "messageFilter" << YAML::Value << m_data->messageFilter.toStdString();
    out << YAML::Key << "commentFilter" << YAML::Value << m_data->commentFilter.toStdString();
    out << YAML::Key << "showSmileys" << YAML::Value << m_data->showSmileys;
    out << YAML::Key << "indicateRatings" << YAML::Value << m_data->indicateRatings;
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
    out << YAML::Key << "remotePollingMs" << YAML::Value << m_data->ipcRemotePollingMs;
    out << YAML::Key << "tokens" << YAML::Value << YAML::BeginSeq;
    for (const auto& t : m_data->ipcTokens)
        out << t.toStdString();
    out << YAML::EndSeq;
    out << YAML::EndMap;

    // Web Server
    out << YAML::Key << "webserver" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "enabled" << YAML::Value << m_data->webServerEnabled;
    out << YAML::Key << "port" << YAML::Value << static_cast<int>(m_data->webServerPort);
    out << YAML::Key << "apiKey" << YAML::Value << m_data->webServerApiKey.toStdString();
    out << YAML::Key << "listenAddress" << YAML::Value << m_data->webServerListenAddress.toStdString();
    out << YAML::Key << "restApiEnabled" << YAML::Value << m_data->webServerRestApiEnabled;
    out << YAML::Key << "gzipEnabled" << YAML::Value << m_data->webServerGzipEnabled;
    out << YAML::Key << "upnp" << YAML::Value << m_data->webServerUPnP;
    out << YAML::Key << "templatePath" << YAML::Value << m_data->webServerTemplatePath.toStdString();
    out << YAML::Key << "sessionTimeout" << YAML::Value << m_data->webServerSessionTimeout;
    out << YAML::Key << "httpsEnabled" << YAML::Value << m_data->webServerHttpsEnabled;
    out << YAML::Key << "certPath" << YAML::Value << m_data->webServerCertPath.toStdString();
    out << YAML::Key << "keyPath" << YAML::Value << m_data->webServerKeyPath.toStdString();
    out << YAML::Key << "adminPassword" << YAML::Value << m_data->webServerAdminPassword.toStdString();
    out << YAML::Key << "adminAllowHiLevFunc" << YAML::Value << m_data->webServerAdminAllowHiLevFunc;
    out << YAML::Key << "guestEnabled" << YAML::Value << m_data->webServerGuestEnabled;
    out << YAML::Key << "guestPassword" << YAML::Value << m_data->webServerGuestPassword.toStdString();
    out << YAML::EndMap;

    // Kademlia
    out << YAML::Key << "kademlia" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "enabled" << YAML::Value << m_data->kadEnabled;
    out << YAML::Key << "udpKey" << YAML::Value << m_data->kadUDPKey;
    out << YAML::EndMap;

    // Scheduler
    out << YAML::Key << "scheduler" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "enabled" << YAML::Value << m_data->schedulerEnabled;
    out << YAML::EndMap;

    // Display
    out << YAML::Key << "display" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "depth3D" << YAML::Value << m_data->depth3D;
    out << YAML::Key << "tooltipDelay" << YAML::Value << m_data->tooltipDelay;
    out << YAML::Key << "minimizeToTray" << YAML::Value << m_data->minimizeToTray;
    out << YAML::Key << "transferDoubleClick" << YAML::Value << m_data->transferDoubleClick;
    out << YAML::Key << "showDwlPercentage" << YAML::Value << m_data->showDwlPercentage;
    out << YAML::Key << "showRatesInTitle" << YAML::Value << m_data->showRatesInTitle;
    out << YAML::Key << "showCatTabInfos" << YAML::Value << m_data->showCatTabInfos;
    out << YAML::Key << "autoRemoveFinishedDownloads" << YAML::Value << m_data->autoRemoveFinishedDownloads;
    out << YAML::Key << "showTransToolbar" << YAML::Value << m_data->showTransToolbar;
    out << YAML::Key << "storeSearches" << YAML::Value << m_data->storeSearches;
    out << YAML::Key << "disableKnownClientList" << YAML::Value << m_data->disableKnownClientList;
    out << YAML::Key << "disableQueueList" << YAML::Value << m_data->disableQueueList;
    out << YAML::Key << "useAutoCompletion" << YAML::Value << m_data->useAutoCompletion;
    out << YAML::Key << "useOriginalIcons" << YAML::Value << m_data->useOriginalIcons;
    out << YAML::Key << "enableIpcLog" << YAML::Value << m_data->enableIpcLog;
    out << YAML::Key << "startCoreWithConsole" << YAML::Value << m_data->startCoreWithConsole;
    if (!m_data->logFont.isEmpty())
        out << YAML::Key << "logFont" << YAML::Value << m_data->logFont.toStdString();
    out << YAML::Key << "watchClipboard4ED2KLinks" << YAML::Value << m_data->watchClipboard4ED2KLinks;
    out << YAML::Key << "useAdvancedCalcRemainingTime" << YAML::Value << m_data->useAdvancedCalcRemainingTime;
    out << YAML::Key << "videoPlayerCommand" << YAML::Value << m_data->videoPlayerCommand.toStdString();
    out << YAML::Key << "videoPlayerArgs" << YAML::Value << m_data->videoPlayerArgs.toStdString();
    out << YAML::Key << "createBackupToPreview" << YAML::Value << m_data->createBackupToPreview;
    out << YAML::Key << "autoCleanupFilenames" << YAML::Value << m_data->autoCleanupFilenames;
    out << YAML::EndMap;

    // Notifications
    out << YAML::Key << "notifications" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "soundType" << YAML::Value << m_data->notifySoundType;
    out << YAML::Key << "soundFile" << YAML::Value << m_data->notifySoundFile.toStdString();
    out << YAML::Key << "onLog" << YAML::Value << m_data->notifyOnLog;
    out << YAML::Key << "onChat" << YAML::Value << m_data->notifyOnChat;
    out << YAML::Key << "onChatMsg" << YAML::Value << m_data->notifyOnChatMsg;
    out << YAML::Key << "onDownloadAdded" << YAML::Value << m_data->notifyOnDownloadAdded;
    out << YAML::Key << "onDownloadFinished" << YAML::Value << m_data->notifyOnDownloadFinished;
    out << YAML::Key << "onNewVersion" << YAML::Value << m_data->notifyOnNewVersion;
    out << YAML::Key << "onUrgent" << YAML::Value << m_data->notifyOnUrgent;
    out << YAML::Key << "emailEnabled" << YAML::Value << m_data->notifyEmailEnabled;
    out << YAML::Key << "emailSmtpServer" << YAML::Value << m_data->notifyEmailSmtpServer.toStdString();
    out << YAML::Key << "emailSmtpPort" << YAML::Value << static_cast<int>(m_data->notifyEmailSmtpPort);
    out << YAML::Key << "emailSmtpAuth" << YAML::Value << m_data->notifyEmailSmtpAuth;
    out << YAML::Key << "emailSmtpTls" << YAML::Value << m_data->notifyEmailSmtpTls;
    out << YAML::Key << "emailSmtpUser" << YAML::Value << m_data->notifyEmailSmtpUser.toStdString();
    out << YAML::Key << "emailRecipient" << YAML::Value << m_data->notifyEmailRecipient.toStdString();
    out << YAML::Key << "emailSender" << YAML::Value << m_data->notifyEmailSender.toStdString();
    // Generate encryption key on first save if needed
    QByteArray encKey = m_data->notifyEmailEncKey;
    if (encKey.isEmpty() && !m_data->notifyEmailSmtpPassword.isEmpty()) {
        encKey.resize(32);
        RAND_bytes(reinterpret_cast<unsigned char*>(encKey.data()), 32);
        m_data->notifyEmailEncKey = encKey;
    }
    if (!encKey.isEmpty()) {
        out << YAML::Key << "emailEncryptionKey" << YAML::Value << encKey.toHex().toStdString();
        out << YAML::Key << "emailSmtpPasswordEnc" << YAML::Value
            << aesEncrypt(m_data->notifyEmailSmtpPassword, encKey).toStdString();
    }
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
    out << YAML::Key << "transferSplitSizes" << YAML::Value << YAML::Flow << YAML::BeginSeq;
    for (int sz : m_data->transferSplitSizes)
        out << sz;
    out << YAML::EndSeq;
    out << YAML::Key << "sharedHorzSplitSizes" << YAML::Value << YAML::Flow << YAML::BeginSeq;
    for (int sz : m_data->sharedHorzSplitSizes)
        out << sz;
    out << YAML::EndSeq;
    out << YAML::Key << "sharedVertSplitSizes" << YAML::Value << YAML::Flow << YAML::BeginSeq;
    for (int sz : m_data->sharedVertSplitSizes)
        out << sz;
    out << YAML::EndSeq;
    out << YAML::Key << "messagesSplitSizes" << YAML::Value << YAML::Flow << YAML::BeginSeq;
    for (int sz : m_data->messagesSplitSizes)
        out << sz;
    out << YAML::EndSeq;
    out << YAML::Key << "ircSplitSizes" << YAML::Value << YAML::Flow << YAML::BeginSeq;
    for (int sz : m_data->ircSplitSizes)
        out << sz;
    out << YAML::EndSeq;
    out << YAML::Key << "statsSplitSizes" << YAML::Value << YAML::Flow << YAML::BeginSeq;
    for (int sz : m_data->statsSplitSizes)
        out << sz;
    out << YAML::EndSeq;
    out << YAML::Key << "windowWidth"     << YAML::Value << m_data->windowWidth;
    out << YAML::Key << "windowHeight"    << YAML::Value << m_data->windowHeight;
    out << YAML::Key << "windowMaximized" << YAML::Value << m_data->windowMaximized;
    out << YAML::Key << "optionsLastPage" << YAML::Value << m_data->optionsLastPage;
    out << YAML::Key << "toolbarButtonStyle" << YAML::Value << m_data->toolbarButtonStyle;
    if (!m_data->toolbarSkinPath.isEmpty())
        out << YAML::Key << "toolbarSkinPath" << YAML::Value << m_data->toolbarSkinPath.toStdString();
    if (!m_data->skinProfilePath.isEmpty())
        out << YAML::Key << "skinProfilePath" << YAML::Value << m_data->skinProfilePath.toStdString();
    if (!m_data->toolbarButtonOrder.isEmpty()) {
        out << YAML::Key << "toolbarButtonOrder" << YAML::Value << YAML::Flow << YAML::BeginSeq;
        for (int id : m_data->toolbarButtonOrder)
            out << id;
        out << YAML::EndSeq;
    }

    if (!m_data->headerStates.isEmpty()) {
        out << YAML::Key << "headers" << YAML::Value << YAML::BeginMap;
        for (auto it = m_data->headerStates.cbegin(); it != m_data->headerStates.cend(); ++it)
            out << YAML::Key << it.key().toStdString()
                << YAML::Value << it.value().toBase64().toStdString();
        out << YAML::EndMap;
    }

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
