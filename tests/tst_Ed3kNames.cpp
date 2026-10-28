/// @file tst_Ed3kNames.cpp
/// @brief Readable names (src/ed3k/Ed3kNames) and the local website server
/// (src/ed3k/Ed3kSiteServer): the DNS duty through a fake resolver, a web name that leads to
/// a set whose page node A serves and node B's website server sends over loopback HTTP, the
/// gate (421, 405, 403), the redirect of a readable host, and the IPC (Ed3kNamesBridge).
/// Range requests (206/416) from verified blocks; websites kept on disk across restarts (LRU).

#include "Ed3kBridge.h"
#include "Ed3kJoinBase.h"
#include "Ed3kNames.h"
#include "Ed3kNamesBridge.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kSiteCache.h"
#include "Ed3kSiteServer.h"
#include "Ed3kStore.h"

#include "IpcProtocol.h"

#include <QCborArray>
#include <QDeadlineTimer>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;
using namespace eMule::ed3k;
using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

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

::ed3k::Source sourceOf(const Ed3kSession& s)
{
    return ::ed3k::Source{*s.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, s.udpPort()), std::nullopt, {}};
}

::ed3k::Root rootOf(const QByteArray& data)
{
    ::ed3k::Hasher h;
    (void)h.update({reinterpret_cast<const std::byte*>(data.constData()), std::size_t(data.size())});
    auto done = h.finish();
    return done ? done->root : ::ed3k::Root{};
}

// -- protobuf by hand: an unsigned manifest (file-set.md §2.4) ------------------

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

/// "ed3kset" ‖ 0x01 ‖ SetFile{body = FileSet{title, files: [Member{root, size, name}]}}
QByteArray manifestOf(const QString& name, const QByteArray& data)
{
    QByteArray member;
    const auto root = rootOf(data);
    bytesField(member, 1, Ed3kNames::toBytes(root.bytes));
    varint(member, 2 << 3 | 0);
    varint(member, std::uint64_t(data.size()));
    bytesField(member, 3, name.toUtf8());
    QByteArray set;
    bytesField(set, 1, "site");
    bytesField(set, 3, member);
    QByteArray file("ed3kset\x01", 8);
    bytesField(file, 1, set);
    return file;
}

/// Node A's side: serves blobs from memory.
class Sharer final : public Ed3kJoinBase {
public:
    Sharer(Ed3kSession& s, const QString& dir) : Ed3kJoinBase(s, dir, QStringLiteral("share"), nullptr) {}
    void start() override { beginStart(); }
    void stop() override { closeAll(); }
    std::optional<::ed3k::Root> share(const QByteArray& data) { return shareBlob(data, QStringLiteral("index.html"), std::nullopt); }

protected:
    std::vector<std::uint64_t> wantedBlocks(const Download&) const override { return {}; }
    void onFileBlock(const Download&, const ::ed3k::event::Block&) override {}
    void onBlobFetched(const QString&, int, const ::ed3k::Root&, const QByteArray&) override {}
    std::optional<FileRead> sharedRead(const Shared&, std::uint64_t, std::uint32_t) const override { return std::nullopt; }
    bool hasOwner(const QString&) const override { return false; }
    void saveOwner(const QString&) override {}
};

/// A TXT resolver by hand: answers @p records for every question, asynchronously.
struct FakeDns {
    QStringList asked;
    std::vector<std::string> records;
    bool secure = false;
    bool fail = false;

    Ed3kNames::DnsResolver resolver()
    {
        return [this](const QString& qname, Ed3kNames::DnsDone done) {
            asked << qname;
            QTimer::singleShot(0, [this, done = std::move(done)] {
                if (fail)
                    done(std::nullopt);
                else
                    done(Ed3kNames::DnsAnswer{records, 60, secure});
            });
        };
    }
};

struct Http {
    int status = 0;
    QByteArray headers;
    QByteArray body;
};

