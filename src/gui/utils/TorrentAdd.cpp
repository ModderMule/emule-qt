#include "pch.h"
/// @file TorrentAdd.cpp
/// @brief The one BitTorrent add exchange, shared by every GUI intake path.

#include "utils/TorrentAdd.h"

#include "app/IpcClient.h"
#include "utils/StatusBarNotifier.h"

#include <QApplication>
#include <QCborMap>
#include <QMessageBox>
#include <QPointer>
#include <QTimer>

#include <utility>

namespace eMule::gui {

QString btAddOutcomeText(BtAddOutcome outcome, const QString& title, const QString& detail)
{
    switch (outcome) {
    case BtAddOutcome::Added:
        return QObject::tr("Torrent \"%1\" added.").arg(title);
    case BtAddOutcome::Duplicate:
        return QObject::tr("\"%1\" is already in the torrent list.").arg(title);
    case BtAddOutcome::Invalid:
        return detail.isEmpty()
                   ? QObject::tr("\"%1\" is not a valid torrent or magnet link.").arg(title)
                   : QObject::tr("\"%1\" is not a valid torrent or magnet link.\n\n%2")
                         .arg(title, detail);
    case BtAddOutcome::NotRunning:
        return QObject::tr("BitTorrent is off — enable it in Options to add \"%1\".").arg(title);
    case BtAddOutcome::Failed:
        break;
    }
    return detail.isEmpty() ? QObject::tr("Could not add \"%1\".").arg(title)
                            : QObject::tr("Could not add \"%1\".\n\n%2").arg(title, detail);
}

BtAddReply decodeTorrentAddReply(const Ipc::IpcMessage& resp)
{
    BtAddReply out;
    if (!resp.isValid()) {
        out.dropped = true;
        return out;
    }
    if (resp.type() == Ipc::IpcMsgType::Error) {
        // 503: the engine is not built or not running
        out.outcome = resp.fieldInt(0) == 503 ? BtAddOutcome::NotRunning : BtAddOutcome::Failed;
        out.detail = resp.fieldString(1);
        return out;
    }
    if (!resp.fieldBool(0))
        return out;
    const QCborMap result = resp.fieldMap(1);
    const auto raw = result.value(QStringLiteral("outcome")).toInteger(int(BtAddOutcome::Failed));
    out.outcome = raw >= 0 && raw <= int(BtAddOutcome::Failed) ? static_cast<BtAddOutcome>(raw)
                                                               : BtAddOutcome::Failed;
    out.id = result.value(QStringLiteral("id")).toString();
    out.detail = result.value(QStringLiteral("message")).toString();
    return out;
}

Ipc::IpcMessage torrentFileRequest(const QByteArray& data, const QString& savePath, bool paused)
{
    Ipc::IpcMessage msg(Ipc::IpcMsgType::AddTorrentFile);
    msg.append(QCborValue(data));
    msg.append(savePath);
    msg.append(paused);
    return msg;
}

Ipc::IpcMessage magnetRequest(const QString& uri, const QString& savePath, bool paused)
{
    Ipc::IpcMessage msg(Ipc::IpcMsgType::AddMagnet);
    msg.append(uri);
    msg.append(savePath);
    msg.append(paused);
    return msg;
}

void sendTorrentAdd(IpcClient* ipc, QWidget* parent, const QString& title,
                    Ipc::IpcMessage request, std::function<void(bool added)> done)
{
    if (!ipc || !ipc->isConnected()) {
        if (done)
            done(false);
        return;
    }

    const QPointer<QWidget> safeParent(parent);
    ipc->sendRequest(std::move(request),
        [safeParent, title, done = std::move(done)](const Ipc::IpcMessage& resp) {
        const BtAddReply reply = decodeTorrentAddReply(resp);
        if (reply.dropped) {
            if (done)
                done(false);
            return;
        }
        const BtAddOutcome outcome = reply.outcome;
        const QString text = btAddOutcomeText(outcome, title, reply.detail);
        if (outcome == BtAddOutcome::Added || outcome == BtAddOutcome::Duplicate) {
            StatusBarNotifier::post(text, 5000);
            if (done)
                done(outcome == BtAddOutcome::Added);
            return;
        }

        QTimer::singleShot(0, qApp, [safeParent, text] {
            QMessageBox::warning(safeParent, QObject::tr("Add Torrent"), text);
        });
        if (done)
            done(false);
    });
}

} // namespace eMule::gui
