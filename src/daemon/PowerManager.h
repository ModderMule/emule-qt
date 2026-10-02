#pragma once

/// @file PowerManager.h
/// @brief Prevent system idle sleep while the daemon has work — MFC PreventStandby.
///
/// MFC: CemuleApp::ResetStandByIdleTimer() (Emule.cpp), re-checked every 60 s from
/// the upload queue timer. The hold is taken only while thePrefs.preventStandby()
/// and the activity predicate (connected / transferring) are both true.
///
/// macOS:   IOPMAssertionCreateWithName(NoIdleSleep) / IOPMAssertionRelease.
/// Windows: SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_CONTINUOUS).
/// Linux:   logind Inhibit("idle:sleep", "block") on the system bus; the hold is
///          the returned fd. Falls back to org.freedesktop.ScreenSaver.Inhibit.
/// None of these need admin/root.

#include <QObject>
#include <QTimer>

#include <cstdint>
#include <functional>

namespace eMule {

class PowerManager : public QObject {
    Q_OBJECT

public:
    /// @p isActive: true while sleep should be held off (connected or transferring).
    explicit PowerManager(std::function<bool()> isActive, QObject* parent = nullptr);
    ~PowerManager() override;

    PowerManager(const PowerManager&) = delete;
    PowerManager& operator=(const PowerManager&) = delete;

    /// Evaluate now, then every 60 s.
    void start();

    /// Re-check pref + activity and take or release the hold. Call on pref change.
    void evaluate();

    /// True while idle-sleep prevention is held.
    [[nodiscard]] bool isPreventingStandby() const { return m_active; }

private:
    /// Platform hold/release. Idempotent.
    void hold(bool prevent);

    std::function<bool()> m_isActive;
    QTimer m_timer;
    bool m_active = false;
    bool m_failureLogged = false;
#ifdef Q_OS_MACOS
    uint32_t m_assertionId = 0;
#elif defined(Q_OS_LINUX)
    int m_inhibitFd = -1;          ///< logind inhibitor fd; closing it releases
    uint32_t m_screenSaverCookie = 0;  ///< fallback when logind is absent
#endif
};

} // namespace eMule
