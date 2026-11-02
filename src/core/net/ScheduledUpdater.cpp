#include "pch.h"
/// @file ScheduledUpdater.cpp
/// @brief Hourly tick, failure back-off and running download of a scheduled updater.

#include "net/ScheduledUpdater.h"

namespace eMule {

ScheduledUpdater::ScheduledUpdater(QObject* parent)
    : QObject(parent)
{
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, [this] { checkSchedule(); });
}

void ScheduledUpdater::start()
{
    m_timer.start();
    QTimer::singleShot(0, this, [this] { checkSchedule(); });
}

void ScheduledUpdater::stop()
{
    m_timer.stop();
    if (m_job)
        m_job->abort();
}

bool ScheduledUpdater::backingOff() const
{
    return m_lastFailure.isValid()
           && m_lastFailure.secsTo(QDateTime::currentDateTimeUtc()) < kFailureBackoffSecs;
}

void ScheduledUpdater::noteOutcome(bool ok)
{
    m_lastFailure = ok ? QDateTime() : QDateTime::currentDateTimeUtc();
}

} // namespace eMule
