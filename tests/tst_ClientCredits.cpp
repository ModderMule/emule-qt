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
    void crypto_keySize384();
    void crypto_keyPersistence();
    void crypto_signVerifyRoundTrip();
    void crypto_signVerifyTampered();
    void crypto_signVerifyWithLocalIP();
    void crypto_signVerifyIPMismatch();
    void crypto_signVerifyMixedIPKinds();
    void crypto_twoPartyCrossVerify();
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

void tst_ClientCredits::crypto_keySize384()
{
    // Verify the generated key is 384-bit and the X.509 public key fits in kMaxPubKeySize
    TempDir tmp;
    thePrefs.setConfigDir(tmp.path());

    ClientCreditsList list;
    QVERIFY(list.cryptoAvailable());

    // Public key must be X.509 SubjectPublicKeyInfo for 384-bit RSA (~78 bytes)
    QVERIFY2(list.pubKeyLen() >= 76 && list.pubKeyLen() <= 80,
             qPrintable(QStringLiteral("Expected 76-80 byte pub key, got %1").arg(list.pubKeyLen())));
    QVERIFY(list.pubKeyLen() <= kMaxPubKeySize);
}

void tst_ClientCredits::crypto_keyPersistence()
{
    // Generate a key, reload from file, verify same public key
    TempDir tmp;
    thePrefs.setConfigDir(tmp.path());

    QByteArray originalPubKey;
    uint8 originalPubKeyLen;

    {
        ClientCreditsList list;
        QVERIFY(list.cryptoAvailable());
        originalPubKeyLen = list.pubKeyLen();
        originalPubKey = QByteArray(reinterpret_cast<const char*>(list.publicKey()), originalPubKeyLen);
    }

    // Reload from the same config dir
    {
        ClientCreditsList list2;
        QVERIFY(list2.cryptoAvailable());
        QCOMPARE(list2.pubKeyLen(), originalPubKeyLen);
        QByteArray reloadedPubKey(reinterpret_cast<const char*>(list2.publicKey()), list2.pubKeyLen());
        QCOMPARE(reloadedPubKey, originalPubKey);
    }
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
    uint8 sigLen = list.createSignature(peer, sig, sizeof(sig), 0, 0);
    QVERIFY(sigLen > 0);

    // Verify (as if peer received our signature) — chaIPKind=0 means v1 (no IP binding)
    bool verified = list.verifyIdent(peer, sig, sigLen, 0x01020304, 0);
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
    uint8 sigLen = list.createSignature(peer, sig, sizeof(sig), 0, 0);
    QVERIFY(sigLen > 0);

    // Tamper with the signature
    sig[0] ^= 0xFF;

    // Verification should fail — chaIPKind=0 means v1 (no IP binding)
    bool verified = list.verifyIdent(peer, sig, sigLen, 0x01020304, 0);
    QVERIFY(!verified);
    QCOMPARE(peer->currentIdentState(0x01020304), IdentState::IdFailed);
}

void tst_ClientCredits::crypto_signVerifyWithLocalIP()
{
    // V2 signature round-trip with kCryptCipLocalClient and a pseudo IP.
    // Exercises the IP-bound code path in createSignature / verifyIdent
    // that is used in production but not covered by the v1 (NoneClient) tests.
    TempDir tmp;
    thePrefs.setConfigDir(tmp.path());

    ClientCreditsList list;
    QVERIFY(list.cryptoAvailable());

    auto* peer = list.getCredit(testHash);
    peer->setSecureIdent(list.publicKey(), list.pubKeyLen());

    peer->cryptRndChallengeFrom = 0x12345678;
    peer->cryptRndChallengeFor  = 0x12345678;

    const uint32 pseudoIP = 0xC0A80101; // 192.168.1.1

    uint8 sig[200];
    uint8 sigLen = list.createSignature(peer, sig, sizeof(sig), pseudoIP, kCryptCipLocalClient);
    QVERIFY2(sigLen > 0, "createSignature with LocalClient IP failed");

    bool verified = list.verifyIdent(peer, sig, sigLen, pseudoIP, kCryptCipLocalClient);
    QVERIFY2(verified, "verifyIdent with matching pseudo IP should succeed");
    QCOMPARE(peer->currentIdentState(pseudoIP), IdentState::Identified);
}

