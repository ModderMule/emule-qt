#include "pch.h"
/// @file DiskLoadLimiter.cpp
/// @brief Keeps a hashing read loop from saturating the disk.

#include "utils/DiskLoadLimiter.h"
#include "prefs/Preferences.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <algorithm>

namespace eMule {

namespace {

qint64 monotonicNs()
{
    static const QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.nsecsElapsed();
}

} // namespace

DiskLoadLimiter::DiskLoadLimiter()
    : DiskLoadLimiter(Hooks{})
{
}

DiskLoadLimiter::DiskLoadLimiter(Hooks hooks)
    : m_hooks(std::move(hooks))
{
    if (!m_hooks.nowNs)
        m_hooks.nowNs = &monotonicNs;
    if (!m_hooks.sleepMs)
        m_hooks.sleepMs = [](qint64 ms) { QThread::msleep(static_cast<unsigned long>(ms)); };
    if (!m_hooks.percent)
        m_hooks.percent = [] { return thePrefs.hashingDiskLoad(); };

    // The main thread never sleeps for this: it would stall the whole core.
    const QCoreApplication* app = QCoreApplication::instance();
    m_active = m_hooks.allowMainThread || !app || QThread::currentThread() != app->thread();
}

void DiskLoadLimiter::beginRead()
{
    if (m_active)
        m_readStartNs = m_hooks.nowNs();
}

void DiskLoadLimiter::endRead()
{
    if (!m_active)
        return;
    m_busyNs += std::max<qint64>(0, m_hooks.nowNs() - m_readStartNs);
    if (m_busyNs < kMinBusyNs)
        return;

    const qint64 pause = pauseNs(m_busyNs, m_hooks.percent());
    m_busyNs = 0;
    if (pause >= 1'000'000)
        m_hooks.sleepMs(pause / 1'000'000);
}

qint64 DiskLoadLimiter::pauseNs(qint64 busyNs, int percent)
{
    if (busyNs <= 0 || percent >= 100)
        return 0;
    const qint64 p = std::max(percent, 1);
    return std::min(busyNs * (100 - p) / p, kMaxPauseNs);
}

} // namespace eMule
