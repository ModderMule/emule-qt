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
///   datagramOut (written at once, in order) and pollKeyedEvent until both are empty, and
///   re-arms the timer to nextTimeout().
/// - eventPolled() is emitted synchronously inside the pump. A slot may call engine
///   commands directly (they only queue work; the running pump picks it up) but must
///   not wait for anything; pump() from inside is a no-op that makes the pump loop once
///   more. Data in the event is owned by the event (ed3k.hpp copies everything).
/// - From outside the pump (a socket slot, a worker result): post() or a command and
///   then pump().

#include "Ed3kFfi.h"
#include "Ed3kHide.h"
#include "Ed3kLan.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkDatagram>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QUdpSocket>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>


namespace eMule::ed3k {

class Ed3kHttp;

/// What start() needs. The daemon fills it from thePrefs; tests set the rest.
struct Ed3kSettings {
    std::uint16_t udpPort = 0;   ///< 0 = any free port
    QString configDir;           ///< state goes to <configDir>/ed3k
    QHostAddress bindAddress{QHostAddress::Any};   ///< Any = dual stack
    QString network;             ///< empty = the public network; tests only; a secret
    bool forceOpen = false;      ///< operator/tests: address open, no NAT tests (§9.2 first nodes)
    std::uint32_t smallNetS = 0; ///< tests: Kad of a few nodes connected this long after the first answer; 0 = default (45 s)
    /// JSON-RPC endpoints (ed3k.creditRpc / ed3k.chaintubeRpc): operator 0 of a call is the
    /// first, n the n-th (as mode B, packmule_udp::http). Empty: calls answered failed.
    QStringList creditRpc;
    QStringList chaintubeRpc;
    QString nick;                ///< Hello.nick (thePrefs.nick())
    std::uint64_t extOff = 0;    ///< ED3K_EXT_* turned off (pref ed3k.extOff, extOffBits)
    /// LAN discovery: the daemon sets the pref ed3k.lan (default on); off here, so that tests
    /// of one machine do not find each other over the real LAN
    bool lan = false;
    Ed3kLan::Hooks lanHooks;     ///< tests: LAN interfaces and sends by hand
    /// non-null: the eD2K UDP socket bound to udpPort (§14.1); nothing is bound here and its
    /// owner hands in what is not eD2K (offerDatagram)
    QPointer<QUdpSocket> sharedSocket;
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
    /// Kad's own network offline (Protocol.md §10 "Own network down"): bit 0 IPv4, bit 1 IPv6
    std::uint32_t kadOffline = 0;
    /// reach test without a verdict yet: bit 0 IPv4, bit 1 IPv6 (class 2/3 may still turn open)
    std::uint32_t natChecking = 0;
    /// Kad node lookups got enough answers: searches and publishing run (not just contacts > 0)
    bool kadConnected = false;
    Ed3kHideStatus hide;         ///< hiding (hops.md §1)
    /// public endpoint the engine adopted per family ("ip:port"); empty = none yet
    QString endpoint4;
    QString endpoint6;
    /// relays this node is reachable through (§9.4, the Kad buddy): "ip:port" per family
    QStringList relays4;
    QStringList relays6;
    /// node IDs (hex) of relays4 + relays6, same order
    QStringList relayIds4;
    QStringList relayIds6;
    /// leaves this node relays for (§9.4, eMule's "buddies served")
    struct Leaf {
        QString address;         ///< "ip:port" of the connection that reserved last
        QString nodeId;          ///< hex
        bool hop = false;        ///< last Relay.hop (hops.md §2.5)
    };
    QList<Leaf> leavesServed;
    /// index nodes this client names (§11, the eD2K server list), in list order
    struct IndexLink {
        QString address;
        QString nodeId;          ///< hex
        int state = 0;           ///< ED3K_INDEX_WAITING / _DIALLING / _CONNECTED / _LOST
        bool current = false;    ///< the one dialled or held
        std::uint32_t tries = 0;
        std::uint64_t upSecs = 0;
        std::uint32_t rttMs = 0; ///< 0 = none
        QString agent;           ///< Hello.agent while connected (the "server version")
        QString nick;            ///< Hello.nick (the "server name")
    };
    QList<IndexLink> indexLinks;
    /// eMule's Kad user estimates: routing table, closest node of lookups (0 = too few)
    std::uint64_t usersByTable = 0;
    std::uint64_t usersByLookups = 0;
    std::uint32_t leafPct = 0;   ///< leaves added in percent
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
    /// A datagram from the shared socket (settings().sharedSocket): true = ed3k took it.
    /// A filtered sender's is false: the owner drops it (its own filter check).
    bool offerDatagram(const QNetworkDatagram& d);
    /// The app's IP filter (#177): true = no datagram from or to that address, on the node's
    /// socket and LAN discovery. ed3k relays forward signalling only (Protocol §9.4), so file
    /// data always comes straight from the peer's address. Survives stop/start.
    void setPeerFilter(std::function<bool(const QHostAddress&)> filter) { m_peerFilter = std::move(filter); }
    [[nodiscard]] bool peerFiltered(const QHostAddress& addr) const { return m_peerFilter && m_peerFilter(addr); }
    /// Datagrams dropped by the peer filter since construction (in and out)
    [[nodiscard]] std::uint64_t filteredDatagrams() const { return m_filtered; }
    /// The node ID in hex (public); empty when not running.
    [[nodiscard]] QString nodeIdHex() const;
    [[nodiscard]] std::optional<::ed3k::NodeId> nodeId() const;
    /// Contacts of the Kad routing table; 0 when not running.
    [[nodiscard]] std::uint64_t contactCount() const;
    /// Running, port, node, contacts, address class, router and the engine's counters.
    [[nodiscard]] Ed3kStatus status() const;
    /// Bytes on the wire so far (see Ed3kTraffic).
    [[nodiscard]] Ed3kTraffic traffic() const;

