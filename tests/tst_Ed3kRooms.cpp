/// @file tst_Ed3kRooms.cpp
/// @brief ed3k rooms (src/ed3k/Ed3kRooms) and their IPC (Ed3kRoomsBridge): two nodes on
/// loopback; an open room joined by its link with posts both ways, a closed room with the
/// out-of-band admit, a ban that drops the member's posts, the rooms across a restart.

#include "Ed3kRooms.h"
#include "Ed3kRoomsBridge.h"
#include "Ed3kSession.h"

#include "IpcProtocol.h"
#include "controls/LogTextView.h"
#include "panels/Ed3kRoomTab.h"

#include <QCborArray>
#include <QCborMap>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;
using eMule::ed3k::Ed3kRooms;
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

/// The message as the GUI gets it: through a CBOR frame.
IpcMessage roundTrip(const IpcMessage& msg)
{
    const auto decoded = Ipc::tryDecodeFrame(msg.toFrame());
    return decoded ? IpcMessage(decoded->message) : IpcMessage();
}

/// The bridge's answer to @p msg for @p rooms, through a frame both ways.
IpcMessage ask(Ed3kRooms* rooms, const IpcMessage& msg)
{
    Ed3kRoomsBridge::setRooms(rooms);
    IpcMessage out;
    const bool taken = Ed3kRoomsBridge::handleRequest(roundTrip(msg), [&](const IpcMessage& r) { out = roundTrip(r); });
    return taken ? out : IpcMessage();
}

/// @p b takes @p a as its neighbour in @p room.
bool connectTo(Ed3kRooms& rb, const ::ed3k::Room& room, Ed3kSession& a)
{
    const auto id = a.nodeId();
    if (!id)
        return false;
    const ::ed3k::Source peer{*id, false, ::ed3k::Address::v4(127, 0, 0, 1, a.udpPort()), std::nullopt, {}};
    const auto ok = rb.connectPeer(room, peer);
    return ok || ok.error() == ::ed3k::Error::State;   // joined already
}

QString hexOf(const ::ed3k::Room& room)
{
    return Ed3kRooms::roomHex(room);
}

/// The texts of the posts @p rooms shows in @p room.
QStringList texts(const Ed3kRooms& rooms, const ::ed3k::Room& room)
{
    QStringList out;
    if (const auto* r = rooms.find(room))
        for (const auto& p : r->posts)
            out << p.text;
    return out;
}

IpcMessage create(Ed3kRooms& rooms, const QString& title, int flags, const QString& nick)
{
    IpcMessage m(IpcMsgType::CreateEd3kRoom, 1);
    m.append(title);
    m.append(QStringLiteral("about %1").arg(title));
    m.append(qint64(flags));
    m.append(nick);
    return ask(&rooms, m);
}

IpcMessage join(Ed3kRooms& rooms, const QString& link, const QString& nick)
{
    IpcMessage m(IpcMsgType::JoinEd3kRoom, 2);
    m.append(link);
    m.append(nick);
    return ask(&rooms, m);
}

IpcMessage post(Ed3kRooms& rooms, const ::ed3k::Room& room, const QString& text, const QString& replyTo = {})
{
    IpcMessage m(IpcMsgType::PostEd3kRoom, 3);
    m.append(hexOf(room));
    m.append(text);
    m.append(replyTo);
    return ask(&rooms, m);
}

/// The room's row of GetEd3kRooms.
QCborMap rowOf(Ed3kRooms& rooms, const ::ed3k::Room& room)
{
    for (const QCborValue& v : ask(&rooms, IpcMessage(IpcMsgType::GetEd3kRooms, 20)).fieldMap(1).value(QStringLiteral("rooms")).toArray())
        if (v.toMap().value(QStringLiteral("room")).toString() == hexOf(room))
            return v.toMap();
    return {};
}

/// A room tab drawing "[reply] text" lines, as the IRC panel does with its own look.
std::unique_ptr<eMule::Ed3kRoomTab> tabOf(const ::ed3k::Room& room)
{
    return std::make_unique<eMule::Ed3kRoomTab>(hexOf(room), [](const QString&) {},
        [](const eMule::Ed3kRoomTab::Post& p, const QString&, bool, const QString& reply) {
            return (reply.isEmpty() ? QString() : QStringLiteral("[%1] ").arg(reply)) + p.text.toHtmlEscaped();
        });
}

