#pragma once

/// @file Preferences.h
/// @brief Central preferences with YAML persistence — replaces MFC CPreferences.
///
/// Provides a thread-safe, non-static Preferences class with ~50 essential
/// settings across 9 categories.  Persists to YAML via yaml-cpp.
/// Factory methods bridge to existing config structs (ObfuscationConfig,
/// ProxySettings) used by already-ported modules.

#include "utils/Types.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QReadWriteLock>
#include <QString>
#include <QStringList>

#include <array>
#include <memory>

namespace eMule {

// Forward declarations for factory method return types
struct ObfuscationConfig;
struct ProxySettings;

class Preferences {
public:
    Preferences();
    ~Preferences();

    Preferences(const Preferences&) = delete;
    Preferences& operator=(const Preferences&) = delete;

    // -- Persistence ----------------------------------------------------------

    /// Load preferences from YAML file.  Returns true on success or first run
    /// (missing file).  Returns false on parse error (defaults are applied).
    bool load(const QString& filePath);

    /// Save preferences to the file specified in load().
    bool save() const;

    /// Save preferences to a specific file.
    bool saveTo(const QString& filePath) const;

    /// Reset all settings to their defaults.
    void setDefaults();

    // -- General --------------------------------------------------------------

    [[nodiscard]] QString nick() const;
    void setNick(const QString& val);

    [[nodiscard]] std::array<uint8, 16> userHash() const;
    void setUserHash(const std::array<uint8, 16>& val);

    [[nodiscard]] bool autoConnect() const;
    void setAutoConnect(bool val);

    [[nodiscard]] bool reconnect() const;
    void setReconnect(bool val);

    [[nodiscard]] bool filterLANIPs() const;
    void setFilterLANIPs(bool val);

    // -- Server connection ----------------------------------------------------

    [[nodiscard]] bool safeServerConnect() const;
    void setSafeServerConnect(bool val);

    [[nodiscard]] bool autoConnectStaticOnly() const;
    void setAutoConnectStaticOnly(bool val);

    [[nodiscard]] bool useServerPriorities() const;
    void setUseServerPriorities(bool val);

    [[nodiscard]] bool addServersFromServer() const;
    void setAddServersFromServer(bool val);

    [[nodiscard]] uint32 serverKeepAliveTimeout() const;
    void setServerKeepAliveTimeout(uint32 val);

    // -- Network --------------------------------------------------------------

    [[nodiscard]] uint16 port() const;
    void setPort(uint16 val);

    [[nodiscard]] uint16 udpPort() const;
    void setUdpPort(uint16 val);

    [[nodiscard]] uint16 serverUDPPort() const;
    void setServerUDPPort(uint16 val);

    [[nodiscard]] uint16 maxConnections() const;
    void setMaxConnections(uint16 val);

    [[nodiscard]] uint16 maxHalfConnections() const;
    void setMaxHalfConnections(uint16 val);

    [[nodiscard]] QString bindAddress() const;
    void setBindAddress(const QString& val);

    // -- Bandwidth ------------------------------------------------------------

    [[nodiscard]] uint32 maxUpload() const;
    void setMaxUpload(uint32 val);

    [[nodiscard]] uint32 maxDownload() const;
    void setMaxDownload(uint32 val);

    [[nodiscard]] uint32 minUpload() const;
    void setMinUpload(uint32 val);

    [[nodiscard]] uint32 maxGraphUploadRate() const;
    void setMaxGraphUploadRate(uint32 val);

    [[nodiscard]] uint32 maxGraphDownloadRate() const;
    void setMaxGraphDownloadRate(uint32 val);

    // -- Encryption -----------------------------------------------------------

    [[nodiscard]] bool cryptLayerSupported() const;
    void setCryptLayerSupported(bool val);

    [[nodiscard]] bool cryptLayerRequested() const;
    void setCryptLayerRequested(bool val);

    [[nodiscard]] bool cryptLayerRequired() const;
    void setCryptLayerRequired(bool val);

    [[nodiscard]] uint8 cryptTCPPaddingLength() const;
    void setCryptTCPPaddingLength(uint8 val);

    // -- Proxy ----------------------------------------------------------------

    [[nodiscard]] int proxyType() const;
    void setProxyType(int val);

