#pragma once

/// @file PortMapper.h
/// @brief Facade that races the port-mapping backends and keeps mappings alive.
///
/// Owns a set of PortMapBackend implementations, decides which one this router
/// actually speaks, and maintains the desired mappings against it.
///
/// Two design rules make the whole thing testable and keep it off the hot path:
///
///  * PortMapper never touches ListenSocket / ClientUDPSocket / WebServer.
///    CoreSession owns those and hands over a desired-state vector, so
///    tst_PortMapper runs against a fake backend with no network at all.
///  * Renewal is driven by one QTimer per mapping, sized from the lifetime the
///    router actually granted — never by the CoreSession tick ladder, which
///    previously ran synchronous SOAP on the event loop every 30 seconds.

#include "portmap/PortMapBackend.h"
#include "portmap/PortMapTypes.h"

#include <QObject>
#include <QTimer>

#include <memory>
#include <vector>

namespace eMule {

class PortMapper : public QObject {
    Q_OBJECT

public:
    /// Bit values for setEnabledMethods(), matching the portMapProtocols pref.
    enum MethodBit : uint32 { BitPcp = 1, BitNatPmp = 2, BitUPnP = 4, BitAll = 7 };

    explicit PortMapper(QObject* parent = nullptr);
    ~PortMapper() override;

    PortMapper(const PortMapper&) = delete;
    PortMapper& operator=(const PortMapper&) = delete;

    // -- Configuration (before start()) ------------------------------------

    void setEnabledMethods(uint32 mask);
    void setLeaseSeconds(uint32 seconds);

    /// Seed the winner learned on a previous run, so the common case costs one
    /// short probe instead of a full race. Ignored if that method is disabled.
    void setPreferredMethod(PortMapMethod method);

    // -- Desired state ------------------------------------------------------

    /// Declare the mappings that should exist. Idempotent and diff-based:
    /// unchanged entries keep their lease, removed entries are released.
    void setDesiredMappings(std::vector<PortMapRequest> requests);

    void start();
    void stop(bool releaseMappings);

    /// Re-run the backend race — on a network change, or from the GUI's
    /// "recheck firewall" action.
    void reprobe();

    // -- Observation --------------------------------------------------------

    [[nodiscard]] PortMapStatus status() const noexcept { return m_status; }
    [[nodiscard]] PortMapMethod activeMethod() const noexcept { return m_activeMethod; }
    [[nodiscard]] Address externalAddress() const noexcept { return m_externalAddress; }
    [[nodiscard]] std::vector<PortMapping> mappings() const;

    /// The external port granted for one mapping, or 0 if there is none.
    [[nodiscard]] uint16 externalPort(PortMapPurpose purpose, PortMapProtocol protocol,
                                      PortMapFamily family = PortMapFamily::IPv4) const;

    // -- Test seams ---------------------------------------------------------

    /// Install a backend instead of the real ones. Must be called before
    /// start(); suppresses the built-in backend factory entirely.
    void addBackendForTest(std::unique_ptr<PortMapBackend> backend);

    /// Pin the renewal jitter so timing assertions are deterministic.
    void setRenewalRandomForTest(double rand01) { m_renewalRandom = rand01; }

    /// Force every renewal timer to this interval. The schedule arithmetic
    /// itself is covered by tst_PortMapWire; this only lets a test observe that
    /// a renewal is wired up at all without waiting out the 4 s RFC floor.
    void setRenewalOverrideMsForTest(int milliseconds) { m_renewalOverrideMs = milliseconds; }

    /// Shorten the probe round: @p graceMs is what the fast backends get, @p totalMs
    /// the budget a still-working one is extended to. Lets a test drive the deadline
    /// paths without waiting out the real UPnP-sized budget.
    void setProbeTimeoutsForTest(int graceMs, int totalMs)
    {
        m_probeGraceMs = graceMs;
        m_probeTotalMs = totalMs;
    }

signals:
    void statusChanged(PortMapStatus status);
    void methodChanged(PortMapMethod method);
    void mappingChanged(const eMule::PortMapping& mapping, bool ok);
    void externalAddressChanged(const eMule::Address& address);

