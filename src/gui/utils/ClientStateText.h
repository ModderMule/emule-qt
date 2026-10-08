#pragma once

/// @file ClientStateText.h
/// @brief Download-state labels for client rows, as MFC shows them.

#include <QCoreApplication>
#include <QString>

namespace eMule {

/// MFC CUpDownClient::GetDownloadStateDisplayString (srchybrid/BaseClient.cpp:2434-2478).
/// The daemon sends the state token (UpDownClient::dbgGetDownloadState); this is the
/// text for it. Idle, banned and unknown states read empty.
[[nodiscard]] inline QString downloadStateText(const QString& token, bool remoteQueueFull)
{
    const auto text = [](const char* s) {
        return QCoreApplication::translate("eMule::ClientState", s);
    };
    if (token == QLatin1String("Connecting"))      return text("Connecting");
    if (token == QLatin1String("Connected"))       return text("Asking");
    if (token == QLatin1String("WaitCallback"))    return text("Connecting via server");
    if (token == QLatin1String("OnQueue"))
        return remoteQueueFull ? text("Queue Full") : text("On Queue");
    if (token == QLatin1String("Downloading"))     return text("Transferring");
    if (token == QLatin1String("ReqHashSet"))      return text("Receiving hashset");
    if (token == QLatin1String("NoNeededParts"))   return text("No needed parts");
    if (token == QLatin1String("LowToLowIp"))      return text("Cannot connect LowID to LowID");
    if (token == QLatin1String("TooManyConns"))    return text("Too many connections");
    if (token == QLatin1String("Error"))           return text("Error");
    if (token == QLatin1String("WaitCallbackKad")) return text("Wait Callback Kad");
    if (token == QLatin1String("TooManyConnsKad")) return text("Too Many Kad Lookups");
    return {};
}

/// Upload side: the token of UpDownClient::uploadStateToken(). @p stalled is the slot
/// with nothing to send, shown in advanced mode only (MFC BaseClient.cpp:2480-2510).
[[nodiscard]] inline QString uploadStateText(const QString& token, bool stalled)
{
    const auto text = [](const char* s) {
        return QCoreApplication::translate("eMule::ClientState", s);
    };
    if (token == QLatin1String("OnQueue"))    return text("On Queue");
    if (token == QLatin1String("Banned"))     return text("Banned");
    if (token == QLatin1String("Connecting")) return text("Connecting");
    if (token == QLatin1String("Transferring") || token == QLatin1String("Standby")) {
        if (stalled)
            return text("Stalled! Waiting for block request.");
        return token == QLatin1String("Standby") ? text("Standby") : text("Transferring");
    }
    return {};
}

/// Sort key of that column: MFC sorts by the state, not by its text.
[[nodiscard]] inline int uploadStateRank(const QString& token)
{
    if (token == QLatin1String("Transferring") || token == QLatin1String("Standby")) return 0;
    if (token == QLatin1String("OnQueue"))    return 1;
    if (token == QLatin1String("Connecting")) return 2;
    if (token == QLatin1String("Banned"))     return 3;
    return 4;
}

/// MFC shows "(Unknown)" for a peer that has not sent a name yet.
[[nodiscard]] inline QString clientNameText(const QString& userName)
{
    if (!userName.isEmpty())
        return userName;
    return QStringLiteral("(%1)").arg(QCoreApplication::translate("eMule::ClientState", "Unknown"));
}

} // namespace eMule
