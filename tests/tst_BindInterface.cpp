/// @file tst_BindInterface.cpp
/// @brief The bind selection resolves to an interface and the pin really confines a socket.
///
/// The pin is checked on the loopback interface: pinned there, a documentation address
/// is refused at once instead of leaving through the default route.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "net/BindAddress.h"
#include "net/ClientReqSocket.h"
#include "net/ClientUDPSocket.h"
#include "net/InterfacePin.h"
#include "net/ListenSocket.h"
#include "net/LocalIPv6.h"
#include "prefs/Preferences.h"

#include <QNetworkInterface>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace eMule;
using BindAddress::LocalInterface;
using BindAddress::State;

class tst_BindInterface : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void resolve_table_data();
    void resolve_table();
    void resolve_literalNarrowsFamily();
    void resolve_wholeInterfaceKeepsBothFamilies();
    void pin_confinesToLoopback();
    void boundByName_listensAndDialsBothFamilies();
    void boundByName_refusesOtherRoutes();
    void blocked_opensNothing_thenResumes();
    void udpSocket_isPinned();
    void ipv6Scan_followsTheInterface();

private:
    QNetworkInterface m_loopback;
    QString m_before;
};

namespace {

QList<LocalInterface> sample()
{
    return {
        {QStringLiteral("lo0"), QStringLiteral("lo0"), 1,
         {QHostAddress(QStringLiteral("127.0.0.1")), QHostAddress(QStringLiteral("::1"))}},
        {QStringLiteral("en0"), QStringLiteral("Ethernet"), 4,
         {QHostAddress(QStringLiteral("192.168.1.20")), QHostAddress(QStringLiteral("fe80::1")),
          QHostAddress(QStringLiteral("2001:db8::20"))}},
        {QStringLiteral("utun4"), QStringLiteral("utun4"), 9,
         {QHostAddress(QStringLiteral("10.66.3.7"))}},
        {QStringLiteral("empty0"), QStringLiteral("empty0"), 11, {}},
    };
}

} // namespace

void tst_BindInterface::init()
{
    m_before = thePrefs.bindAddress();
    const auto all = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& nif : all) {
        if (nif.flags().testFlag(QNetworkInterface::IsLoopBack)
            && nif.flags().testFlag(QNetworkInterface::IsUp)) {
            m_loopback = nif;
            break;
        }
    }
}

void tst_BindInterface::cleanup()
{
    BindAddress::setInterfaceSource({});
    thePrefs.setBindAddress(m_before);
    BindAddress::refresh();
}

void tst_BindInterface::resolve_table_data()
{
    QTest::addColumn<QString>("selector");
    QTest::addColumn<int>("state");
    QTest::addColumn<int>("index");

    QTest::newRow("empty")            << QString()                         << int(State::Unrestricted) << 0;
    QTest::newRow("blank")            << QStringLiteral("   ")             << int(State::Unrestricted) << 0;
    QTest::newRow("name")             << QStringLiteral("utun4")           << int(State::Bound)   << 9;
    QTest::newRow("name-case")        << QStringLiteral("UTUN4")           << int(State::Bound)   << 9;
    QTest::newRow("friendly-name")    << QStringLiteral("ethernet")        << int(State::Bound)   << 4;
    QTest::newRow("v4-literal")       << QStringLiteral("192.168.1.20")    << int(State::Bound)   << 4;
    QTest::newRow("v6-literal")       << QStringLiteral("2001:db8::20")    << int(State::Bound)   << 4;
    QTest::newRow("subnet")           << QStringLiteral("10.64.0.0/10")    << int(State::Bound)   << 9;
    QTest::newRow("subnet-v6")        << QStringLiteral("2001:db8::/32")   << int(State::Bound)   << 4;
    QTest::newRow("unknown-name")     << QStringLiteral("tun0")            << int(State::Blocked) << 0;
    QTest::newRow("literal-not-ours") << QStringLiteral("192.0.2.55")      << int(State::Blocked) << 0;
    QTest::newRow("subnet-no-match")  << QStringLiteral("172.16.0.0/12")   << int(State::Blocked) << 0;
    QTest::newRow("subnet-garbage")   << QStringLiteral("10.0.0.0/zz")     << int(State::Blocked) << 0;
    QTest::newRow("no-address")       << QStringLiteral("empty0")          << int(State::Blocked) << 0;
}

