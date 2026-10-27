/// @file Ed3kCreditBridge.cpp
/// @brief ed3k credits over IPC — implementation.

#include "Ed3kCreditBridge.h"

#include <QCborArray>
#include <QCborMap>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kCredit.h"
#include "Ed3kStore.h"
#endif

namespace eMule::Ed3kCreditBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

ed3k::Ed3kCredit* s_credit = nullptr;

IpcMessage failure(int seqId, const QString& error)
{
    return IpcMessage::makeResult(seqId, false, QCborMap{{QStringLiteral("error"), error}});
}

#ifdef EMULE_HAVE_ED3K
QString hexOf(std::span<const std::uint8_t> bytes)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(bytes.data()), qsizetype(bytes.size())).toHex());
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
    QCborArray claims;
    for (const auto& cl : st.claims)
        claims.append(QCborMap{{QStringLiteral("id"), qint64(cl.id)},
                               {QStringLiteral("to"), ed3k::Ed3kCredit::addressText(cl.to)},
                               {QStringLiteral("amount"), hexOf(cl.amount)},
                               {QStringLiteral("readyAt"), qint64(cl.readyAt)}});
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

} // namespace eMule::Ed3kCreditBridge
