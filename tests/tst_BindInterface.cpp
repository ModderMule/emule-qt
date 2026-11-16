/// @file tst_BindInterface.cpp
/// @brief The bind selection resolves to an interface and the pin really confines a socket.
///
/// The pin is checked on the loopback interface: pinned there, a documentation address
/// is refused at once instead of leaving through the default route.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "net/AddressWatch.h"
#include "net/BindAddress.h"
#include "net/ClientReqSocket.h"
#include "net/ClientUDPSocket.h"
#include "net/InterfacePin.h"
#include "net/ListenSocket.h"
#include "net/LocalIPv6.h"
#include "net/UdpSocketSet.h"
#include "prefs/Preferences.h"

#include <QNetworkInterface>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUdpSocket>

#ifdef Q_OS_LINUX
#include <sys/socket.h>
#endif

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
    void sourceAddress_forUnpinnableProcess();
    void pin_confinesToLoopback();
    void probe_v6RefusedKeepsIPv4();
    void probe_dualStackV4RefusedBindsIPv4();
    void probe_v4RefusedKeepsIPv6();
    void probe_bothRefusedBlocks();
    void probe_literalNeedsItsFamily();
    void probe_loopbackPinsIPv4();
    void boundByName_listensAndDialsBothFamilies();
    void boundByName_refusesOtherRoutes();
    void blocked_opensNothing_thenResumes();
    void udpSocket_isPinned();
    void ipv6Scan_followsTheInterface();
    void sameBinding_ignoresRoutes();
    void sameBinding_ignoresIPv6OnWholeInterface();
    void listenAddresses_table_data();
    void listenAddresses_table();
    void listenAddresses_followPrefs();
    void boundByName_leavesOtherInterfacesFree();
    void listener_followsAddressChanges();
    void udpSet_sendsFromTheFamilySocket();
    void udpSet_followsAddressChanges();
    void addressWatch_foldsBurst();

private:
    QNetworkInterface m_loopback;
    QString m_before;
};

namespace {

#ifdef Q_OS_LINUX
// The interface a socket is bound to (SO_BINDTOIFINDEX), 0 when none.
int pinnedIfIndex(qintptr fd)
{
    int idx = 0;
    socklen_t len = sizeof(idx);
    return ::getsockopt(static_cast<int>(fd), SOL_SOCKET, SO_BINDTOIFINDEX, &idx, &len) == 0 ? idx : 0;
}
#endif

// A non-loopback IPv4 of this host, null when none.
QHostAddress otherIPv4()
{
    const auto all = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& nif : all) {
        const auto flags = nif.flags();
        if (flags.testFlag(QNetworkInterface::IsLoopBack) || !flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning))
            continue;
        for (const QNetworkAddressEntry& e : nif.addressEntries())
            if (e.ip().protocol() == QAbstractSocket::IPv4Protocol && !e.ip().isLoopback())
                return e.ip();
    }
    return {};
}

// ::1 usable on this host
bool hasLoopbackIPv6()
{
    QUdpSocket probe;
    return probe.bind(QHostAddress(QHostAddress::LocalHostIPv6), 0);
}

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
    BindAddress::setPinProbe({});
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

// One address for the hoster sidecar: literal, interface IPv4, else IPv6; loopback when blocked.
void tst_BindInterface::sourceAddress_forUnpinnableProcess()
{
    BindAddress::setInterfaceSource(sample);
    thePrefs.setBindAddress(QString());
    QVERIFY(BindAddress::sourceAddress().isEmpty());

    thePrefs.setBindAddress(QStringLiteral("2001:db8::20"));
    QCOMPARE(BindAddress::sourceAddress(), QStringLiteral("2001:db8::20"));

    thePrefs.setBindAddress(QStringLiteral("en0"));
    QCOMPARE(BindAddress::sourceAddress(), QStringLiteral("192.168.1.20"));

    // IPv4 cannot be pinned: the interface's IPv6, never the link-local one
    BindAddress::setPinProbe([](int, const QString&) {
        return BindAddress::PinProbe{.v4 = false, .v6 = true, .v4Error = 22, .v6Error = 0};
    });
    thePrefs.setBindAddress(QStringLiteral("utun4"));
    thePrefs.setBindAddress(QStringLiteral("en0"));
    QCOMPARE(BindAddress::sourceAddress(), QStringLiteral("2001:db8::20"));
    BindAddress::setPinProbe({});

    // the interface goes away: nothing may leave
    BindAddress::setInterfaceSource([] { return QList<LocalInterface>{}; });
    BindAddress::refresh();
    QCOMPARE(BindAddress::sourceAddress(), QStringLiteral("127.0.0.1"));
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
#ifdef Q_OS_LINUX
    // Linux takes an unrouted destination as on-link of the bound device: the SYN goes
    // out lo and dies there, a timeout and no refusal. The pin is what keeps it on lo.
    QCOMPARE(pinnedIfIndex(outside.socketDescriptor()), m_loopback.index());
    QVERIFY(!outside.waitForConnected(2000));
#else
    QVERIFY(!outside.waitForConnected(2000));
    QVERIFY2(outside.error() != QAbstractSocket::SocketTimeoutError,
             "the connect was attempted instead of being refused by the pin");
#endif

    QVERIFY(!InterfacePin::pinToInterface(-1, m_loopback.index(), m_loopback.name()));
    QVERIFY(!InterfacePin::pinToInterface(inside.socketDescriptor(), 0, QString()));
}

