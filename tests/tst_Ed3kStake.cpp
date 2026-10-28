/// @file tst_Ed3kStake.cpp
/// @brief ed3k stake tokens (credit.md §6.7) in the daemon: a burn through a fake JSON-RPC
/// ledger answered on the session's events (no endpoint URL: the RpcRequest is ours), the
/// claim recorded, kept in stake-claims.json, loaded again and read again, claimed; the IPC.

#include "Ed3kBridge.h"
#include "Ed3kCredit.h"
#include "Ed3kCreditBridge.h"
#include "Ed3kSession.h"
#include "Ed3kStore.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

const QString kLedger = QStringLiteral("0x5fbdb2315678afecb367f032d93f642f64180aa3");
const QString kTo = QStringLiteral("0x00000000000000000000000000000000000000b2");

QByteArray keccak(const QByteArray& b)
{
    return QCryptographicHash::hash(b, QCryptographicHash::Keccak_256);
}

QString hex0x(const QByteArray& b)
{
    return QStringLiteral("0x") + QString::fromLatin1(b.toHex());
}

/// A 32-byte ABI word of a u64.
QByteArray word(quint64 v)
{
    QByteArray w(32, '\0');
    for (int i = 0; i < 8; ++i)
        w[31 - i] = char((v >> (8 * i)) & 0xff);
    return w;
}

/// An address as a 32-byte word / topic.
QByteArray addressWord(const QString& addr)
{
    return QByteArray(12, '\0') + QByteArray::fromHex(addr.mid(2).toLatin1());
}

/// The ledger of credit.md §6.7 as far as a burn and a claim need it, answering the node's
/// RpcRequests in eventPolled (ledger.rs's test node does the same in Rust).
class FakeLedger : public QObject {
public:
    explicit FakeLedger(Ed3kSession& s)
        : m_s(s)
    {
        connect(&m_s, &Ed3kSession::eventPolled, this, [this](const ::ed3k::Event& ev, bool* handled) {
            if (const auto* r = std::get_if<::ed3k::event::RpcRequest>(&ev)) {
                *handled = true;
                answer(*r);
            }
        }, Qt::DirectConnection);
    }
    QString from;               ///< the node's credit address (log topic)
    quint64 claimReadyAt = 0;   ///< readyAt of the burn's claim
    bool claimPaid = false;     ///< stakeClaims(1) reads paid (to = 0)
    QStringList methods;
    int burns = 0;
    int claims = 0;

private:
    void answer(const ::ed3k::event::RpcRequest& r)
    {
        const QJsonObject call = QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(r.body.data()),
                                                                    qsizetype(r.body.size()))).object();
        const QString method = call.value(QStringLiteral("method")).toString();
        const QJsonArray params = call.value(QStringLiteral("params")).toArray();
        methods << method;
        QJsonValue result;
        if (method == QStringLiteral("eth_chainId")) {
            result = QStringLiteral("0x7a69");
        } else if (method == QStringLiteral("eth_getBalance")) {
            result = QStringLiteral("0xde0b6b3a7640000");
        } else if (method == QStringLiteral("eth_call")) {
            const QByteArray data = QByteArray::fromHex(params.at(0).toObject().value(QStringLiteral("data")).toString().mid(2).toLatin1());
            if (data.startsWith(keccak("stakeClaims(uint256)").left(4))) {
                // (address to, uint64 readyAt, uint256 amount); paid: to = 0
                const QByteArray to = claimPaid ? QByteArray(32, '\0') : addressWord(kTo);
                result = hex0x(to + word(claimReadyAt) + word(2'000'000));
            } else if (data.startsWith(keccak("stakeRate()").left(4))) {
                result = hex0x(word(1'000'000'000'000'000'000ULL));   // 1 E3S per credit
            } else {
                // any other view (balance, account, delay, token, …): zeros, long enough for
                // a struct (a short or ill-typed answer marks the endpoint failed)
                result = hex0x(QByteArray(16 * 32, '\0'));
            }
        } else if (method == QStringLiteral("eth_getTransactionCount")) {
            result = QStringLiteral("0x0");
        } else if (method == QStringLiteral("eth_estimateGas")) {
            result = QStringLiteral("0x13880");
        } else if (method == QStringLiteral("eth_sendRawTransaction")) {
            const QByteArray raw = QByteArray::fromHex(params.at(0).toString().mid(2).toLatin1());
            const QByteArray hash = keccak(raw);
            m_kinds[hash] = raw.contains(keccak("burnForStake(uint256,address)").left(4)) ? 1 : 2;
            result = hex0x(hash);
        } else if (method == QStringLiteral("eth_getTransactionReceipt")) {
            const QByteArray hash = QByteArray::fromHex(params.at(0).toString().mid(2).toLatin1());
            result = receipt(hash);
        } else {
            (void)m_s.engine()->rpcFailed(m_s.now(), r.id);
            return;
        }
        const QJsonObject answer{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")},
                                 {QStringLiteral("id"), call.value(QStringLiteral("id"))},
                                 {QStringLiteral("result"), result}};
        const QByteArray body = QJsonDocument(answer).toJson(QJsonDocument::Compact);
        (void)m_s.engine()->rpcAnswer(m_s.now(), r.id, std::as_bytes(std::span{body.constData(), std::size_t(body.size())}));
    }

    QJsonValue receipt(const QByteArray& hash)
    {
        const int kind = m_kinds.value(hash);
        if (kind == 0)
            return QJsonValue::Null;
        QJsonObject log{{QStringLiteral("address"), kLedger}};
        if (kind == 1) {
            ++burns;
            // StakeBurned(uint256 indexed id, address indexed from, address indexed to, credits, amount, readyAt)
            log.insert(QStringLiteral("topics"), QJsonArray{hex0x(keccak("StakeBurned(uint256,address,address,uint256,uint256,uint64)")),
                                                            hex0x(word(1)), hex0x(addressWord(from)), hex0x(addressWord(kTo))});
            log.insert(QStringLiteral("data"), hex0x(word(2'000'000) + word(2'000'000) + word(claimReadyAt)));
        } else {
            ++claims;
            // StakeClaimed(uint256 indexed id, address indexed to, uint256 amount)
            log.insert(QStringLiteral("topics"), QJsonArray{hex0x(keccak("StakeClaimed(uint256,address,uint256)")), hex0x(word(1)),
                                                            hex0x(addressWord(kTo))});
            log.insert(QStringLiteral("data"), hex0x(word(2'000'000)));
        }
        return QJsonObject{{QStringLiteral("transactionHash"), hex0x(hash)},
                           {QStringLiteral("status"), QStringLiteral("0x1")},
                           {QStringLiteral("blockNumber"), QStringLiteral("0x2a")},
                           {QStringLiteral("gasUsed"), QStringLiteral("0x1388")},
                           {QStringLiteral("logs"), QJsonArray{log}}};
    }

    Ed3kSession& m_s;
    QHash<QByteArray, int> m_kinds;   ///< tx hash → 1 burn, 2 claim
};

} // namespace

