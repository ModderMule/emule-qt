/// @file Ed3kBridge.cpp
/// @brief The daemon's ed3k requests — implementation.

#include "Ed3kBridge.h"

#include "BandwidthCoordinator.h"
#include "Ed3kCreditBridge.h"
#include "Ed3kFiltersBridge.h"
#include "Ed3kDownloadsBridge.h"
#include "Ed3kHosterBridge.h"
#include "Ed3kKadBridge.h"
#include "Ed3kLinksBridge.h"
#include "Ed3kFriendsBridge.h"
#include "Ed3kNamesBridge.h"
#include "Ed3kOwnSetsBridge.h"
#include "Ed3kRoomsBridge.h"
#include "Ed3kSetsBridge.h"
#include "Ed3kPaidBridge.h"
#include "Ed3kPaidSellerBridge.h"
#include "app/AppContext.h"
#include "stats/NetworkCounters.h"
#include "stats/Statistics.h"

#include <algorithm>
#include <array>

#ifdef EMULE_HAVE_ED3K
#include "Ed3kBootstrap.h"
#include "Ed3kEd2kJoin.h"
#include "Ed3kSession.h"
#include "Ed3kShared.h"
#endif

namespace eMule::Ed3kBridge {

using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

ed3k::Ed3kSession* s_session = nullptr;
bool s_connectWanted = false;
ed3k::Ed3kBootstrap* s_bootstrap = nullptr;
ed3k::Ed3kEd2kJoin* s_ed2kJoin = nullptr;
ed3k::Ed3kShared* s_shared = nullptr;

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
    Ed3kLinksBridge::setSession(session);   // its cache servers (batch 15)
    Ed3kKadBridge::setSession(session);
}

ed3k::Ed3kSession* session()
{
    return s_session;
}

void setBootstrap(ed3k::Ed3kBootstrap* bootstrap)
{
    s_bootstrap = bootstrap;
    Ed3kKadBridge::setBootstrap(bootstrap);
}

void setEd2kJoin(ed3k::Ed3kEd2kJoin* join)
{
    s_ed2kJoin = join;
}

void setShared(ed3k::Ed3kShared* shared)
{
    s_shared = shared;
}

void appendDownloadInfo([[maybe_unused]] QCborMap& details, [[maybe_unused]] const QByteArray& md4)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_ed2kJoin)
        return;
    details.insert(QLatin1StringView("ed3kEnabled"), s_ed2kJoin->isEnabled());
    const auto info = s_ed2kJoin->isEnabled() ? s_ed2kJoin->infoOf(md4) : std::nullopt;
    if (!info)
        return;
    details.insert(QLatin1StringView("ed3kTracked"), true);
    details.insert(QLatin1StringView("ed3kSearching"), info->searching);
    if (info->root)
        details.insert(QLatin1StringView("ed3kRoot"), QString::fromLatin1(
            QByteArray(reinterpret_cast<const char*>(info->root->bytes.data()), qsizetype(info->root->bytes.size())).toHex()));
    details.insert(QLatin1StringView("ed3kPair"), static_cast<int>(info->pair));
    details.insert(QLatin1StringView("ed3kBytes"), static_cast<qint64>(info->bytes));
    details.insert(QLatin1StringView("ed3kSources"), info->sources);
    details.insert(QLatin1StringView("ed3kTransferring"), info->transferring);
#endif
}

void appendSharedInfo([[maybe_unused]] QCborMap& details, [[maybe_unused]] const QByteArray& md4)
{
#ifdef EMULE_HAVE_ED3K
    if (!s_shared)
        return;
    details.insert(QLatin1StringView("ed3kEnabled"), s_shared->isEnabled());
    if (!s_shared->isEnabled())
        return;
    if (const auto root = s_shared->rootOf(md4))
        details.insert(QLatin1StringView("ed3kRoot"), QString::fromLatin1(
            QByteArray(reinterpret_cast<const char*>(root->bytes.data()), qsizetype(root->bytes.size())).toHex()));
    details.insert(QLatin1StringView("ed3kAliasPublished"), s_shared->aliasPublished(md4));
    details.insert(QLatin1StringView("ed3kKeywordsPublished"), s_shared->keywordsPublished(md4));
#endif
}

bool connectWanted()
{
    return s_connectWanted;
}

void setConnectWanted(bool on)
{
    s_connectWanted = on;
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
    // 1100-1149: second request block (batch 12), the first was full; 1200-1209: Kad window;
    // 1210-1219: the Link Grabber (Ed3kHosterBridge)
    return (t >= 1000 && t <= 1049) || (t >= 1100 && t <= 1149) || (t >= 1200 && t <= 1219);
}

