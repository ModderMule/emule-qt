/// @file Ed3kBridge.cpp
/// @brief The daemon's ed3k requests — implementation.

#include "Ed3kBridge.h"

#include "BandwidthCoordinator.h"

#ifdef EMULE_HAVE_ED3K
#include "Ed3kSession.h"
#endif

namespace eMule::Ed3kBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

ed3k::Ed3kSession* s_session = nullptr;

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
#endif
    return out;
}

} // namespace eMule::Ed3kBridge
