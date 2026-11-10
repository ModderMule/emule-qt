/// @file tst_Ed3kPaid.cpp
/// @brief Paid subscriptions, the buyer's side (src/ed3k/Ed3kPaid, subscription.md §5, §6, §9):
/// a publisher node P holds a season and its own pass; the buyer B follows the scope with its
/// offer, buys (the pay link), the core polls a fake seller (HTTPS on loopback: 404, then 200
/// with a pass for B's holder key), the pass is set, B asks P for keys (PAID_KEYS). The
/// holder key stays owner-only, the pass is kept across a restart; the IPC lists it.
///
/// The test signs as the publisher (OpenSSL Ed25519) and derives the chain's anchor with a
/// small BLAKE3 derive_key (single block: every input here is under 64 bytes).
///
/// Batch 14: `ed3k:pass` links by hand and vouchers (§9.3: a voucher taken once, also after a
/// restart), a pass of several scopes held back for the user's word (§5.1, ruling #10: declined
/// kept unused, accepted used; a bought one too), a paid live channel followed while watched (§7).
///
/// Batch 15: #9 every accepted set of scopes kept (a subset asks nothing, a new combination
/// asks), #10 a pass checked per scope (not checked before the issuer's offer, failing where
/// the signer may not sign), #11 a pass by hand not renewed by itself, "Renew now" polls.

#include "Ed3kLive.h"
#include "Ed3kNames.h"
#include "Ed3kPaid.h"
#include "Ed3kPaidBridge.h"
#include "Ed3kPlumbing.h"
#include "Ed3kProto.h"
#include "Ed3kSession.h"
#include "Ed3kSiteServer.h"
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

/// a pass of several scopes (§5: 1 to 16)
QByteArray makePassOf(const Signer& pub, const QList<QByteArray>& scopes, const QByteArray& holder, std::uint64_t issued, std::uint64_t until)
{
    Ed3kProtoWriter w;
    for (const QByteArray& sc : scopes)
        w.bytes(1, sc);
    const QByteArray pass = w.bytes(2, holder).varint(3, until).varint(5, issued).data();
    return Ed3kProtoWriter().bytes(1, pass).bytes(2, pub.publicKey()).bytes(3, pub.signature(QByteArray("ed3k v1 paid pass") + pass)).data();
}

/// an offer of @p scope signed by @p pub naming @p issuers (§3, §5: they may sign its passes)
QByteArray offerWith(const Signer& pub, const ::ed3k::Scope& scope, std::uint64_t now, const std::vector<::ed3k::Key32>& issuers)
{
    ::ed3k::Key32 seasonSeed{};
    seasonSeed.fill(0x5B);
    const auto chain = ::ed3k::paidChainNew(pub.seed, scope, 0, 24, seasonSeed);
    if (!chain)
        return {};
    ::ed3k::PaidOfferFields f;
    f.scope = scope;
    f.issued = now - 60;
    f.t0 = now - 3600;
    f.period = 3600;
    f.chain.assign(chain->bytes().begin(), chain->bytes().end());
    f.issuers = issuers;
    f.price = "2 EUR";
    f.payUrl = "https://shop.example/pay";
    f.passUrl = "https://shop.example/pass";
    const auto offer = ::ed3k::paidOfferSign(pub.seed, f);
    return offer ? QByteArray(reinterpret_cast<const char*>(offer->bytes().data()), qsizetype(offer->bytes().size())) : QByteArray();
}

/// a pass of @p scopes signed by @p signer (the publisher or an issuer)
QByteArray passBy(const Signer& signer, const std::vector<::ed3k::Scope>& scopes, const ::ed3k::Key32& holder, std::uint64_t issued,
                  std::uint64_t until)
{
    const auto pass = ::ed3k::paidSignPass(signer.seed, scopes, holder, issued, until, 0);
    return pass ? QByteArray(reinterpret_cast<const char*>(pass->bytes().data()), qsizetype(pass->bytes().size())) : QByteArray();
}

