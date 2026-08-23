/// @file tst_HttpCacheResume.cpp
/// @brief HttpCacheClient against a deliberately unreliable HTTP origin.
///
/// The interesting part of HTTP Cache resume is not the Range header, it is the
/// bookkeeping around it: the SHA-256 spans the whole ciphertext and cannot be
/// rewound, so every byte must reach the digest exactly once no matter how many
/// connections it took. These tests drive a real HttpCacheClient — real socket,
/// real decryptor, real PartFile — against a fake cache server that drops
/// connections, ignores Range, or lies in Content-Range, and check the part file
/// byte for byte.

#include "TestHelpers.h"
#include "app/AppConfig.h"
#include "app/AppContext.h"
#include "crypto/AesCbc.h"
#include "files/PartFile.h"
#include "httpcache/HttpCacheClient.h"
#include "prefs/Preferences.h"
#include "utils/Opcodes.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <memory>

using namespace eMule;

namespace {

/// Small enough to keep the test quick, large enough to be split across many
/// reads by the kernel. 999,997 is deliberately not a multiple of the AES block
/// size, so the PKCS#7 pad is 3 bytes rather than a full block and the
/// plaintext/ciphertext split is actually exercised.
constexpr qsizetype kPlainLength = 999'997;
constexpr qsizetype kCipherLength = 1'000'000;   // == cipherLengthFor(kPlainLength)

/// The file is longer than the part we fetch, so its gap list never empties and
/// PartFile::writeToBuffer() does not trip the completion path mid-test.
constexpr uint64 kFileSize = kPlainLength + 100;

QByteArray pattern(qsizetype size)
{
    QByteArray out(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
        out[i] = static_cast<char>((i * 131 + (i >> 11) * 17) & 0xFF);
    return out;
}

/// What one connection to the fake server should do.
struct Behaviour {
    /// Bytes of body to write before closing; -1 sends the whole thing.
    qint64 dropAfter = -1;
    /// Answer 200 with the full object, ignoring whatever Range was asked for.
    bool ignoreRange = false;
    /// Answer 206 with a Content-Range starting somewhere nobody asked for.
    bool lieAboutRange = false;
    /// Close right after the headers, before any body.
    bool headersOnly = false;
    /// Write the body this many bytes at a time, one slice per event-loop turn,
    /// instead of in a single write. 0 sends it all at once.
    qint64 sliceBytes = 0;
};

/// A minimal, deliberately unreliable implementation of GET /v1/chunks/{id}.
class FakeCacheServer : public QTcpServer {
    Q_OBJECT

public:
    explicit FakeCacheServer(QByteArray body, QObject* parent = nullptr)
        : QTcpServer(parent), m_body(std::move(body))
    {
    }

    /// Behaviour for connection n; the last entry repeats for every connection
    /// after it.
    void setScript(QList<Behaviour> script) { m_script = std::move(script); }

    [[nodiscard]] int connectionCount() const { return m_connections; }
    [[nodiscard]] const QList<qint64>& requestedStarts() const { return m_requestedStarts; }

    /// Every request line + headers this server was sent, verbatim.
    [[nodiscard]] const QList<QByteArray>& requests() const { return m_requests; }

    [[nodiscard]] QString url() const
    {
        return QStringLiteral("http://127.0.0.1:%1/v1/chunks/deadbeef").arg(serverPort());
    }

protected:
    void incomingConnection(qintptr handle) override
    {
        auto* socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);

        const int index = m_connections++;

        connect(socket, &QTcpSocket::readyRead, this, [this, socket, index] {
            QByteArray& buffered = m_pending[socket];
            buffered.append(socket->readAll());

            // Loop rather than take-the-lot: a client that pipelines a second GET
            // onto the same socket must show up as two requests here, not one.
            // That is the whole subject of wholePartFetchIssuesOneRequest().
            for (auto end = buffered.indexOf("\r\n\r\n"); end >= 0;
                 end = buffered.indexOf("\r\n\r\n")) {
                const QByteArray request = buffered.left(end + 4);
                buffered.remove(0, end + 4);
                respond(socket, request, behaviourFor(index));
            }
        });

        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    [[nodiscard]] Behaviour behaviourFor(int index) const
    {
        if (m_script.isEmpty())
            return {};
        return m_script.at(std::min<int>(index, static_cast<int>(m_script.size()) - 1));
    }

    void respond(QTcpSocket* socket, const QByteArray& request, const Behaviour& behaviour)
    {
        m_requests.append(request);

        qint64 first = 0;
        qint64 last = m_body.size() - 1;
        parseRange(request, first, last);
        m_requestedStarts.append(first);

        QByteArray body;
        QByteArray head;

        if (behaviour.ignoreRange) {
            body = m_body;
            head = "HTTP/1.1 200 OK\r\n";
        } else {
            body = m_body.mid(first, last - first + 1);
            head = "HTTP/1.1 206 Partial Content\r\n";
            const qint64 reportedFirst = behaviour.lieAboutRange ? first + 16 : first;
            head += "Content-Range: bytes " + QByteArray::number(reportedFirst) + '-'
                  + QByteArray::number(last) + '/' + QByteArray::number(m_body.size()) + "\r\n";
        }

        head += "Accept-Ranges: bytes\r\n";
        head += "Content-Type: application/octet-stream\r\n";
        head += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
        head += "\r\n";

        socket->write(head);

        if (behaviour.headersOnly) {
            socket->flush();
            socket->close();
            return;
        }

        if (behaviour.dropAfter >= 0)
            body = body.left(behaviour.dropAfter);

        if (behaviour.sliceBytes > 0 && behaviour.dropAfter < 0) {
            writeInSlices(socket, body, behaviour.sliceBytes);
            return;
        }

        socket->write(body);
        socket->flush();

        // A truncated body must look like a dropped connection, not a tidy end.
        if (behaviour.dropAfter >= 0)
            socket->close();
    }

    /// Hand the body over a slice per event-loop turn. A body delivered in one
    /// write is consumed by the client in a couple of reads, which is exactly the
    /// case where nothing goes wrong — the interesting one is a response long
    /// enough for the client's own block-completion callbacks to fire while it is
    /// still arriving.
    static void writeInSlices(QTcpSocket* socket, const QByteArray& body, qint64 slice)
    {
        auto remaining = std::make_shared<QByteArray>(body);
        auto* timer = new QTimer(socket);
        timer->setInterval(0);
        QObject::connect(timer, &QTimer::timeout, socket, [socket, remaining, slice, timer] {
            if (remaining->isEmpty()) {
                timer->stop();
                timer->deleteLater();
                return;
            }
            socket->write(remaining->left(static_cast<qsizetype>(slice)));
            socket->flush();
            remaining->remove(0, static_cast<qsizetype>(slice));
        });
        timer->start();
    }

    static void parseRange(const QByteArray& request, qint64& first, qint64& last)
    {
        const auto idx = request.toLower().indexOf("range: bytes=");
        if (idx < 0)
            return;

        const QByteArray spec =
            request.mid(idx + 13, request.indexOf('\r', idx) - (idx + 13)).trimmed();
        const auto dash = spec.indexOf('-');
        if (dash < 0)
            return;

        first = spec.left(dash).toLongLong();
        if (dash + 1 < spec.size())
            last = spec.mid(dash + 1).toLongLong();
    }

    QByteArray m_body;
    QList<Behaviour> m_script;
    QHash<QTcpSocket*, QByteArray> m_pending;
    QList<qint64> m_requestedStarts;
    QList<QByteArray> m_requests;
    int m_connections = 0;
};

} // namespace

class tst_HttpCacheResume : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void resumesAfterMidTransferDrop();
    void resumeStartsOnBlockBoundary();
    void corruptTailDetectedAcrossResume();
    void serverIgnoringRangeRestartsFromZero();
    void badContentRangeRejected();
    void givesUpAfterMaxAttempts();
    void wholePartFetchIssuesOneRequest();
    void fetchIdentifiesItself();
    void downloadedBytesAreAccountedOnce();

private:
    /// Build an offer describing @p cipher, served from @p server.
    [[nodiscard]] HttpCacheOffer makeOffer(const FakeCacheServer& server,
                                           const QByteArray& cipher) const;

