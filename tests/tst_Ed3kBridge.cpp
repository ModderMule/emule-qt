/// @file tst_Ed3kBridge.cpp
/// @brief The daemon side of the ed3k node: GetEd3kStatus (Ed3kBridge) and its bandwidth
/// (BandwidthCoordinator: limits handed to the node, its bytes counted).

#include "BandwidthCoordinator.h"
#include "Ed3kBridge.h"
#include "Ed3kCredit.h"
#include "Ed3kCreditBridge.h"
#include "Ed3kDownloads.h"
#include "Ed3kDownloadsBridge.h"
#include "Ed3kHosterBridge.h"
#include "Ed3kHosterJoin.h"
#include "Ed3kPaid.h"
#include "Ed3kPaidBridge.h"
#include "Ed3kSession.h"
#include "Ed3kSets.h"
#include "Ed3kSetsBridge.h"
#include "Ed3kStore.h"

#include "prefs/Preferences.h"

#include <QDeadlineTimer>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;
using eMule::ed3k::Ed3kSession;
using eMule::ed3k::Ed3kSettings;
using Ipc::IpcMessage;
using Ipc::IpcMsgType;

class tst_Ed3kBridge : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void statusWithoutNode();
    void offWritesNoEd3kState();
    void statusOfARunningNode();
    void onlyTheEd3kBlockIsAnswered();
    void coordinatorLimitsAndCountsTheNode();
    void coordinatorSizesEd3kSlotsFromTheShare();

private:
    static Ed3kSettings loopback(const QString& dir);
    /// A bootstraps from B. true: A has B as a contact.
    static bool connectPair(Ed3kSession& a, Ed3kSession& b);
};

void tst_Ed3kBridge::cleanup()
{
    Ed3kBridge::setSession(nullptr);
}

void tst_Ed3kBridge::statusWithoutNode()
{
    const QCborMap m = Ed3kBridge::statusToCbor();
    QCOMPARE(m.value(QStringLiteral("available")).toBool(), true);
    QCOMPARE(m.value(QStringLiteral("running")).toBool(), false);

    // a session that is not running: running false, no stats
    Ed3kSession s;
    Ed3kBridge::setSession(&s);
    const QCborMap off = Ed3kBridge::statusToCbor();
    QCOMPARE(off.value(QStringLiteral("running")).toBool(), false);
    QCOMPARE(off.value(QStringLiteral("udpPort")).toInteger(), 0);
    QVERIFY(!off.contains(QStringLiteral("stats")));
}

void tst_Ed3kBridge::offWritesNoEd3kState()
{
    // #88: ed3k switched off, the requests that would write state are refused and
    // config/ed3k is never created; reads still answer
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString cfg = tmp.filePath(QStringLiteral("cfg"));
    const QString ed3kDir = eMule::ed3k::stateDir(cfg);
    const bool wasOn = thePrefs.ed3kEnabled();
    thePrefs.setEd3kEnabled(false);

    Ed3kSession s;   // never started, as in the daemon while off
    eMule::ed3k::Ed3kDownloads downloads(s, cfg);
    eMule::ed3k::Ed3kCredit credit(s, cfg);
    eMule::ed3k::Ed3kPaid paid(s, cfg);
    eMule::ed3k::Ed3kSets sets(s, downloads, cfg);
    eMule::ed3k::Ed3kHosterJoin hoster(s, cfg);
    Ed3kBridge::setSession(&s);
    Ed3kDownloadsBridge::setDownloads(&downloads);
    Ed3kCreditBridge::setCredit(&credit);
    Ed3kPaidBridge::setPaid(&paid);
    Ed3kSetsBridge::setSets(&sets, nullptr, nullptr);
    Ed3kHosterBridge::setHoster(&hoster, nullptr, nullptr);

    const auto ask = [](IpcMsgType type, const QList<QCborValue>& fields) {
        IpcMessage msg(type, 7);
        for (const QCborValue& f : fields)
            msg.append(f);
        std::optional<IpcMessage> answer;
        const bool handled = Ed3kBridge::handleRequest(msg, [&](const IpcMessage& r) { answer = r; });
        return std::pair{handled, answer};
    };
    const auto refused = [&](IpcMsgType type, const QList<QCborValue>& fields) {
        const auto [handled, answer] = ask(type, fields);
        return handled && answer && answer->type() == IpcMsgType::Result && !answer->fieldBool(0);
    };
    const QString root(64, QLatin1Char('a'));
    QVERIFY(refused(IpcMsgType::AddEd3kDownload, {root, 1000, QStringLiteral("x.bin"), 0, false}));
    QVERIFY(refused(IpcMsgType::AddEd3kDownloadLink, {QStringLiteral("ed3k:file?x"), 0, false}));
    QVERIFY(refused(IpcMsgType::Ed3kDownloadControl, {root, 5}));
    QVERIFY(refused(IpcMsgType::NewEd3kCreditKey, {}));
    QVERIFY(refused(IpcMsgType::ImportEd3kCreditKey, {QStringLiteral("00")}));
    QVERIFY(refused(IpcMsgType::SetEd3kPassReports, {false}));
    QVERIFY(refused(IpcMsgType::FollowEd3kSet, {QStringLiteral("ed3k:set?x"), 0}));
    QVERIFY(refused(IpcMsgType::AddHosterLinks, {QStringLiteral("https://fake.example/f/1")}));
    QVERIFY(refused(IpcMsgType::DownloadJavaRuntime, {}));
    // reads answer
    QVERIFY(!refused(IpcMsgType::GetEd3kDownloads, {}));
    QVERIFY(!refused(IpcMsgType::GetHosterItems, {}));
    QVERIFY(!refused(IpcMsgType::GetEd3kPaid, {}));
    QVERIFY(!QFileInfo::exists(ed3kDir));

    // the same request with ed3k on does write: the test above is not empty
    thePrefs.setEd3kEnabled(true);
    QVERIFY(!refused(IpcMsgType::SetEd3kPassReports, {false}));
    QVERIFY(QFileInfo::exists(ed3kDir));

    Ed3kDownloadsBridge::setDownloads(nullptr);
    Ed3kCreditBridge::setCredit(nullptr);
    Ed3kPaidBridge::setPaid(nullptr);
    Ed3kSetsBridge::setSets(nullptr, nullptr, nullptr);
    Ed3kHosterBridge::setHoster(nullptr, nullptr, nullptr);
    thePrefs.setEd3kEnabled(wasOn);
}

