#pragma once

/// @file KadLog.h
/// @brief Kad-specific logging controlled by setKadLogging().

#include "utils/DebugUtils.h"

#include <QString>

#include <utility>

namespace eMule::kad {

namespace detail {
inline bool g_kadLoggingEnabled = false;
} // namespace detail

/// Write one Kad line via qCDebug(lcEmuleKad). Use logKad() instead.
void logKadLine(const QString& msg);

/// Builds the message only when Kad logging is on.
template <typename MakeMsg>
inline void logKadLazy(MakeMsg&& makeMsg)
{
    if (detail::g_kadLoggingEnabled)
        logKadLine(std::forward<MakeMsg>(makeMsg)());
}

/// Enable/disable Kad-specific debug logging.
void setKadLogging(bool enabled);

/// Check if Kad logging is enabled.
[[nodiscard]] inline bool isKadLoggingEnabled() { return detail::g_kadLoggingEnabled; }

} // namespace eMule::kad

/// Log a Kad-specific message; the argument is not evaluated while logging is off.
#define logKad(...) logKadLazy([&]() -> QString { return __VA_ARGS__; })
