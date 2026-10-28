/// @file tst_Ed3kSets.cpp
/// @brief Followed ed3k sets (src/ed3k/Ed3kSets) and their IPC (Ed3kSetsBridge): node A
/// publishes a signed set that changes (setPublish with a seed, as the joins publish theirs),
/// node B holds the records; node C follows it by its link and downloads every member into the
/// set's folder, node D only the part it chose; a new version under "ask" waits for the user,
/// under "download" comes at once, both across a restart. Also the website server's answers
/// for a set not held (name.md §6.3: wait, 504, 503) and a set that is no website (§6.1).

#include "Ed3kDownloads.h"
#include "Ed3kNames.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kSetManifest.h"
#include "Ed3kSets.h"
#include "Ed3kSetsBridge.h"
#include "Ed3kShared.h"
#include "Ed3kSiteServer.h"
#include "Ed3kStore.h"

#include "IpcMessage.h"

#include <QCborArray>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <openssl/evp.h>

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
    return s;
}

::ed3k::Source sourceOf(const Ed3kSession& s)
{
    return ::ed3k::Source{*s.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, s.udpPort()), std::nullopt, {}};
}

::ed3k::Node nodeOf(const Ed3kSession& s)
{
    return ::ed3k::Node{*s.nodeId(), ::ed3k::Address::v4(127, 0, 0, 1, s.udpPort())};
}

QByteArray payload(int size, int seed)
{
    QByteArray data(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 13 + (i >> 8) * 3 + seed * 41) & 0xFF);
    return data;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

::ed3k::Root rootOf(const QByteArray& data)
{
    ::ed3k::Hasher h;
    (void)h.update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())});
    auto done = h.finish();
    return done ? done->root : ::ed3k::Root{};
}

QByteArray raw(std::span<const std::uint8_t> b)
{
    return QByteArray(reinterpret_cast<const char*>(b.data()), qsizetype(b.size()));
}

// -- protobuf by hand: a manifest signed by its author (file-set.md §1, §2.4) ------

void varint(QByteArray& out, std::uint64_t v)
{
    while (v >= 0x80) {
        out.append(char((v & 0x7F) | 0x80));
        v >>= 7;
    }
    out.append(char(v));
}

void bytesField(QByteArray& out, int field, const QByteArray& b)
{
    varint(out, std::uint64_t(field) << 3 | 2);
    varint(out, std::uint64_t(b.size()));
    out += b;
}

void varintField(QByteArray& out, int field, std::uint64_t v)
{
    varint(out, std::uint64_t(field) << 3);
    varint(out, v);
}

struct MemberSpec {
    QString name;
    std::uint32_t dir = 0;   // 0 top, else 1 + index into dirs
    QByteArray data;
};

/// An Ed25519 key of a seed (OpenSSL): public key and signatures.
struct Author {
    ::ed3k::Key32 seed{};

    QByteArray publicKey() const
    {
        EVP_PKEY* k = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
        QByteArray out(32, 0);
        std::size_t len = 32;
        EVP_PKEY_get_raw_public_key(k, reinterpret_cast<unsigned char*>(out.data()), &len);
        EVP_PKEY_free(k);
        return out;
    }
    QByteArray sign(const QByteArray& msg) const
    {
        EVP_PKEY* k = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size());
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        QByteArray sig(64, 0);
        std::size_t len = 64;
        EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, k);
        EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(sig.data()), &len, reinterpret_cast<const unsigned char*>(msg.constData()),
                       std::size_t(msg.size()));
        EVP_MD_CTX_free(ctx);
        EVP_PKEY_free(k);
        return sig;
    }
    ::ed3k::Key32 key() const
    {
        ::ed3k::Key32 k{};
        const QByteArray p = publicKey();
        std::memcpy(k.data(), p.constData(), k.size());
        return k;
    }
};

