/// @file ServerList.cpp
/// @brief ED2K server collection implementation — port of CServerList from MFC.

#include "ServerList.h"
#include "protocol/ED2KLink.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QTextStream>

#include <algorithm>

namespace eMule {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ServerList::ServerList(QObject* parent)
    : QObject(parent)
{
}

// ---------------------------------------------------------------------------
// Persistence — server.met binary format
// ---------------------------------------------------------------------------

bool ServerList::loadServerMet(const QString& filePath)
{
    removeAllServers();
    if (!addServerMetToList(filePath, false))
        return false;
    emit listReloaded();
    return true;
}

bool ServerList::saveServerMet(const QString& filePath)
{
    const QString tmpPath = filePath + QStringLiteral(".tmp");

    SafeFile file;
    if (!file.open(tmpPath, QIODevice::WriteOnly)) {
        logError(QStringLiteral("Failed to open %1 for writing").arg(tmpPath));
        return false;
    }

    try {
        file.writeUInt8(MET_HEADER_I64TAGS);
        file.writeUInt32(static_cast<uint32>(m_servers.size()));

        for (const auto& srv : m_servers) {
            // Don't write potentially outdated IPs of dynIP servers
            file.writeUInt32(srv->hasDynIP() ? 0 : srv->ip());
            file.writeUInt16(srv->port());

            // Write tag count placeholder, then tags, then fix count
            const qint64 tagCountPos = file.position();
            file.writeUInt32(0);

            const uint32 tagCount = srv->writeTags(file);

            // Seek back and write actual tag count
            const qint64 endPos = file.position();
            file.seek(tagCountPos, 0);
            file.writeUInt32(tagCount);
            file.seek(endPos, 0);
        }

        file.close();
    } catch (const FileException& ex) {
        logError(QStringLiteral("Error saving server.met: %1").arg(ex.what()));
        QFile::remove(tmpPath);
        return false;
    }

    // Atomic rename: remove old, rename tmp → final
    QFile::remove(filePath);
    if (!QFile::rename(tmpPath, filePath)) {
        logError(QStringLiteral("Failed to rename %1 to %2").arg(tmpPath, filePath));
        return false;
    }

    emit listSaved();
    return true;
}

bool ServerList::addServerMetToList(const QString& filePath, bool merge)
{
    SafeFile file;
    if (!file.open(filePath, QIODevice::ReadOnly)) {
        if (!merge)
            logError(QStringLiteral("Failed to open server.met: %1").arg(filePath));
        return false;
    }

    try {
        const uint8 header = file.readUInt8();
        if (header != MET_HEADER && header != MET_HEADER_I64TAGS && header != 0xE0) {
            logError(QStringLiteral("Bad server.met header: 0x%1").arg(header, 2, 16, QChar(u'0')));
            return false;
        }

        const uint32 serverCount = file.readUInt32();
        for (uint32 j = 0; j < serverCount; ++j) {
            auto srv = std::make_unique<Server>(file, true);

            if (merge)
                srv->setPreference(ServerPriority::Normal);

            if (srv->name().isEmpty())
                srv->setName(srv->address());

            addServer(std::move(srv));
        }
    } catch (const FileException& ex) {
        logError(QStringLiteral("Error reading server.met: %1").arg(ex.what()));
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Static servers — text format
// ---------------------------------------------------------------------------

bool ServerList::loadStaticServers(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream stream(&file);

    QString line;
    while (stream.readLineInto(&line)) {
        if (line.size() < 5)
            continue;
        if (line.startsWith(u'#') || line.startsWith(u'/'))
            continue;

        // Skip BOM if present
        if (line.startsWith(QChar(0xFEFF)))
            line = line.mid(1);

        // Format: host:port,priority,Name
        qsizetype colonPos = line.indexOf(u':');
        if (colonPos < 0) {
            colonPos = line.indexOf(u',');
            if (colonPos < 0)
                continue;
        }
        const QString host = line.left(colonPos);
        line = line.mid(colonPos + 1);

        qsizetype commaPos = line.indexOf(u',');
        if (commaPos < 0)
            continue;
        const uint16 nPort = static_cast<uint16>(line.left(commaPos).toUInt());
        line = line.mid(commaPos + 1);

        // Parse priority
        commaPos = line.indexOf(u',');
        auto priority = ServerPriority::High;
        QString srvName;
        if (commaPos == 1) {
            const int priVal = line.left(commaPos).toInt();
            if (priVal >= 0 && priVal <= 2)
                priority = static_cast<ServerPriority>(priVal);
            srvName = line.mid(commaPos + 1).trimmed();
        } else {
            srvName = line.trimmed();
        }

        auto srv = std::make_unique<Server>(0, nPort);
        srv->setDynIP(host);
        srv->setName(srvName);
        srv->setStaticMember(true);
        srv->setPreference(priority);

        Server* existing = findByAddress(host, nPort);
        if (existing) {
            existing->setName(srvName);
            existing->setStaticMember(true);
            existing->setPreference(priority);
        } else {
            addServer(std::move(srv));
        }
    }

    return true;
}

bool ServerList::saveStaticServers(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logError(QStringLiteral("Failed to save static servers: %1").arg(filePath));
        return false;
    }

    QTextStream stream(&file);
    stream << QChar(0xFEFF);  // Unicode BOM

    for (const auto& srv : m_servers) {
        if (srv->isStaticMember()) {
            stream << srv->address() << u':' << srv->port()
                   << u',' << static_cast<uint32>(srv->preference())
                   << u',' << srv->name() << QStringLiteral("\r\n");
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Text file import (ip:port lines and ed2k links)
// ---------------------------------------------------------------------------

int ServerList::addServersFromTextFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;

    QTextStream stream(&file);
    int added = 0;

    QString line;
    while (stream.readLineInto(&line)) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u'#') || line.startsWith(u'/'))
            continue;

        // Try ed2k link first
        if (line.startsWith(QStringLiteral("ed2k://"), Qt::CaseInsensitive)) {
            auto linkOpt = parseED2KLink(line);
            if (linkOpt && std::holds_alternative<ED2KServerLink>(*linkOpt)) {
                const auto& srvLink = std::get<ED2KServerLink>(*linkOpt);
                auto srv = std::make_unique<Server>(0, srvLink.port);
                srv->setDynIP(srvLink.address);
                srv->setName(srvLink.address);
                if (addServer(std::move(srv)))
                    ++added;
            }
            continue;
        }

        // Try ip:port format
        const qsizetype colonPos = line.indexOf(u':');
        if (colonPos < 1)
            continue;

        const QString host = line.left(colonPos);
        bool portOk = false;
        const uint16 nPort = static_cast<uint16>(line.mid(colonPos + 1).toUInt(&portOk));
        if (!portOk || nPort == 0)
            continue;

        // Try to parse as numeric IP
        QHostAddress addr(host);
        auto srv = std::make_unique<Server>(0, nPort);
        if (!addr.isNull()) {
            srv->setIP(static_cast<uint32>(addr.toIPv4Address()));
        } else {
            srv->setDynIP(host);
        }
        srv->setName(host);
        if (addServer(std::move(srv)))
            ++added;
    }

    return added;
}

// ---------------------------------------------------------------------------
// Add / Remove
// ---------------------------------------------------------------------------

Server* ServerList::addServer(std::unique_ptr<Server> server)
{
    if (!server)
        return nullptr;

    // Validate IP for non-dynIP servers
    if (!server->hasDynIP() && !isGoodServerIP(*server))
        return nullptr;

    // Reject duplicates
    if (isDuplicate(*server))
        return nullptr;

    Server* raw = server.get();
    m_servers.push_back(std::move(server));
    emit serverAdded(raw);
    return raw;
}

bool ServerList::removeServer(const Server* server)
{
    for (size_t i = 0; i < m_servers.size(); ++i) {
        if (m_servers[i].get() == server) {
            emit serverAboutToBeRemoved(server);
            adjustPositionsAfterRemoval(i);
            m_servers.erase(m_servers.begin() + static_cast<ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

void ServerList::removeAllServers()
{
    for (const auto& srv : m_servers)
        emit serverAboutToBeRemoved(srv.get());
    m_servers.clear();
    m_serverPos = 0;
    m_searchServerPos = 0;
    m_statServerPos = 0;
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------

Server* ServerList::findByIPTcp(uint32 ip, uint16 port) const
{
    for (const auto& srv : m_servers) {
        if (srv->ip() == ip && srv->port() == port)
            return srv.get();
    }
    return nullptr;
}

Server* ServerList::findByIPUdp(uint32 ip, uint16 udpPort, bool obfuscationPorts) const
{
    for (const auto& srv : m_servers) {
        if (srv->ip() == ip
            && (udpPort == srv->port() + 4
                || (obfuscationPorts
                    && (udpPort == srv->obfuscationPortUDP()
                        || udpPort == srv->port() + 12))))
        {
            return srv.get();
        }
    }
    return nullptr;
}

Server* ServerList::findByAddress(const QString& address, uint16 port) const
{
    for (const auto& srv : m_servers) {
        if ((port == srv->port() || port == 0)
            && srv->address().compare(address, Qt::CaseInsensitive) == 0)
        {
            return srv.get();
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// serverAt
// ---------------------------------------------------------------------------

Server* ServerList::serverAt(size_t index) const
{
    return (index < m_servers.size()) ? m_servers[index].get() : nullptr;
}

// ---------------------------------------------------------------------------
// Round-robin iterators
// ---------------------------------------------------------------------------

Server* ServerList::nextServer(bool tryObfuscated)
{
    const size_t count = m_servers.size();
    if (count == 0)
        return nullptr;

    for (size_t i = 0; i < count; ++i) {
        if (m_serverPos >= count)
            m_serverPos = 0;

        Server* srv = m_servers[m_serverPos].get();
        ++m_serverPos;

        if (!tryObfuscated || srv->supportsObfuscationTCP() || !srv->triedCrypt())
            return srv;
    }
    return nullptr;
}

Server* ServerList::nextSearchServer()
{
    const size_t count = m_servers.size();
    if (count == 0)
        return nullptr;

    if (m_searchServerPos >= count)
        m_searchServerPos = 0;

    Server* srv = m_servers[m_searchServerPos].get();
    m_searchServerPos = (m_searchServerPos + 1) % count;
    return srv;
}

Server* ServerList::nextStatServer()
{
    const size_t count = m_servers.size();
    if (count == 0)
        return nullptr;

    if (m_statServerPos >= count)
        m_statServerPos = 0;

    Server* srv = m_servers[m_statServerPos].get();
    m_statServerPos = (m_statServerPos + 1) % count;
    return srv;
}

void ServerList::setServerPosition(size_t pos)
{
    m_serverPos = (pos < m_servers.size()) ? pos : 0;
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

ServerListStats ServerList::stats() const
{
    ServerListStats s;
    s.total = static_cast<uint32>(m_servers.size());
    for (const auto& srv : m_servers) {
        if (srv->failedCount() != 0) {
            ++s.failed;
        } else {
            s.users += srv->users();
            s.files += srv->files();
            s.lowIDUsers += srv->lowIDUsers();
        }
    }
    return s;
}

// ---------------------------------------------------------------------------
// Sorting
// ---------------------------------------------------------------------------

void ServerList::sortByPreference()
{
    std::stable_sort(m_servers.begin(), m_servers.end(),
        [](const std::unique_ptr<Server>& a, const std::unique_ptr<Server>& b) {
            // High < Normal < Low  (High=1, Normal=0, Low=2 → sort order: High, Normal, Low)
            auto rank = [](ServerPriority p) -> int {
                switch (p) {
                case ServerPriority::High:   return 0;
                case ServerPriority::Normal: return 1;
                case ServerPriority::Low:    return 2;
                }
                return 1;
            };
            return rank(a->preference()) < rank(b->preference());
        });
}

// ---------------------------------------------------------------------------
// Crypto key management
// ---------------------------------------------------------------------------

void ServerList::checkForExpiredUDPKeys(uint32 currentClientIP)
{
    for (const auto& srv : m_servers) {
        if (srv->supportsObfuscationUDP()) {
            if (srv->serverKeyUDP() != 0 && srv->serverKeyUDPIP() != currentClientIP) {
                srv->setLastPingedTime(0);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// IP validation
// ---------------------------------------------------------------------------

bool ServerList::isGoodServerIP(const Server& server)
{
    return server.port() != 0 && (server.hasDynIP() || isGoodIP(server.ip()));
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool ServerList::isDuplicate(const Server& server) const
{
    // Check by address + port
    if (findByAddress(server.address(), server.port()))
        return true;

    // For non-dynIP servers, also check by IP + port
    if (!server.hasDynIP() && server.ip() != 0 && findByIPTcp(server.ip(), server.port()))
        return true;

    return false;
}

void ServerList::adjustPositionsAfterRemoval(size_t removedIndex)
{
    auto adjust = [&](size_t& pos) {
        if (pos > removedIndex && pos > 0)
            --pos;
        else if (pos == removedIndex)
            pos = m_servers.empty() ? 0 : pos % (m_servers.size() - 1);
    };
    adjust(m_serverPos);
    adjust(m_searchServerPos);
    adjust(m_statServerPos);
}

} // namespace eMule
