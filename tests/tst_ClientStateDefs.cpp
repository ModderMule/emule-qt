/// @file tst_ClientStateDefs.cpp
/// @brief Tests for client/ClientStateDefs — enum types, values, and protocol constants.

#include "TestHelpers.h"
#include "client/ClientStateDefs.h"

#include <QTest>
#include <set>
#include <type_traits>

using namespace eMule;

class tst_ClientStateDefs : public QObject {
    Q_OBJECT

private slots:
    void underlyingType_uint8();
    void uploadState_values();
    void downloadState_values();
    void clientSoftware_values();
    void secureIdentState_values();
    void infoPacketState_values();
    void sourceFrom_values();
    void chatCaptchaState_values();
    void connectingState_values();
    void identState_values();
    void distinctValues_downloadState();
};

void tst_ClientStateDefs::underlyingType_uint8()
{
    static_assert(std::is_same_v<std::underlying_type_t<UploadState>, uint8>);
    static_assert(std::is_same_v<std::underlying_type_t<DownloadState>, uint8>);
    static_assert(std::is_same_v<std::underlying_type_t<ChatState>, uint8>);
    static_assert(std::is_same_v<std::underlying_type_t<KadState>, uint8>);
    static_assert(std::is_same_v<std::underlying_type_t<ClientSoftware>, uint8>);
    static_assert(std::is_same_v<std::underlying_type_t<SecureIdentState>, uint8>);
    static_assert(std::is_same_v<std::underlying_type_t<InfoPacketState>, uint8>);
    static_assert(std::is_same_v<std::underlying_type_t<SourceFrom>, uint8>);
    static_assert(std::is_same_v<std::underlying_type_t<ChatCaptchaState>, uint8>);
    static_assert(std::is_same_v<std::underlying_type_t<ConnectingState>, uint8>);
    static_assert(std::is_same_v<std::underlying_type_t<IdentState>, uint8>);
    QVERIFY(true);
}

void tst_ClientStateDefs::uploadState_values()
{
    QCOMPARE(static_cast<uint8>(UploadState::Uploading), uint8{0});
    QCOMPARE(static_cast<uint8>(UploadState::None), uint8{4});
}

void tst_ClientStateDefs::downloadState_values()
{
    QCOMPARE(static_cast<uint8>(DownloadState::Downloading), uint8{0});
    QCOMPARE(static_cast<uint8>(DownloadState::None), uint8{13});
    QCOMPARE(static_cast<uint8>(DownloadState::RemoteQueueFull), uint8{14});
}

void tst_ClientStateDefs::clientSoftware_values()
{
    QCOMPARE(static_cast<uint8>(ClientSoftware::eMule), uint8{0});
    QCOMPARE(static_cast<uint8>(ClientSoftware::cDonkey), uint8{1});
    QCOMPARE(static_cast<uint8>(ClientSoftware::xMule), uint8{2});
    QCOMPARE(static_cast<uint8>(ClientSoftware::aMule), uint8{3});
    QCOMPARE(static_cast<uint8>(ClientSoftware::Shareaza), uint8{4});
    QCOMPARE(static_cast<uint8>(ClientSoftware::MLDonkey), uint8{10});
    QCOMPARE(static_cast<uint8>(ClientSoftware::lphant), uint8{20});
    QCOMPARE(static_cast<uint8>(ClientSoftware::eDonkeyHybrid), uint8{50});
    QCOMPARE(static_cast<uint8>(ClientSoftware::eDonkey), uint8{51});
    QCOMPARE(static_cast<uint8>(ClientSoftware::OldEMule), uint8{52});
    QCOMPARE(static_cast<uint8>(ClientSoftware::URL), uint8{53});
    QCOMPARE(static_cast<uint8>(ClientSoftware::Unknown), uint8{54});
}

void tst_ClientStateDefs::secureIdentState_values()
{
    QCOMPARE(static_cast<uint8>(SecureIdentState::Unavailable), uint8{0});
    QCOMPARE(static_cast<uint8>(SecureIdentState::AllRequestsSend), uint8{0});
    QCOMPARE(static_cast<uint8>(SecureIdentState::SignatureNeeded), uint8{1});
    QCOMPARE(static_cast<uint8>(SecureIdentState::KeyAndSigNeeded), uint8{2});
}

void tst_ClientStateDefs::infoPacketState_values()
{
    QCOMPARE(static_cast<uint8>(InfoPacketState::None), uint8{0});
    QCOMPARE(static_cast<uint8>(InfoPacketState::EDonkeyProtPack), uint8{1});
    QCOMPARE(static_cast<uint8>(InfoPacketState::EMuleProtPack), uint8{2});
    QCOMPARE(static_cast<uint8>(InfoPacketState::Both), uint8{3});
}

void tst_ClientStateDefs::sourceFrom_values()
{
    QCOMPARE(static_cast<uint8>(SourceFrom::Server), uint8{0});
    QCOMPARE(static_cast<uint8>(SourceFrom::Kademlia), uint8{1});
    QCOMPARE(static_cast<uint8>(SourceFrom::SourceExchange), uint8{2});
    QCOMPARE(static_cast<uint8>(SourceFrom::Passive), uint8{3});
    QCOMPARE(static_cast<uint8>(SourceFrom::Link), uint8{4});
}

void tst_ClientStateDefs::chatCaptchaState_values()
{
    QCOMPARE(static_cast<uint8>(ChatCaptchaState::None), uint8{0});
    QCOMPARE(static_cast<uint8>(ChatCaptchaState::SolutionSent), uint8{5});
}

void tst_ClientStateDefs::connectingState_values()
{
    QCOMPARE(static_cast<uint8>(ConnectingState::None), uint8{0});
    QCOMPARE(static_cast<uint8>(ConnectingState::Preconditions), uint8{5});
}

void tst_ClientStateDefs::identState_values()
{
    QCOMPARE(static_cast<uint8>(IdentState::NotAvailable), uint8{0});
    QCOMPARE(static_cast<uint8>(IdentState::IdNeeded), uint8{1});
    QCOMPARE(static_cast<uint8>(IdentState::Identified), uint8{2});
    QCOMPARE(static_cast<uint8>(IdentState::IdFailed), uint8{3});
    QCOMPARE(static_cast<uint8>(IdentState::IdBadGuy), uint8{4});
}

void tst_ClientStateDefs::distinctValues_downloadState()
{
    // Verify no collisions among sequential values
    std::set<uint8> seen;
    for (uint8 v = 0; v <= static_cast<uint8>(DownloadState::RemoteQueueFull); ++v)
        QVERIFY(seen.insert(v).second);
}

QTEST_MAIN(tst_ClientStateDefs)
#include "tst_ClientStateDefs.moc"