/// "ed3kset" ‖ 0x01 ‖ SetFile{body = FileSet{title, dirs, files, id, seq}, key, sigs}
QByteArray signedManifest(const Author& a, const QString& title, const QStringList& dirs, const std::vector<MemberSpec>& members,
                          const QByteArray& id, std::uint64_t seq)
{
    QByteArray set;
    bytesField(set, 1, title.toUtf8());
    for (const QString& d : dirs)
        bytesField(set, 2, d.toUtf8());
    for (const MemberSpec& m : members) {
        QByteArray member;
        bytesField(member, 1, raw(rootOf(m.data).bytes));
        varintField(member, 2, std::uint64_t(m.data.size()));
        bytesField(member, 3, m.name.toUtf8());
        if (m.dir != 0)
            varintField(member, 4, m.dir);
        bytesField(set, 3, member);
    }
    bytesField(set, 6, id);
    varintField(set, 7, seq);
    QByteArray sig;
    varintField(sig, 1, 1);   // Ed25519
    bytesField(sig, 2, a.sign(QByteArray("ed3k v1 set manifest") + set));
    QByteArray file("ed3kset\x01", 8);
    bytesField(file, 1, set);
    bytesField(file, 2, a.publicKey());
    bytesField(file, 3, sig);
    return file;
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

struct Http {
    int status = 0;
    QByteArray headers;
    QByteArray body;
};

/// One HTTP/1.1 request to 127.0.0.1:@p port; the raw body (chunks not decoded) up to
/// Content-Length, the end of the connection, @p until found in it, or the timeout.
Http request(std::uint16_t port, const QByteArray& host, const QByteArray& path, const QByteArray& extra, int timeoutMs = 30000,
             const QByteArray& until = {})
{
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, port);
    if (!s.waitForConnected(5000))
        return {};
    s.write("GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\n" + extra + "Connection: close\r\n\r\n");
    QByteArray rawIn;
    Http out;
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired()) {
        QTest::qWait(20);
        rawIn += s.readAll();
        const qsizetype end = rawIn.indexOf("\r\n\r\n");
        if (end >= 0) {
            out.headers = rawIn.left(end);
            out.status = rawIn.mid(9, 3).toInt();
            out.body = rawIn.mid(end + 4);
            qsizetype length = -1;
            for (const QByteArray& line : out.headers.split('\n'))
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(15).trimmed().toLongLong();
            if (length >= 0 && out.body.size() >= length)
                return out;
            if (!until.isEmpty() && out.body.contains(until))
                return out;
        }
        if (s.state() == QAbstractSocket::UnconnectedState)
            break;
    }
    return out;
}

const QByteArray kUser = "Sec-Fetch-Site: none\r\nSec-Fetch-Mode: navigate\r\nSec-Fetch-User: ?1\r\n";
const QByteArray kSameOrigin = "Sec-Fetch-Site: same-origin\r\n";

/// A follower: its node, downloads and sets, in a directory kept across restarts.
struct Follower {
    QString dir;
    std::unique_ptr<Ed3kSession> session;
    std::unique_ptr<Ed3kDownloads> downloads;
    std::unique_ptr<Ed3kSets> sets;
    ::ed3k::Source seeder;

    bool start(const QString& d, const ::ed3k::Node& known, const ::ed3k::Source& from)
    {
        dir = d;
        seeder = from;
        session = std::make_unique<Ed3kSession>();
        if (!session->start(loopback(dir)))
            return false;
        session->post([known](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&known, 1}); });
        downloads = std::make_unique<Ed3kDownloads>(*session, dir);
        // Kad sources as in use (A publishes them at B, also for names without a keyword:
        // Ed3kJoinBase::publishShared); the members of one source take turns at it
        // (Ed3kJoinBase's turns, Protocol.md §12.5)
        downloads->setSourceResearch(3000, 2000);
        downloads->setDirs(QDir(dir).filePath(QStringLiteral("temp")), incoming());
        downloads->start();
        // was: the publisher by hand at once (the members' SOURCE records were not published:
        // "a.bin" has no keyword, and Publish::Both failed before the source record)
        // const auto feed = [this] {
        //     for (const auto& d : downloads->list())
        //         if (d.state == Ed3kDownloadInfo::State::Downloading)
        //             downloads->addSourceTo(d.root, seeder);
        // };
        // QObject::connect(downloads.get(), &Ed3kDownloads::added, downloads.get(), feed);
        // QObject::connect(downloads.get(), &Ed3kDownloads::changed, downloads.get(), feed);
        sets = std::make_unique<Ed3kSets>(*session, *downloads, dir);
        sets->setIncomingDir(incoming());
        return true;
    }
    void stop()
    {
        sets.reset();
        if (downloads)
            downloads->stop();
        downloads.reset();
        if (session)
            session->stop();
        session.reset();
    }
    QString incoming() const { return QDir(dir).filePath(QStringLiteral("incoming")); }
};

} // namespace

