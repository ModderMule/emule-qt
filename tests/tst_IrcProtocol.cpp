/// @file tst_IrcProtocol.cpp
/// @brief Tests for chat/IrcMessage — IRC message parsing (RFC 2812).

#include "TestHelpers.h"
#include "chat/IrcMessage.h"
#include "chat/IrcEmuleProto.h"
#include "chat/IrcNickList.h"
#include "chat/IrcRouting.h"

#include <QTest>

using namespace eMule;

class tst_IrcProtocol : public QObject {
    Q_OBJECT

private slots:
    void serverModes_fromIsupport();
    void nickList_keepsSymbolsApartFromNicks();
    void nickList_ranksByTheServersPrefixOrder();
    void nickList_modeChangesMoveNicks();
    void modeChange_consumesParametersByType();
    void routing_infoLineColours();
    void routing_numerics();
    void routing_notices();
    void input_commandsAndText();
    void text_stripsMircCodesAndSanitisesSounds();
    void emuleProto_friendRequestRoundTrip();
    void emuleProto_friendReplyRoundTrip();
    void emuleProto_friendReplyRejectsGarbage();
    void emuleProto_sendLinkKeepsTheLinkWhole();
    void parse_empty();
    void parse_pingNoPrefix();
    void parse_prefixServerOnly();
    void parse_prefixNickUserHost();
    void parse_prefixNickHostNoUser();
    void parse_commandOnly();
    void parse_privmsg_channel();
    void parse_privmsg_private();
    void parse_join();
    void parse_part_withReason();
    void parse_quit();
    void parse_nick();
    void parse_kick();
    void parse_topic();
    void parse_mode();
    void parse_notice();
    void parse_numeric001();
    void parse_numeric353_names();
    void parse_numeric433_nickInUse();
    void parse_ctcpAction();
    void parse_ctcpVersion();
    void parse_trailingOnly();
    void parse_multipleMiddleParams();
    void isNumeric_trueForThreeDigits();
    void isNumeric_falseForText();
    void numericCode_returnsValue();
};

void tst_IrcProtocol::parse_empty()
{
    const auto msg = IrcMessage::parse(QString());
    QVERIFY(!msg.isValid());
}

void tst_IrcProtocol::parse_pingNoPrefix()
{
    const auto msg = IrcMessage::parse(QStringLiteral("PING :irc.server.net"));
    QVERIFY(msg.isValid());
    QCOMPARE(msg.command, QStringLiteral("PING"));
    QVERIFY(msg.prefix.isEmpty());
    QCOMPARE(msg.params.size(), 1);
    QCOMPARE(msg.params[0], QStringLiteral("irc.server.net"));
}

void tst_IrcProtocol::parse_prefixServerOnly()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":irc.server.net 001 myNick :Welcome to IRC"));
    QVERIFY(msg.isValid());
    QCOMPARE(msg.prefix, QStringLiteral("irc.server.net"));
    QVERIFY(msg.nickname.isEmpty()); // server prefix, no '!'
    QCOMPARE(msg.command, QStringLiteral("001"));
    QCOMPARE(msg.params.size(), 2);
    QCOMPARE(msg.params[0], QStringLiteral("myNick"));
    QCOMPARE(msg.params[1], QStringLiteral("Welcome to IRC"));
}

void tst_IrcProtocol::parse_prefixNickUserHost()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":nick!user@host.com PRIVMSG #channel :Hello world"));
    QVERIFY(msg.isValid());
    QCOMPARE(msg.nickname, QStringLiteral("nick"));
    QCOMPARE(msg.user, QStringLiteral("user"));
    QCOMPARE(msg.host, QStringLiteral("host.com"));
    QCOMPARE(msg.command, QStringLiteral("PRIVMSG"));
    QCOMPARE(msg.params.size(), 2);
    QCOMPARE(msg.params[0], QStringLiteral("#channel"));
    QCOMPARE(msg.params[1], QStringLiteral("Hello world"));
}

void tst_IrcProtocol::parse_prefixNickHostNoUser()
{
    // Unusual but valid: nick@host without user
    const auto msg = IrcMessage::parse(
        QStringLiteral(":nick!@host.com QUIT :Leaving"));
    QCOMPARE(msg.nickname, QStringLiteral("nick"));
    QCOMPARE(msg.user, QString());  // empty between ! and @
    QCOMPARE(msg.host, QStringLiteral("host.com"));
}

