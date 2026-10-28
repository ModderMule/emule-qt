/// @file tst_Ed3kPaid.cpp
/// @brief Paid subscriptions, the buyer's side (src/ed3k/Ed3kPaid, subscription.md §5, §6, §9):
/// a publisher node P holds a season and its own pass; the buyer B follows the scope with its
/// offer, buys (the pay link), the core polls a fake seller (HTTPS on loopback: 404, then 200
/// with a pass for B's holder key), the pass is set, B asks P for keys (PAID_KEYS). The
/// holder key stays owner-only, the pass is kept across a restart; the IPC lists it.
///
/// The test signs as the publisher (OpenSSL Ed25519) and derives the chain's anchor with a
/// small BLAKE3 derive_key (single block: every input here is under 64 bytes).

#include "Ed3kPaid.h"
#include "Ed3kPaidBridge.h"
#include "Ed3kPlumbing.h"
#include "Ed3kProto.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "IpcMessage.h"
#include "TestHelpers.h"

#include <QCborArray>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <openssl/evp.h>

#include <array>
#include <cstring>

using namespace eMule;
using namespace eMule::ed3k;

namespace {

// -- BLAKE3 derive_key, one block (inputs ≤ 64 bytes) -----------------------

constexpr std::array<std::uint32_t, 8> kIv{0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
                                           0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19};
constexpr std::array<int, 16> kPerm{2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8};

std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

void g(std::array<std::uint32_t, 16>& s, std::size_t a, std::size_t b, std::size_t c, std::size_t d, std::uint32_t mx, std::uint32_t my)
{
    s[a] = s[a] + s[b] + mx;
    s[d] = rotr(s[d] ^ s[a], 16);
    s[c] = s[c] + s[d];
    s[b] = rotr(s[b] ^ s[c], 12);
    s[a] = s[a] + s[b] + my;
    s[d] = rotr(s[d] ^ s[a], 8);
    s[c] = s[c] + s[d];
    s[b] = rotr(s[b] ^ s[c], 7);
}

std::array<std::uint32_t, 8> compress(const std::array<std::uint32_t, 8>& cv, const QByteArray& data, std::uint32_t flags)
{
    std::array<std::uint32_t, 16> m{};
    for (qsizetype i = 0; i < data.size(); ++i)
        m[std::size_t(i / 4)] |= std::uint32_t(std::uint8_t(data[i])) << (8 * (i % 4));
    std::array<std::uint32_t, 16> s{cv[0], cv[1], cv[2], cv[3], cv[4], cv[5], cv[6], cv[7],
                                    kIv[0], kIv[1], kIv[2], kIv[3], 0, 0, std::uint32_t(data.size()), flags};
    for (int r = 0; r < 7; ++r) {
        g(s, 0, 4, 8, 12, m[0], m[1]);
        g(s, 1, 5, 9, 13, m[2], m[3]);
        g(s, 2, 6, 10, 14, m[4], m[5]);
        g(s, 3, 7, 11, 15, m[6], m[7]);
        g(s, 0, 5, 10, 15, m[8], m[9]);
        g(s, 1, 6, 11, 12, m[10], m[11]);
        g(s, 2, 7, 8, 13, m[12], m[13]);
        g(s, 3, 4, 9, 14, m[14], m[15]);
        std::array<std::uint32_t, 16> p{};
        for (std::size_t i = 0; i < 16; ++i)
            p[i] = m[std::size_t(kPerm[i])];
        m = p;
    }
    std::array<std::uint32_t, 8> out{};
    for (std::size_t i = 0; i < 8; ++i)
        out[i] = s[i] ^ s[i + 8];
    return out;
}

/// H(ctx, data) of Protocol.md: BLAKE3 derive_key, 32 bytes
QByteArray h(const char* ctx, const QByteArray& data)
{
    constexpr std::uint32_t kStartEndRoot = 1 | 2 | 8;
    const auto ck = compress(kIv, QByteArray(ctx), kStartEndRoot | 32);
    const auto o = compress(ck, data, kStartEndRoot | 64);
    QByteArray out(32, 0);
    for (int i = 0; i < 32; ++i)
        out[i] = char(o[std::size_t(i / 4)] >> (8 * (i % 4)));
    return out;
}

QByteArray be64(std::uint64_t v)
{
    QByteArray out(8, 0);
    for (int i = 7; i >= 0; --i, v >>= 8)
        out[i] = char(v & 0xFF);
    return out;
}

QByteArray raw(std::span<const std::uint8_t> b)
{
    return QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size()));
}