QString passLinkOf(const QByteArray& pass, const std::optional<::ed3k::Key32>& seed = {})
{
    const auto link = ::ed3k::passLink(spanOf(pass), seed);
    return link ? QString::fromStdString(*link) : QString();
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
    void aPassLinkAndAVoucherTakenOnce();
    void aPassOfSeveralScopesWaitsForTheUser();
    void aPaidChannelIsFollowedWhileWatched();
    void aPassIsCheckedPerScopeAndAskedOncePerSet();
    void aPassByHandIsRenewedOnlyOnRequest();
    void anAnonymousScopeTakesItsKeyedEvents();
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

// §9.3: a pass by hand (holder key ours) and a voucher (k: its key stored like a bought one,
// the pass taken once, also after a restart); the keys come with it
void tst_Ed3kPaid::aPassLinkAndAVoucherTakenOnce()
{
    QTemporaryDir dp, db;
    Ed3kSession p, b;
    QVERIFY(p.start(loopback(dp.path())));
    QVERIFY(b.start(loopback(db.path())));
    Signer pub;
    pub.seed.fill(0x32);
    const auto scope = ::ed3k::paidScope(3, pub.key(), {});
    QVERIFY(scope);
    const QByteArray scopeRaw = raw(scope->bytes);
    const std::uint64_t now = p.now().unixS;
    const std::uint32_t period = 3600;
    const std::uint64_t t0 = now - 10 * period;
    const std::uint64_t epoch = (now - t0) / period;
    const Season season = makeSeason(pub, scopeRaw, epoch, 24);
    const std::uint64_t until = now + 7 * 86400;
    const QByteArray offer = makeOffer(pub, season, now - 60, t0, period, QStringLiteral("https://shop.example/pay"),
                                       QStringLiteral("https://shop.example/pass"));
    QVERIFY(p.engine()->paidFollow(pub.key(), *scope));
    QVERIFY(p.engine()->paidSetOffer(p.now(), *scope, spanOf(offer)));
    QVERIFY(p.engine()->paidPublish(*scope, spanOf(season.signedChain), season.seed));
    Signer pHolder;
    pHolder.seed.fill(0x42);
    QVERIFY(p.engine()->paidSetPass(p.now(), *scope, spanOf(makePass(pub, scopeRaw, pHolder.publicKey(), now - 60, until)), pHolder.seed));
    p.pump();

    auto paid = std::make_unique<Ed3kPaid>(b, db.path());
    paid->follow(pub.key(), *scope, Ed3kPaid::Kind::HiddenSet, QStringLiteral("Gift"));
    QVERIFY(paid->setOffer(*scope, offer));
    const QString scopeHex = Ed3kPaid::hexOf(*scope);

    // refused: no pass link; a pass for a key not held here
    QVERIFY(!paid->addPassLink(QStringLiteral("ed3k:pass?p=nothing")));
    Signer stranger;
    stranger.seed.fill(0x61);
    const QByteArray strangers = makePass(pub, scopeRaw, stranger.publicKey(), now - 60, until);
    QVERIFY(!paid->addPassLink(passLinkOf(strangers)));
    QVERIFY(!paid->holdsPass(*scope));

    // a voucher: whoever has it holds the pass
    Signer voucher;
    voucher.seed.fill(0x71);
    const QByteArray pass = makePass(pub, scopeRaw, voucher.publicKey(), now - 60, until);
    const QString link = passLinkOf(pass, voucher.seed);
    QVERIFY(link.startsWith(QStringLiteral("ed3k:pass?p=")) && link.contains(QStringLiteral("&k=")));
    QSignalSpy arrived(paid.get(), &Ed3kPaid::passArrived);
    const auto taken = paid->addPassLink(link);
    QVERIFY2(taken, taken ? "" : taken.error().toUtf8().constData());
    QCOMPARE(taken->scope, scopeHex);
    QVERIFY(taken->voucher);
    QVERIFY(!taken->waits);
    QCOMPARE(arrived.count(), 1);
    auto info = paid->info(scopeHex);
    QCOMPARE(info->state, Ed3kPaid::State::Held);
    QCOMPARE(info->until, until);
    QVERIFY(!info->renewing);   // a pass by hand: no pass_url polls (§9.2 is the shop's)
    QVERIFY(paid->holdsPass(*scope));
    // its key: a Secret file like a bought one; neither the link nor the key in paid.json
    const QString holderFile = QDir(stateDir(db.path())).filePath(QStringLiteral("paid/%1.holder").arg(scopeHex));
    QCOMPARE(readBlob(holderFile), raw(voucher.seed));
#ifndef Q_OS_WIN
    QCOMPARE(QFile::permissions(holderFile) & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions{});
#endif
    const QByteArray json = readBlob(paid->statePath());
    QVERIFY(!json.contains(raw(voucher.seed).toHex()) && !json.contains(raw(voucher.seed).toBase64()) && !json.contains(link.toLatin1()));

    // taken once (§9.3)
    QVERIFY(!paid->addPassLink(link));
    // the same pass without k: the holder key is ours now, that is no second person
    QVERIFY(paid->addPassLink(passLinkOf(pass)));

    // the keys (§6.1)
    QSignalSpy keys(paid.get(), &Ed3kPaid::keysArrived);
    QVERIFY(paid->requestKeys(scopeHex, sourceOf(p)));
    QTRY_VERIFY_WITH_TIMEOUT(keys.count() > 0, 30000);
    QCOMPARE(paid->info(scopeHex)->newestEpoch, epoch);

    // over IPC, after a restart: still taken once
    paid.reset();
    b.stop();
    QVERIFY(b.start(loopback(db.path())));
    paid = std::make_unique<Ed3kPaid>(b, db.path());
    QVERIFY(paid->holdsPass(*scope));
    Ed3kPaidBridge::setPaid(paid.get());
    std::vector<Ipc::IpcMessage> replies;
    const auto reply = [&](const Ipc::IpcMessage& m) { replies.push_back(m); };
    Ipc::IpcMessage again(Ipc::IpcMsgType::AddEd3kPassLink, 7);
    again.append(link);
    QVERIFY(Ed3kPaidBridge::handleRequest(again, reply));
    QVERIFY(!replies.back().fieldBool(0));
    QVERIFY(!replies.back().fieldMap(1).value(QStringLiteral("error")).toString().isEmpty());
    Ed3kPaidBridge::setPaid(nullptr);
    paid.reset();
    b.stop();
    p.stop();
}

// §5.1, ruling #10: a pass of more than one scope is not shown before the user's word; no:
// kept unused (also across a restart), yes: used for every scope; a bought one asks too
void tst_Ed3kPaid::aPassOfSeveralScopesWaitsForTheUser()
{
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Signer pub;
    pub.seed.fill(0x33);
    const QByteArray idA("a"), idB("b"), idC("c"), idD("d");
    const auto scopeA = ::ed3k::paidScope(2, pub.key(), spanOf(idA));
    const auto scopeB = ::ed3k::paidScope(2, pub.key(), spanOf(idB));
    const auto scopeC = ::ed3k::paidScope(2, pub.key(), spanOf(idC));
    const auto scopeD = ::ed3k::paidScope(2, pub.key(), spanOf(idD));
    QVERIFY(scopeA && scopeB && scopeC && scopeD);
    const std::uint64_t now = b.now().unixS;
    const std::uint64_t until = now + 7 * 86400;

    auto paid = std::make_unique<Ed3kPaid>(b, db.path());
    paid->follow(pub.key(), *scopeA, Ed3kPaid::Kind::Set, QStringLiteral("Season A"));
    const QString hexA = Ed3kPaid::hexOf(*scopeA);
    const QString hexB = Ed3kPaid::hexOf(*scopeB);

    Signer voucher;
    voucher.seed.fill(0x72);
    const QByteArray pass = makePassOf(pub, {raw(scopeA->bytes), raw(scopeB->bytes)}, voucher.publicKey(), now - 60, until);
    QSignalSpy arrived(paid.get(), &Ed3kPaid::passArrived);
    const auto taken = paid->addPassLink(passLinkOf(pass, voucher.seed));
    QVERIFY2(taken, taken ? "" : taken.error().toUtf8().constData());
    QCOMPARE(taken->scope, hexA);   // the followed one
    QCOMPARE(taken->scopes, 2);
    QVERIFY(taken->waits);
    QCOMPARE(arrived.count(), 0);
    QCOMPARE(paid->info(hexA)->state, Ed3kPaid::State::ScopesWarn);
    QCOMPARE(paid->info(hexA)->passScopes, 2);
    QVERIFY(!paid->holdsPass(*scopeA));
    // the other scope waits too, for its publisher (not followed yet)
    QCOMPARE(paid->info(hexB)->state, Ed3kPaid::State::ScopesWarn);

    // no: kept unused, also after a restart
    QVERIFY(paid->answerScopes(hexA, false));
    QCOMPARE(paid->info(hexA)->state, Ed3kPaid::State::ScopesWarn);
    QVERIFY(paid->info(hexA)->scopesDeclined);
    QVERIFY(!paid->holdsPass(*scopeA) && !paid->holdsPass(*scopeB));
    paid.reset();
    paid = std::make_unique<Ed3kPaid>(b, db.path());
    QCOMPARE(paid->info(hexA)->state, Ed3kPaid::State::ScopesWarn);
    QVERIFY(paid->info(hexA)->scopesDeclined);
    QVERIFY(!paid->holdsPass(*scopeA));

    // asked again from the UI: yes, used for both
    QSignalSpy arrived2(paid.get(), &Ed3kPaid::passArrived);
    QVERIFY(paid->answerScopes(hexA, true));
    QCOMPARE(arrived2.count(), 1);
    QCOMPARE(paid->info(hexA)->state, Ed3kPaid::State::Held);
    QVERIFY(paid->holdsPass(*scopeA) && paid->holdsPass(*scopeB));
    QVERIFY(!paid->answerScopes(hexA, true));   // nothing waits now
    // a later pass of the same scopes asks no more
    const QByteArray later = makePassOf(pub, {raw(scopeA->bytes), raw(scopeB->bytes)}, voucher.publicKey(), now - 30, until + 86400);
    const auto renewed = paid->addPassLink(passLinkOf(later));
    QVERIFY(renewed && !renewed->waits);
    QCOMPARE(paid->info(hexA)->until, until + 86400);
    // B followed later: its pass is set then
    paid->follow(pub.key(), *scopeB, Ed3kPaid::Kind::Set, QStringLiteral("Season B"));
    QCOMPARE(paid->info(hexB)->state, Ed3kPaid::State::Held);

    // bought: the seller's pass covers C and D: the user is asked first (scopesWarning)
    FakeSeller seller([&](const QByteArray& holder) { return makePassOf(pub, {raw(scopeC->bytes), raw(scopeD->bytes)}, holder, now - 60, until); });
    QVERIFY(seller.port() > 0);
    const QString base = QStringLiteral("https://127.0.0.1:%1").arg(seller.port());
    const Season season = makeSeason(pub, raw(scopeC->bytes), 0, 24);
    const QByteArray offerC = makeOffer(pub, season, now - 60, now - 3600, 3600, base + QStringLiteral("/pay"), base + QStringLiteral("/pass"));
    paid->follow(pub.key(), *scopeC, Ed3kPaid::Kind::Set, QStringLiteral("Season C"));
    QVERIFY(paid->setOffer(*scopeC, offerC));
    const QString hexC = Ed3kPaid::hexOf(*scopeC);
    QSignalSpy warned(paid.get(), &Ed3kPaid::scopesWarning);
    QVERIFY(paid->buy(hexC));
    QTRY_VERIFY_WITH_TIMEOUT(warned.count() == 1, 60000);
    QCOMPARE(warned.at(0).at(0).toString(), hexC);
    QCOMPARE(warned.at(0).at(2).toInt(), 1);
    QCOMPARE(paid->info(hexC)->state, Ed3kPaid::State::ScopesWarn);
    QVERIFY(!paid->holdsPass(*scopeC));
    QVERIFY(paid->answerScopes(hexC, true));
    QCOMPARE(paid->info(hexC)->state, Ed3kPaid::State::Held);
    QVERIFY(paid->holdsPass(*scopeC) && paid->holdsPass(*scopeD));
    paid.reset();
    b.stop();
}

// §7: a paid ed3k:live link watched is followed in Ed3kPaid (kind 1) while watched; its pass
// comes like a set's
void tst_Ed3kPaid::aPaidChannelIsFollowedWhileWatched()
{
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Signer channel;
    channel.seed.fill(0x34);
    const QByteArray id("tv");
    const auto scope = ::ed3k::paidScope(1, channel.key(), spanOf(id));
    QVERIFY(scope);
    const QString link = QStringLiteral("ed3k:live?key=%1&id=%2&paid=1&dn=Paid%20TV")
                             .arg(base32(channel.key()), base32(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(id.constData()), 2)));
    const auto parsed = Ed3kPaid::channelOfLink(link);
    QVERIFY(parsed);
    QCOMPARE(parsed->scope, *scope);
    QVERIFY(!Ed3kPaid::channelOfLink(QString(link).remove(QStringLiteral("&paid=1"))));

    auto paid = std::make_unique<Ed3kPaid>(b, db.path());
    Ed3kNames names(b, db.path());
    names.start();
    Ed3kSiteServer sites(b, names);
    Ed3kLive live(b, sites);
    live.setPaid(paid.get());
    const QString hex = Ed3kPaid::hexOf(*scope);
    const auto watched = live.watch(link);
    QVERIFY2(watched, watched ? "" : watched.error().toUtf8().constData());
    auto info = paid->info(hex);
    QVERIFY(info);
    QCOMPARE(info->kind, Ed3kPaid::Kind::Channel);
    QCOMPARE(info->title, QStringLiteral("Paid TV"));
    QCOMPARE(live.info(*watched)->paidScope, hex);
    // followed on the engine: an offer of the channel is taken
    const std::uint64_t now = b.now().unixS;
    const Season season = makeSeason(channel, raw(scope->bytes), 0, 24);
    const QByteArray offer = makeOffer(channel, season, now - 60, now - 3600, 3600, QStringLiteral("https://shop.example/pay"),
                                       QStringLiteral("https://shop.example/pass"));
    QVERIFY(paid->setOffer(*scope, offer));
    QVERIFY(b.engine()->paidOffer(*scope).value_or(std::nullopt).has_value());
    // a voucher for it
    Signer voucher;
    voucher.seed.fill(0x73);
    QVERIFY(paid->addPassLink(passLinkOf(makePass(channel, raw(scope->bytes), voucher.publicKey(), now - 60, now + 86400), voucher.seed)));
    QCOMPARE(paid->info(hex)->state, Ed3kPaid::State::Held);
    // left: the pass stays (its key), the channel is no longer followed
    QVERIFY(live.leave(*watched));
    QVERIFY(paid->holdsPass(*scope));
    QVERIFY(!b.engine()->paidOffer(*scope).value_or(std::nullopt).has_value());   // not on the engine now
    live.setPaid(nullptr);
    paid.reset();
    b.stop();
}

