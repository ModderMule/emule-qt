/// @file tst_Ed3kPlumbing.cpp
/// @brief Preferences → ed3k node (src/ed3k): extension switches, the IPv6 source, filter and
/// hops state across a restart, LAN discovery between two nodes, node list links and asks.

#include "Ed3kBootstrap.h"
#include "Ed3kLan.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include "net/Address.h"
#include "net/IPv6SourcePin.h"
#include "prefs/Preferences.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

using namespace eMule;
using namespace eMule::ed3k;

namespace {

Ed3kSettings loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");
    s.forceOpen = true;
    s.lan = false;
    return s;
}

::ed3k::Key32 key(std::uint8_t fill)
{
    ::ed3k::Key32 k{};
    k.fill(fill);
    return k;
}

/// LanPeer events of one session.
struct LanPeers {
    std::vector<::ed3k::NodeId> seen;
    explicit LanPeers(Ed3kSession& s)
    {
        QObject::connect(&s, &Ed3kSession::eventPolled, &s, [this](const ::ed3k::Event& ev, bool*) {
            if (const auto* p = std::get_if<::ed3k::event::LanPeer>(&ev))
                seen.push_back(p->node);
        }, Qt::DirectConnection);
    }
};

} // namespace

class tst_Ed3kPlumbing : public QObject {
    Q_OBJECT

private slots:
    void extensionNamesMapToBits();
    void extOffReachesTheEngine();
    void nickGoesToTheEngine();
    void base32IsRfc4648LowerCase();
    void nodesLinkParsedAndBuilt();
    void advertisedIPv6FollowsThePin();
    void filterStateKeptAcrossRestart();
    void lanNodesFindEachOther();
    void lanOffWithThePref();
    void nodesLinkRefusedWithoutAGui();
    void nodesLinkAskedAndAnswered();
    void cleanup();
};

void tst_Ed3kPlumbing::cleanup()
{
    IPv6SourcePin::setPinAddress(Address());
    QTest::qWait(10);
}

void tst_Ed3kPlumbing::extensionNamesMapToBits()
{
    // every name the pref takes is an extension of the engine
    std::uint64_t all = 0;
    for (const QString& name : Preferences::ed3kExtensionNames()) {
        const std::uint64_t bit = extBit(name);
        QVERIFY2(bit != 0, qPrintable(name));
        QVERIFY2((all & bit) == 0, qPrintable(name));
        all |= bit;
    }
    QCOMPARE(extBit(QStringLiteral(" LAN ")), std::uint64_t(ED3K_EXT_LAN));
    QCOMPARE(extBit(QStringLiteral("lan-discovery")), std::uint64_t(ED3K_EXT_LAN));
    QCOMPARE(extBit(QStringLiteral("ip-diversity")), std::uint64_t(0));   // required, no switch
    QStringList unknown;
    const auto bits = extOffBits({QStringLiteral("chat"), QStringLiteral("nope"), QString(), QStringLiteral("file-set")}, &unknown);
    QCOMPARE(bits, std::uint64_t(ED3K_EXT_CHAT | ED3K_EXT_FILE_SET));
    QCOMPARE(unknown, QStringList{QStringLiteral("nope")});
}

void tst_Ed3kPlumbing::extOffReachesTheEngine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    const std::uint64_t on = s.extensions();
    QVERIFY((on & ED3K_EXT_CHAT) != 0);
    QVERIFY((on & ED3K_EXT_HOSTER) != 0);
    s.stop();

    Ed3kSettings off = loopback(dir.path());
    off.extOff = extOffBits({QStringLiteral("chat"), QStringLiteral("hoster")});
    QVERIFY(s.start(off));
    QCOMPARE(s.extensions() & (ED3K_EXT_CHAT | ED3K_EXT_HOSTER), std::uint64_t(0));
    QVERIFY((s.extensions() & ED3K_EXT_ALIAS) != 0);
    QCOMPARE(s.settings().extOff, off.extOff);
    s.stop();
}

