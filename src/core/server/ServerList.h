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

    /// Remove a server by pointer. Returns true if removed.
    bool removeServer(const Server* server);

    /// Remove all servers.
    void removeAllServers();

    /// Remove servers with failedCount >= maxRetries. Returns count removed.
    int removeDeadServers(uint32 maxRetries);

    // -- Lookups ----------------------------------------------------------

    [[nodiscard]] Server* findByIPTcp(uint32 ip, uint16 port) const;
    [[nodiscard]] Server* findByIPUdp(uint32 ip, uint16 udpPort, bool obfuscationPorts = true) const;
    [[nodiscard]] Server* findByAddress(const QString& address, uint16 port) const;

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
    void listReloaded();
    void listSaved();

private:
    std::vector<std::unique_ptr<Server>> m_servers;
    size_t m_serverPos = 0;
    size_t m_searchServerPos = 0;
    size_t m_statServerPos = 0;

    [[nodiscard]] bool isDuplicate(const Server& server) const;
    void adjustPositionsAfterRemoval(size_t removedIndex);
};

} // namespace eMule
