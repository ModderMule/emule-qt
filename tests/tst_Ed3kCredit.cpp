/// @file tst_Ed3kCredit.cpp
/// @brief ed3k credits in the daemon (Ed3kCredit, credit.md): the key file, the ledger
/// configured from the prefs through a fake JSON-RPC node, and the IPC (Ed3kCreditBridge).

#include "Ed3kBridge.h"
#include "Ed3kChain.h"
#include "Ed3kCredit.h"
#include "Ed3kCreditBridge.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace eMule;
using eMule::ed3k::Ed3kCredit;
using eMule::ed3k::Ed3kSession;
using eMule::ed3k::Ed3kSettings;
using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

/// A JSON-RPC node on loopback: eth_chainId answers @p chainId, eth_call a balance of
/// 1,000,000 µ, anything else an error. Counts the methods asked.
class FakeLedger : public QObject {
public:
    explicit FakeLedger(QString chainId)
        : m_chainId(std::move(chainId))
    {
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
        connect(&m_server, &QTcpServer::newConnection, this, [this] { onConnection(); });
    }
    [[nodiscard]] QString url() const { return QStringLiteral("http://127.0.0.1:%1/rpc").arg(m_server.serverPort()); }
    QStringList methods;

private:
    void onConnection()
    {
        QTcpSocket* c = m_server.nextPendingConnection();
        auto buf = std::make_shared<QByteArray>();
        connect(c, &QTcpSocket::readyRead, c, [this, c, buf] {
            *buf += c->readAll();
            const qsizetype end = buf->indexOf("\r\n\r\n");
            if (end < 0)
                return;
            const qsizetype lenAt = buf->indexOf("Content-Length: ");
            const qsizetype len = lenAt < 0 ? 0 : buf->mid(lenAt + 16, buf->indexOf("\r\n", lenAt) - lenAt - 16).toLongLong();
            if (buf->size() < end + 4 + len)
                return;
            const QJsonObject call = QJsonDocument::fromJson(buf->mid(end + 4, len)).object();
            const QString method = call.value(QStringLiteral("method")).toString();
            methods << method;
            QJsonObject answer{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), call.value(QStringLiteral("id"))}};
            if (method == QStringLiteral("eth_chainId"))
                answer.insert(QStringLiteral("result"), m_chainId);
            else if (method == QStringLiteral("eth_call"))
                answer.insert(QStringLiteral("result"), QStringLiteral("0x00000000000000000000000000000000000000000000000000000000000f4240"));
            else
                answer.insert(QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), -32601}, {QStringLiteral("message"), QStringLiteral("no")}});
            const QByteArray body = QJsonDocument(answer).toJson(QJsonDocument::Compact);
            c->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size())
                     + "\r\n\r\n" + body);
            c->disconnectFromHost();
        });
    }

    QTcpServer m_server;
    QString m_chainId;
};

} // namespace

class tst_Ed3kCredit : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void offByDefaultMakesNoKey();
    void keyFileOwnerOnlyAndStable();
    void importExportRoundTrip();
    void keyOverIpc();
    void configureReachesTheLedger();
    void wrongChainIsNotUsed();
    void addressParsing();
    void oneRemoteChainEndpointIsNoted();
    void aRefusedChainAnswerIsShown();
    void autoBidStartsAtThresholdAndRunsDown();
    void autoBidStopsBeforeOverdraw();
    void autoBidNeverWithoutNodeOrRead();
    void autoBidPrefsAreHeld();

private:
    static Ed3kSettings loopback(const QString& dir);
    /// Enabled on the test chain of credit.md §7.1 (31337, 0x5fbd…0aa3).
    static Ed3kCreditPrefs testChain();
    [[nodiscard]] static QString keyPath(const QString& dir);
};

void tst_Ed3kCredit::cleanup()
{
    Ed3kCreditBridge::setCredit(nullptr);
    Ed3kBridge::setSession(nullptr);
}

void tst_Ed3kCredit::offByDefaultMakesNoKey()
{
    // §9: off by default; §7.1: no default chain or contract
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kCredit c(s, dir.path());
    c.setConfig(Ed3kCreditPrefs{}, 0);
    QVERIFY(!c.status().configured);
    QVERIFY(!QFile::exists(keyPath(dir.path())));
    // enabled without the network's parameters: still nothing
    Ed3kCreditPrefs p;
    p.enabled = true;
    c.setConfig(p, 0);
    QVERIFY(!c.status().configured);
    QVERIFY(!c.status().problem.isEmpty());
    QVERIFY(!QFile::exists(keyPath(dir.path())));
    s.stop();
}

