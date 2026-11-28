/// @file DaemonEd3kWebBackend.cpp
/// @brief The REST API's view of the ed3k downloads — implementation.

#include "DaemonEd3kWebBackend.h"

#include "BridgeAsk.h"
#include "Ed3kBridge.h"
#include "Ed3kDownloadsBridge.h"

#include <QCborMap>

namespace eMule {

using Ipc::IpcMsgType;

namespace {

Ed3kWebBackend::AddResult addResult(const std::expected<QCborValue, QString>& r)
{
    if (!r)
        return {{}, r.error()};
    return {r->toMap().value(QStringLiteral("root")).toString(), {}};
}

} // namespace

bool DaemonEd3kWebBackend::available() const
{
    // GetEd3kDownloads answers even without a node: "running" says whether one is there
    const auto r = askBridge(Ed3kDownloadsBridge::handleRequest, IpcMsgType::GetEd3kDownloads, {});
    return r && r->toMap().value(QStringLiteral("running")).toBool();
}

QCborArray DaemonEd3kWebBackend::downloads() const
{
    const auto r = askBridge(Ed3kDownloadsBridge::handleRequest, IpcMsgType::GetEd3kDownloads, {});
    return r ? r->toMap().value(QStringLiteral("downloads")).toArray() : QCborArray{};
}

QCborMap DaemonEd3kWebBackend::status() const
{
    const auto r = askBridge(Ed3kBridge::handleRequest, IpcMsgType::GetEd3kStatus, {});
    return r ? r->toMap() : QCborMap{};
}

Ed3kWebBackend::AddResult DaemonEd3kWebBackend::add(const QString& hexRoot, qint64 size, const QString& name,
                                                    int category, bool paused)
{
    return addResult(askBridge(Ed3kDownloadsBridge::handleRequest, IpcMsgType::AddEd3kDownload,
                               {hexRoot, size, name, category, paused}));
}

Ed3kWebBackend::AddResult DaemonEd3kWebBackend::addLink(const QString& link, int category, bool paused)
{
    return addResult(askBridge(Ed3kDownloadsBridge::handleRequest, IpcMsgType::AddEd3kDownloadLink,
                               {link, category, paused}));
}

bool DaemonEd3kWebBackend::control(const QString& hexRoot, Control action, qint64 arg)
{
    const auto r = askBridge(Ed3kDownloadsBridge::handleRequest, IpcMsgType::Ed3kDownloadControl,
                             {hexRoot, static_cast<int>(action), arg});
    // a done action answers true; pause of a paused one answers false, not an error
    return r.has_value() && (r->isNull() || r->toBool() || action == Control::Pause || action == Control::Resume);
}

QString DaemonEd3kWebBackend::moveTo(const QString& hexRoot, const QString& dir)
{
    return Ed3kDownloadsBridge::moveTo(hexRoot, dir);
}

QString DaemonEd3kWebBackend::rename(const QString& hexRoot, const QString& name)
{
    return Ed3kDownloadsBridge::rename(hexRoot, name);
}

} // namespace eMule