class tst_Ed3kStake : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void burnRecordsAClaimInTheFile();
    void claimsReloadAndAreReadAgain();
    void claimPaysTheClaim();
    void ipcRefusesBadInputAndListsClaims();

private:
    static Ed3kSettings loopback(const QString& dir);
    static Ed3kCreditPrefs testChain();
    /// Starts s with credits on one (faked) endpoint; the ledger answers.
    static void configure(Ed3kSession& s, Ed3kCredit& c, FakeLedger& l, const QString& dir);
    /// A burn of 2 credits to kTo, mined: the claim is there.
    static void burnOnce(Ed3kCredit& c, FakeLedger& l);
    [[nodiscard]] static QString claimsPath(const QString& dir);
};

void tst_Ed3kStake::cleanup()
{
    Ed3kCreditBridge::setCredit(nullptr);
    Ed3kBridge::setSession(nullptr);
}

void tst_Ed3kStake::burnRecordsAClaimInTheFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    FakeLedger l(s);
    Ed3kCredit c(s, dir.path());
    configure(s, c, l, dir.path());
    // not before credits are configured: nothing to burn with
    burnOnce(c, l);
    const auto claims = c.claims();
    QCOMPARE(claims.size(), std::size_t(1));
    QCOMPARE(claims.front().id, std::uint64_t(1));
    QCOMPARE(Ed3kCredit::addressText(claims.front().to), kTo);
    QCOMPARE(claims.front().readyAt, l.claimReadyAt);
    QVERIFY(claims.front().state == Ed3kCredit::ClaimState::Waiting);
    QVERIFY(l.methods.contains(QStringLiteral("eth_sendRawTransaction")));

    // kept: id, to, amount, readyAt, state
    QFile f(claimsPath(dir.path()));
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QJsonArray a = QJsonDocument::fromJson(f.readAll()).array();
    QCOMPARE(a.size(), 1);
    const QJsonObject o = a.at(0).toObject();
    QCOMPARE(o.value(QStringLiteral("id")).toString(), QStringLiteral("1"));
    QCOMPARE(o.value(QStringLiteral("to")).toString(), kTo);
    QCOMPARE(o.value(QStringLiteral("readyAt")).toString(), QString::number(l.claimReadyAt));
    QCOMPARE(o.value(QStringLiteral("amount")).toString().size(), 64);
    QCOMPARE(o.value(QStringLiteral("state")).toInt(), 0);
    s.stop();
}

void tst_Ed3kStake::claimsReloadAndAreReadAgain()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    {
        Ed3kSession s;
        FakeLedger l(s);
        Ed3kCredit c(s, dir.path());
        configure(s, c, l, dir.path());
        burnOnce(c, l);
        s.stop();
    }
    // a restart: the claim is loaded before the node runs
    Ed3kSession s;
    FakeLedger l(s);
    Ed3kCredit c(s, dir.path());
    QCOMPARE(c.claims().size(), std::size_t(1));
    QVERIFY(c.claims().front().state == Ed3kCredit::ClaimState::Waiting);
    QCOMPARE(Ed3kCredit::addressText(c.claims().front().to), kTo);
    // configured again: stakeClaims(1) is read; meanwhile someone claimed it (to = 0)
    l.claimPaid = true;
    configure(s, c, l, dir.path());
    QTRY_VERIFY_WITH_TIMEOUT(c.claims().front().state == Ed3kCredit::ClaimState::Paid, 15000);
    s.stop();
    // paid stays paid in the file
    Ed3kSession s2;
    Ed3kCredit c2(s2, dir.path());
    QVERIFY(c2.claims().front().state == Ed3kCredit::ClaimState::Paid);
}

