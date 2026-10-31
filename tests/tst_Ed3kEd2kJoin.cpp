/// @file tst_Ed3kEd2kJoin.cpp
/// @brief eD2K compat (ed3k Protocol.md §14.2, §14.4) in src/ed3k/Ed3kEd2kJoin: node A shares
/// a file over ed3k (Ed3kShared, ed2k ALIAS), node B has an eD2K part file of it and gets the
/// bytes over ed3k; C is the Kad node between them. Pair trust, false pairs, hinted sources.

#include "Ed3kEd2kJoin.h"
#include "Ed3kSession.h"
#include "Ed3kShared.h"

#include "app/AppContext.h"
#include "client/ClientList.h"
#include "client/UpDownClient.h"
#include "files/PartFile.h"
#include "prefs/Preferences.h"
#include "transfer/DownloadQueue.h"
#include "utils/Opcodes.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>
#include <memory>

using namespace eMule;
using namespace eMule::ed3k;

namespace {

constexpr std::uint64_t kBlock = 256 * 1024;
constexpr int kFileSize = int(3 * kBlock + 12345);   // one eD2K part: the file hash is its hash

QByteArray payload(int size, int seed)
{
    QByteArray data(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 13 + (i >> 7) * 3 + seed * 29) & 0xFF);
    return data;
}

QByteArray md4Of(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Md4);
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

Ed3kSettings loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.udpPort = 0;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");
    s.forceOpen = true;
    return s;
}

::ed3k::Node nodeOf(const Ed3kSession& s)
{
    return ::ed3k::Node{*s.nodeId(), ::ed3k::Address::v4(127, 0, 0, 1, s.udpPort())};
}

/// Asks no blocks: the download never completes (sources are still asked).
class IdleJoin final : public Ed3kEd2kJoin {
public:
    using Ed3kEd2kJoin::Ed3kEd2kJoin;

protected:
    std::vector<std::uint64_t> wantedBlocks(const Download&) const override { return {}; }
};

/// A, B and C on loopback; A and B know C (Kad between them).
struct World {
    QTemporaryDir da, db, dc;
    Ed3kSession a, b, c;

    bool up()
    {
        if (!da.isValid() || !db.isValid() || !dc.isValid())
            return false;
        if (!c.start(loopback(dc.path())) || !a.start(loopback(da.path())) || !b.start(loopback(db.path())))
            return false;
        const auto cNode = nodeOf(c);
        for (Ed3kSession* s : {&a, &b})
            s->post([cNode](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&cNode, 1}); });
        return QTest::qWaitFor([&] { return a.contactCount() > 0 && b.contactCount() > 0; }, 20000);
    }
};

} // namespace

class tst_Ed3kEd2kJoin : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void anEd2kDownloadIsFedFromEd3k();
    void aPartFailingWhoseBlocksPassMakesThePairFalse();
    void aSourceNamingAnotherEd2kHashDropsThePair();
    void aHintedSourceIsUsedOverEd3k();
    void anUnansweredHintFallsBackToEd2k();
    void aClientWaitsForEd3kThenFallsBack();

private:
    /// B's eD2K part file for @p md4 of @p size (owned by the queue).
    PartFile* addPartFile(const QByteArray& md4, std::uint64_t size, const QString& name);
    /// The verdict hook as the daemon has it (queued).
    void hookVerdicts(Ed3kEd2kJoin& join);
    /// Flushes B's part files until @p done (verification runs on a flush).
    bool flushUntil(const std::function<bool()>& done, int ms);

    QTemporaryDir m_dir;
    std::unique_ptr<ClientList> m_clients;
    std::unique_ptr<DownloadQueue> m_queue;
    int m_serial = 0;
};

void tst_Ed3kEd2kJoin::initTestCase()
{
    QVERIFY(m_dir.isValid());
    thePrefs.load(m_dir.filePath(QStringLiteral("prefs.yaml")));
    thePrefs.setConfigDir(m_dir.path());
    QDir().mkpath(m_dir.filePath(QStringLiteral("temp")));
    thePrefs.setTempDirs({m_dir.filePath(QStringLiteral("temp"))});
}