void tst_ClientCredits::crypto_signVerifyIPMismatch()
{
    // Sign with IP_A, verify with IP_B — must fail.
    // Proves IP binding prevents cross-IP signature replay.
    TempDir tmp;
    thePrefs.setConfigDir(tmp.path());

    ClientCreditsList list;
    QVERIFY(list.cryptoAvailable());

    auto* peer = list.getCredit(testHash2);
    peer->setSecureIdent(list.publicKey(), list.pubKeyLen());

    peer->cryptRndChallengeFrom = 0xAABBCCDD;
    peer->cryptRndChallengeFor  = 0xAABBCCDD;

    const uint32 ipA = 0xC0A80101; // 192.168.1.1
    const uint32 ipB = 0x0A000001; // 10.0.0.1

    uint8 sig[200];
    uint8 sigLen = list.createSignature(peer, sig, sizeof(sig), ipA, kCryptCipLocalClient);
    QVERIFY(sigLen > 0);

    // Verify with different IP — signature covers ipA, but verifier builds message with ipB
    bool verified = list.verifyIdent(peer, sig, sigLen, ipB, kCryptCipLocalClient);
    QVERIFY2(!verified, "verifyIdent with mismatched IP must fail");
    QCOMPARE(peer->currentIdentState(ipB), IdentState::IdFailed);
}

void tst_ClientCredits::crypto_signVerifyMixedIPKinds()
{
    // Sign with chaIPKind=0 (v1, no IP), verify with kCryptCipLocalClient (v2, with IP).
    // Message formats differ → verification must fail.
    TempDir tmp;
    thePrefs.setConfigDir(tmp.path());

    ClientCreditsList list;
    QVERIFY(list.cryptoAvailable());

    auto* peer = list.getCredit(testHash);
    peer->setSecureIdent(list.publicKey(), list.pubKeyLen());

    peer->cryptRndChallengeFrom = 0xFEEDFACE;
    peer->cryptRndChallengeFor  = 0xFEEDFACE;

    const uint32 pseudoIP = 0xAC100164; // 172.16.1.100

    // Sign without IP (v1)
    uint8 sig[200];
    uint8 sigLen = list.createSignature(peer, sig, sizeof(sig), 0, 0);
    QVERIFY(sigLen > 0);

    // Verify expecting IP (v2) — message buffer mismatch
    bool verified = list.verifyIdent(peer, sig, sigLen, pseudoIP, kCryptCipLocalClient);
    QVERIFY2(!verified, "v1 signature must not verify as v2 (different message format)");
}

void tst_ClientCredits::crypto_twoPartyCrossVerify()
{
    // Simulate two separate eMule clients exchanging SUI signatures.
    // Each has its own ClientCreditsList (key pair) and credits for the other.
    TempDir tmpA, tmpB;
    thePrefs.setConfigDir(tmpA.path());
    ClientCreditsList listA;
    QVERIFY(listA.cryptoAvailable());

    thePrefs.setConfigDir(tmpB.path());
    ClientCreditsList listB;
    QVERIFY(listB.cryptoAvailable());

    // Each creates a credit entry for the other, storing the other's public key
    auto* creditA = listA.getCredit(testHash);   // A's view of B
    auto* creditB = listB.getCredit(testHash2);   // B's view of A

    // Exchange public keys (simulates OP_PUBLICKEY)
    QVERIFY(creditA->setSecureIdent(listB.publicKey(), listB.pubKeyLen()));
    QVERIFY(creditB->setSecureIdent(listA.publicKey(), listA.pubKeyLen()));

    // Set matching challenges (simulates OP_SECIDENTSTATE exchange)
    const uint32 challengeAtoB = 0xAAAA1111; // A sends this, B signs it
    const uint32 challengeBtoA = 0xBBBB2222; // B sends this, A signs it

    // A's challenge for B
    creditA->cryptRndChallengeFor = challengeAtoB;
    // B receives A's challenge
    creditB->cryptRndChallengeFrom = challengeAtoB;

    // B's challenge for A
    creditB->cryptRndChallengeFor = challengeBtoA;
    // A receives B's challenge
    creditA->cryptRndChallengeFrom = challengeBtoA;

    // B signs for A (using challenge A gave B, and B's private key)
    uint8 sigBtoA[200];
    uint8 sigBtoALen = listB.createSignature(creditB, sigBtoA, sizeof(sigBtoA), 0, 0);
    QVERIFY2(sigBtoALen > 0, "B's signature creation failed");

    // A verifies B's signature (using challenge A gave B, and B's public key)
    bool verifiedB = listA.verifyIdent(creditA, sigBtoA, sigBtoALen, 0x01020304, 0);
    QVERIFY2(verifiedB, "A should verify B's signature successfully");
    QCOMPARE(creditA->currentIdentState(0x01020304), IdentState::Identified);

    // A signs for B (using challenge B gave A, and A's private key)
    uint8 sigAtoB[200];
    uint8 sigAtoBLen = listA.createSignature(creditA, sigAtoB, sizeof(sigAtoB), 0, 0);
    QVERIFY2(sigAtoBLen > 0, "A's signature creation failed");

    // B verifies A's signature
    bool verifiedA = listB.verifyIdent(creditB, sigAtoB, sigAtoBLen, 0x01020304, 0);
    QVERIFY2(verifiedA, "B should verify A's signature successfully");
    QCOMPARE(creditB->currentIdentState(0x01020304), IdentState::Identified);
}

QTEST_MAIN(tst_ClientCredits)
#include "tst_ClientCredits.moc"
