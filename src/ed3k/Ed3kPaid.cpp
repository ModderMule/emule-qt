/// @file Ed3kPaid.cpp
/// @brief Paid subscriptions, the buyer's side — implementation.

#include "Ed3kPaid.h"

#include "Ed3kPlumbing.h"
#include "Ed3kProto.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cstring>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

const QString kPaidFile = QStringLiteral("paid.json");

std::span<const std::byte> spanOf(const QByteArray& b)
{
    return {reinterpret_cast<const std::byte*>(b.constData()), std::size_t(b.size())};
}

std::string_view viewOf(const QByteArray& b)
{
    return {b.constData(), std::size_t(b.size())};
}

QString hex(std::span<const std::uint8_t> b)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size())).toHex());
}

template <std::size_t N>
std::optional<std::array<std::uint8_t, N>> arrayOf(const QString& h)
{
    const QByteArray raw = QByteArray::fromHex(h.toLatin1());
    if (raw.size() != qsizetype(N) || h.size() != qsizetype(2 * N))
        return std::nullopt;
    std::array<std::uint8_t, N> out{};
    std::memcpy(out.data(), raw.constData(), N);
    return out;
}

QByteArray bytesOf(std::span<const std::byte> b)
{
    return QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size()));
}

/// §9.3: a voucher's pass is taken once (its hash kept, never the link)
QString passHash(const QByteArray& pass)
{
    return QString::fromLatin1(QCryptographicHash::hash(pass, QCryptographicHash::Sha256).toHex());
}

/// the scopes a serialised SignedPass names
std::vector<ffi::Scope> scopesOfPass(const QByteArray& signedPass)
{
    std::vector<ffi::Scope> out;
    if (const auto p = Ed3kPaidPass::parse(signedPass))
        for (const QByteArray& b : p->scopes)
            if (b.size() == 16) {
                ffi::Scope s{};
                std::memcpy(s.bytes.data(), b.constData(), 16);
                out.push_back(s);
            }
    return out;
}

/// the body (field 1) of a Signed* message
std::optional<QByteArrayView> bodyOf(QByteArrayView signedMsg)
{
    Ed3kProtoReader r(signedMsg);
    std::optional<QByteArrayView> body;
    while (!r.atEnd()) {
        std::uint32_t field = 0, wire = 0;
        if (!r.tag(field, wire))
            return std::nullopt;
        if (field == 1 && wire == 2) {
            QByteArrayView b;
            if (!r.bytes(b))
                return std::nullopt;
            body = b;
        } else if (!r.skip(wire)) {
            return std::nullopt;
        }
    }
    return body;
}

} // namespace

std::optional<Ed3kPaidOffer> Ed3kPaidOffer::parse(const QByteArray& signedOffer)
{
    const auto body = bodyOf(signedOffer);
    if (!body)
        return std::nullopt;
    Ed3kProtoReader r(*body);
    Ed3kPaidOffer o;
    while (!r.atEnd()) {
        std::uint32_t field = 0, wire = 0;
        if (!r.tag(field, wire))
            return std::nullopt;
        if (wire == 0) {
            std::uint64_t v = 0;
            if (!r.varint(v))
                return std::nullopt;
            switch (field) {
            case 2: o.issued = v; break;
            case 3: o.t0 = v; break;
            case 4: o.period = std::uint32_t(v); break;
            case 10: o.freeAfter = std::uint32_t(v); break;
            case 13: o.devices = std::uint32_t(v); break;
            default: break;
            }
        } else if (wire == 2) {
            QByteArrayView b;
            if (!r.bytes(b))
                return std::nullopt;
            switch (field) {
            case 1: o.scope = b.toByteArray(); break;
            case 6: ++o.issuers; break;
            case 7: o.price = QString::fromUtf8(b); break;
            case 8: o.payUrl = QString::fromUtf8(b); break;
            case 9: o.passUrl = QString::fromUtf8(b); break;
            case 11: o.terms = QString::fromUtf8(b); break;
            case 12: o.reportUrl = QString::fromUtf8(b); break;
            default: break;
            }
        } else if (!r.skip(wire)) {
            return std::nullopt;
        }
    }
    if (o.scope.size() != 16)
        return std::nullopt;
    return o;
}

std::optional<Ed3kPaidPass> Ed3kPaidPass::parse(const QByteArray& signedPass)
{
    const auto body = bodyOf(signedPass);
    if (!body)
        return std::nullopt;
    Ed3kProtoReader r(*body);
    Ed3kPaidPass p;
    while (!r.atEnd()) {
        std::uint32_t field = 0, wire = 0;
        if (!r.tag(field, wire))
            return std::nullopt;
        if (wire == 0) {
            std::uint64_t v = 0;
            if (!r.varint(v))
                return std::nullopt;
            if (field == 3)
                p.until = v;
            else if (field == 4)
                p.relay = v == 1;
            else if (field == 5)
                p.issued = v;
        } else if (wire == 2) {
            QByteArrayView b;
            if (!r.bytes(b))
                return std::nullopt;
            if (field == 1)
                p.scopes.push_back(b.toByteArray());
            else if (field == 2)
                p.holder = b.toByteArray();
        } else if (!r.skip(wire)) {
            return std::nullopt;
        }
    }
    return p;
}

Ed3kPaid::Ed3kPaid(Ed3kSession& session, const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_configDir(configDir)
{
    load();
    // was: connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kPaid::onEventPolled, Qt::DirectConnection);
    // node key from eventPolled, the anonymous node's from keyedEventPolled: each event once
    connect(&m_session, &Ed3kSession::eventPolled, this,
            [this](const ffi::Event& ev, bool* handled) { onEventPolled(ev, ffi::KeyKind::Node, handled); }, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::keyedEventPolled, this,
            [this](const ffi::Event& ev, ffi::KeyKind key, bool* handled) {
                if (key == ffi::KeyKind::Anon)
                    onEventPolled(ev, key, handled);
            },
            Qt::DirectConnection);
    connect(&m_session.hide(), &Ed3kHide::started, this, [this] { hideChanged(true); });
    connect(&m_session.hide(), &Ed3kHide::stopped, this, [this] { hideChanged(false); });
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kPaid::onSessionStatus);
    onSessionStatus();
}

Ed3kPaid::~Ed3kPaid() = default;