void tst_IrcProtocol::parse_commandOnly()
{
    const auto msg = IrcMessage::parse(QStringLiteral("QUIT"));
    QVERIFY(msg.isValid());
    QCOMPARE(msg.command, QStringLiteral("QUIT"));
    QVERIFY(msg.params.isEmpty());
}

void tst_IrcProtocol::parse_privmsg_channel()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":Alice!alice@host PRIVMSG #emule :hello everyone!"));
    QCOMPARE(msg.command, QStringLiteral("PRIVMSG"));
    QCOMPARE(msg.params[0], QStringLiteral("#emule"));
    QCOMPARE(msg.params[1], QStringLiteral("hello everyone!"));
}

void tst_IrcProtocol::parse_privmsg_private()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":Bob!bob@host PRIVMSG myNick :secret message"));
    QCOMPARE(msg.params[0], QStringLiteral("myNick"));
    QCOMPARE(msg.params[1], QStringLiteral("secret message"));
}

void tst_IrcProtocol::parse_join()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":nick!user@host JOIN :#channel"));
    QCOMPARE(msg.command, QStringLiteral("JOIN"));
    QCOMPARE(msg.params[0], QStringLiteral("#channel"));
    QCOMPARE(msg.nickname, QStringLiteral("nick"));
}

void tst_IrcProtocol::parse_part_withReason()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":nick!user@host PART #channel :Bye everyone"));
    QCOMPARE(msg.command, QStringLiteral("PART"));
    QCOMPARE(msg.params[0], QStringLiteral("#channel"));
    QCOMPARE(msg.params[1], QStringLiteral("Bye everyone"));
}

void tst_IrcProtocol::parse_quit()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":nick!user@host QUIT :Connection reset"));
    QCOMPARE(msg.command, QStringLiteral("QUIT"));
    QCOMPARE(msg.params[0], QStringLiteral("Connection reset"));
}

void tst_IrcProtocol::parse_nick()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":oldnick!user@host NICK :newnick"));
    QCOMPARE(msg.command, QStringLiteral("NICK"));
    QCOMPARE(msg.nickname, QStringLiteral("oldnick"));
    QCOMPARE(msg.params[0], QStringLiteral("newnick"));
}

void tst_IrcProtocol::parse_kick()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":op!user@host KICK #channel baduser :Spamming"));
    QCOMPARE(msg.command, QStringLiteral("KICK"));
    QCOMPARE(msg.params[0], QStringLiteral("#channel"));
    QCOMPARE(msg.params[1], QStringLiteral("baduser"));
    QCOMPARE(msg.params[2], QStringLiteral("Spamming"));
    QCOMPARE(msg.nickname, QStringLiteral("op"));
}

void tst_IrcProtocol::parse_topic()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":nick!user@host TOPIC #channel :New topic here"));
    QCOMPARE(msg.command, QStringLiteral("TOPIC"));
    QCOMPARE(msg.params[0], QStringLiteral("#channel"));
    QCOMPARE(msg.params[1], QStringLiteral("New topic here"));
}

void tst_IrcProtocol::parse_mode()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":nick!user@host MODE #channel +o someuser"));
    QCOMPARE(msg.command, QStringLiteral("MODE"));
    QCOMPARE(msg.params[0], QStringLiteral("#channel"));
    QCOMPARE(msg.params[1], QStringLiteral("+o"));
    QCOMPARE(msg.params[2], QStringLiteral("someuser"));
}

void tst_IrcProtocol::parse_notice()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":server.net NOTICE * :Looking up your hostname..."));
    QCOMPARE(msg.command, QStringLiteral("NOTICE"));
    QCOMPARE(msg.params[0], QStringLiteral("*"));
    QCOMPARE(msg.params[1], QStringLiteral("Looking up your hostname..."));
}

void tst_IrcProtocol::parse_numeric001()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":irc.server.net 001 nick :Welcome to the IRC Network nick!user@host"));
    QVERIFY(msg.isNumeric());
    QCOMPARE(msg.numericCode(), 1);
    QCOMPARE(msg.params[0], QStringLiteral("nick"));
}

