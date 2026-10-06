/// @file tst_ChatCaptcha.cpp
/// @brief Chat captcha and OP_MESSAGE framing against the MFC wire format
///        (srchybrid/BaseClient.cpp:2625-2811, ListenSocket.cpp:643-664).

#include "TestHelpers.h"

#include "app/AppContext.h"
#include "client/UpDownClient.h"
#include "net/ClientReqSocket.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QBuffer>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>
#include <memory>
#include <vector>

using namespace eMule;

namespace {

/// Records what the client sends and feeds it framed packets through the real dispatch.
class ChatSocket : public ClientReqSocket {
    Q_OBJECT

public:
    struct Sent {
        uint8 opcode = 0;
        QByteArray payload;
    };
    std::vector<Sent> sent;

    void sendPacket(std::unique_ptr<Packet> packet, bool = true, uint32 = 0, bool = false) override
    {
        sent.push_back({ packet->opcode, QByteArray(packet->pBuffer, static_cast<int>(packet->size)) });
    }

    [[nodiscard]] std::vector<QByteArray> all(uint8 opcode) const
    {
        std::vector<QByteArray> out;
        for (const auto& s : sent)
            if (s.opcode == opcode)
                out.push_back(s.payload);
        return out;
    }

    void markConnected() { m_conState.store(EMSState::Connected, std::memory_order_release); }

    bool deliver(uint8 protocol, uint8 opcode, const QByteArray& payload)
    {
        char header[kPacketHeaderSize];
        auto* h = reinterpret_cast<HeaderStruct*>(header);
        h->eDonkeyID = protocol;
        h->packetLength = static_cast<uint32>(payload.size()) + 1;
        h->command = opcode;

        Packet packet(header);
        packet.pBuffer = new char[packet.size + 1];
        std::memcpy(packet.pBuffer, payload.constData(), packet.size);
        try {
            return packetReceived(&packet);
        } catch (...) {
            return false;
        }
    }

    bool say(const QString& text)
    {
        const QByteArray utf8 = text.toUtf8();
        QByteArray wire;
        wire.append(char(utf8.size() & 0xFF));
        wire.append(char((utf8.size() >> 8) & 0xFF));
        wire.append(utf8);
        return deliver(OP_EDONKEYPROT, OP_MESSAGE, wire);
    }
};

/// A peer past the handshake, wired to a recording socket.
struct ChatPeer {
    UpDownClient client;
    ChatSocket sock;
    QSignalSpy shown{ &client, &UpDownClient::chatMessageReceived };

    explicit ChatPeer(bool supportsCaptcha = true)
    {
        client.setUserAddress(Address::fromString(QStringLiteral("81.2.69.200")));
        client.setUserPort(4662);
        uint8 hash[16];
        std::memset(hash, 0x6C, sizeof(hash));
        client.setUserHash(hash);
        client.setUserName(QStringLiteral("peer"));
        client.setSupportsCaptcha(supportsCaptcha);
        client.wireIncomingSocket(&sock);
        sock.markConnected();
    }

    ~ChatPeer() { client.setSocket(nullptr); }
};

/// A captcha as a stock client sends it: tag count 0, then a 1-bit BMP.
QByteArray stockCaptchaRequest(int width = 104, int height = 48)
{
    QImage img(width, height, QImage::Format_Mono);
    img.fill(1);
    QByteArray wire(1, '\0');
    QBuffer buffer(&wire);
    buffer.open(QIODevice::Append);
    img.save(&buffer, "BMP");
    return wire;
}

} // namespace

class tst_ChatCaptcha : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void firstMessage_isHeldBackBehindAStockShapedChallenge();
    void rightAnswer_releasesTheHeldMessage();
    void wrongAnswers_endAfterThreeTries();
    void peerWithoutCaptchaSupport_isToldOnce();
    void unsolicitedResult_changesNothing();
    void unrequestedChallenge_isIgnored();
    void challengeAfterOurMessage_isShownAndAnswered();
    void oversizedChallenge_isIgnored();
    void message_lengthMustMatchThePacket();
    void message_isCutAtTheClientLimit();

private:
    std::unique_ptr<QTemporaryDir> m_tmpDir;
};

void tst_ChatCaptcha::initTestCase()
{
    m_tmpDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmpDir->isValid());
    thePrefs.setConfigDir(m_tmpDir->path());
}

void tst_ChatCaptcha::init()
{
    thePrefs.setEnableSpamFilter(true);
    thePrefs.setUseChatCaptchas(true);
    thePrefs.setMsgOnlyFriends(false);
    thePrefs.setMsgSecure(false);
    thePrefs.setMessageFilter(QString());
}

