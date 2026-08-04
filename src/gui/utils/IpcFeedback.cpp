#include "pch.h"
/// @file IpcFeedback.cpp
/// @brief Reports rejected daemon replies to the user — see IpcFeedback.h.

#include "utils/IpcFeedback.h"

#include <QCoreApplication>
#include <QMessageBox>

namespace eMule::IpcFeedback {

bool checkOrWarn(const Ipc::IpcMessage& resp, QWidget* parent,
                 const QString& title, const QString& fallback)
{
    if (resp.fieldBool(0))
        return true;

    QString text = resp.fieldString(1);
    if (text.isEmpty())
        text = fallback;
    if (text.isEmpty())
        text = QCoreApplication::translate("IpcFeedback", "The request was rejected by eMule.");

    QMessageBox::warning(parent, title, text);
    return false;
}

} // namespace eMule::IpcFeedback