void tst_IrcProtocol::parse_numeric353_names()
{
    // RPL_NAMREPLY: ":server 353 nick = #channel :@op +voice regular"
    const auto msg = IrcMessage::parse(
        QStringLiteral(":irc.server.net 353 myNick = #channel :@op +voice regular"));
    QVERIFY(msg.isNumeric());
    QCOMPARE(msg.numericCode(), 353);
    QCOMPARE(msg.params.size(), 4);
    QCOMPARE(msg.params[0], QStringLiteral("myNick"));
    QCOMPARE(msg.params[1], QStringLiteral("="));
    QCOMPARE(msg.params[2], QStringLiteral("#channel"));
    QCOMPARE(msg.params[3], QStringLiteral("@op +voice regular"));
}

void tst_IrcProtocol::parse_numeric433_nickInUse()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":irc.server.net 433 * myNick :Nickname is already in use"));
    QCOMPARE(msg.numericCode(), 433);
}

void tst_IrcProtocol::parse_ctcpAction()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":nick!user@host PRIVMSG #channel :\001ACTION waves\001"));
    QCOMPARE(msg.command, QStringLiteral("PRIVMSG"));
    // The CTCP is in the trailing param
    QVERIFY(msg.params[1].startsWith(u'\001'));
    QVERIFY(msg.params[1].endsWith(u'\001'));
    QVERIFY(msg.params[1].contains(QStringLiteral("ACTION")));
}

void tst_IrcProtocol::parse_ctcpVersion()
{
    const auto msg = IrcMessage::parse(
        QStringLiteral(":nick!user@host PRIVMSG myNick :\001VERSION\001"));
    QCOMPARE(msg.command, QStringLiteral("PRIVMSG"));
    QCOMPARE(msg.params[1], QStringLiteral("\001VERSION\001"));
}

void tst_IrcProtocol::parse_trailingOnly()
{
    const auto msg = IrcMessage::parse(QStringLiteral(":server NOTICE * :*** hello"));
    QCOMPARE(msg.params.size(), 2);
    QCOMPARE(msg.params[1], QStringLiteral("*** hello"));
}

void tst_IrcProtocol::parse_multipleMiddleParams()
{
    // MODE with multiple params before trailing
    const auto msg = IrcMessage::parse(
        QStringLiteral(":nick!user@host MODE #channel +ov user1 user2"));
    QCOMPARE(msg.params.size(), 4);
    QCOMPARE(msg.params[0], QStringLiteral("#channel"));
    QCOMPARE(msg.params[1], QStringLiteral("+ov"));
    QCOMPARE(msg.params[2], QStringLiteral("user1"));
    QCOMPARE(msg.params[3], QStringLiteral("user2"));
}

void tst_IrcProtocol::isNumeric_trueForThreeDigits()
{
    IrcMessage msg;
    msg.command = QStringLiteral("001");
    QVERIFY(msg.isNumeric());
    msg.command = QStringLiteral("433");
    QVERIFY(msg.isNumeric());
    msg.command = QStringLiteral("999");
    QVERIFY(msg.isNumeric());
}

void tst_IrcProtocol::isNumeric_falseForText()
{
    IrcMessage msg;
    msg.command = QStringLiteral("PRIVMSG");
    QVERIFY(!msg.isNumeric());
    msg.command = QStringLiteral("01");
    QVERIFY(!msg.isNumeric());
    msg.command = QStringLiteral("0001");
    QVERIFY(!msg.isNumeric());
}

void tst_IrcProtocol::numericCode_returnsValue()
{
    IrcMessage msg;
    msg.command = QStringLiteral("353");
    QCOMPARE(msg.numericCode(), 353);
    msg.command = QStringLiteral("PRIVMSG");
    QCOMPARE(msg.numericCode(), -1);
}