void tst_Ed3kBridge::statusOfARunningNode()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    QVERIFY(connectPair(a, b));
    Ed3kBridge::setSession(&a);

    const QCborMap m = Ed3kBridge::statusToCbor();
    QCOMPARE(m.value(QStringLiteral("running")).toBool(), true);
    QCOMPARE(m.value(QStringLiteral("udpPort")).toInteger(), qint64(a.udpPort()));
    QCOMPARE(m.value(QStringLiteral("nodeId")).toString(), a.nodeIdHex());
    QVERIFY(m.value(QStringLiteral("contacts")).toInteger() > 0);
    // address class per family, router duty
    QVERIFY(m.contains(QStringLiteral("class4")));
    QVERIFY(m.contains(QStringLiteral("class6")));
    QVERIFY(m.contains(QStringLiteral("router")));
    QVERIFY(m.value(QStringLiteral("wireSent")).toInteger() > 0);
    QVERIFY(m.value(QStringLiteral("wireRecv")).toInteger() > 0);
    const QCborMap stats = m.value(QStringLiteral("stats")).toMap();
    for (const char* key : {"connections", "lookups", "records", "recordBytes", "published", "queueLen",
                            "bannedNodes", "bannedHosts", "sentFile", "sentOther", "recvFile", "recvOther"})
        QVERIFY2(stats.contains(QString::fromLatin1(key)), key);

    // the same map through IPC
    IpcMessage answer;
    QVERIFY(Ed3kBridge::handleRequest(IpcMessage(IpcMsgType::GetEd3kStatus, 4),
                                      [&answer](const IpcMessage& r) { answer = r; }));
    QCOMPARE(answer.type(), IpcMsgType::Result);
    QCOMPARE(answer.seqId(), 4);
    QVERIFY(answer.fieldBool(0));
    QCOMPARE(answer.fieldMap(1).value(QStringLiteral("nodeId")).toString(), a.nodeIdHex());
    a.stop();
    b.stop();
}

void tst_Ed3kBridge::onlyTheEd3kBlockIsAnswered()
{
    int replies = 0;
    IpcMessage last;
    const auto reply = [&](const IpcMessage& r) {
        ++replies;
        last = r;
    };
    QVERIFY(!Ed3kBridge::handleRequest(IpcMessage(IpcMsgType::GetBtStatus, 1), reply));
    QVERIFY(!Ed3kBridge::handleRequest(IpcMessage(IpcMsgType::PushEd3kStatus, 0), reply));
    QCOMPARE(replies, 0);
    // a number of the block not known (yet): an error, not silence
    QVERIFY(Ed3kBridge::handleRequest(IpcMessage(static_cast<IpcMsgType>(1049), 2), reply));
    QCOMPARE(replies, 1);
    QCOMPARE(last.type(), IpcMsgType::Error);
}

