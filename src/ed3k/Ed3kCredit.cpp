/// @file Ed3kCredit.cpp
/// @brief ed3k credits in the daemon — implementation.

#include "Ed3kCredit.h"

#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "utils/Log.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace eMule::ed3k {

namespace ffi = ::ed3k;

namespace {

const QString kKeyFile = QStringLiteral("credit.key");
/// the claims of own burns (§6.7): ids, to, amounts; public on the chain, not a secret
const QString kClaimsFile = QStringLiteral("stake-claims.json");

QString hexOf(std::span<const std::uint8_t> bytes)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).toHex());
}

} // namespace

Ed3kCredit::Ed3kCredit(Ed3kSession& session, const QString& configDir, QObject* parent)
    : QObject(parent)
    , m_session(session)
    , m_dir(stateDir(configDir))
{
    m_changed.setSingleShot(true);
    m_changed.setInterval(1000);
    connect(&m_changed, &QTimer::timeout, this, &Ed3kCredit::statusChanged);
    connect(&m_session, &Ed3kSession::eventPolled, this, &Ed3kCredit::onEventPolled, Qt::DirectConnection);
    connect(&m_session, &Ed3kSession::statusChanged, this, &Ed3kCredit::onSessionStatus);
    readAddress();
    loadClaims();
}

Ed3kCredit::~Ed3kCredit() = default;

void Ed3kCredit::setConfig(const Ed3kCreditPrefs& prefs, int endpoints)
{
    const bool market = prefs.bidMaxPrice != m_prefs.bidMaxPrice || prefs.sell != m_prefs.sell
                        || prefs.sellFloor != m_prefs.sellFloor || prefs.sellSlots != m_prefs.sellSlots;
    m_prefs = prefs;
    m_endpoints = std::max(endpoints, 0);
    const bool was = m_configured;
    apply();
    // a new configuration applies the market in apply(); else only what moved
    if (m_configured && was && market)
        applyMarket();
    changed(true);
}

Ed3kCredit::Status Ed3kCredit::status() const
{
    Status st = m_status;
    st.enabled = m_prefs.enabled;
    st.configured = m_configured;
    st.endpoints = m_configured ? m_endpoints : 0;
    st.address = m_address;
    st.problem = m_problem;
    return st;
}

std::optional<QString> Ed3kCredit::newKey(QString* error)
{
    // a fresh key from the CSPRNG that is a key (§2), written over the old file
    QString err;
    const QString tmpName = kKeyFile + QStringLiteral(".new");
    QFile::remove(QDir(m_dir).filePath(tmpName));
    auto key = loadOrCreateSecret(m_dir, tmpName, [](const Seed& s) { return ffi::creditAddress(s).has_value(); }, &err);
    QFile::remove(QDir(m_dir).filePath(tmpName));
    if (!key) {
        if (error)
            *error = err;
        return std::nullopt;
    }
    const bool ok = replaceSecret(m_dir, kKeyFile, *key, &err);
    wipe(std::span<std::uint8_t>{*key});
    if (!ok) {
        if (error)
            *error = err;
        return std::nullopt;
    }
    logEd3k(QStringLiteral("ed3k: a new credit key was made"));
    readAddress();
    // m_status = {};   // a new address: no balance, immature
    m_status = Status{.claims = std::move(m_status.claims)};   // claims stay: anyone may claim them (§6.7)
    apply();
    changed(true);
    return m_address;
}

std::optional<QString> Ed3kCredit::importKey(const QByteArray& key, QString* error)
{
    Seed seed{};
    if (key.size() != qsizetype(seed.size())) {
        if (error)
            *error = QStringLiteral("a credit key is 32 bytes");
        return std::nullopt;
    }
    std::copy(key.cbegin(), key.cend(), reinterpret_cast<char*>(seed.data()));
    const bool valid = ffi::creditAddress(seed).has_value();
    QString err;
    const bool ok = valid && replaceSecret(m_dir, kKeyFile, seed, &err);
    wipe(std::span<std::uint8_t>{seed});
    if (!ok) {
        if (error)
            *error = valid ? err : QStringLiteral("not a credit key");
        return std::nullopt;
    }
    logEd3k(QStringLiteral("ed3k: a credit key was imported"));
    readAddress();
    // m_status = {};
    m_status = Status{.claims = std::move(m_status.claims)};
    apply();
    changed(true);
    return m_address;
}

QByteArray Ed3kCredit::exportKey() const
{
    auto key = loadSecret(m_dir, kKeyFile);
    if (!key)
        return {};
    QByteArray out(reinterpret_cast<const char*>(key->data()), qsizetype(key->size()));
    wipe(std::span<std::uint8_t>{*key});
    return out;
}