    [[nodiscard]] QString proxyHost() const;
    void setProxyHost(const QString& val);

    [[nodiscard]] uint16 proxyPort() const;
    void setProxyPort(uint16 val);

    [[nodiscard]] bool proxyEnablePassword() const;
    void setProxyEnablePassword(bool val);

    [[nodiscard]] QString proxyUser() const;
    void setProxyUser(const QString& val);

    [[nodiscard]] QString proxyPassword() const;
    void setProxyPassword(const QString& val);

    // -- Directories ----------------------------------------------------------

    [[nodiscard]] QString incomingDir() const;
    void setIncomingDir(const QString& val);

    [[nodiscard]] QStringList tempDirs() const;
    void setTempDirs(const QStringList& val);

    [[nodiscard]] QString configDir() const;
    void setConfigDir(const QString& val);

    [[nodiscard]] QString fileCommentsFilePath() const;
    void setFileCommentsFilePath(const QString& val);

    [[nodiscard]] QStringList sharedDirs() const;
    void setSharedDirs(const QStringList& val);

    // -- UPnP -----------------------------------------------------------------

    [[nodiscard]] bool enableUPnP() const;
    void setEnableUPnP(bool val);

    [[nodiscard]] bool skipWANIPSetup() const;
    void setSkipWANIPSetup(bool val);

    [[nodiscard]] bool skipWANPPPSetup() const;
    void setSkipWANPPPSetup(bool val);

    [[nodiscard]] bool closeUPnPOnExit() const;
    void setCloseUPnPOnExit(bool val);

    // -- Logging --------------------------------------------------------------

    [[nodiscard]] bool logToDisk() const;
    void setLogToDisk(bool val);

    [[nodiscard]] uint32 maxLogFileSize() const;
    void setMaxLogFileSize(uint32 val);

    [[nodiscard]] bool verbose() const;
    void setVerbose(bool val);

    [[nodiscard]] bool kadVerboseLog() const;
    void setKadVerboseLog(bool val);

    [[nodiscard]] uint32 maxLogLines() const;
    void setMaxLogLines(uint32 val);

    [[nodiscard]] int logLevel() const;
    void setLogLevel(int val);

    [[nodiscard]] bool verboseLogToDisk() const;
    void setVerboseLogToDisk(bool val);

    [[nodiscard]] bool logSourceExchange() const;
    void setLogSourceExchange(bool val);

    [[nodiscard]] bool logBannedClients() const;
    void setLogBannedClients(bool val);

    [[nodiscard]] bool logRatingDescReceived() const;
    void setLogRatingDescReceived(bool val);

    [[nodiscard]] bool logSecureIdent() const;
    void setLogSecureIdent(bool val);

    [[nodiscard]] bool logFilteredIPs() const;
    void setLogFilteredIPs(bool val);

    [[nodiscard]] bool logFileSaving() const;
    void setLogFileSaving(bool val);

    [[nodiscard]] bool logA4AF() const;
    void setLogA4AF(bool val);

    [[nodiscard]] bool logUlDlEvents() const;
    void setLogUlDlEvents(bool val);

    // -- Files ----------------------------------------------------------------

    [[nodiscard]] uint16 maxSourcesPerFile() const;
    void setMaxSourcesPerFile(uint16 val);

    [[nodiscard]] bool useICH() const;
    void setUseICH(bool val);

    [[nodiscard]] bool autoSharedFilesPriority() const;
    void setAutoSharedFilesPriority(bool val);

    [[nodiscard]] bool transferFullChunks() const;
    void setTransferFullChunks(bool val);

    [[nodiscard]] bool previewPrio() const;
    void setPreviewPrio(bool val);

    [[nodiscard]] bool startNextPausedFile() const;
    void setStartNextPausedFile(bool val);

    [[nodiscard]] bool startNextPausedFileSameCat() const;
    void setStartNextPausedFileSameCat(bool val);

    [[nodiscard]] bool startNextPausedFileOnlySameCat() const;
    void setStartNextPausedFileOnlySameCat(bool val);

    [[nodiscard]] bool rememberDownloadedFiles() const;
    void setRememberDownloadedFiles(bool val);