    /// Run one fetch to completion. Returns the reported result.
    [[nodiscard]] HttpCacheResult runFetch(FakeCacheServer& server, PartFile& file,
                                           HttpCacheClient*& clientOut,
                                           const QByteArray& cipher);

    /// Plaintext of the part as it actually landed on disk.
    [[nodiscard]] QByteArray readBackPart(PartFile& file) const;

    QTemporaryDir m_dir;
    QString m_tempDir;
    QByteArray m_plain;
    QByteArray m_key;
    QByteArray m_iv;
    QByteArray m_cipher;
    std::array<uint8, 16> m_fileHash{};
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

void tst_HttpCacheResume::initTestCase()
{
    QVERIFY(m_dir.isValid());

    thePrefs.load(m_dir.filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_dir.path());
    thePrefs.setHttpCacheEnabled(true);
    thePrefs.setHttpCacheAllowDownload(true);

    // The fake server is on loopback, and a LAN address is refused unless the user has
    // declared this a private network — by urlIsAcceptable() on the way in, and now by
    // URLClient on the way out too. This fixture drives HttpCacheClient directly and so
    // used to reach the connect path unscreened; the two sibling fixtures already set
    // this for the same reason.
    thePrefs.setFilterLANIPs(false);

    m_tempDir = m_dir.filePath(QStringLiteral("temp"));
    QDir().mkpath(m_tempDir);

    m_plain = pattern(kPlainLength);
    m_key = aesRandomKey();
    m_iv = aesRandomIv();
    QCOMPARE(m_key.size(), qsizetype{kAesKeySize});

    m_cipher = aesEncrypt(m_plain, m_key, m_iv);
    QCOMPARE(m_cipher.size(), kCipherLength);

    for (std::size_t i = 0; i < m_fileHash.size(); ++i)
        m_fileHash[i] = static_cast<uint8>(i + 1);
}

void tst_HttpCacheResume::init()
{
    // Every case builds its own server and part file; nothing carries over.
}

HttpCacheOffer tst_HttpCacheResume::makeOffer(const FakeCacheServer& server,
                                              const QByteArray& cipher) const
{
    HttpCacheOffer offer;
    offer.fileHash = m_fileHash;
    offer.partIndex = 0;
    offer.plainLength = static_cast<uint64>(kPlainLength);
    offer.cipherLength = static_cast<uint64>(kCipherLength);
    offer.url = server.url();
    offer.key = m_key;
    offer.iv = m_iv;
    offer.cipherSha256 = QCryptographicHash::hash(cipher, QCryptographicHash::Sha256);
    offer.expiresAt = 0;

    return offer;
}

HttpCacheResult tst_HttpCacheResume::runFetch(FakeCacheServer& server, PartFile& file,
                                              HttpCacheClient*& clientOut,
                                              const QByteArray& cipher)
{
    auto* client = new HttpCacheClient();
    clientOut = client;

    // The production backoff is 2 s / 6 s / 15 s; waiting that out would make
    // this suite take half a minute for no extra coverage.
    client->setResumeDelayOverrideMsForTest(50);

    QSignalSpy spy(client, &HttpCacheClient::fetchFinished);

    std::array<uint8, 16> peerHash{};
    peerHash.fill(0x42);

    // Any address will do here: this test is about the transfer, and nothing in it
    // reaches the MD4 check that would consult the attribution.
    const Address peerAddress = Address::fromHostOrder(0x0A0B0C0D);

    if (!client->beginFetch(makeOffer(server, cipher), &file, peerHash, peerAddress))
        return HttpCacheResult::BadOffer;

    if (!spy.wait(30'000))
        return HttpCacheResult::HttpFailed;

    return spy.first().at(1).value<HttpCacheResult>();
}

QByteArray tst_HttpCacheResume::readBackPart(PartFile& file) const
{
    file.flushBuffer();

    // fullName() is the .part.met; the data lives in dataFilePath().
    QFile part(file.dataFilePath());
    if (!part.open(QIODevice::ReadOnly))
        return {};

    return part.read(kPlainLength);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void tst_HttpCacheResume::resumesAfterMidTransferDrop()
{
    FakeCacheServer server(m_cipher);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setScript({Behaviour{.dropAfter = 400'000}, Behaviour{}});

    PartFile file;
    file.setFileSize(kFileSize);
    file.setFileHash(m_fileHash.data());
    file.setTmpPath(m_tempDir);
    QVERIFY(file.createPartFile(m_tempDir));

    HttpCacheClient* client = nullptr;
    QCOMPARE(runFetch(server, file, client, m_cipher), HttpCacheResult::Ok);

    // Two connections: the one that died and the one that finished the job.
    QCOMPARE(server.connectionCount(), 2);
    QCOMPARE(client->attemptCount(), 2);

    // The whole point: the bytes on disk are the bytes we encrypted.
    QCOMPARE(readBackPart(file), m_plain);
}

void tst_HttpCacheResume::resumeStartsOnBlockBoundary()
{
    // 300,003 is not a multiple of 16, so three bytes are staged and thrown away
    // when the connection dies. The resumed request must ask for 300,000 — the
    // last block boundary the decryptor and the digest both reached.
    FakeCacheServer server(m_cipher);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setScript({Behaviour{.dropAfter = 300'003}, Behaviour{}});

    PartFile file;
    file.setFileSize(kFileSize);
    file.setFileHash(m_fileHash.data());
    file.setTmpPath(m_tempDir);
    QVERIFY(file.createPartFile(m_tempDir));

    HttpCacheClient* client = nullptr;
    QCOMPARE(runFetch(server, file, client, m_cipher), HttpCacheResult::Ok);

    QCOMPARE(server.requestedStarts().size(), 2);
    QCOMPARE(server.requestedStarts().at(0), 0);
    QCOMPARE(server.requestedStarts().at(1), 300'000);
    QCOMPARE(client->cipherConsumed(), static_cast<uint64>(kCipherLength));
    QCOMPARE(readBackPart(file), m_plain);
}

void tst_HttpCacheResume::corruptTailDetectedAcrossResume()
{
    // One flipped byte *after* the resume seam. This is the regression test for
    // digest continuity: if the resumed stream were hashed from scratch, or the
    // bytes around the seam were hashed twice or not at all, the digest would be
    // wrong for the wrong reason — or, worse, accidentally right.
    QByteArray corrupted = m_cipher;
    corrupted[600'123] = static_cast<char>(corrupted[600'123] ^ 0xFF);

    FakeCacheServer server(corrupted);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setScript({Behaviour{.dropAfter = 400'000}, Behaviour{}});

    PartFile file;
    file.setFileSize(kFileSize);
    file.setFileHash(m_fileHash.data());
    file.setTmpPath(m_tempDir);
    QVERIFY(file.createPartFile(m_tempDir));

    HttpCacheClient* client = nullptr;
    // The offer still carries the digest of the *good* ciphertext.
    QCOMPARE(runFetch(server, file, client, m_cipher), HttpCacheResult::Corrupt);
    QCOMPARE(server.connectionCount(), 2);
}

void tst_HttpCacheResume::serverIgnoringRangeRestartsFromZero()
{
    // A backend with no Range support answers the resumed request with the whole
    // object. That costs the bytes already fetched but still completes, which
    // beats handing the part back to ed2k.
    FakeCacheServer server(m_cipher);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setScript({Behaviour{.dropAfter = 400'000}, Behaviour{.ignoreRange = true}});

    PartFile file;
    file.setFileSize(kFileSize);
    file.setFileHash(m_fileHash.data());
    file.setTmpPath(m_tempDir);
    QVERIFY(file.createPartFile(m_tempDir));

    HttpCacheClient* client = nullptr;
    QCOMPARE(runFetch(server, file, client, m_cipher), HttpCacheResult::Ok);
    QCOMPARE(readBackPart(file), m_plain);
}

void tst_HttpCacheResume::badContentRangeRejected()
{
    // A 206 that starts 16 bytes past what we asked for would silently shift the
    // whole part. It must be refused, and refused terminally — retrying would
    // just get the same lie back.
    FakeCacheServer server(m_cipher);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setScript({Behaviour{.lieAboutRange = true}});

    PartFile file;
    file.setFileSize(kFileSize);
    file.setFileHash(m_fileHash.data());
    file.setTmpPath(m_tempDir);
    QVERIFY(file.createPartFile(m_tempDir));

    HttpCacheClient* client = nullptr;
    QCOMPARE(runFetch(server, file, client, m_cipher), HttpCacheResult::SizeMismatch);
    QCOMPARE(server.connectionCount(), 1);
}

void tst_HttpCacheResume::givesUpAfterMaxAttempts()
{
    // A server that closes right after the headers never lets the stream move, so
    // the consecutive-attempt limit is what has to stop this.
    FakeCacheServer server(m_cipher);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setScript({Behaviour{.headersOnly = true}});

    PartFile file;
    file.setFileSize(kFileSize);
    file.setFileHash(m_fileHash.data());
    file.setTmpPath(m_tempDir);
    QVERIFY(file.createPartFile(m_tempDir));

    HttpCacheClient* client = nullptr;
    QCOMPARE(runFetch(server, file, client, m_cipher), HttpCacheResult::HttpFailed);

    // First attempt plus kMaxResumeAttempts retries, and not one more.
    QCOMPARE(client->attemptCount(), HttpCacheClient::kMaxResumeAttempts + 1);
    QCOMPARE(server.connectionCount(), HttpCacheClient::kMaxResumeAttempts + 1);
}

void tst_HttpCacheResume::wholePartFetchIssuesOneRequest()
{
    // One request covers the whole part, so however often something asks this
    // client for "the next blocks" while a response is in flight, exactly one GET
    // may reach the server.
    //
    // Both entry points below are ordinary traffic in a running daemon:
    // sendFileRequest() fires when a connection is established and
    // sendBlockRequests() whenever the download machinery wants more data. For a
    // plain URL source answering each with a new GET is the design — it fetches
    // 180 KB at a time. For a whole-part fetch it pipelines a second request
    // behind the response still being read, and that reply is then parsed as a
    // continuation of the first body: the transfer stalls at whatever offset the
    // extra request went out at, and from the outside it looks like a slow server.
    //
    // The body is written a slice per event-loop turn so the calls below really do
    // land mid-response; delivered in one write it would be over before they ran.
    FakeCacheServer server(m_cipher);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setScript({Behaviour{.sliceBytes = 8 * 1024}});

    PartFile file;
    file.setFileSize(kFileSize);
    file.setFileHash(m_fileHash.data());
    file.setTmpPath(m_tempDir);
    QVERIFY(file.createPartFile(m_tempDir));

    auto* client = new HttpCacheClient();
    client->setResumeDelayOverrideMsForTest(50);
    QSignalSpy spy(client, &HttpCacheClient::fetchFinished);

    std::array<uint8, 16> peerHash{};
    peerHash.fill(0x42);

    QVERIFY(client->beginFetch(makeOffer(server, m_cipher), &file, peerHash,
                               Address::fromHostOrder(0x0A0B0C0D)));

    int nudges = 0;
    auto* prodder = new QTimer(client);
    prodder->setInterval(5);
    connect(prodder, &QTimer::timeout, client, [client, prodder, &nudges] {
        if (client->isFinished() || ++nudges > 20) {
            prodder->stop();
            return;
        }
        client->sendBlockRequests();
        client->sendFileRequest();
    });
    prodder->start();

    QVERIFY(spy.wait(30'000));
    QCOMPARE(spy.first().at(1).value<HttpCacheResult>(), HttpCacheResult::Ok);

    // Proof the prodding actually happened while the fetch was live — otherwise
    // this case would pass for the wrong reason.
    QVERIFY2(nudges > 0, "the transfer finished before anything asked for more blocks");

    QCOMPARE(server.connectionCount(), 1);
    QCOMPARE(server.requestedStarts().size(), 1);
    QCOMPARE(client->attemptCount(), 1);
    QCOMPARE(readBackPart(file), m_plain);
}

void tst_HttpCacheResume::fetchIdentifiesItself()
{
    // A chunk fetch is a plain GET to somebody else's web server, and an anonymous
    // one is what a default WAF ruleset challenges — at which point the cache looks
    // dead rather than blocked. Asserted here, on the socket, because the request is
    // hand-built: nothing between buildGetHeader() and the wire would catch a
    // dropped header.
    FakeCacheServer server(m_cipher);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setScript({Behaviour{.dropAfter = 400'000}, Behaviour{}});

    PartFile file;
    file.setFileSize(kFileSize);
    file.setFileHash(m_fileHash.data());
    file.setTmpPath(m_tempDir);
    QVERIFY(file.createPartFile(m_tempDir));

    HttpCacheClient* client = nullptr;
    QCOMPARE(runFetch(server, file, client, m_cipher), HttpCacheResult::Ok);

    // Both of them: the resumed request is built the same way, and a retry that
    // lost the agent would be the one a WAF turns away.
    const QByteArray agentLine = QByteArray("\r\nUser-Agent: ") + kUserAgent.toLatin1() + "\r\n";
    QCOMPARE(server.requests().size(), 2);
    for (const QByteArray& request : server.requests()) {
        QVERIFY(request.contains(agentLine));
        QVERIFY(request.contains("Range: bytes="));
    }
}

void tst_HttpCacheResume::downloadedBytesAreAccountedOnce()
{
    // addPayloadDown() is the only place the HTTP paths book what they downloaded — the ed2k
    // block accounting in processBlockPacket() never runs for them. All three figures it
    // feeds were broken: m_curSessionPayloadDown had no writer at all, sessionDown() read a
    // baseline marker as if it were a counter, and the credit call MFC makes alongside them
    // (srchybrid/URLClient.cpp:374,389-390) was missing.
    //
    // Asserted across a *resumed* transfer on purpose: a double-booked resume would show up
    // here as roughly one and a half parts downloaded.
    FakeCacheServer server(m_cipher);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    server.setScript({Behaviour{.dropAfter = 400'000}, Behaviour{}});

    PartFile file;
    file.setFileSize(kFileSize);
    file.setFileHash(m_fileHash.data());
    file.setTmpPath(m_tempDir);
    QVERIFY(file.createPartFile(m_tempDir));

    HttpCacheClient* client = nullptr;
    QCOMPARE(runFetch(server, file, client, m_cipher), HttpCacheResult::Ok);
    QVERIFY(client);

    // Plaintext bytes that reached the part file, counted once despite the mid-transfer drop.
    QCOMPARE(client->transferredDown(), static_cast<uint64>(kPlainLength));
    QCOMPARE(client->sessionPayloadDown(), static_cast<uint64>(kPlainLength));

    // sessionDown() is the lifetime total minus the mark taken when the download session
    // began. Nothing has reset this client, so the mark is still zero and the two agree.
    QCOMPARE(client->sessionDown(), client->transferredDown());

    // ...and the mark actually moves. Reading m_curSessionDown back directly, which is what
    // this used to do, leaves sessionDown() unchanged here.
    client->resetSessionDown();
    QCOMPARE(client->sessionDown(), static_cast<uint64>(0));
    QCOMPARE(client->sessionPayloadDown(), static_cast<uint64>(0));
    QCOMPARE(client->transferredDown(), static_cast<uint64>(kPlainLength));
}

QTEST_MAIN(tst_HttpCacheResume)
#include "tst_HttpCacheResume.moc"