// #10 (§9.3): a pass is checked per scope once its publisher and, issuer-signed, its offer are
// known: not checked before (no keys, no renewal), held after the offer; failing where the
// signer is no issuer. #9 (§5.1): asked once per set of scopes, a subset of one asks nothing
// (also after a restart), a new combination asks.
void tst_Ed3kPaid::aPassIsCheckedPerScopeAndAskedOncePerSet()
{
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Signer pubA, pubB, issuer, voucher;
    pubA.seed.fill(0x35);
    pubB.seed.fill(0x36);
    issuer.seed.fill(0x37);
    voucher.seed.fill(0x74);
    const QByteArray idA("a"), idB("b"), idC("c"), idD("d");
    const auto scopeA = ::ed3k::paidScope(2, pubA.key(), spanOf(idA));
    const auto scopeB = ::ed3k::paidScope(2, pubB.key(), spanOf(idB));
    const auto scopeC = ::ed3k::paidScope(2, pubA.key(), spanOf(idC));
    const auto scopeD = ::ed3k::paidScope(2, pubA.key(), spanOf(idD));
    QVERIFY(scopeA && scopeB && scopeC && scopeD);
    const QString hexA = Ed3kPaid::hexOf(*scopeA), hexB = Ed3kPaid::hexOf(*scopeB), hexC = Ed3kPaid::hexOf(*scopeC);
    const std::uint64_t now = b.now().unixS;
    const std::uint64_t until = now + 7 * 86400;

    auto paid = std::make_unique<Ed3kPaid>(b, db.path());
    paid->follow(pubA.key(), *scopeA, Ed3kPaid::Kind::Set, QStringLiteral("A"));   // no offer yet
    paid->follow(pubB.key(), *scopeB, Ed3kPaid::Kind::Set, QStringLiteral("B"));
    QVERIFY(paid->setOffer(*scopeB, offerWith(pubB, *scopeB, now, {})));      // the issuer is none of B's

    // one purchase, three scopes: signed by the issuer (§5); the user takes it
    const QByteArray pass = passBy(issuer, {*scopeA, *scopeB, *scopeC}, voucher.key(), now - 60, until);
    QVERIFY(!pass.isEmpty());
    const auto taken = paid->addPassLink(passLinkOf(pass, voucher.seed));
    QVERIFY2(taken, taken ? "" : taken.error().toUtf8().constData());
    QVERIFY(taken->waits);
    QVERIFY(paid->answerScopes(hexA, true));
    // A: the issuer's offer is not known: held, not checked, not used (no renewal)
    auto a = paid->info(hexA);
    QCOMPARE(a->state, Ed3kPaid::State::PassUnchecked);
    QVERIFY(!a->renewing);
    QVERIFY(a->byHand);
    QVERIFY(paid->holdsPass(*scopeA));
    // B: the signer may not sign for it: not used there
    QCOMPARE(paid->info(hexB)->state, Ed3kPaid::State::PassFailed);
    QVERIFY(!paid->holdsPass(*scopeB));
    // C: its publisher not known yet (not followed)
    QCOMPARE(paid->info(hexC)->state, Ed3kPaid::State::PassUnchecked);
    // A's offer names the issuer: the pass is set again, checked now
    QVERIFY(paid->setOffer(*scopeA, offerWith(pubA, *scopeA, now, {issuer.key()})));
    QCOMPARE(paid->info(hexA)->state, Ed3kPaid::State::Held);
    QVERIFY(paid->holdsPass(*scopeA));

    // #9: a subset of the set taken asks nothing; a new combination asks
    const auto subset = paid->addPassLink(passLinkOf(passBy(issuer, {*scopeA, *scopeC}, voucher.key(), now - 50, until + 60)));
    QVERIFY2(subset, subset ? "" : subset.error().toUtf8().constData());
    QVERIFY(!subset->waits);
    QCOMPARE(paid->info(hexA)->until, until + 60);
    const auto other = paid->addPassLink(passLinkOf(passBy(issuer, {*scopeA, *scopeD}, voucher.key(), now - 40, until + 120)));
    QVERIFY(other && other->waits);
    QVERIFY(paid->answerScopes(hexA, false));
    // kept in paid.json for every subscription: after a restart {B, C} asks nothing either
    paid.reset();
    paid = std::make_unique<Ed3kPaid>(b, db.path());
    QVERIFY(readBlob(paid->statePath()).contains("scopeSets"));
    const auto again = paid->addPassLink(passLinkOf(passBy(issuer, {*scopeB, *scopeC}, voucher.key(), now - 30, until + 180)));
    QVERIFY(again && !again->waits);
    paid.reset();
    b.stop();
}

