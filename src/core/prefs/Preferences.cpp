#include "pch.h"
/// @file Preferences.cpp
/// @brief Central preferences with YAML persistence — implementation.

#include "prefs/Preferences.h"

#include "app/AppConfig.h"
#include "crypto/AesCbc.h"
#include "net/EMSocket.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <QCborArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <atomic>

#include <yaml-cpp/yaml.h>


namespace eMule {

namespace {

/// Bumped whenever load() gains a one-time migration; see the migration block at the end
/// of load().  Stored as `startVersion` in the YAML.
///   1 — video player resolved on first run
///   2 — ipFilterLevel raised off the legacy 100, which filtered nothing
constexpr uint32 kCurrentPrefsVersion = 2;

/// See Preferences::usenetSubjectPatternsRevision().
///
/// Process-scope and never reset: load() replaces the whole Data object (and
/// does it again in its catch), so a member would go back to 0 and a cache
/// holding 0 would look fresh while its rules were stale. Starts at 1 so a
/// zero-initialised cache is always stale on first use.
std::atomic<quint64> g_subjectPatternRevision{1};

/// Bumps that revision on the way out of load(), whichever way load() leaves —
/// the first-run path, the success path and the catch all replace Data, so all
/// three have to invalidate. Declared *after* the write locker so it runs while
/// the lock is still held: a reader that gets in afterwards sees the new data
/// and the new number together.
struct SubjectRevisionBump {
    ~SubjectRevisionBump() { ++g_subjectPatternRevision; }
};

/// Normalise, drop the unusable, collapse duplicates, and enforce the cap.
///
/// Shared by the setter and the YAML loader: a hand-edited file is exactly as
/// capable of naming one server twice, or nine of them, as a stream of
/// configuration links is, and the rest of the code is written as though neither
/// can happen.
[[nodiscard]] QList<HttpCacheServerConfig>
sanitizeHttpCacheServers(const QList<HttpCacheServerConfig>& servers)
{
    QList<HttpCacheServerConfig> clean;

    for (const auto& server : servers) {
        HttpCacheServerConfig entry = server;
        entry.baseUrl = Preferences::normalizeHttpCacheBaseUrl(entry.baseUrl);
        entry.apiKey = entry.apiKey.trimmed();
        if (entry.baseUrl.isEmpty())
            continue;

        const auto duplicate = std::ranges::find_if(clean, [&entry](const auto& seen) {
            return seen.baseUrl == entry.baseUrl;
        });
        if (duplicate != clean.end())
            continue;

        clean.append(entry);
        if (clean.size() >= Preferences::kMaxHttpCacheServers)
            break;
    }

    return clean;
}

/// Do these two strings name the same directory? MFC's EqualPaths.
///
/// Compares resolved paths, not text: a category incoming dir and the global
/// one are the same folder whether or not one of them carries a trailing
/// separator or a "..". An empty string matches nothing, so an unset category
/// path never collides with a real directory.
[[nodiscard]] bool samePath(const QString& a, const QString& b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;
    return QDir::cleanPath(QDir(a).absolutePath())
               .compare(QDir::cleanPath(QDir(b).absolutePath()), Qt::CaseInsensitive)
           == 0;
}

} // namespace

// The SMTP password is stored AES-256-CBC encrypted in the YAML; the cipher now
// lives in crypto/AesCbc.h, shared with HTTP Cache chunk encryption.

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
    bool skipFirewalledChecksInLanMode = false;

    // Server connection
    bool safeServerConnect = true;      // Limit to 1 concurrent connection attempt
    bool autoConnectStaticOnly = false; // Only connect to static servers
    bool useServerPriorities = true;    // Sort servers by priority before connecting
    bool addServersFromServer = true;   // Request server list from connected server
    bool useUserSortedServerList = false; // Honor the user's manual server order at auto-connect
    uint32 serverKeepAliveTimeout = 0;  // Keep-alive interval (ms), 0 = disabled

    // Network
    uint16 port = 0;              // 0 = random
    uint16 udpPort = 0;
    uint16 serverUDPPort = 65535; // 65535 = random, 0 = disabled
    uint16 maxConnections = 500;
    // eMule 2026 bandwidth: modern OS handles hundreds of half-open connections. MFC default: 9
    uint16 maxHalfConnections = 50;
    QString bindAddress;
    QString publicIPv6Override;
    // How many distinct peers must independently report the same public IPv6 (via their
    // client-to-client CT_MOD_YOUR_IP), within the window below, before we adopt it as ours
    // — used only when no eNode-go server has observed our egress. YAML-only, no UI.
    uint32 ipv6PublicPeerConfirmThreshold = 3;
    uint32 ipv6PublicPeerConfirmWindowSecs = 300;
    // The same idea for IPv4, fed by the address servers reflect back in the trailing field
    // of OP_GLOBSERVSTATRES — used only when neither Kad nor an ED2K session knows our
    // address. Two rather than three: in that state the obfuscated stat ping is what carries
    // the reflection, so the pool of servers able to vote at all is small. The window is long
    // because a given server is re-asked at most every UDPSERVSTATREASKTIME (4.5 h), and one
    // stat ping goes out every 5 s, so a large list still takes tens of minutes to sweep.
    // YAML-only, no UI.
    uint32 ipv4PublicServerConfirmThreshold = 2;
    uint32 ipv4PublicServerConfirmWindowSecs = 3600;
    // Alternate freed upload slots between IPv4 and IPv6 peers when both are waiting,
    // so a small IPv6 population isn't permanently outbid on score alone.
    bool separateIPv6Queue = true;
    // Resolve a server hostname AAAA-first instead of A-first. Off by default: a client
    // that reaches a server over IPv6 with no routable IPv4 is assigned a LowID
    // unconditionally, so preferring AAAA on a dual-stack server costs a HighID for
    // nothing. Either way the other family is tried when the first finds no records.
    // YAML-only, no UI.
    bool serverPreferIPv6 = false;

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
    bool cryptLayerRequiredStrict = false;  // MFC hidden .ini option; ignores even server test callbacks
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

    // Download categories. Never empty: index 0 is the implicit "All" category,
    // seeded here so no caller has to cope with a category-less eMule. MFC gets
    // the same guarantee from LoadCats' `i <= Count` loop, which runs once even
    // for a missing Category.ini (srchybrid/Preferences.cpp:2467).
    QList<DownloadCategory> categories{DownloadCategory{.title = QStringLiteral("All")}};

    // UPnP
    bool enableUPnP = true;
    bool closeUPnPOnExit = true;
    uint32 portMapProtocols = 7;   // PCP | NAT-PMP | UPnP
    uint32 portMapLeaseSecs = 3600;
    bool portMapIPv6 = true;
    int portMapMethod = 0;         // learned: PortMapMethod::None
    QString portMapSecret;         // learned: hex, minted on first use

    // Logging
    bool logToDiskCore = false;    // daemon writes emulecored[_Verbose|_Kad].log
    bool logToDiskGui = false;     // GUI writes emuleqt[_Verbose|_Kad].log
    uint32 maxLogFileSize = 1048576; // 1 MB
    bool verbose = true;
    bool logPublicIP = false;
    bool kadVerboseLog = true;
    bool serverVerboseLog = false;  // Gated server TCP/UDP/search handshake logging
    uint32 maxLogLines = 5000;  // Max lines kept per log tab in the GUI
    int logLevel = 5;               // 0-5, higher = more verbose
    bool logSourceExchange = false;
    bool logBannedClients = true;
    bool logRatingDescReceived = true;
    bool logSecureIdent = true;
    bool logFilteredIPs = true;
    bool logFileSaving = false;
    bool logA4AF = false;
    bool logUlDlEvents = true;
    bool logRawSocketPackets = false;
    bool logWebServer = false;
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
    // eMule 2026 bandwidth: larger buffer reduces disk flush frequency at high download speeds. MFC default: 4194304 (4 MB)
    uint32 fileBufferSize = 16777216;   // 16 MB
    uint32 fileBufferTimeLimit = 60;    // seconds

    // Extended (PPgTweaks)
    bool useCreditSystem = true;     // Reward uploaders
    bool a4afSaveCpu = false;        // Skip A4AF swap checks
    bool autoArchivePreviewStart = true; // Auto-scan archive contents in file details
    QString ed2kHostname;            // Hostname (or IPv6 literal) for own eD2K links
    bool ed2kLinkAdvertiseIPv6 = true; // Add our public IPv6 as an s6= source hint
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
    uint32 statsSaveInterval = 60;      // Seconds between cumulative flushes (0=shutdown only)
    uint64 statsLastReset = 0;          // Wall clock of the last statistics reset (0=never)

    // Cumulative Statistics
    uint64 cumTotalUploaded = 0;
    uint64 cumTotalDownloaded = 0;
    uint64 cumTotalUploadedToFriend = 0;

    uint32 cumUpSuccessfulSessions = 0;
    uint32 cumUpFailedSessions = 0;
    uint32 cumUpAvgTime = 0;

    uint32 cumDownSuccessfulSessions = 0;
    uint32 cumDownFailedSessions = 0;
    uint32 cumDownCompletedFiles = 0;
    uint32 cumDownAvgTime = 0;

    uint64 cumUpOverheadTotal = 0;
    uint64 cumUpOverheadTotalPackets = 0;
    uint64 cumUpOverheadFileReq = 0;
    uint64 cumUpOverheadFileReqPackets = 0;
    uint64 cumUpOverheadSrcExch = 0;
    uint64 cumUpOverheadSrcExchPackets = 0;
    uint64 cumUpOverheadServer = 0;
    uint64 cumUpOverheadServerPackets = 0;
    uint64 cumUpOverheadKad = 0;
    uint64 cumUpOverheadKadPackets = 0;

    uint64 cumDownOverheadTotal = 0;
    uint64 cumDownOverheadTotalPackets = 0;
    uint64 cumDownOverheadFileReq = 0;
    uint64 cumDownOverheadFileReqPackets = 0;
    uint64 cumDownOverheadSrcExch = 0;
    uint64 cumDownOverheadSrcExchPackets = 0;
    uint64 cumDownOverheadServer = 0;
    uint64 cumDownOverheadServerPackets = 0;
    uint64 cumDownOverheadKad = 0;
    uint64 cumDownOverheadKadPackets = 0;

    uint32 cumConnPeak = 0;
    uint32 cumConnMaxLimitReached = 0;
    uint32 cumConnReconnects = 0;

    uint64 cumRunTime = 0;
    uint64 cumTransferTime = 0;
    uint64 cumUploadTime = 0;
    uint64 cumDownloadTime = 0;
    uint64 cumServerDuration = 0;

    uint64 cumCompressionGain = 0;
    uint64 cumCorruptionLoss = 0;
    uint32 cumIchPartsSaved = 0;

    // Per-client cumulative upload bytes
    uint64 cumUpEmule = 0;
    uint64 cumUpEDHybrid = 0;
    uint64 cumUpEDonkey = 0;
    uint64 cumUpAMule = 0;
    uint64 cumUpMLdonkey = 0;
    uint64 cumUpShareaza = 0;
    uint64 cumUpEMCompat = 0;

    // Per-client cumulative download bytes
    uint64 cumDownEmule = 0;
    uint64 cumDownEDHybrid = 0;
    uint64 cumDownEDonkey = 0;
    uint64 cumDownAMule = 0;
    uint64 cumDownMLdonkey = 0;
    uint64 cumDownShareaza = 0;
    uint64 cumDownEMCompat = 0;
    uint64 cumDownURL = 0;

    // Per-port cumulative bytes
    uint64 cumUpPort4662 = 0;
    uint64 cumUpPortOther = 0;
    uint64 cumDownPort4662 = 0;
    uint64 cumDownPortOther = 0;

    // Per-source cumulative upload bytes
    uint64 cumUpFromFile = 0;
    uint64 cumUpFromPartfile = 0;
    HttpCacheCounters cumHttpCache;
    UsenetCounters cumUsenet;
    IndexerCounters cumIndexer;

    // Records
    uint32 recMaxWorkingServers = 0;
    uint32 recMaxUsersOnline = 0;
    uint32 recMaxFilesAvail = 0;
    uint64 recMaxSharedFiles = 0;
    uint64 recMaxSharedSize = 0;
    uint64 recMaxAvgFileSize = 0;
    uint64 recMaxLargestFile = 0;

    // Security
    // Threshold list entries are tested against, not the level assigned to a level-less
    // entry (that is IPFilter's kDefaultFilterLevel = 100).  MFC keeps the two apart and
    // ships 127 here (srchybrid/Preferences.cpp:2040); at 100 the test `level < 127`
    // would read `100 < 100` for every level-less entry and block nothing.
    uint32 ipFilterLevel = 127;  // lower = more restrictive
    bool warnUntrustedFiles = true;
    bool useSafeKad = true;
    bool useFastKad = true;
    QString ipFilterUpdateUrl;
    QString appToken;
    QString bugReportApiKey;
    QString bugReportDomain;

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
    // Cached Kad notes-search results (filenames/comments) shown on the File Details page.
    int kadFileNameExpiryDays = 30;   // drop cached entries older than this
    int kadFileNameMaxCount = 100;    // keep at most this many newest entries per file

    // Connection
    // eMule 2026 bandwidth: faster source finding on modern networks. MFC default: 20
    uint16 maxConsPerFive = 40;  // MAXCONPER5SEC — max connections per 5 seconds
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
    bool useSaveLoadSources = true;    // Remember a download's sources across restarts (SLS)
    bool rememberUploadQueue = true;   // Remember the upload queue across restarts (UQS)

    // Disk space
    bool checkDiskspace = true;          // Monitor free disk space
    uint64 minFreeDiskSpace = 20971520;  // 20 MB minimum free space

    // Search
    bool enableSearchResultFilter = true;  // Filter search result spam

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
    int versionCheckDays = 2;          // Check interval in days (1-14)
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
    bool showSpeedGraph = true;
    uint32 speedGraphTimeRangeMin = 15;
    bool storeSearches = true;
    bool disableKnownClientList = false;
    bool disableQueueList = false;
    bool useAutoCompletion = true;
    bool useOriginalIcons = true;
    QString logFont;  // Empty = system default; QFont::toString() format

    // GUI (Files page)
    bool watchClipboard4ED2KLinks = true;
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

    // -- HTTP Cache ---------------------------------------------------------
    // Encrypted chunk offload; see docs/protocol/http-cache-spec.md. YAML-only
    // for now (no Options page) while the feature settles.
    bool httpCacheEnabled = false;
    bool httpCacheAllowDownload = true;
    bool httpCacheAllowUpload = true;
    bool httpCacheAllowRelay = true;
    bool httpCachePublishToKad = true;
    bool httpCacheFetchFromKad = true;
    QList<HttpCacheServerConfig> httpCacheServers;   // credentials AES-encrypted in YAML
    uint32 httpCacheMinClients = 2;
    uint32 httpCacheChunkTtlSeconds = 21600;
    uint64 httpCacheMaxPublishBytesPerDay = UINT64_C(2147483648);
    uint32 httpCachePublishRateKBs = 0;   // 0 = derive from the upload limit
    uint32 httpCacheMaxConcurrentPublishes = 1;
    uint32 httpCacheMaxConcurrentFetches = 2;

    // -- Usenet -------------------------------------------------------------
    // Provider passwords are AES-encrypted in the YAML under the same file key
    // as the SMTP password and the HTTP Cache API keys; see the save path.
    bool usenetEnabled = false;
    QList<NewsServer> usenetServers;
    int usenetRetryIntervalSeconds = 60;
    int usenetDownloadSharePercent = 50;
    bool usenetPar2Repair = true;
    bool usenetPar2RenameFiles = true;
    bool usenetUnpack = true;
    bool usenetCleanupAfterUnpack = true;
    bool usenetDirectUnpack = true;
    bool usenetEncryptedPreview = true;
    QString usenetExternalUnpacker;    // empty = search PATH and the usual places
    int usenetHealthCheck = 1;         // 0 off, 1 sample, 2 full
    int usenetHealthMinPercent = 95;
    bool usenetAutoAddPaused = false;
    QString usenetWatchDir;
    QList<UsenetSubjectPattern> usenetSubjectPatterns;   ///< empty = the built-ins

    // -- Indexers (newznab / torznab), a shared module's settings -------------
    QList<IndexerConfig> indexers;
    QList<IndexerFeed> indexerFeeds;
    int indexerResultLimit = 100;
    int indexerMaxPages = 3;
    int indexerTimeoutSeconds = 30;
    int indexerCapsRefreshDays = 7;

    /// Runtime only — never loaded, never saved. -1 means "no split active", so
    /// maxDownloadForEd2k() falls back to the raw ceiling. Persisting it would
    /// let a crash mid-download leave the user throttled to a stale share with
    /// nothing in the UI to explain it.
    qint64 ed2kDownloadBudget = -1;

};

// ---------------------------------------------------------------------------
// Trivial-field accessor helpers
//
// Every trivial getter/setter routes through these so the read/write locking
// lives in exactly one place. T is deduced from the pointer-to-member, so the
// field type never has to be repeated. Defined here (not in the header)
// because Data is only complete in this translation unit and these are the
// only instantiation sites.
// ---------------------------------------------------------------------------

template<class T> T Preferences::get(T Data::*member) const
{
    QReadLocker lock(&m_lock);
    return (*m_data).*member;
}

template<class T> void Preferences::set(T Data::*member, const T& value)
{
    QWriteLocker lock(&m_lock);
    (*m_data).*member = value;
}

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

QString Preferences::nick() const { return get(&Data::nick); }

void Preferences::setNick(const QString& val) { set(&Data::nick, val); }

std::array<uint8, 16> Preferences::userHash() const { return get(&Data::userHash); }

void Preferences::setUserHash(const std::array<uint8, 16>& val) { set(&Data::userHash, val); }

bool Preferences::autoConnect() const { return get(&Data::autoConnect); }

void Preferences::setAutoConnect(bool val) { set(&Data::autoConnect, val); }

bool Preferences::reconnect() const { return get(&Data::reconnect); }

void Preferences::setReconnect(bool val) { set(&Data::reconnect, val); }

bool Preferences::filterLANIPs() const { return get(&Data::filterLANIPs); }

void Preferences::setFilterLANIPs(bool val) { set(&Data::filterLANIPs, val); }

bool Preferences::skipFirewalledChecksInLanMode() const { return get(&Data::skipFirewalledChecksInLanMode); }

