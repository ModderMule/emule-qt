#include "pch.h"
/// @file LastCommonRouteFinder.cpp
/// @brief Adaptive upload bandwidth control via latency-based route analysis.

#include "net/LastCommonRouteFinder.h"
#include "net/Pinger.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"

#include <QElapsedTimer>


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

static constexpr int kMaxPingTries = 60;            // then look for a new host

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
        // KB/s → bytes/s; a minimum above the maximum gives way. MFC :181-189.
        m_minUpload = params.minUpload ? params.minUpload * 1024 : 1024;
        m_maxUpload = (params.maxUpload != UINT32_MAX) ? params.maxUpload * 1024 : UINT32_MAX;
        m_minUpload = std::min(m_minUpload, m_maxUpload);
        m_pingToleranceMilliseconds = params.pingToleranceMilliseconds;
        m_goingUpDivider = params.goingUpDivider;
        m_goingDownDivider = params.goingDownDivider;
        m_numberOfPingsForAverage = params.numberOfPingsForAverage;
        m_lowestInitialPingAllowed = params.lowestInitialPingAllowed;
        m_useMillisecondPingTolerance = params.useMillisecondPingTolerance;
        m_enabled = params.enabled;
        m_prefsReceived = true;

        // A changed limit counts at once, not when run() next wakes. MFC :211-212.
        if (!m_enabled || m_upload.load() > m_maxUpload)
            m_upload.store(m_maxUpload);
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
// Control step
// ---------------------------------------------------------------------------

uint32 LastCommonRouteFinder::adjustUpload(uint32 upload, uint32 curUpload,
                                           int32 normalizedPing, uint32 targetPing,
                                           uint32 lowestPing, uint32 goingUpDivider,
                                           uint32 goingDownDivider, uint32 minUpload,
                                           uint32 maxUpload, bool& acceptNewClient)
{
    const int64 upDiv = std::max<uint32>(goingUpDivider, 1);
    const int64 downDiv = std::max<uint32>(goingDownDivider, 1);
    const int64 lowest = std::max<uint32>(lowestPing, 1);
    const int64 headroom = static_cast<int64>(targetPing) - normalizedPing;

    if (headroom < 0) {
        acceptNewClient = false;
        const int64 diff = headroom * 1024 * 10 / downDiv / lowest;
        upload = (static_cast<int64>(upload) > -diff) ? static_cast<uint32>(upload + diff) : 0;
    } else if (headroom > 0) {
        acceptNewClient = true;
        // No point raising a limit the upload is not even close to.
        if (static_cast<uint64>(curUpload) + 30 * 1024 > upload) {
            const int64 diff = headroom * 1024 * 10 / (upDiv * lowest);
            upload = (INT32_MAX - static_cast<int64>(upload) > diff)
                         ? static_cast<uint32>(upload + diff) : static_cast<uint32>(INT32_MAX);
        }
    }

    if (upload < minUpload) {
        upload = minUpload;
        acceptNewClient = true;
    }
    return std::min(upload, maxUpload);
}

