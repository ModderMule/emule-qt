#pragma once

/// @file LastCommonRouteFinder.h
/// @brief Adaptive upload bandwidth control via latency-based route analysis.
///
/// Replaces MFC CLastCommonRouteFinder (CWinThread subclass).
/// Uses QThread + std::mutex + std::condition_variable instead of
/// MFC CWinThread + CCriticalSection + CEvent.
///
/// Decoupled from theApp — emits needMoreHosts() signal when traceroute
/// hosts are needed; callers provide them via addHostsToCheck().

#include "net/Pinger.h"
#include "utils/Types.h"

#include <QThread>
#include <QString>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

class tst_LastCommonRouteFinder;

namespace eMule {

// ---------------------------------------------------------------------------
// Configuration / status structures
// ---------------------------------------------------------------------------

/// Parameters passed to the finder from preferences.
struct USSParams {
    double pingTolerance = 1.0;           ///< Allowed rise over the lowest ping, as a factor of it.
    uint32 curUpload = 0;                 ///< Measured upload rate (bytes/sec).
    uint32 minUpload = 1;                 ///< Minimum upload speed (KB/s); 0 reads as 1.
    uint32 maxUpload = UINT32_MAX;        ///< Maximum upload speed (KB/s); UINT32_MAX = none.
    uint32 pingToleranceMilliseconds = 0; ///< Absolute tolerance in ms (if enabled).
    uint32 goingUpDivider = 1000;         ///< Speed increase divisor.
    uint32 goingDownDivider = 1000;       ///< Speed decrease divisor.
    uint32 numberOfPingsForAverage = 3;   ///< Ring buffer size for ping median.
    uint32 lowestInitialPingAllowed = 20; ///< Minimum acceptable baseline ping (ms).
    bool useMillisecondPingTolerance = false;
    bool enabled = false;
};

/// What the controller is doing (MFC's status-pane states; Off and Active show none).
enum class UssState : uint8 {
    Off,        ///< option off
    Preparing,  ///< collecting hosts, tracing the route, measuring the baseline
    Waiting,    ///< tracing failed three times; resting before the next try
    Error,      ///< gave up and switched the option off
    Active      ///< limit under control
};

/// Result of the hop search: where the routes to our peers part.
struct RouteProbe {
    uint32 lastCommonHost = 0;  ///< router all traced routes share (network order)
    uint32 hostToPing = 0;      ///< a peer behind it; pinged with lastCommonTTL
    uint8  lastCommonTTL = 0;
    bool   found = false;
    bool   useUdp = false;      ///< ping method that worked last
};

/// Current ping/upload status snapshot.
struct USSStatus {
    QString state;         ///< Human-readable status string.
    UssState phase = UssState::Off;
    uint32  latency = 0;  ///< Current average ping (ms).
    uint32  lowest  = 0;  ///< Baseline ping (ms).
    uint32  currentLimit = 0; ///< Current calculated upload limit (bytes/sec).
    bool    active = false;   ///< Controlling the limit; before that only `state` says something.
};

// ---------------------------------------------------------------------------
// LastCommonRouteFinder
// ---------------------------------------------------------------------------

/// Adaptive upload speed controller using traceroute + latency measurement.
///
/// Finds the last common router hop shared by multiple connections, then
/// continuously pings it to detect congestion and adjust upload speed.
///
/// Runs as a QThread. Thread-safe methods can be called from any thread.
class LastCommonRouteFinder : public QThread {
    Q_OBJECT
    friend class ::tst_LastCommonRouteFinder;

public:
    explicit LastCommonRouteFinder(QObject* parent = nullptr);
    ~LastCommonRouteFinder() override;

    LastCommonRouteFinder(const LastCommonRouteFinder&) = delete;
    LastCommonRouteFinder& operator=(const LastCommonRouteFinder&) = delete;

    /// Stop the thread and wait for it to finish.
    void endThread();

    /// Add host IPs to check for traceroute. Thread-safe.
    /// @param ips  Vector of IPs in network byte order.
    /// @return true if hosts were accepted (currently collecting).
    bool addHostsToCheck(const std::vector<uint32>& ips);

    /// Get a snapshot of the current ping/upload status. Thread-safe.
    [[nodiscard]] USSStatus currentStatus() const;

    /// Switched on and not given up: getUpload() is the controller's limit. Thread-safe.
    [[nodiscard]] bool isEnabled() const { return enabledNow(); }
    /// Tracing failed for good: the limit is the configured one again. Thread-safe.
    [[nodiscard]] bool hasGivenUp() const;

    /// Whether the controller will accept a new upload client. Thread-safe.
    [[nodiscard]] bool acceptNewClient() const;

    /// Update operating parameters from preferences. Thread-safe.
    /// @return true if parameters were accepted.
    bool setPrefs(const USSParams& params);

    /// Restart the staged ramp, so a raised limit is reached sooner.
    void initiateFastReactionPeriod();