void tst_Ed3kPlumbing::nickGoesToTheEngine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    Ed3kSettings st = loopback(dir.path());
    st.nick = QStringLiteral("Tester ünï");
    QVERIFY(s.start(st));
    QCOMPARE(s.settings().nick, st.nick);
    s.stop();
}

void tst_Ed3kPlumbing::base32IsRfc4648LowerCase()
{
    const QByteArray foobar("foobar");
    const std::span<const std::uint8_t> b{reinterpret_cast<const std::uint8_t*>(foobar.constData()), std::size_t(foobar.size())};
    QCOMPARE(base32(b), QStringLiteral("mzxw6ytboi"));
    QCOMPARE(*fromBase32(QStringLiteral("mzxw6ytboi")), foobar);
    QCOMPARE(*fromBase32(QStringLiteral("MZXW6YTBOI")), foobar);
    QVERIFY(!fromBase32(QStringLiteral("mzxw6ytbo1")));    // '1' is not base32
    QVERIFY(!fromBase32(QStringLiteral("mzxw6ytboj")));    // non-zero padding bits
    const auto k = key(0xa5);
    QCOMPARE(fromBase32(base32(k))->size(), 32);
}

void tst_Ed3kPlumbing::nodesLinkParsedAndBuilt()
{
    const auto k = key(0x42);
    const QString url = QStringLiteral("https://nodes.example.org/v1/nodes.ed3knod?x=1&y=2");
    const QString link = nodesLink(url, k);
    QVERIFY(link.startsWith(QStringLiteral("ed3k:nodes?url=https%3A%2F%2F")));
    const auto src = parseNodesLink(link);
    QVERIFY(src);
    QCOMPARE(src->url, url);
    QVERIFY(src->key == k);
    // bootstrap.md §3: missing or repeated url/key, a key not 32 B, not https → invalid
    const QString kb = base32(k);
    QVERIFY(!parseNodesLink(QStringLiteral("ed3k:nodes?key=%1").arg(kb)));
    QVERIFY(!parseNodesLink(QStringLiteral("ed3k:nodes?url=https%3A%2F%2Fa.example")));
    QVERIFY(!parseNodesLink(QStringLiteral("ed3k:nodes?url=https%3A%2F%2Fa.example&key=%1&key=%1").arg(kb)));
    QVERIFY(!parseNodesLink(QStringLiteral("ed3k:nodes?url=http%3A%2F%2Fa.example&key=%1").arg(kb)));
    QVERIFY(!parseNodesLink(QStringLiteral("ed3k:nodes?url=%2Fnodes&key=%1").arg(kb)));
    QVERIFY(!parseNodesLink(QStringLiteral("ed3k:nodes?url=https%3A%2F%2Fa.example&key=mzxw6ytboi")));
    QVERIFY(!parseNodesLink(QStringLiteral("ed3k:file?url=https%3A%2F%2Fa.example&key=%1").arg(kb)));
    // another parameter is no reason to refuse
    QVERIFY(parseNodesLink(QStringLiteral("ed3k:nodes?url=https%3A%2F%2Fa.example&key=%1&n=x").arg(kb)));
}

void tst_Ed3kPlumbing::advertisedIPv6FollowsThePin()
{
    const QHostAddress pin(QStringLiteral("2001:db8::5"));
    QCOMPARE(advertisedIPv6(QHostAddress::Any, pin), pin);
    QCOMPARE(advertisedIPv6(QHostAddress::AnyIPv6, pin), pin);
    QVERIFY(advertisedIPv6(QHostAddress::Any, QHostAddress()).isNull());
    QVERIFY(advertisedIPv6(QHostAddress::Any, QHostAddress(QStringLiteral("10.0.0.1"))).isNull());
    // a bound address sends from itself
    QVERIFY(advertisedIPv6(QHostAddress::LocalHost, pin).isNull());
    QVERIFY(advertisedIPv6(QHostAddress(QStringLiteral("2001:db8::9")), pin).isNull());

    // the session: a wildcard socket advertises the pinned address, and follows a move
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    IPv6SourcePin::setPinAddress(Address::fromQHostAddress(pin));
    Ed3kSession s;
    Ed3kSettings st = loopback(dir.path());
    st.bindAddress = QHostAddress::Any;
    QVERIFY(s.start(st));
    QCOMPARE(s.ipv6Source(), pin);
    const QHostAddress moved(QStringLiteral("2001:db8::6"));
    IPv6SourcePin::setPinAddress(Address::fromQHostAddress(moved));
    QTRY_COMPARE_WITH_TIMEOUT(s.ipv6Source(), moved, 20000);
    IPv6SourcePin::setPinAddress(Address());
    QTRY_VERIFY_WITH_TIMEOUT(s.ipv6Source().isNull(), 20000);
    s.stop();
}