std::span<const std::byte> spanOf(const QByteArray& b)
{
    return {reinterpret_cast<const std::byte*>(b.constData()), std::size_t(b.size())};
}

// -- Ed25519 (OpenSSL) -------------------------------------------------------

struct Signer {
    ::ed3k::Key32 seed{};

    QByteArray publicKey() const
    {
        EVP_PKEY* k = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
        QByteArray out(32, 0);
        std::size_t len = 32;
        EVP_PKEY_get_raw_public_key(k, reinterpret_cast<unsigned char*>(out.data()), &len);
        EVP_PKEY_free(k);
        return out;
    }
    ::ed3k::Key32 key() const
    {
        ::ed3k::Key32 k{};
        std::memcpy(k.data(), publicKey().constData(), k.size());
        return k;
    }
    /// a Signature message (alg 1) over @p msg
    QByteArray signature(const QByteArray& msg) const
    {
        EVP_PKEY* k = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        QByteArray sig(64, 0);
        std::size_t len = 64;
        EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, k);
        EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(sig.data()), &len, reinterpret_cast<const unsigned char*>(msg.constData()),
                       std::size_t(msg.size()));
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(k);
        return Ed3kProtoWriter().varint(1, 1).bytes(2, sig).data();
    }
};

// -- the publisher's documents (subscription.md §3, §4.1, §5) ----------------

struct Season {
    QByteArray scope;
    std::uint64_t base = 0;
    std::uint32_t length = 0;
    ::ed3k::Key32 seed{};
    QByteArray signedChain;
};

Season makeSeason(const Signer& pub, const QByteArray& scope, std::uint64_t base, std::uint32_t length)
{
    Season s{scope, base, length, {}, {}};
    s.seed.fill(0x5E);
    // K(base + length − 1) from the seed, each earlier key from the next, the anchor of K(base)
    QByteArray k = h("ed3k v1 paid seed", scope + be64(base) + raw(s.seed));
    for (std::uint32_t i = 1; i < length; ++i)
        k = h("ed3k v1 paid chain", scope + k);
    const QByteArray anchor = h("ed3k v1 paid anchor", scope + k);
    const QByteArray chain = Ed3kProtoWriter().bytes(1, scope).varint(2, base).varint(3, length).bytes(4, anchor).data();
    s.signedChain = Ed3kProtoWriter().bytes(1, chain).bytes(2, pub.signature(QByteArray("ed3k v1 paid season") + chain)).data();
    return s;
}

QByteArray makeOffer(const Signer& pub, const Season& season, std::uint64_t issued, std::uint64_t t0, std::uint32_t period,
                     const QString& payUrl, const QString& passUrl)
{
    const QByteArray offer = Ed3kProtoWriter()
                                 .bytes(1, season.scope)
                                 .varint(2, issued)
                                 .varint(3, t0)
                                 .varint(4, period)
                                 .bytes(5, season.signedChain)
                                 .bytes(7, QByteArrayLiteral("4.99 EUR / month"))
                                 .bytes(8, payUrl.toUtf8())
                                 .bytes(9, passUrl.toUtf8())
                                 .bytes(11, QByteArrayLiteral("No refunds."))
                                 .data();
    return Ed3kProtoWriter().bytes(1, offer).bytes(2, pub.signature(QByteArray("ed3k v1 paid offer") + offer)).data();
}

QByteArray makePass(const Signer& pub, const QByteArray& scope, const QByteArray& holder, std::uint64_t issued, std::uint64_t until)
{
    const QByteArray pass = Ed3kProtoWriter().bytes(1, scope).bytes(2, holder).varint(3, until).varint(5, issued).data();
    return Ed3kProtoWriter().bytes(1, pass).bytes(2, pub.publicKey()).bytes(3, pub.signature(QByteArray("ed3k v1 paid pass") + pass)).data();
}

// -- a seller's pass_url on loopback (§9.1): 404 first, then the pass -------

