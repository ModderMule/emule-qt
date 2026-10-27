/// @file Ed3kBridge.cpp
/// @brief The daemon's ed3k requests — implementation.

#include "Ed3kBridge.h"

#include "BandwidthCoordinator.h"
#include "Ed3kCreditBridge.h"
#include "Ed3kFriendsBridge.h"
#include "Ed3kNamesBridge.h"

#ifdef EMULE_HAVE_ED3K
#include "Ed3kBootstrap.h"
#include "Ed3kSession.h"
#endif

namespace eMule::Ed3kBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

ed3k::Ed3kSession* s_session = nullptr;
ed3k::Ed3kBootstrap* s_bootstrap = nullptr;

/// OpenEd3kNodesLink, AnswerEd3kBootstrapAsk (1030-1031). False: not one of them.
bool handleBootstrap(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    const IpcMsgType t = msg.type();
    if (t != IpcMsgType::OpenEd3kNodesLink && t != IpcMsgType::AnswerEd3kBootstrapAsk)
        return false;
#ifdef EMULE_HAVE_ED3K
    if (!s_bootstrap) {
        reply(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ed3k is not running")));
        return true;
    }
    if (t == IpcMsgType::OpenEd3kNodesLink) {
        const auto ask = s_bootstrap->openLink(msg.fieldString(0));
        if (ask)
            reply(IpcMessage::makeResult(msg.seqId(), true, QCborMap{{QStringLiteral("ask"), qint64(*ask)}}));
        else
            reply(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Not a usable ed3k:nodes link")));
    } else {
        const bool ok = s_bootstrap->answer(std::uint64_t(msg.fieldInt(0)), msg.fieldBool(1));
        reply(ok ? IpcMessage::makeResult(msg.seqId(), true)
                 : IpcMessage::makeError(msg.seqId(), 404, QStringLiteral("No such ask")));
    }
#else
    reply(IpcMessage::makeError(msg.seqId(), 503, QStringLiteral("ed3k is not built in")));
#endif
    return true;
}

#ifdef EMULE_HAVE_ED3K
QCborMap statsToCbor(const ::ed3k::Stats& s)
{
    return QCborMap{
        {QStringLiteral("connections"), qint64(s.connections)},
        {QStringLiteral("lookups"), qint64(s.lookups)},
        {QStringLiteral("records"), qint64(s.records)},
        {QStringLiteral("recordBytes"), qint64(s.record_bytes)},
        {QStringLiteral("published"), qint64(s.published)},
        {QStringLiteral("queueLen"), qint64(s.queue_len)},
        {QStringLiteral("bannedNodes"), qint64(s.banned_nodes)},
        {QStringLiteral("bannedHosts"), qint64(s.banned_hosts)},
        {QStringLiteral("sentFile"), qint64(s.sent_file)},
        {QStringLiteral("sentOther"), qint64(s.sent_other)},
        {QStringLiteral("recvFile"), qint64(s.recv_file)},
        {QStringLiteral("recvOther"), qint64(s.recv_other)},
    };
}
#endif

} // namespace

void setSession(ed3k::Ed3kSession* session)
{
    s_session = session;
}

ed3k::Ed3kSession* session()
{
    return s_session;
}

void setBootstrap(ed3k::Ed3kBootstrap* bootstrap)
{
    s_bootstrap = bootstrap;
}

bool available()
{
#ifdef EMULE_HAVE_ED3K
    return true;
#else
    return false;
#endif
}

bool isEd3kRequest(IpcMsgType type)
{
    const int t = static_cast<int>(type);
    return t >= 1000 && t <= 1049;
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isEd3kRequest(msg.type()))
        return false;
    // friends and chat: 1001-1009
    if (Ed3kFriendsBridge::handleRequest(msg, reply))
        return true;
    // readable names and websites: 1010-1019
    if (Ed3kNamesBridge::handleRequest(msg, reply))
        return true;
    // credits: 1020-1029
    if (Ed3kCreditBridge::handleRequest(msg, reply))
        return true;
    // node list sources: 1030-1031
    if (handleBootstrap(msg, reply))
        return true;
    if (msg.type() == IpcMsgType::GetEd3kStatus)
        reply(IpcMessage::makeResult(msg.seqId(), true, statusToCbor()));
    else
        reply(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Unknown ed3k request")));
    return true;
}

QCborMap statusToCbor()
{
    QCborMap out{{QStringLiteral("available"), available()}, {QStringLiteral("running"), false}};
#ifdef EMULE_HAVE_ED3K
    if (!s_session)
        return out;
    const ed3k::Ed3kStatus st = s_session->status();
    const ed3k::Ed3kTraffic t = s_session->traffic();
    const ::ed3k::Limits limits = s_session->limits();
    out.insert(QStringLiteral("running"), st.running);
    out.insert(QStringLiteral("udpPort"), st.udpPort);
    out.insert(QStringLiteral("nodeId"), st.nodeId);
    out.insert(QStringLiteral("contacts"), qint64(st.contacts));
    out.insert(QStringLiteral("class4"), qint64(st.class4));
    out.insert(QStringLiteral("class6"), qint64(st.class6));
    out.insert(QStringLiteral("router"), st.router);
    out.insert(QStringLiteral("wireSent"), qint64(t.sent));
    out.insert(QStringLiteral("wireRecv"), qint64(t.recv));
    const auto* bw = BandwidthCoordinator::instance();
    out.insert(QStringLiteral("rateUp"), bw ? bw->snapshot().ed3kUpRate : 0);
    out.insert(QStringLiteral("rateDown"), bw ? bw->snapshot().ed3kDownRate : 0);
    out.insert(QStringLiteral("limitUpKb"), qint64(limits.up / 1024));
    out.insert(QStringLiteral("limitDownKb"), qint64(limits.down / 1024));
    if (st.running)
        out.insert(QStringLiteral("stats"), statsToCbor(st.stats));
    // the engine's upload queue policy (not `slots`: a Qt macro)
    if (const auto q = s_session->uploadQueueStats())
        out.insert(QStringLiteral("queue"), QCborMap{{QStringLiteral("slotCount"), qint64(q->slotCount)},
                                                     {QStringLiteral("holders"), qint64(q->holders)},
                                                     {QStringLiteral("waiting"), qint64(q->waiting)}});
    if (const QCborMap credit = Ed3kCreditBridge::statusToCbor(); !credit.isEmpty())
        out.insert(QStringLiteral("credit"), credit);
#endif
    return out;
}

} // namespace eMule::Ed3kBridge
