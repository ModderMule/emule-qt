#include "pch.h"
/// @file BindAddress.cpp
/// @brief The "bind address" preference applied to every socket the client opens.

#include "net/BindAddress.h"
#include "net/InterfacePin.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QNetworkInterface>

#include <mutex>

namespace eMule::BindAddress {

namespace {

std::mutex s_lock;
Resolution s_cache;
bool s_resolved = false;
std::function<QList<LocalInterface>()> s_source;
std::function<PinProbe(int, const QString&)> s_probe;

QList<LocalInterface> systemInterfaces()
{
    QList<LocalInterface> out;
    const auto all = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& nif : all) {
        const auto flags = nif.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning))
            continue;
        LocalInterface li;
        li.name = nif.name();
        li.friendlyName = nif.humanReadableName();
        li.index = nif.index();
        const auto entries = nif.addressEntries();
        for (const QNetworkAddressEntry& e : entries) {
            QHostAddress a = e.ip();
            a.setScopeId({});
            li.addresses.append(a);
        }
        out.append(li);
    }
    return out;
}

bool isGlobalIPv6(const QHostAddress& a)
{
    return a.protocol() == QAbstractSocket::IPv6Protocol && a.isGlobal();
}

void fill(Resolution& r, const LocalInterface& li)
{
    r.state = State::Bound;
    r.index = li.index;
    r.name = li.name;
    r.friendlyName = li.friendlyName;
    for (const QHostAddress& a : li.addresses) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol && r.v4.isNull())
            r.v4 = a;
        else if (isGlobalIPv6(a))
            r.hasGlobalIPv6 = true;
    }
}

void block(Resolution& r, const QString& reason)
{
    r.state = State::Blocked;
    r.reason = reason;
}

// Whole interface: the wildcard of the families that can be pinned.
QHostAddress wildcard(const Resolution& r)
{
    if (r.state == State::Bound && !r.pin.v6)
        return QHostAddress(QHostAddress::AnyIPv4);
    if (r.state == State::Bound && !r.pin.v4)
        return QHostAddress(QHostAddress::AnyIPv6);
    return QHostAddress(QHostAddress::Any);
}

bool familyUsable(const Resolution& r, bool v4)
{
    return v4 ? r.pin.v4 : r.pin.v6;
}

// Try the pin once per resolution, so a refused option is one clear error, not a
// warning per socket. Caller holds s_lock.
void probePin(Resolution& r)
{
    if (r.state != State::Bound)
        return;
    std::function<PinProbe(int, const QString&)> probe = s_probe;
    if (!probe) {
        if (s_source)
            return;   // test interfaces: their indexes are not real
        probe = InterfacePin::probe;
    }
    PinProbe p = probe(r.index, r.name);
    if (!r.literal.isNull()) {
        // a literal runs its own family only
        const bool v4 = r.literal.protocol() == QAbstractSocket::IPv4Protocol;
        if (!(v4 ? p.v4 : p.v6)) {
            const QString why = QStringLiteral("cannot pin a socket to interface %1 (error %2)")
                                    .arg(r.name).arg(v4 ? p.v4Error : p.v6Error);
            r = Resolution{.selector = r.selector};
            block(r, why);
        }
        return;
    }
    if (!p.v4 && !p.v6) {
        const QString why = QStringLiteral("cannot pin a socket to interface %1 (IPv4 error %2, "
                                           "IPv6 error %3)").arg(r.name).arg(p.v4Error).arg(p.v6Error);
        r = Resolution{.selector = r.selector};
        block(r, why);
        return;
    }
    if (!p.v6)
        r.hasGlobalIPv6 = false;
    if (!p.v4)
        r.v4 = QHostAddress();
    r.pin = p;
}

void report(const Resolution& r)
{
    switch (r.state) {
    case State::Unrestricted:
        logInfo(QStringLiteral("Network interface: any"));
        break;
    case State::Bound:
        if (r.literal.isNull())
            logInfo(QStringLiteral("Network interface: bound to %1 (index %2)").arg(r.name).arg(r.index));
        else
            logInfo(QStringLiteral("Network interface: bound to %1 on %2 (index %3)")
                        .arg(r.literal.toString(), r.name).arg(r.index));
        if (r.literal.protocol() == QAbstractSocket::IPv6Protocol)
            logWarning(QStringLiteral("Bound to IPv6 address %1: IPv4-only servers and peers "
                                      "are unreachable").arg(r.literal.toString()));
        if (!r.pin.v6)
            logWarning(QStringLiteral("Network interface %1 refuses the IPv6 pin (error %2): "
                                      "IPv4 only").arg(r.name).arg(r.pin.v6Error));
        if (!r.pin.v4)
            logWarning(QStringLiteral("Network interface %1 refuses the IPv4 pin (error %2): "
                                      "IPv6 only").arg(r.name).arg(r.pin.v4Error));
        break;
    case State::Blocked:
        logError(QStringLiteral("Network interface: %1 — no connection is opened until it "
                                "is available").arg(r.reason));
        break;
    }
}

// Caller holds s_lock. True when the resolution changed.
bool resolveLocked(const QString& text)
{
    Resolution next = resolve(text, s_source ? s_source() : systemInterfaces());
    probePin(next);
    const bool changed = !s_resolved || next != s_cache;
    s_resolved = true;
    if (changed) {
        s_cache = std::move(next);
        report(s_cache);
    }
    return changed;
}

} // namespace