/// One HTTP/1.1 request to 127.0.0.1:@p port with @p host as Host.
Http request(std::uint16_t port, const QByteArray& host, const QByteArray& path, const QByteArray& extra,
             const QByteArray& method = "GET", int timeoutMs = 60000)
{
    QTcpSocket s;
    s.connectToHost(QHostAddress::LocalHost, port);
    if (!s.waitForConnected(5000))
        return {};
    s.write(method + ' ' + path + " HTTP/1.1\r\nHost: " + host + "\r\n" + extra + "Connection: close\r\n\r\n");
    QByteArray raw;
    Http out;
    QDeadlineTimer deadline(timeoutMs);
    while (!deadline.hasExpired()) {
        QTest::qWait(20);
        raw += s.readAll();
        const qsizetype end = raw.indexOf("\r\n\r\n");
        if (end < 0) {
            if (s.state() == QAbstractSocket::UnconnectedState)
                break;
            continue;
        }
        out.headers = raw.left(end);
        out.status = raw.mid(9, 3).toInt();
        qsizetype length = 0;
        for (const QByteArray& line : out.headers.split('\n'))
            if (line.toLower().startsWith("content-length:"))
                length = line.mid(15).trimmed().toLongLong();
        out.body = raw.mid(end + 4);
        if (method == "HEAD" || out.body.size() >= length)
            return out;
        if (s.state() == QAbstractSocket::UnconnectedState)
            break;
    }
    return out;
}

const QByteArray kUser = "Sec-Fetch-Site: none\r\nSec-Fetch-Mode: navigate\r\nSec-Fetch-User: ?1\r\n";

QString setLink(const ::ed3k::Key32& key, const QByteArray& id)
{
    return QStringLiteral("ed3k:set?key=%1&id=%2")
        .arg(base32(key), base32(std::span(reinterpret_cast<const std::uint8_t*>(id.constData()), std::size_t(id.size()))));
}

} // namespace

class tst_Ed3kNames : public QObject {
    Q_OBJECT

private slots:
    void setLinksParse();
    void theDnsAnswerGoesToTheCore();
    void aWebNameLeadsToAPageServedOverLoopback();
    void theGateRefusesOtherHostsAndSites();
    void rangesComeFromVerifiedBlocks();
    void websitesAreKeptAcrossRestarts();
    void theSiteCacheDropsTheOldestAndBrokenFiles();
    void theIpcAnswersNamesAndPetnames();
    void cleanup();
};

void tst_Ed3kNames::cleanup()
{
    QTest::qWait(10);
}

// file-set.md §5.4: key and id base32; s or no id is no website here; repeats refused
void tst_Ed3kNames::setLinksParse()
{
    ::ed3k::Key32 key{};
    key.fill(7);
    const auto ref = Ed3kNames::parseSetLink(setLink(key, "blog") + QStringLiteral("&path=a%2Fb.html"));
    QVERIFY(ref);
    QCOMPARE(ref->key, key);
    QCOMPARE(ref->id, QByteArray("blog"));
    QCOMPARE(ref->path, QStringLiteral("a/b.html"));
    QVERIFY(!Ed3kNames::parseSetLink(QStringLiteral("ed3k:set?key=%1").arg(base32(key))));
    QVERIFY(!Ed3kNames::parseSetLink(setLink(key, "blog") + QStringLiteral("&id=mjwg6zy")));
    QVERIFY(!Ed3kNames::parseSetLink(QStringLiteral("ed3k:file?b3=x")));
    QVERIFY(Ed3kNames::keyFromText(QString::fromLatin1(Ed3kNames::toBytes(key).toHex())) == key);
    QVERIFY(Ed3kNames::keyFromText(base32(key)) == key);
}

