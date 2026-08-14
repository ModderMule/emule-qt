#pragma once

/// @file PushCoalescer.h
/// @brief Rate limiter that caps how often a given push event is broadcast.
///
/// The daemon turns core signals into push events one-for-one, and several of
/// those signals fire per *item*: one per arriving search result, one per source
/// added to a download, one per file visited by a shared-directory scan. A client
/// that answers a push by refetching the affected list is then quadratic in the
/// item count — a 1262-result Kad search produced 1500 pushes and 363 MB of CBOR
/// in ten seconds, which starved every unrelated reply on the same socket.
///
/// Clients can defend themselves, but only one at a time and only after someone
/// notices. Capping the rate here protects every current and future IPC consumer.

#include "IpcMessage.h"

#include <QHash>
#include <QObject>

#include <functional>

class QTimer;

namespace eMule::Ipc {

/// Leading-edge fixed-window rate limiter for push broadcasts.
///
/// The first event for a key goes out immediately, so an isolated user action
/// keeps its zero added latency. Events arriving inside the window are suppressed
/// and replaced by a single trailing push when it closes.
///
/// Deliberately *not* a restart-on-every-event debounce: that would never fire at
/// all during a continuous burst, which is precisely when the client needs data.
class PushCoalescer : public QObject {
    Q_OBJECT

public:
    /// Builds the message at send time rather than at post time. Snapshot pushes
    /// therefore carry the freshest state, and a suppressed event costs nothing to
    /// build. A builder must be pure — it is not guaranteed to run.
    using Builder = std::function<IpcMessage()>;

    explicit PushCoalescer(QObject* parent = nullptr);
    ~PushCoalescer() override;

    /// Offer an event for broadcast.
    ///
    /// @param subKey  second half of the coalescing identity. PushSearchResult
    ///                passes the searchID so two search tabs do not share a window;
    ///                pushes with no natural key leave it at 0. 32 bits wide so it
    ///                packs with the message type into an exact, collision-free key.
    /// @param windowMs  minimum spacing between two sends of the same key.
    ///                  0 disables coalescing for this call.
    void post(IpcMsgType type, const Builder& build, int windowMs, quint32 subKey = 0);

    /// Number of keys currently holding an open window. Zero when idle — an idle
    /// daemon must own no timers at all, so the test asserts this drains.
    [[nodiscard]] int activeWindows() const;

signals:
    /// A message is due for broadcast.
    void ready(const eMule::Ipc::IpcMessage& msg);

private:
    struct Window {
        QTimer* timer = nullptr;
        Builder pending;      ///< empty when nothing arrived during the window
        int windowMs = 0;
    };

    [[nodiscard]] static quint64 makeKey(IpcMsgType type, quint32 subKey);

    void onWindowElapsed(quint64 key);

    QHash<quint64, Window> m_windows;
};

} // namespace eMule::Ipc