Resolution resolve(const QString& selector, const QList<LocalInterface>& interfaces)
{
    Resolution r;
    r.selector = selector.trimmed();
    if (r.selector.isEmpty())
        return r;

    // Address literal: the interface that holds it.
    if (const QHostAddress literal(r.selector); !literal.isNull()
        && (literal.protocol() == QAbstractSocket::IPv4Protocol
            || literal.protocol() == QAbstractSocket::IPv6Protocol))
    {
        for (const LocalInterface& li : interfaces) {
            if (li.addresses.contains(literal)) {
                fill(r, li);
                r.literal = literal;
                return r;
            }
        }
        block(r, QStringLiteral("address %1 is not assigned to any active interface").arg(r.selector));
        return r;
    }

    // Subnet: the first interface with an address in it.
    if (r.selector.contains(u'/')) {
        const auto subnet = QHostAddress::parseSubnet(r.selector);
        if (subnet.first.isNull() || subnet.second < 0) {
            block(r, QStringLiteral("\"%1\" is not a valid subnet").arg(r.selector));
            return r;
        }
        for (const LocalInterface& li : interfaces) {
            for (const QHostAddress& a : li.addresses) {
                if (a.isInSubnet(subnet)) {
                    fill(r, li);
                    return r;
                }
            }
        }
        block(r, QStringLiteral("no active interface has an address in %1").arg(r.selector));
        return r;
    }

    // Interface name.
    for (const LocalInterface& li : interfaces) {
        if (li.name.compare(r.selector, Qt::CaseInsensitive) == 0
            || li.friendlyName.compare(r.selector, Qt::CaseInsensitive) == 0)
        {
            fill(r, li);
            if (li.addresses.isEmpty()) {
                r = Resolution{.selector = r.selector};
                block(r, QStringLiteral("interface %1 has no address").arg(li.name));
            }
            return r;
        }
    }
    block(r, QStringLiteral("interface \"%1\" is not available").arg(r.selector));
    return r;
}

QList<LocalInterface> localInterfaces()
{
    std::function<QList<LocalInterface>()> source;
    {
        std::lock_guard guard(s_lock);
        source = s_source;
    }
    return source ? source() : systemInterfaces();
}

void setInterfaceSource(std::function<QList<LocalInterface>()> source)
{
    std::lock_guard guard(s_lock);
    s_source = std::move(source);
    s_resolved = false;
}

void setPinProbe(std::function<PinProbe(int index, const QString& name)> probe)
{
    std::lock_guard guard(s_lock);
    s_probe = std::move(probe);
    s_resolved = false;
}

Resolution current()
{
    // The UDP sockets ask per datagram and from their own thread: resolve once per value.
    const QString text = thePrefs.bindAddress().trimmed();
    std::lock_guard guard(s_lock);
    if (!s_resolved || text != s_cache.selector)
        resolveLocked(text);
    return s_cache;
}

bool refresh()
{
    const QString text = thePrefs.bindAddress().trimmed();
    std::lock_guard guard(s_lock);
    return resolveLocked(text);
}

bool outboundAllowed()
{
    return current().state != State::Blocked;
}

bool isConfigured()
{
    return current().state != State::Unrestricted;
}

std::optional<QHostAddress> listenAddress()
{
    const Resolution r = current();
    if (r.state == State::Blocked)
        return std::nullopt;
    if (r.literal.isNull())
        return wildcard(r);
    return r.literal;
}

std::optional<QHostAddress> sourceFor(const Address& dest)
{
    const Resolution r = current();
    if (r.state == State::Blocked)
        return std::nullopt;
    if (!r.literal.isNull()) {
        const bool v4 = r.literal.protocol() == QAbstractSocket::IPv4Protocol;
        if (v4 != dest.isIPv4())
            return std::nullopt;
        return r.literal;
    }
    if (!familyUsable(r, dest.isIPv4()))
        return std::nullopt;
    return QHostAddress(dest.isIPv4() ? QHostAddress::AnyIPv4 : QHostAddress::AnyIPv6);
}

bool canReach(const Address& dest)
{
    const Resolution r = current();
    if (r.state == State::Unrestricted)
        return true;
    if (r.state == State::Blocked)
        return false;
    if (r.literal.isNull())
        return familyUsable(r, dest.isIPv4());
    return r.literal.protocol() == QAbstractSocket::IPv4Protocol ? dest.isIPv4() : dest.isIPv6();
}

QString ipv4Literal()
{
    const Resolution r = current();
    if (r.state != State::Bound)
        return {};
    if (!r.literal.isNull())
        return r.literal.protocol() == QAbstractSocket::IPv4Protocol ? r.literal.toString() : QString();
    return r.v4.isNull() ? QString() : r.v4.toString();
}

bool isIPv4Only()
{
    const Resolution r = current();
    if (r.state != State::Bound)
        return false;
    if (!r.literal.isNull())
        return r.literal.protocol() == QAbstractSocket::IPv4Protocol;
    return !r.hasGlobalIPv6;
}

bool isBoundInterface(const QString& interfaceName)
{
    const Resolution r = current();
    if (r.state == State::Unrestricted)
        return true;
    if (r.state == State::Blocked)
        return false;
    // Windows route rows name the interface by its index.
    return r.name.compare(interfaceName, Qt::CaseInsensitive) == 0
        || r.friendlyName.compare(interfaceName, Qt::CaseInsensitive) == 0
        || interfaceName == QString::number(r.index);
}

} // namespace eMule::BindAddress
