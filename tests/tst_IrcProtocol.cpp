/// @file tst_IrcProtocol.cpp
/// @brief Tests for chat/IrcMessage — IRC message parsing (RFC 2812).

#include "TestHelpers.h"
#include "chat/IrcMessage.h"
#include "chat/IrcEmuleProto.h"

#include <QTest>

using namespace eMule;

class tst_IrcProtocol : public QObject {
    Q_OBJECT

private slots:
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

QTEST_GUILESS_MAIN(tst_IrcProtocol)
#include "tst_IrcProtocol.moc"
