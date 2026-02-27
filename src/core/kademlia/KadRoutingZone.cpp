/// @file KadRoutingZone.cpp
/// @brief Kademlia routing table tree implementation.

#include "kademlia/KadRoutingZone.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingBin.h"
#include "kademlia/KadSearchManager.h"
#include "kademlia/KadUDPListener.h"
#include "ipfilter/IPFilter.h"
#include "prefs/Preferences.h"
#include "utils/Log.h"
#include "utils/OtherFunctions.h"
#include "utils/SafeFile.h"
#include "utils/Opcodes.h"

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

#include <QDir>
#include <QFile>

#include <algorithm>
#include <random>

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
    m_nextSmallTimer = now + SEC(10);
    // MFC fires bigTimer immediately (m_tNextBigTimer = time(NULL)) so that
    // contacts loaded from nodes.dat get verified via HELLO_REQ right away.
    m_nextBigTimer = now;

    // Start timer for root zone only
    if (!m_superZone) {
        startTimer();
    }
}

RoutingZone::~RoutingZone()
{
    // Root zone writes contacts to disk
    if (!m_superZone) {
        writeFile();
        stopTimer();
    }

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

    // Validate IP
    if (!isGoodIP(htonl(ip)))
        return false;

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
        // Contact already exists — update if requested
        if (update) {
            // Check if IP/port changed
            if (existing->getIPAddress() == contact->getIPAddress()
                && existing->getUDPPort() == contact->getUDPPort()) {
                // Same IP/port — just update version and key
                if (contact->getVersion() >= existing->getVersion()) {
                    existing->setVersion(contact->getVersion());
                    existing->setUDPKey(contact->getUDPKey());
                }
                if (contact->getReceivedHelloPacket())
                    existing->setReceivedHelloPacket();
                if (contact->isIpVerified() && !existing->isIpVerified()) {
                    existing->setIpVerified(true);
                    ipVerified = true;
                }
                m_bin->setAlive(existing);
                emit contactUpdated(existing);
            } else {
                // IP or port changed — verify UDP key before accepting
                // Accept change if: new key is empty (unverified), or key values match for our public IP
                uint32 publicIP = thePrefs.publicIP();
                bool keyAcceptable = contact->getUDPKey().isEmpty()
                    || existing->getUDPKey().isEmpty()
                    || contact->getUDPKey().getKeyValue(publicIP) == existing->getUDPKey().getKeyValue(publicIP);
                if (keyAcceptable && m_bin->changeContactIPAddress(existing, contact->getIPAddress())) {
                    existing->setUDPPort(contact->getUDPPort());
                    existing->setTCPPort(contact->getTCPPort());
                    existing->setVersion(contact->getVersion());
                    existing->setUDPKey(contact->getUDPKey());
                    if (contact->getReceivedHelloPacket())
                        existing->setReceivedHelloPacket();
                    existing->setIpVerified(contact->isIpVerified());
                    ipVerified = contact->isIpVerified();
                    m_bin->setAlive(existing);
                    emit contactUpdated(existing);
                }
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
                                     const KadUDPKey& udpKey, bool ipVerified)
{
    return add(id, ip, udpPort, tcpPort, version, udpKey, ipVerified,
               true /*update*/, true /*fromHello*/, false /*fromNodesDat*/);
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

bool RoutingZone::isAcceptableContact(const Contact* contact)
{
    // Accept if type <= 3 and version >= KADEMLIA_VERSION2_47a
    return contact != nullptr
           && contact->getType() <= 3
           && contact->getVersion() >= KADEMLIA_VERSION2_47a;
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

    // If both children are leaves, check if we should merge
    if (m_subZones[0]->isLeaf() && m_subZones[1]->isLeaf()) {
        uint32 total = m_subZones[0]->m_bin->getSize() + m_subZones[1]->m_bin->getSize();
        if (total < kK / 2) {
            // Merge: create new bin, move all contacts
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

            for (auto* c : entries)
                m_bin->addContact(c);
            for (auto* c : entries1)
                m_bin->addContact(c);
        }
    } else {
        m_subZones[0]->consolidate();
        m_subZones[1]->consolidate();
    }
}

void RoutingZone::onBigTimer()
{
    // MFC: OnBigTimer() — randomLookup for zones that are close to our ID,
    // at low depth, or mostly empty.  This keeps the routing table populated
    // and ensures contacts loaded from nodes.dat get verified via HELLO_REQ.
    if (isLeaf() && (m_zoneIndex < kKK || m_level < kKBase
                     || m_bin->getRemaining() >= static_cast<uint32>(kK * 0.8)))
    {
        randomLookup();
    } else if (!isLeaf()) {
        m_subZones[0]->onBigTimer();
        m_subZones[1]->onBigTimer();
    }
}

void RoutingZone::onSmallTimer()
{
    if (!isLeaf()) {
        m_subZones[0]->onSmallTimer();
        m_subZones[1]->onSmallTimer();
        return;
    }

    // Leaf zone: check for expired contacts
    Contact* oldest = m_bin->getOldest();
    if (oldest) {
        if (oldest->getType() == 4) {
            // Expired contact — remove
            if (!oldest->inUse()) {
                m_bin->removeContact(oldest);
                emit contactRemoved(oldest);
                delete oldest;
            }
        } else if (oldest->getExpireTime() > 0 && oldest->getExpireTime() <= time(nullptr)) {
            // Contact needs a type check — send HELLO to verify alive.
            // Pass the contact's KadID to enable NodeID-based encryption.
            oldest->checkingType();
            m_bin->pushToBottom(oldest);
            if (auto* udpListener = Kademlia::getInstanceUDPListener()) {
                const UInt128 contactID = oldest->getClientID();
                udpListener->sendMyDetails(KADEMLIA2_HELLO_REQ,
                    oldest->getIPAddress(), oldest->getUDPPort(),
                    oldest->getVersion(), oldest->getUDPKey(),
                    &contactID, true);
            }
        }
    }
}

uint32 RoutingZone::estimateCount() const
{
    if (!isLeaf())
        return m_subZones[0]->estimateCount() + m_subZones[1]->estimateCount();

    // For zones close to us (level < KBASE), use simple formula
    if (m_level < kKBase) {
        return static_cast<uint32>(kK) * (1u << m_level);
    }

    // For deeper zones, compute from contact density
    uint32 contactCount = m_bin->getSize();
    if (contactCount == 0)
        return 0;

    // Estimate: contactCount * 2^level, adjusted for firewalled node ratio
    uint32 estimate = contactCount * (1u << m_level);
    if (UDPFirewallTester::isFirewalledUDP(true)) {
        // We're behind a firewall — we only see non-firewalled peers.
        // Inflate estimate by the ratio of firewalled nodes.
        if (auto* prefs = Kademlia::getInstancePrefs()) {
            float fwRatio = prefs->statsGetFirewalledRatio(true);
            if (fwRatio > 0.0f && fwRatio < 1.0f)
                estimate = static_cast<uint32>(static_cast<float>(estimate) / (1.0f - fwRatio));
        }
    }
    return estimate;
}

bool RoutingZone::verifyContact(const UInt128& id, uint32 ip)
{
    Contact* contact = getContact(id);
    if (contact && contact->getIPAddress() == ip) {
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
    if (filename.isEmpty())
        return;

    SafeFile sf;
    if (!sf.open(filename, QIODevice::ReadOnly))
        return;

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
            // v2 format (written by this Qt port)
            version = 2;
            numContacts = sf.readUInt32();
        } else if (numContacts == kNodesFileVersion3Tag) {
            // v3 format (written by this Qt port)
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
            }

            // Validate
            if (!isGoodIP(htonl(ip)))
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

        logKad(QStringLiteral("Kad: Loaded nodes.dat — %1 contacts")
                   .arg(getNumContacts()));

    } catch (const FileException& e) {
        logKad(QStringLiteral("Failed to read Kad nodes file: %1").arg(e.what()));
    }
}

void RoutingZone::writeFile()
{
    if (s_nodesFilename.isEmpty())
        return;

    ContactArray contacts;
    getBootstrapContacts(contacts, kMaxBootstrapContacts);

    if (contacts.empty())
        return;

    try {
        // Ensure parent directory exists
        QDir().mkpath(QFileInfo(s_nodesFilename).absolutePath());

        SafeFile sf;
        if (!sf.open(s_nodesFilename, QIODevice::WriteOnly | QIODevice::Truncate))
            return;

        // Write v2 header
        sf.writeUInt32(kNodesFileVersionTag);
        sf.writeUInt32(static_cast<uint32>(contacts.size()));

        for (auto* contact : contacts) {
            // MFC uses WriteUInt128 → GetData() (raw host-order bytes).
            sf.writeHash16(contact->getClientID().getData());

            sf.writeUInt32(contact->getIPAddress());
            sf.writeUInt16(contact->getUDPPort());
            sf.writeUInt16(contact->getTCPPort());
            sf.writeUInt8(contact->getVersion());

            contact->getUDPKey().storeToFile(sf);
            sf.writeUInt8(contact->isIpVerified() ? 1 : 0);
        }

    } catch (const FileException& e) {
        logKad(QStringLiteral("Failed to write Kad nodes file: %1").arg(e.what()));
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

    // Do NOT stop the timer here. Only the root zone has a timer, and it must
    // keep running after splitting (onTimerTick recurses into subzones).
    // stopTimer() is only called in the root zone destructor.

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
    // Generate random ID within this zone's range
    UInt128 randomTarget;
    randomTarget.setValueRandom();
    // Keep bits [0..level) matching m_zoneIndex
    for (uint32 i = 0; i < m_level; ++i)
        randomTarget.setBitNumber(i, m_zoneIndex.getBitNumber(i));
    SearchManager::findNode(randomTarget, false);
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

void RoutingZone::startTimer()
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setInterval(1000); // 1 second
        connect(m_timer, &QTimer::timeout, this, &RoutingZone::onTimerTick);
        m_timer->start();
    }
}

void RoutingZone::stopTimer()
{
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
        m_timer = nullptr;
    }
}

void RoutingZone::onTimerTick()
{
    time_t now = time(nullptr);

    if (now >= m_nextSmallTimer) {
        onSmallTimer();
        m_nextSmallTimer = now + SEC(1);
    }

    if (now >= m_nextBigTimer) {
        logKad(QStringLiteral("Kad: [diag] onBigTimer firing, contacts=%1").arg(getNumContacts()));
        onBigTimer();
        m_nextBigTimer = now + SEC(180);  // 3-minute interval (matches MFC)
    }
}

void RoutingZone::readBootstrapNodesDat(SafeFile& sf)
{
    // Bootstrap nodes.dat files (v3 edition 1) contain 500-1000+ contacts in v1
    // format (25 bytes each). In the original eMule these are not added to the
    // routing table but kept in a bootstrap list for initial Kad connection.
    // For simplicity we add them directly to the routing table here.
    // TODO: implement dedicated bootstrap list for proper bootstrap-only handling

    uint32 numContacts = sf.readUInt32();
    if (numContacts == 0)
        return;

    const uint64 remaining = static_cast<uint64>(sf.length() - sf.position());
    if (static_cast<uint64>(numContacts) * 25 > remaining)
        return;

    for (uint32 i = 0; i < numContacts; ++i) {
        uint8 idBytes[16];
        sf.readHash16(idBytes);
        UInt128 id(idBytes);

        uint32 ip = sf.readUInt32();
        uint16 udpPort = sf.readUInt16();
        uint16 tcpPort = sf.readUInt16();
        uint8 contactVersion = sf.readUInt8();

        if (!isGoodIP(htonl(ip)))
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

        auto* contact = new Contact(id, ip, udpPort, tcpPort, contactVersion,
                                    KadUDPKey(), false, s_localKadId);

        bool verifiedOut = false;
        if (!add(contact, false, verifiedOut)) {
            delete contact;
        } else {
            emit contactAdded(contact);
        }
    }
}

} // namespace eMule::kad
