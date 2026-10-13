#pragma once

/// @file CoreStartWatch.h
/// @brief Notices when the core is not coming up, and says why.
///
/// IpcClient retries for ever and reports only to the log. That is right for a
/// core that needs another second, and useless for one that crashed on start, sits
/// busy without answering, or turned the handshake down: the window stays empty
/// and nothing tells the user which of those it is.

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

#include <functional>

namespace eMule {

class IpcClient;

class CoreStartWatch : public QObject {
    Q_OBJECT

public:
    enum class Problem {
        None,
        CoreExited,    ///< the core we launched is no longer running
        NoAnswer,      ///< something holds the port but does not complete a handshake
        Unreachable,   ///< nothing listens there
        Refused,       ///< the core turned the handshake down (IpcClient::refusalText)
    };
    Q_ENUM(Problem)

    /// What the watch cannot see through the socket. Either may be empty.
    struct Probes {
        std::function<bool()> coreAlive;   ///< false = the launched process is gone
        std::function<bool()> portFree;    ///< true = nothing listens on the IPC port
    };

    explicit CoreStartWatch(IpcClient* ipc, QObject* parent = nullptr);

    void setProbes(Probes probes) { m_probes = std::move(probes); }

    /// How long a core may take to answer before that counts as a problem.
    void setGraceMs(int ms) { m_graceMs = ms; }
    void setCheckIntervalMs(int ms) { m_timer.setInterval(ms); }

    /// Begin (or begin again) waiting for the core: on launch, and on Retry.
    void restart();

    [[nodiscard]] Problem problem() const { return m_problem; }

signals:
    /// problem() changed. None means the core answered.
    void problemChanged(eMule::CoreStartWatch::Problem problem);

private:
    void check();
    void setProblem(Problem problem);

    IpcClient* m_ipc = nullptr;
    Probes m_probes;
    QTimer m_timer;
    QElapsedTimer m_waiting;   ///< since the core was last expected or lost
    int m_graceMs = 30'000;
    int m_deadChecks = 0;      ///< consecutive checks that found the process gone
    Problem m_problem = Problem::None;
};

} // namespace eMule
