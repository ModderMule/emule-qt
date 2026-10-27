#include "pch.h"
/// @file MassRenameAction.cpp
/// @brief "Mass Rename..." for the Shared Files and Downloads lists — implementation.

#include "utils/MassRenameAction.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "utils/AsyncMessageBox.h"
#include "utils/IpcFeedback.h"

#include <QCborArray>
#include <QCoreApplication>
#include <QPointer>

namespace eMule::MassRenameAction {

void run(QWidget* parent, IpcClient* ipc, QList<MassRenameDialog::Item> items,
         std::function<void()> refresh)
{
    if (items.isEmpty() || !IpcFeedback::requireConnection(ipc, QCoreApplication::translate("MassRenameAction", "Mass Rename")))
        return;

    QHash<QString, QString> oldNames;
    for (const auto& it : items)
        oldNames.insert(it.hash, it.name);

    MassRenameDialog dlg(std::move(items), ipc, parent);
    if (dlg.exec() != QDialog::Accepted || dlg.renames().isEmpty())
        return;

    QCborArray pairs;
    for (const auto& [hash, name] : dlg.renames())
        pairs.append(QCborArray{hash, name});
    Ipc::IpcMessage msg(Ipc::IpcMsgType::RenameFiles);
    msg.append(pairs);

    QPointer<QWidget> guard(parent);
    ipc->sendRequest(std::move(msg), [guard, oldNames, refresh](const Ipc::IpcMessage& resp) {
        if (!guard || !IpcFeedback::checkOrWarn(resp, guard, QCoreApplication::translate("MassRenameAction", "Mass Rename")))
            return;
        QStringList failed;
        for (const QCborValue& v : resp.fieldArray(1)) {
            const QCborArray row = v.toArray();
            if (!row.at(1).toBool())
                failed << QStringLiteral("%1: %2").arg(oldNames.value(row.at(0).toString()),
                                                       row.at(2).toString());
        }
        if (refresh)
            refresh();
        if (!failed.isEmpty())
            AsyncMessageBox::warning(guard, QCoreApplication::translate("MassRenameAction", "Mass Rename"),
                QCoreApplication::translate("MassRenameAction", "%n file(s) could not be renamed:", nullptr,
                                            static_cast<int>(failed.size()))
                    + u"\n\n" + failed.join(u'\n'));
    });
}

} // namespace eMule::MassRenameAction