// Windows refuses IPV6_UNICAST_IF on an adapter without IPv6 (#67): IPv4 goes on alone.
void tst_BindInterface::probe_v6RefusedKeepsIPv4()
{
    BindAddress::setInterfaceSource(sample);
    BindAddress::setPinProbe([](int, const QString&) {
        return BindAddress::PinProbe{.v4 = true, .v6 = false, .v4Error = 0, .v6Error = 10022};
    });
    thePrefs.setBindAddress(QStringLiteral("en0"));

    QCOMPARE(int(BindAddress::current().state), int(State::Bound));
    QCOMPARE(*BindAddress::listenAddress(), QHostAddress(QHostAddress::AnyIPv4));
    const Address v4 = Address::fromQHostAddress(QHostAddress(QStringLiteral("1.2.3.4")));
    const Address v6 = Address::fromQHostAddress(QHostAddress(QStringLiteral("2001:db8::1")));
    QVERIFY(BindAddress::canReach(v4));
    QVERIFY(!BindAddress::canReach(v6));
    QCOMPARE(*BindAddress::sourceFor(v4), QHostAddress(QHostAddress::AnyIPv4));
    QVERIFY(!BindAddress::sourceFor(v6));
    QVERIFY(BindAddress::isIPv4Only());
    QCOMPARE(BindAddress::ipv4Literal(), QStringLiteral("192.168.1.20"));
}

// Windows: IPv4 half of a dual-stack socket not pinnable -> IPv4-only sockets, else Kad's
// IPv4 leaves by the default route and peers see another IP (HighID, Kad firewalled)
void tst_BindInterface::probe_dualStackV4RefusedBindsIPv4()
{
    BindAddress::setInterfaceSource(sample);
    BindAddress::setPinProbe([](int, const QString&) {
        return BindAddress::PinProbe{.dual = false, .dualError = 10022};
    });
    thePrefs.setBindAddress(QStringLiteral("en0"));

    QCOMPARE(int(BindAddress::current().state), int(State::Bound));
    QCOMPARE(*BindAddress::listenAddress(), QHostAddress(QHostAddress::AnyIPv4));
    QVERIFY(!BindAddress::current().hasGlobalIPv6);
    QVERIFY(!BindAddress::sourceFor(Address::fromQHostAddress(QHostAddress(QStringLiteral("2001:db8::1")))));
    QVERIFY(BindAddress::isIPv4Only());
}

void tst_BindInterface::probe_v4RefusedKeepsIPv6()
{
    BindAddress::setInterfaceSource(sample);
    BindAddress::setPinProbe([](int, const QString&) {
        return BindAddress::PinProbe{.v4 = false, .v6 = true, .v4Error = 22, .v6Error = 0};
    });
    thePrefs.setBindAddress(QStringLiteral("en0"));

    QCOMPARE(*BindAddress::listenAddress(), QHostAddress(QHostAddress::AnyIPv6));
    QVERIFY(!BindAddress::canReach(Address::fromQHostAddress(QHostAddress(QStringLiteral("1.2.3.4")))));
    QVERIFY(BindAddress::canReach(Address::fromQHostAddress(QHostAddress(QStringLiteral("2001:db8::1")))));
    QVERIFY(BindAddress::ipv4Literal().isEmpty());
    QVERIFY(!BindAddress::isIPv4Only());
}

