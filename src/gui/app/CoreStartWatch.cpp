#include "pch.h"
/// @file CoreStartWatch.cpp
/// @brief Notices when the core is not coming up — implementation.

#include "app/CoreStartWatch.h"

#include "app/IpcClient.h"

namespace eMule {

CoreStartWatch::CoreStartWatch(IpcClient* ipc, QObject* parent)
    : QObject(parent)
    , m_ipc(ipc)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &CoreStartWatch::check);

    connect(m_ipc, &IpcClient::connected, this, [this] {
        m_timer.stop();
        setProblem(Problem::None);
    });
    // A core lost mid-session gets the same patience as one just started
    connect(m_ipc, &IpcClient::disconnected, this, &CoreStartWatch::restart);
    // A refusal is known the moment it arrives: no reason to sit out the grace
    connect(m_ipc, &IpcClient::coreLinkChanged, this, [this] {
        if (m_ipc->coreLink() == IpcClient::CoreLink::Refused)
            check();
    });
}

void CoreStartWatch::restart()
{
    m_deadChecks = 0;
    m_waiting.start();
    setProblem(Problem::None);
    m_timer.start();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void CoreStartWatch::check()
{
    if (m_ipc->isConnected())
        return;

    const auto link = m_ipc->coreLink();
    if (link == IpcClient::CoreLink::Offline)
        return;   // not trying, so nothing is overdue

    if (link == IpcClient::CoreLink::Refused) {
        setProblem(Problem::Refused);
        return;
    }
    // A process that is gone will not answer however long we wait. Seen twice
    // before it counts: a core lost mid-session is relaunched within a second.
    if (m_probes.coreAlive && !m_probes.coreAlive()) {
        if (++m_deadChecks >= 2)
            setProblem(Problem::CoreExited);
        return;
    }
    m_deadChecks = 0;
    if (!m_waiting.isValid() || m_waiting.elapsed() < m_graceMs)
        return;

    // Accepted but unanswered is a busy or stuck core; no listener is no core.
    // Judged once: the port probe binds the port, and doing that every second
    // could take it from a core that is just now trying to open it.
    m_timer.stop();
    const bool accepted = link == IpcClient::CoreLink::Handshaking
                       || link == IpcClient::CoreLink::NotResponding;
    const bool held = accepted || (m_probes.portFree && !m_probes.portFree());
    setProblem(held ? Problem::NoAnswer : Problem::Unreachable);
}

void CoreStartWatch::setProblem(Problem problem)
{
    if (problem == m_problem)
        return;
    m_problem = problem;
    emit problemChanged(problem);
}

} // namespace eMule
