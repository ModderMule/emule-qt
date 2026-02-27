/// @file Kademlia.cpp
/// @brief Main Kademlia DHT engine implementation.

#include "kademlia/Kademlia.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadClientSearcher.h"
#include "kademlia/KadContact.h"
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
#include "utils/Opcodes.h"

#include <QDir>

#include <algorithm>
#include <ctime>
#include <limits>

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
    m_nextFindBuddy = now + MIN2S(5);
    m_statusUpdate = now + SEC(60);
    m_bigTimer = now + SEC(10);
    m_consolidate = now + MIN2S(45);
    m_externPortLookup = now;
    m_bootstrap = now;

    // Start process timer (1-second interval)
    m_processTimer = new QTimer(this);
    m_processTimer->setInterval(1000);
    connect(m_processTimer, &QTimer::timeout, this, &Kademlia::process);
    m_processTimer->start();

    m_running = true;
    m_bootstrapping = true;
    s_instance = this;
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

bool Kademlia::isFirewalled() const
{
    if (!m_running || !m_prefs)
        return true;
    return m_prefs->firewalled();
}

void Kademlia::recheckFirewalled()
{
    if (!m_running || !m_prefs || isRunningInLANMode())
        return;

    // Stop any pending buddy search and force a firewall re-check.
    // Matches MFC Kademlia.cpp:409-426.
    m_prefs->setFindBuddy(false);
    m_prefs->setRecheckIP();
    UDPFirewallTester::reCheckFirewallUDP(false);

    const auto now = static_cast<time_t>(time(nullptr));
    // Delay the next buddy search to at least 5 min so the firewall
    // recheck has time to complete and we don't start a buddy search
    // based on stale firewalled status.
    if (m_nextFindBuddy < now + MIN2S(5))
        m_nextFindBuddy = now + MIN2S(5);
    m_nextFirewallCheck = static_cast<time_t>(time(nullptr));  // Fire on next tick
}

uint32 Kademlia::getKademliaUsers(bool newMethod) const
{
    if (!m_running || !m_prefs)
        return 0;
    if (newMethod)
        return calculateKadUsersNew();
    return m_prefs->kademliaUsers();
}

uint32 Kademlia::getKademliaFiles() const
{
    if (!m_running || !m_prefs)
        return 0;
    return m_prefs->kademliaFiles();
}

uint32 Kademlia::getTotalStoreKey() const
{
    return m_indexed ? m_indexed->m_totalIndexKeyword : 0;
}

uint32 Kademlia::getTotalStoreSrc() const
{
    return m_indexed ? m_indexed->m_totalIndexSource : 0;
}

uint32 Kademlia::getTotalStoreNotes() const
{
    return m_indexed ? m_indexed->m_totalIndexNotes : 0;
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
    if (m_udpListener)
        m_udpListener->bootstrap(ip, port);
}

void Kademlia::bootstrap(const QString& host, uint16 port)
{
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
        m_events[zone->localKadId()] = zone;
}

void Kademlia::removeEvent(RoutingZone* zone)
{
    if (zone)
        m_events.erase(zone->localKadId());
}

void Kademlia::storeClosestDistance(const UInt128& distance)
{
    if (distance != UInt128(uint32{0})) {
        m_statsEstUsersProbes.push_front(distance.get32BitChunk(0));
        // Keep at most 100 probes
        while (m_statsEstUsersProbes.size() > 100)
            m_statsEstUsersProbes.pop_back();
    }
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

    // 1. Bootstrap if needed
    if (m_bootstrapping && now >= m_bootstrap) {
        if (!s_bootstrapList.empty()) {
            Contact* bc = s_bootstrapList.front();
            s_bootstrapList.pop_front();
            if (m_udpListener) {
                m_udpListener->bootstrap(bc->getIPAddress(), bc->getUDPPort(),
                                         bc->getVersion());
            }
            delete bc;
            m_bootstrap = now + SEC(1);
        } else if (m_routingZone && m_routingZone->getNumContacts() > 0) {
            m_bootstrapping = false;
        }
    }

    // 2. Status update (every 60 seconds)
    if (now >= m_statusUpdate) {
        m_statusUpdate = now + SEC(60);
        SearchManager::updateStats();

        if (m_prefs && m_routingZone) {
            uint32 users = m_routingZone->estimateCount();
            m_prefs->setKademliaUsers(users);
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
        SearchManager::findNode(m_prefs->kadId(), false);
    }

    // 5. Firewall check (first at +1hr, then disabled)
    if (now >= m_nextFirewallCheck) {
        m_nextFirewallCheck = std::numeric_limits<time_t>::max();
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
            && m_prefs->findBuddy())
        {
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

    // 8. External port lookup
    if (now >= m_externPortLookup && m_prefs) {
        if (m_prefs->findExternKadPort(false)) {
            m_externPortLookup = now + HR2S(1);
        }
    }

    // 9. Connection state — detect not-connected → connected transition.
    //    Don't gate on m_bootstrapping: it goes false in step 1 (bootstrap
    //    list empty, contacts > 0) before any HELLO_RES sets lastContact.
    const bool nowConnected = m_prefs && m_prefs->hasHadContact();
    if (nowConnected != m_wasConnected) {
        m_wasConnected = nowConnected;
        if (nowConnected) {
            m_bootstrapping = false;
            emit connected();
        }
    }
}

uint32 Kademlia::calculateKadUsersNew() const
{
    if (!m_routingZone)
        return 0;
    return m_routingZone->estimateCount();
}

} // namespace eMule::kad