class tst_Ed3kSets : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void aManifestIsReadWithLocalNames();
    void linksAreCheckedBeforeFollowing();
    void aFollowedSetIsDownloadedWhole();
    void aChosenPartIsDownloaded();
    void aNewVersionComesByPolicyAcrossARestart();
    void theIpcListsAndFollows();
    void aSetNotHeldWaitsThen504Or503();
    void aSetThatIsNoWebsiteIsOffered();
    void cleanupTestCase();
    void cleanup() { QTest::qWait(10); }

private:
    bool publish(const QByteArray& manifest);

    QTemporaryDir m_da, m_db, m_dc, m_dd;
    Ed3kSession m_a, m_b;
    FakeSource m_src;
    std::unique_ptr<Ed3kShared> m_shared;
    Author m_author;
    const QByteArray m_id = "season";
    std::vector<MemberSpec> m_members;   // a.bin, sub/b.bin, sub/c.bin (v2), d.bin (v3)
    QByteArray m_v1, m_v2, m_v3;
    QString m_link;
    Follower m_c;
    std::uint32_t m_cSet = 0;
};

void tst_Ed3kSets::initTestCase()
{
    QVERIFY(m_da.isValid() && m_db.isValid() && m_dc.isValid() && m_dd.isValid());
    m_author.seed.fill(0x21);
    m_members = {
        {QStringLiteral("a.bin"), 0, payload(300 * 1024 + 5, 1)},
        {QStringLiteral("b.bin"), 1, payload(200 * 1024 + 9, 2)},
        {QStringLiteral("c.bin"), 1, payload(100 * 1024 + 3, 3)},
        {QStringLiteral("d.bin"), 0, payload(50 * 1024 + 1, 4)},
    };
    const QStringList dirs{QStringLiteral("sub")};
    m_v1 = signedManifest(m_author, QStringLiteral("Season one"), dirs, {m_members[0], m_members[1]}, m_id, 1);
    m_v2 = signedManifest(m_author, QStringLiteral("Season one"), dirs, {m_members[0], m_members[1], m_members[2]}, m_id, 2);
    m_v3 = signedManifest(m_author, QStringLiteral("Season one"), dirs, m_members, m_id, 3);
    QVERIFY(Ed3kSetManifest::parse(m_v1));

    QVERIFY(m_a.start(loopback(m_da.path())));
    QVERIFY(m_b.start(loopback(m_db.path())));
    const ::ed3k::Node bNode = nodeOf(m_b);
    m_a.post([bNode](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&bNode, 1}); });
    QTRY_VERIFY_WITH_TIMEOUT(m_a.contactCount() > 0, 20000);

    // A shares the members and the three manifests (eMule's share list, faked)
    int n = 0;
    const auto addFile = [&](const QString& name, const QByteArray& data) {
        const QString path = QDir(m_da.path()).filePath(QStringLiteral("share/") + name);
        QVERIFY(writeFile(path, data));
        m_src.entries.push_back(SharedEntry{QByteArray(16, char(0x30 + n++)), path, name, std::uint64_t(data.size())});
    };
    for (const auto& m : m_members)
        addFile(m.name, m.data);
    addFile(QStringLiteral("v1.ed3kset"), m_v1);
    addFile(QStringLiteral("v2.ed3kset"), m_v2);
    addFile(QStringLiteral("v3.ed3kset"), m_v3);
    m_shared = std::make_unique<Ed3kShared>(m_a, m_src, m_da.path());
    QSignalSpy shared(m_shared.get(), &Ed3kShared::fileShared);
    m_shared->start();
    QTRY_COMPARE_WITH_TIMEOUT(shared.count(), int(m_src.entries.size()), 30000);
    QVERIFY(publish(m_v1));
    m_link = Ed3kSets::setLink(m_author.key(), m_id, QStringLiteral("Season one"));
}

