/// @file tst_PortTestApi.cpp
/// @brief emule-qt.org port test API for BitTorrent and ed3k: URL, reply parsing, and the
///        ed3k merge of what the node saw. No network.

#include "app/AppConfig.h"
#include "net/PortTestApi.h"

#include <QCborMap>
#include <QTest>
#include <QUrlQuery>

using namespace eMule;
using namespace eMule::PortTestApi;

class tst_PortTestApi : public QObject {
    Q_OBJECT

private slots:
    void urlBitTorrent();
    void urlEd3k();
    void parseResults();
    void parseServerError();
    void parseWpError();
    void parseGarbage();
    void parseOldServer();
    void ed3kSeenMerge();
};

void tst_PortTestApi::urlBitTorrent()
{
    Request r;
    r.network = Network::BitTorrent;
    r.tcpPort = r.udpPort = 6881;
    r.ip4 = QStringLiteral("203.0.113.5");
    const QUrl u = url(r);
    QCOMPARE(u.toString(QUrl::RemoveQuery), QLatin1String(kWebsiteUrl) + QLatin1String(kPortTestApiPath));
    const QUrlQuery q(u);
    QCOMPARE(q.queryItemValue(QStringLiteral("proto")), QStringLiteral("bt"));
    QCOMPARE(q.queryItemValue(QStringLiteral("tcpport")), QStringLiteral("6881"));
    QCOMPARE(q.queryItemValue(QStringLiteral("udpport")), QStringLiteral("6881"));
    QCOMPARE(q.queryItemValue(QStringLiteral("ip4")), QStringLiteral("203.0.113.5"));
    QVERIFY(!q.hasQueryItem(QStringLiteral("ip6")));
    QVERIFY(!q.hasQueryItem(QStringLiteral("probe")));
}

void tst_PortTestApi::urlEd3k()
{
    Request r;
    r.network = Network::Ed3k;
    r.udpPort = 4321;
    r.probe = QByteArray::fromHex("0102030405060708090a0b0c0d0e0f1011121314ff");
    const QUrlQuery q(url(r));
    QCOMPARE(q.queryItemValue(QStringLiteral("proto")), QStringLiteral("ed3k"));
    QVERIFY(!q.hasQueryItem(QStringLiteral("tcpport")));
    QCOMPARE(q.queryItemValue(QStringLiteral("udpport")), QStringLiteral("4321"));
    QCOMPARE(q.queryItemValue(QStringLiteral("probe")), QStringLiteral("0102030405060708090a0b0c0d0e0f1011121314ff"));
}

void tst_PortTestApi::parseResults()
{
    const QByteArray body = R"({"error":false,"errorMsg":"","data":{"proto":"ed3k","ip":"2001:db8::1","results":[
        {"ip":"2001:db8::1","family":6,"source":"observed",
         "tcp":{"open":true,"message":"x","reason":"open"},"udp":{"open":false,"message":"y","reason":"filtered"}},
        {"ip":"203.0.113.5","family":4,"source":"client","tcp":null,"udp":{"open":false,"message":"z","reason":"sent"}}]}})";
    const Reply r = parseReply(body, 200, Network::Ed3k);
    QVERIFY(r.ok);
    QCOMPARE(r.targets.size(), 2);
    const QCborMap v6 = r.targets.at(0).toMap();
    QCOMPARE(v6.value(QStringLiteral("family")).toInteger(), 6);
    QCOMPARE(v6.value(QStringLiteral("tcp")).toMap().value(QStringLiteral("open")).toBool(), true);
    QCOMPARE(v6.value(QStringLiteral("udp")).toMap().value(QStringLiteral("reason")).toString(), QStringLiteral("filtered"));
    const QCborMap v4 = r.targets.at(1).toMap();
    QVERIFY(!v4.contains(QStringLiteral("tcp")));
    QCOMPARE(sentFamilies(r.targets), kSeenIPv4);
}

void tst_PortTestApi::parseServerError()
{
    const Reply r = parseReply(R"({"error":true,"errorMsg":"Rate limit exceeded.","data":[]})", 429, Network::Ed3k);
    QVERIFY(!r.ok);
    QCOMPARE(r.error, QStringLiteral("Rate limit exceeded."));
}

void tst_PortTestApi::parseWpError()
{
    const Reply r = parseReply(R"({"code":"rest_invalid_param","message":"Invalid parameter(s): proto","data":{"status":400}})", 400, Network::BitTorrent);
    QVERIFY(!r.ok);
    QCOMPARE(r.error, QStringLiteral("Invalid parameter(s): proto"));
}

void tst_PortTestApi::parseGarbage()
{
    const Reply r = parseReply("<html>502 Bad Gateway</html>", 502, Network::BitTorrent);
    QVERIFY(!r.ok);
    QCOMPARE(r.error, QStringLiteral("HTTP 502"));
}

void tst_PortTestApi::parseOldServer()
{
    // no data.proto: the server ran the eD2K test, whose verdicts would be wrong here
    const QByteArray body = R"({"error":false,"data":{"results":[{"ip":"203.0.113.5","family":4,
        "tcp":{"open":false,"reason":"closed"},"udp":null}]}})";
    const Reply r = parseReply(body, 200, Network::BitTorrent);
    QVERIFY(!r.ok);
    QVERIFY(r.proto.isEmpty());
    // and an answer for the other protocol is refused as well
    QVERIFY(!parseReply(QByteArray(body).replace(R"("data":{)", R"("data":{"proto":"ed3k",)"), 200, Network::BitTorrent).ok);
}

void tst_PortTestApi::ed3kSeenMerge()
{
    const auto target = [](int family, const QString& reason) {
        return QCborMap{
            {QStringLiteral("family"), family},
            {QStringLiteral("ip"), QStringLiteral("ip")},
            {QStringLiteral("udp"), QCborMap{{QStringLiteral("open"), false}, {QStringLiteral("reason"), reason}}},
        };
    };
    const QCborArray targets{target(6, QStringLiteral("sent")), target(4, QStringLiteral("sent")),
                             target(4, QStringLiteral("server_egress_blocked"))};
    QCOMPARE(sentFamilies(targets), kSeenIPv4 | kSeenIPv6);

    const QCborArray merged = applySeen(targets, kSeenIPv6);
    const auto udp = [&](int i) { return merged.at(i).toMap().value(QStringLiteral("udp")).toMap(); };
    QCOMPARE(udp(0).value(QStringLiteral("open")).toBool(), true);
    QCOMPARE(udp(0).value(QStringLiteral("reason")).toString(), QStringLiteral("open"));
    QCOMPARE(udp(1).value(QStringLiteral("open")).toBool(), false);
    QCOMPARE(udp(1).value(QStringLiteral("reason")).toString(), QStringLiteral("filtered"));
    // not sent: left as the server said
    QCOMPARE(udp(2).value(QStringLiteral("reason")).toString(), QStringLiteral("server_egress_blocked"));
}

QTEST_GUILESS_MAIN(tst_PortTestApi)
#include "tst_PortTestApi.moc"
