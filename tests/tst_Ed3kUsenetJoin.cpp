/// @file tst_Ed3kUsenetJoin.cpp
/// @brief The usenet join (src/ed3k/Ed3kUsenetJoin): the NZB buffer, articles from a real
/// queue verified under a claimed root, and member blocks from another ed3k node.

#include "FakeNntpServer.h"
#include "TestFixtures.h"
#include "TestHelpers.h"

#include "Ed3kSession.h"
#include "Ed3kUsenetJoin.h"

#include "decode/YencDecoder.h"
#include "nzb/NzbFile.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"

#include "prefs/Preferences.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>

using namespace eMule;
using namespace eMule::ed3k;
using namespace eMule::usenet;
using eMule::testing::FakeNntpServer;

namespace {

constexpr std::uint64_t kBlock = 256 * 1024;

QByteArray payload(int size, int seed = 0)
{
    QByteArray data(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 17 + (i >> 6) * 5 + seed * 31) & 0xFF);
    return data;
}

QString messageIdFor(int part)
{
    return QStringLiteral("j%1@example.com").arg(part);
}

QByteArray beginLine(int part, int total, qint64 size, const QString& name)
{
    return QStringLiteral("=ybegin part=%1 total=%2 line=128 size=%3 name=%4")
        .arg(part).arg(total).arg(size).arg(name).toLatin1();
}

/// One yEnc article as a provider serves it.
QByteArray makeArticle(const QByteArray& whole, int part, int total, int partSize, const QString& name)
{
    const int offset = (part - 1) * partSize;
    const QByteArray chunk = whole.mid(offset, partSize);
    QByteArray out = beginLine(part, total, whole.size(), name) + '\n';
    out += QStringLiteral("=ypart begin=%1 end=%2").arg(offset + 1).arg(offset + chunk.size()).toLatin1() + '\n';
    QByteArray line;
    for (const char raw : chunk) {
        const auto enc = quint8(quint8(raw) + 42);
        if (enc == 0x00 || enc == 0x0A || enc == 0x0D || enc == '=') {
            line.append('=');
            line.append(char(quint8(enc + 64)));
        } else {
            line.append(char(enc));
        }
        if (line.size() >= 128) {
            out += line + '\n';
            line.clear();
        }
    }
    if (!line.isEmpty())
        out += line + '\n';
    out += QStringLiteral("=yend size=%1 part=%2 pcrc32=%3")
               .arg(chunk.size()).arg(part).arg(yencCrc32(0, chunk), 8, 16, QLatin1Char('0')).toLatin1();
    return out;
}

QByteArray makeNzb(const QString& fileName, int parts)
{
    QByteArray xml = R"(<?xml version="1.0" encoding="iso-8859-1" ?>)"
                     "\n<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n";
    xml += QStringLiteral("  <file poster=\"t\" date=\"1700000000\" subject=\"&quot;%1&quot; yEnc (1/%2)\">\n")
               .arg(fileName).arg(parts).toUtf8();
    xml += "    <groups><group>alt.binaries.test</group></groups>\n    <segments>\n";
    for (int p = 1; p <= parts; ++p)
        xml += QStringLiteral("      <segment bytes=\"3500\" number=\"%1\">%2</segment>\n").arg(p).arg(messageIdFor(p)).toUtf8();
    xml += "    </segments>\n  </file>\n</nzb>\n";
    return xml;
}

/// Hash of @p data as the ed3k root, its size and block values.
struct Hashed {
    ::ed3k::Root root;
    std::uint64_t size = 0;
    ::ed3k::Bytes hashes;
};

Hashed hashOf(const QByteArray& data)
{
    ::ed3k::Hasher h;
    (void)h.update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())});
    auto done = h.finish();
    Hashed out;
    if (done) {
        out.root = done->root;
        out.size = done->size;
        const auto b = done->hashes.bytes();
        out.hashes.assign(b.begin(), b.end());
    }
    return out;
}

