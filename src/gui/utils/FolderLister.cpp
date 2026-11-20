#include "pch.h"
/// @file FolderLister.cpp
/// @brief The core's folders, local or over IPC — implementation.

#include "utils/FolderLister.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "utils/SharedDirState.h"

#include <QCborArray>
#include <QCoreApplication>
#include <QDir>
#include <QInputDialog>
#include <QMessageBox>
#include <QPointer>
#include <QThreadPool>

namespace eMule::FolderLister {

namespace {

/// Disk reads for a local core. Never deleted: a listing stuck on an offline
/// share must not hold up quitting (QThreadPool's dtor waits).
QThreadPool& diskPool()
{
    static QThreadPool* pool = [] {
        auto* p = new QThreadPool;
        p->setMaxThreadCount(4);
        return p;
    }();
    return *pool;
}

/// Runs @p work on the disk pool, hands its result to @p deliver on the GUI thread
/// while @p ctx lives. An offline UNC share blocks for seconds (SMB timeout).
template <typename Work, typename Deliver>
void runOffThread(QObject* ctx, Work work, Deliver deliver)
{
    diskPool().start([guard = QPointer<QObject>(ctx), work, deliver] {
        auto result = work();
        auto* app = QCoreApplication::instance();
        if (!app)
            return;
        QMetaObject::invokeMethod(app, [guard, deliver, result] {
            if (guard)
                deliver(result);
        }, Qt::QueuedConnection);
    });
}

struct ListResult {
    bool ok = false;
    QList<FolderEntry> rows;
    QString home;
};

} // namespace

bool remote(const IpcClient* ipc)
{
    return ipc && !ipc->isLocalConnection();
}

void list(IpcClient* ipc, const QString& path, QObject* ctx, Rows done)
{
    if (!remote(ipc)) {
        runOffThread(
            ctx,
            [path] {
                return ListResult{path.isEmpty() || QFileInfo(path).isDir(),
                                  path.isEmpty() ? folderRoots() : subfolders(path), QDir::homePath()};
            },
            [done = std::move(done)](const ListResult& r) { done(r.ok, r.rows, r.home); });
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
        runOffThread(ctx, [root] { return folderTree(root); },
                     [done = std::move(done)](const QStringList& paths) { done(paths); });
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
    // a GUI on macOS adds shares for a core on Windows: '\\' by hand, "//" kept
    return SharedDirState::cleanDir(typed);
}

} // namespace eMule::FolderLister
