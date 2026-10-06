#include "pch.h"
/// @file KadLog.cpp
/// @brief Kad-specific logging implementation.

#include "kademlia/KadLog.h"

namespace eMule::kad {

void logKadLine(const QString& msg)
{
    qCDebug(lcEmuleKad).noquote() << msg;
}

void setKadLogging(bool enabled)
{
    detail::g_kadLoggingEnabled = enabled;
}

} // namespace eMule::kad
