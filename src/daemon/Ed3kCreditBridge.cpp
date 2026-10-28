/// @file Ed3kCreditBridge.cpp
/// @brief ed3k credits over IPC — implementation.

#include "Ed3kCreditBridge.h"

#include <QCborArray>
#include <QCborMap>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kChain.h"
#include "Ed3kCredit.h"
#include "Ed3kStore.h"

#include <QObject>
#endif

namespace eMule::Ed3kCreditBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

ed3k::Ed3kCredit* s_credit = nullptr;
ed3k::Ed3kChain* s_chain = nullptr;

IpcMessage failure(int seqId, const QString& error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

#ifdef EMULE_HAVE_ED3K
QString hexOf(std::span<const std::uint8_t> bytes)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).toHex());
}

QCborArray claimsToCbor(const std::vector<ed3k::Ed3kCredit::StakeClaim>& claims)
{
    QCborArray out;
    for (const auto& cl : claims)
        out.append(QCborMap{{QStringLiteral("id"), qint64(cl.id)},
                            {QStringLiteral("to"), ed3k::Ed3kCredit::addressText(cl.to)},
                            {QStringLiteral("amount"), hexOf(cl.amount)},
                            {QStringLiteral("readyAt"), qint64(cl.readyAt)},
                            // one sent before a restart, sent again once read unpaid
                            {QStringLiteral("state"), int(cl.reclaim ? ed3k::Ed3kCredit::ClaimState::Claiming : cl.state)},
                            {QStringLiteral("burnTx"), cl.burnTx},
                            {QStringLiteral("claimTx"), cl.claimTx}});
    return out;
}

QCborMap lookupToCbor(quint64 id, bool found, bool failed, const QString& to, const QString& amount, quint64 readyAt, bool kept,
                      bool paid)
{
    const auto i64 = [](quint64 v) { return qint64(std::min<quint64>(v, INT64_MAX)); };
    return QCborMap{{QStringLiteral("id"), i64(id)},          {QStringLiteral("found"), found},
                    {QStringLiteral("failed"), failed},       {QStringLiteral("to"), to},
                    {QStringLiteral("amount"), amount},       {QStringLiteral("readyAt"), i64(readyAt)},
                    {QStringLiteral("kept"), kept},           {QStringLiteral("paid"), paid}};
}

/// error as an ed3k::Error code (0: no node or credits not in use)
IpcMessage stakeResult(int seq, const ::ed3k::Result<>& ok)
{
    return ok ? IpcMessage::makeResult(seq, true)
              : IpcMessage::makeResult(seq, false, QCborMap{{QStringLiteral("error"), int(ok.error())}});
}

void answer(const IpcMessage& msg, ed3k::Ed3kCredit& c, const std::function<void(const IpcMessage&)>& reply)
{
    const int seq = msg.seqId();
    QString err;
    switch (msg.type()) {
    case IpcMsgType::NewEd3kCreditKey:
        if (const auto addr = c.newKey(&err))
            reply(IpcMessage::makeResult(seq, true, QCborMap{{QStringLiteral("address"), *addr}}));
        else
            reply(failure(seq, err));
        break;
    case IpcMsgType::ImportEd3kCreditKey: {
        QString text = msg.fieldString(0).trimmed();
        if (text.startsWith(QLatin1StringView("0x"), Qt::CaseInsensitive))
            text = text.mid(2);
        QByteArray hex = text.toLatin1();
        QByteArray key = text.size() == 64 ? QByteArray::fromHex(hex) : QByteArray();
        ed3k::wipe(hex);
        const auto addr = c.importKey(key, &err);
        ed3k::wipe(key);
        reply(addr ? IpcMessage::makeResult(seq, true, QCborMap{{QStringLiteral("address"), *addr}}) : failure(seq, err));
        break;
    }
    case IpcMsgType::ExportEd3kCreditKey: {
        QByteArray key = c.exportKey();
        if (key.isEmpty()) {
            reply(failure(seq, QStringLiteral("no credit key yet")));
            break;
        }
        QByteArray hex = key.toHex();
        ed3k::wipe(key);
        reply(IpcMessage::makeResult(seq, true,
                                     QCborMap{{QStringLiteral("key"), QString::fromLatin1(hex)},
                                              {QStringLiteral("address"), c.status().address}}));
        ed3k::wipe(hex);
        break;
    }
    case IpcMsgType::BurnEd3kStake: {
        const qint64 micro = msg.fieldInt(0);
        const auto to = ed3k::Ed3kCredit::parseAddress(msg.fieldString(1));
        if (micro <= 0 || !to) {
            reply(failure(seq, QStringLiteral("an amount and an address (0x + 40 hex) are needed")));
            break;
        }
        reply(stakeResult(seq, c.burnForStake(std::uint64_t(micro), *to)));
        break;
    }
    case IpcMsgType::ClaimEd3kStake:
        reply(stakeResult(seq, c.claimStake(std::uint64_t(msg.fieldInt(0)))));
        break;
    case IpcMsgType::GetEd3kStakeClaims:
        reply(IpcMessage::makeResult(seq, true, stakeToCbor()));
        break;
    case IpcMsgType::RefreshEd3kStakeClaims:
        c.refreshClaims();
        reply(IpcMessage::makeResult(seq, true));
        break;
    case IpcMsgType::ReadEd3kStakeClaim: {
        const qint64 id = msg.fieldInt(0);
        reply(id > 0 ? stakeResult(seq, c.readClaim(std::uint64_t(id))) : failure(seq, QStringLiteral("a claim id is needed")));
        break;
    }
    default:
        reply(IpcMessage::makeError(seq, 400, QStringLiteral("Unknown ed3k request")));
        break;
    }
}
#endif

} // namespace

