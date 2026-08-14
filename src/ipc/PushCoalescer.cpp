/// @file PushCoalescer.cpp
/// @brief Push broadcast rate limiter — implementation.

#include "PushCoalescer.h"

#include <QTimer>

#include <utility>

namespace eMule::Ipc {

PushCoalescer::PushCoalescer(QObject* parent)
    : QObject(parent)
{
}

PushCoalescer::~PushCoalescer() = default;

void PushCoalescer::post(IpcMsgType type, const Builder& build, int windowMs, quint32 subKey)
{
    if (!build)
        return;

    // A non-positive window means "do not coalesce this one". Pass it through
    // without leaving a window behind, or the next event would be swallowed by a
    // limiter the caller never asked for.
    if (windowMs <= 0) {
        emit ready(build());
        return;
    }

    const quint64 key = makeKey(type, subKey);

    if (auto it = m_windows.find(key); it != m_windows.end()) {
        // Inside the window: remember the newest builder and send nothing. Last one
        // wins, so a snapshot push always reports the state it ends the window in.
        it->pending = build;
        it->windowMs = windowMs;
        return;
    }

    // Leading edge. Open the window *before* emitting: a slot on ready() that posts
    // again — directly, or indirectly through a socket write — must land in the
    // suppression branch above rather than start a second window for the same key.
    Window window;
    window.windowMs = windowMs;
    window.timer = new QTimer(this);
    window.timer->setSingleShot(true);
    window.timer->setInterval(windowMs);
    connect(window.timer, &QTimer::timeout, this, [this, key] { onWindowElapsed(key); });
    m_windows.insert(key, window);
    window.timer->start();

    emit ready(build());
}

int PushCoalescer::activeWindows() const
{
    return static_cast<int>(m_windows.size());
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

quint64 PushCoalescer::makeKey(IpcMsgType type, quint32 subKey)
{
    const auto rawType = static_cast<quint32>(static_cast<int>(type));
    return (static_cast<quint64>(rawType) << 32) | subKey;
}

void PushCoalescer::onWindowElapsed(quint64 key)
{
    auto it = m_windows.find(key);
    if (it == m_windows.end())
        return;

    if (!it->pending) {
        // Nothing arrived during the window. Drop the entry instead of re-arming:
        // an idle daemon must own no timers, otherwise every push type that ever
        // fired leaves a permanent wakeup floor behind it.
        it->timer->deleteLater();
        m_windows.erase(it);
        return;
    }

    const Builder build = std::exchange(it->pending, {});
    it->timer->start(it->windowMs);

    // Nothing may touch `it` past this point — a re-entrant post() can rehash.
    emit ready(build());
}

} // namespace eMule::Ipc
