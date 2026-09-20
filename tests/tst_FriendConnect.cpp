/// @file tst_FriendConnect.cpp
/// @brief The friend connection state machine — MFC CFriend::TryToConnect and
///        UpdateFriendConnectionState (srchybrid/Friend.cpp:226-437).
///
/// Before this existed, every report site in UpDownClient was a dead end: the secure-ident
/// verdict was computed and thrown away, a disconnect never looked the friend up in Kad,
/// and a chat message to an offline friend was simply dropped on the floor.

#include <QSignalSpy>
#include <QTest>

#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "friends/Friend.h"
#include "friends/FriendList.h"

#include <cstring>
#include <memory>

using namespace eMule;

namespace {

/// The address a friend entry is created with, and the one its chat client dials.
constexpr const char* kFriendIP = "81.2.69.170";
constexpr uint16 kFriendPort = 4662;

void fillHash(uint8* hash, uint8 pattern)
{
    std::memset(hash, pattern, 16);
}

} // namespace

class tst_FriendConnect : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void tryToConnect_withNoAddressAndNoKadIdFails();
    void tryToConnect_buildsAndRegistersAChatClient();
    void clientForChatSession_reusesTheLinkedClient();
    void established_withoutSecureIdentWaitsForAuthentication();
    void secureIdentFailed_endsTheAttempt();
    void disconnected_withoutKadGivesUp();
    void userHashFailed_swapsTheClientAndKeepsThePendingText();
    void deleted_endsTheAttemptFromInsideTheClientDestructor();
    void sendOrQueueChatMessage_offlineParksTheTextAndDials();
    void removeFriendWhileConnecting_reportsFailure();

    // Closing the chat window — MFC CChatSelector::EndSession (ChatSelector.cpp:464-490)
    void endChatSession_midDialCancelsTheAttemptAndReportsFailure();
    void endChatSession_dropsTheParkedText();
    void endChatSession_onAnIdleFriendIsHarmless();
    void endChatSession_clearsCaptchaState();

private:
    /// A friend with an address to dial, owned by the list.
    Friend* makeFriend(uint8 hashByte = 0x51);

    std::unique_ptr<FriendList> m_friends;
    std::unique_ptr<ClientList> m_clients;
    FriendList* m_savedFriendList = nullptr;
    ClientList* m_savedClientList = nullptr;
};

void tst_FriendConnect::init()
{
    m_savedFriendList = theApp.friendList;
    m_savedClientList = theApp.clientList;

    m_friends = std::make_unique<FriendList>();
    m_clients = std::make_unique<ClientList>();
    theApp.friendList = m_friends.get();
    theApp.clientList = m_clients.get();
}

void tst_FriendConnect::cleanup()
{
    m_friends.reset();
    m_clients.reset();
    theApp.friendList = m_savedFriendList;
    theApp.clientList = m_savedClientList;
}

Friend* tst_FriendConnect::makeFriend(uint8 hashByte)
{
    uint8 hash[16];
    fillHash(hash, hashByte);
    return m_friends->addFriend(hash, Address::fromString(QLatin1String(kFriendIP)),
                                kFriendPort, QStringLiteral("test-friend"));
}

// Nothing to dial and no Kad ID to look one up with: the attempt fails at once rather than
// leaving the chat tab waiting for something that will never come.
// MFC srchybrid/Friend.cpp:231-238.
void tst_FriendConnect::tryToConnect_withNoAddressAndNoKadIdFails()
{
    uint8 hash[16];
    fillHash(hash, 0x52);
    Friend* f = m_friends->addFriend(hash, Address(), 0, QStringLiteral("nowhere"));
    QVERIFY(f != nullptr);

    QSignalSpy result(m_friends.get(), &FriendList::friendConnectingResult);

    QVERIFY(!f->tryToConnect());
    QCOMPARE(f->connectState(), FriendConnectState::None);
    QCOMPARE(result.count(), 1);
    QCOMPARE(result.at(0).at(1).toBool(), false);
}

// An offline friend has no client object, so the attempt builds one from the stored
// address and registers it — that is what gives the dial something to run on.
// MFC CFriend::GetClientForChatSession (srchybrid/Friend.cpp:210-224).
void tst_FriendConnect::tryToConnect_buildsAndRegistersAChatClient()
{
    Friend* f = makeFriend();
    QVERIFY(f->linkedClient() == nullptr);

    QSignalSpy progress(m_friends.get(), &FriendList::friendConnectionProgress);

    QVERIFY(f->tryToConnect());

    UpDownClient* client = f->linkedClient();
    QVERIFY2(client != nullptr, "the attempt must have a client to run on");
    QVERIFY(m_clients->isValidClient(client));
    QCOMPARE(client->friendPtr(), f);
    QCOMPARE(client->userPort(), kFriendPort);
    QCOMPARE(client->chatState(), ChatState::Connecting);
    QCOMPARE(f->connectState(), FriendConnectState::Connecting);

    QVERIFY(progress.count() >= 1);
    QCOMPARE(progress.at(0).at(1).value<ChatConnectProgress>(), ChatConnectProgress::Connecting);
}