class FakeSeller : public QObject {
public:
    explicit FakeSeller(std::function<QByteArray(const QByteArray& holder)> passFor) : m_passFor(std::move(passFor))
    {
        QSslConfiguration config = QSslConfiguration::defaultConfiguration();
        QFile cert(testing::testDataDir() + QStringLiteral("/tls/localhost-cert.pem"));
        QFile key(testing::testDataDir() + QStringLiteral("/tls/localhost-key.pem"));
        if (cert.open(QIODevice::ReadOnly))
            config.setLocalCertificate(QSslCertificate(&cert));
        if (key.open(QIODevice::ReadOnly))
            config.setPrivateKey(QSslKey(&key, QSsl::Rsa));
        config.setPeerVerifyMode(QSslSocket::VerifyNone);
        m_server.setSslConfiguration(config);
        m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::pendingConnectionAvailable, this, [this] {
            while (QTcpSocket* s = m_server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] { serve(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }
    QList<QByteArray> requests;

private:
    void serve(QTcpSocket* s)
    {
        QByteArray& in = m_in[s];
        in += s->readAll();
        if (!in.contains("\r\n\r\n"))
            return;
        const QByteArray line = in.left(in.indexOf("\r\n"));
        requests << line;
        const QList<QByteArray> parts = line.split(' ');
        const QUrl url(QString::fromLatin1(parts.value(1)));
        const QUrlQuery q(url.query());
        const auto holder = fromBase32(q.queryItemValue(QStringLiteral("holder")));
        QByteArray reply;
        if (url.path() != QLatin1String("/pass") || !holder) {
            reply = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        } else if (requests.size() == 1) {
            reply = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";   // not paid yet
        } else {
            const QByteArray body = m_passFor(*holder);
            reply = "HTTP/1.1 200 OK\r\nContent-Type: application/x-ed3k-pass\r\nContent-Length: " + QByteArray::number(body.size())
                    + "\r\nConnection: close\r\n\r\n" + body;
        }
        s->write(reply);
        s->disconnectFromHost();
        m_in.remove(s);
    }

    QSslServer m_server;
    QHash<QTcpSocket*, QByteArray> m_in;
    std::function<QByteArray(const QByteArray&)> m_passFor;
};

Ed3kSettings loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");   // private addresses are targets (http-source.md §5)
    s.forceOpen = true;
    return s;
}

::ed3k::Source sourceOf(const Ed3kSession& s)
{
    return ::ed3k::Source{*s.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, s.udpPort()), std::nullopt, {}};
}

} // namespace

class tst_Ed3kPaid : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void theHashIsBlake3DeriveKey();
    void offersAndPassesAreRead();
    void anOfferThenAPassThenKeys();
    void cleanup() { QTest::qWait(10); }
};

void tst_Ed3kPaid::initTestCase()
{
    // the seller's self-signed certificate is trusted here (it names 127.0.0.1)
    QSslConfiguration c = QSslConfiguration::defaultConfiguration();
    QFile cert(testing::testDataDir() + QStringLiteral("/tls/localhost-cert.pem"));
    QVERIFY(cert.open(QIODevice::ReadOnly));
    c.addCaCertificate(QSslCertificate(&cert));
    QSslConfiguration::setDefaultConfiguration(c);
}

void tst_Ed3kPaid::theHashIsBlake3DeriveKey()
{
    // NodeID = H("ed3k v1 node id", public key)[0..16) (Protocol.md §6.1): the test's H is the core's
    QTemporaryDir d;
    Ed3kSession s;
    QVERIFY(s.start(loopback(d.path())));
    const auto seed = loadSecret(stateDir(d.path()), QStringLiteral("node.key"));
    QVERIFY(seed);
    Signer node{*seed};
    QCOMPARE(h("ed3k v1 node id", node.publicKey()).left(16), raw(s.nodeId()->bytes));
    s.stop();
}