// eMule's CTCP extensions — MFC CIrcMain (srchybrid/IrcMain.cpp:267-342)
void tst_IrcProtocol::emuleProto_friendRequestRoundTrip()
{
    const QString body = IrcEmuleProto::friendRequest(123456u);
    QCOMPARE(body, QStringLiteral("RQSFRIEND|123456|"));
    QVERIFY(IrcEmuleProto::isEmuleProto(body));
    QCOMPARE(IrcEmuleProto::parseFriendRequest(body).value_or(QString()), QStringLiteral("123456"));
    // MFC compares case-insensitively
    QVERIFY(IrcEmuleProto::parseFriendRequest(QStringLiteral("rqsfriend|7|")).has_value());
    QVERIFY(!IrcEmuleProto::parseFriendRequest(QStringLiteral("RQSFRIEND")).has_value());
    QVERIFY(!IrcEmuleProto::isEmuleProto(QStringLiteral("VERSION")));
}

void tst_IrcProtocol::emuleProto_friendReplyRoundTrip()
{
    const QString hash = QStringLiteral("0123456789ABCDEF0123456789ABCDEF");
    const QString body = IrcEmuleProto::friendReply(QStringLiteral("1.0"), QStringLiteral("42"),
                                                    0x04030201u, 4662, 0x08070605u, 4661, hash);
    QCOMPARE(body, QStringLiteral("REPFRIEND eMule1.0|42|67305985:4662|134678021:4661|%1|").arg(hash));

    const auto reply = IrcEmuleProto::parseFriendReply(body);
    QVERIFY(reply.has_value());
    QCOMPARE(reply->verify, 42u);
    QCOMPARE(reply->clientId, 0x04030201u);
    QCOMPARE(reply->port, uint16(4662));
    QCOMPARE(reply->userHashHex, hash);

    // on no server: MFC writes the dotted zero address and port 0
    QVERIFY(IrcEmuleProto::friendReply(QStringLiteral("1.0"), QStringLiteral("1"), 0, 4662, 0, 4661, hash)
                .contains(QStringLiteral("|0:4662|0.0.0.0:0|")));
}

void tst_IrcProtocol::emuleProto_friendReplyRejectsGarbage()
{
    QVERIFY(!IrcEmuleProto::parseFriendReply(QStringLiteral("REPFRIEND eMule|x|1:2|3:4|00|")).has_value());
    // a verify that is no number can never match the one we sent
    QVERIFY(!IrcEmuleProto::parseFriendReply(
        QStringLiteral("REPFRIEND eMule|abc|1:2|3:4|0123456789ABCDEF0123456789ABCDEF|")).has_value());
    // the hash must be a user hash
    QVERIFY(!IrcEmuleProto::parseFriendReply(
        QStringLiteral("REPFRIEND eMule|5|1:2|3:4|not-a-hash|")).has_value());
    QVERIFY(!IrcEmuleProto::parseFriendReply(QStringLiteral("REPFRIEND eMule|5|")).has_value());
}

void tst_IrcProtocol::emuleProto_sendLinkKeepsTheLinkWhole()
{
    const QString hash = QStringLiteral("0123456789abcdef0123456789abcdef");
    const QString link = QStringLiteral("ed2k://|file|Some.File.avi|1234|0123456789ABCDEF0123456789ABCDEF|/");
    const QString body = IrcEmuleProto::sendLink(hash, link);
    QCOMPARE(body, QStringLiteral("SENDLINK|%1|%2").arg(hash, link));

    // the link has bars of its own, and its case is the file name's
    const auto parsed = IrcEmuleProto::parseSendLink(body);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->userHashHex, hash);
    QCOMPARE(parsed->link, link);

    QVERIFY(!IrcEmuleProto::parseSendLink(QStringLiteral("SENDLINK|") + hash + QStringLiteral("|")).has_value());
    QVERIFY(!IrcEmuleProto::parseSendLink(QStringLiteral("SENDLINK")).has_value());
}

