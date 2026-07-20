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

#include <chrono>
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

    /// Everything the KADEMLIA2_PUBLISH_SOURCE_REQ tag set depends on, passed
    /// explicitly rather than read from app singletons so the builder can be
    /// unit tested.
    struct SourcePublishParams {
        bool    firewalled        = false;
        bool    directUDPCallback = false;  ///< firewalled but reachable by direct UDP callback
        bool    hasBuddy          = false;
        uint32  buddyIP           = 0;
        uint16  buddyUDPPort      = 0;      ///< MFC Search.cpp:668 GetUDPPort() — NOT the ED2K TCP port
        UInt128 buddyHash;
        uint16  tcpPort           = 0;
        uint16  internKadPort     = 0;
        bool    useExternKadPort  = true;
        bool    largeFile         = false;
        bool    hasFileSize       = false;
        uint64  fileSize          = 0;
        uint8   cryptOptions      = 0;
    };

    /// Build the source-publish tag list. Sets @p outCanPublish to false when we
    /// are firewalled with neither a direct UDP callback nor a buddy, in which
    /// case there is nothing worth publishing.
    [[nodiscard]] static std::vector<Tag> buildSourcePublishTags(const SourcePublishParams& params,
                                                                 bool& outCanPublish);
    [[nodiscard]] bool stopping() const { return m_stopping; }
    void updateNodeLoad(uint8 load);

    [[nodiscard]] KadClientSearcher* getNodeSpecialSearchRequester() const { return m_nodeSpecialSearchRequester; }
    void setNodeSpecialSearchRequester(KadClientSearcher* requester) { m_nodeSpecialSearchRequester = requester; }
    [[nodiscard]] LookupHistory* getLookupHistory() const { return m_lookupHistory.get(); }

    // Ownership bookkeeping — exposed for tests asserting no leaks / no double-pins.
    [[nodiscard]] std::size_t deleteListSize() const { return m_deleteList.size(); }
    [[nodiscard]] std::size_t inUseCount() const { return m_inUse.size(); }

private:
    void go(uint32 maxToSend = kAlphaQuery);
    void processResponse(uint32 fromIP, uint16 fromPort, const ContactArray& results);
    void processResult(const UInt128& answer, TagList& info, uint32 fromIP, uint16 fromPort);
    void processResultFile(const UInt128& answer, TagList& info);
    void processResultKeyword(const UInt128& answer, TagList& info, uint32 fromIP, uint16 fromPort);
    void processResultNotes(const UInt128& answer, TagList& info);
    void jumpStart();
    /// Pin routing-zone contacts fetched with setInUse=true into m_inUse so the
    /// zone cannot free them mid-search. Drops the extra reference when a
    /// contact is already pinned (go() may re-seed m_possible repeatedly).
    void pinFetchedContacts(const ContactMap& fetched);
    void sendFindValue(Contact* contact, bool reAskMore = false);
    void prepareToStop();
    void storePacket();
    [[nodiscard]] uint8 getRequestContactCount() const;
    [[nodiscard]] uint32 getLifetime() const;

    WordList m_words;
    UIntList m_fileIDs;
    std::map<UInt128, bool> m_responded;  // distance → provided closer contacts (MFC m_mapResponded)
    ContactMap m_possible;  // untried candidates, sorted by distance
    ContactMap m_tried;     // ALL contacted nodes (responded + not), sorted by distance
    ContactMap m_best;      // top ALPHA_QUERY closest contacts for auto-query (MFC m_mapBest)
    ContactMap m_inUse;     // routing-zone contacts pinned via incUse(), keyed by distance (MFC m_mapInUse)
    ContactArray m_deleteList;  // listener-allocated result contacts we own (MFC m_listDelete)
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
    std::map<UInt128, std::chrono::steady_clock::time_point> m_requestSentTimes;
};

} // namespace eMule::kad