QVector<eMule::Ed3kRoomTab::Post> postsOf(Ed3kRooms& rooms, const ::ed3k::Room& room)
{
    IpcMessage m(IpcMsgType::GetEd3kRoomPosts, 21);
    m.append(hexOf(room));
    QVector<eMule::Ed3kRoomTab::Post> out;
    for (const QCborValue& v : ask(&rooms, m).fieldArray(1))
        out << eMule::Ed3kRoomTab::postFrom(v.toMap());
    return out;
}

} // namespace

class tst_Ed3kRooms : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void withoutNodeEveryRequestFails();
    void admitTextRoundTrip();
    void openRoomByLinkPostsBothWays();
    void closedRoomAdmitRoundTrip();
    void banDropsPosts();
    void roomsAcrossRestart();
    // batch 13
    void replyRoundTripShowsTheQuotedLine();
    void muteIsKeptAndHidesPosts();
    void roomMemberShowsTheMemberKey();
};

void tst_Ed3kRooms::cleanup()
{
    Ed3kRoomsBridge::setRooms(nullptr);
    QTest::qWait(10);
}

// no Ed3kRooms: an empty list and error 0 (no node), never silence
void tst_Ed3kRooms::withoutNodeEveryRequestFails()
{
    const IpcMessage list = ask(nullptr, IpcMessage(IpcMsgType::GetEd3kRooms, 1));
    QCOMPARE(list.type(), IpcMsgType::Result);
    QVERIFY(list.fieldBool(0));
    QCOMPARE(list.fieldMap(1).value(QStringLiteral("running")).toBool(), false);
    QVERIFY(list.fieldMap(1).value(QStringLiteral("rooms")).toArray().isEmpty());

    IpcMessage j(IpcMsgType::JoinEd3kRoom, 2);
    j.append(QStringLiteral("ed3k:room?key=abc&id=aa"));
    j.append(QStringLiteral("me"));
    const IpcMessage r = ask(nullptr, j);
    QVERIFY(!r.fieldBool(0));
    QCOMPARE(r.fieldMap(1).value(QStringLiteral("error")).toInteger(), 0);

    // a stopped node behind it: the same
    Ed3kSession s;
    Ed3kRooms rooms(s);
    QVERIFY(!ask(&rooms, j).fieldBool(0));
    QCOMPARE(ask(&rooms, IpcMessage(IpcMsgType::GetEd3kRooms, 3)).fieldMap(1).value(QStringLiteral("running")).toBool(), false);
    // not ours
    QVERIFY(!Ed3kRoomsBridge::isRoomsRequest(IpcMsgType::GetEd3kFriends));
    QVERIFY(Ed3kRoomsBridge::isRoomsRequest(IpcMsgType::ImportEd3kRoomAdmit));
}

// the admit text the app hands over: room and item survive blanks and line breaks
void tst_Ed3kRooms::admitTextRoundTrip()
{
    ::ed3k::Room room;
    room.bytes.fill(0x42);
    const QByteArray item("\x01\x02\x03 item bytes", 15);
    const QString text = Ed3kRooms::admitText(room, std::as_bytes(std::span{item.constData(), std::size_t(item.size())}));
    QVERIFY(text.startsWith(QLatin1String("ed3k-admit:")));
    QString mangled = text;
    mangled.insert(20, QStringLiteral("\n  "));
    const auto parsed = Ed3kRooms::parseAdmitText(mangled);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->first, room);
    QCOMPARE(parsed->second, item);
    QVERIFY(!Ed3kRooms::parseAdmitText(QStringLiteral("ed3k-admit:zz")).has_value());
    QVERIFY(!Ed3kRooms::parseAdmitText(QStringLiteral("hello")).has_value());
    // a member key as base32 (52) or hex (64)
    ::ed3k::Key32 key{};
    key.fill(7);
    QCOMPARE(Ed3kRooms::keyText(key).size(), 52);
    QCOMPARE(Ed3kRooms::keyFromText(Ed3kRooms::keyText(key)), std::optional{key});
    QCOMPARE(Ed3kRooms::keyFromText(QString::fromLatin1(QByteArray(32, 7).toHex())), std::optional{key});
    QVERIFY(!Ed3kRooms::keyFromText(QStringLiteral("abc")).has_value());
}

