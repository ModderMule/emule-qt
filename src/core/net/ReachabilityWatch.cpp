#include "pch.h"
/// @file ReachabilityWatch.cpp
/// @brief Notices a port that opens after start (#156) — implementation.

#include "net/ReachabilityWatch.h"

#include "utils/Log.h"

namespace eMule {

ReachabilityWatch::ReachabilityWatch(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, &ReachabilityWatch::tick);
}

ReachabilityWatch::~ReachabilityWatch() = default;

void ReachabilityWatch::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    reset();
    if (enabled)
        m_timer.start();
    else
        m_timer.stop();
}

void ReachabilityWatch::reset()
{
    m_since = 0;
    m_step = 0;
    m_portTestRunning = false;
    ++m_generation;
}

void ReachabilityWatch::tick()
{
    if (!m_enabled || !m_hooks.state)
        return;
    const State st = m_hooks.state();
    const time_t t = now();

    // Kad UDP test passed: the mapping is live, TCP very likely too
    const bool udpOpened = st.kadUdpOpen && !m_lastKadUdpOpen;
    m_lastKadUdpOpen = st.kadUdpOpen;

    if (!needsWatch(st)) {
        if (m_since != 0)
            logDebug(QStringLiteral("Reachability watch: reachable, steps stop"));
        m_since = 0;
        m_step = 0;
        return;
    }
    if (m_since == 0) {
        m_since = t;
        m_step = 0;
        logDebug(QStringLiteral("Reachability watch: %1 — rechecking after 2, 10, 30, 60 min")
                     .arg(st.ed2kLowId ? QStringLiteral("LowID") : QStringLiteral("Kad firewalled")));
    }
    if (udpOpened && st.ed2kLowId) {
        onReachable(QStringLiteral("Kad UDP check passed"));
        return;
    }
    if (m_step < int(kStepMinutes.size()) && t - m_since >= time_t(kStepMinutes[usize(m_step)]) * 60) {
        ++m_step;
        runStep(st);
    }
}

void ReachabilityWatch::onReachable(const QString& source)
{
    if (!m_enabled || !m_hooks.state)
        return;
    const State st = m_hooks.state();
    if (!needsWatch(st))
        return;
    bool acted = false;
    if (st.ed2kLowId && m_hooks.requestHighId)
        acted = m_hooks.requestHighId(source, false);
    if (st.kadRunning && st.kadTcpFirewalled)
        acted = recheckKad(now()) || acted;
    if (acted)
        logInfo(QStringLiteral("Port looks reachable now (%1): rechecking").arg(source));
}

void ReachabilityWatch::onMappedPortChanged(uint16 externalPort)
{
    if (externalPort == 0)
        return;   // keep the last one: a re-grant on another port still counts as a change
    const uint16 previous = m_mappedPort;
    m_mappedPort = externalPort;
    if (!m_enabled || previous == 0 || previous == externalPort)
        return;
    // a fresh port may take a while to open: start the steps over
    reset();
    logInfo(QStringLiteral("Mapped eD2K port changed %1 -> %2").arg(previous).arg(externalPort));
    if (m_hooks.requestHighId)
        m_hooks.requestHighId(QStringLiteral("mapped port changed"), true);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void ReachabilityWatch::runStep(const State& st)
{
    logDebug(QStringLiteral("Reachability watch: step %1 (%2 min)")
                 .arg(m_step).arg(kStepMinutes[usize(m_step - 1)]));
    if (st.kadRunning && st.kadTcpFirewalled)
        recheckKad(now());
    if (st.ed2kLowId && m_hooks.portTest && !m_portTestRunning && expectReachable(st)) {
        m_portTestRunning = true;
        m_hooks.portTest([this, generation = m_generation](PortTestApi::Verdict verdict) {
            onPortTestDone(generation, verdict);
        });
    }
}

void ReachabilityWatch::onPortTestDone(uint32 generation, PortTestApi::Verdict verdict)
{
    if (generation != m_generation)
        return;
    m_portTestRunning = false;
    if (verdict == PortTestApi::Verdict::Open)
        onReachable(QStringLiteral("port test"));
    else
        logDebug(QStringLiteral("Reachability watch: port test says %1")
                     .arg(verdict == PortTestApi::Verdict::Closed ? QStringLiteral("closed")
                                                                  : QStringLiteral("unknown")));
}

time_t ReachabilityWatch::now() const
{
    return m_hooks.now ? m_hooks.now() : time(nullptr);
}

bool ReachabilityWatch::needsWatch(const State& st) const
{
    return !st.lanMode && (st.ed2kLowId || (st.kadRunning && st.kadTcpFirewalled));
}

bool ReachabilityWatch::expectReachable(const State& st) const
{
    // keeps users that are always firewalled off the website
    return st.portMapped || (st.lastHighIdAt != 0 && now() - st.lastHighIdAt < kRecentHighIdSecs);
}

bool ReachabilityWatch::recheckKad(time_t t)
{
    if (!m_hooks.recheckKad || (m_lastKadRecheck != 0 && t - m_lastKadRecheck < kKadRecheckGapSecs))
        return false;
    if (!m_hooks.recheckKad())
        return false;
    m_lastKadRecheck = t;
    return true;
}

} // namespace eMule
