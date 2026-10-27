/// @file Ed3kFriendsBridge.cpp
/// @brief ed3k friends and chat over IPC — implementation.

#include "Ed3kFriendsBridge.h"

#include <QCborArray>
#include <QCborMap>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kFriends.h"

#include <QHostAddress>
#include <QObject>
#endif

namespace eMule::Ed3kFriendsBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

ed3k::Ed3kFriends* s_friends = nullptr;

/// error 0: no ed3k node (not built or not running); else an ed3k::Error.
IpcMessage failure(int seqId, int error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

#ifdef EMULE_HAVE_ED3K
namespace ffi = ::ed3k;

QString hexOf(std::span<const std::uint8_t> bytes)
{
    return QString::fromLatin1(ed3k::Ed3kFriends::toBytes(bytes).toHex());
}

QString addressText(const ffi::Address& a)
{
    QHostAddress ip;
    if (a.isV4()) {
        ip = QHostAddress((quint32(a.ip[12]) << 24) | (quint32(a.ip[13]) << 16) | (quint32(a.ip[14]) << 8) | quint32(a.ip[15]));
        return QStringLiteral("%1:%2").arg(ip.toString()).arg(a.port);
    }
    Q_IPV6ADDR v6;
    std::copy(a.ip.begin(), a.ip.end(), v6.c);
    return QStringLiteral("[%1]:%2").arg(QHostAddress(v6).toString()).arg(a.port);
}

QCborMap friendsToCbor(ed3k::Ed3kFriends& f)
{
    QCborArray list;
    for (const auto& row : f.friends()) {
        QCborMap m{
            {QStringLiteral("key"), hexOf(row.key)},
            {QStringLiteral("node"), hexOf(row.node.bytes)},
            {QStringLiteral("name"), row.name},
            {QStringLiteral("mutual"), row.mutual},
            {QStringLiteral("asking"), row.asking},
            {QStringLiteral("heldLink"), row.heldLink},
            {QStringLiteral("lastSeen"), row.lastSeen},
        };
        if (row.addr)
            m.insert(QStringLiteral("addr"), addressText(*row.addr));
        list.append(m);
    }
    QCborArray requests;
    for (const auto& r : f.requests())
        requests.append(QCborMap{
            {QStringLiteral("key"), hexOf(r.key)},
            {QStringLiteral("node"), hexOf(r.node.bytes)},
            {QStringLiteral("text"), r.text},
            {QStringLiteral("issued"), qint64(r.issued)},
        });
    return QCborMap{
        {QStringLiteral("running"), f.isRunning()},
        {QStringLiteral("friends"), list},
        {QStringLiteral("requests"), requests},
    };
}

void answer(const IpcMessage& msg, ed3k::Ed3kFriends& f, const std::function<void(const IpcMessage&)>& reply)
{
    const int seq = msg.seqId();
    const auto ok = [&](const QCborValue& data = {}) { reply(IpcMessage::makeResult(seq, true, data)); };
    const auto fail = [&](ffi::Error e) { reply(failure(seq, int(e))); };
    const auto keyArg = [&] { return ed3k::Ed3kFriends::keyFrom(QByteArray::fromHex(msg.fieldString(0).toLatin1())); };
    const auto nodeArg = [&] { return ed3k::Ed3kFriends::nodeFrom(QByteArray::fromHex(msg.fieldString(0).toLatin1())); };

    if (msg.type() == IpcMsgType::GetEd3kFriends) {
        ok(friendsToCbor(f));
        return;
    }
    if (!f.isRunning()) {
        reply(failure(seq, 0));
        return;
    }
    switch (msg.type()) {
    case IpcMsgType::AddEd3kFriend:
        if (auto key = f.add(msg.fieldString(0), msg.fieldString(1)))
            ok(QCborMap{{QStringLiteral("key"), hexOf(*key)}});
        else
            fail(key.error());
        break;
    case IpcMsgType::RemoveEd3kFriend:
    case IpcMsgType::AnswerEd3kFriendRequest: {
        const auto key = keyArg();
        if (!key) {
            fail(ffi::Error::Argument);
            break;
        }
        const auto r = msg.type() == IpcMsgType::RemoveEd3kFriend ? f.remove(*key) : f.answerRequest(*key, msg.fieldBool(1));
        r ? ok() : fail(r.error());
        break;
    }
    case IpcMsgType::GetEd3kMyFriendLink:
        if (auto link = f.myLink(msg.fieldBool(0), msg.fieldString(1)))
            ok(*link);
        else
            fail(link.error());
        break;
    case IpcMsgType::SendEd3kChat: {
        const auto node = nodeArg();
        if (!node) {
            fail(ffi::Error::Argument);
            break;
        }
        if (auto ids = f.sendChat(*node, msg.fieldString(1))) {
            QCborArray out;
            for (const std::uint64_t id : *ids)
                out.append(qint64(id));
            ok(out);
        } else {
            fail(ids.error());
        }
        break;
    }
    case IpcMsgType::EndEd3kChat:
        if (const auto node = nodeArg())
            f.endChat(*node);
        ok();
        break;
    default:
        reply(IpcMessage::makeError(seq, 400, QStringLiteral("Unknown ed3k request")));
        break;
    }
}
#endif

} // namespace

