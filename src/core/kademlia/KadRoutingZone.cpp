#include "pch.h"
/// @file KadRoutingZone.cpp
/// @brief Kademlia routing table tree implementation.

#include "kademlia/KadRoutingZone.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingBin.h"
#include "app/AppContext.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUDPListener.h"
#include "ipfilter/IPFilter.h"
#include "net/Address.h"
#include "prefs/Preferences.h"
#include "utils/SafeFile.h"


#include <QDir>
#include <QFile>

#include <algorithm>
#include <cmath>


namespace eMule::kad {

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

UInt128 RoutingZone::s_localKadId;
QString RoutingZone::s_nodesFilename;

// ---------------------------------------------------------------------------
// File format constants
// ---------------------------------------------------------------------------

namespace {
constexpr uint32 kNodesFileVersionTag = 0x00000002;
constexpr uint32 kNodesFileVersion3Tag = 0x00000003;
constexpr int kMaxBootstrapContacts = 200;
} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

RoutingZone::RoutingZone(const UInt128& localKadId, const QString& nodesFilePath,
                         QObject* parent)
    : QObject(parent)
{
    s_localKadId = localKadId;
    s_nodesFilename = nodesFilePath;

    init(nullptr, 0, UInt128(uint32{0}));
    readFile();
}

RoutingZone::RoutingZone(RoutingZone* superZone, uint32 level, const UInt128& zoneIndex)
    : QObject(nullptr)
{
    init(superZone, level, zoneIndex);
}

void RoutingZone::init(RoutingZone* superZone, uint32 level, const UInt128& zoneIndex)
{
    m_superZone = superZone;
    m_level = level;
    m_zoneIndex = zoneIndex;
    m_subZones[0] = nullptr;
    m_subZones[1] = nullptr;
    m_bin = new RoutingBin();

    time_t now = time(nullptr);
    // MFC staggers small timers via zone index: time(NULL) + SEC(m_uZoneIndex.Get32BitChunk(3)) // can add  % 10 to have max delay
    m_nextSmallTimer = now + static_cast<time_t>(m_zoneIndex.get32BitChunk(3));
    // MFC: m_tNextBigTimer = time(NULL) + SEC(10) in StartTimer()
    m_nextBigTimer = now + SEC(10);

    // Register this leaf zone with Kademlia for timer processing.
    // Matches MFC: CKademlia::AddEvent(this) in StartTimer().
    if (auto* kad = Kademlia::instance())
        kad->addEvent(this);
}

RoutingZone::~RoutingZone()
{
    // Root zone writes contacts to disk
    if (!m_superZone)
        writeFile();

    // Deregister from Kademlia's zone event list.
    if (auto* kad = Kademlia::instance())
        kad->removeEvent(this);

    if (isLeaf()) {
        delete m_bin;
    } else {
        delete m_subZones[0];
        delete m_subZones[1];
    }
}

// ---------------------------------------------------------------------------
// Public methods — Contact management
// ---------------------------------------------------------------------------

bool RoutingZone::add(const UInt128& id, uint32 ip, uint16 udpPort, uint16 tcpPort,
                      uint8 version, const KadUDPKey& udpKey, bool ipVerified,
                      bool update, bool fromHello, bool fromNodesDat)
{
    // Reject ourselves
    if (id == s_localKadId)
        return false;

    // Reject Kad1 (version 1) contacts
    if (version <= KADEMLIA_VERSION1_46c)
        return false;

    // Validate IP (allow LAN IPs when filterLANIPs is disabled)
    if (!Address::fromHostOrder(ip).isRoutable(!thePrefs.filterLANIPs())) {
        logKad(QStringLiteral("Kad: Rejected contact %1 — isRoutable failed (filterLAN=%2)")
                   .arg(Address::fromHostOrder(ip).toString()).arg(thePrefs.filterLANIPs()));
        return false;
    }

    // Reject port 0
    if (udpPort == 0)
        return false;

    // IPFilter check
    if (auto* ipFilter = Kademlia::getIPFilter()) {
        if (ipFilter->isFiltered(htonl(ip), thePrefs.ipFilterLevel()))
            return false;
    }

    // Reject DNS port for older clients
    if (udpPort == 53 && version <= KADEMLIA_VERSION5_48a)
        return false;

    return addUnfiltered(id, ip, udpPort, tcpPort, version, udpKey, ipVerified,
                         update, fromHello, fromNodesDat);
}

bool RoutingZone::addUnfiltered(const UInt128& id, uint32 ip, uint16 udpPort,
                                uint16 tcpPort, uint8 version,
                                const KadUDPKey& udpKey, bool ipVerified,
                                bool update, bool fromHello, bool /*fromNodesDat*/)
{
    // Reject Kad1 (version 1) contacts
    if (version <= KADEMLIA_VERSION1_46c)
        return false;

    // Don't add ourselves
    if (id == s_localKadId)
        return false;

    auto* contact = new Contact(id, ip, udpPort, tcpPort, version, udpKey,
                                ipVerified, s_localKadId);

    if (fromHello)
        contact->setReceivedHelloPacket();

    bool verifiedOut = ipVerified;
    if (!add(contact, update, verifiedOut)) {
        delete contact;
        return false;
    }
    return true;
}

bool RoutingZone::add(Contact* contact, bool update, bool& ipVerified)
{
    // Reject ourselves
    if (contact->getClientID() == s_localKadId) {
        return false;
    }

    // Non-leaf: recurse into the appropriate subtree
    if (!isLeaf()) {
        // Determine which subtree based on the bit at this level
        uint32 bit = contact->getDistance().getBitNumber(m_level);
        return m_subZones[bit]->add(contact, update, ipVerified);
    }

    // Leaf zone: try to add to bin
    Contact* existing = m_bin->getContact(contact->getClientID());

    if (existing) {
        // Contact already exists — update if requested.
        // Structure mirrors MFC RoutingZone.cpp:497-560.
        if (update) {
            const uint32 publicIP = theApp.publicIP();
            const uint32 existingKeyVal = existing->getUDPKey().getKeyValue(publicIP);
            const uint32 newKeyVal = contact->getUDPKey().getKeyValue(publicIP);

            if (existingKeyVal != 0 && existingKeyVal != newKeyVal) {
                // If the stored contact carries a UDP sender key (true for all
                // >= 0.49a clients, unless our own IP changed recently) then any
                // packet wanting to update it must present the same key. This
                // gates *every* update, not just ones that move the IP — a
                // same-address update can still rewrite version and key, which
                // is enough to hijack the entry.
                logKad(QStringLiteral("Kad: %1 tried to update contact %2 but failed to provide proper sender key (sent empty: %3) — denying")
                           .arg(contact->address().toString(),
                                existing->address().toString(),
                                newKeyVal == 0 ? u"yes" : u"no"));
            } else if (existing->getVersion() >= KADEMLIA_VERSION1_46c
                       && existing->getVersion() < KADEMLIA_VERSION6_49aBETA
                       && existing->getReceivedHelloPacket()) {
                // Legacy Kad2 contacts (pre-0.49a) have no key to authenticate
                // with, so once we have heard a HELLO from one it may only
                // refresh its liveness timer — never change its values.
                // Otherwise an attacker could rewrite it at will.
                if (existing->address() == contact->address()
                    && existing->getTCPPort() == contact->getTCPPort()
                    && existing->getUDPPort() == contact->getUDPPort()
                    && existing->getVersion() == contact->getVersion()) {
                    ipVerified = existing->isIpVerified();
                    m_bin->setAlive(existing);
                    emit contactUpdated(existing);
                } else {
                    logKad(QStringLiteral("Kad: rejected value update for legacy kad2 contact (%1 -> %2, %3 -> %4)")
                               .arg(existing->address().toString(),
                                    contact->address().toString())
                               .arg(existing->getVersion()).arg(contact->getVersion()));
                }
            } else if (m_bin->changeContactIPAddress(existing, contact->address().toUint32())
                       && contact->getVersion() >= existing->getVersion()) {
                // Everything else (Kad2 >= 0.49a with the key checked or unset,
                // and first-HELLO updates) may do a full update. The version
                // guard stops an older response from downgrading a newer entry.
                existing->setUDPPort(contact->getUDPPort());
                existing->setTCPPort(contact->getTCPPort());
                existing->setVersion(contact->getVersion());
                existing->setUDPKey(contact->getUDPKey());
                // Only ever *set* the verified flag. Clearing it is the job of
                // changeContactIPAddress() on a genuine IP change; doing it here
                // would let any KADEMLIA2_RES — which always supplies
                // ipVerified=false — strip an existing contact's verification.
                if (!existing->isIpVerified())
                    existing->setIpVerified(contact->isIpVerified());
                ipVerified = existing->isIpVerified();
                if (contact->getReceivedHelloPacket())
                    existing->setReceivedHelloPacket();
                m_bin->setAlive(existing);
                emit contactUpdated(existing);
            }
        }
        return false; // Contact was not newly added (caller should delete)
    }

    // New contact — try to add to bin
    if (m_bin->addContact(contact)) {
        emit contactAdded(contact);
        return true;
    }

    // Bin full — try to split
    if (canSplit()) {
        split();
        // After split, this zone is no longer a leaf — recurse
        uint32 bit = contact->getDistance().getBitNumber(m_level);
        return m_subZones[bit]->add(contact, update, ipVerified);
    }

    // Cannot split, try to replace worst contact
    // Check if there's an expired contact we can replace
    Contact* oldest = m_bin->getOldest();
    if (oldest && oldest->getType() == 4 && !oldest->inUse()) {
        m_bin->removeContact(oldest);
        emit contactRemoved(oldest);
        delete oldest;
        if (m_bin->addContact(contact)) {
            emit contactAdded(contact);
            return true;
        }
    }

    return false;
}

bool RoutingZone::addOrUpdateContact(const UInt128& id, uint32 ip, uint16 udpPort,
                                     uint16 tcpPort, uint8 version,
                                     const KadUDPKey& udpKey, bool ipVerified,
                                     bool update, bool fromHello)
{
    return add(id, ip, udpPort, tcpPort, version, udpKey, ipVerified,
               update, fromHello, false /*fromNodesDat*/);
}

// ---------------------------------------------------------------------------
// Public methods — Queries
// ---------------------------------------------------------------------------

Contact* RoutingZone::getContact(const UInt128& id) const
{
    if (isLeaf())
        return m_bin->getContact(id);

    Contact* result = m_subZones[0]->getContact(id);
    if (result)
        return result;
    return m_subZones[1]->getContact(id);
}

Contact* RoutingZone::getContact(uint32 ip, uint16 port, bool tcpPort) const
{
    if (isLeaf())
        return m_bin->getContact(ip, port, tcpPort);

    Contact* result = m_subZones[0]->getContact(ip, port, tcpPort);
    if (result)
        return result;
    return m_subZones[1]->getContact(ip, port, tcpPort);
}

Contact* RoutingZone::getRandomContact(uint32 maxType, uint32 minVersion) const
{
    if (isLeaf())
        return m_bin->getRandomContact(maxType, minVersion);

    // Try random subtree first, then the other
    auto& rng = randomEngine();
    int first = std::uniform_int_distribution<int>(0, 1)(rng);
    Contact* result = m_subZones[first]->getRandomContact(maxType, minVersion);
    if (result)
        return result;
    return m_subZones[1 - first]->getRandomContact(maxType, minVersion);
}

uint32 RoutingZone::getNumContacts() const
{
    if (isLeaf())
        return m_bin->getSize();
    return m_subZones[0]->getNumContacts() + m_subZones[1]->getNumContacts();
}

void RoutingZone::getNumContacts(uint32& inOutContacts, uint32& inOutFilteredContacts,
                                 uint8 minVersion) const
{
    if (isLeaf()) {
        m_bin->getNumContacts(inOutContacts, inOutFilteredContacts, minVersion);
    } else {
        m_subZones[0]->getNumContacts(inOutContacts, inOutFilteredContacts, minVersion);
        m_subZones[1]->getNumContacts(inOutContacts, inOutFilteredContacts, minVersion);
    }
}

bool RoutingZone::isAcceptableContact(const Contact* contact) const
{
    // MFC RoutingZone.cpp:928-948. Used to vet contacts arriving in a
    // KADEMLIA2_RES routing answer before they are handed to a live search.
    if (contact == nullptr)
        return false;

    // No Kad1 contacts.
    if (contact->getVersion() < KADEMLIA_VERSION2_47a)
        return false;

    if (const Contact* duplicate = getContact(contact->getClientID())) {
        // We already know this KadID. If the known contact proved its IP and the
        // new one claims a different address, this is a hijack attempt — someone
        // trying to displace a verified node in other peers' searches. Note only
        // IP and *UDP* port identify a contact here; the TCP port may legitimately
        // differ. Otherwise a duplicate is simply a node we already have, which
        // is fine.
        return !duplicate->isIpVerified()
               || (duplicate->address() == contact->address()
                   && duplicate->getUDPPort() == contact->getUDPPort());
    }

    // Unknown contact — it must not blow the global per-IP / per-/24 budget,
    // otherwise a single host could saturate our searches with sibyls.
    return RoutingBin::checkGlobalIPLimits(contact->address().toUint32(),
                                           contact->getUDPPort(), false);
}

// ---------------------------------------------------------------------------
// Public methods — Bulk operations
// ---------------------------------------------------------------------------

void RoutingZone::getAllEntries(ContactArray& result, bool emptyFirst) const
{
    if (isLeaf()) {
        m_bin->getEntries(result, emptyFirst);
    } else {
        m_subZones[0]->getAllEntries(result, emptyFirst);
        m_subZones[1]->getAllEntries(result, false);
    }
}

void RoutingZone::getClosestTo(uint32 maxType, const UInt128& target,
                               const UInt128& distance, uint32 maxRequired,
                               ContactMap& result, bool emptyFirst,
                               bool setInUse) const
{
    // Determine which subtree is closer to the target
    if (!isLeaf()) {
        // Check the bit at this level of the distance
        uint32 bit = distance.getBitNumber(m_level);
        // Recurse into the closer subtree first
        m_subZones[bit]->getClosestTo(maxType, target, distance, maxRequired,
                                      result, emptyFirst, setInUse);
        // Then the farther subtree if we need more
        if (result.size() < maxRequired) {
            m_subZones[1 - bit]->getClosestTo(maxType, target, distance, maxRequired,
                                              result, false, setInUse);
        }
    } else {
        m_bin->getClosestTo(maxType, target, maxRequired, result, emptyFirst, setInUse);
    }
}

void RoutingZone::getBootstrapContacts(ContactArray& result, uint32 maxRequired) const
{
    getAllEntries(result, true);

    // If we have more than maxRequired, pick the freshest ones (lowest type)
    if (result.size() > maxRequired) {
        // Sort by type (lower = fresher), then truncate
        std::sort(result.begin(), result.end(), [](const Contact* a, const Contact* b) {
            return a->getType() < b->getType();
        });
        result.resize(maxRequired);
    }
}

// ---------------------------------------------------------------------------
// Public methods — Maintenance
// ---------------------------------------------------------------------------

void RoutingZone::consolidate()
{
    if (isLeaf())
        return;

    // Recurse into any non-leaf child FIRST, so a merge that collapses a child to
    // a leaf can cascade up and be merged at this level in the same pass. The old
    // code recursed only when *neither* child could merge, so a parent merge newly
    // exposed by merging the children was missed until the next 45-min cycle.
    // MFC RoutingZone.cpp:697-699.
    if (!m_subZones[0]->isLeaf())
        m_subZones[0]->consolidate();
    if (!m_subZones[1]->isLeaf())
        m_subZones[1]->consolidate();

    // Re-test this level once the children are (now possibly) leaves. MFC :701.
    if (m_subZones[0]->isLeaf() && m_subZones[1]->isLeaf()
        && (m_subZones[0]->m_bin->getSize() + m_subZones[1]->m_bin->getSize()) < kK / 2)
    {
        auto* newBin = new RoutingBin();
        newBin->m_dontDeleteContacts = true;

        ContactArray entries;
        m_subZones[0]->m_bin->getEntries(entries);
        m_subZones[0]->m_bin->m_dontDeleteContacts = true;

        ContactArray entries1;
        m_subZones[1]->m_bin->getEntries(entries1);
        m_subZones[1]->m_bin->m_dontDeleteContacts = true;

        delete m_subZones[0];
        delete m_subZones[1];
        m_subZones[0] = nullptr;
        m_subZones[1] = nullptr;

        newBin->m_dontDeleteContacts = false;
        m_bin = newBin;

        // Delete any contact that fails re-insertion (subnet/IP limits), matching
        // split(); the old code discarded the bool return, leaking the contact and
        // undercounting the global-IP tracker. MFC RoutingZone.cpp:719-724.
        for (auto* c : entries)
            if (!m_bin->addContact(c))
                delete c;
        for (auto* c : entries1)
            if (!m_bin->addContact(c))
                delete c;

        // Re-register as a leaf zone AND reset its big timer (MFC Consolidate →
        // StartTimer, RoutingZone.cpp:726,741-746); the old code left m_nextBigTimer
        // at its stale pre-split value.
        m_nextBigTimer = time(nullptr) + SEC(10);
        if (auto* kad = Kademlia::instance())
            kad->addEvent(this);
    }
}

bool RoutingZone::onBigTimer()
{
    // MFC: OnBigTimer() — called per leaf zone from Kademlia::process().
    // Non-recursive: the event map only contains leaf zones.
    // Returns true if a randomLookup was triggered (qualifying zone).
    if (isLeaf() && (m_zoneIndex < kKK || m_level < kKBase
                     || m_bin->getRemaining() >= static_cast<uint32>(kK * 0.8)))
    {
        randomLookup();
        return true;
    }
    return false;
}

void RoutingZone::onSmallTimer()
{
    // Non-recursive: called per leaf zone from Kademlia::process().
    if (!isLeaf())
        return;

    const time_t now = time(nullptr);
    auto* sk = Kademlia::getInstanceSafeKad();

    // Pass 1: iterate ALL entries. Remove every due type-4 contact — but only once
    // its ~2-min CheckingType revival grace (m_expires) has elapsed, giving it time
    // to answer a HELLO_RES — and stamp any zero-expire contact so it becomes
    // eligible next tick. The old code inspected only the single oldest contact and
    // deleted type-4 with no grace. MFC RoutingZone.cpp:808-821. (The SafeKad ban is
    // a port-only extension, folded into the same removal path with no grace.)
    ContactArray entries;
    m_bin->getEntries(entries);
    for (auto* c : entries) {
        const bool banned = sk && sk->isBanned(c->address().toUint32());
        if (banned) {
            if (!c->inUse()) {
                m_bin->removeContact(c);
                emit contactRemoved(c);
                delete c;
            }
            continue;
        }
        if (c->getType() == 4 && c->getExpireTime() > 0 && c->getExpireTime() <= now) {
            if (!c->inUse()) {
                m_bin->removeContact(c);
                emit contactRemoved(c);
                delete c;
            }
            continue;
        }
        if (c->getExpireTime() == 0)
            c->setExpireTime(now);
    }

    // Pass 2: HELLO the oldest contact to re-check its type — but first rotate a
    // not-yet-due (or type-4) front contact to the bottom, so a long-expiry contact
    // stops blocking liveness checks of the ones behind it. MFC :822-827.
    Contact* oldest = m_bin->getOldest();
    if (!oldest)
        return;
    if (oldest->getExpireTime() >= now || oldest->getType() == 4) {
        m_bin->pushToBottom(oldest);
        return;
    }

    oldest->checkingType();
    m_bin->pushToBottom(oldest);
    auto* udpListener = Kademlia::getInstanceUDPListener();
    if (!udpListener)
        return;

    // Never request an ACK from a routine liveness HELLO. For v2-v5 (which support
    // no keys) send an EMPTY key and NULL id — the old code sent a stored key + id +
    // requestAck=true to *every* version, which is undecryptable/nonconforming to
    // 0.47a-0.48a peers. MFC :829-853.
    if (oldest->getVersion() >= KADEMLIA_VERSION6_49aBETA) {
        const UInt128 contactID = oldest->getClientID();
        udpListener->sendMyDetails(KADEMLIA2_HELLO_REQ,
            oldest->address().toUint32(), oldest->getUDPPort(),
            oldest->getVersion(), oldest->getUDPKey(), &contactID, false);
        if (oldest->getVersion() >= KADEMLIA_VERSION8_49b) {
            // We won't get a counted HELLO_REQ back from a node we pinged, so record
            // the firewalled-stats sample here instead. MFC :836-846.
            if (auto* prefs = Kademlia::getInstancePrefs()) {
                prefs->statsIncUDPFirewalledNodes(false);
                prefs->statsIncTCPFirewalledNodes(false);
            }
        }
    } else if (oldest->getVersion() >= KADEMLIA_VERSION2_47a) {
        udpListener->sendMyDetails(KADEMLIA2_HELLO_REQ,
            oldest->address().toUint32(), oldest->getUDPPort(),
            oldest->getVersion(), KadUDPKey(), nullptr, false);
    }
}

uint32 RoutingZone::estimateCount() const
{
    // Non-leaf zones contribute nothing; the caller takes the MAX over leaves. The
    // old code recursively SUMMED per-leaf whole-network extrapolations, wildly
    // inflating the displayed count. MFC RoutingZone.cpp:761-796, caller :247-249.
    if (!isLeaf())
        return 0;
    if (m_level < kKBase)
        return static_cast<uint32>((1u << m_level) * kK);

    // Density model: how full is the subtree three levels up?
    const RoutingZone* cur = this;
    for (int up = 0; up < 3 && cur; ++up)
        cur = cur->m_superZone;
    if (!cur)
        return 0;
    const float fModify = static_cast<float>(cur->getNumContacts()) / (kK * 2.0f);

    float fwModify = 1.20f;
    if (auto* prefs = Kademlia::getInstancePrefs())
        fwModify = prefs->statsFirewalledModifyTotal();

    // 2^(level-2) * K * density * firewall-inflation (ldexp avoids the 32-bit
    // overflow a shift would hit for deep zones).
    return static_cast<uint32>(std::ldexp(static_cast<double>(kK) * fModify * fwModify,
                                          static_cast<int>(m_level) - 2));
}

UInt128 RoutingZone::makeRandomLookupTarget(const UInt128& zoneIndex, uint32 level,
                                            const UInt128& localKadId)
{
    // MFC RoutingZone.cpp:857-865.  zoneIndex accumulates the zone's path in its
    // LOW bits (see genSubZone), so it has to be shifted up before the padding
    // constructor can keep it as the leading prefix.  shiftLeft(>127) yields 0,
    // which is exactly what the root zone (level 0) needs.
    UInt128 prefix(zoneIndex);
    prefix.shiftLeft(128 - level);
    // Keep the top `level` bits, randomise the rest.
    UInt128 target(prefix, level);
    // The prefix lives in distance space; XOR with our own ID to turn it into an
    // actual keyspace target.  Without this every zone refresh probes the same
    // wrong region and distant buckets never get maintained.
    target.xorWith(localKadId);
    return target;
}

UInt128 RoutingZone::randomLookupTarget() const
{
    return makeRandomLookupTarget(m_zoneIndex, m_level, s_localKadId);
}

bool RoutingZone::verifyContact(const UInt128& id, uint32 ip)
{
    Contact* contact = getContact(id);
    if (contact && contact->address().toUint32() == ip) {
        contact->setIpVerified(true);
        return true;
    }
    return false;
}

bool RoutingZone::hasOnlyLANNodes() const
{
    if (isLeaf())
        return m_bin->hasOnlyLANNodes();
    return m_subZones[0]->hasOnlyLANNodes() && m_subZones[1]->hasOnlyLANNodes();
}

// ---------------------------------------------------------------------------
// Public methods — File I/O
// ---------------------------------------------------------------------------

void RoutingZone::readFile(const QString& specialNodesdat)
{
    QString filename = specialNodesdat.isEmpty() ? s_nodesFilename : specialNodesdat;
    if (filename.isEmpty()) {
        logKad(QStringLiteral("Kad: No nodes.dat path configured"));
        return;
    }

    SafeFile sf;
    if (!sf.open(filename, QIODevice::ReadOnly)) {
        logKad(QStringLiteral("Kad: Could not open nodes.dat at %1").arg(filename));
        return;
    }

    try {
        uint32 numContacts = sf.readUInt32();
        uint32 version = 0;

        if (numContacts == 0) {
            // Newer eMule clients write 0 as first uint32 to prevent older clients
            // from reading the file (original eMule format).
            if (sf.length() >= 8) {
                version = sf.readUInt32();
                if (version == 3) {
                    uint32 bootstrapEdition = sf.readUInt32();
                    if (bootstrapEdition == 1) {
                        // Bootstrap nodes.dat — contacts used for initial Kad bootstrapping
                        readBootstrapNodesDat(sf);
                        return;
                    }
                }
                if (version >= 1 && version <= 3)
                    numContacts = sf.readUInt32();
            }
        } else if (numContacts == kNodesFileVersionTag) {
            // Backwards compatibility: earlier builds of this port wrote the
            // header without the leading zero sentinel ([2][count] instead of
            // [0][2][count]).  Keep reading it so an existing install doesn't
            // lose its routing table.  The only file this can misparse is a
            // genuine legacy v0 nodes.dat holding exactly 2 contacts, a format
            // that predates 0.48a.
            version = 2;
            numContacts = sf.readUInt32();
        } else if (numContacts == kNodesFileVersion3Tag) {
            // Same, for the port's v3 header (legacy v0 with exactly 3 contacts).
            version = 3;
            numContacts = sf.readUInt32();
        }
        // else: legacy version 0 — numContacts is the actual count

        // Sanity check
        if (numContacts > 5000) {
            logKad(QStringLiteral("Kad nodes file has too many contacts (%1), truncating to 5000")
                   .arg(numContacts));
            numContacts = 5000;
        }

        // Tracks whether the *file* declared any verified contact. Deliberately
        // set from the byte as read, before the add attempt, matching MFC
        // RoutingZone.cpp:224-232 — a file whose verified contacts all get
        // ipfiltered should still suppress the fallback.
        bool haveVerifiedContacts = false;

        for (uint32 i = 0; i < numContacts; ++i) {
            // Read KadID (16 bytes)
            // MFC uses ReadUInt128 → GetDataPtr() (raw host-order bytes).
            UInt128 id;
            sf.readHash16(id.getDataPtr());

            uint32 ip = sf.readUInt32();
            uint16 udpPort = sf.readUInt16();
            uint16 tcpPort = sf.readUInt16();

            uint8 contactVersion = 0;
            if (version >= 1) {
                contactVersion = sf.readUInt8();
            } else {
                // Legacy format: byte is contact type, not version
                uint8 type = sf.readUInt8();
                if (type >= 4)
                    continue; // expired contact
            }

            KadUDPKey udpKey(uint32{0});
            bool ipVerified = false;

            if (version >= 2) {
                udpKey = KadUDPKey(sf);
                ipVerified = sf.readUInt8() != 0;
                if (ipVerified)
                    haveVerifiedContacts = true;
            }

            // Validate (allow LAN IPs when filterLANIPs is disabled)
            if (!Address::fromHostOrder(ip).isRoutable(!thePrefs.filterLANIPs()))
                continue;
            if (udpPort == 0)
                continue;
            // Reject DNS port for old clients
            if (udpPort == 53 && contactVersion <= KADEMLIA_VERSION5_48a)
                continue;
            // Reject Kad1
            if (contactVersion <= KADEMLIA_VERSION1_46c)
                continue;

            // IPFilter check
            if (auto* ipFilter = Kademlia::getIPFilter()) {
                if (ipFilter->isFiltered(htonl(ip), thePrefs.ipFilterLevel()))
                    continue;
            }

            // Don't add ourselves
            if (id == s_localKadId)
                continue;

            auto* contact = new Contact(id, ip, udpPort, tcpPort, contactVersion,
                                        udpKey, ipVerified, s_localKadId);

            bool verifiedOut = ipVerified;
            if (!add(contact, false, verifiedOut)) {
                delete contact;
            } else {
                emit contactAdded(contact);
            }
        }

        uint32 loaded = getNumContacts();
        if (loaded == 0)
            logKad(QStringLiteral("Kad: nodes.dat loaded but 0 contacts added (file had %1 entries, filterLANIPs=%2)")
                       .arg(numContacts).arg(thePrefs.filterLANIPs()));
        else
            logKad(QStringLiteral("Kad: Loaded nodes.dat — %1 contacts (filterLANIPs=%2)")
                       .arg(loaded).arg(thePrefs.filterLANIPs()));

        // getClosestTo() only returns IP-verified contacts, but a nodes.dat
        // written by an older client (or any pre-v2 file) carries no verified
        // flags at all — leaving every contact unusable and Kad unable to
        // bootstrap. Trust them once, for this session, as MFC does
        // (RoutingZone.cpp:252-255). The exposure is small: these are contacts
        // we chose to persist, and they lose the flag again on any IP change.
        if (!haveVerifiedContacts) {
            logKad(QStringLiteral("Kad: no verified contacts in nodes.dat — might be an old file version; "
                                  "setting all contacts verified for this session to speed up bootstrapping"));
            setAllContactsVerified();
        }

    } catch (const FileException& e) {
        logKad(QStringLiteral("Failed to read Kad nodes file: %1").arg(QLatin1StringView(e.what())));
    }
}

void RoutingZone::writeFile()
{
    if (s_nodesFilename.isEmpty())
        return;

    // Don't persist the routing table while a bootstrap probe run is unfinished —
    // it isn't populated yet, so we'd overwrite the shipped/previous nodes.dat
    // with a near-empty one. MFC RoutingZone.cpp:344-348.
    if (!Kademlia::s_bootstrapList.empty())
        return;

    ContactArray contacts;
    getBootstrapContacts(contacts, kMaxBootstrapContacts);

    if (contacts.empty())
        return;

    const QString tmpPath = s_nodesFilename + QStringLiteral(".tmp");
    const QString bakPath = s_nodesFilename + QStringLiteral(".bak");

    try {
        // Ensure parent directory exists
        QDir().mkpath(QFileInfo(s_nodesFilename).absolutePath());

        QFile::remove(tmpPath);

        {
            SafeFile sf;
            if (!sf.open(tmpPath, QIODevice::WriteOnly))
                return;

            // Write v2 header in the official eMule layout: [0][2][count].
            // The leading zero is a sentinel that stops pre-0.48a clients from
            // reading the file (they would take the first uint32 as a contact
            // count).  MFC RoutingZone.cpp:363-367.
            sf.writeUInt32(0);
            sf.writeUInt32(kNodesFileVersionTag);
            sf.writeUInt32(static_cast<uint32>(contacts.size()));

            for (auto* contact : contacts) {
                // MFC uses WriteUInt128 → GetData() (raw host-order bytes).
                sf.writeHash16(contact->getClientID().getData());

                sf.writeUInt32(contact->address().toUint32());
                sf.writeUInt16(contact->getUDPPort());
                sf.writeUInt16(contact->getTCPPort());
                sf.writeUInt8(contact->getVersion());

                contact->getUDPKey().storeToFile(sf);
                sf.writeUInt8(contact->isIpVerified() ? 1 : 0);
            }
        } // file closed before rename

        // Rotate: current → .bak
        QFile::remove(bakPath);
        if (QFile::exists(s_nodesFilename)) {
            if (!QFile::rename(s_nodesFilename, bakPath))
                QFile::remove(s_nodesFilename);
        }

        // Rename temp → final
        if (!QFile::rename(tmpPath, s_nodesFilename)) {
            logKad(QStringLiteral("Failed to rename tmp → nodes.dat"));
            if (QFile::exists(bakPath))
                QFile::rename(bakPath, s_nodesFilename);
        }

    } catch (const FileException& e) {
        logKad(QStringLiteral("Failed to write Kad nodes file: %1").arg(QLatin1StringView(e.what())));
        QFile::remove(tmpPath);
    }
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

bool RoutingZone::isLeaf() const
{
    return m_bin != nullptr;
}

bool RoutingZone::canSplit() const
{
    // Can split if:
    // 1. Level < 127 (max 128 bits)
    // 2. Zone is close enough to local ID (zoneIndex < KK or level < KBASE)
    // 3. Bin is full
    if (m_level >= 127)
        return false;
    if (m_zoneIndex >= kKK && m_level >= kKBase)
        return false;
    if (!isLeaf() || m_bin->getRemaining() > 0)
        return false;
    return true;
}

void RoutingZone::split()
{
    Q_ASSERT(isLeaf());

    // Deregister this zone — it's no longer a leaf.
    // The new sub-zones register themselves in their constructor.
    if (auto* kad = Kademlia::instance())
        kad->removeEvent(this);

    m_subZones[0] = genSubZone(0);
    m_subZones[1] = genSubZone(1);

    // Redistribute contacts from old bin
    ContactArray entries;
    m_bin->getEntries(entries);
    m_bin->m_dontDeleteContacts = true;
    delete m_bin;
    m_bin = nullptr;

    for (auto* contact : entries) {
        uint32 bit = contact->getDistance().getBitNumber(m_level);
        if (!m_subZones[bit]->m_bin->addContact(contact)) {
            // Should not happen — we just created fresh bins
            delete contact;
        }
    }
}

RoutingZone* RoutingZone::genSubZone(int side)
{
    Q_ASSERT(side == 0 || side == 1);

    UInt128 newIndex(m_zoneIndex);
    newIndex.shiftLeft(1);
    if (side == 1)
        newIndex.add(uint32{1});

    return new RoutingZone(this, m_level + 1, newIndex);
}

uint32 RoutingZone::topDepth() const
{
    if (isLeaf())
        return 0;
    return 1 + std::max(m_subZones[0]->topDepth(), m_subZones[1]->topDepth());
}

uint32 RoutingZone::getMaxDepth() const
{
    if (isLeaf())
        return m_level;
    return std::max(m_subZones[0]->getMaxDepth(), m_subZones[1]->getMaxDepth());
}

RoutingBin* RoutingZone::randomBin() const
{
    if (isLeaf())
        return m_bin;

    auto& rng = randomEngine();
    int side = std::uniform_int_distribution<int>(0, 1)(rng);
    return m_subZones[side]->randomBin();
}

void RoutingZone::randomLookup()
{
    SearchManager::findNode(randomLookupTarget(), false);
}

void RoutingZone::setAllContactsVerified()
{
    if (isLeaf()) {
        m_bin->setAllContactsVerified();
    } else {
        m_subZones[0]->setAllContactsVerified();
        m_subZones[1]->setAllContactsVerified();
    }
}

void RoutingZone::readBootstrapNodesDat(SafeFile& sf)
{
    // Bootstrap-edition nodes.dat files (v3 edition 1) ship 500-1000+ contacts in
    // v1 format (25 bytes each). Rather than injecting them straight into the
    // routing table, keep the 50 closest to our own ID in a separate bootstrap
    // list and probe them one at a time via BOOTSTRAP_REQ — the way official
    // eMule does (RoutingZone.cpp:266-340). This avoids hammering the shipped
    // bootstrap nodes and only promotes contacts that actually answer.
    uint32 numContacts = sf.readUInt32();
    if (numContacts == 0)
        return;

    const uint64 remaining = static_cast<uint64>(sf.length() - sf.position());
    if (static_cast<uint64>(numContacts) * 25 > remaining)
        return;

    ContactArray candidates;
    candidates.reserve(numContacts);
    for (uint32 i = 0; i < numContacts; ++i) {
        uint8 idBytes[16];
        sf.readHash16(idBytes);
        UInt128 id(idBytes);

        uint32 ip = sf.readUInt32();
        uint16 udpPort = sf.readUInt16();
        uint16 tcpPort = sf.readUInt16();
        uint8 contactVersion = sf.readUInt8();

        if (!Address::fromHostOrder(ip).isRoutable(!thePrefs.filterLANIPs()))
            continue;
        if (udpPort == 0)
            continue;
        if (udpPort == 53 && contactVersion <= KADEMLIA_VERSION5_48a)
            continue;
        if (contactVersion <= KADEMLIA_VERSION1_46c)
            continue;

        if (auto* ipFilter = Kademlia::getIPFilter()) {
            if (ipFilter->isFiltered(htonl(ip), thePrefs.ipFilterLevel()))
                continue;
        }

        if (id == s_localKadId)
            continue;

        candidates.push_back(new Contact(id, ip, udpPort, tcpPort, contactVersion,
                                         KadUDPKey(), false, s_localKadId));
    }

    // Keep only the closest 50 to our own ID; probe those. MFC RoutingZone.cpp:305-340.
    std::sort(candidates.begin(), candidates.end(),
              [](const Contact* a, const Contact* b) { return a->getDistance() < b->getDistance(); });
    constexpr size_t kMaxBootstrap = 50;
    size_t kept = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (i < kMaxBootstrap) {
            Kademlia::s_bootstrapList.push_back(candidates[i]);
            ++kept;
        } else {
            delete candidates[i];
        }
    }
    logKad(QStringLiteral("Kad: queued %1 bootstrap contacts (of %2) for probing")
               .arg(kept).arg(numContacts));
}

} // namespace eMule::kad