// MFC IrcMain.cpp:525-565
void tst_IrcProtocol::serverModes_fromIsupport()
{
    eMule::IrcServerModes modes;
    QCOMPARE(modes.userSymbols, QStringLiteral("@+"));   // until the server says otherwise

    modes.applyIsupport(QStringLiteral(
        "CHANTYPES=# PREFIX=(qaohv)~&@%+ CHANMODES=beI,k,l,imnpst NETWORK=Test :are supported"));
    QCOMPARE(modes.userModes, QStringLiteral("qaohv"));
    QCOMPARE(modes.userSymbols, QStringLiteral("~&@%+"));
    QCOMPARE(modes.chanModesA, QStringLiteral("beI"));
    QCOMPARE(modes.chanModesB, QStringLiteral("k"));
    QCOMPARE(modes.chanModesC, QStringLiteral("l"));
    QCOMPARE(modes.chanModesD, QStringLiteral("imnpst"));

    // a malformed PREFIX leaves what was known
    modes.applyIsupport(QStringLiteral("PREFIX=(ov)@"));
    QCOMPARE(modes.userModes, QStringLiteral("qaohv"));
}

// MFC CIrcNickListCtrl::NewNick (IrcNickListCtrl.cpp:149-178). The list used to hold
// "@alice" as one string, so a part, quit or rename of "alice" never found it.
void tst_IrcProtocol::nickList_keepsSymbolsApartFromNicks()
{
    const eMule::IrcServerModes modes;
    eMule::IrcNickList list;
    QVERIFY(list.add(QStringLiteral("@alice"), modes));
    QVERIFY(list.add(QStringLiteral("bob"), modes));
    QVERIFY(!list.add(QStringLiteral("alice"), modes));    // the same nick again (NAMES after JOIN)
    QVERIFY(!list.add(QStringLiteral("+ALICE"), modes));
    QCOMPARE(list.size(), 2);

    QCOMPARE(list.decorated(QStringLiteral("alice")), QStringLiteral("@alice"));
    QCOMPARE(list.decorated(QStringLiteral("nobody")), QStringLiteral("nobody"));

    QVERIFY(list.rename(QStringLiteral("alice"), QStringLiteral("alicia")));
    QCOMPARE(list.display(), (QStringList{QStringLiteral("@alicia"), QStringLiteral("bob")}));   // keeps its rank

    QVERIFY(list.remove(QStringLiteral("ALICIA")));
    QVERIFY(!list.remove(QStringLiteral("alicia")));
    QCOMPARE(list.bareNicks(), QStringList{QStringLiteral("bob")});

    QCOMPARE(eMule::IrcNickList::bare(QStringLiteral("@+carol"), modes), QStringLiteral("carol"));
}

// MFC SortProc (IrcNickListCtrl.cpp:57-73): by the index of the first symbol in the
// server's list, then by name. Only '@' and '+' were ranked.
void tst_IrcProtocol::nickList_ranksByTheServersPrefixOrder()
{
    eMule::IrcServerModes modes;
    modes.applyIsupport(QStringLiteral("PREFIX=(qaohv)~&@%+"));
    eMule::IrcNickList list;
    for (const char* nick : {"zed", "+voice", "%half", "@op", "~owner", "&admin", "Anna", "@Bert"})
        list.add(QString::fromLatin1(nick), modes);

    QCOMPARE(list.display(),
             (QStringList{QStringLiteral("~owner"), QStringLiteral("&admin"), QStringLiteral("@Bert"),
                          QStringLiteral("@op"), QStringLiteral("%half"), QStringLiteral("+voice"),
                          QStringLiteral("Anna"), QStringLiteral("zed")}));
}

// MFC ChangeNickMode (IrcNickListCtrl.cpp:272). MODE was parsed and nothing listened.
void tst_IrcProtocol::nickList_modeChangesMoveNicks()
{
    const eMule::IrcServerModes modes;
    eMule::IrcNickList list;
    list.add(QStringLiteral("alice"), modes);
    list.add(QStringLiteral("bob"), modes);

    QVERIFY(list.changeMode(QStringLiteral("bob"), u'v', true, modes));
    QCOMPARE(list.display(), (QStringList{QStringLiteral("+bob"), QStringLiteral("alice")}));
    QVERIFY(list.changeMode(QStringLiteral("bob"), u'o', true, modes));
    QCOMPARE(list.decorated(QStringLiteral("bob")), QStringLiteral("@+bob"));   // server order, not "+@"
    QVERIFY(list.changeMode(QStringLiteral("bob"), u'o', false, modes));
    QCOMPARE(list.decorated(QStringLiteral("bob")), QStringLiteral("+bob"));
    QVERIFY(list.changeMode(QStringLiteral("bob"), u'v', false, modes));
    QCOMPARE(list.display(), (QStringList{QStringLiteral("alice"), QStringLiteral("bob")}));

    QVERIFY(!list.changeMode(QStringLiteral("bob"), u'k', true, modes));     // not a user mode
    QVERIFY(!list.changeMode(QStringLiteral("carol"), u'o', true, modes));   // not here
}

