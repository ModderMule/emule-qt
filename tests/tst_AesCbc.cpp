/// @file tst_AesCbc.cpp
/// @brief Tests for crypto/AesCbc — AES-256-CBC, one-shot and streaming.

#include "TestHelpers.h"
#include "crypto/AesCbc.h"
#include "utils/Opcodes.h"

#include <QTest>

#include <algorithm>

using namespace eMule;

namespace {

/// Deterministic filler so a failure is reproducible.
QByteArray pattern(qsizetype size)
{
    QByteArray out(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
        out[i] = static_cast<char>((i * 31 + (i >> 8) * 7) & 0xFF);
    return out;
}

QByteArray fixedKey()
{
    return QByteArray(kAesKeySize, '\x42');
}

QByteArray fixedIv()
{
    return QByteArray(kAesIvSize, '\x17');
}

} // namespace

class tst_AesCbc : public QObject {
    Q_OBJECT

private slots:
    void randomBytes_sizeAndVariation();
    void begin_rejectsWrongSizes();

    void oneShot_roundTrip();
    void oneShot_emptyPlaintext();
    void cipherLength_addsAWholeBlockOnExactMultiples();
    void cipherLength_matchesAnEmulePart();

    void streaming_matchesOneShot();
    void streaming_oddSlices();

    void decrypt_wrongKey_reportsFailure();
    void decrypt_truncated_reportsFailure();
    void decrypt_flippedByte_reportsFailure();

    void resume_fromBlockBoundary();
    void resume_middleSliceWithoutPadding();
    void resume_mirrorsHttpCacheClientPipeline();