void tst_Ed3kSets::cleanupTestCase()
{
    m_c.stop();
    if (m_shared)
        m_shared->stop();
    m_shared.reset();
    m_a.stop();
    m_b.stop();
}

bool tst_Ed3kSets::publish(const QByteArray& manifest)
{
    const ::ed3k::Root root = rootOf(manifest);
    bool ok = false;
    const ::ed3k::Key32 seed = m_author.seed;
    m_a.post([&](::ed3k::Engine& e, ::ed3k::Now now) {
        const auto r = e.setPublish(now, {reinterpret_cast<const std::byte*>(manifest.constData()), std::size_t(manifest.size())}, root,
                                    "Season one.ed3kset", ::ed3k::SetSigner::Seed, &seed);
        ok = r.has_value() && r->has_value();
    });
    return ok;
}

// file-set.md §1: the reader of the GUI; §2.2: local names
void tst_Ed3kSets::aManifestIsReadWithLocalNames()
{
    const auto m = Ed3kSetManifest::parse(m_v2);
    QVERIFY(m);
    QCOMPARE(m->title, QStringLiteral("Season one"));
    QCOMPARE(m->id, m_id);
    QCOMPARE(m->seq, std::uint64_t(2));
    QCOMPARE(m->key, m_author.publicKey());
    QCOMPARE(m->files.size(), std::size_t(3));
    QCOMPARE(m->files[1].path, QStringLiteral("sub/b.bin"));
    QCOMPARE(m->files[1].size, std::uint64_t(m_members[1].data.size()));
    QVERIFY(m->files[1].root == rootOf(m_members[1].data));
    QVERIFY(!Ed3kSetManifest::parse("ed3kset\x02xx"));
    QVERIFY(!Ed3kSetManifest::parse(m_v2.left(m_v2.size() - 7)));

    QCOMPARE(ed3kSafeName(QStringLiteral("a:b?.")), QStringLiteral("a_b_"));
    QCOMPARE(ed3kSafeName(QStringLiteral("CON.txt")), QStringLiteral("_CON.txt"));
    QCOMPARE(ed3kSafeName(QStringLiteral("..")), QStringLiteral("_"));
    QCOMPARE(ed3kSetLocalPath(QStringLiteral("/x/set"), QStringLiteral("sub/b.bin")), QStringLiteral("/x/set/sub/b.bin"));
    QCOMPARE(ed3kSetLocalPath(QStringLiteral("/x/set"), QStringLiteral("../../etc/passwd")), QStringLiteral("/x/set/_/_/etc/passwd"));
}

