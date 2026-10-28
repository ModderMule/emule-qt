/// @file Ed3kPaid.cpp
/// @brief Paid subscriptions, the buyer's side — implementation.

#include "Ed3kPaid.h"

#include "Ed3kPlumbing.h"
#include "Ed3kProto.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>

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
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kPaid::onEventPolled, Qt::DirectConnection);
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
    const bool was = s.followed;
    s.followed = true;
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
    if (auto* e = m_session.engine(); e && s->onEngine && !s->anon)
        (void)e->paidUnfollow(scope);
    s->onEngine = false;
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
            logEd3kDebug(QStringLiteral("ed3k: paid scope %1: an offer refused (error %2)").arg(hexOf(scope)).arg(int(ok.error())));
            return false;
        }
    }
    s->offer = signedOffer;
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
        if (auto* e = m_session.engine(); e && s->onEngine && !s->anon)
            (void)e->paidUnfollow(s->scope);
        s->anon = anon;
        s->onEngine = false;
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
    if (s->state == State::Held)
        startRenewal(*s);
    else
        (void)startPoll(*s, s->anonResume);
    save();
    markChanged();
    return purchase ? payLinkOf(*s) : QString();
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
    return s && !s->pass.isEmpty();
}

bool Ed3kPaid::requestKeys(const QString& scopeHex, const ffi::Source& node)
{
    const auto scope = scopeOf(scopeHex);
    auto* e = m_session.engine();
    if (!scope || !e || !find(*scope))
        return false;
    const auto ok = e->paidRequestKeys(m_session.now(), node, *scope);
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

void Ed3kPaid::onEventPolled(const ffi::Event& event, bool* handled)
{
    namespace e = ffi::event;
    if (const auto* k = std::get_if<e::PaidKeys>(&event)) {
        if (Sub* s = find(k->scope)) {
            *handled = true;
            s->keys = true;
            for (const auto ep : k->epochs)
                s->newestEpoch = std::max(s->newestEpoch, ep);
            logEd3kDebug(QStringLiteral("ed3k: paid scope %1: keys of %2 epoch(s)").arg(hexOf(s->scope)).arg(k->epochs.size()));
            emit keysArrived(s->scope);
            markChanged();
        }
    } else if (const auto* o = std::get_if<e::PaidOffer>(&event)) {
        if (Sub* s = find(o->scope)) {
            *handled = true;
            // was: ToDo: read the offer the core holds (done: paidOffer; a hidden set's comes
            // from pass holders, §6.2)
            logEd3kDebug(QStringLiteral("ed3k: paid scope %1: an offer of %2 held").arg(hexOf(s->scope)).arg(o->issued));
            readOffer(*s);
            markChanged();
        }
    } else if (const auto* r = std::get_if<e::PaidRefused>(&event)) {
        if (find(r->scope)) {
            *handled = true;
            logEd3kDebug(QStringLiteral("ed3k: paid scope %1: no key (refusal %2)").arg(hexOf(r->scope)).arg(r->refusal));
        }
    } else if (const auto* f = std::get_if<e::PaidFault>(&event)) {
        if (find(f->scope)) {
            *handled = true;
            logEd3kDebug(QStringLiteral("ed3k: paid scope %1: a key that failed; that node is no source for 24 h").arg(hexOf(f->scope)));
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
            logEd3kDebug(QStringLiteral("ed3k: paid scope %1: a share report %2").arg(hexOf(rp->scope), rp->ok ? QStringLiteral("taken") : QStringLiteral("not taken")));
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
    const auto t = m_session.now();
    const auto ok = s.anon ? e->paidAnonFollow(t, s.publisher, s.scope) : e->paidFollow(s.publisher, s.scope);
    if (!ok && ok.error() != ffi::Error::State) {
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1 not followed (error %2)").arg(hexOf(s.scope)).arg(int(ok.error())));
        return;
    }
    s.onEngine = true;
    if (!s.offer.isEmpty())
        (void)(s.anon ? e->paidAnonSetOffer(t, s.scope, spanOf(s.offer)) : e->paidSetOffer(t, s.scope, spanOf(s.offer)));
    readOffer(s);
    setPassOnEngine(s);
    // without an offer: KeyRequests without a pass give it (§6.2); with a pass: the keys
    if (s.followed && (s.offer.isEmpty() || !s.pass.isEmpty()))
        (void)e->paidWantKeys(t, s.scope);
    // §9.1 "at each start": a purchase polled again; §9.2: the renewal
    if (s.state == State::Buying)
        (void)startPoll(s, true);
    else if (s.state == State::Held)
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
    if (!e || !offer || !s.holder || s.until == 0 || offer->passUrl.isEmpty())
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
    s.pass = QByteArray(reinterpret_cast<const char*>(p.pass.data()), qsizetype(p.pass.size()));
    s.until = p.until;
    if (!p.offer.empty()) {
        const QByteArray o(reinterpret_cast<const char*>(p.offer.data()), qsizetype(p.offer.size()));
        const auto held = Ed3kPaidOffer::parse(s.offer);
        const auto got = Ed3kPaidOffer::parse(o);
        if (got && (!held || got->issued >= held->issued))
            s.offer = o;
    }
    s.state = State::Held;
    s.renewing = false;
    s.renewalEnded = false;
    if (auto* e = m_session.engine()) {
        const auto t = m_session.now();
        if (!s.offer.isEmpty())
            (void)(s.anon ? e->paidAnonSetOffer(t, s.scope, spanOf(s.offer)) : e->paidSetOffer(t, s.scope, spanOf(s.offer)));
        setPassOnEngine(s);
        (void)e->paidWantKeys(t, s.scope);
    }
    startRenewal(s);
    logEd3k(QStringLiteral("ed3k: paid scope %1: a pass until %2").arg(hexOf(s.scope)).arg(p.until));
    save();
    emit passArrived(hexOf(s.scope), s.title, p.until);
    markChanged();
}

QString Ed3kPaid::payLinkOf(const Sub& s) const
{
    const auto offer = Ed3kPaidOffer::parse(s.offer);
    if (!offer || offer->payUrl.isEmpty() || !s.holder)
        return {};
    return withQuery(offer->payUrl, s.scope, *s.holder);
}

void Ed3kPaid::setPassOnEngine(Sub& s)
{
    auto* e = m_session.engine();
    if (!e || s.pass.isEmpty())
        return;
    auto seed = loadSecret(paidDir(), QFileInfo(holderFile(s)).fileName());
    if (!seed) {
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1: the holder key is missing").arg(hexOf(s.scope)));
        return;
    }
    const Secret seedS(*seed);
    wipe(std::span<std::uint8_t>{*seed});
    const auto ok = s.anon ? e->paidAnonSetPass(m_session.now(), s.scope, spanOf(s.pass), seedS.expose())
                           : e->paidSetPass(m_session.now(), s.scope, spanOf(s.pass), seedS.expose());
    if (!ok)
        logEd3kWarning(QStringLiteral("ed3k: paid scope %1: the pass was not taken (error %2)").arg(hexOf(s.scope)).arg(int(ok.error())));
}

void Ed3kPaid::readOffer(Sub& s)
{
    auto* e = m_session.engine();
    if (!e)
        return;
    const auto got = e->paidOffer(m_session.now(), s.scope, s.anon ? ffi::KeyKind::Anon : ffi::KeyKind::Node);
    if (!got || !*got)
        return;
    const auto b = (*got)->bytes();
    const QByteArray o(reinterpret_cast<const char*>(b.data()), qsizetype(b.size()));
    const auto parsed = Ed3kPaidOffer::parse(o);
    const auto held = Ed3kPaidOffer::parse(s.offer);
    if (!parsed || (held && held->issued >= parsed->issued))
        return;
    s.offer = o;
    save();
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

Ed3kPaid::Info Ed3kPaid::infoOf(const Sub& s) const
{
    Info i;
    i.scope = hexOf(s.scope);
    i.kind = s.kind;
    i.title = s.title;
    i.anon = s.anon;
    i.offer = Ed3kPaidOffer::parse(s.offer);
    i.state = s.anonWait ? State::AnonWarning : s.state;
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
        m_subs.emplace(s.scope, std::move(s));
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
        });
    const QByteArray out = QJsonDocument(QJsonObject{{QStringLiteral("reports"), m_reports}, {QStringLiteral("subs"), subs}})
                               .toJson(QJsonDocument::Compact);
    QDir().mkpath(stateDir(m_configDir));
    // a pass is no credential without its holder key (a file of its own); owner-only all the same
    if (!writeBlob(statePath(), spanOf(out)))
        logEd3kWarning(QStringLiteral("ed3k: %1 not written").arg(statePath()));
}

} // namespace eMule::ed3k
