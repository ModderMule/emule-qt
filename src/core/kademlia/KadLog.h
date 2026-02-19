#pragma once

/// @file KadLog.h
/// @brief Kad-specific logging controlled by setKadLogging().

#include "utils/DebugUtils.h"

#include <QString>

namespace eMule::kad {

/// Log a Kad-specific message via qCDebug(lcEmuleKad) when enabled.
void logKad(const QString& msg);

/// Enable/disable Kad-specific debug logging.
void setKadLogging(bool enabled);

/// Check if Kad logging is enabled.
[[nodiscard]] bool isKadLoggingEnabled();

} // namespace eMule::kad
