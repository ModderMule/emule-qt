#pragma once

/// @file KadIncomingLog.h
/// @brief Recent Kad requests other nodes sent us (searches, publishes, lookups, checks),
/// for the Kad window's "Incoming Searches" list. Not MFC.

#include "kademlia/KadUInt128.h"
#include "utils/Types.h"

#include <QMutex>
#include <QString>

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace eMule::kad {

/// What a node asked us. Stable numbers: the IPC sends them.
enum class IncomingType : uint8 {
    Bootstrap = 0,
    NodeLookup = 1,      ///< KADEMLIA2_REQ
    KeywordSearch = 2,
    SourceSearch = 3,
    NotesSearch = 4,
    PublishKeyword = 5,
    PublishSource = 6,
    PublishNotes = 7,
    FirewallCheck = 8,   ///< KADEMLIA_FIREWALLED(2)_REQ
    FindBuddy = 9,
    Callback = 10,
};

/// Why a request got no answer. Stable numbers: the IPC sends them.
enum class IncomingRefusal : uint8 {
    None = 0,
    Malformed = 1,       ///< too short to parse
    Firewalled = 2,      ///< we are firewalled or UDP-unverified
    OwnBuddy = 3,        ///< we use a buddy ourselves
    SlotsFull = 4,       ///< arg: served buddy limit
    Ourselves = 5,
    FirewallCheck = 6,   ///< a firewall check runs with it
    SilentCoolOff = 7,   ///< arg: minutes left
    IpServed = 8,        ///< one served buddy per IP
    KnownClient = 9,     ///< already in our client list
};

/// A contact we handed out in an answer.
struct HandedContact {
    UInt128 id;
    uint32 ip = 0;              ///< host order
    uint16 port = 0;            ///< UDP
    uint8 version = 0;
};

/// One request. Counts -1: not for this type.
struct IncomingRequest {
    uint64 id = 0;              ///< log-wide sequence, set by IncomingLog::add
    uint64 at = 0;              ///< getTickCount()
    IncomingType type = IncomingType::NodeLookup;
    UInt128 target;
    bool hasTarget = false;
    QString name;               ///< search words, file name; empty: unknown
    uint32 ip = 0;              ///< host order
    uint16 port = 0;            ///< UDP
    int32_t contacts = -1;      ///< contacts sent back
    int32_t results = -1;       ///< search results sent back
    int32_t records = -1;       ///< publish: entries offered
    int32_t load = -1;          ///< publish: load answered (0..100)
    int32_t wanted = -1;        ///< node lookup: contacts asked for (KADEMLIA2_REQ type)
    bool answered = false;
    IncomingRefusal refusal = IncomingRefusal::None;
    int32_t refusalArg = 0;
    // for the incoming search graph
    UInt128 own;                ///< our Kad ID when it came
    UInt128 asker;              ///< requester's Kad ID, from the routing table by IP
    bool hasAsker = false;
    uint8 askerVersion = 0;
    std::vector<HandedContact> handed;
};

/// Two rings, so the many node lookups never push out the searches and publishes.
class IncomingLog {
public:
    static constexpr size_t kMaxLookups = 100;
    static constexpr size_t kMaxOther = 400;

    void add(IncomingRequest request);
    /// Newest first.
    [[nodiscard]] std::vector<IncomingRequest> snapshot() const;
    /// Kept requests, without copying them.
    [[nodiscard]] size_t size() const;
    /// The request with IncomingRequest::id @p id, if still kept.
    [[nodiscard]] std::optional<IncomingRequest> find(uint64 id) const;
    void clear();

private:
    mutable QMutex m_mutex;
    uint64 m_nextId = 0;
    std::deque<IncomingRequest> m_lookups;
    std::deque<IncomingRequest> m_other;
};

/// Notes one request on scope exit: unanswered unless answered() was called.
class IncomingNote {
public:
    IncomingNote(IncomingLog& log, IncomingType type, uint32 ip, uint16 port);
    ~IncomingNote();
    IncomingNote(const IncomingNote&) = delete;
    IncomingNote& operator=(const IncomingNote&) = delete;

    IncomingRequest& operator*() { return m_request; }
    IncomingRequest* operator->() { return &m_request; }
    void setTarget(const UInt128& target);
    void answered() { m_request.answered = true; }
    void refuse(IncomingRefusal why, int32_t arg = 0) { m_request.refusal = why; m_request.refusalArg = arg; }
    void handOut(const HandedContact& contact) { m_request.handed.push_back(contact); }

private:
    IncomingLog& m_log;
    IncomingRequest m_request;
};

} // namespace eMule::kad
