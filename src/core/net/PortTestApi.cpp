/// @file PortTestApi.cpp
/// @brief The emule-qt.org port test API — implementation.

#include "net/PortTestApi.h"

#include "app/AppConfig.h"
#include "net/BindAddress.h"

#include <QCborMap>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

#include <algorithm>

namespace eMule::PortTestApi {

namespace {

QCborMap entryToCbor(const QJsonValue& v)
{
    const QJsonObject o = v.toObject();
    QCborMap m{
        {QStringLiteral("open"), o.value(QStringLiteral("open")).toBool()},
        {QStringLiteral("reason"), o.value(QStringLiteral("reason")).toString()},
    };
    // ed3k: datagrams sent (plugin >= 1.7.0)
    if (const int copies = o.value(QStringLiteral("copies")).toInt(); copies > 0)
        m.insert(QStringLiteral("copies"), copies);
    return m;
}

int copiesOf(const QCborMap& udp)
{
    return int(std::max<qint64>(1, udp.value(QStringLiteral("copies")).toInteger()));
}

int familyBit(qint64 family)
{
    return family == 6 ? kSeenIPv6 : kSeenIPv4;
}

} // namespace

QUrl url(const Request& request)
{
    QUrl result(QLatin1String(kWebsiteUrl) + QLatin1String(kPortTestApiPath));
    QUrlQuery query;
    const bool ed3k = request.network == Network::Ed3k;
    query.addQueryItem(QStringLiteral("proto"), protoName(request.network));
    if (!ed3k)
        query.addQueryItem(QStringLiteral("tcpport"), QString::number(request.tcpPort));
    query.addQueryItem(QStringLiteral("udpport"), QString::number(request.udpPort));
    if (ed3k)
        query.addQueryItem(QStringLiteral("probe"), QString::fromLatin1(request.probe.toHex()));
    if (request.network == Network::Ed2k && request.userHash.size() == 16)
        query.addQueryItem(QStringLiteral("hash"), QString::fromLatin1(request.userHash.toHex()));
    if (!request.ip4.isEmpty())
        query.addQueryItem(QStringLiteral("ip4"), request.ip4);
    if (!request.ip6.isEmpty())
        query.addQueryItem(QStringLiteral("ip6"), request.ip6);
    // plugin >= 1.8.0: the observed address of another family is not probed
    if (request.families == kSeenIPv4)
        query.addQueryItem(QStringLiteral("families"), QStringLiteral("4"));
    else if (request.families == kSeenIPv6)
        query.addQueryItem(QStringLiteral("families"), QStringLiteral("6"));
    result.setQuery(query);
    return result;
}

QUrl sourceUrl()
{
    return QUrl(QLatin1String(kWebsiteUrl) + QLatin1String(kPortTestApiPath) + QLatin1String("/source"));
}

QStringList parseSources(const QByteArray& body)
{
    const QJsonObject data = QJsonDocument::fromJson(body).object().value(QStringLiteral("data")).toObject();
    QStringList out;
    for (const auto* key : {"ip4", "ip6"})
        for (const QJsonValue& v : data.value(QLatin1String(key)).toArray())
            if (const QHostAddress a(v.toString()); !a.isNull())
                out << a.toString();
    return out;
}

QString protoName(Network network)
{
    switch (network) {
    case Network::BitTorrent: return QStringLiteral("bt");
    case Network::Ed3k:       return QStringLiteral("ed3k");
    case Network::Ed2k:       return QStringLiteral("ed2k");
    }
    return {};
}

Reply parseReply(const QByteArray& body, int httpStatus, Network network)
{
    Reply reply;
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    if (root.isEmpty()) {
        reply.error = httpStatus > 0 ? QStringLiteral("HTTP %1").arg(httpStatus) : QStringLiteral("No reply");
        return reply;
    }
    // WP REST error ({code, message}) or our own ({error: true, errorMsg})
    if (root.value(QStringLiteral("error")).toBool() || root.contains(QStringLiteral("code"))) {
        QString msg = root.value(QStringLiteral("errorMsg")).toString();
        if (msg.isEmpty())
            msg = root.value(QStringLiteral("message")).toString();
        reply.error = msg.isEmpty() ? QStringLiteral("HTTP %1").arg(httpStatus) : msg;
        if (root.value(QStringLiteral("data")).toObject().value(QStringLiteral("reason")).toString() == kReasonNoTarget)
            reply.failure = Failure::NoBoundAddress;
        return reply;
    }
    const QJsonObject data = root.value(QStringLiteral("data")).toObject();
    reply.proto = data.value(QStringLiteral("proto")).toString();
    if (reply.proto != protoName(network)) {
        reply.failure = Failure::OldServer;
        reply.error = QStringLiteral("no proto %1 in the reply").arg(protoName(network));
        return reply;
    }
    reply.obfuscated = data.value(QStringLiteral("obfuscated")).toBool();
    const QJsonArray results = data.value(QStringLiteral("results")).toArray();
    for (const QJsonValue& r : results) {
        const QJsonObject o = r.toObject();
        QCborMap target{
            {QStringLiteral("family"), qint64(o.value(QStringLiteral("family")).toInt())},
            {QStringLiteral("ip"), o.value(QStringLiteral("ip")).toString()},
        };
        if (o.value(QStringLiteral("tcp")).isObject())
            target.insert(QStringLiteral("tcp"), entryToCbor(o.value(QStringLiteral("tcp"))));
        if (o.value(QStringLiteral("udp")).isObject())
            target.insert(QStringLiteral("udp"), entryToCbor(o.value(QStringLiteral("udp"))));
        reply.targets.append(target);
    }
    if (reply.targets.isEmpty()) {
        reply.error = QStringLiteral("Empty reply");
        return reply;
    }
    reply.ok = true;
    return reply;
}

int boundFamilies(const BindAddress::Resolution& r)
{
    if (r.state != BindAddress::State::Bound)
        return kBothFamilies;
    if (!r.literal.isNull())
        return r.literal.protocol() == QAbstractSocket::IPv6Protocol ? kSeenIPv6 : kSeenIPv4;
    int bits = 0;
    if (r.pin.v4 && !r.v4.isNull())
        bits |= kSeenIPv4;
    if (r.pin.v6 && r.hasGlobalIPv6)
        bits |= kSeenIPv6;
    return bits;
}

QCborArray onlyFamilies(const QCborArray& targets, int families)
{
    QCborArray out;
    for (const QCborValue& t : targets)
        if (families & familyBit(t.toMap().value(QStringLiteral("family")).toInteger()))
            out.append(t);
    return out;
}

int sentFamilies(const QCborArray& targets)
{
    int bits = 0;
    for (const QCborValue& t : targets) {
        const QCborMap m = t.toMap();
        if (m.value(QStringLiteral("udp")).toMap().value(QStringLiteral("reason")).toString() == kReasonSent)
            bits |= familyBit(m.value(QStringLiteral("family")).toInteger());
    }
    return bits;
}

QString hostOf(const QString& address)
{
    if (address.startsWith(QLatin1Char('['))) {
        const qsizetype end = address.indexOf(QLatin1Char(']'));
        return end > 1 ? address.mid(1, end - 1) : QString();
    }
    // one colon: IPv4 with port; none or several: a bare address
    return address.count(QLatin1Char(':')) == 1 ? address.section(QLatin1Char(':'), 0, 0) : address;
}

QCborArray applyPeerView(const QCborArray& targets, const PeerView& v4, const PeerView& v6)
{
    QCborArray out;
    for (const QCborValue& t : targets) {
        QCborMap m = t.toMap();
        const PeerView& view = m.value(QStringLiteral("family")).toInteger() == 6 ? v6 : v4;
        const QHostAddress peer(hostOf(view.address));
        if (!peer.isNull()) {
            const QHostAddress tested(m.value(QStringLiteral("ip")).toString());
            m.insert(QStringLiteral("peerAddress"), view.address);
            m.insert(QStringLiteral("peerSame"), peer.isEqual(tested, QHostAddress::TolerantConversion));
            if (!view.tcpProof.isEmpty())
                m.insert(QStringLiteral("peerTcp"), view.tcpProof);
            if (!view.udpProof.isEmpty())
                m.insert(QStringLiteral("peerUdp"), view.udpProof);
        }
        out.append(m);
    }
    return out;
}

int sentCopies(const QCborArray& targets, int bit)
{
    int n = 0;
    for (const QCborValue& t : targets) {
        const QCborMap m = t.toMap();
        const QCborMap udp = m.value(QStringLiteral("udp")).toMap();
        if (udp.value(QStringLiteral("reason")).toString() == kReasonSent
            && familyBit(m.value(QStringLiteral("family")).toInteger()) == bit)
            n += copiesOf(udp);
    }
    return n;
}

QCborArray applySeen(const QCborArray& targets, int seenFamilies, int packets4, int packets6)
{
    QCborArray out;
    for (const QCborValue& t : targets) {
        QCborMap m = t.toMap();
        const QCborMap udp = m.value(QStringLiteral("udp")).toMap();
        if (udp.value(QStringLiteral("reason")).toString() == kReasonSent) {
            const int bit = familyBit(m.value(QStringLiteral("family")).toInteger());
            const bool seen = (seenFamilies & bit) != 0;
            const int packets = bit == kSeenIPv6 ? packets6 : packets4;
            m.insert(QStringLiteral("udp"), QCborMap{
                {QStringLiteral("open"), seen},
                {QStringLiteral("reason"), seen ? QString(kReasonOpen) : QString(kReasonFiltered)},
                {QStringLiteral("packets"), packets > 0 ? packets : copiesOf(udp)},
            });
        }
        out.append(m);
    }
    return out;
}

Verdict tcpVerdict(const QCborArray& targets, bool obfuscated, bool cryptRequired, int family)
{
    bool closed = false;
    for (const QCborValue& t : targets) {
        const QCborMap m = t.toMap();
        if (family != 0 && m.value(QStringLiteral("family")).toInteger() != family)
            continue;
        const QCborMap tcp = m.value(QStringLiteral("tcp")).toMap();
        if (tcp.value(QStringLiteral("open")).toBool())
            return Verdict::Open;
        const QString reason = tcp.value(QStringLiteral("reason")).toString();
        // a plain dial to a core that requires encryption is refused although the port is open
        const bool plainRefused = !obfuscated && cryptRequired
                                  && (reason == QLatin1String("bad_reply") || reason == QLatin1String("rejected"));
        if (reason == QLatin1String("closed") || reason == QLatin1String("filtered")
            || reason == QLatin1String("rejected") || reason == QLatin1String("bad_reply"))
            closed = closed || !plainRefused;
    }
    return closed ? Verdict::Closed : Verdict::Unknown;
}

} // namespace eMule::PortTestApi