// §5.1: NameDns → the injected TXT lookup → nameDnsAnswer; no answer → nameDnsFailed
void tst_Ed3kNames::theDnsAnswerGoesToTheCore()
{
    QTemporaryDir d;
    QVERIFY(d.isValid());
    Ed3kSession s;
    QVERIFY(s.start(loopback(d.path())));
    Ed3kNames names(s, d.path());
    FakeDns dns;
    ::ed3k::Key32 key{};
    key.fill(9);
    const QString link = setLink(key, "blog");
    dns.records = {"v=spf1 -all", QString(QStringLiteral("v=ed3k1 ") + link).toStdString()};
    names.setDnsResolver(dns.resolver());

    std::vector<Ed3kNames::Resolved> resolved;
    connect(&names, &Ed3kNames::resolved, &names, [&](const Ed3kNames::Resolved& r) { resolved.push_back(r); });
    QSignalSpy failed(&names, &Ed3kNames::failed);
    const auto id = names.resolve(QStringLiteral("Blog.invalid"));
    QVERIFY(id);
    // ed3k.json (§5.2) fails at the resolver; the core waits for both, each up to 10 s
    QTRY_COMPARE_WITH_TIMEOUT(resolved.size(), std::size_t(1), 30000);
    QCOMPARE(dns.asked, QStringList{QStringLiteral("_ed3k.blog.invalid")});
    const auto& r = resolved.front();
    QCOMPARE(r.id, *id);
    QCOMPARE(r.links, QStringList{link});
    QCOMPARE(r.zone, key);
    QCOMPARE(r.how.how, int(ED3K_NAME_HOW_DNS_UNVERIFIED));   // not secure: ask first (§5.3)
    QVERIFY(names.resolution(*id));
    QVERIFY(names.open(*id));
    QVERIFY(std::ranges::any_of(names.pins(), [](const auto& p) { return p.name == QStringLiteral("blog.invalid"); }));

    // no answer at all
    dns.fail = true;
    const auto id2 = names.resolve(QStringLiteral("bob@nobody.invalid"));
    QVERIFY(id2);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 30000);
    QCOMPARE(dns.asked.last(), QStringLiteral("bob._ed3k.nobody.invalid"));
    QCOMPARE(failed.first().at(0).toULongLong(), *id2);
    QCOMPARE(failed.first().at(2).toInt(), int(ED3K_NAME_WHY_NO_LINK));
    names.stop();
    s.stop();
}

// A shares index.html; B resolves a domain-verified web name to the set (held here), its
// website server redirects the readable host to the set's origin and sends the page it
// fetched from A, with sitePage's headers
void tst_Ed3kNames::aWebNameLeadsToAPageServedOverLoopback()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    const QByteArray page = "<!doctype html><title>blog</title><p>hello from ed3k</p>\n";
    Sharer share(a, da.path());
    share.start();
    const auto root = share.share(page);
    QVERIFY(root);

    Ed3kNames names(b, db.path());
    names.setSearchEnabled(false);
    FakeDns dns;
    ::ed3k::Key32 key{};
    key.fill(5);
    dns.records = {QString(QStringLiteral("v=ed3k1 ") + setLink(key, "blog")).toStdString()};
    dns.secure = true;   // a validating resolver: domain-verified, no question first
    names.setDnsResolver(dns.resolver());
    names.setSiteManifest(key, "blog", manifestOf(QStringLiteral("index.html"), page));
    Ed3kSiteServer sites(b, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    const std::uint16_t port = sites.port();
    const QByteArray portText = QByteArray::number(port);

    // §6.2: a readable host is resolved for the user and answered with a 302 only
    const Http redirect = request(port, "blog.invalid.web.localhost:" + portText, "/", kUser, "GET", 40000);
    QCOMPARE(redirect.status, 302);
    const QString origin = sites.originUrl(key, "blog");
    QVERIFY(origin.startsWith(QStringLiteral("http://")));
    QVERIFY2(redirect.headers.toLower().contains((QStringLiteral("location: ") + origin).toLatin1()), redirect.headers.constData());
    QVERIFY(redirect.body.isEmpty());

    // the set's origin: the member is fetched from A first (only verified bytes are sent)
    const QByteArray host = QUrl(origin).host().toLatin1() + ':' + portText;
    QTimer feed;   // a source by hand (no Kad search here), once the fetch is open
    connect(&feed, &QTimer::timeout, &names, [&] { names.addSiteSource(*root, sourceOf(a)); });
    feed.start(100);
    const Http got = request(port, host, "/", kUser);
    feed.stop();
    QCOMPARE(got.status, 200);
    QCOMPARE(got.body, page);
    const QByteArray h = got.headers.toLower();
    QVERIFY2(h.contains("content-type: text/html"), got.headers.constData());
    QVERIFY(h.contains("x-content-type-options: nosniff"));
    QVERIFY(h.contains("content-security-policy: default-src 'self'"));
    QVERIFY(!h.contains("access-control-"));   // never a CORS header
    // held now: same-origin requests are served at once; a path not held is 404
    QCOMPARE(request(port, host, "/", "Sec-Fetch-Site: same-origin\r\n").body, page);
    QCOMPARE(request(port, host, "/nothing.css", "Sec-Fetch-Site: same-origin\r\n").status, 404);
    QCOMPARE(request(port, host, "/", kUser, "HEAD").status, 200);
    names.stop();
    share.stop();
}

