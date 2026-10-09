#pragma once

/// @file NotifierText.h
/// @brief Wording of the pop-ups the daemon asks for (PushNotifierEvent).
///
/// MFC builds these where the event happens and hands the finished string to
/// ShowNotifier; here the daemon sends the event and its one variable part, so the
/// text is in the GUI's language.

#include "IpcProtocol.h"

#include <QCoreApplication>
#include <QString>

namespace eMule {

struct NotifierText {
    /// Which Notifications option the pop-up belongs to: "Urgent" when true,
    /// "Download finished" otherwise.
    bool urgent = true;
    QString title;
    QString text;
};

[[nodiscard]] inline NotifierText notifierEventText(Ipc::NotifierEvent kind, const QString& arg)
{
    // translate() spelled out each time: lupdate reads the context from the call.
    switch (kind) {
    case Ipc::NotifierEvent::DownloadFinished:    // IDS_TBN_DOWNLOADDONE
        return {false, QCoreApplication::translate("Notifier", "Downloaded:"), arg};
    case Ipc::NotifierEvent::ConnectionLost:      // IDS_CONNECTIONLOST
        return {true, QCoreApplication::translate("Notifier", "Connection lost"), arg};
    case Ipc::NotifierEvent::OutOfDiskSpace:      // IDS_ERR_OUTOFSPACE
        return {true, QCoreApplication::translate("Notifier", "Out of disk space"),
                QCoreApplication::translate("Notifier", "You have insufficient disk space to download \"%1\"!").arg(arg)};
    case Ipc::NotifierEvent::PortBindFailed:      // IDS_MAIN_SOCKETERROR
        return {true, QCoreApplication::translate("Notifier", "Port not available"),
                QCoreApplication::translate("Notifier", "Fatal Error: Unable to create socket on port %1").arg(arg)};
    }
    return {};
}

} // namespace eMule