void tst_ChatCaptcha::firstMessage_isHeldBackBehindAStockShapedChallenge()
{
    ChatPeer p;
    QVERIFY(p.sock.say(QStringLiteral("hello there")));

    QCOMPARE(p.shown.count(), 0);
    QCOMPARE(p.client.chatCaptchaState(), ChatCaptchaState::ChallengeSent);
    QCOMPARE(p.client.captchaChallenge().length(), 4);

    const auto challenges = p.sock.all(OP_CHATCAPTCHAREQ);
    QCOMPARE(challenges.size(), size_t(1));
    const QByteArray& wire = challenges.front();
    // What a stock reader does with it (srchybrid/BaseClient.cpp:2769-2783)
    QCOMPARE(uint8(wire.at(0)), uint8(0));                 // tag count
    const QByteArray bmp = wire.mid(1);
    QVERIFY2(bmp.size() > 128 && bmp.size() < 4096, qPrintable(QString::number(bmp.size())));
    QImage img;
    QVERIFY(img.loadFromData(bmp, "BMP"));
    QVERIFY(img.height() > 10 && img.height() < 50);
    QVERIFY(img.width() > 10 && img.width() < 150);
    QCOMPARE(img.depth(), 1);

    // For a look at what peers are asked to read
    if (const QByteArray dump = qgetenv("CAPTCHA_DUMP"); !dump.isEmpty())
        QVERIFY(img.scaled(img.size() * 4).save(QString::fromLocal8Bit(dump), "PNG"));
}

void tst_ChatCaptcha::rightAnswer_releasesTheHeldMessage()
{
    ChatPeer p;
    QVERIFY(p.sock.say(QStringLiteral("hello there")));
    const QString challenge = p.client.captchaChallenge();

    // Case and surrounding text do not matter: the tail is compared
    QVERIFY(p.sock.say(QStringLiteral("it says ") + challenge.toLower() + QStringLiteral("  ")));

    QCOMPARE(p.sock.all(OP_CHATCAPTCHARES), std::vector<QByteArray>{ QByteArray(1, '\0') });
    QCOMPARE(p.shown.count(), 1);
    QCOMPARE(p.shown.at(0).at(1).toString(), QStringLiteral("hello there"));
    QCOMPARE(p.client.chatCaptchaState(), ChatCaptchaState::CaptchaSolved);

    // From here on the peer just talks
    QVERIFY(p.sock.say(QStringLiteral("second")));
    QCOMPARE(p.shown.count(), 2);
    QCOMPARE(p.sock.all(OP_CHATCAPTCHAREQ).size(), size_t(1));
}

void tst_ChatCaptcha::wrongAnswers_endAfterThreeTries()
{
    ChatPeer p;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        QVERIFY(p.sock.say(QStringLiteral("let me in")));
        QCOMPARE(p.sock.all(OP_CHATCAPTCHAREQ).size(), size_t(attempt));
        QVERIFY(p.sock.say(QStringLiteral("????")));
        QCOMPARE(p.client.chatCaptchaState(), ChatCaptchaState::None);
    }
    const std::vector<QByteArray> verdicts{ QByteArray(1, '\1'), QByteArray(1, '\1'),
                                           QByteArray(1, '\2') };
    QCOMPARE(p.sock.all(OP_CHATCAPTCHARES), verdicts);

    // No fourth challenge, and nothing was ever shown
    QVERIFY(p.sock.say(QStringLiteral("let me in")));
    QCOMPARE(p.sock.all(OP_CHATCAPTCHAREQ).size(), size_t(3));
    QCOMPARE(p.shown.count(), 0);
}

void tst_ChatCaptcha::peerWithoutCaptchaSupport_isToldOnce()
{
    ChatPeer p(/*supportsCaptcha*/ false);
    QVERIFY(p.sock.say(QStringLiteral("hi")));
    QVERIFY(p.sock.say(QStringLiteral("hi again")));

    QCOMPARE(p.sock.all(OP_CHATCAPTCHAREQ).size(), size_t(0));
    QCOMPARE(p.sock.all(OP_MESSAGE).size(), size_t(1));
    QVERIFY(p.sock.all(OP_MESSAGE).front().contains("captcha"));
    QCOMPARE(p.shown.count(), 0);
}