// file-set.md §5.4: key 32 bytes, id 1 to 32, not with s; repeats refused
void tst_Ed3kSets::linksAreCheckedBeforeFollowing()
{
    QTemporaryDir d;
    Ed3kSession off;
    Ed3kDownloads dl(off, d.path());
    Ed3kSets sets(off, dl, d.path());
    QVERIFY(!sets.follow(QStringLiteral("ed3k:set?key=abc&id=aa"), Ed3kSets::Policy::Ask));
    QVERIFY(!sets.follow(m_link + QStringLiteral("&id=aaaa"), Ed3kSets::Policy::Ask));
    QVERIFY(!sets.follow(m_link + QStringLiteral("&s=aaaa"), Ed3kSets::Policy::Ask));
    QVERIFY(!sets.follow(QStringLiteral("ed3k:file?b3=x"), Ed3kSets::Policy::Ask));
    const auto id = sets.follow(m_link, Ed3kSets::Policy::Ask);
    QVERIFY(id);
    // once; the policy given again holds
    QCOMPARE(*sets.follow(m_link, Ed3kSets::Policy::Download), *id);
    QCOMPARE(sets.info(*id)->policy, Ed3kSets::Policy::Download);
    QCOMPARE(sets.info(*id)->title, QStringLiteral("Season one"));
    QVERIFY(!sets.info(*id)->followed);   // no node
    // hidden: kept, its link never shown
    const auto hidden = sets.follow(QStringLiteral("ed3k:set?key=%1&s=%2").arg(base32(m_author.key()), base32(m_author.key())),
                                    Ed3kSets::Policy::Ask);
    QVERIFY(hidden);
    QVERIFY(sets.info(*hidden)->hidden);
    QVERIFY(sets.info(*hidden)->link.isEmpty());
    // kept owner-only (the hidden link is a credential)
    QVERIFY(QFile::exists(sets.statePath()));
#ifndef Q_OS_WIN
    QCOMPARE(QFile::permissions(sets.statePath()) & (QFile::ReadGroup | QFile::ReadOther), QFileDevice::Permissions{});
#endif
    QVERIFY(sets.unfollow(*hidden));
    QVERIFY(!sets.info(*hidden));
}