// MFC CIrcWnd::ParseChangeMode (IrcWnd.cpp:1039-1097)
void tst_IrcProtocol::modeChange_consumesParametersByType()
{
    eMule::IrcServerModes modes;
    modes.applyIsupport(QStringLiteral("PREFIX=(ov)@+ CHANMODES=b,k,l,imnt"));

    // +o alice, -v bob, +b mask, +l 20, +t (none), -l (none when unsetting), -k key
    const auto changes = eMule::parseModeChange(
        QStringLiteral("+o-v+blt-lk"),
        {QStringLiteral("alice"), QStringLiteral("bob"), QStringLiteral("*!*@x"), QStringLiteral("20"),
         QStringLiteral("key")},
        modes);
    QCOMPARE(changes.size(), 7);
    QVERIFY(changes[0].userMode && changes[0].on);
    QCOMPARE(changes[0].param, QStringLiteral("alice"));
    QVERIFY(changes[1].userMode && !changes[1].on);
    QCOMPARE(changes[1].param, QStringLiteral("bob"));
    QVERIFY(!changes[2].userMode);
    QCOMPARE(changes[2].param, QStringLiteral("*!*@x"));
    QCOMPARE(changes[3].param, QStringLiteral("20"));
    QCOMPARE(changes[4].param, QString());            // +t: type D
    QCOMPARE(changes[5].param, QString());            // -l: type C, unsetting
    QCOMPARE(changes[6].param, QStringLiteral("key")); // -k: type B, always
}

// MFC IrcWnd.cpp:599-610
void tst_IrcProtocol::routing_infoLineColours()
{
    using namespace eMule::IrcRouting;
    QCOMPARE(infoLineColor(QStringLiteral("* alice has joined #x")), LineColor::Info);
    QCOMPARE(infoLineColor(QStringLiteral("-NickServ- hello")), LineColor::Notice);
    QCOMPARE(infoLineColor(QStringLiteral("- no closing dash")), LineColor::Default);
    QCOMPARE(infoLineColor(QStringLiteral("plain")), LineColor::Default);
    QCOMPARE(colorName(LineColor::Info), QStringLiteral("#009300"));
    QCOMPARE(colorName(LineColor::Notice), QStringLiteral("#7F0000"));
    QCOMPARE(colorName(LineColor::Quit), QStringLiteral("#00007F"));
    QCOMPARE(colorName(LineColor::Default), QString());
}

// MFC IrcMain.cpp:650-689, IrcWnd.cpp:556-587, 1099
void tst_IrcProtocol::routing_numerics()
{
    using namespace eMule::IrcRouting;
    for (int code : {311, 312, 313, 317, 318, 319, 314, 369})
        QCOMPARE(numericRoute(code), NumericRoute::Current);
    for (int code : {401, 433, 482, 502})
        QCOMPARE(numericRoute(code), NumericRoute::Error);
    for (int code : {2, 250, 372, 376})
        QCOMPARE(numericRoute(code), NumericRoute::Status);

    QCOMPARE(errorLine(QStringLiteral("alice :No such nick")), QStringLiteral("-Error- alice :No such nick"));
    QCOMPARE(errorLine(QStringLiteral("-x- already marked")), QStringLiteral("-x- already marked"));

    QCOMPARE(whoisIdleText(QStringLiteral("alice 45 0 :seconds idle")), QStringLiteral("alice 45secs idle"));
    QCOMPARE(whoisIdleText(QStringLiteral("alice 185 0")), QStringLiteral("alice 03mins 05secs idle"));
    QVERIFY(whoisIdleText(QStringLiteral("alice 3725 0")).startsWith(QStringLiteral("alice 01hrs 02mins 05secs idle")));
    QVERIFY(whoisIdleText(QStringLiteral("alice 5 1700000000")).contains(QStringLiteral(" idle, signed on ")));
}

