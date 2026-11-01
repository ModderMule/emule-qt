/// @file Ed3kLiveWatch.cpp
/// @brief Watching an ed3k live stream from the GUI — implementation.

#include "pch.h"
#include "app/Ed3kLiveWatch.h"

#include "app/IpcClient.h"
#include "dialogs/Ed3kSetsDialog.h"
#include "IpcMessage.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QWidget>

namespace eMule::Ed3kLiveWatch {

namespace {

/// a fetch of the core ends within 20 s; the answer is not waited for longer than this
constexpr int kPushWaitMs = 2 * 60 * 1000;

QString tr(const char* text)
{
    return QCoreApplication::translate("eMule::Ed3kLiveWatch", text);
}

} // namespace

void showReply(QWidget* window, IpcClient* ipc, const Ipc::IpcMessage& reply)
{
    show(window, ipc, reply.fieldBool(0), reply.fieldMap(1));
}

void show(QWidget* window, IpcClient* ipc, bool ok, const QCborMap& data)
{
    if (!ok) {
        const QString why = data.value(QStringLiteral("error")).toString();
        if (window)
            QMessageBox::warning(window, tr("ed3k live stream"), why.isEmpty() ? tr("The stream could not be watched.") : why);
        return;
    }
    // the stream is served on the core's loopback: a remote GUI only lists it
    const QString url = data.value(QStringLiteral("url")).toString();
    if (!url.isEmpty() && ipc && ipc->isLocalConnection())
        QDesktopServices::openUrl(QUrl(url));
    if (window)
        Ed3kSetsDialog::showDialog(window, ipc, Ed3kSetsDialog::TabLive);
}

void watchFound(QWidget* window, IpcClient* ipc, const QString& root, quint64 size)
{
    if (!ipc)
        return;
    QPointer<QWidget> w(window);
    QPointer<IpcClient> client(ipc);
    ipc->watchEd3kLiveFile(root, size, [w, client, root](const Ipc::IpcMessage& reply) {
        const QCborMap data = reply.fieldMap(1);
        if (!reply.fieldBool(0) || !data.value(QStringLiteral("pending")).toBool()) {
            show(w, client, reply.fieldBool(0), data);
            return;
        }
        if (!client)
            return;
        // fetched by the core first: its answer comes as a push, once
        auto* waiter = new QObject(client);
        QTimer::singleShot(kPushWaitMs, waiter, &QObject::deleteLater);
        QObject::connect(client, &IpcClient::ed3kLiveFileWatched, waiter, [w, client, root, waiter](const Ipc::IpcMessage& msg) {
            if (msg.fieldString(0).compare(root, Qt::CaseInsensitive) != 0)
                return;
            show(w, client, msg.fieldBool(1), msg.fieldMap(2));
            waiter->deleteLater();
        });
    });
}

} // namespace eMule::Ed3kLiveWatch
