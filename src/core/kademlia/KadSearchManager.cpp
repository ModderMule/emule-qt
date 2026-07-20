#include "pch.h"
/// @file KadSearchManager.cpp
/// @brief Kademlia search lifecycle management implementation.

#include "kademlia/KadSearchManager.h"
#include "kademlia/Kademlia.h"
#include "kademlia/KadContact.h"
#include "kademlia/KadLog.h"
#include "kademlia/KadMiscUtils.h"
#include "kademlia/KadPrefs.h"
#include "kademlia/KadRoutingZone.h"
#include "kademlia/KadSearch.h"


namespace eMule::kad {

// ---------------------------------------------------------------------------
// Static data
// ---------------------------------------------------------------------------

uint32 SearchManager::s_totalResponsesReceived = 0;
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
            notifySearchesChanged();
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
    notifySearchesChanged();
}

Search* SearchManager::prepareLookup(SearchType type, bool start, const UInt128& id, const QString& guiName)
{
    // Check if already searching for this target
    if (alreadySearchingFor(id))
        return nullptr;

    auto* search = new Search();
    search->setTargetID(id);
    search->setSearchType(type);
    // Set the display name before starting so the first GUI push carries it.
    if (!guiName.isEmpty())
        search->setGUIName(guiName);

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
    QString lowerKeyword = kadTagStrToLower(keyword);
    std::vector<QString> words;
    getWords(lowerKeyword, words);
    if (words.empty())
        return nullptr;
    // Same keyword selection the search-terms blob is built against —
    // see kadSearchKeyword().
    UInt128 target;
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

    //uint32 maxType = (kadInst && !kadInst->isConnected()) ? 3 : KADEMLIA_FIND_VALUE; // MFC always 3
    uint32 maxType = 3;

    if (auto* rz = Kademlia::getInstanceRoutingZone()) {
        ContactMap contacts;
        // setInUse=true (MFC Go(), Search.cpp:172): pin these so the routing
        // zone cannot free them while they sit untried in m_possible.
        rz->getClosestTo(maxType, search->getTarget(),
                         distance, 50, contacts, true, true);
        search->pinFetchedContacts(contacts);
        for (auto& [dist, contact] : contacts)
            search->m_possible[dist] = contact;
    }

    s_searches[search->getTarget()] = search;
    search->go();

    logKad(QStringLiteral("Kad: Started %1 search %2 for %3")
               .arg(Search::getTypeName(search->getSearchType()))
               .arg(search->getSearchID())
               .arg(search->getTarget().toHexString()));

    notifySearchesChanged();
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
    ++s_totalResponsesReceived;
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
            notifySearchesChanged();
        }
    }
}

void SearchManager::updateStats()
{
    // Count active searches by type so Kademlia knows whether it can start
    // new publish/search operations.  Matches MFC CSearchManager::UpdateStats().
    // Deletion is handled by jumpStart() which runs every ~1s.
    auto* prefs = Kademlia::getInstancePrefs();
    if (!prefs)
        return;

    uint8 totalFile = 0;
    uint8 totalStoreSrc = 0;
    uint8 totalStoreKey = 0;
    uint8 totalSource = 0;
    uint8 totalNotes = 0;
    uint8 totalStoreNotes = 0;

    for (const auto& [target, search] : s_searches) {
        switch (search->getSearchType()) {
        case SearchType::File:         ++totalFile;      break;
        case SearchType::StoreFile:    ++totalStoreSrc;  break;
        case SearchType::StoreKeyword: ++totalStoreKey;  break;
        case SearchType::FindSource:   ++totalSource;    break;
        case SearchType::Notes:        ++totalNotes;     break;
        case SearchType::StoreNotes:   ++totalStoreNotes; break;
        default: break;
        }
    }

    prefs->setTotalFile(totalFile);
    prefs->setTotalStoreSrc(totalStoreSrc);
    prefs->setTotalStoreKey(totalStoreKey);
    prefs->setTotalSource(totalSource);
    prefs->setTotalNotes(totalNotes);
    prefs->setTotalStoreNotes(totalStoreNotes);
}

bool SearchManager::alreadySearchingFor(const UInt128& target)
{
    return s_searches.count(target) > 0;
}

QString SearchManager::findActiveKeyword(const QString& expression)
{
    QString lower = kadTagStrToLower(expression);
    std::vector<QString> words;
    getWords(lower, words);
    if (words.empty())
        return {};

    UInt128 target;
    getKeywordHash(words.front(), target);
    if (alreadySearchingFor(target))
        return words.front();
    return {};
}

