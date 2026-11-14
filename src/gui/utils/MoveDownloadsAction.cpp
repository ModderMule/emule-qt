#include "pch.h"
/// @file MoveDownloadsAction.cpp
/// @brief "Move to..." for the download lists — implementation.

#include "utils/MoveDownloadsAction.h"

#include "app/IpcClient.h"
#include "dialogs/FolderPickerDialog.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "utils/AsyncMessageBox.h"
#include "utils/IpcFeedback.h"

#include <QCborArray>
#include <QCoreApplication>
#include <QDir>
#include <QPointer>

#include <memory>

namespace eMule::MoveDownloadsAction {

void run(QWidget* parent, IpcClient* ipc, QList<Group> groups, const QHash<QString, QString>& names,
         const QString& startDir, std::function<void()> refresh)
{
    groups.removeIf([](const Group& g) { return g.ids.isEmpty(); });
    const QString title = QCoreApplication::translate("MoveDownloadsAction", "Move To Folder");
    if (groups.isEmpty() || !IpcFeedback::requireConnection(ipc, title))
        return;
    int count = 0;
    for (const Group& g : groups)
        count += int(g.ids.size());

    // the core's disk: browsed over IPC when it runs on another host
    QString dir = FolderPickerDialog::getExistingDirectory(
        parent, ipc, QCoreApplication::translate("MoveDownloadsAction", "Move %n download(s) to", nullptr, count),
        startDir);
    dir = QDir::fromNativeSeparators(dir.trimmed());
    if (dir.isEmpty())
        return;

    // the failures of every answer in one box, after the last
    struct Pending {
        int left = 0;
        QStringList failed;
    };
    auto pending = std::make_shared<Pending>();
    pending->left = int(groups.size());
    QPointer<QWidget> guard(parent);
    for (const Group& g : std::as_const(groups)) {
        Ipc::IpcMessage msg(Ipc::IpcMsgType::MoveDownloads);
        msg.append(static_cast<qint64>(g.network));
        msg.append(QCborArray::fromStringList(g.ids));
        msg.append(dir);
        ipc->sendRequest(std::move(msg), [guard, names, refresh, title, pending](const Ipc::IpcMessage& resp) {
            --pending->left;
            if (!guard || !IpcFeedback::checkOrWarn(resp, guard, title))
                return;
            for (const QCborValue& v : resp.fieldArray(1)) {
                const QCborArray row = v.toArray();
                if (row.at(1).toBool())
                    continue;
                const QString id = row.at(0).toString();
                pending->failed << QStringLiteral("%1: %2").arg(names.value(id, id), row.at(2).toString());
            }
            if (refresh)
                refresh();
            if (pending->left == 0 && !pending->failed.isEmpty())
                AsyncMessageBox::warning(guard, title,
                    QCoreApplication::translate("MoveDownloadsAction", "%n download(s) could not be moved:", nullptr,
                                                static_cast<int>(pending->failed.size()))
                        + u"\n\n" + pending->failed.join(u'\n'));
        });
    }
}

QString menuText()
{
    return QCoreApplication::translate("MoveDownloadsAction", "Move To Folder...");
}

} // namespace eMule::MoveDownloadsAction
