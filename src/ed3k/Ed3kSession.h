#pragma once

/// @file Ed3kSession.h
/// @brief The ed3k node (packmule) in the daemon: what DaemonApp owns.
///
/// Mode A of libs/packmule/docs/socket-modes.md: this object owns the UDP socket (its
/// own port, not the eD2K one), the clock and the one timer; the engine runs on the
/// daemon thread. Lives in eMule::Ed3k, which links eMule::Core; core never reaches in.
///
/// Threading and the pump (libs/packmule/docs/ffi.md "The loop"):
/// - Every call is on the thread of this object. The engine is not thread-safe.
/// - A datagram, the timer or a command enters the engine; then pump() drains
///   datagramOut (written at once, in order) and pollEvent until both are empty, and
///   re-arms the timer to nextTimeout().
/// - eventPolled() is emitted synchronously inside the pump. A slot may call engine
///   commands directly (they only queue work; the running pump picks it up) but must
///   not wait for anything; pump() from inside is a no-op that makes the pump loop once
///   more. Data in the event is owned by the event (ed3k.hpp copies everything).
/// - From outside the pump (a socket slot, a worker result): post() or a command and
///   then pump().

#include "Ed3kFfi.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

class QUdpSocket;

namespace eMule::ed3k {

class Ed3kHttp;

/// What start() needs. The daemon fills it from thePrefs; tests set the rest.
struct Ed3kSettings {
    std::uint16_t udpPort = 0;   ///< 0 = any free port
    QString configDir;           ///< state goes to <configDir>/ed3k
    QHostAddress bindAddress{QHostAddress::Any};   ///< Any = dual stack
    QString network;             ///< empty = the public network; tests only; a secret
    bool forceOpen = false;      ///< tests: treat the address as open (no NAT tests)
};

class Ed3kSession : public QObject {
    Q_OBJECT

public:
    explicit Ed3kSession(QObject* parent = nullptr);
    ~Ed3kSession() override;

    /// Key file, engine, saved state, socket. false: not started (logged).
    bool start(const Ed3kSettings& settings);
    /// Saves the state and frees the engine. Idempotent.
    void stop();

    [[nodiscard]] bool isRunning() const { return m_engine != nullptr; }
    /// The bound UDP port; 0 when not running.
    [[nodiscard]] std::uint16_t udpPort() const;
    /// The node ID in hex (public); empty when not running.
    [[nodiscard]] QString nodeIdHex() const;
    [[nodiscard]] std::optional<::ed3k::NodeId> nodeId() const;
    /// Contacts of the Kad routing table; 0 when not running.
    [[nodiscard]] std::uint64_t contactCount() const;
    /// Folder of the key and state files.
    [[nodiscard]] QString stateDir() const { return m_dir; }

    /// The engine while running, else nullptr. Only on this thread; pump() after commands
    /// called from outside the pump.
    [[nodiscard]] ::ed3k::Engine* engine() { return m_engine.get(); }
    /// The time to pass to engine commands.
    [[nodiscard]] ::ed3k::Now now() const;

    /// Runs @p cmd on the engine and pumps; inside the pump it runs after the current
    /// event. Dropped when not running.
    void post(std::function<void(::ed3k::Engine&, ::ed3k::Now)> cmd);
    /// Drains the engine (see the file comment). Inside the pump: loops once more.
    void pump();

signals:
    /// Running, port, contacts, address class changed.
    void statusChanged();
    /// Each polled event, synchronously inside the pump. Set *handled when the slot
    /// answered an event that needs an answer (WantBlock ...); else the session answers
    /// it with a failure so the core does not wait.
    void eventPolled(const ::ed3k::Event& event, bool* handled);

private:
    void onReadyRead();
    void onTimer();
    void dispatch(::ed3k::Event& ev);
    /// Default answer for an event nobody answered.
    void answerUnhandled(const ::ed3k::Event& ev);
    void loadState();
    void saveState(bool all);
    void saveSlot(::ed3k::State slot, const QString& file);
    void saveFriends();
    /// ED3K_ERR_DEAD: the engine is gone; ed3k off until the next start.
    void die(::ed3k::Error error);
    void armTimer();
    [[nodiscard]] QString filePath(const QString& name) const;

    std::unique_ptr<::ed3k::Engine> m_engine;
    std::unique_ptr<QUdpSocket> m_socket;
    std::unique_ptr<Ed3kHttp> m_http;
    QTimer m_timer;
    QTimer m_badSaveTimer;
    QElapsedTimer m_clock;
    QString m_dir;
    std::uint16_t m_port = 0;
    bool m_pumping = false;
    bool m_pumpAgain = false;
    bool m_badDirty = false;
    std::vector<std::function<void(::ed3k::Engine&, ::ed3k::Now)>> m_posted;
    std::uint64_t m_notOurs = 0;
};

} // namespace eMule::ed3k