void Ed3kPaid::follow(const ffi::Key32& publisher, const ffi::Scope& scope, Kind kind, const QString& title)
{
    auto [it, inserted] = m_subs.try_emplace(scope);
    Sub& s = it->second;
    s.scope = scope;
    s.publisher = publisher;
    s.kind = kind;
    if (!title.isEmpty())
        s.title = title;
    const bool was = s.followed && s.known;
    s.followed = true;
    // a pass link's subscription waited for this (its publisher)
    s.known = true;
    if (inserted || !was) {
        logEd3k(QStringLiteral("ed3k: paid scope %1 followed").arg(hexOf(scope)));
        save();
    }
    if (m_attached)
        attach(s);
    markChanged();
}

void Ed3kPaid::unfollow(const ffi::Scope& scope)
{
    Sub* s = find(scope);
    if (!s)
        return;
    // was: if (auto* e = m_session.engine(); e && s->onEngine && !s->anon)
    //     (void)e->paidUnfollow(scope);
    detachSub(*s);
    s->followed = false;
    // a bought pass stays (its holder key, a renewal)
    if (!s->holder)
        m_subs.erase(scope);
    save();
    markChanged();
}

bool Ed3kPaid::setOffer(const ffi::Scope& scope, const QByteArray& signedOffer)
{
    Sub* s = find(scope);
    const auto o = Ed3kPaidOffer::parse(signedOffer);
    if (!s || !o || o->scope != QByteArray(reinterpret_cast<const char*>(scope.bytes.data()), 16))
        return false;
    // a later offer replaces an earlier one (§3); the core checks the signature
    if (const auto held = Ed3kPaidOffer::parse(s->offer); held && held->issued > o->issued)
        return false;
    if (auto* e = m_session.engine(); e && s->onEngine) {
        const auto ok = s->anon ? e->paidAnonSetOffer(m_session.now(), scope, spanOf(signedOffer))
                                : e->paidSetOffer(m_session.now(), scope, spanOf(signedOffer));
        m_session.pump();
        if (!ok) {
            logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: paid scope %1: an offer refused (error %2)").arg(hexOf(scope)).arg(int(ok.error())); });
            return false;
        }
    }
    s->offer = signedOffer;
    // §9.3 (#10): an issuer-signed pass is checked once the offer is known
    if (s->onEngine && !s->pass.isEmpty() && s->check != Check::Ok)
        checkPass(*s);
    save();
    markChanged();
    return true;
}

std::expected<QString, QString> Ed3kPaid::buy(const QString& scopeHex, bool anon)
{
    const auto scope = scopeOf(scopeHex);
    Sub* s = scope ? find(*scope) : nullptr;
    if (!s)
        return std::unexpected(tr("This subscription is not followed."));
    if (!m_session.engine())
        return std::unexpected(tr("ed3k is not running."));
    const auto offer = Ed3kPaidOffer::parse(s->offer);
    if (!offer)
        return std::unexpected(tr("The price of this subscription is not known yet."));
    // §3 (#15): a newer offer without price, pay_url, pass_url: the sale stopped
    if (offer->price.isEmpty())
        return std::unexpected(tr("This subscription is no longer sold."));
    if (offer->payUrl.isEmpty() || offer->passUrl.isEmpty())
        return std::unexpected(tr("The seller sells passes by hand only (no web shop in the offer)."));
    // §5.1: a holder key of its own per subscription, kept for renewals; never the node key
    if (!s->holder) {
        QDir().mkpath(paidDir());
        auto seed = loadOrCreateSecret(paidDir(), QFileInfo(holderFile(*s)).fileName(), nullptr);
        if (!seed)
            return std::unexpected(tr("The holder key could not be made."));
        const Secret seedS(*seed);
        wipe(std::span<std::uint8_t>{*seed});
        const auto pub = ffi::roomMemberKey(seedS.expose());   // the Ed25519 key of the seed
        if (!pub)
            return std::unexpected(tr("The holder key could not be made."));
        s->holder = *pub;
    }
    if (anon != s->anon) {
        // the pass is held under one key only (§5.1)
        // was: if (auto* e = m_session.engine(); e && s->onEngine && !s->anon)
        //     (void)e->paidUnfollow(s->scope);
        detachSub(*s);
        s->anon = anon;
        attach(*s);
    }
    s->began = m_session.now().unixS;
    s->state = State::Buying;
    QString error;
    const bool polling = startPoll(*s, false, &error);
    save();
    markChanged();
    if (!polling && !s->anonWait)
        return std::unexpected(error);
    logEd3k(QStringLiteral("ed3k: paid scope %1: buying a pass%2").arg(hexOf(s->scope), s->anon ? QStringLiteral(" (anonymous key)") : QString()));
    // §5.1: the anonymous key is told first; confirmAnon gives the link
    return s->anonWait ? QString() : payLinkOf(*s);
}

std::optional<QString> Ed3kPaid::confirmAnon(const QString& scopeHex)
{
    const auto scope = scopeOf(scopeHex);
    Sub* s = scope ? find(*scope) : nullptr;
    auto* e = m_session.engine();
    if (!s || !s->anonWait || !s->holder || !e)
        return std::nullopt;
    if (const auto ok = e->paidAnonConfirm(m_session.now(), s->scope, *s->holder); !ok) {
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1: the anonymous confirmation failed (%2)").arg(hexOf(s->scope)).arg(int(ok.error())));
        return std::nullopt;
    }
    // ED3K_STATE_PAID keeps it: a restart asks no second time
    m_session.savePaidState();
    s->anonWait = false;
    const bool purchase = s->state == State::Buying && !s->anonResume;
    if (s->state == State::Held && s->renewAsked)
        (void)renewNow(hexOf(s->scope));
    else if (s->state == State::Held)
        startRenewal(*s);
    else
        (void)startPoll(*s, s->anonResume);
    save();
    markChanged();
    return purchase ? payLinkOf(*s) : QString();
}

