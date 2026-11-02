#pragma once

/// @file ScheduledUpdater.h
/// @brief Base of the core's scheduled list downloads (IP filter, country database).
///
/// Holds what they share: an hourly tick that asks checkSchedule() whether a run
/// is due, the 6 h back-off after a failed run, and the running download.

#include "net/HttpFileDownload.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QTimer>

namespace eMule {

class ScheduledUpdater : public QObject {
    Q_OBJECT

public:
    explicit ScheduledUpdater(QObject* parent = nullptr);

    /// Arm the hourly tick; checks once right away.
    virtual void start();
    /// Stop the tick and abort a running download (its callback still fires).
    virtual void stop();

    [[nodiscard]] bool isRunning() const { return m_job && !m_job->isFinished(); }

    /// Run when due and not backing off.
    virtual void checkSchedule() = 0;

protected:
    /// A failed run blocks automatic runs for kFailureBackoffSecs.
    [[nodiscard]] bool backingOff() const;
    void noteOutcome(bool ok);
    void clearBackoff() { m_lastFailure = {}; }

    static constexpr int kTickMs = 60 * 60 * 1000;
    static constexpr qint64 kFailureBackoffSecs = 6 * 60 * 60;

    /// The running download, from HttpFileDownload::fetch(this, ...).
    QPointer<HttpDownloadJob> m_job;

private:
    QTimer m_timer;
    QDateTime m_lastFailure;
};

} // namespace eMule