void tst_Ed3kEd2kJoin::init()
{
    m_clients = std::make_unique<ClientList>();
    theApp.clientList = m_clients.get();
    m_queue = std::make_unique<DownloadQueue>();
    theApp.downloadQueue = m_queue.get();
}

void tst_Ed3kEd2kJoin::cleanup()
{
    theApp.partVerdict = {};
    theApp.ed3kHint = {};
    m_queue.reset();
    theApp.downloadQueue = nullptr;
    m_clients.reset();
    theApp.clientList = nullptr;
    QTest::qWait(10);
}

// §14.4: blocks verified under BLAKE3 land in the eD2K part file; the part then passes MD4,
// so the pair is trusted
void tst_Ed3kEd2kJoin::anEd2kDownloadIsFedFromEd3k()
{
    World w;
    QVERIFY(w.up());
    const QByteArray data = payload(kFileSize, 1);
    const QByteArray md4 = md4Of(data);
    const QString path = QDir(w.da.path()).filePath(QStringLiteral("both.bin"));
    QVERIFY(writeFile(path, data));
    FakeSource src;
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("both.bin"), std::uint64_t(data.size())});
    Ed3kShared shared(w.a, src, w.da.path());
    QSignalSpy sharedSpy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QTRY_COMPARE_WITH_TIMEOUT(sharedSpy.count(), 1, 20000);
    QVERIFY(shared.aliasPublished(md4));

    PartFile* pf = addPartFile(md4, std::uint64_t(data.size()), QStringLiteral("both.bin"));
    QVERIFY(pf);
    Ed3kEd2kJoin join(w.b, *m_queue, w.db.path());
    join.setSearchGap(1000);
    hookVerdicts(join);
    QSignalSpy pairSpy(&join, &Ed3kEd2kJoin::pairChanged);
    join.start();
    QTRY_VERIFY_WITH_TIMEOUT(join.rootOf(md4).has_value(), 30000);
    QCOMPARE(*join.rootOf(md4), *shared.rootOf(md4));
    QVERIFY(flushUntil([&] { return join.pairOf(md4) == Ed3kEd2kJoin::Pair::Trusted || !m_queue->fileByID(reinterpret_cast<const uint8*>(md4.constData())) || pf->isComplete(0); }, 60000));
    QTRY_VERIFY_WITH_TIMEOUT(!pairSpy.isEmpty(), 5000);
    QCOMPARE(pairSpy.first().at(1).value<Ed3kEd2kJoin::Pair>(), Ed3kEd2kJoin::Pair::Trusted);
    // what File Details shows
    if (const auto info = join.infoOf(md4)) {
        QCOMPARE(info->root, join.rootOf(md4));
        QCOMPARE(info->pair, Ed3kEd2kJoin::Pair::Trusted);
        QVERIFY(!info->searching);
        QCOMPARE(info->bytes, join.ed3kBytes(md4));
        QVERIFY(info->bytes > 0);
        QVERIFY(info->transferring <= info->sources);
    }
    QVERIFY(!join.infoOf(md4Of(payload(kFileSize, 9))).has_value());   // not a download here
    join.stop();
    shared.stop();
}