std::expected<Ed3kPaid::PassTaken, QString> Ed3kPaid::addPassLink(const QString& link)
{
    // §9.3: a credential: never in a log line
    QByteArray utf8 = link.trimmed().toUtf8();
    auto parsed = ffi::parsePassLink(std::string_view(utf8.constData(), std::size_t(utf8.size())));
    wipe(utf8);
    if (!parsed)
        return std::unexpected(parsed.error() == ffi::Error::Crypto ? tr("The key in this pass link does not belong to its pass.")
                                                                    : tr("This is not a valid ed3k pass link."));
    const QByteArray pass = bytesOf(parsed->pass);
    const bool voucher = parsed->holderSeed.has_value();
    const QString hash = passHash(pass);
    std::optional<Secret> seed;
    if (voucher) {
        seed.emplace(*parsed->holderSeed);
        wipe(std::span<std::uint8_t>{*parsed->holderSeed});
        // §9.3 SHOULD: a voucher is taken once (twice: one pass for two people)
        if (m_vouchers.contains(hash))
            return std::unexpected(tr("This voucher was taken already. A voucher used twice is one pass for two people."));
    } else {
        // the holder key must be ours: one of a purchase or an earlier voucher
        for (const auto& [sc, s] : m_subs)
            if (s.holder == parsed->holder)
                if (auto held = loadSecret(paidDir(), QFileInfo(holderFile(s)).fileName())) {
                    seed.emplace(*held);
                    wipe(std::span<std::uint8_t>{*held});
                    break;
                }
        if (!seed)
            return std::unexpected(tr("This pass is for a key this computer does not hold. Ask the seller for a voucher."));
    }
    if (parsed->scopes.empty())
        return std::unexpected(tr("This is not a valid ed3k pass link."));
    // the subscription the pass is shown for: a followed one first
    const auto first = std::ranges::find_if(parsed->scopes, [this](const ffi::Scope& sc) {
        const Sub* s = find(sc);
        return s && s->known;
    });
    const ffi::Scope main = first != parsed->scopes.end() ? *first : parsed->scopes.front();
    auto [it, inserted] = m_subs.try_emplace(main);
    Sub& s = it->second;
    if (inserted) {
        s.scope = main;
        s.known = false;
        s.followed = false;
    }
    // §9.2 (#11): a pass by hand is renewed only when the user asks
    const bool waits = spreadPass(s, pass, parsed->until, parsed->scopes, parsed->holder, seed->expose(), true);
    if (voucher)
        m_vouchers.insert(hash);
    m_session.pump();
    save();
    markChanged();
    logEd3k(QStringLiteral("ed3k: paid scope %1: a pass by hand%2 until %3")
                .arg(hexOf(main), voucher ? QStringLiteral(" (voucher)") : QString())
                .arg(parsed->until));
    if (!waits)
        emit passArrived(hexOf(main), s.title, parsed->until);
    return PassTaken{hexOf(main), s.title, int(parsed->scopes.size()), voucher, waits};
}

bool Ed3kPaid::answerScopes(const QString& scopeHex, bool use)
{
    const auto scope = scopeOf(scopeHex);
    Sub* s = scope ? find(*scope) : nullptr;
    if (!s || s->waitingPass.isEmpty())
        return false;
    const QByteArray pass = s->waitingPass;
    const auto scopes = scopesOfPass(pass);
    // every subscription the pass came to: one word for the pass
    // §5.1 (#9): kept for every subscription: a subset of these asks no more
    if (use)
        acceptScopes(scopes);
    for (auto& [sc, t] : m_subs) {
        if (t.waitingPass != pass)
            continue;
        if (use) {
            // t.scopesOk.clear();   (was: per subscription, cleared at each word)
            // for (const auto& x : scopes)
            //     t.scopesOk << hexOf(x);
            const std::uint64_t until = t.waitingUntil;
            t.waitingPass.clear();
            t.waitingUntil = 0;
            t.scopesDeclined = false;
            t.byHand = t.waitingByHand;
            t.waitingByHand = false;
            usePass(t, pass, until);
        } else {
            // kept unused (§5.1); offered again from the UI
            t.scopesDeclined = true;
        }
    }
    m_session.pump();
    logEd3k(QStringLiteral("ed3k: paid scope %1: a pass of %2 scopes %3").arg(hexOf(s->scope)).arg(scopes.size()).arg(use ? QStringLiteral("taken") : QStringLiteral("kept unused")));
    save();
    markChanged();
    if (use)
        emit passArrived(hexOf(s->scope), s->title, s->until);
    return true;
}

std::expected<void, QString> Ed3kPaid::renewNow(const QString& scopeHex)
{
    const auto scope = scopeOf(scopeHex);
    Sub* s = scope ? find(*scope) : nullptr;
    auto* e = m_session.engine();
    if (!s)
        return std::unexpected(tr("This subscription is not followed."));
    if (!e)
        return std::unexpected(tr("ed3k is not running."));
    const auto offer = Ed3kPaidOffer::parse(s->offer);
    if (s->pass.isEmpty() || !s->holder)
        return std::unexpected(tr("No pass is held for this subscription."));
    if (!offer || offer->passUrl.isEmpty())
        return std::unexpected(tr("The seller's offer has no pass address: ask the seller for a new pass link."));
    // §9.2: polled now, same holder key; 200 with a later `until` replaces the pass (onPass)
    const QByteArray url = offer->passUrl.toUtf8();
    const auto ok = e->passPoll(m_session.now(), s->scope, s->publisher, *s->holder, viewOf(url), m_session.now().unixS, 0, spanOf(s->offer), false);
    m_session.pump();
    if (!ok && ok.error() == ffi::Error::Denied) {
        // §5.1: the user is told first; confirmAnon() polls then
        s->anonWait = true;
        s->anonResume = true;
        s->renewAsked = true;
        emit anonWarning(hexOf(s->scope), s->title);
        markChanged();
        return {};
    }
    s->renewAsked = false;
    if (!ok) {
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1: pass_url not polled (error %2)").arg(hexOf(s->scope)).arg(int(ok.error())));
        return std::unexpected(tr("The pass address of the offer cannot be used."));
    }
    s->renewing = true;
    s->renewalEnded = false;
    logEd3k(QStringLiteral("ed3k: paid scope %1: the pass is renewed on the user's word").arg(hexOf(s->scope)));
    markChanged();
    return {};
}

void Ed3kPaid::followChannel(const ffi::Key32& key, const ffi::Scope& scope, const QString& title)
{
    // attached while watched only: a private channel's key requests prove its secret, which
    // only the live watch gives the core (paidFollowPrivate there)
    m_subs[scope].watching = true;
    follow(key, scope, Kind::Channel, title);
}

