#include "IpcListenWatch.h"

#include "IpcServer.h"
#include "net/ListenConflict.h"
#include "net/OwnAddress.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"

#include <QHostInfo>
#include <QNetworkInterface>

#include <algorithm>

namespace eMule {

namespace {
constexpr int kTickMs = 30 * 1000;            ///< adapter / host name poll; no DNS
constexpr int kFollowUpMs = 2 * 60 * 1000;    ///< DNS may learn a new address late
} // namespace

IpcListenWatch::IpcListenWatch(IpcServer* server, const QString& name, QObject* parent)
    : QObject(parent)
    , m_server(server)
    , m_name(name)
    , m_state(networkState())
{
    m_sinceLookup.start();   // start() has just looked it up
    connect(&m_tick, &QTimer::timeout, this, &IpcListenWatch::onTick);
    m_tick.start(kTickMs);
}

void IpcListenWatch::lookUp()
{
    if (m_looking)
        return;
    m_looking = true;
    m_sinceLookup.restart();
    OwnAddress::refresh(m_name, this, [this](bool answered) {
        m_looking = false;
        if (!answered) {   // DNS down: keep what we listen on
            logWarning(QStringLiteral("IPC listen host %1 did not resolve: still listening on %2")
                           .arg(m_name, addressListText(m_server->listenAddresses())));
            return;
        }
        // From the answer just cached: no wait
        const auto addrs = Preferences::ipcBindAddressesOf(m_name);
        if (!addrs)
            return;
        QList<QHostAddress> want = *addrs;
        QList<QHostAddress> have = m_server->listenAddresses();
        const auto byText = [](const QHostAddress& a, const QHostAddress& b) {
            return a.toString() < b.toString();
        };
        std::ranges::sort(want, byText);
        std::ranges::sort(have, byText);
        if (want == have)
            return;
        logInfo(QStringLiteral("IPC listen host %1 is now %2")
                    .arg(m_name, addressListText(*addrs)));
        m_server->relisten(*addrs);
    });
}

void IpcListenWatch::onTick()
{
    if (const QString state = networkState(); state != m_state) {
        m_state = state;
        logInfo(QStringLiteral("Network addresses or host name changed: looking up IPC "
                               "listen host %1 again").arg(m_name));
        lookUp();
        QTimer::singleShot(kFollowUpMs, this, &IpcListenWatch::lookUp);
        return;
    }
    const int minutes = thePrefs.ipcListenRecheckMinutes();
    if (minutes > 0 && m_sinceLookup.elapsed() >= qint64(minutes) * 60 * 1000)
        lookUp();
}

QString IpcListenWatch::networkState()
{
    QStringList parts;
    for (QHostAddress addr : QNetworkInterface::allAddresses()) {
        addr.setScopeId({});
        parts.append(addr.toString());
    }
    parts.sort();
    parts.append(QHostInfo::localHostName());
    return parts.join(u' ');
}

} // namespace eMule
