/// @file tst_HttpCacheCorruptBan.cpp
/// @brief Who gets blocked when a downloaded part turns out to be corrupt.
///
/// eMule bans a peer that feeds it bad bytes over eD2K, and a chunk fetched
/// through the HTTP Cache must be treated the same way — with one difference that
/// is the whole point of these tests: the party to blame is the *peer that offered
/// the chunk*, never the cache server that stored it. The server holds an opaque
/// AES blob it has no key for, and the ciphertext is checked against the SHA-256
/// the peer pinned in its offer before a single plaintext byte is trusted. So a
/// part that reaches the disk and then fails MD4 can only be the peer's doing.
///
/// The eD2K cases are here too, in the same file, because they run through exactly
/// the same attribution — that shared path is the reason the HTTP Cache case needs
/// no machinery of its own.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/ClientStateDefs.h"
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

/// Two parts, so filling one never empties the gap list and PartFile does not
/// wander off into completeFile() in the middle of a test.
constexpr uint64 kFileSize = PARTSIZE * 2;

/// Neither of these is on loopback, so "the peer got banned" and "the cache
/// server got banned" can never turn out to be the same assertion.
const Address kPeerAddress = Address::fromString(QStringLiteral("87.65.43.21"));
const Address kOtherPeerAddress = Address::fromString(QStringLiteral("12.34.56.78"));

/// The eD2K file hash of a hash set: MD4 over the concatenated part hashes.
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

/// Whole-object GET of one fixed body. The cache server's only job in these tests
/// is to hand back exactly what it was given, which is precisely why it cannot be
/// the guilty party.
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

            QByteArray head = "HTTP/1.1 200 OK\r\n";
            head += "Content-Type: application/octet-stream\r\n";
            head += "Content-Length: " + QByteArray::number(m_body.size()) + "\r\n";
            head += "Connection: close\r\n\r\n";
            socket->write(head);
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

class tst_HttpCacheCorruptBan : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void singleSenderOfABadPartIsBanned();
    void aConnectedClientIsBannedThroughItsOwnBanPath();
    void aGoodPartBlamesNobody();
    void twoSendersMeanNoBlameWithoutAich();
    void anUnattributedWriteBlamesNobody();
    void aPartOnlyHalfAttributedBlamesNobody();
    void httpCacheChunkBlamesThePeerAndNotTheServer();

private:
    std::unique_ptr<PartFile> makeFile(const QString& name);
    static void writeRange(PartFile& file, const QByteArray& bytes, uint64 offset,
                           const Address& sender);

    QTemporaryDir m_dir;
    QString m_tempDir;
    ClientList* m_clientList = nullptr;

    /// The bytes part 0 is supposed to contain, and a same-length impostor.
    QByteArray m_goodPart;
    QByteArray m_badPart;
    std::array<uint8, 16> m_goodPartHash{};
    int m_serial = 0;
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------

void tst_HttpCacheCorruptBan::initTestCase()
{
    QVERIFY(m_dir.isValid());

    thePrefs.load(m_dir.filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_dir.path());
    thePrefs.setHttpCacheEnabled(true);
    thePrefs.setHttpCacheAllowDownload(true);

    // The fake cache server lives on loopback, and urlIsAcceptable() refuses LAN
    // addresses unless the user has said this is a private network.
    thePrefs.setFilterLANIPs(false);

    m_tempDir = m_dir.filePath(QStringLiteral("temp"));
    QDir().mkpath(m_tempDir);
    thePrefs.setTempDirs({m_tempDir});

    m_goodPart = pattern(static_cast<qsizetype>(PARTSIZE), 0);
    m_badPart = pattern(static_cast<qsizetype>(PARTSIZE), 1);
    QCOMPARE(m_goodPart.size(), m_badPart.size());
    QVERIFY(m_goodPart != m_badPart);

    uint8 hash[16]{};
    KnownFile::createHashFromMemory(reinterpret_cast<const uint8*>(m_goodPart.constData()),
                                    static_cast<uint32>(m_goodPart.size()), hash, nullptr);
    std::memcpy(m_goodPartHash.data(), hash, 16);
}

void tst_HttpCacheCorruptBan::init()
{
    m_clientList = new ClientList();
    theApp.clientList = m_clientList;
}