/// A queue of one item, by hand: records what the join asks of it.
class FakeQueue final : public UsenetJoinQueue {
public:
    std::unique_ptr<UsenetQueueItem> item;
    QList<QList<qint64>> accepted, refetched;
    QStringList excluded;
    SealHold hold;
    int released = 0;

    const UsenetQueueItem* findItem(const QString& id) const override
    {
        return item && item->id == id ? item.get() : nullptr;
    }
    QStringList itemIds() const override { return item ? QStringList{item->id} : QStringList{}; }
    int refetchRange(const QString&, int fileIndex, qint64 offset, qint64 length, const QString& exclude) override
    {
        refetched.append({fileIndex, offset, length});
        excluded.append(exclude);
        return 1;
    }
    int acceptExternalRange(const QString&, int fileIndex, qint64 offset, qint64 length) override
    {
        accepted.append({fileIndex, offset, length});
        return 1;
    }
    void setSealHold(SealHold h) override { hold = std::move(h); }
    void releaseSealHold(const QString&, int) override { ++released; }
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

/// A shares @p data under its root and answers every request with a slot and the block.
void shareFrom(Ed3kSession& a, const QByteArray& data, const Hashed& h, QObject* context)
{
    a.post([&](::ed3k::Engine& e, ::ed3k::Now now) {
        const ::ed3k::SharedFile f{h.root, h.size, "f.bin", std::nullopt, std::nullopt, ::ed3k::Have::complete(), std::span{h.hashes}};
        QVERIFY(e.fileOpen(now, f).has_value());
    });
    QObject::connect(&a, &Ed3kSession::eventPolled, context, [&a, data, root = h.root](const ::ed3k::Event& ev, bool* handled) {
        namespace event = ::ed3k::event;
        if (const auto* r = std::get_if<event::FileRequested>(&ev); r && r->root == root) {
            if (r->first)
                (void)a.engine()->queueSet(a.now(), r->node, 0, 0);
            *handled = true;
        } else if (const auto* w = std::get_if<event::WantBlock>(&ev); w && w->root == root) {
            auto buf = ::ed3k::Buffer::alloc(w->len);
            if (buf && w->offset + w->len <= std::uint64_t(data.size())) {
                std::memcpy(buf->bytes().data(), data.constData() + w->offset, w->len);
                (void)a.engine()->blockProvide(a.now(), w->id, std::move(*buf));
            } else {
                (void)a.engine()->blockFail(a.now(), w->id);
            }
            *handled = true;
        }
    });
}

} // namespace

class tst_Ed3kUsenetJoin : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void nzbBufferFromNzbInfo();
    void serverTagIsStableAndOpaque();
    void articlesFromTheQueueVerifyAClaimedMember();
    void blocksFromAnotherNodeFillTheHoles();
    void aFalseClaimDropsItsEd3kBytes();
    void cleanup();

private:
    /// The FakeQueue item of one file, @p parts articles of @p partSize; tempPath in @p dir.
    static std::unique_ptr<UsenetQueueItem> fakeItem(const QString& dir, qint64 size, int parts);
    /// Writes part @p part of @p data where the queue would, and reports it.
    static void landArticle(Ed3kUsenetJoin& join, const UsenetQueueItem& item, const QByteArray& data, int part,
                            int parts, int partSize);
};

void tst_Ed3kUsenetJoin::initTestCase()
{
    thePrefs.setUsenetHealthCheck(0);
}

void tst_Ed3kUsenetJoin::cleanup()
{
    QTest::qWait(10);
}

