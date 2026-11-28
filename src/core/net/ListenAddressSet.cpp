#include "pch.h"
/// @file ListenAddressSet.cpp
/// @brief One bound socket per listen address, all on one port.

#include "net/ListenAddressSet.h"
#include "utils/Log.h"
#include "utils/ArgStr.h"

#include <QStringList>

namespace eMule {

namespace {

// a random port taken on one of the addresses: another try with a new one
constexpr int kRandomPortTries = 5;

QString joined(const QList<QHostAddress>& addresses)
{
    QStringList out;
    for (const QHostAddress& a : addresses)
        out.append(a.toString());
    return out.join(QStringLiteral(", "));
}

} // namespace

bool ListenAddressSet::openAll(const QList<QHostAddress>& wanted, uint16 port, QString& error)
{
    closeAll();
    const int tries = port == 0 ? kRandomPortTries : 1;
    for (int attempt = 0; attempt < tries; ++attempt) {
        uint16 p = port;
        bool failed = false;
        for (const QHostAddress& a : wanted) {
            QString why;
            switch (bindOne(a, p, why)) {
            case BindResult::Ok:
                m_bound.append(a);
                if (p == 0)
                    p = portOf(a);
                break;
            case BindResult::Later:
                m_pending.append(a);
                logDebug(QStringLiteral("%1: %2 not usable yet (%3), bound when it is")
                             .arg(setName(), a.toString(), why));
                m_reported.append(a);
                break;
            case BindResult::Failed:
                error = QStringLiteral("%1: %2").arg(a.toString(), why);
                failed = true;
                break;
            }
            if (failed)
                break;
        }
        if (!failed) {
            m_active = true;
            m_port = p;
            if (!m_bound.isEmpty()) {
                if (m_bound.size() > 1 || m_bound.front() != QHostAddress(QHostAddress::Any))
                    logDebug(QStringLiteral("%1: port %2 on %3").arg(argStr(setName()), argStr(m_port), argStr(joined(m_bound))));
                return true;
            }
            error = wanted.isEmpty() ? QStringLiteral("no usable address on the interface")
                                     : QStringLiteral("no address usable yet");
            return false;
        }
        closeAll();
    }
    return false;
}

bool ListenAddressSet::syncTo(const QList<QHostAddress>& wanted)
{
    if (!m_active)
        return false;
    bool changed = false;
    for (const QHostAddress& a : QList<QHostAddress>(m_bound)) {
        if (wanted.contains(a))
            continue;
        closeOne(a);
        m_bound.removeOne(a);
        changed = true;
        logInfo(QStringLiteral("%1: stopped listening on %2 (address gone)").arg(setName(), a.toString()));
    }
    m_pending.removeIf([&wanted](const QHostAddress& a) { return !wanted.contains(a); });
    m_reported.removeIf([&wanted](const QHostAddress& a) { return !wanted.contains(a); });

    for (const QHostAddress& a : wanted) {
        if (m_bound.contains(a))
            continue;
        QString why;
        const BindResult r = bindOne(a, m_port, why);
        if (r == BindResult::Ok) {
            if (m_port == 0)
                m_port = portOf(a);
            m_bound.append(a);
            m_pending.removeOne(a);
            m_reported.removeOne(a);
            changed = true;
            logInfo(QStringLiteral("%1: listening on %2 port %3").arg(argStr(setName()), argStr(a.toString()), argStr(m_port)));
            continue;
        }
        if (!m_pending.contains(a))
            m_pending.append(a);
        // once per address: the 5 s watchdog retries
        if (!m_reported.contains(a)) {
            m_reported.append(a);
            const QString line = QStringLiteral("%1: cannot listen on %2 port %3: %4")
                                     .arg(argStr(setName()), argStr(a.toString()), argStr(m_port), argStr(why));
            if (r == BindResult::Later)
                logDebug(line);
            else
                logWarning(line);
        }
    }
    if (changed) {
        // the order asked for: the first of a family sends (UdpSocketSet)
        QList<QHostAddress> ordered;
        for (const QHostAddress& a : wanted)
            if (m_bound.contains(a))
                ordered.append(a);
        m_bound = ordered;
    }
    return changed;
}

void ListenAddressSet::closeAll()
{
    for (const QHostAddress& a : QList<QHostAddress>(m_bound))
        closeOne(a);
    m_bound.clear();
    m_pending.clear();
    m_reported.clear();
    m_port = 0;
    m_active = false;
}

} // namespace eMule