    [[nodiscard]] bool rememberCancelledFiles() const;
    void setRememberCancelledFiles(bool val);

    // -- Transfer -------------------------------------------------------------

    [[nodiscard]] uint32 fileBufferSize() const;
    void setFileBufferSize(uint32 val);

    [[nodiscard]] uint32 fileBufferTimeLimit() const;
    void setFileBufferTimeLimit(uint32 val);

    // -- Extended (PPgTweaks) -------------------------------------------------

    [[nodiscard]] bool useCreditSystem() const;
    void setUseCreditSystem(bool val);

    [[nodiscard]] bool a4afSaveCpu() const;
    void setA4afSaveCpu(bool val);

    [[nodiscard]] bool autoArchivePreviewStart() const;
    void setAutoArchivePreviewStart(bool val);

    [[nodiscard]] QString ed2kHostname() const;
    void setEd2kHostname(const QString& val);

    [[nodiscard]] bool showExtControls() const;
    void setShowExtControls(bool val);

    [[nodiscard]] int commitFiles() const;
    void setCommitFiles(int val);

    [[nodiscard]] int extractMetaData() const;
    void setExtractMetaData(int val);

    [[nodiscard]] uint32 queueSize() const;
    void setQueueSize(uint32 val);

    // Upload SpeedSense (USS)
    [[nodiscard]] bool dynUpEnabled() const;
    void setDynUpEnabled(bool val);

    [[nodiscard]] int dynUpPingTolerance() const;
    void setDynUpPingTolerance(int val);

    [[nodiscard]] int dynUpPingToleranceMs() const;
    void setDynUpPingToleranceMs(int val);

    [[nodiscard]] bool dynUpUseMillisecondPingTolerance() const;
    void setDynUpUseMillisecondPingTolerance(bool val);

    [[nodiscard]] int dynUpGoingUpDivider() const;
    void setDynUpGoingUpDivider(int val);

    [[nodiscard]] int dynUpGoingDownDivider() const;
    void setDynUpGoingDownDivider(int val);

    [[nodiscard]] int dynUpNumberOfPings() const;
    void setDynUpNumberOfPings(int val);

#ifdef Q_OS_WIN
    [[nodiscard]] bool autotakeEd2kLinks() const;
    void setAutotakeEd2kLinks(bool val);

    [[nodiscard]] bool openPortsOnWinFirewall() const;
    void setOpenPortsOnWinFirewall(bool val);

    [[nodiscard]] bool sparsePartFiles() const;
    void setSparsePartFiles(bool val);

    [[nodiscard]] bool allocFullFile() const;
    void setAllocFullFile(bool val);

    [[nodiscard]] bool resolveShellLinks() const;
    void setResolveShellLinks(bool val);

    [[nodiscard]] int multiUserSharing() const;
    void setMultiUserSharing(int val);
#endif

    // -- Statistics -----------------------------------------------------------

    [[nodiscard]] float connMaxDownRate() const;
    void setConnMaxDownRate(float val);

    [[nodiscard]] float connAvgDownRate() const;
    void setConnAvgDownRate(float val);

    [[nodiscard]] float connMaxAvgDownRate() const;
    void setConnMaxAvgDownRate(float val);

    [[nodiscard]] float connAvgUpRate() const;
    void setConnAvgUpRate(float val);

    [[nodiscard]] float connMaxAvgUpRate() const;
    void setConnMaxAvgUpRate(float val);

    [[nodiscard]] float connMaxUpRate() const;
    void setConnMaxUpRate(float val);

    [[nodiscard]] uint32 statsAverageMinutes() const;
    void setStatsAverageMinutes(uint32 val);

    [[nodiscard]] uint32 graphsUpdateSec() const;
    void setGraphsUpdateSec(uint32 val);

    [[nodiscard]] uint32 statsUpdateSec() const;
    void setStatsUpdateSec(uint32 val);

    [[nodiscard]] bool fillGraphs() const;
    void setFillGraphs(bool val);

    [[nodiscard]] uint32 statsConnectionsMax() const;
    void setStatsConnectionsMax(uint32 val);

    [[nodiscard]] uint32 statsConnectionsRatio() const;
    void setStatsConnectionsRatio(uint32 val);

    // -- Security -------------------------------------------------------------

