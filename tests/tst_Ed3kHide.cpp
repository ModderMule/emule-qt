/// @file tst_Ed3kHide.cpp
/// @brief Hiding (hops.md §1), the global switch (Ed3kHide, batch 15): the prefs start and
/// stop the anonymous node live; one hop count for every use; new shares and downloads go
/// under the anonymous key while hiding, older ones keep theirs; keyed events reach only the
/// joins that know the anonymous key; moving a file asks nothing here (the GUI asks) and
/// closes it under one key first; the anonymous credit key follows each start; the status
/// (hide, kadOffline) over IPC. One node on loopback: no hop nodes, so paths stay unbuilt
/// ("waitingForHops").

#include "utils/BackgroundFileWriter.h"
#include "Ed3kBridge.h"
#include "Ed3kCredit.h"
#include "Ed3kDownloads.h"
#include "Ed3kHide.h"
#include "Ed3kHideBridge.h"
#include "Ed3kJoinBase.h"
#include "Ed3kSession.h"
#include "Ed3kShared.h"

#include "prefs/Preferences.h"

#include <QCborMap>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;
using namespace eMule::ed3k;

namespace {

QByteArray payload(int size, int seed)
{
    QByteArray data(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 7 + seed * 31 + (i >> 9)) & 0xFF);
    return data;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

class FakeSource final : public SharedFileSource {
public:
    std::vector<SharedEntry> entries;
    std::vector<SharedEntry> files() const override { return entries; }
    std::optional<SharedEntry> find(const QByteArray& md4) const override
    {
        for (const auto& e : entries)
            if (e.md4 == md4)
                return e;
        return std::nullopt;
    }
};

/// A join of the node key only (not anonCapable): shares a blob.
class NodeJoin final : public Ed3kJoinBase {
public:
    explicit NodeJoin(Ed3kSession& s, const QString& dir) : Ed3kJoinBase(s, dir, QStringLiteral("nodejoin"), nullptr) {}
    void start() override { beginStart(); }
    void stop() override { closeAll(); }
    std::optional<::ed3k::Root> share(const QByteArray& data) { return shareBlob(data, QStringLiteral("blob.bin"), std::nullopt); }

protected:
    std::vector<std::uint64_t> wantedBlocks(const Download&) const override { return {}; }
    void onFileBlock(const Download&, const ::ed3k::event::Block&) override {}
    void onBlobFetched(const QString&, int, const ::ed3k::Root&, const QByteArray&) override {}
    std::optional<FileRead> sharedRead(const Shared&, std::uint64_t, std::uint32_t) const override { return std::nullopt; }
    bool hasOwner(const QString&) const override { return false; }
    void saveOwner(const QString&) override {}
};

Ed3kSettings loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.udpPort = 0;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k hide test");
    s.forceOpen = true;
    return s;
}

Ed3kHideSettings hidden(int hops = 1)
{
    Ed3kHideSettings h;
    h.on = true;
    h.hops = hops;
    return h;
}

QByteArray md4Of(char c)
{
    return QByteArray(16, c);
}

QString hexOf(const ::ed3k::Root& r)
{
    return QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(r.bytes.data()), 32).toHex());
}

} // namespace

class tst_Ed3kHide : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void prefStartsAndStopsTheAnonymousNode();
    void oneHopCountForEveryUse();
    void zeroHopsOnlyWhileHopping();
    void newUsesFollowTheSwitch();
    void keyedEventsReachOnlyAnonCapableJoins();
    void newSharesGoAnonymousOldOnesKeepTheirKey();
    void movingAFileClosesItUnderTheOtherKey();
    void downloadsFollowTheSwitch();
    void anonymousCreditKeyAfterEachStart();
    void newAnonymousKeyRestartsTheNode();
    void statusOverIpc();
};

void tst_Ed3kHide::cleanup()
{
    Ed3kHideBridge::setTargets(nullptr, nullptr, nullptr);
    Ed3kBridge::setSession(nullptr);
}

