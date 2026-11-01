/// @file Ed3kPaidSellerBridge.cpp
/// @brief ed3k selling over IPC — implementation.

#include "Ed3kPaidSellerBridge.h"
#include "Ed3kOffGate.h"

#include <QCborArray>
#include <QCoreApplication>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kLiveBroadcast.h"
#include "Ed3kPaidSeller.h"

#include <QObject>
#endif

namespace eMule::Ed3kPaidSellerBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

#ifdef EMULE_HAVE_ED3K
ed3k::Ed3kPaidSeller* s_seller = nullptr;
ed3k::Ed3kLiveBroadcast* s_broadcast = nullptr;
#endif

IpcMessage failure(int seqId, const QString& error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

#ifdef EMULE_HAVE_ED3K
using Terms = ed3k::Ed3kPaidSeller::Terms;

QCborMap termsToCbor(const Terms& t)
{
    return QCborMap{
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

/// the keys present in @p m over @p base (the rest of base stays)
Terms termsOf(const QCborMap& m, Terms base)
{
    const auto str = [&](const char* k, QString& out) {
        if (m.contains(QLatin1String(k)))
            out = m.value(QLatin1String(k)).toString().trimmed();
    };
    const auto num = [&](const char* k, std::uint32_t& out, qint64 lo, qint64 hi) {
        if (m.contains(QLatin1String(k)))
            out = std::uint32_t(std::clamp<qint64>(m.value(QLatin1String(k)).toInteger(), lo, hi));
    };
    str("price", base.price);
    str("payUrl", base.payUrl);
    str("passUrl", base.passUrl);
    str("reportUrl", base.reportUrl);
    if (m.contains(QStringLiteral("terms")))
        base.terms = m.value(QStringLiteral("terms")).toString();
    num("freeAfter", base.freeAfter, 0, 1 << 20);
    num("devices", base.devices, 0, 64);   // 64: refused by the seller (§5.3: 1 to 63)
    num("period", base.period, 0, 2678400);
    num("seasonLength", base.seasonLength, 0, 1 << 20);
    num("passDays", base.passDays, 1, 3660);
    return base;
}

QCborMap toCbor(const ed3k::Ed3kPaidSeller::Info& i)
{
    QCborArray issued;
    for (const auto& p : i.issued)
        issued.append(QCborMap{
            {QStringLiteral("scopes"), QCborArray::fromStringList(p.scopes)},
            {QStringLiteral("holder"), p.holder},
            {QStringLiteral("issued"), qint64(p.issued)},
            {QStringLiteral("until"), qint64(p.until)},
            {QStringLiteral("voucher"), p.voucher},
            {QStringLiteral("note"), p.note},
            {QStringLiteral("link"), p.link},
        });
    QCborMap m = termsToCbor(i.terms);
    m.insert(QStringLiteral("scope"), i.scope);
    m.insert(QStringLiteral("kind"), int(i.kind));
    m.insert(QStringLiteral("title"), i.title);
    m.insert(QStringLiteral("publisher"), i.publisher);
    m.insert(QStringLiteral("target"), i.target);
    m.insert(QStringLiteral("link"), i.link);
    m.insert(QStringLiteral("t0"), qint64(i.t0));
    m.insert(QStringLiteral("epoch"), qint64(i.epoch));
    m.insert(QStringLiteral("seasonBase"), qint64(i.seasonBase));
    m.insert(QStringLiteral("seasonEpochs"), qint64(i.seasonLength));
    m.insert(QStringLiteral("offerIssued"), qint64(i.offerIssued));
    m.insert(QStringLiteral("planned"), i.planned);
    m.insert(QStringLiteral("ownUntil"), qint64(i.ownUntil));
    m.insert(QStringLiteral("running"), i.onEngine);
    m.insert(QStringLiteral("issued"), issued);
    return m;
}

void answer(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    const int seq = msg.seqId();
    const auto ok = [&](const QCborValue& data = {}) { reply(IpcMessage::makeResult(seq, true, data)); };
    auto& seller = *s_seller;
    switch (msg.type()) {
    case IpcMsgType::GetEd3kSelling:
        ok(sellingToCbor());
        break;
    case IpcMsgType::SellEd3k: {
        const qint64 kind = msg.fieldInt(0);
        const QString target = msg.fieldString(1);
        const QString title = msg.fieldString(2).trimmed();
        const Terms terms = termsOf(msg.fieldMap(3), seller.defaults());
        std::expected<QString, QString> r = std::unexpected(QCoreApplication::translate("eMule::Ed3kPaidSellerBridge", "Unknown kind of sale."));
        if (kind == int(ed3k::Ed3kPaidSeller::Kind::Channel)) {
            r = s_broadcast ? seller.sellBroadcast(*s_broadcast, target, terms) : std::unexpected(QCoreApplication::translate("eMule::Ed3kPaidSellerBridge", "Live streams are not available."));
        } else if (kind == int(ed3k::Ed3kPaidSeller::Kind::Set)) {
            // ToDo (Ed3kPaidSeller.h): a set of the user's own; a new one only for now
            r = target.isEmpty() ? seller.newSet(title, terms) : seller.setTerms(target, terms);
        }
        if (!r) {
            reply(failure(seq, r.error()));
            break;
        }
        const auto i = seller.info(*r);
        ok(QCborMap{{QStringLiteral("scope"), *r}, {QStringLiteral("link"), i ? i->link : QString()}});
        break;
    }
    case IpcMsgType::IssueEd3kPass: {
        QStringList scopes;
        for (const auto& v : msg.fieldArray(0))
            scopes << v.toString();
        const QString holderText = msg.fieldString(1).trimmed();
        std::optional<::ed3k::Key32> holder;
        if (!holderText.isEmpty()) {
            holder = ed3k::Ed3kPaidSeller::parseHolder(holderText);
            if (!holder) {
                reply(failure(seq, QCoreApplication::translate("eMule::Ed3kPaidSellerBridge", "The buyer's key is not a key (52 base32 characters or 64 hex digits).")));
                break;
            }
        }
        // a voucher's link is a credential: in this answer only, never logged
        if (auto r = seller.issuePass(scopes, holder, std::uint64_t(std::max<qint64>(msg.fieldInt(2), 0)), msg.fieldString(3)))
            ok(QCborMap{{QStringLiteral("link"), *r}, {QStringLiteral("voucher"), !holder.has_value()}});
        else
            reply(failure(seq, r.error()));
        break;
    }
    case IpcMsgType::Ed3kSellControl: {
        const QString scope = msg.fieldString(1);
        switch (msg.fieldInt(0)) {
        case 1:
            if (auto r = seller.newSeasonAfterLeak(scope))
                ok();
            else
                reply(failure(seq, r.error()));
            break;
        case 2: {
            const auto i = seller.info(scope);
            if (!i) {
                reply(failure(seq, QCoreApplication::translate("eMule::Ed3kPaidSellerBridge", "This scope is not sold here.")));
                break;
            }
            if (auto r = seller.setTerms(scope, termsOf(msg.fieldMap(2), i->terms)))
                ok();
            else
                reply(failure(seq, r.error()));
            break;
        }
        case 3:
            seller.setDefaults(termsOf(msg.fieldMap(2), seller.defaults()));
            ok();
            break;
        default:
            reply(failure(seq, QCoreApplication::translate("eMule::Ed3kPaidSellerBridge", "Unknown action.")));
            break;
        }
        break;
    }
    default:
        reply(IpcMessage::makeError(seq, 400, QStringLiteral("Unknown ed3k request")));
        break;
    }
}
#endif

} // namespace

void setSeller(ed3k::Ed3kPaidSeller* seller, ed3k::Ed3kLiveBroadcast* broadcast)
{
#ifdef EMULE_HAVE_ED3K
    s_seller = seller;
    s_broadcast = broadcast;
#else
    (void)seller;
    (void)broadcast;
#endif
}

bool isSellerRequest(IpcMsgType type)
{
    return type == IpcMsgType::GetEd3kSelling || type == IpcMsgType::SellEd3k || type == IpcMsgType::IssueEd3kPass
           || type == IpcMsgType::Ed3kSellControl;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isSellerRequest(msg.type()))
        return false;
    // ed3k off: nothing under config/ed3k is written
    if (Ed3kBridge::refuseWhileOff(msg, {IpcMsgType::GetEd3kSelling}, reply))
        return true;
#ifdef EMULE_HAVE_ED3K
    if (s_seller) {
        answer(msg, reply);
        return true;
    }
#endif
    if (msg.type() == IpcMsgType::GetEd3kSelling)
        reply(IpcMessage::makeResult(msg.seqId(), true, sellingToCbor()));
    else
        reply(failure(msg.seqId(), QCoreApplication::translate("eMule::Ed3kPaidSellerBridge", "ed3k is not available.")));
    return true;
}

QCborMap sellingToCbor()
{
    QCborArray list;
    QCborMap defaults;
    bool running = false;
#ifdef EMULE_HAVE_ED3K
    if (s_seller) {
        running = true;
        defaults = termsToCbor(s_seller->defaults());
        for (const auto& i : s_seller->list())
            list.append(toCbor(i));
    }
#endif
    return QCborMap{{QStringLiteral("running"), running}, {QStringLiteral("defaults"), defaults}, {QStringLiteral("sales"), list}};
}

void connectPushes(ed3k::Ed3kPaidSeller& seller, std::function<void(const IpcMessage&)> broadcast)
{
#ifdef EMULE_HAVE_ED3K
    QObject::connect(&seller, &ed3k::Ed3kPaidSeller::changed, &seller, [broadcast] {
        IpcMessage m(IpcMsgType::PushEd3kSellingChanged, 0);
        m.append(sellingToCbor());
        broadcast(m);
    });
    QObject::connect(&seller, &ed3k::Ed3kPaidSeller::seasonStarted, &seller,
                     [broadcast](const QString& scope, const QString& title, quint64 base) {
                         IpcMessage m(IpcMsgType::PushEd3kSeasonStarted, 0);
                         m.append(scope);
                         m.append(title);
                         m.append(qint64(base));
                         broadcast(m);
                     });
#else
    (void)seller;
    (void)broadcast;
#endif
}

} // namespace eMule::Ed3kPaidSellerBridge
