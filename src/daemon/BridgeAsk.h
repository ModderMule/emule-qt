#pragma once

/// @file BridgeAsk.h
/// @brief One IPC request answered in place by a bridge's handleRequest, for the web
///        backends: they reuse the IPC's one implementation instead of a second copy.

#include "IpcMessage.h"

#include <QCborArray>
#include <QCborMap>
#include <QCborValue>
#include <QString>

#include <expected>
#include <utility>

namespace eMule {

/// The data of a successful Result (null when it has none), else the error it names.
template <typename Handler>
[[nodiscard]] std::expected<QCborValue, QString> askBridge(Handler&& handler, Ipc::IpcMsgType type,
                                                           const QCborArray& fields)
{
    Ipc::IpcMessage request(type, 1);
    for (const QCborValue& f : fields)
        request.append(f);
    Ipc::IpcMessage answer;
    std::forward<Handler>(handler)(request, [&answer](const Ipc::IpcMessage& m) { answer = m; });
    const QCborValue data = answer.fieldCount() > 1 ? answer.field(1) : QCborValue(QCborValue::Null);
    if (answer.type() == Ipc::IpcMsgType::Result && answer.fieldBool(0))
        return data;
    const QString why = data.isMap() ? data.toMap().value(QStringLiteral("error")).toString() : data.toString();
    return std::unexpected(why);
}

} // namespace eMule