    /// One control step, MFC LastCommonRouteFinder.cpp:727-763. @p normalizedPing is the
    /// ping median minus the lowest ping, @p targetPing the allowed rise. The limit falls
    /// in proportion to the excess and rises in proportion to the headroom, but rises
    /// only while the measured rate @p curUpload is within 30 KB/s of it.
    /// @p acceptNewClient is left alone when the ping is exactly on target.
    [[nodiscard]] static uint32 adjustUpload(uint32 upload, uint32 curUpload,
                                             int32 normalizedPing, uint32 targetPing,
                                             uint32 lowestPing, uint32 goingUpDivider,
                                             uint32 goingDownDivider, uint32 minUpload,
                                             uint32 maxUpload, bool& acceptNewClient);

    /// Dividers @p msSinceStart into the ramp: full for 20 s, then a quarter, half,
    /// three quarters for 10/10/20 s (smaller = faster), full again after a minute.
    static void rampDividers(qint64 msSinceStart, uint32& goingUpDivider, uint32& goingDownDivider);

    /// Pause between pings: about 1% of @p upload in 64-byte pings, 125 ms to 1 s.
    [[nodiscard]] static uint32 pingIntervalMs(uint32 upload);

    /// Current calculated upload limit (bytes/sec). Thread-safe.
    [[nodiscard]] uint32 getUpload() const;

    /// Hosts wanted before a route is traced, and the count below which a try is over.
    static constexpr std::size_t kHostsToTrace = 10;
    static constexpr std::size_t kTooFewHosts = 8;

    using PingFn = std::function<PingStatus(uint32 addr, uint8 ttl, bool useUdp)>;

    /// One traceroute try, MFC LastCommonRouteFinder.cpp:327-449: every host is pinged
    /// with a rising TTL until two of them answer from different routers. Hosts that
    /// answer themselves (too close) or are unreachable leave @p hosts.
    /// @p pause sleeps between a failed ping and its retry; @p keepGoing ends the try.
    [[nodiscard]] static RouteProbe traceLastCommonHost(std::vector<uint32>& hosts,
                                                        const PingFn& ping,
                                                        const std::function<bool()>& keepGoing,
                                                        const std::function<void(uint32)>& pause);

signals:
    /// Emitted when the finder needs more hosts for traceroute.
    /// Connect to server/client list providers.
    void needMoreHosts();

    /// Emitted when the upload limit changes.
    void uploadLimitChanged(uint32 newLimit);

    /// Tracing the route failed for good. The finder stays off until it is switched
    /// off and on again; the receiver turns the option off (MFC SetDynUpEnabled(false)).
    void tracerouteGaveUp();

protected:
    void run() override;

private:
    /// Compute median of a vector.
    [[nodiscard]] static uint32 median(std::vector<uint32>& values);
    void setState(UssState state);
    [[nodiscard]] bool enabledNow() const;
    /// Sleep up to @p ms; false as soon as the thread is stopped or the option goes off.
    bool waitWhileEnabled(uint32 ms);

    // --- Synchronization ---
    mutable std::mutex m_hostsMutex;
    mutable std::mutex m_prefsMutex;
    mutable std::mutex m_pingMutex;

    std::condition_variable m_hostsCV;
    std::condition_variable m_prefsCV;

    // --- Host data (guarded by m_hostsMutex) ---
    std::unordered_map<uint32, uint32> m_hostsToTraceRoute;
    bool m_needMoreHosts = false;

    // --- Preferences (guarded by m_prefsMutex) ---
    double m_pingTolerance = 1.0;
    uint32 m_lowestInitialPingAllowed = 20;
    uint32 m_minUpload = 1024;
    uint32 m_maxUpload = UINT32_MAX;
    uint32 m_curUpload = 0;                 ///< measured rate, bytes/sec
    uint32 m_pingToleranceMilliseconds = 0;
    uint32 m_goingUpDivider = 1000;
    uint32 m_goingDownDivider = 1000;
    uint32 m_numberOfPingsForAverage = 3;
    bool m_useMillisecondPingTolerance = false;
    bool m_enabled = false;
    /// Set once the first setPrefs() lands. Lets run()'s startup wait finish as soon as
    /// real preferences exist, instead of blocking for the full timeout when USS is off.
    bool m_prefsReceived = false;
    /// Gave up tracing: stays off until a setPrefs() with the option off.
    bool m_gaveUp = false;

    // --- Ping data (guarded by m_pingMutex) ---
    std::deque<uint32> m_pingDelays;
    uint64 m_pingDelaysTotal = 0;
    uint32 m_pingAverage = 0;
    uint32 m_lowestPing = 0;
    UssState m_state = UssState::Off;

    // --- Upload limit (atomic, no lock needed) ---
    std::atomic<uint32> m_upload{0};
    std::atomic<bool> m_acceptNewClient{true};

    // --- Thread control ---
    std::atomic<bool> m_run{true};
    std::atomic<char> m_initiateFastReaction{0};
};

} // namespace eMule