    /// A PROBE to this node itself for a port test from outside (ed3k_port_test_probe);
    /// empty when not running.
    [[nodiscard]] QByteArray portTestProbe(std::uint64_t nonce);
    /// Families a PROBE of that nonce arrived over: bit 0 IPv4, bit 1 IPv6.
    [[nodiscard]] int portTestSeen(std::uint64_t nonce);

    /// Upload/download limits in bytes/s, 0 = none (ed3k_set_limits; others are raised to
    /// its 16 KiB/s floor). Kept across restarts; set by BandwidthCoordinator.
    void setLimits(std::uint64_t upBytesPerSec, std::uint64_t downBytesPerSec);
    [[nodiscard]] ::ed3k::Limits limits() const { return m_limits; }

    /// Slots of the engine's upload queue policy (ed3k_queue_auto, on from start): 0 = from
    /// the upload limit. Kept across restarts; set by BandwidthCoordinator.
    void setUploadSlots(std::uint32_t count);
    /// An eD2K server's index node (ST_ED3K, ed3k §11), an ed3k:node link: kept across
    /// restarts, each once. false: not a valid ed3k:node link.
    bool addIndexLink(const QString& link);
    /// The policy's slots, holders and waiting entries; nullopt while not running.
    [[nodiscard]] std::optional<::ed3k::QueueStats> uploadQueueStats();

    /// The JSON-RPC endpoints of a class (see Ed3kSettings); calls out keep their URL.
    void setRpcEndpoints(::ed3k::RpcClass cls, const QStringList& urls);

    /// What the running node was started with (extOff, lan, bind address ...).
    [[nodiscard]] const Ed3kSettings& settings() const { return m_settings; }
    /// LAN discovery while it runs, else nullptr.
    [[nodiscard]] Ed3kLan* lan() { return m_lan.get(); }
    /// The extensions in force (ED3K_EXT_*); 0 when not running.
    [[nodiscard]] std::uint64_t extensions() const;
    /// The IPv6 address advertised as local6 and sent from to global peers (socket-modes.md
    /// §8.3); null: [::], the system picks.
    [[nodiscard]] QHostAddress ipv6Source() const { return m_ipv6Source; }

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
    /// Saves ED3K_STATE_PAID now (after paidAnonConfirm, subscription.md §5.1).
    void savePaidState();

    /// Hiding (hops.md §1): the anonymous node and the key of each file. Lives as long as this.
    [[nodiscard]] Ed3kHide& hide() { return *m_hide; }
    [[nodiscard]] const Ed3kHide& hide() const { return *m_hide; }
    /// The hiding prefs; applied live (no restart).
    void setHideSettings(const Ed3kHideSettings& settings) { m_hide->setSettings(settings); }
    /// Which node had the event being dispatched (valid inside eventPolled / keyedEventPolled).
    [[nodiscard]] ::ed3k::KeyKind eventKey() const { return m_eventKey; }

