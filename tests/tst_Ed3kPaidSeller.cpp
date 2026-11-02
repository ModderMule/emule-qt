/// @file tst_Ed3kPaidSeller.cpp
/// @brief Paid subscriptions, the seller's side (src/ed3k/Ed3kPaidSeller, subscription.md §3,
/// §4, §5, §7, §9.3): a seller node S sells a set (its offer for FileSet.offer, a season, its
/// own pass), seals a member with the epoch's k_data and issues a voucher; a buyer node B on
/// loopback takes the `ed3k:pass` link, gets the keys from S and opens the sealed member. Also
/// a pass for a buyer's key, a season after a leak, a planned season, a paid broadcast, the
/// owner-only files, a restart and the IPC.
///
/// Batch 15: a private paid channel sold (its link with `s`, the buyer with that link and a
/// pass gets the keys); stopping a sale (an offer without price and shop, t0/period kept) and
/// selling it again.

#include "Ed3kLiveBroadcast.h"
#include "Ed3kPaid.h"
#include "Ed3kPaidSeller.h"
#include "Ed3kPaidSellerBridge.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "IpcMessage.h"

#include <QCborArray>
#include <QDir>
#include <QUrlQuery>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>

using namespace eMule;
using namespace eMule::ed3k;

namespace {

std::span<const std::byte> spanOf(const QByteArray& b)
{
    return {reinterpret_cast<const std::byte*>(b.constData()), std::size_t(b.size())};
}

QByteArray raw(std::span<const std::uint8_t> b)
{
    return QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size()));
}

::ed3k::Key32 key32(const QString& hexText)
{
    ::ed3k::Key32 k{};
    const QByteArray b = QByteArray::fromHex(hexText.toLatin1());
    if (b.size() == 32)
        std::memcpy(k.data(), b.constData(), k.size());
    return k;
}

Ed3kSettings loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");
    s.forceOpen = true;
    return s;
}

::ed3k::Source sourceOf(const Ed3kSession& s)
{
    return ::ed3k::Source{*s.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, s.udpPort()), std::nullopt, {}};
}

Ed3kPaidSeller::Terms shopTerms()
{
    Ed3kPaidSeller::Terms t;
    t.price = QStringLiteral("3 EUR / month");
    t.terms = QStringLiteral("Personal use.");
    t.devices = 2;
    return t;
}

/// nonce(12) ‖ ChaCha20(k_data, nonce, plain) (§8.1)
QByteArray seal(const ::ed3k::Key32& kData, const QByteArray& plain)
{
    std::array<std::uint8_t, 12> nonce{};
    for (std::size_t i = 0; i < nonce.size(); ++i)
        nonce[i] = std::uint8_t(0xA0 + i);
    QByteArray body = plain;
    if (!::ed3k::sealedReadAt(kData, nonce, 0, {reinterpret_cast<std::byte*>(body.data()), std::size_t(body.size())}))
        return {};
    return raw(nonce) + body;
}

QByteArray open(const ::ed3k::Key32& kData, const QByteArray& sealed)
{
    std::array<std::uint8_t, 12> nonce{};
    std::memcpy(nonce.data(), sealed.constData(), nonce.size());
    QByteArray body = sealed.mid(12);
    if (!::ed3k::sealedReadAt(kData, nonce, 0, {reinterpret_cast<std::byte*>(body.data()), std::size_t(body.size())}))
        return {};
    return body;
}

bool ownerOnly(const QString& path)
{
#ifdef Q_OS_WIN
    Q_UNUSED(path);
    return true;
#else
    return (QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::ReadOther)) == QFileDevice::Permissions{};
#endif
}

} // namespace

class tst_Ed3kPaidSeller : public QObject {
    Q_OBJECT

private slots:
    void holderKeysAndTermsAreChecked();
    void aSetSoldAVoucherTakenKeysTheMemberOpens();
    void seasonsAndARestart();
    void aBroadcastIsSoldAndGoesOnPaid();
    void aPrivateChannelIsSoldAndAPassHolderGetsKeys();
    void aSaleIsStoppedAndSoldAgain();
    void cleanup() { QTest::qWait(10); }
};

