#include "pch.h"
#include "utils/HostnameResolver.h"
#include "utils/AddressSortKey.h"

#include <QHostAddress>
#include <QHostInfo>

namespace eMule {

namespace {

constexpr qsizetype kMaxLookups = 8;

} // namespace

HostnameResolver::HostnameResolver(QObject* parent)
    : QObject(parent)
{
}

HostnameResolver::~HostnameResolver()
{
    for (const int id : std::as_const(m_lookups))
        QHostInfo::abortHostLookup(id);
}

bool HostnameResolver::isResolvable(const QString& address)
{
    const QHostAddress a(address);
    return !a.isNull() && !a.isMulticast()
        && a != QHostAddress(QHostAddress::AnyIPv4)
        && a != QHostAddress(QHostAddress::AnyIPv6)
        && a != QHostAddress(QHostAddress::Broadcast);
}

void HostnameResolver::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    // a callback of an aborted lookup must not fill the cache
    ++m_generation;
    if (!on) {
        for (const int id : std::as_const(m_lookups))
            QHostInfo::abortHostLookup(id);
        m_lookups.clear();
        m_queue.clear();
    }
}

void HostnameResolver::sync(const QSet<QString>& listed, const QStringList& candidates)
{
    m_queue.clear();
    if (m_enabled) {
        QSet<QString> queued;
        for (const QString& address : candidates) {
            if (!queued.contains(address) && !isKnown(address) && isResolvable(address)) {
                m_queue.enqueue(address);
                queued.insert(address);
            }
        }
    }
    for (auto it = m_hostnames.begin(); it != m_hostnames.end();) {
        if (!listed.contains(it.key()))
            it = m_hostnames.erase(it);
        else
            ++it;
    }
    startLookups();
}

void HostnameResolver::sync(const QStringList& addresses)
{
    sync(QSet<QString>(addresses.cbegin(), addresses.cend()), addresses);
}

QString HostnameResolver::hostname(const QString& address) const
{
    return m_enabled ? m_hostnames.value(address) : QString();
}

QString HostnameResolver::displayText(const QString& address, const QString& fallback) const
{
    const QString name = hostname(address);
    return name.isEmpty() ? fallback : name;
}

QString HostnameResolver::endpointText(const QString& address, uint16_t port) const
{
    const QString name = hostname(address);
    if (port == 0)
        return name.isEmpty() ? address : name;
    return name.isEmpty() ? addressWithPort(address, port) : QStringLiteral("%1:%2").arg(name).arg(port);
}

QString HostnameResolver::toolTip(const QString& address, uint16_t port) const
{
    const QString shown = port != 0 ? addressWithPort(address, port) : address;
    if (!m_enabled)
        return shown;
    const auto name = m_hostnames.constFind(address);
    if (name != m_hostnames.cend())
        return name->isEmpty() ? tr("%1\nNo hostname found.").arg(shown)
                               : QStringLiteral("%1\n%2").arg(*name, shown);
    if (m_lookups.contains(address))
        return tr("%1 (resolving…)").arg(shown);
    return shown;
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void HostnameResolver::startLookups()
{
    while (m_enabled && m_lookups.size() < kMaxLookups && !m_queue.isEmpty()) {
        const QString address = m_queue.dequeue();
        const quint64 generation = m_generation;
        const int id = QHostInfo::lookupHost(address, this, [this, address, generation](const QHostInfo& info) {
            if (generation != m_generation)
                return;
            m_lookups.remove(address);
            // Qt can return the original IP when no PTR record exists
            const QString name = info.error() == QHostInfo::NoError
                    && QHostAddress(info.hostName()).isNull() ? info.hostName() : QString();
            m_hostnames.insert(address, name);
            emit resolved(address);
            startLookups();
        });
        m_lookups.insert(address, id);
    }
}

} // namespace eMule