// §14.4: the eD2K part fails while its blocks pass under the root: the pair is false, the
// root dropped
void tst_Ed3kEd2kJoin::aPartFailingWhoseBlocksPassMakesThePairFalse()
{
    World w;
    QVERIFY(w.up());
    const QByteArray data = payload(kFileSize, 2);
    // a record pairing the root with another eD2K file (A shares it under that hash)
    const QByteArray wrong = md4Of(payload(kFileSize, 3));
    const QString path = QDir(w.da.path()).filePath(QStringLiteral("liar.bin"));
    QVERIFY(writeFile(path, data));
    FakeSource src;
    src.entries.push_back(SharedEntry{wrong, path, QStringLiteral("liar.bin"), std::uint64_t(data.size())});
    Ed3kShared shared(w.a, src, w.da.path());
    QSignalSpy sharedSpy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QTRY_COMPARE_WITH_TIMEOUT(sharedSpy.count(), 1, 20000);

    PartFile* pf = addPartFile(wrong, std::uint64_t(data.size()), QStringLiteral("liar.bin"));
    QVERIFY(pf);
    Ed3kEd2kJoin join(w.b, *m_queue, w.db.path());
    join.setSearchGap(1000);
    hookVerdicts(join);
    join.start();
    // found and dropped may lie between two polls: the verdict is what counts
    QVERIFY(flushUntil([&] { return join.pairOf(wrong) == Ed3kEd2kJoin::Pair::False; }, 60000));
    QVERIFY(!join.rootOf(wrong).has_value());
    // nothing of it kept: the part is a gap again
    QVERIFY(!pf->isComplete(0));
    join.stop();
    shared.stop();
}

// §14.4: FileStatus names the root's eD2K hash; another one than ours makes the pair false
void tst_Ed3kEd2kJoin::aSourceNamingAnotherEd2kHashDropsThePair()
{
    World w;
    QVERIFY(w.up());
    const QByteArray data = payload(kFileSize, 4);
    const QByteArray md4 = md4Of(data);
    const QByteArray other = md4Of(payload(kFileSize, 5));
    const QString path = QDir(w.da.path()).filePath(QStringLiteral("named.bin"));
    QVERIFY(writeFile(path, data));
    FakeSource src;
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("named.bin"), std::uint64_t(data.size())});
    Ed3kShared shared(w.a, src, w.da.path());
    QSignalSpy sharedSpy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QTRY_COMPARE_WITH_TIMEOUT(sharedSpy.count(), 1, 20000);
    // a 2nd record claims the root for `other` too
    const auto root = *shared.rootOf(md4);
    const auto alias = Ed3kShared::ed2kAlias(other, std::uint64_t(data.size()), root, QStringLiteral("named.bin"));
    bool published = false;
    w.a.post([&](::ed3k::Engine& e, ::ed3k::Now now) { published = e.publishAlias(now, std::span{&alias, 1}).has_value(); });
    QTRY_VERIFY_WITH_TIMEOUT(published, 5000);

    QVERIFY(addPartFile(other, std::uint64_t(data.size()), QStringLiteral("named.bin")));
    Ed3kEd2kJoin join(w.b, *m_queue, w.db.path());
    join.setSearchGap(1000);
    join.start();
    QTRY_COMPARE_WITH_TIMEOUT(join.pairOf(other), Ed3kEd2kJoin::Pair::False, 60000);
    QVERIFY(!join.rootOf(other).has_value());
    join.stop();
    shared.stop();
}