void tst_Ed3kPaidSeller::holderKeysAndTermsAreChecked()
{
    ::ed3k::Key32 k{};
    for (std::size_t i = 0; i < k.size(); ++i)
        k[i] = std::uint8_t(i);
    QVERIFY(Ed3kPaidSeller::parseHolder(QString::fromLatin1(raw(k).toHex())) == std::optional(k));
    QVERIFY(Ed3kPaidSeller::parseHolder(base32(k)) == std::optional(k));
    QVERIFY(Ed3kPaidSeller::parseHolder(QStringLiteral("  ") + base32(k).toUpper() + QStringLiteral(" ")) == std::optional(k));
    QVERIFY(!Ed3kPaidSeller::parseHolder(QStringLiteral("abc")));
    QVERIFY(!Ed3kPaidSeller::parseHolder(QString::fromLatin1(raw(k).left(31).toHex())));

    QTemporaryDir d;
    Ed3kSession s;
    QVERIFY(s.start(loopback(d.path())));
    Ed3kPaidSeller seller(s, d.path());
    Ed3kPaidSeller::Terms t = shopTerms();
    t.price.clear();
    QVERIFY(!seller.newSet(QStringLiteral("x"), t));                 // a price is needed
    t = shopTerms();
    t.payUrl = QStringLiteral("http://shop.example/pay");             // §3: https only
    t.passUrl = QStringLiteral("https://shop.example/pass");
    QVERIFY(!seller.newSet(QStringLiteral("x"), t));
    t.payUrl = QStringLiteral("https://user:pw@shop.example/pay");    // no user name
    QVERIFY(!seller.newSet(QStringLiteral("x"), t));
    t.payUrl = QStringLiteral("https://shop.example/pay");
    t.passUrl.clear();                                                // both or none
    QVERIFY(!seller.newSet(QStringLiteral("x"), t));
    t = shopTerms();
    t.devices = 64;                                                   // §5.3: 1 to 63
    QVERIFY(!seller.newSet(QStringLiteral("x"), t));
    QVERIFY(seller.list().empty());
    s.stop();
}

