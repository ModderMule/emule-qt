/// @file tst_Ed3kFriends.cpp
/// @brief ed3k friends and chat (src/ed3k/Ed3kFriends) and their IPC (Ed3kFriendsBridge):
/// three nodes on loopback (C the Kad router between them); A asks B by B's link, B
/// accepts, A finds B by its record and both chat; the IPC messages through CBOR frames.

#include "Ed3kBridge.h"
#include "Ed3kFriends.h"
#include "Ed3kFriendsBridge.h"
#include "Ed3kSession.h"

#include "IpcProtocol.h"

#include <QDeadlineTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;
using eMule::ed3k::Ed3kFriends;
using eMule::ed3k::Ed3kSession;
using eMule::ed3k::Ed3kSettings;
using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

Ed3kSettings loopback(const QString& dir, std::uint16_t port = 0)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.udpPort = port;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");
    s.forceOpen = true;
    return s;
}

/// A bootstraps from B; true: A has a contact.
bool bootstrap(Ed3kSession& a, Ed3kSession& b)
{
    const auto bId = b.nodeId();
    if (!bId || !a.engine())
        return false;
    const ::ed3k::Node bNode{*bId, ::ed3k::Address::v4(127, 0, 0, 1, b.udpPort())};
    // a second try: the first lookup may go out while B is busy with another node
    for (int attempt = 0; attempt < 2 && a.contactCount() == 0; ++attempt) {
        bool started = false;
        a.post([&](::ed3k::Engine& e, ::ed3k::Now now) { started = e.bootstrap(now, std::span<const ::ed3k::Node>{&bNode, 1}).has_value(); });
        QDeadlineTimer deadline(15000);
        while (started && a.contactCount() == 0 && !deadline.hasExpired())
            QTest::qWait(50);
    }
    return a.contactCount() > 0;
}

QByteArray nodeBytes(const Ed3kSession& s)
{
    const auto id = s.nodeId();
    return id ? Ed3kFriends::toBytes(id->bytes) : QByteArray();
}

/// The message as the GUI gets it: through a CBOR frame.
IpcMessage roundTrip(const IpcMessage& msg)
{
    const auto decoded = Ipc::tryDecodeFrame(msg.toFrame());
    return decoded ? IpcMessage(decoded->message) : IpcMessage();
}

/// Ed3kBridge's answer to @p msg, through a frame both ways.
IpcMessage ask(const IpcMessage& msg)
{
    IpcMessage out;
    const bool taken = Ed3kBridge::handleRequest(roundTrip(msg), [&](const IpcMessage& r) { out = roundTrip(r); });
    return taken ? out : IpcMessage();
}

} // namespace

class tst_Ed3kFriends : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void withoutNodeEveryRequestFails();
    void friendsByLinkAndChatBothWays();
};

void tst_Ed3kFriends::cleanup()
{
    Ed3kFriendsBridge::setFriends(nullptr);
    QTest::qWait(10);
}

// no Ed3kFriends: an empty list and error 0 (no node), never silence
void tst_Ed3kFriends::withoutNodeEveryRequestFails()
{
    IpcMessage get(IpcMsgType::GetEd3kFriends, 1);
    const IpcMessage list = ask(get);
    QCOMPARE(list.type(), IpcMsgType::Result);
    QVERIFY(list.fieldBool(0));
    QCOMPARE(list.fieldMap(1).value(QStringLiteral("running")).toBool(), false);
    QVERIFY(list.fieldMap(1).value(QStringLiteral("friends")).toArray().isEmpty());

    IpcMessage add(IpcMsgType::AddEd3kFriend, 2);
    add.append(QStringLiteral("ed3k:friend?key=abc"));
    add.append(QString());
    const IpcMessage r = ask(add);
    QCOMPARE(r.type(), IpcMsgType::Result);
    QVERIFY(!r.fieldBool(0));
    QCOMPARE(r.fieldMap(1).value(QStringLiteral("error")).toInteger(), 0);

    // a stopped node behind it: the same
    Ed3kSession s;
    Ed3kFriends f(s);
    Ed3kFriendsBridge::setFriends(&f);
    QVERIFY(!ask(add).fieldBool(0));
    QCOMPARE(ask(get).fieldMap(1).value(QStringLiteral("running")).toBool(), false);
}