// §14.2: an eD2K source with A's derived user hash, at A's address and port, answers over
// ed3k; the client is then used over ed3k only
void tst_Ed3kEd2kJoin::aHintedSourceIsUsedOverEd3k()
{
    World w;
    QVERIFY(w.up());
    const QByteArray data = payload(kFileSize, 6);
    const QByteArray md4 = md4Of(data);
    const QString path = QDir(w.da.path()).filePath(QStringLiteral("hint.bin"));
    QVERIFY(writeFile(path, data));
    FakeSource src;
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("hint.bin"), std::uint64_t(data.size())});
    Ed3kShared shared(w.a, src, w.da.path());
    QSignalSpy sharedSpy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QTRY_COMPARE_WITH_TIMEOUT(sharedSpy.count(), 1, 20000);

    PartFile* pf = addPartFile(md4, std::uint64_t(data.size()), QStringLiteral("hint.bin"));
    QVERIFY(pf);
    IdleJoin join(w.b, *m_queue, w.db.path());
    join.setSearchGap(1000);
    join.setHintTimeout(60000);
    join.start();

    auto client = std::make_unique<UpDownClient>();
    const auto hash = ::ed3k::userHash(*w.a.nodeId());
    client->setUserHash(hash.data());
    client->setConnectAddress(Address::fromString(QStringLiteral("127.0.0.1")));
    client->setUserIDHybrid(0x7F000001);   // a HighID
    client->setUserPort(w.a.udpPort());     // §14.1: ed3k UDP = eD2K TCP
    client->setReqFile(pf);
    QCOMPARE(join.offerHint(*client, true), AppContext::Ed3kHint::Started);
    client->setEd3kRoute(Ed3kRoute::Pending);
    QTRY_COMPARE_WITH_TIMEOUT(client->ed3kRoute(), Ed3kRoute::Preferred, 60000);
    QCOMPARE(client->downloadStateDisplayString(), QStringLiteral("ViaEd3k"));

    // a legacy hash is no hint
    auto legacy = std::make_unique<UpDownClient>();
    std::array<uint8, 16> plain{};
    plain.fill(0x42);
    plain[5] = 14;
    plain[14] = 111;
    legacy->setUserHash(plain.data());
    legacy->setReqFile(pf);
    QCOMPARE(join.offerHint(*legacy, true), AppContext::Ed3kHint::NoHint);

    join.stop();
    // the node stopped: back to eD2K
    QCOMPARE(client->ed3kRoute(), Ed3kRoute::Untried);
    legacy->setReqFile(nullptr);
    client->setReqFile(nullptr);
    shared.stop();
}

// §14.2 MUST fall back: a hinted source that never answers over ed3k is released to eD2K
void tst_Ed3kEd2kJoin::anUnansweredHintFallsBackToEd2k()
{
    World w;
    QVERIFY(w.up());
    const QByteArray data = payload(kFileSize, 7);
    const QByteArray md4 = md4Of(data);
    const QString path = QDir(w.da.path()).filePath(QStringLiteral("quiet.bin"));
    QVERIFY(writeFile(path, data));
    FakeSource src;
    src.entries.push_back(SharedEntry{md4, path, QStringLiteral("quiet.bin"), std::uint64_t(data.size())});
    Ed3kShared shared(w.a, src, w.da.path());
    QSignalSpy sharedSpy(&shared, &Ed3kShared::fileShared);
    shared.start();
    QTRY_COMPARE_WITH_TIMEOUT(sharedSpy.count(), 1, 20000);

    PartFile* pf = addPartFile(md4, std::uint64_t(data.size()), QStringLiteral("quiet.bin"));
    QVERIFY(pf);
    IdleJoin join(w.b, *m_queue, w.db.path());   // the file must not complete first
    join.setSearchGap(1000);
    join.setHintTimeout(1500);
    join.start();
    QTRY_VERIFY_WITH_TIMEOUT(join.rootOf(md4).has_value(), 30000);

    // a derived hash of a node nobody runs, at a port nobody listens on
    ::ed3k::NodeId ghost{};
    ghost.bytes.fill(0x77);
    const auto hash = ::ed3k::userHash(ghost);
    auto client = std::make_unique<UpDownClient>();
    client->setUserHash(hash.data());
    client->setConnectAddress(Address::fromString(QStringLiteral("127.0.0.1")));
    client->setUserIDHybrid(0x7F000001);
    client->setUserPort(9);
    client->setReqFile(pf);
    QCOMPARE(join.offerHint(*client, true), AppContext::Ed3kHint::Started);
    client->setEd3kRoute(Ed3kRoute::Pending);
    QTRY_VERIFY_WITH_TIMEOUT(client->ed3kRoute() != Ed3kRoute::Pending, 20000);
    QVERIFY(client->ed3kRoute() != Ed3kRoute::Preferred);
    client->setReqFile(nullptr);
    join.stop();
    shared.stop();
}