void setCredit(ed3k::Ed3kCredit* credit)
{
    s_credit = credit;
}

void setChain(ed3k::Ed3kChain* chain)
{
    s_chain = chain;
}

QCborMap chainStatusToCbor()
{
#ifdef EMULE_HAVE_ED3K
    if (!s_chain)
        return {};
    return QCborMap{{QStringLiteral("configured"), s_chain->isConfigured()},
                    {QStringLiteral("singleRemote"), s_chain->isSingleRemote()},
                    {QStringLiteral("refused"), s_chain->lastRefusal()}};
#else
    return {};
#endif
}

bool isCreditRequest(IpcMsgType type)
{
    const int t = static_cast<int>(type);
    return t >= 1020 && t <= 1029;
}

bool carriesKey(IpcMsgType type)
{
    return type == IpcMsgType::ImportEd3kCreditKey || type == IpcMsgType::ExportEd3kCreditKey
           || type == IpcMsgType::NewEd3kCreditKey;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isCreditRequest(msg.type()))
        return false;
#ifdef EMULE_HAVE_ED3K
    if (s_credit) {
        answer(msg, *s_credit, reply);
        return true;
    }
#endif
    reply(failure(msg.seqId(), QStringLiteral("ed3k is not built in")));
    return true;
}

QCborMap statusToCbor()
{
#ifdef EMULE_HAVE_ED3K
    if (!s_credit)
        return {};
    const auto st = s_credit->status();
    QCborArray wrong;
    for (const std::uint32_t e : st.wrongChain)
        wrong.append(qint64(e));
    // QCborArray claims;
    // for (const auto& cl : st.claims) claims.append(QCborMap{{id}, {to}, {amount}, {readyAt}});
    const QCborArray claims = claimsToCbor(st.claims);   // + state
    const auto i64 = [](std::uint64_t v) { return qint64(std::min<std::uint64_t>(v, INT64_MAX)); };
    return QCborMap{
        {QStringLiteral("enabled"), st.enabled},
        {QStringLiteral("configured"), st.configured},
        {QStringLiteral("endpoints"), st.endpoints},
        {QStringLiteral("address"), st.address},
        {QStringLiteral("problem"), st.problem},
        {QStringLiteral("balance"), st.balance ? i64(*st.balance) : qint64(-1)},
        {QStringLiteral("mature"), st.mature},
        {QStringLiteral("overdrawn"), st.overdrawn},
        {QStringLiteral("lastTxWhat"), qint64(st.lastTxWhat)},
        {QStringLiteral("lastTxState"), qint64(st.lastTxState)},
        {QStringLiteral("lastTxRevert"), qint64(st.lastTxRevert)},
        {QStringLiteral("lastTx"), st.lastTx},
        {QStringLiteral("receiptsSigned"), i64(st.receiptsSigned)},
        {QStringLiteral("kibSigned"), i64(st.kibSigned)},
        {QStringLiteral("receiptsTaken"), i64(st.receiptsTaken)},
        {QStringLiteral("kibTaken"), i64(st.kibTaken)},
        {QStringLiteral("slotHints"), i64(st.slotHints)},
        {QStringLiteral("lateBids"), i64(st.lateBids)},
        {QStringLiteral("wrongChain"), wrong},
        {QStringLiteral("stakeRate"), st.stakeRate ? hexOf(*st.stakeRate) : QString()},
        {QStringLiteral("stakeDelay"), i64(st.stakeDelay)},
        {QStringLiteral("claims"), claims},
    };
#else
    return {};
#endif
}

QCborMap stakeToCbor()
{
#ifdef EMULE_HAVE_ED3K
    if (!s_credit)
        return {};
    const auto st = s_credit->status();
    return QCborMap{{QStringLiteral("claims"), claimsToCbor(st.claims)},
                    {QStringLiteral("rate"), st.stakeRate ? hexOf(*st.stakeRate) : QString()},
                    {QStringLiteral("delay"), qint64(std::min<std::uint64_t>(st.stakeDelay, INT64_MAX))},
                    {QStringLiteral("address"), st.address},
                    {QStringLiteral("configured"), st.configured}};
#else
    return {};
#endif
}

void connectPushes(ed3k::Ed3kCredit& credit, std::function<void(const IpcMessage&)> broadcast)
{
#ifdef EMULE_HAVE_ED3K
    using C = ed3k::Ed3kCredit;
    QObject::connect(&credit, &C::stakeChanged, &credit, [broadcast] {
        IpcMessage m(IpcMsgType::PushEd3kStakeClaims, 0);
        m.append(stakeToCbor());
        broadcast(m);
    });
    QObject::connect(&credit, &C::stakeTx, &credit, [broadcast](int what, int state, int revert, const QString& tx) {
        IpcMessage m(IpcMsgType::PushEd3kStakeTx, 0);
        m.append(QCborMap{{QStringLiteral("what"), what},
                          {QStringLiteral("state"), state},
                          {QStringLiteral("revert"), revert},
                          {QStringLiteral("tx"), tx}});
        broadcast(m);
    });
    QObject::connect(&credit, &C::stakeLookup, &credit,
                     [broadcast](quint64 id, bool found, bool failed, const QString& to, const QString& amount, quint64 readyAt, bool kept,
                                 bool paid) {
                         IpcMessage m(IpcMsgType::PushEd3kStakeLookup, 0);
                         m.append(lookupToCbor(id, found, failed, to, amount, readyAt, kept, paid));
                         broadcast(m);
                     });
#else
    (void)credit;
    (void)broadcast;
#endif
}

} // namespace eMule::Ed3kCreditBridge
