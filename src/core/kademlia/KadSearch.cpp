/// @file KadSearch.cpp
/// @brief Kademlia search state machine implementation.

#include "kademlia/KadSearch.h"
#include "kademlia/KadClientSearcher.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadFirewallTester.h"
#include "kademlia/KadIO.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadLookupHistory.h"
#include "kademlia/KadMiscUtils.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearchDefs.h"
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
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <algorithm>
#include <ctime>

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

void Search::preparePacketForTags(SafeMemFile& packet, KnownFile* file, uint8 /*targetKadVersion*/)
{
    if (!file) {
        packet.writeUInt32(0);
        return;
    }

    // Count tags first
    uint32 tagCount = 0;
    if (!file->fileName().isEmpty())        ++tagCount; // FT_FILENAME
    ++tagCount;                                          // FT_FILESIZE (always)
    if (file->fileSize() > 0xFFFFFFFFULL)   ++tagCount; // FT_FILESIZE_HI
    if (!file->fileType().isEmpty())        ++tagCount;  // FT_FILETYPE
    if (file->getFileRating() > 0)          ++tagCount;  // FT_FILERATING

    packet.writeUInt32(tagCount);

    // Write tags using new ed2k format
    if (!file->fileName().isEmpty())
        Tag(FT_FILENAME, file->fileName()).writeNewEd2kTag(packet);

    auto sz = static_cast<uint64>(file->fileSize());
    Tag(FT_FILESIZE, static_cast<uint32>(sz & 0xFFFFFFFF)).writeNewEd2kTag(packet);

    if (sz > 0xFFFFFFFFULL)
        Tag(FT_FILESIZE_HI, static_cast<uint32>(sz >> 32)).writeNewEd2kTag(packet);

    if (!file->fileType().isEmpty())
        Tag(FT_FILETYPE, file->fileType()).writeNewEd2kTag(packet);

    if (file->getFileRating() > 0)
        Tag(FT_FILERATING, file->getFileRating()).writeNewEd2kTag(packet);
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

void Search::go()
{
    // Called when the search is started or jumpstarted
    if (m_stopping)
        return;

    if (m_possible.empty() && m_tried.empty()) {
        // No contacts to query — search converged or failed
        logKad(QStringLiteral("Kad search %1: go() — no possible or tried contacts, stopping")
                   .arg(m_searchID));
        prepareToStop();
        return;
    }

    // Convergence detection: when enough close contacts have responded
    // (m_best >= K), check whether the closest untried contact is farther
    // than the closest contact that already responded.  If so, the iterative
    // lookup has converged — no closer contacts exist in the network.
    // Transition immediately to the action phase (storePacket / search
    // request) instead of waiting for the full lifetime to expire.
    // Note: m_tried may still contain non-responsive contacts — we don't
    // require m_tried to be empty because offline contacts would block
    // convergence indefinitely.
    if (!m_possible.empty() && m_best.size() >= kK) {
        UInt128 closestBest = m_best.begin()->first;
        UInt128 closestPossible = m_possible.begin()->first;
        if (!(closestPossible < closestBest)) {
            // All remaining candidates are at least as far as our closest
            // responded contact — the lookup has converged.
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
    while (it != m_possible.end() && sent < kAlphaQuery) {
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

    // Record the responding contact: move from m_tried → m_best, mark in m_responded.
    // Without this, storePacket() would find m_responded empty and skip the store phase.
    bool foundSender = false;
    for (auto it = m_tried.begin(); it != m_tried.end(); ++it) {
        Contact* c = it->second;
        if (c->getIPAddress() == fromIP && c->getUDPPort() == fromPort) {
            m_best[it->first] = c;
            m_responded[c->getClientID()] = true;
            m_tried.erase(it);
            foundSender = true;
            break;
        }
    }
    logKad(QStringLiteral("Kad search %1: response from %2:%3 — sender %4, +%5 contacts, best=%6 responded=%7 possible=%8")
               .arg(m_searchID).arg(ipToString(fromIP)).arg(fromPort)
               .arg(foundSender ? QStringLiteral("found") : QStringLiteral("NOT found"))
               .arg(results.size()).arg(m_best.size()).arg(m_responded.size()).arg(m_possible.size()));

    for (auto* contact : results) {
        // Check if we've already tried this contact
        if (m_tried.count(contact->getClientID()) > 0 ||
            m_responded.count(contact->getClientID()) > 0) {
            delete contact;
            continue;
        }

        // Check if already in possible list
        if (m_possible.count(contact->getClientID()) > 0) {
            delete contact;
            continue;
        }

        // Add to possible contacts
        m_possible[contact->getDistance()] = contact;

        // Track in lookup history
        if (m_lookupHistory) {
            // Find the from-contact
            bool closer = contact->getDistance() < m_closestDistantFound;
            m_lookupHistory->contactReceived(contact, nullptr, contact->getDistance(), closer);
            if (closer)
                m_closestDistantFound = contact->getDistance();
        }
    }

    ++m_totalRequestAnswers;

    // NodeSpecial: check if exact match (distance 0) was found among results.
    // Matches MFC Search.cpp:878-885.
    if (m_type == SearchType::NodeSpecial && m_nodeSpecialSearchRequester) {
        static const UInt128 zero(uint32{0});
        for (auto& [dist, contact] : m_possible) {
            if (dist == zero) {
                m_nodeSpecialSearchRequester->kadSearchIPByNodeIDResult(
                    KadClientSearchResult::Succeeded,
                    contact->getIPAddress(), contact->getTCPPort());
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
    ++m_answers;

    // Extract source information from tags
    uint32 sourceIP = 0;
    uint16 sourcePort = 0;
    uint16 sourceUPort = 0;
    uint8 sourceType = 0;
    uint32 buddyIP = 0;
    uint16 buddyPort = 0;
    uint8 cryptOptions = 0;

    for (const auto& tag : info) {
        if (tag.name() == QByteArray(TAG_SOURCEIP) && tag.isInt())
            sourceIP = tag.intValue();
        else if (tag.name() == QByteArray(TAG_SOURCEPORT) && tag.isInt())
            sourcePort = static_cast<uint16>(tag.intValue());
        else if (tag.name() == QByteArray(TAG_SOURCEUPORT) && tag.isInt())
            sourceUPort = static_cast<uint16>(tag.intValue());
        else if (tag.name() == QByteArray(TAG_SOURCETYPE) && tag.isInt())
            sourceType = static_cast<uint8>(tag.intValue());
        else if (tag.name() == QByteArray(TAG_ENCRYPTION) && tag.isInt())
            cryptOptions = static_cast<uint8>(tag.intValue());
    }

    // Report via callback to DownloadQueue
    const auto& cb = Kademlia::kadSourceResultCallback();
    if (cb) {
        uint8 fileHash[16];
        answer.toByteArray(fileHash);
        cb(m_searchID, fileHash, sourceIP, sourcePort, buddyIP, buddyPort, cryptOptions);
    }

    logKad(QStringLiteral("Kad search %1: got file source %2, IP=%3:%4")
               .arg(m_searchID).arg(answer.toHexString())
               .arg(ipToString(sourceIP)).arg(sourcePort));
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

    // Report via callback
    const auto& cb = Kademlia::kadNotesResultCallback();
    if (cb) {
        uint8 fileHash[16];
        answer.toByteArray(fileHash);
        cb(m_searchID, fileHash, fileName, rating, comment);
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

    // Check for response timeout
    if ((now - m_lastResponse) > kSearchJumpstart) {
        // Try sending to more contacts
        go();
    }
}

void Search::sendFindValue(Contact* contact, bool /*reAskMore*/)
{
    if (!contact || m_stopping)
        return;

    auto* udpListener = Kademlia::getInstanceUDPListener();
    if (!udpListener)
        return;

    ++m_kadPacketSent;
    m_inUse[contact->getClientID()] = contact;
    contact->incUse();

    if (m_lookupHistory)
        m_lookupHistory->contactAskedKad(contact);

    // Find phase: always send KADEMLIA2_REQ to discover closest contacts.
    // The type-specific opcodes (SEARCH_KEY_REQ, SEARCH_SOURCE_REQ, etc.)
    // are sent in the action phase from storePacket(), matching MFC's
    // two-phase design: SendFindValue → KADEMLIA2_REQ, StorePacket → opcode.
    {
        SafeMemFile packet;
        // Type byte determines how many contacts the receiver returns
        uint8 searchType = KADEMLIA_FIND_NODE;
        if (m_type == SearchType::File || m_type == SearchType::Keyword ||
            m_type == SearchType::Notes)
            searchType = KADEMLIA_FIND_VALUE;
        packet.writeUInt8(searchType);
        io::writeUInt128(packet, m_target);
        // Third field: the contact's Kad ID — the receiver checks this
        // against its own ID for sanity (MFC: "for sanity checks on the
        // other end").  Sending our own ID here causes silent drops.
        io::writeUInt128(packet, contact->getClientID());
        UInt128 reqClientID = contact->getClientID();
        udpListener->sendPacket(packet, KADEMLIA2_REQ,
                                contact->getIPAddress(), contact->getUDPPort(),
                                contact->getUDPKey(), &reqClientID);
    }
}

void Search::prepareToStop()
{
    if (m_stopping)
        return;

    m_stopping = true;
    m_storePhaseStarted = time(nullptr);

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

    // Collect the best responded contacts from m_best
    uint32 storeCount = 0;
    for (auto& [dist, contact] : m_best) {
        if (!contact || storeCount >= maxStore)
            break;

        // Only store to contacts that responded
        if (m_responded.count(contact->getClientID()) == 0)
            continue;

        switch (m_type) {
        case SearchType::Keyword: {
            // Action phase: send KADEMLIA2_SEARCH_KEY_REQ to closest responded contacts
            logKad(QStringLiteral("Kad search %1: SEARCH_KEY_REQ #%2 → %3:%4 dist=%5")
                       .arg(m_searchID).arg(storeCount + 1)
                       .arg(ipToString(contact->getIPAddress())).arg(contact->getUDPPort())
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
                                        contact->getIPAddress(), contact->getUDPPort(),
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
                                        contact->getIPAddress(), contact->getUDPPort(),
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
                                        contact->getIPAddress(), contact->getUDPPort(),
                                        contact->getUDPKey(), &noteClientID);
            }
            ++storeCount;
            break;
        }
        case SearchType::StoreKeyword: {
            // Build keyword publish packet: targetID + fileCount + [fileID + tags]*
            SafeMemFile packet;
            io::writeUInt128(packet, m_target);

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

            packet.writeUInt16(static_cast<uint16>(validFiles.size()));
            for (const auto& [fileID, file] : validFiles) {
                io::writeUInt128(packet, fileID);
                preparePacketForTags(packet, file, KADEMLIA_VERSION);
            }

            {
                UInt128 pubKeyClientID = contact->getClientID();
                udpListener->sendPacket(packet, KADEMLIA2_PUBLISH_KEY_REQ,
                                        contact->getIPAddress(), contact->getUDPPort(),
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
            io::writeUInt128(packet, prefs ? prefs->kadId() : RoutingZone::localKadId());

            // Build source tags matching MFC format.  The receiver (MFC
            // Process_KADEMLIA2_PUBLISH_SOURCE_REQ) requires TAG_SOURCETYPE
            // to be present — without it, m_bSource stays false and the
            // publish is silently dropped.  TAG_SOURCEIP is NOT sent; the
            // receiver adds it from the sender's actual IP address.
            std::vector<Tag> tags;

            // Look up file for size information
            uint8 targetHash[16];
            m_target.toByteArray(targetHash);
            auto* pubFile = theApp.sharedFileList
                ? theApp.sharedFileList->getFileByID(targetHash) : nullptr;
            bool largeFile = pubFile && pubFile->isLargeFile();

            auto* kadInst = Kademlia::instance();
            if (kadInst && kadInst->isFirewalled()) {
                auto* clientList = Kademlia::getClientList();
                auto* buddy = clientList ? clientList->getBuddy() : nullptr;
                if (buddy) {
                    // Source type 3 (firewalled) or 5 (firewalled, >4GB)
                    tags.emplace_back(QByteArray(TAG_SOURCETYPE, 1),
                                      static_cast<uint32>(largeFile ? 5 : 3));
                    tags.emplace_back(QByteArray(TAG_SERVERIP, 1), buddy->userIP());
                    tags.emplace_back(QByteArray(TAG_SERVERPORT, 1),
                                      static_cast<uint32>(buddy->userPort()));
                    tags.emplace_back(QByteArray(TAG_BUDDYHASH, 1), buddy->userHash());
                    tags.emplace_back(QByteArray(TAG_SOURCEPORT, 1),
                                      static_cast<uint32>(thePrefs.port()));
                    tags.emplace_back(QByteArray(TAG_SOURCEUPORT, 1),
                                      static_cast<uint32>(thePrefs.udpPort()));
                    if (pubFile)
                        tags.emplace_back(QByteArray(TAG_FILESIZE, 1),
                                          static_cast<uint64>(pubFile->fileSize()));

                    uint8 byCrypt = 0;
                    if (buddy->supportsCryptLayer())  byCrypt |= 0x01;
                    if (buddy->requestsCryptLayer())  byCrypt |= 0x02;
                    if (buddy->requiresCryptLayer())  byCrypt |= 0x04;
                    tags.emplace_back(QByteArray(TAG_ENCRYPTION, 1),
                                      static_cast<uint32>(byCrypt));
                }
            } else {
                // Not firewalled: source type 1 (normal) or 4 (>4GB)
                tags.emplace_back(QByteArray(TAG_SOURCETYPE, 1),
                                  static_cast<uint32>(largeFile ? 4 : 1));
                tags.emplace_back(QByteArray(TAG_SOURCEPORT, 1),
                                  static_cast<uint32>(thePrefs.port()));
                tags.emplace_back(QByteArray(TAG_SOURCEUPORT, 1),
                                  static_cast<uint32>(thePrefs.udpPort()));
                if (pubFile)
                    tags.emplace_back(QByteArray(TAG_FILESIZE, 1),
                                      static_cast<uint64>(pubFile->fileSize()));
                // TAG_ENCRYPTION: connect options for non-firewalled sources
                uint8 byCrypt = 0;
                if (thePrefs.cryptLayerSupported())  byCrypt |= 0x01;
                if (thePrefs.cryptLayerRequested())  byCrypt |= 0x02;
                if (thePrefs.cryptLayerRequired())   byCrypt |= 0x04;
                tags.emplace_back(QByteArray(TAG_ENCRYPTION, 1),
                                  static_cast<uint32>(byCrypt));
            }

            io::writeKadTagList(packet, tags);
            logKad(QStringLiteral("Kad: PUBLISH_SOURCE_REQ pktLen=%1 tags=%2 srcType=%3")
                       .arg(packet.length())
                       .arg(tags.size())
                       .arg(tags.empty() ? 0 : tags[0].intValue()));
            {
                UInt128 pubSrcClientID = contact->getClientID();
                udpListener->sendPacket(packet, KADEMLIA2_PUBLISH_SOURCE_REQ,
                                        contact->getIPAddress(), contact->getUDPPort(),
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
                if (!noteFile->fileName().isEmpty())
                    tags.emplace_back(FT_FILENAME, noteFile->fileName());
                uint32 rating = noteFile->getFileRating();
                if (rating > 0)
                    tags.emplace_back(FT_FILERATING, rating);
                QString comment = noteFile->getFileComment();
                if (!comment.isEmpty())
                    tags.emplace_back(FT_FILECOMMENT, comment);
                auto sz = static_cast<uint64>(noteFile->fileSize());
                tags.emplace_back(FT_FILESIZE,
                                  static_cast<uint32>(sz & 0xFFFFFFFF));
                if (sz > 0xFFFFFFFFULL)
                    tags.emplace_back(FT_FILESIZE_HI,
                                      static_cast<uint32>(sz >> 32));
            }

            io::writeKadTagList(packet, tags);
            {
                UInt128 pubNotesClientID = contact->getClientID();
                udpListener->sendPacket(packet, KADEMLIA2_PUBLISH_NOTES_REQ,
                                        contact->getIPAddress(), contact->getUDPPort(),
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
            // Write our KadID XOR'd with the target (check/verify ID)
            UInt128 check(prefs->kadId());
            check.xorWith(m_target);
            io::writeUInt128(packet, check);
            // Write our client hash
            io::writeUInt128(packet, prefs->clientHash());
            // Write our TCP port
            packet.writeUInt16(prefs->internKadPort());
            // Write our connect options
            packet.writeUInt8(prefs->myConnectOptions());

            udpListener->sendPacket(packet, KADEMLIA_FINDBUDDY_REQ,
                                    contact->getIPAddress(), contact->getUDPPort(),
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
            // Write our TCP port
            auto* prefs = Kademlia::getInstancePrefs();
            packet.writeUInt16(prefs ? prefs->internKadPort() : uint16{0});

            udpListener->sendPacket(packet, KADEMLIA_CALLBACK_REQ,
                                    contact->getIPAddress(), contact->getUDPPort(),
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
                    contact->getIPAddress(), contact->getTCPPort());
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
    switch (m_type) {
    case SearchType::Node:
    case SearchType::NodeComplete:
    case SearchType::NodeSpecial:
    case SearchType::NodeFwCheckUDP:
        return 11; // KADEMLIA_FIND_NODE returns more contacts
    default:
        return 2;  // KADEMLIA_FIND_VALUE returns fewer
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
