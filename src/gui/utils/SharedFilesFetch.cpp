#include "pch.h"
/// @file SharedFilesFetch.cpp
/// @brief Fetch the daemon's whole shared-files list, a page at a time.

#include "utils/SharedFilesFetch.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QCborMap>
#include <QPointer>

#include <memory>

namespace eMule {

namespace {

/// Rows per request: about 2 MB, an eighth of the frame limit.
constexpr qint64 kPageRows = 2000;

struct Fetch {
    QPointer<IpcClient> ipc;
    QPointer<QObject> context;
    std::function<void(bool, const QCborArray&)> done;
    QCborArray rows;
};

void requestPage(const std::shared_ptr<Fetch>& fetch, const QString& afterHash)
{
    if (!fetch->ipc || !fetch->context)
        return;
    if (!fetch->ipc->isConnected()) {
        fetch->done(false, {});
        return;
    }

    Ipc::IpcMessage req(Ipc::IpcMsgType::GetSharedFiles);
    req.append(afterHash);
    req.append(kPageRows);
    fetch->ipc->sendRequest(std::move(req), [fetch](const Ipc::IpcMessage& resp) {
        if (!fetch->context)
            return;
        if (resp.type() != Ipc::IpcMsgType::Result || !resp.fieldBool(0)) {
            fetch->done(false, {});
            return;
        }
        const QCborMap result = resp.fieldMap(1);
        const QCborArray page = result.value(QStringLiteral("files")).toArray();
        for (const auto& row : page)
            fetch->rows.append(row);

        if (!page.isEmpty() && result.value(QStringLiteral("more")).toBool()) {
            const QString last = page.last().toMap().value(QStringLiteral("hash")).toString();
            requestPage(fetch, last);
            return;
        }
        fetch->done(true, fetch->rows);
    });
}

void requestKnownPage(const std::shared_ptr<Fetch>& fetch, qint64 offset)
{
    if (!fetch->ipc || !fetch->context)
        return;
    if (!fetch->ipc->isConnected()) {
        fetch->done(false, {});
        return;
    }

    Ipc::IpcMessage req(Ipc::IpcMsgType::GetKnownFiles);
    req.append(offset);
    fetch->ipc->sendRequest(std::move(req), [fetch](const Ipc::IpcMessage& resp) {
        if (!fetch->context)
            return;
        if (resp.type() != Ipc::IpcMsgType::Result || !resp.fieldBool(0)) {
            fetch->done(false, {});
            return;
        }
        const QCborMap result = resp.fieldMap(1);
        const QCborArray page = result.value(QStringLiteral("files")).toArray();
        for (const auto& row : page)
            fetch->rows.append(row);

        if (!page.isEmpty() && result.value(QStringLiteral("more")).toBool()) {
            requestKnownPage(fetch, result.value(QStringLiteral("next")).toInteger());
            return;
        }
        fetch->done(true, fetch->rows);
    });
}

} // namespace

void fetchKnownFileRows(IpcClient* ipc, QObject* context,
                        std::function<void(bool ok, const QCborArray& rows)> done)
{
    auto fetch = std::make_shared<Fetch>();
    fetch->ipc = ipc;
    fetch->context = context;
    fetch->done = std::move(done);
    requestKnownPage(fetch, 0);
}

void fetchSharedFileRows(IpcClient* ipc, QObject* context,
                         std::function<void(bool ok, const QCborArray& rows)> done)
{
    auto fetch = std::make_shared<Fetch>();
    fetch->ipc = ipc;
    fetch->context = context;
    fetch->done = std::move(done);
    requestPage(fetch, {});
}

} // namespace eMule
