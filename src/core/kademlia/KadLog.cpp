#include "pch.h"
/// @file KadLog.cpp
/// @brief Kad-specific logging implementation.

#include "kademlia/KadLog.h"

namespace eMule::kad {

static bool s_kadLoggingEnabled = false;

void logKad(const QString& msg)
{
    if (s_kadLoggingEnabled)
        qCDebug(lcEmuleKad).noquote() << msg;
}

void setKadLogging(bool enabled)
{
    s_kadLoggingEnabled = enabled;
}

bool isKadLoggingEnabled()
{
    return s_kadLoggingEnabled;
}

} // namespace eMule::kad
