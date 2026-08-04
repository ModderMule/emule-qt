#include "pch.h"
/// @file UPnPBackend.cpp
/// @brief UPnP IGD backend — main-thread signal source over a UPnPWorker.

#include "portmap/UPnPBackend.h"
#include "portmap/UPnPWorker.h"
#include "utils/Log.h"

namespace eMule {

namespace {

/// How long to wait for the worker to notice the abort flag before giving up on
/// it. A SOAP call to an unresponsive router can sit in connect() for a while.
constexpr int kShutdownWaitMs = 7000;

} // namespace

UPnPBackend::UPnPBackend(QObject* parent)
    : PortMapBackend(parent)
{
}

UPnPBackend::~UPnPBackend()
{
    shutdown();
}

bool UPnPBackend::supports(PortMapFamily family) const
{
    if (family == PortMapFamily::IPv4)
        return true;
    // Only after a probe has confirmed the device both advertises
    // WANIPv6FirewallControl and reports InboundPinholeAllowed.
    return m_pinholesAvailable;
}

void UPnPBackend::probe(int timeoutMs)
{
    Q_UNUSED(timeoutMs)   // miniupnpc's SSDP timeout is fixed inside the worker

    ensureThread();
    m_probing = true;
    m_worker->clearAbort();
    // Functor form rather than Q_ARG: the string-name overload matches on the
    // spelling of the type, so Q_ARG(uint32, ...) would fail to resolve against
    // a slot declared with the underlying unsigned int.
    UPnPWorker* worker = m_worker;
    const QString bindAddress = m_bindAddress;
    QMetaObject::invokeMethod(worker, [worker, bindAddress] { worker->discover(bindAddress); },
                              Qt::QueuedConnection);
}

void UPnPBackend::requestMapping(const PortMapRequest& request, uint32 lifetimeSecs)
{
    if (!m_ready || m_worker == nullptr) {
        PortMapping mapping;
        mapping.request = request;
        mapping.method = PortMapMethod::UPnP;
        emit mappingResult(mapping, false, QStringLiteral("UPnP not ready"));
        return;
    }
    UPnPWorker* worker = m_worker;
    QMetaObject::invokeMethod(
        worker, [worker, request, lifetimeSecs] { worker->addMapping(request, lifetimeSecs); },
        Qt::QueuedConnection);
}

void UPnPBackend::releaseMapping(const PortMapping& mapping)
{
    if (!m_ready || m_worker == nullptr)
        return;
    UPnPWorker* worker = m_worker;
    QMetaObject::invokeMethod(worker, [worker, mapping] { worker->deleteMapping(mapping); },
                              Qt::QueuedConnection);
}

void UPnPBackend::shutdown()
{
    if (m_worker != nullptr)
        m_worker->requestAbort();

    if (m_thread.isRunning()) {
        m_thread.quit();
        if (!m_thread.wait(kShutdownWaitMs)) {
            // Deliberately leak rather than terminate(): killing the thread
            // inside a SOAP call would leave the socket and the UPNPUrls
            // allocation unrecoverable, and the process is exiting anyway.
            logWarning(QStringLiteral("UPnP: worker did not finish in %1 ms; "
                                      "abandoning it rather than terminating")
                           .arg(kShutdownWaitMs));
            m_worker = nullptr;
            return;
        }
    }
    m_worker = nullptr;
    m_ready = false;
    m_probing = false;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void UPnPBackend::ensureThread()
{
    if (m_worker != nullptr)
        return;

    m_worker = new UPnPWorker;
    m_worker->moveToThread(&m_thread);
    // The worker belongs to the thread, so it must be destroyed there.
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &UPnPWorker::discovered, this,
            [this](bool ok, const QString& detail, const Address& externalAddress,
                   bool pinholesAvailable) {
                m_ready = ok;
                m_pinholesAvailable = ok && pinholesAvailable;
                if (ok && !externalAddress.isNull())
                    emit externalAddressLearned(externalAddress);
                if (m_probing) {
                    m_probing = false;
                    emit probeFinished(ok, detail);
                }
            });

    connect(m_worker, &UPnPWorker::mapped, this,
            [this](const PortMapping& mapping, bool ok, const QString& error) {
                emit mappingResult(mapping, ok, error);
            });

    m_thread.setObjectName(QStringLiteral("UPnP-Worker"));
    m_thread.start();
}

} // namespace eMule
