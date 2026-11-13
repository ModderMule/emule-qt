/// @file tst_Ed3kNames.cpp
/// @brief Readable names (src/ed3k/Ed3kNames) and the local website server
/// (src/ed3k/Ed3kSiteServer): the DNS duty through a fake resolver, a web name that leads to
/// a set whose page node A serves and node B's website server sends over loopback HTTP, the
/// gate (421, 405, 403), the redirect of a readable host, and the IPC (Ed3kNamesBridge).
/// Range requests (206/416) from verified blocks; websites kept on disk across restarts (LRU).
/// Batch 14: a member streamed without Range (chunked, cut on a bad block, 504 with no first
/// byte); a site unfollowed once neither shown nor held; a set the user follows not evicted.
/// Batch 15 (#16): a held site not followed is followed on any request, which waits for its
/// head check (SetHeadChecked) or a newer manifest; 504 when neither comes in time.

#include "utils/BackgroundFileWriter.h"
#include "Ed3kBridge.h"
#include "Ed3kDownloads.h"
#include "Ed3kJoinBase.h"
#include "Ed3kNames.h"
#include "Ed3kNamesBridge.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSession.h"
#include "Ed3kSets.h"
#include "Ed3kSiteCache.h"
#include "Ed3kSiteServer.h"
#include "Ed3kStore.h"

#include "IpcProtocol.h"

#include <QCborArray>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QTimer>
#include <QUrl>
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
    s.smallNetS = 1;   // a few nodes: connected 1 s after the first answer, not 45 s
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

