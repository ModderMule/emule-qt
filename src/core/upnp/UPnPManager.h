#pragma once

/// @file UPnPManager.h
/// @brief Cross-platform UPnP port mapping manager using miniupnpc.
///
/// Replaces the MFC UPnPImpl / UPnPImplMiniLib / UPnPImplWrapper /
/// FirewallOpener hierarchy with a single concrete class.

#include "utils/Types.h"

#include <QObject>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>
#include <mutex>

namespace eMule {

class UPnPManager : public QObject {
    Q_OBJECT

public:
    explicit UPnPManager(QObject* parent = nullptr);
    ~UPnPManager() override;

    UPnPManager(const UPnPManager&) = delete;
    UPnPManager& operator=(const UPnPManager&) = delete;

    /// Start async UPnP discovery and port mapping.
    void startDiscovery(uint16 tcpPort, uint16 udpPort, uint16 webPort = 0);

    /// Verify existing mappings are still active; re-map if lost.
    bool checkAndRefresh();

    /// Remove all port mappings and stop discovery.
    void deletePorts();

    /// Cancel ongoing discovery.
    void stopDiscovery();

    /// Add web server port to existing mapping (late enable).
    void enableWebServerPort(uint16 port);

    [[nodiscard]] bool isReady() const;

    enum class PortStatus { Unknown, Forwarded, NotForwarded, Failed };
    Q_ENUM(PortStatus)

    [[nodiscard]] PortStatus portStatus() const;

    /// Optional: bind address for multi-homed hosts.
    void setBindAddress(const QString& addr);

signals:
    void discoveryComplete(bool success);
    void portStatusChanged(PortStatus status);

private:
    struct IGDState;

    void runDiscovery();
    bool openPort(uint16 port, const char* proto, const char* description);
    bool verifyPort(uint16 port, const char* proto);
    void closePort(uint16 port, const char* proto);
    void cleanupIGD();
    void setStatus(PortStatus status);

    std::unique_ptr<IGDState> m_igd;
    uint16 m_tcpPort = 0;
    uint16 m_udpPort = 0;
    uint16 m_webPort = 0;
    uint16 m_oldTcpPort = 0;
    uint16 m_oldUdpPort = 0;
    uint16 m_oldWebPort = 0;
    QString m_bindAddress;
    std::atomic<bool> m_abortDiscovery{false};
    std::atomic<PortStatus> m_status{PortStatus::Unknown};
    bool m_ready = false;
    bool m_succeededOnce = false;
    int m_consecutiveRefreshFailures = 0;
    static constexpr int MaxRefreshRetries = 5;
    QThread* m_discoveryThread = nullptr;
    mutable std::mutex m_mutex;
};

} // namespace eMule
