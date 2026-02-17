#pragma once

/// @file ThreadUtils.h
/// @brief Portable threading primitives replacing MFC CCriticalSection / CEvent.
///
/// Header-only, no Qt dependency.  All types live in namespace eMule.
///
/// Migration patterns:
///   CCriticalSection  → eMule::Mutex   (std::mutex)
///   CSingleLock       → eMule::Lock    (std::unique_lock<std::mutex>)
///   CRWLock           → eMule::SharedMutex / ReadLock / WriteLock
///   CEvent(FALSE,TRUE)  → eMule::ManualResetEvent
///   CEvent(FALSE,FALSE) → eMule::AutoResetEvent
///   InterlockedIncrement/Decrement → std::atomic<T>::fetch_add / fetch_sub
///   CWinThread        → std::jthread

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>

namespace eMule {

// ---- Mutex aliases ----------------------------------------------------------

using Mutex       = std::mutex;
using Lock        = std::unique_lock<std::mutex>;
using SharedMutex = std::shared_mutex;
using ReadLock    = std::shared_lock<std::shared_mutex>;
using WriteLock   = std::unique_lock<std::shared_mutex>;

// ---- Atomic alias -----------------------------------------------------------

template <typename T>
using Atomic = std::atomic<T>;

// ---- ManualResetEvent -------------------------------------------------------

/// Replaces MFC CEvent(FALSE, TRUE) — stays signalled until explicitly reset.
class ManualResetEvent {
public:
    explicit ManualResetEvent(bool initialState = false) noexcept
        : m_signalled(initialState) {}

    /// Signal the event — all waiting threads are released.
    void set() noexcept
    {
        {
            std::lock_guard lk(m_mutex);
            m_signalled = true;
        }
        m_cv.notify_all();
    }

    /// Reset the event to non-signalled state.
    void reset() noexcept
    {
        std::lock_guard lk(m_mutex);
        m_signalled = false;
    }

    /// Block until the event is signalled.
    void wait() noexcept
    {
        std::unique_lock lk(m_mutex);
        m_cv.wait(lk, [this] { return m_signalled; });
    }

    /// Block until signalled or @p timeout elapses.
    /// @return true if signalled, false on timeout.
    template <typename Rep, typename Period>
    [[nodiscard]] bool waitFor(std::chrono::duration<Rep, Period> timeout) noexcept
    {
        std::unique_lock lk(m_mutex);
        return m_cv.wait_for(lk, timeout, [this] { return m_signalled; });
    }

    /// Check if currently signalled (non-blocking).
    [[nodiscard]] bool isSet() const noexcept
    {
        std::lock_guard lk(m_mutex);
        return m_signalled;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_signalled;
};

// ---- AutoResetEvent ---------------------------------------------------------

/// Replaces MFC CEvent(FALSE, FALSE) — auto-resets after releasing one waiter.
class AutoResetEvent {
public:
    explicit AutoResetEvent(bool initialState = false) noexcept
        : m_signalled(initialState) {}

    /// Signal the event — releases exactly one waiting thread.
    void set() noexcept
    {
        {
            std::lock_guard lk(m_mutex);
            m_signalled = true;
        }
        m_cv.notify_one();
    }

    /// Block until the event is signalled; auto-resets after waking.
    void wait() noexcept
    {
        std::unique_lock lk(m_mutex);
        m_cv.wait(lk, [this] { return m_signalled; });
        m_signalled = false;  // auto-reset
    }

    /// Block until signalled or @p timeout elapses; auto-resets on wake.
    /// @return true if signalled, false on timeout.
    template <typename Rep, typename Period>
    [[nodiscard]] bool waitFor(std::chrono::duration<Rep, Period> timeout) noexcept
    {
        std::unique_lock lk(m_mutex);
        bool result = m_cv.wait_for(lk, timeout, [this] { return m_signalled; });
        if (result)
            m_signalled = false;  // auto-reset
        return result;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_signalled;
};

} // namespace eMule
