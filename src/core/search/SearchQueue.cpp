#include "pch.h"
/// @file SearchQueue.cpp
/// @brief Search queue — implementation.

#include "search/SearchQueue.h"

#include "utils/Log.h"

#include <QCoreApplication>

#include <algorithm>

namespace eMule {

SearchQueue::SearchQueue(SearchQueueBackend backend, QObject* parent)
    : QObject(parent)
    , m_backend(std::move(backend))
{
    qRegisterMetaType<eMule::SearchStatus>();
}

// ---------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------

SearchQueue::Result SearchQueue::enqueue(const SearchParams& params)
{
    Result result;
    if (const QString error = m_backend.validate(params); !error.isEmpty()) {
        result.error = error;
        return result;
    }

    // The same search asked for twice is one search.
    const QString key = dedupKeyFor(params);
    for (const Entry& entry : m_entries) {
        if (entry.dedupKey == key && (entry.status.state == SearchRunState::Queued
                                      || entry.status.state == SearchRunState::Running)) {
            result.ok = true;
            result.duplicate = true;
            result.status = entry.status;
            return result;
        }
    }

    if (queuedCount() >= kMaxQueued) {
        result.error = QCoreApplication::translate(
            "eMule::IpcClientHandler",
            "Too many searches are waiting to be sent (%1). Close some of them first.")
                           .arg(kMaxQueued);
        return result;
    }

    Entry entry;
    entry.params = params;
    entry.dedupKey = key;
    entry.queuedAtMs = m_backend.nowMs();
    entry.status.searchID = m_backend.create(params);
    entry.status.type = params.type;
    entry.status.state = SearchRunState::Queued;
    const uint32 searchID = entry.status.searchID;
    m_entries.push_back(std::move(entry));

    pump();

    // pump() may have sent it, failed it, or left it waiting with a reason.
    result.ok = true;
    if (const Entry* now = find(searchID)) {
        result.status = now->status;
        if (now->status.state == SearchRunState::Failed) {
            // Refused on the first try: as if it had never been taken.
            result.ok = false;
            result.error = now->status.error;
            std::erase_if(m_entries, [searchID](const Entry& e) {
                return e.status.searchID == searchID;
            });
            m_backend.discard(searchID);
        }
    }
    return result;
}

void SearchQueue::pump()
{
    if (m_pumping)
        return;   // a dispatch reported back while we were walking the queue
    m_pumping = true;

    // First come, first sent — within a lane. A server search that has to wait keeps
    // the ones behind it waiting too; Kad searches do not hold each other up.
    bool serverLaneHeld = m_inFlight != 0;
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].status.state != SearchRunState::Queued)
            continue;

        const std::optional<SearchType> type = m_backend.resolve(m_entries[i].params);
        if (!type) {
            setWaiting(m_entries[i], SearchWait::Connection);
            continue;
        }
        const bool server = isServerType(*type);
        if (server && serverLaneHeld) {
            setWaiting(m_entries[i], SearchWait::PreviousSearch);
            continue;
        }
        if (const QString reason = m_backend.waitReason(*type); !reason.isEmpty()) {
            setWaiting(m_entries[i], reason.toLatin1().constData());
            serverLaneHeld = serverLaneHeld || server;
            continue;
        }

        const uint32 searchID = m_entries[i].status.searchID;
        const SearchDispatch sent = m_backend.dispatch(searchID, *type, m_entries[i].params);
        Entry& entry = m_entries[i];   // dispatch() does not add or remove entries
        switch (sent.outcome) {
        case SearchDispatch::Outcome::Sent:
            entry.status.type = *type;
            entry.status.keyword = sent.keyword;
            entry.status.primaryKeyword = sent.primaryKeyword;
            entry.kad = !server;
            entry.awaitsSweep = sent.awaitsSweep;
            if (server) {
                m_inFlight = searchID;
                m_inFlightDeadlineMs = m_backend.nowMs()
                                     + (sent.awaitsSweep ? kSweepTimeoutMs : kAnswerTimeoutMs);
                serverLaneHeld = true;
            }
            setState(entry, SearchRunState::Running);
            break;
        case SearchDispatch::Outcome::SendFailed:
            sendFailed(entry);
            serverLaneHeld = serverLaneHeld || server;
            break;
        case SearchDispatch::Outcome::Busy:
            setWaiting(entry, SearchWait::PreviousSearch);
            serverLaneHeld = serverLaneHeld || server;
            break;
        case SearchDispatch::Outcome::Refused:
            setState(entry, SearchRunState::Failed, sent.error);
            break;
        }
    }
    m_pumping = false;
}

void SearchQueue::tick()
{
    const qint64 now = m_backend.nowMs();

    if (m_inFlight != 0 && now >= m_inFlightDeadlineMs)
        finishServerSearch(SearchRunState::Finished);   // no answer is an answer too

    for (Entry& entry : m_entries) {
        if (entry.status.state == SearchRunState::Running && entry.kad
            && !m_backend.kadSearchAlive(entry.status.searchID)) {
            setState(entry, SearchRunState::Finished);
        } else if (entry.status.state == SearchRunState::Queued
                   && now - entry.queuedAtMs >= kMaxWaitMs) {
            setState(entry, SearchRunState::Failed,
                     QCoreApplication::translate(
                         "eMule::IpcClientHandler",
                         "The search could not be sent within %1 minutes.")
                         .arg(kMaxWaitMs / 60000));
        }
    }
    pump();
}

