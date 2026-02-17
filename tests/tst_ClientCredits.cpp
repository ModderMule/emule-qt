/// @file tst_ClientCredits.cpp
/// @brief Tests for client/ClientCredits — credit formula, persistence, identity state, RSA crypto.

#include "TestHelpers.h"
#include "client/ClientCredits.h"
#include "prefs/Preferences.h"
#include "utils/OtherFunctions.h"
#include "utils/Opcodes.h"
#include "utils/SafeFile.h"

#include <QTest>

#include <cstring>
#include <ctime>

using namespace eMule;
using namespace eMule::testing;

class tst_ClientCredits : public QObject {
    Q_OBJECT

private slots:
    // ClientCredits
    void construct_fromKey();
    void construct_fromStruct();
    void addUploaded_addDownloaded();
    void uploadedTotal_downloadedTotal();
    void scoreRatio_noDownloads();
    void scoreRatio_withCredits();
    void scoreRatio_cappedAt10();
    void identState_initial();
    void identState_verified();
    void identState_badGuy();
    void setSecureIdent_basic();
    void setSecureIdent_noOverwrite();
    void waitTime();

    // ClientCreditsList
    void persistence_roundTrip();
    void persistence_expiry();
    void persistence_versionCompat();
    void getCredit_createNew();
    void getCredit_existing();
    void creditCount();

    // RSA crypto
    void crypto_keyPairGeneration();
    void crypto_signVerifyRoundTrip();
    void crypto_signVerifyTampered();
    void cryptoAvailable_afterInit();
};

static uint8 testHash[16] = {
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10
};

static uint8 testHash2[16] = {
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
    0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20
};

// ---------------------------------------------------------------------------
// ClientCredits
// ---------------------------------------------------------------------------

void tst_ClientCredits::construct_fromKey()
{
    ClientCredits c(testHash);
    QVERIFY(md4equ(c.key(), testHash));
    QCOMPARE(c.uploadedTotal(), uint64{0});
    QCOMPARE(c.downloadedTotal(), uint64{0});
    QCOMPARE(c.scoreRatio(0x01020304), 1.0f);
}

void tst_ClientCredits::construct_fromStruct()
{
    CreditStruct cs{};
    std::memcpy(cs.key.data(), testHash, 16);
    cs.uploadedLo = 5000;
    cs.downloadedLo = 10000;
    cs.lastSeen = static_cast<uint32>(std::time(nullptr));

    ClientCredits c(cs);
    QVERIFY(md4equ(c.key(), testHash));
    QCOMPARE(c.uploadedTotal(), uint64{5000});
    QCOMPARE(c.downloadedTotal(), uint64{10000});
}

void tst_ClientCredits::addUploaded_addDownloaded()
{
    ClientCredits c(testHash);
    c.addUploaded(1000, 0x01020304);
    QCOMPARE(c.uploadedTotal(), uint64{1000});
    c.addDownloaded(2000, 0x01020304);
    QCOMPARE(c.downloadedTotal(), uint64{2000});
    c.addUploaded(500, 0x01020304);
    QCOMPARE(c.uploadedTotal(), uint64{1500});
}

void tst_ClientCredits::uploadedTotal_downloadedTotal()
{
    // Test 64-bit arithmetic across high/low words
    CreditStruct cs{};
    std::memcpy(cs.key.data(), testHash, 16);
    cs.uploadedHi = 1;
    cs.uploadedLo = 0;
    cs.downloadedHi = 2;
    cs.downloadedLo = 100;
    cs.lastSeen = static_cast<uint32>(std::time(nullptr));

    ClientCredits c(cs);
    QCOMPARE(c.uploadedTotal(), (uint64{1} << 32));
    QCOMPARE(c.downloadedTotal(), (uint64{2} << 32) + 100);
}

void tst_ClientCredits::scoreRatio_noDownloads()
{
    ClientCredits c(testHash);
    // No downloads → 1.0
    QCOMPARE(c.scoreRatio(0x01020304), 1.0f);

    // Below 1MB downloaded → 1.0
    c.addDownloaded(500000, 0x01020304);
    QCOMPARE(c.scoreRatio(0x01020304), 1.0f);
}