// usenet-join.md "The NZB buffer": per segment in NZB order raw number and mᵢ; a segment
// whose ID came out empty keeps its place with an empty mᵢ
void tst_Ed3kUsenetJoin::nzbBufferFromNzbInfo()
{
    const QByteArray xml = R"NZB(<?xml version="1.0" encoding="utf-8" ?>
<nzb xmlns="http://www.newzbin.com/DTD/2003/nzb">
  <head><meta type="name">x</meta></head>
  <file poster="p" date="1" subject="a.bin (1/3)">
    <groups><group>g</group></groups>
    <segments>
      <segment bytes="10" number="2">&lt;b2@x&gt;</segment>
      <segment bytes="10" number="1"> </segment>
      <segment bytes="10" number="3">c3@x</segment>
    </segments>
  </file>
  <file poster="p" date="1" subject="b.bin (1/1)">
    <groups><group>g</group></groups>
    <segments><segment bytes="10" number="1">d1@x</segment></segments>
  </file>
</nzb>
)NZB";
    NzbInfo nzb;
    QString error;
    QVERIFY2(NzbFile::parse(xml, nzb, error), qPrintable(error));
    QCOMPARE(nzb.files.size(), 2);

    const std::vector<::ed3k::NzbFileSegments> expected{
        {{"2", "b2@x"}, {"1", ""}, {"3", "c3@x"}},
        {{"1", "d1@x"}},
    };
    const auto segs = Ed3kUsenetJoin::nzbSegments(nzb);
    QCOMPARE(segs.size(), expected.size());
    for (std::size_t f = 0; f < segs.size(); ++f) {
        QCOMPARE(segs[f].size(), expected[f].size());
        for (std::size_t i = 0; i < segs[f].size(); ++i) {
            QCOMPARE(QString::fromStdString(segs[f][i].number), QString::fromStdString(expected[f][i].number));
            QCOMPARE(QString::fromStdString(segs[f][i].messageId), QString::fromStdString(expected[f][i].messageId));
        }
    }
    const auto raw = ::ed3k::detail::nzbBuffer(expected);
    const QByteArray want(reinterpret_cast<const char*>(raw.data()), qsizetype(raw.size()));
    QCOMPARE(Ed3kUsenetJoin::nzbBuffer(nzb).toHex(), want.toHex());
    // spelled out: u32 files = 2, u32 segments = 3, u32 1 "2", u32 4 "b2@x"
    const QByteArray head = QByteArray::fromHex("020000000300000001000000") + QByteArray("2")
                            + QByteArray::fromHex("04000000") + QByteArray("b2@x");
    QVERIFY(Ed3kUsenetJoin::nzbBuffer(nzb).startsWith(head));
    QVERIFY(!Ed3kUsenetJoin::metaRoot(nzb).has_value());
}

void tst_Ed3kUsenetJoin::serverTagIsStableAndOpaque()
{
    const QString key = QStringLiteral("news.example.com:563:user");
    QCOMPARE(Ed3kUsenetJoin::serverTag(key), Ed3kUsenetJoin::serverTag(key));
    QVERIFY(Ed3kUsenetJoin::serverTag(key) != Ed3kUsenetJoin::serverTag(key + QLatin1Char('x')));
    QCOMPARE(Ed3kUsenetJoin::serverTag(QString()), std::uint64_t(0));
}

