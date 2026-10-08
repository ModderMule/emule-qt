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

static constexpr int kMaxTTL = 64;
static constexpr int kBaselinePingCount = 10;
static constexpr uint32 kBaselinePingInterval = 200;
static constexpr uint32 kHostAskIntervalMs = 30'000;  // ask the lists again
static constexpr uint32 kPrefsTimeoutMs = 180'000;    // 3 minutes
static constexpr int kMaxPingTries = 60;              // then trace the route anew
static constexpr uint32 kTraceUpload = 2 * 1024;      // upload held down while tracing

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

    if (m_hostsToTraceRoute.size() >= kHostsToTrace) {
        m_needMoreHosts = false;
        m_hostsCV.notify_all();
    }
    return true;
}

USSStatus LastCommonRouteFinder::currentStatus() const
{
    std::lock_guard lock(m_pingMutex);
    USSStatus status;
    status.phase = m_state;
    switch (m_state) {
    case UssState::Off:       status.state = QStringLiteral("USS disabled"); break;
    case UssState::Preparing: status.state = QStringLiteral("Preparing..."); break;
    case UssState::Waiting:   status.state = QStringLiteral("Waiting..."); break;
    case UssState::Error:     status.state = QStringLiteral("Error!"); break;
    case UssState::Active:    status.state = QStringLiteral("Active"); break;
    }
    status.latency = m_pingAverage;
    status.lowest = m_lowestPing;
    status.currentLimit = m_upload.load();
    status.active = m_state == UssState::Active;
    return status;
}

void LastCommonRouteFinder::setState(UssState state)
{
    std::lock_guard lock(m_pingMutex);
    m_state = state;
}

bool LastCommonRouteFinder::enabledNow() const
{
    std::lock_guard lock(m_prefsMutex);
    return m_enabled;
}

bool LastCommonRouteFinder::hasGivenUp() const
{
    std::lock_guard lock(m_prefsMutex);
    return m_gaveUp;
}