void tst_Ed3kCredit::keyFileOwnerOnlyAndStable()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString address;
    QByteArray first;
    {
        Ed3kSession s;
        QVERIFY(s.start(loopback(dir.path())));
        Ed3kCredit c(s, dir.path());
        c.setConfig(testChain(), 0);
        QVERIFY2(c.status().configured, qPrintable(c.status().problem));
        address = c.status().address;
        QCOMPARE(address.size(), 42);
        QVERIFY(address.startsWith(QLatin1StringView("0x")));
        QFile key(keyPath(dir.path()));
        QVERIFY(key.open(QIODevice::ReadOnly));
        first = key.readAll();
        QCOMPARE(first.size(), 32);
#ifdef Q_OS_UNIX
        const auto perms = key.permissions();
        QVERIFY(perms.testFlag(QFileDevice::ReadOwner));
        QVERIFY(!(perms & (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther
                           | QFileDevice::WriteOther | QFileDevice::ExeOther)));
#endif
        s.stop();
        QVERIFY(!c.status().configured);
    }
    // a restart: the same key, the same address, configured again at start
    Ed3kSession s;
    Ed3kCredit c(s, dir.path());
    c.setConfig(testChain(), 0);
    QCOMPARE(c.status().address, address);
    QVERIFY(!c.status().configured);
    QVERIFY(s.start(loopback(dir.path())));
    QVERIFY(c.status().configured);
    QFile key(keyPath(dir.path()));
    QVERIFY(key.open(QIODevice::ReadOnly));
    QCOMPARE(key.readAll(), first);
    // switched off: CREDIT no longer advertised, the key stays
    Ed3kCreditPrefs off = testChain();
    off.enabled = false;
    c.setConfig(off, 0);
    QVERIFY(!c.status().configured);
    QVERIFY(QFile::exists(keyPath(dir.path())));
    s.stop();
}

void tst_Ed3kCredit::importExportRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    Ed3kCredit c(s, dir.path());
    QVERIFY(c.exportKey().isEmpty());
    // key 1: the well-known Ethereum address of private key 1
    QByteArray one(32, '\0');
    one[31] = 1;
    const auto addr = c.importKey(one);
    QVERIFY(addr);
    QCOMPARE(*addr, QStringLiteral("0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"));
    QCOMPARE(c.exportKey(), one);
    QCOMPARE(c.status().address, *addr);
    // not a key: 0, the wrong size; the file is kept
    QString err;
    QVERIFY(!c.importKey(QByteArray(32, '\0'), &err));
    QVERIFY(!err.isEmpty());
    QVERIFY(!c.importKey(QByteArray(31, '\1')));
    QCOMPARE(c.exportKey(), one);
    // a new key: another address, the file replaced, owner-only
    const auto fresh = c.newKey();
    QVERIFY(fresh && *fresh != *addr);
    QVERIFY(c.exportKey() != one);
#ifdef Q_OS_UNIX
    const auto perms = QFile(keyPath(dir.path())).permissions();
    QVERIFY(!(perms & (QFileDevice::ReadGroup | QFileDevice::ReadOther)));
#endif
    QVERIFY(!QFile::exists(keyPath(dir.path()) + QStringLiteral(".new")));
}

void tst_Ed3kCredit::keyOverIpc()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    Ed3kCredit c(s, dir.path());
    Ed3kCreditBridge::setCredit(&c);
    QVERIFY(Ed3kCreditBridge::carriesKey(IpcMsgType::ExportEd3kCreditKey));
    QVERIFY(!Ed3kCreditBridge::carriesKey(IpcMsgType::GetEd3kStatus));
    IpcMessage last;
    const auto reply = [&](const IpcMessage& r) { last = r; };

    IpcMessage imp(IpcMsgType::ImportEd3kCreditKey, 1);
    imp.append(QStringLiteral("0x0000000000000000000000000000000000000000000000000000000000000001"));
    QVERIFY(Ed3kBridge::handleRequest(imp, reply));
    QVERIFY(last.fieldBool(0));
    QCOMPARE(last.fieldMap(1).value(QStringLiteral("address")).toString(), QStringLiteral("0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"));

    QVERIFY(Ed3kBridge::handleRequest(IpcMessage(IpcMsgType::ExportEd3kCreditKey, 2), reply));
    QVERIFY(last.fieldBool(0));
    QCOMPARE(last.fieldMap(1).value(QStringLiteral("key")).toString(),
             QStringLiteral("0000000000000000000000000000000000000000000000000000000000000001"));

    IpcMessage bad(IpcMsgType::ImportEd3kCreditKey, 3);
    bad.append(QStringLiteral("zz"));
    QVERIFY(Ed3kBridge::handleRequest(bad, reply));
    QVERIFY(!last.fieldBool(0));

    QVERIFY(Ed3kBridge::handleRequest(IpcMessage(IpcMsgType::NewEd3kCreditKey, 4), reply));
    QVERIFY(last.fieldBool(0));
    QVERIFY(last.fieldMap(1).value(QStringLiteral("address")).toString() != QStringLiteral("0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"));
}

