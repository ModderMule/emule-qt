/// @file DeferredRequests.cpp
/// @brief Requests held while the core cannot answer them yet — implementation.

#include "DeferredRequests.h"

#include <utility>

namespace eMule::Ipc {

std::optional<IpcMessage> DeferredRequests::hold(const IpcMessage& msg)
{
    std::optional<IpcMessage> refused;

    if (isReadOnlyRequest(msg.type())) {
        const QByteArray identity = requestIdentity(msg);
        if (const auto it = m_readOnly.constFind(identity); it != m_readOnly.constEnd()) {
            // The newer one takes the later slot: a write held between the two must
            // still run before the read that follows it.
            refused = std::exchange(m_held[static_cast<std::size_t>(*it)], std::nullopt);
            --m_live;
            // The dead slots of a client repeating itself must not pile up either
            if (static_cast<int>(m_held.size()) - m_live > kMaxHeld)
                compact();
        } else if (m_live >= kMaxHeld) {
            return msg;
        }
        m_readOnly.insert(identity, static_cast<qsizetype>(m_held.size()));
    } else if (m_live >= kMaxHeld) {
        return msg;
    }

    m_held.emplace_back(msg);
    ++m_live;
    return refused;
}

std::vector<IpcMessage> DeferredRequests::take()
{
    std::vector<IpcMessage> result;
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
        if (isReadOnlyRequest(m_held[i]->type()))
            m_readOnly.insert(requestIdentity(*m_held[i]), static_cast<qsizetype>(i));
    }
}

} // namespace eMule::Ipc
