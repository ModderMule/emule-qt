#include "pch.h"
/// @file LocateFilesAction.cpp
/// @brief "Locate Files..." — implementation.

#include "utils/LocateFilesAction.h"

#include "app/IpcClient.h"
#include "dialogs/FolderPickerDialog.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "utils/AsyncMessageBox.h"
#include "utils/IpcFeedback.h"

#include <QCborArray>
#include <QCborMap>
#include <QCoreApplication>
#include <QDir>
#include <QPointer>

namespace eMule::LocateFilesAction {

void run(QWidget* parent, IpcClient* ipc, const QString& startDir, std::function<void()> refresh)
{
    const QString title = QCoreApplication::translate("LocateFilesAction", "Locate Files");
    if (!IpcFeedback::requireConnection(ipc, title))
        return;
    // the core's disk: browsed over IPC when it runs on another host
    QString dir = FolderPickerDialog::getExistingDirectory(
        parent, ipc, QCoreApplication::translate("LocateFilesAction", "Folder with files of the downloads"), startDir);
    dir = QDir::fromNativeSeparators(dir.trimmed());
    if (dir.isEmpty())
        return;
    Ipc::IpcMessage msg(Ipc::IpcMsgType::LocateLocalCopies);
    msg.append(QCborArray{dir});
    QPointer<QWidget> guard(parent);
    ipc->sendRequest(std::move(msg), [guard, title, refresh](const Ipc::IpcMessage& resp) {
        if (!guard || !IpcFeedback::checkOrWarn(resp, guard, title))
            return;
        const QCborMap r = resp.fieldMap(1);
        const int matched = int(r.value(QStringLiteral("matched")).toInteger());
        QString text = QCoreApplication::translate("LocateFilesAction", "%n file(s) of the downloads found; they are copied from there.",
                                                   nullptr, matched);
        if (matched == 0)
            text = QCoreApplication::translate("LocateFilesAction", "No file of the downloads found there.");
        text += u"\n\n"
                + QCoreApplication::translate("LocateFilesAction", "%1 file(s) looked at, %2 of a matching size hashed.")
                      .arg(r.value(QStringLiteral("files")).toInteger())
                      .arg(r.value(QStringLiteral("hashed")).toInteger());
        AsyncMessageBox::information(guard, title, text);
        if (refresh)
            refresh();
    });
}

QString menuText()
{
    return QCoreApplication::translate("LocateFilesAction", "Locate Files...");
}

} // namespace eMule::LocateFilesAction
