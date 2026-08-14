/// @file PanelPoller.cpp
/// @brief Visibility-gated panel refresh driver — implementation.

#include "PanelPoller.h"

#include <QEvent>
#include <QTimer>
#include <QWidget>

namespace eMule {

namespace {

/// Matches IpcClient::LocalPollingMs. Panels override it from
/// IpcClient::pollingInterval() once they have a connection, which is slower for a
/// remote daemon.
constexpr int kDefaultIntervalMs = 500;

} // namespace

PanelPoller::PanelPoller(QWidget* panel, std::function<void()> refresh)
    : QObject(panel)
    , m_panel(panel)
    , m_refresh(std::move(refresh))
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(kDefaultIntervalMs);
    connect(m_timer, &QTimer::timeout, this, [this] {
        if (m_refresh)
            m_refresh();
    });

    if (m_panel)
        m_panel->installEventFilter(this);
}

void PanelPoller::setInterval(int ms)
{
    if (ms <= 0 || ms == m_timer->interval())
        return;

    m_timer->setInterval(ms);
    // QTimer::setInterval on a running timer restarts it, which is what we want:
    // a panel changing its period should not first serve out the old one.
}

int PanelPoller::interval() const
{
    return m_timer->interval();
}

void PanelPoller::setEnabled(bool on)
{
    if (m_enabled == on)
        return;

    m_enabled = on;
    applyState();

    // Coming online should show data now, not one period from now.
    if (m_enabled)
        refreshNow();
}

bool PanelPoller::isPolling() const
{
    return m_timer->isActive();
}

void PanelPoller::nudge()
{
    // Hidden panels ignore pushes outright: nothing is on screen to update, and the
    // refresh on show fetches the current state anyway.
    if (!isPolling() || m_nudgeQueued)
        return;

    m_nudgeQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_nudgeQueued = false;
        refreshNow();
    });
}

void PanelPoller::refreshNow()
{
    if (!m_enabled || !m_panel || !m_panel->isVisible() || !m_refresh)
        return;

    // Restart the period from here so this refresh replaces the next scheduled one
    // rather than adding to it.
    m_timer->start();
    m_refresh();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

bool PanelPoller::eventFilter(QObject* watched, QEvent* event)
{
    // Only the panel's own visibility matters. Qt delivers Show/Hide to a
    // QStackedWidget page as the current index changes, and propagates them down
    // when the main window is hidden to the tray, so this covers both.
    if (watched == m_panel) {
        if (event->type() == QEvent::Show) {
            applyState();
            // Arriving at a tab must show current data, not whatever was on screen
            // when it was last hidden.
            refreshNow();
        } else if (event->type() == QEvent::Hide) {
            applyState();
        }
    }
    return QObject::eventFilter(watched, event);
}

void PanelPoller::applyState()
{
    const bool shouldPoll = m_enabled && m_panel && m_panel->isVisible();
    if (shouldPoll) {
        if (!m_timer->isActive())
            m_timer->start();
    } else {
        m_timer->stop();
    }
}

} // namespace eMule