void tst_ChatCaptcha::unsolicitedResult_changesNothing()
{
    ChatPeer p;
    QSignalSpy result(&p.client, &UpDownClient::captchaResultReceived);

    QVERIFY(p.sock.deliver(OP_EMULEPROT, OP_CHATCAPTCHARES, QByteArray(1, '\0')));

    QCOMPARE(p.client.chatCaptchaState(), ChatCaptchaState::None);
    QCOMPARE(result.count(), 0);
    // It bought the peer nothing: the first message is still challenged
    QVERIFY(p.sock.say(QStringLiteral("hello")));
    QCOMPARE(p.shown.count(), 0);
    QCOMPARE(p.sock.all(OP_CHATCAPTCHAREQ).size(), size_t(1));
}

void tst_ChatCaptcha::unrequestedChallenge_isIgnored()
{
    ChatPeer p;
    QSignalSpy request(&p.client, &UpDownClient::captchaRequestReceived);

    QVERIFY(p.sock.deliver(OP_EMULEPROT, OP_CHATCAPTCHAREQ, stockCaptchaRequest()));

    QCOMPARE(request.count(), 0);
    QCOMPARE(p.client.chatCaptchaState(), ChatCaptchaState::None);
}

void tst_ChatCaptcha::challengeAfterOurMessage_isShownAndAnswered()
{
    ChatPeer p;
    QSignalSpy request(&p.client, &UpDownClient::captchaRequestReceived);
    QSignalSpy result(&p.client, &UpDownClient::captchaResultReceived);

    p.client.sendChatMessage(QStringLiteral("hi"));
    QCOMPARE(p.client.chatCaptchaState(), ChatCaptchaState::Accepting);
    QCOMPARE(p.sock.all(OP_MESSAGE).size(), size_t(1));

    QVERIFY(p.sock.deliver(OP_EMULEPROT, OP_CHATCAPTCHAREQ, stockCaptchaRequest()));
    QCOMPARE(request.count(), 1);
    QCOMPARE(request.at(0).at(1).value<QImage>().size(), QSize(104, 48));
    QCOMPARE(p.client.chatCaptchaState(), ChatCaptchaState::CaptchaRecv);

    p.client.sendChatMessage(QStringLiteral("AB3D"));
    QCOMPARE(p.client.chatCaptchaState(), ChatCaptchaState::SolutionSent);

    QVERIFY(p.sock.deliver(OP_EMULEPROT, OP_CHATCAPTCHARES, QByteArray(1, '\0')));
    QCOMPARE(result.count(), 1);
    QCOMPARE(result.at(0).at(1).toBool(), true);
    QCOMPARE(p.client.chatCaptchaState(), ChatCaptchaState::None);
}

void tst_ChatCaptcha::oversizedChallenge_isIgnored()
{
    ChatPeer p;
    QSignalSpy request(&p.client, &UpDownClient::captchaRequestReceived);
    p.client.sendChatMessage(QStringLiteral("hi"));

    // A picture too large to be a captcha, in bytes and in pixels
    QVERIFY(p.sock.deliver(OP_EMULEPROT, OP_CHATCAPTCHAREQ, stockCaptchaRequest(1024, 768)));
    QVERIFY(p.sock.deliver(OP_EMULEPROT, OP_CHATCAPTCHAREQ, stockCaptchaRequest(200, 48)));

    QCOMPARE(request.count(), 0);
    QCOMPARE(p.client.chatCaptchaState(), ChatCaptchaState::Accepting);
}

void tst_ChatCaptcha::message_lengthMustMatchThePacket()
{
    thePrefs.setUseChatCaptchas(false);
    ChatPeer p;

    QByteArray wire;
    wire.append(char(10));   // says 10 bytes
    wire.append(char(0));
    wire.append("hello");    // has 5
    p.sock.deliver(OP_EDONKEYPROT, OP_MESSAGE, wire);
    p.sock.deliver(OP_EDONKEYPROT, OP_MESSAGE, QByteArray(1, 'x'));

    QCOMPARE(p.shown.count(), 0);
}

void tst_ChatCaptcha::message_isCutAtTheClientLimit()
{
    thePrefs.setUseChatCaptchas(false);
    ChatPeer p;

    QVERIFY(p.sock.say(QString(2000, QLatin1Char('a'))));

    QCOMPARE(p.shown.count(), 1);
    QCOMPARE(p.shown.at(0).at(1).toString().length(), qsizetype(MAX_CLIENT_MSG_LEN));
}

// Without a GUI application on purpose: that is how the daemon runs, and drawing the
// captcha must not need one.
QTEST_GUILESS_MAIN(tst_ChatCaptcha)
#include "tst_ChatCaptcha.moc"