void tst_Ed3kPaid::offersAndPassesAreRead()
{
    Signer pub;
    pub.seed.fill(0x21);
    const QByteArray scope(16, 's');
    const Season season = makeSeason(pub, scope, 5, 3);
    const auto o = Ed3kPaidOffer::parse(makeOffer(pub, season, 100, 50, 3600, QStringLiteral("https://shop.example/pay?x=1"),
                                                  QStringLiteral("https://shop.example/pass")));
    QVERIFY(o);
    QCOMPARE(o->scope, scope);
    QCOMPARE(o->issued, 100u);
    QCOMPARE(o->t0, 50u);
    QCOMPARE(o->period, 3600u);
    QCOMPARE(o->price, QStringLiteral("4.99 EUR / month"));
    QCOMPARE(o->terms, QStringLiteral("No refunds."));
    QCOMPARE(o->payUrl, QStringLiteral("https://shop.example/pay?x=1"));
    QVERIFY(!Ed3kPaidOffer::parse(QByteArray("\x0a\x02\x08", 3)));
    const auto p = Ed3kPaidPass::parse(makePass(pub, scope, QByteArray(32, 'h'), 10, 999));
    QVERIFY(p);
    QCOMPARE(p->until, 999u);
    QCOMPARE(p->holder, QByteArray(32, 'h'));
    QCOMPARE(p->scopes.size(), std::size_t(1));
    // §9.1: `&` when the URL has a query
    ::ed3k::Scope sc{};
    ::ed3k::Key32 holder{};
    QCOMPARE(Ed3kPaid::withQuery(QStringLiteral("https://a.example/pay?x=1"), sc, holder),
             QStringLiteral("https://a.example/pay?x=1&scope=%1&holder=%2").arg(base32(sc.bytes), base32(holder)));
    QVERIFY(Ed3kPaid::withQuery(QStringLiteral("https://a.example/pay"), sc, holder).startsWith(QStringLiteral("https://a.example/pay?scope=")));
}

