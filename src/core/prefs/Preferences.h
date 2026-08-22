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
#include <QCborMap>
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

    [[nodiscard]] bool skipFirewalledChecksInLanMode() const;
    void setSkipFirewalledChecksInLanMode(bool val);

    // -- Server connection ----------------------------------------------------

    [[nodiscard]] bool safeServerConnect() const;
    void setSafeServerConnect(bool val);

    [[nodiscard]] bool autoConnectStaticOnly() const;
    void setAutoConnectStaticOnly(bool val);

    [[nodiscard]] bool useServerPriorities() const;
    void setUseServerPriorities(bool val);

    [[nodiscard]] bool addServersFromServer() const;
    void setAddServersFromServer(bool val);

    [[nodiscard]] bool useUserSortedServerList() const;
    void setUseUserSortedServerList(bool val);

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

    /// Pin the public IPv6 we advertise, instead of auto-selecting a stable address.
    /// Must be a global-unicast IPv6 literal assigned to a local interface; anything
    /// else is reported and ignored. Independent of bindAddress, which stays IPv4.
    [[nodiscard]] QString publicIPv6Override() const;
    void setPublicIPv6Override(const QString& val);

    // Distinct peers that must independently report the same public IPv6, within the
    // window, before we adopt it (only when no server has observed our egress). YAML-only.
    [[nodiscard]] uint32 ipv6PublicPeerConfirmThreshold() const;
    void setIpv6PublicPeerConfirmThreshold(uint32 val);
    [[nodiscard]] uint32 ipv6PublicPeerConfirmWindowSecs() const;
    void setIpv6PublicPeerConfirmWindowSecs(uint32 val);

    // Distinct ED2K servers that must independently reflect the same IPv4 back to us, within
    // the window, before we adopt it (only when neither Kad nor a session knows it). YAML-only.
    [[nodiscard]] uint32 ipv4PublicServerConfirmThreshold() const;
    void setIpv4PublicServerConfirmThreshold(uint32 val);
    [[nodiscard]] uint32 ipv4PublicServerConfirmWindowSecs() const;
    void setIpv4PublicServerConfirmWindowSecs(uint32 val);

    /// Give IPv6 peers their own share of the upload slots: when clients of both
    /// families are waiting, each freed slot alternates between them instead of going
    /// to the highest score outright. Off restores plain score ordering.
    [[nodiscard]] bool separateIPv6Queue() const;
    void setSeparateIPv6Queue(bool val);

    /// Resolve a server hostname AAAA-first instead of A-first. Off by default: reaching
    /// a server over IPv6 without a routable IPv4 yields a LowID unconditionally, so
    /// preferring AAAA on a dual-stack server would cost a HighID for nothing. The other
    /// family is tried whenever the first returns no records. YAML-only.
    [[nodiscard]] bool serverPreferIPv6() const;
    void setServerPreferIPv6(bool val);

    // -- Bandwidth ------------------------------------------------------------

    /// Raw upload limit in KB/s. 0 means "no limit" — that is what the YAML store,
    /// the IPC prefs map and both GUI limit controls read and write.
    [[nodiscard]] uint32 maxUpload() const;
    void setMaxUpload(uint32 val);

    /// Upload limit in KB/s using MFC's sentinel: UNLIMITED when no limit is set.
    /// MFC normalises 0 to UNLIMITED inside SetMaxUpload (srchybrid/Preferences.cpp:2610),
    /// so every bandwidth path ported from MFC compares against UNLIMITED. Those paths
    /// must read this, not maxUpload() — with the raw 0 they silently degrade to a
    /// zero-byte budget (throttler) or a zero slot cap (upload queue).
    [[nodiscard]] uint32 maxUploadLimit() const;

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

    // Hidden option (MFC CryptLayerRequiredStrict, .ini-only, default false): when set, even
    // unencrypted server LowID/firewall-test callbacks are rejected while require-encryption is on.
    [[nodiscard]] bool cryptLayerRequiredStrict() const;
    void setCryptLayerRequiredStrict(bool val);

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

    [[nodiscard]] bool closeUPnPOnExit() const;
    void setCloseUPnPOnExit(bool val);

    /// Enabled port-mapping protocols, as a bitmask: 1 = PCP, 2 = NAT-PMP,
    /// 4 = UPnP. One mask rather than three booleans keeps the IPC and GUI
    /// plumbing to a single key.
    [[nodiscard]] uint32 portMapProtocols() const;
    void setPortMapProtocols(uint32 val);

    /// Lease length requested from the router, in seconds. Routers routinely
    /// grant less; the granted value is what gets renewed.
    [[nodiscard]] uint32 portMapLeaseSecs() const;
    void setPortMapLeaseSecs(uint32 val);

    /// Also open IPv6 firewall pinholes. On a CGNAT line this is the only path
    /// to real inbound reachability, so it defaults on.
    [[nodiscard]] bool portMapIPv6() const;
    void setPortMapIPv6(bool val);

    /// Learned state, not a user knob: the protocol that won the race last run,
    /// tried first on the next start. Values match PortMapMethod.
    [[nodiscard]] int portMapMethod() const;
    void setPortMapMethod(int val);

    /// Learned state, not a user knob: a per-install random secret (hex) that
    /// PCP mapping nonces are derived from. It has to survive restarts, because
    /// the nonce is what owns a mapping — coming back with a fresh one leaves
    /// the router refusing to renew or delete mappings we still hold.
    [[nodiscard]] QString portMapSecret() const;
    void setPortMapSecret(const QString& val);

    // -- Logging --------------------------------------------------------------

    /// Write emulecored.log, emulecored_Verbose.log and emulecored_Kad.log in the
    /// config directory. One switch per process: the daemon and the GUI keep
    /// separate files, so a line's origin is never in doubt. @see logToDiskGui
    [[nodiscard]] bool logToDiskCore() const;
    void setLogToDiskCore(bool val);

    /// Write emuleqt.log, emuleqt_Verbose.log and emuleqt_Kad.log in the config
    /// directory (the Kad file stays empty — Kad runs in the daemon).
    /// Set by the GUI but persisted by the daemon, which owns preferences.yml.
    [[nodiscard]] bool logToDiskGui() const;
    void setLogToDiskGui(bool val);

    [[nodiscard]] uint32 maxLogFileSize() const;
    void setMaxLogFileSize(uint32 val);

    [[nodiscard]] bool verbose() const;
    void setVerbose(bool val);

    [[nodiscard]] bool logPublicIP() const;
    void setLogPublicIP(bool val);

    [[nodiscard]] bool kadVerboseLog() const;
    void setKadVerboseLog(bool val);

    [[nodiscard]] bool serverVerboseLog() const;
    void setServerVerboseLog(bool val);

    [[nodiscard]] uint32 maxLogLines() const;
    void setMaxLogLines(uint32 val);

    [[nodiscard]] int logLevel() const;
    void setLogLevel(int val);

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

    [[nodiscard]] bool logRawSocketPackets() const;
    void setLogRawSocketPackets(bool val);

    [[nodiscard]] bool logWebServer() const;
    void setLogWebServer(bool val);

    [[nodiscard]] bool enableIpcLog() const;
    void setEnableIpcLog(bool val);

    [[nodiscard]] bool startCoreWithConsole() const;
    void setStartCoreWithConsole(bool val);

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

    /// Append our public IPv6 as an `s6=` source hint in generated eD2K links.
    /// Gated additionally by AppContext::shouldAdvertisePublicIPv6().
    [[nodiscard]] bool ed2kLinkAdvertiseIPv6() const;
    void setEd2kLinkAdvertiseIPv6(bool val);

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

    /// Seconds between two writes of the cumulative statistics; 0 = only at
    /// shutdown. MFC: [Statistics] SaveInterval, ini-only, default 60.
    [[nodiscard]] uint32 statsSaveInterval() const;
    void setStatsSaveInterval(uint32 val);

    /// Wall-clock second at which the statistics were last reset; 0 = never
    /// (MFC: statsDateTimeLastReset).
    [[nodiscard]] uint64 statsLastReset() const;
    void setStatsLastReset(uint64 val);

    // -- Cumulative Statistics ------------------------------------------------

    // Transfer totals
    [[nodiscard]] uint64 cumTotalUploaded() const;
    void setCumTotalUploaded(uint64 val);
    [[nodiscard]] uint64 cumTotalDownloaded() const;
    void setCumTotalDownloaded(uint64 val);
    [[nodiscard]] uint64 cumTotalUploadedToFriend() const;
    void setCumTotalUploadedToFriend(uint64 val);

    // Upload sessions
    [[nodiscard]] uint32 cumUpSuccessfulSessions() const;
    void setCumUpSuccessfulSessions(uint32 val);
    [[nodiscard]] uint32 cumUpFailedSessions() const;
    void setCumUpFailedSessions(uint32 val);
    [[nodiscard]] uint32 cumUpAvgTime() const;
    void setCumUpAvgTime(uint32 val);

    // Download sessions
    [[nodiscard]] uint32 cumDownSuccessfulSessions() const;
    void setCumDownSuccessfulSessions(uint32 val);
    [[nodiscard]] uint32 cumDownFailedSessions() const;
    void setCumDownFailedSessions(uint32 val);
    [[nodiscard]] uint32 cumDownCompletedFiles() const;
    void setCumDownCompletedFiles(uint32 val);
    [[nodiscard]] uint32 cumDownAvgTime() const;
    void setCumDownAvgTime(uint32 val);

    // Cumulative overhead — upload (bytes + packets)
    [[nodiscard]] uint64 cumUpOverheadTotal() const;
    void setCumUpOverheadTotal(uint64 val);
    [[nodiscard]] uint64 cumUpOverheadTotalPackets() const;
    void setCumUpOverheadTotalPackets(uint64 val);
    [[nodiscard]] uint64 cumUpOverheadFileReq() const;
    void setCumUpOverheadFileReq(uint64 val);
    [[nodiscard]] uint64 cumUpOverheadFileReqPackets() const;
    void setCumUpOverheadFileReqPackets(uint64 val);
    [[nodiscard]] uint64 cumUpOverheadSrcExch() const;
    void setCumUpOverheadSrcExch(uint64 val);
    [[nodiscard]] uint64 cumUpOverheadSrcExchPackets() const;
    void setCumUpOverheadSrcExchPackets(uint64 val);
    [[nodiscard]] uint64 cumUpOverheadServer() const;
    void setCumUpOverheadServer(uint64 val);
    [[nodiscard]] uint64 cumUpOverheadServerPackets() const;
    void setCumUpOverheadServerPackets(uint64 val);
    [[nodiscard]] uint64 cumUpOverheadKad() const;
    void setCumUpOverheadKad(uint64 val);
    [[nodiscard]] uint64 cumUpOverheadKadPackets() const;
    void setCumUpOverheadKadPackets(uint64 val);

    // Cumulative overhead — download (bytes + packets)
    [[nodiscard]] uint64 cumDownOverheadTotal() const;
    void setCumDownOverheadTotal(uint64 val);
    [[nodiscard]] uint64 cumDownOverheadTotalPackets() const;
    void setCumDownOverheadTotalPackets(uint64 val);
    [[nodiscard]] uint64 cumDownOverheadFileReq() const;
    void setCumDownOverheadFileReq(uint64 val);
    [[nodiscard]] uint64 cumDownOverheadFileReqPackets() const;
    void setCumDownOverheadFileReqPackets(uint64 val);
    [[nodiscard]] uint64 cumDownOverheadSrcExch() const;
    void setCumDownOverheadSrcExch(uint64 val);
    [[nodiscard]] uint64 cumDownOverheadSrcExchPackets() const;
    void setCumDownOverheadSrcExchPackets(uint64 val);
    [[nodiscard]] uint64 cumDownOverheadServer() const;
    void setCumDownOverheadServer(uint64 val);
    [[nodiscard]] uint64 cumDownOverheadServerPackets() const;
    void setCumDownOverheadServerPackets(uint64 val);
    [[nodiscard]] uint64 cumDownOverheadKad() const;
    void setCumDownOverheadKad(uint64 val);
    [[nodiscard]] uint64 cumDownOverheadKadPackets() const;
    void setCumDownOverheadKadPackets(uint64 val);

    // Cumulative connection stats
    [[nodiscard]] uint32 cumConnPeak() const;
    void setCumConnPeak(uint32 val);
    [[nodiscard]] uint32 cumConnMaxLimitReached() const;
    void setCumConnMaxLimitReached(uint32 val);
    [[nodiscard]] uint32 cumConnReconnects() const;
    void setCumConnReconnects(uint32 val);

    // Cumulative times
    [[nodiscard]] uint64 cumRunTime() const;
    void setCumRunTime(uint64 val);
    [[nodiscard]] uint64 cumTransferTime() const;
    void setCumTransferTime(uint64 val);
    [[nodiscard]] uint64 cumUploadTime() const;
    void setCumUploadTime(uint64 val);
    [[nodiscard]] uint64 cumDownloadTime() const;
    void setCumDownloadTime(uint64 val);
    [[nodiscard]] uint64 cumServerDuration() const;
    void setCumServerDuration(uint64 val);

    // Cumulative quality stats
    [[nodiscard]] uint64 cumCompressionGain() const;
    void setCumCompressionGain(uint64 val);
    [[nodiscard]] uint64 cumCorruptionLoss() const;
    void setCumCorruptionLoss(uint64 val);
    [[nodiscard]] uint32 cumIchPartsSaved() const;
    void setCumIchPartsSaved(uint32 val);

    // Per-client breakdown — cumulative upload bytes
    [[nodiscard]] uint64 cumUpEmule() const;
    void setCumUpEmule(uint64 val);
    [[nodiscard]] uint64 cumUpEDHybrid() const;
    void setCumUpEDHybrid(uint64 val);
    [[nodiscard]] uint64 cumUpEDonkey() const;
    void setCumUpEDonkey(uint64 val);
    [[nodiscard]] uint64 cumUpAMule() const;
    void setCumUpAMule(uint64 val);
    [[nodiscard]] uint64 cumUpMLdonkey() const;
    void setCumUpMLdonkey(uint64 val);
    [[nodiscard]] uint64 cumUpShareaza() const;
    void setCumUpShareaza(uint64 val);
    [[nodiscard]] uint64 cumUpEMCompat() const;
    void setCumUpEMCompat(uint64 val);

    // Per-client breakdown — cumulative download bytes
    [[nodiscard]] uint64 cumDownEmule() const;
    void setCumDownEmule(uint64 val);
    [[nodiscard]] uint64 cumDownEDHybrid() const;
    void setCumDownEDHybrid(uint64 val);
    [[nodiscard]] uint64 cumDownEDonkey() const;
    void setCumDownEDonkey(uint64 val);
    [[nodiscard]] uint64 cumDownAMule() const;
    void setCumDownAMule(uint64 val);
    [[nodiscard]] uint64 cumDownMLdonkey() const;
    void setCumDownMLdonkey(uint64 val);
    [[nodiscard]] uint64 cumDownShareaza() const;
    void setCumDownShareaza(uint64 val);
    [[nodiscard]] uint64 cumDownEMCompat() const;
    void setCumDownEMCompat(uint64 val);
    [[nodiscard]] uint64 cumDownURL() const;
    void setCumDownURL(uint64 val);

    // Per-port breakdown — cumulative bytes
    [[nodiscard]] uint64 cumUpPort4662() const;
    void setCumUpPort4662(uint64 val);
    [[nodiscard]] uint64 cumUpPortOther() const;
    void setCumUpPortOther(uint64 val);
    [[nodiscard]] uint64 cumDownPort4662() const;
    void setCumDownPort4662(uint64 val);
    [[nodiscard]] uint64 cumDownPortOther() const;
    void setCumDownPortOther(uint64 val);

    // Per-source breakdown — cumulative upload bytes
    [[nodiscard]] uint64 cumUpFromFile() const;
    void setCumUpFromFile(uint64 val);
    [[nodiscard]] uint64 cumUpFromPartfile() const;
    void setCumUpFromPartfile(uint64 val);

    // HTTP Cache — cumulative totals. Written absolutely by
    // Statistics::flushCumulativeToPrefs, never incremented in place.
    [[nodiscard]] uint64 cumHttpCacheBytesPublished() const;
    void setCumHttpCacheBytesPublished(uint64 val);
    [[nodiscard]] uint64 cumHttpCacheBytesFetched() const;
    void setCumHttpCacheBytesFetched(uint64 val);
    [[nodiscard]] uint64 cumHttpCacheBytesSaved() const;
    void setCumHttpCacheBytesSaved(uint64 val);
    [[nodiscard]] uint32 cumHttpCacheChunksPublished() const;
    void setCumHttpCacheChunksPublished(uint32 val);
    [[nodiscard]] uint32 cumHttpCacheChunksFetched() const;
    void setCumHttpCacheChunksFetched(uint32 val);

    // Records
    [[nodiscard]] uint32 recMaxWorkingServers() const;
    void setRecMaxWorkingServers(uint32 val);
    [[nodiscard]] uint32 recMaxUsersOnline() const;
    void setRecMaxUsersOnline(uint32 val);
    [[nodiscard]] uint32 recMaxFilesAvail() const;
    void setRecMaxFilesAvail(uint32 val);
    [[nodiscard]] uint64 recMaxSharedFiles() const;
    void setRecMaxSharedFiles(uint64 val);
    [[nodiscard]] uint64 recMaxSharedSize() const;
    void setRecMaxSharedSize(uint64 val);
    [[nodiscard]] uint64 recMaxAvgFileSize() const;
    void setRecMaxAvgFileSize(uint64 val);
    [[nodiscard]] uint64 recMaxLargestFile() const;
    void setRecMaxLargestFile(uint64 val);

    /// Zero every cumulative counter above and stamp statsLastReset with @p now.
    /// The records (rec*) and the session counters are left alone, as in MFC's
    /// CPreferences::ResetCumulativeStatistics.
    void resetCumulativeStats(uint64 now);

    /// Absolute path of the statistics backup, a sibling of the preferences file
    /// (MFC: statbkup.ini in the config directory).
    [[nodiscard]] QString cumulativeStatsBackupPath() const;

    /// True when a backup exists, i.e. when a restore is possible. The Restore
    /// Statistics menu item is greyed out when this is false, as in MFC
    /// (srchybrid/StatisticsTree.cpp:127).
    [[nodiscard]] bool hasCumulativeStatsBackup() const;

    /// Write the current cumulative counters, the records and statsLastReset to
    /// the backup file, so that a following resetCumulativeStats() can be undone.
    /// MFC does this from ResetCumulativeStatistics via SaveStats(1).
    bool backupCumulativeStats() const;

    /// Load the backup back into the cumulative counters. Returns false when there
    /// is no backup to load. The current values become the new backup, so calling
    /// this twice undoes the restore — MFC's statbkuptmp.ini rename
    /// (srchybrid/Preferences.cpp:1330-1334).
    bool restoreCumulativeStats();

    // -- HTTP Cache -----------------------------------------------------------
    //
    // Encrypted chunk offload (docs/protocol/http-cache-spec.md). Deliberately
    // YAML-only for now — no Options page — while the design is still moving.
    // Edit $HOME/eMuleQt/Config/preferences.yml under the `httpCache:` key.

    /// Master switch. Off means we neither publish nor accept offers, and the
    /// capability bit we advertise becomes a promise we simply never act on.
    [[nodiscard]] bool httpCacheEnabled() const;
    void setHttpCacheEnabled(bool val);

    /// Accept OP_HTTPCACHE offers from peers and fetch over HTTP.
    [[nodiscard]] bool httpCacheAllowDownload() const;
    void setHttpCacheAllowDownload(bool val);

    /// Publish chunks. Also needs a base URL and an API key to do anything.
    [[nodiscard]] bool httpCacheAllowUpload() const;
    void setHttpCacheAllowUpload(bool val);

    /// Cache server root, e.g. "http://localhost/emule-http-cache-php".
    [[nodiscard]] QString httpCacheBaseUrl() const;
    void setHttpCacheBaseUrl(const QString& val);

    /// Upload credential. Stored AES-encrypted in the YAML.
    [[nodiscard]] QString httpCacheApiKey() const;
    void setHttpCacheApiKey(const QString& val);

    /// How many peers must want the same part before it is worth publishing.
    /// The feature's whole premise is one upload serving many, so the default is
    /// 2; set it to 1 only to exercise the path with a single peer.
    [[nodiscard]] uint32 httpCacheMinClients() const;
    void setHttpCacheMinClients(uint32 val);

    /// TTL requested from the cache server, in seconds.
    [[nodiscard]] uint32 httpCacheChunkTtlSeconds() const;
    void setHttpCacheChunkTtlSeconds(uint32 val);

    /// Ceiling on bytes published per day, so a misconfigured node cannot burn
    /// an operator's quota overnight.
    [[nodiscard]] uint64 httpCacheMaxPublishBytesPerDay() const;
    void setHttpCacheMaxPublishBytesPerDay(uint64 val);

    /// Publish rate cap in KB/s. 0 derives one from the upload limit, so the
    /// offload never starves the ed2k uploads it is meant to relieve.
    [[nodiscard]] uint32 httpCachePublishRateKBs() const;
    void setHttpCachePublishRateKBs(uint32 val);

    [[nodiscard]] uint32 httpCacheMaxConcurrentPublishes() const;
    void setHttpCacheMaxConcurrentPublishes(uint32 val);

    [[nodiscard]] uint32 httpCacheMaxConcurrentFetches() const;
    void setHttpCacheMaxConcurrentFetches(uint32 val);

    // -- Security -------------------------------------------------------------

    [[nodiscard]] uint32 ipFilterLevel() const;
    void setIpFilterLevel(uint32 val);

    [[nodiscard]] bool warnUntrustedFiles() const;
    void setWarnUntrustedFiles(bool val);

    [[nodiscard]] QString ipFilterUpdateUrl() const;
    void setIpFilterUpdateUrl(const QString& val);

    [[nodiscard]] bool useSafeKad() const;
    void setUseSafeKad(bool val);

    [[nodiscard]] bool useFastKad() const;
    void setUseFastKad(bool val);

    [[nodiscard]] QString appToken() const;
    void setAppToken(const QString& val);

    [[nodiscard]] QString bugReportApiKey() const;
    [[nodiscard]] QString bugReportDomain() const;

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

    // Cached Kad notes-search filenames/comments on the File Details page (core-only).
    [[nodiscard]] int kadFileNameExpiryDays() const;
    void setKadFileNameExpiryDays(int val);

    [[nodiscard]] int kadFileNameMaxCount() const;
    void setKadFileNameMaxCount(int val);

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

    /// Save/Load Sources — write each download's best sources to a MorphXT-compatible
    /// `<TempDir>/Source Lists/*.txtsrc` and restore them on the next run.
    [[nodiscard]] bool useSaveLoadSources() const;
    void setUseSaveLoadSources(bool val);

    /// Upload Queue Storage — write the top waiting uploaders to
    /// `<ConfigDir>/uploadqueue.met` and restore their queue positions on the next run.
    [[nodiscard]] bool rememberUploadQueue() const;
    void setRememberUploadQueue(bool val);

    // -- Disk space -----------------------------------------------------------

    [[nodiscard]] bool checkDiskspace() const;
    void setCheckDiskspace(bool val);

    [[nodiscard]] uint64 minFreeDiskSpace() const;
    void setMinFreeDiskSpace(uint64 val);

    // -- Search ---------------------------------------------------------------

    [[nodiscard]] bool enableSearchResultFilter() const;
    void setEnableSearchResultFilter(bool val);

    // Our public IP lives on AppContext (theApp.publicIP()), not here: it is
    // session state that must be re-derived each run, and it has a Kad source
    // that Preferences cannot reach. Persisting it made hasValidUDPKey() match
    // server UDP keys issued for an IP we no longer hold. MFC keeps it on
    // CemuleApp for the same reason.

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
    // The timestamp of the last check lives in UiState, not here: the daemon owns
    // preferences.yml but never runs a check, so only the GUI can record one.

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

    [[nodiscard]] bool showSpeedGraph() const;
    void setShowSpeedGraph(bool val);

    [[nodiscard]] uint32 speedGraphTimeRangeMin() const;
    void setSpeedGraphTimeRangeMin(uint32 val);

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

    // -- IPC sync -------------------------------------------------------------

    /// Overwrite all daemon-owned settings from the CBOR map returned by the
    /// daemon's GetPreferences IPC response.  Called once at IPC connect time
    /// so the GUI's thePrefs always reflects live daemon values.
    void updateFromCbor(const QCborMap& prefs);

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

    /// Generate a 32-char hex REST API key (128-bit random).
    [[nodiscard]] static QString generateApiKey();

private:
    struct Data;

    // Thread-safe accessors for trivial preference fields. Defined in the .cpp
    // where Data is complete; all instantiations live in that single TU, so the
    // Data layout stays fully encapsulated. Locking lives here in one place.
    template<class T> [[nodiscard]] T get(T Data::*member) const;
    template<class T> void           set(T Data::*member, const T& value);

    /// Visit every field a statistics reset clears — the cumulative counters plus
    /// the six cumulative rate records — as (key, reference) pairs. Reset, backup
    /// and restore all walk this, so the field list exists in exactly one place.
    /// @p D is Data or const Data, so one walk serves both the mutating visitors
    /// and the const one.
    template<class D, class F> static void forEachCumulativeStat(D& d, F&& f);
    /// Visit the all-time records, which a reset leaves alone and a restore merges
    /// with max() so a record set after the reset is not thrown away (MFC's
    /// "Smart Load For Restores", srchybrid/Preferences.cpp:1299).
    template<class D, class F> static void forEachStatRecord(D& d, F&& f);

    /// Write the cumulative block of @p d to @p filePath. Caller holds the lock.
    static bool writeStatsBackup(const Data& d, const QString& filePath);

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