// usenet-join-host.md loop steps 1, 2, 6: a real queue fetches from a fake news server;
// each articleDecoded goes to the join, the block is read from tempPath, the complete
// member is hashed and verified under the claimed root, then shared
void tst_Ed3kUsenetJoin::articlesFromTheQueueVerifyAClaimedMember()
{
    constexpr int kParts = 3;
    constexpr int kPart = 2500;
    const QByteArray whole = payload(kParts * kPart);
    const QString name = QStringLiteral("joined.bin");

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), kParts, 1, kParts);
    for (int p = 1; p <= kParts; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, kParts, kPart, name));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::ScopedStatistics stats;
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    NewsServer ns;
    ns.name = QStringLiteral("fake");
    ns.host = QStringLiteral("127.0.0.1");
    ns.port = port;
    ns.tlsMode = NntpTlsMode::None;
    ns.user = QStringLiteral("testuser");
    ns.pass = QStringLiteral("testpass");
    ns.maxConnections = 2;
    ns.accountId = QStringLiteral("acct-join");

    Ed3kSession session;
    QVERIFY(session.start(loopback(tmp.filePath(QStringLiteral("cfg")))));
    UsenetQueue queue;
    auto adapter = Ed3kUsenetJoin::queueAdapter(queue);
    Ed3kUsenetJoin join(session, *adapter, tmp.filePath(QStringLiteral("cfg")));
    join.setSearchEnabled(false);
    join.connectQueue(queue);
    join.start();
    QVERIFY(join.isStarted());

    queue.applyServers({ns}, 60);
    queue.start();
    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QSignalSpy verified(&join, &Ed3kUsenetJoin::memberVerified);
    QSignalSpy problems(&join, &Ed3kUsenetJoin::memberProblem);

    QString error;
    const QString id = queue.addNzb(makeNzb(name, kParts), QStringLiteral("joined"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    QVERIFY(join.hasJoin(id));
    // §4.2: an nzb-file record claims the root; before any article landed (async)
    const Hashed h = hashOf(whole);
    const ::ed3k::PublisherKey publisher{7};
    QVERIFY(join.fileClaim(id, 0, h.root, h.size, std::span{&publisher, 1}));

    QVERIFY(finished.wait(30000));
    QVERIFY(finished.at(0).at(1).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(verified.count(), 1, 20000);
    QCOMPARE(verified.at(0).at(0).toString(), id);
    QCOMPARE(problems.count(), 0);
    // finished: the join and its blob are dropped
    QTRY_VERIFY_WITH_TIMEOUT(!join.hasJoin(id), 5000);
    QVERIFY(!QFile::exists(join.joinPath(id)));

    join.stop();
    queue.stop();
    session.stop();
}

// usenet-join-host.md loop step 4: the member's holes come from another ed3k node.
// Articles of block 0 land first (they test the claim, §4.2), blocks 1 and 2 come from
// A; ACCEPT_RANGEs reach the queue merged; the member verifies
void tst_Ed3kUsenetJoin::blocksFromAnotherNodeFillTheHoles()
{
    constexpr int kParts = 3;
    constexpr int kPart = 200000;
    const QByteArray data = payload(int(2 * kBlock + 70000), 3);
    const Hashed h = hashOf(data);

    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    shareFrom(a, data, h, this);

    FakeQueue queue;
    queue.item = fakeItem(db.path(), data.size(), kParts);
    Ed3kUsenetJoin join(b, queue, db.path());
    join.setSearchEnabled(false);
    QSignalSpy verified(&join, &Ed3kUsenetJoin::memberVerified);
    join.start();
    QVERIFY(queue.hold);
    const QString id = queue.item->id;
    QVERIFY(join.hasJoin(id));
    QVERIFY(join.fileClaim(id, 0, h.root, h.size));
    // the download is open and bytes are missing: the file is kept unsealed
    QVERIFY(queue.hold(id, 0));

    landArticle(join, *queue.item, data, 1, kParts, kPart);
    landArticle(join, *queue.item, data, 2, kParts, kPart);
    join.addSource(id, 0, ::ed3k::Source{*a.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, a.udpPort()), std::nullopt, {}});

    QTRY_COMPARE_WITH_TIMEOUT(verified.count(), 1, 30000);
    QVERIFY(!queue.accepted.isEmpty());
    // the last call covers both ed3k blocks as one range: part 3 is wholly inside
    const QList<qint64> last = queue.accepted.last();
    QCOMPARE(last.at(0), qint64(0));
    QVERIFY(last.at(1) <= qint64(2 * kPart) && last.at(1) + last.at(2) == data.size());
    QVERIFY(queue.released > 0);
    QVERIFY(!queue.hold(id, 0));
    QCOMPARE(queue.refetched.size(), 0);
    QFile f(queue.item->files.at(0).tempPath);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), data);

    join.stop();
    QVERIFY(!queue.hold);
    a.stop();
    b.stop();
}

