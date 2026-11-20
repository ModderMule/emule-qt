#include "pch.h"
/// @file FolderLister.cpp
/// @brief The core's folders, local or over IPC — implementation.

#include "utils/FolderLister.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCborArray>
#include <QCoreApplication>
#include <QDir>
#include <QInputDialog>
#include <QMessageBox>
#include <QPointer>

namespace eMule::FolderLister {

bool remote(const IpcClient* ipc)
{
    return ipc && !ipc->isLocalConnection();
}

void list(IpcClient* ipc, const QString& path, QObject* ctx, Rows done)
{
    if (!remote(ipc)) {
        done(path.isEmpty() || QFileInfo(path).isDir(),
             path.isEmpty() ? folderRoots() : subfolders(path), QDir::homePath());
        return;
    }
    Ipc::IpcMessage msg(Ipc::IpcMsgType::ListFolders);
    msg.append(path);
    msg.append(false);
    ipc->sendRequest(std::move(msg), [guard = QPointer<QObject>(ctx), done = std::move(done)](const Ipc::IpcMessage& resp) {
        if (!guard)
            return;
        QList<FolderEntry> rows;
        const bool ok = resp.isValid() && resp.fieldBool(0);
        if (ok) {
            for (const QCborValue& v : resp.fieldArray(1)) {
                const QCborArray r = v.toArray();
                rows.append({r.at(0).toString(), r.at(1).toString(), r.at(2).toBool()});
            }
        }
        done(ok, rows, ok ? resp.fieldString(2) : QString());
    });
}

void tree(IpcClient* ipc, const QString& root, QObject* ctx, std::function<void(const QStringList&)> done)
{
    if (!remote(ipc)) {
        done(folderTree(root));
        return;
    }
    Ipc::IpcMessage msg(Ipc::IpcMsgType::ListFolders);
    msg.append(root);
    msg.append(true);
    ipc->sendRequest(std::move(msg), [guard = QPointer<QObject>(ctx), done = std::move(done)](const Ipc::IpcMessage& resp) {
        if (!guard)
            return;
        QStringList paths;
        if (resp.isValid() && resp.fieldBool(0))
            for (const QCborValue& v : resp.fieldArray(1))
                paths.append(v.toArray().at(1).toString());
        done(paths);
    });
}

QString askUncShare(QWidget* parent)
{
    bool ok = false;
    const QString typed = QInputDialog::getText(
        parent, QCoreApplication::translate("eMule::FolderLister", "Add UNC Share"),
        QCoreApplication::translate("eMule::FolderLister", "Network folder (\\\\server\\share):"),
        QLineEdit::Normal, QStringLiteral("\\\\"), &ok).trimmed();
    if (!ok || typed.isEmpty() || typed == QLatin1String("\\\\"))
        return {};
    if (uncShareRoot(typed).isEmpty()) {
        QMessageBox::warning(parent, QCoreApplication::translate("eMule::FolderLister", "Invalid Path"),
                             QCoreApplication::translate("eMule::FolderLister",
                                 "Not a UNC path. Enter it like \\\\server\\share or \\\\server\\share\\folder."));
        return {};
    }
    // '\\' by hand: a GUI on macOS adds shares for a core on Windows
    QString path = QString(typed).replace(u'\\', u'/');
    while (path.endsWith(u'/'))
        path.chop(1);
    // cleanPath folds a leading "//" off-Windows: keep it apart
    return QStringLiteral("//") + QDir::cleanPath(path.mid(2));
}

} // namespace eMule::FolderLister