void tst_Ed3kPaidSeller::aSetSoldAVoucherTakenKeysTheMemberOpens()
{
    QTemporaryDir ds, db;
    Ed3kSession s, b;
    QVERIFY(s.start(loopback(ds.path())));
    QVERIFY(b.start(loopback(db.path())));

    // S sells a catalogue of its own (scope kind 2, §8.2)
    Ed3kPaidSeller seller(s, ds.path());
    Ed3kPaidSeller::Terms terms = shopTerms();
    terms.payUrl = QStringLiteral("https://shop.example/pay");
    terms.passUrl = QStringLiteral("https://shop.example/pass");
    const auto scopeHex = seller.newSet(QStringLiteral("Paid Catalogue"), terms);
    QVERIFY2(scopeHex, scopeHex ? "" : scopeHex.error().toUtf8().constData());
    auto info = seller.info(*scopeHex);
    QVERIFY(info && info->onEngine);
    QCOMPARE(info->kind, Ed3kPaidSeller::Kind::Set);
    QCOMPARE(info->terms.period, 86400u);          // §4.2: 30 one-day epochs
    QCOMPARE(info->seasonLength, 30u);
    QCOMPARE(info->seasonBase, info->epoch);
    QVERIFY(info->link.startsWith(QStringLiteral("ed3k:set?key=")));
    QVERIFY(info->ownUntil > s.now().unixS);
    const auto scope = Ed3kPaid::scopeOf(*scopeHex);
    QVERIFY(scope);
    // the scope is the one a reader computes from the set key and FileSet.id (§2)
    const ::ed3k::Key32 publisher = key32(info->publisher);
    const QByteArray id = QByteArray::fromHex(info->target.toLatin1());
    const auto computed = ::ed3k::paidScope(2, publisher, spanOf(id));
    QVERIFY(computed && *computed == *scope);

    // the offer (for FileSet.offer): the terms, the seller key as issuer (§5)
    const QByteArray offer = seller.offerOf(*scopeHex);
    const auto o = Ed3kPaidOffer::parse(offer);
    QVERIFY(o);
    QCOMPARE(o->price, terms.price);
    QCOMPARE(o->terms, terms.terms);
    QCOMPARE(o->devices, 2u);
    QCOMPARE(o->issuers, 1);
    QCOMPARE(o->period, 86400u);
    QCOMPARE(o->scope, raw(scope->bytes));
    QVERIFY(!seller.sellerKey().isEmpty());

    // a member sealed with k_data of the epoch of now (§8.1)
    const QByteArray plain = QByteArrayLiteral("episode one: the plain bytes of a sealed member");
    const auto sk = seller.sealKey(*scopeHex);
    QVERIFY(sk);
    QCOMPARE(sk->epoch, info->epoch);
    const QByteArray sealed = seal(sk->kData, plain);
    QCOMPARE(sealed.size(), plain.size() + 12);
    QVERIFY(sealed.mid(12) != plain);

    // §9.3: a voucher (the holder key in the link) and a pass for a buyer's own key
    const std::uint64_t until = s.now().unixS + 7 * 86400;
    QVERIFY(!seller.issuePass({*scopeHex}, std::nullopt, s.now().unixS - 1));   // ends before it begins
    QVERIFY(!seller.issuePass({QString(32, QLatin1Char('0'))}, std::nullopt, until));   // not sold here
    const auto voucher = seller.issuePass({*scopeHex}, std::nullopt, until, QStringLiteral("gift"));
    QVERIFY2(voucher, voucher ? "" : voucher.error().toUtf8().constData());
    QVERIFY(voucher->startsWith(QStringLiteral("ed3k:pass?p=")) && voucher->contains(QStringLiteral("&k=")));
    ::ed3k::Key32 buyerKey{};
    buyerKey.fill(0x42);
    const auto byKey = seller.issuePass({*scopeHex}, buyerKey, until);
    QVERIFY(byKey && !byKey->contains(QStringLiteral("&k=")));
    const QByteArray byKeyUtf8 = byKey->toUtf8();
    const auto parsed = ::ed3k::parsePassLink(std::string_view(byKeyUtf8.constData(), std::size_t(byKeyUtf8.size())));
    QVERIFY(parsed);
    QVERIFY(parsed->holder == buyerKey);
    QVERIFY(parsed->signer == publisher);     // one scope: the publisher signs (§5)
    QCOMPARE(parsed->until, until);
    QVERIFY(!parsed->holderSeed);
    info = seller.info(*scopeHex);
    QCOMPARE(info->issued.size(), std::size_t(2));
    QVERIFY(info->issued.at(0).voucher && info->issued.at(0).link.isEmpty());   // a credential: not kept
    QCOMPARE(info->issued.at(0).note, QStringLiteral("gift"));
    QCOMPARE(info->issued.at(1).link, *byKey);

    // the files: seeds owner-only, none of them in sell.json, no voucher link either
    const QDir sell(seller.sellDir());
    const QStringList files{*scopeHex + QStringLiteral(".publisher"), *scopeHex + QStringLiteral(".holder"),
                            *scopeHex + QStringLiteral("-%1.season").arg(info->seasonBase), QStringLiteral("seller.key")};
    for (const QString& f : files) {
        QVERIFY2(QFileInfo::exists(sell.filePath(f)), qPrintable(f));
        QCOMPARE(QFileInfo(sell.filePath(f)).size(), 32);
        QVERIFY(ownerOnly(sell.filePath(f)));
        const QByteArray seed = readBlob(sell.filePath(f));
        const QByteArray json = readBlob(seller.statePath());
        QVERIFY(!json.contains(seed.toHex()) && !json.contains(seed.toBase64()));
    }
    QVERIFY(ownerOnly(seller.statePath()));
    QVERIFY(!readBlob(seller.statePath()).contains(voucher->toUtf8()));

    // B: follows the catalogue with its offer (from the manifest), takes the voucher
    Ed3kPaid paid(b, db.path());
    paid.follow(publisher, *scope, Ed3kPaid::Kind::Set, QStringLiteral("Paid Catalogue"));
    QVERIFY(paid.setOffer(*scope, offer));
    const auto taken = paid.addPassLink(*voucher);
    QVERIFY2(taken, taken ? "" : taken.error().toUtf8().constData());
    QVERIFY(taken->voucher);
    QCOMPARE(taken->scope, *scopeHex);
    QVERIFY(paid.holdsPass(*scope));

    // keys from S (§6.1: S shows its own pass first, then B's pass, then the key)
    QSignalSpy keys(&paid, &Ed3kPaid::keysArrived);
    QVERIFY(paid.requestKeys(*scopeHex, sourceOf(s)));
    QTRY_VERIFY_WITH_TIMEOUT(keys.count() > 0, 30000);
    // the member opens with B's k_data of its epoch
    const auto kData = b.engine()->paidMemberKey(b.now(), *scope, sk->epoch);
    QVERIFY(kData && kData->has_value());
    QVERIFY(**kData == sk->kData);
    QCOMPARE(open(**kData, sealed), plain);

    // the IPC lists the sale and refuses a key that is none
    Ed3kPaidSellerBridge::setSeller(&seller, nullptr);
    const QCborMap listed = Ed3kPaidSellerBridge::sellingToCbor();
    QVERIFY(listed.value(QStringLiteral("running")).toBool());
    const QCborMap row = listed.value(QStringLiteral("sales")).toArray().at(0).toMap();
    QCOMPARE(row.value(QStringLiteral("scope")).toString(), *scopeHex);
    QCOMPARE(row.value(QStringLiteral("price")).toString(), terms.price);
    QCOMPARE(row.value(QStringLiteral("issued")).toArray().size(), 2);
    Ipc::IpcMessage req(Ipc::IpcMsgType::IssueEd3kPass, 7);
    req.append(QCborArray{*scopeHex});
    req.append(QStringLiteral("not a key"));
    req.append(qint64(until));
    req.append(QString());
    bool answered = false;
    QVERIFY(Ed3kPaidSellerBridge::handleRequest(req, [&](const Ipc::IpcMessage& r) {
        answered = true;
        QVERIFY(!r.fieldBool(0));
    }));
    QVERIFY(answered);
    Ed3kPaidSellerBridge::setSeller(nullptr, nullptr);
    b.stop();
    s.stop();
}