    [[nodiscard]] uint32 ipFilterLevel() const;
    void setIpFilterLevel(uint32 val);

    // -- IRC ------------------------------------------------------------------

    [[nodiscard]] QString ircServer() const;
    void setIrcServer(const QString& val);

    [[nodiscard]] QString ircNick() const;
    void setIrcNick(const QString& val);

    [[nodiscard]] bool ircEnableUTF8() const;
    void setIrcEnableUTF8(bool val);

    [[nodiscard]] bool ircUsePerform() const;
    void setIrcUsePerform(bool val);

    [[nodiscard]] QString ircPerformString() const;
    void setIrcPerformString(const QString& val);

    [[nodiscard]] bool ircConnectHelpChannel() const;
    void setIrcConnectHelpChannel(bool val);

    [[nodiscard]] bool ircLoadChannelList() const;
    void setIrcLoadChannelList(bool val);

    [[nodiscard]] bool ircAddTimestamp() const;
    void setIrcAddTimestamp(bool val);

    [[nodiscard]] bool ircIgnoreMiscInfoMessages() const;
    void setIrcIgnoreMiscInfoMessages(bool val);

    [[nodiscard]] bool ircIgnoreJoinMessages() const;
    void setIrcIgnoreJoinMessages(bool val);

    [[nodiscard]] bool ircIgnorePartMessages() const;
    void setIrcIgnorePartMessages(bool val);

    [[nodiscard]] bool ircIgnoreQuitMessages() const;
    void setIrcIgnoreQuitMessages(bool val);

    [[nodiscard]] bool ircUseChannelFilter() const;
    void setIrcUseChannelFilter(bool val);

    [[nodiscard]] QString ircChannelFilter() const;
    void setIrcChannelFilter(const QString& val);

    // -- IPC Daemon -----------------------------------------------------------

    [[nodiscard]] bool ipcEnabled() const;
    void setIpcEnabled(bool val);

    [[nodiscard]] uint16 ipcPort() const;
    void setIpcPort(uint16 val);

    [[nodiscard]] QString ipcListenAddress() const;
    void setIpcListenAddress(const QString& val);

    /// Path to emulecored binary. Empty = search next to GUI executable.
    [[nodiscard]] QString ipcDaemonPath() const;
    void setIpcDaemonPath(const QString& val);

    /// Remote IPC polling interval in milliseconds (default 1500).
    [[nodiscard]] int ipcRemotePollingMs() const;
    void setIpcRemotePollingMs(int val);

    /// IPC authentication tokens (array for future multi-token support).
    [[nodiscard]] QStringList ipcTokens() const;
    void setIpcTokens(const QStringList& val);

    // -- Web Server -----------------------------------------------------------

    [[nodiscard]] bool webServerEnabled() const;
    void setWebServerEnabled(bool val);

    [[nodiscard]] uint16 webServerPort() const;
    void setWebServerPort(uint16 val);

    [[nodiscard]] QString webServerApiKey() const;
    void setWebServerApiKey(const QString& val);

    [[nodiscard]] QString webServerListenAddress() const;
    void setWebServerListenAddress(const QString& val);

    [[nodiscard]] bool webServerRestApiEnabled() const;
    void setWebServerRestApiEnabled(bool val);

    [[nodiscard]] bool webServerGzipEnabled() const;
    void setWebServerGzipEnabled(bool val);

    [[nodiscard]] bool webServerUPnP() const;
    void setWebServerUPnP(bool val);

    [[nodiscard]] QString webServerTemplatePath() const;
    void setWebServerTemplatePath(const QString& val);

    [[nodiscard]] int webServerSessionTimeout() const;
    void setWebServerSessionTimeout(int val);

    [[nodiscard]] bool webServerHttpsEnabled() const;
    void setWebServerHttpsEnabled(bool val);

    [[nodiscard]] QString webServerCertPath() const;
    void setWebServerCertPath(const QString& val);

    [[nodiscard]] QString webServerKeyPath() const;
    void setWebServerKeyPath(const QString& val);

    [[nodiscard]] QString webServerAdminPassword() const;
    void setWebServerAdminPassword(const QString& val);

    [[nodiscard]] bool webServerAdminAllowHiLevFunc() const;
    void setWebServerAdminAllowHiLevFunc(bool val);

