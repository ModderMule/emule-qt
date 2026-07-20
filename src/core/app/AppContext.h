#pragma once

/// @file AppContext.h
/// @brief Global application context — provides access to all manager services.
///
/// Follows the same pattern as `extern Preferences thePrefs`.
/// Each manager pointer is set during application startup and remains
/// valid for the lifetime of the process.

#include "utils/Types.h"

namespace eMule {

// Forward declarations
class ClientCreditsList;
class ClientList;
class ClientUDPSocket;
class DownloadQueue;
class FriendList;
class IPFilter;
class KnownFileList;
class ListenSocket;
class SearchList;
class ServerConnect;
class ServerList;
class SharedFileList;
class Statistics;
class Scheduler;
class LastCommonRouteFinder;
class UPnPManager;
class UploadBandwidthThrottler;
class UploadQueue;

struct AppContext {
    ClientList*      clientList     = nullptr;
    ClientCreditsList* clientCredits = nullptr;
    ClientUDPSocket* clientUDP      = nullptr;
    DownloadQueue*   downloadQueue  = nullptr;
    UploadQueue*     uploadQueue    = nullptr;
    SharedFileList*  sharedFileList = nullptr;
    KnownFileList*   knownFileList  = nullptr;
    IPFilter*        ipFilter       = nullptr;
    ListenSocket*    listenSocket   = nullptr;
    FriendList*      friendList     = nullptr;
    ServerConnect*   serverConnect  = nullptr;
    ServerList*      serverList     = nullptr;
    SearchList*      searchList     = nullptr;
    Statistics*      statistics     = nullptr;
    UploadBandwidthThrottler* uploadBandwidthThrottler = nullptr;
    LastCommonRouteFinder* lastCommonRouteFinder = nullptr;
    Scheduler*   scheduler      = nullptr;
    UPnPManager* upnpManager    = nullptr;

    /// Returns our server-assigned client ID (0 if not connected).
    [[nodiscard]] uint32 getID() const;

    /// Returns true when connected to any network — an ED2K server *or* Kad.
    /// Mirrors MFC CemuleApp::IsConnected() (emule.cpp:1122).
    /// Callers that specifically mean "connected to an ED2K server" (GUI status
    /// indicators, the Connect button, connection-lost notifications) must use
    /// serverConnect->isConnected() directly instead.
    [[nodiscard]] bool isConnected() const;

    /// Returns true when we are firewalled on all connected networks (ed2k + Kad).
    [[nodiscard]] bool isFirewalled() const;

    /// Our public IP in ED2K byte order (first octet in the LSB), 0 if unknown.
    ///
    /// Priority is Kad -> ED2K server -> peer. Kad's IP wins whenever it has one
    /// because KadPrefs::setIPAddress() only commits a value confirmed by two
    /// independent nodes, whereas a server's claim is a single unverified
    /// assertion. Pass @p ignoreKadIP to read only the ED2K-derived value —
    /// callers that must agree with what a *specific* server sees need that.
    ///
    /// MFC: CemuleApp::GetPublicIP() — Emule.cpp:1542. Note the priority there is
    /// the other way round: the stored ED2K value wins and Kad is only a fallback.
    [[nodiscard]] uint32 publicIP(bool ignoreKadIP = false) const;

    /// Records an ED2K-derived public IP (HighID, server-reported IP, or a peer's
    /// OP_PUBLICIP answer). Pass 0 to clear it on server disconnect.
    /// MFC: CemuleApp::SetPublicIP() — Emule.cpp:1548.
    void setPublicIP(uint32 ip);

    /// Re-checks server UDP keys after something *other* than setPublicIP()
    /// changed our effective public IP — in practice, Kad learning a new one.
    /// MFC has no equivalent because Kad never outranks the stored value there.
    void onEffectivePublicIPChanged(uint32 newIP);

private:
    /// ED2K-derived public IP only; publicIP() layers the Kad source on top.
    /// Deliberately not persisted — it is session state, as in MFC.
    uint32 m_publicIP = 0;
};

extern AppContext theApp;

} // namespace eMule