void Ed3kPaid::leaveChannel(const ffi::Scope& scope)
{
    if (Sub* s = find(scope)) {
        s->watching = false;
        unfollow(scope);
    }
}

std::optional<Ed3kPaid::ChannelLink> Ed3kPaid::channelOfLink(const QString& link)
{
    static const QString kPrefix = QStringLiteral("ed3k:live?");
    const QString text = link.trimmed();
    if (!text.startsWith(kPrefix, Qt::CaseInsensitive))
        return std::nullopt;
    const QUrlQuery q(text.mid(kPrefix.size()));
    if (q.queryItemValue(QStringLiteral("paid")) != QLatin1String("1"))
        return std::nullopt;
    const auto key = fromBase32(q.queryItemValue(QStringLiteral("key")));
    const auto id = fromBase32(q.queryItemValue(QStringLiteral("id")));
    if (!key || key->size() != 32 || !id || id->isEmpty())
        return std::nullopt;
    ChannelLink out;
    std::memcpy(out.key.data(), key->constData(), out.key.size());
    // §7: kind 1, the channel key and id (a private channel's too)
    const auto scope = ffi::paidScope(1, out.key, spanOf(*id));
    if (!scope)
        return std::nullopt;
    out.scope = *scope;
    return out;
}

void Ed3kPaid::setReports(bool on)
{
    m_reports = on;
    if (auto* e = m_session.engine())
        (void)e->passSetReports(on);
    save();
    markChanged();
}

std::vector<Ed3kPaid::Info> Ed3kPaid::list() const
{
    std::vector<Info> out;
    for (const auto& [scope, s] : m_subs)
        out.push_back(infoOf(s));
    return out;
}

std::optional<Ed3kPaid::Info> Ed3kPaid::info(const QString& scopeHex) const
{
    const auto scope = scopeOf(scopeHex);
    const Sub* s = scope ? find(*scope) : nullptr;
    return s ? std::optional(infoOf(*s)) : std::nullopt;
}

bool Ed3kPaid::holdsPass(const ffi::Scope& scope) const
{
    const Sub* s = find(scope);
    // return s && !s->pass.isEmpty();   (was: a pass failing for the scope too, §9.3)
    return s && !s->pass.isEmpty() && s->check != Check::Failed;
}

bool Ed3kPaid::requestKeys(const QString& scopeHex, const ffi::Source& node)
{
    const auto scope = scopeOf(scopeHex);
    auto* e = m_session.engine();
    if (!scope || !e || !find(*scope))
        return false;
    // was: const auto ok = e->paidRequestKeys(m_session.now(), node, *scope);
    const auto ok = find(*scope)->anon ? e->paidAnonRequestKeys(m_session.now(), node, *scope)
                                       : e->paidRequestKeys(m_session.now(), node, *scope);
    m_session.pump();
    return ok.has_value();
}

QString Ed3kPaid::hexOf(const ffi::Scope& scope)
{
    return hex(scope.bytes);
}

std::optional<ffi::Scope> Ed3kPaid::scopeOf(const QString& h)
{
    const auto a = arrayOf<16>(h);
    return a ? std::optional(ffi::Scope{*a}) : std::nullopt;
}

QString Ed3kPaid::withQuery(const QString& url, const ffi::Scope& scope, const ffi::Key32& holder)
{
    const QChar sep = url.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?');
    return url + sep + QStringLiteral("scope=") + base32(scope.bytes) + QStringLiteral("&holder=") + base32(holder);
}

