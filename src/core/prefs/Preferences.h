#pragma once

/// @file Preferences.h
/// @brief Central preferences with YAML persistence — replaces MFC CPreferences.
///
/// Provides a thread-safe, non-static Preferences class with ~50 essential
/// settings across 9 categories.  Persists to YAML via yaml-cpp.
/// Factory methods bridge to existing config structs (ObfuscationConfig,
/// ProxySettings) used by already-ported modules.

#include "utils/Types.h"

#include <QString>
#include <QReadWriteLock>
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

    // -- Files ----------------------------------------------------------------

    [[nodiscard]] uint16 maxSourcesPerFile() const;
    void setMaxSourcesPerFile(uint16 val);

    [[nodiscard]] bool useICH() const;
    void setUseICH(bool val);

    // -- Transfer -------------------------------------------------------------

    [[nodiscard]] uint32 fileBufferSize() const;
    void setFileBufferSize(uint32 val);

    [[nodiscard]] uint32 fileBufferTimeLimit() const;
    void setFileBufferTimeLimit(uint32 val);

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

    // -- Web Server -----------------------------------------------------------

    [[nodiscard]] bool webServerEnabled() const;
    void setWebServerEnabled(bool val);

    [[nodiscard]] uint16 webServerPort() const;
    void setWebServerPort(uint16 val);

    [[nodiscard]] QString webServerApiKey() const;
    void setWebServerApiKey(const QString& val);

    [[nodiscard]] QString webServerListenAddress() const;
    void setWebServerListenAddress(const QString& val);

    // -- Kademlia -------------------------------------------------------------

    [[nodiscard]] bool kadEnabled() const;
    void setKadEnabled(bool val);

    [[nodiscard]] uint32 kadUDPKey() const;
    void setKadUDPKey(uint32 val);

    // -- Connection -----------------------------------------------------------

    [[nodiscard]] uint16 maxConsPerFive() const;
    void setMaxConsPerFive(uint16 val);

    // -- Server management (extended) -----------------------------------------

    [[nodiscard]] bool addServersFromClients() const;
    void setAddServersFromClients(bool val);

    [[nodiscard]] bool filterServerByIP() const;
    void setFilterServerByIP(bool val);

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
    bool saveImpl(const QString& filePath) const;

    std::unique_ptr<Data> m_data;
    QString m_filePath;
    mutable QReadWriteLock m_lock;
};

/// Global preferences instance.
extern Preferences thePrefs;

} // namespace eMule
