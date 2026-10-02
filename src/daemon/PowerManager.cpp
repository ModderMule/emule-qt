/// @file PowerManager.cpp
/// @brief Prevent system idle sleep while the daemon has work — implementation.

#include "PowerManager.h"

#include "prefs/Preferences.h"
#include "utils/Log.h"

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <unistd.h>
#endif

#include <chrono>

namespace eMule {

using namespace std::chrono_literals;

PowerManager::PowerManager(std::function<bool()> isActive, QObject* parent)
    : QObject(parent)
    , m_isActive(std::move(isActive))
{
    // MFC: every 60 s from CUploadQueue::UploadTimer
    m_timer.setInterval(60s);
    connect(&m_timer, &QTimer::timeout, this, &PowerManager::evaluate);
}

PowerManager::~PowerManager()
{
    hold(false);
}

void PowerManager::start()
{
    evaluate();
    m_timer.start();
}

void PowerManager::evaluate()
{
    hold(thePrefs.preventStandby() && m_isActive && m_isActive());
}

void PowerManager::hold(bool prevent)
{
    if (prevent == m_active)
        return;

    bool ok = true;
#ifdef Q_OS_MACOS
    if (prevent) {
        IOPMAssertionID id = 0;
        ok = IOPMAssertionCreateWithName(kIOPMAssertionTypeNoIdleSleep,
                                         kIOPMAssertionLevelOn,
                                         CFSTR("eMule Qt: connected or transferring"),
                                         &id) == kIOReturnSuccess;
        if (ok)
            m_assertionId = id;
    } else {
        IOPMAssertionRelease(m_assertionId);
        m_assertionId = 0;
    }
#elif defined(Q_OS_WIN)
    // per-thread state; always called from the main thread
    if (prevent)
        ok = SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED) != 0;
    else
        SetThreadExecutionState(ES_CONTINUOUS);
#elif defined(Q_OS_LINUX)
    if (prevent) {
        // logind: works headless, blocks idle and idle-triggered suspend
        QDBusInterface login1(QStringLiteral("org.freedesktop.login1"),
                              QStringLiteral("/org/freedesktop/login1"),
                              QStringLiteral("org.freedesktop.login1.Manager"),
                              QDBusConnection::systemBus());
        const QDBusReply<QDBusUnixFileDescriptor> reply = login1.call(
            QStringLiteral("Inhibit"), QStringLiteral("idle:sleep"), QStringLiteral("eMule Qt"),
            QStringLiteral("Connected or transferring"), QStringLiteral("block"));
        if (reply.isValid() && reply.value().isValid()) {
            m_inhibitFd = ::dup(reply.value().fileDescriptor());  // reply's copy closes with it
            ok = m_inhibitFd >= 0;
        } else {
            // no systemd: desktop session idle inhibit
            QDBusInterface saver(QStringLiteral("org.freedesktop.ScreenSaver"),
                                 QStringLiteral("/org/freedesktop/ScreenSaver"),
                                 QStringLiteral("org.freedesktop.ScreenSaver"),
                                 QDBusConnection::sessionBus());
            const QDBusReply<uint32_t> cookie = saver.call(
                QStringLiteral("Inhibit"), QStringLiteral("eMule Qt"),
                QStringLiteral("Connected or transferring"));
            ok = cookie.isValid();
            if (ok)
                m_screenSaverCookie = cookie.value();
        }
    } else {
        if (m_inhibitFd >= 0) {
            ::close(m_inhibitFd);
            m_inhibitFd = -1;
        }
        if (m_screenSaverCookie != 0) {
            QDBusInterface saver(QStringLiteral("org.freedesktop.ScreenSaver"),
                                 QStringLiteral("/org/freedesktop/ScreenSaver"),
                                 QStringLiteral("org.freedesktop.ScreenSaver"),
                                 QDBusConnection::sessionBus());
            saver.call(QStringLiteral("UnInhibit"), m_screenSaverCookie);
            m_screenSaverCookie = 0;
        }
    }
#endif

    if (!ok) {
        if (!m_failureLogged) {
            logWarning(QStringLiteral("Could not prevent standby mode on this system"));
            m_failureLogged = true;
        }
        return;
    }
    m_active = prevent;
    logInfo(prevent ? QStringLiteral("Standby prevented: connected or transferring")
                    : QStringLiteral("Standby allowed again"));
}

} // namespace eMule