// §6.2: Host (421), method (405), Sec-Fetch-* (403), in that order; also for what is held
void tst_Ed3kNames::theGateRefusesOtherHostsAndSites()
{
    QTemporaryDir d;
    QVERIFY(d.isValid());
    Ed3kSession s;
    QVERIFY(s.start(loopback(d.path())));
    Ed3kNames names(s, d.path());
    ::ed3k::Key32 key{};
    key.fill(3);
    names.setSiteManifest(key, "blog", manifestOf(QStringLiteral("index.html"), "x"));
    Ed3kSiteServer sites(s, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    const std::uint16_t port = sites.port();
    const QByteArray p = QByteArray::number(port);
    const QByteArray host = QUrl(sites.originUrl(key, "blog")).host().toLatin1() + ':' + p;

    QCOMPARE(request(port, "evil.example:" + p, "/", kUser).status, 421);      // DNS rebinding
    QCOMPARE(request(port, "127.0.0.1:" + p, "/", kUser).status, 421);
    QCOMPARE(request(port, host.left(host.size() - p.size()) + "1", "/", kUser).status, 421);   // another port
    QCOMPARE(request(port, host, "/", kUser, "POST").status, 405);
    QCOMPARE(request(port, host, "/", "Sec-Fetch-Site: cross-site\r\nSec-Fetch-Mode: no-cors\r\n").status, 403);
    QCOMPARE(request(port, host, "/", {}).status, 403);   // no Sec-Fetch-Site
    // a readable host only for the user: a page cannot make the client resolve a name
    QCOMPARE(request(port, "alice.ed3k.localhost:" + p, "/", "Sec-Fetch-Site: cross-site\r\n").status, 403);
    names.stop();
    s.stop();
    // not running: not listening (the port serves websites while the node runs)
    QTRY_VERIFY_WITH_TIMEOUT(!sites.isListening(), 5000);
}

// §6.3 "Range requests answered from verified blocks" (RFC 9110 §14): one range → 206 once
// its blocks are there, 416 beyond the end, several → 200 whole, If-Range other → 200;
// a range whose blocks do not come → 504
void tst_Ed3kNames::rangesComeFromVerifiedBlocks()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    QByteArray big;   // 3 blocks of 256 KiB, not repeating
    for (int i = 0; big.size() < 600 * 1024; ++i)
        big += QByteArray::number(i * 7919) + ' ';
    Sharer share(a, da.path());
    share.start();
    const auto root = share.share(big);
    QVERIFY(root);
    Ed3kNames names(b, db.path());
    names.setSearchEnabled(false);
    ::ed3k::Key32 key{};
    key.fill(6);
    names.setSiteManifest(key, "big", manifestOf(QStringLiteral("index.html"), big));
    Ed3kSiteServer sites(b, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    const std::uint16_t port = sites.port();
    const QByteArray host = QUrl(sites.originUrl(key, "big")).host().toLatin1() + ':' + QByteArray::number(port);
    const QByteArray total = QByteArray::number(big.size());

    QTimer feed;
    connect(&feed, &QTimer::timeout, &names, [&] { names.addSiteSource(*root, sourceOf(a)); });
    feed.start(100);
    // not held yet: the range waits for its blocks
    const Http mid = request(port, host, "/", kUser + "Range: bytes=300000-300009\r\n");
    feed.stop();
    QCOMPARE(mid.status, 206);
    QCOMPARE(mid.body, big.mid(300000, 10));
    QVERIFY2(mid.headers.toLower().contains(QByteArray(QByteArray("content-range: bytes 300000-300009/") + total)), mid.headers.constData());
    QVERIFY(mid.headers.toLower().contains("accept-ranges: bytes"));
    QTRY_VERIFY_WITH_TIMEOUT(names.siteMember(*root).has_value(), 30000);

    const QByteArray same = "Sec-Fetch-Site: same-origin\r\n";
    const Http tail = request(port, host, "/", same + "Range: bytes=-5\r\n");
    QCOMPARE(tail.status, 206);
    QCOMPARE(tail.body, big.right(5));
    const Http open = request(port, host, "/", same + "Range: bytes=600000-\r\n");
    QCOMPARE(open.status, 206);
    QCOMPARE(open.body, big.mid(600000));
    const Http beyond = request(port, host, "/", same + "Range: bytes=" + total + "-\r\n");
    QCOMPARE(beyond.status, 416);
    QVERIFY2(beyond.headers.toLower().contains(QByteArray(QByteArray("content-range: bytes */") + total)), beyond.headers.constData());
    // several ranges: the whole member (no multipart here)
    const Http several = request(port, host, "/", same + "Range: bytes=0-1,5-6\r\n");
    QCOMPARE(several.status, 200);
    QCOMPARE(several.body, big);
    // If-Range of another version: the whole member
    const Http other = request(port, host, "/", same + "Range: bytes=0-1\r\nIf-Range: \"other\"\r\n");
    QCOMPARE(other.status, 200);
    QCOMPARE(other.body.size(), big.size());
    // a range that is no "bytes=" range is ignored
    QCOMPARE(request(port, host, "/", same + "Range: items=0-1\r\n").status, 200);

    // a member whose blocks never come: 504 within the wait
    sites.setWaitMs(1500);
    names.setSiteManifest(key, "gone", manifestOf(QStringLiteral("index.html"), QByteArray(1000, 'g')));
    const QByteArray goneHost = QUrl(sites.originUrl(key, "gone")).host().toLatin1() + ':' + QByteArray::number(port);
    QCOMPARE(request(port, goneHost, "/", same + "Range: bytes=0-9\r\n").status, 504);
    names.stop();
    share.stop();
}

// §6.4: manifests and members are kept on disk; after a restart the site is served from
// there with no source at all
void tst_Ed3kNames::websitesAreKeptAcrossRestarts()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    const QByteArray page = "<!doctype html><p>kept</p>\n";
    Sharer share(a, da.path());
    share.start();
    const auto root = share.share(page);
    QVERIFY(root);
    ::ed3k::Key32 key{};
    key.fill(8);
    const QByteArray same = "Sec-Fetch-Site: same-origin\r\n";
    {
        Ed3kNames names(b, db.path());
        names.setSearchEnabled(false);
        names.setSiteManifest(key, "kept", manifestOf(QStringLiteral("index.html"), page));
        Ed3kSiteServer sites(b, names);
        QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
        const QByteArray host = QUrl(sites.originUrl(key, "kept")).host().toLatin1() + ':' + QByteArray::number(sites.port());
        QTimer feed;
        connect(&feed, &QTimer::timeout, &names, [&] { names.addSiteSource(*root, sourceOf(a)); });
        feed.start(100);
        QCOMPARE(request(sites.port(), host, "/", kUser).body, page);
        feed.stop();
        QVERIFY(names.siteCache().hasMember(*root));
        QVERIFY(names.siteCache().bytes() >= std::uint64_t(page.size()));
        names.stop();
    }
    share.stop();
    a.stop();   // no source any more
    b.stop();
    QVERIFY(b.start(loopback(db.path())));
    Ed3kNames names(b, db.path());
    names.setSearchEnabled(false);
    QVERIFY(!names.siteManifest(key, "kept").isEmpty());
    Ed3kSiteServer sites(b, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    const QByteArray host = QUrl(sites.originUrl(key, "kept")).host().toLatin1() + ':' + QByteArray::number(sites.port());
    const Http got = request(sites.port(), host, "/", same, "GET", 10000);
    QCOMPARE(got.status, 200);
    QCOMPARE(got.body, page);
    names.stop();
    b.stop();
}

// LRU: the file used longest ago goes first; a member not under its root is removed
void tst_Ed3kNames::theSiteCacheDropsTheOldestAndBrokenFiles()
{
    QTemporaryDir d;
    QVERIFY(d.isValid());
    Ed3kSiteCache cache(d.filePath(QStringLiteral("c")), 250);
    const QByteArray m1(100, '1'), m2(100, '2'), m3(100, '3');
    cache.putMember(rootOf(m1), m1);
    QTest::qWait(30);
    cache.putMember(rootOf(m2), m2);
    QTest::qWait(30);
    QCOMPARE(cache.member(rootOf(m1)), std::optional<QByteArray>(m1));   // used: the newest now
    QTest::qWait(30);
    cache.putMember(rootOf(m3), m3);
    QVERIFY(cache.hasMember(rootOf(m1)));
    QVERIFY(!cache.hasMember(rootOf(m2)));
    QVERIFY(cache.hasMember(rootOf(m3)));
    QCOMPARE(cache.bytes(), std::uint64_t(200));
    cache.putManifest(QStringLiteral("aa.bb"), "manifest");
    QCOMPARE(cache.manifest(QStringLiteral("aa.bb")), std::optional<QByteArray>("manifest"));
    // the totals come back after a restart
    Ed3kSiteCache again(d.filePath(QStringLiteral("c")), 250);
    QCOMPARE(again.bytes(), cache.bytes());
    // a broken file: not sent, removed
    QFile f(QDir(d.filePath(QStringLiteral("c"))).filePath(QStringLiteral("m-") + QString::fromLatin1(Ed3kNames::toBytes(rootOf(m3).bytes).toHex())));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QByteArray(100, 'x'));
    f.close();
    QVERIFY(!again.member(rootOf(m3)));
    QVERIFY(!again.hasMember(rootOf(m3)));
    again.setMax(0);
    QCOMPARE(again.bytes(), std::uint64_t(0));
}

