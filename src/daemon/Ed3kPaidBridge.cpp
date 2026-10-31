/// @file Ed3kPaidBridge.cpp
/// @brief ed3k paid subscriptions over IPC — implementation.

#include "Ed3kPaidBridge.h"
#include "Ed3kOffGate.h"

#include <QCborArray>
#include <QCoreApplication>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kPaid.h"

#include <QObject>
#include <QUrl>
#endif

namespace eMule::Ed3kPaidBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

#ifdef EMULE_HAVE_ED3K
ed3k::Ed3kPaid* s_paid = nullptr;
#endif

IpcMessage failure(int seqId, const QString& error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

#ifdef EMULE_HAVE_ED3K
QCborMap toCbor(const ed3k::Ed3kPaid::Info& i)
{
    QCborMap m{
        {QStringLiteral("scope"), i.scope},
        {QStringLiteral("kind"), int(i.kind)},
        {QStringLiteral("title"), i.title},
        {QStringLiteral("anon"), i.anon},
        {QStringLiteral("state"), int(i.state)},
        {QStringLiteral("until"), qint64(i.until)},
        {QStringLiteral("renewing"), i.renewing},
        {QStringLiteral("renewalEnded"), i.renewalEnded},
        {QStringLiteral("keys"), i.keys},
        {QStringLiteral("newestEpoch"), qint64(i.newestEpoch)},
        {QStringLiteral("hasOffer"), i.offer.has_value()},
    };
    if (i.offer) {
        // what the user is shown before buying (§3: for display); the seller's host
        m.insert(QStringLiteral("price"), i.offer->price);
        m.insert(QStringLiteral("terms"), i.offer->terms);
        m.insert(QStringLiteral("payHost"), QUrl(i.offer->payUrl).host());
        m.insert(QStringLiteral("period"), qint64(i.offer->period));
        m.insert(QStringLiteral("devices"), qint64(i.offer->devices == 0 ? 3 : i.offer->devices));
        m.insert(QStringLiteral("reports"), !i.offer->reportUrl.isEmpty());
    }
    return m;
}

void answer(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    const int seq = msg.seqId();
    const auto ok = [&](const QCborValue& data = {}) { reply(IpcMessage::makeResult(seq, true, data)); };
    auto& paid = *s_paid;
    switch (msg.type()) {
    case IpcMsgType::GetEd3kPaid:
        ok(paidToCbor());
        break;
    case IpcMsgType::BuyEd3kPass:
        if (auto r = paid.buy(msg.fieldString(0), msg.fieldBool(1)))
            ok(QCborMap{{QStringLiteral("payLink"), *r}});
        else
            reply(failure(seq, r.error()));
        break;
    case IpcMsgType::ConfirmEd3kPaidAnon:
        if (auto r = paid.confirmAnon(msg.fieldString(0)))
            ok(QCborMap{{QStringLiteral("payLink"), *r}});
        else
            reply(failure(seq, QCoreApplication::translate("eMule::Ed3kPaidBridge", "Nothing waits for this subscription.")));
        break;
    case IpcMsgType::SetEd3kPassReports:
        paid.setReports(msg.fieldBool(0));
        ok();
        break;
    default:
        reply(IpcMessage::makeError(seq, 400, QStringLiteral("Unknown ed3k request")));
        break;
    }
}
#endif

} // namespace

void setPaid(ed3k::Ed3kPaid* paid)
{
#ifdef EMULE_HAVE_ED3K
    s_paid = paid;
#else
    (void)paid;
#endif
}

bool isPaidRequest(IpcMsgType type)
{
    const int t = static_cast<int>(type);
    return t >= static_cast<int>(IpcMsgType::GetEd3kPaid) && t <= static_cast<int>(IpcMsgType::SetEd3kPassReports);
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isPaidRequest(msg.type()))
        return false;
    // ed3k off: nothing under config/ed3k is written
    if (Ed3kBridge::refuseWhileOff(msg, {IpcMsgType::GetEd3kPaid}, reply))
        return true;
#ifdef EMULE_HAVE_ED3K
    if (s_paid) {
        answer(msg, reply);
        return true;
    }
#endif
    if (msg.type() == IpcMsgType::GetEd3kPaid)
        reply(IpcMessage::makeResult(msg.seqId(), true, paidToCbor()));
    else
        reply(failure(msg.seqId(), QCoreApplication::translate("eMule::Ed3kPaidBridge", "ed3k is not available.")));
    return true;
}

QCborMap paidToCbor()
{
    QCborArray list;
    bool reports = true;
    bool running = false;
#ifdef EMULE_HAVE_ED3K
    if (s_paid) {
        reports = s_paid->reports();
        running = true;
        for (const auto& i : s_paid->list())
            list.append(toCbor(i));
    }
#endif
    return QCborMap{{QStringLiteral("running"), running}, {QStringLiteral("reports"), reports}, {QStringLiteral("subs"), list}};
}

void connectPushes(ed3k::Ed3kPaid& paid, std::function<void(const IpcMessage&)> broadcast)
{
#ifdef EMULE_HAVE_ED3K
    QObject::connect(&paid, &ed3k::Ed3kPaid::changed, &paid, [broadcast] {
        IpcMessage m(IpcMsgType::PushEd3kPaidChanged, 0);
        m.append(paidToCbor());
        broadcast(m);
    });
    QObject::connect(&paid, &ed3k::Ed3kPaid::anonWarning, &paid, [broadcast](const QString& scope, const QString& title) {
        IpcMessage m(IpcMsgType::PushEd3kPaidAnonWarn, 0);
        m.append(scope);
        m.append(title);
        broadcast(m);
    });
    QObject::connect(&paid, &ed3k::Ed3kPaid::passArrived, &paid, [broadcast](const QString& scope, const QString& title, quint64 until) {
        IpcMessage m(IpcMsgType::PushEd3kPassArrived, 0);
        m.append(scope);
        m.append(title);
        m.append(qint64(until));
        broadcast(m);
    });
#else
    (void)paid;
    (void)broadcast;
#endif
}

} // namespace eMule::Ed3kPaidBridge
