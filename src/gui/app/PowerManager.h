#pragma once

/// @file PowerManager.h
/// @brief Prevent system idle sleep while the application is running.
///
/// macOS:   IOPMAssertionCreateWithName / IOPMAssertionRelease.
/// Windows: SetThreadExecutionState.
/// Linux:   D-Bus org.freedesktop.ScreenSaver Inhibit / UnInhibit.

#include <cstdint>

namespace eMule {

class PowerManager {
public:
    PowerManager() = default;
    ~PowerManager();

    PowerManager(const PowerManager&) = delete;
    PowerManager& operator=(const PowerManager&) = delete;

    /// Enable or disable idle-sleep prevention.
    void setPreventStandby(bool prevent);

    /// Returns true if idle-sleep prevention is currently active.
    [[nodiscard]] bool isPreventingStandby() const { return m_active; }

private:
    bool m_active = false;
#ifdef Q_OS_MACOS
    uint32_t m_assertionId = 0;
#elif defined(Q_OS_LINUX)
    uint32_t m_cookie = 0;
#endif
};

} // namespace eMule