// Ed3kNamesBridge through Ed3kBridge: GetEd3kNames, a petname, a resolve, an unknown open
void tst_Ed3kNames::theIpcAnswersNamesAndPetnames()
{
    QTemporaryDir d;
    QVERIFY(d.isValid());
    Ed3kSession s;
    QVERIFY(s.start(loopback(d.path())));
    Ed3kNames names(s, d.path());
    Ed3kSiteServer sites(s, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    Ed3kNamesBridge::setNames(&names, &sites);
    const auto call = [](const IpcMessage& msg) {
        IpcMessage got;
        Ed3kBridge::handleRequest(msg, [&](const IpcMessage& r) { got = r; });
        return got;
    };
    ::ed3k::Key32 zone{};
    zone.fill(4);
    IpcMessage set(IpcMsgType::SetEd3kPetname, 7);
    set.append(QStringLiteral("alice"));
    set.append(base32(zone));
    set.append(true);
    QVERIFY(call(set).fieldBool(0));
    QVERIFY(QFile::exists(QDir(stateDir(d.path())).filePath(QStringLiteral("name-petnames.dat"))));

    const IpcMessage got = call(IpcMessage(IpcMsgType::GetEd3kNames, 7));
    QVERIFY(got.fieldBool(0));
    const QCborMap m = got.field(1).toMap();
    QVERIFY(m.value(QStringLiteral("running")).toBool());
    QCOMPARE(m.value(QStringLiteral("sitePort")).toInteger(), qint64(sites.port()));
    const QCborArray pets = m.value(QStringLiteral("petnames")).toArray();
    QCOMPARE(pets.size(), 1);
    QCOMPARE(pets.at(0).toMap().value(QStringLiteral("zone")).toString(), QString::fromLatin1(Ed3kNames::toBytes(zone).toHex()));

    // a petname resolves (§4.1 source 1)
    IpcMessage res(IpcMsgType::ResolveEd3kName, 7);
    res.append(QStringLiteral("alice"));
    const IpcMessage r = call(res);
    QVERIFY(r.fieldBool(0));
    QVERIFY(r.field(1).toMap().contains(QStringLiteral("id")));
    IpcMessage open(IpcMsgType::OpenEd3kName, 7);
    open.append(qint64(987654));
    const IpcMessage o = call(open);
    QVERIFY(!o.fieldBool(0));
    QCOMPARE(o.field(1).toMap().value(QStringLiteral("error")).toInteger(), qint64(::ed3k::Error::NotFound));

    IpcMessage remove(IpcMsgType::RemoveEd3kPetname, 7);
    remove.append(QStringLiteral("alice"));
    QVERIFY(call(remove).fieldBool(0));
    QVERIFY(names.petnames().empty());
    Ed3kNamesBridge::setNames(nullptr, nullptr);
    // without a node object: empty lists, every other request [false, {error: 0}]
    QVERIFY(call(IpcMessage(IpcMsgType::GetEd3kNames, 7)).fieldBool(0));
    QVERIFY(!call(res).fieldBool(0));
    names.stop();
    s.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kNames)
#include "tst_Ed3kNames.moc"