bool handleRequest(const IpcMessage& msg, const std::function<void(const IpcMessage&)>& reply)
{
    if (!isEd3kRequest(msg.type()))
        return false;
    // other links (batch 15): 1032, 1049, 1137 (first: 1137 is inside the filter lists' block)
    if (Ed3kLinksBridge::handleRequest(msg, reply))
        return true;
    // hoster links: 1100-1109, and 1139 (AddClickNLoad) inside the filter lists' block, so first;
    // the Link Grabber: 1210-1218
    if (Ed3kHosterBridge::handleRequest(msg, reply))
        return true;
    // Kad window: 1200-1205 (1205 in IpcClientHandler, it re-applies the config)
    if (Ed3kKadBridge::handleRequest(msg, reply))
        return true;
    // friends and chat: 1001-1009
    if (Ed3kFriendsBridge::handleRequest(msg, reply))
        return true;
    // readable names and websites: 1010-1019
    if (Ed3kNamesBridge::handleRequest(msg, reply))
        return true;
    // paid subscriptions: 1035-1038 (batch 13), 1028-1029 (batch 14: inside the credits' block, so first)
    if (Ed3kPaidBridge::handleRequest(msg, reply))
        return true;
    // selling: 1033, 1034, 1040, 1041 (batch 14)
    if (Ed3kPaidSellerBridge::handleRequest(msg, reply))
        return true;
    // credits: 1020-1029
    if (Ed3kCreditBridge::handleRequest(msg, reply))
        return true;
    // filter lists: 1130-1139 (batch 12)
    if (Ed3kFiltersBridge::handleRequest(msg, reply))
        return true;
    // node list sources: 1030-1031
    if (handleBootstrap(msg, reply))
        return true;
    // hiding: 1047-1048 (batch 15) are Ed3kHideBridge's, called before this (IpcClientHandler)
    // paid subscriptions: 1035-1038 (batch 13); was here, now before the credits (1028-1029)
    // if (Ed3kPaidBridge::handleRequest(msg, reply))
    //     return true;
    // rooms: 1120-1129
    if (Ed3kRoomsBridge::handleRequest(msg, reply))
        return true;
    // My ed3k Sets: 1145-1148 (batch 15; before the downloads' 1140-1149)
    if (Ed3kOwnSetsBridge::handleRequest(msg, reply))
        return true;
    // sets and live: 1110-1119, 1144 (before the downloads' 1140-1149)
    if (Ed3kSetsBridge::handleRequest(msg, reply))
        return true;
    // plain downloads: 1140-1149
    if (Ed3kDownloadsBridge::handleRequest(msg, reply))
        return true;
    if (msg.type() == IpcMsgType::GetEd3kStatus)
        reply(IpcMessage::makeResult(msg.seqId(), true, statusToCbor()));
    else
        reply(IpcMessage::makeError(msg.seqId(), 400, QStringLiteral("Unknown ed3k request")));
    return true;
}

quint16 udpPort()
{
#ifdef EMULE_HAVE_ED3K
    return s_session ? s_session->udpPort() : 0;
#else
    return 0;
#endif
}

QByteArray portTestProbe(quint64 nonce)
{
#ifdef EMULE_HAVE_ED3K
    return s_session ? s_session->portTestProbe(nonce) : QByteArray();
#else
    Q_UNUSED(nonce);
    return {};
#endif
}

int portTestSeen(quint64 nonce)
{
#ifdef EMULE_HAVE_ED3K
    return s_session ? s_session->portTestSeen(nonce) : 0;
#else
    Q_UNUSED(nonce);
    return 0;
#endif
}

