#pragma once

/// @file PanelPoller.h
/// @brief Periodic refetch driver for a panel, gated on whether the panel is visible.
///
/// Every list panel in this GUI refreshes by refetching its whole list over IPC on a
/// timer. Two things were wrong with doing that by hand in each panel:
///
///   - MainWindow keeps the panels in a QStackedWidget, so a panel on an inactive tab
///     is genuinely hidden — but nothing checked, so all of them kept polling. Sitting
///     on the Search tab still shipped the download, upload, client, shared-file and
///     Kad lists twice a second, forever.
///   - Panels that also refetched on a push event opened a second, unbounded refresh
///     path for data the poll was going to fetch anyway.
///
/// This class is the one place both are handled. KadPanel already did the equivalent
/// for its lookup history ("only fetch when the Search Details tab is visible"); this
/// applies the same idea to the panels themselves.

#include <QObject>

#include <functional>

class QTimer;
class QWidget;

namespace eMule {

/// Drives a panel's periodic refetch. Polls only while the panel is on screen, and
/// lets a push event pull the next poll forward instead of opening a second path.
class PanelPoller : public QObject {
    Q_OBJECT

public:
    /// @param panel    the widget whose visibility gates the polling. Also the parent.
    /// @param refresh  the panel's existing refresh routine.
    PanelPoller(QWidget* panel, std::function<void()> refresh);

    /// Polling period in milliseconds. Takes effect on the next tick.
    void setInterval(int ms);
    [[nodiscard]] int interval() const;

    /// Enable or disable polling entirely — panels call this as the IPC connection
    /// comes and goes. Enabling refreshes immediately if the panel is visible.
    void setEnabled(bool on);
    [[nodiscard]] bool isEnabled() const { return m_enabled; }

    /// True when the timer is actually running, i.e. enabled *and* visible.
    [[nodiscard]] bool isPolling() const;

    /// A push event arrived: bring the next refresh forward to now, and restart the
    /// period from here so the push and the poll never both fire for one change.
    /// Collapses to a single refresh per event-loop iteration, and is a no-op while
    /// hidden — the refresh on show covers whatever was missed.
    void nudge();

    /// Refresh now (if visible and enabled) and restart the period.
    void refreshNow();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyState();

    QWidget* m_panel = nullptr;
    std::function<void()> m_refresh;
    QTimer* m_timer = nullptr;
    bool m_enabled = false;
    bool m_nudgeQueued = false;
};

} // namespace eMule
