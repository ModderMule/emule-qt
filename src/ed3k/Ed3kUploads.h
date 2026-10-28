#pragma once

/// @file Ed3kUploads.h
/// @brief What the engine's upload queue does, for the GUI's Uploading / On Queue lists
/// (merged into GetUploads by Ed3kUploadsBridge).
///
/// Only observes the queue events (FileRequested, SlotOfferResult, SlotUsed, SlotEnded,
/// QueueEntryEnded): it never marks one handled, the joins and the queue policy answer them.
/// - A node that asked for a file waits; one whose blocks go out (SlotUsed) or that took an
///   offered slot holds a slot. SlotEnded / QueueEntryEnded drop the row.
/// - The engine tells no rank and no address: the place is the order of waiting (the
///   policy's score also weighs the file's priority), the address stays empty.
///   ToDo: rank and address once the FFI lists the queue (ed3k_queue_list or similar).

#include "Ed3kFfi.h"

#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
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
        QString fileName;            ///< empty: not known (the name lookup knows no such root)
        std::uint64_t fileSize = 0;
        QString address;             ///< empty: not known
        bool holding = false;        ///< holds a slot (else waits)
        bool lan = false;
        bool due = false;            ///< the turn reached 16 MiB / 1 h (SlotUsed due)
        std::uint64_t bytes = 0;     ///< file bytes of the running turn
        qint64 since = 0;            ///< ms since the epoch: entered the queue, or got the slot
        int place = 0;               ///< waiting: 1-based place by waiting time; holding: 0
    };
    /// name and size of a shared root; nullopt: not known.
    using NameLookup = std::function<std::optional<std::pair<QString, std::uint64_t>>(const ::ed3k::Root&)>;

    explicit Ed3kUploads(Ed3kSession& session, QObject* parent = nullptr);
    ~Ed3kUploads() override;

    /// Names files by root (DaemonApp: Ed3kShared); none: names stay empty.
    void setNameLookup(NameLookup lookup) { m_names = std::move(lookup); }

    [[nodiscard]] std::vector<Row> uploading() const;
    /// Oldest first; place set.
    [[nodiscard]] std::vector<Row> waiting() const;
    [[nodiscard]] std::size_t size() const { return m_rows.size(); }

signals:
    /// A row was added, changed or dropped.
    void changed();

private:
    void onEventPolled(const ::ed3k::Event& event, bool* handled);
    void onSessionStatus();
    [[nodiscard]] static qint64 nowMs();

    Ed3kSession& m_session;
    NameLookup m_names;
    std::map<::ed3k::NodeId, Row> m_rows;
};

} // namespace eMule::ed3k