// MFC CIrcWnd::NoticeMessage (IrcWnd.cpp:913-942). Every notice went to Status.
void tst_IrcProtocol::routing_notices()
{
    using eMule::IrcRouting::routeNotice;
    const QStringList tabs{QStringLiteral("#emule"), QStringLiteral("#other")};

    // to us: where the user is reading
    auto r = routeNotice(QStringLiteral("NickServ"), QStringLiteral("me"), QStringLiteral("hi"),
                         QStringLiteral("Me"), QStringLiteral("#emule"), tabs, {});
    QCOMPARE(r.channels, QStringList{QStringLiteral("#emule")});
    QVERIFY(!r.status);
    QCOMPARE(r.text, QStringLiteral("-NickServ- hi"));

    // to us while on the Status tab
    r = routeNotice(QStringLiteral("NickServ"), QStringLiteral("me"), QStringLiteral("hi"),
                    QStringLiteral("me"), QString(), tabs, {});
    QVERIFY(r.status && r.channels.isEmpty());

    // to a channel we have open
    r = routeNotice(QStringLiteral("op"), QStringLiteral("#OTHER"), QStringLiteral("rules"),
                    QStringLiteral("me"), QStringLiteral("#emule"), tabs, {});
    QCOMPARE(r.channels, QStringList{QStringLiteral("#OTHER")});
    QCOMPARE(r.text, QStringLiteral("-op:#OTHER- rules"));

    // to something else: every channel the sender is in
    r = routeNotice(QStringLiteral("op"), QStringLiteral("$*"), QStringLiteral("news"),
                    QStringLiteral("me"), QStringLiteral("#emule"), tabs, tabs);
    QCOMPARE(r.channels, tabs);

    // and when nobody knows the sender, Status
    r = routeNotice(QStringLiteral("irc.example.org"), QStringLiteral("*"), QStringLiteral("looking up"),
                    QStringLiteral("me"), QStringLiteral("#emule"), tabs, {});
    QVERIFY(r.status && r.channels.isEmpty());
    QCOMPARE(r.text, QStringLiteral("-irc.example.org- looking up"));
}