void tst_Ed3kPaid::anOfferThenAPassThenKeys()
{
    QTemporaryDir dp, db;
    Ed3kSession p, b;
    QVERIFY(p.start(loopback(dp.path())));
    QVERIFY(b.start(loopback(db.path())));

    // the publisher of a hidden paid set (scope kind 3: the set key, no id)
    Signer pub;
    pub.seed.fill(0x31);
    const auto scope = ::ed3k::paidScope(3, pub.key(), {});
    QVERIFY(scope);
    const QByteArray scopeRaw = raw(scope->bytes);
    const std::uint64_t now = p.now().unixS;
    const std::uint32_t period = 3600;
    const std::uint64_t t0 = now - 10 * period;
    const std::uint64_t epoch = (now - t0) / period;
    const Season season = makeSeason(pub, scopeRaw, epoch, 24);
    std::uint64_t until = now + 7 * 86400;
    FakeSeller seller([&](const QByteArray& holder) { return makePass(pub, scopeRaw, holder, now - 60, until); });
    QVERIFY(seller.port() > 0);
    const QString base = QStringLiteral("https://127.0.0.1:%1").arg(seller.port());
    const QByteArray offer = makeOffer(pub, season, now - 60, t0, period, base + QStringLiteral("/pay"), base + QStringLiteral("/pass"));

    // P: the offer, the season (its seed) and a pass of its own: a key holder (§5, §6.2)
    QVERIFY(p.engine()->paidFollow(pub.key(), *scope));
    QVERIFY(p.engine()->paidSetOffer(p.now(), *scope, spanOf(offer)));
    QVERIFY(p.engine()->paidPublish(*scope, spanOf(season.signedChain), season.seed));
    Signer pHolder;
    pHolder.seed.fill(0x41);
    QVERIFY(p.engine()->paidSetPass(p.now(), *scope, spanOf(makePass(pub, scopeRaw, pHolder.publicKey(), now - 60, until)), pHolder.seed));
    p.pump();

    // B: follows the scope, the offer comes (from the manifest or a pass holder; by hand here)
    auto paid = std::make_unique<Ed3kPaid>(b, db.path());
    paid->follow(pub.key(), *scope, Ed3kPaid::Kind::HiddenSet, QStringLiteral("Season Pass"));
    QVERIFY(paid->setOffer(*scope, offer));
    const QString scopeHex = Ed3kPaid::hexOf(*scope);
    auto info = paid->info(scopeHex);
    QVERIFY(info && info->offer);
    QCOMPARE(info->offer->price, QStringLiteral("4.99 EUR / month"));
    QCOMPARE(info->state, Ed3kPaid::State::NoPass);
    // no anonymous node runs: refused, nothing bought
    QVERIFY(!paid->buy(scopeHex, true));

    // §9.1: the pay link carries scope and holder; pass_url is polled (404, then the pass)
    QSignalSpy passes(paid.get(), &Ed3kPaid::passArrived);
    const auto link = paid->buy(scopeHex);
    QVERIFY2(link, link ? "" : link.error().toUtf8().constData());
    QVERIFY(link->startsWith(base + QStringLiteral("/pay?scope=") + base32(scope->bytes) + QStringLiteral("&holder=")));
    QCOMPARE(paid->info(scopeHex)->state, Ed3kPaid::State::Buying);
    QTRY_VERIFY_WITH_TIMEOUT(passes.count() == 1, 60000);
    QVERIFY(seller.requests.size() >= 2);
    QVERIFY(seller.requests.first().startsWith(QByteArray("GET /pass?scope=" + base32(scope->bytes).toLatin1() + "&holder=")));
    info = paid->info(scopeHex);
    QCOMPARE(info->state, Ed3kPaid::State::Held);
    QCOMPARE(info->until, until);
    QVERIFY(info->renewing);   // §9.2: polled in the last 48 h

    // the holder key: a file of its own, owner-only; paid.json does not hold it
    const QString holderFile = QDir(stateDir(db.path())).filePath(QStringLiteral("paid/%1.holder").arg(scopeHex));
    QVERIFY(QFileInfo::exists(holderFile));
    QCOMPARE(QFileInfo(holderFile).size(), 32);
#ifndef Q_OS_WIN
    QCOMPARE(QFile::permissions(holderFile) & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions{});
#endif
    const QByteArray seedBytes = readBlob(holderFile);
    const QByteArray json = readBlob(paid->statePath());
    QVERIFY(!json.contains(seedBytes.toHex()) && !json.contains(seedBytes.toBase64()));

    // keys from P (§6.1: P shows its pass, then B's pass, then the key)
    QSignalSpy keys(paid.get(), &Ed3kPaid::keysArrived);
    QVERIFY(paid->requestKeys(scopeHex, sourceOf(p)));
    QTRY_VERIFY_WITH_TIMEOUT(keys.count() > 0, 30000);
    QTRY_VERIFY_WITH_TIMEOUT(paid->info(scopeHex)->keys, 5000);
    QCOMPARE(paid->info(scopeHex)->newestEpoch, epoch);
    const auto k = b.engine()->paidKey(*scope, epoch);
    QVERIFY(k && k->has_value());

    // the IPC lists it (price, state, until)
    Ed3kPaidBridge::setPaid(paid.get());
    const QCborMap listed = Ed3kPaidBridge::paidToCbor();
    const QCborMap row = listed.value(QStringLiteral("subs")).toArray().at(0).toMap();
    QCOMPARE(row.value(QStringLiteral("price")).toString(), QStringLiteral("4.99 EUR / month"));
    QCOMPARE(row.value(QStringLiteral("state")).toInteger(), 2);
    QCOMPARE(row.value(QStringLiteral("payHost")).toString(), QStringLiteral("127.0.0.1"));
    QVERIFY(listed.value(QStringLiteral("reports")).toBool());
    paid->setReports(false);
    QVERIFY(!Ed3kPaidBridge::paidToCbor().value(QStringLiteral("reports")).toBool());
    Ed3kPaidBridge::setPaid(nullptr);

    // a restart: the pass is set again from paid.json and the holder key file
    paid.reset();
    b.stop();
    QVERIFY(b.start(loopback(db.path())));
    paid = std::make_unique<Ed3kPaid>(b, db.path());
    info = paid->info(scopeHex);
    QVERIFY(info);
    QCOMPARE(info->state, Ed3kPaid::State::Held);
    QCOMPARE(info->until, until);
    QVERIFY(!paid->reports());
    QVERIFY(paid->holdsPass(*scope));
    QSignalSpy keys2(paid.get(), &Ed3kPaid::keysArrived);
    QVERIFY(paid->requestKeys(scopeHex, sourceOf(p)));
    QTRY_VERIFY_WITH_TIMEOUT(keys2.count() > 0, 30000);
    paid.reset();
    b.stop();
    p.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kPaid)
#include "tst_Ed3kPaid.moc"