void tst_Ed3kFriends::friendsByLinkAndChatBothWays()
{
    QTemporaryDir da, db, dc;
    QVERIFY(da.isValid() && db.isValid() && dc.isValid());
    Ed3kSession a, b, c;
    QVERIFY(c.start(loopback(dc.path())));
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    QVERIFY(bootstrap(a, c));
    QVERIFY(bootstrap(b, c));
    const std::uint16_t bPort = b.udpPort();

    Ed3kFriends fa(a), fb(b);
    QVERIFY(fa.isRunning() && fb.isRunning());
    Ed3kFriendsBridge::setFriends(&fa);
    // the pushes as the daemon sends them
    QList<IpcMessage> pushes;
    Ed3kFriendsBridge::connectPushes(fa, [&](const IpcMessage& m) { pushes.append(roundTrip(m)); });

    // B's link with a request value and a name, over IPC from B's daemon
    Ed3kFriendsBridge::setFriends(&fb);
    IpcMessage mine(IpcMsgType::GetEd3kMyFriendLink, 3);
    mine.append(true);
    mine.append(QStringLiteral("Bob"));
    const IpcMessage linkReply = ask(mine);
    QVERIFY(linkReply.fieldBool(0));
    const QString link = linkReply.fieldString(1);
    QVERIFY2(link.startsWith(QLatin1String("ed3k:friend?key=")) && link.contains(QLatin1String("&r=")), qPrintable(link));
    Ed3kFriendsBridge::setFriends(&fa);

    // B goes offline so that A's request is stored at C alone (B's records die with it)
    const QByteArray bNode = nodeBytes(b);
    b.stop();
    // A adds it: a request stored in Kad (friend.md §3; no KadStored for it)
    IpcMessage add(IpcMsgType::AddEd3kFriend, 4);
    add.append(link);
    add.append(QStringLiteral("it's me, A"));
    const IpcMessage added = ask(add);
    QVERIFY(added.fieldBool(0));
    const QByteArray bKey = QByteArray::fromHex(added.fieldMap(1).value(QStringLiteral("key")).toString().toLatin1());
    QCOMPARE(bKey.size(), 32);
    // a bad link: Malformed, not a friend
    IpcMessage bad(IpcMsgType::AddEd3kFriend, 5);
    bad.append(QStringLiteral("ed3k:friend?key=xyz"));
    bad.append(QString());
    const IpcMessage refused = ask(bad);
    QVERIFY(!refused.fieldBool(0));
    QCOMPARE(refused.fieldMap(1).value(QStringLiteral("error")).toInteger(), qint64(::ed3k::Error::Malformed));

    // the list over IPC: B, named by its link's dn, our request pending
    const IpcMessage list = ask(IpcMessage(IpcMsgType::GetEd3kFriends, 6));
    const QCborArray friends = list.fieldMap(1).value(QStringLiteral("friends")).toArray();
    QCOMPARE(friends.size(), 1);
    const QCborMap row = friends.at(0).toMap();
    QCOMPARE(row.value(QStringLiteral("name")).toString(), QStringLiteral("Bob"));
    QCOMPARE(row.value(QStringLiteral("node")).toString(), QString::fromLatin1(bNode.toHex()));
    QVERIFY(row.value(QStringLiteral("asking")).toBool());
    QVERIFY(std::ranges::any_of(pushes, [](const IpcMessage& m) { return m.type() == IpcMsgType::PushEd3kFriendsChanged; }));

    // B searches its request place when it starts (friend.md §3): started again, same
    // port, until A's store got there
    QSignalSpy requests(&fb, &Ed3kFriends::friendRequest);
    for (int attempt = 0; attempt < 4 && requests.isEmpty(); ++attempt) {
        QTest::qWait(3000);
        b.stop();
        QVERIFY(b.start(loopback(db.path(), bPort)));
        QVERIFY(bootstrap(b, c));
        for (QDeadlineTimer wait(20000); requests.isEmpty() && !wait.hasExpired();)
            QTest::qWait(100);
    }
    QVERIFY(!requests.isEmpty());
    const QByteArray aKey = requests.first().at(0).toByteArray();
    QCOMPARE(requests.first().at(1).toByteArray(), nodeBytes(a));
    QCOMPARE(requests.first().at(2).toString(), QStringLiteral("it's me, A"));
    QCOMPARE(fb.requests().size(), std::size_t(1));
    // B accepts over IPC
    Ed3kFriendsBridge::setFriends(&fb);
    IpcMessage accept(IpcMsgType::AnswerEd3kFriendRequest, 7);
    accept.append(QString::fromLatin1(aKey.toHex()));
    accept.append(true);
    QVERIFY(ask(accept).fieldBool(0));
    QVERIFY(fb.requests().empty());
    QVERIFY(fb.isFriend(*a.nodeId()));
    Ed3kFriendsBridge::setFriends(&fa);

    // A → B: A knows no address of B; the send looks B up when it fails, then goes
    QSignalSpy got(&fb, &Ed3kFriends::chatMessage);
    QSignalSpy sentA(&fa, &Ed3kFriends::chatSent);
    pushes.clear();
    IpcMessage send(IpcMsgType::SendEd3kChat, 8);
    send.append(QString::fromLatin1(nodeBytes(b).toHex()));
    send.append(QStringLiteral("hello B"));
    const IpcMessage sendReply = ask(send);
    QVERIFY(sendReply.fieldBool(0));
    QCOMPARE(sendReply.fieldArray(1).size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(got.count() >= 1, 120000);
    QCOMPARE(got.first().at(0).toByteArray(), nodeBytes(a));
    QCOMPARE(got.first().at(2).toString(), QStringLiteral("hello B"));
    QTRY_VERIFY_WITH_TIMEOUT(sentA.count() >= 1, 30000);
    QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(pushes, [](const IpcMessage& m) { return m.type() == IpcMsgType::PushEd3kChatSent; }), 5000);

    // B → A, B's name for A unknown (no dn): the open connection carries it
    QSignalSpy gotA(&fa, &Ed3kFriends::chatMessage);
    QVERIFY(fb.sendChat(*a.nodeId(), QStringLiteral("hi A")).has_value());
    QTRY_VERIFY_WITH_TIMEOUT(gotA.count() >= 1, 60000);
    QCOMPARE(gotA.first().at(0).toByteArray(), nodeBytes(b));
    QCOMPARE(gotA.first().at(1).toString(), QStringLiteral("Bob"));
    QCOMPARE(gotA.first().at(2).toString(), QStringLiteral("hi A"));
    // the push the GUI shows: [node, name, text, id]
    QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(pushes, [](const IpcMessage& m) { return m.type() == IpcMsgType::PushEd3kChatMessage; }), 5000);
    const auto msg = std::ranges::find_if(pushes, [](const IpcMessage& m) { return m.type() == IpcMsgType::PushEd3kChatMessage; });
    QCOMPARE(msg->fieldString(0), QString::fromLatin1(nodeBytes(b).toHex()));
    QCOMPARE(msg->fieldString(1), QStringLiteral("Bob"));
    QCOMPARE(msg->fieldString(2), QStringLiteral("hi A"));

    // the message filter (eD2K "Filter messages containing") drops one
    Ed3kFriends::ChatOptions o;
    o.filter = {QStringLiteral("fastest")};
    fa.setChatOptions(o);
    gotA.clear();
    QSignalSpy sentB(&fb, &Ed3kFriends::chatSent);
    QVERIFY(fb.sendChat(*a.nodeId(), QStringLiteral("the FASTEST eMule")).has_value());
    QTRY_VERIFY_WITH_TIMEOUT(sentB.count() >= 1, 30000);
    QTest::qWait(200);
    QCOMPARE(gotA.count(), 0);

    // removal over IPC; the name file follows
    IpcMessage remove(IpcMsgType::RemoveEd3kFriend, 9);
    remove.append(QString::fromLatin1(bKey.toHex()));
    QVERIFY(ask(remove).fieldBool(0));
    QVERIFY(fa.friends().empty());
    IpcMessage end(IpcMsgType::EndEd3kChat, 10);
    end.append(QString::fromLatin1(nodeBytes(b).toHex()));
    QVERIFY(ask(end).fieldBool(0));
}

QTEST_GUILESS_MAIN(tst_Ed3kFriends)
#include "tst_Ed3kFriends.moc"
