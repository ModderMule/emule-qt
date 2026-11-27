/// @file tst_PortTestReport.cpp
/// @brief The port test message (PortTest::NetworkTest::reportText): peer proof overrides a
///        lost test packet, the proof names its source, lost packets are said plainly (#216).

#include "dialogs/PortTest.h"

#include <QTest>

using namespace eMule;
using eMule::PortTest::NetworkTest;

namespace {

QCborMap udpTarget(int family, const QString& ip, const QString& reason, int packets)
{
    return QCborMap{
        {QStringLiteral("family"), family},
        {QStringLiteral("ip"), ip},
        {QStringLiteral("udp"), QCborMap{{QStringLiteral("open"), reason == QLatin1String("open")},
                                         {QStringLiteral("reason"), reason},
                                         {QStringLiteral("packets"), packets}}},
    };
}

QCborMap withPeer(QCborMap t, const QString& address, bool same, const QString& key, QLatin1StringView source)
{
    t.insert(QStringLiteral("peerAddress"), address);
    t.insert(QStringLiteral("peerSame"), same);
    t.insert(key, QString(source));
    return t;
}

QCborMap ed3kResult(const QCborArray& targets)
{
    return QCborMap{{QStringLiteral("network"), 2}, {QStringLiteral("udpPort"), 11719},
                    {QStringLiteral("targets"), targets}};
}

} // namespace

class tst_PortTestReport : public QObject {
    Q_OBJECT

private slots:
    void ed3kLostButProven();
    void ed3kNotArrived();
    void ed3kOtherRouteNotProven();
    void ed2kKadAndServerProof();
    void boundInterfaceNote();
    void ipFilterNotes();
};

void tst_PortTestReport::ed3kLostButProven()
{
    const QCborArray targets{
        withPeer(udpTarget(4, QStringLiteral("198.51.100.7"), QStringLiteral("filtered"), 6),
                 QStringLiteral("198.51.100.7:11719"), true, QStringLiteral("peerUdp"), PortTestApi::kProofEd3k),
        udpTarget(6, QStringLiteral("2001:db8::5"), QStringLiteral("open"), 3),
    };
    bool all = false;
    const QString text = NetworkTest::reportText(ed3kResult(targets), &all);
    QVERIFY(all);
    QVERIFY(text.contains(QStringLiteral("UDP is reachable.")));
    QVERIFY(text.contains(QStringLiteral("did not arrive, but peers prove the port open")));
    QVERIFY(text.contains(QStringLiteral("other ed3k nodes reach this node at 198.51.100.7:11719")));
    QVERIFY(text.contains(QStringLiteral("None of the 6 test packet(s)")));
    QVERIFY(!text.contains(QStringLiteral("eD2K clients")));
}

void tst_PortTestReport::ed3kNotArrived()
{
    bool all = true;
    const QString text = NetworkTest::reportText(
        ed3kResult({udpTarget(4, QStringLiteral("198.51.100.7"), QStringLiteral("filtered"), 3)}), &all);
    QVERIFY(!all);
    QVERIFY(text.contains(QStringLiteral("UDP is not reachable")));
    QVERIFY(text.contains(QStringLiteral("The website's test packets did not arrive.")));
    QVERIFY(text.contains(QStringLiteral("none of the 3 test packet(s) arrived")));
}

void tst_PortTestReport::ed3kOtherRouteNotProven()
{
    // a VPN exit was tested, peers reach another address: no proof for the tested one
    const QCborArray targets{
        withPeer(udpTarget(4, QStringLiteral("203.0.113.9"), QStringLiteral("filtered"), 3),
                 QStringLiteral("198.51.100.7:11719"), false, QStringLiteral("peerUdp"), PortTestApi::kProofEd3k),
    };
    bool all = true;
    const QString text = NetworkTest::reportText(ed3kResult(targets), &all);
    QVERIFY(!all);
    QVERIFY(text.contains(QStringLiteral("not at the tested address")));
    QVERIFY(!text.contains(QStringLiteral("peers prove the port open")));
}