    [[nodiscard]] bool webServerGuestEnabled() const;
    void setWebServerGuestEnabled(bool val);

    [[nodiscard]] QString webServerGuestPassword() const;
    void setWebServerGuestPassword(const QString& val);

    // -- Scheduler ------------------------------------------------------------

    [[nodiscard]] bool schedulerEnabled() const;
    void setSchedulerEnabled(bool val);

    // -- Kademlia -------------------------------------------------------------

    [[nodiscard]] bool kadEnabled() const;
    void setKadEnabled(bool val);

    [[nodiscard]] uint32 kadUDPKey() const;
    void setKadUDPKey(uint32 val);

    // -- Connection -----------------------------------------------------------

    [[nodiscard]] uint16 maxConsPerFive() const;
    void setMaxConsPerFive(uint16 val);

    [[nodiscard]] bool showOverhead() const;
    void setShowOverhead(bool val);

    // -- Server management (extended) -----------------------------------------

    [[nodiscard]] bool addServersFromClients() const;
    void setAddServersFromClients(bool val);

    [[nodiscard]] bool filterServerByIP() const;
    void setFilterServerByIP(bool val);

    [[nodiscard]] uint32 deadServerRetries() const;
    void setDeadServerRetries(uint32 val);

    [[nodiscard]] bool autoUpdateServerList() const;
    void setAutoUpdateServerList(bool val);

    [[nodiscard]] QString serverListURL() const;
    void setServerListURL(const QString& val);

    [[nodiscard]] bool smartLowIdCheck() const;
    void setSmartLowIdCheck(bool val);

    [[nodiscard]] bool manualServerHighPriority() const;
    void setManualServerHighPriority(bool val);

    // -- Network modes --------------------------------------------------------

    [[nodiscard]] bool networkED2K() const;
    void setNetworkED2K(bool val);

    // -- Chat / Messages ------------------------------------------------------

    [[nodiscard]] bool msgOnlyFriends() const;
    void setMsgOnlyFriends(bool val);

    [[nodiscard]] bool msgSecure() const;
    void setMsgSecure(bool val);

    [[nodiscard]] bool useChatCaptchas() const;
    void setUseChatCaptchas(bool val);

    [[nodiscard]] bool enableSpamFilter() const;
    void setEnableSpamFilter(bool val);

    [[nodiscard]] QString messageFilter() const;
    void setMessageFilter(const QString& val);

    [[nodiscard]] QString commentFilter() const;
    void setCommentFilter(const QString& val);

    [[nodiscard]] bool showSmileys() const;
    void setShowSmileys(bool val);

    [[nodiscard]] bool indicateRatings() const;
    void setIndicateRatings(bool val);

    // -- Security (extended) --------------------------------------------------

    [[nodiscard]] bool useSecureIdent() const;
    void setUseSecureIdent(bool val);

    // -- Shared file visibility -----------------------------------------------

    /// Who can browse our shared files: 0=nobody, 1=friends, 2=everybody.
    [[nodiscard]] int viewSharedFilesAccess() const;
    void setViewSharedFilesAccess(int val);

    // -- Download behavior ----------------------------------------------------

    [[nodiscard]] bool autoDownloadPriority() const;
    void setAutoDownloadPriority(bool val);

    [[nodiscard]] bool addNewFilesPaused() const;
    void setAddNewFilesPaused(bool val);

    // -- Disk space -----------------------------------------------------------

    [[nodiscard]] bool checkDiskspace() const;
    void setCheckDiskspace(bool val);

    [[nodiscard]] uint64 minFreeDiskSpace() const;
    void setMinFreeDiskSpace(uint64 val);

    // -- Search ---------------------------------------------------------------

    [[nodiscard]] bool enableSearchResultFilter() const;
    void setEnableSearchResultFilter(bool val);

    // -- Network detection ----------------------------------------------------

    [[nodiscard]] uint32 publicIP() const;
    void setPublicIP(uint32 val);

    // -- GUI (General page) ---------------------------------------------------

    [[nodiscard]] bool promptOnExit() const;
    void setPromptOnExit(bool val);

    [[nodiscard]] bool startMinimized() const;
    void setStartMinimized(bool val);

