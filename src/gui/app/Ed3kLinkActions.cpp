/// @file Ed3kLinkActions.cpp
/// @brief Small ed3k link actions — implementation.

#include "app/Ed3kLinkActions.h"

#include "app/IpcClient.h"
#include "IpcMessage.h"
#include "utils/Log.h"

#include <QCborValue>
#include <QCoreApplication>
#include <QFile>
#include <QMessageBox>
#include <QPointer>
#include <QWidget>

namespace eMule::Ed3kLinkActions {

namespace {

/// the translation context (lupdate reads Q_DECLARE_TR_FUNCTIONS)
struct Words {
    Q_DECLARE_TR_FUNCTIONS(Ed3kLinkActions)
};

} // namespace

QString errorText(int code)
{
    switch (code) {
    case 0:   return Words::tr("The ed3k node is not running.");
    case -10: return Words::tr("The ed3k link is invalid.");
    case -11: return Words::tr("The ed3k link is too long.");
    case -12: return Words::tr("The signature of the file does not verify.");
    case -13: return Words::tr("This is switched off for ed3k.");
    case -14: return Words::tr("The file is too old.");
    case -15: return Words::tr("The ed3k node is not ready for this.");
    case -16: return Words::tr("Too many of these are in use.");
    default:  return Words::tr("ed3k error %1.").arg(code);
    }
}

QString replyError(const Ipc::IpcMessage& reply)
{
    if (reply.type() == Ipc::IpcMsgType::Error)
        return errorText(0);
    return errorText(int(reply.fieldMap(1).value(QStringLiteral("error")).toInteger()));
}

void sendNodeListFile(QWidget* parent, IpcClient* ipc, const QString& path)
{
    // bootstrap.md §2: a list is at most 32 KiB
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 64 * 1024) {
        QMessageBox::warning(parent, Words::tr("ed3k node list"), Words::tr("The file %1 could not be read.").arg(path));
        return;
    }
    if (!ipc || !ipc->isConnected()) {
        QMessageBox::warning(parent, Words::tr("ed3k node list"), errorText(0));
        return;
    }
    Ipc::IpcMessage msg(Ipc::IpcMsgType::OpenEd3kNodeListFile);
    msg.append(QCborValue(f.readAll()));
    QPointer<QWidget> window = parent;
    ipc->sendRequest(std::move(msg), [window](const Ipc::IpcMessage& reply) {
        if (reply.type() == Ipc::IpcMsgType::Result && reply.fieldBool(0)) {
            const qint64 nodes = reply.fieldMap(1).value(QStringLiteral("nodes")).toInteger();
            logInfo(QStringLiteral("ed3k node list file: %1 node(s) added").arg(nodes));
            QMessageBox::information(window, Words::tr("ed3k node list"), Words::tr("%n ed3k node(s) of the list are tried.", nullptr, int(nodes)));
            return;
        }
        QMessageBox::warning(window, Words::tr("ed3k node list"), Words::tr("The node list was not used.\n\n%1").arg(replyError(reply)));
    });
}

bool isNodeListPath(const QString& path)
{
    return path.endsWith(QStringLiteral(".ed3knod"), Qt::CaseInsensitive);
}

} // namespace eMule::Ed3kLinkActions