    /// File bytes moved with a peer since the daemon started, per key (our node, anonymous node).
    struct PeerBytes {
        std::uint64_t up = 0;     ///< sent to it (finished turns; the running one is in the queue row)
        std::uint64_t down = 0;   ///< verified blocks from it
    };
    void addPeerBytes(::ed3k::KeyKind key, const ::ed3k::NodeId& node, std::uint64_t up, std::uint64_t down);
    [[nodiscard]] PeerBytes peerBytes(::ed3k::KeyKind key, const ::ed3k::NodeId& node) const;

signals:
    /// Running, port, contacts, address class changed.
    void statusChanged();
    /// Each polled event, synchronously inside the pump. Set *handled when the slot
    /// answered an event that needs an answer (WantBlock ...); else the session answers
    /// it with a failure so the core does not wait.
    void eventPolled(const ::ed3k::Event& event, bool* handled);
    /// Every polled event with the node that had it (KeyKind::Anon: the anonymous node), before
    /// eventPolled. eventPolled gets the node key's events only: a slot that does not know the
    /// anonymous key never takes its events for the node key's (hops.md §1).
    void keyedEventPolled(const ::ed3k::Event& event, ::ed3k::KeyKind key, bool* handled);

private:
    void onReadyRead();
    void onTimer();
    void dispatch(::ed3k::Event& ev, ::ed3k::KeyKind key);
    /// Starts or stops the anonymous node as the prefs say (after applyLogLevel and on a change).
    void applyHide();
    /// RpcRequest to the configured endpoint; false: none for its class and operator.
    bool startRpc(const ::ed3k::event::RpcRequest& r);
    void applyLimits();
    void applyUploadSlots();
    /// addIndexLink's links to the engine (at start)
    void applyIndexLinks();
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
    /// An exported state of the engine (filter, hops) into @p file; nothing when empty.
    void saveExport(const ::ed3k::Result<::ed3k::Buffer>& blob, const QString& file);
    /// The stable IPv6 source moved (IPv6SourcePin): local6 again.
    void followIPv6Pin();
    void writeDatagram(const char* data, qint64 len, const ::ed3k::Address& to);
    /// ED3K_ERR_DEAD: the engine is gone; ed3k off until the next start.
    void die(::ed3k::Error error);
    void armTimer();
    [[nodiscard]] QString filePath(const QString& name) const;

    std::unique_ptr<::ed3k::Engine> m_engine;
    std::unique_ptr<QUdpSocket> m_ownSocket;   ///< null on a shared socket
    QPointer<QUdpSocket> m_socket;             ///< own or shared
    std::unique_ptr<Ed3kHttp> m_http;
    std::unique_ptr<Ed3kLan> m_lan;
    Ed3kSettings m_settings;
    QHostAddress m_ipv6Source;   ///< advertised as local6 and sent from; null: the system's
    QTimer m_timer;
    QTimer m_badSaveTimer;
    QTimer m_pinTimer;   ///< looks at IPv6SourcePin (LocalIPv6 moves it on a rescan)
    QThreadPool m_readPool;
    QElapsedTimer m_clock;
    QString m_dir;
    std::uint16_t m_port = 0;
    /// ED3K_EV_ENDPOINT per family, for status()
    QString m_endpoint4;
    QString m_endpoint6;
    /// ED3K_EV_RELAYS, for status(): "ip:port" and node ID hex per family
    QStringList m_relays4, m_relays6, m_relayIds4, m_relayIds6;
    /// ST_ED3K links of eD2K servers (addIndexLink), in arrival order
    QStringList m_indexLinks;
    bool m_pumping = false;
    bool m_pumpQueued = false;   ///< a pump after offered datagrams is on its way
    bool m_pumpAgain = false;
    bool m_badDirty = false;
    std::vector<std::function<void(::ed3k::Engine&, ::ed3k::Now)>> m_posted;
    std::uint64_t m_notOurs = 0;
    std::function<bool(const QHostAddress&)> m_peerFilter;
    std::uint64_t m_filtered = 0;
    QStringList m_creditRpc;
    QStringList m_chaintubeRpc;
    ::ed3k::Limits m_limits{};
    std::uint32_t m_uploadSlots = 0;
    int m_logLevel = -1;   ///< ed3k::LogLevel set on this engine; -1 = none yet
    Ed3kTraffic m_trafficBase;   ///< of the engines before this one
    std::unique_ptr<Ed3kHide> m_hide;
    ::ed3k::KeyKind m_eventKey = ::ed3k::KeyKind::Node;
    std::map<std::pair<::ed3k::KeyKind, ::ed3k::NodeId>, PeerBytes> m_peerBytes;
};

} // namespace eMule::ed3k
