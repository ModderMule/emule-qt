/// @file Ed3kPaidSeller.cpp
/// @brief Paid subscriptions, the seller's side — implementation.

#include "Ed3kPaidSeller.h"

#include "Ed3kLiveBroadcast.h"
#include "Ed3kPaid.h"
#include "Ed3kPlumbing.h"
#include "Ed3kProto.h"
#include "Ed3kSession.h"
#include "Ed3kSets.h"

#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>
#include <QUrl>

#include <algorithm>
#include <cstring>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

const QString kSellFile = QStringLiteral("sell.json");
const QString kSellerKey = QStringLiteral("seller.key");

constexpr std::uint32_t kChannelPeriod = 3600;     // §4.2: 720 one-hour epochs
constexpr std::uint32_t kChannelSeason = 720;
constexpr std::uint32_t kSetPeriod = 86400;        // 30 one-day epochs
constexpr std::uint32_t kSetSeason = 30;
constexpr std::uint64_t kOwnPassS = 30ull * 86400;   // the own pass, signed again 7 days before its end
constexpr std::uint64_t kOwnRenewS = 7ull * 86400;
constexpr int kMaxScopes = 16;                     // §5: 1 to 16 per pass

std::span<const std::byte> spanOf(const QByteArray& b)
{
    return {reinterpret_cast<const std::byte*>(b.constData()), std::size_t(b.size())};
}

QByteArray bytesOf(std::span<const std::byte> b)
{
    return QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size()));
}

QString hex(std::span<const std::uint8_t> b)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size())).toHex());
}

std::optional<ffi::Key32> key32Of(const QByteArray& raw)
{
    if (raw.size() != 32)
        return std::nullopt;
    ffi::Key32 k{};
    std::memcpy(k.data(), raw.constData(), k.size());
    return k;
}

ffi::Bytes ffiBytes(const QByteArray& b)
{
    const auto s = spanOf(b);
    return ffi::Bytes(s.begin(), s.end());
}

/// a fresh 32-byte secret from the CSPRNG; the caller wraps it in a Secret and wipes
Seed randomSeed()
{
    Seed s{};
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(s.data()), s.size() / sizeof(quint32));
    return s;
}

/// base and length of a SignedChain (§3); nullopt: not one
std::optional<std::pair<std::uint64_t, std::uint32_t>> chainShape(const QByteArray& signedChain)
{
    Ed3kProtoReader outer(signedChain);
    QByteArrayView body;
    while (!outer.atEnd()) {
        std::uint32_t field = 0, wire = 0;
        if (!outer.tag(field, wire))
            return std::nullopt;
        if (field == 1 && wire == 2) {
            if (!outer.bytes(body))
                return std::nullopt;
        } else if (!outer.skip(wire)) {
            return std::nullopt;
        }
    }
    Ed3kProtoReader r(body);
    std::uint64_t base = 0, length = 0;
    while (!r.atEnd()) {
        std::uint32_t field = 0, wire = 0;
        if (!r.tag(field, wire))
            return std::nullopt;
        if (wire == 0) {
            std::uint64_t v = 0;
            if (!r.varint(v))
                return std::nullopt;
            if (field == 2)
                base = v;
            else if (field == 3)
                length = v;
        } else if (!r.skip(wire)) {
            return std::nullopt;
        }
    }
    if (length == 0 || length > (1u << 20))
        return std::nullopt;
    return std::pair{base, std::uint32_t(length)};
}

QJsonObject termsToJson(const Ed3kPaidSeller::Terms& t)
{
    return QJsonObject{
        {QStringLiteral("price"), t.price},
        {QStringLiteral("payUrl"), t.payUrl},
        {QStringLiteral("passUrl"), t.passUrl},
        {QStringLiteral("reportUrl"), t.reportUrl},
        {QStringLiteral("terms"), t.terms},
        {QStringLiteral("freeAfter"), qint64(t.freeAfter)},
        {QStringLiteral("devices"), qint64(t.devices)},
        {QStringLiteral("period"), qint64(t.period)},
        {QStringLiteral("seasonLength"), qint64(t.seasonLength)},
        {QStringLiteral("passDays"), qint64(t.passDays)},
    };
}

Ed3kPaidSeller::Terms termsOf(const QJsonObject& o)
{
    Ed3kPaidSeller::Terms t;
    t.price = o.value(QStringLiteral("price")).toString();
    t.payUrl = o.value(QStringLiteral("payUrl")).toString();
    t.passUrl = o.value(QStringLiteral("passUrl")).toString();
    t.reportUrl = o.value(QStringLiteral("reportUrl")).toString();
    t.terms = o.value(QStringLiteral("terms")).toString();
    t.freeAfter = std::uint32_t(std::clamp<qint64>(o.value(QStringLiteral("freeAfter")).toInteger(), 0, 1 << 20));
    t.devices = std::uint32_t(std::clamp<qint64>(o.value(QStringLiteral("devices")).toInteger(), 0, 63));
    t.period = std::uint32_t(std::clamp<qint64>(o.value(QStringLiteral("period")).toInteger(), 0, 2678400));
    t.seasonLength = std::uint32_t(std::clamp<qint64>(o.value(QStringLiteral("seasonLength")).toInteger(), 0, 1 << 20));
    t.passDays = std::uint32_t(std::clamp<qint64>(o.value(QStringLiteral("passDays")).toInteger(30), 1, 3660));
    return t;
}

/// §3: https with a host, no user name or password; empty = none
bool urlOk(const QString& u)
{
    if (u.isEmpty())
        return true;
    const QUrl url(u, QUrl::StrictMode);
    return url.isValid() && url.scheme() == QLatin1String("https") && !url.host().isEmpty() && url.userInfo().isEmpty()
           && u.toUtf8().size() <= 1024;
}

