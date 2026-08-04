#include "pch.h"
/// @file Kademlia.cpp
/// @brief Main Kademlia DHT engine implementation.

#include "kademlia/Kademlia.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadClientSearcher.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadIndexed.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUDPListener.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "prefs/Preferences.h"
#include "ipfilter/IPFilter.h"
#include "utils/Log.h"

#include <QDir>

#include <algorithm>
#include <vector>


namespace eMule::kad {

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

ContactList Kademlia::s_bootstrapList;
Kademlia* Kademlia::s_instance = nullptr;
eMule::IPFilter* Kademlia::s_ipFilter = nullptr;
eMule::ClientList* Kademlia::s_clientList = nullptr;
Kademlia::KadKeywordResultCallback Kademlia::s_keywordResultCb;
Kademlia::KadSourceResultCallback Kademlia::s_sourceResultCb;
Kademlia::KadNotesResultCallback Kademlia::s_notesResultCb;

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

Kademlia::Kademlia(QObject* parent)
    : QObject(parent)
{
    // Set the singleton at construction, not in start(), so Kademlia::instance()
    // is addressable for a manual connect (GUI/IPC) even before Kad is started —
    // matching MFC's always-addressable static CKademlia. The destructor clears
    // it. All external instance() callers already guard on isRunning()/
    // isConnected() (or null-check sub-objects), so a constructed-but-not-running
    // instance is safe; it is the same state left behind by stop().
    s_instance = this;
}

Kademlia::~Kademlia()
{
    if (m_running)
        stop();
    if (s_instance == this)
        s_instance = nullptr;
}

void Kademlia::start()
{
    start(nullptr);
}

void Kademlia::start(KadPrefs* prefs)
{
    if (m_running) {
        logKad(QStringLiteral("Kad: Already running"));
        return;
    }

    setKadLogging(thePrefs.kadVerboseLog());
    logKad(QStringLiteral("Kad: Starting Kademlia"));

    if (prefs) {
        m_prefs = prefs;
        m_ownsPrefs = false;
    } else if (!m_prefs) {
        const QString cfgDir = !thePrefs.configDir().isEmpty()
            ? thePrefs.configDir() : QDir::tempPath();
        m_prefs = new KadPrefs(cfgDir);
        m_ownsPrefs = true;
    }

    // Create UDP listener — socket binding is done externally by CoreSession
    // via ClientUDPSocket (shared socket for both client and Kad traffic).
    m_udpListener = new KademliaUDPListener(this);

    // Create indexed storage
    m_indexed = new Indexed(this);

    // s_instance is set in the constructor, so the zones created below can
    // already register via Kademlia::instance().

    // Create routing zone — use config directory for nodes.dat persistence
    const QString cfgDir = thePrefs.configDir();
    const QString nodesFile = (cfgDir.isEmpty() ? QDir::tempPath() : cfgDir)
        + QStringLiteral("/nodes.dat");
    m_routingZone = new RoutingZone(m_prefs->kadId(), nodesFile, this);
    m_prefs->setRoutingZone(m_routingZone);

    // Initialize timers — matches MFC Kademlia.cpp Start()
    time_t now = time(nullptr);
    m_nextSearchJumpStart = now;
    m_nextSelfLookup = now + MIN2S(3);
    m_nextFirewallCheck = now + HR2S(1);
    m_nextFindBuddy = now + MIN2S(5);   // MFC Kademlia.cpp:125
    m_statusUpdate = now + SEC(60);
    m_bigTimer = now;  // MFC: m_tBigTimer = tNow (per-zone timers gate first fire)
    m_consolidate = now + MIN2S(45);
    m_externPortLookup = now;
    // Zero, not `now` — MFC Kademlia.cpp:131. This is a "last bootstrap attempt"
    // stamp, and stamping it at startup arms both rate limits against us during the
    // one window where bootstrapping matters most: process() step 1 would wait 2-15s
    // before probing the bootstrap list, and bootstrap() would ignore every peer that
    // advertises a Kad port in the first 10 seconds.
    m_bootstrap = 0;

    // Start process timer (1-second interval)
    m_processTimer = new QTimer(this);
    m_processTimer->setInterval(1000);
    connect(m_processTimer, &QTimer::timeout, this, &Kademlia::process);
    m_processTimer->start();

    m_running = true;
    // We are actively bootstrapping only if readFile queued shipped bootstrap
    // contacts to probe (fresh install). A normal saved nodes.dat populates the
    // routing table directly and connects via the HELLO flow, so bootstrapping
    // stays false and step 1 never logs a spurious "bootstrap failed".
    m_bootstrapping = !s_bootstrapList.empty();
    UDPFirewallTester::reset();

    emit started();
    logKad(QStringLiteral("Kad: Started with ID %1").arg(m_prefs->kadId().toHexString()));
}

void Kademlia::stop()
{
    if (!m_running)
        return;

    logKad(QStringLiteral("Kad: Stopping Kademlia"));

    m_running = false;
    m_bootstrapping = false;
    // Note: s_instance intentionally NOT cleared here — the object still
    // exists, just isn't running.  Cleared in the destructor so that
    // handleBootstrapKad can call start() on a stopped instance.

    // Stop timer
    if (m_processTimer) {
        m_processTimer->stop();
        delete m_processTimer;
        m_processTimer = nullptr;
    }

    // Stop all searches
    SearchManager::stopAllSearches();

    // Clear zone events before deleting routing zones.
    // MFC: m_mapEvents.clear() in Stop().
    m_zoneEvents.clear();

    // Clean up components (reverse order of creation)
    delete m_routingZone;
    m_routingZone = nullptr;

    delete m_indexed;
    m_indexed = nullptr;

    delete m_udpListener;
    m_udpListener = nullptr;

    // Delete prefs only if we created them internally.
    if (m_ownsPrefs) {
        delete m_prefs;
        m_prefs = nullptr;
        m_ownsPrefs = false;
    }

    m_safeKad.shutdownCleanup();
    m_fastKad.shutdownCleanup();

    UDPFirewallTester::reset();

    // Clear bootstrap list
    for (auto* c : s_bootstrapList)
        delete c;
    s_bootstrapList.clear();

    emit stopped();
    logKad(QStringLiteral("Kad: Stopped"));
}

bool Kademlia::isConnected() const
{
    return m_running && m_prefs && m_prefs->hasHadContact();
}

bool Kademlia::isKadReady() const
{
    return isConnected()
        && m_udpListener
        && m_udpListener->totalHellosReceived() >= kMinPacketsForReady;
}

bool Kademlia::isFirewalled() const
{
    if (!m_running || !m_prefs)
        return true;
    if (shouldSkipFirewallChecks())
        return false;
    return m_prefs->firewalled();
}

RecheckFirewallResult Kademlia::recheckFirewalled()
{
    if (!m_running || !m_prefs)
        return RecheckFirewallResult::NotRunning;
    if (isRunningInLANMode())
        return RecheckFirewallResult::LanMode;

    // Only one NodeFwCheckUDP lookup at a time — each one queries 11 contacts,
    // so an unguarded "Recheck Firewall" button stacks them up. The guard clears
    // itself: jumpStart() reaps the lookup after kSearchNodeLifetime.
    if (SearchManager::isNodeFWCheckUDPSearchActive()) {
        logKad(QStringLiteral("Kad: Firewall recheck ignored — a check is already running"));
        return RecheckFirewallResult::AlreadyRunning;
    }

    // Stop any pending buddy search and force a firewall re-check.
    // Matches MFC Kademlia.cpp:409-426.
    m_prefs->setFindBuddy(false);
    m_prefs->setRecheckIP();
    UDPFirewallTester::reCheckFirewallUDP(false);

    const auto now = static_cast<time_t>(time(nullptr));
    // Delay the next buddy search to at least 5 min so the firewall recheck has
    // time to complete and we don't start a buddy search based on stale
    // firewalled status. MFC Kademlia.cpp:422-423.
    if (m_nextFindBuddy < now + MIN2S(5))
        m_nextFindBuddy = now + MIN2S(5);
    m_nextFirewallCheck = now + HR2S(1);
    return RecheckFirewallResult::Started;
}

uint32 Kademlia::getKademliaUsers(bool newMethod) const
{
    if (!m_running || !m_prefs)
        return 0;
    if (newMethod)
        return calculateKadUsersNew();
    return m_prefs->kademliaUsers();
}

// Helpers returning the currently running searches per type.
// For total stored counts per type at our node see Indexed class.

uint32 Kademlia::getKademliaFiles() const
{
    if (!m_running || !m_prefs)
        return 0;
    return m_prefs->kademliaFiles();
}

uint32 Kademlia::getTotalStoreKey() const
{
    return m_prefs ? m_prefs->totalStoreKey() : 0;
}

uint32 Kademlia::getTotalStoreSrc() const
{
    return m_prefs ? m_prefs->totalStoreSrc() : 0;
}

uint32 Kademlia::getTotalStoreNotes() const
{
    return m_prefs ? m_prefs->totalStoreNotes() : 0;
}

uint32 Kademlia::getTotalFile() const
{
    return m_prefs ? m_prefs->totalFile() : 0;
}

bool Kademlia::getPublish() const
{
    return m_prefs && m_prefs->publish();
}

uint32 Kademlia::getIPAddress() const
{
    return m_prefs ? m_prefs->ipAddress() : 0;
}

void Kademlia::bootstrap(uint32 ip, uint16 port)
{
    // Skip if already connected, and rate-limit to one manual bootstrap per 10 s
    // so repeated GUI/DNS triggers can't hammer a node. MFC Kademlia.cpp:393-407.
    const time_t now = time(nullptr);
    if (isConnected() || now < m_bootstrap + SEC(10))
        return;
    m_bootstrap = now;
    m_bootstrapping = true;
    if (m_udpListener)
        m_udpListener->bootstrap(ip, port);
}

void Kademlia::bootstrap(const QString& host, uint16 port)
{
    const time_t now = time(nullptr);
    if (isConnected() || now < m_bootstrap + SEC(10))
        return;
    m_bootstrap = now;
    m_bootstrapping = true;
    if (m_udpListener)
        m_udpListener->bootstrap(host, port);
}

void Kademlia::processPacket(const uint8* data, uint32 len, uint32 ip, uint16 port,
                              bool validReceiverKey, const KadUDPKey& senderKey)
{
    if (m_udpListener)
        m_udpListener->processPacket(data, len, ip, port, validReceiverKey, senderKey);
}

void Kademlia::addEvent(RoutingZone* zone)
{
    if (zone)
        m_zoneEvents.insert(zone);
}

void Kademlia::removeEvent(RoutingZone* zone)
{
    if (zone)
        m_zoneEvents.erase(zone);
}

void Kademlia::storeClosestDistance(const UInt128& distance)
{
    // Store a per-probe user-count *estimate*, not the raw distance: an evenly
    // distributed keyspace means the closest node's distance implies how full the
    // space is (users ≈ (2^32 / distance) / 2). Dedup and cap at 100. The old code
    // stored the raw distance chunk, which calculateKadUsersNew can't average into
    // a count. MFC StatsAddClosestDistance, Kademlia.cpp:533-542.
    const uint32 chunk = distance.get32BitChunk(0);
    if (chunk > 0) {
        const uint32 estimate = (0xFFFFFFFFu / chunk) / 2;
        if (std::find(m_statsEstUsersProbes.begin(), m_statsEstUsersProbes.end(), estimate)
            == m_statsEstUsersProbes.end())
            m_statsEstUsersProbes.push_front(estimate);
    }
    while (m_statsEstUsersProbes.size() > 100)
        m_statsEstUsersProbes.pop_back();
}

bool Kademlia::isRunningInLANMode() const
{
    // MFC: CKademlia::IsRunningInLANMode() — cached check every 10 seconds.
    // If FilterLANIPs is on, LAN mode is never active (LAN contacts are rejected).
    if (thePrefs.filterLANIPs() || !m_running || !m_routingZone)
        return false;

    time_t now = time(nullptr);
    if (now >= m_lanModeCheck + 10) {
        // const_cast: MFC uses mutable statics; we cache in mutable-equivalent members.
        auto* self = const_cast<Kademlia*>(this);
        self->m_lanModeCheck = now;
        uint32 count = m_routingZone->getNumContacts();
        // Limit to 256 nodes — larger networks are not small home LANs
        if (count == 0 || count > 256) {
            self->m_lanMode = false;
        } else {
            if (m_routingZone->hasOnlyLANNodes()) {
                if (!m_lanMode) {
                    self->m_lanMode = true;
                    logKad(QStringLiteral("Kad: Activating LAN Mode"));
                }
            } else if (m_lanMode) {
                self->m_lanMode = false;
                logKad(QStringLiteral("Kad: Deactivating LAN Mode"));
            }
        }
    }
    return m_lanMode;
}

bool Kademlia::shouldSkipFirewallChecks()
{
    return thePrefs.skipFirewalledChecksInLanMode()
        && instance() && instance()->isRunningInLANMode();
}

bool Kademlia::findNodeIDByIP(KadClientSearcher& requester, uint32 ip, uint16 tcpPort, uint16 udpPort)
{
    if (!m_udpListener)
        return false;

    // Check routing table first for an immediate result.
    // Matches MFC Kademlia.cpp: GetRoutingZone()->GetContact(ntohl(dwIP), nTCPPort, true).
    // In the Qt port, IPs are already in host byte order throughout.
    if (m_routingZone) {
        if (auto* contact = m_routingZone->getContact(ip, tcpPort, true)) {
            uint8 nodeIDBytes[16];
            contact->getClientID().toByteArray(nodeIDBytes);
            requester.kadSearchNodeIDByIPResult(KadClientSearchResult::Succeeded, nodeIDBytes);
            return true;
        }
    }

    return m_udpListener->findNodeIDByIP(&requester, ip, tcpPort, udpPort);
}

bool Kademlia::findIPByNodeID(KadClientSearcher& requester, const uint8* nodeID)
{
    if (!m_running)
        return false;

    UInt128 target;
    target.setValueBE(nodeID);
    return SearchManager::findNodeSpecial(target, &requester);
}

void Kademlia::cancelClientSearch(const KadClientSearcher& requester)
{
    if (m_udpListener)
        m_udpListener->expireClientSearch(&requester);
    SearchManager::cancelNodeSpecial(&requester);
}

void Kademlia::setIPFilter(eMule::IPFilter* filter)
{
    s_ipFilter = filter;
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

void Kademlia::process()
{
    if (!m_running)
        return;

    time_t now = time(nullptr);

    // 1. Bootstrap — while not yet connected, probe the shipped bootstrap list one
    //    contact at a time: one per 15 s, or every 2 s while the routing table is
    //    still empty. Log a failure once the list is exhausted without connecting.
    //    The port previously paced this at one per second and stopped silently.
    //    MFC Kademlia.cpp:287-301.
    if (!isConnected()
        && (now >= m_bootstrap + SEC(15)
            || ((m_routingZone ? m_routingZone->getNumContacts() : 0) == 0
                && now >= m_bootstrap + SEC(2))))
    {
        if (!s_bootstrapList.empty()) {
            Contact* bc = s_bootstrapList.front();
            s_bootstrapList.pop_front();
            m_bootstrap = now;
            m_bootstrapping = true;
            if (m_udpListener) {
                m_udpListener->bootstrap(bc->address().toUint32(), bc->getUDPPort(),
                                         bc->getVersion());
            }
            delete bc;
        } else if (m_bootstrapping) {
            m_bootstrapping = false;
            logKad(QStringLiteral("Kad: bootstrap failed — no more bootstrap contacts to try"));
        }
    }

    // 2. Status update (every 60 seconds)
    if (now >= m_statusUpdate) {
        m_statusUpdate = now + SEC(60);
        SearchManager::updateStats();

        if (m_prefs && m_routingZone) {
            // Take the MAX estimate over all leaf zones (estimateCount() returns 0
            // for non-leaf zones now). In LAN mode use the real contact count, since
            // the density estimator is meant for large networks. Calling it on the
            // root — as before — returned 0 once the tree had split. MFC Kademlia.cpp:243-250.
            const bool lan = isRunningInLANMode();
            uint32 users = 0;
            for (auto* zone : m_zoneEvents) {
                const uint32 t = lan ? zone->getNumContacts() : zone->estimateCount();
                if (t > users)
                    users = t;
            }
            if (m_zoneEvents.empty())
                users = lan ? m_routingZone->getNumContacts() : m_routingZone->estimateCount();
            m_prefs->setKademliaUsers(users);
            if (m_indexed)
                m_prefs->setKademliaFiles(m_indexed->getFileKeyCount());
            emit statsUpdated(users, m_prefs->kademliaFiles());
        }
    }

    // 3. Search jumpstart (every second)
    if (now >= m_nextSearchJumpStart) {
        m_nextSearchJumpStart = now + kSearchJumpstart;
        SearchManager::jumpStart();
    }

    // 4. Self-lookup for routing table refresh (every 4 hours, first at +3min)
    if (now >= m_nextSelfLookup && m_prefs) {
        m_nextSelfLookup = now + HR2S(4);
        SearchManager::findNode(m_prefs->kadId(), true);
    }

    // 5. Firewall recheck (hourly, matching MFC Kademlia.cpp:216-217)
    //    Also triggers UDPFirewallTester::connected() which starts the
    //    NodeFwCheckUDP search if needed.  connected() has its own guard
    //    (!s_nodeSearchStarted && isFWCheckUDPRunning()).
    if (now >= m_nextFirewallCheck) {
        // Back off here rather than relying on recheckFirewalled() to advance
        // the timer: it only does so on the success path, so a rejected check
        // (LAN mode, or one already running) would re-fire on every tick.
        m_nextFirewallCheck = now + HR2S(1);
        (void)recheckFirewalled();
        UDPFirewallTester::connected();
    }

    // 6. Find buddy — set the one-shot flag on the timer; the actual search
    //    only fires below if we are firewalled and have no buddy.
    //    Matches MFC Kademlia.cpp:227-229 + ClientList.cpp:592-610.
    if (now >= m_nextFindBuddy && m_prefs) {
        m_prefs->setFindBuddy(true);
        m_nextFindBuddy = now + MIN2S(20);
    }

    // 6b. Consume the flag and start a buddy search if we actually need one:
    //     only when both TCP and UDP firewalled, no buddy, and Kad connected.
    if (m_prefs && isConnected()
        && isFirewalled() && UDPFirewallTester::isFirewalledUDP(true))
    {
        if (theApp.clientList && theApp.clientList->buddyStatus() == BuddyStatus::None
            && m_prefs->findBuddy() && !thePrefs.cryptLayerRequired())
        {
            // Buddy callbacks don't support obfuscation, so a buddy search is
            // futile when RequireCrypt is on. Evaluated after findBuddy() so the
            // one-shot flag is still consumed each cycle. MFC ClientList.cpp:599.
            // Target = ~kadID (bitwise NOT).  MFC: CUInt128(true).Xor(GetKadID())
            UInt128 target(UInt128(true));
            target.xorWith(m_prefs->kadId());
            auto* search = SearchManager::prepareLookup(SearchType::FindBuddy,
                                                         true, target);
            if (search) {
                SearchManager::startSearch(search);
                logKad(QStringLiteral("Kad: Initiated buddy search"));
            } else {
                // Search ID already in use — re-set the flag for next cycle
                m_prefs->setFindBuddy(true);
            }
        }
    }

    // 7. Consolidate routing table
    if (now >= m_consolidate && m_routingZone) {
        m_consolidate = now + MIN2S(45);
        m_routingZone->consolidate();
    }

    // 7b. Zone maintenance — iterate leaf zones (matches MFC Process() zone loop).
    //     BigTimer: global gate kBigTimerGlobal (10s), per-zone kBigTimerPerZone (1hr).
    //     SmallTimer: per-zone kSmallTimerInterval (1min).
    //     Extra OR: fire early when 15+ min since last contact (disconnect at 20 min).
    const time_t lastContact = m_prefs ? m_prefs->lastContact() : 0;
    for (auto* zone : m_zoneEvents) {
        if (now >= m_bigTimer
            && (now >= zone->nextBigTimer()
                || (lastContact && now >= lastContact + KADEMLIADISCONNECTDELAY - MIN2S(5)))
            && zone->onBigTimer())
        {
            zone->setNextBigTimer(now + kBigTimerPerZone);
            m_bigTimer = now + kBigTimerGlobal;
        }
        if (now >= zone->nextSmallTimer()) {
            zone->onSmallTimer();
            zone->setNextSmallTimer(now + kSmallTimerInterval);
        }
    }

    // 8. External-Kad-port discovery — while the UDP firewall check is running and
    //    we don't yet know our external Kad port, PING a random v6+ contact every
    //    15 s. Its PONG reports the port we appear to send from, which NATed users
    //    need to learn (else wrong firewall status + wrong published port). The
    //    port previously only polled the getter and rescheduled +1h, sending no
    //    ping. MFC Kademlia.cpp:231-241.
    if (now >= m_externPortLookup && m_prefs
        && UDPFirewallTester::isFWCheckUDPRunning()
        && m_prefs->findExternKadPort(false))
    {
        if (m_routingZone && m_udpListener) {
            if (auto* c = m_routingZone->getRandomContact(3, KADEMLIA_VERSION6_49aBETA)) {
                UInt128 targetID = c->getClientID();
                m_udpListener->sendNullPacket(KADEMLIA2_PING, c->address().toUint32(),
                                              c->getUDPPort(), c->getUDPKey(), &targetID);
            }
        }
        m_externPortLookup = now + SEC(15);
    }

    // 9. Connection state — detect not-connected → connected transition.
    //    Don't gate on m_bootstrapping: it goes false in step 1 (bootstrap
    //    list empty, contacts > 0) before any HELLO_RES sets lastContact.
    const bool nowConnected = m_prefs && m_prefs->hasHadContact();
    if (nowConnected != m_wasConnected) {
        m_wasConnected = nowConnected;
        if (nowConnected) {
            m_bootstrapping = false;
            const uint32 numContacts = m_routingZone ? m_routingZone->getNumContacts() : 0;
            logKad(QStringLiteral("Kad: Bootstrap complete — connected to the network (%1 nodes in routing table)")
                       .arg(numContacts));
            // Trigger first firewall check 10s after connect so the
            // NodeFwCheckUDP search isn't the very first search (ID=1).
            m_nextFirewallCheck = now + SEC(10);
            // In LAN mode, start self-lookup sooner (30s vs 3min) since
            // the network is small and populates quickly.
            if (isRunningInLANMode())
                m_nextSelfLookup = now + SEC(30);
            emit connected();
        }
    }

    // 10. Expire timed-out findNodeIDByIP requests so their Timeout callback
    //     actually fires. Only one comparison in the common (empty) case, so no
    //     dedicated timer is needed. MFC Kademlia.cpp:303-304.
    if (m_udpListener)
        m_udpListener->expireClientSearch();
}

uint32 Kademlia::calculateKadUsersNew() const
{
    // Median-of-probes user count: average the closest-node estimates gathered
    // from lookups, trimming the top and bottom 1/6 to shed spikes, then apply the
    // firewalled inflation. Needs ≥10 samples. The port previously just aliased
    // estimateCount() here, dropping this algorithm entirely. MFC Kademlia.cpp:544-613.
    if (m_statsEstUsersProbes.size() < 10)
        return 0;

    std::vector<uint32> sorted(m_statsEstUsersProbes.begin(), m_statsEstUsersProbes.end());
    std::sort(sorted.begin(), sorted.end());

    const size_t cut = sorted.size() / 6;
    if (sorted.size() <= 2 * cut)
        return 0;
    uint64 sum = 0;
    size_t count = 0;
    for (size_t i = cut; i + cut < sorted.size(); ++i) {
        sum += sorted[i];
        ++count;
    }
    if (count == 0)
        return 0;
    const uint64 median = sum / count;

    float fwModify = 1.20f;
    if (m_prefs)
        fwModify = m_prefs->statsFirewalledModifyTotal();

    return static_cast<uint32>(static_cast<double>(median) * fwModify);
}

} // namespace eMule::kad
