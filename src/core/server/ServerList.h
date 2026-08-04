#pragma once

/// @file ServerList.h
/// @brief ED2K server collection — modern C++23 replacement for MFC CServerList.
///
/// QObject-derived with Qt signals for decoupled notifications.
/// Replaces CTypedPtrList<CPtrList, CServer*> with std::vector<std::unique_ptr<Server>>.

#include "Server.h"
#include "utils/Types.h"

#include <QObject>
#include <QString>

#include <memory>
#include <vector>

namespace eMule {

struct ServerListStats {
    uint32 total = 0;
    uint32 failed = 0;
    uint32 users = 0;
    uint32 files = 0;
    uint32 lowIDUsers = 0;
};

class ServerList : public QObject {
    Q_OBJECT

public:
    explicit ServerList(QObject* parent = nullptr);
    ~ServerList() override = default;

    // -- Persistence ------------------------------------------------------

    /// Load servers from a server.met binary file. Returns true on success.
    bool loadServerMet(const QString& filePath);

    /// Save servers to a server.met binary file. Returns true on success.
    bool saveServerMet(const QString& filePath);

    /// Load/merge additional servers from a server.met file.
    bool addServerMetToList(const QString& filePath, bool merge);

    /// Load static servers from a text file (host:port,priority,name format).
    bool loadStaticServers(const QString& filePath);

    /// Save static servers to a text file.
    bool saveStaticServers(const QString& filePath) const;

    /// Import servers from a text file (ip:port lines or ed2k links).
    int addServersFromTextFile(const QString& filePath);

    // -- Add/Remove -------------------------------------------------------

    /// Add a server. Returns raw pointer if added, nullptr on duplicate/bad IP.
    /// Takes ownership of the server.
    Server* addServer(std::unique_ptr<Server> server);

    /// Parse an OP_SERVERLIST (0x32) payload — uint8 count, [ip4 port2]*count —
    /// and add each as a Low-priority server (dedup/IP-validity via addServer).
    /// Port of the OP_SERVERLIST case in CServerSocket::ProcessPacket.
    void addServersFromPacket(const uint8* data, uint32 size);

    /// Remove a server by pointer. Returns true if removed.
    bool removeServer(const Server* server);

    /// Remove all servers.
    void removeAllServers();

    /// Remove servers with failedCount >= maxRetries. Returns count removed.
    int removeDeadServers(uint32 maxRetries);

    /// Remove every other entry sharing `except`'s address string and port —
    /// used after a dynIP server resolves so stale duplicates don't accumulate.
    /// Port of CServerList::RemoveDuplicatesByAddress().
    void removeDuplicatesByAddress(const Server* except);

    /// Emit serverUpdated(server) so views refresh after an in-place mutation.
    void notifyServerUpdated(Server* server) { emit serverUpdated(server); }

    // -- Lookups ----------------------------------------------------------

    [[nodiscard]] Server* findByIPTcp(uint32 ip, uint16 port) const;
    /// Same lookup for an address of either family. findByIPTcp() delegates here;
    /// an IPv6 server can only be found this way (its uint32 form is 0).
    [[nodiscard]] Server* findByIPTcp(const Address& addr, uint16 port) const;
    [[nodiscard]] Server* findByIPUdp(uint32 ip, uint16 udpPort, bool obfuscationPorts = true) const;
    /// Same lookup for an address of either family. findByIPUdp(uint32) delegates here;
    /// prefer this form when the caller has an Endpoint, or an IPv6 server's reply
    /// would be attributed to whichever entry happens to have a null address.
    [[nodiscard]] Server* findByIPUdp(const Address& addr, uint16 udpPort,
                                      bool obfuscationPorts = true) const;
    [[nodiscard]] Server* findByAddress(const QString& address, uint16 port) const;

    /// IP-only lookup (ignores port). Port of CServerList::GetServerByIP().
    [[nodiscard]] Server* getServerByIP(uint32 ip) const;

    /// The server after `last` in list order, or nullptr past the tail (or when
    /// `last` is no longer present). Unlike nextStatServer() this does NOT wrap, so
    /// a rotation over it terminates after one pass. Port of GetSuccServer().
    /// Passing nullptr returns the first server.
    [[nodiscard]] Server* getSuccServer(const Server* last) const;