/// the user-visible problem of @p t, empty when fine (§3 limits; the core checks again)
QString termsProblem(const Ed3kPaidSeller::Terms& t)
{
    if (t.price.trimmed().isEmpty())
        return Ed3kPaidSeller::tr("The offer needs a price.");
    if (t.price.toUtf8().size() > 128)
        return Ed3kPaidSeller::tr("The price is longer than 128 bytes.");
    if (t.terms.toUtf8().size() > 1024)
        return Ed3kPaidSeller::tr("The terms are longer than 1024 bytes.");
    if (!urlOk(t.payUrl) || !urlOk(t.passUrl) || !urlOk(t.reportUrl))
        return Ed3kPaidSeller::tr("The shop addresses must be https URLs with a host (no user name or password).");
    if (t.payUrl.isEmpty() != t.passUrl.isEmpty())
        return Ed3kPaidSeller::tr("A web shop needs both the payment and the pass address.");
    if (t.devices > 63)
        return Ed3kPaidSeller::tr("At most 63 devices per pass.");
    if (t.period != 0 && (t.period < 60 || t.period > 2678400))
        return Ed3kPaidSeller::tr("An epoch lasts 1 minute to 31 days.");
    return {};
}

} // namespace

Ed3kPaidSeller::Ed3kPaidSeller(Ed3kSession& session, const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_configDir(configDir)
{
    load();
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kPaidSeller::onEventPolled, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kPaidSeller::onSessionStatus);
    m_timer.setInterval(60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &Ed3kPaidSeller::tick);
    onSessionStatus();
}

Ed3kPaidSeller::~Ed3kPaidSeller() = default;

std::expected<QString, QString> Ed3kPaidSeller::sell(Kind kind, const Seed& publisherSeed, const QByteArray& id, const QString& title,
                                                     const Terms& terms, bool isPrivate)
{
    if (const QString p = termsProblem(terms); !p.isEmpty())
        return std::unexpected(p);
    if (id.isEmpty() || id.size() > 16)
        return std::unexpected(tr("The channel or set has no valid id."));
    auto* e = m_session.engine();
    if (!e)
        return std::unexpected(tr("ed3k is not running."));
    const Secret pub(publisherSeed);
    const auto pubKey = ffi::keyPublic(pub.expose());
    if (!pubKey)
        return std::unexpected(tr("The key could not be read."));
    const auto scope = ffi::paidScope(std::uint8_t(kind), *pubKey, spanOf(id));
    if (!scope)
        return std::unexpected(tr("The scope could not be made."));
    const QString scopeHex = Ed3kPaid::hexOf(*scope);
    if (Sale* held = find(*scope)) {
        if (!title.isEmpty())
            held->title = title;
        held->isPrivate = held->isPrivate || isPrivate;
        return setTerms(scopeHex, terms);   // a stopped sale too: sold again (§3)
    }
    QDir().mkpath(sellDir());
    Sale s;
    s.scope = *scope;
    s.kind = kind;
    s.publisher = *pubKey;
    s.id = id;
    s.title = title;
    s.terms = terms;
    s.isPrivate = kind == Kind::Channel && isPrivate;
    const std::uint32_t period = periodOf(s);
    s.terms.period = period;
    s.terms.seasonLength = lengthOf(s);
    // t0 on an epoch boundary of the clock; t0 and period never change (§3)
    const std::uint64_t now = m_session.now().unixS;
    s.t0 = now - now % period;
    if (!replaceSecret(sellDir(), QFileInfo(fileOf(s, QStringLiteral(".publisher"))).fileName(), pub.expose()))
        return std::unexpected(tr("The key could not be kept."));
    auto season = makeSeason(s, epochOf(s, now), s.terms.seasonLength, pub);
    QString error;
    if (!season || !signOffer(s, *season, pub, &error)) {
        removeFiles(s);
        return std::unexpected(error.isEmpty() ? tr("The season could not be made.") : error);
    }
    season->inOffer = true;
    s.seasons.push_back(*season);
    auto [it, inserted] = m_sales.emplace(s.scope, std::move(s));
    logEd3k(QStringLiteral("ed3k: selling paid scope %1 (%2)").arg(scopeHex, title));
    if (m_attached)
        attach(it->second);
    save();
    markChanged();
    return scopeHex;
}

std::expected<QString, QString> Ed3kPaidSeller::newSet(const QString& title, const Terms& terms)
{
    QByteArray id(16, '\0');
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(id.data()), 4);
    Seed seed = randomSeed();
    auto r = sell(Kind::Set, seed, id, title, terms);
    wipe(std::span<std::uint8_t>{seed});
    return r;
}