void tst_Ed3kPlumbing::filterStateKeptAcrossRestart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    std::array<std::uint8_t, 16> id{};
    id.fill(0x7e);
    const QString file = QDir(stateDir(dir.path())).filePath(QStringLiteral("filter.dat"));
    const QString names = QDir(stateDir(dir.path())).filePath(QStringLiteral("names.dat"));
    const auto bytes = [](const ::ed3k::Buffer& b) {
        const auto v = b.bytes();
        return QByteArray(reinterpret_cast<const char*>(v.data()), qsizetype(v.size()));
    };
    QByteArray namesBefore;
    {
        Ed3kSession s;
        QVERIFY(s.start(loopback(dir.path())));
        QVERIFY(s.engine()->filterSetOwn(s.now(), ::ed3k::FilterSubject::File, id, ::ed3k::FilterVerdict::Spam, "junk"));
        // the names slot (ED3K_STATE_NAMES) like the others
        QVERIFY(s.engine()->namePetnameSet(s.now(), "alice", key(5)));
        s.pump();
        const auto blob = s.engine()->stateGet(s.now(), ::ed3k::State::Names);
        QVERIFY(blob && !blob->empty());
        namesBefore = bytes(*blob);
        s.stop();
    }
    QVERIFY(QFile::exists(file));
    QVERIFY(QFile::exists(names));
#ifdef Q_OS_UNIX
    QCOMPARE(QFile(file).permissions() & (QFileDevice::ReadOther | QFileDevice::ReadGroup), QFileDevice::Permissions{});
#endif
    Ed3kSession again;
    QVERIFY(again.start(loopback(dir.path())));
    const auto j = again.engine()->filterJudge(::ed3k::FilterSubject::File, id);
    QVERIFY(j);
    QVERIFY(j->own);
    QCOMPARE(j->own->first, ::ed3k::FilterVerdict::Spam);
    const auto namesAfter = again.engine()->stateGet(again.now(), ::ed3k::State::Names);
    QVERIFY(namesAfter);
    QCOMPARE(bytes(*namesAfter), namesBefore);
    again.stop();
}

// lan-discovery.md §1 between two nodes; the sockets by hand (the core leaves loopback out,
// a real interface needs the Local Network permission on macOS)
void tst_Ed3kPlumbing::lanNodesFindEachOther()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    const QHostAddress ipA(QStringLiteral("10.77.0.1")), ipB(QStringLiteral("10.77.0.2"));
    std::vector<std::pair<Ed3kSession*, QByteArray>> wire;
    const auto hooks = [&](const QHostAddress& self, Ed3kSession& peer) {
        Ed3kLan::Hooks h;
        h.interfaces = [self] { return std::vector<Ed3kLan::Iface>{{7, QStringLiteral("fake0"), {{self, 16}}}}; };
        h.send = [&peer, self](std::uint32_t iface, const QHostAddress& to, std::uint16_t port, const QByteArray& data) {
            QCOMPARE(iface, 7u);
            QVERIFY(to == QHostAddress(QStringLiteral("239.255.101.51")) || to == QHostAddress(QStringLiteral("ff02::6533:6b")));
            QCOMPARE(port, Ed3kLan::kPort);
            // the group delivers on the next turn, from the sender's address
            QTimer::singleShot(0, &peer, [&peer, self, data] {
                if (peer.lan())
                    peer.lan()->datagramIn(7, self, Ed3kLan::kPort, data);
            });
        };
        return h;
    };
    Ed3kSettings sa = loopback(da.path()), sb = loopback(db.path());
    sa.lan = sb.lan = true;
    sa.lanHooks = hooks(ipA, b);
    sb.lanHooks = hooks(ipB, a);
    LanPeers seenByA(a), seenByB(b);
    QVERIFY(a.start(sa));
    QVERIFY(b.start(sb));
    QVERIFY(a.lan() && b.lan());
    QCOMPARE(a.lan()->interfacesUp(), std::vector<std::uint32_t>{7});
    QTRY_VERIFY_WITH_TIMEOUT(!seenByA.seen.empty() && !seenByB.seen.empty(), 30000);
    QVERIFY(seenByA.seen.front() == *b.nodeId());
    QVERIFY(seenByB.seen.front() == *a.nodeId());
    a.stop();
    b.stop();
}