QString Ed3kCredit::addressText(const ffi::event::CreditAddress& addr)
{
    return QStringLiteral("0x") + hexOf(addr);
}

std::optional<ffi::event::CreditAddress> Ed3kCredit::parseAddress(const QString& text)
{
    const QString t = text.trimmed();
    if (t.size() != 42 || !t.startsWith(QLatin1StringView("0x"), Qt::CaseInsensitive))
        return std::nullopt;
    for (const QChar c : t.mid(2))
        if (!c.isDigit() && !(c.toLower() >= u'a' && c.toLower() <= u'f'))
            return std::nullopt;
    const QByteArray raw = QByteArray::fromHex(t.mid(2).toLatin1());
    ffi::event::CreditAddress out{};
    if (raw.size() != qsizetype(out.size()))
        return std::nullopt;
    std::copy(raw.cbegin(), raw.cend(), reinterpret_cast<char*>(out.data()));
    return out;
}

QString Ed3kCredit::keyFileName()
{
    return kKeyFile;
}

QString Ed3kCredit::claimsFileName()
{
    return kClaimsFile;
}

ffi::Result<> Ed3kCredit::burnForStake(std::uint64_t micro, const ffi::event::CreditAddress& to)
{
    auto* engine = m_session.engine();
    if (!engine || !m_configured)
        return std::unexpected(ffi::Error::State);
    // §9.1 "No burn": the main key only (anon false); no anonymous credit address exists here
    // (ToDo of the anonymous key), so no `to` can name one
    auto ok = engine->creditBurnForStake(m_session.now(), false, micro, to);
    if (ok)
        logEd3k(QStringLiteral("ed3k: stake: burning %1 µ for %2").arg(micro).arg(addressText(to)));
    else
        logEd3kWarning(QStringLiteral("ed3k: stake: burn refused (error %1)").arg(int(ok.error())));
    m_session.pump();
    return ok;
}

ffi::Result<> Ed3kCredit::claimStake(std::uint64_t id)
{
    auto* engine = m_session.engine();
    StakeClaim* c = claim(id);
    if (!engine || !m_configured || !c)
        return std::unexpected(!c ? ffi::Error::NotFound : ffi::Error::State);
    if (c->state == ClaimState::Paid)
        return std::unexpected(ffi::Error::State);
    auto ok = engine->creditClaimStake(m_session.now(), false, id);
    if (ok) {
        c->state = ClaimState::Claiming;
        saveClaims();
        emit stakeChanged();
        changed(true);
    } else {
        logEd3kWarning(QStringLiteral("ed3k: stake: claim %1 refused (error %2)").arg(id).arg(int(ok.error())));
    }
    m_session.pump();
    return ok;
}

void Ed3kCredit::refreshClaims()
{
    auto* engine = m_session.engine();
    if (!engine || !m_configured || m_endpoints == 0)
        return;
    for (const StakeClaim& c : m_status.claims)
        if (c.state != ClaimState::Paid)
            (void)engine->creditStakeClaim(m_session.now(), c.id);
    m_session.pump();
}

