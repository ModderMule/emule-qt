#pragma once

/// @file DeferredRequests.h
/// @brief Requests held while the core cannot answer them yet.
///
/// A GUI that polls keeps asking for the same list for as long as the core loads.
/// Held blindly, a minute of that is hundreds of identical requests, all answered
/// in one burst the moment loading ends — each a full list the GUI then has to
/// apply. Only the newest of identical read-only requests is worth an answer.

#include "IpcMessage.h"

#include <QByteArray>
#include <QHash>

#include <optional>
#include <vector>

namespace eMule::Ipc {

class DeferredRequests {
public:
    /// Requests kept at most. A client past this is not waiting, it is flooding.
    static constexpr int kMaxHeld = 2048;

    /// Hold @p msg for replay. Returns the request to refuse right now, if any: an
    /// older read-only request that asks the same thing, or @p msg itself when full.
    [[nodiscard]] std::optional<IpcMessage> hold(const IpcMessage& msg);

    /// Everything held, in arrival order. Leaves this empty.
    [[nodiscard]] std::vector<IpcMessage> take();

    [[nodiscard]] int size() const { return m_live; }

private:
    std::vector<std::optional<IpcMessage>> m_held;   ///< nullopt = superseded
    QHash<QByteArray, qsizetype> m_readOnly;         ///< requestIdentity -> index in m_held
    int m_live = 0;
};

} // namespace eMule::Ipc