void tst_HttpCacheCorruptBan::cleanup()
{
    theApp.httpCache = nullptr;
    theApp.downloadQueue = nullptr;
    theApp.clientList = nullptr;
    delete m_clientList;
    m_clientList = nullptr;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void tst_HttpCacheCorruptBan::singleSenderOfABadPartIsBanned()
{
    auto file = makeFile(QStringLiteral("single-sender"));
    QVERIFY(file);
    QVERIFY2(file->fileIdentifier().getMD4PartHash(0) != nullptr,
             "no part hash to verify against — every part would look fine");

    writeRange(*file, m_badPart, 0, kPeerAddress);
    file->flushBuffer();

    // The part is rejected...
    QVERIFY(file->isCorruptedPart(0));
    QVERIFY(!file->isComplete(0));

    // ...and so is the only peer that could have supplied it. No AICH recovery was
    // involved: with one sender there is nothing left to narrow down, which is what
    // makes this work for a cache chunk that no second source has a hash for.
    QVERIFY(m_clientList->isBannedClient(kPeerAddress));
}

void tst_HttpCacheCorruptBan::aConnectedClientIsBannedThroughItsOwnBanPath()
{
    auto file = makeFile(QStringLiteral("connected-client"));
    QVERIFY(file);

    UpDownClient peer;
    peer.setConnectAddress(kPeerAddress);
    m_clientList->addClient(&peer);

    writeRange(*file, m_badPart, 0, kPeerAddress);
    file->flushBuffer();

    // Banning the bare address would have left the client object untouched — still
    // holding whatever upload state it had, and only refused on its next reconnect.
    QVERIFY(peer.isBanned());
    QCOMPARE(peer.uploadState(), UploadState::Banned);
    QVERIFY(m_clientList->isBannedClient(kPeerAddress));

    m_clientList->removeClient(&peer);
}

void tst_HttpCacheCorruptBan::aGoodPartBlamesNobody()
{
    auto file = makeFile(QStringLiteral("good-part"));
    QVERIFY(file);

    writeRange(*file, m_goodPart, 0, kPeerAddress);
    file->flushBuffer();

    QVERIFY(file->isComplete(0));
    QVERIFY(!file->isCorruptedPart(0));
    QVERIFY(!m_clientList->isBannedClient(kPeerAddress));
    QCOMPARE(m_clientList->bannedCount(), 0);
}

void tst_HttpCacheCorruptBan::twoSendersMeanNoBlameWithoutAich()
{
    auto file = makeFile(QStringLiteral("two-senders"));
    QVERIFY(file);

    // Half the part from each. Only one of them ruined it, and from here there is
    // no way to tell which — narrowing that down is what AICH recovery is for, so
    // nobody is banned on a guess.
    const qsizetype half = m_badPart.size() / 2;
    writeRange(*file, m_badPart.left(half), 0, kPeerAddress);
    writeRange(*file, m_badPart.mid(half), static_cast<uint64>(half), kOtherPeerAddress);
    file->flushBuffer();

    QVERIFY(file->isCorruptedPart(0));
    QVERIFY(!m_clientList->isBannedClient(kPeerAddress));
    QVERIFY(!m_clientList->isBannedClient(kOtherPeerAddress));
    QCOMPARE(m_clientList->bannedCount(), 0);
}

void tst_HttpCacheCorruptBan::anUnattributedWriteBlamesNobody()
{
    auto file = makeFile(QStringLiteral("no-sender"));
    QVERIFY(file);

    // A null sender is how an eD2K URL source writes: a web server is not a peer,
    // and there is nobody to ban. It must not become a sender keyed on "nothing"
    // either, or every unattributed download would share one guilty identity.
    writeRange(*file, m_badPart, 0, Address{});
    file->flushBuffer();

    QVERIFY(file->isCorruptedPart(0));
    QCOMPARE(m_clientList->bannedCount(), 0);
}

void tst_HttpCacheCorruptBan::aPartOnlyHalfAttributedBlamesNobody()
{
    auto file = makeFile(QStringLiteral("half-attributed"));
    QVERIFY(file);

    // What a resumed download looks like: the records do not survive a restart, so
    // the first half of the part arrived with nobody attached to it. The peer that
    // fills the rest must not be handed the blame for bytes it never sent.
    const qsizetype half = m_badPart.size() / 2;
    writeRange(*file, m_badPart.left(half), 0, Address{});
    writeRange(*file, m_badPart.mid(half), static_cast<uint64>(half), kPeerAddress);
    file->flushBuffer();

    QVERIFY(file->isCorruptedPart(0));
    QVERIFY(!m_clientList->isBannedClient(kPeerAddress));
    QCOMPARE(m_clientList->bannedCount(), 0);
}

void tst_HttpCacheCorruptBan::httpCacheChunkBlamesThePeerAndNotTheServer()
{
    // A chunk that is honest about itself in every way the transfer can check —
    // the ciphertext matches the SHA-256 in the offer, the padding is right, the
    // length is right — and still decrypts to bytes that are not the part.
    const QByteArray key = aesRandomKey();
    const QByteArray iv = aesRandomIv();
    const QByteArray cipher = aesEncrypt(m_badPart, key, iv);

    GetOnlyServer server(cipher);
    QVERIFY(server.listen(QHostAddress::LocalHost));
    const Address serverAddress = Address::fromString(QStringLiteral("127.0.0.1"));

    DownloadQueue queue;
    theApp.downloadQueue = &queue;

    // The queue takes ownership; releasing here keeps the two from double-freeing.
    PartFile* file = makeFile(QStringLiteral("http-cache-chunk")).release();
    QVERIFY(file);
    queue.addDownload(file);

    HttpCacheManager cache;
    theApp.httpCache = &cache;

    UpDownClient peer;
    peer.setConnectAddress(kPeerAddress);
    std::array<uint8, 16> peerHash{};
    peerHash.fill(0x5A);
    peer.setUserHash(peerHash.data());
    peer.setReqFile(file);          // handleOffer only listens to sources of the file
    m_clientList->addClient(&peer);

    HttpCacheOffer offer;
    std::memcpy(offer.fileHash.data(), file->fileHash(), offer.fileHash.size());
    offer.partIndex = 0;
    offer.plainLength = PARTSIZE;
    offer.cipherLength = static_cast<uint64>(cipher.size());
    offer.url = server.url();
    offer.key = key;
    offer.iv = iv;
    offer.cipherSha256 = QCryptographicHash::hash(cipher, QCryptographicHash::Sha256);
    offer.expiresAt = 0;
    QVERIFY2(offer.isWellFormed(), qPrintable(offer.malformedReason()));

    // Through the real packet entry point rather than a back door, so the offer
    // passes the same validation ladder a peer's packet would.
    const auto packet = HttpCacheCodec::buildOffer(offer);
    QVERIFY(packet != nullptr);
    cache.handlePacket(&peer, reinterpret_cast<const uint8*>(packet->pBuffer), packet->size);

    // The fetch itself succeeds: nothing it is able to verify is wrong.
    QTRY_VERIFY_WITH_TIMEOUT(cache.activeFetchCount() == 0, 120'000);
    QCOMPARE(cache.sessionChunksFetched(), 1U);
    QVERIFY(cache.hasFetchAttributionForTest(offer.fileHash, 0));

    // MD4 is the first check that can tell the difference, and it runs on the flush.
    file->flushBuffer();

    QVERIFY(file->isCorruptedPart(0));
    QVERIFY(m_clientList->isBannedClient(kPeerAddress));

    // The whole point: the machine that served the bytes is not the machine that
    // chose them. Banning it would cost every other download its cache.
    QVERIFY(!m_clientList->isBannedClient(serverAddress));
    QCOMPARE(m_clientList->bannedCount(), 1);

    // The attribution was spent telling the uploader, not left to age out.
    QVERIFY(!cache.hasFetchAttributionForTest(offer.fileHash, 0));

    m_clientList->removeClient(&peer);
    peer.setReqFile(nullptr);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::unique_ptr<PartFile> tst_HttpCacheCorruptBan::makeFile(const QString& name)
{
    auto file = std::make_unique<PartFile>();

    file->setFileName(name + QStringLiteral(".bin"));
    file->setFileSize(EMFileSize(kFileSize));
    file->setTmpPath(m_tempDir);

    // Only part 0's hash has to be real; part 1 is never written, so it is never
    // hashed. Varying it is what makes each case a distinct file.
    std::array<uint8, 16> unusedHash{};
    unusedHash.fill(0x22);
    unusedHash[0] = static_cast<uint8>(++m_serial);
    const std::vector<std::array<uint8, 16>> partHashes{m_goodPartHash, unusedHash};

    // The file hash is not free to choose: setMD4HashSet() verifies the set against
    // it and throws the whole set away when they disagree — which then leaves
    // hashSinglePart() with nothing to compare against and every part looking fine.
    file->setFileHash(fileHashOf(partHashes).data());
    if (!file->fileIdentifier().setMD4HashSet(partHashes))
        return nullptr;

    if (!file->createPartFile(m_tempDir))
        return nullptr;

    return file;
}

void tst_HttpCacheCorruptBan::writeRange(PartFile& file, const QByteArray& bytes, uint64 offset,
                                         const Address& sender)
{
    // In block-sized pieces, the way a real transfer arrives — and the way the
    // blackbox expects them: it refuses a single range as large as a whole part.
    for (qsizetype pos = 0; pos < bytes.size(); pos += EMBLOCKSIZE) {
        const qsizetype len = std::min<qsizetype>(EMBLOCKSIZE, bytes.size() - pos);
        const uint64 start = offset + static_cast<uint64>(pos);
        file.writeToBuffer(static_cast<uint64>(len),
                           reinterpret_cast<const uint8*>(bytes.constData() + pos),
                           start, start + static_cast<uint64>(len) - 1,
                           nullptr, sender);
    }
}

QTEST_MAIN(tst_HttpCacheCorruptBan)
#include "tst_HttpCacheCorruptBan.moc"