void Ed3kCredit::onEventPolled(const ffi::Event& ev, bool* handled)
{
    namespace e = ffi::event;
    if (const auto* fs = std::get_if<e::FileStatus>(&ev)) {
        // not handled here: the joins answer it; only the root is noted for the bid
        if (m_configured && m_roots.insert(fs->root).second)
            bid(fs->root);
        return;
    }
    if (const auto* s = std::get_if<e::CreditSigned>(&ev)) {
        ++m_status.receiptsSigned;
        m_status.kibSigned += s->kib;
    } else if (const auto* r = std::get_if<e::CreditReceipt>(&ev)) {
        ++m_status.receiptsTaken;
        m_status.kibTaken += r->kib;
    } else if (const auto* h = std::get_if<e::CreditSlotHint>(&ev)) {
        // the queue policy granted it already (ffi.md: the engine acts on its own hint)
        ++m_status.slotHints;
        logEd3kDebug(QStringLiteral("ed3k: credit: priority turn to %1 at %2 µ/MiB")
                         .arg(hexOf(h->node.bytes).left(8)).arg(h->price));
    } else if (std::holds_alternative<e::CreditPaymentLate>(ev)) {
        ++m_status.lateBids;
    } else if (const auto* t = std::get_if<e::CreditTx>(&ev)) {
        m_status.lastTxWhat = t->what;
        m_status.lastTxState = t->state;
        m_status.lastTxRevert = t->revert;
        m_status.lastTx = t->tx ? hexOf(*t->tx) : QString();
        logEd3k(QStringLiteral("ed3k: credit transaction %1: state %2 (revert %3) %4")
                    .arg(t->what).arg(t->state).arg(t->revert).arg(m_status.lastTx));
        if (t->what == ED3K_CREDIT_TX_BURN_FOR_STAKE || t->what == ED3K_CREDIT_TX_CLAIM_STAKE) {
            // a claim that did not go through can be sent again (the tx names no claim id)
            if (t->what == ED3K_CREDIT_TX_CLAIM_STAKE
                && (t->state == ED3K_CREDIT_STATE_REVERTED || t->state == ED3K_CREDIT_STATE_FAILED)) {
                for (StakeClaim& c : m_status.claims)
                    if (c.state == ClaimState::Claiming)
                        c.state = ClaimState::Waiting;
                saveClaims();
                emit stakeChanged();
            }
            emit stakeTx(int(t->what), int(t->state), int(t->revert), m_status.lastTx);
        }
        changed(true);
    } else if (const auto* b = std::get_if<e::CreditBalance>(&ev)) {
        m_status.balance = b->balance;
        changed(true);
    } else if (const auto* w = std::get_if<e::CreditWrongChain>(&ev)) {
        logEd3kWarning(QStringLiteral("ed3k: credit ledger endpoint %1 is on chain %2, not %3; not used again")
                           .arg(w->endpoint).arg(w->chainId).arg(m_prefs.chainId));
        if (std::ranges::find(m_status.wrongChain, w->endpoint) == m_status.wrongChain.end())
            m_status.wrongChain.push_back(w->endpoint);
        changed(true);
    } else if (std::holds_alternative<e::CreditOverdrawn>(ev)) {
        // §6.4: for good; a new credit address is the way on
        logEd3kWarning(QStringLiteral("ed3k: the credit account is overdrawn (credit.md §6.4)"));
        m_status.overdrawn = true;
        changed(true);
    } else if (std::holds_alternative<e::CreditMature>(ev)) {
        logEd3k(QStringLiteral("ed3k: the credit account is mature: signed receipts mint"));
        m_status.mature = true;
        changed(true);
    } else if (const auto* p = std::get_if<e::CreditStakeParams>(&ev)) {
        m_status.stakeRate = p->rate;
        m_status.stakeDelay = p->delay;
        changed(true);
    } else if (std::holds_alternative<e::CreditStakeBurned>(ev) || std::holds_alternative<e::CreditStakeClaimed>(ev)
               || std::holds_alternative<e::CreditStakeClaim>(ev)) {
        // was: ToDo: keep the claims across restarts (a file) once burns can be made from the GUI
        // (done: stake-claims.json, onStakeEvent)
        // m_status.claims.push_back(StakeClaim{sb->id, sb->to, sb->amount, sb->readyAt});
        // std::erase_if(m_status.claims, [id = sc->id](const StakeClaim& c) { return c.id == id; });
        onStakeEvent(ev);
        changed(true);
    } else if (std::holds_alternative<e::CreditOffer>(ev) || std::holds_alternative<e::CreditRelayed>(ev)
               || std::holds_alternative<e::CreditRelayAsked>(ev)) {
        // informational; nothing to answer
    } else {
        return;
    }
    *handled = true;
    changed();
}

void Ed3kCredit::onSessionStatus()
{
    if (!m_session.isRunning()) {
        if (m_configured) {
            m_configured = false;
            m_configuredAs.clear();
            m_roots.clear();
            changed(true);
        }
        return;
    }
    if (!m_configured)
        apply();
}

