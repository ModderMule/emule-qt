#include "pch.h"
/// @file GluetunPortSync.cpp
/// @brief Gluetun forwarded port and VPN health — implementation.

#include "portmap/GluetunPortSync.h"

#include "prefs/Preferences.h"
#include "webserver/ArrCompat.h"
#include "utils/Log.h"

#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace eMule {

namespace {

constexpr int kRequestTimeoutMs = 10 * 1000;

} // namespace

GluetunPortSync::GluetunPortSync(QObject* parent)
    : QObject(parent)
{
    // the control server is local (same network namespace)
    m_nam.setProxy(QNetworkProxy::NoProxy);
    m_timer.setInterval(kPollMs);
    connect(&m_timer, &QTimer::timeout, this, &GluetunPortSync::poll);
}

void GluetunPortSync::applyPreferences()
{
    QString base = thePrefs.gluetunUrl().trimmed();
    if (!base.isEmpty() && !base.contains(QLatin1StringView("://")))
        base.prepend(QStringLiteral("http://"));
    while (base.endsWith(u'/'))
        base.chop(1);
    const bool on = thePrefs.gluetunEnabled() && !base.isEmpty();
    const bool changed = base != m_base || m_apiKey != thePrefs.gluetunApiKey().toUtf8();
    m_base = base;
    m_apiKey = thePrefs.gluetunApiKey().toUtf8();
    if (!on) {
        if (m_timer.isActive())
            logInfo(QStringLiteral("Gluetun: port sync off"));
        m_timer.stop();
        m_port = 0;
        return;
    }
    if (changed || !m_timer.isActive()) {
        m_legacyPortRoute = false;
        m_legacyStatusRoute = false;
        m_warnedUnreachable = false;
        m_timer.start();
        poll();
    }
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

void GluetunPortSync::poll()
{
    get(m_legacyStatusRoute ? QStringLiteral("/v1/openvpn/status") : QStringLiteral("/v1/vpn/status"),
        [this](int status, const QByteArray& body) {
            if (status == 404 && !m_legacyStatusRoute) {
                m_legacyStatusRoute = true;   // before v3.40
                return;
            }
            if (status != 200)
                return;
            const QString vpn = arr::parseGluetunStatus(body);
            if (!vpn.isEmpty() && vpn != m_status) {
                if (vpn == QLatin1StringView("running"))
                    logInfo(QStringLiteral("Gluetun: VPN running"));
                else
                    logWarning(QStringLiteral("Gluetun: VPN %1").arg(vpn));
                m_status = vpn;
            }
        });

    get(QStringLiteral("/v1/publicip/ip"), [this](int status, const QByteArray& body) {
        if (status != 200)
            return;
        const QString ip = arr::parseGluetunPublicIp(body);
        if (!ip.isEmpty() && ip != m_publicIp) {
            logInfo(QStringLiteral("Gluetun: public IP %1").arg(ip));
            m_publicIp = ip;
        }
    });

    get(m_legacyPortRoute ? QStringLiteral("/v1/openvpn/portforwarded") : QStringLiteral("/v1/portforward"),
        [this](int status, const QByteArray& body) {
            if (status == 404 && !m_legacyPortRoute) {
                m_legacyPortRoute = true;
                poll();
                return;
            }
            if (status == 0) {
                if (!m_warnedUnreachable)
                    logWarning(QStringLiteral("Gluetun: control server %1 not reachable").arg(m_base));
                m_warnedUnreachable = true;
                return;
            }
            if (status == 401 || status == 403) {
                if (!m_warnedUnreachable)
                    logWarning(QStringLiteral("Gluetun: control server refused the request (API key / auth config)"));
                m_warnedUnreachable = true;
                return;
            }
            m_warnedUnreachable = false;
            const quint16 port = status == 200 ? arr::parseGluetunPort(body) : quint16(0);
            if (port == 0 || port == m_port)
                return;
            logInfo(QStringLiteral("Gluetun: forwarded port %1").arg(port));
            m_port = port;
            emit forwardedPortChanged(port);
        });
}

void GluetunPortSync::get(const QString& path, std::function<void(int, const QByteArray&)> done)
{
    QNetworkRequest req(QUrl(m_base + path));
    if (!m_apiKey.isEmpty())
        req.setRawHeader("X-API-Key", m_apiKey);
    req.setTransferTimeout(kRequestTimeoutMs);
    QNetworkReply* reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        done(status, reply->readAll());
    });
}

} // namespace eMule
