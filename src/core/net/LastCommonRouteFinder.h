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

#include "utils/Types.h"

#include <QThread>
#include <QString>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace eMule {

// ---------------------------------------------------------------------------
// Configuration / status structures
// ---------------------------------------------------------------------------

/// Parameters passed to the finder from preferences.
struct USSParams {
    double pingTolerance = 1.0;           ///< % tolerance above baseline (0.1–2.0).
    uint32 curUpload = 0;                 ///< Current upload limit (bytes/sec from prefs).
    uint32 minUpload = 1024;              ///< Minimum upload speed (bytes/sec).
    uint32 maxUpload = UINT32_MAX;        ///< Maximum upload speed (bytes/sec).
    uint32 pingToleranceMilliseconds = 0; ///< Absolute tolerance in ms (if enabled).
    uint32 goingUpDivider = 1000;         ///< Speed increase divisor.
    uint32 goingDownDivider = 1000;       ///< Speed decrease divisor.
    uint32 numberOfPingsForAverage = 3;   ///< Ring buffer size for ping median.
    uint32 lowestInitialPingAllowed = 20; ///< Minimum acceptable baseline ping (ms).
    bool useMillisecondPingTolerance = false;
    bool enabled = false;
};

/// Current ping/upload status snapshot.
struct USSStatus {
    QString state;         ///< Human-readable status string.
    uint32  latency = 0;  ///< Current average ping (ms).
    uint32  lowest  = 0;  ///< Baseline ping (ms).
    uint32  currentLimit = 0; ///< Current calculated upload limit (bytes/sec).
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

    /// Whether the controller will accept a new upload client. Thread-safe.
    [[nodiscard]] bool acceptNewClient() const;

    /// Update operating parameters from preferences. Thread-safe.
    /// @return true if parameters were accepted.
    bool setPrefs(const USSParams& params);

    /// Trigger a fast-reaction period (e.g. after new upload slot given).
    void initiateFastReactionPeriod();

    /// Current calculated upload limit (bytes/sec). Thread-safe.
    [[nodiscard]] uint32 getUpload() const;

signals:
    /// Emitted when the finder needs more hosts for traceroute.
    /// Connect to server/client list providers.
    void needMoreHosts();

    /// Emitted when the upload limit changes.
    void uploadLimitChanged(uint32 newLimit);

protected:
    void run() override;

private:
    /// Compute median of a vector.
    [[nodiscard]] static uint32 median(std::vector<uint32>& values);

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
    uint32 m_curUpload = 0;
    uint32 m_pingToleranceMilliseconds = 0;
    uint32 m_goingUpDivider = 1000;
    uint32 m_goingDownDivider = 1000;
    uint32 m_numberOfPingsForAverage = 3;
    bool m_useMillisecondPingTolerance = false;
    bool m_enabled = false;

    // --- Ping data (guarded by m_pingMutex) ---
    std::deque<uint32> m_pingDelays;
    uint64 m_pingDelaysTotal = 0;
    uint32 m_pingAverage = 0;
    uint32 m_lowestPing = 0;
    QString m_stateString;

    // --- Upload limit (atomic, no lock needed) ---
    std::atomic<uint32> m_upload{0};
    std::atomic<bool> m_acceptNewClient{true};

    // --- Thread control ---
    std::atomic<bool> m_run{true};
    std::atomic<char> m_initiateFastReaction{0};
};

} // namespace eMule
