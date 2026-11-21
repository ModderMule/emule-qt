#pragma once

/// @file Ed3kUploads.h
/// @brief What the engine's upload queue does, for the GUI's Uploading / On Queue lists
/// (merged into GetUploads by Ed3kUploadsBridge).
///
/// Only observes the queue events (FileRequested, SlotOfferResult, SlotUsed, SlotEnded,
/// QueueEntryEnded): it never marks one handled, the joins and the queue policy answer them.
/// - A node that asked for a file waits; one whose blocks go out (SlotUsed) or that took an
///   offered slot holds a slot. SlotEnded / QueueEntryEnded drop the row.
/// - was: The engine tells no rank and no address: the place is the order of waiting (the
///   policy's score also weighs the file's priority), the address stays empty.
///   was: ToDo: rank and address once the FFI lists the queue (ed3k_queue_list or similar).
///   (done: uploading()/waiting() read Engine::queueList: the engine's rank as the place, the
///   endpoint, since when, the turn's bytes, connected; the events add name, LAN and due.
///   Without an engine the event rows, place by waiting time.)
/// - Both nodes (hops.md §1): the anonymous node's queue too while hiding (queueList(Anon),
///   keyed events); its rows carry KeyKind::Anon and never an address.

#include "Ed3kFfi.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <cstdint>
#include <deque>
#include <functional>
#include <array>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace eMule::ed3k {

class Ed3kSession;

class Ed3kUploads : public QObject {
    Q_OBJECT

public:
    /// One node of the queue.
    struct Row {
        ::ed3k::NodeId node{};
        ::ed3k::Root root{};
        ::ed3k::KeyKind key = ::ed3k::KeyKind::Node;   ///< the queue of this node: Anon shows no address
        QString fileName;            ///< empty: not known (the name lookup knows no such root)
        std::uint64_t fileSize = 0;
        QString address;             ///< literal IP; empty: not known (relays only)
        std::uint16_t port = 0;
        bool connected = true;       ///< a file stream open (else sent away / not connected)
        bool holding = false;        ///< holds a slot (else waits)
        bool lan = false;
        bool due = false;            ///< the turn reached 16 MiB / 1 h (SlotUsed due)
        std::uint64_t bytes = 0;     ///< file bytes of the running turn
        std::uint64_t rate = 0;      ///< upload speed of the running turn, B/s (uploading() only)
        qint64 since = 0;            ///< ms since the epoch: entered the queue, or got the slot
        int place = 0;               ///< waiting: the engine's rank (else 1-based by waiting time); holding: 0
        QString agent;               ///< Hello.agent ("eMuleQt 0.7.0"); empty: not known
        QString nick;                ///< Hello.nick (the peer's user name); may be empty
        std::uint64_t caps = 0;      ///< Hello.caps (§2)
        QByteArray have;             ///< downloader's FileRequest.have (§12.1), LSB first, missing bit = not held
        bool haveKnown = false;      ///< a have told on this stream
        std::optional<std::uint64_t> nextOffset;   ///< holding: offset of the block it gets next
        std::vector<std::uint64_t> sent;           ///< holding: offsets of the blocks sent on its stream, oldest first
    };
    /// name and size of a shared root; nullopt: not known.
    using NameLookup = std::function<std::optional<std::pair<QString, std::uint64_t>>(const ::ed3k::Root&)>;

    explicit Ed3kUploads(Ed3kSession& session, QObject* parent = nullptr);
    ~Ed3kUploads() override;

    /// Names files by root (DaemonApp: Ed3kShared); none: names stay empty.
    void setNameLookup(NameLookup lookup) { m_names = std::move(lookup); }
    [[nodiscard]] Ed3kSession& session() const { return m_session; }

    [[nodiscard]] std::vector<Row> uploading() const;
    /// Oldest first; place set.
    [[nodiscard]] std::vector<Row> waiting() const;
    [[nodiscard]] std::size_t size() const { return m_rows.size(); }
    /// A node whose ID starts with @p prefix waits or holds a slot at the node key (ed3k §14.2 hint).
    /// File bytes with @p node since the daemon started: sent in finished turns, received.
    [[nodiscard]] std::pair<std::uint64_t, std::uint64_t> peerBytes(::ed3k::KeyKind key, const ::ed3k::NodeId& node) const;
    /// Verified blocks of @p root from the peer (Ed3kSession::peerFileDown).
    [[nodiscard]] std::uint64_t peerFileDown(::ed3k::KeyKind key, const ::ed3k::NodeId& node, const ::ed3k::Root& root) const;
    /// Download speed from @p node in B/s (10 s average).
    [[nodiscard]] std::uint64_t downRate(::ed3k::KeyKind key, const ::ed3k::NodeId& node) const;
    [[nodiscard]] bool hasNodeStarting(const std::array<std::uint8_t, 12>& prefix) const;

signals:
    /// A row was added, changed or dropped.
    void changed();

private:
    /// The engine's queue (queueList) merged with what the events told; nullopt: no engine.
    [[nodiscard]] std::optional<std::vector<Row>> listed() const;
    /// queueList of one node's queue, merged with the event rows of that key. false: no list.
    bool appendListed(::ed3k::KeyKind key, std::vector<Row>& out) const;
    void onEventPolled(const ::ed3k::Event& event, ::ed3k::KeyKind key, bool* handled);
    void onSessionStatus();
    [[nodiscard]] static qint64 nowMs();
    /// The turn's bytes over the last 10 s (the GUI polls about once a second); 0 at first.
    [[nodiscard]] std::uint64_t sampleUpRate(const Row& row) const;

    Ed3kSession& m_session;
    NameLookup m_names;
    /// by (key, node): the two nodes' queues may hold the same downloader ID
    std::map<std::pair<::ed3k::KeyKind, ::ed3k::NodeId>, Row> m_rows;
    /// sampleUpRate(): (ms, turn bytes) per holder, and when it was last asked
    struct UpSamples {
        std::deque<std::pair<qint64, std::uint64_t>> at;
        qint64 asked = 0;
    };
    mutable std::map<std::pair<::ed3k::KeyKind, ::ed3k::NodeId>, UpSamples> m_upSamples;
    mutable QElapsedTimer m_upClock;
};

} // namespace eMule::ed3k
