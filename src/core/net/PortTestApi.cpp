/// @file PortTestApi.cpp
/// @brief The emule-qt.org port test API — implementation.

#include "net/PortTestApi.h"

#include "app/AppConfig.h"

#include <QCborMap>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

namespace eMule::PortTestApi {

namespace {

QCborMap entryToCbor(const QJsonValue& v)
{
    const QJsonObject o = v.toObject();
    return QCborMap{
        {QStringLiteral("open"), o.value(QStringLiteral("open")).toBool()},
        {QStringLiteral("reason"), o.value(QStringLiteral("reason")).toString()},
    };
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
    const bool bt = request.network == Network::BitTorrent;
    query.addQueryItem(QStringLiteral("proto"), protoName(request.network));
    if (bt)
        query.addQueryItem(QStringLiteral("tcpport"), QString::number(request.tcpPort));
    query.addQueryItem(QStringLiteral("udpport"), QString::number(request.udpPort));
    if (!bt)
        query.addQueryItem(QStringLiteral("probe"), QString::fromLatin1(request.probe.toHex()));
    if (!request.ip4.isEmpty())
        query.addQueryItem(QStringLiteral("ip4"), request.ip4);
    if (!request.ip6.isEmpty())
        query.addQueryItem(QStringLiteral("ip6"), request.ip6);
    result.setQuery(query);
    return result;
}

QString protoName(Network network)
{
    return network == Network::BitTorrent ? QStringLiteral("bt") : QStringLiteral("ed3k");
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
        return reply;
    }
    const QJsonObject data = root.value(QStringLiteral("data")).toObject();
    reply.proto = data.value(QStringLiteral("proto")).toString();
    if (reply.proto != protoName(network)) {
        reply.failure = Failure::OldServer;
        reply.error = QStringLiteral("no proto %1 in the reply").arg(protoName(network));
        return reply;
    }
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

QCborArray applySeen(const QCborArray& targets, int seenFamilies)
{
    QCborArray out;
    for (const QCborValue& t : targets) {
        QCborMap m = t.toMap();
        const QCborMap udp = m.value(QStringLiteral("udp")).toMap();
        if (udp.value(QStringLiteral("reason")).toString() == kReasonSent) {
            const bool seen = (seenFamilies & familyBit(m.value(QStringLiteral("family")).toInteger())) != 0;
            m.insert(QStringLiteral("udp"), QCborMap{
                {QStringLiteral("open"), seen},
                {QStringLiteral("reason"), seen ? QString(kReasonOpen) : QString(kReasonFiltered)},
            });
        }
        out.append(m);
    }
    return out;
}

} // namespace eMule::PortTestApi