void tst_Ed3kRooms::openRoomByLinkPostsBothWays()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    Ed3kRooms ra(a), rb(b);
    QList<IpcMessage> pushes;
    Ed3kRoomsBridge::connectPushes(rb, [&](const IpcMessage& m) { pushes.append(roundTrip(m)); });

    // A makes an open room over IPC
    const IpcMessage made = create(ra, QStringLiteral("Lobby"), 0, QStringLiteral("alice"));
    QVERIFY2(made.fieldBool(0), qPrintable(made.fieldMap(1).value(QStringLiteral("error")).toVariant().toString()));
    const QString link = made.fieldMap(1).value(QStringLiteral("link")).toString();
    QVERIFY2(link.startsWith(QLatin1String("ed3k:room?")) && !link.contains(QLatin1String("&s=")), qPrintable(link));
    const auto room = Ed3kRooms::roomFrom(made.fieldMap(1).value(QStringLiteral("room")).toString());
    QVERIFY(room.has_value());
    QVERIFY(ra.find(*room) && ra.find(*room)->owner && ra.find(*room)->canModerate());

    // B joins by the link, takes A as its neighbour
    const IpcMessage joined = join(rb, link, QStringLiteral("bob"));
    QVERIFY(joined.fieldBool(0));
    QCOMPARE(joined.fieldMap(1).value(QStringLiteral("room")).toString(), hexOf(*room));
    // the same link again: the same room
    QCOMPARE(join(rb, link, QStringLiteral("bob")).fieldMap(1).value(QStringLiteral("room")).toString(), hexOf(*room));
    QVERIFY(connectTo(rb, *room, a));
    QTRY_VERIFY_WITH_TIMEOUT(rb.find(*room)->joined, 30000);
    // the room info reached B: the title shows as the topic
    QTRY_COMPARE_WITH_TIMEOUT(rb.find(*room)->title, QStringLiteral("Lobby"), 30000);
    QVERIFY(!rb.find(*room)->canModerate());

    // A -> B and B -> A
    QVERIFY(post(ra, *room, QStringLiteral("hello from A")).fieldBool(0));
    QTRY_VERIFY_WITH_TIMEOUT(texts(rb, *room).contains(QStringLiteral("hello from A")), 30000);
    const IpcMessage sent = post(rb, *room, QStringLiteral("hi from B"));
    QVERIFY(sent.fieldBool(0));
    QCOMPARE(sent.fieldArray(1).size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(texts(ra, *room).contains(QStringLiteral("hi from B")), 30000);
    // A's post at B: A's nick, a moderator (the owner's member key is in mods)
    const auto& atB = rb.find(*room)->posts;
    const auto fromA = std::ranges::find_if(atB, [](const auto& p) { return p.text == QStringLiteral("hello from A"); });
    QVERIFY(fromA != atB.end());
    QCOMPARE(fromA->nick, QStringLiteral("alice"));
    QVERIFY(fromA->moderator);

    // the pushes of B: the post, a presence of A, the list
    QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(pushes, [](const IpcMessage& m) {
        return m.type() == IpcMsgType::PushEd3kRoomPost && m.fieldMap(1).value(QStringLiteral("text")).toString() == QStringLiteral("hello from A");
    }), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(std::ranges::any_of(pushes, [](const IpcMessage& m) {
        return m.type() == IpcMsgType::PushEd3kRoomPresence && m.fieldString(2) == QStringLiteral("alice");
    }), 30000);
    // the presence carries the member ID (room §6): 16 bytes as hex
    QVERIFY(std::ranges::any_of(pushes, [](const IpcMessage& m) {
        return m.type() == IpcMsgType::PushEd3kRoomPresence && m.fieldString(2) == QStringLiteral("alice")
            && m.fieldString(4).size() == 32;
    }));
    QVERIFY(std::ranges::any_of(pushes, [](const IpcMessage& m) { return m.type() == IpcMsgType::PushEd3kRoomsChanged; }));

    // the list and the posts over IPC
    const QCborArray list = ask(&rb, IpcMessage(IpcMsgType::GetEd3kRooms, 9)).fieldMap(1).value(QStringLiteral("rooms")).toArray();
    QCOMPARE(list.size(), 1);
    const QCborMap row = list.at(0).toMap();
    QCOMPARE(row.value(QStringLiteral("title")).toString(), QStringLiteral("Lobby"));
    QCOMPARE(row.value(QStringLiteral("nick")).toString(), QStringLiteral("bob"));
    QCOMPARE(row.value(QStringLiteral("memberKey")).toString().size(), 52);
    QCOMPARE(row.value(QStringLiteral("memberId")).toString().size(), 32);
    QVERIFY(!row.value(QStringLiteral("owner")).toBool());
    IpcMessage getPosts(IpcMsgType::GetEd3kRoomPosts, 10);
    getPosts.append(hexOf(*room));
    QVERIFY(ask(&rb, getPosts).fieldArray(1).size() >= 2);

    // a new nick: in the list at once; the others take it with the next presence, as one
    // presence a minute is the rate (§4.4: the one sent right after the join's is dropped)
    IpcMessage nick(IpcMsgType::SetEd3kRoomNick, 11);
    nick.append(hexOf(*room));
    nick.append(QStringLiteral("robert"));
    QVERIFY(ask(&rb, nick).fieldBool(0));
    QCOMPARE(rb.find(*room)->nick, QStringLiteral("robert"));

    // B leaves: gone from its list, its member key file too
    IpcMessage leave(IpcMsgType::LeaveEd3kRoom, 12);
    leave.append(hexOf(*room));
    QVERIFY(ask(&rb, leave).fieldBool(0));
    QVERIFY(rb.rooms().empty());
    QVERIFY(!QFile::exists(QDir(b.stateDir()).filePath(QStringLiteral("rooms/%1.member").arg(hexOf(*room)))));
}

