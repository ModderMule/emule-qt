#pragma once

/// @file PortMapBackend.h
/// @brief Abstract interface every port-mapping protocol implements.
///
/// Contract, which PortMapper relies on and every backend must honour:
///
///  * Every signal is emitted on the backend's own (main) thread. A backend that
///    does blocking work runs it on a worker and marshals the result back — the
///    facade never sees another thread.
///  * Each of probe() / requestMapping() / releaseMapping() emits exactly one
///    terminal signal, even on failure. Silence deadlocks the state machine.
///  * shutdown() returns promptly (target: single-digit milliseconds) and emits
///    nothing afterwards.
///  * Discovery is the backend's own business: PCP and NAT-PMP consult the route
///    table, UPnP does SSDP. The facade never passes a gateway in.

#include "portmap/PortMapTypes.h"

#include <QObject>
#include <QString>

namespace eMule {

class PortMapBackend : public QObject {
    Q_OBJECT

public:
    explicit PortMapBackend(QObject* parent = nullptr) : QObject(parent) {}
    ~PortMapBackend() override = default;

    [[nodiscard]] virtual PortMapMethod method() const = 0;
    [[nodiscard]] virtual bool supports(PortMapFamily family) const = 0;

    /// Display name, derived from method() — deliberately not virtual.
    [[nodiscard]] QString name() const { return portMapMethodName(method()); }

    /// Is a server of this protocol reachable? Must not create any mapping.
    /// Emits probeFinished() within roughly @p timeoutMs.
    virtual void probe(int timeoutMs) = 0;

    /// Create or renew @p request. Emits mappingResult().
    virtual void requestMapping(const PortMapRequest& request, uint32 lifetimeSecs) = 0;

    /// Best-effort removal. Emits mappingResult() with the released mapping.
    ///
    /// Deliberately best-effort: the dev FRITZ!Box never acknowledges a PCP
    /// delete, so callers must not block on this. Finite leases are the real
    /// cleanup mechanism.
    virtual void releaseMapping(const PortMapping& mapping) = 0;

    /// Stop everything in flight. No signals after this returns.
    virtual void shutdown() = 0;

signals:
    void probeFinished(bool supported, const QString& detail);
    void mappingResult(const eMule::PortMapping& mapping, bool ok, const QString& error);
    void externalAddressLearned(const eMule::Address& address);

    /// The server lost state (PCP epoch violation, NAT-PMP SSSoE reset), so
    /// every mapping it held must be re-added now rather than at lease expiry.
    void mappingsInvalidated(const QString& reason);
};

} // namespace eMule
