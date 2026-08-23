/// @file tst_HttpCacheRelay.cpp
/// @brief Passing a fetched HTTP Cache chunk on to other peers.
///
/// A downloader that pulled a whole part out of the cache is holding a URL, a key and
/// a digest that every other peer wanting that part could use for nothing. Relaying is
/// handing that same offer on — byte for byte, with no marker saying it was relayed, so
/// the receiver cannot tell who originally published it, and cannot tell a relay from a
/// first-hand offer either.
///
/// The rule that makes it safe is that we only relay what our own MD4 check confirmed.
/// Blame for a bad part falls on the peer that offered it (see tst_HttpCacheCorruptBan),
/// and under relay that peer is us — so we must be vouching for bytes we actually
/// verified, not merely forwarding somebody's claim.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "crypto/AesCbc.h"
#include "crypto/FileIdentifier.h"
#include "files/KnownFile.h"
#include "files/PartFile.h"
#include "httpcache/HttpCacheManager.h"
#include "httpcache/HttpCacheOffer.h"
#include "net/Address.h"
#include "net/Packet.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"
#include "utils/Opcodes.h"

#include <QCryptographicHash>
#include <QDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <memory>

using namespace eMule;

namespace {

/// Two parts, so filling one never empties the gap list and the file does not run off
/// into completeFile() mid-test.
constexpr uint64 kFileSize = PARTSIZE * 2;

const Address kPeerAddress = Address::fromString(QStringLiteral("87.65.43.21"));

std::array<uint8, 16> fileHashOf(const std::vector<std::array<uint8, 16>>& parts)
{
    QByteArray buffer;
    for (const auto& part : parts)
        buffer.append(reinterpret_cast<const char*>(part.data()), 16);

    const QByteArray digest = QCryptographicHash::hash(buffer, QCryptographicHash::Md4);
    std::array<uint8, 16> out{};
    std::memcpy(out.data(), digest.constData(), out.size());
    return out;
}

QByteArray pattern(qsizetype size, quint8 salt)
{
    QByteArray out(size, Qt::Uninitialized);
    for (qsizetype i = 0; i < size; ++i)
        out[i] = static_cast<char>((i * 131 + (i >> 11) * 17 + salt) & 0xFF);
    return out;
}

/// Whole-object GET of one fixed body — the cache server's entire role here.
class GetOnlyServer : public QTcpServer {
public:
    explicit GetOnlyServer(QByteArray body, QObject* parent = nullptr)
        : QTcpServer(parent), m_body(std::move(body))
    {
    }

    [[nodiscard]] QString url() const
    {
        return QStringLiteral("http://127.0.0.1:%1/v1/chunks/cafebabe").arg(serverPort());
    }

protected:
    void incomingConnection(qintptr handle) override
    {
        auto* socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);

        auto seen = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, seen] {
            seen->append(socket->readAll());
            if (!seen->contains("\r\n\r\n"))
                return;

            const QByteArray header =
                "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(m_body.size())
                + "\r\nConnection: close\r\n\r\n";
            socket->write(header);
            socket->write(m_body);
            socket->flush();
            socket->disconnectFromHost();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    QByteArray m_body;
};

} // namespace

class tst_HttpCacheRelay : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void verifiedChunkBecomesRelayable();
    void relayIsIdempotentAcrossFlushes();
    void corruptPartIsNeverRelayed();
    void chunkWithoutAnExpiryIsNeverRelayed();
    void relayNeedsNoCacheAccount();
    void relayCanBeTurnedOff();

private:
    /// Drive one offer end to end: fetch, then run MD4 by flushing. @p body is what the
    /// cache server hands back, so a test can serve the right part or a wrong one.
    void runOffer(const QByteArray& plaintext, uint32 expiresAt = 1800000000);

    std::unique_ptr<PartFile> makeFile(const QString& name);

    QTemporaryDir m_dir;
    QString m_tempDir;

    std::unique_ptr<ClientList> m_clientList;
    std::unique_ptr<DownloadQueue> m_queue;
    std::unique_ptr<HttpCacheManager> m_cache;
    std::unique_ptr<GetOnlyServer> m_server;
    std::unique_ptr<UpDownClient> m_peer;
    PartFile* m_file = nullptr;

    QByteArray m_goodPart;
    QByteArray m_badPart;
    std::array<uint8, 16> m_goodPartHash{};
    std::array<uint8, 16> m_peerHash{};
    HttpCacheOffer m_offer;
    int m_serial = 0;
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