    /// Move a server to the bottom of the list (used by the GUI multi-connect flow
    /// to deprioritize full servers). Port of CServerList::MoveServerDown().
    void moveServerDown(const Server* server);

    /// Reorder the list to the GUI-supplied (ip, port) sequence; any server not in
    /// `order` keeps its relative position appended at the tail. This is the
    /// daemon-side of the user-sorted-server-list feature (#24) — persisted via
    /// saveServerMet, which writes m_servers in order.
    void applyUserOrder(const std::vector<std::pair<Address, uint16>>& order);

    // -- Iteration --------------------------------------------------------

    [[nodiscard]] size_t serverCount() const { return m_servers.size(); }
    [[nodiscard]] Server* serverAt(size_t index) const;
    [[nodiscard]] const std::vector<std::unique_ptr<Server>>& servers() const { return m_servers; }

    // -- Round-robin iterators --------------------------------------------

    [[nodiscard]] Server* nextServer(bool tryObfuscated = false);
    [[nodiscard]] Server* nextSearchServer();
    [[nodiscard]] Server* nextStatServer();

    void setServerPosition(size_t pos);
    void resetSearchServerPos() { m_searchServerPos = 0; }
    void resetStatServerPos()   { m_statServerPos = 0; }

    // -- UDP server status (OP_GLOBSERVSTATREQ / OP_GLOBSERVSTATRES) -------

    /// Send an OP_GLOBSERVSTATREQ ping to the next due server in the list.
    /// Rate-limited by the caller; each server is re-pinged at most every
    /// UDPSERVSTATREASKTIME. Port of CServerList::ServerStats().
    void serverStats();

    /// Periodic tick: persist server.met to `metPath` at most every 17 minutes so
    /// list state (users/files/ping/failedCount/UDP keys) survives a kill/crash,
    /// not just a clean shutdown. Port of CServerList::Process() — ServerList.cpp:849.
    void process(const QString& metPath);

    /// Parse an OP_GLOBSERVSTATRES (0x97) reply and update the matching server.
    /// Port of the OP_GLOBSERVSTATRES case in CUDPSocket::ProcessPacket.
    void processStatusResponse(const uint8* data, uint32 size, const Endpoint& from);

    /// Parse an OP_SERVER_DESC_RES (0xA3) reply and refresh the matching server's
    /// name/description/version/dynIP. Port of the OP_SERVER_DESC_RES case in
    /// CUDPSocket::ProcessPacket.
    void processDescResponse(const uint8* data, uint32 size, const Endpoint& from);

    // -- Aggregate stats --------------------------------------------------

    [[nodiscard]] ServerListStats stats() const;

    // -- Sorting ----------------------------------------------------------

    void sortByPreference();

    // -- Crypto key management --------------------------------------------

    void checkForExpiredUDPKeys(uint32 currentClientIP);

    // -- IP validation ----------------------------------------------------

    [[nodiscard]] static bool isGoodServerIP(const Server& server);

signals:
    void serverAdded(Server* server);
    void serverAboutToBeRemoved(const Server* server);
    void serverUpdated(Server* server);
    void listReloaded();
    void listSaved();

private:
    std::vector<std::unique_ptr<Server>> m_servers;
    size_t m_serverPos = 0;
    size_t m_searchServerPos = 0;
    size_t m_statServerPos = 0;
    uint32 m_lastServerMetSave = 0;   ///< epoch-secs of last periodic save (#28)

    [[nodiscard]] bool isDuplicate(const Server& server) const;
    void adjustPositionsAfterRemoval(size_t removedIndex);

    /// Validate the IPv4 a server reflected back at us in the trailing field of a
    /// challenge-matched OP_GLOBSERVSTATRES and, if it survives, cast one vote for it.
    /// @p raw is the wire value (ed2k ID convention, first octet in the LSB).
    static void noteObservedIPv4(const Server& server, const Endpoint& from, uint32 raw);
};

} // namespace eMule
