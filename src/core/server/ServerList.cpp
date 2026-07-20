#include "pch.h"
/// @file ServerList.cpp
/// @brief ED2K server collection implementation — port of CServerList from MFC.

#include "ServerList.h"
#include "ServerConnect.h"
#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "net/Packet.h"
#include "protocol/ED2KLink.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"

#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QTextStream>


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
            file.writeUInt32(srv->hasDynIP() ? 0 : srv->ipAddress().toNetworkUint32());
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
        logError(QStringLiteral("Error saving server.met: %1").arg(QLatin1StringView(ex.what())));
        QFile::remove(tmpPath);
        return false;
    }

    // Rotate: current → .bak, then rename tmp → final
    const QString bakPath = filePath + QStringLiteral(".bak");
    QFile::remove(bakPath);
    if (QFile::exists(filePath)) {
        if (!QFile::rename(filePath, bakPath))
            QFile::remove(filePath);
    }
    if (!QFile::rename(tmpPath, filePath)) {
        logError(QStringLiteral("Failed to rename %1 to %2").arg(tmpPath, filePath));
        if (QFile::exists(bakPath))
            QFile::rename(bakPath, filePath);
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
        logError(QStringLiteral("Error reading server.met: %1").arg(QLatin1StringView(ex.what())));
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
            srv->setIpAddress(Address::fromQHostAddress(addr));
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

int ServerList::removeDeadServers(uint32 maxRetries)
{
    if (maxRetries == 0)
        return 0;

    int removed = 0;
    for (auto i = static_cast<ptrdiff_t>(m_servers.size()) - 1; i >= 0; --i) {
        if (m_servers[static_cast<size_t>(i)]->failedCount() >= maxRetries) {
            emit serverAboutToBeRemoved(m_servers[static_cast<size_t>(i)].get());
            adjustPositionsAfterRemoval(static_cast<size_t>(i));
            m_servers.erase(m_servers.begin() + i);
            ++removed;
        }
    }
    return removed;
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
        if (srv->ipAddress().toNetworkUint32() == ip && srv->port() == port)
            return srv.get();
    }
    return nullptr;
}

