#include "pch.h"
#include "utils/ArchiveContentsIpc.h"

#include "IpcMessage.h"
#include "IpcProtocol.h"
#include "app/IpcClient.h"
#include "controls/ArchiveContentsView.h"

#include <QCborArray>
#include <QMessageBox>
#include <QPointer>

namespace eMule::ArchiveContentsIpc {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

void send(IpcClient* ipc, const QString& kind, const QCborMap& payload, std::function<void(bool, const QCborMap&, const QString&)> done)
{
    if (!ipc || !ipc->isConnected()) {
        done(false, {}, ArchiveContentsView::tr("Not connected to the core."));
        return;
    }
    IpcMessage msg(IpcMsgType::LookupArchiveContents);
    msg.append(kind);
    msg.append(payload);
    ipc->sendRequest(std::move(msg), [done = std::move(done)](const IpcMessage& resp) {
        if (!resp.isValid())
            done(false, {}, {});
        else if (!resp.fieldBool(0))
            done(false, {}, resp.fieldString(1));
        else
            done(true, resp.fieldMap(1), {});
    });
}

} // namespace

QCborMap entriesToCbor(const QHash<QString, QStringList>& entries)
{
    QCborMap out;
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
        out.insert(it.key(), QCborArray::fromStringList(it.value()));
    return out;
}

void attach(ArchiveContentsView* view, IpcClient* ipc, RequestFn request, QString folder, int category)
{
    QPointer<ArchiveContentsView> guard(view);
    QPointer<IpcClient> client(ipc);
    QObject::connect(view, &ArchiveContentsView::lookupRequested, view, [guard, client, request] {
        const auto [kind, payload] = request();
        send(client, kind, payload, [guard](bool ok, const QCborMap& result, const QString& error) {
            if (!guard)
                return;
            if (ok)
                guard->setResult(result);
            else
                guard->setError(error);
        });
    });
    QObject::connect(view, &ArchiveContentsView::nestedLookupRequested, view, [guard, client](const QString& name, const QString& root) {
        const QCborMap payload{{QStringLiteral("volumes"),
                                QCborArray{QCborMap{{QStringLiteral("name"), name}, {QStringLiteral("root"), root}}}}};
        send(client, QStringLiteral("roots"), payload, [guard, root](bool ok, const QCborMap& result, const QString& error) {
            if (!guard)
                return;
            if (ok)
                guard->setNestedResult(root, result);
            else
                guard->setNestedError(root, error);
        });
    });
    QObject::connect(view, &ArchiveContentsView::downloadEntriesRequested, view,
                     [guard, client, folder, category](const QCborArray& entries) {
        if (!client || !client->isConnected() || entries.isEmpty())
            return;
        IpcMessage msg(IpcMsgType::DownloadArchiveEntries);
        msg.append(entries);
        msg.append(folder);
        msg.append(qint64(category));
        client->sendRequest(std::move(msg), [guard](const IpcMessage& resp) {
            if (!guard)
                return;
            if (!resp.isValid() || !resp.fieldBool(0)) {
                QMessageBox::warning(guard, ArchiveContentsView::tr("Archive"),
                                     resp.isValid() ? resp.fieldString(1) : ArchiveContentsView::tr("The core did not answer."));
                return;
            }
            const int added = int(resp.fieldMap(1).value(QStringLiteral("added")).toInteger());
            guard->statusLabel()->setText(ArchiveContentsView::tr("%n file(s) added to the ed3k downloads.", nullptr, added));
        });
    });
}

} // namespace eMule::ArchiveContentsIpc
