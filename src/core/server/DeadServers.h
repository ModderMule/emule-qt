#pragma once
/// @file DeadServers.h
/// @brief Servers removed as dead, kept out of the list for a while (#161).
///
/// A removed server comes straight back with the next OP_SERVERLIST or server.met merge.
/// Its addresses are remembered here, so such announcements are refused until the block
/// ends. The user's own add lifts it. Saved to deadservers.dat in the config dir, one
/// line per address: `<removed at, epoch secs>\t<address>\t<port>`.

#include "utils/Types.h"

#include <QHash>
#include <QString>

namespace eMule {

class Server;

class DeadServers {
public:
    /// Load @p filePath; expired lines are dropped. An empty path keeps it in memory only.
    void load(const QString& filePath, uint32 now);
    bool save() const;

    /// Block every address of @p server, from @p now on.
    void block(const Server& server, uint32 now);
    /// Lift the block on every address of @p server. True when one was blocked.
    bool unblock(const Server& server);

    /// Any address of @p server blocked within the last @p days? 0 days = never blocked.
    [[nodiscard]] bool isBlocked(const Server& server, uint32 days, uint32 now) const;

    /// Drop blocks older than @p days.
    void prune(uint32 days, uint32 now);

    [[nodiscard]] qsizetype count() const { return m_blocked.size(); }

private:
    QHash<QString, uint32> m_blocked;   ///< key → removed at
    QString m_path;

    [[nodiscard]] static QString key(const QString& address, uint16 port);
    [[nodiscard]] static QStringList keysOf(const Server& server);
};

} // namespace eMule