// §4.2 claim test: A shares another file under the claimed root; its blocks are written
// but held. The articles of block 0 do not pass under that root: CLAIM_FALSE, the ed3k
// ranges are fetched again from Usenet (no server excluded), a localized message
void tst_Ed3kUsenetJoin::aFalseClaimDropsItsEd3kBytes()
{
    constexpr int kParts = 3;
    constexpr int kPart = 200000;
    const QByteArray data = payload(int(2 * kBlock + 70000), 5);
    const QByteArray other = payload(int(data.size()), 9);
    const Hashed wrong = hashOf(other);

    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    shareFrom(a, other, wrong, this);

    FakeQueue queue;
    queue.item = fakeItem(db.path(), data.size(), kParts);
    Ed3kUsenetJoin join(b, queue, db.path());
    join.setSearchEnabled(false);
    QSignalSpy problems(&join, &Ed3kUsenetJoin::memberProblem);
    join.start();
    const QString id = queue.item->id;
    const ::ed3k::PublisherKey liar{9, 9, 9};
    QVERIFY(join.fileClaim(id, 0, wrong.root, wrong.size, std::span{&liar, 1}));
    join.addSource(id, 0, ::ed3k::Source{*a.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, a.udpPort()), std::nullopt, {}});

    // the last block of A's file is on disk (held: not accepted)
    const QString path = queue.item->files.at(0).tempPath;
    const auto tail = [&] {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) && f.seek(qint64(2 * kBlock)) ? f.readAll() : QByteArray();
    };
    QTRY_COMPARE_WITH_TIMEOUT(tail(), other.mid(int(2 * kBlock)), 30000);
    QCOMPARE(queue.accepted.size(), 0);

    landArticle(join, *queue.item, data, 1, kParts, kPart);
    landArticle(join, *queue.item, data, 2, kParts, kPart);
    QTRY_COMPARE_WITH_TIMEOUT(problems.count(), 1, 10000);
    QVERIFY(!queue.refetched.isEmpty());
    for (const QString& ex : std::as_const(queue.excluded))
        QVERIFY(ex.isEmpty());
    QCOMPARE(queue.accepted.size(), 0);
    // the member goes on from Usenet: its root is the articles' now, or none
    const auto m = join.member(id, 0);
    QVERIFY(m.has_value());
    QVERIFY(!m->root || *m->root != wrong.root);

    join.stop();
    a.stop();
    b.stop();
}

// ---------------------------------------------------------------------------
// private
// ---------------------------------------------------------------------------

std::unique_ptr<UsenetQueueItem> tst_Ed3kUsenetJoin::fakeItem(const QString& dir, qint64 size, int parts)
{
    auto item = std::make_unique<UsenetQueueItem>();
    item->id = QStringLiteral("item-1");
    item->name = QStringLiteral("fake");
    item->status = UsenetItemStatus::Downloading;
    QString error;
    if (!NzbFile::parse(makeNzb(QStringLiteral("f.bin"), parts), item->nzb, error))
        return item;
    UsenetFileState st;
    st.tempPath = QDir(dir).filePath(QStringLiteral("f.bin.usenetpart"));
    st.declaredSize = size;
    QFile f(st.tempPath);
    if (f.open(QIODevice::WriteOnly))
        f.resize(size);
    item->files.append(st);
    return item;
}

void tst_Ed3kUsenetJoin::landArticle(Ed3kUsenetJoin& join, const UsenetQueueItem& item, const QByteArray& data,
                                     int part, int parts, int partSize)
{
    const qint64 offset = qint64(part - 1) * partSize;
    const QByteArray chunk = data.mid(int(offset), partSize);
    QFile f(item.files.at(0).tempPath);
    QVERIFY(f.open(QIODevice::ReadWrite));
    QVERIFY(f.seek(offset));
    QCOMPARE(f.write(chunk), qint64(chunk.size()));
    f.close();
    join.onArticleDecoded(item.id, 0, offset, chunk.size(), true, part, beginLine(part, parts, data.size(), QStringLiteral("f.bin")),
                          QStringLiteral("news:563:u"));
}

QTEST_GUILESS_MAIN(tst_Ed3kUsenetJoin)
#include "tst_Ed3kUsenetJoin.moc"