void tst_PortTestReport::ed2kKadAndServerProof()
{
    QCborMap t{
        {QStringLiteral("family"), 4},
        {QStringLiteral("ip"), QStringLiteral("198.51.100.7")},
        {QStringLiteral("tcp"), QCborMap{{QStringLiteral("open"), false}, {QStringLiteral("reason"), QStringLiteral("filtered")}}},
        {QStringLiteral("udp"), QCborMap{{QStringLiteral("open"), false}, {QStringLiteral("reason"), QStringLiteral("filtered")}}},
        {QStringLiteral("peerAddress"), QStringLiteral("198.51.100.7")},
        {QStringLiteral("peerSame"), true},
        {QStringLiteral("peerTcp"), QString(PortTestApi::kProofServer)},
        {QStringLiteral("peerUdp"), QString(PortTestApi::kProofKad)},
    };
    const QCborMap result{{QStringLiteral("network"), 3}, {QStringLiteral("tcpPort"), 4662},
                          {QStringLiteral("udpPort"), 4672}, {QStringLiteral("targets"), QCborArray{t}}};
    bool all = false;
    const QString text = NetworkTest::reportText(result, &all);
    QVERIFY(all);
    QVERIFY(text.contains(QStringLiteral("the eD2K server gave a High ID at 198.51.100.7")));
    QVERIFY(text.contains(QStringLiteral("eD2K clients reach this UDP port at 198.51.100.7")));
    QVERIFY(!text.contains(QStringLiteral("ed3k nodes")));

    // Kad proves nothing about TCP
    t.remove(QStringLiteral("peerTcp"));
    const QCborMap noServer{{QStringLiteral("network"), 3}, {QStringLiteral("tcpPort"), 4662},
                            {QStringLiteral("udpPort"), 4672}, {QStringLiteral("targets"), QCborArray{t}}};
    QVERIFY(!(NetworkTest::reportText(noServer, &all), all));
}

void tst_PortTestReport::boundInterfaceNote()
{
    // #246: interface without IPv6, only IPv4 tested and said why
    QCborMap result = ed3kResult(QCborArray{udpTarget(4, QStringLiteral("203.0.113.9"), QStringLiteral("open"), 3)});
    QVERIFY(!NetworkTest::reportText(result).contains(QStringLiteral("Only IPv4")));
    result.insert(QStringLiteral("families"), PortTestApi::kSeenIPv4);
    result.insert(QStringLiteral("bindInterface"), QStringLiteral("utun5"));
    bool all = false;
    const QString text = NetworkTest::reportText(result, &all);
    QVERIFY(all);
    QVERIFY(text.contains(QStringLiteral("Only IPv4 was tested: the network interface utun5")));
}

void tst_PortTestReport::ipFilterNotes()
{
    // #266: the core dropped the test link, its IP filter rejected the test server
    const QCborMap t{
        {QStringLiteral("family"), 4},
        {QStringLiteral("ip"), QStringLiteral("198.51.100.7")},
        {QStringLiteral("tcp"), QCborMap{{QStringLiteral("open"), false}, {QStringLiteral("reason"), QString(PortTestApi::kReasonDropped)}}},
    };
    QCborMap result{{QStringLiteral("network"), 3}, {QStringLiteral("tcpPort"), 24662},
                    {QStringLiteral("udpPort"), 0}, {QStringLiteral("targets"), QCborArray{t}}};
    const QCborArray hits{QCborMap{{QStringLiteral("address"), QStringLiteral("192.0.2.80")},
                                   {QStringLiteral("description"), QStringLiteral("Cloud | hosting")},
                                   {QStringLiteral("tcp"), true}}};
    result.insert(QStringLiteral("filterHits"), hits);
    bool all = true;
    QString text = NetworkTest::reportText(result, &all);
    QVERIFY(!all);
    QVERIFY(text.contains(QStringLiteral("closed the connection at once")));
    QVERIFY(text.contains(QStringLiteral("Your IP filter rejected 192.0.2.80 (Cloud | hosting) during the test")));

    // let through: passed, and said so
    QCborMap open = t;
    open.insert(QStringLiteral("tcp"), QCborMap{{QStringLiteral("open"), true}, {QStringLiteral("reason"), QStringLiteral("open")}});
    result.insert(QStringLiteral("targets"), QCborArray{open});
    result.insert(QStringLiteral("serverFiltered"), hits);
    text = NetworkTest::reportText(result, &all);
    QVERIFY(all);
    QVERIFY(!text.contains(QStringLiteral("during the test")));   // hits only matter on a failure
    QVERIFY(text.contains(QStringLiteral("Your IP filter blocks the test server 192.0.2.80 (Cloud | hosting)")));
}

QTEST_GUILESS_MAIN(tst_PortTestReport)
#include "tst_PortTestReport.moc"