bool LastCommonRouteFinder::waitWhileEnabled(uint32 ms)
{
    std::unique_lock lock(m_prefsMutex);
    m_prefsCV.wait_for(lock, std::chrono::milliseconds(ms),
                       [this] { return !m_run.load() || !m_enabled; });
    return m_run.load() && m_enabled;
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
        // After giving up the option has to go off once before it counts again
        if (!params.enabled)
            m_gaveUp = false;
        m_enabled = params.enabled && !m_gaveUp;
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
// Hop search
// ---------------------------------------------------------------------------

RouteProbe LastCommonRouteFinder::traceLastCommonHost(std::vector<uint32>& hosts,
                                                      const PingFn& ping,
                                                      const std::function<bool()>& keepGoing,
                                                      const std::function<void(uint32)>& pause)
{
    RouteProbe probe;
    bool failed = false;
    uint32 curHost = 0;   // router answering at this TTL

    // No router within the first four hops: there is nothing to trace
    for (int ttl = 1; keepGoing() && !probe.found && !failed
                      && ((curHost != 0 && ttl <= kMaxTTL) || (curHost == 0 && ttl < 5)); ++ttl)
    {
        probe.useUdp = false;
        curHost = 0;
        uint32 lastSuccessfulPingAddress = 0;
        uint32 lastDestinationAddress = 0;
        uint32 hostCounter = 0;
        bool failedThisTtl = false;

        // Stops at the first host answering from another router than the one before
        for (std::size_t i = 0; keepGoing() && !failed && !failedThisTtl && i < hosts.size()
                                && (lastDestinationAddress == 0 || lastDestinationAddress == curHost);)
        {
            ++hostCounter;
            const uint32 address = hosts[i];

            PingStatus status;
            for (int cnt = 0; keepGoing() && cnt < 2; ++cnt) {
                status = ping(address, static_cast<uint8>(ttl), probe.useUdp);
                if (status.success
                    && (status.status == kPingSuccess || status.status == kPingTTLExpired))
                    break;
                if (cnt == 0)
                    pause(1000);
                probe.useUdp = !probe.useUdp;
            }

            bool removed = false;
            if (status.success) {
                switch (status.status) {
                case kPingTTLExpired:
                    if (curHost == 0)
                        curHost = status.destinationAddress;
                    lastSuccessfulPingAddress = address;
                    lastDestinationAddress = status.destinationAddress;
                    break;
                case kPingSuccess:           // answered itself: closer than the hop
                case kPingDestUnreachable:
                    hosts.erase(hosts.begin() + static_cast<std::ptrdiff_t>(i));
                    removed = true;
                    break;
                default:
                    probe.useUdp = !probe.useUdp;
                }
            } else {
                if (status.error == kPingTimedOut) {
                    // A silent host stays; three silent ones in a row end this TTL
                    if (hostCounter > 2 && lastSuccessfulPingAddress == 0)
                        failedThisTtl = true;
                } else {
                    probe.useUdp = !probe.useUdp;
                }
                if (hosts.size() <= kTooFewHosts)
                    failed = true;
            }
            if (!removed)
                ++i;
        }

        if (failed)
            break;
        if (curHost != 0 && lastDestinationAddress != 0) {
            if (lastDestinationAddress == curHost) {
                probe.lastCommonHost = curHost;
                probe.lastCommonTTL = static_cast<uint8>(ttl);
            } else {
                // The routes part here: ping a host behind the last shared router
                probe.found = true;
                probe.hostToPing = lastSuccessfulPingAddress;
                if (probe.lastCommonHost == 0) {
                    // The hop before could not be pinged; take this one on trust
                    probe.lastCommonHost = lastDestinationAddress;
                    probe.lastCommonTTL = static_cast<uint8>(ttl);
                }
            }
        } else {
            probe.lastCommonHost = 0;
        }
    }
    if (!probe.found)
        probe.lastCommonHost = 0;
    return probe;
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

    // A route that was found once is searched for without limit; a first search gives
    // up after five tries (MFC hasSucceededAtLeastOnce).
    bool hasSucceededAtLeastOnce = false;

    while (m_run.load()) {
        if (!enabledNow()) {
            // USS disabled — pass through the prefs upload limit
            {
                std::lock_guard lock(m_prefsMutex);
                m_upload.store(m_maxUpload);
            }
            m_acceptNewClient.store(true);
            {
                // "Error!" stays up: the option went off because tracing failed
                std::lock_guard lock(m_pingMutex);
                if (m_state != UssState::Error)
                    m_state = UssState::Off;
            }

            // Wait for prefs change or stop
            std::unique_lock lock(m_prefsMutex);
            m_prefsCV.wait_for(lock, std::chrono::milliseconds(kPrefsTimeoutMs),
                               [this] { return !m_run.load() || m_enabled; });
            continue;
        }

        // --- Phase 1 + 2: collect hosts, trace the route (MFC :273-500) ---
        {
            std::lock_guard lock(m_pingMutex);
            m_state = UssState::Preparing;
            m_pingDelays.clear();
            m_pingDelaysTotal = 0;
            m_pingAverage = 0;
            m_lowestPing = 0;
        }
        {
            std::lock_guard lock(m_hostsMutex);
            m_hostsToTraceRoute.clear();
        }

        uint32 startUpload;
        {
            std::lock_guard lock(m_prefsMutex);
            startUpload = m_maxUpload != UINT32_MAX ? m_maxUpload
                                                    : std::max<uint32>(m_curUpload, 10 * 1024);
        }

        const auto keepGoing = [this] { return m_run.load() && enabledNow(); };
        const PingFn pingFn = [&pinger](uint32 addr, uint8 ttl, bool useUdp) {
            return pinger.ping(addr, ttl, useUdp);
        };

        RouteProbe probe;
        std::vector<uint32> hosts;
        for (uint32 tries = 0; keepGoing() && !probe.found
                               && (tries < 5 || hasSucceededAtLeastOnce);)
        {
            ++tries;

            // Hosts dropped by an earlier try are made up for
            {
                std::lock_guard lock(m_hostsMutex);
                m_needMoreHosts = m_hostsToTraceRoute.size() < kHostsToTrace;
            }
            for (;;) {
                {
                    std::lock_guard lock(m_hostsMutex);
                    if (!m_needMoreHosts)
                        break;
                }
                emit needMoreHosts();
                {
                    std::unique_lock lock(m_hostsMutex);
                    m_hostsCV.wait_for(lock, std::chrono::milliseconds(kHostAskIntervalMs),
                                       [this] { return !m_run.load() || !m_needMoreHosts; });
                }
                if (!keepGoing())
                    break;
            }
            if (!keepGoing())
                break;
            {
                std::lock_guard lock(m_hostsMutex);
                hosts.clear();
                for (const auto& [ip, _] : m_hostsToTraceRoute)
                    hosts.push_back(ip);
            }

            // Our own upload would distort the pings
            m_upload.store(kTraceUpload);
            emit uploadLimitChanged(kTraceUpload);
            if (!waitWhileEnabled(1000))
                break;

            logDebug(QStringLiteral("USS: Try #%1, tracing %2 hosts").arg(tries).arg(hosts.size()));
            probe = traceLastCommonHost(hosts, pingFn, keepGoing,
                                        [this](uint32 ms) { waitWhileEnabled(ms); });

            // What the try dropped (too close, unreachable) stays dropped
            {
                std::lock_guard lock(m_hostsMutex);
                std::erase_if(m_hostsToTraceRoute, [&hosts](const auto& entry) {
                    return std::ranges::find(hosts, entry.first) == hosts.end();
                });
            }

            if (!probe.found && tries >= 3 && keepGoing()) {
                logDebug(QStringLiteral("USS: Tracing failed several times, resting 3 minutes"));
                {
                    std::lock_guard lock(m_prefsMutex);
                    m_upload.store(m_maxUpload);
                }
                emit uploadLimitChanged(m_upload.load());
                setState(UssState::Waiting);
                waitWhileEnabled(3 * 60 * 1000);
                setState(UssState::Preparing);
            }
        }

        if (!m_run.load())
            break;
        if (!enabledNow())
            continue;

        if (!probe.found) {
            logWarning(QStringLiteral("UploadSpeedSense: Tracing the route failed too often. "
                                      "Disabling UploadSpeedSense."));
            {
                std::lock_guard lock(m_prefsMutex);
                m_gaveUp = true;
                m_enabled = false;
                m_upload.store(m_maxUpload);
            }
            setState(UssState::Error);
            emit tracerouteGaveUp();
            continue;
        }

        const uint32 lastCommonHost = probe.lastCommonHost;
        const uint8 lastCommonTTL = probe.lastCommonTTL;
        const uint32 hostToPing = probe.hostToPing;
        bool useUdp = probe.useUdp;
        logInfo(QStringLiteral("USS: Last common hop at TTL %1: %2, pinging through it to %3")
                    .arg(lastCommonTTL).arg(ipstr(lastCommonHost), ipstr(hostToPing)));

        // --- Phase 3: lowest ping, the smallest of ten (MFC :502-549) ---
        uint32 lowestInitAllowed;
        {
            std::lock_guard lock(m_prefsMutex);
            lowestInitAllowed = m_lowestInitialPingAllowed;
        }
        uint32 initialPing = UINT32_MAX;
        bool foundWorkingPingMethod = false;
        for (int i = 0; i < kBaselinePingCount && keepGoing(); ++i) {
            QThread::msleep(kBaselinePingInterval);
            const PingStatus ps = pinger.ping(hostToPing, lastCommonTTL, useUdp);
            if (ps.success && ps.status == kPingTTLExpired) {
                foundWorkingPingMethod = true;
                if (ps.delay > 0 && ps.delay < static_cast<float>(initialPing))
                    initialPing = std::max(static_cast<uint32>(ps.delay), lowestInitAllowed);
            } else if (!ps.success && !foundWorkingPingMethod) {
                useUdp = !useUdp;
            }
        }
        if (initialPing == UINT32_MAX)
            initialPing = lowestInitAllowed;

        m_upload.store(startUpload);
        emit uploadLimitChanged(startUpload);
        if (!waitWhileEnabled(1000))
            continue;
        hasSucceededAtLeastOnce = true;

        logInfo(QStringLiteral("USS: Lowest ping: %1 ms").arg(initialPing));

        // --- Phase 4: Dynamic adjustment loop ---
        {
            std::lock_guard lock(m_pingMutex);
            m_lowestPing = initialPing;
            m_state = UssState::Active;
        }

        // The staged ramp runs from here, and again from every manual limit change.
        QElapsedTimer rampTimer;
        rampTimer.start();
        QElapsedTimer loopTimer;
        loopTimer.start();

        // The controller itself starts from the measured rate. MFC :558-559.
        uint32 upload;
        {
            std::lock_guard lock(m_prefsMutex);
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

            // Ping through the common hop: the answer is its "TTL exceeded". A silent
            // hop is retried for a while before the route is traced anew (MFC :640-668).
            bool pinged = false;
            uint32 pingMs = 0;
            for (int tries = 0; keepGoing() && !pinged && tries < kMaxPingTries; ++tries) {
                const PingStatus ps = pinger.ping(hostToPing, lastCommonTTL, useUdp);
                if (ps.success && ps.status == kPingTTLExpired) {
                    if (ps.destinationAddress != lastCommonHost) {
                        logInfo(QStringLiteral("USS: Network topology has changed at TTL %1 "
                                               "(expected %2, got %3), tracing again")
                                    .arg(lastCommonTTL)
                                    .arg(ipstr(lastCommonHost), ipstr(ps.destinationAddress)));
                        restart = true;
                    }
                    pingMs = static_cast<uint32>(ps.delay);
                    pinged = true;
                } else if (tries > 3) {
                    waitWhileEnabled(1000);
                }
            }
            if (!m_run.load())
                break;
            if (!pinged) {
                if (enabledNow())
                    logInfo(QStringLiteral("USS: No answer to pings for a long time, tracing again"));
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

    setState(UssState::Off);
}

} // namespace eMule