    [[nodiscard]] bool showSplashScreen() const;
    void setShowSplashScreen(bool val);

    [[nodiscard]] QString language() const;
    void setLanguage(const QString& val);

    [[nodiscard]] bool enableOnlineSignature() const;
    void setEnableOnlineSignature(bool val);

    [[nodiscard]] bool enableMiniMule() const;
    void setEnableMiniMule(bool val);

    [[nodiscard]] bool preventStandby() const;
    void setPreventStandby(bool val);

    [[nodiscard]] bool startWithOS() const;
    void setStartWithOS(bool val);

    [[nodiscard]] uint32 startVersion() const;
    void setStartVersion(uint32 val);

    [[nodiscard]] bool versionCheckEnabled() const;
    void setVersionCheckEnabled(bool val);

    [[nodiscard]] int versionCheckDays() const;
    void setVersionCheckDays(int val);

    [[nodiscard]] int64_t lastVersionCheck() const;
    void setLastVersionCheck(int64_t val);

    [[nodiscard]] bool bringToFrontOnLinkClick() const;
    void setBringToFrontOnLinkClick(bool val);

    // -- GUI (Display page) ---------------------------------------------------

    [[nodiscard]] int depth3D() const;
    void setDepth3D(int val);

    [[nodiscard]] int tooltipDelay() const;
    void setTooltipDelay(int val);

    [[nodiscard]] bool minimizeToTray() const;
    void setMinimizeToTray(bool val);

    [[nodiscard]] bool transferDoubleClick() const;
    void setTransferDoubleClick(bool val);

    [[nodiscard]] bool showDwlPercentage() const;
    void setShowDwlPercentage(bool val);

    [[nodiscard]] bool showRatesInTitle() const;
    void setShowRatesInTitle(bool val);

    [[nodiscard]] bool showCatTabInfos() const;
    void setShowCatTabInfos(bool val);

    [[nodiscard]] bool autoRemoveFinishedDownloads() const;
    void setAutoRemoveFinishedDownloads(bool val);

    [[nodiscard]] bool showTransToolbar() const;
    void setShowTransToolbar(bool val);

    [[nodiscard]] bool storeSearches() const;
    void setStoreSearches(bool val);

    [[nodiscard]] bool disableKnownClientList() const;
    void setDisableKnownClientList(bool val);

    [[nodiscard]] bool disableQueueList() const;
    void setDisableQueueList(bool val);

    [[nodiscard]] bool useAutoCompletion() const;
    void setUseAutoCompletion(bool val);

    [[nodiscard]] bool useOriginalIcons() const;
    void setUseOriginalIcons(bool val);

    [[nodiscard]] QString logFont() const;
    void setLogFont(const QString& val);

    // -- GUI (Files page) -----------------------------------------------------

    [[nodiscard]] bool watchClipboard4ED2KLinks() const;
    void setWatchClipboard4ED2KLinks(bool val);

    [[nodiscard]] bool useAdvancedCalcRemainingTime() const;
    void setUseAdvancedCalcRemainingTime(bool val);

    [[nodiscard]] QString videoPlayerCommand() const;
    void setVideoPlayerCommand(const QString& val);

    [[nodiscard]] QString videoPlayerArgs() const;
    void setVideoPlayerArgs(const QString& val);

    [[nodiscard]] bool createBackupToPreview() const;
    void setCreateBackupToPreview(bool val);

    [[nodiscard]] bool autoCleanupFilenames() const;
    void setAutoCleanupFilenames(bool val);

    // -- Notifications (GUI-side) --------------------------------------------

    [[nodiscard]] int notifySoundType() const;       // 0=noSound, 1=soundFile, 2=speech
    void setNotifySoundType(int val);
    [[nodiscard]] QString notifySoundFile() const;
    void setNotifySoundFile(const QString& val);

    // -- Notifications (daemon-side triggers + email) -------------------------

