#include "pch.h"
/// @file ChatNetwork.cpp
/// @brief eD2K and ed3k behind the Messages panel — implementation.

#include "panels/ChatNetwork.h"

#include "app/IpcClient.h"

namespace eMule {

using namespace Ipc;

FriendNetwork ChatNetwork::networkOf(const QString& key)
{
    return FriendListModel::ed3kNodeOf(key).isEmpty() ? FriendNetwork::Ed2k : FriendNetwork::Ed3k;
}

bool ChatNetwork::online() const
{
    return m_ipc && m_ipc->isConnected();
}

// ---------------------------------------------------------------------------
// eD2K
// ---------------------------------------------------------------------------

void Ed2kChatNetwork::send(const QString& key, const QString& text, std::function<void(bool)> done)
{
    IpcMessage msg(IpcMsgType::SendChatMessage);
    msg.append(key);
    msg.append(text);
    m_ipc->sendRequest(std::move(msg), [done = std::move(done)](const IpcMessage& resp) {
        // An Error reply arrives through this same callback, and its field 0 is the
        // numeric code — reading it as a bool silently swallowed the failure, so a
        // message to an offline friend simply vanished.
        done(resp.type() != IpcMsgType::Error && resp.fieldBool(0));
    });
}

void Ed2kChatNetwork::endSession(const QString& key)
{
    if (!m_ipc || key.isEmpty())
        return;
    IpcMessage msg(IpcMsgType::EndChatSession);
    msg.append(key);
    m_ipc->sendRequest(std::move(msg));
}

void Ed2kChatNetwork::removeFriend(const FriendRow& row, std::function<void()> done)
{
    if (!online())
        return;
    IpcMessage msg(IpcMsgType::RemoveFriend);
    msg.append(row.hash);
    // a friend added by IP has no hash: the daemon matches address + port
    msg.append(row.addr);
    msg.append(qint64(row.port));
    m_ipc->sendRequest(std::move(msg), [done = std::move(done)](const IpcMessage&) { done(); });
}

void Ed2kChatNetwork::requestFriends(QObject* owner, std::function<void(const QCborArray&, const QCborMap&)> done)
{
    if (!online())
        return;
    m_ipc->sendPoll(owner, IpcMessage(IpcMsgType::GetFriends), [done = std::move(done)](const IpcMessage& resp) {
        if (resp.fieldBool(0))   // else keep the list: a dropped connection would otherwise empty it
            done(resp.fieldArray(1), {});
    });
}

// ---------------------------------------------------------------------------
// ed3k
// ---------------------------------------------------------------------------

void Ed3kChatNetwork::send(const QString& key, const QString& text, std::function<void(bool)> done)
{
    IpcMessage msg(IpcMsgType::SendEd3kChat);
    msg.append(FriendListModel::ed3kNodeOf(key));
    msg.append(text);
    // ok: queued; delivery comes as PushEd3kChatSent / _Failed
    m_ipc->sendRequest(std::move(msg), [done = std::move(done)](const IpcMessage& resp) {
        done(resp.type() != IpcMsgType::Error && resp.fieldBool(0));
    });
}

void Ed3kChatNetwork::endSession(const QString& key)
{
    if (!m_ipc || key.isEmpty())
        return;
    IpcMessage msg(IpcMsgType::EndEd3kChat);
    msg.append(FriendListModel::ed3kNodeOf(key));
    m_ipc->sendRequest(std::move(msg));
}

void Ed3kChatNetwork::removeFriend(const FriendRow& row, std::function<void()> done)
{
    if (!online())
        return;
    IpcMessage msg(IpcMsgType::RemoveEd3kFriend);
    msg.append(row.id);
    m_ipc->sendRequest(std::move(msg), [done = std::move(done)](const IpcMessage&) { done(); });
}

void Ed3kChatNetwork::requestFriends(QObject* owner, std::function<void(const QCborArray&, const QCborMap&)> done)
{
    if (!online())
        return;
    m_ipc->sendPoll(owner, IpcMessage(IpcMsgType::GetEd3kFriends), [done = std::move(done)](const IpcMessage& resp) {
        if (resp.type() == IpcMsgType::Error || !resp.fieldBool(0))
            return;
        const QCborMap m = resp.fieldMap(1);
        done(m.value(QStringLiteral("friends")).toArray(), m);
    });
}

} // namespace eMule