void tst_Ed3kRooms::closedRoomAdmitRoundTrip()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    Ed3kRooms ra(a), rb(b);

    const IpcMessage made = create(ra, QStringLiteral("Closed"), ED3K_ROOM_CLOSED, QStringLiteral("owner"));
    QVERIFY(made.fieldBool(0));
    const QString link = made.fieldMap(1).value(QStringLiteral("link")).toString();
    QVERIFY(link.contains(QLatin1String("&s=")));   // closed implies private
    const auto room = Ed3kRooms::roomFrom(made.fieldMap(1).value(QStringLiteral("room")).toString());
    QVERIFY(room && ra.find(*room)->closed && ra.find(*room)->isPrivate);

    // the newcomer joins and copies its member key (base32, §1.1)
    QVERIFY(join(rb, link, QStringLiteral("newbie")).fieldBool(0));
    const QString memberKey = rb.memberKeyText(*room);
    QCOMPARE(memberKey.size(), 52);
    // the moderator's core signs the admit; a non-moderator cannot
    IpcMessage admit(IpcMsgType::ModerateEd3kRoom, 4);
    admit.append(hexOf(*room));
    admit.append(qint64(Ed3kRooms::Moderation::Admit));
    admit.append(memberKey);
    admit.append(qint64(0));
    const IpcMessage notMod = ask(&rb, admit);
    QVERIFY(!notMod.fieldBool(0));
    const IpcMessage admitted = ask(&ra, admit);
    QVERIFY(admitted.fieldBool(0));
    const QString text = admitted.fieldMap(1).value(QStringLiteral("admit")).toString();
    QVERIFY(text.startsWith(QLatin1String("ed3k-admit:")));
    // the newcomer imports it, then takes a neighbour
    IpcMessage import(IpcMsgType::ImportEd3kRoomAdmit, 5);
    import.append(text);
    const IpcMessage imported = ask(&rb, import);
    QVERIFY(imported.fieldBool(0));
    QCOMPARE(imported.fieldMap(1).value(QStringLiteral("room")).toString(), hexOf(*room));
    QVERIFY(connectTo(rb, *room, a));
    // the generation key came with the first answer: posting works
    QTRY_VERIFY_WITH_TIMEOUT(rb.find(*room)->keyHeld, 30000);
    QVERIFY(post(rb, *room, QStringLiteral("sealed hello")).fieldBool(0));
    QTRY_VERIFY_WITH_TIMEOUT(texts(ra, *room).contains(QStringLiteral("sealed hello")), 30000);
    QVERIFY(post(ra, *room, QStringLiteral("welcome")).fieldBool(0));
    QTRY_VERIFY_WITH_TIMEOUT(texts(rb, *room).contains(QStringLiteral("welcome")), 30000);

    // a new generation by the moderator
    IpcMessage gen(IpcMsgType::ModerateEd3kRoom, 6);
    gen.append(hexOf(*room));
    gen.append(qint64(Ed3kRooms::Moderation::NewGeneration));
    gen.append(QString());
    gen.append(qint64(0));
    const IpcMessage g = ask(&ra, gen);
    QVERIFY(g.fieldBool(0));
    QVERIFY(g.fieldMap(1).value(QStringLiteral("generation")).toInteger() >= 2);
    // a garbled admit is refused
    IpcMessage bad(IpcMsgType::ImportEd3kRoomAdmit, 7);
    bad.append(QStringLiteral("ed3k-admit:nonsense"));
    QCOMPARE(ask(&rb, bad).fieldMap(1).value(QStringLiteral("error")).toInteger(), qint64(::ed3k::Error::Malformed));
}

