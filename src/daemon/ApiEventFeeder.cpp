/// @file ApiEventFeeder.cpp
/// @brief Push -> event mapping.

#include "ApiEventFeeder.h"

#include "IpcProtocol.h"
#include "search/SearchQueue.h"
#include "webserver/ApiEventHub.h"

#include <QCborMap>
#include <QDateTime>

namespace eMule {

using Ipc::IpcMsgType;

namespace {

constexpr int kHintWindowMs = 1000;

/// The fields of a state push that say something changed, not the counters
/// that tick all the time.
[[nodiscard]] QJsonObject pick(const QCborMap& map, std::initializer_list<const char*> keys)
{
    QJsonObject out;
    for (const char* key : keys) {
        const QCborValue v = map.value(QLatin1StringView(key));
        if (!v.isUndefined())
            out.insert(QLatin1StringView(key), v.toJsonValue());
    }
    return out;
}

} // namespace

ApiEventFeeder::ApiEventFeeder(ApiEventHub* hub, QObject* parent)
    : QObject(parent)
    , m_hub(hub)
{
    m_flushTimer.setSingleShot(true);
    m_flushTimer.setInterval(kHintWindowMs);
    connect(&m_flushTimer, &QTimer::timeout, this, &ApiEventFeeder::flush);
}

void ApiEventFeeder::onPush(const Ipc::IpcMessage& msg)
{
    switch (msg.type()) {
    case IpcMsgType::PushDownloadAdded:
    case IpcMsgType::PushDownloadRemoved:
    case IpcMsgType::PushDownloadUpdate:
        hint(QStringLiteral("downloads.changed"));
        break;
    case IpcMsgType::PushServerState:
        state(QStringLiteral("connection.changed"),
              pick(msg.fieldMap(0), {"connected", "connecting", "lowID", "firewalled", "netBlocked",
                                     "netBlockReason", "serverName", "serverAddress", "serverPort"}));
        break;
    case IpcMsgType::PushSearchResult:
        hint(QStringLiteral("search.results"));
        break;
    case IpcMsgType::PushSearchState:
        event(QStringLiteral("search.state"), QJsonObject{
            {QStringLiteral("searchID"), static_cast<qint64>(msg.fieldInt(0))},
            {QStringLiteral("state"),
             searchRunStateName(static_cast<SearchRunState>(msg.fieldInt(1)))},
        });
        break;
    case IpcMsgType::PushUploadUpdate:
        hint(QStringLiteral("uploads.changed"));
        break;
    case IpcMsgType::PushKadUpdate:
        state(QStringLiteral("kad.changed"),
              pick(msg.fieldMap(0), {"running", "connected", "firewalled"}));
        break;
    case IpcMsgType::PushFriendListChanged:
        hint(QStringLiteral("friends.changed"));
        break;
    case IpcMsgType::PushPortMapStatus:
        state(QStringLiteral("nat.changed"),
              pick(msg.fieldMap(0), {"statusText", "methodText", "externalAddress"}));
        break;
    case IpcMsgType::PushCategoriesChanged:
        hint(QStringLiteral("categories.changed"));
        break;
    case IpcMsgType::PushUsenetQueueItem:
    case IpcMsgType::PushUsenetItemRemoved:
    case IpcMsgType::PushUsenetEngineState:
        hint(QStringLiteral("usenet.changed"));
        break;
    case IpcMsgType::PushUsenetItemFinished:
        event(QStringLiteral("usenet.finished"), QJsonObject{
            {QStringLiteral("id"), msg.fieldString(0)},
            {QStringLiteral("success"), msg.fieldBool(1)},
            {QStringLiteral("message"), msg.fieldString(2)},
        });
        hint(QStringLiteral("usenet.changed"));
        break;
    default:
        // Statistics, log lines, chat, shared-file rows: polled, private or too chatty.
        break;
    }
}

void ApiEventFeeder::hint(const QString& type)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastHintMs.value(type, 0) >= kHintWindowMs) {
        m_lastHintMs.insert(type, now);
        m_hub->publish(type);
        return;
    }
    m_pendingHints.insert(type);
    if (!m_flushTimer.isActive())
        m_flushTimer.start();
}

void ApiEventFeeder::state(const QString& type, const QJsonObject& data)
{
    if (m_lastState.value(type) == data && m_lastState.contains(type))
        return;
    m_lastState.insert(type, data);
    m_hub->publish(type, data);
}

void ApiEventFeeder::event(const QString& type, const QJsonObject& data)
{
    m_hub->publish(type, data);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void ApiEventFeeder::flush()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const QSet<QString> pending = std::exchange(m_pendingHints, {});
    for (const QString& type : pending) {
        m_lastHintMs.insert(type, now);
        m_hub->publish(type);
    }
}

} // namespace eMule