void tst_Ed3kPlumbing::lanOffWithThePref()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    Ed3kSettings st = loopback(dir.path());
    st.lan = false;
    QVERIFY(s.start(st));
    QVERIFY(!s.lan());
    s.stop();
    // the extension off: no LAN either, whatever the pref
    st.lan = true;
    st.lanHooks.interfaces = [] { return std::vector<Ed3kLan::Iface>{}; };
    st.lanHooks.send = [](std::uint32_t, const QHostAddress&, std::uint16_t, const QByteArray&) {};
    st.extOff = ED3K_EXT_LAN;
    QVERIFY(s.start(st));
    QVERIFY(!s.lan());
    s.stop();
}

// bootstrap.md §3: a source from a link MUST be asked; with nobody to ask it is refused
void tst_Ed3kPlumbing::nodesLinkRefusedWithoutAGui()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kBootstrap boot(s);
    QSignalSpy added(&boot, &Ed3kBootstrap::sourceAdded);
    const QString link = nodesLink(QStringLiteral("https://nodes.example.org/nodes.ed3knod"), key(9));
    const auto ask = boot.openLink(link);
    QVERIFY(ask);
    QCOMPARE(boot.pendingAsks(), std::size_t(0));
    QVERIFY(!boot.answer(*ask, true));   // refused already
    QTest::qWait(50);
    QCOMPARE(added.count(), 0);
    // the asker says no GUI is there: refused too
    bool asked = false;
    boot.setAsker([&](std::uint64_t, const QString&, const QString&) {
        asked = true;
        return false;
    });
    QVERIFY(boot.openLink(link));
    QVERIFY(asked);
    QCOMPARE(boot.pendingAsks(), std::size_t(0));
    QVERIFY(!boot.openLink(QStringLiteral("ed3k:nodes?url=http%3A%2F%2Fa&key=x")));
    s.stop();
}

void tst_Ed3kPlumbing::nodesLinkAskedAndAnswered()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kBootstrap boot(s);
    QString host;
    std::uint64_t asked = 0;
    boot.setAsker([&](std::uint64_t ask, const QString& h, const QString&) {
        asked = ask;
        host = h;
        return true;
    });
    QSignalSpy added(&boot, &Ed3kBootstrap::sourceAdded);
    const QString url = QStringLiteral("https://nodes.example.org/nodes.ed3knod");
    const auto ask = boot.openLink(nodesLink(url, key(3)));
    QVERIFY(ask);
    QCOMPARE(asked, *ask);
    QCOMPARE(host, QStringLiteral("nodes.example.org"));
    QCOMPARE(boot.pendingAsks(), std::size_t(1));
    QVERIFY(boot.answer(*ask, true));
    QTRY_COMPARE_WITH_TIMEOUT(added.count(), 1, 5000);
    const auto src = parseNodesLink(added.first().first().toString());
    QVERIFY(src);
    QCOMPARE(src->url, url);
    QVERIFY(src->key == key(3));
    QVERIFY(boot.sources().contains(added.first().first().toString()));
    // a no adds nothing
    const auto second = boot.openLink(nodesLink(QStringLiteral("https://other.example.org/n"), key(4)));
    QVERIFY(second);
    QVERIFY(boot.answer(*second, false));
    QTest::qWait(50);
    QCOMPARE(added.count(), 1);
    s.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kPlumbing)
#include "tst_Ed3kPlumbing.moc"