void tst_BindInterface::resolve_table()
{
    QFETCH(QString, selector);
    QFETCH(int, state);
    QFETCH(int, index);

    const BindAddress::Resolution r = BindAddress::resolve(selector, sample());
    QCOMPARE(int(r.state), state);
    QCOMPARE(r.index, index);
    QCOMPARE(r.reason.isEmpty(), r.state != State::Blocked);
}

void tst_BindInterface::resolve_literalNarrowsFamily()
{
    BindAddress::setInterfaceSource(sample);
    thePrefs.setBindAddress(QStringLiteral("192.168.1.20"));

    QCOMPARE(*BindAddress::listenAddress(), QHostAddress(QStringLiteral("192.168.1.20")));
    QVERIFY(BindAddress::canReach(Address::fromQHostAddress(QHostAddress(QStringLiteral("1.2.3.4")))));
    QVERIFY(!BindAddress::canReach(Address::fromQHostAddress(QHostAddress(QStringLiteral("2001:db8::1")))));
    QVERIFY(BindAddress::isIPv4Only());
    QCOMPARE(BindAddress::ipv4Literal(), QStringLiteral("192.168.1.20"));
}

void tst_BindInterface::resolve_wholeInterfaceKeepsBothFamilies()
{
    BindAddress::setInterfaceSource(sample);
    thePrefs.setBindAddress(QStringLiteral("en0"));

    QCOMPARE(*BindAddress::listenAddress(), QHostAddress(QHostAddress::Any));
    QVERIFY(BindAddress::canReach(Address::fromQHostAddress(QHostAddress(QStringLiteral("1.2.3.4")))));
    QVERIFY(BindAddress::canReach(Address::fromQHostAddress(QHostAddress(QStringLiteral("2001:db8::1")))));
    QVERIFY(!BindAddress::isIPv4Only());
    QCOMPARE(BindAddress::ipv4Literal(), QStringLiteral("192.168.1.20"));
    QVERIFY(BindAddress::isBoundInterface(QStringLiteral("Ethernet")));
    QVERIFY(!BindAddress::isBoundInterface(QStringLiteral("utun4")));

    // A tunnel without IPv6 leaves none to advertise.
    thePrefs.setBindAddress(QStringLiteral("utun4"));
    QVERIFY(BindAddress::isIPv4Only());

    // The interface goes away: blocked, with a reason.
    BindAddress::setInterfaceSource([] { return QList<LocalInterface>{}; });
    QVERIFY(BindAddress::refresh());
    QVERIFY(!BindAddress::outboundAllowed());
    QVERIFY(!BindAddress::listenAddress());
    QVERIFY(!BindAddress::current().reason.isEmpty());
    QVERIFY(!BindAddress::refresh());   // unchanged
}

// The raw option: a dual-stack socket pinned to loopback reaches loopback and nothing else.
void tst_BindInterface::pin_confinesToLoopback()
{
    if (!m_loopback.isValid())
        QSKIP("no loopback interface");

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    QTcpSocket inside;
    QVERIFY(inside.bind(QHostAddress(QHostAddress::Any), 0));
    QVERIFY(InterfacePin::pinToInterface(inside.socketDescriptor(), m_loopback.index(), m_loopback.name()));
    inside.connectToHost(QHostAddress(QHostAddress::LocalHost), server.serverPort());
    QVERIFY(inside.waitForConnected(3000));

    // TEST-NET-1: routed through the default route when unpinned, refused when pinned.
    QTcpSocket outside;
    QVERIFY(outside.bind(QHostAddress(QHostAddress::Any), 0));
    QVERIFY(InterfacePin::pinToInterface(outside.socketDescriptor(), m_loopback.index(), m_loopback.name()));
    outside.connectToHost(QHostAddress(QStringLiteral("192.0.2.1")), 9);
    QVERIFY(!outside.waitForConnected(2000));
    QVERIFY2(outside.error() != QAbstractSocket::SocketTimeoutError,
             "the connect was attempted instead of being refused by the pin");

    QVERIFY(!InterfacePin::pinToInterface(-1, m_loopback.index(), m_loopback.name()));
    QVERIFY(!InterfacePin::pinToInterface(inside.socketDescriptor(), 0, QString()));
}