std::expected<QString, QString> Ed3kPaidSeller::setTerms(const QString& scopeHex, const Terms& terms)
{
    Sale* s = find(scopeHex);
    if (!s)
        return std::unexpected(tr("This scope is not sold here."));
    if (const QString p = termsProblem(terms); !p.isEmpty())
        return std::unexpected(p);
    auto seed = publisherSeed(*s);
    if (!seed)
        return std::unexpected(tr("The key of this scope is missing."));
    const Secret pub(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    // §3: t0, period and the season stay; a stopped sale is sold again (#15)
    const Terms old = s->terms;
    const bool wasStopped = s->stopped;
    s->terms = terms;
    s->terms.period = old.period;
    s->terms.seasonLength = old.seasonLength;
    s->stopped = false;
    const Season* season = offerSeason(*s);
    QString error;
    if (!season || !signOffer(*s, *season, pub, &error)) {
        s->terms = old;
        s->stopped = wasStopped;
        return std::unexpected(error.isEmpty() ? tr("The offer could not be signed.") : error);
    }
    // if (auto* e = m_session.engine(); e && s->onEngine) {
    //     (void)e->paidSetOffer(m_session.now(), s->scope, spanOf(s->offer));
    //     m_session.pump();
    // }
    pushOffer(*s);
    logEd3k(QStringLiteral("ed3k: paid scope %1: a new offer%2").arg(scopeHex, wasStopped ? QStringLiteral(" (sold again)") : QString()));
    save();
    markChanged();
    return scopeHex;
}

std::expected<void, QString> Ed3kPaidSeller::stopSale(const QString& scopeHex)
{
    Sale* s = find(scopeHex);
    if (!s)
        return std::unexpected(tr("This scope is not sold here."));
    if (s->stopped)
        return {};
    auto seed = publisherSeed(*s);
    if (!seed)
        return std::unexpected(tr("The key of this scope is missing."));
    const Secret pub(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    // §3: no termsProblem (no price is the point); t0, period, the season stay
    s->stopped = true;
    const Season* season = offerSeason(*s);
    QString error;
    if (!season || !signOffer(*s, *season, pub, &error)) {
        s->stopped = false;
        return std::unexpected(error.isEmpty() ? tr("The offer could not be signed.") : error);
    }
    pushOffer(*s);
    logEd3k(QStringLiteral("ed3k: paid scope %1: no longer sold").arg(scopeHex));
    save();
    markChanged();
    return {};
}

std::expected<void, QString> Ed3kPaidSeller::resumeSale(const QString& scopeHex)
{
    const Sale* s = find(scopeHex);
    if (!s)
        return std::unexpected(tr("This scope is not sold here."));
    if (!s->stopped)
        return {};
    auto r = setTerms(scopeHex, s->terms);
    if (!r)
        return std::unexpected(r.error());
    return {};
}

QString Ed3kPaidSeller::saleLink(const QString& scopeHex) const
{
    const Sale* s = find(scopeHex);
    return s ? linkOf(*s) : QString();
}

std::expected<void, QString> Ed3kPaidSeller::newSeasonAfterLeak(const QString& scopeHex)
{
    Sale* s = find(scopeHex);
    auto* e = m_session.engine();
    if (!s)
        return std::unexpected(tr("This scope is not sold here."));
    if (!e || !s->onEngine)
        return std::unexpected(tr("ed3k is not running."));
    auto seed = publisherSeed(*s);
    if (!seed)
        return std::unexpected(tr("The key of this scope is missing."));
    const Secret pub(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    // §4.5: base = the next epoch; the seed drawn here (the season after is made the same way)
    const std::uint64_t base = epochOf(*s, m_session.now().unixS) + 1;
    const QString name = QFileInfo(fileOf(*s, QStringLiteral("-%1.season").arg(base))).fileName();
    QFile::remove(QDir(sellDir()).filePath(name));   // a planned season of that base is replaced
    auto seasonSeed = loadOrCreateSecret(sellDir(), name, nullptr);
    if (!seasonSeed)
        return std::unexpected(tr("The season could not be made."));
    const Secret ss(*seasonSeed);
    wipe(std::span<std::uint8_t>{*seasonSeed});
    auto made = e->paidNewSeason(m_session.now(), s->scope, pub.expose(), ss.expose(), lengthOf(*s));
    m_session.pump();
    if (!made) {
        QFile::remove(QDir(sellDir()).filePath(name));
        // its seed is gone: a planned season of that base too
        std::erase_if(s->seasons, [&](const Season& x) { return !x.inOffer && x.base == base; });
        save();
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1: no new season (error %2)").arg(scopeHex).arg(int(made.error())));
        return std::unexpected(tr("The new season could not start."));
    }
    const QByteArray chain = bytesOf(made->chain.bytes());
    const auto shape = chainShape(chain);
    if (!shape)
        return std::unexpected(tr("The new season could not start."));
    if (shape->first != base)
        QFile::rename(QDir(sellDir()).filePath(name), fileOf(*s, QStringLiteral("-%1.season").arg(shape->first)));
    // the planned seasons are gone: the new one takes the next epoch
    std::erase_if(s->seasons, [&](const Season& x) {
        const bool drop = !x.inOffer || x.base >= shape->first;
        if (drop && x.base != shape->first)
            QFile::remove(fileOf(*s, QStringLiteral("-%1.season").arg(x.base)));
        return drop;
    });
    s->seasons.push_back(Season{shape->first, shape->second, chain, true});
    s->offer = bytesOf(made->offer.bytes());
    if (const auto o = Ed3kPaidOffer::parse(s->offer))
        s->offerIssued = o->issued;
    logEd3k(QStringLiteral("ed3k: paid scope %1: a new season from epoch %2 after a leak").arg(scopeHex).arg(shape->first));
    save();
    emit seasonStarted(scopeHex, s->title, shape->first);
    markChanged();
    return {};
}

std::expected<QString, QString> Ed3kPaidSeller::sellBroadcast(Ed3kLiveBroadcast& broadcast, const QString& streamId, const Terms& terms)
{
    const auto on = broadcast.info(streamId);
    if (!on)
        return std::unexpected(tr("This channel is not on air."));
    // was: ToDo (header): a private paid channel needs paidFollowPrivate with its secret
    // if (on->isPrivate)
    //     return std::unexpected(tr("A private channel cannot be sold yet."));
    // (done: its secret stays the broadcast's, attach() follows with paidFollowPrivate)
    if (on->isPrivate) {
        auto secret = broadcast.channelSecret(streamId);
        if (!secret)
            return std::unexpected(tr("The secret of the private channel could not be read."));
        wipe(std::span<std::uint8_t>{*secret});
    }
    QByteArray id;
    auto seed = broadcast.channelSeed(streamId, &id);
    if (!seed)
        return std::unexpected(tr("The channel key could not be read."));
    serve(broadcast);
    auto r = sell(Kind::Channel, *seed, id, on->title, terms, on->isPrivate);
    wipe(std::span<std::uint8_t>{*seed});
    if (!r || on->paid)
        return r;
    // §7: every segment of a paid channel is sealed: the broadcast starts again as a paid one
    const auto o = broadcast.optionsOf(streamId);
    if (!o)
        return r;
    (void)broadcast.stop(streamId);
    if (auto again = broadcast.start(*o); !again)
        return std::unexpected(again.error());
    return r;
}

void Ed3kPaidSeller::serve(Ed3kLiveBroadcast& broadcast)
{
    m_broadcast = &broadcast;
    broadcast.setOfferSource([self = QPointer(this)](const ffi::Key32& key, const QByteArray& id) {
        return self ? self->offerOf(key, id) : QByteArray();
    });
    // a channel sold that went on air open (resumed before this): again as a paid one
    for (const auto& on : broadcast.list()) {
        // if (on.paid || on.isPrivate)   (was: a private channel was not sold)
        if (on.paid)
            continue;
        QByteArray id;
        auto seed = broadcast.channelSeed(on.id, &id);
        if (!seed)
            continue;
        const auto key = ffi::keyPublic(*seed);
        wipe(std::span<std::uint8_t>{*seed});
        const auto o = broadcast.optionsOf(on.id);
        if (!key || !o || offerOf(*key, id).isEmpty())
            continue;
        (void)broadcast.stop(on.id);
        if (auto again = broadcast.start(*o); !again)
            logEd3kWarning(QStringLiteral("ed3k: paid channel %1 not started again: %2").arg(on.title, again.error()));
    }
}

std::expected<QString, QString> Ed3kPaidSeller::issuePass(const QStringList& scopesHex, const std::optional<ffi::Key32>& holder,
                                                          std::uint64_t until, const QString& note)
{
    if (scopesHex.isEmpty() || scopesHex.size() > kMaxScopes)
        return std::unexpected(tr("A pass names 1 to 16 subscriptions."));
    const std::uint64_t now = m_session.now().unixS;
    if (until <= now)
        return std::unexpected(tr("The pass would end before it begins."));
    std::vector<ffi::Scope> scopes;
    std::vector<const Sale*> sales;
    for (const QString& h : scopesHex) {
        const Sale* s = find(h);
        if (!s)
            return std::unexpected(tr("This scope is not sold here."));
        if (std::ranges::find(scopes, s->scope) == scopes.end()) {
            scopes.push_back(s->scope);
            sales.push_back(s);
        }
    }
    // the publisher signs when it is one for all; else the seller key, an issuer of each offer (§5)
    const bool onePublisher = std::ranges::all_of(sales, [&](const Sale* s) { return s->publisher == sales.front()->publisher; });
    auto signer = onePublisher ? publisherSeed(*sales.front()) : sellerSeed();
    if (!signer)
        return std::unexpected(tr("The signing key is missing."));
    const Secret sign(*signer);
    wipe(std::span<std::uint8_t>{*signer});
    std::optional<Secret> voucherSeed;
    ffi::Key32 holderKey{};
    if (holder) {
        holderKey = *holder;
    } else {
        Seed v = randomSeed();
        voucherSeed.emplace(v);
        wipe(std::span<std::uint8_t>{v});
        const auto k = ffi::keyPublic(voucherSeed->expose());
        if (!k)
            return std::unexpected(tr("The holder key could not be made."));
        holderKey = *k;
    }
    const auto pass = ffi::paidSignPass(sign.expose(), scopes, holderKey, now, until, 0);
    if (!pass)
        return std::unexpected(tr("The pass could not be signed."));
    const auto link = voucherSeed ? ffi::passLink(pass->bytes(), voucherSeed->expose()) : ffi::passLink(pass->bytes());
    if (!link)
        return std::unexpected(tr("The pass could not be signed."));
    Issued i;
    for (const auto& sc : scopes)
        i.scopes << Ed3kPaid::hexOf(sc);
    i.holder = hex(holderKey);
    i.issued = now;
    i.until = until;
    i.voucher = voucherSeed.has_value();
    i.note = note;
    // a voucher carries the holder key: a credential, never kept or logged (§9.3)
    if (!i.voucher)
        i.link = QString::fromStdString(*link);
    for (const auto& sc : scopes)
        if (Sale* s = find(sc))
            s->issued.push_back(i);
    logEd3k(QStringLiteral("ed3k: a pass issued for %1 scope(s) until %2%3")
                .arg(scopes.size())
                .arg(until)
                .arg(i.voucher ? QStringLiteral(" (voucher)") : QStringLiteral(" (holder %1)").arg(i.holder.left(16))));
    save();
    markChanged();
    return QString::fromStdString(*link);
}

QByteArray Ed3kPaidSeller::offerOf(const ffi::Key32& channelKey, const QByteArray& id) const
{
    const auto scope = ffi::paidScope(std::uint8_t(Kind::Channel), channelKey, spanOf(id));
    if (!scope)
        return {};
    const auto it = m_sales.find(*scope);
    return it == m_sales.end() ? QByteArray() : it->second.offer;
}

QByteArray Ed3kPaidSeller::offerOf(const QString& scopeHex) const
{
    const Sale* s = find(scopeHex);
    return s ? s->offer : QByteArray();
}

std::optional<Ed3kPaidSeller::SealKey> Ed3kPaidSeller::sealKey(const QString& scopeHex)
{
    const Sale* s = find(scopeHex);
    auto* e = m_session.engine();
    if (!s || !e || !s->onEngine)
        return std::nullopt;
    const std::uint64_t epoch = epochOf(*s, m_session.now().unixS);
    auto k = e->paidKey(s->scope, epoch);
    if (!k || !*k)
        return std::nullopt;
    const auto data = ffi::paidDataKey(s->scope, **k);
    wipe(std::span<std::uint8_t>{**k});
    if (!data)
        return std::nullopt;
    return SealKey{epoch, *data};
}

std::vector<Ed3kPaidSeller::Info> Ed3kPaidSeller::list() const
{
    std::vector<Info> out;
    for (const auto& [scope, s] : m_sales)
        out.push_back(infoOf(s));
    return out;
}

std::optional<Ed3kPaidSeller::Info> Ed3kPaidSeller::info(const QString& scopeHex) const
{
    const Sale* s = find(scopeHex);
    return s ? std::optional(infoOf(*s)) : std::nullopt;
}

void Ed3kPaidSeller::setDefaults(const Terms& t)
{
    m_defaults = t;
    save();
    markChanged();
}

QString Ed3kPaidSeller::sellerKey()
{
    auto seed = sellerSeed();
    if (!seed)
        return {};
    const Secret s(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    const auto k = ffi::keyPublic(s.expose());
    return k ? hex(*k) : QString();
}

void Ed3kPaidSeller::tick()
{
    if (!m_session.engine())
        return;
    for (auto& [scope, s] : m_sales)
        tickOne(s);
}

std::optional<ffi::Key32> Ed3kPaidSeller::parseHolder(const QString& text)
{
    const QString t = text.trimmed();
    if (t.size() == 64) {
        const QByteArray raw = QByteArray::fromHex(t.toLatin1());
        if (raw.size() == 32)
            return key32Of(raw);
    }
    if (const auto raw = fromBase32(t))
        return key32Of(*raw);
    return std::nullopt;
}

QString Ed3kPaidSeller::statePath() const
{
    return QDir(sellDir()).filePath(kSellFile);
}

QString Ed3kPaidSeller::sellDir() const
{
    return QDir(stateDir(m_configDir)).filePath(QStringLiteral("sell"));
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kPaidSeller::onEventPolled(const ffi::Event& event, bool* handled)
{
    namespace e = ffi::event;
    // the core answers key requests by itself; these only tell
    if (const auto* k = std::get_if<e::PaidKeys>(&event)) {
        if (find(k->scope))
            *handled = true;
    } else if (const auto* o = std::get_if<e::PaidOffer>(&event)) {
        if (Sale* s = find(o->scope)) {
            *handled = true;
            // a newer offer than ours under our key: a stolen publisher key (§4.4)
            if (o->issued > s->offerIssued)
                logEd3kWarning(QStringLiteral("ed3k: paid scope %1: a newer offer than ours is around").arg(Ed3kPaid::hexOf(s->scope)));
        }
    } else if (const auto* r = std::get_if<e::PaidRefused>(&event)) {
        if (find(r->scope))
            *handled = true;
    } else if (const auto* f = std::get_if<e::PaidFault>(&event)) {
        if (find(f->scope))
            *handled = true;
    } else if (const auto* h = std::get_if<e::PaidHorizon>(&event)) {
        if (find(h->scope))
            *handled = true;   // the seed is held here: no horizon keys (§4.4)
    }
}

void Ed3kPaidSeller::onSessionStatus()
{
    if (m_session.isRunning() && !m_attached) {
        m_attached = true;
        for (auto& [scope, s] : m_sales)
            attach(s);
        tick();
        m_timer.start();
        markChanged();
    } else if (!m_session.isRunning() && m_attached) {
        m_attached = false;
        m_timer.stop();
        for (auto& [scope, s] : m_sales)
            s.onEngine = false;
        markChanged();
    }
}

void Ed3kPaidSeller::attach(Sale& s)
{
    auto* e = m_session.engine();
    if (!e || s.onEngine)
        return;
    const QString scopeHex = Ed3kPaid::hexOf(s.scope);
    const auto t = m_session.now();
    if (s.isPrivate) {
        // live.md §10, subscription.md §6.1: every key request and answer proves the secret
        auto secret = Ed3kLiveBroadcast::channelSecretOf(m_configDir, s.id);
        if (!secret) {
            logEd3kWarning(QStringLiteral("ed3k: paid scope %1 not sold: the secret of the private channel is missing").arg(scopeHex));
            return;
        }
        const Secret sec(*secret);
        wipe(std::span<std::uint8_t>{*secret});
        const auto ok = e->paidFollowPrivate(s.publisher, spanOf(s.id), sec.expose());
        if (!ok || *ok != s.scope) {
            logEd3kWarning(QStringLiteral("ed3k: paid scope %1 not sold (error %2)").arg(scopeHex).arg(ok ? -1 : int(ok.error())));
            return;
        }
    } else if (const auto ok = e->paidFollow(s.publisher, s.scope); !ok && ok.error() != ffi::Error::State) {
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1 not sold (error %2)").arg(scopeHex).arg(int(ok.error())));
        return;
    }
    s.onEngine = true;
    // the offer first, then every season held (earlier ones give the tips, §4.2)
    if (const auto ok = e->paidSetOffer(t, s.scope, spanOf(s.offer)); !ok)
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1: the offer was refused (error %2)").arg(scopeHex).arg(int(ok.error())));
    for (const Season& season : s.seasons)
        publishSeason(s, season);
    if (auto seed = publisherSeed(s)) {
        const Secret pub(*seed);
        wipe(std::span<std::uint8_t>{*seed});
        renewOwnPass(s, pub);
    }
    if (!s.ownPass.isEmpty()) {
        if (auto h = loadSecret(sellDir(), QFileInfo(fileOf(s, QStringLiteral(".holder"))).fileName())) {
            const Secret holder(*h);
            wipe(std::span<std::uint8_t>{*h});
            if (const auto ok = e->paidSetPass(t, s.scope, spanOf(s.ownPass), holder.expose()); !ok)
                logEd3kWarning(QStringLiteral("ed3k: paid scope %1: the own pass was not taken (error %2)").arg(scopeHex).arg(int(ok.error())));
        }
    }
    m_session.pump();
}

bool Ed3kPaidSeller::signOffer(Sale& s, const Season& season, const Secret& pub, QString* error)
{
    ffi::PaidOfferFields f;
    f.scope = s.scope;
    // a later offer replaces an earlier one (§3): never the same second twice
    f.issued = std::max(m_session.now().unixS, s.offerIssued + 1);
    f.t0 = s.t0;
    f.period = periodOf(s);
    f.chain = ffiBytes(season.chain);
    // the seller key may sign passes of several scopes (§5); the publisher signs its own
    if (auto seller = sellerSeed()) {
        const Secret sk(*seller);
        wipe(std::span<std::uint8_t>{*seller});
        if (const auto k = ffi::keyPublic(sk.expose()); k && *k != s.publisher)
            f.issuers.push_back(*k);
    }
    // §3 (#15): a stopped sale's offer has no price, pay_url, pass_url
    if (!s.stopped) {
        f.price = s.terms.price.toStdString();
        f.payUrl = s.terms.payUrl.toStdString();
        f.passUrl = s.terms.passUrl.toStdString();
    }
    f.terms = s.terms.terms.toStdString();
    f.reportUrl = s.terms.reportUrl.toStdString();
    f.freeAfter = s.terms.freeAfter;
    f.devices = s.terms.devices;
    const auto offer = ffi::paidOfferSign(pub.expose(), f);
    if (!offer) {
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1: the offer was not signed (error %2)").arg(Ed3kPaid::hexOf(s.scope)).arg(int(offer.error())));
        if (error)
            *error = offer.error() == ffi::Error::TooLarge ? tr("The offer is larger than 4 KiB: shorten the terms.")
                                                           : tr("The offer could not be signed.");
        return false;
    }
    s.offer = bytesOf(offer->bytes());
    s.offerIssued = f.issued;
    return true;
}

std::optional<Ed3kPaidSeller::Season> Ed3kPaidSeller::makeSeason(Sale& s, std::uint64_t base, std::uint32_t length, const Secret& pub)
{
    // §4.1: a fresh seed per season
    const QString name = QFileInfo(fileOf(s, QStringLiteral("-%1.season").arg(base))).fileName();
    auto seed = loadOrCreateSecret(sellDir(), name, nullptr);
    if (!seed)
        return std::nullopt;
    const Secret ss(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    const auto chain = ffi::paidChainNew(pub.expose(), s.scope, base, length, ss.expose());
    if (!chain) {
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1: no season at %2 (error %3)").arg(Ed3kPaid::hexOf(s.scope)).arg(base).arg(int(chain.error())));
        QFile::remove(QDir(sellDir()).filePath(name));
        return std::nullopt;
    }
    return Season{base, length, bytesOf(chain->bytes()), false};
}

void Ed3kPaidSeller::publishSeason(Sale& s, const Season& season)
{
    auto* e = m_session.engine();
    if (!e || !s.onEngine)
        return;
    auto seed = loadSecret(sellDir(), QFileInfo(fileOf(s, QStringLiteral("-%1.season").arg(season.base))).fileName());
    if (!seed) {
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1: the seed of the season at %2 is missing").arg(Ed3kPaid::hexOf(s.scope)).arg(season.base));
        return;
    }
    const Secret ss(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    if (const auto ok = e->paidPublish(s.scope, spanOf(season.chain), ss.expose()); !ok)
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1: the season at %2 was refused (error %3)")
                           .arg(Ed3kPaid::hexOf(s.scope))
                           .arg(season.base)
                           .arg(int(ok.error())));
}

void Ed3kPaidSeller::renewOwnPass(Sale& s, const Secret& pub)
{
    const std::uint64_t now = m_session.now().unixS;
    if (!s.ownPass.isEmpty() && s.ownUntil > now + kOwnRenewS)
        return;
    // §5: every publisher node holds a pass signed by the publisher; a holder key of its own
    auto h = loadOrCreateSecret(sellDir(), QFileInfo(fileOf(s, QStringLiteral(".holder"))).fileName(), nullptr);
    if (!h)
        return;
    const Secret holder(*h);
    wipe(std::span<std::uint8_t>{*h});
    const auto key = ffi::keyPublic(holder.expose());
    if (!key)
        return;
    const std::array<ffi::Scope, 1> scopes{s.scope};
    const auto pass = ffi::paidSignPass(pub.expose(), scopes, *key, now, now + kOwnPassS, 0);
    if (!pass) {
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1: the own pass was not signed (error %2)").arg(Ed3kPaid::hexOf(s.scope)).arg(int(pass.error())));
        return;
    }
    s.ownPass = bytesOf(pass->bytes());
    s.ownUntil = now + kOwnPassS;
    if (auto* e = m_session.engine(); e && s.onEngine) {
        if (const auto ok = e->paidSetPass(m_session.now(), s.scope, spanOf(s.ownPass), holder.expose()); !ok)
            logEd3kWarning(QStringLiteral("ed3k: paid scope %1: the own pass was not taken (error %2)").arg(Ed3kPaid::hexOf(s.scope)).arg(int(ok.error())));
    }
    save();
}

void Ed3kPaidSeller::tickOne(Sale& s)
{
    auto* e = m_session.engine();
    if (!e || !s.onEngine || s.seasons.empty())
        return;
    auto seed = publisherSeed(s);
    if (!seed)
        return;
    const Secret pub(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    const QString scopeHex = Ed3kPaid::hexOf(s.scope);
    const std::uint64_t epoch = epochOf(s, m_session.now().unixS);
    bool dirty = false;
    // a planned season began: an offer names it now (§3)
    for (Season& p : s.seasons) {
        if (p.inOffer || p.base > epoch)
            continue;
        QString error;
        if (signOffer(s, p, pub, &error)) {
            p.inOffer = true;
            (void)e->paidSetOffer(m_session.now(), s.scope, spanOf(s.offer));
            logEd3k(QStringLiteral("ed3k: paid scope %1: season %2 began").arg(scopeHex).arg(p.base));
            emit seasonStarted(scopeHex, s.title, p.base);
            dirty = true;
        }
    }
    const Season& newest = s.seasons.back();
    const std::uint64_t end = newest.base + newest.length;   // the first epoch after it
    if (epoch >= end) {
        // every season is over (the node was off): one from now; the gap stays keyless (§3)
        if (auto season = makeSeason(s, epoch, lengthOf(s), pub)) {
            QString error;
            if (signOffer(s, *season, pub, &error)) {
                season->inOffer = true;
                s.seasons.push_back(*season);
                publishSeason(s, s.seasons.back());
                (void)e->paidSetOffer(m_session.now(), s.scope, spanOf(s.offer));
                emit seasonStarted(scopeHex, s.title, season->base);
                dirty = true;
            }
        }
    } else if (epoch + 1 == end && newest.inOffer) {
        // the last epoch of the season: the next one is published early (§4.4)
        if (auto season = makeSeason(s, end, lengthOf(s), pub)) {
            s.seasons.push_back(*season);
            publishSeason(s, s.seasons.back());
            logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: paid scope %1: season %2 planned").arg(scopeHex).arg(end); });
            dirty = true;
        }
    }
    renewOwnPass(s, pub);
    m_session.pump();
    if (dirty) {
        save();
        markChanged();
    }
}

std::uint64_t Ed3kPaidSeller::epochOf(const Sale& s, std::uint64_t unixS) const
{
    const std::uint32_t period = periodOf(s);
    return unixS <= s.t0 || period == 0 ? 0 : (unixS - s.t0) / period;
}

const Ed3kPaidSeller::Season* Ed3kPaidSeller::offerSeason(const Sale& s) const
{
    const Season* out = nullptr;
    for (const Season& x : s.seasons)
        if (x.inOffer && (!out || x.base > out->base))
            out = &x;
    return out;
}

std::uint32_t Ed3kPaidSeller::periodOf(const Sale& s) const
{
    if (s.terms.period != 0)
        return s.terms.period;
    return s.kind == Kind::Channel ? kChannelPeriod : kSetPeriod;
}

std::uint32_t Ed3kPaidSeller::lengthOf(const Sale& s) const
{
    if (s.terms.seasonLength != 0)
        return s.terms.seasonLength;
    return s.kind == Kind::Channel ? kChannelSeason : kSetSeason;
}

std::optional<Seed> Ed3kPaidSeller::sellerSeed()
{
    QDir().mkpath(sellDir());
    return loadOrCreateSecret(sellDir(), kSellerKey, nullptr);
}

std::optional<Seed> Ed3kPaidSeller::publisherSeed(const Sale& s) const
{
    return loadSecret(sellDir(), QFileInfo(fileOf(s, QStringLiteral(".publisher"))).fileName());
}

Ed3kPaidSeller::Sale* Ed3kPaidSeller::find(const QString& scopeHex)
{
    const auto scope = Ed3kPaid::scopeOf(scopeHex);
    return scope ? find(*scope) : nullptr;
}

const Ed3kPaidSeller::Sale* Ed3kPaidSeller::find(const QString& scopeHex) const
{
    const auto scope = Ed3kPaid::scopeOf(scopeHex);
    if (!scope)
        return nullptr;
    const auto it = m_sales.find(*scope);
    return it == m_sales.end() ? nullptr : &it->second;
}

Ed3kPaidSeller::Sale* Ed3kPaidSeller::find(const ffi::Scope& scope)
{
    const auto it = m_sales.find(scope);
    return it == m_sales.end() ? nullptr : &it->second;
}

Ed3kPaidSeller::Info Ed3kPaidSeller::infoOf(const Sale& s) const
{
    Info i;
    i.scope = Ed3kPaid::hexOf(s.scope);
    i.kind = s.kind;
    i.title = s.title;
    i.publisher = hex(s.publisher);
    i.target = QString::fromLatin1(s.id.toHex());
    i.link = linkOf(s);
    i.terms = s.terms;
    i.t0 = s.t0;
    i.epoch = epochOf(s, m_session.now().unixS);
    if (const Season* season = offerSeason(s)) {
        i.seasonBase = season->base;
        i.seasonLength = season->length;
    }
    i.offerIssued = s.offerIssued;
    i.planned = std::ranges::any_of(s.seasons, [](const Season& x) { return !x.inOffer; });
    i.ownUntil = s.ownUntil;
    i.onEngine = s.onEngine;
    i.issued = s.issued;
    i.isPrivate = s.isPrivate;
    i.stopped = s.stopped;
    return i;
}

QString Ed3kPaidSeller::linkOf(const Sale& s) const
{
    const auto idSpan = std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.id.constData()), std::size_t(s.id.size()));
    if (s.kind == Kind::Channel) {
        // §7: ed3k:live?key&id&paid=1[&s][&dn]
        QString link = QStringLiteral("ed3k:live?key=%1&id=%2&paid=1").arg(base32(s.publisher), base32(idSpan));
        if (s.isPrivate) {
            // live.md §10: the link is the credential (never logged, not in sell.json)
            if (auto secret = Ed3kLiveBroadcast::channelSecretOf(m_configDir, s.id)) {
                link += QStringLiteral("&s=") + base32(*secret);
                wipe(std::span<std::uint8_t>{*secret});
            }
        }
        if (!s.title.isEmpty())
            link += QStringLiteral("&dn=") + QString::fromLatin1(QUrl::toPercentEncoding(s.title));
        return link;
    }
    // §8.2: a catalogue's link is the ordinary one
    return Ed3kSets::setLink(s.publisher, s.id, s.title);
}

QString Ed3kPaidSeller::fileOf(const Sale& s, const QString& suffix) const
{
    return QDir(sellDir()).filePath(Ed3kPaid::hexOf(s.scope) + suffix);
}

void Ed3kPaidSeller::removeFiles(const Sale& s)
{
    QFile::remove(fileOf(s, QStringLiteral(".publisher")));
    QFile::remove(fileOf(s, QStringLiteral(".holder")));
    const QString prefix = Ed3kPaid::hexOf(s.scope) + QLatin1Char('-');
    for (const QString& f : QDir(sellDir()).entryList({prefix + QStringLiteral("*.season")}, QDir::Files))
        QFile::remove(QDir(sellDir()).filePath(f));
}

void Ed3kPaidSeller::pushOffer(Sale& s)
{
    if (auto* e = m_session.engine(); e && s.onEngine) {
        (void)e->paidSetOffer(m_session.now(), s.scope, spanOf(s.offer));
        m_session.pump();
    }
    // §7: the offer of an open channel is in its channel file: a changed one is a new file
    // ToDo: off air the channel file is not shared yet (live #5, Ed3kLiveFiles): then at its start
    if (!m_broadcast || s.kind != Kind::Channel || s.isPrivate)
        return;
    for (const auto& on : m_broadcast->list()) {
        QByteArray id;
        auto seed = m_broadcast->channelSeed(on.id, &id);
        if (!seed)
            continue;
        const auto key = ffi::keyPublic(*seed);
        wipe(std::span<std::uint8_t>{*seed});
        const auto o = m_broadcast->optionsOf(on.id);
        if (!key || *key != s.publisher || id != s.id || !o)
            continue;
        if (auto r = m_broadcast->describe(on.id, o->comment, o->lang, o->kbps); !r)
            logEd3kWarning(QStringLiteral("ed3k: paid channel %1: the channel file was not renewed: %2").arg(on.title, r.error()));
    }
}

void Ed3kPaidSeller::markChanged()
{
    if (m_changePending)
        return;
    m_changePending = true;
    QTimer::singleShot(300, this, [this] {
        m_changePending = false;
        emit changed();
    });
}

void Ed3kPaidSeller::load()
{
    const QJsonObject root = QJsonDocument::fromJson(readBlob(statePath())).object();
    if (root.contains(QStringLiteral("defaults")))
        m_defaults = termsOf(root.value(QStringLiteral("defaults")).toObject());
    for (const QJsonValue& v : root.value(QStringLiteral("sales")).toArray()) {
        const QJsonObject o = v.toObject();
        const auto scope = Ed3kPaid::scopeOf(o.value(QStringLiteral("scope")).toString());
        const auto pub = key32Of(QByteArray::fromHex(o.value(QStringLiteral("publisher")).toString().toLatin1()));
        if (!scope || !pub)
            continue;
        Sale s;
        s.scope = *scope;
        s.kind = o.value(QStringLiteral("kind")).toInt() == int(Kind::Channel) ? Kind::Channel : Kind::Set;
        s.publisher = *pub;
        s.id = QByteArray::fromHex(o.value(QStringLiteral("id")).toString().toLatin1());
        s.title = o.value(QStringLiteral("title")).toString();
        s.terms = termsOf(o.value(QStringLiteral("terms")).toObject());
        s.t0 = o.value(QStringLiteral("t0")).toString().toULongLong();
        s.offer = QByteArray::fromBase64(o.value(QStringLiteral("offer")).toString().toLatin1());
        s.offerIssued = o.value(QStringLiteral("offerIssued")).toString().toULongLong();
        s.ownPass = QByteArray::fromBase64(o.value(QStringLiteral("ownPass")).toString().toLatin1());
        s.ownUntil = o.value(QStringLiteral("ownUntil")).toString().toULongLong();
        s.isPrivate = s.kind == Kind::Channel && o.value(QStringLiteral("private")).toBool();
        s.stopped = o.value(QStringLiteral("stopped")).toBool();
        for (const QJsonValue& sv : o.value(QStringLiteral("seasons")).toArray()) {
            const QJsonObject so = sv.toObject();
            const QByteArray chain = QByteArray::fromBase64(so.value(QStringLiteral("chain")).toString().toLatin1());
            const auto shape = chainShape(chain);
            if (!shape)
                continue;
            s.seasons.push_back(Season{shape->first, shape->second, chain, so.value(QStringLiteral("inOffer")).toBool(true)});
        }
        std::ranges::sort(s.seasons, {}, &Season::base);
        for (const QJsonValue& iv : o.value(QStringLiteral("issued")).toArray()) {
            const QJsonObject io = iv.toObject();
            Issued i;
            for (const QJsonValue& sc : io.value(QStringLiteral("scopes")).toArray())
                i.scopes << sc.toString();
            i.holder = io.value(QStringLiteral("holder")).toString();
            i.issued = io.value(QStringLiteral("issued")).toString().toULongLong();
            i.until = io.value(QStringLiteral("until")).toString().toULongLong();
            i.voucher = io.value(QStringLiteral("voucher")).toBool();
            i.note = io.value(QStringLiteral("note")).toString();
            i.link = io.value(QStringLiteral("link")).toString();
            s.issued.push_back(std::move(i));
        }
        if (s.offer.isEmpty() || s.seasons.empty())
            continue;
        m_sales.emplace(s.scope, std::move(s));
    }
}

void Ed3kPaidSeller::save() const
{
    QJsonArray sales;
    for (const auto& [scope, s] : m_sales) {
        QJsonArray seasons;
        for (const Season& x : s.seasons)
            seasons.append(QJsonObject{{QStringLiteral("chain"), QString::fromLatin1(x.chain.toBase64())}, {QStringLiteral("inOffer"), x.inOffer}});
        QJsonArray issued;
        for (const Issued& i : s.issued)
            issued.append(QJsonObject{
                {QStringLiteral("scopes"), QJsonArray::fromStringList(i.scopes)},
                {QStringLiteral("holder"), i.holder},
                {QStringLiteral("issued"), QString::number(i.issued)},
                {QStringLiteral("until"), QString::number(i.until)},
                {QStringLiteral("voucher"), i.voucher},
                {QStringLiteral("note"), i.note},
                {QStringLiteral("link"), i.link},
            });
        sales.append(QJsonObject{
            {QStringLiteral("scope"), Ed3kPaid::hexOf(s.scope)},
            {QStringLiteral("kind"), int(s.kind)},
            {QStringLiteral("publisher"), hex(s.publisher)},
            {QStringLiteral("id"), QString::fromLatin1(s.id.toHex())},
            {QStringLiteral("title"), s.title},
            {QStringLiteral("terms"), termsToJson(s.terms)},
            {QStringLiteral("t0"), QString::number(s.t0)},
            {QStringLiteral("offer"), QString::fromLatin1(s.offer.toBase64())},
            {QStringLiteral("offerIssued"), QString::number(s.offerIssued)},
            {QStringLiteral("ownPass"), QString::fromLatin1(s.ownPass.toBase64())},
            {QStringLiteral("ownUntil"), QString::number(s.ownUntil)},
            {QStringLiteral("seasons"), seasons},
            {QStringLiteral("issued"), issued},
            // the secret of a private channel is not here (the broadcast's file, owner-only)
            {QStringLiteral("private"), s.isPrivate},
            {QStringLiteral("stopped"), s.stopped},
        });
    }
    const QByteArray out = QJsonDocument(QJsonObject{{QStringLiteral("defaults"), termsToJson(m_defaults)}, {QStringLiteral("sales"), sales}})
                               .toJson(QJsonDocument::Compact);
    QDir().mkpath(sellDir());
    // no secret here (the seeds are files of their own); owner-only all the same
    writeBlobLater(statePath(), spanOf(out));
}

} // namespace eMule::ed3k
