/// @file DaemonHosterWebBackend.cpp
/// @brief The web page's view of the hoster downloads — implementation.

#include "DaemonHosterWebBackend.h"

#include "Ed3kHosterBridge.h"
#include "IpcMessage.h"

#include <QCborMap>

namespace eMule {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

bool DaemonHosterWebBackend::available() const
{
    return ask(IpcMsgType::GetHosterItems, {}).has_value();
}

QCborArray DaemonHosterWebBackend::items() const
{
    return ask(IpcMsgType::GetHosterItems, {}).value_or(QCborValue{}).toMap().value(QStringLiteral("items")).toArray();
}

int DaemonHosterWebBackend::addLinks(const QString& text, const QString& package)
{
    const QCborValue r = ask(IpcMsgType::AddHosterLinks, {text, QCborArray{}, package}).value_or(QCborValue{});
    return static_cast<int>(r.toMap().value(QStringLiteral("ids")).toArray().size());
}

bool DaemonHosterWebBackend::action(quint32 id, const QString& op)
{
    return ask(IpcMsgType::HosterAction, {qint64(id), op}).has_value();
}

std::optional<QCborValue> DaemonHosterWebBackend::ask(IpcMsgType type, const QCborArray& fields)
{
    IpcMessage request(type, 1);
    for (const QCborValue& f : fields)
        request.append(f);
    IpcMessage answer;
    Ed3kHosterBridge::handleRequest(request, [&answer](const IpcMessage& m) { answer = m; });
    if (answer.type() != IpcMsgType::Result || !answer.fieldBool(0))
        return std::nullopt;
    return answer.fieldCount() > 1 ? answer.field(1) : QCborValue(QCborValue::Null);
}

} // namespace eMule
