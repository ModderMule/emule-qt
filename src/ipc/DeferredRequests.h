#pragma once

/// @file DeferredRequests.h
/// @brief Requests held while the core cannot answer them yet.
///
/// A GUI that polls keeps asking for the same list for as long as the core loads.
/// Held blindly, a minute of that is hundreds of identical requests, all answered
/// in one burst the moment loading ends — each a full list the GUI then has to
/// apply. Identical read-only requests are run once, as the newest; the older ones
/// get a copy of its answer (report 26: refused, a one-shot caller never asked again).

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

    /// Older copies of one read answered along with the newest. Past this the oldest
    /// is refused: GUI pollers keep one in flight, so only a flooding client gets here.
    static constexpr int kMaxSharers = 8;

    struct Held {
        IpcMessage msg;
        std::vector<int> alsoAnswers;   ///< seqIds of older identical reads, oldest first
    };

    /// Hold @p msg for replay. Returns the request to refuse right now, if any: the
    /// oldest copy of a read asked more than kMaxSharers + 1 times, or @p msg when full.
    [[nodiscard]] std::optional<IpcMessage> hold(const IpcMessage& msg);

    /// Everything held, in arrival order. Leaves this empty.
    [[nodiscard]] std::vector<Held> take();

    [[nodiscard]] int size() const { return m_live; }

    /// Slots in use, superseded ones included — bounded at 2 * kMaxHeld.
    [[nodiscard]] int slotCount() const { return static_cast<int>(m_held.size()); }

private:
    /// Drop the superseded slots and re-index what is left.
    void compact();

    std::vector<std::optional<Held>> m_held;         ///< nullopt = superseded
    QHash<QByteArray, qsizetype> m_readOnly;         ///< requestIdentity -> index in m_held
    int m_live = 0;
};

} // namespace eMule::Ipc