void Preferences::setSkipFirewalledChecksInLanMode(bool val) { set(&Data::skipFirewalledChecksInLanMode, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Server connection
// ---------------------------------------------------------------------------

bool Preferences::safeServerConnect() const { return get(&Data::safeServerConnect); }

void Preferences::setSafeServerConnect(bool val) { set(&Data::safeServerConnect, val); }

bool Preferences::autoConnectStaticOnly() const { return get(&Data::autoConnectStaticOnly); }

void Preferences::setAutoConnectStaticOnly(bool val) { set(&Data::autoConnectStaticOnly, val); }

bool Preferences::useServerPriorities() const { return get(&Data::useServerPriorities); }

void Preferences::setUseServerPriorities(bool val) { set(&Data::useServerPriorities, val); }

bool Preferences::addServersFromServer() const { return get(&Data::addServersFromServer); }

void Preferences::setAddServersFromServer(bool val) { set(&Data::addServersFromServer, val); }

bool Preferences::useUserSortedServerList() const { return get(&Data::useUserSortedServerList); }

void Preferences::setUseUserSortedServerList(bool val) { set(&Data::useUserSortedServerList, val); }

uint32 Preferences::serverKeepAliveTimeout() const { return get(&Data::serverKeepAliveTimeout); }

void Preferences::setServerKeepAliveTimeout(uint32 val) { set(&Data::serverKeepAliveTimeout, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Network
// ---------------------------------------------------------------------------

uint16 Preferences::port() const { return get(&Data::port); }

void Preferences::setPort(uint16 val) { set(&Data::port, val); }

uint16 Preferences::udpPort() const { return get(&Data::udpPort); }

void Preferences::setUdpPort(uint16 val) { set(&Data::udpPort, val); }

uint16 Preferences::serverUDPPort() const { return get(&Data::serverUDPPort); }

void Preferences::setServerUDPPort(uint16 val) { set(&Data::serverUDPPort, val); }

uint16 Preferences::maxConnections() const { return get(&Data::maxConnections); }

void Preferences::setMaxConnections(uint16 val) { set(&Data::maxConnections, val); }

uint16 Preferences::maxHalfConnections() const { return get(&Data::maxHalfConnections); }

void Preferences::setMaxHalfConnections(uint16 val) { set(&Data::maxHalfConnections, val); }

QString Preferences::bindAddress() const { return get(&Data::bindAddress); }

void Preferences::setBindAddress(const QString& val) { set(&Data::bindAddress, val); }

QString Preferences::publicIPv6Override() const { return get(&Data::publicIPv6Override); }

void Preferences::setPublicIPv6Override(const QString& val) { set(&Data::publicIPv6Override, val); }

uint32 Preferences::ipv6PublicPeerConfirmThreshold() const { return get(&Data::ipv6PublicPeerConfirmThreshold); }

void Preferences::setIpv6PublicPeerConfirmThreshold(uint32 val) { set(&Data::ipv6PublicPeerConfirmThreshold, val); }

uint32 Preferences::ipv6PublicPeerConfirmWindowSecs() const { return get(&Data::ipv6PublicPeerConfirmWindowSecs); }

void Preferences::setIpv6PublicPeerConfirmWindowSecs(uint32 val) { set(&Data::ipv6PublicPeerConfirmWindowSecs, val); }

uint32 Preferences::ipv4PublicServerConfirmThreshold() const { return get(&Data::ipv4PublicServerConfirmThreshold); }

void Preferences::setIpv4PublicServerConfirmThreshold(uint32 val) { set(&Data::ipv4PublicServerConfirmThreshold, val); }

uint32 Preferences::ipv4PublicServerConfirmWindowSecs() const { return get(&Data::ipv4PublicServerConfirmWindowSecs); }

void Preferences::setIpv4PublicServerConfirmWindowSecs(uint32 val) { set(&Data::ipv4PublicServerConfirmWindowSecs, val); }

bool Preferences::separateIPv6Queue() const { return get(&Data::separateIPv6Queue); }

void Preferences::setSeparateIPv6Queue(bool val) { set(&Data::separateIPv6Queue, val); }

bool Preferences::serverPreferIPv6() const { return get(&Data::serverPreferIPv6); }

void Preferences::setServerPreferIPv6(bool val) { set(&Data::serverPreferIPv6, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Bandwidth
// ---------------------------------------------------------------------------

uint32 Preferences::maxUpload() const { return get(&Data::maxUpload); }

void Preferences::setMaxUpload(uint32 val) { set(&Data::maxUpload, val); }

uint32 Preferences::maxUploadLimit() const
{
    const uint32 val = maxUpload();
    return val ? val : UNLIMITED;
}

uint32 Preferences::maxDownload() const { return get(&Data::maxDownload); }

void Preferences::setMaxDownload(uint32 val) { set(&Data::maxDownload, val); }

uint32 Preferences::minUpload() const { return get(&Data::minUpload); }

void Preferences::setMinUpload(uint32 val) { set(&Data::minUpload, val); }

uint32 Preferences::maxGraphUploadRate() const { return get(&Data::maxGraphUploadRate); }

void Preferences::setMaxGraphUploadRate(uint32 val) { set(&Data::maxGraphUploadRate, val); }

uint32 Preferences::maxGraphDownloadRate() const { return get(&Data::maxGraphDownloadRate); }

void Preferences::setMaxGraphDownloadRate(uint32 val) { set(&Data::maxGraphDownloadRate, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Encryption
// ---------------------------------------------------------------------------

bool Preferences::cryptLayerSupported() const { return get(&Data::cryptLayerSupported); }

void Preferences::setCryptLayerSupported(bool val) { set(&Data::cryptLayerSupported, val); }

bool Preferences::cryptLayerRequested() const { return get(&Data::cryptLayerRequested); }

void Preferences::setCryptLayerRequested(bool val) { set(&Data::cryptLayerRequested, val); }

bool Preferences::cryptLayerRequired() const { return get(&Data::cryptLayerRequired); }

void Preferences::setCryptLayerRequired(bool val) { set(&Data::cryptLayerRequired, val); }

bool Preferences::cryptLayerRequiredStrict() const { return get(&Data::cryptLayerRequiredStrict); }

void Preferences::setCryptLayerRequiredStrict(bool val) { set(&Data::cryptLayerRequiredStrict, val); }

uint8 Preferences::cryptTCPPaddingLength() const { return get(&Data::cryptTCPPaddingLength); }

void Preferences::setCryptTCPPaddingLength(uint8 val) { set(&Data::cryptTCPPaddingLength, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Proxy
// ---------------------------------------------------------------------------

int Preferences::proxyType() const { return get(&Data::proxyType); }

void Preferences::setProxyType(int val) { set(&Data::proxyType, val); }

QString Preferences::proxyHost() const { return get(&Data::proxyHost); }

void Preferences::setProxyHost(const QString& val) { set(&Data::proxyHost, val); }

uint16 Preferences::proxyPort() const { return get(&Data::proxyPort); }

void Preferences::setProxyPort(uint16 val) { set(&Data::proxyPort, val); }

bool Preferences::proxyEnablePassword() const { return get(&Data::proxyEnablePassword); }

void Preferences::setProxyEnablePassword(bool val) { set(&Data::proxyEnablePassword, val); }

QString Preferences::proxyUser() const { return get(&Data::proxyUser); }

void Preferences::setProxyUser(const QString& val) { set(&Data::proxyUser, val); }

QString Preferences::proxyPassword() const { return get(&Data::proxyPassword); }

void Preferences::setProxyPassword(const QString& val) { set(&Data::proxyPassword, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Directories
// ---------------------------------------------------------------------------

QString Preferences::incomingDir() const { return get(&Data::incomingDir); }

void Preferences::setIncomingDir(const QString& val) { set(&Data::incomingDir, val); }

QStringList Preferences::tempDirs() const { return get(&Data::tempDirs); }

bool Preferences::isShareableDirectory(const QString& dir) const
{
    return isShareableDirectory(dir, configDir(), incomingDir(), tempDirs());
}

void Preferences::setTempDirs(const QStringList& val) { set(&Data::tempDirs, val); }

// ---------------------------------------------------------------------------
// Download categories
// ---------------------------------------------------------------------------

QList<DownloadCategory> Preferences::categories() const { return get(&Data::categories); }

void Preferences::setCategories(const QList<DownloadCategory>& val)
{
    // Sanitised before the lock is taken: the check needs the config, incoming
    // and temp directories, and reading those back here would re-enter a
    // non-recursive lock.
    set(&Data::categories,
        sanitizeCategories(val, configDir(), incomingDir(), tempDirs()));
}

int Preferences::categoryCount() const
{
    QReadLocker lock(&m_lock);
    return static_cast<int>(m_data->categories.size());
}

DownloadCategory Preferences::category(int index) const
{
    QReadLocker lock(&m_lock);
    if (index < 0 || index >= m_data->categories.size())
        return {};
    return m_data->categories.at(index);
}

QString Preferences::incomingDirForCategory(int index) const
{
    const QString path = category(index).incomingPath;

    // The existence test is not redundant with the load-time validation: the
    // user can delete or unmount the folder while eMule runs, and a download
    // that completes then still has to land somewhere. MFC makes the same
    // check at the same moment (srchybrid/PartFile.cpp:2840).
    if (!path.isEmpty() && QDir(path).exists())
        return path;

    return incomingDir();
}

QStringList Preferences::allIncomingDirs() const
{
    QStringList dirs;
    dirs.append(incomingDir());

    for (const auto& cat : categories()) {
        if (cat.incomingPath.isEmpty())
            continue;
        // Two categories may legitimately point at one folder, and one of them
        // may point at the global incoming dir. Callers scan and path-match
        // over this list, so hand it to them already deduplicated — by path
        // identity, not by string equality: "~/Movies" and "~/Movies/" are one
        // directory and scanning it twice would hash every file twice.
        const bool seen = std::ranges::any_of(dirs, [&cat](const QString& known) {
            return samePath(known, cat.incomingPath);
        });
        if (!seen)
            dirs.append(cat.incomingPath);
    }

    return dirs;
}

quint32 Preferences::categoryColor(int index) const { return category(index).color; }

QString Preferences::configDir() const { return resolveConfigDir(get(&Data::configDir)); }

QString Preferences::resolveConfigDir(const QString& stored)
{
    // A --config override must redirect every consumer (server.met, nodes.dat,
    // known.met, ...), not just the preferences.yml lookup in main(). Read the
    // override here rather than writing it into m_data->configDir, so
    // saveImpl() keeps persisting the user's real path and a --config run
    // cannot leak its sandbox path into their preferences.yml.
    const QString overrideDir = AppConfig::configDirOverride();
    return overrideDir.isEmpty() ? stored : overrideDir;
}

void Preferences::setConfigDir(const QString& val) { set(&Data::configDir, val); }

QString Preferences::fileCommentsFilePath() const
{
    // MFC resolves this once in CPreferences::Init (srchybrid/Preferences.cpp:510).
    // Two sequential locks, never nested: get() and configDir() each take the read
    // lock and QReadWriteLock is not recursive.
    const QString stored = get(&Data::fileCommentsFilePath);
    if (!stored.isEmpty())
        return stored;
    return QDir(configDir()).filePath(QStringLiteral("fileinfo.ini"));
}

void Preferences::setFileCommentsFilePath(const QString& val) { set(&Data::fileCommentsFilePath, val); }

QStringList Preferences::sharedDirs() const { return get(&Data::sharedDirs); }

void Preferences::setSharedDirs(const QStringList& val) { set(&Data::sharedDirs, val); }

// ---------------------------------------------------------------------------
// Getters / setters — UPnP
// ---------------------------------------------------------------------------

bool Preferences::enableUPnP() const { return get(&Data::enableUPnP); }

void Preferences::setEnableUPnP(bool val) { set(&Data::enableUPnP, val); }

bool Preferences::closeUPnPOnExit() const { return get(&Data::closeUPnPOnExit); }

void Preferences::setCloseUPnPOnExit(bool val) { set(&Data::closeUPnPOnExit, val); }

uint32 Preferences::portMapProtocols() const { return get(&Data::portMapProtocols); }

void Preferences::setPortMapProtocols(uint32 val) { set(&Data::portMapProtocols, val); }

uint32 Preferences::portMapLeaseSecs() const { return get(&Data::portMapLeaseSecs); }

void Preferences::setPortMapLeaseSecs(uint32 val) { set(&Data::portMapLeaseSecs, val); }

bool Preferences::portMapIPv6() const { return get(&Data::portMapIPv6); }

void Preferences::setPortMapIPv6(bool val) { set(&Data::portMapIPv6, val); }

int Preferences::portMapMethod() const { return get(&Data::portMapMethod); }

void Preferences::setPortMapMethod(int val) { set(&Data::portMapMethod, val); }

QString Preferences::portMapSecret() const { return get(&Data::portMapSecret); }

void Preferences::setPortMapSecret(const QString& val) { set(&Data::portMapSecret, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Logging
// ---------------------------------------------------------------------------

bool Preferences::logToDiskCore() const { return get(&Data::logToDiskCore); }

void Preferences::setLogToDiskCore(bool val) { set(&Data::logToDiskCore, val); }

bool Preferences::logToDiskGui() const { return get(&Data::logToDiskGui); }

void Preferences::setLogToDiskGui(bool val) { set(&Data::logToDiskGui, val); }

uint32 Preferences::maxLogFileSize() const { return get(&Data::maxLogFileSize); }

void Preferences::setMaxLogFileSize(uint32 val) { set(&Data::maxLogFileSize, val); }

bool Preferences::verbose() const { return get(&Data::verbose); }

void Preferences::setVerbose(bool val) { set(&Data::verbose, val); }

bool Preferences::logPublicIP() const { return get(&Data::logPublicIP); }

void Preferences::setLogPublicIP(bool val) { set(&Data::logPublicIP, val); }

bool Preferences::kadVerboseLog() const { return get(&Data::kadVerboseLog); }

void Preferences::setKadVerboseLog(bool val) { set(&Data::kadVerboseLog, val); }

bool Preferences::serverVerboseLog() const { return get(&Data::serverVerboseLog); }

void Preferences::setServerVerboseLog(bool val) { set(&Data::serverVerboseLog, val); }

uint32 Preferences::maxLogLines() const { return get(&Data::maxLogLines); }

void Preferences::setMaxLogLines(uint32 val) { set(&Data::maxLogLines, val); }

int Preferences::logLevel() const { return get(&Data::logLevel); }

void Preferences::setLogLevel(int val) { set(&Data::logLevel, val); }

bool Preferences::logSourceExchange() const { return get(&Data::logSourceExchange); }

void Preferences::setLogSourceExchange(bool val) { set(&Data::logSourceExchange, val); }

bool Preferences::logBannedClients() const { return get(&Data::logBannedClients); }

void Preferences::setLogBannedClients(bool val) { set(&Data::logBannedClients, val); }

bool Preferences::logRatingDescReceived() const { return get(&Data::logRatingDescReceived); }

void Preferences::setLogRatingDescReceived(bool val) { set(&Data::logRatingDescReceived, val); }

bool Preferences::logSecureIdent() const { return get(&Data::logSecureIdent); }

void Preferences::setLogSecureIdent(bool val) { set(&Data::logSecureIdent, val); }

bool Preferences::logFilteredIPs() const { return get(&Data::logFilteredIPs); }

void Preferences::setLogFilteredIPs(bool val) { set(&Data::logFilteredIPs, val); }

bool Preferences::logFileSaving() const { return get(&Data::logFileSaving); }

void Preferences::setLogFileSaving(bool val) { set(&Data::logFileSaving, val); }

bool Preferences::logA4AF() const { return get(&Data::logA4AF); }

void Preferences::setLogA4AF(bool val) { set(&Data::logA4AF, val); }

bool Preferences::logUlDlEvents() const { return get(&Data::logUlDlEvents); }

void Preferences::setLogUlDlEvents(bool val) { set(&Data::logUlDlEvents, val); }

bool Preferences::logRawSocketPackets() const { return get(&Data::logRawSocketPackets); }

void Preferences::setLogRawSocketPackets(bool val) { set(&Data::logRawSocketPackets, val); }

bool Preferences::logWebServer() const { return get(&Data::logWebServer); }

void Preferences::setLogWebServer(bool val) { set(&Data::logWebServer, val); }

bool Preferences::enableIpcLog() const { return get(&Data::enableIpcLog); }
void Preferences::setEnableIpcLog(bool val) { set(&Data::enableIpcLog, val); }
bool Preferences::startCoreWithConsole() const { return get(&Data::startCoreWithConsole); }
void Preferences::setStartCoreWithConsole(bool val) { set(&Data::startCoreWithConsole, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Files
// ---------------------------------------------------------------------------

uint16 Preferences::maxSourcesPerFile() const { return get(&Data::maxSourcesPerFile); }

void Preferences::setMaxSourcesPerFile(uint16 val) { set(&Data::maxSourcesPerFile, val); }

bool Preferences::useICH() const { return get(&Data::useICH); }

void Preferences::setUseICH(bool val) { set(&Data::useICH, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Transfer
// ---------------------------------------------------------------------------

uint32 Preferences::fileBufferSize() const { return get(&Data::fileBufferSize); }

void Preferences::setFileBufferSize(uint32 val) { set(&Data::fileBufferSize, val); }

uint32 Preferences::fileBufferTimeLimit() const { return get(&Data::fileBufferTimeLimit); }

void Preferences::setFileBufferTimeLimit(uint32 val) { set(&Data::fileBufferTimeLimit, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Extended (PPgTweaks)
// ---------------------------------------------------------------------------

bool Preferences::useCreditSystem() const { return get(&Data::useCreditSystem); }

void Preferences::setUseCreditSystem(bool val) { set(&Data::useCreditSystem, val); }

bool Preferences::a4afSaveCpu() const { return get(&Data::a4afSaveCpu); }

void Preferences::setA4afSaveCpu(bool val) { set(&Data::a4afSaveCpu, val); }

bool Preferences::autoArchivePreviewStart() const { return get(&Data::autoArchivePreviewStart); }

void Preferences::setAutoArchivePreviewStart(bool val) { set(&Data::autoArchivePreviewStart, val); }

QString Preferences::ed2kHostname() const { return get(&Data::ed2kHostname); }

void Preferences::setEd2kHostname(const QString& val) { set(&Data::ed2kHostname, val); }

bool Preferences::ed2kLinkAdvertiseIPv6() const { return get(&Data::ed2kLinkAdvertiseIPv6); }

void Preferences::setEd2kLinkAdvertiseIPv6(bool val) { set(&Data::ed2kLinkAdvertiseIPv6, val); }

bool Preferences::showExtControls() const { return get(&Data::showExtControls); }

void Preferences::setShowExtControls(bool val) { set(&Data::showExtControls, val); }

int Preferences::commitFiles() const { return get(&Data::commitFiles); }

void Preferences::setCommitFiles(int val) { set(&Data::commitFiles, val); }

int Preferences::extractMetaData() const { return get(&Data::extractMetaData); }

void Preferences::setExtractMetaData(int val) { set(&Data::extractMetaData, val); }

uint32 Preferences::queueSize() const { return get(&Data::queueSize); }

void Preferences::setQueueSize(uint32 val) { set(&Data::queueSize, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Upload SpeedSense (USS)
// ---------------------------------------------------------------------------

bool Preferences::dynUpEnabled() const { return get(&Data::dynUpEnabled); }

void Preferences::setDynUpEnabled(bool val) { set(&Data::dynUpEnabled, val); }

int Preferences::dynUpPingTolerance() const { return get(&Data::dynUpPingTolerance); }

void Preferences::setDynUpPingTolerance(int val) { set(&Data::dynUpPingTolerance, val); }

int Preferences::dynUpPingToleranceMs() const { return get(&Data::dynUpPingToleranceMs); }

void Preferences::setDynUpPingToleranceMs(int val) { set(&Data::dynUpPingToleranceMs, val); }

bool Preferences::dynUpUseMillisecondPingTolerance() const { return get(&Data::dynUpUseMillisecondPingTolerance); }

void Preferences::setDynUpUseMillisecondPingTolerance(bool val) { set(&Data::dynUpUseMillisecondPingTolerance, val); }

int Preferences::dynUpGoingUpDivider() const { return get(&Data::dynUpGoingUpDivider); }

void Preferences::setDynUpGoingUpDivider(int val) { set(&Data::dynUpGoingUpDivider, val); }

int Preferences::dynUpGoingDownDivider() const { return get(&Data::dynUpGoingDownDivider); }

void Preferences::setDynUpGoingDownDivider(int val) { set(&Data::dynUpGoingDownDivider, val); }

int Preferences::dynUpNumberOfPings() const { return get(&Data::dynUpNumberOfPings); }

void Preferences::setDynUpNumberOfPings(int val) { set(&Data::dynUpNumberOfPings, val); }

#ifdef Q_OS_WIN

bool Preferences::autotakeEd2kLinks() const { return get(&Data::autotakeEd2kLinks); }

void Preferences::setAutotakeEd2kLinks(bool val) { set(&Data::autotakeEd2kLinks, val); }

bool Preferences::openPortsOnWinFirewall() const { return get(&Data::openPortsOnWinFirewall); }

void Preferences::setOpenPortsOnWinFirewall(bool val) { set(&Data::openPortsOnWinFirewall, val); }

bool Preferences::sparsePartFiles() const { return get(&Data::sparsePartFiles); }

void Preferences::setSparsePartFiles(bool val) { set(&Data::sparsePartFiles, val); }

bool Preferences::allocFullFile() const { return get(&Data::allocFullFile); }

void Preferences::setAllocFullFile(bool val) { set(&Data::allocFullFile, val); }

bool Preferences::resolveShellLinks() const { return get(&Data::resolveShellLinks); }

void Preferences::setResolveShellLinks(bool val) { set(&Data::resolveShellLinks, val); }

int Preferences::multiUserSharing() const { return get(&Data::multiUserSharing); }

void Preferences::setMultiUserSharing(int val) { set(&Data::multiUserSharing, val); }

#endif // Q_OS_WIN

// ---------------------------------------------------------------------------
// Getters / setters — Statistics
// ---------------------------------------------------------------------------

float Preferences::connMaxDownRate() const { return get(&Data::connMaxDownRate); }

void Preferences::setConnMaxDownRate(float val) { set(&Data::connMaxDownRate, val); }

float Preferences::connAvgDownRate() const { return get(&Data::connAvgDownRate); }

void Preferences::setConnAvgDownRate(float val) { set(&Data::connAvgDownRate, val); }

float Preferences::connMaxAvgDownRate() const { return get(&Data::connMaxAvgDownRate); }

void Preferences::setConnMaxAvgDownRate(float val) { set(&Data::connMaxAvgDownRate, val); }

float Preferences::connAvgUpRate() const { return get(&Data::connAvgUpRate); }

void Preferences::setConnAvgUpRate(float val) { set(&Data::connAvgUpRate, val); }

float Preferences::connMaxAvgUpRate() const { return get(&Data::connMaxAvgUpRate); }

void Preferences::setConnMaxAvgUpRate(float val) { set(&Data::connMaxAvgUpRate, val); }

float Preferences::connMaxUpRate() const { return get(&Data::connMaxUpRate); }

void Preferences::setConnMaxUpRate(float val) { set(&Data::connMaxUpRate, val); }

uint32 Preferences::statsAverageMinutes() const { return get(&Data::statsAverageMinutes); }

void Preferences::setStatsAverageMinutes(uint32 val) { set(&Data::statsAverageMinutes, val); }

uint32 Preferences::graphsUpdateSec() const { return get(&Data::graphsUpdateSec); }

void Preferences::setGraphsUpdateSec(uint32 val) { set(&Data::graphsUpdateSec, val); }

uint32 Preferences::statsUpdateSec() const { return get(&Data::statsUpdateSec); }

void Preferences::setStatsUpdateSec(uint32 val) { set(&Data::statsUpdateSec, val); }

bool Preferences::fillGraphs() const { return get(&Data::fillGraphs); }

void Preferences::setFillGraphs(bool val) { set(&Data::fillGraphs, val); }

uint32 Preferences::statsConnectionsMax() const { return get(&Data::statsConnectionsMax); }

void Preferences::setStatsConnectionsMax(uint32 val) { set(&Data::statsConnectionsMax, val); }

uint32 Preferences::statsConnectionsRatio() const { return get(&Data::statsConnectionsRatio); }

void Preferences::setStatsConnectionsRatio(uint32 val) { set(&Data::statsConnectionsRatio, val); }

uint32 Preferences::statsSaveInterval() const { return get(&Data::statsSaveInterval); }

void Preferences::setStatsSaveInterval(uint32 val) { set(&Data::statsSaveInterval, val); }

uint64 Preferences::statsLastReset() const { return get(&Data::statsLastReset); }

void Preferences::setStatsLastReset(uint64 val) { set<uint64>(&Data::statsLastReset, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Cumulative Statistics
// ---------------------------------------------------------------------------

// Macro to reduce boilerplate for trivial getter/setter pairs. Routes through
// the get()/set() templates so locking lives in exactly one place.
#define PREF_GET_SET(Type, Name)                                     \
    Type Preferences::Name() const { return get(&Data::Name); }      \
    void Preferences::set##Name(Type val) { set<Type>(&Data::Name, val); }

// Helper with capital first letter for setter name
#define PREF_GS(Type, name, Name)                                    \
    Type Preferences::name() const { return get(&Data::name); }      \
    void Preferences::set##Name(Type val) { set<Type>(&Data::name, val); }

PREF_GS(uint64, cumTotalUploaded, CumTotalUploaded)
PREF_GS(uint64, cumTotalDownloaded, CumTotalDownloaded)
PREF_GS(uint64, cumTotalUploadedToFriend, CumTotalUploadedToFriend)

PREF_GS(uint32, cumUpSuccessfulSessions, CumUpSuccessfulSessions)
PREF_GS(uint32, cumUpFailedSessions, CumUpFailedSessions)
PREF_GS(uint32, cumUpAvgTime, CumUpAvgTime)

PREF_GS(uint32, cumDownSuccessfulSessions, CumDownSuccessfulSessions)
PREF_GS(uint32, cumDownFailedSessions, CumDownFailedSessions)
PREF_GS(uint32, cumDownCompletedFiles, CumDownCompletedFiles)
PREF_GS(uint32, cumDownAvgTime, CumDownAvgTime)

PREF_GS(uint64, cumUpOverheadTotal, CumUpOverheadTotal)
PREF_GS(uint64, cumUpOverheadTotalPackets, CumUpOverheadTotalPackets)
PREF_GS(uint64, cumUpOverheadFileReq, CumUpOverheadFileReq)
PREF_GS(uint64, cumUpOverheadFileReqPackets, CumUpOverheadFileReqPackets)
PREF_GS(uint64, cumUpOverheadSrcExch, CumUpOverheadSrcExch)
PREF_GS(uint64, cumUpOverheadSrcExchPackets, CumUpOverheadSrcExchPackets)
PREF_GS(uint64, cumUpOverheadServer, CumUpOverheadServer)
PREF_GS(uint64, cumUpOverheadServerPackets, CumUpOverheadServerPackets)
PREF_GS(uint64, cumUpOverheadKad, CumUpOverheadKad)
PREF_GS(uint64, cumUpOverheadKadPackets, CumUpOverheadKadPackets)

PREF_GS(uint64, cumDownOverheadTotal, CumDownOverheadTotal)
PREF_GS(uint64, cumDownOverheadTotalPackets, CumDownOverheadTotalPackets)
PREF_GS(uint64, cumDownOverheadFileReq, CumDownOverheadFileReq)
PREF_GS(uint64, cumDownOverheadFileReqPackets, CumDownOverheadFileReqPackets)
PREF_GS(uint64, cumDownOverheadSrcExch, CumDownOverheadSrcExch)
PREF_GS(uint64, cumDownOverheadSrcExchPackets, CumDownOverheadSrcExchPackets)
PREF_GS(uint64, cumDownOverheadServer, CumDownOverheadServer)
PREF_GS(uint64, cumDownOverheadServerPackets, CumDownOverheadServerPackets)
PREF_GS(uint64, cumDownOverheadKad, CumDownOverheadKad)
PREF_GS(uint64, cumDownOverheadKadPackets, CumDownOverheadKadPackets)

PREF_GS(uint32, cumConnPeak, CumConnPeak)
PREF_GS(uint32, cumConnMaxLimitReached, CumConnMaxLimitReached)
PREF_GS(uint32, cumConnReconnects, CumConnReconnects)

PREF_GS(uint64, cumRunTime, CumRunTime)
PREF_GS(uint64, cumTransferTime, CumTransferTime)
PREF_GS(uint64, cumUploadTime, CumUploadTime)
PREF_GS(uint64, cumDownloadTime, CumDownloadTime)
PREF_GS(uint64, cumServerDuration, CumServerDuration)

PREF_GS(uint64, cumCompressionGain, CumCompressionGain)
PREF_GS(uint64, cumCorruptionLoss, CumCorruptionLoss)
PREF_GS(uint32, cumIchPartsSaved, CumIchPartsSaved)

PREF_GS(uint64, cumUpEmule, CumUpEmule)
PREF_GS(uint64, cumUpEDHybrid, CumUpEDHybrid)
PREF_GS(uint64, cumUpEDonkey, CumUpEDonkey)
PREF_GS(uint64, cumUpAMule, CumUpAMule)
PREF_GS(uint64, cumUpMLdonkey, CumUpMLdonkey)
PREF_GS(uint64, cumUpShareaza, CumUpShareaza)
PREF_GS(uint64, cumUpEMCompat, CumUpEMCompat)

PREF_GS(uint64, cumDownEmule, CumDownEmule)
PREF_GS(uint64, cumDownEDHybrid, CumDownEDHybrid)
PREF_GS(uint64, cumDownEDonkey, CumDownEDonkey)
PREF_GS(uint64, cumDownAMule, CumDownAMule)
PREF_GS(uint64, cumDownMLdonkey, CumDownMLdonkey)
PREF_GS(uint64, cumDownShareaza, CumDownShareaza)
PREF_GS(uint64, cumDownEMCompat, CumDownEMCompat)
PREF_GS(uint64, cumDownURL, CumDownURL)

PREF_GS(uint64, cumUpPort4662, CumUpPort4662)
PREF_GS(uint64, cumUpPortOther, CumUpPortOther)
PREF_GS(uint64, cumDownPort4662, CumDownPort4662)
PREF_GS(uint64, cumDownPortOther, CumDownPortOther)

PREF_GS(uint64, cumUpFromFile, CumUpFromFile)
PREF_GS(uint64, cumUpFromPartfile, CumUpFromPartfile)
HttpCacheCounters Preferences::cumHttpCache() const { return get(&Data::cumHttpCache); }
void Preferences::setCumHttpCache(const HttpCacheCounters& val) { set(&Data::cumHttpCache, val); }

UsenetCounters Preferences::cumUsenet() const { return get(&Data::cumUsenet); }
void Preferences::setCumUsenet(const UsenetCounters& val) { set(&Data::cumUsenet, val); }
IndexerCounters Preferences::cumIndexer() const { return get(&Data::cumIndexer); }
void Preferences::setCumIndexer(const IndexerCounters& val) { set(&Data::cumIndexer, val); }

PREF_GS(uint32, recMaxWorkingServers, RecMaxWorkingServers)
PREF_GS(uint32, recMaxUsersOnline, RecMaxUsersOnline)
PREF_GS(uint32, recMaxFilesAvail, RecMaxFilesAvail)
PREF_GS(uint64, recMaxSharedFiles, RecMaxSharedFiles)
PREF_GS(uint64, recMaxSharedSize, RecMaxSharedSize)
PREF_GS(uint64, recMaxAvgFileSize, RecMaxAvgFileSize)
PREF_GS(uint64, recMaxLargestFile, RecMaxLargestFile)

#undef PREF_GET_SET
#undef PREF_GS

namespace {

// A statistics value as YAML: a counter block becomes a nested map keyed by its
// field walk, anything else is a plain scalar.
template<class T>
void emitStatValue(YAML::Emitter& out, const T& value)
{
    if constexpr (CounterBlock<T>) {
        out << YAML::BeginMap;
        T::forEachField([&](const char* key, uint64 T::* m, CounterAgg) {
            out << YAML::Key << key << YAML::Value << value.*m;
        });
        out << YAML::EndMap;
    } else {
        out << value;
    }
}

// The inverse. A missing node or key keeps @p fallback, like as<T>(fallback).
template<class T>
[[nodiscard]] T readStatValue(const YAML::Node& node, const T& fallback)
{
    if constexpr (CounterBlock<T>) {
        T out = fallback;
        if (node && node.IsMap()) {
            T::forEachField([&](const char* key, uint64 T::* m, CounterAgg) {
                out.*m = node[key].template as<uint64>(out.*m);
            });
        }
        return out;
    } else {
        return node.template as<T>(fallback);
    }
}

} // namespace

// The one and only list of what "cumulative statistics" means. Templated on the
// Data type so the same walk serves a mutating visitor (reset, restore) and a
// const one (backup); anything added to the cumulative block belongs here and
// nowhere else.
template<class D, class F>
void Preferences::forEachCumulativeStat(D& d, F&& f)
{
    f("cumTotalUploaded", d.cumTotalUploaded);
    f("cumTotalDownloaded", d.cumTotalDownloaded);
    f("cumTotalUploadedToFriend", d.cumTotalUploadedToFriend);

    f("cumUpSuccessfulSessions", d.cumUpSuccessfulSessions);
    f("cumUpFailedSessions", d.cumUpFailedSessions);
    f("cumUpAvgTime", d.cumUpAvgTime);
    f("cumDownSuccessfulSessions", d.cumDownSuccessfulSessions);
    f("cumDownFailedSessions", d.cumDownFailedSessions);
    f("cumDownCompletedFiles", d.cumDownCompletedFiles);
    f("cumDownAvgTime", d.cumDownAvgTime);

    f("cumUpOverheadTotal", d.cumUpOverheadTotal);
    f("cumUpOverheadTotalPackets", d.cumUpOverheadTotalPackets);
    f("cumUpOverheadFileReq", d.cumUpOverheadFileReq);
    f("cumUpOverheadFileReqPackets", d.cumUpOverheadFileReqPackets);
    f("cumUpOverheadSrcExch", d.cumUpOverheadSrcExch);
    f("cumUpOverheadSrcExchPackets", d.cumUpOverheadSrcExchPackets);
    f("cumUpOverheadServer", d.cumUpOverheadServer);
    f("cumUpOverheadServerPackets", d.cumUpOverheadServerPackets);
    f("cumUpOverheadKad", d.cumUpOverheadKad);
    f("cumUpOverheadKadPackets", d.cumUpOverheadKadPackets);

    f("cumDownOverheadTotal", d.cumDownOverheadTotal);
    f("cumDownOverheadTotalPackets", d.cumDownOverheadTotalPackets);
    f("cumDownOverheadFileReq", d.cumDownOverheadFileReq);
    f("cumDownOverheadFileReqPackets", d.cumDownOverheadFileReqPackets);
    f("cumDownOverheadSrcExch", d.cumDownOverheadSrcExch);
    f("cumDownOverheadSrcExchPackets", d.cumDownOverheadSrcExchPackets);
    f("cumDownOverheadServer", d.cumDownOverheadServer);
    f("cumDownOverheadServerPackets", d.cumDownOverheadServerPackets);
    f("cumDownOverheadKad", d.cumDownOverheadKad);
    f("cumDownOverheadKadPackets", d.cumDownOverheadKadPackets);

    f("cumConnPeak", d.cumConnPeak);
    f("cumConnMaxLimitReached", d.cumConnMaxLimitReached);
    f("cumConnReconnects", d.cumConnReconnects);

    f("cumRunTime", d.cumRunTime);
    f("cumTransferTime", d.cumTransferTime);
    f("cumUploadTime", d.cumUploadTime);
    f("cumDownloadTime", d.cumDownloadTime);
    f("cumServerDuration", d.cumServerDuration);

    f("cumCompressionGain", d.cumCompressionGain);
    f("cumCorruptionLoss", d.cumCorruptionLoss);
    f("cumIchPartsSaved", d.cumIchPartsSaved);

    f("cumUpEmule", d.cumUpEmule);
    f("cumUpEDHybrid", d.cumUpEDHybrid);
    f("cumUpEDonkey", d.cumUpEDonkey);
    f("cumUpAMule", d.cumUpAMule);
    f("cumUpMLdonkey", d.cumUpMLdonkey);
    f("cumUpShareaza", d.cumUpShareaza);
    f("cumUpEMCompat", d.cumUpEMCompat);

    f("cumDownEmule", d.cumDownEmule);
    f("cumDownEDHybrid", d.cumDownEDHybrid);
    f("cumDownEDonkey", d.cumDownEDonkey);
    f("cumDownAMule", d.cumDownAMule);
    f("cumDownMLdonkey", d.cumDownMLdonkey);
    f("cumDownShareaza", d.cumDownShareaza);
    f("cumDownEMCompat", d.cumDownEMCompat);
    f("cumDownURL", d.cumDownURL);

    f("cumUpPort4662", d.cumUpPort4662);
    f("cumUpPortOther", d.cumUpPortOther);
    f("cumDownPort4662", d.cumDownPort4662);
    f("cumDownPortOther", d.cumDownPortOther);

    f("cumUpFromFile", d.cumUpFromFile);
    f("cumUpFromPartfile", d.cumUpFromPartfile);

    // Whole blocks, written as nested maps (emitStatValue/readStatValue).
    f("cumHttpCache", d.cumHttpCache);
    f("cumUsenet", d.cumUsenet);
    f("cumIndexer", d.cumIndexer);

    // The cumulative rates are records rather than sums, but MFC clears them
    // with the rest (srchybrid/Preferences.cpp:1140-1163), so they travel with
    // the cumulative block through backup and restore too.
    f("connMaxDownRate", d.connMaxDownRate);
    f("connAvgDownRate", d.connAvgDownRate);
    f("connMaxAvgDownRate", d.connMaxAvgDownRate);
    f("connAvgUpRate", d.connAvgUpRate);
    f("connMaxAvgUpRate", d.connMaxAvgUpRate);
    f("connMaxUpRate", d.connMaxUpRate);
}

template<class D, class F>
void Preferences::forEachStatRecord(D& d, F&& f)
{
    f("recMaxWorkingServers", d.recMaxWorkingServers);
    f("recMaxUsersOnline", d.recMaxUsersOnline);
    f("recMaxFilesAvail", d.recMaxFilesAvail);
    f("recMaxSharedFiles", d.recMaxSharedFiles);
    f("recMaxSharedSize", d.recMaxSharedSize);
    f("recMaxAvgFileSize", d.recMaxAvgFileSize);
    f("recMaxLargestFile", d.recMaxLargestFile);
}

void Preferences::resetCumulativeStats(uint64 now)
{
    QWriteLocker lock(&m_lock);
    Data& d = *m_data;

    forEachCumulativeStat(d, [](const char*, auto& value) { value = {}; });
    d.statsLastReset = now;
}

QString Preferences::cumulativeStatsBackupPath() const
{
    // Next to the preferences file it mirrors, not next to configDir(): a
    // --config run must not reach into the default directory's backup.
    const QString dir = m_filePath.isEmpty() ? configDir() : QFileInfo(m_filePath).absolutePath();
    return dir + QStringLiteral("/statsbackup.yml");
}

bool Preferences::hasCumulativeStatsBackup() const
{
    return QFileInfo::exists(cumulativeStatsBackupPath());
}

bool Preferences::backupCumulativeStats() const
{
    // Resolve the path before locking: it reads configDir(), which locks too, and
    // m_lock is not recursive.
    const QString path = cumulativeStatsBackupPath();
    QReadLocker lock(&m_lock);
    return writeStatsBackup(*m_data, path);
}

bool Preferences::restoreCumulativeStats()
{
    const QString path = cumulativeStatsBackupPath();
    if (!QFileInfo::exists(path))
        return false;

    QWriteLocker lock(&m_lock);
    Data& d = *m_data;

    // Stash what we are about to overwrite before reading, so a restore can
    // itself be undone by restoring again (MFC renames statbkuptmp.ini over
    // statbkup.ini at the end of LoadStats, srchybrid/Preferences.cpp:1330-1334).
    const QString tmpPath = path + QStringLiteral(".undo");
    if (!writeStatsBackup(d, tmpPath))
        return false;

    try {
        const YAML::Node root = YAML::LoadFile(path.toStdString());

        forEachCumulativeStat(d, [&root](const char* key, auto& value) {
            value = readStatValue(root[key], value);
        });

        // Records are merged, not overwritten: a record set after the reset is
        // still a record (MFC's "Smart Load For Restores", :1299).
        forEachStatRecord(d, [&root](const char* key, auto& value) {
            value = std::max(value, root[key].as<std::decay_t<decltype(value)>>(value));
        });

        d.statsLastReset = root["statsLastReset"].as<uint64>(d.statsLastReset);
    } catch (const YAML::Exception& ex) {
        logError(QStringLiteral("Failed to read statistics backup %1: %2")
                     .arg(path, QString::fromStdString(ex.what())));
        QFile::remove(tmpPath);
        return false;
    }

    QFile::remove(path);
    if (!QFile::rename(tmpPath, path)) {
        logError(QStringLiteral("Failed to swap the statistics backup: %1").arg(tmpPath));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Getters / setters — HTTP Cache
// ---------------------------------------------------------------------------

bool Preferences::httpCacheEnabled() const { return get(&Data::httpCacheEnabled); }

void Preferences::setHttpCacheEnabled(bool val) { set(&Data::httpCacheEnabled, val); }

bool Preferences::httpCacheAllowDownload() const { return get(&Data::httpCacheAllowDownload); }

void Preferences::setHttpCacheAllowDownload(bool val) { set(&Data::httpCacheAllowDownload, val); }

bool Preferences::httpCacheAllowUpload() const { return get(&Data::httpCacheAllowUpload); }

void Preferences::setHttpCacheAllowUpload(bool val) { set(&Data::httpCacheAllowUpload, val); }

bool Preferences::httpCacheAllowRelay() const { return get(&Data::httpCacheAllowRelay); }

void Preferences::setHttpCacheAllowRelay(bool val) { set(&Data::httpCacheAllowRelay, val); }

bool Preferences::httpCachePublishToKad() const { return get(&Data::httpCachePublishToKad); }

void Preferences::setHttpCachePublishToKad(bool val) { set(&Data::httpCachePublishToKad, val); }

bool Preferences::httpCacheFetchFromKad() const { return get(&Data::httpCacheFetchFromKad); }

void Preferences::setHttpCacheFetchFromKad(bool val) { set(&Data::httpCacheFetchFromKad, val); }

QString Preferences::normalizeHttpCacheBaseUrl(const QString& val)
{
    QString clean = val.trimmed();
    while (clean.endsWith(QLatin1Char('/')))
        clean.chop(1);
    return clean;
}

bool Preferences::usenetEnabled() const { return get(&Data::usenetEnabled); }
void Preferences::setUsenetEnabled(bool val) { set(&Data::usenetEnabled, val); }

QList<NewsServer> Preferences::usenetServers() const
{
    return get(&Data::usenetServers);
}

void Preferences::setUsenetServers(const QList<NewsServer>& val)
{
    // Drop unusable entries and cap the list on the way in, so every reader —
    // the pool, the Options page, the IPC handler — sees the same sanitised
    // view and none of them has to re-check.
    QList<NewsServer> clean;
    clean.reserve(val.size());
    for (const auto& server : val) {
        if (!server.isValid())
            continue;
        if (clean.size() >= kMaxUsenetServers)
            break;
        clean.append(server);

        // Every account gets a stable id here, on the one path every writer goes
        // through. The usage meter is keyed by it, and an id minted lazily — or
        // only on load — would give a newly added account a fresh meter on every
        // restart until something happened to write the file.
        if (clean.last().accountId.isEmpty())
            clean.last().accountId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    set(&Data::usenetServers, clean);
}

int Preferences::usenetRetryIntervalSeconds() const
{
    return get(&Data::usenetRetryIntervalSeconds);
}

void Preferences::setUsenetRetryIntervalSeconds(int val)
{
    set(&Data::usenetRetryIntervalSeconds, std::max(0, val));
}

int Preferences::usenetDownloadSharePercent() const
{
    return get(&Data::usenetDownloadSharePercent);
}

void Preferences::setUsenetDownloadSharePercent(int val)
{
    set(&Data::usenetDownloadSharePercent, std::clamp(val, 1, 99));
}

bool Preferences::usenetPar2Repair() const
{
    return get(&Data::usenetPar2Repair);
}

void Preferences::setUsenetPar2Repair(bool val)
{
    set(&Data::usenetPar2Repair, val);
}

bool Preferences::usenetPar2RenameFiles() const
{
    return get(&Data::usenetPar2RenameFiles);
}

void Preferences::setUsenetPar2RenameFiles(bool val)
{
    set(&Data::usenetPar2RenameFiles, val);
}

bool Preferences::usenetUnpack() const
{
    return get(&Data::usenetUnpack);
}

void Preferences::setUsenetUnpack(bool val)
{
    set(&Data::usenetUnpack, val);
}

bool Preferences::usenetCleanupAfterUnpack() const
{
    return get(&Data::usenetCleanupAfterUnpack);
}

void Preferences::setUsenetCleanupAfterUnpack(bool val)
{
    set(&Data::usenetCleanupAfterUnpack, val);
}

bool Preferences::usenetDirectUnpack() const
{
    return get(&Data::usenetDirectUnpack);
}

void Preferences::setUsenetDirectUnpack(bool val)
{
    set(&Data::usenetDirectUnpack, val);
}

bool Preferences::usenetEncryptedPreview() const
{
    return get(&Data::usenetEncryptedPreview);
}

void Preferences::setUsenetEncryptedPreview(bool val)
{
    set(&Data::usenetEncryptedPreview, val);
}

QString Preferences::usenetExternalUnpacker() const
{
    return get(&Data::usenetExternalUnpacker);
}

void Preferences::setUsenetExternalUnpacker(const QString& val)
{
    set(&Data::usenetExternalUnpacker, val);
}

int Preferences::usenetHealthCheck() const
{
    return get(&Data::usenetHealthCheck);
}

void Preferences::setUsenetHealthCheck(int val)
{
    set(&Data::usenetHealthCheck, std::clamp(val, 0, 2));
}

int Preferences::usenetHealthMinPercent() const
{
    return get(&Data::usenetHealthMinPercent);
}

void Preferences::setUsenetHealthMinPercent(int val)
{
    // 0 disables the pause without disabling the figure — a reasonable thing to
    // want: see the number, never be stopped by it.
    set(&Data::usenetHealthMinPercent, std::clamp(val, 0, 100));
}

bool Preferences::usenetAutoAddPaused() const
{
    return get(&Data::usenetAutoAddPaused);
}

void Preferences::setUsenetAutoAddPaused(bool val)
{
    set(&Data::usenetAutoAddPaused, val);
}

QString Preferences::usenetWatchDir() const
{
    return get(&Data::usenetWatchDir);
}

void Preferences::setUsenetWatchDir(const QString& val)
{
    set(&Data::usenetWatchDir, sanitizeWatchDir(val, configDir(), incomingDir(), tempDirs()));
}

QList<UsenetSubjectPattern> Preferences::usenetSubjectPatterns() const
{
    return get(&Data::usenetSubjectPatterns);
}

void Preferences::setUsenetSubjectPatterns(const QList<UsenetSubjectPattern>& val)
{
    // Sanitised before the lock: m_lock is not recursive.
    const QList<UsenetSubjectPattern> clean = sanitizeSubjectPatterns(val);
    set(&Data::usenetSubjectPatterns, clean);
    ++g_subjectPatternRevision;
}

quint64 Preferences::usenetSubjectPatternsRevision()
{
    return g_subjectPatternRevision.load(std::memory_order_relaxed);
}

QList<UsenetSubjectPattern> Preferences::sanitizeSubjectPatterns(
    const QList<UsenetSubjectPattern>& patterns)
{
    QList<UsenetSubjectPattern> out;
    QSet<QString> seen;

    for (const UsenetSubjectPattern& p : patterns) {
        UsenetSubjectPattern clean = p;
        clean.name = clean.name.trimmed();

        // The pattern is *not* trimmed. A regex can legitimately end in a
        // literal space or in " $", and quietly editing what the user wrote is
        // the exact kind of helpfulness this whole preference exists to avoid.
        if (!clean.isValid())
            continue;
        if (seen.contains(clean.key()))
            continue;
        if (out.size() >= kMaxSubjectPatterns)
            break;

        seen.insert(clean.key());
        out.append(std::move(clean));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Indexers (newznab / torznab)
// ---------------------------------------------------------------------------

QList<IndexerConfig> Preferences::indexers() const
{
    return get(&Data::indexers);
}

QList<IndexerFeed> Preferences::indexerFeeds() const
{
    return get(&Data::indexerFeeds);
}

void Preferences::setIndexerFeeds(const QList<IndexerFeed>& val)
{
    set(&Data::indexerFeeds, sanitizeFeeds(val));
}

QList<IndexerFeed> Preferences::sanitizeFeeds(const QList<IndexerFeed>& feeds)
{
    QList<IndexerFeed> clean;
    QSet<QString> seen;

    for (const auto& feed : feeds) {
        IndexerFeed entry = feed;
        entry.name = entry.name.trimmed();
        entry.url = entry.url.trimmed();

        if (!entry.isValid())
            continue;
        if (seen.contains(entry.key()))
            continue;
        seen.insert(entry.key());

        // Clamped here and not only in the dialog. A hand-edited file naming one
        // minute would poll a public indexer ninety-six times an hour, and the
        // usual answer to that is a banned account rather than a warning.
        entry.intervalMinutes = std::max(entry.intervalMinutes, IndexerFeed::kMinIntervalMinutes);
        if (entry.minSize < 0)
            entry.minSize = 0;
        if (entry.maxSize < 0)
            entry.maxSize = 0;
        if (entry.maxAgeDays < 0)
            entry.maxAgeDays = 0;

        clean.append(entry);
        if (clean.size() >= kMaxFeeds)
            break;
    }

    return clean;
}

void Preferences::setIndexers(const QList<IndexerConfig>& val)
{
    // Drop the unusable and de-duplicate by name, the same shape setUsenetServers
    // uses. Name is the identity here: it keys the caps cache on disk and the
    // GetIndexerCaps request, so two accounts sharing one would silently share a
    // cache file.
    QList<IndexerConfig> clean;
    QSet<QString> seen;
    for (const auto& entry : val) {
        if (!entry.isValid())
            continue;
        if (seen.contains(entry.key()))
            continue;
        seen.insert(entry.key());
        clean.append(entry);
        if (clean.size() >= kMaxIndexers)
            break;
    }
    set(&Data::indexers, clean);
}

int Preferences::indexerResultLimit() const { return get(&Data::indexerResultLimit); }
void Preferences::setIndexerResultLimit(int val)
{
    set(&Data::indexerResultLimit, std::clamp(val, 10, 1000));
}

int Preferences::indexerMaxPages() const { return get(&Data::indexerMaxPages); }
void Preferences::setIndexerMaxPages(int val)
{
    set(&Data::indexerMaxPages, std::clamp(val, 1, 20));
}

int Preferences::indexerTimeoutSeconds() const { return get(&Data::indexerTimeoutSeconds); }
void Preferences::setIndexerTimeoutSeconds(int val)
{
    set(&Data::indexerTimeoutSeconds, std::clamp(val, 5, 300));
}

int Preferences::indexerCapsRefreshDays() const { return get(&Data::indexerCapsRefreshDays); }
void Preferences::setIndexerCapsRefreshDays(int val)
{
    set(&Data::indexerCapsRefreshDays, std::clamp(val, 1, 365));
}

uint32 Preferences::maxDownloadForEd2k() const
{
    const qint64 budget = get(&Data::ed2kDownloadBudget);
    return budget < 0 ? maxDownload() : static_cast<uint32>(budget);
}

void Preferences::setEd2kDownloadBudget(qint64 kbPerSecOrNegative)
{
    set(&Data::ed2kDownloadBudget, kbPerSecOrNegative < 0 ? qint64(-1) : kbPerSecOrNegative);
}

QString Preferences::usenetTempDir() const
{
    const QStringList dirs = tempDirs();
    if (dirs.isEmpty() || dirs.first().isEmpty())
        return {};
    return QDir(dirs.first()).filePath(QStringLiteral("Usenet"));
}

bool Preferences::isUsenetTempPath(const QString& path) const
{
    const QString root = usenetTempDir();
    if (root.isEmpty() || path.isEmpty())
        return false;

    // Compare cleaned absolute paths so "temp/./Usenet" and a trailing slash both
    // land on the same string, and require a separator after the root so a
    // sibling directory named "Usenet Archive" does not match.
    const QString cleanRoot = QDir::cleanPath(QDir(root).absolutePath());
    const QString cleanPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());

    // Case-insensitive, matching SharedFileList::samePath and MFC's CompareNoCase.
    // For a guard whose job is to keep files *off* the network, the insensitive
    // comparison is also the conservative one: it can only ever exclude more.
    if (cleanPath.compare(cleanRoot, Qt::CaseInsensitive) == 0)
        return true;
    return cleanPath.startsWith(cleanRoot + QLatin1Char('/'), Qt::CaseInsensitive);
}

QList<HttpCacheServerConfig> Preferences::httpCacheServers() const
{
    return get(&Data::httpCacheServers);
}

void Preferences::setHttpCacheServers(const QList<HttpCacheServerConfig>& val)
{
    // Normalised and de-duplicated on the way in, so every later comparison — the
    // search in addOrUpdateHttpCacheServer(), the manager's per-server health map —
    // can be a plain string equality.
    set(&Data::httpCacheServers, sanitizeHttpCacheServers(val));
}

bool Preferences::addOrUpdateHttpCacheServer(const HttpCacheServerConfig& server)
{
    HttpCacheServerConfig entry = server;
    entry.baseUrl = normalizeHttpCacheBaseUrl(entry.baseUrl);
    entry.apiKey = entry.apiKey.trimmed();
    if (entry.baseUrl.isEmpty())
        return false;

    QWriteLocker lock(&m_lock);

    for (auto& existing : m_data->httpCacheServers) {
        if (existing.baseUrl != entry.baseUrl)
            continue;

        // Re-applying a link for a server we already have is how an operator
        // rotates its credential, so the key, the labels and the enabled flag are
        // all taken from the newer link. The position in the list is not: it is
        // the same server, and moving it would reshuffle the rotation.
        existing = entry;
        return true;
    }

    if (m_data->httpCacheServers.size() >= kMaxHttpCacheServers)
        return false;

    m_data->httpCacheServers.append(entry);
    return true;
}

HttpCacheServerAction Preferences::httpCacheServerAction(const QString& baseUrl,
                                                         const QString& apiKey) const
{
    const QString clean = normalizeHttpCacheBaseUrl(baseUrl);
    const QString key = apiKey.trimmed();

    QReadLocker lock(&m_lock);

    for (const auto& existing : m_data->httpCacheServers) {
        if (existing.baseUrl != clean)
            continue;

        // "Unchanged" has to mean the link would genuinely do nothing, which
        // includes the switches: a disabled entry, or uploads turned off
        // globally, is a link worth acting on even though the credential matches.
        const bool live = existing.enabled && m_data->httpCacheEnabled
            && m_data->httpCacheAllowUpload;
        return (existing.apiKey == key && live) ? HttpCacheServerAction::Unchanged
                                                : HttpCacheServerAction::UpdateKey;
    }

    return m_data->httpCacheServers.size() >= kMaxHttpCacheServers ? HttpCacheServerAction::Full
                                                                   : HttpCacheServerAction::Add;
}

uint32 Preferences::httpCacheMinClients() const
{
    // 1 is meaningful (single-peer testing), 0 is not — it would publish every
    // part of every shared file the moment anything was queued.
    return std::max(1u, get(&Data::httpCacheMinClients));
}

void Preferences::setHttpCacheMinClients(uint32 val)
{
    set(&Data::httpCacheMinClients, std::max(1u, val));
}

uint32 Preferences::httpCacheChunkTtlSeconds() const
{
    return get(&Data::httpCacheChunkTtlSeconds);
}

void Preferences::setHttpCacheChunkTtlSeconds(uint32 val)
{
    set(&Data::httpCacheChunkTtlSeconds, val);
}

uint64 Preferences::httpCacheMaxPublishBytesPerDay() const
{
    return get(&Data::httpCacheMaxPublishBytesPerDay);
}

void Preferences::setHttpCacheMaxPublishBytesPerDay(uint64 val)
{
    set(&Data::httpCacheMaxPublishBytesPerDay, val);
}

uint32 Preferences::httpCachePublishRateKBs() const
{
    return get(&Data::httpCachePublishRateKBs);
}

void Preferences::setHttpCachePublishRateKBs(uint32 val)
{
    set(&Data::httpCachePublishRateKBs, val);
}

uint32 Preferences::httpCacheMaxConcurrentPublishes() const
{
    return std::max(1u, get(&Data::httpCacheMaxConcurrentPublishes));
}

void Preferences::setHttpCacheMaxConcurrentPublishes(uint32 val)
{
    set(&Data::httpCacheMaxConcurrentPublishes, std::max(1u, val));
}

uint32 Preferences::httpCacheMaxConcurrentFetches() const
{
    return std::max(1u, get(&Data::httpCacheMaxConcurrentFetches));
}

void Preferences::setHttpCacheMaxConcurrentFetches(uint32 val)
{
    set(&Data::httpCacheMaxConcurrentFetches, std::max(1u, val));
}

// ---------------------------------------------------------------------------
// Getters / setters — Security
// ---------------------------------------------------------------------------

uint32 Preferences::ipFilterLevel() const { return get(&Data::ipFilterLevel); }

void Preferences::setIpFilterLevel(uint32 val) { set(&Data::ipFilterLevel, val); }

bool Preferences::warnUntrustedFiles() const { return get(&Data::warnUntrustedFiles); }

void Preferences::setWarnUntrustedFiles(bool val) { set(&Data::warnUntrustedFiles, val); }

bool Preferences::useSafeKad() const { return get(&Data::useSafeKad); }

void Preferences::setUseSafeKad(bool val) { set(&Data::useSafeKad, val); }

bool Preferences::useFastKad() const { return get(&Data::useFastKad); }

void Preferences::setUseFastKad(bool val) { set(&Data::useFastKad, val); }

QString Preferences::ipFilterUpdateUrl() const { return get(&Data::ipFilterUpdateUrl); }

void Preferences::setIpFilterUpdateUrl(const QString& val) { set(&Data::ipFilterUpdateUrl, val); }

QString Preferences::appToken() const { return get(&Data::appToken); }

void Preferences::setAppToken(const QString& val) { set(&Data::appToken, val); }

QString Preferences::bugReportApiKey() const { return get(&Data::bugReportApiKey); }

QString Preferences::bugReportDomain() const { return get(&Data::bugReportDomain); }

// ---------------------------------------------------------------------------
// Getters / setters — IRC
// ---------------------------------------------------------------------------

QString Preferences::ircServer() const { return get(&Data::ircServer); }

void Preferences::setIrcServer(const QString& val) { set(&Data::ircServer, val); }

QString Preferences::ircNick() const { return get(&Data::ircNick); }

void Preferences::setIrcNick(const QString& val) { set(&Data::ircNick, val); }

bool Preferences::ircEnableUTF8() const { return get(&Data::ircEnableUTF8); }

void Preferences::setIrcEnableUTF8(bool val) { set(&Data::ircEnableUTF8, val); }

bool Preferences::ircUsePerform() const { return get(&Data::ircUsePerform); }

void Preferences::setIrcUsePerform(bool val) { set(&Data::ircUsePerform, val); }

QString Preferences::ircPerformString() const { return get(&Data::ircPerformString); }

void Preferences::setIrcPerformString(const QString& val) { set(&Data::ircPerformString, val); }

bool Preferences::ircConnectHelpChannel() const { return get(&Data::ircConnectHelpChannel); }

void Preferences::setIrcConnectHelpChannel(bool val) { set(&Data::ircConnectHelpChannel, val); }

bool Preferences::ircLoadChannelList() const { return get(&Data::ircLoadChannelList); }

void Preferences::setIrcLoadChannelList(bool val) { set(&Data::ircLoadChannelList, val); }

bool Preferences::ircAddTimestamp() const { return get(&Data::ircAddTimestamp); }

void Preferences::setIrcAddTimestamp(bool val) { set(&Data::ircAddTimestamp, val); }

bool Preferences::ircIgnoreMiscInfoMessages() const { return get(&Data::ircIgnoreMiscInfoMessages); }

void Preferences::setIrcIgnoreMiscInfoMessages(bool val) { set(&Data::ircIgnoreMiscInfoMessages, val); }

bool Preferences::ircIgnoreJoinMessages() const { return get(&Data::ircIgnoreJoinMessages); }

void Preferences::setIrcIgnoreJoinMessages(bool val) { set(&Data::ircIgnoreJoinMessages, val); }

bool Preferences::ircIgnorePartMessages() const { return get(&Data::ircIgnorePartMessages); }

void Preferences::setIrcIgnorePartMessages(bool val) { set(&Data::ircIgnorePartMessages, val); }

bool Preferences::ircIgnoreQuitMessages() const { return get(&Data::ircIgnoreQuitMessages); }

void Preferences::setIrcIgnoreQuitMessages(bool val) { set(&Data::ircIgnoreQuitMessages, val); }

bool Preferences::ircUseChannelFilter() const { return get(&Data::ircUseChannelFilter); }

void Preferences::setIrcUseChannelFilter(bool val) { set(&Data::ircUseChannelFilter, val); }

QString Preferences::ircChannelFilter() const { return get(&Data::ircChannelFilter); }

void Preferences::setIrcChannelFilter(const QString& val) { set(&Data::ircChannelFilter, val); }

// ---------------------------------------------------------------------------
// Getters / setters — IPC Daemon
// ---------------------------------------------------------------------------

bool Preferences::ipcEnabled() const { return get(&Data::ipcEnabled); }

void Preferences::setIpcEnabled(bool val) { set(&Data::ipcEnabled, val); }

uint16 Preferences::ipcPort() const { return get(&Data::ipcPort); }

void Preferences::setIpcPort(uint16 val) { set(&Data::ipcPort, val); }

QString Preferences::ipcListenAddress() const { return get(&Data::ipcListenAddress); }

void Preferences::setIpcListenAddress(const QString& val) { set(&Data::ipcListenAddress, val); }

QString Preferences::ipcDaemonPath() const { return get(&Data::ipcDaemonPath); }

void Preferences::setIpcDaemonPath(const QString& val) { set(&Data::ipcDaemonPath, val); }

int Preferences::ipcRemotePollingMs() const { return get(&Data::ipcRemotePollingMs); }

void Preferences::setIpcRemotePollingMs(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->ipcRemotePollingMs = std::clamp(val, 200, 10000);
}

QStringList Preferences::ipcTokens() const { return get(&Data::ipcTokens); }

void Preferences::setIpcTokens(const QStringList& val) { set(&Data::ipcTokens, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Web Server
// ---------------------------------------------------------------------------

bool Preferences::webServerEnabled() const { return get(&Data::webServerEnabled); }

void Preferences::setWebServerEnabled(bool val) { set(&Data::webServerEnabled, val); }

uint16 Preferences::webServerPort() const { return get(&Data::webServerPort); }

void Preferences::setWebServerPort(uint16 val) { set(&Data::webServerPort, val); }

QString Preferences::webServerApiKey() const { return get(&Data::webServerApiKey); }

void Preferences::setWebServerApiKey(const QString& val) { set(&Data::webServerApiKey, val); }

QString Preferences::webServerListenAddress() const { return get(&Data::webServerListenAddress); }

void Preferences::setWebServerListenAddress(const QString& val) { set(&Data::webServerListenAddress, val); }

bool Preferences::webServerRestApiEnabled() const { return get(&Data::webServerRestApiEnabled); }

void Preferences::setWebServerRestApiEnabled(bool val) { set(&Data::webServerRestApiEnabled, val); }

bool Preferences::webServerGzipEnabled() const { return get(&Data::webServerGzipEnabled); }

void Preferences::setWebServerGzipEnabled(bool val) { set(&Data::webServerGzipEnabled, val); }

bool Preferences::webServerUPnP() const { return get(&Data::webServerUPnP); }

void Preferences::setWebServerUPnP(bool val) { set(&Data::webServerUPnP, val); }

QString Preferences::webServerTemplatePath() const { return get(&Data::webServerTemplatePath); }

void Preferences::setWebServerTemplatePath(const QString& val) { set(&Data::webServerTemplatePath, val); }

int Preferences::webServerSessionTimeout() const { return get(&Data::webServerSessionTimeout); }

void Preferences::setWebServerSessionTimeout(int val) { set(&Data::webServerSessionTimeout, val); }

bool Preferences::webServerHttpsEnabled() const { return get(&Data::webServerHttpsEnabled); }

void Preferences::setWebServerHttpsEnabled(bool val) { set(&Data::webServerHttpsEnabled, val); }

QString Preferences::webServerCertPath() const { return get(&Data::webServerCertPath); }

void Preferences::setWebServerCertPath(const QString& val) { set(&Data::webServerCertPath, val); }

QString Preferences::webServerKeyPath() const { return get(&Data::webServerKeyPath); }

void Preferences::setWebServerKeyPath(const QString& val) { set(&Data::webServerKeyPath, val); }

QString Preferences::webServerAdminPassword() const { return get(&Data::webServerAdminPassword); }

void Preferences::setWebServerAdminPassword(const QString& val) { set(&Data::webServerAdminPassword, val); }

bool Preferences::webServerAdminAllowHiLevFunc() const { return get(&Data::webServerAdminAllowHiLevFunc); }

void Preferences::setWebServerAdminAllowHiLevFunc(bool val) { set(&Data::webServerAdminAllowHiLevFunc, val); }

bool Preferences::webServerGuestEnabled() const { return get(&Data::webServerGuestEnabled); }

void Preferences::setWebServerGuestEnabled(bool val) { set(&Data::webServerGuestEnabled, val); }

QString Preferences::webServerGuestPassword() const { return get(&Data::webServerGuestPassword); }

void Preferences::setWebServerGuestPassword(const QString& val) { set(&Data::webServerGuestPassword, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Scheduler
// ---------------------------------------------------------------------------

bool Preferences::schedulerEnabled() const { return get(&Data::schedulerEnabled); }

void Preferences::setSchedulerEnabled(bool val) { set(&Data::schedulerEnabled, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Kademlia
// ---------------------------------------------------------------------------

bool Preferences::kadEnabled() const { return get(&Data::kadEnabled); }

void Preferences::setKadEnabled(bool val) { set(&Data::kadEnabled, val); }

uint32 Preferences::kadUDPKey() const { return get(&Data::kadUDPKey); }

void Preferences::setKadUDPKey(uint32 val) { set(&Data::kadUDPKey, val); }

int Preferences::kadFileNameExpiryDays() const { return get(&Data::kadFileNameExpiryDays); }

void Preferences::setKadFileNameExpiryDays(int val) { set(&Data::kadFileNameExpiryDays, val); }

int Preferences::kadFileNameMaxCount() const { return get(&Data::kadFileNameMaxCount); }

void Preferences::setKadFileNameMaxCount(int val) { set(&Data::kadFileNameMaxCount, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Connection
// ---------------------------------------------------------------------------

uint16 Preferences::maxConsPerFive() const { return get(&Data::maxConsPerFive); }

void Preferences::setMaxConsPerFive(uint16 val) { set(&Data::maxConsPerFive, val); }

bool Preferences::showOverhead() const { return get(&Data::showOverhead); }

void Preferences::setShowOverhead(bool val) { set(&Data::showOverhead, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Server management (extended)
// ---------------------------------------------------------------------------

bool Preferences::addServersFromClients() const { return get(&Data::addServersFromClients); }

void Preferences::setAddServersFromClients(bool val) { set(&Data::addServersFromClients, val); }

bool Preferences::filterServerByIP() const { return get(&Data::filterServerByIP); }

void Preferences::setFilterServerByIP(bool val) { set(&Data::filterServerByIP, val); }

uint32 Preferences::deadServerRetries() const { return get(&Data::deadServerRetries); }

void Preferences::setDeadServerRetries(uint32 val) { set(&Data::deadServerRetries, val); }

bool Preferences::autoUpdateServerList() const { return get(&Data::autoUpdateServerList); }

void Preferences::setAutoUpdateServerList(bool val) { set(&Data::autoUpdateServerList, val); }

QString Preferences::serverListURL() const { return get(&Data::serverListURL); }

void Preferences::setServerListURL(const QString& val) { set(&Data::serverListURL, val); }

bool Preferences::smartLowIdCheck() const { return get(&Data::smartLowIdCheck); }

void Preferences::setSmartLowIdCheck(bool val) { set(&Data::smartLowIdCheck, val); }

bool Preferences::manualServerHighPriority() const { return get(&Data::manualServerHighPriority); }

void Preferences::setManualServerHighPriority(bool val) { set(&Data::manualServerHighPriority, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Network modes
// ---------------------------------------------------------------------------

bool Preferences::networkED2K() const { return get(&Data::networkED2K); }

void Preferences::setNetworkED2K(bool val) { set(&Data::networkED2K, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Chat / Messages
// ---------------------------------------------------------------------------

bool Preferences::msgOnlyFriends() const { return get(&Data::msgOnlyFriends); }

void Preferences::setMsgOnlyFriends(bool val) { set(&Data::msgOnlyFriends, val); }

bool Preferences::msgSecure() const { return get(&Data::msgSecure); }

void Preferences::setMsgSecure(bool val) { set(&Data::msgSecure, val); }

bool Preferences::useChatCaptchas() const { return get(&Data::useChatCaptchas); }

void Preferences::setUseChatCaptchas(bool val) { set(&Data::useChatCaptchas, val); }

bool Preferences::enableSpamFilter() const { return get(&Data::enableSpamFilter); }

void Preferences::setEnableSpamFilter(bool val) { set(&Data::enableSpamFilter, val); }

QString Preferences::messageFilter() const { return get(&Data::messageFilter); }

void Preferences::setMessageFilter(const QString& val) { set(&Data::messageFilter, val); }

QString Preferences::commentFilter() const { return get(&Data::commentFilter); }

void Preferences::setCommentFilter(const QString& val) { set(&Data::commentFilter, val); }

bool Preferences::showSmileys() const { return get(&Data::showSmileys); }

void Preferences::setShowSmileys(bool val) { set(&Data::showSmileys, val); }

bool Preferences::indicateRatings() const { return get(&Data::indicateRatings); }

void Preferences::setIndicateRatings(bool val) { set(&Data::indicateRatings, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Security (extended)
// ---------------------------------------------------------------------------

bool Preferences::useSecureIdent() const { return get(&Data::useSecureIdent); }

void Preferences::setUseSecureIdent(bool val) { set(&Data::useSecureIdent, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Shared file visibility
// ---------------------------------------------------------------------------

int Preferences::viewSharedFilesAccess() const { return get(&Data::viewSharedFilesAccess); }

void Preferences::setViewSharedFilesAccess(int val) { set(&Data::viewSharedFilesAccess, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Download behavior
// ---------------------------------------------------------------------------

bool Preferences::autoDownloadPriority() const { return get(&Data::autoDownloadPriority); }

void Preferences::setAutoDownloadPriority(bool val) { set(&Data::autoDownloadPriority, val); }

bool Preferences::addNewFilesPaused() const { return get(&Data::addNewFilesPaused); }

void Preferences::setAddNewFilesPaused(bool val) { set(&Data::addNewFilesPaused, val); }

bool Preferences::useSaveLoadSources() const { return get(&Data::useSaveLoadSources); }

void Preferences::setUseSaveLoadSources(bool val) { set(&Data::useSaveLoadSources, val); }

bool Preferences::rememberUploadQueue() const { return get(&Data::rememberUploadQueue); }

void Preferences::setRememberUploadQueue(bool val) { set(&Data::rememberUploadQueue, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Files (extended)
// ---------------------------------------------------------------------------

bool Preferences::autoSharedFilesPriority() const { return get(&Data::autoSharedFilesPriority); }

void Preferences::setAutoSharedFilesPriority(bool val) { set(&Data::autoSharedFilesPriority, val); }

bool Preferences::transferFullChunks() const { return get(&Data::transferFullChunks); }

void Preferences::setTransferFullChunks(bool val) { set(&Data::transferFullChunks, val); }

bool Preferences::previewPrio() const { return get(&Data::previewPrio); }

void Preferences::setPreviewPrio(bool val) { set(&Data::previewPrio, val); }

bool Preferences::startNextPausedFile() const { return get(&Data::startNextPausedFile); }

void Preferences::setStartNextPausedFile(bool val) { set(&Data::startNextPausedFile, val); }

bool Preferences::startNextPausedFileSameCat() const { return get(&Data::startNextPausedFileSameCat); }

void Preferences::setStartNextPausedFileSameCat(bool val) { set(&Data::startNextPausedFileSameCat, val); }

bool Preferences::startNextPausedFileOnlySameCat() const { return get(&Data::startNextPausedFileOnlySameCat); }

void Preferences::setStartNextPausedFileOnlySameCat(bool val) { set(&Data::startNextPausedFileOnlySameCat, val); }

bool Preferences::rememberDownloadedFiles() const { return get(&Data::rememberDownloadedFiles); }

void Preferences::setRememberDownloadedFiles(bool val) { set(&Data::rememberDownloadedFiles, val); }

bool Preferences::rememberCancelledFiles() const { return get(&Data::rememberCancelledFiles); }

void Preferences::setRememberCancelledFiles(bool val) { set(&Data::rememberCancelledFiles, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Disk space
// ---------------------------------------------------------------------------

bool Preferences::checkDiskspace() const { return get(&Data::checkDiskspace); }

void Preferences::setCheckDiskspace(bool val) { set(&Data::checkDiskspace, val); }

uint64 Preferences::minFreeDiskSpace() const { return get(&Data::minFreeDiskSpace); }

void Preferences::setMinFreeDiskSpace(uint64 val) { set(&Data::minFreeDiskSpace, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Search
// ---------------------------------------------------------------------------

bool Preferences::enableSearchResultFilter() const { return get(&Data::enableSearchResultFilter); }

void Preferences::setEnableSearchResultFilter(bool val) { set(&Data::enableSearchResultFilter, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Network detection
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Getters / setters — GUI (General page)
// ---------------------------------------------------------------------------

bool Preferences::promptOnExit() const { return get(&Data::promptOnExit); }

void Preferences::setPromptOnExit(bool val) { set(&Data::promptOnExit, val); }

bool Preferences::startMinimized() const { return get(&Data::startMinimized); }

void Preferences::setStartMinimized(bool val) { set(&Data::startMinimized, val); }

bool Preferences::showSplashScreen() const { return get(&Data::showSplashScreen); }

void Preferences::setShowSplashScreen(bool val) { set(&Data::showSplashScreen, val); }

QString Preferences::language() const { return get(&Data::language); }

void Preferences::setLanguage(const QString& val) { set(&Data::language, val); }

bool Preferences::enableOnlineSignature() const { return get(&Data::enableOnlineSignature); }

void Preferences::setEnableOnlineSignature(bool val) { set(&Data::enableOnlineSignature, val); }

bool Preferences::enableMiniMule() const { return get(&Data::enableMiniMule); }

void Preferences::setEnableMiniMule(bool val) { set(&Data::enableMiniMule, val); }

bool Preferences::preventStandby() const { return get(&Data::preventStandby); }

void Preferences::setPreventStandby(bool val) { set(&Data::preventStandby, val); }

bool Preferences::startWithOS() const { return get(&Data::startWithOS); }

void Preferences::setStartWithOS(bool val) { set(&Data::startWithOS, val); }

uint32 Preferences::startVersion() const { return get(&Data::startVersion); }

void Preferences::setStartVersion(uint32 val) { set(&Data::startVersion, val); }

bool Preferences::versionCheckEnabled() const { return get(&Data::versionCheckEnabled); }

void Preferences::setVersionCheckEnabled(bool val) { set(&Data::versionCheckEnabled, val); }

int Preferences::versionCheckDays() const { return get(&Data::versionCheckDays); }

void Preferences::setVersionCheckDays(int val)
{
    QWriteLocker lock(&m_lock);
    m_data->versionCheckDays = std::clamp(val, 1, 14);
}

bool Preferences::bringToFrontOnLinkClick() const { return get(&Data::bringToFrontOnLinkClick); }

void Preferences::setBringToFrontOnLinkClick(bool val) { set(&Data::bringToFrontOnLinkClick, val); }

// ---------------------------------------------------------------------------
// Getters / setters — GUI (Display page)
// ---------------------------------------------------------------------------

int Preferences::depth3D() const { return get(&Data::depth3D); }

void Preferences::setDepth3D(int val) { set(&Data::depth3D, val); }

int Preferences::tooltipDelay() const { return get(&Data::tooltipDelay); }

void Preferences::setTooltipDelay(int val) { set(&Data::tooltipDelay, val); }

bool Preferences::minimizeToTray() const { return get(&Data::minimizeToTray); }

void Preferences::setMinimizeToTray(bool val) { set(&Data::minimizeToTray, val); }

bool Preferences::transferDoubleClick() const { return get(&Data::transferDoubleClick); }

void Preferences::setTransferDoubleClick(bool val) { set(&Data::transferDoubleClick, val); }

bool Preferences::showDwlPercentage() const { return get(&Data::showDwlPercentage); }

void Preferences::setShowDwlPercentage(bool val) { set(&Data::showDwlPercentage, val); }

bool Preferences::showRatesInTitle() const { return get(&Data::showRatesInTitle); }

void Preferences::setShowRatesInTitle(bool val) { set(&Data::showRatesInTitle, val); }

bool Preferences::showCatTabInfos() const { return get(&Data::showCatTabInfos); }

void Preferences::setShowCatTabInfos(bool val) { set(&Data::showCatTabInfos, val); }

bool Preferences::autoRemoveFinishedDownloads() const { return get(&Data::autoRemoveFinishedDownloads); }

void Preferences::setAutoRemoveFinishedDownloads(bool val) { set(&Data::autoRemoveFinishedDownloads, val); }

bool Preferences::showTransToolbar() const { return get(&Data::showTransToolbar); }

void Preferences::setShowTransToolbar(bool val) { set(&Data::showTransToolbar, val); }

bool Preferences::showSpeedGraph() const { return get(&Data::showSpeedGraph); }

void Preferences::setShowSpeedGraph(bool val) { set(&Data::showSpeedGraph, val); }

uint32 Preferences::speedGraphTimeRangeMin() const { return get(&Data::speedGraphTimeRangeMin); }

void Preferences::setSpeedGraphTimeRangeMin(uint32 val) { set(&Data::speedGraphTimeRangeMin, val); }

bool Preferences::storeSearches() const { return get(&Data::storeSearches); }

void Preferences::setStoreSearches(bool val) { set(&Data::storeSearches, val); }

bool Preferences::disableKnownClientList() const { return get(&Data::disableKnownClientList); }

void Preferences::setDisableKnownClientList(bool val) { set(&Data::disableKnownClientList, val); }

bool Preferences::disableQueueList() const { return get(&Data::disableQueueList); }

void Preferences::setDisableQueueList(bool val) { set(&Data::disableQueueList, val); }

bool Preferences::useAutoCompletion() const { return get(&Data::useAutoCompletion); }

void Preferences::setUseAutoCompletion(bool val) { set(&Data::useAutoCompletion, val); }

bool Preferences::useOriginalIcons() const { return get(&Data::useOriginalIcons); }

void Preferences::setUseOriginalIcons(bool val) { set(&Data::useOriginalIcons, val); }

QString Preferences::logFont() const { return get(&Data::logFont); }

void Preferences::setLogFont(const QString& val) { set(&Data::logFont, val); }

// ---------------------------------------------------------------------------
// Getters / setters — GUI (Files page)
// ---------------------------------------------------------------------------

bool Preferences::watchClipboard4ED2KLinks() const { return get(&Data::watchClipboard4ED2KLinks); }

void Preferences::setWatchClipboard4ED2KLinks(bool val) { set(&Data::watchClipboard4ED2KLinks, val); }

bool Preferences::useAdvancedCalcRemainingTime() const { return get(&Data::useAdvancedCalcRemainingTime); }

void Preferences::setUseAdvancedCalcRemainingTime(bool val) { set(&Data::useAdvancedCalcRemainingTime, val); }

QString Preferences::videoPlayerCommand() const { return get(&Data::videoPlayerCommand); }

void Preferences::setVideoPlayerCommand(const QString& val) { set(&Data::videoPlayerCommand, val); }

QString Preferences::videoPlayerArgs() const { return get(&Data::videoPlayerArgs); }

void Preferences::setVideoPlayerArgs(const QString& val) { set(&Data::videoPlayerArgs, val); }

bool Preferences::createBackupToPreview() const { return get(&Data::createBackupToPreview); }

void Preferences::setCreateBackupToPreview(bool val) { set(&Data::createBackupToPreview, val); }

bool Preferences::autoCleanupFilenames() const { return get(&Data::autoCleanupFilenames); }

void Preferences::setAutoCleanupFilenames(bool val) { set(&Data::autoCleanupFilenames, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Notifications (GUI-side)
// ---------------------------------------------------------------------------

int Preferences::notifySoundType() const { return get(&Data::notifySoundType); }

void Preferences::setNotifySoundType(int val) { set(&Data::notifySoundType, val); }

QString Preferences::notifySoundFile() const { return get(&Data::notifySoundFile); }

void Preferences::setNotifySoundFile(const QString& val) { set(&Data::notifySoundFile, val); }

// ---------------------------------------------------------------------------
// Getters / setters — Notifications (daemon-side)
// ---------------------------------------------------------------------------

bool Preferences::notifyOnLog() const { return get(&Data::notifyOnLog); }

void Preferences::setNotifyOnLog(bool val) { set(&Data::notifyOnLog, val); }

bool Preferences::notifyOnChat() const { return get(&Data::notifyOnChat); }

void Preferences::setNotifyOnChat(bool val) { set(&Data::notifyOnChat, val); }

bool Preferences::notifyOnChatMsg() const { return get(&Data::notifyOnChatMsg); }

void Preferences::setNotifyOnChatMsg(bool val) { set(&Data::notifyOnChatMsg, val); }

bool Preferences::notifyOnDownloadAdded() const { return get(&Data::notifyOnDownloadAdded); }

void Preferences::setNotifyOnDownloadAdded(bool val) { set(&Data::notifyOnDownloadAdded, val); }

bool Preferences::notifyOnDownloadFinished() const { return get(&Data::notifyOnDownloadFinished); }

void Preferences::setNotifyOnDownloadFinished(bool val) { set(&Data::notifyOnDownloadFinished, val); }

bool Preferences::notifyOnNewVersion() const { return get(&Data::notifyOnNewVersion); }

void Preferences::setNotifyOnNewVersion(bool val) { set(&Data::notifyOnNewVersion, val); }

bool Preferences::notifyOnUrgent() const { return get(&Data::notifyOnUrgent); }

void Preferences::setNotifyOnUrgent(bool val) { set(&Data::notifyOnUrgent, val); }

bool Preferences::notifyEmailEnabled() const { return get(&Data::notifyEmailEnabled); }

void Preferences::setNotifyEmailEnabled(bool val) { set(&Data::notifyEmailEnabled, val); }

QString Preferences::notifyEmailSmtpServer() const { return get(&Data::notifyEmailSmtpServer); }

void Preferences::setNotifyEmailSmtpServer(const QString& val) { set(&Data::notifyEmailSmtpServer, val); }

uint16 Preferences::notifyEmailSmtpPort() const { return get(&Data::notifyEmailSmtpPort); }

void Preferences::setNotifyEmailSmtpPort(uint16 val) { set(&Data::notifyEmailSmtpPort, val); }

int Preferences::notifyEmailSmtpAuth() const { return get(&Data::notifyEmailSmtpAuth); }

void Preferences::setNotifyEmailSmtpAuth(int val) { set(&Data::notifyEmailSmtpAuth, val); }

bool Preferences::notifyEmailSmtpTls() const { return get(&Data::notifyEmailSmtpTls); }

void Preferences::setNotifyEmailSmtpTls(bool val) { set(&Data::notifyEmailSmtpTls, val); }

QString Preferences::notifyEmailSmtpUser() const { return get(&Data::notifyEmailSmtpUser); }

void Preferences::setNotifyEmailSmtpUser(const QString& val) { set(&Data::notifyEmailSmtpUser, val); }

QString Preferences::notifyEmailSmtpPassword() const { return get(&Data::notifyEmailSmtpPassword); }

void Preferences::setNotifyEmailSmtpPassword(const QString& val) { set(&Data::notifyEmailSmtpPassword, val); }

QString Preferences::notifyEmailRecipient() const { return get(&Data::notifyEmailRecipient); }

void Preferences::setNotifyEmailRecipient(const QString& val) { set(&Data::notifyEmailRecipient, val); }

QString Preferences::notifyEmailSender() const { return get(&Data::notifyEmailSender); }

void Preferences::setNotifyEmailSender(const QString& val) { set(&Data::notifyEmailSender, val); }

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
    // Defaults to true, so a missing key (older daemon) must NOT read back as false.
    m_data->separateIPv6Queue = p.value(QStringLiteral("separateIPv6Queue")).toBool(true);

    // Server
    m_data->safeServerConnect       = p.value(QStringLiteral("safeServerConnect")).toBool();
    m_data->autoConnectStaticOnly   = p.value(QStringLiteral("autoConnectStaticOnly")).toBool();
    m_data->useServerPriorities     = p.value(QStringLiteral("useServerPriorities")).toBool();
    m_data->addServersFromServer    = p.value(QStringLiteral("addServersFromServer")).toBool();
    m_data->useUserSortedServerList = p.value(QStringLiteral("useUserSortedServerList")).toBool();
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
    // Defaults to true: an older peer that omits the key must not silently disable SLS.
    m_data->useSaveLoadSources            = p.value(QStringLiteral("useSaveLoadSources")).toBool(true);
    m_data->rememberUploadQueue           = p.value(QStringLiteral("rememberUploadQueue")).toBool(true);
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
    m_data->cryptLayerRequiredStrict  = p.value(QStringLiteral("cryptLayerRequiredStrict")).toBool();
    m_data->useSecureIdent            = p.value(QStringLiteral("useSecureIdent")).toBool();
    m_data->enableSearchResultFilter  = p.value(QStringLiteral("enableSearchResultFilter")).toBool();
    m_data->warnUntrustedFiles        = p.value(QStringLiteral("warnUntrustedFiles")).toBool();
    m_data->useSafeKad                = p.value(QStringLiteral("useSafeKad")).toBool();
    m_data->useFastKad                = p.value(QStringLiteral("useFastKad")).toBool();
    m_data->ipFilterUpdateUrl         = p.value(QStringLiteral("ipFilterUpdateUrl")).toString();
    m_data->appToken                  = p.value(QStringLiteral("appToken")).toString();
    m_data->usenetEnabled             = p.value(QStringLiteral("usenetEnabled")).toBool();
    m_data->usenetRetryIntervalSeconds =
        int(p.value(QStringLiteral("usenetRetryIntervalSeconds")).toInteger(60));

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
    m_data->skipFirewalledChecksInLanMode = p.value(QStringLiteral("skipFirewalledChecksInLanMode")).toBool();
    m_data->checkDiskspace              = p.value(QStringLiteral("checkDiskspace")).toBool();
    m_data->minFreeDiskSpace            = static_cast<uint64>(p.value(QStringLiteral("minFreeDiskSpace")).toInteger());
    m_data->logToDiskCore               = p.value(QStringLiteral("logToDiskCore")).toBool();
    m_data->logToDiskGui                = p.value(QStringLiteral("logToDiskGui")).toBool();
    m_data->verbose                     = p.value(QStringLiteral("verbose")).toBool();
    m_data->logPublicIP                 = p.value(QStringLiteral("logPublicIP")).toBool();
    m_data->serverVerboseLog            = p.value(QStringLiteral("serverVerboseLog")).toBool();
    m_data->closeUPnPOnExit             = p.value(QStringLiteral("closeUPnPOnExit")).toBool();
    m_data->fileBufferSize              = static_cast<uint32>(p.value(QStringLiteral("fileBufferSize")).toInteger());
    m_data->useCreditSystem             = p.value(QStringLiteral("useCreditSystem")).toBool();
    m_data->a4afSaveCpu                 = p.value(QStringLiteral("a4afSaveCpu")).toBool();
    m_data->autoArchivePreviewStart     = p.value(QStringLiteral("autoArchivePreviewStart")).toBool();
    m_data->ed2kHostname                = p.value(QStringLiteral("ed2kHostname")).toString();
    m_data->ed2kLinkAdvertiseIPv6       = p.value(QStringLiteral("ed2kLinkAdvertiseIPv6")).toBool();
    m_data->showExtControls             = p.value(QStringLiteral("showExtControls")).toBool();
    m_data->commitFiles                 = static_cast<int>(p.value(QStringLiteral("commitFiles")).toInteger());
    m_data->extractMetaData             = static_cast<int>(p.value(QStringLiteral("extractMetaData")).toInteger());
    m_data->logLevel                    = static_cast<int>(p.value(QStringLiteral("logLevel")).toInteger());
    m_data->logSourceExchange           = p.value(QStringLiteral("logSourceExchange")).toBool();
    m_data->logBannedClients            = p.value(QStringLiteral("logBannedClients")).toBool();
    m_data->logRatingDescReceived       = p.value(QStringLiteral("logRatingDescReceived")).toBool();
    m_data->logSecureIdent              = p.value(QStringLiteral("logSecureIdent")).toBool();
    m_data->logFilteredIPs              = p.value(QStringLiteral("logFilteredIPs")).toBool();
    m_data->logFileSaving               = p.value(QStringLiteral("logFileSaving")).toBool();
    m_data->logA4AF                     = p.value(QStringLiteral("logA4AF")).toBool();
    m_data->logUlDlEvents               = p.value(QStringLiteral("logUlDlEvents")).toBool();
    m_data->logRawSocketPackets         = p.value(QStringLiteral("logRawSocketPackets")).toBool();
    m_data->logWebServer                = p.value(QStringLiteral("logWebServer")).toBool();
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
    const SubjectRevisionBump bumpOnExit;

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
        m_data->startVersion = kCurrentPrefsVersion;  // fresh config, nothing to migrate
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
            m_data->skipFirewalledChecksInLanMode = g["skipFirewalledChecksInLanMode"].as<bool>(m_data->skipFirewalledChecksInLanMode);
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
            m_data->bringToFrontOnLinkClick = g["bringToFrontOnLinkClick"].as<bool>(m_data->bringToFrontOnLinkClick);
            if (g["appToken"])
                m_data->appToken = QString::fromStdString(g["appToken"].as<std::string>());

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
            m_data->useUserSortedServerList = s["useUserSortedServerList"].as<bool>(m_data->useUserSortedServerList);
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
            m_data->publicIPv6Override = QString::fromStdString(n["publicIPv6Override"].as<std::string>(m_data->publicIPv6Override.toStdString()));
            m_data->ipv6PublicPeerConfirmThreshold = static_cast<uint32>(n["ipv6PublicPeerConfirmThreshold"].as<int>(static_cast<int>(m_data->ipv6PublicPeerConfirmThreshold)));
            m_data->ipv6PublicPeerConfirmWindowSecs = static_cast<uint32>(n["ipv6PublicPeerConfirmWindowSecs"].as<int>(static_cast<int>(m_data->ipv6PublicPeerConfirmWindowSecs)));
            m_data->ipv4PublicServerConfirmThreshold = static_cast<uint32>(n["ipv4PublicServerConfirmThreshold"].as<int>(static_cast<int>(m_data->ipv4PublicServerConfirmThreshold)));
            m_data->ipv4PublicServerConfirmWindowSecs = static_cast<uint32>(n["ipv4PublicServerConfirmWindowSecs"].as<int>(static_cast<int>(m_data->ipv4PublicServerConfirmWindowSecs)));
            m_data->separateIPv6Queue = n["separateIPv6Queue"].as<bool>(m_data->separateIPv6Queue);
            m_data->serverPreferIPv6 = n["serverPreferIPv6"].as<bool>(m_data->serverPreferIPv6);
            m_data->maxConsPerFive = static_cast<uint16>(n["maxConsPerFive"].as<int>(m_data->maxConsPerFive));
            m_data->showOverhead = n["showOverhead"].as<bool>(m_data->showOverhead);
            m_data->networkED2K = n["networkED2K"].as<bool>(m_data->networkED2K);
            // "publicIP" is deliberately not read: it now lives on AppContext as
            // session state. A value left over from an older preferences.yml is
            // ignored rather than resurrecting keys bound to a stale address.
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
            m_data->cryptLayerRequiredStrict = e["cryptLayerRequiredStrict"].as<bool>(m_data->cryptLayerRequiredStrict);
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

        // Download categories. After `directories`, because sanitizing a
        // category path judges it against the incoming, temp and config dirs
        // that block just loaded. MFC's LoadCats runs last for the same reason
        // (srchybrid/Preferences.cpp:2401).
        if (auto c = root["categories"]; c && c.IsSequence()) {
            QList<DownloadCategory> list;
            for (const auto& node : c) {
                if (!node.IsMap())
                    continue;

                DownloadCategory entry;
                entry.title = QString::fromStdString(node["title"].as<std::string>(""));
                entry.incomingPath = QString::fromStdString(node["incoming"].as<std::string>(""));
                entry.comment = QString::fromStdString(node["comment"].as<std::string>(""));
                entry.autocat = QString::fromStdString(node["autocat"].as<std::string>(""));
                entry.regexp = QString::fromStdString(node["regexp"].as<std::string>(""));
                entry.color = node["color"].as<quint32>(kCategoryColorAuto);
                entry.prio = static_cast<quint8>(node["prio"].as<int>(entry.prio));
                entry.filter = node["filter"].as<int>(0);
                entry.filterNeg = node["filterNeg"].as<bool>(false);
                entry.care4all = node["care4all"].as<bool>(false);
                entry.downloadInAlphabeticalOrder = node["alphabetical"].as<bool>(false);
                entry.autocatIsRegexp = node["autocatRegexp"].as<bool>(false);

                list.append(entry);
            }

            m_data->categories = sanitizeCategories(list, resolveConfigDir(m_data->configDir),
                                                    m_data->incomingDir, m_data->tempDirs);
        }

        // UPnP
        if (auto u = root["upnp"]) {
            m_data->enableUPnP = u["enableUPnP"].as<bool>(m_data->enableUPnP);
            m_data->closeUPnPOnExit = u["closeUPnPOnExit"].as<bool>(m_data->closeUPnPOnExit);
            m_data->portMapProtocols = u["portMapProtocols"].as<uint32>(m_data->portMapProtocols);
            m_data->portMapLeaseSecs = u["portMapLeaseSecs"].as<uint32>(m_data->portMapLeaseSecs);
            m_data->portMapIPv6 = u["portMapIPv6"].as<bool>(m_data->portMapIPv6);
            m_data->portMapMethod = u["portMapMethod"].as<int>(m_data->portMapMethod);
            m_data->portMapSecret = QString::fromStdString(
                u["portMapSecret"].as<std::string>(m_data->portMapSecret.toStdString()));
        }

        // Logging
        if (auto l = root["logging"]) {
            m_data->logToDiskCore = l["logToDiskCore"].as<bool>(m_data->logToDiskCore);
            m_data->logToDiskGui = l["logToDiskGui"].as<bool>(m_data->logToDiskGui);
            m_data->maxLogFileSize = l["maxLogFileSize"].as<uint32>(m_data->maxLogFileSize);
            m_data->verbose = l["verbose"].as<bool>(m_data->verbose);
            m_data->logPublicIP = l["logPublicIP"].as<bool>(m_data->logPublicIP);
            m_data->kadVerboseLog = l["kadVerboseLog"].as<bool>(m_data->kadVerboseLog);
            m_data->serverVerboseLog = l["serverVerboseLog"].as<bool>(m_data->serverVerboseLog);
            m_data->maxLogLines = l["maxLogLines"].as<uint32>(m_data->maxLogLines);
            m_data->logLevel = l["logLevel"].as<int>(m_data->logLevel);
            m_data->logSourceExchange = l["logSourceExchange"].as<bool>(m_data->logSourceExchange);
            m_data->logBannedClients = l["logBannedClients"].as<bool>(m_data->logBannedClients);
            m_data->logRatingDescReceived = l["logRatingDescReceived"].as<bool>(m_data->logRatingDescReceived);
            m_data->logSecureIdent = l["logSecureIdent"].as<bool>(m_data->logSecureIdent);
            m_data->logFilteredIPs = l["logFilteredIPs"].as<bool>(m_data->logFilteredIPs);
            m_data->logFileSaving = l["logFileSaving"].as<bool>(m_data->logFileSaving);
            m_data->logA4AF = l["logA4AF"].as<bool>(m_data->logA4AF);
            m_data->logUlDlEvents = l["logUlDlEvents"].as<bool>(m_data->logUlDlEvents);
            m_data->logRawSocketPackets = l["logRawSocketPackets"].as<bool>(m_data->logRawSocketPackets);
            m_data->logWebServer = l["logWebServer"].as<bool>(m_data->logWebServer);
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
            m_data->useSaveLoadSources = t["useSaveLoadSources"].as<bool>(m_data->useSaveLoadSources);
            m_data->rememberUploadQueue = t["rememberUploadQueue"].as<bool>(m_data->rememberUploadQueue);
            m_data->useCreditSystem = t["useCreditSystem"].as<bool>(m_data->useCreditSystem);
            m_data->a4afSaveCpu = t["a4afSaveCpu"].as<bool>(m_data->a4afSaveCpu);
            m_data->autoArchivePreviewStart = t["autoArchivePreviewStart"].as<bool>(m_data->autoArchivePreviewStart);
            m_data->ed2kHostname = QString::fromStdString(t["ed2kHostname"].as<std::string>(m_data->ed2kHostname.toStdString()));
            m_data->ed2kLinkAdvertiseIPv6 = t["ed2kLinkAdvertiseIPv6"].as<bool>(m_data->ed2kLinkAdvertiseIPv6);
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
            m_data->statsSaveInterval = st["statsSaveInterval"].as<uint32>(m_data->statsSaveInterval);
            m_data->statsLastReset = st["statsLastReset"].as<uint64>(m_data->statsLastReset);

            // Cumulative transfer totals
            m_data->cumTotalUploaded = st["cumTotalUploaded"].as<uint64>(m_data->cumTotalUploaded);
            m_data->cumTotalDownloaded = st["cumTotalDownloaded"].as<uint64>(m_data->cumTotalDownloaded);
            m_data->cumTotalUploadedToFriend = st["cumTotalUploadedToFriend"].as<uint64>(m_data->cumTotalUploadedToFriend);

            // Cumulative upload sessions
            m_data->cumUpSuccessfulSessions = st["cumUpSuccessfulSessions"].as<uint32>(m_data->cumUpSuccessfulSessions);
            m_data->cumUpFailedSessions = st["cumUpFailedSessions"].as<uint32>(m_data->cumUpFailedSessions);
            m_data->cumUpAvgTime = st["cumUpAvgTime"].as<uint32>(m_data->cumUpAvgTime);

            // Cumulative download sessions
            m_data->cumDownSuccessfulSessions = st["cumDownSuccessfulSessions"].as<uint32>(m_data->cumDownSuccessfulSessions);
            m_data->cumDownFailedSessions = st["cumDownFailedSessions"].as<uint32>(m_data->cumDownFailedSessions);
            m_data->cumDownCompletedFiles = st["cumDownCompletedFiles"].as<uint32>(m_data->cumDownCompletedFiles);
            m_data->cumDownAvgTime = st["cumDownAvgTime"].as<uint32>(m_data->cumDownAvgTime);

            // Cumulative overhead — upload
            m_data->cumUpOverheadTotal = st["cumUpOverheadTotal"].as<uint64>(m_data->cumUpOverheadTotal);
            m_data->cumUpOverheadTotalPackets = st["cumUpOverheadTotalPackets"].as<uint64>(m_data->cumUpOverheadTotalPackets);
            m_data->cumUpOverheadFileReq = st["cumUpOverheadFileReq"].as<uint64>(m_data->cumUpOverheadFileReq);
            m_data->cumUpOverheadFileReqPackets = st["cumUpOverheadFileReqPackets"].as<uint64>(m_data->cumUpOverheadFileReqPackets);
            m_data->cumUpOverheadSrcExch = st["cumUpOverheadSrcExch"].as<uint64>(m_data->cumUpOverheadSrcExch);
            m_data->cumUpOverheadSrcExchPackets = st["cumUpOverheadSrcExchPackets"].as<uint64>(m_data->cumUpOverheadSrcExchPackets);
            m_data->cumUpOverheadServer = st["cumUpOverheadServer"].as<uint64>(m_data->cumUpOverheadServer);
            m_data->cumUpOverheadServerPackets = st["cumUpOverheadServerPackets"].as<uint64>(m_data->cumUpOverheadServerPackets);
            m_data->cumUpOverheadKad = st["cumUpOverheadKad"].as<uint64>(m_data->cumUpOverheadKad);
            m_data->cumUpOverheadKadPackets = st["cumUpOverheadKadPackets"].as<uint64>(m_data->cumUpOverheadKadPackets);

            // Cumulative overhead — download
            m_data->cumDownOverheadTotal = st["cumDownOverheadTotal"].as<uint64>(m_data->cumDownOverheadTotal);
            m_data->cumDownOverheadTotalPackets = st["cumDownOverheadTotalPackets"].as<uint64>(m_data->cumDownOverheadTotalPackets);
            m_data->cumDownOverheadFileReq = st["cumDownOverheadFileReq"].as<uint64>(m_data->cumDownOverheadFileReq);
            m_data->cumDownOverheadFileReqPackets = st["cumDownOverheadFileReqPackets"].as<uint64>(m_data->cumDownOverheadFileReqPackets);
            m_data->cumDownOverheadSrcExch = st["cumDownOverheadSrcExch"].as<uint64>(m_data->cumDownOverheadSrcExch);
            m_data->cumDownOverheadSrcExchPackets = st["cumDownOverheadSrcExchPackets"].as<uint64>(m_data->cumDownOverheadSrcExchPackets);
            m_data->cumDownOverheadServer = st["cumDownOverheadServer"].as<uint64>(m_data->cumDownOverheadServer);
            m_data->cumDownOverheadServerPackets = st["cumDownOverheadServerPackets"].as<uint64>(m_data->cumDownOverheadServerPackets);
            m_data->cumDownOverheadKad = st["cumDownOverheadKad"].as<uint64>(m_data->cumDownOverheadKad);
            m_data->cumDownOverheadKadPackets = st["cumDownOverheadKadPackets"].as<uint64>(m_data->cumDownOverheadKadPackets);

            // Cumulative connection stats
            m_data->cumConnPeak = st["cumConnPeak"].as<uint32>(m_data->cumConnPeak);
            m_data->cumConnMaxLimitReached = st["cumConnMaxLimitReached"].as<uint32>(m_data->cumConnMaxLimitReached);
            m_data->cumConnReconnects = st["cumConnReconnects"].as<uint32>(m_data->cumConnReconnects);

            // Cumulative times
            m_data->cumRunTime = st["cumRunTime"].as<uint64>(m_data->cumRunTime);
            m_data->cumTransferTime = st["cumTransferTime"].as<uint64>(m_data->cumTransferTime);
            m_data->cumUploadTime = st["cumUploadTime"].as<uint64>(m_data->cumUploadTime);
            m_data->cumDownloadTime = st["cumDownloadTime"].as<uint64>(m_data->cumDownloadTime);
            m_data->cumServerDuration = st["cumServerDuration"].as<uint64>(m_data->cumServerDuration);

            // Cumulative quality stats
            m_data->cumCompressionGain = st["cumCompressionGain"].as<uint64>(m_data->cumCompressionGain);
            m_data->cumCorruptionLoss = st["cumCorruptionLoss"].as<uint64>(m_data->cumCorruptionLoss);
            m_data->cumIchPartsSaved = st["cumIchPartsSaved"].as<uint32>(m_data->cumIchPartsSaved);

            // Per-client cumulative upload
            m_data->cumUpEmule = st["cumUpEmule"].as<uint64>(m_data->cumUpEmule);
            m_data->cumUpEDHybrid = st["cumUpEDHybrid"].as<uint64>(m_data->cumUpEDHybrid);
            m_data->cumUpEDonkey = st["cumUpEDonkey"].as<uint64>(m_data->cumUpEDonkey);
            m_data->cumUpAMule = st["cumUpAMule"].as<uint64>(m_data->cumUpAMule);
            m_data->cumUpMLdonkey = st["cumUpMLdonkey"].as<uint64>(m_data->cumUpMLdonkey);
            m_data->cumUpShareaza = st["cumUpShareaza"].as<uint64>(m_data->cumUpShareaza);
            m_data->cumUpEMCompat = st["cumUpEMCompat"].as<uint64>(m_data->cumUpEMCompat);

            // Per-client cumulative download
            m_data->cumDownEmule = st["cumDownEmule"].as<uint64>(m_data->cumDownEmule);
            m_data->cumDownEDHybrid = st["cumDownEDHybrid"].as<uint64>(m_data->cumDownEDHybrid);
            m_data->cumDownEDonkey = st["cumDownEDonkey"].as<uint64>(m_data->cumDownEDonkey);
            m_data->cumDownAMule = st["cumDownAMule"].as<uint64>(m_data->cumDownAMule);
            m_data->cumDownMLdonkey = st["cumDownMLdonkey"].as<uint64>(m_data->cumDownMLdonkey);
            m_data->cumDownShareaza = st["cumDownShareaza"].as<uint64>(m_data->cumDownShareaza);
            m_data->cumDownEMCompat = st["cumDownEMCompat"].as<uint64>(m_data->cumDownEMCompat);
            m_data->cumDownURL = st["cumDownURL"].as<uint64>(m_data->cumDownURL);

            // Per-port cumulative
            m_data->cumUpPort4662 = st["cumUpPort4662"].as<uint64>(m_data->cumUpPort4662);
            m_data->cumUpPortOther = st["cumUpPortOther"].as<uint64>(m_data->cumUpPortOther);
            m_data->cumDownPort4662 = st["cumDownPort4662"].as<uint64>(m_data->cumDownPort4662);
            m_data->cumDownPortOther = st["cumDownPortOther"].as<uint64>(m_data->cumDownPortOther);

            // Per-source cumulative
            m_data->cumUpFromFile = st["cumUpFromFile"].as<uint64>(m_data->cumUpFromFile);
            m_data->cumUpFromPartfile = st["cumUpFromPartfile"].as<uint64>(m_data->cumUpFromPartfile);

            // HTTP Cache / Usenet / indexer cumulative blocks
            m_data->cumHttpCache = readStatValue(st["cumHttpCache"], m_data->cumHttpCache);
            m_data->cumUsenet = readStatValue(st["cumUsenet"], m_data->cumUsenet);
            m_data->cumIndexer = readStatValue(st["cumIndexer"], m_data->cumIndexer);

            // Records
            m_data->recMaxWorkingServers = st["recMaxWorkingServers"].as<uint32>(m_data->recMaxWorkingServers);
            m_data->recMaxUsersOnline = st["recMaxUsersOnline"].as<uint32>(m_data->recMaxUsersOnline);
            m_data->recMaxFilesAvail = st["recMaxFilesAvail"].as<uint32>(m_data->recMaxFilesAvail);
            m_data->recMaxSharedFiles = st["recMaxSharedFiles"].as<uint64>(m_data->recMaxSharedFiles);
            m_data->recMaxSharedSize = st["recMaxSharedSize"].as<uint64>(m_data->recMaxSharedSize);
            m_data->recMaxAvgFileSize = st["recMaxAvgFileSize"].as<uint64>(m_data->recMaxAvgFileSize);
            m_data->recMaxLargestFile = st["recMaxLargestFile"].as<uint64>(m_data->recMaxLargestFile);
        }

        // Security
        if (auto sec = root["security"]) {
            m_data->ipFilterLevel = sec["ipFilterLevel"].as<uint32>(m_data->ipFilterLevel);
            m_data->useSecureIdent = sec["useSecureIdent"].as<bool>(m_data->useSecureIdent);
            m_data->viewSharedFilesAccess = sec["viewSharedFilesAccess"].as<int>(m_data->viewSharedFilesAccess);
            m_data->warnUntrustedFiles = sec["warnUntrustedFiles"].as<bool>(m_data->warnUntrustedFiles);
            m_data->useSafeKad = sec["useSafeKad"].as<bool>(m_data->useSafeKad);
            m_data->useFastKad = sec["useFastKad"].as<bool>(m_data->useFastKad);
            if (sec["ipFilterUpdateUrl"])
                m_data->ipFilterUpdateUrl = QString::fromStdString(sec["ipFilterUpdateUrl"].as<std::string>());
            if (sec["bugReportApiKey"])
                m_data->bugReportApiKey = QString::fromStdString(sec["bugReportApiKey"].as<std::string>());
            if (sec["bugReportDomain"])
                m_data->bugReportDomain = QString::fromStdString(sec["bugReportDomain"].as<std::string>());
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
            m_data->kadFileNameExpiryDays = k["fileNameExpiryDays"].as<int>(m_data->kadFileNameExpiryDays);
            m_data->kadFileNameMaxCount = k["fileNameMaxCount"].as<int>(m_data->kadFileNameMaxCount);
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
            m_data->showSpeedGraph = d["showSpeedGraph"].as<bool>(m_data->showSpeedGraph);
            m_data->speedGraphTimeRangeMin = d["speedGraphTimeRangeMin"].as<uint32_t>(m_data->speedGraphTimeRangeMin);
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
                m_data->notifyEmailSmtpPassword =
                    aesDecryptFromBase64(enc, m_data->notifyEmailEncKey);
            }
        }

        // HTTP Cache — encrypted chunk offload. No Options page yet, so this
        // section is hand-edited: every field falls back to its compiled-in
        // default and a partial block is valid.
        //
        // Must come after `notifications`, which is where notifyEmailEncKey — the
        // key the API key is encrypted under — gets loaded.
        if (auto hc = root["httpCache"]) {
            m_data->httpCacheEnabled = hc["enabled"].as<bool>(m_data->httpCacheEnabled);
            m_data->httpCacheAllowDownload =
                hc["allowDownload"].as<bool>(m_data->httpCacheAllowDownload);
            m_data->httpCacheAllowUpload =
                hc["allowUpload"].as<bool>(m_data->httpCacheAllowUpload);
            m_data->httpCacheAllowRelay =
                hc["allowRelay"].as<bool>(m_data->httpCacheAllowRelay);
            m_data->httpCachePublishToKad =
                hc["publishToKad"].as<bool>(m_data->httpCachePublishToKad);
            m_data->httpCacheFetchFromKad =
                hc["fetchFromKad"].as<bool>(m_data->httpCacheFetchFromKad);

            // The servers we may publish to, in rotation order. Each credential is
            // stored like the SMTP password: AES-encrypted under the notification
            // key, so preferences.yml never holds a usable upload credential in the
            // clear.
            if (const auto servers = hc["servers"]; servers && servers.IsSequence()) {
                QList<HttpCacheServerConfig> list;

                for (const auto& node : servers) {
                    if (!node.IsMap())
                        continue;

                    HttpCacheServerConfig entry;
                    entry.baseUrl = normalizeHttpCacheBaseUrl(
                        QString::fromStdString(node["baseUrl"].as<std::string>("")));
                    if (entry.baseUrl.isEmpty())
                        continue;

                    entry.name = QString::fromStdString(node["name"].as<std::string>("")).trimmed();
                    entry.keyId =
                        QString::fromStdString(node["keyId"].as<std::string>("")).trimmed();
                    entry.enabled = node["enabled"].as<bool>(true);

                    if (node["apiKeyEnc"] && !m_data->notifyEmailEncKey.isEmpty()) {
                        const auto enc =
                            QString::fromStdString(node["apiKeyEnc"].as<std::string>(""));
                        entry.apiKey = aesDecryptFromBase64(enc, m_data->notifyEmailEncKey);
                    } else if (node["apiKey"]) {
                        // Plaintext escape hatch for first setup and hand-editing;
                        // rewritten as apiKeyEnc on the next save.
                        entry.apiKey =
                            QString::fromStdString(node["apiKey"].as<std::string>("")).trimmed();
                    }

                    list.append(entry);
                }

                m_data->httpCacheServers = sanitizeHttpCacheServers(list);
            }

            m_data->httpCacheMinClients =
                hc["minClients"].as<uint32>(m_data->httpCacheMinClients);
            m_data->httpCacheChunkTtlSeconds =
                hc["chunkTtlSeconds"].as<uint32>(m_data->httpCacheChunkTtlSeconds);
            m_data->httpCacheMaxPublishBytesPerDay =
                hc["maxPublishBytesPerDay"].as<uint64>(m_data->httpCacheMaxPublishBytesPerDay);
            m_data->httpCachePublishRateKBs =
                hc["publishRateKBs"].as<uint32>(m_data->httpCachePublishRateKBs);
            m_data->httpCacheMaxConcurrentPublishes =
                hc["maxConcurrentPublishes"].as<uint32>(m_data->httpCacheMaxConcurrentPublishes);
            m_data->httpCacheMaxConcurrentFetches =
                hc["maxConcurrentFetches"].as<uint32>(m_data->httpCacheMaxConcurrentFetches);
        }

        // Usenet. Like `httpCache`, this must come *after* `notifications`:
        // that is where notifyEmailEncKey is read, and it is the key every
        // provider password is encrypted under. Load this block earlier and
        // every password silently comes back empty.
        if (auto un = root["usenet"]) {
            m_data->usenetEnabled = un["enabled"].as<bool>(m_data->usenetEnabled);
            m_data->usenetRetryIntervalSeconds =
                un["retryIntervalSeconds"].as<int>(m_data->usenetRetryIntervalSeconds);
            m_data->usenetPar2Repair =
                un["par2Repair"].as<bool>(m_data->usenetPar2Repair);
            m_data->usenetPar2RenameFiles =
                un["par2RenameFiles"].as<bool>(m_data->usenetPar2RenameFiles);
            m_data->usenetUnpack =
                un["unpack"].as<bool>(m_data->usenetUnpack);
            m_data->usenetCleanupAfterUnpack =
                un["cleanupAfterUnpack"].as<bool>(m_data->usenetCleanupAfterUnpack);
            m_data->usenetDirectUnpack =
                un["directUnpack"].as<bool>(m_data->usenetDirectUnpack);
            m_data->usenetEncryptedPreview =
                un["encryptedPreview"].as<bool>(m_data->usenetEncryptedPreview);
            m_data->usenetExternalUnpacker = QString::fromStdString(
                un["externalUnpacker"].as<std::string>(
                    m_data->usenetExternalUnpacker.toStdString()));
            m_data->usenetDownloadSharePercent = std::clamp(
                un["downloadSharePercent"].as<int>(m_data->usenetDownloadSharePercent),
                1, 99);
            m_data->usenetHealthCheck = std::clamp(
                un["healthCheck"].as<int>(m_data->usenetHealthCheck), 0, 2);
            m_data->usenetHealthMinPercent = std::clamp(
                un["healthMinPercent"].as<int>(m_data->usenetHealthMinPercent), 0, 100);
            m_data->usenetAutoAddPaused =
                un["autoAddPaused"].as<bool>(m_data->usenetAutoAddPaused);
            if (const auto pats = un["subjectPatterns"]; pats && pats.IsSequence()) {
                QList<UsenetSubjectPattern> list;
                for (const auto& node : pats) {
                    if (!node.IsMap())
                        continue;

                    UsenetSubjectPattern p;
                    p.name = QString::fromStdString(node["name"].as<std::string>(""));
                    p.pattern = QString::fromStdString(node["pattern"].as<std::string>(""));

                    // A rule with no usable role is dropped rather than
                    // defaulted: guessing would quietly fill the wrong field.
                    // A bad *regex*, by contrast, is kept — see
                    // sanitizeSubjectPatterns().
                    if (!usenetSubjectRoleFromName(
                            QString::fromStdString(node["role"].as<std::string>("")), p.role)) {
                        continue;
                    }

                    p.pick = usenetSubjectPickFromName(
                        QString::fromStdString(node["pick"].as<std::string>("first")));
                    p.caseInsensitive = node["caseInsensitive"].as<bool>(false);
                    p.enabled = node["enabled"].as<bool>(true);
                    list.append(std::move(p));
                }
                m_data->usenetSubjectPatterns = sanitizeSubjectPatterns(list);
            }

            if (un["watchDir"]) {
                // Sanitised here as well as in the setter: a hand-edited file is
                // exactly as capable of naming the temp tree as an IPC client is.
                m_data->usenetWatchDir = sanitizeWatchDir(
                    QString::fromStdString(un["watchDir"].as<std::string>("")),
                    resolveConfigDir(m_data->configDir), m_data->incomingDir,
                    m_data->tempDirs);
            }

            if (const auto servers = un["servers"]; servers && servers.IsSequence()) {
                QList<NewsServer> list;

                for (const auto& node : servers) {
                    if (!node.IsMap())
                        continue;

                    NewsServer entry;
                    entry.host = QString::fromStdString(node["host"].as<std::string>("")).trimmed();
                    if (entry.host.isEmpty())
                        continue;

                    entry.name = QString::fromStdString(node["name"].as<std::string>("")).trimmed();
                    entry.port = static_cast<quint16>(
                        node["port"].as<int>(kDefaultNntpTlsPort));
                    entry.tlsMode = static_cast<NntpTlsMode>(
                        node["tls"].as<int>(static_cast<int>(NntpTlsMode::Implicit)));
                    entry.user = QString::fromStdString(node["user"].as<std::string>(""));
                    entry.level = node["level"].as<int>(0);
                    entry.group = node["group"].as<int>(0);
                    entry.optional = node["optional"].as<bool>(false);
                    entry.retention = node["retention"].as<int>(0);
                    entry.joinGroup = node["joinGroup"].as<bool>(false);
                    entry.accountId =
                        QString::fromStdString(node["accountId"].as<std::string>("")).trimmed();
                    entry.quotaKind = static_cast<NntpQuotaKind>(
                        node["quotaKind"].as<int>(static_cast<int>(NntpQuotaKind::None)));
                    entry.quotaBytes = node["quotaBytes"].as<long long>(0);
                    entry.quotaResetDay = std::clamp(node["quotaResetDay"].as<int>(1), 1, 31);
                    entry.quotaFallThrough = node["quotaFallThrough"].as<bool>(false);

                    // A row written before quotas existed has no id. Mint one
                    // now rather than at first use: the usage meter is keyed by
                    // it, and an id minted lazily would be a fresh meter every
                    // restart until something happened to save the file.
                    if (entry.accountId.isEmpty())
                        entry.accountId = QUuid::createUuid().toString(QUuid::WithoutBraces);
                    entry.maxConnections = node["maxConnections"].as<int>(kDefaultMaxConnections);
                    entry.certVerification = static_cast<NntpCertVerification>(
                        node["certVerification"].as<int>(
                            static_cast<int>(NntpCertVerification::Strict)));
                    entry.enabled = node["enabled"].as<bool>(true);

                    if (node["passEnc"]) {
                        const auto enc =
                            QString::fromStdString(node["passEnc"].as<std::string>(""));
                        if (m_data->notifyEmailEncKey.isEmpty()) {
                            // The blob is here but the key is not. Say so: the
                            // user would otherwise only see their provider
                            // answer "authentication failed" and have no reason
                            // to suspect the config file.
                            logWarning(QStringLiteral(
                                "Usenet: %1 has a stored password but no encryption key "
                                "in preferences.yml — re-enter the password")
                                           .arg(entry.displayName()));
                        } else {
                            entry.pass = aesDecryptFromBase64(enc, m_data->notifyEmailEncKey);
                            if (entry.pass.isEmpty() && !enc.isEmpty()) {
                                logWarning(QStringLiteral(
                                    "Usenet: could not decrypt the stored password for %1 "
                                    "— re-enter it")
                                               .arg(entry.displayName()));
                            }
                        }
                    } else if (node["pass"]) {
                        // Plaintext escape hatch for first setup and hand-editing;
                        // rewritten as passEnc on the next save.
                        entry.pass = QString::fromStdString(node["pass"].as<std::string>(""));
                    }

                    list.append(entry);
                    if (list.size() >= kMaxUsenetServers)
                        break;
                }

                m_data->usenetServers = list;
            }
        }

        // Indexers. After `notifications` for the same reason `usenet` is: the
        // API keys are encrypted under notifyEmailEncKey, which is only read
        // there. A top-level block rather than a key under `usenet`, because the
        // client is shared with BitTorrent.
        if (auto ix = root["indexers"]) {
            m_data->indexerResultLimit = std::clamp(
                ix["resultLimit"].as<int>(m_data->indexerResultLimit), 10, 1000);
            m_data->indexerMaxPages = std::clamp(
                ix["maxPages"].as<int>(m_data->indexerMaxPages), 1, 20);
            m_data->indexerTimeoutSeconds = std::clamp(
                ix["timeoutSeconds"].as<int>(m_data->indexerTimeoutSeconds), 5, 300);
            m_data->indexerCapsRefreshDays = std::clamp(
                ix["capsRefreshDays"].as<int>(m_data->indexerCapsRefreshDays), 1, 365);

            if (const auto accounts = ix["accounts"]; accounts && accounts.IsSequence()) {
                QList<IndexerConfig> list;

                for (const auto& node : accounts) {
                    if (!node.IsMap())
                        continue;

                    IndexerConfig entry;
                    entry.name = QString::fromStdString(node["name"].as<std::string>("")).trimmed();
                    entry.url = QString::fromStdString(node["url"].as<std::string>("")).trimmed();
                    if (entry.name.isEmpty() || entry.url.isEmpty())
                        continue;

                    entry.kind = indexerKindFromString(
                        QString::fromStdString(node["kind"].as<std::string>("newznab")));
                    entry.enabled = node["enabled"].as<bool>(true);
                    entry.timeoutMs = node["timeoutMs"].as<int>(
                        m_data->indexerTimeoutSeconds * 1000);

                    if (node["apiKeyEnc"]) {
                        const auto enc =
                            QString::fromStdString(node["apiKeyEnc"].as<std::string>(""));
                        if (m_data->notifyEmailEncKey.isEmpty()) {
                            // The blob is here and the key is not. Say so: the
                            // indexer would otherwise just answer "Incorrect
                            // user credentials" and nothing would point at the
                            // config file.
                            logWarning(QStringLiteral(
                                "Indexers: %1 has a stored API key but no encryption key "
                                "in preferences.yml — re-enter the key")
                                           .arg(entry.displayName()));
                        } else {
                            entry.apiKey = aesDecryptFromBase64(enc, m_data->notifyEmailEncKey);
                            if (entry.apiKey.isEmpty() && !enc.isEmpty()) {
                                logWarning(QStringLiteral(
                                    "Indexers: could not decrypt the stored API key for %1 "
                                    "— re-enter it")
                                               .arg(entry.displayName()));
                            }
                        }
                    } else if (node["apiKey"]) {
                        // Plaintext escape hatch for first setup and hand-editing;
                        // rewritten as apiKeyEnc on the next save.
                        entry.apiKey =
                            QString::fromStdString(node["apiKey"].as<std::string>(""));
                    }

                    list.append(entry);
                    if (list.size() >= kMaxIndexers)
                        break;
                }

                m_data->indexers = list;
            }

            if (const auto feeds = ix["feeds"]; feeds && feeds.IsSequence()) {
                QList<IndexerFeed> list;

                for (const auto& node : feeds) {
                    if (!node.IsMap())
                        continue;

                    IndexerFeed entry;
                    entry.name = QString::fromStdString(node["name"].as<std::string>("")).trimmed();
                    if (entry.name.isEmpty())
                        continue;

                    entry.kind = indexerFeedKindFromString(
                        QString::fromStdString(node["kind"].as<std::string>("search")));
                    entry.enabled = node["enabled"].as<bool>(true);
                    entry.query = QString::fromStdString(node["query"].as<std::string>(""));
                    entry.accept = QString::fromStdString(node["accept"].as<std::string>(""));
                    entry.reject = QString::fromStdString(node["reject"].as<std::string>(""));
                    entry.minSize = node["minSize"].as<qint64>(0);
                    entry.maxSize = node["maxSize"].as<qint64>(0);
                    entry.maxAgeDays = node["maxAgeDays"].as<int>(0);
                    entry.intervalMinutes = node["intervalMinutes"].as<int>(30);
                    entry.grabExisting = node["grabExisting"].as<bool>(false);
                    // Not clamped against categoryCount() here: that would make
                    // a feed's destination depend on the order this function
                    // reads its blocks in. SetCategories is what keeps it right.
                    entry.downloadCategory = node["downloadCategory"].as<int>(0);

                    if (const auto cats = node["categories"]; cats && cats.IsSequence()) {
                        for (const auto& cat : cats)
                            entry.categories.append(cat.as<int>(0));
                    }
                    if (const auto names = node["indexers"]; names && names.IsSequence()) {
                        for (const auto& name : names) {
                            entry.indexers.append(
                                QString::fromStdString(name.as<std::string>("")));
                        }
                    }

                    if (node["urlEnc"]) {
                        // A feed URL is a credential: newznab's RSS endpoint
                        // takes the key as a query parameter, so the whole
                        // string is as sensitive as an apiKey and is stored the
                        // same way.
                        const auto enc = QString::fromStdString(node["urlEnc"].as<std::string>(""));
                        if (m_data->notifyEmailEncKey.isEmpty()) {
                            logWarning(QStringLiteral(
                                "Feeds: %1 has a stored URL but no encryption key in "
                                "preferences.yml — re-enter it")
                                           .arg(entry.name));
                        } else {
                            entry.url = aesDecryptFromBase64(enc, m_data->notifyEmailEncKey);
                            if (entry.url.isEmpty() && !enc.isEmpty()) {
                                logWarning(QStringLiteral(
                                    "Feeds: could not decrypt the stored URL for %1 "
                                    "— re-enter it")
                                               .arg(entry.name));
                            }
                        }
                    } else if (node["url"]) {
                        // Plaintext escape hatch, rewritten as urlEnc on save.
                        entry.url = QString::fromStdString(node["url"].as<std::string>(""));
                    }

                    list.append(entry);
                    if (list.size() >= kMaxFeeds)
                        break;
                }

                m_data->indexerFeeds = sanitizeFeeds(list);
            }
        }

        // UI State is now in its own uistate.yml (managed by UiState class)

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

    // One-time migrations keyed by startVersion.  Persist immediately: without the save
    // the counter never reaches disk and every migration re-runs on the next start.
    if (m_data->startVersion < kCurrentPrefsVersion) {
        if (m_data->startVersion < 1)
            resolveDefaultVideoPlayer();

        // v2: the shipped default used to be 100, which is also the level given to a list
        // entry with no level column.  Since the test is `level < filterLevel`, that made
        // the filter a no-op for most lists.  Only the old default is rewritten, so a
        // level deliberately set to anything else is left alone; a user who genuinely
        // chose 100 is bumped once, which nothing in the config lets us distinguish.
        if (m_data->startVersion < 2 && m_data->ipFilterLevel == 100)
            m_data->ipFilterLevel = 127;

        m_data->startVersion = kCurrentPrefsVersion;
        saveImpl(m_filePath);
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
    cfg.cryptLayerRequiredStrict = m_data->cryptLayerRequiredStrict;
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
    out << YAML::Key << "skipFirewalledChecksInLanMode" << YAML::Value << m_data->skipFirewalledChecksInLanMode;
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
    out << YAML::Key << "bringToFrontOnLinkClick" << YAML::Value << m_data->bringToFrontOnLinkClick;
    if (!m_data->appToken.isEmpty())
        out << YAML::Key << "appToken" << YAML::Value << m_data->appToken.toStdString();
    out << YAML::EndMap;

    // Server connection
    out << YAML::Key << "serverConnection" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "safeServerConnect" << YAML::Value << m_data->safeServerConnect;
    out << YAML::Key << "autoConnectStaticOnly" << YAML::Value << m_data->autoConnectStaticOnly;
    out << YAML::Key << "useServerPriorities" << YAML::Value << m_data->useServerPriorities;
    out << YAML::Key << "addServersFromServer" << YAML::Value << m_data->addServersFromServer;
    out << YAML::Key << "useUserSortedServerList" << YAML::Value << m_data->useUserSortedServerList;
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
    out << YAML::Key << "publicIPv6Override" << YAML::Value << m_data->publicIPv6Override.toStdString();
    out << YAML::Key << "ipv6PublicPeerConfirmThreshold" << YAML::Value << static_cast<int>(m_data->ipv6PublicPeerConfirmThreshold);
    out << YAML::Key << "ipv6PublicPeerConfirmWindowSecs" << YAML::Value << static_cast<int>(m_data->ipv6PublicPeerConfirmWindowSecs);
    out << YAML::Key << "ipv4PublicServerConfirmThreshold" << YAML::Value << static_cast<int>(m_data->ipv4PublicServerConfirmThreshold);
    out << YAML::Key << "ipv4PublicServerConfirmWindowSecs" << YAML::Value << static_cast<int>(m_data->ipv4PublicServerConfirmWindowSecs);
    out << YAML::Key << "separateIPv6Queue" << YAML::Value << m_data->separateIPv6Queue;
    out << YAML::Key << "serverPreferIPv6" << YAML::Value << m_data->serverPreferIPv6;
    out << YAML::Key << "maxConsPerFive" << YAML::Value << static_cast<int>(m_data->maxConsPerFive);
    out << YAML::Key << "showOverhead" << YAML::Value << m_data->showOverhead;
    out << YAML::Key << "networkED2K" << YAML::Value << m_data->networkED2K;
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
    out << YAML::Key << "cryptLayerRequiredStrict" << YAML::Value << m_data->cryptLayerRequiredStrict;
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

    // Download categories. A sequence, not a map: the index *is* the identity —
    // part.met stores it (FT_CATEGORY) — so the order has to survive the round
    // trip. Index 0 is written like any other so the file reads the way the tab
    // bar looks; it is the loader that keeps its path empty.
    out << YAML::Key << "categories" << YAML::Value << YAML::BeginSeq;
    for (const auto& cat : m_data->categories) {
        out << YAML::BeginMap;
        out << YAML::Key << "title" << YAML::Value << cat.title.toStdString();
        if (!cat.incomingPath.isEmpty())
            out << YAML::Key << "incoming" << YAML::Value << cat.incomingPath.toStdString();
        if (!cat.comment.isEmpty())
            out << YAML::Key << "comment" << YAML::Value << cat.comment.toStdString();
        if (!cat.autocat.isEmpty())
            out << YAML::Key << "autocat" << YAML::Value << cat.autocat.toStdString();
        if (cat.autocatIsRegexp)
            out << YAML::Key << "autocatRegexp" << YAML::Value << cat.autocatIsRegexp;
        if (!cat.regexp.isEmpty())
            out << YAML::Key << "regexp" << YAML::Value << cat.regexp.toStdString();
        out << YAML::Key << "color" << YAML::Value << cat.color;
        out << YAML::Key << "prio" << YAML::Value << static_cast<int>(cat.prio);
        // The view-filter trio is written only when set. Nothing reads it back
        // yet; it is here so those modes can land without a file migration.
        if (cat.filter != 0)
            out << YAML::Key << "filter" << YAML::Value << cat.filter;
        if (cat.filterNeg)
            out << YAML::Key << "filterNeg" << YAML::Value << cat.filterNeg;
        if (cat.care4all)
            out << YAML::Key << "care4all" << YAML::Value << cat.care4all;
        if (cat.downloadInAlphabeticalOrder)
            out << YAML::Key << "alphabetical" << YAML::Value << cat.downloadInAlphabeticalOrder;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    // UPnP
    out << YAML::Key << "upnp" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "enableUPnP" << YAML::Value << m_data->enableUPnP;
    out << YAML::Key << "closeUPnPOnExit" << YAML::Value << m_data->closeUPnPOnExit;
    out << YAML::Key << "portMapProtocols" << YAML::Value << m_data->portMapProtocols;
    out << YAML::Key << "portMapLeaseSecs" << YAML::Value << m_data->portMapLeaseSecs;
    out << YAML::Key << "portMapIPv6" << YAML::Value << m_data->portMapIPv6;
    out << YAML::Key << "portMapMethod" << YAML::Value << m_data->portMapMethod;
    out << YAML::Key << "portMapSecret" << YAML::Value << m_data->portMapSecret.toStdString();
    out << YAML::EndMap;

    // Logging
    out << YAML::Key << "logging" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "logToDiskCore" << YAML::Value << m_data->logToDiskCore;
    out << YAML::Key << "logToDiskGui" << YAML::Value << m_data->logToDiskGui;
    out << YAML::Key << "maxLogFileSize" << YAML::Value << m_data->maxLogFileSize;
    out << YAML::Key << "verbose" << YAML::Value << m_data->verbose;
    out << YAML::Key << "logPublicIP" << YAML::Value << m_data->logPublicIP;
    out << YAML::Key << "kadVerboseLog" << YAML::Value << m_data->kadVerboseLog;
    out << YAML::Key << "serverVerboseLog" << YAML::Value << m_data->serverVerboseLog;
    out << YAML::Key << "maxLogLines" << YAML::Value << m_data->maxLogLines;
    out << YAML::Key << "logLevel" << YAML::Value << m_data->logLevel;
    out << YAML::Key << "logSourceExchange" << YAML::Value << m_data->logSourceExchange;
    out << YAML::Key << "logBannedClients" << YAML::Value << m_data->logBannedClients;
    out << YAML::Key << "logRatingDescReceived" << YAML::Value << m_data->logRatingDescReceived;
    out << YAML::Key << "logSecureIdent" << YAML::Value << m_data->logSecureIdent;
    out << YAML::Key << "logFilteredIPs" << YAML::Value << m_data->logFilteredIPs;
    out << YAML::Key << "logFileSaving" << YAML::Value << m_data->logFileSaving;
    out << YAML::Key << "logA4AF" << YAML::Value << m_data->logA4AF;
    out << YAML::Key << "logUlDlEvents" << YAML::Value << m_data->logUlDlEvents;
    out << YAML::Key << "logRawSocketPackets" << YAML::Value << m_data->logRawSocketPackets;
    out << YAML::Key << "logWebServer" << YAML::Value << m_data->logWebServer;
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
    out << YAML::Key << "useSaveLoadSources" << YAML::Value << m_data->useSaveLoadSources;
    out << YAML::Key << "rememberUploadQueue" << YAML::Value << m_data->rememberUploadQueue;
    out << YAML::Key << "useCreditSystem" << YAML::Value << m_data->useCreditSystem;
    out << YAML::Key << "a4afSaveCpu" << YAML::Value << m_data->a4afSaveCpu;
    out << YAML::Key << "autoArchivePreviewStart" << YAML::Value << m_data->autoArchivePreviewStart;
    out << YAML::Key << "ed2kHostname" << YAML::Value << m_data->ed2kHostname.toStdString();
    out << YAML::Key << "ed2kLinkAdvertiseIPv6" << YAML::Value << m_data->ed2kLinkAdvertiseIPv6;
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
    out << YAML::Key << "statsSaveInterval" << YAML::Value << m_data->statsSaveInterval;
    out << YAML::Key << "statsLastReset" << YAML::Value << m_data->statsLastReset;

    // Cumulative transfer totals
    out << YAML::Key << "cumTotalUploaded" << YAML::Value << m_data->cumTotalUploaded;
    out << YAML::Key << "cumTotalDownloaded" << YAML::Value << m_data->cumTotalDownloaded;
    out << YAML::Key << "cumTotalUploadedToFriend" << YAML::Value << m_data->cumTotalUploadedToFriend;

    // Cumulative upload sessions
    out << YAML::Key << "cumUpSuccessfulSessions" << YAML::Value << m_data->cumUpSuccessfulSessions;
    out << YAML::Key << "cumUpFailedSessions" << YAML::Value << m_data->cumUpFailedSessions;
    out << YAML::Key << "cumUpAvgTime" << YAML::Value << m_data->cumUpAvgTime;

    // Cumulative download sessions
    out << YAML::Key << "cumDownSuccessfulSessions" << YAML::Value << m_data->cumDownSuccessfulSessions;
    out << YAML::Key << "cumDownFailedSessions" << YAML::Value << m_data->cumDownFailedSessions;
    out << YAML::Key << "cumDownCompletedFiles" << YAML::Value << m_data->cumDownCompletedFiles;
    out << YAML::Key << "cumDownAvgTime" << YAML::Value << m_data->cumDownAvgTime;

    // Cumulative overhead — upload
    out << YAML::Key << "cumUpOverheadTotal" << YAML::Value << m_data->cumUpOverheadTotal;
    out << YAML::Key << "cumUpOverheadTotalPackets" << YAML::Value << m_data->cumUpOverheadTotalPackets;
    out << YAML::Key << "cumUpOverheadFileReq" << YAML::Value << m_data->cumUpOverheadFileReq;
    out << YAML::Key << "cumUpOverheadFileReqPackets" << YAML::Value << m_data->cumUpOverheadFileReqPackets;
    out << YAML::Key << "cumUpOverheadSrcExch" << YAML::Value << m_data->cumUpOverheadSrcExch;
    out << YAML::Key << "cumUpOverheadSrcExchPackets" << YAML::Value << m_data->cumUpOverheadSrcExchPackets;
    out << YAML::Key << "cumUpOverheadServer" << YAML::Value << m_data->cumUpOverheadServer;
    out << YAML::Key << "cumUpOverheadServerPackets" << YAML::Value << m_data->cumUpOverheadServerPackets;
    out << YAML::Key << "cumUpOverheadKad" << YAML::Value << m_data->cumUpOverheadKad;
    out << YAML::Key << "cumUpOverheadKadPackets" << YAML::Value << m_data->cumUpOverheadKadPackets;

    // Cumulative overhead — download
    out << YAML::Key << "cumDownOverheadTotal" << YAML::Value << m_data->cumDownOverheadTotal;
    out << YAML::Key << "cumDownOverheadTotalPackets" << YAML::Value << m_data->cumDownOverheadTotalPackets;
    out << YAML::Key << "cumDownOverheadFileReq" << YAML::Value << m_data->cumDownOverheadFileReq;
    out << YAML::Key << "cumDownOverheadFileReqPackets" << YAML::Value << m_data->cumDownOverheadFileReqPackets;
    out << YAML::Key << "cumDownOverheadSrcExch" << YAML::Value << m_data->cumDownOverheadSrcExch;
    out << YAML::Key << "cumDownOverheadSrcExchPackets" << YAML::Value << m_data->cumDownOverheadSrcExchPackets;
    out << YAML::Key << "cumDownOverheadServer" << YAML::Value << m_data->cumDownOverheadServer;
    out << YAML::Key << "cumDownOverheadServerPackets" << YAML::Value << m_data->cumDownOverheadServerPackets;
    out << YAML::Key << "cumDownOverheadKad" << YAML::Value << m_data->cumDownOverheadKad;
    out << YAML::Key << "cumDownOverheadKadPackets" << YAML::Value << m_data->cumDownOverheadKadPackets;

    // Cumulative connection stats
    out << YAML::Key << "cumConnPeak" << YAML::Value << m_data->cumConnPeak;
    out << YAML::Key << "cumConnMaxLimitReached" << YAML::Value << m_data->cumConnMaxLimitReached;
    out << YAML::Key << "cumConnReconnects" << YAML::Value << m_data->cumConnReconnects;

    // Cumulative times
    out << YAML::Key << "cumRunTime" << YAML::Value << m_data->cumRunTime;
    out << YAML::Key << "cumTransferTime" << YAML::Value << m_data->cumTransferTime;
    out << YAML::Key << "cumUploadTime" << YAML::Value << m_data->cumUploadTime;
    out << YAML::Key << "cumDownloadTime" << YAML::Value << m_data->cumDownloadTime;
    out << YAML::Key << "cumServerDuration" << YAML::Value << m_data->cumServerDuration;

    // Cumulative quality stats
    out << YAML::Key << "cumCompressionGain" << YAML::Value << m_data->cumCompressionGain;
    out << YAML::Key << "cumCorruptionLoss" << YAML::Value << m_data->cumCorruptionLoss;
    out << YAML::Key << "cumIchPartsSaved" << YAML::Value << m_data->cumIchPartsSaved;

    // Per-client cumulative upload
    out << YAML::Key << "cumUpEmule" << YAML::Value << m_data->cumUpEmule;
    out << YAML::Key << "cumUpEDHybrid" << YAML::Value << m_data->cumUpEDHybrid;
    out << YAML::Key << "cumUpEDonkey" << YAML::Value << m_data->cumUpEDonkey;
    out << YAML::Key << "cumUpAMule" << YAML::Value << m_data->cumUpAMule;
    out << YAML::Key << "cumUpMLdonkey" << YAML::Value << m_data->cumUpMLdonkey;
    out << YAML::Key << "cumUpShareaza" << YAML::Value << m_data->cumUpShareaza;
    out << YAML::Key << "cumUpEMCompat" << YAML::Value << m_data->cumUpEMCompat;

    // Per-client cumulative download
    out << YAML::Key << "cumDownEmule" << YAML::Value << m_data->cumDownEmule;
    out << YAML::Key << "cumDownEDHybrid" << YAML::Value << m_data->cumDownEDHybrid;
    out << YAML::Key << "cumDownEDonkey" << YAML::Value << m_data->cumDownEDonkey;
    out << YAML::Key << "cumDownAMule" << YAML::Value << m_data->cumDownAMule;
    out << YAML::Key << "cumDownMLdonkey" << YAML::Value << m_data->cumDownMLdonkey;
    out << YAML::Key << "cumDownShareaza" << YAML::Value << m_data->cumDownShareaza;
    out << YAML::Key << "cumDownEMCompat" << YAML::Value << m_data->cumDownEMCompat;
    out << YAML::Key << "cumDownURL" << YAML::Value << m_data->cumDownURL;

    // Per-port cumulative
    out << YAML::Key << "cumUpPort4662" << YAML::Value << m_data->cumUpPort4662;
    out << YAML::Key << "cumUpPortOther" << YAML::Value << m_data->cumUpPortOther;
    out << YAML::Key << "cumDownPort4662" << YAML::Value << m_data->cumDownPort4662;
    out << YAML::Key << "cumDownPortOther" << YAML::Value << m_data->cumDownPortOther;

    // Per-source cumulative
    out << YAML::Key << "cumUpFromFile" << YAML::Value << m_data->cumUpFromFile;
    out << YAML::Key << "cumUpFromPartfile" << YAML::Value << m_data->cumUpFromPartfile;

    // HTTP Cache / Usenet / indexer cumulative blocks
    out << YAML::Key << "cumHttpCache" << YAML::Value;
    emitStatValue(out, m_data->cumHttpCache);
    out << YAML::Key << "cumUsenet" << YAML::Value;
    emitStatValue(out, m_data->cumUsenet);
    out << YAML::Key << "cumIndexer" << YAML::Value;
    emitStatValue(out, m_data->cumIndexer);

    // Records
    out << YAML::Key << "recMaxWorkingServers" << YAML::Value << m_data->recMaxWorkingServers;
    out << YAML::Key << "recMaxUsersOnline" << YAML::Value << m_data->recMaxUsersOnline;
    out << YAML::Key << "recMaxFilesAvail" << YAML::Value << m_data->recMaxFilesAvail;
    out << YAML::Key << "recMaxSharedFiles" << YAML::Value << m_data->recMaxSharedFiles;
    out << YAML::Key << "recMaxSharedSize" << YAML::Value << m_data->recMaxSharedSize;
    out << YAML::Key << "recMaxAvgFileSize" << YAML::Value << m_data->recMaxAvgFileSize;
    out << YAML::Key << "recMaxLargestFile" << YAML::Value << m_data->recMaxLargestFile;
    out << YAML::EndMap;

    // Security
    out << YAML::Key << "security" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "ipFilterLevel" << YAML::Value << m_data->ipFilterLevel;
    out << YAML::Key << "useSecureIdent" << YAML::Value << m_data->useSecureIdent;
    out << YAML::Key << "viewSharedFilesAccess" << YAML::Value << m_data->viewSharedFilesAccess;
    out << YAML::Key << "warnUntrustedFiles" << YAML::Value << m_data->warnUntrustedFiles;
    out << YAML::Key << "useSafeKad" << YAML::Value << m_data->useSafeKad;
    out << YAML::Key << "useFastKad" << YAML::Value << m_data->useFastKad;
    if (!m_data->ipFilterUpdateUrl.isEmpty())
        out << YAML::Key << "ipFilterUpdateUrl" << YAML::Value << m_data->ipFilterUpdateUrl.toStdString();
    if (!m_data->bugReportApiKey.isEmpty())
        out << YAML::Key << "bugReportApiKey" << YAML::Value << m_data->bugReportApiKey.toStdString();
    if (!m_data->bugReportDomain.isEmpty())
        out << YAML::Key << "bugReportDomain" << YAML::Value << m_data->bugReportDomain.toStdString();
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
    out << YAML::Key << "fileNameExpiryDays" << YAML::Value << m_data->kadFileNameExpiryDays;
    out << YAML::Key << "fileNameMaxCount" << YAML::Value << m_data->kadFileNameMaxCount;
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
    out << YAML::Key << "showSpeedGraph" << YAML::Value << m_data->showSpeedGraph;
    out << YAML::Key << "speedGraphTimeRangeMin" << YAML::Value << m_data->speedGraphTimeRangeMin;
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
    // Generate the at-rest key on first save if needed. It protects every secret
    // in this file, not just the SMTP password — the HTTP Cache API key rides on
    // it too — so any of them being set is reason enough to mint one.
    QByteArray encKey = m_data->notifyEmailEncKey;
    const bool haveCacheKey = std::ranges::any_of(m_data->httpCacheServers,
                                                  [](const HttpCacheServerConfig& server) {
                                                      return !server.apiKey.isEmpty();
                                                  });
    // A user who configures only Usenet has neither an SMTP password nor a
    // cache key, so without this term no key is ever minted and every provider
    // password is dropped on the first save.
    const bool haveUsenetPassword = std::ranges::any_of(m_data->usenetServers,
                                                        [](const NewsServer& server) {
                                                            return !server.pass.isEmpty();
                                                        });
    // Same again for indexer API keys: a user who configures only a search
    // indexer has none of the three secrets above.
    const bool haveIndexerKey = std::ranges::any_of(m_data->indexers,
                                                    [](const IndexerConfig& indexer) {
                                                        return !indexer.apiKey.isEmpty();
                                                    });
    // And once more for a pasted feed URL. It looks like a URL and is stored like
    // a password, because newznab's RSS endpoint takes the API key as a query
    // parameter — so a user whose only secret is a feed still needs a key, or
    // the feed is silently dropped on the first save.
    const bool haveFeedUrl = std::ranges::any_of(m_data->indexerFeeds,
                                                 [](const IndexerFeed& feed) {
                                                     return !feed.url.isEmpty();
                                                 });
    if (encKey.isEmpty() && (!m_data->notifyEmailSmtpPassword.isEmpty() || haveCacheKey
                             || haveUsenetPassword || haveIndexerKey || haveFeedUrl)) {
        encKey = aesRandomKey();
        m_data->notifyEmailEncKey = encKey;
    }
    if (!encKey.isEmpty()) {
        out << YAML::Key << "emailEncryptionKey" << YAML::Value << encKey.toHex().toStdString();
        out << YAML::Key << "emailSmtpPasswordEnc" << YAML::Value
            << aesEncryptToBase64(m_data->notifyEmailSmtpPassword, encKey).toStdString();
    }
    out << YAML::EndMap;

    // HTTP Cache. Written after `notifications` so encKey above is already
    // settled; each API key is stored encrypted under it and the plaintext
    // `apiKey` form the loader accepts is never written back.
    out << YAML::Key << "httpCache" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "enabled" << YAML::Value << m_data->httpCacheEnabled;
    out << YAML::Key << "allowDownload" << YAML::Value << m_data->httpCacheAllowDownload;
    out << YAML::Key << "allowUpload" << YAML::Value << m_data->httpCacheAllowUpload;
    out << YAML::Key << "allowRelay" << YAML::Value << m_data->httpCacheAllowRelay;
    out << YAML::Key << "publishToKad" << YAML::Value << m_data->httpCachePublishToKad;
    out << YAML::Key << "fetchFromKad" << YAML::Value << m_data->httpCacheFetchFromKad;
    out << YAML::Key << "servers" << YAML::Value << YAML::BeginSeq;
    for (const auto& server : m_data->httpCacheServers) {
        out << YAML::BeginMap;
        if (!server.name.isEmpty())
            out << YAML::Key << "name" << YAML::Value << server.name.toStdString();
        out << YAML::Key << "baseUrl" << YAML::Value << server.baseUrl.toStdString();
        if (!server.apiKey.isEmpty() && !encKey.isEmpty()) {
            out << YAML::Key << "apiKeyEnc" << YAML::Value
                << aesEncryptToBase64(server.apiKey, encKey).toStdString();
        }
        if (!server.keyId.isEmpty())
            out << YAML::Key << "keyId" << YAML::Value << server.keyId.toStdString();
        out << YAML::Key << "enabled" << YAML::Value << server.enabled;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::Key << "minClients" << YAML::Value << m_data->httpCacheMinClients;
    out << YAML::Key << "chunkTtlSeconds" << YAML::Value << m_data->httpCacheChunkTtlSeconds;
    out << YAML::Key << "maxPublishBytesPerDay" << YAML::Value
        << m_data->httpCacheMaxPublishBytesPerDay;
    out << YAML::Key << "publishRateKBs" << YAML::Value << m_data->httpCachePublishRateKBs;
    out << YAML::Key << "maxConcurrentPublishes" << YAML::Value
        << m_data->httpCacheMaxConcurrentPublishes;
    out << YAML::Key << "maxConcurrentFetches" << YAML::Value
        << m_data->httpCacheMaxConcurrentFetches;
    out << YAML::EndMap;

    // Usenet. Written after `notifications` for the same reason it is loaded
    // after it: encKey above is only settled by then. Each provider password is
    // stored encrypted under it, and the plaintext `pass` form the loader
    // accepts is never written back.
    out << YAML::Key << "usenet" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "enabled" << YAML::Value << m_data->usenetEnabled;
    out << YAML::Key << "retryIntervalSeconds" << YAML::Value
        << m_data->usenetRetryIntervalSeconds;
    out << YAML::Key << "downloadSharePercent" << YAML::Value
        << m_data->usenetDownloadSharePercent;
    out << YAML::Key << "par2Repair" << YAML::Value << m_data->usenetPar2Repair;
    out << YAML::Key << "par2RenameFiles" << YAML::Value << m_data->usenetPar2RenameFiles;
    out << YAML::Key << "unpack" << YAML::Value << m_data->usenetUnpack;
    out << YAML::Key << "cleanupAfterUnpack" << YAML::Value
        << m_data->usenetCleanupAfterUnpack;
    out << YAML::Key << "directUnpack" << YAML::Value << m_data->usenetDirectUnpack;
    out << YAML::Key << "encryptedPreview" << YAML::Value << m_data->usenetEncryptedPreview;
    if (!m_data->usenetExternalUnpacker.isEmpty()) {
        out << YAML::Key << "externalUnpacker" << YAML::Value
            << m_data->usenetExternalUnpacker.toStdString();
    }
    out << YAML::Key << "healthCheck" << YAML::Value << m_data->usenetHealthCheck;
    out << YAML::Key << "healthMinPercent" << YAML::Value << m_data->usenetHealthMinPercent;
    out << YAML::Key << "autoAddPaused" << YAML::Value << m_data->usenetAutoAddPaused;
    if (!m_data->usenetWatchDir.isEmpty())
        out << YAML::Key << "watchDir" << YAML::Value << m_data->usenetWatchDir.toStdString();

    // Only when the user has actually set some. Writing the built-ins out would
    // change every existing preferences.yml for nothing, and would freeze this
    // release's defaults into installations that should keep following ours.
    if (!m_data->usenetSubjectPatterns.isEmpty()) {
        out << YAML::Key << "subjectPatterns" << YAML::Value << YAML::BeginSeq;
        for (const UsenetSubjectPattern& p : m_data->usenetSubjectPatterns) {
            out << YAML::BeginMap;
            out << YAML::Key << "name" << YAML::Value << p.name.toStdString();
            out << YAML::Key << "role" << YAML::Value
                << usenetSubjectRoleName(p.role).toStdString();
            out << YAML::Key << "pattern" << YAML::Value << p.pattern.toStdString();
            if (p.pick != UsenetSubjectPick::First)
                out << YAML::Key << "pick" << YAML::Value << usenetSubjectPickName(p.pick).toStdString();
            if (p.caseInsensitive)
                out << YAML::Key << "caseInsensitive" << YAML::Value << true;
            if (!p.enabled)
                out << YAML::Key << "enabled" << YAML::Value << false;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
    }

    out << YAML::Key << "servers" << YAML::Value << YAML::BeginSeq;
    for (const auto& server : m_data->usenetServers) {
        out << YAML::BeginMap;
        if (!server.name.isEmpty())
            out << YAML::Key << "name" << YAML::Value << server.name.toStdString();
        out << YAML::Key << "host" << YAML::Value << server.host.toStdString();
        out << YAML::Key << "port" << YAML::Value << static_cast<int>(server.port);
        out << YAML::Key << "tls" << YAML::Value << static_cast<int>(server.tlsMode);
        if (!server.user.isEmpty())
            out << YAML::Key << "user" << YAML::Value << server.user.toStdString();
        if (!server.pass.isEmpty() && !encKey.isEmpty()) {
            out << YAML::Key << "passEnc" << YAML::Value
                << aesEncryptToBase64(server.pass, encKey).toStdString();
        }
        out << YAML::Key << "level" << YAML::Value << server.level;
        if (server.group != 0)
            out << YAML::Key << "group" << YAML::Value << server.group;
        if (server.optional)
            out << YAML::Key << "optional" << YAML::Value << server.optional;
        if (server.retention != 0)
            out << YAML::Key << "retention" << YAML::Value << server.retention;
        if (server.joinGroup)
            out << YAML::Key << "joinGroup" << YAML::Value << server.joinGroup;
        if (!server.accountId.isEmpty())
            out << YAML::Key << "accountId" << YAML::Value << server.accountId.toStdString();
        // Written only when set, like group/optional/retention above, so an
        // unmetered account's block stays byte-identical to what it was.
        if (server.quotaKind != NntpQuotaKind::None) {
            out << YAML::Key << "quotaKind" << YAML::Value
                << static_cast<int>(server.quotaKind);
            out << YAML::Key << "quotaBytes" << YAML::Value
                << static_cast<long long>(server.quotaBytes);
            if (server.quotaKind == NntpQuotaKind::Monthly)
                out << YAML::Key << "quotaResetDay" << YAML::Value << server.quotaResetDay;
            if (server.quotaFallThrough)
                out << YAML::Key << "quotaFallThrough" << YAML::Value << server.quotaFallThrough;
        }
        out << YAML::Key << "maxConnections" << YAML::Value << server.maxConnections;
        out << YAML::Key << "certVerification" << YAML::Value
            << static_cast<int>(server.certVerification);
        out << YAML::Key << "enabled" << YAML::Value << server.enabled;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;
    out << YAML::EndMap;

    // Indexers. After `notifications` for the same reason as the two blocks
    // above: encKey is only settled there. `url` is written back exactly as the
    // user typed it — the /api completion is a request-time decision, and
    // normalising on save would take away their ability to correct it.
    out << YAML::Key << "indexers" << YAML::Value << YAML::BeginMap;
    out << YAML::Key << "resultLimit" << YAML::Value << m_data->indexerResultLimit;
    out << YAML::Key << "maxPages" << YAML::Value << m_data->indexerMaxPages;
    out << YAML::Key << "timeoutSeconds" << YAML::Value << m_data->indexerTimeoutSeconds;
    out << YAML::Key << "capsRefreshDays" << YAML::Value << m_data->indexerCapsRefreshDays;
    out << YAML::Key << "accounts" << YAML::Value << YAML::BeginSeq;
    for (const auto& indexer : m_data->indexers) {
        out << YAML::BeginMap;
        out << YAML::Key << "name" << YAML::Value << indexer.name.toStdString();
        out << YAML::Key << "url" << YAML::Value << indexer.url.toStdString();
        out << YAML::Key << "kind" << YAML::Value
            << indexerKindToString(indexer.kind).toStdString();
        if (!indexer.apiKey.isEmpty() && !encKey.isEmpty()) {
            out << YAML::Key << "apiKeyEnc" << YAML::Value
                << aesEncryptToBase64(indexer.apiKey, encKey).toStdString();
        }
        if (indexer.timeoutMs != m_data->indexerTimeoutSeconds * 1000)
            out << YAML::Key << "timeoutMs" << YAML::Value << indexer.timeoutMs;
        out << YAML::Key << "enabled" << YAML::Value << indexer.enabled;
        out << YAML::EndMap;
    }
    out << YAML::EndSeq;

    // Feeds. Optional fields written only when set, so a plain keyword feed's
    // block stays two lines and stays readable.
    if (!m_data->indexerFeeds.isEmpty()) {
        out << YAML::Key << "feeds" << YAML::Value << YAML::BeginSeq;
        for (const auto& feed : m_data->indexerFeeds) {
            out << YAML::BeginMap;
            out << YAML::Key << "name" << YAML::Value << feed.name.toStdString();
            out << YAML::Key << "kind" << YAML::Value
                << indexerFeedKindToString(feed.kind).toStdString();

            if (feed.kind == IndexerFeedKind::Url) {
                if (!feed.url.isEmpty() && !encKey.isEmpty()) {
                    // Encrypted, not written verbatim: the URL carries the API
                    // key as a query parameter.
                    out << YAML::Key << "urlEnc" << YAML::Value
                        << aesEncryptToBase64(feed.url, encKey).toStdString();
                }
            } else {
                if (!feed.query.isEmpty())
                    out << YAML::Key << "query" << YAML::Value << feed.query.toStdString();
                if (!feed.categories.isEmpty()) {
                    out << YAML::Key << "categories" << YAML::Value << YAML::Flow
                        << YAML::BeginSeq;
                    for (const int cat : feed.categories)
                        out << cat;
                    out << YAML::EndSeq;
                }
                if (!feed.indexers.isEmpty()) {
                    out << YAML::Key << "indexers" << YAML::Value << YAML::BeginSeq;
                    for (const auto& name : feed.indexers)
                        out << name.toStdString();
                    out << YAML::EndSeq;
                }
            }

            if (!feed.accept.isEmpty())
                out << YAML::Key << "accept" << YAML::Value << feed.accept.toStdString();
            if (!feed.reject.isEmpty())
                out << YAML::Key << "reject" << YAML::Value << feed.reject.toStdString();
            if (feed.minSize > 0)
                out << YAML::Key << "minSize" << YAML::Value << static_cast<long long>(feed.minSize);
            if (feed.maxSize > 0)
                out << YAML::Key << "maxSize" << YAML::Value << static_cast<long long>(feed.maxSize);
            if (feed.maxAgeDays > 0)
                out << YAML::Key << "maxAgeDays" << YAML::Value << feed.maxAgeDays;
            out << YAML::Key << "intervalMinutes" << YAML::Value << feed.intervalMinutes;
            if (feed.grabExisting)
                out << YAML::Key << "grabExisting" << YAML::Value << feed.grabExisting;
            if (feed.downloadCategory > 0)
                out << YAML::Key << "downloadCategory" << YAML::Value << feed.downloadCategory;
            out << YAML::Key << "enabled" << YAML::Value << feed.enabled;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
    }

    out << YAML::EndMap;

    // UI State is now in its own uistate.yml (managed by UiState class)

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

bool Preferences::writeStatsBackup(const Data& d, const QString& filePath)
{
    YAML::Emitter out;
    out << YAML::BeginMap;

    forEachCumulativeStat(d, [&out](const char* key, const auto& value) {
        out << YAML::Key << key << YAML::Value;
        emitStatValue(out, value);
    });
    forEachStatRecord(d, [&out](const char* key, const auto& value) {
        out << YAML::Key << key << YAML::Value << value;
    });
    out << YAML::Key << "statsLastReset" << YAML::Value << d.statsLastReset;

    out << YAML::EndMap;

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logError(QStringLiteral("Failed to open statistics backup for writing: %1").arg(filePath));
        return false;
    }

    file.write(out.c_str(), static_cast<qint64>(out.size()));
    file.write("\n", 1);

    if (!file.commit()) {
        logError(QStringLiteral("Failed to commit statistics backup: %1").arg(filePath));
        return false;
    }

    return true;
}

bool Preferences::isShareableDirectory(const QString& dir, const QString& configDir,
                                       const QString& incomingDir,
                                       const QStringList& tempDirs)
{
    if (dir.isEmpty())
        return false;

    if (samePath(dir, configDir))
        return false;
    if (samePath(dir, incomingDir))
        return false;
    for (const QString& tmp : tempDirs)
        if (samePath(dir, tmp))
            return false;

    // Deliberately *not* rejecting the category incoming dirs, though they are
    // shared unconditionally too. This same predicate validates a category's
    // own path (MFC: srchybrid/CatDialog.cpp:163), so rejecting them here would
    // make every category fail to keep the folder it already has. A category
    // folder listed again under shared dirs is harmless: shouldBeShared()
    // matches it on the category rule first.
    return true;
}

QString Preferences::sanitizeWatchDir(const QString& dir, const QString& configDir,
                                      const QString& incomingDir,
                                      const QStringList& tempDirs)
{
    const QString trimmed = dir.trimmed();
    if (trimmed.isEmpty())
        return {};

    // Containment, not equality — unlike isShareableDirectory(). A shared folder
    // that merely sits beside the temp tree is fine; a *watched* one below it is
    // not, because the scanner would be reading .nzb files the daemon is itself
    // still writing, and would then move them out from under it.
    const auto isInside = [&trimmed](const QString& root) {
        if (root.isEmpty())
            return false;
        const QString cleanRoot = QDir::cleanPath(QDir(root).absolutePath());
        const QString cleanDir = QDir::cleanPath(QFileInfo(trimmed).absoluteFilePath());
        if (cleanDir.compare(cleanRoot, Qt::CaseInsensitive) == 0)
            return true;
        // The separator matters: a sibling "Temp Archive" must not match "Temp".
        return cleanDir.startsWith(cleanRoot + QLatin1Char('/'), Qt::CaseInsensitive);
    };

    if (isInside(configDir) || isInside(incomingDir))
        return {};
    for (const QString& tmp : tempDirs)
        if (isInside(tmp))
            return {};

    return trimmed;
}

QList<DownloadCategory>
Preferences::sanitizeCategories(const QList<DownloadCategory>& cats, const QString& configDir,
                                const QString& incomingDir, const QStringList& tempDirs)
{
    QList<DownloadCategory> clean;
    clean.reserve(cats.size());

    for (const auto& cat : cats) {
        DownloadCategory entry = cat;
        entry.title = entry.title.trimmed();
        entry.incomingPath = entry.incomingPath.trimmed();

        if (clean.isEmpty()) {
            // Index 0 is the implicit "All" category. It carries no path of its
            // own — that is what makes incomingDirForCategory(0) resolve to the
            // global incoming dir, and it is why MFC's ShouldBeShared loop stops
            // at index 1 (srchybrid/SharedFileList.cpp:1394). MFC leaves it empty
            // at load and only fills it in from the GUI
            // (srchybrid/Preferences.cpp:2484, TransferWnd.cpp:156); keeping it
            // empty everywhere means there is no window where it is wrong.
            entry.incomingPath.clear();
            if (entry.title.isEmpty())
                entry.title = QStringLiteral("All");
            clean.append(entry);
            continue;
        }

        if (!entry.incomingPath.isEmpty()) {
            entry.incomingPath = QDir::cleanPath(QDir(entry.incomingPath).absolutePath());

            // A path eMule uses for its own storage, or one we cannot create,
            // degrades to the global incoming dir rather than losing the whole
            // category — MFC's exact fallback (srchybrid/Preferences.cpp:2478).
            // Stored as empty, not as a copy of incomingDir(): that way the
            // category follows the global dir if the user later moves it.
            if (!isShareableDirectory(entry.incomingPath, configDir, incomingDir, tempDirs)) {
                entry.incomingPath.clear();
            } else if (!QDir(entry.incomingPath).exists()
                       && !QDir().mkpath(entry.incomingPath)) {
                logWarning(QStringLiteral(
                               "Category \"%1\": cannot use incoming directory %2 — "
                               "falling back to the global incoming directory")
                               .arg(entry.displayName(), entry.incomingPath));
                entry.incomingPath.clear();
            }
        }

        // A title is the only thing that makes a tab identifiable, so an entry
        // without one gets MFC's placeholder rather than being dropped: losing
        // a category silently renumbers every download behind it.
        if (entry.title.isEmpty())
            entry.title = entry.displayName();

        clean.append(entry);
        if (clean.size() >= kMaxCategories)
            break;
    }

    if (clean.isEmpty())
        clean.append(DownloadCategory{.title = QStringLiteral("All")});

    return clean;
}

} // namespace eMule
