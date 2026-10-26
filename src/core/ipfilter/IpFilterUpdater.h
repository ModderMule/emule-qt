#pragma once

/// @file IpFilterUpdater.h
/// @brief Downloads ipfilter.dat from the user's update URL, on demand and on a schedule.
///
/// The daemon owns the file, so the download runs here and not in the GUI: a GUI
/// attached to a remote core would otherwise write the list to its own disk.
///
/// Schedule: an hourly tick updates when automatic updates are on and the last
/// successful update is ipFilterUpdateDays old. A failure blocks retries for 6 h.

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <vector>

namespace eMule {

class IPFilter;

class IpFilterUpdater : public QObject {
    Q_OBJECT

public:
    /// @p entries is the number of ranges loaded; 0 on failure.
    using Callback = std::function<void(bool ok, int entries, const QString& message)>;

    IpFilterUpdater(IPFilter* filter, const QString& configDir, QObject* parent = nullptr);

    /// Arm the schedule; checks once right away.
    void start();
    void stop();

    /// Download now, ignoring the schedule and the failure back-off. @p url empty =
    /// the configured update URL. @p done fires once with the outcome.
    void updateNow(const QString& url = {}, Callback done = {});

    [[nodiscard]] bool isRunning() const { return m_running; }

    /// Update when automatic updates are on, the last one is due and not backing off.
    void checkSchedule();

signals:
    void updateFinished(bool ok, int entries, const QString& message);

private:
    void finish(bool ok, int entries, const QString& message);

    IPFilter* m_filter = nullptr;
    QString m_configDir;
    QTimer m_timer;
    std::vector<Callback> m_waiting;
    QDateTime m_lastFailure;
    bool m_running = false;
};

} // namespace eMule
