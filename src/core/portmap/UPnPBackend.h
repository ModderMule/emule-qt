#pragma once

/// @file UPnPBackend.h
/// @brief UPnP IGD backend — main-thread signal source over a UPnPWorker.
///
/// Replaces UPnPManager. Behavioural differences worth knowing:
///
///  * All blocking miniupnpc work happens on a worker thread, so nothing here
///    can stall the core event loop.
///  * Leases are finite and renewed, instead of the old indefinite "0" lease
///    re-verified from the 30 s tick.
///  * Shutdown is a cooperative abort plus a bounded wait — never
///    QThread::terminate() inside a SOAP call.
///  * IPv6 is supported through IGD2 pinholes (WANIPv6FirewallControl).

#include "portmap/PortMapBackend.h"

#include <QThread>

namespace eMule {

class UPnPWorker;

class UPnPBackend : public PortMapBackend {
    Q_OBJECT

public:
    explicit UPnPBackend(QObject* parent = nullptr);
    ~UPnPBackend() override;

    [[nodiscard]] PortMapMethod method() const override { return PortMapMethod::UPnP; }

    /// IPv6 support depends on the device advertising WANIPv6FirewallControl
    /// *and* permitting pinholes, which is only known after probing.
    [[nodiscard]] bool supports(PortMapFamily family) const override;

    void probe(int timeoutMs) override;
    void requestMapping(const PortMapRequest& request, uint32 lifetimeSecs) override;
    void releaseMapping(const PortMapping& mapping) override;
    void shutdown() override;

    /// Bind discovery to one interface on a multi-homed host.
    void setBindAddress(const QString& address) { m_bindAddress = address; }

private:
    void ensureThread();

    QThread     m_thread;
    UPnPWorker* m_worker = nullptr;      ///< owned by m_thread, deleted on quit
    QString     m_bindAddress;
    bool        m_ready = false;
    bool        m_pinholesAvailable = false;
    bool        m_probing = false;
};

} // namespace eMule