// #11 (§9.2): a pass from an ed3k:pass link or voucher is polled at pass_url only when the
// user asks (polling shows an address to the seller); "Renew now" polls and takes the new one
void tst_Ed3kPaid::aPassByHandIsRenewedOnlyOnRequest()
{
    QTemporaryDir db;
    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    Signer pub, voucher;
    pub.seed.fill(0x38);
    voucher.seed.fill(0x75);
    const QByteArray id("r");
    const auto scope = ::ed3k::paidScope(2, pub.key(), spanOf(id));
    QVERIFY(scope);
    const QByteArray scopeRaw = raw(scope->bytes);
    const QString hex = Ed3kPaid::hexOf(*scope);
    const std::uint64_t now = b.now().unixS;
    const std::uint64_t renewed = now + 20 * 86400;
    FakeSeller seller([&](const QByteArray& holder) { return makePass(pub, scopeRaw, holder, now - 30, renewed); });
    QVERIFY(seller.port() > 0);
    const QString base = QStringLiteral("https://127.0.0.1:%1").arg(seller.port());
    const Season season = makeSeason(pub, scopeRaw, 0, 24);
    const QByteArray offer = makeOffer(pub, season, now - 60, now - 3600, 3600, base + QStringLiteral("/pay"), base + QStringLiteral("/pass"));

    auto paid = std::make_unique<Ed3kPaid>(b, db.path());
    paid->follow(pub.key(), *scope, Ed3kPaid::Kind::Set, QStringLiteral("Renewable"));
    QVERIFY(paid->setOffer(*scope, offer));
    // within its last 48 h: a bought pass would be polled now (§9.2)
    QVERIFY(paid->addPassLink(passLinkOf(makePass(pub, scopeRaw, voucher.publicKey(), now - 60, now + 86400), voucher.seed)));
    auto info = paid->info(hex);
    QCOMPARE(info->state, Ed3kPaid::State::Held);
    QVERIFY(info->byHand && info->renewable && !info->renewing);
    QTest::qWait(1500);
    QVERIFY(seller.requests.isEmpty());
    // nor after a restart
    paid.reset();
    paid = std::make_unique<Ed3kPaid>(b, db.path());
    QVERIFY(paid->info(hex)->byHand);
    QTest::qWait(1000);
    QVERIFY(seller.requests.isEmpty());

    // the user asks: pass_url polled (404 first, then the pass), a pass by hand still
    QSignalSpy arrived(paid.get(), &Ed3kPaid::passArrived);
    QVERIFY(paid->renewNow(hex));
    QVERIFY(paid->info(hex)->renewing);
    QTRY_VERIFY_WITH_TIMEOUT(arrived.count() > 0, 60000);
    QVERIFY(!seller.requests.isEmpty());
    info = paid->info(hex);
    QCOMPARE(info->until, renewed);
    QCOMPARE(info->state, Ed3kPaid::State::Held);
    QVERIFY(info->byHand);
    // over IPC: BuyEd3kPass with renew
    Ed3kPaidBridge::setPaid(paid.get());
    Ipc::IpcMessage req(Ipc::IpcMsgType::BuyEd3kPass, 9);
    req.append(hex);
    req.append(false);
    req.append(true);
    bool ok = false;
    QVERIFY(Ed3kPaidBridge::handleRequest(req, [&](const Ipc::IpcMessage& r) { ok = r.fieldBool(0); }));
    QVERIFY(ok);
    Ed3kPaidBridge::setPaid(nullptr);
    paid.reset();
    b.stop();
}