void tst_BindInterface::boundByName_listensAndDialsBothFamilies()
{
    if (!m_loopback.isValid())
        QSKIP("no loopback interface");
    thePrefs.setBindAddress(m_loopback.name().toUpper());
    QCOMPARE(BindAddress::current().index, m_loopback.index());

    ListenSocket listener;
    theApp.listenSocket = &listener;   // accepted sockets unregister through it
    const auto unhook = qScopeGuard([] { theApp.listenSocket = nullptr; });
    QVERIFY(listener.startListening(0));
    QCOMPARE(listener.serverAddress(), QHostAddress(QHostAddress::Any));

    for (const QHostAddress& dest : {QHostAddress(QHostAddress::LocalHost),
                                     QHostAddress(QHostAddress::LocalHostIPv6)}) {
        ClientReqSocket out;
        QSignalSpy connected(&out, &QAbstractSocket::connected);
        out.connectToPeer(Address::fromQHostAddress(dest), listener.serverPort());
        QVERIFY2(connected.wait(3000), qPrintable(dest.toString()));
    }
    listener.stopListening();
}

void tst_BindInterface::boundByName_refusesOtherRoutes()
{
    if (!m_loopback.isValid())
        QSKIP("no loopback interface");
    thePrefs.setBindAddress(m_loopback.name());

    // On the heap: a failed peer socket deletes itself.
    auto* out = new ClientReqSocket;
    QSignalSpy connected(out, &QAbstractSocket::connected);
    QSignalSpy error(out, &QAbstractSocket::errorOccurred);
    out->connectToPeer(Address::fromQHostAddress(QHostAddress(QStringLiteral("192.0.2.1"))), 9);
    QTRY_VERIFY_WITH_TIMEOUT(error.count() > 0, 2000);   // may be raised inside the call
    QCOMPARE(connected.count(), 0);
}

void tst_BindInterface::blocked_opensNothing_thenResumes()
{
    if (!m_loopback.isValid())
        QSKIP("no loopback interface");
    thePrefs.setBindAddress(QStringLiteral("vpn-not-there0"));
    QVERIFY(!BindAddress::outboundAllowed());

    ListenSocket listener;
    QVERIFY(!listener.startListening(0));
    ClientUDPSocket udp;
    QVERIFY(!udp.create());

    QTcpSocket plain;
    QVERIFY(!InterfacePin::prepareOutgoing(plain, false));
    QVERIFY(!InterfacePin::prepareOutgoing(plain, true));   // a proxy is no way round it
    QVERIFY(!InterfacePin::pin(0));

    // The tunnel comes up under that name.
    const int index = m_loopback.index();
    const QString realName = m_loopback.name();
    BindAddress::setInterfaceSource([index, realName] {
        return QList<LocalInterface>{{realName, QStringLiteral("vpn-not-there0"), index,
                                      {QHostAddress(QHostAddress::LocalHost)}}};
    });
    QVERIFY(BindAddress::refresh());
    QVERIFY(BindAddress::outboundAllowed());
    QVERIFY(listener.startListening(0));
    QVERIFY(udp.create());
    QVERIFY(InterfacePin::prepareOutgoing(plain, false));
    QCOMPARE(plain.state(), QAbstractSocket::BoundState);
    listener.stopListening();
}

void tst_BindInterface::udpSocket_isPinned()
{
    if (!m_loopback.isValid())
        QSKIP("no loopback interface");
    thePrefs.setBindAddress(m_loopback.name());

    ClientUDPSocket udp;
    QVERIFY(udp.create());
    QVERIFY(udp.connectedPort() != 0);
    udp.close();
    QVERIFY(udp.rebind(0));
}

void tst_BindInterface::ipv6Scan_followsTheInterface()
{
    if (!m_loopback.isValid())
        QSKIP("no loopback interface");
    // Loopback holds no global address, whatever the host has elsewhere.
    thePrefs.setBindAddress(m_loopback.name());
    QVERIFY(scanBoundIPv6().addresses.empty());

    thePrefs.setBindAddress(QStringLiteral("vpn-not-there0"));
    QVERIFY(scanBoundIPv6().addresses.empty());
}

QTEST_MAIN(tst_BindInterface)
#include "tst_BindInterface.moc"