void tst_ClientCredits::scoreRatio_withCredits()
{
    CreditStruct cs{};
    std::memcpy(cs.key.data(), testHash, 16);
    // 5MB downloaded, 1MB uploaded
    cs.downloadedLo = 5 * 1024 * 1024;
    cs.uploadedLo = 1 * 1024 * 1024;
    cs.lastSeen = static_cast<uint32>(std::time(nullptr));

    ClientCredits c(cs);
    float ratio = c.scoreRatio(0x01020304);
    QVERIFY(ratio > 1.0f);
    QVERIFY(ratio <= 10.0f);
}

void tst_ClientCredits::scoreRatio_cappedAt10()
{
    CreditStruct cs{};
    std::memcpy(cs.key.data(), testHash, 16);
    // Massive download, tiny upload → should cap at 10.0
    cs.downloadedHi = 10;
    cs.downloadedLo = 0;
    cs.uploadedLo = 1;
    cs.lastSeen = static_cast<uint32>(std::time(nullptr));

    ClientCredits c(cs);
    float ratio = c.scoreRatio(0x01020304);
    QCOMPARE(ratio, 10.0f);
}

void tst_ClientCredits::identState_initial()
{
    // No public key → NotAvailable
    ClientCredits c(testHash);
    QCOMPARE(c.currentIdentState(0x01020304), IdentState::NotAvailable);

    // With public key → IdNeeded
    CreditStruct cs{};
    std::memcpy(cs.key.data(), testHash, 16);
    cs.keySize = 10;
    std::memset(cs.secureIdent.data(), 0xAA, 10);
    cs.lastSeen = static_cast<uint32>(std::time(nullptr));

    ClientCredits c2(cs);
    QCOMPARE(c2.currentIdentState(0x01020304), IdentState::IdNeeded);
}

void tst_ClientCredits::identState_verified()
{
    ClientCredits c(testHash);

    uint8 pubKey[10];
    std::memset(pubKey, 0xBB, sizeof(pubKey));
    QVERIFY(c.setSecureIdent(pubKey, 10));
    QCOMPARE(c.currentIdentState(0x01020304), IdentState::IdNeeded);

    c.verified(0x01020304);
    QCOMPARE(c.currentIdentState(0x01020304), IdentState::Identified);
}

void tst_ClientCredits::identState_badGuy()
{
    ClientCredits c(testHash);

    uint8 pubKey[10];
    std::memset(pubKey, 0xCC, sizeof(pubKey));
    c.setSecureIdent(pubKey, 10);
    c.verified(0x01020304);

    // Same IP → Identified
    QCOMPARE(c.currentIdentState(0x01020304), IdentState::Identified);
    // Different IP → BadGuy
    QCOMPARE(c.currentIdentState(0x09080706), IdentState::IdBadGuy);
}

void tst_ClientCredits::setSecureIdent_basic()
{
    ClientCredits c(testHash);
    uint8 pubKey[20];
    std::memset(pubKey, 0xDD, sizeof(pubKey));

    QVERIFY(c.setSecureIdent(pubKey, 20));
    QCOMPARE(c.secIDKeyLen(), uint8{20});
    QCOMPARE(c.currentIdentState(0x01020304), IdentState::IdNeeded);
}

void tst_ClientCredits::setSecureIdent_noOverwrite()
{
    // If already has a key stored in struct, reject new key
    CreditStruct cs{};
    std::memcpy(cs.key.data(), testHash, 16);
    cs.keySize = 5;
    std::memset(cs.secureIdent.data(), 0xEE, 5);
    cs.lastSeen = static_cast<uint32>(std::time(nullptr));

    ClientCredits c(cs);

    uint8 newKey[10];
    std::memset(newKey, 0xFF, sizeof(newKey));
    QVERIFY(!c.setSecureIdent(newKey, 10));  // should reject
}

void tst_ClientCredits::waitTime()
{
    ClientCredits c(testHash);

    // After construction with key, wait times are initialized
    uint32 wt = c.secureWaitStartTime(0x01020304);
    QVERIFY(wt != 0);

    // Clear and verify
    c.clearWaitStartTime();
    // secureWaitStartTime should re-initialize when called after clear
    uint32 wt2 = c.secureWaitStartTime(0x01020304);
    QVERIFY(wt2 != 0);
}