void tst_Ed3kHide::prefStartsAndStopsTheAnonymousNode()
{
    QTemporaryDir dir;
    Ed3kSession s;
    // before the start: kept, applied at start
    s.setHideSettings(hidden());
    QVERIFY(!s.hide().hiding());
    QSignalSpy started(&s.hide(), &Ed3kHide::started);
    QSignalSpy stopped(&s.hide(), &Ed3kHide::stopped);
    QVERIFY(s.start(loopback(dir.path())));
    QVERIFY(s.hide().hiding());
    QCOMPARE(started.count(), 1);
    const auto id = s.hide().anonId();
    QVERIFY(id.has_value());
    QVERIFY(*id != *s.nodeId());   // never the node key (§1)
    // the seed: a secret, owner-only
    const QString seed = QDir(s.stateDir()).filePath(Ed3kHide::kSeedFile);
    QVERIFY(QFile::exists(seed));
    QCOMPARE(QFile(seed).size(), 32);
#ifndef Q_OS_WIN
    QCOMPARE(QFile::permissions(seed) & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther),
             QFile::Permissions{});
#endif

    // off live: no restart of the node
    const QString nodeId = s.nodeIdHex();
    Ed3kHideSettings off = hidden();
    off.on = false;
    s.setHideSettings(off);
    QVERIFY(!s.hide().hiding());
    QVERIFY(!s.engine()->anonId()->has_value());
    QCOMPARE(stopped.count(), 1);
    QCOMPARE(s.nodeIdHex(), nodeId);

    // on again: the same anonymous key (anon.key kept)
    s.setHideSettings(hidden());
    QCOMPARE(s.hide().anonId(), id);
    QCOMPARE(started.count(), 2);
    s.stop();
    QVERIFY(!s.hide().hiding());
    QCOMPARE(stopped.count(), 2);
}

void tst_Ed3kHide::oneHopCountForEveryUse()
{
    // HideCfg::uniform: out and in, every per-type count inherits (0); Ed3kHide names each (below)
    const auto cfg = ::ed3k::HideCfg::uniform(2);
    QCOMPARE(int(cfg.hopsOut), 2);
    QCOMPARE(int(cfg.hopsIn), 2);
    QCOMPARE(int(cfg.gates), 1);
    for (const auto per : {cfg.lookupHops, cfg.downloadHops, cfg.shareHops, cfg.publishHops, cfg.roomHops, cfg.paidHops})
        QCOMPARE(int(per), 0);

    QTemporaryDir dir;
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    for (const int hops : {1, 2, 3}) {
        s.setHideSettings(hidden(hops));
        QVERIFY2(s.hide().hiding(), qPrintable(QString::number(hops)));
        QCOMPARE(s.status().hide.hops, hops);
    }
    // out of range: 0 to 3 only
    s.setHideSettings(hidden(7));
    QCOMPARE(s.hide().settings().hops, 3);
    // was: hidden(0) gave 1 (now 0 while hopping, zeroHopsOnlyWhileHopping)
    s.stop();

    // what anonStart is given: every use at the one count (0 inherits), nothing under the node key
    const auto two = Ed3kHide::hideCfg(2);
    QCOMPARE(int(two.hopsOut), 2);
    QCOMPARE(int(two.hopsIn), 2);
    QCOMPARE(int(two.gates), 1);
    for (const auto per : {two.lookupHops, two.downloadHops, two.shareHops, two.publishHops, two.roomHops, two.paidHops})
        QCOMPARE(int(per), 0);
    QCOMPARE(int(two.nodeKeyUses), 0);
    QVERIFY(Ed3kHide::hideCfg(9).hopsOut != ::ed3k::HideCfg::kHopsOff);
}

// hops.md §2.6: 0 = deniable, not hidden; offered only while forwarding for others
void tst_Ed3kHide::zeroHopsOnlyWhileHopping()
{
    // hopping defaults on with hiding (§1)
    QVERIFY(Ed3kHideSettings{}.hopping);
    Ed3kHideSettings h = hidden(0);
    QCOMPARE(Ed3kHide::normalized(h).hops, 0);
    h.hopping = false;
    QCOMPARE(Ed3kHide::normalized(h).hops, 1);   // hopping off at 0: 1
    h.hops = -3;
    QCOMPARE(Ed3kHide::normalized(h).hops, 1);
    h.hopping = true;
    QCOMPARE(Ed3kHide::normalized(h).hops, 0);
    const auto zero = Ed3kHide::hideCfg(0);
    QCOMPARE(int(zero.hopsOut), 0);
    QCOMPARE(int(zero.hopsIn), 0);

    QTemporaryDir dir;
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    s.setHideSettings(hidden(0));
    QCOMPARE(s.hide().settings().hops, 0);
    // hiding runs (a core without 0 hops yet takes 1)
    QVERIFY(s.hide().hiding());
    Ed3kHideSettings noHop = hidden(0);
    noHop.hopping = false;
    s.setHideSettings(noHop);
    QCOMPARE(s.hide().settings().hops, 1);
    QVERIFY(s.hide().hiding());
    s.stop();
}

