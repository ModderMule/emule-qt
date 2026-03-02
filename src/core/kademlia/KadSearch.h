#pragma once

/// @file KadSearch.h
/// @brief Kademlia search state machine (ported from kademlia/kademlia/Search.h).

#include "kademlia/KadDefines.h"
#include "kademlia/KadSearchDefs.h"
#include "kademlia/KadTypes.h"
#include "kademlia/KadUInt128.h"
#include "utils/SafeFile.h"
#include "utils/Types.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <ctime>
#include <map>
#include <memory>

namespace eMule {
class KnownFile;
} // namespace eMule

namespace eMule::kad {

class Contact;
class LookupHistory;
class KadClientSearcher;

/// Core DHT search state machine.
class Search {
    friend class SearchManager;

public:
    Search();
    ~Search();
    Search(const Search&) = delete;
    Search& operator=(const Search&) = delete;

    [[nodiscard]] uint32 getSearchID() const { return m_searchID; }
    void setSearchID(uint32 id) { m_searchID = id; }
    [[nodiscard]] SearchType getSearchType() const { return m_type; }
    void setSearchType(SearchType type);
    void setTargetID(const UInt128& target);
    [[nodiscard]] const UInt128& getTarget() const { return m_target; }
    [[nodiscard]] uint32 getAnswers() const { return m_answers; }
    [[nodiscard]] uint32 getKadPacketSent() const { return m_kadPacketSent; }
    [[nodiscard]] uint32 getRequestAnswer() const { return m_totalRequestAnswers; }
    [[nodiscard]] uint32 getNodeLoad() const;
    [[nodiscard]] uint32 getNodeLoadResponse() const { return m_totalLoadResponses; }
    [[nodiscard]] uint32 getNodeLoadTotal() const { return m_totalLoad; }
    [[nodiscard]] const QString& getGUIName() const { return m_guiName; }
    void setGUIName(const QString& name) { m_guiName = name; }
    void setSearchTermData(uint32 size, const uint8* data);
    [[nodiscard]] static QString getTypeName(SearchType type);

    void addFileID(const UInt128& id);
    static void preparePacketForTags(SafeMemFile& io, KnownFile* file, uint8 targetKadVersion);
    [[nodiscard]] bool stopping() const { return m_stopping; }
    void updateNodeLoad(uint8 load);

    [[nodiscard]] KadClientSearcher* getNodeSpecialSearchRequester() const { return m_nodeSpecialSearchRequester; }
    void setNodeSpecialSearchRequester(KadClientSearcher* requester) { m_nodeSpecialSearchRequester = requester; }
    [[nodiscard]] LookupHistory* getLookupHistory() const { return m_lookupHistory.get(); }

private:
    void go(uint32 maxToSend = kAlphaQuery);
    void processResponse(uint32 fromIP, uint16 fromPort, const ContactArray& results);
    void processResult(const UInt128& answer, TagList& info, uint32 fromIP, uint16 fromPort);
    void processResultFile(const UInt128& answer, TagList& info);
    void processResultKeyword(const UInt128& answer, TagList& info, uint32 fromIP, uint16 fromPort);
    void processResultNotes(const UInt128& answer, TagList& info);
    void jumpStart();
    void sendFindValue(Contact* contact, bool reAskMore = false);
    void prepareToStop();
    void storePacket();
    [[nodiscard]] uint8 getRequestContactCount() const;
    [[nodiscard]] uint32 getLifetime() const;

    WordList m_words;
    UIntList m_fileIDs;
    std::map<UInt128, bool> m_responded;
    ContactMap m_possible, m_tried, m_best, m_inUse;
    ContactArray m_deleteList;
    UInt128 m_target;
    UInt128 m_closestDistantFound;
    std::unique_ptr<SearchTerm> m_searchTerm;
    KadClientSearcher* m_nodeSpecialSearchRequester = nullptr;
    std::unique_ptr<LookupHistory> m_lookupHistory;
    Contact* m_requestedMoreNodesContact = nullptr;
    QByteArray m_searchTermsData;
    QString m_guiName;
    time_t m_lastResponse = 0;
    time_t m_created = 0;
    SearchType m_type = SearchType::Node;
    uint32 m_answers = 0;
    uint32 m_totalRequestAnswers = 0;
    uint32 m_kadPacketSent = 0;
    uint32 m_totalLoad = 0;
    uint32 m_totalLoadResponses = 0;
    uint32 m_searchID = 0;
    bool m_stopping = false;
    time_t m_storePhaseStarted = 0;  // When prepareToStop() triggered storePacket()
};

} // namespace eMule::kad
