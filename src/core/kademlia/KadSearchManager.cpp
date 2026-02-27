/// @file KadSearchManager.cpp
/// @brief Kademlia search lifecycle management implementation.

#include "kademlia/KadSearchManager.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadDefines.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadMiscUtils.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearch.h"
#include "utils/Log.h"
#include "utils/Opcodes.h"

#include <algorithm>
#include <ctime>

namespace eMule::kad {

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

uint32 SearchManager::s_nextID = 1;
SearchMap SearchManager::s_searches;

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

bool SearchManager::isSearching(uint32 searchID)
{
    for (const auto& [target, search] : s_searches) {
        if (search->getSearchID() == searchID)
            return true;
    }
    return false;
}

void SearchManager::stopSearch(uint32 searchID, bool /*delayDelete*/)
{
    for (auto it = s_searches.begin(); it != s_searches.end(); ++it) {
        if (it->second->getSearchID() == searchID) {
            it->second->prepareToStop();
            delete it->second;
            s_searches.erase(it);
            return;
        }
    }
}

void SearchManager::stopAllSearches()
{
    for (auto& [target, search] : s_searches) {
        search->prepareToStop();
        delete search;
    }
    s_searches.clear();
}

Search* SearchManager::prepareLookup(SearchType type, bool start, const UInt128& id)
{
    // Check if already searching for this target
    if (alreadySearchingFor(id))
        return nullptr;

    auto* search = new Search();
    search->setTargetID(id);
    search->setSearchType(type);

    if (start) {
        if (!startSearch(search)) {
            delete search;
            return nullptr;
        }
    }

    return search;
}

Search* SearchManager::prepareFindKeywords(const QString& keyword,
                                            uint32 searchTermsSize,
                                            const uint8* searchTermsData)
{
    // Split keywords first, then hash only the first word (MFC behavior).
    // The Kad DHT indexes keywords individually, so the target for "test file"
    // is MD4("test"), not MD4("test file").
    UInt128 target;
    QString lowerKeyword = kadTagStrToLower(keyword);
    std::vector<QString> words;
    getWords(lowerKeyword, words);
    if (words.empty())
        return nullptr;
    getKeywordHash(words.front(), target);

    // Check for duplicate
    if (alreadySearchingFor(target))
        return nullptr;

    auto* search = new Search();
    search->setTargetID(target);
    search->setSearchType(SearchType::Keyword);
    search->setGUIName(keyword);

    // Copy split words for search term matching
    search->m_words = std::move(words);

    // Store search terms data
    if (searchTermsSize > 0 && searchTermsData)
        search->setSearchTermData(searchTermsSize, searchTermsData);

    return search;
}

bool SearchManager::startSearch(Search* search)
{
    if (!search)
        return false;

    // Check for duplicate
    if (s_searches.count(search->getTarget()) > 0) {
        logKad(QStringLiteral("Kad: Search for %1 already active")
                   .arg(search->getTarget().toHexString()));
        return false;
    }

    // Populate initial contacts from routing table.
    // While still connecting (no verified contact yet), include type-3
    // nodes.dat contacts so bootstrap lookups have candidates.
    UInt128 distance(RoutingZone::localKadId());
    distance.xorWith(search->getTarget());

    auto* kadInst = Kademlia::instance();
    uint32 maxType = (kadInst && !kadInst->isConnected()) ? 3 : KADEMLIA_FIND_VALUE;

    if (auto* rz = Kademlia::getInstanceRoutingZone()) {
        ContactMap contacts;
        rz->getClosestTo(maxType, search->getTarget(),
                         distance, 50, contacts, true, false);
        for (auto& [dist, contact] : contacts)
            search->m_possible[dist] = contact;
    }

    s_searches[search->getTarget()] = search;
    search->go();

    logKad(QStringLiteral("Kad: Started %1 search %2 for %3")
               .arg(Search::getTypeName(search->getSearchType()))
               .arg(search->getSearchID())
               .arg(search->getTarget().toHexString()));

    return true;
}

void SearchManager::processResponse(const UInt128& target, uint32 fromIP, uint16 fromPort,
                                     ContactArray& results)
{
    auto it = s_searches.find(target);
    if (it == s_searches.end()) {
        // No matching search — delete contacts
        for (auto* c : results)
            delete c;
        results.clear();
        return;
    }

    it->second->processResponse(fromIP, fromPort, results);
}

uint8 SearchManager::getExpectedResponseContactCount(const UInt128& target)
{
    auto it = s_searches.find(target);
    if (it != s_searches.end())
        return it->second->getRequestContactCount();
    return 0;
}

void SearchManager::processResult(const UInt128& target, const UInt128& answer,
                                   TagList& info, uint32 fromIP, uint16 fromPort)
{
    auto it = s_searches.find(target);
    if (it != s_searches.end()) {
        it->second->processResult(answer, info, fromIP, fromPort);
    } else {
        logKad(QStringLiteral("Kad: SEARCH_RES result for %1 — search not found (already removed)")
                   .arg(target.toHexString()));
    }
}

void SearchManager::processPublishResult(const UInt128& target, uint8 load, bool loadResponse)
{
    auto it = s_searches.find(target);
    if (it != s_searches.end()) {
        it->second->m_answers++;
        if (loadResponse)
            it->second->updateNodeLoad(load);

        // Immediately complete store searches that reached the answer threshold.
        // updateStats() only runs every 60s; without this, a search with 140s
        // lifetime might not be removed until ~200s (worst-case alignment).
        uint32 maxAnswers = 0;
        switch (it->second->getSearchType()) {
        case SearchType::StoreFile:    maxAnswers = kSearchStoreFileTotal; break;
        case SearchType::StoreKeyword: maxAnswers = kSearchStoreKeywordTotal; break;
        case SearchType::StoreNotes:   maxAnswers = kSearchStoreNotesTotal; break;
        default: break;
        }
        if (maxAnswers > 0 && it->second->getAnswers() >= maxAnswers) {
            it->second->prepareToStop();
            delete it->second;
            s_searches.erase(it);
        }
    }
}

void SearchManager::updateStats()
{
    // Remove expired searches — by time or by answer count (MFC compat)
    time_t now = time(nullptr);
    auto it = s_searches.begin();
    while (it != s_searches.end()) {
        Search* search = it->second;
        uint32 lifetime = search->getLifetime();
        bool expired = (now - search->m_created) > static_cast<time_t>(lifetime);

        // Keyword/File/Notes searches enter the store phase when the find
        // phase converges, then wait for SEARCH_RES responses.  Give them
        // at least 30 extra seconds after the store phase starts so that
        // remote nodes have time to send back results.
        if (expired && search->stopping() && search->m_storePhaseStarted > 0) {
            auto type = search->getSearchType();
            bool needsResults = (type == SearchType::Keyword
                                 || type == SearchType::File
                                 || type == SearchType::Notes);
            if (needsResults && (now - search->m_storePhaseStarted) < 30)
                expired = false;
        }

        // Store searches also complete when enough responses arrive
        if (!expired) {
            uint32 maxAnswers = 0;
            switch (search->getSearchType()) {
            case SearchType::StoreFile:    maxAnswers = kSearchStoreFileTotal; break;
            case SearchType::StoreKeyword: maxAnswers = kSearchStoreKeywordTotal; break;
            case SearchType::StoreNotes:   maxAnswers = kSearchStoreNotesTotal; break;
            default: break;
            }
            if (maxAnswers > 0 && search->getAnswers() >= maxAnswers)
                expired = true;
        }

        if (expired) {
            logKad(QStringLiteral("Kad: updateStats removing search %1 (type=%2 age=%3s answers=%4)")
                       .arg(search->getSearchID()).arg(static_cast<int>(search->getSearchType()))
                       .arg(now - search->m_created).arg(search->getAnswers()));
            search->prepareToStop();
            delete search;
            it = s_searches.erase(it);
        } else {
            ++it;
        }
    }
}

bool SearchManager::alreadySearchingFor(const UInt128& target)
{
    return s_searches.count(target) > 0;
}

void SearchManager::cancelNodeFWCheckUDPSearch()
{
    for (auto it = s_searches.begin(); it != s_searches.end(); ++it) {
        if (it->second->getSearchType() == SearchType::NodeFwCheckUDP) {
            it->second->prepareToStop();
            delete it->second;
            s_searches.erase(it);
            return;
        }
    }
}

bool SearchManager::findNodeFWCheckUDP()
{
    UInt128 target;
    target.setValueRandom();
    auto* search = prepareLookup(SearchType::NodeFwCheckUDP, true, target);
    return search != nullptr;
}

bool SearchManager::isFWCheckUDPSearch(const UInt128& target)
{
    auto it = s_searches.find(target);
    if (it != s_searches.end())
        return it->second->getSearchType() == SearchType::NodeFwCheckUDP;
    return false;
}

void SearchManager::setNextSearchID(uint32 nextID)
{
    s_nextID = nextID;
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

void SearchManager::findNode(const UInt128& id, bool complete)
{
    prepareLookup(complete ? SearchType::NodeComplete : SearchType::Node, true, id);
}

bool SearchManager::findNodeSpecial(const UInt128& id, KadClientSearcher* requester)
{
    auto* search = prepareLookup(SearchType::NodeSpecial, true, id);
    if (search) {
        search->setNodeSpecialSearchRequester(requester);
        return true;
    }
    return false;
}

void SearchManager::cancelNodeSpecial(const KadClientSearcher* requester)
{
    for (auto it = s_searches.begin(); it != s_searches.end(); ++it) {
        if (it->second->getSearchType() == SearchType::NodeSpecial
            && it->second->getNodeSpecialSearchRequester() == requester) {
            it->second->prepareToStop();
            delete it->second;
            s_searches.erase(it);
            return;
        }
    }
}

void SearchManager::jumpStart()
{
    // Jump-start all searches including stopped ones — matches MFC behavior.
    // MFC JumpStart() always calls pSearch->JumpStart() unconditionally.
    // Stopped searches remain in s_searches until updateStats() removes them.
    for (auto it = s_searches.begin(); it != s_searches.end(); ++it) {
        it->second->jumpStart();
    }
}

} // namespace eMule::kad