// hops.md §1: a scope bought for the anonymous key runs on the anonymous node: its Paid*
// events come keyed (the node key's are not its own), unfollow goes there too
void tst_Ed3kPaid::anAnonymousScopeTakesItsKeyedEvents()
{
    QTemporaryDir db;
    Ed3kSession b;
    Ed3kHideSettings hide;
    hide.on = true;
    b.setHideSettings(hide);
    QVERIFY(b.start(loopback(db.path())));
    QVERIFY(b.hide().hiding());

    Signer pub;
    pub.seed.fill(0x35);
    const auto scope = ::ed3k::paidScope(3, pub.key(), {});
    QVERIFY(scope);
    const QByteArray scopeRaw = raw(scope->bytes);
    const std::uint64_t now = b.now().unixS;
    const std::uint32_t period = 3600;
    const std::uint64_t t0 = now - 10 * period;
    const Season season = makeSeason(pub, scopeRaw, (now - t0) / period, 24);
    const QByteArray offer = makeOffer(pub, season, now - 60, t0, period, QStringLiteral("https://127.0.0.1:1/pay"),
                                       QStringLiteral("https://127.0.0.1:1/pass"));

    auto paid = std::make_unique<Ed3kPaid>(b, db.path());
    paid->follow(pub.key(), *scope, Ed3kPaid::Kind::HiddenSet, QStringLiteral("Anon Pass"));
    QVERIFY(paid->setOffer(*scope, offer));
    const QString scopeHex = Ed3kPaid::hexOf(*scope);
    (void)paid->buy(scopeHex, true);   // waits for the user's word (§5.1)
    QVERIFY(paid->info(scopeHex)->anon);
    // followed on the anonymous node, with its offer
    auto held = b.engine()->paidOffer(b.now(), *scope, ::ed3k::KeyKind::Anon);
    QVERIFY(held && *held);

    QSignalSpy keys(paid.get(), &Ed3kPaid::keysArrived);
    const ::ed3k::Event ev{::ed3k::event::PaidKeys{*scope, *b.nodeId(), {5}}};
    bool handled = false;
    emit b.eventPolled(ev, &handled);   // the node key's: not this scope's
    QVERIFY(!handled);
    QCOMPARE(keys.count(), 0);
    emit b.keyedEventPolled(ev, ::ed3k::KeyKind::Anon, &handled);
    QVERIFY(handled);
    QCOMPARE(keys.count(), 1);

    // unfollowed on the anonymous node (was: skipped)
    paid->unfollow(*scope);
    held = b.engine()->paidOffer(b.now(), *scope, ::ed3k::KeyKind::Anon);
    QVERIFY(!held || !*held);
    b.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kPaid)
#include "tst_Ed3kPaid.moc"
