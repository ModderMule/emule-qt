#pragma once

/// @file UPnPWorker.h
/// @brief Blocking miniupnpc calls, isolated on a worker thread.
///
/// miniupnpc's SSDP discovery and SOAP commands are synchronous and can stall
/// for seconds against a slow or absent router. Everything that blocks lives
/// here and is reached only through queued signals, so the core event loop never
/// waits on the network. The old UPnPManager called into miniupnpc directly from
/// the 100 ms CoreSession tick, which is exactly the stall this removes.
///
/// Ownership: created on the main thread, moved onto a QThread by UPnPBackend,
/// and destroyed via deleteLater() on that thread.

#include "portmap/PortMapTypes.h"

#include <QObject>
#include <QString>

#include <atomic>
#include <memory>

namespace eMule {

class UPnPWorker : public QObject {
    Q_OBJECT

public:
    explicit UPnPWorker(QObject* parent = nullptr);
    ~UPnPWorker() override;

    UPnPWorker(const UPnPWorker&) = delete;
    UPnPWorker& operator=(const UPnPWorker&) = delete;

    /// Ask the current operation to stop at the next checkpoint.
    ///
    /// Cooperative rather than QThread::terminate(): killing a thread inside a
    /// SOAP call leaks the socket and the UPNPUrls allocation with no way to
    /// recover them.
    void requestAbort() { m_abort.store(true, std::memory_order_relaxed); }
    void clearAbort() { m_abort.store(false, std::memory_order_relaxed); }

public slots:
    /// SSDP discovery plus IGD selection. Emits discovered().
    void discover(const QString& bindAddress);

    /// Create or renew one mapping. Emits mapped().
    void addMapping(const eMule::PortMapRequest& request, uint32 lifetimeSecs);

    /// Best-effort removal. Emits mapped() with ok=true once attempted.
    void deleteMapping(const eMule::PortMapping& mapping);

    /// Release the IGD handles. Safe to call more than once.
    void releaseIGD();

signals:
    /// @param pinholesAvailable true only when the device advertises
    ///        WANIPv6FirewallControl *and* GetFirewallStatus reports both
    ///        FirewallEnabled and InboundPinholeAllowed. Devices routinely
    ///        advertise the service and then refuse every pinhole.
    void discovered(bool ok, const QString& detail, const eMule::Address& externalAddress,
                    bool pinholesAvailable);
    void mapped(const eMule::PortMapping& mapping, bool ok, const QString& error);

private:
    struct IGDState;

    // Private helpers after the public interface, per the project standard.
    [[nodiscard]] bool aborted() const { return m_abort.load(std::memory_order_relaxed); }
    [[nodiscard]] bool addPortMapping(const PortMapRequest& request, uint32 lifetimeSecs,
                                      PortMapping& out, QString& error);
    [[nodiscard]] bool addPinhole(const PortMapRequest& request, uint32 lifetimeSecs,
                                  PortMapping& out, QString& error);

    std::unique_ptr<IGDState> m_igd;
    std::atomic<bool>         m_abort{false};
    bool                      m_pinholesAvailable = false;
};

} // namespace eMule
