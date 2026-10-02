/// @file tst_IPv6SourcePin.cpp
/// @brief The pinned IPv6 source really leaves the socket: a datagram and a TCP connect
///        to our own global address arrive from the pinned address, not the OS choice.
///
/// Needs a global IPv6 on this host (loopback cannot tell sources apart); skipped otherwise.

#include "TestHelpers.h"
#include "net/IPv6SourcePin.h"
#include "net/LocalIPv6.h"
#include "prefs/Preferences.h"

#include <QNetworkDatagram>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUdpSocket>

using namespace eMule;

class tst_IPv6SourcePin : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void udpDatagramLeavesFromPin();
    void tcpConnectLeavesFromPin();
    void privacySettingLeavesSourceToOS();

private:
    /// Two distinct global addresses of this host: the pin and a destination.
    bool pickAddresses(Address& pin, Address& dest);
};

void tst_IPv6SourcePin::init()
{
    thePrefs.setIpv6UsePrivacyAddress(false);
}

void tst_IPv6SourcePin::cleanup()
{
    IPv6SourcePin::setPinAddress(Address{});
    thePrefs.setIpv6UsePrivacyAddress(false);
}

bool tst_IPv6SourcePin::pickAddresses(Address& pin, Address& dest)
{
    std::vector<Address> global;
    for (const auto& a : scanLocalIPv6().addresses) {
        if (!a.tentative && a.address.isPublicIP())
            global.push_back(a.address);
    }
    if (global.size() < 2)
        return false;
    // Destination: the address the OS would pick anyway; pin: another one, so a
    // match proves the pin was honoured rather than coincidence.
    dest = global.front();
    pin = global.back();
    return true;
}

void tst_IPv6SourcePin::udpDatagramLeavesFromPin()
{
    Address pin, dest;
    if (!pickAddresses(pin, dest))
        QSKIP("needs two global IPv6 addresses on this host");
    IPv6SourcePin::setPinAddress(pin);

    QUdpSocket receiver;
    QVERIFY(receiver.bind(dest.toQHostAddress(), 0));
    QUdpSocket sender;
    QVERIFY(sender.bind(QHostAddress::Any, 0));   // dual-stack, as ClientUDPSocket

    QNetworkDatagram out(QByteArrayLiteral("ping"), dest.toQHostAddress(), receiver.localPort());
    IPv6SourcePin::applyToDatagram(out, dest);
    QVERIFY(sender.writeDatagram(out) > 0);

    QTRY_VERIFY_WITH_TIMEOUT(receiver.hasPendingDatagrams(), 3000);
    const QNetworkDatagram in = receiver.receiveDatagram();
    QCOMPARE(Address::fromQHostAddress(in.senderAddress()), pin);
}

void tst_IPv6SourcePin::tcpConnectLeavesFromPin()
{
    Address pin, dest;
    if (!pickAddresses(pin, dest))
        QSKIP("needs two global IPv6 addresses on this host");
    IPv6SourcePin::setPinAddress(pin);

    QTcpServer server;
    QVERIFY(server.listen(dest.toQHostAddress(), 0));
    QTcpSocket client;
    IPv6SourcePin::bindForConnect(client, dest);
    client.connectToHost(dest.toQHostAddress(), server.serverPort());

    QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 3000);
    std::unique_ptr<QTcpSocket> accepted(server.nextPendingConnection());
    QCOMPARE(Address::fromQHostAddress(accepted->peerAddress()), pin);
}

void tst_IPv6SourcePin::privacySettingLeavesSourceToOS()
{
    const Address pin = Address::fromString(QStringLiteral("2001:678:6d4:9202::1"));
    const Address dest = Address::fromString(QStringLiteral("2606:4700::1"));
    IPv6SourcePin::setPinAddress(pin);
    QCOMPARE(IPv6SourcePin::sourceFor(dest), pin);

    thePrefs.setIpv6UsePrivacyAddress(true);
    QVERIFY(IPv6SourcePin::sourceFor(dest).isNull());

    QNetworkDatagram dg(QByteArrayLiteral("x"), dest.toQHostAddress(), 5672);
    IPv6SourcePin::applyToDatagram(dg, dest);
    QVERIFY(dg.senderAddress().isNull());
}

QTEST_GUILESS_MAIN(tst_IPv6SourcePin)
#include "tst_IPv6SourcePin.moc"
