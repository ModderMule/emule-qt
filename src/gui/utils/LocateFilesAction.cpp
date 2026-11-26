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

#include <memory>

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

void runTorrents(QWidget* parent, IpcClient* ipc, const QStringList& ids, const QString& startDir, std::function<void()> refresh)
{
    const QString title = QCoreApplication::translate("LocateFilesAction", "Locate Files");
    if (ids.isEmpty() || !IpcFeedback::requireConnection(ipc, title))
        return;
    QString dir = FolderPickerDialog::getExistingDirectory(
        parent, ipc, QCoreApplication::translate("LocateFilesAction", "Folder with files of the torrents"), startDir);
    dir = QDir::fromNativeSeparators(dir.trimmed());
    if (dir.isEmpty())
        return;
    struct Sum {
        int left = 0, linked = 0, partial = 0, copied = 0, failed = 0, errors = 0;
        qint64 files = 0, hashed = 0;
    };
    auto sum = std::make_shared<Sum>();
    sum->left = int(ids.size());
    QPointer<QWidget> guard(parent);
    for (const QString& id : ids) {
        Ipc::IpcMessage msg(Ipc::IpcMsgType::LocateTorrentFiles);
        msg.append(id);
        msg.append(QCborArray{dir});
        ipc->sendRequest(std::move(msg), [guard, title, refresh, sum](const Ipc::IpcMessage& resp) {
            if (!resp.isValid() || resp.type() == Ipc::IpcMsgType::Error) {
                ++sum->errors;
            } else {
                const QCborMap r = resp.fieldMap(1);
                sum->files += r.value(QStringLiteral("files")).toInteger();
                sum->hashed += r.value(QStringLiteral("hashed")).toInteger();
                sum->linked += int(r.value(QStringLiteral("linked")).toInteger());
                sum->partial += int(r.value(QStringLiteral("partial")).toInteger());
                sum->copied += int(r.value(QStringLiteral("copied")).toInteger());
                sum->failed += int(r.value(QStringLiteral("failed")).toInteger());
            }
            if (--sum->left > 0 || !guard)
                return;
            QString text;
            if (sum->linked > 0)
                text = QCoreApplication::translate("LocateFilesAction", "%n file(s) found and used where they are; the torrent is being rechecked.", nullptr, sum->linked);
            if (sum->copied > 0)
                text += (text.isEmpty() ? QString() : QStringLiteral("\n"))
                    + QCoreApplication::translate("LocateFilesAction", "%n file(s) found but not provable as a whole; their matching pieces were copied into the download.", nullptr, sum->copied);
            if (text.isEmpty())
                text = QCoreApplication::translate("LocateFilesAction", "No file of the torrents found there.");
            text += u"\n\n" + QCoreApplication::translate("LocateFilesAction", "%1 file(s) looked at, %2 of a matching size hashed.").arg(sum->files).arg(sum->hashed);
            if (sum->partial > 0)
                text += u"\n" + QCoreApplication::translate("LocateFilesAction", "%n file(s) of the right size did not match and were left alone.", nullptr, sum->partial);
            if (sum->failed + sum->errors > 0)
                text += u"\n" + QCoreApplication::translate("LocateFilesAction", "%n could not be linked (see the log).", nullptr, sum->failed + sum->errors);
            AsyncMessageBox::information(guard, title, text);
            if (refresh)
                refresh();
        });
    }
}

QString menuText()
{
    return QCoreApplication::translate("LocateFilesAction", "Locate Files...");
}

} // namespace eMule::LocateFilesAction