void tst_HttpCacheRelay::initTestCase()
{
    QVERIFY(m_dir.isValid());

    thePrefs.load(m_dir.filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_dir.path());
    thePrefs.setHttpCacheEnabled(true);
    thePrefs.setHttpCacheAllowDownload(true);
    thePrefs.setHttpCacheAllowRelay(true);

    // The fake server is on loopback, and urlIsAcceptable() refuses LAN addresses
    // unless the user has declared this a private network.
    thePrefs.setFilterLANIPs(false);

    m_tempDir = m_dir.filePath(QStringLiteral("temp"));
    QDir().mkpath(m_tempDir);
    thePrefs.setTempDirs({m_tempDir});

    m_goodPart = pattern(static_cast<qsizetype>(PARTSIZE), 0);
    m_badPart = pattern(static_cast<qsizetype>(PARTSIZE), 1);
    QVERIFY(m_goodPart != m_badPart);

    const QByteArray digest = QCryptographicHash::hash(m_goodPart, QCryptographicHash::Md4);
    std::memcpy(m_goodPartHash.data(), digest.constData(), m_goodPartHash.size());

    m_peerHash.fill(0x5A);
}

void tst_HttpCacheRelay::init()
{
    thePrefs.setHttpCacheAllowRelay(true);
    thePrefs.setHttpCacheAllowUpload(false);
    thePrefs.setHttpCacheBaseUrl(QString());
    thePrefs.setHttpCacheApiKey(QString());

    m_clientList = std::make_unique<ClientList>();
    theApp.clientList = m_clientList.get();

    m_queue = std::make_unique<DownloadQueue>();
    theApp.downloadQueue = m_queue.get();

    m_cache = std::make_unique<HttpCacheManager>();
    theApp.httpCache = m_cache.get();

    // Released to the queue, which owns it from here.
    m_file = makeFile(QStringLiteral("relay-%1").arg(++m_serial)).release();
    QVERIFY(m_file);
    m_queue->addDownload(m_file);

    m_peer = std::make_unique<UpDownClient>();
    m_peer->setConnectAddress(kPeerAddress);
    m_peer->setUserHash(m_peerHash.data());
    m_peer->setReqFile(m_file);   // handleOffer only listens to sources of the file
    m_clientList->addClient(m_peer.get());
}

void tst_HttpCacheRelay::cleanup()
{
    if (m_peer) {
        m_clientList->removeClient(m_peer.get());
        m_peer->setReqFile(nullptr);
    }
    m_peer.reset();
    m_file = nullptr;

    m_cache.reset();
    m_queue.reset();
    m_server.reset();
    m_clientList.reset();

    theApp.httpCache = nullptr;
    theApp.downloadQueue = nullptr;
    theApp.clientList = nullptr;
}

// ---------------------------------------------------------------------------
// Promotion
// ---------------------------------------------------------------------------

void tst_HttpCacheRelay::verifiedChunkBecomesRelayable()
{
    runOffer(m_goodPart);

    QVERIFY2(m_file->isComplete(0), "the part should have verified");
    QVERIFY2(!m_file->isCorruptedPart(0), "the part should not be corrupt");

    // The chunk is now ours to pass on — and it is marked as borrowed, which is what
    // keeps it out of the Kad record and stops us ever trying to DELETE a blob we do
    // not own.
    QVERIFY2(m_cache->hasRelayEntryForTest(m_offer.fileHash, 0),
             "a verified chunk was not promoted for relay");
    QCOMPARE(m_cache->liveEntryCount(), 1);

    // Never back to whoever gave it to us.
    QVERIFY2(m_cache->wasOfferedToForTest(m_offer.fileHash, 0, m_peerHash),
             "the origin must be excluded from the offer list");
}

void tst_HttpCacheRelay::relayIsIdempotentAcrossFlushes()
{
    runOffer(m_goodPart);
    QCOMPARE(m_cache->liveEntryCount(), 1);

    // flushBuffer() re-verifies every already complete part on every flush, so the
    // promotion hook fires over and over for the life of the download.
    for (int i = 0; i < 10; ++i)
        m_file->flushBuffer();

    QCOMPARE(m_cache->liveEntryCount(), 1);
}

// ---------------------------------------------------------------------------
// What must never be relayed
// ---------------------------------------------------------------------------

void tst_HttpCacheRelay::corruptPartIsNeverRelayed()
{
    // Internally consistent — right length, right padding, ciphertext matches the
    // digest the offer pinned — and still not the part. Only MD4 can tell.
    runOffer(m_badPart);

    QVERIFY2(m_file->isCorruptedPart(0), "the part should have failed MD4");
    QVERIFY2(!m_cache->hasRelayEntryForTest(m_offer.fileHash, 0),
             "a part that failed its hash must never be handed on");
    QCOMPARE(m_cache->liveEntryCount(), 0);
}