// hops.md §1: a new use (search, live, set, room, paid) goes under the anonymous key while
// hiding runs, else the node key; nothing under the anonymous key while off
void tst_Ed3kHide::newUsesFollowTheSwitch()
{
    QTemporaryDir dir;
    Ed3kSession s;
    QCOMPARE(s.hide().keyForUse(), ::ed3k::KeyKind::Node);
    QVERIFY(s.start(loopback(dir.path())));
    QCOMPARE(s.hide().keyForUse(), ::ed3k::KeyKind::Node);
    s.setHideSettings(hidden());
    QCOMPARE(s.hide().keyForUse(), ::ed3k::KeyKind::Anon);
    Ed3kHideSettings off = hidden();
    off.on = false;
    s.setHideSettings(off);
    QCOMPARE(s.hide().keyForUse(), ::ed3k::KeyKind::Node);
    s.stop();
    QCOMPARE(s.hide().keyForUse(), ::ed3k::KeyKind::Node);
}

void tst_Ed3kHide::keyedEventsReachOnlyAnonCapableJoins()
{
    namespace e = ::ed3k::event;
    QTemporaryDir dir;
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    s.setHideSettings(hidden());
    NodeJoin node(s, dir.path());
    node.start();
    const auto blob = node.share(payload(5000, 1));
    QVERIFY(blob);

    // the anonymous node's FileRequested of a root the node key shares: never taken for it
    // as the session dispatches: every event keyed, the node key's also on eventPolled
    const auto send = [&s](const ::ed3k::Event& ev, ::ed3k::KeyKind key) {
        bool handled = false;
        emit s.keyedEventPolled(ev, key, &handled);
        if (key == ::ed3k::KeyKind::Node)
            emit s.eventPolled(ev, &handled);
        return handled;
    };
    ::ed3k::NodeId asker;
    asker.bytes.fill(9);
    QVERIFY(!send(e::FileRequested{asker, *blob, std::nullopt, true, false, true}, ::ed3k::KeyKind::Anon));
    QVERIFY(send(e::FileRequested{asker, *blob, std::nullopt, true, false, true}, ::ed3k::KeyKind::Node));

    // the share list (anonCapable) answers its anonymous file's request (anonQueueSet)
    FakeSource src;
    const QByteArray data = payload(300 * 1024, 2);
    const QString path = QDir(dir.path()).filePath(QStringLiteral("anon.bin"));
    QVERIFY(writeFile(path, data));
    src.entries.push_back(SharedEntry{md4Of('a'), path, QStringLiteral("anon.bin"), std::uint64_t(data.size())});
    Ed3kShared shared(s, src, dir.path());
    shared.start();
    QTRY_VERIFY_WITH_TIMEOUT(shared.rootOf(md4Of('a')).has_value(), 20000);
    const auto root = *shared.rootOf(md4Of('a'));
    QCOMPARE(*s.engine()->fileKey(root), std::optional(::ed3k::KeyKind::Anon));
    QVERIFY(send(e::FileRequested{asker, root, std::nullopt, true, false, false}, ::ed3k::KeyKind::Anon));

    // a slot of eventPolled never sees an event of the anonymous node
    int anonSeen = 0;
    int mixed = 0;
    connect(&s, &Ed3kSession::keyedEventPolled, this, [&](const ::ed3k::Event&, ::ed3k::KeyKind key, bool*) {
        if (key == ::ed3k::KeyKind::Anon)
            ++anonSeen;
    });
    connect(&s, &Ed3kSession::eventPolled, this, [&](const ::ed3k::Event&, bool*) {
        if (s.eventKey() == ::ed3k::KeyKind::Anon)
            ++mixed;
    });
    QTest::qWait(1500);
    s.pump();
    QCOMPARE(mixed, 0);
    qInfo("anonymous events seen: %d", anonSeen);
    shared.stop();
    node.stop();
    s.stop();
}