void tst_Ed3kPaidSeller::seasonsAndARestart()
{
    QTemporaryDir ds;
    Ed3kSession s;
    QVERIFY(s.start(loopback(ds.path())));
    auto seller = std::make_unique<Ed3kPaidSeller>(s, ds.path());
    Ed3kPaidSeller::Terms terms = shopTerms();
    terms.seasonLength = 1;   // the season's last epoch is now
    const auto scopeHex = seller->newSet(QStringLiteral("Short Seasons"), terms);
    QVERIFY(scopeHex);
    const auto first = seller->info(*scopeHex);
    QVERIFY(first && !first->planned);

    // §4.4: in a season's last epoch the next is published early, not in an offer (§3)
    seller->tick();
    auto info = seller->info(*scopeHex);
    QVERIFY(info->planned);
    QCOMPARE(info->seasonBase, first->seasonBase);
    QCOMPARE(info->offerIssued, first->offerIssued);
    const auto scope = Ed3kPaid::scopeOf(*scopeHex);
    QVERIFY(scope);
    const auto planned = s.engine()->paidKey(*scope, first->epoch + 1);
    QVERIFY(planned && planned->has_value());

    // §4.5: a new season after a leak from the next epoch, a newer offer naming it
    QSignalSpy began(seller.get(), &Ed3kPaidSeller::seasonStarted);
    const auto oldKey = s.engine()->paidKey(*scope, first->epoch);
    QVERIFY(oldKey && oldKey->has_value());
    QVERIFY(seller->newSeasonAfterLeak(*scopeHex));
    QCOMPARE(began.count(), 1);
    info = seller->info(*scopeHex);
    QCOMPARE(info->seasonBase, first->epoch + 1);
    QVERIFY(info->offerIssued > first->offerIssued);
    QVERIFY(!info->planned);   // the planned one is replaced
    const auto o = Ed3kPaidOffer::parse(seller->offerOf(*scopeHex));
    QVERIFY(o && o->issued == info->offerIssued);
    QCOMPARE(o->price, terms.price);   // the terms stay
    const auto leakKey = s.engine()->paidKey(*scope, first->epoch + 1);
    QVERIFY(leakKey && leakKey->has_value());
    QVERIFY(**leakKey != **planned);
    QVERIFY(QFileInfo::exists(QDir(seller->sellDir()).filePath(*scopeHex + QStringLiteral("-%1.season").arg(first->epoch + 1))));

    // new terms: a newer offer, same t0 and period (§3)
    Ed3kPaidSeller::Terms cheaper = terms;
    cheaper.price = QStringLiteral("2 EUR / month");
    QVERIFY(seller->setTerms(*scopeHex, cheaper));
    const auto o2 = Ed3kPaidOffer::parse(seller->offerOf(*scopeHex));
    QVERIFY(o2 && o2->issued > o->issued);
    QCOMPARE(o2->price, cheaper.price);
    QCOMPARE(o2->t0, o->t0);
    QCOMPARE(o2->period, o->period);

    // a restart: every season and the own pass on the engine again from the files
    seller.reset();
    s.stop();
    QVERIFY(s.start(loopback(ds.path())));
    seller = std::make_unique<Ed3kPaidSeller>(s, ds.path());
    info = seller->info(*scopeHex);
    QVERIFY(info && info->onEngine);
    QCOMPARE(info->terms.price, cheaper.price);
    QCOMPARE(info->seasonBase, first->epoch + 1);
    const auto again = s.engine()->paidKey(*scope, first->epoch);
    QVERIFY(again && again->has_value());
    QVERIFY(**again == **oldKey);
    const auto againLeak = s.engine()->paidKey(*scope, first->epoch + 1);
    QVERIFY(againLeak && againLeak->has_value());
    QVERIFY(**againLeak == **leakKey);
    seller.reset();
    s.stop();
}