void Ed3kCredit::apply()
{
    auto* engine = m_session.engine();
    if (!engine) {
        m_problem = m_prefs.enabled ? QStringLiteral("the ed3k node is not running") : QString();
        return;
    }
    if (!m_prefs.enabled) {
        unconfigure({});
        return;
    }
    // §7.1: both parameters from the network, no defaults
    const auto contract = parseAddress(m_prefs.contract);
    if (m_prefs.chainId == 0 || !contract) {
        unconfigure(QStringLiteral("no chain ID or ledger contract set"));
        return;
    }
    QString err;
    auto key = loadKey(true, &err);
    if (!key) {
        unconfigure(QStringLiteral("no credit key: %1").arg(err));
        return;
    }
    readAddress();
    const auto endpoints = std::uint32_t(m_endpoints);
    // §7.3: quorum of different operators; never above what there is
    const std::uint32_t quorum = endpoints == 0 ? 0 : std::clamp<std::uint32_t>(m_prefs.quorum, 1, endpoints);
    const QString as = QStringLiteral("%1/%2/%3/%4/%5").arg(m_prefs.chainId).arg(m_prefs.contract.toLower())
                           .arg(endpoints).arg(quorum).arg(m_address);
    if (m_configured && as == m_configuredAs) {
        wipe(std::span<std::uint8_t>{*key});
        return;
    }
    const auto ok = engine->creditConfigure(m_session.now(), *key, m_prefs.chainId, *contract, endpoints, quorum);
    wipe(std::span<std::uint8_t>{*key});
    if (!ok) {
        // Unsupported: the credit extension is off (ed3k.extOff)
        m_configured = false;
        m_configuredAs.clear();
        m_problem = ok.error() == ffi::Error::Unsupported ? QStringLiteral("the credit extension is turned off")
                                                          : QStringLiteral("not configured (error %1)").arg(int(ok.error()));
        logEd3kWarning(QStringLiteral("ed3k: credits: %1").arg(m_problem));
        return;
    }
    m_configured = true;
    m_configuredAs = as;
    m_problem = endpoints == 0 ? QStringLiteral("no ledger endpoints: receipts are kept, nothing is sold or settled")
                               : QString();
    m_status.wrongChain.clear();
    logEd3k(QStringLiteral("ed3k: credits on (chain %1, %2 ledger endpoint(s), quorum %3)")
                .arg(m_prefs.chainId).arg(endpoints).arg(quorum));
    applyMarket();
    if (endpoints > 0)
        (void)engine->creditStakeParams(m_session.now());
    refreshClaims();
    m_session.pump();
}

void Ed3kCredit::applyMarket()
{
    auto* engine = m_session.engine();
    if (!engine || !m_configured)
        return;
    // §5.1: the lowest bid taken; slot count 0 follows the queue policy's slots
    const std::optional<std::uint64_t> floor = m_prefs.sell ? std::optional(m_prefs.sellFloor) : std::nullopt;
    if (auto ok = engine->creditSell(m_session.now(), floor, m_prefs.sellSlots); !ok && m_prefs.sell)
        logEd3kWarning(QStringLiteral("ed3k: credits: no priority sold (error %1)").arg(int(ok.error())));
    for (const auto& root : m_roots)
        bid(root);
    m_session.pump();
}

void Ed3kCredit::bid(const ffi::Root& root)
{
    auto* engine = m_session.engine();
    if (!engine)
        return;
    // 0 takes a bid away (receipts only, §5.4)
    if (auto ok = engine->creditBid(m_session.now(), root, m_prefs.bidMaxPrice); !ok)
        logEd3kDebug(QStringLiteral("ed3k: credits: no bid for %1 (error %2)").arg(hexOf(root.bytes).left(8)).arg(int(ok.error())));
}

void Ed3kCredit::unconfigure(const QString& why)
{
    m_problem = why;
    if (!m_configured)
        return;
    if (auto* engine = m_session.engine())
        (void)engine->creditOff(m_session.now());
    m_configured = false;
    m_configuredAs.clear();
    logEd3k(why.isEmpty() ? QStringLiteral("ed3k: credits off") : QStringLiteral("ed3k: credits off: %1").arg(why));
    m_session.pump();
}

std::optional<ffi::Key32> Ed3kCredit::loadKey(bool create, QString* error) const
{
    std::optional<Seed> key = create ? loadOrCreateSecret(m_dir, kKeyFile, [](const Seed& s) { return ffi::creditAddress(s).has_value(); }, error)
                                     : loadSecret(m_dir, kKeyFile);
    if (!key)
        return std::nullopt;
#ifdef Q_OS_UNIX
    // as private as the node key (§2): owner-only even when it came from elsewhere
    QFile f(QDir(m_dir).filePath(kKeyFile));
    if (f.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther | QFileDevice::WriteGroup | QFileDevice::WriteOther))
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
    if (!ffi::creditAddress(*key)) {
        wipe(std::span<std::uint8_t>{*key});
        if (error)
            *error = QStringLiteral("%1 is not a credit key").arg(kKeyFile);
        return std::nullopt;
    }
    return key;
}

void Ed3kCredit::readAddress()
{
    m_address.clear();
    auto key = loadKey(false, nullptr);
    if (!key)
        return;
    if (const auto addr = ffi::creditAddress(*key))
        m_address = addressText(*addr);
    wipe(std::span<std::uint8_t>{*key});
}

void Ed3kCredit::changed(bool now)
{
    if (now) {
        m_changed.stop();
        emit statusChanged();
    } else if (!m_changed.isActive()) {
        m_changed.start();
    }
}

