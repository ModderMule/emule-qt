#pragma once

/// @file ReachabilityWatch.h
/// @brief Notices a port that opens after start (#156, not MFC).
///
/// Some VPNs / ISPs open a fresh port mapping minutes after it was granted. The server
/// tests us only at login and Kad only hourly, so a LowID / firewalled start stays that
/// way. While on (pref reachabilityWatch, default off) and eD2K has a LowID or Kad TCP is
/// firewalled:
/// - steps 2, 10, 30 and 60 min after that started: Kad firewall recheck, and the website
///   eD2K port test when we expect to be reachable (port mapped, or HighID in the last week)
/// - evidence (Kad TCP open, Kad UDP open, direct inbound peer, port test open) asks the
///   server for a HighID (ServerConnect::requestHighIdRecheck) and Kad for a recheck
/// - a changed mapped TCP port restarts the steps and tells the server the new port
///
/// The watch only decides; CoreSession wires the hooks, the daemon the port test.

#include "net/PortTestApi.h"
#include "utils/Types.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <array>
#include <ctime>
#include <functional>

namespace eMule {

class ReachabilityWatch : public QObject {
    Q_OBJECT

public:
    /// Minutes after the LowID / firewalled start.
    static constexpr std::array<int, 4> kStepMinutes{2, 10, 30, 60};
    /// Evidence-driven Kad rechecks at most this often.
    static constexpr time_t kKadRecheckGapSecs = 120;
    /// A HighID this recent says the port is normally open.
    static constexpr time_t kRecentHighIdSecs = 7 * 24 * 3600;
    static constexpr int kTickMs = 15'000;

    struct State {
        bool ed2kLowId = false;          ///< connected to a server with a LowID
        bool kadRunning = false;
        bool kadTcpFirewalled = false;
        bool kadUdpOpen = false;         ///< UDP firewall test passed
        bool lanMode = false;            ///< Kad LAN mode: no firewall checks
        bool portMapped = false;         ///< router / VPN granted the eD2K TCP mapping
        time_t lastHighIdAt = 0;         ///< epoch s, 0 = never
    };

    using PortTestDone = std::function<void(PortTestApi::Verdict)>;

    struct Hooks {
        std::function<State()> state;
        std::function<bool()> recheckKad;   ///< true = a recheck started
        std::function<bool(const QString& why, bool portChanged)> requestHighId;
        std::function<void(PortTestDone)> portTest;   ///< null = no website test (core only)
        std::function<time_t()> now;                  ///< null = time(nullptr)
    };

    explicit ReachabilityWatch(QObject* parent = nullptr);
    ~ReachabilityWatch() override;

    void setHooks(Hooks hooks) { m_hooks = std::move(hooks); }
    void setPortTest(std::function<void(PortTestDone)> portTest) { m_hooks.portTest = std::move(portTest); }

    /// From the pref. Off stops the steps; evidence is ignored then too.
    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const { return m_enabled; }

    /// Something says the port is open now (inbound peer, Kad, port test).
    void onReachable(const QString& source);
    /// The router mapped our eD2K TCP port to @p externalPort (0 is ignored).
    void onMappedPortChanged(uint16 externalPort);
    /// Network / interface change: start the steps over.
    void reset();

    /// One check; the timer calls it, tests call it with their clock.
    void tick();

    // -- Observation (tests) --
    [[nodiscard]] int stepsDone() const { return m_step; }
    [[nodiscard]] time_t watchingSince() const { return m_since; }

private:
    void runStep(const State& st);
    void onPortTestDone(uint32 generation, PortTestApi::Verdict verdict);
    [[nodiscard]] time_t now() const;
    [[nodiscard]] bool needsWatch(const State& st) const;
    [[nodiscard]] bool expectReachable(const State& st) const;
    bool recheckKad(time_t now);

    Hooks m_hooks;
    QTimer m_timer;
    bool m_enabled = false;
    time_t m_since = 0;            ///< LowID / firewalled since; 0 = not watching
    int m_step = 0;                ///< kStepMinutes entries done
    time_t m_lastKadRecheck = 0;
    bool m_lastKadUdpOpen = false;
    bool m_portTestRunning = false;
    uint32 m_generation = 0;       ///< drops port test answers from before a reset
    uint16 m_mappedPort = 0;
};

} // namespace eMule