QString Ed3kPaid::statePath() const
{
    return QDir(stateDir(m_configDir)).filePath(kPaidFile);
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void Ed3kPaid::onEventPolled(const ffi::Event& event, ffi::KeyKind key, bool* handled)
{
    namespace e = ffi::event;
    // a scope's key events come from the node it runs on (hops.md §1); the pass polls
    // (PaidAnonWarn, Pass*) go by scope and holder, whichever node reports them
    const auto keyed = [this, key](const ffi::Scope& scope) { return findKeyed(scope, key); };
    if (const auto* k = std::get_if<e::PaidKeys>(&event)) {
        if (Sub* s = keyed(k->scope)) {
            *handled = true;
            s->keys = true;
            for (const auto ep : k->epochs)
                s->newestEpoch = std::max(s->newestEpoch, ep);
            logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: paid scope %1: keys of %2 epoch(s)").arg(hexOf(s->scope)).arg(k->epochs.size()); });
            emit keysArrived(s->scope);
            markChanged();
        }
    } else if (const auto* o = std::get_if<e::PaidOffer>(&event)) {
        if (Sub* s = keyed(o->scope)) {
            *handled = true;
            // was: ToDo: read the offer the core holds (done: paidOffer; a hidden set's comes
            // from pass holders, §6.2)
            logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: paid scope %1: an offer of %2 held").arg(hexOf(s->scope)).arg(o->issued); });
            readOffer(*s);
            markChanged();
        }
    } else if (const auto* r = std::get_if<e::PaidRefused>(&event)) {
        if (keyed(r->scope)) {
            *handled = true;
            logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: paid scope %1: no key (refusal %2)").arg(hexOf(r->scope)).arg(r->refusal); });
        }
    } else if (const auto* f = std::get_if<e::PaidFault>(&event)) {
        if (keyed(f->scope)) {
            *handled = true;
            logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: paid scope %1: a key that failed; that node is no source for 24 h").arg(hexOf(f->scope)); });
        }
    } else if (const auto* w = std::get_if<e::PaidAnonWarn>(&event)) {
        if (Sub* s = find(w->scope)) {
            *handled = true;
            // the call that warned returns DENIED; startPoll tells the user
            Q_UNUSED(s);
        }
    } else if (const auto* p = std::get_if<e::PassBought>(&event)) {
        if (Sub* s = find(p->scope); s && s->holder == p->holder) {
            *handled = true;
            onPass(*s, *p);
        }
    } else if (const auto* en = std::get_if<e::PassEnded>(&event)) {
        if (Sub* s = find(en->scope); s && s->holder == en->holder) {
            *handled = true;
            if (s->state == State::Held) {
                // a renewal ended: the pass held runs to its `until` (§9.2)
                s->renewing = false;
                s->renewalEnded = en->refused;
            } else {
                s->state = en->refused ? State::Refused : State::GaveUp;
            }
            logEd3k(QStringLiteral("ed3k: paid scope %1: %2").arg(hexOf(s->scope), en->refused ? QStringLiteral("the seller will send no pass") : QStringLiteral("polling for a pass ended")));
            save();
            markChanged();
        }
    } else if (const auto* rp = std::get_if<e::PassReported>(&event)) {
        if (find(rp->scope)) {
            *handled = true;
            logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: paid scope %1: a share report %2").arg(hexOf(rp->scope), rp->ok ? QStringLiteral("taken") : QStringLiteral("not taken")); });
        }
    }
}

void Ed3kPaid::onSessionStatus()
{
    if (m_session.isRunning() && !m_attached) {
        m_attached = true;
        if (auto* e = m_session.engine())
            (void)e->passSetReports(m_reports);
        for (auto& [scope, s] : m_subs)
            attach(s);
        markChanged();
    } else if (!m_session.isRunning() && m_attached) {
        m_attached = false;
        for (auto& [scope, s] : m_subs) {
            s.onEngine = false;
            s.anonWait = false;
        }
        markChanged();
    }
}

void Ed3kPaid::attach(Sub& s)
{
    auto* e = m_session.engine();
    if (!e || s.onEngine)
        return;
    // a pass link's scope not followed yet (no publisher); a channel while watched only
    if (!s.known || (s.kind == Kind::Channel && !s.watching))
        return;
    // hops.md §1: an anonymous scope waits while hiding is off (hideChanged follows it then)
    if (s.anon && !m_session.hide().hiding())
        return;
    const auto t = m_session.now();
    const auto ok = s.anon ? e->paidAnonFollow(t, s.publisher, s.scope) : e->paidFollow(s.publisher, s.scope);
    if (!ok && ok.error() != ffi::Error::State) {
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1 not followed (error %2)").arg(hexOf(s.scope)).arg(int(ok.error())));
        return;
    }
    // s.onEngine = true;   (was: here; now after readOffer, which checks the pass when on the engine)
    if (!s.offer.isEmpty())
        (void)(s.anon ? e->paidAnonSetOffer(t, s.scope, spanOf(s.offer)) : e->paidSetOffer(t, s.scope, spanOf(s.offer)));
    (void)readOffer(s);
    s.onEngine = true;
    // setPassOnEngine(s);   (was: its result only logged, #10)
    checkPass(s, true);
    const bool checked = !s.pass.isEmpty() && s.check == Check::Ok;
    // without an offer: KeyRequests without a pass give it (§6.2); with a pass: the keys. A
    // channel's come from its mesh (§7)
    // if (s.followed && s.kind != Kind::Channel && (s.offer.isEmpty() || !s.pass.isEmpty()))
    if (s.followed && s.kind != Kind::Channel && (s.offer.isEmpty() || checked))
        wantKeys(s);   // was: (void)e->paidWantKeys(t, s.scope);
    // §9.1 "at each start": a purchase polled again; §9.2: the renewal (of a pass checked)
    if (s.state == State::Buying)
        (void)startPoll(s, true);
    else if (s.state == State::Held && checked)
        startRenewal(s);
    m_session.pump();
}

bool Ed3kPaid::startPoll(Sub& s, bool resume, QString* error)
{
    auto* e = m_session.engine();
    const auto offer = Ed3kPaidOffer::parse(s.offer);
    if (!e || !offer || !s.holder) {
        if (error)
            *error = tr("ed3k is not running.");
        return false;
    }
    const QByteArray url = offer->passUrl.toUtf8();
    const auto t = m_session.now();
    const auto ok = s.anon && !resume ? e->passPollAnon(t, s.scope, s.publisher, *s.holder, viewOf(url), s.began, spanOf(s.offer))
                                      : e->passPoll(t, s.scope, s.publisher, *s.holder, viewOf(url), s.began, 0, spanOf(s.offer), resume);
    m_session.pump();
    if (!ok && ok.error() == ffi::Error::Denied) {
        // §5.1: the user is told first that the seller sees an address
        s.anonWait = true;
        s.anonResume = resume;
        emit anonWarning(hexOf(s.scope), s.title);
        return false;
    }
    if (!ok) {
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1: pass_url not polled (error %2)").arg(hexOf(s.scope)).arg(int(ok.error())));
        if (error)
            *error = ok.error() == ffi::Error::State && s.anon ? tr("No anonymous key runs (hops are off), or the pass is held under the node key.")
                                                      : tr("The pass address of the offer cannot be used.");
        return false;
    }
    return true;
}

void Ed3kPaid::startRenewal(Sub& s)
{
    auto* e = m_session.engine();
    const auto offer = Ed3kPaidOffer::parse(s.offer);
    // a pass by hand (§9.3, began 0) is not renewed over pass_url
    // if (!e || !offer || !s.holder || s.until == 0 || offer->passUrl.isEmpty() || s.began == 0)
    // §9.2 (#11): a link/voucher pass only on the user's word (renewNow); a pass not checked
    // for the scope is not renewed either (#10)
    if (!e || !offer || !s.holder || s.until == 0 || offer->passUrl.isEmpty() || s.began == 0 || s.byHand || s.check != Check::Ok)
        return;
    const QByteArray url = offer->passUrl.toUtf8();
    // §9.2: the core polls in the last 48 h before `until`, every 6 h
    const auto ok = e->passPoll(m_session.now(), s.scope, s.publisher, *s.holder, viewOf(url), s.began, s.until, spanOf(s.offer), false);
    if (!ok && ok.error() == ffi::Error::Denied) {
        s.anonWait = true;
        s.anonResume = true;
        emit anonWarning(hexOf(s.scope), s.title);
        return;
    }
    s.renewing = ok.has_value();
}

void Ed3kPaid::onPass(Sub& s, const ffi::event::PassBought& p)
{
    // was: set here at once (now usePass, or the user's word first: §5.1, ruling #10)
    // s.pass = QByteArray(reinterpret_cast<const char*>(p.pass.data()), qsizetype(p.pass.size()));
    // s.until = p.until;
    const QByteArray pass(reinterpret_cast<const char*>(p.pass.data()), qsizetype(p.pass.size()));
    if (!p.offer.empty()) {
        const QByteArray o(reinterpret_cast<const char*>(p.offer.data()), qsizetype(p.offer.size()));
        const auto held = Ed3kPaidOffer::parse(s.offer);
        const auto got = Ed3kPaidOffer::parse(o);
        if (got && (!held || got->issued >= held->issued))
            s.offer = o;
    }
    // s.state = State::Held;
    // s.renewing = false;
    // s.renewalEnded = false;
    if (auto* e = m_session.engine()) {
        const auto t = m_session.now();
        if (!s.offer.isEmpty())
            (void)(s.anon ? e->paidAnonSetOffer(t, s.scope, spanOf(s.offer)) : e->paidSetOffer(t, s.scope, spanOf(s.offer)));
        // setPassOnEngine(s);
        // (void)e->paidWantKeys(t, s.scope);
    }
    // startRenewal(s);
    // every scope of the pass (§5: up to 16), the polled one at least
    std::vector<ffi::Scope> scopes = p.scopes;
    if (std::ranges::find(scopes, s.scope) == scopes.end())
        scopes.push_back(s.scope);
    bool waits = false;
    // a purchase is renewed by itself (§9.2); a by-hand pass renewed on the user's word stays by hand
    const bool byHand = s.state != State::Buying && s.byHand;
    if (auto seed = loadSecret(paidDir(), QFileInfo(holderFile(s)).fileName()); seed && s.holder) {
        const Secret seedS(*seed);
        wipe(std::span<std::uint8_t>{*seed});
        waits = spreadPass(s, pass, p.until, scopes, *s.holder, seedS.expose(), byHand);
    } else {
        waits = takePass(s, pass, p.until, scopes, byHand);
    }
    if (waits && s.state == State::Buying)
        s.state = State::NoPass;   // the purchase is done; the pass waits (ScopesWarn)
    logEd3k(QStringLiteral("ed3k: paid scope %1: a pass until %2%3").arg(hexOf(s.scope)).arg(p.until).arg(waits ? QStringLiteral(" (waits for the user: several scopes)") : QString()));
    save();
    if (waits)
        emit scopesWarning(hexOf(s.scope), s.title, int(scopes.size()) - 1);
    else
        emit passArrived(hexOf(s.scope), s.title, p.until);
    markChanged();
}

bool Ed3kPaid::spreadPass(Sub& first, const QByteArray& pass, std::uint64_t until, const std::vector<ffi::Scope>& scopes,
                          const ffi::Key32& holder, const ffi::Key32& seed, bool byHand)
{
    bool waits = false;
    for (const ffi::Scope& sc : scopes) {
        const bool isFirst = sc == first.scope;
        // std::map: the references stay valid while others are added
        auto [it, inserted] = m_subs.try_emplace(sc);
        Sub& t = it->second;
        if (inserted) {
            // another scope of the pass, not followed yet: it waits for its publisher (follow)
            t.scope = sc;
            t.known = false;
            t.followed = false;
        }
        // a pass or purchase of its own under another holder key stays
        if (!isFirst && t.holder && *t.holder != holder && ((!t.pass.isEmpty() && t.until >= until) || t.state == State::Buying))
            continue;
        if (t.holder != holder) {
            QDir().mkpath(paidDir());
            // the same key as a bought one: a Secret file, owner-only (§5.1 "like a password")
            if (!replaceSecret(paidDir(), QFileInfo(holderFile(t)).fileName(), seed)) {
                logEd3kWarning(QStringLiteral("ed3k: paid scope %1: the holder key was not stored").arg(hexOf(sc)));
                continue;
            }
            t.holder = holder;
        }
        if (takePass(t, pass, until, scopes, byHand))
            waits = true;
    }
    return waits;
}

bool Ed3kPaid::takePass(Sub& s, const QByteArray& pass, std::uint64_t until, const std::vector<ffi::Scope>& scopes, bool byHand)
{
    // §5.1 SHOULD, ruling #10: a pass of several scopes shows them all; the user is told first
    // const bool known = std::ranges::all_of(scopes, [&s](const ffi::Scope& sc) { return s.scopesOk.contains(hexOf(sc)); });
    // batch 15 #9: asked once per set of scopes, for every subscription (a subset asks nothing)
    const bool known = scopesAccepted(scopes);
    if (scopes.size() > 1 && !known) {
        if (s.waitingPass != pass) {
            s.waitingPass = pass;
            s.waitingUntil = until;
            s.waitingByHand = byHand;
            s.scopesDeclined = false;
        }
        return true;
    }
    s.waitingPass.clear();
    s.waitingUntil = 0;
    s.waitingByHand = false;
    s.scopesDeclined = false;
    s.byHand = byHand;
    usePass(s, pass, until);
    return false;
}

void Ed3kPaid::usePass(Sub& s, const QByteArray& pass, std::uint64_t until)
{
    s.pass = pass;
    s.until = until;
    s.state = State::Held;
    s.renewing = false;
    s.renewalEnded = false;
    // if (auto* e = m_session.engine(); e && s.onEngine) {
    //     setPassOnEngine(s);
    //     if (s.kind != Kind::Channel)
    //         (void)e->paidWantKeys(m_session.now(), s.scope);
    // }
    // startRenewal(s);
    // §9.3 (#10): keys and renewal once the pass is checked for this scope
    s.check = Check::Unchecked;
    checkPass(s);
}

void Ed3kPaid::checkPass(Sub& s, bool attaching)
{
    auto* e = m_session.engine();
    if (!e || !s.onEngine || s.pass.isEmpty())
        return;   // not followed on the engine (publisher unknown, a channel not watched): checked then
    const Check before = s.check;
    s.check = setPassOnEngine(s);
    if (s.check != before)
        markChanged();
    if (s.check != Check::Ok || attaching)
        return;
    if (s.kind != Kind::Channel)
        wantKeys(s);   // was: (void)e->paidWantKeys(m_session.now(), s.scope);
    startRenewal(s);
}

bool Ed3kPaid::scopesAccepted(const std::vector<ffi::Scope>& scopes) const
{
    if (scopes.size() <= 1)
        return true;
    return std::ranges::any_of(m_scopeSets, [&](const QStringList& set) {
        return std::ranges::all_of(scopes, [&](const ffi::Scope& sc) { return set.contains(hexOf(sc)); });
    });
}

void Ed3kPaid::acceptScopes(const std::vector<ffi::Scope>& scopes)
{
    if (scopes.size() <= 1 || scopesAccepted(scopes))
        return;
    QStringList set;
    for (const auto& sc : scopes)
        if (!set.contains(hexOf(sc)))
            set << hexOf(sc);
    set.sort();
    // the sets within the new one are covered by it
    std::erase_if(m_scopeSets, [&](const QStringList& old) { return std::ranges::all_of(old, [&](const QString& h) { return set.contains(h); }); });
    m_scopeSets.push_back(set);
}

QString Ed3kPaid::payLinkOf(const Sub& s) const
{
    const auto offer = Ed3kPaidOffer::parse(s.offer);
    if (!offer || offer->payUrl.isEmpty() || !s.holder)
        return {};
    return withQuery(offer->payUrl, s.scope, *s.holder);
}

Ed3kPaid::Check Ed3kPaid::setPassOnEngine(Sub& s)
{
    auto* e = m_session.engine();
    if (!e || s.pass.isEmpty())
        return s.check;
    auto seed = loadSecret(paidDir(), QFileInfo(holderFile(s)).fileName());
    if (!seed) {
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1: the holder key is missing").arg(hexOf(s.scope)));
        return Check::Failed;
    }
    const Secret seedS(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    const auto ok = s.anon ? e->paidAnonSetPass(m_session.now(), s.scope, spanOf(s.pass), seedS.expose())
                           : e->paidSetPass(m_session.now(), s.scope, spanOf(s.pass), seedS.expose());
    if (ok)
        return Check::Ok;
    // §9.3: an issuer-signed pass without the offer (or a scope not followed): held unused
    if (ok.error() == ffi::Error::NotFound) {
        logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: paid scope %1: the pass is not checked yet (no offer)").arg(hexOf(s.scope)); });
        return Check::Unchecked;
    }
    // if (!ok)
    logEd3kWarning(QStringLiteral("ed3k: paid scope %1: the pass was not taken (error %2)").arg(hexOf(s.scope)).arg(int(ok.error())));
    return Check::Failed;
}

bool Ed3kPaid::readOffer(Sub& s)
{
    auto* e = m_session.engine();
    if (!e)
        return false;
    const auto got = e->paidOffer(m_session.now(), s.scope, s.anon ? ffi::KeyKind::Anon : ffi::KeyKind::Node);
    if (!got || !*got)
        return false;
    const auto b = (*got)->bytes();
    const QByteArray o(reinterpret_cast<const char*>(b.data()), qsizetype(b.size()));
    const auto parsed = Ed3kPaidOffer::parse(o);
    const auto held = Ed3kPaidOffer::parse(s.offer);
    if (!parsed || (held && held->issued >= parsed->issued))
        return false;
    s.offer = o;
    // §9.3 (#10): the offer of an issuer-signed pass came (a key answer, §6.2)
    if (s.onEngine && !s.pass.isEmpty() && s.check != Check::Ok)
        checkPass(s);
    save();
    return true;
}

Ed3kPaid::Sub* Ed3kPaid::find(const ffi::Scope& scope)
{
    const auto it = m_subs.find(scope);
    return it == m_subs.end() ? nullptr : &it->second;
}

const Ed3kPaid::Sub* Ed3kPaid::find(const ffi::Scope& scope) const
{
    const auto it = m_subs.find(scope);
    return it == m_subs.end() ? nullptr : &it->second;
}

Ed3kPaid::Sub* Ed3kPaid::findKeyed(const ffi::Scope& scope, ffi::KeyKind key)
{
    Sub* s = find(scope);
    return s && s->anon == (key == ffi::KeyKind::Anon) ? s : nullptr;
}

void Ed3kPaid::hideChanged(bool hiding)
{
    bool any = false;
    for (auto& [scope, s] : m_subs) {
        if (!s.anon)
            continue;
        any = true;
        if (hiding) {
            if (m_attached)
                attach(s);
        } else {
            // the anonymous node took the scope along; nothing falls back to the node key
            s.onEngine = false;
            s.anonWait = false;
        }
    }
    if (any)
        markChanged();
}

void Ed3kPaid::detachSub(Sub& s)
{
    auto* e = m_session.engine();
    if (e && s.onEngine) {
        // was: skipped for the anonymous key
        const auto ok = s.anon ? e->paidAnonUnfollow(m_session.now(), s.scope) : e->paidUnfollow(s.scope);
        if (!ok && ok.error() != ffi::Error::NotFound && ok.error() != ffi::Error::State)
            logEd3kDebug([&]() -> QString { return QStringLiteral("ed3k: paid scope %1 not unfollowed (error %2)").arg(hexOf(s.scope)).arg(int(ok.error())); });
    }
    s.onEngine = false;
}

void Ed3kPaid::wantKeys(Sub& s)
{
    auto* e = m_session.engine();
    if (!e)
        return;
    const auto t = m_session.now();
    (void)(s.anon ? e->paidAnonWantKeys(t, s.scope) : e->paidWantKeys(t, s.scope));
}

Ed3kPaid::Info Ed3kPaid::infoOf(const Sub& s) const
{
    Info i;
    i.scope = hexOf(s.scope);
    i.kind = s.kind;
    i.title = s.title;
    i.anon = s.anon;
    i.offer = Ed3kPaidOffer::parse(s.offer);
    // i.state = s.anonWait ? State::AnonWarning : s.state;
    i.state = s.anonWait ? State::AnonWarning : !s.waitingPass.isEmpty() ? State::ScopesWarn : s.state;
    // §9.3 (#10): a pass held, not (yet) good for this scope
    if (i.state == State::Held && s.check != Check::Ok)
        i.state = s.check == Check::Failed ? State::PassFailed : State::PassUnchecked;
    i.byHand = s.byHand;
    i.renewable = !s.pass.isEmpty() && s.holder && i.offer && !i.offer->passUrl.isEmpty();
    i.passScopes = int(scopesOfPass(s.waitingPass.isEmpty() ? s.pass : s.waitingPass).size());
    i.scopesDeclined = s.scopesDeclined;
    i.until = s.until;
    i.renewing = s.renewing;
    i.renewalEnded = s.renewalEnded;
    i.newestEpoch = s.newestEpoch;
    i.keys = s.keys;
    return i;
}

QString Ed3kPaid::holderFile(const Sub& s) const
{
    return QDir(paidDir()).filePath(hexOf(s.scope) + QStringLiteral(".holder"));
}

QString Ed3kPaid::paidDir() const
{
    return QDir(stateDir(m_configDir)).filePath(QStringLiteral("paid"));
}

void Ed3kPaid::markChanged()
{
    if (m_changePending)
        return;
    m_changePending = true;
    QTimer::singleShot(300, this, [this] {
        m_changePending = false;
        emit changed();
    });
}

void Ed3kPaid::load()
{
    const QJsonObject root = QJsonDocument::fromJson(readBlob(statePath())).object();
    m_reports = root.value(QStringLiteral("reports")).toBool(true);
    for (const QJsonValue& v : root.value(QStringLiteral("subs")).toArray()) {
        const QJsonObject o = v.toObject();
        const auto scope = scopeOf(o.value(QStringLiteral("scope")).toString());
        const auto pub = arrayOf<32>(o.value(QStringLiteral("publisher")).toString());
        if (!scope || !pub)
            continue;
        Sub s;
        s.scope = *scope;
        s.publisher = *pub;
        s.kind = Kind(std::clamp(o.value(QStringLiteral("kind")).toInt(3), 1, 3));
        s.title = o.value(QStringLiteral("title")).toString();
        s.anon = o.value(QStringLiteral("anon")).toBool();
        s.followed = o.value(QStringLiteral("followed")).toBool(true);
        s.offer = QByteArray::fromBase64(o.value(QStringLiteral("offer")).toString().toLatin1());
        s.pass = QByteArray::fromBase64(o.value(QStringLiteral("pass")).toString().toLatin1());
        if (const auto h = arrayOf<32>(o.value(QStringLiteral("holder")).toString()))
            s.holder = *h;
        s.until = o.value(QStringLiteral("until")).toString().toULongLong();
        s.began = o.value(QStringLiteral("began")).toString().toULongLong();
        s.state = State(std::clamp(o.value(QStringLiteral("state")).toInt(), 0, 4));
        s.renewalEnded = o.value(QStringLiteral("renewalEnded")).toBool();
        s.known = o.value(QStringLiteral("known")).toBool(true);
        s.waitingPass = QByteArray::fromBase64(o.value(QStringLiteral("waitingPass")).toString().toLatin1());
        s.waitingUntil = o.value(QStringLiteral("waitingUntil")).toString().toULongLong();
        s.scopesDeclined = o.value(QStringLiteral("scopesDeclined")).toBool();
        for (const QJsonValue& x : o.value(QStringLiteral("scopesOk")).toArray())
            s.scopesOk << x.toString();
        s.byHand = o.value(QStringLiteral("byHand")).toBool();
        s.waitingByHand = o.value(QStringLiteral("waitingByHand")).toBool();
        // checked again on the engine (attach); a failure seen before shows until then
        s.check = o.value(QStringLiteral("passFailed")).toBool() ? Check::Failed : Check::Unchecked;
        // #9: a set accepted of an older paid.json counts for every subscription now
        if (s.scopesOk.size() > 1) {
            QStringList set = s.scopesOk;
            set.sort();
            if (std::ranges::find(m_scopeSets, set) == m_scopeSets.end())
                m_scopeSets.push_back(set);
        }
        m_subs.emplace(s.scope, std::move(s));
    }
    for (const QJsonValue& v : root.value(QStringLiteral("vouchers")).toArray())
        m_vouchers.insert(v.toString());
    for (const QJsonValue& v : root.value(QStringLiteral("scopeSets")).toArray()) {
        QStringList set;
        for (const QJsonValue& x : v.toArray())
            set << x.toString();
        set.sort();
        if (set.size() > 1 && std::ranges::find(m_scopeSets, set) == m_scopeSets.end())
            m_scopeSets.push_back(set);
    }
}

void Ed3kPaid::save() const
{
    QJsonArray subs;
    for (const auto& [scope, s] : m_subs)
        subs.append(QJsonObject{
            {QStringLiteral("scope"), hexOf(s.scope)},
            {QStringLiteral("publisher"), hex(s.publisher)},
            {QStringLiteral("kind"), int(s.kind)},
            {QStringLiteral("title"), s.title},
            {QStringLiteral("anon"), s.anon},
            {QStringLiteral("followed"), s.followed},
            {QStringLiteral("offer"), QString::fromLatin1(s.offer.toBase64())},
            {QStringLiteral("pass"), QString::fromLatin1(s.pass.toBase64())},
            {QStringLiteral("holder"), s.holder ? hex(*s.holder) : QString()},
            {QStringLiteral("until"), QString::number(s.until)},
            {QStringLiteral("began"), QString::number(s.began)},
            {QStringLiteral("state"), int(s.state)},
            {QStringLiteral("renewalEnded"), s.renewalEnded},
            {QStringLiteral("known"), s.known},
            {QStringLiteral("waitingPass"), QString::fromLatin1(s.waitingPass.toBase64())},
            {QStringLiteral("waitingUntil"), QString::number(s.waitingUntil)},
            {QStringLiteral("scopesDeclined"), s.scopesDeclined},
            // {QStringLiteral("scopesOk"), QJsonArray::fromStringList(s.scopesOk)},   (was: per subscription; now scopeSets)
            {QStringLiteral("byHand"), s.byHand},
            {QStringLiteral("waitingByHand"), s.waitingByHand},
            {QStringLiteral("passFailed"), s.check == Check::Failed},
        });
    QJsonArray vouchers;
    for (const QString& h : m_vouchers)
        vouchers.append(h);
    QJsonArray scopeSets;
    for (const QStringList& set : m_scopeSets)
        scopeSets.append(QJsonArray::fromStringList(set));
    // const QByteArray out = QJsonDocument(QJsonObject{{QStringLiteral("reports"), m_reports}, {QStringLiteral("subs"), subs}})
    //                            .toJson(QJsonDocument::Compact);
    const QByteArray out =
        QJsonDocument(QJsonObject{{QStringLiteral("reports"), m_reports},
                                  {QStringLiteral("subs"), subs},
                                  {QStringLiteral("vouchers"), vouchers},
                                  {QStringLiteral("scopeSets"), scopeSets}})
            .toJson(QJsonDocument::Compact);
    QDir().mkpath(stateDir(m_configDir));
    // a pass is no credential without its holder key (a file of its own); owner-only all the same
    if (!writeBlob(statePath(), spanOf(out)))
        logEd3kWarning(QStringLiteral("ed3k: %1 not written").arg(statePath()));
}

} // namespace eMule::ed3k