void SearchManager::cancelNodeFWCheckUDPSearch()
{
    for (auto it = s_searches.begin(); it != s_searches.end(); ++it) {
        if (it->second->getSearchType() == SearchType::NodeFwCheckUDP) {
            it->second->prepareToStop();
            delete it->second;
            s_searches.erase(it);
            notifySearchesChanged();
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
            notifySearchesChanged();
            return;
        }
    }
}

void SearchManager::jumpStart()
{
    // Find stalled searches and jump-start them; also prune expired ones.
    // Matches MFC CSearchManager::JumpStart() which is the main deletion path.
    time_t now = time(nullptr);
    bool anyRemoved = false;

    for (auto it = s_searches.begin(); it != s_searches.end();) {
        Search* search = it->second;
        bool del = false;
        bool stop = false;

        switch (search->getSearchType()) {
        case SearchType::File:
            if (now >= search->m_created + kSearchFileLifetime)
                del = true;
            else if (search->getAnswers() >= kSearchFileTotal
                     || now >= search->m_created + kSearchFileLifetime - 20)
                stop = true;
            break;
        case SearchType::Keyword:
            if (now >= search->m_created + kSearchKeywordLifetime)
                del = true;
            else if (search->getAnswers() >= kSearchKeywordTotal
                     || now >= search->m_created + kSearchKeywordLifetime - 20)
                stop = true;
            break;
        case SearchType::Notes:
            if (now >= search->m_created + kSearchNotesLifetime)
                del = true;
            else if (search->getAnswers() >= kSearchNotesTotal
                     || now >= search->m_created + kSearchNotesLifetime - 20)
                stop = true;
            break;
        case SearchType::FindBuddy:
            if (now >= search->m_created + kSearchFindBuddyLifetime)
                del = true;
            else if (search->getAnswers() >= kSearchFindBuddyTotal
                     || now >= search->m_created + kSearchFindBuddyLifetime - 20)
                stop = true;
            break;
        case SearchType::FindSource:
            if (now >= search->m_created + kSearchFindSourceLifetime)
                del = true;
            else if (search->getAnswers() >= kSearchFindSourceTotal
                     || now >= search->m_created + kSearchFindSourceLifetime - 20)
                stop = true;
            break;
        case SearchType::Node:
        case SearchType::NodeSpecial:
        case SearchType::NodeFwCheckUDP:
            if (now >= search->m_created + kSearchNodeLifetime)
                del = true;
            break;
        case SearchType::NodeComplete:
        {
            // In LAN mode, allow publishing after just 1 response since the
            // network is small and we don't need 10 answers to populate the table.
            const uint32_t minAnswers =
                (Kademlia::instance() && Kademlia::instance()->isRunningInLANMode())
                    ? 1 : kSearchNodeCompTotal;
            if (now >= search->m_created + kSearchNodeLifetime
                || (now >= search->m_created + kSearchNodeCompLifetime
                    && search->getAnswers() >= minAnswers))
            {
                del = true;
                // Tell Kad that it can start publishing.
                // Matches MFC SearchManager.cpp:315
                if (auto* prefs = Kademlia::getInstancePrefs())
                    prefs->setPublish(true);
            }
            break;
        }
        case SearchType::StoreFile:
            if (now >= search->m_created + kSearchStoreFileLifetime)
                del = true;
            else if (search->getAnswers() >= kSearchStoreFileTotal
                     || now >= search->m_created + kSearchStoreFileLifetime - 20)
                stop = true;
            break;
        case SearchType::StoreKeyword:
            if (now >= search->m_created + kSearchStoreKeywordLifetime)
                del = true;
            else if (search->getAnswers() >= kSearchStoreKeywordTotal
                     || now >= search->m_created + kSearchStoreKeywordLifetime - 20)
                stop = true;
            break;
        case SearchType::StoreNotes:
            if (now >= search->m_created + kSearchStoreNotesLifetime)
                del = true;
            else if (search->getAnswers() >= kSearchStoreNotesTotal
                     || now >= search->m_created + kSearchStoreNotesLifetime - 20)
                stop = true;
            break;
        default:
            if (now >= search->m_created + kSearchLifetime)
                del = true;
        }

        if (del) {
            delete search;
            it = s_searches.erase(it);
            anyRemoved = true;
        } else {
            if (stop)
                search->prepareToStop();
            else
                search->jumpStart();
            ++it;
        }
    }

    if (anyRemoved)
        notifySearchesChanged();
}

void SearchManager::notifySearchesChanged()
{
    if (auto* kad = Kademlia::instance())
        emit kad->searchesChanged();
}

} // namespace eMule::kad
