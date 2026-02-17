/// @file LastCommonRouteFinder.cpp
/// @brief Adaptive upload bandwidth control via latency-based route analysis.

#include "net/LastCommonRouteFinder.h"
#include "net/Pinger.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>

using namespace std::chrono_literals;

namespace eMule {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static constexpr int kMinHostsForTraceroute = 5;
static constexpr int kMaxTTL = 64;
static constexpr int kBaselinePingCount = 10;
static constexpr uint32 kPingInterval = 1000;       // 1 second
static constexpr uint32 kHostCollectTimeoutMs = 180'000; // 3 minutes
static constexpr uint32 kPrefsTimeoutMs = 180'000;  // 3 minutes
static constexpr uint32 kMaxPingMs = 5000;

// Multiplier ramp: fast for first 60s, then slower
static constexpr double kFastReactionMultiplier = 2.0;
static constexpr int kFastReactionDurationMs = 60'000;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

LastCommonRouteFinder::LastCommonRouteFinder(QObject* parent)
    : QThread(parent)
{
}

LastCommonRouteFinder::~LastCommonRouteFinder()
{
    endThread();
}

void LastCommonRouteFinder::endThread()
{
    m_run.store(false);

    // Wake up any waiting condition variables
    m_hostsCV.notify_all();
    m_prefsCV.notify_all();

    if (isRunning())
        wait();
}

// ---------------------------------------------------------------------------
// Thread-safe public API
// ---------------------------------------------------------------------------

bool LastCommonRouteFinder::addHostsToCheck(const std::vector<uint32>& ips)
{
    std::lock_guard lock(m_hostsMutex);
    if (!m_needMoreHosts)
        return false;

    for (uint32 ip : ips) {
        if (ip != 0 && isGoodIP(ip))
            m_hostsToTraceRoute[ip] = 0;
    }

    if (static_cast<int>(m_hostsToTraceRoute.size()) >= kMinHostsForTraceroute) {
        m_needMoreHosts = false;
        m_hostsCV.notify_all();
    }
    return true;
}

USSStatus LastCommonRouteFinder::currentStatus() const
{
    std::lock_guard lock(m_pingMutex);
    return USSStatus{
        m_stateString,
        m_pingAverage,
        m_lowestPing,
        m_upload.load()
    };
}

bool LastCommonRouteFinder::acceptNewClient() const
{
    return m_acceptNewClient.load();
}

bool LastCommonRouteFinder::setPrefs(const USSParams& params)
{
    {
        std::lock_guard lock(m_prefsMutex);
        m_pingTolerance = params.pingTolerance;
        m_curUpload = params.curUpload;
        m_minUpload = params.minUpload * 1024; // KB/s → bytes/s
        m_maxUpload = (params.maxUpload != UINT32_MAX) ? params.maxUpload * 1024 : UINT32_MAX;
        m_pingToleranceMilliseconds = params.pingToleranceMilliseconds;
        m_goingUpDivider = params.goingUpDivider;
        m_goingDownDivider = params.goingDownDivider;
        m_numberOfPingsForAverage = params.numberOfPingsForAverage;
        m_lowestInitialPingAllowed = params.lowestInitialPingAllowed;
        m_useMillisecondPingTolerance = params.useMillisecondPingTolerance;
        m_enabled = params.enabled;
    }
    m_prefsCV.notify_all();
    return true;
}

void LastCommonRouteFinder::initiateFastReactionPeriod()
{
    m_initiateFastReaction.store(1);
}

uint32 LastCommonRouteFinder::getUpload() const
{
    return m_upload.load();
}

// ---------------------------------------------------------------------------
// Median helper
// ---------------------------------------------------------------------------

uint32 LastCommonRouteFinder::median(std::vector<uint32>& values)
{
    if (values.empty())
        return 0;

    auto mid = values.begin() + static_cast<ptrdiff_t>(values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());

    if (values.size() % 2 == 0) {
        auto mid2 = std::max_element(values.begin(), mid);
        return (*mid + *mid2) / 2;
    }
    return *mid;
}

// ---------------------------------------------------------------------------
// Thread entry
// ---------------------------------------------------------------------------

void LastCommonRouteFinder::run()
{
    Pinger pinger;

    // --- Phase 0: Wait for preferences ---
    {
        std::unique_lock lock(m_prefsMutex);
        m_prefsCV.wait_for(lock, std::chrono::milliseconds(kPrefsTimeoutMs),
                           [this] { return !m_run.load() || m_enabled; });
    }

    while (m_run.load()) {
        bool enabled;
        {
            std::lock_guard lock(m_prefsMutex);
            enabled = m_enabled;
        }

        if (!enabled) {
            // USS disabled — pass through the prefs upload limit
            {
                std::lock_guard lock(m_prefsMutex);
                m_upload.store(m_maxUpload);
            }
            m_acceptNewClient.store(true);

            {
                std::lock_guard lock(m_pingMutex);
                m_stateString = QStringLiteral("USS disabled");
            }

            // Wait for prefs change or stop
            std::unique_lock lock(m_prefsMutex);
            m_prefsCV.wait_for(lock, std::chrono::milliseconds(kPrefsTimeoutMs),
                               [this] { return !m_run.load() || m_enabled; });
            continue;
        }

        // --- Phase 1: Collect hosts for traceroute ---
        {
            std::lock_guard lock(m_pingMutex);
            m_stateString = QStringLiteral("Collecting hosts for traceroute...");
        }

        {
            std::lock_guard lock(m_hostsMutex);
            m_hostsToTraceRoute.clear();
            m_needMoreHosts = true;
        }

        emit needMoreHosts();

        // Wait for hosts
        {
            std::unique_lock lock(m_hostsMutex);
            m_hostsCV.wait_for(lock, std::chrono::milliseconds(kHostCollectTimeoutMs),
                               [this] {
                                   return !m_run.load() || !m_needMoreHosts;
                               });
        }

        if (!m_run.load())
            break;

        // Snapshot hosts
        std::vector<uint32> hostIPs;
        {
            std::lock_guard lock(m_hostsMutex);
            hostIPs.reserve(m_hostsToTraceRoute.size());
            for (auto& [ip, _] : m_hostsToTraceRoute)
                hostIPs.push_back(ip);
        }

        if (hostIPs.empty()) {
            logWarning(QStringLiteral("USS: No hosts available for traceroute, retrying..."));
            QThread::msleep(5000);
            continue;
        }

        // --- Phase 2: Traceroute to find last common hop ---
        {
            std::lock_guard lock(m_pingMutex);
            m_stateString = QStringLiteral("Finding last common router hop...");
        }

        uint32 lastCommonHost = 0;
        uint8 lastCommonTTL = 0;

        if (!pinger.isIcmpAvailable()) {
            logWarning(QStringLiteral("USS: ICMP not available, using first host directly"));
            lastCommonHost = hostIPs.front();
            lastCommonTTL = kDefaultTTL;
        } else {
            // For each TTL, ping all hosts and check if responses come from same IP
            for (uint8 ttl = 1; ttl <= kMaxTTL && m_run.load(); ++ttl) {
                std::unordered_map<uint32, int> responseIPs;
                int validResponses = 0;

                for (uint32 hostIP : hostIPs) {
                    if (!m_run.load())
                        break;

                    PingStatus ps = pinger.ping(hostIP, ttl);
                    if (ps.success) {
                        ++validResponses;
                        responseIPs[ps.destinationAddress]++;
                    }
                }

                if (!m_run.load())
                    break;

                if (validResponses == 0)
                    continue;

                // Check if all responses came from same IP
                if (responseIPs.size() == 1) {
                    lastCommonHost = responseIPs.begin()->first;
                    lastCommonTTL = ttl;
                } else if (responseIPs.size() > 1) {
                    // Responses diverged — we found it
                    if (lastCommonHost != 0)
                        break; // Use the previous TTL's common host

                    // No common hop found before divergence; use the most frequent
                    uint32 bestIP = 0;
                    int bestCount = 0;
                    for (auto& [ip, count] : responseIPs) {
                        if (count > bestCount) {
                            bestCount = count;
                            bestIP = ip;
                        }
                    }
                    lastCommonHost = bestIP;
                    lastCommonTTL = ttl;
                    break;
                }

                // Check if any host responded with its own IP (reached destination)
                bool reachedDest = false;
                for (uint32 hip : hostIPs) {
                    if (responseIPs.contains(hip)) {
                        reachedDest = true;
                        break;
                    }
                }
                if (reachedDest) {
                    // All hosts are on same subnet; use previous hop if available
                    if (lastCommonHost != 0)
                        break;
                    // Otherwise use the destination itself
                    lastCommonHost = responseIPs.begin()->first;
                    lastCommonTTL = ttl;
                    break;
                }
            }
        }

        if (!m_run.load())
            break;

        if (lastCommonHost == 0) {
            logWarning(QStringLiteral("USS: Could not find common route, retrying..."));
            QThread::msleep(10'000);
            continue;
        }

        logInfo(QStringLiteral("USS: Found last common hop at TTL %1: %2")
                    .arg(lastCommonTTL)
                    .arg(ipstr(lastCommonHost)));

        // --- Phase 3: Establish baseline ping ---
        {
            std::lock_guard lock(m_pingMutex);
            m_stateString = QStringLiteral("Establishing baseline ping...");
        }

        std::vector<uint32> baselinePings;
        baselinePings.reserve(kBaselinePingCount);

        for (int i = 0; i < kBaselinePingCount && m_run.load(); ++i) {
            PingStatus ps = pinger.ping(lastCommonHost, lastCommonTTL);
            if (ps.success && ps.delay < kMaxPingMs) {
                baselinePings.push_back(static_cast<uint32>(ps.delay));
            }
            QThread::msleep(kPingInterval);
        }

        if (!m_run.load())
            break;

        if (baselinePings.empty()) {
            logWarning(QStringLiteral("USS: Could not establish baseline ping, retrying..."));
            QThread::msleep(10'000);
            continue;
        }

        uint32 initialPing = median(baselinePings);

        uint32 lowestInitAllowed;
        {
            std::lock_guard lock(m_prefsMutex);
            lowestInitAllowed = m_lowestInitialPingAllowed;
        }

        if (initialPing < lowestInitAllowed) {
            logInfo(QStringLiteral("USS: Baseline ping %1ms below minimum %2ms, using minimum")
                        .arg(initialPing).arg(lowestInitAllowed));
            initialPing = lowestInitAllowed;
        }

        {
            std::lock_guard lock(m_pingMutex);
            m_lowestPing = initialPing;
            m_pingDelays.clear();
            m_pingDelaysTotal = 0;
        }

        logInfo(QStringLiteral("USS: Baseline ping: %1ms").arg(initialPing));

        // --- Phase 4: Dynamic adjustment loop ---
        {
            std::lock_guard lock(m_pingMutex);
            m_stateString = QStringLiteral("Active — monitoring latency");
        }

        QElapsedTimer phaseTimer;
        phaseTimer.start();
        bool fastReactionActive = false;
        qint64 fastReactionStart = 0;

        // Initialize upload to current prefs
        {
            std::lock_guard lock(m_prefsMutex);
            if (m_curUpload != UINT32_MAX)
                m_upload.store(m_curUpload);
            else
                m_upload.store(m_maxUpload);
        }

        while (m_run.load()) {
            // Check if still enabled
            {
                std::lock_guard lock(m_prefsMutex);
                if (!m_enabled)
                    break; // Go back to outer loop
            }

            // Ping the common hop
            PingStatus ps = pinger.ping(lastCommonHost, lastCommonTTL);

            if (ps.success && ps.delay < kMaxPingMs) {
                uint32 pingMs = static_cast<uint32>(ps.delay);

                // Read prefs
                uint32 numPingsForAvg, goingUpDiv, goingDownDiv, minUp, maxUp;
                uint32 pingTolMs;
                double pingTol;
                bool useMsTol;

                {
                    std::lock_guard lock(m_prefsMutex);
                    numPingsForAvg = m_numberOfPingsForAverage;
                    goingUpDiv = m_goingUpDivider;
                    goingDownDiv = m_goingDownDivider;
                    minUp = m_minUpload;
                    maxUp = m_maxUpload;
                    pingTolMs = m_pingToleranceMilliseconds;
                    pingTol = m_pingTolerance;
                    useMsTol = m_useMillisecondPingTolerance;
                }

                // Update ring buffer
                {
                    std::lock_guard lock(m_pingMutex);
                    m_pingDelays.push_back(pingMs);
                    m_pingDelaysTotal += pingMs;

                    while (m_pingDelays.size() > numPingsForAvg) {
                        m_pingDelaysTotal -= m_pingDelays.front();
                        m_pingDelays.pop_front();
                    }

                    // Compute median
                    std::vector<uint32> tmp(m_pingDelays.begin(), m_pingDelays.end());
                    m_pingAverage = median(tmp);
                }

                // Calculate normalized ping (above baseline)
                uint32 currentMedian;
                {
                    std::lock_guard lock(m_pingMutex);
                    currentMedian = m_pingAverage;
                }

                int32 normalizedPing = static_cast<int32>(currentMedian) - static_cast<int32>(initialPing);
                if (normalizedPing < 0)
                    normalizedPing = 0;

                // Calculate tolerance threshold
                uint32 threshold;
                if (useMsTol)
                    threshold = pingTolMs;
                else
                    threshold = static_cast<uint32>(initialPing * pingTol);

                // Check for fast reaction
                if (m_initiateFastReaction.exchange(0) != 0) {
                    fastReactionActive = true;
                    fastReactionStart = phaseTimer.elapsed();
                }

                if (fastReactionActive &&
                    (phaseTimer.elapsed() - fastReactionStart) > kFastReactionDurationMs) {
                    fastReactionActive = false;
                }

                double multiplier = fastReactionActive ? kFastReactionMultiplier : 1.0;

                // Adjust upload
                uint32 currentUpload = m_upload.load();

                if (static_cast<uint32>(normalizedPing) > threshold) {
                    // Congestion detected — decrease
                    uint32 decrease = std::max<uint32>(1, static_cast<uint32>(
                        currentUpload * multiplier / goingDownDiv));
                    if (currentUpload > decrease + minUp)
                        currentUpload -= decrease;
                    else
                        currentUpload = minUp;

                    m_acceptNewClient.store(false);
                } else {
                    // Room available — increase
                    uint32 increase = std::max<uint32>(1, static_cast<uint32>(
                        currentUpload * multiplier / goingUpDiv));
                    if (currentUpload + increase < maxUp)
                        currentUpload += increase;
                    else
                        currentUpload = maxUp;

                    m_acceptNewClient.store(true);
                }

                // Clamp
                currentUpload = std::clamp(currentUpload, minUp, maxUp);
                m_upload.store(currentUpload);
                emit uploadLimitChanged(currentUpload);

            } else if (ps.success) {
                // Ping was valid but very high — check for topology change
                // (response from unexpected host)
                // If topology changed, restart traceroute
                if (ps.destinationAddress != lastCommonHost &&
                    ps.destinationAddress != 0 &&
                    ps.status != kPingTTLExpired) {
                    logInfo(QStringLiteral("USS: Topology change detected, restarting traceroute"));
                    break; // Restart from phase 1
                }
            }

            QThread::msleep(kPingInterval);
        }
    }

    // Clean up
    {
        std::lock_guard lock(m_pingMutex);
        m_stateString = QStringLiteral("Stopped");
    }
}

} // namespace eMule