    [[nodiscard]] bool notifyOnLog() const;
    void setNotifyOnLog(bool val);
    [[nodiscard]] bool notifyOnChat() const;
    void setNotifyOnChat(bool val);
    [[nodiscard]] bool notifyOnChatMsg() const;
    void setNotifyOnChatMsg(bool val);
    [[nodiscard]] bool notifyOnDownloadAdded() const;
    void setNotifyOnDownloadAdded(bool val);
    [[nodiscard]] bool notifyOnDownloadFinished() const;
    void setNotifyOnDownloadFinished(bool val);
    [[nodiscard]] bool notifyOnNewVersion() const;
    void setNotifyOnNewVersion(bool val);
    [[nodiscard]] bool notifyOnUrgent() const;
    void setNotifyOnUrgent(bool val);
    [[nodiscard]] bool notifyEmailEnabled() const;
    void setNotifyEmailEnabled(bool val);
    [[nodiscard]] QString notifyEmailSmtpServer() const;
    void setNotifyEmailSmtpServer(const QString& val);
    [[nodiscard]] uint16 notifyEmailSmtpPort() const;
    void setNotifyEmailSmtpPort(uint16 val);
    [[nodiscard]] int notifyEmailSmtpAuth() const;
    void setNotifyEmailSmtpAuth(int val);
    [[nodiscard]] bool notifyEmailSmtpTls() const;
    void setNotifyEmailSmtpTls(bool val);
    [[nodiscard]] QString notifyEmailSmtpUser() const;
    void setNotifyEmailSmtpUser(const QString& val);
    [[nodiscard]] QString notifyEmailSmtpPassword() const;
    void setNotifyEmailSmtpPassword(const QString& val);
    [[nodiscard]] QString notifyEmailRecipient() const;
    void setNotifyEmailRecipient(const QString& val);
    [[nodiscard]] QString notifyEmailSender() const;
    void setNotifyEmailSender(const QString& val);

    // -- UI State (persisted window layout) -----------------------------------

    [[nodiscard]] QList<int> serverSplitSizes() const;
    void setServerSplitSizes(const QList<int>& val);

    [[nodiscard]] QList<int> kadSplitSizes() const;
    void setKadSplitSizes(const QList<int>& val);

    [[nodiscard]] QList<int> transferSplitSizes() const;
    void setTransferSplitSizes(const QList<int>& val);

    [[nodiscard]] QList<int> sharedHorzSplitSizes() const;
    void setSharedHorzSplitSizes(const QList<int>& val);

    [[nodiscard]] QList<int> sharedVertSplitSizes() const;
    void setSharedVertSplitSizes(const QList<int>& val);

    [[nodiscard]] QList<int> messagesSplitSizes() const;
    void setMessagesSplitSizes(const QList<int>& val);

    [[nodiscard]] QList<int> ircSplitSizes() const;
    void setIrcSplitSizes(const QList<int>& val);

    [[nodiscard]] QList<int> statsSplitSizes() const;
    void setStatsSplitSizes(const QList<int>& val);

    [[nodiscard]] int windowWidth() const;
    void setWindowWidth(int val);

    [[nodiscard]] int windowHeight() const;
    void setWindowHeight(int val);

    [[nodiscard]] bool windowMaximized() const;
    void setWindowMaximized(bool val);

    [[nodiscard]] QByteArray headerState(const QString& key) const;
    void setHeaderState(const QString& key, const QByteArray& val);

    // -- Factory methods (bridge to existing config structs) -------------------

    /// Build an ObfuscationConfig from current encryption + general settings.
    [[nodiscard]] ObfuscationConfig obfuscationConfig() const;

    /// Build a ProxySettings struct from current proxy settings.
    [[nodiscard]] ProxySettings proxySettings() const;

    // -- Static utilities -----------------------------------------------------

    /// Generate a random TCP port in [4096, 65095].
    [[nodiscard]] static uint16 randomTCPPort();

    /// Generate a random UDP port in [4096, 65095].
    [[nodiscard]] static uint16 randomUDPPort();

    /// Generate a 16-byte user hash with eMule markers at [5]=14 and [14]=111.
    [[nodiscard]] static std::array<uint8, 16> generateUserHash();

private:
    struct Data;

    void validate();
    void resolveDefaultDirectories();
    void resolveDefaultVideoPlayer();
    bool saveImpl(const QString& filePath) const;

    std::unique_ptr<Data> m_data;
    QString m_filePath;
    mutable QReadWriteLock m_lock;
};

/// Global preferences instance.
extern Preferences thePrefs;

} // namespace eMule