void tst_Ed3kHide::newSharesGoAnonymousOldOnesKeepTheirKey()
{
    QTemporaryDir dir;
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    FakeSource src;
    const QByteArray old = payload(260 * 1024, 3);
    const QString oldPath = QDir(dir.path()).filePath(QStringLiteral("old.bin"));
    QVERIFY(writeFile(oldPath, old));
    src.entries.push_back(SharedEntry{md4Of('o'), oldPath, QStringLiteral("old.bin"), std::uint64_t(old.size())});
    Ed3kShared shared(s, src, dir.path());
    shared.start();
    QTRY_VERIFY_WITH_TIMEOUT(shared.rootOf(md4Of('o')).has_value(), 20000);
    QCOMPARE(shared.keyOfFile(md4Of('o')), std::optional(::ed3k::KeyKind::Node));
    QVERIFY(shared.aliasPublished(md4Of('o')));

    // hiding on: the shared file keeps its key; a new one goes under the anonymous key
    s.setHideSettings(hidden());
    QCOMPARE(shared.keyOfFile(md4Of('o')), std::optional(::ed3k::KeyKind::Node));
    const QByteArray fresh = payload(270 * 1024, 4);
    const QString freshPath = QDir(dir.path()).filePath(QStringLiteral("new.bin"));
    QVERIFY(writeFile(freshPath, fresh));
    const SharedEntry entry{md4Of('n'), freshPath, QStringLiteral("new.bin"), std::uint64_t(fresh.size())};
    src.entries.push_back(entry);
    shared.onFileAdded(entry);
    QTRY_VERIFY_WITH_TIMEOUT(shared.rootOf(md4Of('n')).has_value(), 20000);
    const auto anonRoot = *shared.rootOf(md4Of('n'));
    QCOMPARE(shared.keyOfFile(md4Of('n')), std::optional(::ed3k::KeyKind::Anon));
    QCOMPARE(*s.engine()->fileKey(anonRoot), std::optional(::ed3k::KeyKind::Anon));
    // eD2K is never anonymous: no ed2k ALIAS for it
    QVERIFY(!shared.aliasPublished(md4Of('n')));
    QCOMPARE(s.status().hide.anonFiles, 1);

    // hiding off: the anonymous file is not served by the node key
    Ed3kHideSettings off;
    s.setHideSettings(off);
    QVERIFY(!shared.rootOf(md4Of('n')).has_value());
    QVERIFY(!shared.sharesRoot(anonRoot));
    QCOMPARE(shared.keyOfFile(md4Of('n')), std::optional(::ed3k::KeyKind::Anon));
    QVERIFY(shared.rootOf(md4Of('o')).has_value());
    // back on: shared again under the anonymous key
    s.setHideSettings(hidden());
    QVERIFY(shared.rootOf(md4Of('n')).has_value());
    QCOMPARE(*s.engine()->fileKey(anonRoot), std::optional(::ed3k::KeyKind::Anon));

    // a restart keeps each key (keys.json)
    shared.stop();
    s.stop();
    eMule::BackgroundFileWriter::instance().flush();   // saves are queued (report 43)
    QVERIFY(QFile::exists(QDir(dir.path()).filePath(QStringLiteral("ed3k/") + Ed3kHide::kKeysFile)));
    QVERIFY(s.start(loopback(dir.path())));
    shared.start();
    QTRY_VERIFY_WITH_TIMEOUT(shared.rootOf(md4Of('n')).has_value() && shared.rootOf(md4Of('o')).has_value(), 20000);
    QCOMPARE(shared.keyOfFile(md4Of('n')), std::optional(::ed3k::KeyKind::Anon));
    QCOMPARE(shared.keyOfFile(md4Of('o')), std::optional(::ed3k::KeyKind::Node));
    shared.stop();
    s.stop();
}

