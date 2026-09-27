#include "pch.h"
/// @file IpcFeedback.cpp
/// @brief Reports rejected daemon replies to the user — see IpcFeedback.h.

#include "utils/IpcFeedback.h"

#include "app/IpcClient.h"
#include "utils/StatusBarNotifier.h"

#include <QCoreApplication>
#include <QMessageBox>

namespace eMule::IpcFeedback {

bool checkOrWarn(const Ipc::IpcMessage& resp, QWidget* parent,
                 const QString& title, const QString& fallback)
{
    if (resp.fieldBool(0))
        return true;
    if (!resp.isValid())
        return false;

    QString text = resp.fieldString(1);
    if (text.isEmpty())
        text = fallback;
    if (text.isEmpty())
        text = QCoreApplication::translate("IpcFeedback", "The request was rejected by eMule.");

    QMessageBox::warning(parent, title, text);
    return false;
}

bool requireConnection(const IpcClient* ipc, const QString& what)
{
    if (ipc && ipc->isConnected())
        return true;
    StatusBarNotifier::post(
        QCoreApplication::translate("IpcFeedback", "Not connected to daemon — %1").arg(what), 4000);
    return false;
}

} // namespace eMule::IpcFeedback