/// A manifest signed by the key of @p seed (file-set.md §2, §5): index.html of @p page, version @p seq
QByteArray signedManifestOf(const ::ed3k::Key32& seed, const QByteArray& id, std::uint64_t seq, const QByteArray& page)
{
    ::ed3k::SetFields f;
    f.title = "site";
    f.created = 1'700'000'000;
    for (char ch : id)
        f.id.push_back(std::byte(ch));
    f.seq = seq;
    ::ed3k::SetMemberIn m;
    m.root = rootOf(page);
    m.size = std::uint64_t(page.size());
    m.path = "index.html";
    auto file = ::ed3k::setManifestWrite(std::uint64_t(QDateTime::currentSecsSinceEpoch()), seed, f, std::span(&m, 1));
    if (!file)
        return {};
    return QByteArray(reinterpret_cast<const char*>(file->bytes().data()), qsizetype(file->size()));
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

/// Node A's side for a bad block: serves a member under its true root and hashes, read from
/// a file whose last block is wrong (the uploader does not check what it reads).
class BadSharer final : public Ed3kJoinBase {
public:
    BadSharer(Ed3kSession& s, const QString& dir) : Ed3kJoinBase(s, dir, QStringLiteral("bad"), nullptr) {}
    void start() override { beginStart(); }
    void stop() override { closeAll(); }
    bool share(const QByteArray& good, const QString& path)
    {
        m_path = path;
        ::ed3k::Hasher h;
        if (!h.update({reinterpret_cast<const std::byte*>(good.constData()), std::size_t(good.size())}))
            return false;
        auto done = h.finish();
        if (!done)
            return false;
        const ::ed3k::SharedFile f{done->root, done->size, "index.html", std::nullopt, std::nullopt, ::ed3k::Have::complete(),
                                   done->hashes.bytes()};
        return shareMember(f, QStringLiteral("bad"), 0);
    }

protected:
    std::vector<std::uint64_t> wantedBlocks(const Download&) const override { return {}; }
    void onFileBlock(const Download&, const ::ed3k::event::Block&) override {}
    void onBlobFetched(const QString&, int, const ::ed3k::Root&, const QByteArray&) override {}
    std::optional<FileRead> sharedRead(const Shared&, std::uint64_t offset, std::uint32_t len) const override
    {
        return FileRead{m_path, offset, len};
    }
    bool hasOwner(const QString& owner) const override { return owner == QLatin1String("bad"); }
    void saveOwner(const QString&) override {}

private:
    QString m_path;
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
    bool chunked = false;    ///< Transfer-Encoding: chunked (body decoded)
    bool complete = false;   ///< chunked: its last chunk came (else cut)
};

/// The chunks of a chunked body so far; @p last: the last chunk came.
QByteArray dechunk(const QByteArray& raw, bool* last)
{
    QByteArray out;
    *last = false;
    qsizetype at = 0;
    while (true) {
        const qsizetype eol = raw.indexOf("\r\n", at);
        if (eol < 0)
            return out;
        bool ok = false;
        const qsizetype n = raw.mid(at, eol - at).split(';').front().trimmed().toLongLong(&ok, 16);
        if (!ok)
            return out;
        if (n == 0) {
            *last = raw.indexOf("\r\n", eol + 2) >= 0;
            return out;
        }
        if (raw.size() < eol + 2 + n + 2)
            return out;
        out += raw.mid(eol + 2, n);
        at = eol + 2 + n + 2;
    }
}

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
        out.chunked = out.headers.toLower().contains("transfer-encoding: chunked");
        if (out.chunked && method != "HEAD") {
            // until the last chunk, or the connection is cut
            const QByteArray chunks = out.body;
            out.body = dechunk(chunks, &out.complete);
            if (out.complete)
                return out;
            if (s.state() == QAbstractSocket::UnconnectedState)
                break;
            continue;
        }
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
    void aMemberIsStreamedWithoutRange();
    void aBadBlockCutsTheStream();
    void aSiteIsUnfollowedOnceNeitherShownNorHeld();
    void aSetTheUserFollowsIsNotEvicted();
    void aHeldSiteIsServedAfterItsHeadCheck();
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

// batch 13 #12: a GET without Range of a member not held is streamed in file order as its
// blocks verify (chunked, complete); a member whose blocks never come: 504 (no first byte)
void tst_Ed3kNames::aMemberIsStreamedWithoutRange()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    QByteArray big;   // 3 blocks of 256 KiB, not repeating
    for (int i = 0; big.size() < 700 * 1024; ++i)
        big += QByteArray::number(i * 104729) + ' ';
    Sharer share(a, da.path());
    share.start();
    const auto root = share.share(big);
    QVERIFY(root);
    Ed3kNames names(b, db.path());
    names.setSearchEnabled(false);
    ::ed3k::Key32 key{};
    key.fill(9);
    names.setSiteManifest(key, "stream", manifestOf(QStringLiteral("index.html"), big));
    Ed3kSiteServer sites(b, names);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    const std::uint16_t port = sites.port();
    const QByteArray host = QUrl(sites.originUrl(key, "stream")).host().toLatin1() + ':' + QByteArray::number(port);

    QTimer feed;
    connect(&feed, &QTimer::timeout, &names, [&] { names.addSiteSource(*root, sourceOf(a)); });
    feed.start(100);
    const Http got = request(port, host, "/", kUser);
    feed.stop();
    QCOMPARE(got.status, 200);
    QVERIFY2(got.chunked, got.headers.constData());   // streamed, not sent whole
    QVERIFY(got.complete);
    QCOMPARE(got.body.size(), big.size());
    QVERIFY(got.body == big);
    QVERIFY(got.headers.toLower().contains("x-content-type-options: nosniff"));
    // held now: sent whole, with its length
    const Http again = request(port, host, "/", "Sec-Fetch-Site: same-origin\r\n");
    QCOMPARE(again.status, 200);
    QVERIFY(!again.chunked);
    QVERIFY(again.body == big);

    // no first byte within the wait: 504
    sites.setWaitMs(1500);
    names.setSiteManifest(key, "never", manifestOf(QStringLiteral("index.html"), QByteArray(5000, 'n')));
    const QByteArray neverHost = QUrl(sites.originUrl(key, "never")).host().toLatin1() + ':' + QByteArray::number(port);
    QCOMPARE(request(port, neverHost, "/", "Sec-Fetch-Site: same-origin\r\n").status, 504);
    names.stop();
    share.stop();
}