    /// A backend won the race; persist it as the preferred method for next run.
    void preferredMethodLearned(PortMapMethod method);

private:
    /// How far a trial got, which is what decides the race.
    ///
    /// Exact beats protocol preference on purpose: eD2K advertises
    /// thePrefs.port() and has no external-port tag, so a PCP grant on a
    /// different port is a silent LowID. A UPnP backend that honours the port
    /// is genuinely better than a PCP backend that does not.
    enum class TrialQuality : uint8 { None = 0, Partial = 1, Mismatched = 2, Exact = 3 };

    enum class State : uint8 { Idle, Probing, Trialling, Active, Stopped };

    struct BackendSlot {
        std::unique_ptr<PortMapBackend> backend;
        bool probed = false;
        bool available = false;
    };

    struct Active {
        PortMapping mapping;
        std::unique_ptr<QTimer> renewTimer;
        int renewAttempt = 0;
        int consecutiveFailures = 0;
    };

    /// What one candidate backend achieved. Trials are kept rather than undone
    /// as the race proceeds, so falling back to a runner-up costs no extra round
    /// trip; the losers' mappings are released once a winner is chosen.
    struct TrialResult {
        usize                    slot = 0;
        std::vector<PortMapping> mappings;
        TrialQuality             quality = TrialQuality::None;
    };

    // Private functions live after the public ones, per the project standard.
    void buildBackends();
    void connectBackend(PortMapBackend* backend);
    void beginProbe();
    void finishProbe();
    void tryNextCandidate();
    void evaluateTrial();
    void commitBestTrial();
    void scheduleRenewal(Active& active);
    void releaseAll();
    void clearActive();
    void setStatus(PortMapStatus status);
    void recomputeStatus();
    void noteExternalAddress(const Address& address);

    void onProbeDeadline();
    void scheduleReprobe();
    void onProbeFinished(PortMapBackend* backend, bool supported, const QString& detail);
    void adoptLateProbe(PortMapBackend* backend, bool supported, const QString& detail);
    void onMappingResult(PortMapBackend* backend, const PortMapping& mapping,
                         bool ok, const QString& error);
    void onMappingsInvalidated(PortMapBackend* backend, const QString& reason);

    [[nodiscard]] PortMapBackend* currentCandidate() const;
    [[nodiscard]] usize desiredCountFor(const PortMapBackend* backend) const;
    [[nodiscard]] static TrialQuality gradeTrial(const std::vector<PortMapping>& mappings,
                                                 usize expected);

    std::vector<BackendSlot>      m_backends;
    std::vector<PortMapRequest>   m_desired;
    std::vector<Active>           m_active;

    // Trial state
    std::vector<usize>            m_candidates;      ///< indices into m_backends
    usize                         m_candidateIndex = 0;
    std::vector<PortMapping>      m_trialMappings;   ///< current candidate's results
    usize                         m_trialExpected = 0;
    usize                         m_trialReceived = 0;
    std::vector<TrialResult>      m_trialResults;    ///< every candidate tried so far

    QTimer        m_probeDeadline;
    QTimer        m_reprobeBackoff;
    State         m_state = State::Idle;
    PortMapStatus m_status = PortMapStatus::Unknown;
    PortMapMethod m_activeMethod = PortMapMethod::None;
    PortMapMethod m_preferredMethod = PortMapMethod::None;
    Address       m_externalAddress;
    uint32        m_enabledMask = BitAll;
    uint32        m_leaseSeconds = 3600;
    int           m_reprobeAttempt = 0;
    int           m_renewalOverrideMs = -1;
    double        m_renewalRandom = 0.5;
    bool          m_testBackends = false;
    /// Set once a probe round has already been given the slow-backend extension,
    /// so a round can wait longer but never indefinitely.
    bool          m_probeExtended = false;
    /// Probe budgets, seeded from the file-scope constants in the constructor.
    int           m_probeGraceMs = 0;
    int           m_probeTotalMs = 0;
    /// Set while tryNextCandidate() is issuing a candidate's requests, so a backend that
    /// answers synchronously cannot advance the race from under that loop.
    bool          m_issuingTrialRequests = false;
};

} // namespace eMule