void tst_BindInterface::probe_bothRefusedBlocks()
{
    BindAddress::setInterfaceSource(sample);
    BindAddress::setPinProbe([](int, const QString&) {
        return BindAddress::PinProbe{.v4 = false, .v6 = false, .v4Error = 10022, .v6Error = 10022};
    });
    thePrefs.setBindAddress(QStringLiteral("en0"));

    QCOMPARE(int(BindAddress::current().state), int(State::Blocked));
    QVERIFY(BindAddress::current().reason.contains(QStringLiteral("10022")));
    QVERIFY(!BindAddress::listenAddress());
    QVERIFY(!BindAddress::outboundAllowed());
}

void tst_BindInterface::probe_literalNeedsItsFamily()
{
    BindAddress::setInterfaceSource(sample);
    BindAddress::setPinProbe([](int, const QString&) {
        return BindAddress::PinProbe{.v4 = true, .v6 = false, .v4Error = 0, .v6Error = 10022};
    });
    thePrefs.setBindAddress(QStringLiteral("192.168.1.20"));
    QCOMPARE(int(BindAddress::current().state), int(State::Bound));

    thePrefs.setBindAddress(QStringLiteral("2001:db8::20"));
    QCOMPARE(int(BindAddress::current().state), int(State::Blocked));
}