void tst_Ed3kBridge::coordinatorLimitsAndCountsTheNode()
{
    const uint32 maxDownBefore = thePrefs.maxDownload();
    const uint32 maxUpBefore = thePrefs.maxUpload();
    thePrefs.setMaxDownload(200);
    thePrefs.setMaxUpload(50);

    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    {
        BandwidthCoordinator bw(nullptr, nullptr);
        bw.setEd3k(&a);
        bw.start();   // the first sample is the baseline
        QCOMPARE(BandwidthCoordinator::instance(), &bw);
        // nothing else busy: the node may use eMule's whole line
        QCOMPARE(bw.snapshot().ed3kDownKb, qint64(200));
        QCOMPARE(bw.snapshot().ed3kUpKb, qint64(50));
        QCOMPARE(a.limits().down, std::uint64_t(200 * 1024));
        QCOMPARE(a.limits().up, std::uint64_t(50 * 1024));

        // traffic between two ticks: rates, and eD2K yields what ed3k receives
        QVERIFY(connectPair(a, b));
        QTest::qWait(20);
        bw.tick();
        const auto& s = bw.snapshot();
        QVERIFY(s.ed3kUpRate > 0);
        QVERIFY(s.ed3kDownRate > 0);
        const qint64 ed2kDown = std::max<qint64>(1, 200 - s.ed3kDownRate / 1024);
        QCOMPARE(qint64(thePrefs.maxDownloadForEd2k()), ed2kDown);
        QCOMPARE(s.ed2kDownKb, ed2kDown);

        // the status carries the coordinator's rates and the node's limits
        Ed3kBridge::setSession(&a);
        const QCborMap m = Ed3kBridge::statusToCbor();
        QCOMPARE(m.value(QStringLiteral("rateDown")).toInteger(), s.ed3kDownRate);
        QCOMPARE(m.value(QStringLiteral("limitDownKb")).toInteger(), qint64(200));
        QCOMPARE(m.value(QStringLiteral("limitUpKb")).toInteger(), qint64(50));

        // a new line reaches the node at the next tick
        thePrefs.setMaxDownload(100);
        bw.tick();
        QCOMPARE(a.limits().down, std::uint64_t(100 * 1024));
    }
    // stopped: the global line, unshared
    QCOMPARE(a.limits().down, std::uint64_t(100 * 1024));
    QCOMPARE(BandwidthCoordinator::instance(), nullptr);
    QCOMPARE(thePrefs.maxDownloadForEd2k(), uint32(100));

    a.stop();
    b.stop();
    thePrefs.setMaxDownload(maxDownBefore);
    thePrefs.setMaxUpload(maxUpBefore);
}

void tst_Ed3kBridge::coordinatorSizesEd3kSlotsFromTheShare()
{
    // 25 KB/s a slot, at least 2, no cap (Protocol.md §12.5); no limit: the node chooses
    QCOMPARE(BandwidthCoordinator::ed3kSlotsFor(0), 0u);
    QCOMPARE(BandwidthCoordinator::ed3kSlotsFor(10), 2u);
    QCOMPARE(BandwidthCoordinator::ed3kSlotsFor(250), 10u);
    QCOMPARE(BandwidthCoordinator::ed3kSlotsFor(1000), 40u);

    const uint32 maxUpBefore = thePrefs.maxUpload();
    thePrefs.setMaxUpload(250);
    QTemporaryDir da;
    QVERIFY(da.isValid());
    Ed3kSession a;
    QVERIFY(a.start(loopback(da.path())));
    {
        BandwidthCoordinator bw(nullptr, nullptr);
        bw.setEd3k(&a);
        bw.start();
        // an idle node gets the slots of its share, not 2 for the rate it has not reached
        QCOMPARE(bw.snapshot().ed3kUpKb, qint64(250));
        const auto q = a.uploadQueueStats();
        QVERIFY(q.has_value());
        QCOMPARE(q->slotCount, 10u);
        QCOMPARE(q->holders, 0u);
    }
    a.stop();
    thePrefs.setMaxUpload(maxUpBefore);
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

Ed3kSettings tst_Ed3kBridge::loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.udpPort = 0;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");
    s.forceOpen = true;
    return s;
}

bool tst_Ed3kBridge::connectPair(Ed3kSession& a, Ed3kSession& b)
{
    const auto bId = b.nodeId();
    if (!bId || !a.engine())
        return false;
    const ::ed3k::Node bNode{*bId, ::ed3k::Address::v4(127, 0, 0, 1, b.udpPort())};
    bool started = false;
    a.post([&](::ed3k::Engine& e, ::ed3k::Now now) {
        started = e.bootstrap(now, std::span<const ::ed3k::Node>{&bNode, 1}).has_value();
    });
    if (!started)
        return false;
    QDeadlineTimer deadline(20000);
    while (a.contactCount() == 0 && !deadline.hasExpired())
        QTest::qWait(50);
    return a.contactCount() > 0;
}

QTEST_GUILESS_MAIN(tst_Ed3kBridge)
#include "tst_Ed3kBridge.moc"
