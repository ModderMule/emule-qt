#include "pch.h"
/// @file DeadServers.cpp
/// @brief Block list of servers removed as dead (#161).

#include "DeadServers.h"
#include "Server.h"
#include "utils/Log.h"

#include <QFile>
#include <QSaveFile>
#include <QTextStream>

namespace eMule {

namespace {
constexpr uint32 kDaySecs = 24 * 60 * 60;

bool expired(uint32 since, uint32 days, uint32 now)
{
    return days == 0 || now >= since + days * kDaySecs;
}
} // namespace

void DeadServers::load(const QString& filePath, uint32 now)
{
    m_path = filePath;
    m_blocked.clear();
    if (m_path.isEmpty())
        return;
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QTextStream in(&file);
    QString line;
    while (in.readLineInto(&line)) {
        const QStringList parts = line.split(u'\t');
        if (parts.size() != 3)
            continue;
        bool okSince = false;
        bool okPort = false;
        const uint32 since = parts[0].toUInt(&okSince);
        const uint port = parts[2].toUInt(&okPort);
        if (!okSince || !okPort || port == 0 || port > 0xFFFF || parts[1].isEmpty()
            || since > now)
            continue;
        m_blocked.insert(key(parts[1], static_cast<uint16>(port)), since);
    }
}

bool DeadServers::save() const
{
    if (m_path.isEmpty())
        return true;
    if (m_blocked.isEmpty()) {
        QFile::remove(m_path);
        return true;
    }
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logWarning(QStringLiteral("Failed to save %1").arg(m_path));
        return false;
    }
    QTextStream out(&file);
    for (auto it = m_blocked.cbegin(); it != m_blocked.cend(); ++it) {
        // key is "<address>\t<port>"
        out << it.value() << u'\t' << it.key() << u'\n';
    }
    out.flush();
    return file.commit();
}

void DeadServers::block(const Server& server, uint32 now)
{
    for (const QString& k : keysOf(server))
        m_blocked.insert(k, now);
}

bool DeadServers::unblock(const Server& server)
{
    bool any = false;
    for (const QString& k : keysOf(server))
        any = m_blocked.remove(k) > 0 || any;
    return any;
}

bool DeadServers::isBlocked(const Server& server, uint32 days, uint32 now) const
{
    if (days == 0 || m_blocked.isEmpty())
        return false;
    for (const QString& k : keysOf(server)) {
        const auto it = m_blocked.constFind(k);
        if (it != m_blocked.cend() && !expired(it.value(), days, now))
            return true;
    }
    return false;
}

void DeadServers::prune(uint32 days, uint32 now)
{
    m_blocked.removeIf([&](const auto& it) { return expired(it.value(), days, now); });
}

// ---------------------------------------------------------------------------

QString DeadServers::key(const QString& address, uint16 port)
{
    return address.toLower() + u'\t' + QString::number(port);
}

QStringList DeadServers::keysOf(const Server& server)
{
    // address() is the dynIP name or the primary IP; a dual-stack row also has its IPv6.
    QStringList keys{key(server.address(), server.port())};
    if (server.hasBothFamilies())
        keys << key(server.ipv6Address().toString(), server.port());
    return keys;
}

} // namespace eMule