// file-set.md §5.3: followed by its link; every member downloaded below the set's folder
void tst_Ed3kSets::aFollowedSetIsDownloadedWhole()
{
    QVERIFY(m_c.start(m_dc.path(), nodeOf(m_b), sourceOf(m_a)));
    QTRY_VERIFY_WITH_TIMEOUT(m_c.session->contactCount() > 0, 20000);
    const auto id = m_c.sets->follow(m_link, Ed3kSets::Policy::Download);
    QVERIFY(id);
    m_cSet = *id;
    QTRY_VERIFY_WITH_TIMEOUT(m_c.sets->info(m_cSet)->followed, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(m_c.sets->info(m_cSet)->seq == std::uint64_t(1), 90000);
    const auto info = m_c.sets->info(m_cSet);
    QCOMPARE(info->members.size(), std::size_t(2));
    QCOMPARE(info->dir, QDir(m_c.incoming()).filePath(QStringLiteral("Season one")));
    const QString a = QDir(info->dir).filePath(QStringLiteral("a.bin"));
    const QString b = QDir(info->dir).filePath(QStringLiteral("sub/b.bin"));
    QTRY_VERIFY_WITH_TIMEOUT(readFile(a) == m_members[0].data && readFile(b) == m_members[1].data, 90000);
    QTRY_VERIFY_WITH_TIMEOUT(std::ranges::all_of(m_c.sets->info(m_cSet)->members,
                                                 [](const auto& m) { return m.state == Ed3kSets::MemberState::Complete; }),
                             10000);
    // the downloads are the set's
    QCOMPARE(m_c.downloads->info(rootOf(m_members[0].data))->owner, Ed3kSets::ownerOf(m_cSet));
}

// file-set.md §2.3: the user chooses members before the downloads start
void tst_Ed3kSets::aChosenPartIsDownloaded()
{
    Follower d;
    QVERIFY(d.start(m_dd.path(), nodeOf(m_b), sourceOf(m_a)));
    QTRY_VERIFY_WITH_TIMEOUT(d.session->contactCount() > 0, 20000);
    QSignalSpy asked(d.sets.get(), &Ed3kSets::newVersion);
    const auto id = d.sets->follow(m_link, Ed3kSets::Policy::Ask);
    QVERIFY(id);
    QTRY_COMPARE_WITH_TIMEOUT(asked.count(), 1, 90000);
    auto info = d.sets->info(*id);
    QVERIFY(!info->seq);
    QCOMPARE(info->pendingSeq, std::optional<std::uint64_t>(1));
    QCOMPARE(info->members.size(), std::size_t(2));
    QCOMPARE(d.downloads->list().size(), std::size_t(0));   // nothing before the user's word
    // the folder "sub" left out
    QVERIFY(d.sets->setSkipped(*id, {QStringLiteral("sub")}));
    QVERIFY(d.sets->applyVersion(*id));
    const QString a = QDir(d.sets->info(*id)->dir).filePath(QStringLiteral("a.bin"));
    const QString b = QDir(d.sets->info(*id)->dir).filePath(QStringLiteral("sub/b.bin"));
    QTRY_VERIFY_WITH_TIMEOUT(readFile(a) == m_members[0].data, 90000);
    QVERIFY(!QFile::exists(b));
    QCOMPARE(d.downloads->list().size(), std::size_t(1));
    info = d.sets->info(*id);
    QCOMPARE(info->seq, std::optional<std::uint64_t>(1));
    const auto skipped = std::ranges::find_if(info->members, [](const auto& m) { return m.path == QLatin1String("sub/b.bin"); });
    QVERIFY(skipped != info->members.end() && skipped->skipped && skipped->state == Ed3kSets::MemberState::Skipped);
    // taken in again
    QVERIFY(d.sets->setSkipped(*id, {}));
    QVERIFY(d.sets->applyVersion(*id));
    QTRY_VERIFY_WITH_TIMEOUT(readFile(b) == m_members[1].data, 90000);
    d.stop();
}

// §5.3: ask keeps a new version until the user takes it; download takes it at once.
// Across restarts: the followed set, its version and policy come back from sets.json
void tst_Ed3kSets::aNewVersionComesByPolicyAcrossARestart()
{
    QVERIFY(m_cSet != 0);
    QVERIFY(m_c.sets->setPolicy(m_cSet, Ed3kSets::Policy::Ask));
    QVERIFY(publish(m_v2));
    m_c.stop();
    // A's connection to C's run before times out first (a new one of the same node is closed
    // meanwhile, and an unavailable manifest is tried again only after 30 min)
    QTest::qWait(35000);
    QVERIFY(m_c.start(m_dc.path(), nodeOf(m_b), sourceOf(m_a)));
    auto info = m_c.sets->info(m_cSet);
    QVERIFY(!info->followed || m_c.session->contactCount() > 0);   // followed once there are contacts
    QVERIFY(info);
    QCOMPARE(info->seq, std::optional<std::uint64_t>(1));
    QCOMPARE(info->policy, Ed3kSets::Policy::Ask);
    QSignalSpy asked(m_c.sets.get(), &Ed3kSets::newVersion);
    // followed again with version 1 held: the head of version 2 is newer
    QTRY_COMPARE_WITH_TIMEOUT(asked.count(), 1, 90000);
    QCOMPARE(asked.first().at(2).toULongLong(), 2ull);
    QCOMPARE(asked.first().at(3).toStringList(), QStringList{QStringLiteral("sub/c.bin")});
    info = m_c.sets->info(m_cSet);
    QCOMPARE(info->pendingSeq, std::optional<std::uint64_t>(2));
    const QString c = QDir(info->dir).filePath(QStringLiteral("sub/c.bin"));
    QTest::qWait(1000);
    QVERIFY(!QFile::exists(c));
    QVERIFY(m_c.sets->applyVersion(m_cSet));
    QTRY_VERIFY_WITH_TIMEOUT(readFile(c) == m_members[2].data, 90000);
    QCOMPARE(m_c.sets->info(m_cSet)->seq, std::optional<std::uint64_t>(2));

    // download: version 3 comes without a question
    QVERIFY(m_c.sets->setPolicy(m_cSet, Ed3kSets::Policy::Download));
    QVERIFY(publish(m_v3));
    m_c.stop();
    // A's connection to C's run before times out first (a new one of the same node is closed
    // meanwhile, and an unavailable manifest is tried again only after 30 min)
    QTest::qWait(35000);
    QVERIFY(m_c.start(m_dc.path(), nodeOf(m_b), sourceOf(m_a)));
    QSignalSpy asked3(m_c.sets.get(), &Ed3kSets::newVersion);
    const QString d = QDir(info->dir).filePath(QStringLiteral("d.bin"));
    QTRY_VERIFY_WITH_TIMEOUT(readFile(d) == m_members[3].data, 90000);
    QCOMPARE(m_c.sets->info(m_cSet)->seq, std::optional<std::uint64_t>(3));
    QCOMPARE(asked3.count(), 0);
    // the members of before were not downloaded again (nothing overwritten, §5.3)
    QVERIFY(!m_c.downloads->contains(rootOf(m_members[0].data)) || m_c.downloads->info(rootOf(m_members[0].data))->state
                                                                       == Ed3kDownloadInfo::State::Complete);
    QVERIFY(!QFile::exists(QDir(info->dir).filePath(QStringLiteral("a (1).bin"))));
}

void tst_Ed3kSets::theIpcListsAndFollows()
{
    QVERIFY(m_c.sets);
    Ed3kSetsBridge::setSets(m_c.sets.get(), nullptr, nullptr);
    std::vector<Ipc::IpcMessage> replies;
    const auto reply = [&](const Ipc::IpcMessage& m) { replies.push_back(m); };

    Ipc::IpcMessage list(Ipc::IpcMsgType::GetEd3kSets, 1);
    QVERIFY(Ed3kSetsBridge::handleRequest(list, reply));
    QVERIFY(replies.back().fieldBool(0));
    const QCborArray sets = replies.back().fieldMap(1).value(QStringLiteral("sets")).toArray();
    QCOMPARE(sets.size(), 1);
    const QCborMap s = sets.at(0).toMap();
    QCOMPARE(s.value(QStringLiteral("id")).toInteger(), qint64(m_cSet));
    QCOMPARE(s.value(QStringLiteral("title")).toString(), QStringLiteral("Season one"));
    QCOMPARE(s.value(QStringLiteral("seq")).toInteger(), 3);
    QCOMPARE(s.value(QStringLiteral("members")).toArray().size(), 4);
    QCOMPARE(s.value(QStringLiteral("author")).toString(), QString::fromLatin1(m_author.publicKey().toHex()));

    Ipc::IpcMessage bad(Ipc::IpcMsgType::FollowEd3kSet, 2);
    bad.append(QStringLiteral("ed3k:set?nothing"));
    bad.append(qint64(0));
    QVERIFY(Ed3kSetsBridge::handleRequest(bad, reply));
    QVERIFY(!replies.back().fieldBool(0));
    QVERIFY(!replies.back().fieldMap(1).value(QStringLiteral("error")).toString().isEmpty());

    Ipc::IpcMessage policy(Ipc::IpcMsgType::SetEd3kSetPolicy, 3);
    policy.append(qint64(m_cSet));
    policy.append(qint64(0));
    QVERIFY(Ed3kSetsBridge::handleRequest(policy, reply));
    QVERIFY(replies.back().fieldBool(0));
    QCOMPARE(m_c.sets->info(m_cSet)->policy, Ed3kSets::Policy::Ask);

    // live without a live object: refused, not crashed
    Ipc::IpcMessage watch(Ipc::IpcMsgType::WatchEd3kLive, 4);
    watch.append(QStringLiteral("ed3k:live?key=x&id=y"));
    QVERIFY(Ed3kSetsBridge::handleRequest(watch, reply));
    QVERIFY(!replies.back().fieldBool(0));
    // 1144 is ours, 1140 the downloads'
    QVERIFY(Ed3kSetsBridge::isSetsRequest(Ipc::IpcMsgType::GetEd3kSetSite));
    QVERIFY(!Ed3kSetsBridge::isSetsRequest(Ipc::IpcMsgType::GetEd3kDownloads));
    Ed3kSetsBridge::setSets(nullptr, nullptr, nullptr);
}

// name.md §6.3 (batch 11 ruling #9): a set not held and not fetched is 503; one fetched for the
// user waits for its manifest (served when it comes), else 504
void tst_Ed3kSets::aSetNotHeldWaitsThen504Or503()
{
    // the core's helper itself
    QCOMPARE(::ed3k::siteSetStatus(true, std::nullopt, 1000)->status, 0);
    QCOMPARE(::ed3k::siteSetStatus(true, std::nullopt, 1000)->waitS, 0u);
    QCOMPARE(::ed3k::siteSetStatus(false, std::nullopt, 1000)->status, 503);
    QCOMPARE(::ed3k::siteSetStatus(false, 1000, 1010)->status, 0);
    QVERIFY(::ed3k::siteSetStatus(false, 1000, 1010)->waitS > 0);
    QCOMPARE(::ed3k::siteSetStatus(false, 1000, 1000 + ED3K_SITE_SET_WAIT_S)->status, 504);

    QTemporaryDir dir;
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kNames names(s, dir.path());
    names.setSearchEnabled(false);
    names.start();
    Ed3kSiteServer sites(s, names);
    sites.setWaitMs(1500);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    const std::uint16_t port = sites.port();
    ::ed3k::Key32 key{};
    key.fill(9);
    const auto hostOf = [&](const QByteArray& id) { return QUrl(sites.originUrl(key, id)).host().toLatin1() + ':' + QByteArray::number(port); };

    // not held, not fetched: 503 (was 404)
    QCOMPARE(request(port, hostOf("one"), "/", kSameOrigin).status, 503);
    // fetched for the user: waits, then 504 (the local wait here is 1.5 s)
    QElapsedTimer t;
    t.start();
    QCOMPARE(request(port, hostOf("two"), "/", kUser).status, 504);
    QVERIFY(t.elapsed() >= 1000);
    // fetched for longer than ED3K_SITE_SET_WAIT_S: 504 at once, also for a page's own request
    sites.setFetchingSince(key, "two", QDateTime::currentSecsSinceEpoch() - ED3K_SITE_SET_WAIT_S - 1);
    t.restart();
    QCOMPARE(request(port, hostOf("two"), "/", kSameOrigin).status, 504);
    QVERIFY(t.elapsed() < 1000);
    // the manifest comes while it waits: answered from it (a set without index.html: 404, §6.1)
    t.restart();
    QTimer::singleShot(300, &names, [&] { names.setSiteManifest(key, "three", m_v1); });
    QCOMPARE(request(port, hostOf("three"), "/", kUser).status, 404);
    QVERIFY(t.elapsed() >= 250 && t.elapsed() < 1400);
    names.stop();
    s.stop();
}

// name.md §6.1: a set without index.html is shown as a set, for download: offered to the GUI
void tst_Ed3kSets::aSetThatIsNoWebsiteIsOffered()
{
    QTemporaryDir dir;
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kNames names(s, dir.path());
    names.setSearchEnabled(false);
    names.start();
    Ed3kSiteServer sites(s, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    names.setSiteManifest(m_author.key(), m_id, m_v1);
    QSignalSpy offered(&sites, &Ed3kSiteServer::setOffered);
    const QByteArray host = QUrl(sites.originUrl(m_author.key(), m_id)).host().toLatin1() + ':' + QByteArray::number(sites.port());
    QCOMPARE(request(sites.port(), host, "/", kUser).status, 404);
    QCOMPARE(offered.count(), 1);
    const QString link = offered.first().at(0).toString();
    QVERIFY(link.startsWith(QStringLiteral("ed3k:set?key=") + base32(m_author.key())));
    // a page's own request offers nothing
    QCOMPARE(request(sites.port(), host, "/", kSameOrigin).status, 404);
    QCOMPARE(offered.count(), 1);
    names.stop();
    s.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kSets)
#include "tst_Ed3kSets.moc"
