#include "app/PowerManager.h"

#ifdef Q_OS_MACOS
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <CoreFoundation/CoreFoundation.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#endif

namespace eMule {

PowerManager::~PowerManager()
{
    if (m_active)
        setPreventStandby(false);
}

void PowerManager::setPreventStandby(bool prevent)
{
    if (prevent == m_active)
        return;

#ifdef Q_OS_MACOS
    if (prevent) {
        CFStringRef reason = CFSTR("eMule Qt: active transfers");
        IOReturn ret = IOPMAssertionCreateWithName(
            kIOPMAssertionTypeNoIdleSleep,
            kIOPMAssertionLevelOn,
            reason,
            &m_assertionId);
        m_active = (ret == kIOReturnSuccess);
    } else {
        IOPMAssertionRelease(m_assertionId);
        m_assertionId = 0;
        m_active = false;
    }
#elif defined(Q_OS_WIN)
    if (prevent) {
        m_active = SetThreadExecutionState(
            ES_CONTINUOUS | ES_SYSTEM_REQUIRED) != 0;
    } else {
        SetThreadExecutionState(ES_CONTINUOUS);
        m_active = false;
    }
#elif defined(Q_OS_LINUX)
    QDBusInterface iface(
        QStringLiteral("org.freedesktop.ScreenSaver"),
        QStringLiteral("/org/freedesktop/ScreenSaver"),
        QStringLiteral("org.freedesktop.ScreenSaver"),
        QDBusConnection::sessionBus());

    if (prevent) {
        QDBusReply<uint32_t> reply = iface.call(
            QStringLiteral("Inhibit"),
            QStringLiteral("eMule Qt"),
            QStringLiteral("Active transfers"));
        if (reply.isValid()) {
            m_cookie = reply.value();
            m_active = true;
        }
    } else {
        iface.call(QStringLiteral("UnInhibit"), m_cookie);
        m_cookie = 0;
        m_active = false;
    }
#else
    m_active = prevent;
#endif
}

} // namespace eMule