void SearchQueue::onServerDisconnected()
{
    Entry* entry = find(m_inFlight);
    if (!entry || entry->awaitsSweep)
        return;   // a sweep goes on without the server, and ends by itself

    // The answer was due on the session that just went: ask again on the next one.
    m_backend.endServerSearch(m_inFlight);
    m_inFlight = 0;
    sendFailed(*entry);
    pump();
}

void SearchQueue::onServerAnswer()
{
    const Entry* entry = find(m_inFlight);
    if (!entry || entry->awaitsSweep)
        return;   // a global search carries on over UDP
    finishServerSearch(SearchRunState::Finished);
}

void SearchQueue::onSweepFinished(uint32 searchID)
{
    if (searchID != 0 && searchID == m_inFlight)
        finishServerSearch(SearchRunState::Finished);
}

void SearchQueue::stop(uint32 searchID)
{
    Entry* entry = find(searchID);
    if (!entry)
        return;
    if (searchID == m_inFlight) {
        finishServerSearch(SearchRunState::Finished);
        return;
    }
    if (entry->status.state == SearchRunState::Queued
        || entry->status.state == SearchRunState::Running) {
        setState(*entry, SearchRunState::Finished);
        pump();   // a search held up behind it may go now
    }
}

void SearchQueue::remove(uint32 searchID)
{
    const auto it = std::ranges::find_if(m_entries, [searchID](const Entry& e) {
        return e.status.searchID == searchID;
    });
    if (it == m_entries.end())
        return;
    const bool wasInFlight = searchID == m_inFlight;
    m_entries.erase(it);
    if (wasInFlight) {
        m_backend.endServerSearch(searchID);
        m_inFlight = 0;
    }
    pump();
}

void SearchQueue::clear()
{
    if (m_inFlight != 0)
        m_backend.endServerSearch(m_inFlight);
    m_inFlight = 0;
    m_entries.clear();
}

std::optional<SearchStatus> SearchQueue::status(uint32 searchID) const
{
    const Entry* entry = find(searchID);
    return entry ? std::optional(entry->status) : std::nullopt;
}

int SearchQueue::queuedCount() const
{
    return static_cast<int>(std::ranges::count_if(m_entries, [](const Entry& e) {
        return e.status.state == SearchRunState::Queued;
    }));
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

SearchQueue::Entry* SearchQueue::find(uint32 searchID)
{
    if (searchID == 0)
        return nullptr;
    const auto it = std::ranges::find_if(m_entries, [searchID](const Entry& e) {
        return e.status.searchID == searchID;
    });
    return it == m_entries.end() ? nullptr : &*it;
}

const SearchQueue::Entry* SearchQueue::find(uint32 searchID) const
{
    return const_cast<SearchQueue*>(this)->find(searchID);
}

QString SearchQueue::dedupKeyFor(const SearchParams& p)
{
    // Everything that makes it a different question; the words case- and
    // space-insensitively.
    const QChar sep(u'\x1F');
    return QString::number(static_cast<int>(p.type)) + sep
         + p.expression.simplified().toLower() + sep + p.fileType.toLower() + sep
         + QString::number(p.minSize) + sep + QString::number(p.maxSize) + sep
         + QString::number(p.availability) + sep + p.extension.toLower() + sep
         + QString::number(p.completeSources) + sep + p.codec.toLower() + sep
         + QString::number(p.minBitrate) + sep + QString::number(p.minLength) + sep
         + p.title.toLower() + sep + p.album.toLower() + sep + p.artist.toLower();
}

bool SearchQueue::isServerType(SearchType type)
{
    return type == SearchType::Ed2kServer || type == SearchType::Ed2kGlobal;
}

void SearchQueue::setWaiting(Entry& entry, const char* reason)
{
    const QString text = QString::fromLatin1(reason);
    if (entry.status.state == SearchRunState::Queued && entry.status.reason == text)
        return;
    entry.status.state = SearchRunState::Queued;
    entry.status.reason = text;
    emit stateChanged(entry.status);
}

void SearchQueue::setState(Entry& entry, SearchRunState state, const QString& error)
{
    entry.status.state = state;
    entry.status.reason.clear();
    entry.status.error = error;
    logServerVerbose(QStringLiteral("Search %1 \"%2\": %3%4")
                         .arg(entry.status.searchID)
                         .arg(entry.params.expression)
                         .arg(state == SearchRunState::Running ? QStringLiteral("running")
                              : state == SearchRunState::Finished ? QStringLiteral("finished")
                              : state == SearchRunState::Failed ? QStringLiteral("failed")
                              : QStringLiteral("queued"))
                         .arg(error.isEmpty() ? QString() : QStringLiteral(" — ") + error));
    emit stateChanged(entry.status);
}

void SearchQueue::finishServerSearch(SearchRunState state, const QString& error)
{
    const uint32 searchID = std::exchange(m_inFlight, 0);
    if (searchID == 0)
        return;
    m_backend.endServerSearch(searchID);
    if (Entry* entry = find(searchID))
        setState(*entry, state, error);
    pump();
}

void SearchQueue::sendFailed(Entry& entry)
{
    if (++entry.sendRetries > kMaxSendRetries) {
        setState(entry, SearchRunState::Failed,
                 QCoreApplication::translate(
                     "eMule::IpcClientHandler",
                     "The search could not be sent: the server connection kept dropping."));
        return;
    }
    setWaiting(entry, SearchWait::ServerConnection);
}

} // namespace eMule