void tst_HttpCacheRelay::chunkWithoutAnExpiryIsNeverRelayed()
{
    // expiresAt == 0 reads as "never lapses", which is right for a blob we published
    // and wrong for one we are borrowing: the owner can DELETE it whenever it likes,
    // and we would go on pointing peers at a dead URL forever.
    runOffer(m_goodPart, /*expiresAt*/ 0);

    QVERIFY(m_file->isComplete(0));
    QVERIFY2(!m_cache->hasRelayEntryForTest(m_offer.fileHash, 0),
             "a chunk with no stated expiry must not be relayed");
}

// ---------------------------------------------------------------------------
// Gating
// ---------------------------------------------------------------------------

void tst_HttpCacheRelay::relayNeedsNoCacheAccount()
{
    // init() already leaves allowUpload off with no base url and no API key. That is
    // the whole point of the feature: passing a chunk on costs one packet and never
    // touches the cache server, so a node that could not publish anything still can.
    QVERIFY(thePrefs.httpCacheBaseUrl().isEmpty());
    QVERIFY(thePrefs.httpCacheApiKey().isEmpty());
    QVERIFY(!thePrefs.httpCacheAllowUpload());

    runOffer(m_goodPart);

    QVERIFY2(m_cache->hasRelayEntryForTest(m_offer.fileHash, 0),
             "relaying must not require a cache account");
}

void tst_HttpCacheRelay::relayCanBeTurnedOff()
{
    thePrefs.setHttpCacheAllowRelay(false);

    runOffer(m_goodPart);

    QVERIFY2(m_file->isComplete(0), "the fetch itself must still work");
    QVERIFY2(!m_cache->hasRelayEntryForTest(m_offer.fileHash, 0),
             "allowRelay=false must stop the promotion");
    QCOMPARE(m_cache->liveEntryCount(), 0);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void tst_HttpCacheRelay::runOffer(const QByteArray& plaintext, uint32 expiresAt)
{
    const QByteArray key = aesRandomKey();
    const QByteArray iv = aesRandomIv();
    const QByteArray cipher = aesEncrypt(plaintext, key, iv);

    m_server = std::make_unique<GetOnlyServer>(cipher);
    QVERIFY(m_server->listen(QHostAddress::LocalHost));

    m_offer = HttpCacheOffer{};
    std::memcpy(m_offer.fileHash.data(), m_file->fileHash(), m_offer.fileHash.size());
    m_offer.partIndex = 0;
    m_offer.plainLength = PARTSIZE;
    m_offer.cipherLength = static_cast<uint64>(cipher.size());
    m_offer.url = m_server->url();
    m_offer.key = key;
    m_offer.iv = iv;
    m_offer.cipherSha256 = QCryptographicHash::hash(cipher, QCryptographicHash::Sha256);
    m_offer.expiresAt = expiresAt;
    QVERIFY2(m_offer.isWellFormed(), qPrintable(m_offer.malformedReason()));

    // Through the real packet entry point, so the offer walks the same validation
    // ladder a peer's packet would.
    const auto packet = HttpCacheCodec::buildOffer(m_offer);
    QVERIFY(packet != nullptr);
    m_cache->handlePacket(m_peer.get(),
                          reinterpret_cast<const uint8*>(packet->pBuffer), packet->size);

    QTRY_VERIFY_WITH_TIMEOUT(m_cache->activeFetchCount() == 0, 120'000);
    QCOMPARE(m_cache->sessionChunksFetched(), 1U);

    // MD4 does not run until the part file next flushes, which is where the relay
    // decision is taken.
    m_file->flushBuffer();
}

std::unique_ptr<PartFile> tst_HttpCacheRelay::makeFile(const QString& name)
{
    auto file = std::make_unique<PartFile>();

    file->setFileName(name + QStringLiteral(".bin"));
    file->setFileSize(EMFileSize(kFileSize));
    file->setTmpPath(m_tempDir);

    // Only part 0's hash has to be real; part 1 is never written. Varying part 1 is
    // what gives each case a distinct file hash and its own .part files.
    std::array<uint8, 16> unusedHash{};
    unusedHash.fill(0x22);
    unusedHash[0] = static_cast<uint8>(m_serial);
    const std::vector<std::array<uint8, 16>> partHashes{m_goodPartHash, unusedHash};

    // setMD4HashSet() verifies the set against the file hash and throws it away when
    // they disagree — which would leave hashSinglePart() nothing to compare against and
    // make every part look fine, quietly turning these tests green for the wrong reason.
    file->setFileHash(fileHashOf(partHashes).data());
    if (!file->fileIdentifier().setMD4HashSet(partHashes))
        return nullptr;
    if (file->fileIdentifier().getMD4PartHash(0) == nullptr)
        return nullptr;

    if (!file->createPartFile(m_tempDir))
        return nullptr;

    return file;
}

QTEST_MAIN(tst_HttpCacheRelay)
#include "tst_HttpCacheRelay.moc"