void Ed3kCredit::onStakeEvent(const ffi::Event& ev)
{
    namespace e = ffi::event;
    if (const auto* sb = std::get_if<e::CreditStakeBurned>(&ev)) {
        logEd3k(QStringLiteral("ed3k: stake: burn mined, claim %1 for %2 ready at %3")
                    .arg(sb->id).arg(addressText(sb->to)).arg(sb->readyAt));
        if (StakeClaim* c = claim(sb->id))
            *c = StakeClaim{sb->id, sb->to, sb->amount, sb->readyAt, ClaimState::Waiting};
        else
            m_status.claims.push_back(StakeClaim{sb->id, sb->to, sb->amount, sb->readyAt, ClaimState::Waiting});
    } else if (const auto* sc = std::get_if<e::CreditStakeClaimed>(&ev)) {
        logEd3k(QStringLiteral("ed3k: stake: claim %1 paid to %2").arg(sc->id).arg(addressText(sc->to)));
        if (StakeClaim* c = claim(sc->id)) {
            c->state = ClaimState::Paid;
            c->amount = sc->amount;
        }
    } else if (const auto* r = std::get_if<e::CreditStakeClaim>(&ev)) {
        StakeClaim* c = claim(r->id);
        if (!c || r->failed)
            return;   // not ours, or the read failed: as it was
        if (!r->to) {
            // §6.7: a paid claim reads `to` = 0 (never made does not happen for a kept one)
            c->state = ClaimState::Paid;
        } else {
            c->to = *r->to;
            c->readyAt = r->readyAt;
            c->amount = r->amount;
        }
    } else {
        return;
    }
    saveClaims();
    emit stakeChanged();
}

void Ed3kCredit::loadClaims()
{
    m_status.claims.clear();
    QFile f(QDir(m_dir).filePath(kClaimsFile));
    if (!f.exists())
        return;
    if (!f.open(QIODevice::ReadOnly)) {
        logEd3kWarning(QStringLiteral("ed3k: %1 not readable").arg(kClaimsFile));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.read(4 * 1024 * 1024));
    if (!doc.isArray()) {
        logEd3kWarning(QStringLiteral("ed3k: %1 does not parse, ignored").arg(kClaimsFile));
        return;
    }
    for (const QJsonValue& v : doc.array()) {
        const QJsonObject o = v.toObject();
        bool okId = false;
        bool okAt = false;
        StakeClaim c;
        c.id = o.value(QStringLiteral("id")).toString().toULongLong(&okId);
        c.readyAt = o.value(QStringLiteral("readyAt")).toString().toULongLong(&okAt);
        const auto to = parseAddress(o.value(QStringLiteral("to")).toString());
        const QByteArray amount = QByteArray::fromHex(o.value(QStringLiteral("amount")).toString().toLatin1());
        const int state = o.value(QStringLiteral("state")).toInt(0);
        if (!okId || c.id == 0 || !okAt || !to || amount.size() != qsizetype(c.amount.size()) || state < 0 || state > 2) {
            logEd3kWarning(QStringLiteral("ed3k: a claim of %1 does not parse, dropped").arg(kClaimsFile));
            continue;
        }
        c.to = *to;
        std::copy(amount.cbegin(), amount.cend(), reinterpret_cast<char*>(c.amount.data()));
        // a claim sent before a restart: its outcome is read again (refreshClaims)
        c.state = state == int(ClaimState::Paid) ? ClaimState::Paid : ClaimState::Waiting;
        if (!claim(c.id))
            m_status.claims.push_back(c);
    }
}

void Ed3kCredit::saveClaims() const
{
    QJsonArray out;
    for (const StakeClaim& c : m_status.claims)
        // u64 as text: a JSON number is a double
        out.append(QJsonObject{{QStringLiteral("id"), QString::number(c.id)},
                               {QStringLiteral("to"), addressText(c.to)},
                               {QStringLiteral("amount"), hexOf(c.amount)},
                               {QStringLiteral("readyAt"), QString::number(c.readyAt)},
                               {QStringLiteral("state"), int(c.state)}});
    const QByteArray data = QJsonDocument(out).toJson(QJsonDocument::Indented);
    QDir().mkpath(m_dir);
    if (!writeBlob(QDir(m_dir).filePath(kClaimsFile), std::as_bytes(std::span{data.constData(), std::size_t(data.size())})))
        logEd3kWarning(QStringLiteral("ed3k: %1 not written").arg(kClaimsFile));
}

Ed3kCredit::StakeClaim* Ed3kCredit::claim(std::uint64_t id)
{
    const auto it = std::ranges::find(m_status.claims, id, &StakeClaim::id);
    return it == m_status.claims.end() ? nullptr : &*it;
}

} // namespace eMule::ed3k
