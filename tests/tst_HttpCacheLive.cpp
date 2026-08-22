/// @file tst_HttpCacheLive.cpp
/// @brief End-to-end test of HttpCachePublisher against a real cache server.
///
/// Proves the half of the feature that no unit test can: that the C++ publisher
/// and a real HTTP backend agree on the contract. It reads a part off disk,
/// encrypts it, POSTs it, then pulls the blob back down and decrypts it with the
/// key from the offer the publisher produced. If any of the encryption, the
/// framing, the JSON, or the server's Range handling is wrong, this fails.
///
/// Point it at any implementation of the contract — the PHP reference server or
/// a different backend:
///
///     EMULE_HTTPCACHE_URL=http://localhost/emule-http-cache-php \
///     EMULE_HTTPCACHE_KEY=<secret from config.php> \
///     ./tests/tst_HttpCacheLive
///
/// Skipped when those are unset, so it is inert on a machine with no server.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "crypto/AesCbc.h"
#include "files/PartFile.h"
#include "httpcache/HttpCacheClient.h"
#include "httpcache/HttpCachePublisher.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace eMule;

namespace {

QString serverUrl()
{
    return qEnvironmentVariable("EMULE_HTTPCACHE_URL");
}

QString serverKey()
{
    return qEnvironmentVariable("EMULE_HTTPCACHE_KEY");
}

/// Blocking GET, optionally ranged. Returns an empty array on any failure.
QByteArray httpGet(const QString& url, const QByteArray& range, int* statusOut = nullptr)
{
    QNetworkAccessManager nam;
    QNetworkRequest request{QUrl(url)};
    if (!range.isEmpty())
        request.setRawHeader("Range", range);

    QNetworkReply* reply = nam.get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(60'000, &loop, &QEventLoop::quit);
    loop.exec();

    if (statusOut)
        *statusOut = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    const QByteArray body = reply->readAll();
    reply->deleteLater();

    return body;
}

/// Deterministic filler, so a mismatch is reproducible.
QByteArray partPattern(qsizetype size)
{
    QByteArray out(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
        out[i] = static_cast<char>((i * 131 + (i >> 11) * 17) & 0xFF);
    return out;
}

} // namespace

class tst_HttpCacheLive : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void publishFetchAndDecrypt();
    void serverSupportsRangeResume();
    void clientFetchesWholePart();

private:
    QTemporaryDir m_dir;
    QString m_partPath;
    QByteArray m_plain;

    // Carried from the first test to the second so the 9.28 MB upload happens once.
    HttpCacheOffer m_offer;
};

void tst_HttpCacheLive::initTestCase()
{
    if (serverUrl().isEmpty() || serverKey().isEmpty())
        QSKIP("set EMULE_HTTPCACHE_URL and EMULE_HTTPCACHE_KEY to run this test");

    QVERIFY(m_dir.isValid());

    thePrefs.load(m_dir.filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_dir.path());
    thePrefs.setHttpCacheEnabled(true);
    thePrefs.setHttpCacheAllowDownload(true);

    // One full part, exactly as the manager would hand to the publisher.
    m_plain = partPattern(static_cast<qsizetype>(PARTSIZE));
    m_partPath = m_dir.filePath(QStringLiteral("fake.part"));

    QFile file(m_partPath);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(m_plain), static_cast<qint64>(m_plain.size()));
    file.close();
}

