/// @file DeferredRequests.cpp
/// @brief Requests held while the core cannot answer them yet — implementation.

#include "DeferredRequests.h"

#include <utility>

namespace eMule::Ipc {

std::optional<IpcMessage> DeferredRequests::hold(const IpcMessage& msg)
{
    if (!isReadOnlyRequest(msg.type())) {
        if (m_live >= kMaxHeld)
            return msg;
        m_held.emplace_back(Held{msg, {}});
        ++m_live;
        return std::nullopt;
    }

    const QByteArray identity = requestIdentity(msg);
    const auto it = m_readOnly.constFind(identity);
    if (it == m_readOnly.constEnd()) {
        if (m_live >= kMaxHeld)
            return msg;
        m_readOnly.insert(identity, static_cast<qsizetype>(m_held.size()));
        m_held.emplace_back(Held{msg, {}});
        ++m_live;
        return std::nullopt;
    }

    // The newer one takes the later slot: a write held between the two must still
    // run before the read that follows it. The older one gets a copy of its answer.
    Held older = *std::exchange(m_held[static_cast<std::size_t>(*it)], std::nullopt);
    std::vector<int> sharers = std::move(older.alsoAnswers);
    sharers.push_back(older.msg.seqId());
    std::optional<IpcMessage> refused;
    if (static_cast<int>(sharers.size()) > kMaxSharers) {
        refused = older.msg.withSeqId(sharers.front());
        sharers.erase(sharers.begin());
    }
    m_readOnly.insert(identity, static_cast<qsizetype>(m_held.size()));
    m_held.emplace_back(Held{msg, std::move(sharers)});

    // The dead slots of a client repeating itself must not pile up either
    if (static_cast<int>(m_held.size()) - m_live > kMaxHeld)
        compact();
    return refused;
}

std::vector<DeferredRequests::Held> DeferredRequests::take()
{
    std::vector<Held> result;
    result.reserve(static_cast<std::size_t>(m_live));
    for (auto& held : m_held) {
        if (held)
            result.push_back(std::move(*held));
    }
    m_held.clear();
    m_readOnly.clear();
    m_live = 0;
    return result;
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void DeferredRequests::compact()
{
    std::erase_if(m_held, [](const auto& held) { return !held.has_value(); });
    m_readOnly.clear();
    for (std::size_t i = 0; i < m_held.size(); ++i) {
        if (isReadOnlyRequest(m_held[i]->msg.type()))
            m_readOnly.insert(requestIdentity(m_held[i]->msg), static_cast<qsizetype>(i));
    }
}

} // namespace eMule::Ipc