void tst_Ed3kRooms::banDropsPosts()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    Ed3kRooms ra(a), rb(b);

    const IpcMessage made = create(ra, QStringLiteral("Open"), 0, QStringLiteral("mod"));
    const auto room = Ed3kRooms::roomFrom(made.fieldMap(1).value(QStringLiteral("room")).toString());
    QVERIFY(room.has_value());
    QVERIFY(join(rb, made.fieldMap(1).value(QStringLiteral("link")).toString(), QStringLiteral("troll")).fieldBool(0));
    QVERIFY(connectTo(rb, *room, a));
    QTRY_VERIFY_WITH_TIMEOUT(rb.find(*room)->joined, 30000);
    QVERIFY(post(rb, *room, QStringLiteral("spam 1")).fieldBool(0));
    QTRY_VERIFY_WITH_TIMEOUT(texts(ra, *room).contains(QStringLiteral("spam 1")), 30000);

    // A bans B's member key (from its post): B's posts go, also the one held
    QSignalSpy removed(&ra, &Ed3kRooms::postsRemoved);
    QSignalSpy notices(&ra, &Ed3kRooms::notice);
    const auto& posts = ra.find(*room)->posts;
    const auto spam = std::ranges::find_if(posts, [](const auto& p) { return p.text == QStringLiteral("spam 1"); });
    QVERIFY(spam != posts.end());
    const auto troll = spam->author;   // the deque changes below
    IpcMessage ban(IpcMsgType::ModerateEd3kRoom, 4);
    ban.append(hexOf(*room));
    ban.append(qint64(Ed3kRooms::Moderation::Ban));
    ban.append(Ed3kRooms::keyText(troll));
    ban.append(qint64(a.now().unixS + 3600));
    QVERIFY(ask(&ra, ban).fieldBool(0));
    QTRY_VERIFY_WITH_TIMEOUT(!texts(ra, *room).contains(QStringLiteral("spam 1")), 10000);
    QVERIFY(removed.count() >= 1);
    QVERIFY(std::ranges::any_of(notices, [](const QList<QVariant>& n) { return n.at(1).toInt() == int(Ed3kRooms::Notice::Banned); }));
    // a later post of B does not show at A
    (void)post(rb, *room, QStringLiteral("spam 2"));
    QTest::qWait(1500);
    QVERIFY(!texts(ra, *room).contains(QStringLiteral("spam 2")));

    // lifted: B posts again
    IpcMessage lift(IpcMsgType::ModerateEd3kRoom, 5);
    lift.append(hexOf(*room));
    lift.append(qint64(Ed3kRooms::Moderation::Lift));
    lift.append(Ed3kRooms::keyText(troll));
    lift.append(qint64(0));
    QVERIFY(ask(&ra, lift).fieldBool(0));
}