void tst_HttpCacheLive::publishFetchAndDecrypt()
{
    HttpCachePublisher::Request request;
    request.baseUrl = serverUrl();
    request.apiKey = serverKey();
    request.ttlSeconds = 600;
    request.rateBytesPerSecond = 0;   // unthrottled; the throttle is covered elsewhere
    request.fileHash = {0xAA, 0xBB, 0xCC, 0xDD, 0x01, 0x02, 0x03, 0x04,
                        0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
    request.partIndex = 3;
    request.dataFilePath = m_partPath;
    request.partOffset = 0;
    request.partLength = PARTSIZE;

    auto* publisher = new HttpCachePublisher(this);
    QSignalSpy spy(publisher, &HttpCachePublisher::finished);

    publisher->start(request);
    QVERIFY2(spy.wait(120'000), "publisher never finished");
    QCOMPARE(spy.count(), 1);

    const auto result = spy.at(0).at(0).value<HttpCachePublishResult>();
    const auto offer = spy.at(0).at(1).value<HttpCacheOffer>();

    QVERIFY2(result.ok, qPrintable(result.error));
    QVERIFY(!result.url.isEmpty());
    QVERIFY2(offer.isWellFormed(), qPrintable(offer.malformedReason()));

    // The offer must describe the part we actually published.
    QCOMPARE(offer.partIndex, request.partIndex);
    QCOMPARE(offer.plainLength, PARTSIZE);
    QCOMPARE(offer.cipherLength, AesCbcEncryptor::cipherLengthFor(PARTSIZE));
    QCOMPARE(offer.key.size(), qsizetype{kAesKeySize});
    QCOMPARE(offer.iv.size(), qsizetype{kAesIvSize});

    // -- Now be the downloader ------------------------------------------------

    int status = 0;
    const QByteArray cipher = httpGet(offer.url, {}, &status);
    QCOMPARE(status, 200);
    QCOMPARE(static_cast<uint64>(cipher.size()), offer.cipherLength);

    // The digest in the offer is what lets a downloader tell "the blob was
    // mangled" from "the uploader published rubbish".
    QCOMPARE(QCryptographicHash::hash(cipher, QCryptographicHash::Sha256), offer.cipherSha256);

    bool ok = false;
    const QByteArray plain = aesDecrypt(cipher, offer.key, offer.iv, &ok);
    QVERIFY2(ok, "ciphertext did not decrypt with the key from the offer");
    QCOMPARE(plain.size(), m_plain.size());
    QCOMPARE(plain, m_plain);

    // The server must never have been in a position to do that itself.
    QVERIFY(!cipher.contains(m_plain.left(4096)));

    m_offer = offer;
}

void tst_HttpCacheLive::serverSupportsRangeResume()
{
    if (m_offer.url.isEmpty())
        QSKIP("publish step did not run");

    // A downloader that drops mid-chunk resumes from the previous block boundary
    // and seeds the CBC chaining value from it. Without a working 206 that whole
    // path is dead, so assert the server does it and that the maths works.
    const uint64 resumeFrom = 4'096'000;   // block-aligned
    QCOMPARE(resumeFrom % kAesBlockSize, UINT64_C(0));

    int status = 0;
    const QByteArray tail =
        httpGet(m_offer.url,
                "bytes=" + QByteArray::number(static_cast<qlonglong>(resumeFrom - kAesBlockSize))
                    + "-",
                &status);

    QCOMPARE(status, 206);
    QCOMPARE(static_cast<uint64>(tail.size()), m_offer.cipherLength - resumeFrom + kAesBlockSize);

    AesCbcDecryptor dec;
    QVERIFY(dec.beginAt(m_offer.key, tail.left(kAesBlockSize), true));

    QByteArray plainTail = dec.update(tail.mid(kAesBlockSize));
    bool ok = false;
    plainTail.append(dec.finish(&ok));

    QVERIFY2(ok, "resumed decryption failed its padding check");
    QCOMPARE(plainTail, m_plain.mid(static_cast<qsizetype>(resumeFrom)));
}

void tst_HttpCacheLive::clientFetchesWholePart()
{
    if (m_offer.url.isEmpty())
        QSKIP("publish step did not run");

    // The two cases above act as the downloader by hand. This one runs the real
    // HttpCacheClient — its socket, its staged block intake, its decryptor and
    // its writes into a real PartFile — against a real origin, which is the only
    // way to prove the whole downloader path end to end.
    const QString tempDir = m_dir.filePath(QStringLiteral("temp"));
    QDir().mkpath(tempDir);

    // Retarget the offer at part 0 so the part file need only be one part long;
    // the blob on the server is the same bytes either way.
    HttpCacheOffer offer = m_offer;
    offer.partIndex = 0;
    QVERIFY2(offer.isWellFormed(), qPrintable(offer.malformedReason()));

    // A shade longer than the part, so the gap list never empties and the fetch
    // does not trip PartFile's completion path mid-test.
    PartFile file;
    file.setFileSize(PARTSIZE + 100);
    file.setFileHash(offer.fileHash.data());
    file.setTmpPath(tempDir);
    QVERIFY(file.createPartFile(tempDir));

    auto* client = new HttpCacheClient();
    QSignalSpy spy(client, &HttpCacheClient::fetchFinished);

    std::array<uint8, 16> peerHash{};
    peerHash.fill(0x7E);

    // No live peer behind this fetch; the address only matters if the part later
    // fails MD4, which a real round trip through the real server does not.
    QVERIFY(client->beginFetch(offer, &file, peerHash, Address::fromHostOrder(0x7E7E7E01)));
    QVERIFY2(spy.wait(180'000), "fetch never finished");

    QCOMPARE(spy.first().at(1).value<HttpCacheResult>(), HttpCacheResult::Ok);
    QCOMPARE(client->plainWritten(), PARTSIZE);
    QCOMPARE(client->cipherConsumed(), offer.cipherLength);

    file.flushBuffer();

    QFile part(file.dataFilePath());
    QVERIFY(part.open(QIODevice::ReadOnly));
    QCOMPARE(part.read(static_cast<qint64>(PARTSIZE)), m_plain);
}

QTEST_MAIN(tst_HttpCacheLive)
#include "tst_HttpCacheLive.moc"