void tst_FriendConnect::clientForChatSession_reusesTheLinkedClient()
{
    Friend* f = makeFriend(0x53);

    UpDownClient* first = f->clientForChatSession();
    QVERIFY(first != nullptr);
    QCOMPARE(f->clientForChatSession(), first);
    QCOMPARE(first->chatState(), ChatState::Chatting);
}

// Connected is not the end of it: the friend still has to prove who it is, and MFC parks
// the attempt in Auth until the signature arrives. srchybrid/Friend.cpp:273-300.
void tst_FriendConnect::established_withoutSecureIdentWaitsForAuthentication()
{
    Friend* f = makeFriend(0x54);
    QVERIFY(f->tryToConnect());

    QSignalSpy progress(m_friends.get(), &FriendList::friendConnectionProgress);
    QSignalSpy result(m_friends.get(), &FriendList::friendConnectingResult);

    f->updateFriendConnectionState(FriendConnectReport::Established);

    QCOMPARE(f->connectState(), FriendConnectState::Auth);
    QCOMPARE(result.count(), 0);   // no verdict yet
    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress.at(0).at(1).value<ChatConnectProgress>(),
             ChatConnectProgress::Authenticating);
}

// It has our friend's hash but cannot prove it. Looking for another address would only
// find the same impostor, so the attempt stops here. srchybrid/Friend.cpp:350-359.
void tst_FriendConnect::secureIdentFailed_endsTheAttempt()
{
    Friend* f = makeFriend(0x55);
    QVERIFY(f->tryToConnect());
    f->updateFriendConnectionState(FriendConnectReport::Established);
    QCOMPARE(f->connectState(), FriendConnectState::Auth);

    QSignalSpy result(m_friends.get(), &FriendList::friendConnectingResult);

    f->updateFriendConnectionState(FriendConnectReport::SecureIdentFailed);

    QCOMPARE(f->connectState(), FriendConnectState::None);
    QCOMPARE(result.count(), 1);
    QCOMPARE(result.at(0).at(1).toBool(), false);
}

// Without a Kad ID there is nowhere else to look, so a dropped connection is the end.
void tst_FriendConnect::disconnected_withoutKadGivesUp()
{
    Friend* f = makeFriend(0x56);
    QVERIFY(f->tryToConnect());
    QVERIFY(!f->hasKadID());

    QSignalSpy result(m_friends.get(), &FriendList::friendConnectingResult);

    f->updateFriendConnectionState(FriendConnectReport::Disconnected);

    QCOMPARE(f->connectState(), FriendConnectState::None);
    QCOMPARE(result.count(), 1);
    QCOMPARE(result.at(0).at(1).toBool(), false);
}

// Somebody else answered at that address. MFC drops the client object entirely — we want
// nothing to do with it — and starts a fresh one carrying the hash we are looking for.
// The text the user typed has to survive that swap. srchybrid/Friend.cpp:325-345.
void tst_FriendConnect::userHashFailed_swapsTheClientAndKeepsThePendingText()
{
    Friend* f = makeFriend(0x57);
    QVERIFY(f->tryToConnect());

    UpDownClient* impostor = f->linkedClient();
    QVERIFY(impostor != nullptr);
    impostor->setPendingChatMessage(QStringLiteral("are you there?"));

    QSignalSpy result(m_friends.get(), &FriendList::friendConnectingResult);

    f->updateFriendConnectionState(FriendConnectReport::UserHashFailed);

    UpDownClient* fresh = f->linkedClient();
    QVERIFY2(fresh != nullptr, "the friend must be left with a client to try again with");
    QVERIFY2(fresh != impostor, "the impostor's client object must be dropped");
    QCOMPARE(impostor->friendPtr(), nullptr);
    QCOMPARE(impostor->chatState(), ChatState::None);
    QVERIFY2(fresh->hasPendingChatMessage(), "the typed message was lost in the swap");
    QCOMPARE(f->connectState(), FriendConnectState::None);
    QCOMPARE(result.count(), 1);
    QCOMPARE(result.at(0).at(1).toBool(), false);
}

