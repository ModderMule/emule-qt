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

    /// Returns true when connected to an ED2K server.
    [[nodiscard]] bool isConnected() const;

    /// Returns true when we have a low (firewalled) ID on the server.
    [[nodiscard]] bool isFirewalled() const;
};

extern AppContext theApp;

} // namespace eMule