// UpDownClient: askForDownload waits for ed3k (no eD2K dial); the verdict lets it go on
void tst_Ed3kEd2kJoin::aClientWaitsForEd3kThenFallsBack()
{
    PartFile* pf = addPartFile(md4Of(payload(1000, 8)), 1000, QStringLiteral("wait.bin"));
    QVERIFY(pf);
    int asked = 0;
    theApp.ed3kHint = [&asked](UpDownClient&, bool wait) {
        ++asked;
        return wait ? AppContext::Ed3kHint::Started : AppContext::Ed3kHint::Off;
    };
    auto client = std::make_unique<UpDownClient>();
    std::array<uint8, 16> hash{};
    hash.fill(0x31);
    client->setUserHash(hash.data());
    client->setConnectAddress(Address::fromString(QStringLiteral("127.0.0.1")));
    client->setUserIDHybrid(0x7F000001);
    client->setUserPort(9);
    client->setReqFile(pf);
    QVERIFY(client->askForDownload());
    QCOMPARE(asked, 1);
    QCOMPARE(client->connectingState(), ConnectingState::Ed3kHint);
    QCOMPARE(client->ed3kRoute(), Ed3kRoute::Pending);
    QVERIFY(!client->socket());
    // not answered: eD2K dials now
    client->setEd3kRoute(Ed3kRoute::Failed);
    QVERIFY(client->connectingState() != ConnectingState::Ed3kHint);
    // Failed: never asked again
    client->setConnectingState(ConnectingState::None);
    client->setDownloadState(DownloadState::None);
    (void)client->askForDownload();
    QCOMPARE(asked, 1);

    // Preferred: idle on eD2K, no dial
    auto viaEd3k = std::make_unique<UpDownClient>();
    hash.fill(0x32);
    viaEd3k->setUserHash(hash.data());
    viaEd3k->setConnectAddress(Address::fromString(QStringLiteral("127.0.0.1")));
    viaEd3k->setUserIDHybrid(0x7F000002);
    viaEd3k->setUserPort(9);
    viaEd3k->setReqFile(pf);
    viaEd3k->setEd3kRoute(Ed3kRoute::Preferred);
    QVERIFY(!viaEd3k->askForDownload());
    QCOMPARE(viaEd3k->connectingState(), ConnectingState::None);
    QCOMPARE(viaEd3k->downloadStateDisplayString(), QStringLiteral("ViaEd3k"));

    if (client->socket())
        client->disconnected(QStringLiteral("test"));
    client->setReqFile(nullptr);
    viaEd3k->setReqFile(nullptr);
}

// ---------------------------------------------------------------------------

PartFile* tst_Ed3kEd2kJoin::addPartFile(const QByteArray& md4, std::uint64_t size, const QString& name)
{
    auto file = std::make_unique<PartFile>();
    const QString temp = m_dir.filePath(QStringLiteral("temp"));
    file->setFileName(QStringLiteral("%1-%2").arg(++m_serial).arg(name));
    file->setFileSize(EMFileSize(size));
    file->setTmpPath(temp);
    file->setFileHash(reinterpret_cast<const uint8*>(md4.constData()));
    if (!file->createPartFile(temp))
        return nullptr;
    PartFile* raw = file.release();
    m_queue->addDownload(raw);
    return raw;
}

void tst_Ed3kEd2kJoin::hookVerdicts(Ed3kEd2kJoin& join)
{
    theApp.partVerdict = [j = QPointer<Ed3kEd2kJoin>(&join)](PartFile& file, uint32 part, bool ok) {
        const QByteArray md4(reinterpret_cast<const char*>(file.fileHash()), 16);
        QMetaObject::invokeMethod(
            j.data(), [j, md4, part, ok] {
                if (j)
                    j->onPartVerdict(md4, part, ok);
            },
            Qt::QueuedConnection);
    };
}

bool tst_Ed3kEd2kJoin::flushUntil(const std::function<bool()>& done, int ms)
{
    return QTest::qWaitFor(
        [&] {
            for (PartFile* f : m_queue->files())
                if (f)
                    f->flushBuffer();
            return done();
        },
        ms);
}

QTEST_MAIN(tst_Ed3kEd2kJoin)
#include "tst_Ed3kEd2kJoin.moc"