void sampleCounters()
{
#ifdef EMULE_HAVE_ED3K
    // last raw totals: the engine's own reset with each restart, a drop counts from 0
    static std::array<std::uint64_t, 6> last{};
    Statistics* stats = theApp.statistics;
    if (!s_session || !stats)
        return;
    const ed3k::Ed3kStatus st = s_session->status();
    const ed3k::Ed3kTraffic t = s_session->traffic();
    const std::array<std::uint64_t, 6> now{t.recv, t.sent, st.stats.recv_file, st.stats.recv_other,
                                           st.stats.sent_file, st.stats.sent_other};
    Ed3kCounters& c = stats->ed3kSession();
    uint64 Ed3kCounters::* const fields[] = {&Ed3kCounters::wireRecv, &Ed3kCounters::wireSent,
                                             &Ed3kCounters::recvFile, &Ed3kCounters::recvOther,
                                             &Ed3kCounters::sentFile, &Ed3kCounters::sentOther};
    for (size_t i = 0; i < now.size(); ++i) {
        c.*fields[i] += now[i] >= last[i] ? now[i] - last[i] : now[i];
        last[i] = now[i];
    }
    if (const auto* bw = BandwidthCoordinator::instance()) {
        const auto snap = bw->snapshot();
        if (snap.ed3kDownRate > 0)
            c.downloadTimeMs += 1000;
        if (snap.ed3kUpRate > 0)
            c.uploadTimeMs += 1000;
        raiseCounter(c.maxDownRate, uint64(std::max<qint64>(snap.ed3kDownRate, 0)));
        raiseCounter(c.maxUpRate, uint64(std::max<qint64>(snap.ed3kUpRate, 0)));
    }
    raiseCounter(c.peakConnections, st.stats.connections);
    raiseCounter(c.peakContacts, st.contacts);
    raiseCounter(c.peakPublished, st.stats.published);
#endif
}

QCborMap hideToCbor()
{
#ifdef EMULE_HAVE_ED3K
    if (!s_session)
        return {};
    const ed3k::Ed3kHideStatus h = s_session->hide().status();
    return QCborMap{{QStringLiteral("on"), h.on},
                    {QStringLiteral("running"), h.running},
                    {QStringLiteral("nodeId"), h.nodeId},
                    {QStringLiteral("hops"), h.hops},
                    {QStringLiteral("paths"), h.paths},
                    {QStringLiteral("built"), h.built},
                    {QStringLiteral("waitingForHops"), h.waitingForHops},
                    {QStringLiteral("anonFiles"), h.anonFiles},
                    {QStringLiteral("hop"), h.hop},
                    {QStringLiteral("router"), h.router},
                    {QStringLiteral("shares"), h.shares}};
#else
    return {};
#endif
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
    out.insert(QStringLiteral("natChecking"), qint64(st.natChecking));
    out.insert(QStringLiteral("kadConnected"), st.kadConnected);
    out.insert(QStringLiteral("endpoint4"), st.endpoint4);
    out.insert(QStringLiteral("endpoint6"), st.endpoint6);
    out.insert(QStringLiteral("wireSent"), qint64(t.sent));
    out.insert(QStringLiteral("wireRecv"), qint64(t.recv));
    const auto* bw = BandwidthCoordinator::instance();
    out.insert(QStringLiteral("rateUp"), bw ? bw->snapshot().ed3kUpRate : 0);
    out.insert(QStringLiteral("rateDown"), bw ? bw->snapshot().ed3kDownRate : 0);
    out.insert(QStringLiteral("limitUpKb"), qint64(limits.up / 1024));
    out.insert(QStringLiteral("limitDownKb"), qint64(limits.down / 1024));
    if (st.running)
        out.insert(QStringLiteral("stats"), statsToCbor(st.stats));
    // batch 15: Kad's own network down (bit 0 IPv4, bit 1 IPv6) and hiding (hops.md §1)
    out.insert(QStringLiteral("kadOffline"), qint64(st.kadOffline));
    if (const QCborMap hide = hideToCbor(); !hide.isEmpty())
        out.insert(QStringLiteral("hide"), hide);
    if (const Statistics* stats = theApp.statistics) {
        out.insert(QStringLiteral("session"), countersToCbor(stats->ed3kSession()));
        out.insert(QStringLiteral("cumulative"), countersToCbor(stats->cumulativeEd3k()));
    }
    // the engine's upload queue policy (not `slots`: a Qt macro)
    if (const auto q = s_session->uploadQueueStats())
        out.insert(QStringLiteral("queue"), QCborMap{{QStringLiteral("slotCount"), qint64(q->slotCount)},
                                                     {QStringLiteral("holders"), qint64(q->holders)},
                                                     {QStringLiteral("waiting"), qint64(q->waiting)}});
    if (const QCborMap credit = Ed3kCreditBridge::statusToCbor(); !credit.isEmpty())
        out.insert(QStringLiteral("credit"), credit);
    if (const QCborMap chain = Ed3kCreditBridge::chainStatusToCbor(); !chain.isEmpty())
        out.insert(QStringLiteral("chain"), chain);   // batch 13
    if (const QCborMap hoster = Ed3kHosterBridge::statusToCbor(); !hoster.isEmpty())
        out.insert(QStringLiteral("hoster"), hoster);
#endif
    return out;
}

} // namespace eMule::Ed3kBridge