// The real probe on loopback; the unbound probe socket must still report its family.
void tst_BindInterface::probe_loopbackPinsIPv4()
{
    if (!m_loopback.isValid())
        QSKIP("no loopback interface");
    const BindAddress::PinProbe p = InterfacePin::probe(m_loopback.index(), m_loopback.name());
    QVERIFY2(p.v4, qPrintable(QString::number(p.v4Error)));
    QCOMPARE(p.v4Error, 0);

    int error = 0;
    QVERIFY(!InterfacePin::pinToInterface(-1, m_loopback.index(), m_loopback.name(), &error));
    QVERIFY(error != 0);
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
    // each address of the interface, not the wildcard (#190)
    QCOMPARE(listener.boundAddresses(), BindAddress::listenAddresses());
    QVERIFY(!listener.boundAddresses().contains(QHostAddress(QHostAddress::Any)));
    QVERIFY(listener.boundAddresses().contains(QHostAddress(QHostAddress::LocalHost)));

    // The accepted side must keep the connection too (Windows: a v4 peer on the
    // dual-stack listener refused the IPv6 re-pin and was dropped)
    QTest::failOnWarning(QRegularExpression(QStringLiteral("socket not used")));
    for (const QHostAddress& dest : {QHostAddress(QHostAddress::LocalHost),
                                     QHostAddress(QHostAddress::LocalHostIPv6)}) {
        ClientReqSocket out;
        QSignalSpy connected(&out, &QAbstractSocket::connected);
        out.connectToPeer(Address::fromQHostAddress(dest), listener.serverPort());
        QVERIFY2(connected.wait(3000), qPrintable(dest.toString()));
        QTest::qWait(100);   // let the listener accept
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
#ifdef Q_OS_LINUX
    // no refusal on Linux (see pin_confinesToLoopback): pinned to lo, never connected
    const QPointer<ClientReqSocket> guard(out);
    QVERIFY(out->socketDescriptor() >= 0);
    QCOMPARE(pinnedIfIndex(out->socketDescriptor()), m_loopback.index());
    QTest::qWait(500);
    QCOMPARE(connected.count(), 0);
    delete guard.data();
#else
    QTRY_VERIFY_WITH_TIMEOUT(error.count() > 0, 2000);   // may be raised inside the call
    QCOMPARE(connected.count(), 0);
#endif
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

// VPN routes appear after the adapter: no reopen for that alone (#67)
void tst_BindInterface::sameBinding_ignoresRoutes()
{
    const BindAddress::Resolution a = BindAddress::resolve(QStringLiteral("utun4"), sample());
    BindAddress::Resolution b = a;
    b.noInternetRoute = true;
    QVERIFY(a != b);
    QVERIFY(BindAddress::sameBinding(a, b));
    b.index = 12;   // adapter recreated under a new index
    QVERIFY(!BindAddress::sameBinding(a, b));
}

// Linux privacy addresses expire: whole-interface pin keeps its sockets (#117)
void tst_BindInterface::sameBinding_ignoresIPv6OnWholeInterface()
{
    BindAddress::Resolution a = BindAddress::resolve(QStringLiteral("utun4"), sample());
    QVERIFY(a.wholeInterface());
    BindAddress::Resolution b = a;
    b.hasGlobalIPv6 = !a.hasGlobalIPv6;
    QVERIFY(BindAddress::sameBinding(a, b));

    // pinned to an IPv6 literal: unchanged behaviour
    a.literal = QHostAddress(QStringLiteral("2001:db8::1"));
    b.literal = a.literal;
    QVERIFY(!a.wholeInterface());
    QVERIFY(!BindAddress::sameBinding(a, b));
}

void tst_BindInterface::listenAddresses_table_data()
{
    QTest::addColumn<QString>("selector");
    QTest::addColumn<bool>("v4Pin");
    QTest::addColumn<bool>("v6Pin");
    QTest::addColumn<bool>("privacy");
    QTest::addColumn<QStringList>("expected");

    const QString v4 = QStringLiteral("192.168.1.20");
    const QString stable = QStringLiteral("2001:db8::20");
    const QString temp = QStringLiteral("2001:db8::99");
    QTest::newRow("unrestricted")  << QString() << true << true << false << QStringList{QStringLiteral("0.0.0.0")};
    QTest::newRow("blocked")       << QStringLiteral("tun0") << true << true << false << QStringList{};
    QTest::newRow("literal")       << v4 << true << true << false << QStringList{v4};
    // no link-local, no privacy address, no deprecated one; IPv4 first
    QTest::newRow("whole")         << QStringLiteral("en0") << true << true << false << QStringList{v4, stable};
    // the privacy pref advertises the temporary one: it sends, so first
    QTest::newRow("privacy")       << QStringLiteral("en0") << true << true << true << QStringList{v4, temp, stable};
    QTest::newRow("v6-unpinnable") << QStringLiteral("en0") << true << false << false << QStringList{v4};
    QTest::newRow("v4-unpinnable") << QStringLiteral("en0") << false << true << false << QStringList{stable};
    // nothing but temporary ones: those, else no IPv6 at all
    QTest::newRow("only-temp")     << QStringLiteral("utun4") << true << true << false
                                   << QStringList{QStringLiteral("10.66.3.7"), QStringLiteral("2001:db8:1::5")};
    QTest::newRow("v4-only-tunnel") << QStringLiteral("tap1") << true << true << false
                                   << QStringList{QStringLiteral("10.8.0.2"), QStringLiteral("10.8.0.3")};
}

void tst_BindInterface::listenAddresses_table()
{
    QFETCH(QString, selector);
    QFETCH(bool, v4Pin);
    QFETCH(bool, v6Pin);
    QFETCH(bool, privacy);
    QFETCH(QStringList, expected);

    const auto h = [](const char* a) { return QHostAddress(QString::fromLatin1(a)); };
    QList<LocalInterface> ifs = sample();
    ifs[1].addresses += {h("2001:db8::99"), h("2001:db8::98")};
    ifs[1].temporary = {h("2001:db8::99"), h("2001:db8::98")};
    ifs[1].deprecated = {h("2001:db8::98")};
    ifs[2].addresses += {h("fe80::7"), h("2001:db8:1::5")};
    ifs[2].temporary = {h("2001:db8:1::5")};
    ifs.append({QStringLiteral("tap1"), QStringLiteral("tap1"), 12, {h("10.8.0.2"), h("10.8.0.3")}});

    BindAddress::Resolution r = BindAddress::resolve(selector, ifs);
    r.pin.v4 = v4Pin;
    r.pin.v6 = v6Pin;
    QStringList got;
    for (const QHostAddress& a : BindAddress::listenAddressesOf(r, ifs, privacy))
        got.append(a.toString());
    QCOMPARE(got, expected);
}

// The prefs path: the selection and the interface list of now
void tst_BindInterface::listenAddresses_followPrefs()
{
    BindAddress::setInterfaceSource(sample);
    thePrefs.setBindAddress(QStringLiteral("en0"));
    QCOMPARE(BindAddress::listenAddresses(),
             (QList{QHostAddress(QStringLiteral("192.168.1.20")), QHostAddress(QStringLiteral("2001:db8::20"))}));
    thePrefs.setBindAddress(QString());
    QCOMPARE(BindAddress::listenAddresses(), QList{QHostAddress(QHostAddress::Any)});
    thePrefs.setBindAddress(QStringLiteral("vpn-not-there0"));
    QVERIFY(BindAddress::listenAddresses().isEmpty());
}

// #190: an interface selected by name holds its port on its own addresses only; another
// program takes the same port on another interface
void tst_BindInterface::boundByName_leavesOtherInterfacesFree()
{
    if (!m_loopback.isValid())
        QSKIP("no loopback interface");
    const QHostAddress other = otherIPv4();
    if (other.isNull())
        QSKIP("no IPv4 on another interface");
    thePrefs.setBindAddress(m_loopback.name());

    ListenSocket listener;
    QVERIFY(listener.startListening(0));
    QVERIFY(!listener.boundAddresses().contains(QHostAddress(QHostAddress::Any)));
    QTcpServer otherApp;
    QVERIFY2(otherApp.listen(other, listener.serverPort()), qPrintable(otherApp.errorString()));
    otherApp.close();
    listener.stopListening();

    ClientUDPSocket udp;
    QVERIFY(udp.create());
    QCOMPARE(udp.socket().boundAddresses(), BindAddress::listenAddresses());
    QUdpSocket otherUdp;
    QVERIFY2(otherUdp.bind(other, udp.connectedPort(), QAbstractSocket::DontShareAddress),
             qPrintable(otherUdp.errorString()));
}

// An address comes and goes on the selected interface: the listener follows it, the
// others keep listening, the port stays
void tst_BindInterface::listener_followsAddressChanges()
{
    if (!m_loopback.isValid())
        QSKIP("no loopback interface");
    if (!hasLoopbackIPv6())
        QSKIP("no ::1");
    const int index = m_loopback.index();
    const QString name = m_loopback.name();
    QList<QHostAddress> addresses{QHostAddress(QHostAddress::LocalHost)};
    BindAddress::setInterfaceSource([&] { return QList<LocalInterface>{{name, name, index, addresses}}; });
    thePrefs.setBindAddress(name);

    ListenSocket listener;
    theApp.listenSocket = &listener;
    const auto unhook = qScopeGuard([] { theApp.listenSocket = nullptr; });
    QVERIFY(listener.startListening(0));
    const uint16 port = listener.serverPort();
    QCOMPARE(listener.boundAddresses(), QList{QHostAddress(QHostAddress::LocalHost)});
    // Darwin 27: a refused dial reports connected() with no peer port
    const auto dial = [port](const QHostAddress& to) {
        QTcpSocket s;
        s.connectToHost(to, port);
        return s.waitForConnected(2000) && s.peerPort() == port;
    };
    QVERIFY(dial(QHostAddress(QHostAddress::LocalHost)));
    QVERIFY(!dial(QHostAddress(QHostAddress::LocalHostIPv6)));

    addresses.append(QHostAddress(QHostAddress::LocalHostIPv6));
    QVERIFY(listener.syncAddresses(BindAddress::listenAddresses()));
    QCOMPARE(listener.serverPort(), port);
    QVERIFY(dial(QHostAddress(QHostAddress::LocalHostIPv6)));
    QVERIFY(dial(QHostAddress(QHostAddress::LocalHost)));
    QVERIFY(!listener.syncAddresses(BindAddress::listenAddresses()));   // nothing new

    // the first address goes: the server moves to the next, still listening on the port
    addresses.removeFirst();
    QVERIFY(listener.syncAddresses(BindAddress::listenAddresses()));
    QVERIFY(listener.isListening());
    QCOMPARE(listener.serverPort(), port);
    QCOMPARE(listener.boundAddresses(), QList{QHostAddress(QHostAddress::LocalHostIPv6)});
    QVERIFY(dial(QHostAddress(QHostAddress::LocalHostIPv6)));
    QVERIFY(!dial(QHostAddress(QHostAddress::LocalHost)));

    listener.stopListening();
    QVERIFY(!listener.isListening());
    QVERIFY(listener.boundAddresses().isEmpty());
    QVERIFY(!listener.syncAddresses(BindAddress::listenAddresses()));   // closed: no-op
}

// Each datagram leaves from the socket of its family; the peer sees that address and the
// one port of the set
void tst_BindInterface::udpSet_sendsFromTheFamilySocket()
{
    if (!hasLoopbackIPv6())
        QSKIP("no ::1");
    const QHostAddress v4(QHostAddress::LocalHost);
    const QHostAddress v6(QHostAddress::LocalHostIPv6);
    UdpSocketSet set(QStringLiteral("test UDP"));
    QVERIFY(set.bindTo({v4, v6}, 0));
    QCOMPARE(set.socketCount(), 2);
    const quint16 port = set.localPort();
    QVERIFY(port != 0);

    QUdpSocket peer4, peer6;
    QVERIFY(peer4.bind(v4, 0));
    QVERIFY(peer6.bind(v6, 0));
    QVERIFY(set.writeDatagram(QByteArray("four"), v4, peer4.localPort()) > 0);
    // a sender of the other family is not ours to honour
    QNetworkDatagram dg(QByteArray("six"), v6, peer6.localPort());
    dg.setSender(v4);
    QVERIFY(set.writeDatagram(dg) > 0);
    QTRY_VERIFY_WITH_TIMEOUT(peer4.hasPendingDatagrams(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(peer6.hasPendingDatagrams(), 2000);
    const QNetworkDatagram got4 = peer4.receiveDatagram();
    const QNetworkDatagram got6 = peer6.receiveDatagram();
    QCOMPARE(got4.data(), QByteArray("four"));
    QCOMPARE(got4.senderPort(), int(port));
    QCOMPARE(got6.data(), QByteArray("six"));
    QCOMPARE(got6.senderAddress(), v6);
    QCOMPARE(got6.senderPort(), int(port));

    // both sockets come out of one receive queue
    QSignalSpy ready(&set, &UdpSocketSet::readyRead);
    peer4.writeDatagram(QByteArray("a"), v4, port);
    peer6.writeDatagram(QByteArray("b"), v6, port);
    QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 2000);
    QSet<QByteArray> received;
    QTRY_VERIFY_WITH_TIMEOUT([&] {
        while (set.hasPendingDatagrams())
            received.insert(set.receiveDatagram().data());
        return received.size() == 2;
    }(), 2000);

    // one family only: the other has no socket
    UdpSocketSet only4(QStringLiteral("test UDP"));
    QVERIFY(only4.bindTo({v4}, 0));
    QCOMPARE(only4.writeDatagram(QByteArray("x"), v6, peer6.localPort()), qint64(-1));
    QVERIFY(!only4.sendErrorString().isEmpty());
}

void tst_BindInterface::udpSet_followsAddressChanges()
{
    if (!m_loopback.isValid())
        QSKIP("no loopback interface");
    if (!hasLoopbackIPv6())
        QSKIP("no ::1");
    const int index = m_loopback.index();
    const QString name = m_loopback.name();
    QList<QHostAddress> addresses{QHostAddress(QHostAddress::LocalHost)};
    BindAddress::setInterfaceSource([&] { return QList<LocalInterface>{{name, name, index, addresses}}; });
    thePrefs.setBindAddress(name);

    ClientUDPSocket udp;
    QVERIFY(udp.create());
    const uint16 port = udp.connectedPort();
    QCOMPARE(udp.socket().socketCount(), 1);
    addresses.append(QHostAddress(QHostAddress::LocalHostIPv6));
    QVERIFY(udp.syncAddresses(BindAddress::listenAddresses()));
    QCOMPARE(udp.socket().socketCount(), 2);
    QCOMPARE(udp.connectedPort(), port);
    addresses.removeFirst();
    QVERIFY(udp.syncAddresses(BindAddress::listenAddresses()));
    QCOMPARE(udp.socket().boundAddresses(), QList{QHostAddress(QHostAddress::LocalHostIPv6)});
    QVERIFY(udp.isBound());

    // bound to an explicit address: sync leaves it be
    UdpSocketSet fixed(QStringLiteral("test UDP"));
    QVERIFY(fixed.bindTo({QHostAddress(QHostAddress::LocalHost)}, 0));
    QVERIFY(!fixed.sync({}));
    QVERIFY(fixed.isBound());
}

// The OS source is open on the desktop systems; a burst of events is one changed()
void tst_BindInterface::addressWatch_foldsBurst()
{
    AddressWatch watch;
#if defined(Q_OS_LINUX) || defined(Q_OS_DARWIN) || defined(Q_OS_WIN)
    QVERIFY(watch.isActive());
#endif
    QSignalSpy changed(&watch, &AddressWatch::changed);
    for (int i = 0; i < 5; ++i)
        watch.noteEvent();
    QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 2000);
    QTest::qWait(400);
    QCOMPARE(changed.count(), 1);
}

QTEST_MAIN(tst_BindInterface)
#include "tst_BindInterface.moc"