// ---------------------------------------------------------------------------
// ClientCreditsList — persistence
// ---------------------------------------------------------------------------

void tst_ClientCredits::persistence_roundTrip()
{
    TempDir tmp;
    QString path = tmp.filePath(QStringLiteral("clients.met"));

    // Create and save
    {
        ClientCreditsList list;
        auto* c1 = list.getCredit(testHash);
        c1->addUploaded(5000, 0x01020304);
        c1->addDownloaded(10000, 0x01020304);

        auto* c2 = list.getCredit(testHash2);
        c2->addUploaded(1000, 0x01020304);

        QVERIFY(list.saveList(path));
    }

    // Load and verify
    {
        ClientCreditsList list;
        QVERIFY(list.loadList(path));
        QCOMPARE(list.creditCount(), std::size_t{2});

        auto* c1 = list.getCredit(testHash);
        QCOMPARE(c1->uploadedTotal(), uint64{5000});
        QCOMPARE(c1->downloadedTotal(), uint64{10000});

        auto* c2 = list.getCredit(testHash2);
        QCOMPARE(c2->uploadedTotal(), uint64{1000});
    }
}

void tst_ClientCredits::persistence_expiry()
{
    TempDir tmp;
    QString path = tmp.filePath(QStringLiteral("clients.met"));

    // Write a credit file with one expired and one current entry
    {
        SafeFile file;
        QVERIFY(file.open(path, QIODevice::WriteOnly));
        file.writeUInt8(CREDITFILE_VERSION);
        file.writeUInt32(2);  // 2 entries

        // Entry 1: current
        CreditStruct cs1{};
        std::memcpy(cs1.key.data(), testHash, 16);
        cs1.uploadedLo = 1000;
        cs1.lastSeen = static_cast<uint32>(std::time(nullptr));
        file.write(&cs1, sizeof(CreditStruct));

        // Entry 2: expired (200 days ago)
        CreditStruct cs2{};
        std::memcpy(cs2.key.data(), testHash2, 16);
        cs2.uploadedLo = 2000;
        cs2.lastSeen = static_cast<uint32>(std::time(nullptr) - DAY2S(200));
        file.write(&cs2, sizeof(CreditStruct));
    }

    ClientCreditsList list;
    QVERIFY(list.loadList(path));
    QCOMPARE(list.creditCount(), std::size_t{1});  // only the current one
}

void tst_ClientCredits::persistence_versionCompat()
{
    TempDir tmp;
    QString path = tmp.filePath(QStringLiteral("clients.met"));

    // Write a version 0x11 (CreditStruct_29a) file
    {
        SafeFile file;
        QVERIFY(file.open(path, QIODevice::WriteOnly));
        file.writeUInt8(CREDITFILE_VERSION_29);
        file.writeUInt32(1);

        CreditStruct_29a cs{};
        std::memcpy(cs.key.data(), testHash, 16);
        cs.uploadedLo = 3000;
        cs.downloadedLo = 6000;
        cs.lastSeen = static_cast<uint32>(std::time(nullptr));
        file.write(&cs, sizeof(CreditStruct_29a));
    }

    ClientCreditsList list;
    QVERIFY(list.loadList(path));
    QCOMPARE(list.creditCount(), std::size_t{1});

    auto* c = list.getCredit(testHash);
    QCOMPARE(c->uploadedTotal(), uint64{3000});
    QCOMPARE(c->downloadedTotal(), uint64{6000});
}

void tst_ClientCredits::getCredit_createNew()
{
    ClientCreditsList list;
    auto* c = list.getCredit(testHash);
    QVERIFY(c != nullptr);
    QVERIFY(md4equ(c->key(), testHash));
    QCOMPARE(c->uploadedTotal(), uint64{0});
    QCOMPARE(c->downloadedTotal(), uint64{0});
}

void tst_ClientCredits::getCredit_existing()
{
    ClientCreditsList list;
    auto* c1 = list.getCredit(testHash);
    c1->addUploaded(1000, 0x01020304);

    auto* c2 = list.getCredit(testHash);
    QCOMPARE(c1, c2);  // same pointer
    QCOMPARE(c2->uploadedTotal(), uint64{1000});
}