void tst_Ed3kHide::movingAFileClosesItUnderTheOtherKey()
{
    QTemporaryDir dir;
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kBridge::setSession(&s);
    FakeSource src;
    const QByteArray data = payload(280 * 1024, 5);
    const QString path = QDir(dir.path()).filePath(QStringLiteral("m.bin"));
    QVERIFY(writeFile(path, data));
    src.entries.push_back(SharedEntry{md4Of('m'), path, QStringLiteral("m.bin"), std::uint64_t(data.size())});
    Ed3kShared shared(s, src, dir.path());
    Ed3kHideBridge::setTargets(&s, &shared, nullptr);
    shared.start();
    QTRY_VERIFY_WITH_TIMEOUT(shared.rootOf(md4Of('m')).has_value(), 20000);
    const auto root = *shared.rootOf(md4Of('m'));

    const auto move = [](const QString& what, bool toAnon) {
        Ipc::IpcMessage msg(Ipc::IpcMsgType::MoveEd3kFileKey, 3);
        msg.append(what);
        msg.append(toAnon);
        Ipc::IpcMessage reply;
        thePrefs.setEd3kEnabled(true);
        if (!Ed3kHideBridge::handleRequest(msg, [&](const Ipc::IpcMessage& r) { reply = r; }))
            return Ipc::IpcMessage{};
        return reply;
    };
    // not hiding: nothing goes to the anonymous key
    QVERIFY(!move(hexOf(root), true).fieldBool(0));
    QCOMPARE(*s.engine()->fileKey(root), std::optional(::ed3k::KeyKind::Node));

    s.setHideSettings(hidden());
    // by the MD4 of the shared file (the Shared Files list has no root)
    const auto r = move(QString::fromLatin1(md4Of('m').toHex()), true);
    QVERIFY(r.fieldBool(0));
    QCOMPARE(r.fieldMap(1).value(QStringLiteral("key")).toString(), QStringLiteral("Anonymous"));
    QCOMPARE(*s.engine()->fileKey(root), std::optional(::ed3k::KeyKind::Anon));
    QVERIFY(!shared.aliasPublished(md4Of('m')));
    // the same key again: refused
    QVERIFY(!move(hexOf(root), true).fieldBool(0));
    // back to the node key by root
    QVERIFY(move(hexOf(root), false).fieldBool(0));
    QCOMPARE(*s.engine()->fileKey(root), std::optional(::ed3k::KeyKind::Node));
    QVERIFY(shared.aliasPublished(md4Of('m')));
    shared.stop();
    s.stop();
}

void tst_Ed3kHide::downloadsFollowTheSwitch()
{
    QTemporaryDir dir;
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kDownloads d(s, dir.path());
    d.setSearchEnabled(false);
    d.setDirs(QDir(dir.path()).filePath(QStringLiteral("temp")), QDir(dir.path()).filePath(QStringLiteral("incoming")));
    d.start();
    Ed3kDownloadSpec node;
    node.root.bytes.fill(0x11);
    node.size = 600 * 1024;
    node.name = QStringLiteral("node.bin");
    QVERIFY(d.add(node));
    QCOMPARE(d.keyOfRoot(node.root), ::ed3k::KeyKind::Node);

    s.setHideSettings(hidden());
    Ed3kDownloadSpec anon = node;
    anon.root.bytes.fill(0x22);
    anon.name = QStringLiteral("anon.bin");
    QVERIFY(d.add(anon));
    QCOMPARE(d.keyOfRoot(anon.root), ::ed3k::KeyKind::Anon);
    QCOMPARE(d.keyOfRoot(node.root), ::ed3k::KeyKind::Node);   // kept

    // moved (the GUI asked): opened again under the other key
    QVERIFY(d.moveKey(node.root, ::ed3k::KeyKind::Anon));
    QCOMPARE(d.keyOfRoot(node.root), ::ed3k::KeyKind::Anon);
    QVERIFY(!d.moveKey(node.root, ::ed3k::KeyKind::Anon));
    QVERIFY(d.moveKey(anon.root, ::ed3k::KeyKind::Node));
    QCOMPARE(*s.engine()->fileKey(anon.root), std::optional(::ed3k::KeyKind::Node));
    d.stop();
    s.stop();
}

