/// @file Ed3kUploads.cpp
/// @brief The ed3k upload queue as the GUI shows it — implementation.

#include "Ed3kUploads.h"

#include "Ed3kSession.h"

#include "utils/Log.h"

#include <QDateTime>

#include <algorithm>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

Ed3kUploads::Ed3kUploads(Ed3kSession& session, QObject* parent)
    : QObject(parent)
    , m_session(session)
{
    // observe only: the joins and the queue policy answer these events
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kUploads::onEventPolled, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kUploads::onSessionStatus);
}

Ed3kUploads::~Ed3kUploads() = default;

std::vector<Ed3kUploads::Row> Ed3kUploads::uploading() const
{
    std::vector<Row> out;
    for (const auto& [node, row] : m_rows)
        if (row.holding)
            out.push_back(row);
    std::ranges::sort(out, {}, &Row::since);
    return out;
}

std::vector<Ed3kUploads::Row> Ed3kUploads::waiting() const
{
    std::vector<Row> out;
    for (const auto& [node, row] : m_rows)
        if (!row.holding)
            out.push_back(row);
    std::ranges::sort(out, {}, &Row::since);
    int place = 0;
    for (Row& r : out)
        r.place = ++place;
    return out;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kUploads::onEventPolled(const ffi::Event& ev, bool* handled)
{
    Q_UNUSED(handled)   // never set: others answer these
    namespace e = ffi::event;
    if (const auto* r = std::get_if<e::FileRequested>(&ev)) {
        auto [it, added] = m_rows.try_emplace(r->node);
        Row& row = it->second;
        if (added) {
            row.node = r->node;
            row.since = nowMs();
        }
        if (added || row.root != r->root) {
            row.root = r->root;
            row.fileName.clear();
            row.fileSize = 0;
            if (m_names)
                if (const auto named = m_names(r->root)) {
                    row.fileName = named->first;
                    row.fileSize = named->second;
                }
        }
        row.lan = r->lan;
    } else if (const auto* o = std::get_if<e::SlotOfferResult>(&ev)) {
        const auto it = m_rows.find(o->node);
        if (it == m_rows.end() || !o->ok)
            return;   // a failed offer: still waiting
        if (!it->second.holding) {
            it->second.holding = true;
            it->second.since = nowMs();
            it->second.bytes = 0;
        }
    } else if (const auto* u = std::get_if<e::SlotUsed>(&ev)) {
        auto [it, added] = m_rows.try_emplace(u->node);
        Row& row = it->second;
        if (added)
            row.node = u->node;   // a slot of a request not seen (a restart of this view)
        if (!row.holding || added) {
            row.holding = true;
            row.since = nowMs();
        }
        row.bytes = u->bytes;
        row.due = u->due;
    } else if (const auto* se = std::get_if<e::SlotEnded>(&ev)) {
        if (m_rows.erase(se->node) == 0)
            return;
        logEd3kDebug(QStringLiteral("ed3k: upload slot of %1 ended (why %2, %3 B)")
                         .arg(QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(se->node.bytes.data()), 4).toHex()))
                         .arg(int(se->why))
                         .arg(se->bytes));
    } else if (const auto* q = std::get_if<e::QueueEntryEnded>(&ev)) {
        const auto it = m_rows.find(q->node);
        if (it == m_rows.end() || it->second.holding)
            return;   // a holder's end comes as SlotEnded
        m_rows.erase(it);
    } else {
        return;
    }
    emit changed();
}

void Ed3kUploads::onSessionStatus()
{
    if (m_session.isRunning() || m_rows.empty())
        return;
    m_rows.clear();
    emit changed();
}

qint64 Ed3kUploads::nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

} // namespace eMule::ed3k
