/// @file Kademlia.cpp
/// @brief Main Kademlia DHT engine implementation.

#include "kademlia/Kademlia.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadIndexed.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUDPListener.h"
#include "ipfilter/IPFilter.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <QDir>

#include <algorithm>
#include <ctime>

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
}

void Kademlia::start()
{
    start(nullptr);
}

void Kademlia::start(KadPrefs* prefs)
{
    if (m_running) {
        logWarning(QStringLiteral("Kad: Already running"));
        return;
    }

    logInfo(QStringLiteral("Kad: Starting Kademlia"));

    if (prefs) {
        m_prefs = prefs;
    } else if (!m_prefs) {
        // Create default prefs with temp directory
        m_prefs = new KadPrefs(QDir::tempPath());
    }

    // Create UDP listener
    m_udpListener = new KademliaUDPListener(this);

    // Create indexed storage
    m_indexed = new Indexed(this);

    // Create routing zone
    QString nodesFile = QDir::tempPath() + QStringLiteral("/nodes.dat");
    m_routingZone = new RoutingZone(m_prefs->kadId(), nodesFile, this);
    m_prefs->setRoutingZone(m_routingZone);

    // Initialize timers
    time_t now = time(nullptr);
    m_nextSearchJumpStart = now;
    m_nextSelfLookup = now + MIN2S(3);
    m_nextFirewallCheck = now + HR2S(1);
    m_nextFindBuddy = now + MIN2S(5);
    m_statusUpdate = now + SEC(60);
    m_bigTimer = now + SEC(10);
    m_consolidate = now + MIN2S(45);
    m_externPortLookup = now;
    m_lanModeCheck = now + SEC(10);
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
    logInfo(QStringLiteral("Kad: Started with ID %1").arg(m_prefs->kadId().toHexString()));
}

void Kademlia::stop()
{
    if (!m_running)
        return;

    logInfo(QStringLiteral("Kad: Stopping Kademlia"));

    m_running = false;
    m_bootstrapping = false;
    if (s_instance == this)
        s_instance = nullptr;

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

    // Note: m_prefs may be externally owned, don't delete if we didn't create it

    UDPFirewallTester::reset();

    // Clear bootstrap list
    for (auto* c : s_bootstrapList)
        delete c;
    s_bootstrapList.clear();

    emit stopped();
    logInfo(QStringLiteral("Kad: Stopped"));
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
    if (!m_running || !m_prefs)
        return;
    m_prefs->setRecheckIP();
    UDPFirewallTester::reCheckFirewallUDP(false);
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
    return m_lanMode;
}

bool Kademlia::findNodeIDByIP(KadClientSearcher& requester, uint32 ip, uint16 tcpPort, uint16 udpPort)
{
    if (!m_udpListener)
        return false;
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

    // 4. Self-lookup for routing table refresh
    if (now >= m_nextSelfLookup && m_prefs) {
        m_nextSelfLookup = now + HR2S(4);
        SearchManager::findNode(m_prefs->kadId(), false);
    }

    // 5. Firewall check
    if (now >= m_nextFirewallCheck) {
        m_nextFirewallCheck = now + HR2S(1);
        if (m_prefs)
            m_prefs->incFirewalled();
        UDPFirewallTester::connected();
    }

    // 6. Find buddy
    if (now >= m_nextFindBuddy && m_prefs) {
        m_nextFindBuddy = now + MIN2S(20);
        if (m_prefs->findBuddy()) {
            auto* search = SearchManager::prepareLookup(SearchType::FindBuddy,
                                                         true, m_prefs->kadId());
            if (search) {
                SearchManager::startSearch(search);
                logDebug(QStringLiteral("Kad: Initiated buddy search"));
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

    // 9. LAN mode check
    if (now >= m_lanModeCheck && m_routingZone) {
        m_lanModeCheck = now + SEC(10);
        bool previousLanMode = m_lanMode;
        m_lanMode = m_routingZone->hasOnlyLANNodes();
        if (m_lanMode != previousLanMode) {
            logInfo(QStringLiteral("Kad: LAN mode %1").arg(m_lanMode ? "enabled" : "disabled"));
        }
    }

    // 10. Connection state
    if (m_prefs && m_prefs->hasHadContact()) {
        if (m_bootstrapping) {
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