// MFC CIrcChannelTabCtrl::ChatSend (IrcChannelTabCtrl.cpp:531-628)
void tst_IrcProtocol::input_commandsAndText()
{
    using namespace eMule::IrcRouting;
    InputContext chan;
    chan.tabName = QStringLiteral("#emule");
    chan.isChannel = true;
    chan.live = true;
    chan.ownNick = QStringLiteral("me");
    const InputContext status{QString(), false, false, QStringLiteral("me")};

    // text in a channel is a message, echoed as ours
    auto r = interpretInput(QStringLiteral("hello there"), chan);
    QCOMPARE(r.raw, QStringList{QStringLiteral("PRIVMSG #emule :hello there")});
    QVERIFY(r.ownMessage);
    QCOMPARE(r.echoTo, InputResult::Echo::Tab);

    // text on the Status tab is a raw line; it used to be dropped
    r = interpretInput(QStringLiteral("WHOIS alice"), status);
    QCOMPARE(r.raw, QStringList{QStringLiteral("WHOIS alice")});
    QCOMPARE(r.echoTo, InputResult::Echo::None);

    // the same in a channel we were kicked from
    InputContext detached = chan;
    detached.live = false;
    QCOMPARE(interpretInput(QStringLiteral("JOIN #emule"), detached).raw, QStringList{QStringLiteral("JOIN #emule")});

    // /hop leaves and re-enters; it was sent to the server as "HOP"
    QCOMPARE(interpretInput(QStringLiteral("/hop"), chan).raw,
             (QStringList{QStringLiteral("PART #emule"), QStringLiteral("JOIN #emule")}));
    QVERIFY(interpretInput(QStringLiteral("/hop"), status).raw.isEmpty());

    // /msg echoes where the user is, not always on Status
    r = interpretInput(QStringLiteral("/msg alice see you"), chan);
    QCOMPARE(r.raw, QStringList{QStringLiteral("PRIVMSG alice :see you")});
    QCOMPARE(r.echo, QStringLiteral(" -> *alice* see you"));
    QCOMPARE(r.echoTo, InputResult::Echo::Tab);
    QCOMPARE(interpretInput(QStringLiteral("/msg alice see you"), status).echoTo, InputResult::Echo::Status);
    QCOMPARE(interpretInput(QStringLiteral("/notice alice psst"), chan).raw,
             QStringList{QStringLiteral("NOTICE alice :psst")});

    QCOMPARE(interpretInput(QStringLiteral("/privmsg nickserv identify pw"), chan).raw,
             QStringList{QStringLiteral("ns identify pw")});
    QCOMPARE(interpretInput(QStringLiteral("/privmsg ChanServ op #x"), chan).raw,
             QStringList{QStringLiteral("cs op #x")});
    QCOMPARE(interpretInput(QStringLiteral("/privmsg bob two words"), chan).raw,
             QStringList{QStringLiteral("PRIVMSG bob :two words")});

    // /topic with a channel names that channel; without one, the tab's
    QCOMPARE(interpretInput(QStringLiteral("/topic #other new topic"), chan).raw,
             QStringList{QStringLiteral("TOPIC #other :new topic")});
    QCOMPARE(interpretInput(QStringLiteral("/topic new topic"), chan).raw,
             QStringList{QStringLiteral("TOPIC #emule :new topic")});

    QCOMPARE(interpretInput(QStringLiteral("/part"), chan).raw, QStringList{QStringLiteral("PART #emule")});
    QCOMPARE(interpretInput(QStringLiteral("/part #x"), status).raw, QStringList{QStringLiteral("PART #x")});

    r = interpretInput(QStringLiteral("/me waves"), chan);
    QCOMPARE(r.raw, QStringList{QStringLiteral("PRIVMSG #emule :\001ACTION waves\001")});
    QCOMPARE(r.echo, QStringLiteral("* me waves"));
    QCOMPARE(r.echoColor, LineColor::Action);

    r = interpretInput(QStringLiteral("/sound ..\\ding.wav hear this"), chan);
    QCOMPARE(r.raw, QStringList{QStringLiteral("PRIVMSG #emule :\001SOUND ..\\ding.wav hear this\001")});
    QCOMPARE(r.sound, QStringLiteral("..ding.wav"));
    QCOMPARE(r.echo, QStringLiteral("* me hear this"));
    QCOMPARE(interpretInput(QStringLiteral("/sound ding.wav"), chan).echo, QStringLiteral("* me [SOUND]"));

    // /nick is only asked for here; it is stored when the server confirms
    r = interpretInput(QStringLiteral("/nick newme"), chan);
    QCOMPARE(r.raw, QStringList{QStringLiteral("NICK newme")});
    QCOMPARE(r.newNick, QStringLiteral("newme"));

    // anything else is the server's
    QCOMPARE(interpretInput(QStringLiteral("/whois alice"), chan).raw, QStringList{QStringLiteral("WHOIS alice")});
}

// MFC IrcWnd.cpp:944-983 and IrcMain.cpp:214-225
void tst_IrcProtocol::text_stripsMircCodesAndSanitisesSounds()
{
    using namespace eMule::IrcRouting;
    QCOMPARE(stripMircCodes(QStringLiteral("\x02" "Bold\x02 \x03" "04,12red on blue\x03 \x1Funder\x0F plain")),
             QStringLiteral("Bold red on blue under plain"));
    QCOMPARE(stripMircCodes(QStringLiteral("\x03" "5five 12 monkeys")), QStringLiteral("five 12 monkeys"));
    QCOMPARE(stripMircCodes(QStringLiteral("a,b 1,2")), QStringLiteral("a,b 1,2"));

    QCOMPARE(soundFileName(QStringLiteral("Ding.WAV a message")), QStringLiteral("ding.wav"));
    QCOMPARE(soundFileName(QStringLiteral("..\\..\\evil.mp3")), QStringLiteral("....evil.mp3"));
    QCOMPARE(soundFileName(QStringLiteral("/etc/passwd")), QString());
    QCOMPARE(soundFileName(QStringLiteral("script.exe")), QString());
    QCOMPARE(soundFileName(QStringLiteral(".wav")), QString());
}

QTEST_GUILESS_MAIN(tst_IrcProtocol)
#include "tst_IrcProtocol.moc"
