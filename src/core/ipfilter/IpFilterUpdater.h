#pragma once

/// @file IpFilterUpdater.h
/// @brief Downloads ipfilter.dat from the user's update URL, on demand and on a schedule.
///
/// The daemon owns the file, so the download runs here and not in the GUI: a GUI
/// attached to a remote core would otherwise write the list to its own disk.
///
/// Schedule: an hourly tick checks when automatic updates are on and the last
/// successful check is ipFilterUpdateDays old. A failure blocks retries for 6 h.
/// Scheduled checks are conditional: the ETag / Last-Modified of the last download
/// go back to the server, and an unchanged list (HTTP 304) is not downloaded again.
/// updateNow() always downloads in full: the user asked for it.

#include "net/ScheduledUpdater.h"

#include <QString>

#include <functional>
#include <vector>

namespace eMule {

class IPFilter;

class IpFilterUpdater : public ScheduledUpdater {
    Q_OBJECT

public:
    /// @p entries is the number of ranges loaded; 0 on failure.
    using Callback = std::function<void(bool ok, int entries, const QString& message)>;

    /// Key of the stored validators in Preferences::downloadValidators().
    static constexpr auto kValidatorKey = "ipfilter";

    IpFilterUpdater(IPFilter* filter, const QString& configDir, QObject* parent = nullptr);

    /// Download now, ignoring the schedule and the failure back-off. @p url empty =
    /// the configured update URL. @p done fires once with the outcome.
    void updateNow(const QString& url = {}, Callback done = {});

    /// Check when automatic updates are on, the last one is due and not backing off.
    void checkSchedule() override;

signals:
    void updateFinished(bool ok, int entries, const QString& message);

private:
    void run(const QString& url, bool conditional);
    void onDownloaded(const QString& source, const HttpFileDownload::Result& r);
    void finish(bool ok, int entries, const QString& message);

    IPFilter* m_filter = nullptr;
    QString m_configDir;
    std::vector<Callback> m_waiting;
};

} // namespace eMule