// batch 13 #12: a stream under way is cut when a block of its member does not verify (no
// last chunk, the connection closed); only verified bytes were sent before
void tst_Ed3kNames::aBadBlockCutsTheStream()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    QByteArray good;
    for (int i = 0; good.size() < 700 * 1024; ++i)
        good += QByteArray::number(i * 7907) + ' ';
    // the file A reads from: its last block wrong
    QByteArray bad = good;
    for (qsizetype i = 2 * 256 * 1024; i < bad.size(); ++i)
        bad[i] = 'x';
    const QString badPath = da.filePath(QStringLiteral("bad.bin"));
    {
        QFile f(badPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QCOMPARE(f.write(bad), bad.size());
    }
    BadSharer share(a, da.path());
    share.start();
    QVERIFY(share.share(good, badPath));
    const auto root = rootOf(good);
    Ed3kNames names(b, db.path());
    names.setSearchEnabled(false);
    ::ed3k::Key32 key{};
    key.fill(10);
    names.setSiteManifest(key, "bad", manifestOf(QStringLiteral("index.html"), good));
    Ed3kSiteServer sites(b, names);
    sites.setWaitMs(20000);
    QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
    const std::uint16_t port = sites.port();
    const QByteArray host = QUrl(sites.originUrl(key, "bad")).host().toLatin1() + ':' + QByteArray::number(port);
    QSignalSpy bads(&names, &Ed3kNames::siteMemberBad);

    QTimer feed;
    connect(&feed, &QTimer::timeout, &names, [&] { names.addSiteSource(root, sourceOf(a)); });
    feed.start(100);
    const Http got = request(port, host, "/", kUser, "GET", 30000);
    feed.stop();
    QVERIFY(bads.count() > 0);
    QCOMPARE(got.status, 200);
    QVERIFY(got.chunked);
    QVERIFY(!got.complete);   // cut
    QVERIFY(got.body.size() < good.size());
    QVERIFY(good.startsWith(got.body));   // what came was verified
    QVERIFY(!names.siteCache().hasMember(root));
    names.stop();
    share.stop();
}

// §6.1, batch 13 #13: a site's head is followed while shown or held, then unfollowed
void tst_Ed3kNames::aSiteIsUnfollowedOnceNeitherShownNorHeld()
{
    QTemporaryDir d;
    QVERIFY(d.isValid());
    Ed3kSession s;
    QVERIFY(s.start(loopback(d.path())));
    Ed3kNames names(s, d.path());
    names.setSiteShownMs(300);
    ::ed3k::Key32 key{};
    key.fill(11);
    // followed first (an unsigned manifest is no version the core takes as held), then held
    QVERIFY(names.followSite(key, "site"));
    names.setSiteManifest(key, "site", manifestOf(QStringLiteral("index.html"), "x"));   // held (on disk)
    QVERIFY(names.siteFollowed(key, "site"));
    QTest::qWait(500);
    names.expireSites();
    QVERIFY(names.siteFollowed(key, "site"));   // not shown any more, still held
    names.siteShown(key, "site");
    names.siteCache().setMax(0);   // no longer held
    names.expireSites();
    QVERIFY(names.siteFollowed(key, "site"));   // shown a moment ago
    QTRY_VERIFY_WITH_TIMEOUT(!names.siteFollowed(key, "site"), 5000);   // the timer, after 300 ms
    // shown again (a request for the user): followed again
    QVERIFY(names.followSite(key, "site"));
    QVERIFY(names.siteFollowed(key, "site"));
    names.stop();
    s.stop();
}

