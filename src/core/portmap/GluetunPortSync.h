#pragma once

/// @file GluetunPortSync.h
/// @brief Reads the VPN's forwarded port from a Gluetun control server and reports
///        changes; also logs the VPN state and public IP (health).
///
/// With `network_mode: service:gluetun` every packet leaves through the VPN, so UPnP
/// and NAT-PMP see nothing; the provider's forwarded port is the only way in. Gluetun
/// changes it on every reconnect. Routes (control server, default :8000):
/// /v1/portforward (v3.40+) or /v1/openvpn/portforwarded, /v1/vpn/status or
/// /v1/openvpn/status, /v1/publicip/ip. Auth: X-API-Key when one is set.

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>

namespace eMule {

class GluetunPortSync : public QObject {
    Q_OBJECT

public:
    explicit GluetunPortSync(QObject* parent = nullptr);

    /// Reads gluetunEnabled / gluetunUrl / gluetunApiKey; polls at once when on.
    void applyPreferences();

    [[nodiscard]] quint16 forwardedPort() const { return m_port; }

    static constexpr int kPollMs = 60 * 1000;

signals:
    /// A new forwarded port (not sent again while it stays the same).
    void forwardedPortChanged(quint16 port);

private:
    void poll();
    void get(const QString& path, std::function<void(int status, const QByteArray& body)> done);

    QNetworkAccessManager m_nam;
    QTimer m_timer;
    QString m_base;
    QByteArray m_apiKey;
    quint16 m_port = 0;
    bool m_legacyPortRoute = false;
    bool m_legacyStatusRoute = false;
    QString m_status;
    QString m_publicIp;
    bool m_warnedUnreachable = false;
};

} // namespace eMule
