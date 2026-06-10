#include "pch.h"
/// @file KadSearch.cpp
/// @brief Kademlia search state machine implementation.

#include "kademlia/KadSearch.h"
#include "kademlia/KadClientSearcher.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadLookupHistory.h"
#include "kademlia/KadMiscUtils.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadUDPListener.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "files/SharedFileList.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"
#include "protocol/Tag.h"
#include "utils/OtherFunctions.h"


namespace eMule::kad {

namespace {
uint32 s_nextSearchID = 1;
} // namespace

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

Search::Search()
    : m_lastResponse(time(nullptr))
    , m_created(time(nullptr))
{
    m_searchID = s_nextSearchID++;
    m_closestDistantFound.setValue(UInt128(true)); // max distance
    m_lookupHistory = std::make_unique<LookupHistory>();
}

Search::~Search()
{
    // Notify NodeSpecial requester if still waiting.
    // Matches MFC Search.cpp:113-117.
    if (m_nodeSpecialSearchRequester) {
        m_nodeSpecialSearchRequester->kadSearchIPByNodeIDResult(
            KadClientSearchResult::NotFound, 0, 0);
        m_nodeSpecialSearchRequester = nullptr;
    }

    // Clean up contacts in use
    for (auto& [id, contact] : m_inUse) {
        if (contact)
            contact->decUse();
    }
    for (auto* contact : m_deleteList)
        delete contact;
}

void Search::setSearchType(SearchType type)
{
    m_type = type;
    if (m_lookupHistory)
        m_lookupHistory->setSearchType(static_cast<uint32>(type));
}

void Search::setTargetID(const UInt128& target)
{
    m_target = target;
}

void Search::setSearchTermData(uint32 size, const uint8* data)
{
    m_searchTermsData = QByteArray(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
}

QString Search::getTypeName(SearchType type)
{
    switch (type) {
    case SearchType::Node:            return QStringLiteral("Node");
    case SearchType::NodeComplete:    return QStringLiteral("NodeComplete");
    case SearchType::File:            return QStringLiteral("File");
    case SearchType::Keyword:         return QStringLiteral("Keyword");
    case SearchType::Notes:           return QStringLiteral("Notes");
    case SearchType::StoreFile:       return QStringLiteral("StoreFile");
    case SearchType::StoreKeyword:    return QStringLiteral("StoreKeyword");
    case SearchType::StoreNotes:      return QStringLiteral("StoreNotes");
    case SearchType::FindBuddy:       return QStringLiteral("FindBuddy");
    case SearchType::FindSource:      return QStringLiteral("FindSource");
    case SearchType::NodeSpecial:     return QStringLiteral("NodeSpecial");
    case SearchType::NodeFwCheckUDP:  return QStringLiteral("NodeFwCheckUDP");
    }
    return QStringLiteral("Unknown");
}

void Search::addFileID(const UInt128& id)
{
    m_fileIDs.push_back(id);
}

void Search::preparePacketForTags(SafeMemFile& packet, KnownFile* file, uint8 targetKadVersion)
{
    if (!file) {
        packet.writeUInt8(0);
        return;
    }

    // Build tag list matching MFC PreparePacketForTags.
    // We write manually (count + tags) instead of writeKadTagList because
    // the AICH tag requires BSOB format which writeKadTag doesn't handle.
    std::vector<Tag> tags;

    // TAG_FILENAME (0x01) — always included (MFC line 1338)
    tags.emplace_back(FT_FILENAME, file->fileName());

    // TAG_FILESIZE (0x02) — single uint64 tag (MFC uses CKadTagUInt with uint64)
    tags.emplace_back(FT_FILESIZE, static_cast<uint64>(file->fileSize()));

    // TAG_SOURCES (0x15) — complete sources count
    tags.emplace_back(QByteArrayLiteral(TAG_SOURCES), static_cast<uint32>(file->completeSourcesCount()));

    // TAG_KADAICHHASHPUB (0x36) — AICH hash as BSOB (MFC: Kad >= v9)
    bool hasAICH = targetKadVersion >= KADEMLIA_VERSION9_50a
                   && file->fileIdentifier().hasAICHHash();

    // TAG_FILETYPE (0x03)
    if (!file->fileType().isEmpty())
        tags.emplace_back(FT_FILETYPE, file->fileType());

    // TAG_FILERATING (0xF7)
    if (file->getFileRating() > 0)
        tags.emplace_back(FT_FILERATING, file->getFileRating());

    // Media metadata tags (MFC: only if verified metadata exists)
    if (file->metaDataVer() > 0) {
        constexpr uint8 metaTags[] = {
            FT_MEDIA_ARTIST, FT_MEDIA_ALBUM, FT_MEDIA_TITLE,
            FT_MEDIA_LENGTH, FT_MEDIA_BITRATE, FT_MEDIA_CODEC
        };
        for (uint8 id : metaTags) {
            if (const Tag* t = file->getTag(id)) {
                // Skip empty strings and zero integers (MFC behavior)
                if (t->isStr() && t->strValue().isEmpty())
                    continue;
                if (t->isInt() && t->intValue() == 0)
                    continue;
                tags.push_back(*t);
            }
        }
    }

    // Write count (regular tags + AICH BSOB if present)
    uint8 totalCount = static_cast<uint8>(tags.size()) + (hasAICH ? 1 : 0);
    packet.writeUInt8(totalCount);

    // Write regular tags
    for (const auto& tag : tags)
        io::writeKadTag(packet, tag);

    // Write AICH BSOB tag separately (writeKadTag can't write BSOB)
    if (hasAICH) {
        const auto& aich = file->fileIdentifier().getAICHHash();
        QByteArray hashData(reinterpret_cast<const char*>(aich.getRawHash()),
                            static_cast<qsizetype>(AICHHash::getHashSize()));
        io::writeKadTagBsob(packet, QByteArrayLiteral(TAG_KADAICHHASHPUB), hashData);
    }
}

void Search::updateNodeLoad(uint8 load)
{
    m_totalLoad += load;
    ++m_totalLoadResponses;
}

uint32 Search::getNodeLoad() const
{
    if (m_totalLoadResponses == 0)
        return 0;
    return m_totalLoad / m_totalLoadResponses;
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

void Search::go(uint32 maxToSend)
{
    // Called when the search is started (maxToSend = kAlphaQuery = 3) or
    // jumpstarted (maxToSend = kJumpstartMaxSend = 1).
    // MFC's Go() sends ALPHA_QUERY on initial start; JumpStart() sends 1.
    if (m_stopping)
        return;

    // MFC Go() lines 169-177: if possible map is empty, repopulate from
    // routing table with maxType=3 (includes type-3 nodes.dat contacts that
    // haven't responded to HELLO yet).  If still empty, just return without
    // stopping — JumpStart will retry later.
    if (m_possible.empty()) {
        uint32 rtContacts = 0;
        if (auto* rz = Kademlia::getInstanceRoutingZone())
            rtContacts = rz->getNumContacts();
        logKad(QStringLiteral("Kad search %1: go() — possible map empty, routing table has %2 contacts")
                   .arg(m_searchID).arg(rtContacts));
        UInt128 distance(RoutingZone::localKadId());
        distance.xorWith(m_target);
        if (auto* rz = Kademlia::getInstanceRoutingZone())
            rz->getClosestTo(3, m_target, distance, 50, m_possible, true, false);
        // Remove contacts already in m_tried to avoid resending
        for (auto it = m_possible.begin(); it != m_possible.end(); ) {
            if (m_tried.count(it->first) > 0)
                it = m_possible.erase(it);
            else
                ++it;
        }
        if (m_possible.empty())
            return;  // Still empty — just return, don't stop (MFC behavior)
    }

    // Convergence detection: when K contacts have responded (m_responded >= K),
    // check whether the closest untried contact is farther than the closest
    // responded contact.  If so, the iterative lookup has converged.
    if (!m_possible.empty() && m_responded.size() >= kK) {
        UInt128 closestResponded = m_responded.begin()->first;
        UInt128 closestPossible = m_possible.begin()->first;
        if (!(closestPossible < closestResponded)) {
            logKad(QStringLiteral("Kad search %1: converged — best=%2 responded=%3 possible=%4 tried=%5")
                       .arg(m_searchID).arg(m_best.size()).arg(m_responded.size())
                       .arg(m_possible.size()).arg(m_tried.size()));
            prepareToStop();
            return;
        }
    }

    // Send FindValue to the closest untried contacts
    uint32 sent = 0;
    auto it = m_possible.begin();
    while (it != m_possible.end() && sent < maxToSend) {
        Contact* contact = it->second;
        auto curIt = it++;

        sendFindValue(contact);
        m_tried[curIt->first] = contact;
        m_possible.erase(curIt);
        ++sent;
    }
}

void Search::processResponse(uint32 fromIP, uint16 fromPort, const ContactArray& results)
{
    m_lastResponse = time(nullptr);

    // Validate response size (MFC Search.cpp:341-344).
    // Reject nodes sending more contacts than requested (protocol violation / malicious).
    {
        uint8 expected = getRequestContactCount();
        if (results.size() > expected
            && !(m_requestedMoreNodesContact != nullptr
                 && results.size() <= KADEMLIA_FIND_VALUE_MORE)) {
            logKad(QStringLiteral("Kad search %1: node %2:%3 sent %4 contacts (expected <= %5), ignoring")
                       .arg(m_searchID).arg(ipToString(fromIP)).arg(fromPort)
                       .arg(results.size()).arg(expected));
            return;
        }
    }

    // Find the responding contact in m_tried and record in m_responded.
    // MFC keeps responders in m_tried; m_responded tracks who replied (keyed by distance).
    bool foundSender = false;
    UInt128 uFromDistance;
    Contact* pFromContact = nullptr;
    for (auto it = m_tried.begin(); it != m_tried.end(); ++it) {
        Contact* c = it->second;
        if (c->address().toUint32() == fromIP && c->getUDPPort() == fromPort) {
            uFromDistance = it->first;
            pFromContact = c;
            foundSender = true;
            break;
        }
    }
    // SafeKad: validate responding contact and record response time
    if (foundSender && pFromContact) {
        // Record response time for FastKad adaptive timeout
        auto sentIt = m_requestSentTimes.find(pFromContact->getClientID());
        if (sentIt != m_requestSentTimes.end()) {
            auto elapsed = std::chrono::steady_clock::now() - sentIt->second;
            double ms = std::chrono::duration<double, std::milli>(elapsed).count();
            if (auto* fk = Kademlia::getInstanceFastKad())
                fk->addResponseTime(fromIP, ms);
            m_requestSentTimes.erase(sentIt);
        }
        // Check if this node is bad (may have changed identity since we sent the request)
        if (auto* sk = Kademlia::getInstanceSafeKad()) {
            if (sk->isBadNode(fromIP, fromPort, pFromContact->getClientID(),
                              pFromContact->getVersion(), true, false)) {
                logKad(QStringLiteral("Kad search %1: SafeKad rejected response from %2:%3")
                           .arg(m_searchID).arg(ipToString(fromIP)).arg(fromPort));
                return;
            }
        }
    }

    logKad(QStringLiteral("Kad search %1: response from %2:%3 — sender %4, +%5 contacts, best=%6 responded=%7 possible=%8")
               .arg(m_searchID).arg(ipToString(fromIP)).arg(fromPort)
               .arg(foundSender ? QStringLiteral("found") : QStringLiteral("NOT found"))
               .arg(results.size()).arg(m_best.size()).arg(m_responded.size()).arg(m_possible.size()));

    // NodeFwCheckUDP: feed response contacts to the UDP firewall tester
    // before the dedup loop which may delete some contacts.
    // Matches MFC Search.cpp ProcessResponse() NODEFWCHECKUDP case.
    if (m_type == SearchType::NodeFwCheckUDP) {
        for (auto* contact : results) {
            if (!UDPFirewallTester::needsMoreTestContacts())
                break;
            UDPFirewallTester::addPossibleTestContact(
                contact->getClientID(), contact->address().toUint32(),
                contact->getUDPPort(), contact->getTCPPort(),
                m_target, contact->getVersion(),
                contact->getUDPKey(), contact->isIpVerified(),
                contact->connectOptions(), contact->clientHash());
        }
        UDPFirewallTester::queryNextClient();
    }

    // Anti-spam: reject duplicate IPs and limit to 2 per /24 subnet (MFC Search.cpp:379-423).
    std::map<uint32, uint32> receivedIPs;
    std::map<uint32, uint32> receivedSubnets;
    receivedIPs[fromIP] = 1;             // node must not answer with itself
    receivedSubnets[fromIP & ~0xFFu] = 1;

    bool providedCloserContacts = false;

    for (auto* contact : results) {
        // Dedup by distance key (MFC Search.cpp:399-400).
        // m_tried contains ALL contacted nodes (responded or not), so checking
        // m_tried + m_possible covers everything. m_best entries are always also
        // in m_tried, so no separate m_best check needed.
        UInt128 dist = contact->getDistance();
        if (m_tried.count(dist) > 0 || m_possible.count(dist) > 0) {
            delete contact;
            continue;
        }

        // Reject duplicate IPs (MFC Search.cpp:403-407)
        if (receivedIPs.count(contact->address().toUint32()) > 0) {
            delete contact;
            continue;
        }
        receivedIPs[contact->address().toUint32()] = 1;

        // Limit to 2 IPs per /24 subnet (MFC Search.cpp:412-423)
        uint32 subnetIP = contact->address().toUint32() & ~0xFFu;
        if (!contact->address().isLan()) {
            auto sit = receivedSubnets.find(subnetIP);
            if (sit != receivedSubnets.end()) {
                if (sit->second >= 2) {
                    delete contact;
                    continue;
                }
                ++sit->second;
            } else {
                receivedSubnets[subnetIP] = 1;
            }
        }

        if (foundSender && dist < uFromDistance)
            providedCloserContacts = true;

        // Add to possible contacts
        m_possible[contact->getDistance()] = contact;

        // Track in lookup history
        if (m_lookupHistory) {
            bool closer = contact->getDistance() < m_closestDistantFound;
            m_lookupHistory->contactReceived(contact, pFromContact, contact->getDistance(), closer);
            if (closer)
                m_closestDistantFound = contact->getDistance();
        }
    }

    // Record response in m_responded (keyed by distance, matching MFC m_mapResponded).
    if (foundSender)
        m_responded[uFromDistance] = providedCloserContacts;

    ++m_totalRequestAnswers;

    // Auto-query closer contacts using persistent m_best (MFC Search.cpp:429-452).
    // m_best tracks the top ALPHA_QUERY closest contacts discovered so far.
    // When a new contact closer than the responder makes it into the top set,
    // it's immediately queried — dramatically speeding up convergence.
    if (pFromContact != nullptr && !m_stopping) {
        // Collect contacts to auto-query first, then erase from m_possible
        // (avoid iterator invalidation during range-for).
        ContactMap toAutoQuery;
        for (auto& [dist, contact] : m_possible) {
            if (!(dist < uFromDistance))
                break;  // m_possible sorted by distance; stop once past responder
            if (m_tried.count(dist) > 0)
                continue;  // already tried

            // Check if this contact fits in the top ALPHA_QUERY (MFC m_mapBest)
            bool isTop = (m_best.size() < kAlphaQuery);
            if (!isTop) {
                auto itLast = std::prev(m_best.end());
                if (dist < itLast->first) {
                    m_best.erase(itLast);
                    isTop = true;
                }
            }
            if (isTop) {
                m_best[dist] = contact;
                toAutoQuery[dist] = contact;
            }
        }
        for (auto& [dist, contact] : toAutoQuery) {
            m_tried[dist] = contact;
            m_possible.erase(dist);
            sendFindValue(contact);
        }
    }

    // NodeSpecial: check if exact match (distance 0) was found among results.
    // Matches MFC Search.cpp:878-885.
    if (m_type == SearchType::NodeSpecial && m_nodeSpecialSearchRequester) {
        static const UInt128 zero(uint32{0});
        for (auto& [dist, contact] : m_possible) {
            if (dist == zero) {
                m_nodeSpecialSearchRequester->kadSearchIPByNodeIDResult(
                    KadClientSearchResult::Succeeded,
                    contact->address().toUint32(), contact->getTCPPort());
                m_nodeSpecialSearchRequester = nullptr;
                prepareToStop();
                break;
            }
        }
    }
}

void Search::processResult(const UInt128& answer, TagList& info, uint32 fromIP, uint16 fromPort)
{
    switch (m_type) {
    case SearchType::File:
        processResultFile(answer, info);
        break;
    case SearchType::Keyword:
        processResultKeyword(answer, info, fromIP, fromPort);
        break;
    case SearchType::Notes:
        processResultNotes(answer, info);
        break;
    default:
        processResultFile(answer, info);
        break;
    }
}

void Search::processResultFile(const UInt128& answer, TagList& info)
{
    // Extract source information from tags (MFC Search.cpp:915-948)
    uint32 sourceIP = 0;
    uint16 sourcePort = 0;
    uint16 udpPort = 0;
    uint8 sourceType = 0;
    uint32 buddyIP = 0;
    uint16 buddyPort = 0;
    uint8 cryptOptions = 0;

    // Kad tags use numeric IDs (single-byte names → nameId), not string names.
    for (const auto& tag : info) {
        if (!tag.isInt())
            continue;
        switch (tag.nameId()) {
        case FT_SOURCEIP:    sourceIP      = tag.intValue();                      break;
        case FT_SOURCEPORT:  sourcePort    = static_cast<uint16>(tag.intValue()); break;
        case FT_SOURCEUPORT: udpPort       = static_cast<uint16>(tag.intValue()); break;
        case FT_SOURCETYPE:  sourceType    = static_cast<uint8>(tag.intValue());  break;
        case FT_SERVERIP:    buddyIP       = tag.intValue();                      break;
        case FT_SERVERPORT:  buddyPort     = static_cast<uint16>(tag.intValue()); break;
        case FT_ENCRYPTION:  cryptOptions  = static_cast<uint8>(tag.intValue());  break;
        default: break;
        }
    }

    // FT_BUDDYHASH is a string tag (hex MD4), not int — parse separately (MFC Search.cpp:941-946)
    uint8 parsedBuddyHash[16]{};
    bool hasBuddyHash = false;
    for (const auto& tag : info) {
        if (tag.isStr() && tag.nameId() == FT_BUDDYHASH) {
            if (strmd4(tag.strValue(), parsedBuddyHash))
                hasBuddyHash = true;
            break;
        }
    }

    // Handle source types — MFC Search.cpp:952-962, DownloadQueue.cpp:1529-1601
    uint8 buddyHash[16]{};
    switch (sourceType) {
    case 4:
    case 1:
        // Non-firewalled users (MFC DownloadQueue.cpp:1530-1548)
        break;
    case 5:
    case 3:
        // Firewalled with buddy callback (MFC DownloadQueue.cpp:1553-1582)
        if (hasBuddyHash)
            std::memcpy(buddyHash, parsedBuddyHash, 16);
        break;
    case 6:
        // Direct UDP callback (MFC DownloadQueue.cpp:1584-1601)
        break;
    case 2:
        // MFC DownloadQueue.cpp:1550: "Don't use this type... Some clients will process it wrong."
        return;
    default:
        logKad(QStringLiteral("Kad search %1: skipping unknown source type %2 from %3")
                   .arg(m_searchID).arg(sourceType).arg(answer.toHexString()));
        return;
    }

    ++m_answers; // Only count accepted types (MFC Search.cpp:958)

    // Report via callback to DownloadQueue
    const auto& cb = Kademlia::kadSourceResultCallback();
    if (cb) {
        // Pass the search target (= file hash) and the answer (= source client hash).
        // MFC publishes GetClientHash() (the ED2K user hash) as the source ID in
        // PUBLISH_SOURCE, NOT GetKadID().  So the answer IS the correct user hash
        // and toByteArray() recovers the original bytes for encryption key derivation.
        uint8 fileHash[16];
        m_target.toByteArray(fileHash);
        uint8 sourceClientHash[16];
        answer.toByteArray(sourceClientHash);
        cb(m_searchID, fileHash, sourceIP, sourcePort, buddyIP, buddyPort, cryptOptions,
           sourceType, buddyHash, sourceClientHash, udpPort);
    }

    logKad(QStringLiteral("Kad search %1: got file source %2, type=%3 IP=%4:%5 buddy=%6:%7")
               .arg(m_searchID).arg(answer.toHexString()).arg(sourceType)
               .arg(ipToString(sourceIP)).arg(sourcePort)
               .arg(ipToString(buddyIP)).arg(buddyPort));
}

void Search::processResultKeyword(const UInt128& answer, TagList& info, uint32 fromIP, uint16 fromPort)
{
    ++m_answers;
    logKad(QStringLiteral("Kad: keyword result #%1 from %2:%3, %4 tags")
               .arg(m_answers).arg(fromIP).arg(fromPort).arg(info.size()));

    // Extract file metadata from tags
    QString fileName;
    uint64 fileSize = 0;
    QString fileType;
    uint32 sources = 0;
    uint32 completeSources = 0;

    for (const auto& tag : info) {
        switch (tag.nameId()) {
        case FT_FILENAME:
            if (tag.isStr())
                fileName = tag.strValue();
            break;
        case FT_FILESIZE:
            if (tag.isInt())
                fileSize = tag.intValue();
            else if (tag.isInt64(false))
                fileSize = tag.int64Value();
            break;
        case FT_FILESIZE_HI:
            if (tag.isInt())
                fileSize |= static_cast<uint64>(tag.intValue()) << 32;
            break;
        case FT_FILETYPE:
            if (tag.isStr())
                fileType = tag.strValue();
            break;
        case FT_SOURCES:
            if (tag.isInt())
                sources = tag.intValue();
            break;
        case FT_COMPLETE_SOURCES:
            if (tag.isInt())
                completeSources = tag.intValue();
            break;
        default:
            break;
        }
    }

    // Report via callback to SearchList
    const auto& cb = Kademlia::kadKeywordResultCallback();
    if (cb) {
        uint8 fileHash[16];
        answer.toByteArray(fileHash);
        cb(m_searchID, fileHash, fileName, fileSize, fileType, sources, completeSources);
    }

    if (m_lookupHistory)
        m_lookupHistory->contactRespondedKeyword(fromIP, fromPort, m_answers);
}

void Search::processResultNotes(const UInt128& answer, TagList& info)
{
    ++m_answers;

    // Extract notes information from tags
    QString fileName;
    uint8 rating = 0;
    QString comment;

    for (const auto& tag : info) {
        switch (tag.nameId()) {
        case FT_FILENAME:
            if (tag.isStr())
                fileName = tag.strValue();
            break;
        case FT_FILERATING:
            if (tag.isInt())
                rating = static_cast<uint8>((tag.intValue() & 0xFF) >> 1);
            break;
        case FT_FILECOMMENT:
            if (tag.isStr())
                comment = tag.strValue();
            break;
        default:
            break;
        }
    }

    // Report via callback. For a notes search the search target IS the file hash;
    // `answer` is the note publisher's source ID (used downstream to dedup results).
    const auto& cb = Kademlia::kadNotesResultCallback();
    if (cb) {
        uint8 fileHash[16];
        m_target.toByteArray(fileHash);
        uint8 publisherId[16];
        answer.toByteArray(publisherId);
        cb(m_searchID, fileHash, publisherId, fileName, rating, comment);
    }

    logKad(QStringLiteral("Kad search %1: got notes result %2, rating=%3")
               .arg(m_searchID).arg(answer.toHexString()).arg(rating));
}

void Search::jumpStart()
{
    if (m_stopping)
        return;

    time_t now = time(nullptr);

    // Check if search has expired
    time_t lifetime = static_cast<time_t>(getLifetime());
    if ((now - m_created) > lifetime) {
        logKad(QStringLiteral("Kad search %1: lifetime expired (%2s) — best=%3 responded=%4 possible=%5")
                   .arg(m_searchID).arg(now - m_created)
                   .arg(m_best.size()).arg(m_responded.size()).arg(m_possible.size()));
        prepareToStop();
        return;
    }

    // Adaptive cooldown: use FastKad estimate for find operations, fixed 3s for store.
    time_t cooldown = kSearchJumpstartCooldown;
    if (m_type != SearchType::StoreFile && m_type != SearchType::StoreKeyword
        && m_type != SearchType::StoreNotes)
    {
        if (auto* fk = Kademlia::getInstanceFastKad()) {
            double estMs = fk->getEstMaxResponseTimeMs();
            time_t estSec = static_cast<time_t>(estMs / 1000.0) + 1;
            if (estSec > 0 && estSec < 10)
                cooldown = estSec;
        }
    }
    if ((now - m_lastResponse) < cooldown)
        return;

    // If we ran out of contacts, stop search (MFC Search.cpp:268-270).
    if (m_possible.empty()) {
        prepareToStop();
        return;
    }

    // Dead-node detection (MFC Search.cpp:277-293):
    // For FIND_VALUE searches (return 2 contacts), if the closest tried contacts
    // are all dead, ask a responding contact for MORE nodes (11 instead of 2/4).
    // m_tried contains ALL contacted nodes; m_responded tracks which ones replied.
    if (m_requestedMoreNodesContact == nullptr
        && getRequestContactCount() == KADEMLIA_FIND_VALUE
        && m_tried.size() >= 3u * KADEMLIA_FIND_VALUE) {
        auto itTried = m_tried.begin();
        bool closestAreDead = true;
        for (int i = 0; i < KADEMLIA_FIND_VALUE && itTried != m_tried.end(); ++i, ++itTried) {
            if (m_responded.count(itTried->first) > 0) {
                closestAreDead = false;
                break;
            }
        }
        if (closestAreDead) {
            // Find first responded contact further out to re-ask for more
            for (; itTried != m_tried.end(); ++itTried) {
                if (m_responded.count(itTried->first) > 0) {
                    logKad(QStringLiteral("Kad search %1: closest FIND_VALUE nodes dead, re-asking for more")
                               .arg(m_searchID));
                    sendFindValue(itTried->second, true);
                    return;
                }
            }
        }
    }

    // Send a single packet to unstick a stalled search (MFC sends 1 per jumpstart).
    go(kJumpstartMaxSend);
}

void Search::sendFindValue(Contact* contact, bool reAskMore)
{
    if (!contact || m_stopping)
        return;

    auto* udpListener = Kademlia::getInstanceUDPListener();
    if (!udpListener)
        return;

    ++m_kadPacketSent;
    m_inUse[contact->getClientID()] = contact;
    contact->incUse();
    m_requestSentTimes[contact->getClientID()] = std::chrono::steady_clock::now();

    if (m_lookupHistory)
        m_lookupHistory->contactAskedKad(contact);

    // Find phase: always send KADEMLIA2_REQ to discover closest contacts.
    // The type-specific opcodes (SEARCH_KEY_REQ, SEARCH_SOURCE_REQ, etc.)
    // are sent in the action phase from storePacket(), matching MFC's
    // two-phase design: SendFindValue → KADEMLIA2_REQ, StorePacket → opcode.
    {
        SafeMemFile packet;
        // Type byte determines how many contacts the receiver returns
        // Must match MFC GetRequestContactCount() mapping
        uint8 searchType = KADEMLIA_FIND_NODE;
        if (m_type == SearchType::File || m_type == SearchType::Keyword ||
            m_type == SearchType::FindSource || m_type == SearchType::Notes)
            searchType = KADEMLIA_FIND_VALUE;
        else if (m_type == SearchType::StoreFile || m_type == SearchType::StoreKeyword ||
                 m_type == SearchType::StoreNotes || m_type == SearchType::FindBuddy)
            searchType = KADEMLIA_STORE;

        // Dead-node recovery: ask for more contacts (MFC Search.cpp:291).
        if (reAskMore && m_requestedMoreNodesContact == nullptr) {
            m_requestedMoreNodesContact = contact;
            searchType = KADEMLIA_FIND_VALUE_MORE;
        }

        packet.writeUInt8(searchType);
        io::writeUInt128(packet, m_target);
        // Third field: the contact's Kad ID — the receiver checks this
        // against its own ID for sanity (MFC: "for sanity checks on the
        // other end").  Sending our own ID here causes silent drops.
        io::writeUInt128(packet, contact->getClientID());
        UInt128 reqClientID = contact->getClientID();
        udpListener->sendPacket(packet, KADEMLIA2_REQ,
                                contact->address().toUint32(), contact->getUDPPort(),
                                contact->getUDPKey(), &reqClientID);
    }
}

void Search::prepareToStop()
{
    if (m_stopping)
        return;

    m_stopping = true;
    m_storePhaseStarted = time(nullptr);

    // SafeKad: mark non-responding contacts as problematic
    if (auto* sk = Kademlia::getInstanceSafeKad()) {
        for (const auto& [dist, contact] : m_tried) {
            if (m_responded.find(dist) == m_responded.end())
                sk->trackProblematicNode(contact->address().toUint32(), contact->getUDPPort());
        }
    }

    // Adjust m_created so the search expires within ~15 seconds.
    // MFC: m_tCreated = time(NULL) - uBaseTime + SEC(15);
    // This lets SearchManager::jumpStart() delete it on the next cycle.
    m_created = time(nullptr) - static_cast<time_t>(getLifetime()) + 15;

    if (m_lookupHistory)
        m_lookupHistory->setSearchStopped();

    // Action phase: search types send their search requests to closest
    // responded contacts; store types send publish packets; FindBuddy/
    // FindSource/NodeSpecial send their respective packets.
    switch (m_type) {
    case SearchType::File:
    case SearchType::Keyword:
    case SearchType::Notes:
    case SearchType::StoreFile:
    case SearchType::StoreKeyword:
    case SearchType::StoreNotes:
    case SearchType::FindBuddy:
    case SearchType::FindSource:
    case SearchType::NodeSpecial:
        storePacket();
        break;
    default:
        break;
    }
}

void Search::storePacket()
{
    auto* udpListener = Kademlia::getInstanceUDPListener();
    if (!udpListener)
        return;

    // Send store packets to the closest contacts that responded
    if (m_responded.empty()) {
        logKad(QStringLiteral("Kad search %1: store phase — no responded contacts")
                   .arg(m_searchID));
        return;
    }

    // Determine the per-type contact limit
    uint32 maxStore = kSearchStoreKeywordTotal;
    switch (m_type) {
    case SearchType::FindBuddy:  maxStore = kSearchFindBuddyTotal; break;
    case SearchType::FindSource: maxStore = kSearchFindSourceTotal; break;
    default: break;
    }

    // Send to the closest responded contacts from m_tried (MFC iterates m_mapPossible
    // for responded entries; we iterate m_tried which is sorted by distance).
    uint32 storeCount = 0;
    for (auto& [dist, contact] : m_tried) {
        if (!contact || storeCount >= maxStore)
            break;

        // Only store to contacts that responded
        if (m_responded.count(dist) == 0)
            continue;

        // Distance tolerance: skip contacts too far from target (MFC Search.cpp:479-482).
        if (dist.get32BitChunk(0) > kSearchTolerance && !contact->address().isLan()) // or always bypass in LAN mode? && !(Kademlia::instance() && Kademlia::instance()->isRunningInLANMode())
            continue;

        switch (m_type) {
        case SearchType::Keyword: {
            // Action phase: send KADEMLIA2_SEARCH_KEY_REQ to closest responded contacts
            logKad(QStringLiteral("Kad search %1: SEARCH_KEY_REQ #%2 → %3:%4 dist=%5")
                       .arg(m_searchID).arg(storeCount + 1)
                       .arg(contact->address().toString()).arg(contact->getUDPPort())
                       .arg(dist.toHexString()));
            SafeMemFile packet;
            io::writeUInt128(packet, m_target);
            // Position marker with 0x8000 flag indicating search terms follow
            if (!m_searchTermsData.isEmpty()) {
                packet.writeUInt16(0x8000);
                packet.write(m_searchTermsData.constData(), m_searchTermsData.size());
            } else {
                packet.writeUInt16(0);
            }
            {
                UInt128 keyClientID = contact->getClientID();
                udpListener->sendPacket(packet, KADEMLIA2_SEARCH_KEY_REQ,
                                        contact->address().toUint32(), contact->getUDPPort(),
                                        contact->getUDPKey(), &keyClientID);
            }
            ++storeCount;
            break;
        }
        case SearchType::File: {
            // Action phase: send KADEMLIA2_SEARCH_SOURCE_REQ to closest responded contacts
            SafeMemFile packet;
            io::writeUInt128(packet, m_target);
            uint8 hash[16];
            m_target.toByteArray(hash);
            auto* partFile = theApp.downloadQueue
                ? theApp.downloadQueue->fileByID(hash) : nullptr;
            if (!partFile)
                break;
            packet.writeUInt16(0); // start position
            packet.writeUInt64(static_cast<uint64>(partFile->fileSize()));
            {
                UInt128 clientID = contact->getClientID();
                udpListener->sendPacket(packet, KADEMLIA2_SEARCH_SOURCE_REQ,
                                        contact->address().toUint32(), contact->getUDPPort(),
                                        contact->getUDPKey(), &clientID);
            }
            ++storeCount;
            break;
        }
        case SearchType::Notes: {
            // Action phase: send KADEMLIA2_SEARCH_NOTES_REQ to closest responded contacts
            SafeMemFile packet;
            io::writeUInt128(packet, m_target);
            uint8 noteHash[16];
            m_target.toByteArray(noteHash);
            auto* noteSearchFile = theApp.sharedFileList
                ? theApp.sharedFileList->getFileByID(noteHash) : nullptr;
            packet.writeUInt64(noteSearchFile
                ? static_cast<uint64>(noteSearchFile->fileSize()) : 0);
            {
                UInt128 noteClientID = contact->getClientID();
                udpListener->sendPacket(packet, KADEMLIA2_SEARCH_NOTES_REQ,
                                        contact->address().toUint32(), contact->getUDPPort(),
                                        contact->getUDPKey(), &noteClientID);
            }
            ++storeCount;
            break;
        }
        case SearchType::StoreKeyword: {
            // Collect files that are still shared
            std::vector<std::pair<UInt128, KnownFile*>> validFiles;
            for (const auto& fileID : m_fileIDs) {
                uint8 hash[16];
                fileID.toByteArray(hash);
                auto* file = theApp.sharedFileList
                    ? theApp.sharedFileList->getFileByID(hash) : nullptr;
                if (file)
                    validFiles.emplace_back(fileID, file);
            }

            // MFC batches up to 50 files per PUBLISH_KEY_REQ packet
            constexpr size_t kMaxFilesPerPacket = 50;
            size_t totalFiles = std::min(validFiles.size(), size_t{150});

            for (size_t i = 0; i < totalFiles; ) {
                SafeMemFile packet;
                io::writeUInt128(packet, m_target);
                auto countPos = packet.position();
                packet.writeUInt16(0); // placeholder

                uint16 packetFileCount = 0;
                for (; packetFileCount < kMaxFilesPerPacket && i < totalFiles; ++i) {
                    io::writeUInt128(packet, validFiles[i].first);
                    preparePacketForTags(packet, validFiles[i].second, KADEMLIA_VERSION);
                    ++packetFileCount;
                }

                // Fix up file count
                auto endPos = packet.position();
                packet.seek(countPos, 0);
                packet.writeUInt16(packetFileCount);
                packet.seek(endPos, 0);

                UInt128 pubKeyClientID = contact->getClientID();
                udpListener->sendPacket(packet, KADEMLIA2_PUBLISH_KEY_REQ,
                                        contact->address().toUint32(), contact->getUDPPort(),
                                        contact->getUDPKey(), &pubKeyClientID);
            }
            ++storeCount;
            break;
        }
        case SearchType::StoreFile: {
            // Build source publish packet: targetID + sourceID + tagList
            SafeMemFile packet;
            io::writeUInt128(packet, m_target);
            auto* prefs = Kademlia::getInstancePrefs();
            // MFC uses GetClientHash() (ED2K user hash) for source publishing
            io::writeUInt128(packet, prefs ? prefs->clientHash() : RoutingZone::localKadId());

            // Build source tags matching MFC StorePacket format
            // Source types: 1=HighID, 3=FW+buddy(<=4GB), 4=HighID(>4GB),
            //               5=FW+buddy(>4GB), 6=FW+directUDP
            std::vector<Tag> tags;

            uint8 targetHash[16];
            m_target.toByteArray(targetHash);
            auto* pubFile = theApp.sharedFileList
                ? theApp.sharedFileList->getFileByID(targetHash) : nullptr;
            bool largeFile = pubFile && pubFile->isLargeFile();

            auto* kadInst = Kademlia::instance();
            if (kadInst && kadInst->isFirewalled()) {
                // Check for direct UDP callback (source type 6)
                bool directCallback = kadInst->isRunning()
                    && !UDPFirewallTester::isFirewalledUDP(true)
                    && UDPFirewallTester::isVerified();

                if (directCallback) {
                    // Source type 6: firewalled but direct UDP callback possible
                    tags.emplace_back(FT_SOURCETYPE,
                                      static_cast<uint32>(6));
                    tags.emplace_back(FT_SOURCEPORT,
                                      static_cast<uint32>(thePrefs.port()));
                    if (prefs && !prefs->useExternKadPort())
                        tags.emplace_back(FT_SOURCEUPORT,
                                          static_cast<uint32>(prefs->internKadPort()));
                    if (pubFile)
                        tags.emplace_back(FT_FILESIZE,
                                          static_cast<uint64>(pubFile->fileSize()));
                } else {
                    auto* clientList = Kademlia::getClientList();
                    auto* buddy = clientList ? clientList->getBuddy() : nullptr;
                    if (!buddy) {
                        // Firewalled, no direct callback, no buddy — can't publish
                        prepareToStop();
                        break;
                    }
                    // Source type 3 (firewalled <=4GB) or 5 (firewalled >4GB)
                    tags.emplace_back(FT_SOURCETYPE,
                                      static_cast<uint32>(largeFile ? 5 : 3));
                    tags.emplace_back(FT_SERVERIP,
                                      buddy->userAddress().toUint32());
                    tags.emplace_back(FT_SERVERPORT,
                                      static_cast<uint32>(buddy->userPort()));
                    // FT_BUDDYHASH: hex string of KadID XOR (MFC: md4str(uBuddyID))
                    UInt128 buddyID(true);
                    buddyID.xorWith(prefs ? prefs->kadId() : RoutingZone::localKadId());
                    tags.emplace_back(FT_BUDDYHASH,
                                      buddyID.toHexString());
                    tags.emplace_back(FT_SOURCEPORT,
                                      static_cast<uint32>(thePrefs.port()));
                    if (prefs && !prefs->useExternKadPort())
                        tags.emplace_back(FT_SOURCEUPORT,
                                          static_cast<uint32>(prefs->internKadPort()));
                    if (pubFile)
                        tags.emplace_back(FT_FILESIZE,
                                          static_cast<uint64>(pubFile->fileSize()));
                }
            } else {
                // Not firewalled: source type 1 (normal) or 4 (>4GB)
                tags.emplace_back(FT_SOURCETYPE,
                                  static_cast<uint32>(largeFile ? 4 : 1));
                tags.emplace_back(FT_SOURCEPORT,
                                  static_cast<uint32>(thePrefs.port()));
                if (prefs && !prefs->useExternKadPort())
                    tags.emplace_back(FT_SOURCEUPORT,
                                      static_cast<uint32>(prefs->internKadPort()));
                if (pubFile)
                    tags.emplace_back(FT_FILESIZE,
                                      static_cast<uint64>(pubFile->fileSize()));
            }

            // FT_ENCRYPTION: connect options (MFC: CKadTagUInt8)
            uint8 byCrypt = 0;
            if (thePrefs.cryptLayerSupported())  byCrypt |= 0x01;
            if (thePrefs.cryptLayerRequested())  byCrypt |= 0x02;
            if (thePrefs.cryptLayerRequired())   byCrypt |= 0x04;
            tags.emplace_back(FT_ENCRYPTION,
                              static_cast<uint32>(byCrypt));

            io::writeKadTagList(packet, tags);
            logKad(QStringLiteral("Kad: PUBLISH_SOURCE_REQ pktLen=%1 tags=%2 srcType=%3")
                       .arg(packet.length())
                       .arg(tags.size())
                       .arg(tags.empty() ? 0 : tags[0].intValue()));
            {
                UInt128 pubSrcClientID = contact->getClientID();
                udpListener->sendPacket(packet, KADEMLIA2_PUBLISH_SOURCE_REQ,
                                        contact->address().toUint32(), contact->getUDPPort(),
                                        contact->getUDPKey(), &pubSrcClientID);
            }
            ++storeCount;
            break;
        }
        case SearchType::StoreNotes: {
            // Build notes publish packet: targetID + sourceID + tagList
            SafeMemFile packet;
            io::writeUInt128(packet, m_target);
            auto* prefs = Kademlia::getInstancePrefs();
            io::writeUInt128(packet, prefs ? prefs->kadId() : RoutingZone::localKadId());

            // Build notes tags from the file data
            std::vector<Tag> tags;
            // Look up the file via the first fileID (notes are per-file)
            KnownFile* noteFile = nullptr;
            if (!m_fileIDs.empty()) {
                uint8 hash[16];
                m_fileIDs[0].toByteArray(hash);
                noteFile = theApp.sharedFileList
                    ? theApp.sharedFileList->getFileByID(hash) : nullptr;
            }
            if (noteFile) {
                // MFC STORENOTES: TAG_FILENAME, TAG_FILERATING, TAG_DESCRIPTION, TAG_FILESIZE
                if (!noteFile->fileName().isEmpty())
                    tags.emplace_back(FT_FILENAME, noteFile->fileName());
                uint32 rating = noteFile->getFileRating();
                if (rating > 0)
                    tags.emplace_back(FT_FILERATING, rating);
                QString comment = noteFile->getFileComment();
                if (!comment.isEmpty())
                    tags.emplace_back(QByteArrayLiteral(TAG_DESCRIPTION), comment); // 0x0B, not FT_FILECOMMENT
                // Single uint64 filesize tag (MFC: CKadTagUInt(TAG_FILESIZE, pFile->GetFileSize()))
                tags.emplace_back(FT_FILESIZE, static_cast<uint64>(noteFile->fileSize()));
            }

            io::writeKadTagList(packet, tags);
            {
                UInt128 pubNotesClientID = contact->getClientID();
                udpListener->sendPacket(packet, KADEMLIA2_PUBLISH_NOTES_REQ,
                                        contact->address().toUint32(), contact->getUDPPort(),
                                        contact->getUDPKey(), &pubNotesClientID);
            }
            ++storeCount;
            break;
        }
        case SearchType::FindBuddy: {
            // Send KADEMLIA_FINDBUDDY_REQ to closest responded contacts.
            // Matches MFC Search.cpp:810-837.
            auto* prefs = Kademlia::getInstancePrefs();
            if (!prefs)
                break;

            SafeMemFile packet;
            // Write the target (= ~kadID) as BuddyID — used for callback verification.
            // MFC writes m_uTarget directly (which is ~kadID, set at search creation).
            io::writeUInt128(packet, m_target);
            // Write our client hash so the remote can do a callback
            io::writeUInt128(packet, prefs->clientHash());
            // Write our ED2K TCP port so the remote can TCP-connect to us
            packet.writeUInt16(thePrefs.port());

            udpListener->sendPacket(packet, KADEMLIA_FINDBUDDY_REQ,
                                    contact->address().toUint32(), contact->getUDPPort(),
                                    contact->getUDPKey(), nullptr);
            ++storeCount;
            break;
        }
        case SearchType::FindSource: {
            // Send KADEMLIA_CALLBACK_REQ through the buddy's contact.
            // Matches MFC Search.cpp:844-876.
            SafeMemFile packet;
            // Write the target ID (the buddy's ID)
            io::writeUInt128(packet, m_target);
            // Write the file ID (stored in m_fileIDs[0])
            if (!m_fileIDs.empty())
                io::writeUInt128(packet, m_fileIDs[0]);
            else
                io::writeUInt128(packet, UInt128());
            // Write our ED2K TCP port so the callback works (MFC: thePrefs.GetPort())
            packet.writeUInt16(thePrefs.port());

            udpListener->sendPacket(packet, KADEMLIA_CALLBACK_REQ,
                                    contact->address().toUint32(), contact->getUDPPort(),
                                    contact->getUDPKey(), nullptr);
            ++storeCount;
            break;
        }
        case SearchType::NodeSpecial: {
            // Check if exact match (distance 0) was found among best contacts.
            // Matches MFC Search.cpp:878-885.
            static const UInt128 zero(uint32{0});
            if (dist == zero && m_nodeSpecialSearchRequester) {
                m_nodeSpecialSearchRequester->kadSearchIPByNodeIDResult(
                    KadClientSearchResult::Succeeded,
                    contact->address().toUint32(), contact->getTCPPort());
                m_nodeSpecialSearchRequester = nullptr;
            }
            break;
        }
        default:
            break;
        }
    }

    logKad(QStringLiteral("Kad search %1: store phase — sent to %2 contacts")
               .arg(m_searchID).arg(storeCount));
}

uint8 Search::getRequestContactCount() const
{
    // Must match MFC GetRequestContactCount() (Search.cpp:1510-1534).
    switch (m_type) {
    case SearchType::Node:
    case SearchType::NodeComplete:
    case SearchType::NodeSpecial:
    case SearchType::NodeFwCheckUDP:
        return KADEMLIA_FIND_NODE;  // 11
    case SearchType::StoreFile:
    case SearchType::StoreKeyword:
    case SearchType::StoreNotes:
    case SearchType::FindBuddy:
        return KADEMLIA_STORE;      // 4
    default:
        return KADEMLIA_FIND_VALUE; // 2
    }
}

uint32 Search::getLifetime() const
{
    switch (m_type) {
    case SearchType::Node:           return kSearchNodeLifetime;
    case SearchType::NodeComplete:   return kSearchNodeCompLifetime;
    case SearchType::File:           return kSearchFileLifetime;
    case SearchType::Keyword:        return kSearchKeywordLifetime;
    case SearchType::Notes:          return kSearchNotesLifetime;
    case SearchType::StoreFile:      return kSearchStoreFileLifetime;
    case SearchType::StoreKeyword:   return kSearchStoreKeywordLifetime;
    case SearchType::StoreNotes:     return kSearchStoreNotesLifetime;
    case SearchType::FindBuddy:      return kSearchFindBuddyLifetime;
    case SearchType::FindSource:     return kSearchFindSourceLifetime;
    case SearchType::NodeSpecial:    return kSearchNodeLifetime;
    case SearchType::NodeFwCheckUDP: return kSearchNodeLifetime;
    }
    return kSearchLifetime;
}

} // namespace eMule::kad