void setFriends(ed3k::Ed3kFriends* friends)
{
    s_friends = friends;
}

bool isFriendsRequest(IpcMsgType type)
{
    const int t = static_cast<int>(type);
    return t >= 1001 && t <= 1009;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isFriendsRequest(msg.type()))
        return false;
#ifdef EMULE_HAVE_ED3K
    if (s_friends) {
        answer(msg, *s_friends, reply);
        return true;
    }
#endif
    if (msg.type() == IpcMsgType::GetEd3kFriends)
        reply(IpcMessage::makeResult(msg.seqId(), true,
                                     QCborMap{{QStringLiteral("running"), false},
                                              {QStringLiteral("friends"), QCborArray{}},
                                              {QStringLiteral("requests"), QCborArray{}}}));
    else
        reply(failure(msg.seqId(), 0));
    return true;
}

void connectPushes(ed3k::Ed3kFriends& friends, std::function<void(const IpcMessage&)> broadcast)
{
#ifdef EMULE_HAVE_ED3K
    using F = ed3k::Ed3kFriends;
    const auto hex = [](const QByteArray& b) { return QString::fromLatin1(b.toHex()); };
    QObject::connect(&friends, &F::friendsChanged, &friends, [broadcast] {
        broadcast(IpcMessage(IpcMsgType::PushEd3kFriendsChanged, 0));
    });
    QObject::connect(&friends, &F::chatMessage, &friends,
                     [broadcast, hex](const QByteArray& node, const QString& name, const QString& text, quint64 id) {
                         IpcMessage m(IpcMsgType::PushEd3kChatMessage, 0);
                         m.append(hex(node));
                         m.append(name);
                         m.append(text);
                         m.append(qint64(id));
                         broadcast(m);
                     });
    QObject::connect(&friends, &F::chatSent, &friends, [broadcast, hex](const QByteArray& node, quint64 id) {
        IpcMessage m(IpcMsgType::PushEd3kChatSent, 0);
        m.append(hex(node));
        m.append(qint64(id));
        broadcast(m);
    });
    QObject::connect(&friends, &F::chatFailed, &friends,
                     [broadcast, hex](const QByteArray& node, quint64 id, int why, quint32 retryAfter) {
                         IpcMessage m(IpcMsgType::PushEd3kChatFailed, 0);
                         m.append(hex(node));
                         m.append(qint64(id));
                         m.append(qint64(why));
                         m.append(qint64(retryAfter));
                         broadcast(m);
                     });
    QObject::connect(&friends, &F::friendRequest, &friends,
                     [broadcast, hex](const QByteArray& key, const QByteArray& node, const QString& text, quint64 issued) {
                         IpcMessage m(IpcMsgType::PushEd3kFriendRequest, 0);
                         m.append(hex(key));
                         m.append(hex(node));
                         m.append(text);
                         m.append(qint64(issued));
                         broadcast(m);
                     });
    QObject::connect(&friends, &F::chatProgress, &friends, [broadcast, hex](const QByteArray& node, int progress) {
        IpcMessage m(IpcMsgType::PushEd3kChatState, 0);
        m.append(hex(node));
        m.append(qint64(progress));
        broadcast(m);
    });
#else
    (void)friends;
    (void)broadcast;
#endif
}

} // namespace eMule::Ed3kFriendsBridge