void tst_Ed3kRooms::roomsAcrossRestart()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    const std::uint16_t aPort = a.udpPort();
    auto ra = std::make_unique<Ed3kRooms>(a);
    Ed3kRooms rb(b);

    const IpcMessage made = create(*ra, QStringLiteral("Kept"), ED3K_ROOM_PRIVATE, QStringLiteral("keeper"));
    QVERIFY(made.fieldBool(0));
    const QString link = made.fieldMap(1).value(QStringLiteral("link")).toString();
    const auto room = Ed3kRooms::roomFrom(made.fieldMap(1).value(QStringLiteral("room")).toString());
    QVERIFY(room.has_value());
    const auto memberKey = ra->find(*room)->memberKey;
    // the secrets: owner-only files, never the link in a log
    const QString dir = QDir(a.stateDir()).filePath(QStringLiteral("rooms"));
    const QString memberFile = QDir(dir).filePath(QStringLiteral("%1.member").arg(hexOf(*room)));
    QVERIFY(QFile::exists(memberFile));
    QCOMPARE(QFile(memberFile).size(), 32);
    QCOMPARE(QFile::permissions(memberFile) & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions());
    QCOMPARE(QFile::permissions(QDir(dir).filePath(QStringLiteral("joined.cbor"))) & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
             QFileDevice::Permissions());

    // A restarts: the room is joined again, as owner, with the same member key and nick
    a.stop();
    QVERIFY(ra->rooms().empty());
    QVERIFY(a.start(loopback(da.path(), aPort)));
    const auto* again = ra->find(*room);
    QVERIFY(again);
    QVERIFY(again->owner);
    QCOMPARE(again->link, link);
    QCOMPARE(again->nick, QStringLiteral("keeper"));
    // the room info of the last run shows before a neighbour sends one
    QCOMPARE(again->title, QStringLiteral("Kept"));
    QVERIFY(again->canModerate());
    QVERIFY(again->memberKey == memberKey);
    // a new Ed3kRooms on the running node (the daemon's start order) finds it too
    ra = std::make_unique<Ed3kRooms>(a);
    QVERIFY(ra->find(*room) && ra->find(*room)->owner);

    // and it works: B joins, posts reach the restarted owner
    QVERIFY(join(rb, link, QStringLiteral("guest")).fieldBool(0));
    QVERIFY(connectTo(rb, *room, a));
    QTRY_VERIFY_WITH_TIMEOUT(rb.find(*room)->joined, 30000);
    QVERIFY(post(rb, *room, QStringLiteral("still here?")).fieldBool(0));
    QTRY_VERIFY_WITH_TIMEOUT(texts(*ra, *room).contains(QStringLiteral("still here?")), 30000);
    // the owner can still set the info (the room key came back from its file)
    IpcMessage info(IpcMsgType::SetEd3kRoomInfo, 8);
    info.append(hexOf(*room));
    info.append(QStringLiteral("Kept, renamed"));
    info.append(QStringLiteral("new about"));
    info.append(qint64(0));
    info.append(QCborArray{Ed3kRooms::keyText(memberKey)});
    QVERIFY(ask(ra.get(), info).fieldBool(0));
    QTRY_COMPARE_WITH_TIMEOUT(rb.find(*room)->title, QStringLiteral("Kept, renamed"), 30000);
}

