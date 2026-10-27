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
#include <QStringList>
#include <QThreadPool>
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
    /// JSON-RPC endpoints (ed3k.creditRpc / ed3k.chaintubeRpc): operator 0 of a call is the
    /// first, n the n-th (as mode B, packmule_udp::http). Empty: calls answered failed.
    QStringList creditRpc;
    QStringList chaintubeRpc;
};

/// What GetEd3kStatus shows (Ed3kBridge); all 0 / empty while not running.
struct Ed3kStatus {
    bool running = false;
    std::uint16_t udpPort = 0;
    QString nodeId;              ///< hex, public
    std::uint64_t contacts = 0;
    std::uint32_t class4 = 0;    ///< 0 unknown, 1 open, 2 cone, 3 symmetric
    std::uint32_t class6 = 0;
    bool router = false;
    ::ed3k::Stats stats{};       ///< the engine's counters (size 0 while not running)
};

/// Datagram payload bytes since this object was made (wire_sent / wire_recv of ed3k_stats,
/// summed over restarts): what the limits count.
struct Ed3kTraffic {
    std::uint64_t sent = 0;
    std::uint64_t recv = 0;
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
    /// Running, port, node, contacts, address class, router and the engine's counters.
    [[nodiscard]] Ed3kStatus status() const;
    /// Bytes on the wire so far (see Ed3kTraffic).
    [[nodiscard]] Ed3kTraffic traffic() const;

    /// Upload/download limits in bytes/s, 0 = none (ed3k_set_limits; others are raised to
    /// its 16 KiB/s floor). Kept across restarts; set by BandwidthCoordinator.
    void setLimits(std::uint64_t upBytesPerSec, std::uint64_t downBytesPerSec);
    [[nodiscard]] ::ed3k::Limits limits() const { return m_limits; }

    /// Slots of the engine's upload queue policy (ed3k_queue_auto, on from start): 0 = from
    /// the upload limit. Kept across restarts; set by BandwidthCoordinator.
    void setUploadSlots(std::uint32_t count);
    /// The policy's slots, holders and waiting entries; nullopt while not running.
    [[nodiscard]] std::optional<::ed3k::QueueStats> uploadQueueStats();

    /// The JSON-RPC endpoints of a class (see Ed3kSettings); calls out keep their URL.
    void setRpcEndpoints(::ed3k::RpcClass cls, const QStringList& urls);

    /// Folder of the key and state files.
    [[nodiscard]] QString stateDir() const { return m_dir; }

    /// The engine while running, else nullptr. Only on this thread; pump() after commands
    /// called from outside the pump.
    [[nodiscard]] ::ed3k::Engine* engine() { return m_engine.get(); }
    /// The time to pass to engine commands.
    [[nodiscard]] ::ed3k::Now now() const;
    /// Workers for the joins' file reads (2 threads; not behind hashing in the global pool).
    [[nodiscard]] QThreadPool& readPool() { return m_readPool; }

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
    /// RpcRequest to the configured endpoint; false: none for its class and operator.
    bool startRpc(const ::ed3k::event::RpcRequest& r);
    void applyLimits();
    void applyUploadSlots();
    /// packmule's log level from the filter rules of emule.ed3k / emule.ed3k.kad (verbose
    /// when either lets debug lines through); set when it changed.
    void applyLogLevel();
    /// A line of packmule (event::Log) into emule.ed3k or emule.ed3k.kad.
    static void logEngineLine(const ::ed3k::event::Log& line);
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
    QThreadPool m_readPool;
    QElapsedTimer m_clock;
    QString m_dir;
    std::uint16_t m_port = 0;
    bool m_pumping = false;
    bool m_pumpAgain = false;
    bool m_badDirty = false;
    std::vector<std::function<void(::ed3k::Engine&, ::ed3k::Now)>> m_posted;
    std::uint64_t m_notOurs = 0;
    QStringList m_creditRpc;
    QStringList m_chaintubeRpc;
    ::ed3k::Limits m_limits{};
    std::uint32_t m_uploadSlots = 0;
    int m_logLevel = -1;   ///< ed3k::LogLevel set on this engine; -1 = none yet
    Ed3kTraffic m_trafficBase;   ///< of the engines before this one
};

} // namespace eMule::ed3k