void tst_Ed3kCredit::configureReachesTheLedger()
{
    // creditConfigure with one endpoint: the engine asks the chain, then the balance (§7.3)
    FakeLedger ledger(QStringLiteral("0x7a69"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSettings settings = loopback(dir.path());
    settings.creditRpc = {ledger.url()};
    Ed3kSession s;
    QVERIFY(s.start(settings));
    Ed3kCredit c(s, dir.path());
    Ed3kBridge::setSession(&s);
    Ed3kCreditBridge::setCredit(&c);
    Ed3kCreditPrefs p = testChain();
    p.sell = true;   // selling needs the balance read
    p.sellFloor = 1'000'000;
    p.sellSlots = 3;
    c.setConfig(p, 1);
    QVERIFY2(c.status().configured, qPrintable(c.status().problem));
    QCOMPARE(c.status().endpoints, 1);
    QTRY_VERIFY_WITH_TIMEOUT(ledger.methods.contains(QStringLiteral("eth_chainId")), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(c.status().balance.has_value(), 10000);
    QCOMPARE(*c.status().balance, std::uint64_t(1'000'000));
    // GetEd3kStatus carries it
    const QCborMap credit = Ed3kBridge::statusToCbor().value(QStringLiteral("credit")).toMap();
    QVERIFY(credit.value(QStringLiteral("configured")).toBool());
    QCOMPARE(credit.value(QStringLiteral("balance")).toInteger(), qint64(1'000'000));
    QCOMPARE(credit.value(QStringLiteral("address")).toString(), c.status().address);
    QVERIFY(Ed3kBridge::statusToCbor().contains(QStringLiteral("queue")));

    // the same ledger again: no new configure (it would drop the receipts held)
    const qsizetype asked = ledger.methods.count(QStringLiteral("eth_chainId"));
    c.setConfig(p, 1);
    QTest::qWait(300);
    QCOMPARE(ledger.methods.count(QStringLiteral("eth_chainId")), asked);
    s.stop();
}

void tst_Ed3kCredit::wrongChainIsNotUsed()
{
    // an endpoint on another chain (§7.1): reported, not used again
    FakeLedger ledger(QStringLiteral("0x1"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSettings settings = loopback(dir.path());
    settings.creditRpc = {ledger.url()};
    Ed3kSession s;
    QVERIFY(s.start(settings));
    Ed3kCredit c(s, dir.path());
    Ed3kCreditPrefs p = testChain();
    p.sell = true;
    c.setConfig(p, 1);
    QTRY_VERIFY_WITH_TIMEOUT(!c.status().wrongChain.empty(), 10000);
    QVERIFY(!c.status().balance);
    s.stop();
}

void tst_Ed3kCredit::addressParsing()
{
    const auto a = Ed3kCredit::parseAddress(QStringLiteral("0x5FbDB2315678afecb367f032d93F642f64180aa3"));
    QVERIFY(a);
    QCOMPARE(Ed3kCredit::addressText(*a), QStringLiteral("0x5fbdb2315678afecb367f032d93f642f64180aa3"));
    QVERIFY(!Ed3kCredit::parseAddress(QStringLiteral("5fbdb2315678afecb367f032d93f642f64180aa3")));
    QVERIFY(!Ed3kCredit::parseAddress(QStringLiteral("0x5fbdb2315678afecb367f032d93f642f64180aa")));
    QVERIFY(!Ed3kCredit::parseAddress(QStringLiteral("0xzzbdb2315678afecb367f032d93f642f64180aa3")));
}

// chaintube.md §4.3, batch 12 #8: one remote endpoint can't give two providers (noted once)
void tst_Ed3kCredit::oneRemoteChainEndpointIsNoted()
{
    using eMule::ed3k::Ed3kChain;
    const QString remote = QStringLiteral("https://rpc.example.org/ext/bc/C/rpc");
    QVERIFY(Ed3kChain::singleRemote({remote}));
    QCOMPARE(Ed3kChain::providersFor({remote}), 1u);
    QVERIFY(!Ed3kChain::singleRemote({remote, QStringLiteral("https://other.example.net/rpc")}));
    QCOMPARE(Ed3kChain::providersFor({remote, QStringLiteral("https://other.example.net/rpc")}), 2u);
    QVERIFY(!Ed3kChain::singleRemote({QStringLiteral("http://127.0.0.1:8545")}));   // a local node: enough
    QVERIFY(!Ed3kChain::singleRemote({}));
    // an unusable one does not count (plain http to elsewhere)
    QVERIFY(Ed3kChain::singleRemote({remote, QStringLiteral("http://plain.example.org/rpc")}));
    // the status the options page reads (GetEd3kStatus "chain")
    Ed3kSession s;
    Ed3kChain chain(s);
    Ed3kCreditBridge::setChain(&chain);
    chain.setConfig(Ed3kChainPrefs{}, {remote});
    const QCborMap st = Ed3kCreditBridge::chainStatusToCbor();
    QVERIFY(st.value(QStringLiteral("singleRemote")).toBool());
    QCOMPARE(st.value(QStringLiteral("refused")).toString(), QString());
    Ed3kCreditBridge::setChain(nullptr);
    QVERIFY(Ed3kCreditBridge::chainStatusToCbor().isEmpty());
}

// batch 12 #6: a refused read/write is shown with the node's text; other failures are not refusals
void tst_Ed3kCredit::aRefusedChainAnswerIsShown()
{
    using eMule::ed3k::Ed3kChain;
    QVERIFY(Ed3kChain::refusalText(ED3K_CHAIN_REFUSED, QStringLiteral("insufficient funds for gas * price + value")).contains(
        QStringLiteral("insufficient funds")));
    QVERIFY(!Ed3kChain::refusalText(ED3K_CHAIN_REFUSED, {}).isEmpty());
    QVERIFY(!Ed3kChain::refusalText(ED3K_CHAIN_BAD_REQUEST, {}).isEmpty());
    QVERIFY(Ed3kChain::refusalText(ED3K_CHAIN_UNREACHABLE, QStringLiteral("x")).isEmpty());
    // a stranger's text: one line, kept short
    const QString t = Ed3kChain::refusalText(ED3K_CHAIN_REFUSED, QString(500, QLatin1Char('a')) + QStringLiteral("\nb"));
    QVERIFY(!t.contains(QLatin1Char('\n')));
    QVERIFY(t.size() < 300);
}

Ed3kSettings tst_Ed3kCredit::loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.udpPort = 0;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");
    s.forceOpen = true;
    return s;
}

Ed3kCreditPrefs tst_Ed3kCredit::testChain()
{
    Ed3kCreditPrefs p;
    p.enabled = true;
    p.chainId = 31337;
    p.contract = QStringLiteral("0x5fbdb2315678afecb367f032d93f642f64180aa3");
    p.quorum = 1;
    return p;
}

QString tst_Ed3kCredit::keyPath(const QString& dir)
{
    return QDir(eMule::ed3k::stateDir(dir)).filePath(Ed3kCredit::keyFileName());
}

// autostart highspeed download: from the threshold on, down to ~20 credits (hysteresis)
void tst_Ed3kCredit::autoBidStartsAtThresholdAndRunsDown()
{
    using AutoBid = Ed3kCredit::AutoBid;
    constexpr std::uint64_t M = AutoBid::kMicro;
    Ed3kCreditPrefs prefs;
    prefs.autoBid = true;   // factor 5.0, threshold 2000
    prefs.bidMaxPrice = 1 * M;
    AutoBid a;
    a.ledgerBalance(1999 * M);
    QVERIFY(!a.update(prefs, true, false, 0));
    QCOMPARE(a.price(prefs), 1 * M);
    a.ledgerBalance(2000 * M);
    QVERIFY(a.update(prefs, true, false, 0));
    QVERIFY(a.active);
    QCOMPARE(a.price(prefs), 5 * M);
    // below the threshold now, far from the stop: keeps running
    const ::ed3k::event::CreditAddress holder{1};
    a.signedReceipt(holder, 100, 10 * M, 1'000);
    a.ledgerBalance(1500 * M);
    QVERIFY(!a.update(prefs, true, false, 60'000));
    QVERIFY(a.active);
    // the open payments reach the 80 % of the read balance: stops, back to the normal bid
    a.signedReceipt(holder, 100, 1180 * M, 120'000);
    QVERIFY(a.update(prefs, true, false, 120'000));
    QVERIFY(!a.active);
    QCOMPARE(a.price(prefs), 1 * M);
    // no restart until the estimate is at the threshold again
    a.ledgerBalance(2100 * M);
    QVERIFY(!a.update(prefs, true, false, 200'000));
    a.ledgerBalance(3200 * M);
    QVERIFY(a.update(prefs, true, false, 200'000));
    // two days on, the old receipts are no longer open
    a.signedReceipt(holder, 102, 1 * M, 300'000);
    QCOMPARE(a.open(), 1 * M);
}

// the spend of the past seconds stops it while there is still room (never overdrawn, §6.4)
void tst_Ed3kCredit::autoBidStopsBeforeOverdraw()
{
    using AutoBid = Ed3kCredit::AutoBid;
    constexpr std::uint64_t M = AutoBid::kMicro;
    Ed3kCreditPrefs prefs;
    prefs.autoBid = true;
    prefs.autoThreshold = 200;
    AutoBid a;
    a.ledgerBalance(10'000 * M);
    QVERIFY(a.update(prefs, true, false, 0));
    // 300 credits in the past 10 s: 30/s, so 300 more coming plus a tranche (20)
    const ::ed3k::event::CreditAddress h1{1}, h2{2};
    a.signedReceipt(h1, 100, 150 * M, 1'000);
    a.signedReceipt(h2, 100, 150 * M, 2'000);
    QCOMPARE(a.rate(2'000), 30 * M);
    QVERIFY(!a.update(prefs, true, false, 2'000));
    // a balance read that lags: what is open still counts
    a.ledgerBalance(700 * M);
    // 700 - 300 = 400 left, 320 coming: would run on; but 620 open + coming is past 80 % of 700
    QVERIFY(a.update(prefs, true, false, 2'000));
    QVERIFY(!a.active);
    // the same cumulative value again adds nothing
    a.signedReceipt(h1, 100, 150 * M, 3'000);
    QCOMPARE(a.open(), 300 * M);
}

// ed3k off, credits not in use, no ledger read or overdrawn: never bids
void tst_Ed3kCredit::autoBidNeverWithoutNodeOrRead()
{
    using AutoBid = Ed3kCredit::AutoBid;
    constexpr std::uint64_t M = AutoBid::kMicro;
    Ed3kCreditPrefs prefs;
    prefs.autoBid = true;
    AutoBid a;
    QVERIFY(!a.update(prefs, true, false, 0));   // no read
    a.ledgerBalance(100'000 * M);
    QVERIFY(!a.update(prefs, false, false, 0));   // ed3k off
    QVERIFY(!a.update(prefs, true, true, 0));     // overdrawn
    prefs.autoBid = false;
    QVERIFY(!a.update(prefs, true, false, 0));
    prefs.autoBid = true;
    QVERIFY(a.update(prefs, true, false, 0));
    QVERIFY(a.update(prefs, false, false, 0));   // node stopped
    QCOMPARE(a.price(prefs), prefs.bidMaxPrice);
}

void tst_Ed3kCredit::autoBidPrefsAreHeld()
{
    Ed3kCreditPrefs c;
    QCOMPARE(c.autoFactor, 50u);
    QCOMPARE(c.autoThreshold, std::uint64_t(2000));
    QVERIFY(!c.autoBid);
    const auto factor = [&c](double v) {
        Preferences::applyEd3kCreditKey(c, QStringLiteral("ed3kCreditAutoFactor"), QCborValue(v));
        return c.autoFactor;
    };
    QCOMPARE(factor(1.0), 20u);
    QCOMPARE(factor(80.0), 500u);
    QCOMPARE(factor(5.04), 50u);
    QCOMPARE(factor(12.3), 123u);
    Preferences::applyEd3kCreditKey(c, QStringLiteral("ed3kCreditAutoThreshold"), QCborValue(qint64(10)));
    QCOMPARE(c.autoThreshold, std::uint64_t(200));
    Preferences::applyEd3kCreditKey(c, QStringLiteral("ed3kCreditAutoThreshold"), QCborValue(qint64(5000)));
    QCOMPARE(c.autoThreshold, std::uint64_t(5000));
    QCOMPARE(c.autoPrice(), std::uint64_t(12'300'000));
}

QTEST_GUILESS_MAIN(tst_Ed3kCredit)
#include "tst_Ed3kCredit.moc"
