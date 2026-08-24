#include "pch.h"
/// @file PortMapper.cpp
/// @brief Backend race and mapping lifecycle.

#include "portmap/PortMapper.h"
#include "portmap/NatPmpBackend.h"
#include "portmap/PcpBackend.h"
#include "portmap/PortMapWire.h"
#include "portmap/UPnPBackend.h"
#include "utils/Log.h"

#include <algorithm>

namespace eMule {

namespace {

/// How long the concurrent probe round is allowed to take. All backends probe
/// at once, so this is the whole discovery cost, not a per-protocol budget.
constexpr int kProbeTimeoutMs = 3000;

/// Renewal failures tolerated on one mapping before the whole race is re-run.
constexpr int kMaxRenewFailures = 3;

/// Re-probe backoff after a total failure: 30s, 60s, 120s ... capped at 30 min.
constexpr int kReprobeBaseMs = 30'000;
constexpr int kReprobeMaxMs = 30 * 60'000;

} // namespace

// ============================================================================
// Construction
// ============================================================================

PortMapper::PortMapper(QObject* parent)
    : QObject(parent)
{
    m_probeDeadline.setSingleShot(true);
    connect(&m_probeDeadline, &QTimer::timeout, this, [this] {
        if (m_state == State::Probing) {
            logDebug(QStringLiteral("PortMap: probe deadline reached"));
            finishProbe();
        }
    });

    m_reprobeBackoff.setSingleShot(true);
    connect(&m_reprobeBackoff, &QTimer::timeout, this, [this] {
        if (m_state != State::Stopped)
            beginProbe();
    });
}

PortMapper::~PortMapper()
{
    // Do not release here: the owner decides via stop(), and a destructor is the
    // wrong place to wait on network round trips.
    for (BackendSlot& slot : m_backends) {
        if (slot.backend)
            slot.backend->shutdown();
    }
}

// ============================================================================
// Configuration
// ============================================================================

void PortMapper::setEnabledMethods(uint32 mask)
{
    m_enabledMask = mask;
}

void PortMapper::setLeaseSeconds(uint32 seconds)
{
    m_leaseSeconds = seconds == 0 ? portmap::kDefaultLifetimeSecs : seconds;
}

void PortMapper::setPreferredMethod(PortMapMethod method)
{
    m_preferredMethod = method;
}

// ============================================================================
// Desired state
// ============================================================================

void PortMapper::setDesiredMappings(std::vector<PortMapRequest> requests)
{
    const std::vector<PortMapRequest> previous = m_desired;
    m_desired = std::move(requests);

    if (m_state != State::Active)
        return;

    // Release anything that is no longer wanted, or whose internal port moved.
    for (auto it = m_active.begin(); it != m_active.end();) {
        const auto match = std::find_if(m_desired.begin(), m_desired.end(),
                                        [&](const PortMapRequest& r) {
                                            return r == it->mapping.request;
                                        });
        if (match != m_desired.end()) {
            ++it;
            continue;
        }
        if (PortMapBackend* backend = currentCandidate())
            backend->releaseMapping(it->mapping);
        it = m_active.erase(it);
    }

    // Request anything newly wanted. Unchanged entries keep their lease, so a
    // no-op call really is a no-op on the wire.
    PortMapBackend* backend = currentCandidate();
    if (backend == nullptr)
        return;
    for (const PortMapRequest& request : m_desired) {
        if (!backend->supports(request.family))
            continue;
        const bool alreadyActive = std::any_of(m_active.begin(), m_active.end(),
                                               [&](const Active& a) {
                                                   return a.mapping.request == request;
                                               });
        if (!alreadyActive)
            backend->requestMapping(request, m_leaseSeconds);
    }
    recomputeStatus();
}

void PortMapper::start()
{
    if (m_enabledMask == 0) {
        logInfo(QStringLiteral("PortMap: automatic port forwarding is disabled"));
        setStatus(PortMapStatus::Disabled);
        return;
    }
    m_state = State::Idle;
    buildBackends();
    if (m_backends.empty()) {
        setStatus(PortMapStatus::Disabled);
        return;
    }
    beginProbe();
}

void PortMapper::stop(bool releaseMappings)
{
    m_probeDeadline.stop();
    m_reprobeBackoff.stop();

    if (releaseMappings)
        releaseAll();

    clearActive();
    for (BackendSlot& slot : m_backends) {
        if (slot.backend)
            slot.backend->shutdown();
    }
    m_state = State::Stopped;
    m_activeMethod = PortMapMethod::None;
    setStatus(PortMapStatus::Unknown);
}

void PortMapper::reprobe()
{
    if (m_state == State::Stopped || m_backends.empty())
        return;
    logInfo(QStringLiteral("PortMap: re-probing port-mapping protocols"));
    releaseAll();
    clearActive();
    m_reprobeBackoff.stop();
    beginProbe();
}

// ============================================================================
// Observation
// ============================================================================

std::vector<PortMapping> PortMapper::mappings() const
{
    std::vector<PortMapping> out;
    out.reserve(m_active.size());
    for (const Active& active : m_active)
        out.push_back(active.mapping);
    return out;
}

uint16 PortMapper::externalPort(PortMapPurpose purpose, PortMapProtocol protocol,
                                PortMapFamily family) const
{
    for (const Active& active : m_active) {
        const PortMapRequest& request = active.mapping.request;
        if (request.purpose == purpose && request.protocol == protocol
            && request.family == family)
            return active.mapping.externalPort;
    }
    return 0;
}

void PortMapper::addBackendForTest(std::unique_ptr<PortMapBackend> backend)
{
    m_testBackends = true;
    connectBackend(backend.get());
    m_backends.push_back(BackendSlot{std::move(backend), false, false});
}

// ============================================================================
// Private — backend construction
// ============================================================================

void PortMapper::buildBackends()
{
    if (m_testBackends || !m_backends.empty())
        return;

    auto add = [this](std::unique_ptr<PortMapBackend> backend) {
        connectBackend(backend.get());
        m_backends.push_back(BackendSlot{std::move(backend), false, false});
    };

    // Order here is irrelevant — every backend probes concurrently and the
    // winner is decided by trial quality, then by the PortMapMethod ordinal.
    if ((m_enabledMask & BitPcp) != 0)
        add(std::make_unique<PcpBackend>(this));
    if ((m_enabledMask & BitNatPmp) != 0)
        add(std::make_unique<NatPmpBackend>(this));
    if ((m_enabledMask & BitUPnP) != 0)
        add(std::make_unique<UPnPBackend>(this));
}

void PortMapper::connectBackend(PortMapBackend* backend)
{
    connect(backend, &PortMapBackend::probeFinished, this,
            [this, backend](bool supported, const QString& detail) {
                onProbeFinished(backend, supported, detail);
            });
    connect(backend, &PortMapBackend::mappingResult, this,
            [this, backend](const PortMapping& mapping, bool ok, const QString& error) {
                onMappingResult(backend, mapping, ok, error);
            });
    connect(backend, &PortMapBackend::externalAddressLearned, this,
            [this](const Address& address) { noteExternalAddress(address); });
    connect(backend, &PortMapBackend::mappingsInvalidated, this,
            [this, backend](const QString& reason) {
                onMappingsInvalidated(backend, reason);
            });
}

// ============================================================================
// Private — the race
// ============================================================================

void PortMapper::beginProbe()
{
    m_state = State::Probing;
    setStatus(PortMapStatus::Probing);

    m_candidates.clear();
    m_candidateIndex = 0;
    m_trialResults.clear();
    m_trialMappings.clear();
    m_trialExpected = 0;
    m_trialReceived = 0;

    for (BackendSlot& slot : m_backends) {
        slot.probed = false;
        slot.available = false;
    }

    m_probeDeadline.start(kProbeTimeoutMs);
    // All backends probe at once, so discovery costs one timeout rather than one
    // per protocol. A preferred method only reorders the trial phase below.
    for (BackendSlot& slot : m_backends)
        slot.backend->probe(kProbeTimeoutMs);
}

void PortMapper::onProbeFinished(PortMapBackend* backend, bool supported, const QString& detail)
{
    if (m_state != State::Probing)
        return;

    for (BackendSlot& slot : m_backends) {
        if (slot.backend.get() != backend)
            continue;
        slot.probed = true;
        slot.available = supported;
        logDebug(QStringLiteral("PortMap: %1 probe -> %2%3")
                     .arg(backend->name(),
                          supported ? QStringLiteral("available")
                                    : QStringLiteral("not available"),
                          detail.isEmpty() ? QString()
                                           : QStringLiteral(" (%1)").arg(detail)));
        break;
    }

    const bool allProbed = std::all_of(m_backends.begin(), m_backends.end(),
                                       [](const BackendSlot& s) { return s.probed; });
    if (allProbed)
        finishProbe();
}

void PortMapper::finishProbe()
{
    m_probeDeadline.stop();

    for (usize i = 0; i < m_backends.size(); ++i) {
        if (m_backends[i].available)
            m_candidates.push_back(i);
    }

    // Order: last run's winner first, then by protocol preference (the enum
    // ordinal). Exact-port-match can still promote a later candidate.
    std::stable_sort(m_candidates.begin(), m_candidates.end(),
                     [this](usize a, usize b) {
                         const PortMapMethod ma = m_backends[a].backend->method();
                         const PortMapMethod mb = m_backends[b].backend->method();
                         const bool prefA = ma == m_preferredMethod;
                         const bool prefB = mb == m_preferredMethod;
                         if (prefA != prefB)
                             return prefA;
                         return static_cast<uint8>(ma) < static_cast<uint8>(mb);
                     });

    if (m_candidates.empty()) {
        logWarning(QStringLiteral("PortMap: no port-mapping protocol available on this network"));
        m_state = State::Idle;
        setStatus(PortMapStatus::NotMapped);
        const int delay = std::min(kReprobeBaseMs << std::min(m_reprobeAttempt, 6), kReprobeMaxMs);
        ++m_reprobeAttempt;
        m_reprobeBackoff.start(delay);
        return;
    }

    m_reprobeAttempt = 0;
    m_candidateIndex = 0;
    tryNextCandidate();
}

void PortMapper::tryNextCandidate()
{
    if (m_candidateIndex >= m_candidates.size()) {
        commitBestTrial();
        return;
    }

    PortMapBackend* backend = m_backends[m_candidates[m_candidateIndex]].backend.get();
    m_state = State::Trialling;
    m_trialMappings.clear();
    m_trialReceived = 0;
    m_trialExpected = desiredCountFor(backend);

    if (m_trialExpected == 0) {
        ++m_candidateIndex;
        tryNextCandidate();
        return;
    }

    logDebug(QStringLiteral("PortMap: trying %1 for %2 mapping(s)")
                 .arg(backend->name())
                 .arg(m_trialExpected));

    // requestMapping() is allowed to answer synchronously, and UdpMappingBackend does for
    // "no gateway for this family", "could not encode request" and "could not open a
    // socket". Without this flag the resulting mappingResult would re-enter
    // evaluateTrial() -> tryNextCandidate() and reset m_trial* underneath the loop still
    // issuing requests against the previous candidate. Today the count can only complete
    // on the final iteration, so it survives by arithmetic accident; the flag makes it
    // structural, and moves the trial on once every request is out.
    m_issuingTrialRequests = true;
    for (const PortMapRequest& request : m_desired) {
        if (backend->supports(request.family))
            backend->requestMapping(request, m_leaseSeconds);
    }
    m_issuingTrialRequests = false;

    if (m_state == State::Trialling && m_trialReceived >= m_trialExpected)
        evaluateTrial();
}

void PortMapper::evaluateTrial()
{
    const usize slot = m_candidates[m_candidateIndex];
    const TrialQuality quality = gradeTrial(m_trialMappings, m_trialExpected);

    m_trialResults.push_back(TrialResult{slot, m_trialMappings, quality});
    logDebug(QStringLiteral("PortMap: %1 trial graded %2")
                 .arg(m_backends[slot].backend->name())
                 .arg(static_cast<int>(quality)));

    // An exact match cannot be beaten, so stop the race immediately.
    if (quality == TrialQuality::Exact) {
        commitBestTrial();
        return;
    }

    ++m_candidateIndex;
    tryNextCandidate();
}

void PortMapper::commitBestTrial()
{
    const auto best = std::max_element(m_trialResults.begin(), m_trialResults.end(),
                                       [](const TrialResult& a, const TrialResult& b) {
                                           return a.quality < b.quality;
                                       });

    if (best == m_trialResults.end() || best->quality == TrialQuality::None) {
        logWarning(QStringLiteral("PortMap: no backend could create a mapping"));
        m_state = State::Idle;
        setStatus(PortMapStatus::Failed);
        return;
    }

    // Release whatever the losing backends managed to set up, so we do not leave
    // duplicate mappings for the same ports behind on the router.
    for (const TrialResult& result : m_trialResults) {
        if (&result == &*best)
            continue;
        PortMapBackend* backend = m_backends[result.slot].backend.get();
        for (const PortMapping& mapping : result.mappings) {
            if (mapping.externalPort != 0)
                backend->releaseMapping(mapping);
        }
    }

    PortMapBackend* winner = m_backends[best->slot].backend.get();
    m_state = State::Active;

    clearActive();
    for (const PortMapping& mapping : best->mappings) {
        if (mapping.externalPort == 0)
            continue;
        Active active;
        active.mapping = mapping;
        m_active.push_back(std::move(active));
        scheduleRenewal(m_active.back());
        emit mappingChanged(mapping, true);
    }

    if (m_activeMethod != winner->method()) {
        m_activeMethod = winner->method();
        emit methodChanged(m_activeMethod);
        emit preferredMethodLearned(m_activeMethod);
    }

    logInfo(QStringLiteral("PortMap: using %1 — %2 mapping(s) active")
                .arg(winner->name())
                .arg(m_active.size()));
    recomputeStatus();
}

PortMapper::TrialQuality PortMapper::gradeTrial(const std::vector<PortMapping>& mappings,
                                                usize expected)
{
    if (mappings.empty())
        return TrialQuality::None;

    const usize granted = static_cast<usize>(
        std::count_if(mappings.begin(), mappings.end(),
                      [](const PortMapping& m) { return m.externalPort != 0; }));
    if (granted == 0)
        return TrialQuality::None;

    const bool allGranted = granted >= expected;
    const bool allExact = std::all_of(mappings.begin(), mappings.end(),
                                      [](const PortMapping& m) { return m.portMatches(); });

    if (allGranted && allExact)
        return TrialQuality::Exact;
    if (allGranted)
        return TrialQuality::Mismatched;
    return TrialQuality::Partial;
}

// ============================================================================
// Private — results and renewal
// ============================================================================

void PortMapper::onMappingResult(PortMapBackend* backend, const PortMapping& mapping,
                                 bool ok, const QString& error)
{
    if (m_state == State::Trialling) {
        if (m_candidateIndex >= m_candidates.size()
            || m_backends[m_candidates[m_candidateIndex]].backend.get() != backend)
            return;   // a straggler from a candidate we already moved past

        ++m_trialReceived;
        if (ok)
            m_trialMappings.push_back(mapping);
        else
            logDebug(QStringLiteral("PortMap: %1 could not map %2/%3: %4")
                         .arg(backend->name(),
                              portMapPurposeName(mapping.request.purpose))
                         .arg(mapping.request.internalPort)
                         .arg(error));

        // Deferred while tryNextCandidate() is still issuing this candidate's requests —
        // it calls evaluateTrial() itself once the loop is done. See the note there.
        if (m_trialReceived >= m_trialExpected && !m_issuingTrialRequests)
            evaluateTrial();
        return;
    }

    if (m_state != State::Active)
        return;

    const auto it = std::find_if(m_active.begin(), m_active.end(), [&](const Active& a) {
        return a.mapping.request.key() == mapping.request.key();
    });

    if (it == m_active.end()) {
        // A mapping requested by setDesiredMappings() after the race finished:
        // there is no Active entry to update yet, so adopt it here. Without
        // this, mappings added mid-session are silently dropped.
        const bool wanted = std::any_of(m_desired.begin(), m_desired.end(),
                                        [&](const PortMapRequest& r) {
                                            return r.key() == mapping.request.key();
                                        });
        if (!wanted)
            return;
        if (!ok) {
            logWarning(QStringLiteral("PortMap: could not map %1: %2")
                           .arg(portMapPurposeName(mapping.request.purpose), error));
            emit mappingChanged(mapping, false);
            return;
        }
        Active adopted;
        adopted.mapping = mapping;
        m_active.push_back(std::move(adopted));
        scheduleRenewal(m_active.back());
        emit mappingChanged(mapping, true);
        recomputeStatus();
        return;
    }

    if (ok) {
        it->mapping = mapping;
        it->consecutiveFailures = 0;
        it->renewAttempt = 0;
        scheduleRenewal(*it);
        emit mappingChanged(mapping, true);
    } else {
        ++it->consecutiveFailures;
        logWarning(QStringLiteral("PortMap: renewing %1 failed (%2/%3): %4")
                       .arg(portMapPurposeName(mapping.request.purpose))
                       .arg(it->consecutiveFailures)
                       .arg(kMaxRenewFailures)
                       .arg(error));
        emit mappingChanged(it->mapping, false);
        if (it->consecutiveFailures >= kMaxRenewFailures) {
            // The router stopped honouring this method; start over rather than
            // retrying a mapping it no longer holds.
            reprobe();
            return;
        }
        ++it->renewAttempt;
        scheduleRenewal(*it);
    }
    recomputeStatus();
}

void PortMapper::onMappingsInvalidated(PortMapBackend* backend, const QString& reason)
{
    if (m_state != State::Active || currentCandidate() != backend)
        return;

    logWarning(QStringLiteral("PortMap: %1 lost state (%2) — re-adding every mapping")
                   .arg(backend->name(), reason));

    // Re-add immediately, with the renewal jitter spread so several mappings do
    // not all fire in the same millisecond.
    int index = 0;
    for (Active& active : m_active) {
        active.renewAttempt = 0;
        active.renewTimer->start(100 * (++index));
    }
}

void PortMapper::scheduleRenewal(Active& active)
{
    if (!active.renewTimer) {
        active.renewTimer = std::make_unique<QTimer>();
        active.renewTimer->setSingleShot(true);
        const PortMapRequest request = active.mapping.request;
        connect(active.renewTimer.get(), &QTimer::timeout, this, [this, request] {
            if (m_state != State::Active)
                return;
            if (PortMapBackend* backend = currentCandidate())
                backend->requestMapping(request, m_leaseSeconds);
        });
    }

    if (m_renewalOverrideMs >= 0) {
        active.renewTimer->start(m_renewalOverrideMs);
        return;
    }

    uint32 delaySecs = 0;
    if (active.mapping.lifetimeSecs == 0) {
        // An indefinite lease (IGD1 permanent mapping) still needs periodic
        // verification, but there is nothing to renew against — poll slowly.
        delaySecs = 30 * 60;
    } else if (active.mapping.method == PortMapMethod::NatPmp) {
        delaySecs = portmap::natPmpRenewalDelaySecs(active.mapping.lifetimeSecs);
    } else {
        delaySecs = portmap::renewalDelaySecs(active.mapping.lifetimeSecs,
                                              active.renewAttempt, m_renewalRandom);
    }

    active.renewTimer->start(static_cast<int>(delaySecs) * 1000);
}

void PortMapper::releaseAll()
{
    PortMapBackend* backend = currentCandidate();
    if (backend == nullptr)
        return;
    for (Active& active : m_active)
        backend->releaseMapping(active.mapping);
}

void PortMapper::clearActive()
{
    for (Active& active : m_active) {
        if (active.renewTimer)
            active.renewTimer->stop();
    }
    m_active.clear();
}

// ============================================================================
// Private — status
// ============================================================================

void PortMapper::setStatus(PortMapStatus status)
{
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged(status);
}

void PortMapper::recomputeStatus()
{
    if (m_active.empty()) {
        setStatus(m_state == State::Active ? PortMapStatus::NotMapped : m_status);
        return;
    }

    const bool allUsable = std::all_of(m_active.begin(), m_active.end(),
                                       [](const Active& a) { return a.mapping.isUsable(); });
    // Degraded, not Mapped: a granted-but-unreachable mapping reported as
    // success would show "forwarded" beside a permanently firewalled client.
    setStatus(allUsable ? PortMapStatus::Mapped : PortMapStatus::Degraded);
}

void PortMapper::noteExternalAddress(const Address& address)
{
    if (address.isNull() || m_externalAddress == address)
        return;
    m_externalAddress = address;

    if (!address.isPublicIP()) {
        // Carrier-grade NAT (100.64.0.0/10) and double NAT both land here: the
        // mapping succeeds at the router while the port stays unreachable.
        logWarning(QStringLiteral("PortMap: external address %1 is not publicly "
                                  "routable — inbound connections will not arrive")
                       .arg(address.toString()));
    } else {
        logInfo(QStringLiteral("PortMap: external address %1").arg(address.toString()));
    }

    emit externalAddressChanged(address);
    recomputeStatus();
}

PortMapBackend* PortMapper::currentCandidate() const
{
    if (m_activeMethod != PortMapMethod::None) {
        for (const BackendSlot& slot : m_backends) {
            if (slot.backend && slot.backend->method() == m_activeMethod)
                return slot.backend.get();
        }
    }
    if (m_state == State::Trialling && m_candidateIndex < m_candidates.size())
        return m_backends[m_candidates[m_candidateIndex]].backend.get();
    return nullptr;
}

usize PortMapper::desiredCountFor(const PortMapBackend* backend) const
{
    return static_cast<usize>(
        std::count_if(m_desired.begin(), m_desired.end(), [backend](const PortMapRequest& r) {
            return backend->supports(r.family);
        }));
}

} // namespace eMule