// §6.4: a set the user chose to follow goes only when the user removes it: not evicted, not
// unfollowed; any other site's files are LRU
void tst_Ed3kNames::aSetTheUserFollowsIsNotEvicted()
{
    // the cache alone: what Keep names stays, the rest goes oldest first
    {
        QTemporaryDir d;
        QVERIFY(d.isValid());
        Ed3kSiteCache cache(d.filePath(QStringLiteral("c")), 1000);
        const QByteArray m1(100, '1'), m2(100, '2');
        cache.putMember(rootOf(m1), m1);
        QTest::qWait(30);
        cache.putMember(rootOf(m2), m2);
        cache.putManifest(QStringLiteral("aa.bb"), "manifest");
        cache.setKeep([&] { return Ed3kSiteCache::Keep{{QStringLiteral("aa.bb")}, {rootOf(m1)}}; });
        cache.setMax(0);
        QVERIFY(cache.hasMember(rootOf(m1)));   // the oldest, but kept
        QVERIFY(!cache.hasMember(rootOf(m2)));
        QVERIFY(cache.hasManifest(QStringLiteral("aa.bb")));
    }
    QTemporaryDir d;
    QVERIFY(d.isValid());
    Ed3kSession s;
    QVERIFY(s.start(loopback(d.path())));
    Ed3kDownloads dl(s, d.path());
    Ed3kSets sets(s, dl, d.path());
    Ed3kNames names(s, d.path());
    names.keepUserSets(&sets);
    names.setSiteShownMs(100);
    ::ed3k::Key32 key{};
    key.fill(12);
    QVERIFY(sets.follow(setLink(key, "mine"), Ed3kSets::Policy::Ask).has_value());
    QVERIFY(names.followSite(key, "mine"));
    names.setSiteManifest(key, "mine", manifestOf(QStringLiteral("index.html"), "m"));
    names.setSiteManifest(key, "other", manifestOf(QStringLiteral("index.html"), "o"));
    names.siteCache().setMax(0);
    QVERIFY(!names.siteManifest(key, "mine").isEmpty());
    const auto site = [&](const QByteArray& id) { return QString::fromLatin1(Ed3kNames::toBytes(key).toHex() + '.' + id.toHex()); };
    QVERIFY(names.siteCache().hasManifest(site("mine")));
    QVERIFY(!names.siteCache().hasManifest(site("other")));
    QTest::qWait(300);
    names.expireSites();
    QVERIFY(names.siteFollowed(key, "mine"));   // the user's set: not unfollowed here
    names.stop();
    s.stop();
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
    eMule::BackgroundFileWriter::instance().flush();   // saves are queued (report 43)
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


// name.md §6.1 (#16): a site held and not followed (an earlier run) is followed on any request
// (not only the user's), and no member is served before its head was checked: the held
// version after SetHeadChecked, a newer manifest when one comes meanwhile, 504 when neither
void tst_Ed3kNames::aHeldSiteIsServedAfterItsHeadCheck()
{
    QTemporaryDir db, dc;
    QVERIFY(db.isValid() && dc.isValid());
    ::ed3k::Key32 seed{};
    seed.fill(0x2b);
    const auto key = ::ed3k::keyPublic(seed);
    QVERIFY(key);
    const QByteArray page1 = "<!doctype html><p>one</p>\n";
    const QByteArray page2 = "<!doctype html><p>two</p>\n";
    const QByteArray v1 = signedManifestOf(seed, "held", 1, page1);
    const QByteArray v2 = signedManifestOf(seed, "held", 2, page2);
    QVERIFY(!v1.isEmpty() && !v2.isEmpty());
    const QByteArray sub = "Sec-Fetch-Site: same-origin\r\nSec-Fetch-Mode: no-cors\r\n";   // a page's own request

    Ed3kSession b;
    QVERIFY(b.start(loopback(db.path())));
    // a contact that is gone: B's head search waits for it (with none it ends at once)
    const auto deadContact = [&] {
        QTemporaryDir dd;
        Ed3kSession d;
        if (!d.start(loopback(dd.path())))
            return false;
        const ::ed3k::Node node{*d.nodeId(), ::ed3k::Address::v4(127, 0, 0, 1, d.udpPort())};
        const auto before = b.contactCount();
        b.post([&](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&node, 1}); });
        QDeadlineTimer until(20000);
        while (b.contactCount() <= before && !until.hasExpired())
            QTest::qWait(20);
        // Kad connected (the small-network wait): a head search runs, not held
        while (!b.status().kadConnected && !until.hasExpired())
            QTest::qWait(20);
        d.stop();
        return b.contactCount() > before && b.status().kadConnected;
    };
    {
        QVERIFY(deadContact());
        // B holds v1; no head check within the wait: 504 (was: v1 served at once)
        Ed3kNames names(b, db.path());
        names.setSearchEnabled(false);
        names.setSiteManifest(*key, "held", v1);
        names.siteCache().putMember(rootOf(page1), page1);
        names.siteCache().putMember(rootOf(page2), page2);
        Ed3kSiteServer sites(b, names);
        sites.setWaitMs(1000);
        QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
        const QByteArray host = QUrl(sites.originUrl(*key, "held")).host().toLatin1() + ':' + QByteArray::number(sites.port());
        QVERIFY(!names.siteFollowed(*key, "held"));
        QSignalSpy checked(&names, &Ed3kNames::siteHeadChecked);
        const Http late = request(sites.port(), host, "/", kUser);
        QVERIFY(checked.isEmpty());
        QCOMPARE(late.status, 504);
        QVERIFY(names.siteFollowed(*key, "held"));
        QVERIFY(names.siteHeadPending(*key, "held"));
        names.stop();
    }
    {
        QVERIFY(deadContact());
        // B again (a restart): a newer manifest while a page's request waits: that one is served
        Ed3kNames names(b, db.path());
        names.setSearchEnabled(false);
        Ed3kSiteServer sites(b, names);
        QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
        QCOMPARE(names.siteManifest(*key, "held"), v1);
        QVERIFY(!names.siteFollowed(*key, "held"));
        const QByteArray host = QUrl(sites.originUrl(*key, "held")).host().toLatin1() + ':' + QByteArray::number(sites.port());
        bool pending = false;
        QTimer::singleShot(400, &names, [&] {
            pending = names.siteHeadPending(*key, "held");
            names.setSiteManifest(*key, "held", v2);
        });
        const Http newer = request(sites.port(), host, "/", sub);
        QVERIFY(pending);
        QCOMPARE(newer.status, 200);
        QCOMPARE(newer.body, page2);
        QVERIFY(names.siteFollowed(*key, "held"));   // followed for a page's request too (was: the user's only)
        QVERIFY(!names.siteHeadPending(*key, "held"));
        names.stop();
    }
    {
        // B knows a live C: the head search ends (no newer head anywhere), the held one is served
        Ed3kSession c;
        QVERIFY(c.start(loopback(dc.path())));
        const ::ed3k::Node cNode{*c.nodeId(), ::ed3k::Address::v4(127, 0, 0, 1, c.udpPort())};
        b.post([&](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&cNode, 1}); });
        QTRY_VERIFY_WITH_TIMEOUT(c.contactCount() > 0, 20000);
        Ed3kNames names(b, db.path());
        names.setSearchEnabled(false);
        Ed3kSiteServer sites(b, names);
        QTRY_VERIFY_WITH_TIMEOUT(sites.isListening(), 5000);
        QSignalSpy checked(&names, &Ed3kNames::siteHeadChecked);
        const QByteArray host = QUrl(sites.originUrl(*key, "held")).host().toLatin1() + ':' + QByteArray::number(sites.port());
        const Http got = request(sites.port(), host, "/", sub);
        QCOMPARE(got.status, 200);
        QCOMPARE(got.body, page2);
        QVERIFY(!checked.isEmpty());   // it came before the answer
        QCOMPARE(checked.first().at(2).toBool(), false);
        QVERIFY(!names.siteHeadPending(*key, "held"));
        // shown again, followed: served at once
        QCOMPARE(request(sites.port(), host, "/", sub).body, page2);
        names.stop();
        c.stop();
    }
    b.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kNames)
#include "tst_Ed3kNames.moc"