Server* ServerList::findByIPUdp(uint32 ip, uint16 udpPort, bool obfuscationPorts) const
{
    for (const auto& srv : m_servers) {
        if (srv->ipAddress().toNetworkUint32() == ip
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
// UDP server status — OP_GLOBSERVSTATREQ / OP_GLOBSERVSTATRES
// ---------------------------------------------------------------------------

void ServerList::serverStats()
{
    if (m_servers.empty() || !theApp.serverConnect)
        return;

    // MFC: CServerList::ServerStats() gates on theApp.IsConnected() — "ED2K
    // connected OR Kad connected". Its comment notes stats should keep running
    // in Kad-only mode so the two networks refresh each other. sendUDPPacket()
    // applies the same predicate, so there is nothing to pre-check here beyond
    // having a socket to send on.
    if (!theApp.isConnected())
        return;

    const auto now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());

    // Find the next server that is actually due for a ping. Walk at most one
    // full lap so an all-recently-pinged list terminates.
    Server* target = nextStatServer();
    if (target == nullptr)
        return;
    const Server* const first = target;
    while (target->lastPingedTime() > 0
           && now < target->lastPingedTime() + UDPSERVSTATREASKTIME) {
        target = nextStatServer();
        if (target == first)
            return;  // every server pinged recently
    }

    if (target->failedCount() >= MAX_SERVERFAILCOUNT)
        return;

    target->setRealLastPingedTime(now);

    // Plain (non-obfuscated) status request. The challenge convention
    // 0x55AA<random16> matches original eMule — some servers key extended
    // reply formats off the 0x55AA marker, so keep it exactly.
    // NOTE: original eMule first attempts an *obfuscated* ping to port+12 and
    // only falls back to this on no reply. Not implemented yet — see
    // Server::cryptPingReplyPending(), which stays false for now.
    auto packet = std::make_unique<Packet>(OP_GLOBSERVSTATREQ, 4);
    packet->prot = OP_EDONKEYPROT;

    const uint32 challenge = 0x55AA0000u | (QRandomGenerator::global()->generate() & 0xFFFFu);
    pokeUInt32(packet->pBuffer, challenge);

    target->setChallenge(challenge);
    target->setLastPinged(static_cast<uint32>(QDateTime::currentMSecsSinceEpoch()));
    // Spread re-asks out by up to an hour so the whole list does not come due
    // at once (matches eMule's `tNow - (rand() % HR2S(1))`).
    target->setLastPingedTime(
        now - static_cast<uint32>(QRandomGenerator::global()->bounded(HR2S(1))));
    target->incFailedCount();

    logDebug(QStringLiteral("ServerList: OP_GLOBSERVSTATREQ -> %1 (%2:%3) challenge=0x%4")
                 .arg(target->name())
                 .arg(ipstr(target->ipAddress()))
                 .arg(target->port())
                 .arg(challenge, 8, 16, QLatin1Char('0')));

    theApp.serverConnect->sendUDPPacket(std::move(packet), *target,
                                        static_cast<uint16>(target->port() + 4));
}

void ServerList::processStatusResponse(const uint8* data, uint32 size, const Endpoint& from)
{
    // Layout (original eMule CUDPSocket::ProcessPacket, OP_GLOBSERVSTATRES):
    //   +0  uint32 challenge
    //   +4  uint32 users
    //   +8  uint32 files
    //  +12  uint32 maxUsers        (size >= 16)
    //  +16  uint32 softFiles       (size >= 24)
    //  +20  uint32 hardFiles       (size >= 24)
    //  +24  uint32 udpFlags        (size >= 28)
    //  +28  uint32 lowIDUsers      (size >= 32)
    //  +32  uint16 udpObfuscationPort  (size >= 40)
    //  +34  uint16 tcpObfuscationPort  (size >= 40)
    //  +36  uint32 serverUDPKey        (size >= 40)
    // Anything past 40 bytes is a vendor extension and is ignored (ed2kNET
    // appends a tagged X25519 public key here) — tolerate, do not reject.
    if (data == nullptr || size < 12)
        return;

    // The reply arrives on the server's UDP port (tcpPort + 4) or its
    // obfuscation port; findByIPUdp handles both.
    Server* server = findByIPUdp(from.address().toNetworkUint32(), from.port(), true);
    if (server == nullptr) {
        logDebug(QStringLiteral("ServerList: OP_GLOBSERVSTATRES from unknown server %1:%2")
                     .arg(ipstr(from.address()))
                     .arg(from.port()));
        return;
    }

    const uint32 challenge = peekUInt32(data);
    if (challenge != server->challenge()) {
        logDebug(QStringLiteral("ServerList: OP_GLOBSERVSTATRES challenge mismatch from %1 "
                                "(got 0x%2, expected 0x%3)")
                     .arg(server->name())
                     .arg(challenge, 8, 16, QLatin1Char('0'))
                     .arg(server->challenge(), 8, 16, QLatin1Char('0')));
        return;
    }

    server->setChallenge(0);
    server->setCryptPingReplyPending(false);
    server->resetFailedCount();

    const auto nowMs = static_cast<uint32>(QDateTime::currentMSecsSinceEpoch());
    server->setPing(nowMs - server->lastPinged());

    server->setUsers(peekUInt32(data + 4));
    server->setFiles(peekUInt32(data + 8));

    if (size >= 16)
        server->setMaxUsers(peekUInt32(data + 12));
    if (size >= 24) {
        server->setSoftFiles(peekUInt32(data + 16));
        server->setHardFiles(peekUInt32(data + 20));
    }

    uint32 udpFlags = 0;
    if (size >= 28) {
        udpFlags = peekUInt32(data + 24);
        server->setUDPFlags(udpFlags);
    }
    if (size >= 32)
        server->setLowIDUsers(peekUInt32(data + 28));

    uint16 udpObfPort = 0;
    uint16 tcpObfPort = 0;
    if (size >= 40) {
        udpObfPort = peekUInt16(data + 32);
        tcpObfPort = peekUInt16(data + 34);
        // setServerKeyUDP() stamps our current public IP itself, as MFC does.
        server->setServerKeyUDP(peekUInt32(data + 36));

        if (size > 40) {
            logDebug(QStringLiteral("ServerList: OP_GLOBSERVSTATRES from %1 has %2 extra bytes "
                                    "(vendor extension, ignored)")
                         .arg(server->name())
                         .arg(size - 40));
        }
    }

    // Apply default obfuscation ports when a short packet carried no port data
    // but the flags claim obfuscation support.
    if (tcpObfPort == 0 && (udpFlags & SrvUdpFlag::TcpObfuscation) != 0)
        tcpObfPort = server->port();
    if (udpObfPort == 0 && (udpFlags & SrvUdpFlag::UdpObfuscation) != 0)
        udpObfPort = static_cast<uint16>(server->port() + 12);

    server->setObfuscationPortTCP(tcpObfPort);
    server->setObfuscationPortUDP(udpObfPort);

    logDebug(QStringLiteral("ServerList: OP_GLOBSERVSTATRES from %1 — users=%2 files=%3 "
                            "ping=%4ms udpFlags=0x%5 udpKey=0x%6")
                 .arg(server->name())
                 .arg(server->users())
                 .arg(server->files())
                 .arg(server->ping())
                 .arg(udpFlags, 8, 16, QLatin1Char('0'))
                 .arg(server->serverKeyUDPRaw(), 8, 16, QLatin1Char('0')));

    emit serverUpdated(server);
}

// ---------------------------------------------------------------------------
// Crypto key management
// ---------------------------------------------------------------------------

void ServerList::checkForExpiredUDPKeys(uint32 currentClientIP)
{
    // MFC: CServerList::CheckForExpiredUDPKeys() — ServerList.cpp:1086.
    // Our public IP changed, so every UDP key issued for the old one is now
    // dead. Clearing lastPingedTime re-queues those servers for a stat ping,
    // which is how a fresh key is obtained.
    const auto now = static_cast<uint32>(QDateTime::currentSecsSinceEpoch());

    uint32 keysTotal = 0;
    uint32 keysExpired = 0;
    uint32 pingsDelayed = 0;

    for (const auto& srv : m_servers) {
        if (!srv->supportsObfuscationUDP())
            continue;

        // Deliberately the raw key: serverKeyUDP() already hides anything
        // issued for a different IP, which is exactly what we are hunting for.
        if (srv->serverKeyUDPRaw() == 0)
            continue;

        ++keysTotal;

        if (srv->serverKeyUDPIP() == currentClientIP)
            continue;  // still valid

        ++keysExpired;

        // Don't let an IP change fire the whole list at once — a server pinged
        // within the last UDPSERVSTATMINREASKTIME is backdated so it comes due
        // exactly when that minimum elapses, rather than immediately.
        const uint32 sinceLastPing = now - srv->realLastPingedTime();
        if (sinceLastPing < UDPSERVSTATMINREASKTIME) {
            ++pingsDelayed;
            srv->setLastPingedTime((now - static_cast<uint32>(UDPSERVSTATREASKTIME))
                                   + (UDPSERVSTATMINREASKTIME - sinceLastPing));
        } else {
            srv->setLastPingedTime(0);  // due now
        }
    }

    logDebug(QStringLiteral("ServerList: public IP changed — %1 UDP keys total, %2 expired, "
                            "%3 immediate pings forced, %4 delayed")
                 .arg(keysTotal)
                 .arg(keysExpired)
                 .arg(keysExpired - pingsDelayed)
                 .arg(pingsDelayed));
}

// ---------------------------------------------------------------------------
// IP validation
// ---------------------------------------------------------------------------

bool ServerList::isGoodServerIP(const Server& server)
{
    return server.port() != 0 && (server.hasDynIP() || server.ipAddress().isRoutable());
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
    if (!server.hasDynIP() && !server.ipAddress().isNull() && findByIPTcp(server.ipAddress().toNetworkUint32(), server.port()))
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