// §2.5: reply_to round trip; the post shows a quoted line of its post, or that it answers
// a post not held (room.md: a reply_to naming no held post)
void tst_Ed3kRooms::replyRoundTripShowsTheQuotedLine()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    Ed3kRooms ra(a), rb(b);
    const IpcMessage made = create(ra, QStringLiteral("Talk"), 0, QStringLiteral("alice"));
    const auto room = Ed3kRooms::roomFrom(made.fieldMap(1).value(QStringLiteral("room")).toString());
    QVERIFY(room.has_value());
    QVERIFY(join(rb, made.fieldMap(1).value(QStringLiteral("link")).toString(), QStringLiteral("bob")).fieldBool(0));
    QVERIFY(connectTo(rb, *room, a));
    QTRY_VERIFY_WITH_TIMEOUT(rb.find(*room)->joined, 30000);
    QVERIFY(post(ra, *room, QStringLiteral("who has the film?")).fieldBool(0));
    QTRY_VERIFY_WITH_TIMEOUT(texts(rb, *room).contains(QStringLiteral("who has the film?")), 30000);
    const auto& atB = rb.find(*room)->posts;
    const auto question = std::ranges::find_if(atB, [](const auto& p) { return p.text == QStringLiteral("who has the film?"); });
    QVERIFY(question != atB.end());
    const QString questionId = QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(question->id.data()), 16).toHex());
    QVERIFY(post(rb, *room, QStringLiteral("I do"), questionId).fieldBool(0));
    QTRY_VERIFY_WITH_TIMEOUT(texts(ra, *room).contains(QStringLiteral("I do")), 30000);
    const auto& atA = ra.find(*room)->posts;
    const auto answer = std::ranges::find_if(atA, [](const auto& p) { return p.text == QStringLiteral("I do"); });
    QVERIFY(answer != atA.end() && answer->replyTo.has_value());
    QVERIFY(*answer->replyTo == question->id);
    // over IPC and in the tab: the quoted parent
    auto tab = tabOf(*room);
    tab->setRow(rowOf(ra, *room));
    auto posts = postsOf(ra, *room);
    tab->setPosts(posts);
    QVERIFY2(tab->log()->toPlainText().contains(QStringLiteral("] I do")), qPrintable(tab->log()->toPlainText()));
    QVERIFY(tab->log()->toPlainText().contains(QStringLiteral("who has the film?] I do")));
    // a reply whose post is not held
    eMule::Ed3kRoomTab::Post orphan = posts.last();
    orphan.id = QStringLiteral("ff").repeated(16);
    orphan.replyTo = QStringLiteral("ee").repeated(16);
    orphan.text = QStringLiteral("late");
    orphan.sent = posts.last().sent + 1;
    QVERIFY(tab->addPost(orphan));
    QVERIFY(tab->log()->toPlainText().contains(QStringLiteral("[answer to a post not held] late")));
    // the pending reply of the input is taken once
    tab->setReplyTo(questionId);
    QCOMPARE(tab->takeReplyTo(), questionId);
    QVERIFY(tab->replyTo().isEmpty());
}

// §6: a member muted here alone, kept across a restart (joined.cbor); its posts not drawn
void tst_Ed3kRooms::muteIsKeptAndHidesPosts()
{
    QTemporaryDir da;
    QVERIFY(da.isValid());
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    const std::uint16_t aPort = a.udpPort();
    auto ra = std::make_unique<Ed3kRooms>(a);
    const IpcMessage made = create(*ra, QStringLiteral("Quiet"), 0, QStringLiteral("me"));
    const auto room = Ed3kRooms::roomFrom(made.fieldMap(1).value(QStringLiteral("room")).toString());
    QVERIFY(room.has_value());
    const QString troll = QStringLiteral("ab").repeated(16);
    QVERIFY(Ed3kRoomsBridge::isRoomsRequest(IpcMsgType::MuteEd3kRoomMember));
    IpcMessage mute(IpcMsgType::MuteEd3kRoomMember, 30);
    mute.append(hexOf(*room));
    mute.append(troll);
    mute.append(true);
    QVERIFY(ask(ra.get(), mute).fieldBool(0));
    IpcMessage bad(IpcMsgType::MuteEd3kRoomMember, 31);
    bad.append(hexOf(*room));
    bad.append(QStringLiteral("ab"));
    bad.append(true);
    QVERIFY(!ask(ra.get(), bad).fieldBool(0));
    QCOMPARE(rowOf(*ra, *room).value(QStringLiteral("muted")).toArray(), QCborArray{troll});

    // a restart: still muted
    a.stop();
    QVERIFY(a.start(loopback(da.path(), aPort)));
    ra = std::make_unique<Ed3kRooms>(a);
    QVERIFY(ra->find(*room));
    QCOMPARE(rowOf(*ra, *room).value(QStringLiteral("muted")).toArray(), QCborArray{troll});

    // the tab draws no post of that member ID; unmuted, it does again
    auto tab = tabOf(*room);
    tab->setRow(rowOf(*ra, *room));
    eMule::Ed3kRoomTab::Post p;
    p.id = QStringLiteral("01").repeated(16);
    p.author = QStringLiteral("x").repeated(52);
    p.authorId = troll;
    p.nick = QStringLiteral("troll");
    p.sent = 100;
    p.text = QStringLiteral("buy cheap");
    eMule::Ed3kRoomTab::Post q = p;
    q.id = QStringLiteral("02").repeated(16);
    q.authorId = QStringLiteral("cd").repeated(16);
    q.text = QStringLiteral("hello");
    q.sent = 101;
    tab->setPosts({p, q});
    QVERIFY(tab->isMuted(troll));
    QVERIFY(!tab->log()->toPlainText().contains(QStringLiteral("buy cheap")));
    QVERIFY(tab->log()->toPlainText().contains(QStringLiteral("hello")));
    IpcMessage unmute(IpcMsgType::MuteEd3kRoomMember, 32);
    unmute.append(hexOf(*room));
    unmute.append(troll);
    unmute.append(false);
    QVERIFY(ask(ra.get(), unmute).fieldBool(0));
    tab->setRow(rowOf(*ra, *room));
    QVERIFY(tab->log()->toPlainText().contains(QStringLiteral("buy cheap")));
}