void tst_Ed3kHide::anonymousCreditKeyAfterEachStart()
{
    QTemporaryDir dir;
    Ed3kSession s;
    Ed3kCredit c(s, dir.path());
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kCreditPrefs p;
    p.enabled = true;
    p.chainId = 31337;
    p.contract = QStringLiteral("0x") + QString(40, QLatin1Char('1'));
    p.quorum = 1;
    c.setConfig(p, 0);
    QTRY_VERIFY(c.status().configured);
    QVERIFY(!c.status().anonSet);
    // was: ToDo at Ed3kCredit.h (no anonStart caller); now each start hands the key in
    s.setHideSettings(hidden());
    QVERIFY(c.status().anonSet);
    const QString anon = c.status().anonAddress;
    QVERIFY(!anon.isEmpty());
    // a new anonymous key: a new anonymous credit address (§9.1)
    QVERIFY(s.hide().newAnonKey());
    QVERIFY(c.status().anonSet);
    QVERIFY(c.status().anonAddress != anon);
    s.stop();
}

void tst_Ed3kHide::newAnonymousKeyRestartsTheNode()
{
    QTemporaryDir dir;
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    s.setHideSettings(hidden());
    const auto before = s.hide().anonId();
    QSignalSpy stopped(&s.hide(), &Ed3kHide::stopped);
    QSignalSpy started(&s.hide(), &Ed3kHide::started);
    QVERIFY(s.hide().newAnonKey());
    QCOMPARE(stopped.count(), 1);
    QCOMPARE(started.count(), 1);
    QVERIFY(s.hide().anonId().has_value());
    QVERIFY(s.hide().anonId() != before);
    // not hiding: only the file is replaced
    Ed3kHideSettings off;
    s.setHideSettings(off);
    QVERIFY(s.hide().newAnonKey());
    QCOMPARE(started.count(), 1);
    QVERIFY(!s.hide().hiding());
    s.stop();
}

void tst_Ed3kHide::statusOverIpc()
{
    QTemporaryDir dir;
    Ed3kSession s;
    Ed3kBridge::setSession(&s);
    QVERIFY(s.start(loopback(dir.path())));
    QCborMap st = Ed3kBridge::statusToCbor();
    QVERIFY(st.contains(QStringLiteral("kadOffline")));
    QCOMPARE(st.value(QStringLiteral("hide")).toMap().value(QStringLiteral("running")).toBool(), false);

    s.setHideSettings(hidden(2));
    st = Ed3kBridge::statusToCbor();
    const QCborMap h = st.value(QStringLiteral("hide")).toMap();
    QVERIFY(h.value(QStringLiteral("on")).toBool());
    QVERIFY(h.value(QStringLiteral("running")).toBool());
    QCOMPARE(h.value(QStringLiteral("hops")).toInteger(), 2);
    QCOMPARE(h.value(QStringLiteral("nodeId")).toString().size(), 8);
    // one node, no hop nodes: no path is built
    QCOMPARE(h.value(QStringLiteral("built")).toInteger(), 0);
    QVERIFY(h.value(QStringLiteral("waitingForHops")).toBool());
    QCOMPARE(Ed3kHideBridge::statusToCbor(), h);
    // anonState(): HOP, ROUTER, whether its files can be found (§2.6 Inwards)
    const auto a = s.engine()->anonState();
    QVERIFY(a && a->running);
    QCOMPARE(h.value(QStringLiteral("hop")).toBool(), a->hop);
    QCOMPARE(h.value(QStringLiteral("router")).toBool(), a->router);
    QCOMPARE(h.value(QStringLiteral("shares")).toBool(), a->shares);
    // the status of the session carries the engine's kad_offline bits
    QCOMPARE(qint64(s.status().kadOffline), st.value(QStringLiteral("kadOffline")).toInteger());

    // PushEd3kHideChanged after a change (debounced)
    std::vector<Ipc::IpcMessage> pushes;
    Ed3kHideBridge::connectPushes(s, [&](const Ipc::IpcMessage& m) { pushes.push_back(m); });
    Ed3kHideSettings off;
    s.setHideSettings(off);
    QTRY_VERIFY(!pushes.empty());
    QCOMPARE(pushes.front().type(), Ipc::IpcMsgType::PushEd3kHideChanged);
    QVERIFY(!pushes.front().fieldMap(0).value(QStringLiteral("running")).toBool());
    s.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kHide)
#include "tst_Ed3kHide.moc"