void tst_Ed3kStake::claimPaysTheClaim()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    FakeLedger l(s);
    Ed3kCredit c(s, dir.path());
    configure(s, c, l, dir.path());
    burnOnce(c, l);
    QVERIFY(!c.claimStake(99));   // not ours
    QVERIFY2(c.claimStake(1), "claimStake refused");
    QVERIFY(c.claims().front().state == Ed3kCredit::ClaimState::Claiming);
    QTRY_VERIFY_WITH_TIMEOUT(c.claims().front().state == Ed3kCredit::ClaimState::Paid, 20000);
    QCOMPARE(l.claims, 1);
    QVERIFY(!c.claimStake(1));   // paid once
    s.stop();
}

void tst_Ed3kStake::ipcRefusesBadInputAndListsClaims()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    FakeLedger l(s);
    Ed3kCredit c(s, dir.path());
    Ed3kCreditBridge::setCredit(&c);
    IpcMessage last;
    const auto reply = [&](const IpcMessage& r) { last = r; };

    // credits not in use: a burn is refused (State)
    IpcMessage burn(IpcMsgType::BurnEd3kStake, 1);
    burn.append(qint64(1'000'000));
    burn.append(kTo);
    QVERIFY(Ed3kBridge::handleRequest(burn, reply));
    QVERIFY(!last.fieldBool(0));
    // no address
    IpcMessage bad(IpcMsgType::BurnEd3kStake, 2);
    bad.append(qint64(1'000'000));
    bad.append(QStringLiteral("0x12"));
    QVERIFY(Ed3kBridge::handleRequest(bad, reply));
    QVERIFY(!last.fieldBool(0));

    configure(s, c, l, dir.path());
    QVERIFY(Ed3kBridge::handleRequest(burn, reply));
    QVERIFY(last.fieldBool(0));
    QTRY_VERIFY_WITH_TIMEOUT(c.claims().size() == 1, 20000);
    QVERIFY(Ed3kBridge::handleRequest(IpcMessage(IpcMsgType::GetEd3kStakeClaims, 3), reply));
    QVERIFY(last.fieldBool(0));
    const QCborArray claims = last.fieldMap(1).value(QStringLiteral("claims")).toArray();
    QCOMPARE(claims.size(), 1);
    QCOMPARE(claims.at(0).toMap().value(QStringLiteral("to")).toString(), kTo);
    QCOMPARE(claims.at(0).toMap().value(QStringLiteral("state")).toInteger(), 0);
    QCOMPARE(last.fieldMap(1).value(QStringLiteral("address")).toString(), c.status().address);
    s.stop();
}

Ed3kSettings tst_Ed3kStake::loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.udpPort = 0;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");
    s.forceOpen = true;
    return s;
}

Ed3kCreditPrefs tst_Ed3kStake::testChain()
{
    Ed3kCreditPrefs p;
    p.enabled = true;
    p.chainId = 31337;
    p.contract = kLedger;
    p.quorum = 1;
    return p;
}

void tst_Ed3kStake::configure(Ed3kSession& s, Ed3kCredit& c, FakeLedger& l, const QString& dir)
{
    if (!s.isRunning())
        QVERIFY(s.start(loopback(dir)));
    // one endpoint class, no URL: the session hands the RpcRequests to the slots (FakeLedger)
    c.setConfig(testChain(), 1);
    QVERIFY2(c.status().configured, qPrintable(c.status().problem));
    l.from = c.status().address;
    QTRY_VERIFY_WITH_TIMEOUT(l.methods.contains(QStringLiteral("eth_chainId")), 10000);
}

void tst_Ed3kStake::burnOnce(Ed3kCredit& c, FakeLedger& l)
{
    l.claimReadyAt = quint64(QDateTime::currentSecsSinceEpoch()) - 60;   // ready already
    // wait for the stake rate (a burn below one unit or with burning off is refused)
    QTRY_VERIFY_WITH_TIMEOUT(c.status().stakeRate.has_value(), 10000);
    const auto to = Ed3kCredit::parseAddress(kTo);
    QVERIFY(to);
    QVERIFY2(c.burnForStake(2'000'000, *to), "burnForStake refused");
    QTRY_VERIFY2_WITH_TIMEOUT(c.claims().size() == 1, qPrintable(l.methods.join(QLatin1Char(' '))), 20000);
    QCOMPARE(l.burns, 1);
}

QString tst_Ed3kStake::claimsPath(const QString& dir)
{
    return QDir(eMule::ed3k::stateDir(dir)).filePath(Ed3kCredit::claimsFileName());
}

QTEST_GUILESS_MAIN(tst_Ed3kStake)
#include "tst_Ed3kStake.moc"