    void base64_roundTrip();
    void base64_wrongKeyReturnsEmpty();
    void base64_rejectsShortInput();
};

// ---------------------------------------------------------------------------
// Basics
// ---------------------------------------------------------------------------

void tst_AesCbc::randomBytes_sizeAndVariation()
{
    QCOMPARE(aesRandomKey().size(), qsizetype{kAesKeySize});
    QCOMPARE(aesRandomIv().size(), qsizetype{kAesIvSize});
    QVERIFY(aesRandomBytes(0).isEmpty());
    QVERIFY(aesRandomBytes(-1).isEmpty());

    // Two draws colliding would mean the RNG is not wired up at all.
    QVERIFY(aesRandomKey() != aesRandomKey());
}

void tst_AesCbc::begin_rejectsWrongSizes()
{
    AesCbcEncryptor enc;
    QVERIFY(!enc.begin(QByteArray(16, 'k'), fixedIv()));   // key too short
    QVERIFY(!enc.begin(fixedKey(), QByteArray(8, 'i')));   // iv too short
    QVERIFY(!enc.begin(QByteArray(), QByteArray()));
    QVERIFY(!enc.isValid());

    QVERIFY(enc.begin(fixedKey(), fixedIv()));
    QVERIFY(enc.isValid());

    AesCbcDecryptor dec;
    QVERIFY(!dec.begin(QByteArray(31, 'k'), fixedIv()));
    QVERIFY(dec.begin(fixedKey(), fixedIv()));
}

// ---------------------------------------------------------------------------
// One-shot
// ---------------------------------------------------------------------------

void tst_AesCbc::oneShot_roundTrip()
{
    const QByteArray plain = pattern(1000);
    const QByteArray cipher = aesEncrypt(plain, fixedKey(), fixedIv());

    QVERIFY(!cipher.isEmpty());
    QVERIFY(cipher != plain);
    QCOMPARE(static_cast<uint64>(cipher.size()),
             AesCbcEncryptor::cipherLengthFor(static_cast<uint64>(plain.size())));

    bool ok = false;
    QCOMPARE(aesDecrypt(cipher, fixedKey(), fixedIv(), &ok), plain);
    QVERIFY(ok);
}

void tst_AesCbc::oneShot_emptyPlaintext()
{
    // An empty message is still a full pad block, and must survive the round trip.
    const QByteArray cipher = aesEncrypt(QByteArray(), fixedKey(), fixedIv());
    QCOMPARE(cipher.size(), qsizetype{kAesBlockSize});

    bool ok = false;
    QVERIFY(aesDecrypt(cipher, fixedKey(), fixedIv(), &ok).isEmpty());
    QVERIFY(ok);
}

void tst_AesCbc::cipherLength_addsAWholeBlockOnExactMultiples()
{
    QCOMPARE(AesCbcEncryptor::cipherLengthFor(0), UINT64_C(16));
    QCOMPARE(AesCbcEncryptor::cipherLengthFor(1), UINT64_C(16));
    QCOMPARE(AesCbcEncryptor::cipherLengthFor(15), UINT64_C(16));
    QCOMPARE(AesCbcEncryptor::cipherLengthFor(16), UINT64_C(32));
    QCOMPARE(AesCbcEncryptor::cipherLengthFor(17), UINT64_C(32));

    // The prediction has to agree with what the cipher actually emits.
    for (const qsizetype len : {0, 1, 15, 16, 17, 31, 32, 33, 1000}) {
        const QByteArray cipher = aesEncrypt(pattern(len), fixedKey(), fixedIv());
        QCOMPARE(static_cast<uint64>(cipher.size()),
                 AesCbcEncryptor::cipherLengthFor(static_cast<uint64>(len)));
    }
}

void tst_AesCbc::cipherLength_matchesAnEmulePart()
{
    // The number the HTTP Cache offer carries and the server enforces. PARTSIZE is
    // an exact multiple of the block size, so PKCS#7 costs a whole extra block.
    QCOMPARE(PARTSIZE % kAesBlockSize, UINT64_C(0));
    QCOMPARE(AesCbcEncryptor::cipherLengthFor(PARTSIZE), UINT64_C(9728016));
}

// ---------------------------------------------------------------------------
// Streaming
// ---------------------------------------------------------------------------

void tst_AesCbc::streaming_matchesOneShot()
{
    const QByteArray plain = pattern(70000);
    const QByteArray expected = aesEncrypt(plain, fixedKey(), fixedIv());

    AesCbcEncryptor enc;
    QVERIFY(enc.begin(fixedKey(), fixedIv()));

    QByteArray streamed;
    for (qsizetype off = 0; off < plain.size(); off += 4096)
        streamed.append(enc.update(plain.mid(off, 4096)));
    streamed.append(enc.finish());

    QCOMPARE(streamed, expected);
    QVERIFY(!enc.isValid()); // finish() consumes the context

    AesCbcDecryptor dec;
    QVERIFY(dec.begin(fixedKey(), fixedIv()));

    QByteArray back;
    for (qsizetype off = 0; off < streamed.size(); off += 3333)
        back.append(dec.update(streamed.mid(off, 3333)));

    bool ok = false;
    back.append(dec.finish(&ok));

    QVERIFY(ok);
    QCOMPARE(back, plain);
}

void tst_AesCbc::streaming_oddSlices()
{
    // Slices that are not block multiples exercise EVP's internal carry-over.
    const QByteArray plain = pattern(5000);

    AesCbcEncryptor enc;
    QVERIFY(enc.begin(fixedKey(), fixedIv()));

    QByteArray cipher;
    const QList<qsizetype> slices{1, 2, 3, 15, 17, 33, 1, 4928};
    qsizetype off = 0;
    for (const qsizetype n : slices) {
        cipher.append(enc.update(plain.mid(off, n)));
        off += n;
    }
    QCOMPARE(off, plain.size());
    cipher.append(enc.finish());

    bool ok = false;
    QCOMPARE(aesDecrypt(cipher, fixedKey(), fixedIv(), &ok), plain);
    QVERIFY(ok);
}

// ---------------------------------------------------------------------------
// Failure modes
// ---------------------------------------------------------------------------

void tst_AesCbc::decrypt_wrongKey_reportsFailure()
{
    const QByteArray cipher = aesEncrypt(pattern(4096), fixedKey(), fixedIv());

    QByteArray wrong = fixedKey();
    wrong[0] = wrong[0] ^ 0x01;

    bool ok = true;
    const QByteArray out = aesDecrypt(cipher, wrong, fixedIv(), &ok);

    // A wrong key almost always breaks the padding check; the caller must be told,
    // not handed plausible-looking garbage.
    QVERIFY(!ok);
    QVERIFY(out.isEmpty());
}

void tst_AesCbc::decrypt_truncated_reportsFailure()
{
    const QByteArray cipher = aesEncrypt(pattern(4096), fixedKey(), fixedIv());

    bool ok = true;
    QVERIFY(aesDecrypt(cipher.left(cipher.size() - kAesBlockSize), fixedKey(), fixedIv(), &ok)
                .isEmpty());
    QVERIFY(!ok);
}

void tst_AesCbc::decrypt_flippedByte_reportsFailure()
{
    QByteArray cipher = aesEncrypt(pattern(4096), fixedKey(), fixedIv());

    // Flip a bit inside the final block so the padding no longer verifies.
    const qsizetype victim = cipher.size() - kAesBlockSize - 1;
    cipher[victim] = static_cast<char>(cipher[victim] ^ '\x80');

    bool ok = true;
    QVERIFY(aesDecrypt(cipher, fixedKey(), fixedIv(), &ok).isEmpty());
    QVERIFY(!ok);
}

// ---------------------------------------------------------------------------
// Resume — what makes a Range-resumed HTTP Cache fetch possible
// ---------------------------------------------------------------------------

void tst_AesCbc::resume_fromBlockBoundary()
{
    const QByteArray plain = pattern(64000);
    const QByteArray cipher = aesEncrypt(plain, fixedKey(), fixedIv());

    // Pretend the transfer died after `done` ciphertext bytes. The downloader
    // re-requests from done-16 and uses that block as the chaining value.
    const qsizetype done = 32000;
    QCOMPARE(done % kAesBlockSize, qsizetype{0});

    AesCbcDecryptor dec;
    QVERIFY(dec.beginAt(fixedKey(), cipher.mid(done - kAesBlockSize, kAesBlockSize), true));

    QByteArray tail = dec.update(cipher.mid(done));

    bool ok = false;
    tail.append(dec.finish(&ok));

    QVERIFY(ok);
    QCOMPARE(tail, plain.mid(done));
}

void tst_AesCbc::resume_middleSliceWithoutPadding()
{
    const QByteArray plain = pattern(64000);
    const QByteArray cipher = aesEncrypt(plain, fixedKey(), fixedIv());

    const qsizetype from = 16000;
    const qsizetype len = 8000;

    AesCbcDecryptor dec;
    // expectPadding=false: we stop before the last block, so there is no padding
    // to strip — asking for it would swallow the final block of real data.
    QVERIFY(dec.beginAt(fixedKey(), cipher.mid(from - kAesBlockSize, kAesBlockSize), false));

    QByteArray slice = dec.update(cipher.mid(from, len));
    slice.append(dec.finish());

    QCOMPARE(slice, plain.mid(from, len));
}

void tst_AesCbc::resume_mirrorsHttpCacheClientPipeline()
{
    // Exactly what HttpCacheClient does, in miniature: padding disabled for the
    // whole stream, input only ever advanced in whole blocks, the chaining value
    // carried over from the dead segment rather than refetched, and the PKCS#7
    // tail stripped by hand because the plaintext length is known up front.
    //
    // 999,997 is not a multiple of the block size, so the pad is 3 bytes — the
    // case that would pass unnoticed if the pad were always a full block.
    const qsizetype plainLength = 999'997;
    const QByteArray plain = pattern(plainLength);
    const QByteArray cipher = aesEncrypt(plain, fixedKey(), fixedIv());

    const qsizetype padLength = cipher.size() - plainLength;
    QCOMPARE(padLength, qsizetype{3});

    QByteArray out;
    QByteArray chain;
    qsizetype consumed = 0;

    // The connection dies here, mid-block on purpose: 300,003 is not a multiple
    // of 16, so three staged bytes are thrown away and the resume must start at
    // 300,000.
    const auto feed = [&](qsizetype from, qsizetype until, const QByteArray& iv) {
        AesCbcDecryptor dec;
        QVERIFY(dec.beginAt(fixedKey(), iv, false));

        QByteArray stage;
        qsizetype pos = from;
        qsizetype slice = 1;

        while (pos < until) {
            const qsizetype take = std::min(slice, until - pos);
            stage.append(cipher.mid(pos, take));
            pos += take;
            slice = slice * 3 + 7;   // deliberately ragged, like a socket

            const qsizetype whole = stage.size() - (stage.size() % kAesBlockSize);
            if (whole == 0)
                continue;

            const QByteArray blocks = stage.left(whole);
            stage.remove(0, whole);

            const QByteArray plainOut = dec.update(blocks);
            QCOMPARE(plainOut.size(), whole);   // padding off: in == out

            chain = blocks.right(kAesBlockSize);
            consumed += whole;
            out.append(plainOut);
        }
    };

    feed(0, 300'003, fixedIv());
    QCOMPARE(consumed, qsizetype{300'000});

    feed(consumed, cipher.size(), chain);
    QCOMPARE(consumed, cipher.size());

    // The file bytes and the pad separate cleanly at plainLength.
    QCOMPARE(out.left(plainLength), plain);
    QCOMPARE(out.mid(plainLength), QByteArray(padLength, static_cast<char>(padLength)));
}

// ---------------------------------------------------------------------------
// Base64 one-shot (the SMTP password path)
// ---------------------------------------------------------------------------

void tst_AesCbc::base64_roundTrip()
{
    const QByteArray key = aesRandomKey();
    const QString secret = QStringLiteral("hunter2 — with a non-ASCII ümlaut");

    const QString blob = aesEncryptToBase64(secret, key);
    QVERIFY(!blob.isEmpty());
    QVERIFY(!blob.contains(secret));

    QCOMPARE(aesDecryptFromBase64(blob, key), secret);

    // A fresh IV each time means the same secret never encrypts to the same blob.
    QVERIFY(aesEncryptToBase64(secret, key) != blob);
}

void tst_AesCbc::base64_wrongKeyReturnsEmpty()
{
    const QByteArray key = aesRandomKey();
    const QString blob = aesEncryptToBase64(QStringLiteral("secret"), key);

    QVERIFY(aesDecryptFromBase64(blob, aesRandomKey()).isEmpty());
    QVERIFY(aesDecryptFromBase64(blob, QByteArray(16, 'x')).isEmpty());
}

void tst_AesCbc::base64_rejectsShortInput()
{
    const QByteArray key = aesRandomKey();

    QVERIFY(aesEncryptToBase64(QString(), key).isEmpty());
    QVERIFY(aesDecryptFromBase64(QString(), key).isEmpty());
    QVERIFY(aesDecryptFromBase64(QStringLiteral("AAAA"), key).isEmpty());
}

QTEST_MAIN(tst_AesCbc)
#include "tst_AesCbc.moc"