// The client dies underneath a running attempt. The report fires from inside
// ~UpDownClient, so nothing may call back into that object. srchybrid/BaseClient.cpp:269.
void tst_FriendConnect::deleted_endsTheAttemptFromInsideTheClientDestructor()
{
    Friend* f = makeFriend(0x58);
    QVERIFY(f->tryToConnect());

    UpDownClient* client = f->linkedClient();
    QVERIFY(client != nullptr);

    QSignalSpy result(m_friends.get(), &FriendList::friendConnectingResult);

    m_clients->removeClient(client);
    delete client;   // ~UpDownClient raises Deleted

    QCOMPARE(f->connectState(), FriendConnectState::None);
    QCOMPARE(f->linkedClient(), nullptr);
    QCOMPARE(result.count(), 1);
    QCOMPARE(result.at(0).at(1).toBool(), false);
}

// The whole point of the exercise: a message typed at an offline friend is parked and the
// friend dialled, instead of being silently dropped.
void tst_FriendConnect::sendOrQueueChatMessage_offlineParksTheTextAndDials()
{
    Friend* f = makeFriend(0x59);

    QVERIFY(f->sendOrQueueChatMessage(QStringLiteral("hello")));

    UpDownClient* client = f->linkedClient();
    QVERIFY(client != nullptr);
    QVERIFY2(client->hasPendingChatMessage(), "the message must wait for the session");
    QCOMPARE(f->connectState(), FriendConnectState::Connecting);
}

// Deleting a friend mid-dial owes its watchers a verdict, or the chat tab spins for ever
// over an entry that no longer exists.
void tst_FriendConnect::removeFriendWhileConnecting_reportsFailure()
{
    Friend* f = makeFriend(0x5A);
    QVERIFY(f->tryToConnect());

    QSignalSpy result(m_friends.get(), &FriendList::friendConnectingResult);

    QVERIFY(m_friends->removeFriend(f));
    QCOMPARE(result.count(), 1);
    QCOMPARE(result.at(0).at(1).toBool(), false);
}

// Closing the tab mid-dial has to stop the dial, not just tidy the GUI. MFC gets away
// with setting MS_NONE because it has no connect state of its own; here an attempt left
// running lands in finishConnecting(true) and re-opens the session.
void tst_FriendConnect::endChatSession_midDialCancelsTheAttemptAndReportsFailure()
{
    Friend* f = makeFriend(0x5B);
    QVERIFY(f->tryToConnect());
    UpDownClient* client = f->linkedClient();
    QVERIFY(client != nullptr);
    QCOMPARE(client->chatState(), ChatState::Connecting);

    QSignalSpy result(m_friends.get(), &FriendList::friendConnectingResult);

    f->endChatSession();

    QVERIFY(!f->isTryingToConnect());
    QCOMPARE(f->connectState(), FriendConnectState::None);
    QCOMPARE(client->chatState(), ChatState::None);
    QCOMPARE(result.count(), 1);
    QCOMPARE(result.at(0).at(1).toBool(), false);
}

// The text the user typed and then walked away from must not turn up later.
void tst_FriendConnect::endChatSession_dropsTheParkedText()
{
    Friend* f = makeFriend(0x5C);
    QVERIFY(f->sendOrQueueChatMessage(QStringLiteral("never mind")));

    UpDownClient* client = f->linkedClient();
    QVERIFY(client != nullptr);
    QVERIFY(client->hasPendingChatMessage());

    f->endChatSession();
    QVERIFY(!client->hasPendingChatMessage());

    // And a dial that lands afterwards may not resurrect the session.
    f->updateFriendConnectionState(FriendConnectReport::Established);
    QCOMPARE(client->chatState(), ChatState::None);
    QVERIFY(!client->hasPendingChatMessage());
}

void tst_FriendConnect::endChatSession_onAnIdleFriendIsHarmless()
{
    Friend* f = makeFriend(0x5D);
    QSignalSpy result(m_friends.get(), &FriendList::friendConnectingResult);

    f->endChatSession();   // nothing running, no client yet

    QCOMPARE(f->connectState(), FriendConnectState::None);
    QCOMPARE(result.count(), 0);
    QCOMPARE(f->linkedClient(), nullptr);
}

// MFC clears the captcha state alongside the chat state (ChatSelector.cpp:475-476), so a
// reopened window starts a fresh challenge instead of inheriting a half-finished one.
void tst_FriendConnect::endChatSession_clearsCaptchaState()
{
    Friend* f = makeFriend(0x5E);
    UpDownClient* client = f->clientForChatSession();
    QVERIFY(client != nullptr);
    client->setChatCaptchaState(ChatCaptchaState::ChallengeSent);

    client->endChatSession();

    QCOMPARE(client->chatState(), ChatState::None);
    QCOMPARE(client->chatCaptchaState(), ChatCaptchaState::None);
}

QTEST_MAIN(tst_FriendConnect)
#include "tst_FriendConnect.moc"