void tst_Ed3kPaidSeller::aBroadcastIsSoldAndGoesOnPaid()
{
    QTemporaryDir ds;
    Ed3kSession s;
    QVERIFY(s.start(loopback(ds.path())));
    Ed3kPaidSeller seller(s, ds.path());
    Ed3kLiveBroadcast live(s, ds.path());
    seller.serve(live);
    const QString folder = QDir(ds.path()).filePath(QStringLiteral("hls"));
    QVERIFY(QDir().mkpath(folder));
    Ed3kLiveBroadcast::Options o;
    o.folder = folder;
    o.title = QStringLiteral("Paid Show");
    const auto on = live.start(o);
    QVERIFY2(on, on ? "" : on.error().toUtf8().constData());
    QVERIFY(!on->paid);

    const auto scopeHex = seller.sellBroadcast(live, on->id, shopTerms());
    QVERIFY2(scopeHex, scopeHex ? "" : scopeHex.error().toUtf8().constData());
    // §7: on air again, paid: `paid=1` in the link
    const auto list = live.list();
    QCOMPARE(list.size(), std::size_t(1));
    QVERIFY(list.front().paid);
    QVERIFY(list.front().link.contains(QStringLiteral("&paid=1")));
    const auto info = seller.info(*scopeHex);
    QVERIFY(info);
    QCOMPARE(info->kind, Ed3kPaidSeller::Kind::Channel);
    QCOMPARE(info->terms.period, 3600u);        // §4.2: 720 one-hour epochs
    QCOMPARE(info->seasonLength, 720u);
    QVERIFY(info->link.contains(QStringLiteral("&paid=1")));
    // the scope of the channel key and id (§2 kind 1)
    QByteArray id;
    auto seed = live.channelSeed(list.front().id, &id);
    QVERIFY(seed);
    const auto key = ::ed3k::keyPublic(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    QVERIFY(key);
    QCOMPARE(raw(*key).toHex(), info->publisher.toLatin1());
    QVERIFY(!seller.offerOf(*key, id).isEmpty());
    QCOMPARE(seller.offerOf(*key, id), seller.offerOf(*scopeHex));
    // was: a private channel is not sold (ToDo); batch 15: it is (aPrivateChannelIsSold…)
    // Ed3kLiveBroadcast::Options p = o;
    // p.title = QStringLiteral("Private Show");
    // p.isPrivate = true;
    // const auto priv = live.start(p);
    // QVERIFY(priv);
    // QVERIFY(!seller.sellBroadcast(live, priv->id, shopTerms()));
    // (void)live.stop(priv->id);
    (void)live.stop(list.front().id);
    s.stop();
}

// subscription.md §7, live.md §10: a private paid channel: its link carries `s` (a credential,
// not in sell.json), the seller follows it privately; a buyer with that link and a pass gets
// the keys (every key request proves the secret)
void tst_Ed3kPaidSeller::aPrivateChannelIsSoldAndAPassHolderGetsKeys()
{
    QTemporaryDir ds, db;
    Ed3kSession s, b;
    QVERIFY(s.start(loopback(ds.path())));
    QVERIFY(b.start(loopback(db.path())));
    Ed3kPaidSeller seller(s, ds.path());
    Ed3kLiveBroadcast live(s, ds.path());
    seller.serve(live);
    const QString folder = QDir(ds.path()).filePath(QStringLiteral("hls"));
    QVERIFY(QDir().mkpath(folder));
    Ed3kLiveBroadcast::Options o;
    o.folder = folder;
    o.title = QStringLiteral("Private Show");
    o.isPrivate = true;
    const auto on = live.start(o);
    QVERIFY2(on, on ? "" : on.error().toUtf8().constData());
    QVERIFY(on->isPrivate && !on->paid);
    const auto secret = live.channelSecret(on->id);
    QVERIFY(secret);
    const QString secret32 = base32(*secret);

    const auto scopeHex = seller.sellBroadcast(live, on->id, shopTerms());
    QVERIFY2(scopeHex, scopeHex ? "" : scopeHex.error().toUtf8().constData());
    // on air again, paid and private: the same link as the broadcast's (paid=1 and s)
    const auto list = live.list();
    QCOMPARE(list.size(), std::size_t(1));
    QVERIFY(list.front().paid && list.front().isPrivate);
    const auto info = seller.info(*scopeHex);
    QVERIFY(info && info->isPrivate && info->onEngine);
    QVERIFY(info->link.contains(QStringLiteral("&paid=1")));
    QVERIFY(info->link.contains(QStringLiteral("&s=") + secret32));
    QCOMPARE(info->link, list.front().link);
    QCOMPARE(seller.saleLink(*scopeHex), info->link);
    // the secret stays the broadcast's file: never in sell.json
    const QByteArray json = readBlob(seller.statePath());
    QVERIFY(!json.contains(secret32.toLatin1()) && !json.contains(raw(*secret).toHex()) && !json.contains(raw(*secret).toBase64()));
    QVERIFY(json.contains("\"private\":true"));

    // a voucher; the IPC gives the private link with it
    const std::uint64_t until = s.now().unixS + 7 * 86400;
    Ed3kPaidSellerBridge::setSeller(&seller, &live);
    Ipc::IpcMessage req(Ipc::IpcMsgType::IssueEd3kPass, 3);
    req.append(QCborArray{*scopeHex});
    req.append(QString());
    req.append(qint64(until));
    req.append(QStringLiteral("private gift"));
    QString voucher;
    QStringList channelLinks;
    QVERIFY(Ed3kPaidSellerBridge::handleRequest(req, [&](const Ipc::IpcMessage& r) {
        QVERIFY(r.fieldBool(0));
        voucher = r.fieldMap(1).value(QStringLiteral("link")).toString();
        for (const auto& v : r.fieldMap(1).value(QStringLiteral("channelLinks")).toArray())
            channelLinks << v.toString();
    }));
    QVERIFY(voucher.contains(QStringLiteral("&k=")));
    QCOMPARE(channelLinks, QStringList{info->link});
    Ipc::IpcMessage get(Ipc::IpcMsgType::GetEd3kSaleLink, 4);
    get.append(*scopeHex);
    QVERIFY(Ed3kPaidSellerBridge::handleRequest(get, [&](const Ipc::IpcMessage& r) {
        QVERIFY(r.fieldBool(0));
        QCOMPARE(r.fieldMap(1).value(QStringLiteral("link")).toString(), info->link);
        QVERIFY(r.fieldMap(1).value(QStringLiteral("private")).toBool());
    }));
    QVERIFY(Ed3kPaidSellerBridge::sellingToCbor().value(QStringLiteral("sales")).toArray().at(0).toMap().value(QStringLiteral("private")).toBool());
    Ed3kPaidSellerBridge::setSeller(nullptr, nullptr);

    // B: the private link (as Ed3kLive's watch gives the core: paidFollowPrivate) and the voucher
    const QUrlQuery q(info->link.mid(info->link.indexOf(QLatin1Char('?')) + 1));
    const auto key = fromBase32(q.queryItemValue(QStringLiteral("key")));
    const auto id = fromBase32(q.queryItemValue(QStringLiteral("id")));
    const auto s32 = fromBase32(q.queryItemValue(QStringLiteral("s")));
    QVERIFY(key && id && s32 && key->size() == 32 && s32->size() == 32);
    ::ed3k::Key32 channelKey{}, sec{};
    std::memcpy(channelKey.data(), key->constData(), 32);
    std::memcpy(sec.data(), s32->constData(), 32);
    const auto scope = b.engine()->paidFollowPrivate(channelKey, spanOf(*id), sec);
    QVERIFY(scope);
    QCOMPARE(Ed3kPaid::hexOf(*scope), *scopeHex);
    Ed3kPaid paid(b, db.path());
    paid.followChannel(channelKey, *scope, QStringLiteral("Private Show"));
    QVERIFY(paid.addPassLink(voucher));
    QCOMPARE(paid.info(*scopeHex)->state, Ed3kPaid::State::Held);
    QSignalSpy keys(&paid, &Ed3kPaid::keysArrived);
    QVERIFY(paid.requestKeys(*scopeHex, sourceOf(s)));
    QTRY_VERIFY_WITH_TIMEOUT(keys.count() > 0, 30000);
    const auto epoch = info->epoch;
    const auto kB = b.engine()->paidKey(*scope, epoch);
    const auto kS = s.engine()->paidKey(*scope, epoch);
    QVERIFY(kB && kB->has_value() && kS && kS->has_value());
    QVERIFY(**kB == **kS);

    paid.leaveChannel(*scope);
    (void)live.stop(list.front().id);
    b.stop();
    s.stop();
}

// subscription.md §3 (#15): a stop is a newer offer without pay_url, pass_url and price, t0 and
// period kept; the buyer is told it is no longer sold; selling again: same t0 and period
void tst_Ed3kPaidSeller::aSaleIsStoppedAndSoldAgain()
{
    QTemporaryDir ds, db;
    Ed3kSession s, b;
    QVERIFY(s.start(loopback(ds.path())));
    QVERIFY(b.start(loopback(db.path())));
    auto seller = std::make_unique<Ed3kPaidSeller>(s, ds.path());
    Ed3kPaidSeller::Terms terms = shopTerms();
    terms.payUrl = QStringLiteral("https://shop.example/pay");
    terms.passUrl = QStringLiteral("https://shop.example/pass");
    const auto scopeHex = seller->newSet(QStringLiteral("Stoppable"), terms);
    QVERIFY(scopeHex);
    const auto o1 = Ed3kPaidOffer::parse(seller->offerOf(*scopeHex));
    QVERIFY(o1 && !o1->price.isEmpty() && !o1->payUrl.isEmpty());
    const auto info = seller->info(*scopeHex);
    QVERIFY(info && !info->stopped);

    // stopped (over the IPC, action 4)
    Ed3kPaidSellerBridge::setSeller(seller.get(), nullptr);
    const auto control = [&](qint64 action) {
        Ipc::IpcMessage m(Ipc::IpcMsgType::Ed3kSellControl, 5);
        m.append(action);
        m.append(*scopeHex);
        m.append(QCborMap{});
        bool ok = false;
        (void)Ed3kPaidSellerBridge::handleRequest(m, [&](const Ipc::IpcMessage& r) { ok = r.fieldBool(0); });
        return ok;
    };
    QVERIFY(control(4));
    const auto o2 = Ed3kPaidOffer::parse(seller->offerOf(*scopeHex));
    QVERIFY(o2);
    QVERIFY(o2->issued > o1->issued);
    QVERIFY(o2->price.isEmpty() && o2->payUrl.isEmpty() && o2->passUrl.isEmpty());
    QCOMPARE(o2->t0, o1->t0);
    QCOMPARE(o2->period, o1->period);
    QCOMPARE(o2->terms, o1->terms);   // the rest stays
    QVERIFY(seller->info(*scopeHex)->stopped);
    QCOMPARE(seller->info(*scopeHex)->terms.price, terms.price);   // kept for selling again
    QVERIFY(Ed3kPaidSellerBridge::sellingToCbor().value(QStringLiteral("sales")).toArray().at(0).toMap().value(QStringLiteral("stopped")).toBool());
    // the core takes it as the newer offer
    const auto scope = Ed3kPaid::scopeOf(*scopeHex);
    QVERIFY(scope);
    const auto held = s.engine()->paidOffer(*scope);
    QVERIFY(held && *held);
    QCOMPARE(QByteArray(reinterpret_cast<const char*>((*held)->bytes().data()), qsizetype((*held)->bytes().size())), seller->offerOf(*scopeHex));

    // the buyer: no longer sold (an offer without price), not "by hand"
    Ed3kPaid paid(b, db.path());
    paid.follow(key32(info->publisher), *scope, Ed3kPaid::Kind::Set, QStringLiteral("Stoppable"));
    QVERIFY(paid.setOffer(*scope, seller->offerOf(*scopeHex)));
    const auto bought = paid.buy(*scopeHex);
    QVERIFY(!bought);
    QCOMPARE(bought.error(), Ed3kPaid::tr("This subscription is no longer sold."));

    // kept across a restart
    Ed3kPaidSellerBridge::setSeller(nullptr, nullptr);
    seller.reset();
    seller = std::make_unique<Ed3kPaidSeller>(s, ds.path());
    QVERIFY(seller->info(*scopeHex)->stopped);
    Ed3kPaidSellerBridge::setSeller(seller.get(), nullptr);

    // sold again (action 5): a later offer, the same t0 and period, the price and shop back
    QVERIFY(control(5));
    const auto o3 = Ed3kPaidOffer::parse(seller->offerOf(*scopeHex));
    QVERIFY(o3 && o3->issued > o2->issued);
    QCOMPARE(o3->price, terms.price);
    QCOMPARE(o3->payUrl, terms.payUrl);
    QCOMPARE(o3->passUrl, terms.passUrl);
    QCOMPARE(o3->t0, o1->t0);
    QCOMPARE(o3->period, o1->period);
    QVERIFY(!seller->info(*scopeHex)->stopped);
    QVERIFY(paid.setOffer(*scope, seller->offerOf(*scopeHex)));
    QVERIFY(paid.info(*scopeHex)->offer->price == terms.price);
    // stopping twice, selling twice: nothing new
    QVERIFY(control(5));
    QCOMPARE(Ed3kPaidOffer::parse(seller->offerOf(*scopeHex))->issued, o3->issued);
    Ed3kPaidSellerBridge::setSeller(nullptr, nullptr);
    seller.reset();
    b.stop();
    s.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kPaidSeller)
#include "tst_Ed3kPaidSeller.moc"