void tst_ClientCredits::creditCount()
{
    ClientCreditsList list;
    QCOMPARE(list.creditCount(), std::size_t{0});

    list.getCredit(testHash);
    QCOMPARE(list.creditCount(), std::size_t{1});

    list.getCredit(testHash2);
    QCOMPARE(list.creditCount(), std::size_t{2});

    // Getting same hash again shouldn't increase count
    list.getCredit(testHash);
    QCOMPARE(list.creditCount(), std::size_t{2});
}

// ---------------------------------------------------------------------------
// RSA crypto tests
// ---------------------------------------------------------------------------

void tst_ClientCredits::cryptoAvailable_afterInit()
{
    TempDir tmp;
    thePrefs.setConfigDir(tmp.path());

    ClientCreditsList list;
    QVERIFY(list.cryptoAvailable());
    QVERIFY(list.pubKeyLen() > 0);
    QVERIFY(list.pubKeyLen() <= kMaxPubKeySize);
}

void tst_ClientCredits::crypto_keyPairGeneration()
{
    TempDir tmp;
    thePrefs.setConfigDir(tmp.path());

    ClientCreditsList list;
    QVERIFY(list.cryptoAvailable());

    // Verify the key file was created
    QFileInfo fi(QDir(tmp.path()).filePath(QStringLiteral("cryptkey.dat")));
    QVERIFY(fi.exists());
    QVERIFY(fi.size() > 0);
}

void tst_ClientCredits::crypto_signVerifyRoundTrip()
{
    // Mirrors original eMule Debug_CheckCrypting():
    // Create a credit list with crypto, set up a simulated peer,
    // sign a challenge, and verify it.
    TempDir tmp;
    thePrefs.setConfigDir(tmp.path());

    ClientCreditsList list;
    QVERIFY(list.cryptoAvailable());

    // Create a "peer" credit entry
    auto* peer = list.getCredit(testHash);

    // Set the peer's public key to our own public key (self-test, like Debug_CheckCrypting)
    peer->setSecureIdent(list.publicKey(), list.pubKeyLen());
    QCOMPARE(peer->secIDKeyLen(), list.pubKeyLen());

    // Set up challenges
    peer->cryptRndChallengeFrom = 0xDEADBEEF;
    peer->cryptRndChallengeFor  = 0xDEADBEEF;

    // Sign (as if sending to peer)
    uint8 sig[200];
    uint8 sigLen = list.createSignature(peer, sig, sizeof(sig), 0, kCryptCipNoneClient);
    QVERIFY(sigLen > 0);

    // Verify (as if peer received our signature)
    bool verified = list.verifyIdent(peer, sig, sigLen, 0x01020304, kCryptCipNoneClient);
    QVERIFY(verified);
    QCOMPARE(peer->currentIdentState(0x01020304), IdentState::Identified);
}

void tst_ClientCredits::crypto_signVerifyTampered()
{
    TempDir tmp;
    thePrefs.setConfigDir(tmp.path());

    ClientCreditsList list;
    QVERIFY(list.cryptoAvailable());

    auto* peer = list.getCredit(testHash2);
    peer->setSecureIdent(list.publicKey(), list.pubKeyLen());

    peer->cryptRndChallengeFrom = 0xCAFEBABE;
    peer->cryptRndChallengeFor  = 0xCAFEBABE;

    // Create a valid signature
    uint8 sig[200];
    uint8 sigLen = list.createSignature(peer, sig, sizeof(sig), 0, kCryptCipNoneClient);
    QVERIFY(sigLen > 0);

    // Tamper with the signature
    sig[0] ^= 0xFF;

    // Verification should fail
    bool verified = list.verifyIdent(peer, sig, sigLen, 0x01020304, kCryptCipNoneClient);
    QVERIFY(!verified);
    QCOMPARE(peer->currentIdentState(0x01020304), IdentState::IdFailed);
}

QTEST_MAIN(tst_ClientCredits)
#include "tst_ClientCredits.moc"