// #17: a newcomer of a closed room is refused with ROOM_MEMBER: the row says so, the tab
// shows the own member key to send, "Import admit" is offered; an open room offers no import
void tst_Ed3kRooms::roomMemberShowsTheMemberKey()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    Ed3kRooms ra(a), rb(b);
    const IpcMessage made = create(ra, QStringLiteral("Members"), ED3K_ROOM_CLOSED, QStringLiteral("owner"));
    QVERIFY(made.fieldBool(0));
    const auto room = Ed3kRooms::roomFrom(made.fieldMap(1).value(QStringLiteral("room")).toString());
    QVERIFY(room.has_value());
    QVERIFY(join(rb, made.fieldMap(1).value(QStringLiteral("link")).toString(), QStringLiteral("newbie")).fieldBool(0));
    // before any refusal: a room with `s` whose info is not known yet offers no import
    auto tab = tabOf(*room);
    tab->setRow(rowOf(rb, *room));
    QVERIFY(!tab->admitImportable());
    QVERIFY(!tab->showsMemberKeyBar());
    QSignalSpy notices(&rb, &Ed3kRooms::notice);
    QVERIFY(connectTo(rb, *room, a));
    QTRY_VERIFY_WITH_TIMEOUT(rb.find(*room)->notAdmitted, 30000);
    QVERIFY(std::ranges::any_of(notices, [](const QList<QVariant>& n) {
        return n.at(1).toInt() == int(Ed3kRooms::Notice::Refused) && n.at(2).toULongLong() == ED3K_ROOM_MEMBER;
    }));
    const QCborMap row = rowOf(rb, *room);
    QVERIFY(row.value(QStringLiteral("notAdmitted")).toBool());
    tab->setRow(row);
    QVERIFY(tab->showsMemberKeyBar());
    QVERIFY(tab->admitImportable());
    const auto* edit = tab->findChild<QLineEdit*>();
    QVERIFY(edit);
    QCOMPARE(edit->text(), rb.memberKeyText(*room));
    QCOMPARE(edit->text().size(), 52);

    // an open private room: never for its `s` alone
    const IpcMessage open = create(ra, QStringLiteral("Private"), ED3K_ROOM_PRIVATE, QStringLiteral("owner"));
    const auto openRoom = Ed3kRooms::roomFrom(open.fieldMap(1).value(QStringLiteral("room")).toString());
    QVERIFY(openRoom.has_value());
    auto openTab = tabOf(*openRoom);
    openTab->setRow(rowOf(ra, *openRoom));
    QVERIFY(openTab->row().value(QStringLiteral("private")).toBool());
    QVERIFY(!openTab->admitImportable());
}

QTEST_MAIN(tst_Ed3kRooms)   // the room tab is a widget
#include "tst_Ed3kRooms.moc"