void LastCommonRouteFinder::rampDividers(qint64 msSinceStart, uint32& goingUpDivider,
                                         uint32& goingDownDivider)
{
    uint32 mul = 0;
    if (msSinceStart < 20'000)
        mul = 4;
    else if (msSinceStart < 30'000)
        mul = 1;
    else if (msSinceStart < 40'000)
        mul = 2;
    else if (msSinceStart < 60'000)
        mul = 3;

    if (mul) {
        goingUpDivider = goingUpDivider * mul / 4;
        goingDownDivider = goingDownDivider * mul / 4;
    }
    goingUpDivider = std::max<uint32>(goingUpDivider, 1);
    goingDownDivider = std::max<uint32>(goingDownDivider, 1);
}

uint32 LastCommonRouteFinder::pingIntervalMs(uint32 upload)
{
    if (upload == 0)
        return 1000;
    return std::clamp<uint32>(64u * 100u * 1000u / upload, 125, 1000);
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
    // Wake on m_prefsReceived as well as m_enabled: CoreSession pushes prefs every second,
    // but with USS disabled (the default) m_enabled never turns true, so waiting on it alone
    // burned the whole kPrefsTimeoutMs before the loop below could publish the pass-through
    // limit or the status string. The disabled-branch wait further down deliberately does
    // NOT test m_prefsReceived — the flag stays set, so it would spin.
    {
        std::unique_lock lock(m_prefsMutex);
        m_prefsCV.wait_for(lock, std::chrono::milliseconds(kPrefsTimeoutMs),
                           [this] { return !m_run.load() || m_enabled || m_prefsReceived; });
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

        // The staged ramp runs from here, and again from every manual limit change.
        QElapsedTimer rampTimer;
        rampTimer.start();
        QElapsedTimer loopTimer;
        loopTimer.start();

        // Published limit starts at the maximum (unlimited: at what we upload now);
        // the controller itself starts from the measured rate. MFC :294, :539, :558-559.
        uint32 upload;
        {
            std::lock_guard lock(m_prefsMutex);
            m_upload.store(m_maxUpload != UINT32_MAX ? m_maxUpload
                                                     : std::max<uint32>(m_curUpload, 10 * 1024));
            upload = std::min(std::max(m_curUpload, m_minUpload), m_maxUpload);
        }

        bool restart = false;
        while (m_run.load() && !restart) {
            // Ping traffic stays near 1% of the upload.
            const qint64 sinceLastLoop = loopTimer.restart();
            const uint32 interval = pingIntervalMs(upload);
            if (sinceLastLoop < interval) {
                QThread::msleep(static_cast<unsigned long>(interval - sinceLastLoop));
                loopTimer.restart();
            }

            uint32 numPingsForAvg, goingUpDiv, goingDownDiv, minUp, maxUp, curUp;
            uint32 pingTolMs, lowestInitAllowedNow;
            double pingTol;
            bool useMsTol;
            {
                std::lock_guard lock(m_prefsMutex);
                if (!m_enabled)
                    break; // Go back to outer loop
                numPingsForAvg = m_numberOfPingsForAverage;
                goingUpDiv = m_goingUpDivider;
                goingDownDiv = m_goingDownDivider;
                minUp = m_minUpload;
                maxUp = m_maxUpload;
                curUp = m_curUpload;
                pingTolMs = m_pingToleranceMilliseconds;
                pingTol = m_pingTolerance;
                useMsTol = m_useMillisecondPingTolerance;
                lowestInitAllowedNow = m_lowestInitialPingAllowed;
            }

            if (m_initiateFastReaction.exchange(0) != 0)
                rampTimer.restart();
            rampDividers(rampTimer.elapsed(), goingUpDiv, goingDownDiv);

            // Ping the common hop; a silent hop is retried before the route is given up.
            bool pinged = false;
            uint32 pingMs = 0;
            for (int tries = 0; m_run.load() && !pinged && tries < kMaxPingTries; ++tries) {
                {
                    std::lock_guard lock(m_prefsMutex);
                    if (!m_enabled)
                        break;
                }
                const PingStatus ps = pinger.ping(lastCommonHost, lastCommonTTL);
                if (ps.success && ps.delay < kMaxPingMs) {
                    pingMs = static_cast<uint32>(ps.delay);
                    pinged = true;
                } else if (ps.success && ps.destinationAddress != lastCommonHost
                           && ps.destinationAddress != 0 && ps.status != kPingTTLExpired) {
                    logInfo(QStringLiteral("USS: Topology change detected, restarting traceroute"));
                    restart = true;
                    break;
                } else if (tries > 3) {
                    QThread::msleep(1000);
                }
            }
            if (restart || !m_run.load())
                break;
            if (!pinged) {
                bool stillEnabled;
                {
                    std::lock_guard lock(m_prefsMutex);
                    stillEnabled = m_enabled;
                }
                if (stillEnabled)
                    logInfo(QStringLiteral("USS: No answer from the pinged hop, restarting traceroute"));
                break;
            }

            // A faster answer than the baseline is the new baseline.
            if (pingMs > 0 && pingMs < initialPing && initialPing > lowestInitAllowedNow)
                initialPing = std::max(pingMs, lowestInitAllowedNow);

            uint32 currentMedian;
            {
                std::lock_guard lock(m_pingMutex);
                m_pingDelays.push_back(pingMs);
                m_pingDelaysTotal += pingMs;
                while (m_pingDelays.size() > numPingsForAvg) {
                    m_pingDelaysTotal -= m_pingDelays.front();
                    m_pingDelays.pop_front();
                }
                std::vector<uint32> tmp(m_pingDelays.begin(), m_pingDelays.end());
                m_pingAverage = median(tmp);
                m_lowestPing = initialPing;
                currentMedian = m_pingAverage;
            }

            const int32 normalizedPing =
                static_cast<int32>(currentMedian) - static_cast<int32>(initialPing);
            const uint32 targetPing = useMsTol ? pingTolMs
                                               : static_cast<uint32>(initialPing * pingTol);

            bool accept = m_acceptNewClient.load();
            upload = adjustUpload(upload, curUp, normalizedPing, targetPing, initialPing,
                                  goingUpDiv, goingDownDiv, minUp, maxUp, accept);
            m_acceptNewClient.store(accept);
            m_upload.store(upload);
            emit uploadLimitChanged(upload);
        }
    }

    // Clean up
    {
        std::lock_guard lock(m_pingMutex);
        m_stateString = QStringLiteral("Stopped");
    }
}

} // namespace eMule
