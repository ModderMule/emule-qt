/// @file tst_Ed3kOwnSets.cpp
/// @brief My ed3k Sets (src/ed3k/Ed3kOwnSets) and their IPC (Ed3kOwnSetsBridge, SellEd3k kind 2
/// through Ed3kPaidSellerBridge): node A publishes a folder as a set, node B holds the records,
/// a follower C downloads it by its link and sees a new version; a hidden set's link only
/// through GetEd3kOwnSetLink, its sealed members opened by a follower with the link; a paid
/// catalogue's members sealed into .sealed/ (no plain root shared), a voucher holder opens one;
/// a restart publishes the version kept; unpublish ends the head (setUnpublish).

#include "Ed3kDownloads.h"
#include "Ed3kOwnSets.h"
#include "Ed3kOwnSetsBridge.h"
#include "Ed3kPaid.h"
#include "Ed3kPaidSeller.h"
#include "Ed3kPaidSellerBridge.h"
#include "Ed3kSession.h"
#include "Ed3kSetManifest.h"
#include "Ed3kSets.h"
#include "Ed3kStore.h"

#include "IpcMessage.h"

#include <QCborArray>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
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
        data[i] = char((i * 7 + (i >> 9) * 5 + seed * 29) & 0xFF);
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

/// An IPC request answered at once by @p handle.
IpcMessage ask(bool (*handle)(const IpcMessage&, const std::function<void(const IpcMessage&)>&), IpcMessage req)
{
    IpcMessage out;
    (void)handle(req, [&](const IpcMessage& r) { out = r; });
    return out;
}

/// file-set.md §6: the sets whose MEMBER records name @p root, as a member search from @p s
/// finds them; -1: the search did not end.
int memberSets(Ed3kSession& s, const ::ed3k::Root& root)
{
    std::optional<std::uint32_t> search;
    std::optional<std::uint32_t> sets;
    const auto conn = QObject::connect(&s, &Ed3kSession::eventPolled, &s, [&](const ::ed3k::Event& ev, bool*) {
        if (const auto* m = std::get_if<::ed3k::event::MemberSets>(&ev); m && search && m->search == *search)
            sets = m->sets;
    }, Qt::DirectConnection);
    s.post([&](::ed3k::Engine& e, ::ed3k::Now now) {
        if (auto r = e.memberSearchStart(now, root))
            search = *r;
    });
    QElapsedTimer t;
    t.start();
    while (search && !sets && t.elapsed() < 30000)
        QTest::qWait(50);
    QObject::disconnect(conn);
    if (search)
        s.post([id = *search](::ed3k::Engine& e, ::ed3k::Now) { (void)e.memberSearchEnd(id); });
    return sets ? int(*sets) : -1;
}

/// A follower: its node, downloads, sets and paid subscriptions.
struct Follower {
    std::unique_ptr<Ed3kSession> session;
    std::unique_ptr<Ed3kDownloads> downloads;
    std::unique_ptr<Ed3kPaid> paid;
    std::unique_ptr<Ed3kSets> sets;
    QString dir;

    bool start(const QString& d, const ::ed3k::Node& known)
    {
        dir = d;
        session = std::make_unique<Ed3kSession>();
        if (!session->start(loopback(dir)))
            return false;
        session->post([known](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&known, 1}); });
        downloads = std::make_unique<Ed3kDownloads>(*session, dir);
        downloads->setSourceResearch(3000, 2000);
        downloads->setDirs(QDir(dir).filePath(QStringLiteral("temp")), incoming());
        downloads->start();
        paid = std::make_unique<Ed3kPaid>(*session, dir);
        sets = std::make_unique<Ed3kSets>(*session, *downloads, dir);
        sets->setIncomingDir(incoming());
        sets->setPaid(paid.get());
        return true;
    }
    void stop()
    {
        sets.reset();
        paid.reset();
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

class tst_Ed3kOwnSets : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void aFolderIsPublishedAndFollowed();
    void aNewVersionIsSeenByTheFollower();
    void aRestartPublishesTheVersionKept();
    void aHiddenSetsLinkComesOnlyFrom1148();
    void aPaidCatalogueIsSealedAndAPassHolderOpensIt();
    void theIpcListsPublishesAndRetitles();
    void unpublishEndsTheHead();
    void cleanupTestCase();
    void cleanup() { QTest::qWait(10); }

private:
    /// waits for a version of @p id (true: published)
    bool waitVersion(const QString& id, int timeoutMs = 60000);

    QTemporaryDir m_da, m_db, m_dc, m_dd, m_de, m_files;
    Ed3kSession m_a, m_b;
    std::unique_ptr<Ed3kPaidSeller> m_seller;
    std::unique_ptr<Ed3kOwnSets> m_own;
    Follower m_c;
    QString m_folder;
    QString m_openId;
    std::uint32_t m_cSet = 0;
};

void tst_Ed3kOwnSets::initTestCase()
{
    QVERIFY(m_da.isValid() && m_db.isValid() && m_dc.isValid() && m_files.isValid());
    QVERIFY(m_a.start(loopback(m_da.path())));
    QVERIFY(m_b.start(loopback(m_db.path())));
    const ::ed3k::Node bNode = nodeOf(m_b);
    m_a.post([bNode](::ed3k::Engine& e, ::ed3k::Now now) { (void)e.bootstrap(now, std::span<const ::ed3k::Node>{&bNode, 1}); });
    QTRY_VERIFY_WITH_TIMEOUT(m_a.contactCount() > 0, 20000);
    m_seller = std::make_unique<Ed3kPaidSeller>(m_a, m_da.path());
    m_own = std::make_unique<Ed3kOwnSets>(m_a, m_da.path());
    m_own->setSeller(m_seller.get());
    QVERIFY(m_own->isStarted());
    Ed3kOwnSetsBridge::setOwnSets(m_own.get());
    Ed3kPaidSellerBridge::setSeller(m_seller.get(), nullptr);
    Ed3kPaidSellerBridge::setOwnSets(m_own.get());

    m_folder = QDir(m_files.path()).filePath(QStringLiteral("Holiday"));
    QVERIFY(writeFile(QDir(m_folder).filePath(QStringLiteral("a.bin")), payload(300 * 1024 + 5, 1)));
    QVERIFY(writeFile(QDir(m_folder).filePath(QStringLiteral("sub/b.bin")), payload(120 * 1024 + 9, 2)));
    // hidden files are left out
    QVERIFY(writeFile(QDir(m_folder).filePath(QStringLiteral(".hidden")), "x"));
}

void tst_Ed3kOwnSets::cleanupTestCase()
{
    Ed3kOwnSetsBridge::setOwnSets(nullptr);
    Ed3kPaidSellerBridge::setOwnSets(nullptr);
    Ed3kPaidSellerBridge::setSeller(nullptr, nullptr);
    m_c.stop();
    m_own.reset();
    m_seller.reset();
    m_a.stop();
    m_b.stop();
}

bool tst_Ed3kOwnSets::waitVersion(const QString& id, int timeoutMs)
{
    QSignalSpy done(m_own.get(), &Ed3kOwnSets::versionDone);
    const auto seen = [&] {
        for (const auto& args : done)
            if (args.at(0).toString() == id)
                return true;
        return false;
    };
    QElapsedTimer t;
    t.start();
    while (!seen() && t.elapsed() < timeoutMs)
        QTest::qWait(50);
    for (const auto& args : done)
        if (args.at(0).toString() == id)
            return args.at(2).toBool();
    return false;
}

// file-set.md §5: a folder published as a set that changes; C follows its link
void tst_Ed3kOwnSets::aFolderIsPublishedAndFollowed()
{
    QSignalSpy done(m_own.get(), &Ed3kOwnSets::versionDone);
    const auto id = m_own->create(QStringLiteral("Holiday"), m_folder, {}, Ed3kOwnSets::Kind::Open);
    QVERIFY2(id, qPrintable(id ? QString() : id.error()));
    m_openId = *id;
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
    QVERIFY(done.at(0).at(2).toBool());
    const auto info = m_own->info(m_openId);
    QVERIFY(info);
    QCOMPARE(info->seq, std::uint64_t(1));
    QCOMPARE(info->files, 2);
    QVERIFY(info->published);
    QVERIFY(info->link.startsWith(QStringLiteral("ed3k:set?key=")));
    QVERIFY(m_own->headOf(m_openId));
    // the manifest as a reader reads it: the paths, the directory from them
    const auto man = Ed3kSetManifest::parse(readBlob(QDir(QDir(stateDir(m_da.path())).filePath(QStringLiteral("own-sets"))).filePath(m_openId + QStringLiteral(".manifest"))));
    QVERIFY(man);
    QCOMPARE(int(man->files.size()), 2);
    // the set key is a secret file, owner-only
#ifndef Q_OS_WIN
    const QString key = QDir(m_own->setDir(QString())).filePath(m_openId + QStringLiteral(".key"));
    QVERIFY(QFileInfo::exists(key));
    QCOMPARE(QFile::permissions(key) & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions{});
#endif

    QVERIFY(m_c.start(m_dc.path(), nodeOf(m_b)));
    QTRY_VERIFY_WITH_TIMEOUT(m_c.session->contactCount() > 0, 20000);
    const auto cid = m_c.sets->follow(info->link, Ed3kSets::Policy::Download);
    QVERIFY2(cid, qPrintable(cid ? QString() : cid.error()));
    m_cSet = *cid;
    QTRY_VERIFY_WITH_TIMEOUT(m_c.sets->info(m_cSet)->seq == std::uint64_t(1), 90000);
    const QString dir = m_c.sets->info(m_cSet)->dir;
    QTRY_VERIFY_WITH_TIMEOUT(readFile(QDir(dir).filePath(QStringLiteral("a.bin"))) == payload(300 * 1024 + 5, 1)
                                 && readFile(QDir(dir).filePath(QStringLiteral("sub/b.bin"))) == payload(120 * 1024 + 9, 2),
                             120000);
    // an open member: A's MEMBER record names the set (file-set.md §6)
    QTRY_VERIFY_WITH_TIMEOUT(memberSets(*m_c.session, rootOf(payload(300 * 1024 + 5, 1))) >= 1, 60000);
}

// a new version (seq + 1): the member kept keeps its root; the follower takes the new one
void tst_Ed3kOwnSets::aNewVersionIsSeenByTheFollower()
{
    QVERIFY(m_cSet != 0);
    QVERIFY(writeFile(QDir(m_folder).filePath(QStringLiteral("c.bin")), payload(80 * 1024 + 3, 3)));
    QSignalSpy done(m_own.get(), &Ed3kOwnSets::versionDone);
    QVERIFY(m_own->publishNewVersion(m_openId));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
    QVERIFY(done.at(0).at(2).toBool());
    QCOMPARE(m_own->info(m_openId)->seq, std::uint64_t(2));
    QCOMPARE(m_own->info(m_openId)->files, 3);
    // a follower searches the head every 1 to 6 h (file-set.md §5.3): C starts again, under a
    // new node key (as tst_Ed3kSets::restartC: A and B still hold the old one's connection)
    m_c.stop();
    QVERIFY(QFile::remove(QDir(stateDir(m_dc.path())).filePath(QStringLiteral("node.key"))));
    QVERIFY(m_c.start(m_dc.path(), nodeOf(m_b)));
    QTRY_VERIFY_WITH_TIMEOUT(m_c.session->contactCount() > 0, 20000);
    QVERIFY(m_c.sets->info(m_cSet));
    QTRY_VERIFY_WITH_TIMEOUT(m_c.sets->info(m_cSet)->seq == std::uint64_t(2), 120000);
    const QString dir = m_c.sets->info(m_cSet)->dir;
    QTRY_VERIFY_WITH_TIMEOUT(readFile(QDir(dir).filePath(QStringLiteral("c.bin"))) == payload(80 * 1024 + 3, 3), 120000);
}

// at start the version kept is published again (same seq), its head signed again
void tst_Ed3kOwnSets::aRestartPublishesTheVersionKept()
{
    const auto before = m_own->manifestOf(m_openId);
    QVERIFY(before);
    m_own->stop();
    QVERIFY(!m_own->manifestOf(m_openId));
    QSignalSpy done(m_own.get(), &Ed3kOwnSets::versionDone);
    m_own->start();
    QTRY_VERIFY_WITH_TIMEOUT(done.count() >= 1, 60000);
    QVERIFY(done.at(0).at(2).toBool());
    QCOMPARE(m_own->info(m_openId)->seq, std::uint64_t(2));
    QCOMPARE(m_own->manifestOf(m_openId), before);
    QVERIFY(m_own->sharesRoot(*before));
    // and from its state file (a new object, the daemon's next start)
    m_own.reset();
    m_own = std::make_unique<Ed3kOwnSets>(m_a, m_da.path());
    m_own->setSeller(m_seller.get());
    Ed3kOwnSetsBridge::setOwnSets(m_own.get());
    Ed3kPaidSellerBridge::setOwnSets(m_own.get());
    QVERIFY(waitVersion(m_openId));
    QCOMPARE(m_own->info(m_openId)->seq, std::uint64_t(2));
    QCOMPARE(m_own->manifestOf(m_openId), before);
}

// hidden-set.md: the link with `s` only from GetEd3kOwnSetLink; members sealed (§4.2)
void tst_Ed3kOwnSets::aHiddenSetsLinkComesOnlyFrom1148()
{
    const QString folder = QDir(m_files.path()).filePath(QStringLiteral("Private"));
    const QByteArray plain = payload(90 * 1024 + 1, 5);
    QVERIFY(writeFile(QDir(folder).filePath(QStringLiteral("p.bin")), plain));
    IpcMessage req(IpcMsgType::PublishEd3kOwnSet);
    req.append(folder);
    req.append(QCborArray{});
    req.append(QStringLiteral("Private"));
    req.append(qint64(2));
    QSignalSpy done(m_own.get(), &Ed3kOwnSets::versionDone);
    const IpcMessage r = ask(&Ed3kOwnSetsBridge::handleRequest, std::move(req));
    QVERIFY2(r.fieldBool(0), qPrintable(r.fieldMap(1).value(QStringLiteral("error")).toString()));
    const QString id = r.fieldMap(1).value(QStringLiteral("id")).toString();
    QVERIFY(r.fieldMap(1).value(QStringLiteral("link")).toString().isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
    QVERIFY(done.at(0).at(2).toBool());
    // the list carries no link for it
    const IpcMessage list = ask(&Ed3kOwnSetsBridge::handleRequest, IpcMessage(IpcMsgType::GetEd3kOwnSets));
    bool listed = false;
    for (const auto& v : list.fieldMap(1).value(QStringLiteral("sets")).toArray()) {
        const QCborMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() != id)
            continue;
        listed = true;
        QCOMPARE(m.value(QStringLiteral("kind")).toInteger(), 2);
        QVERIFY(m.value(QStringLiteral("link")).toString().isEmpty());
    }
    QVERIFY(listed);
    IpcMessage linkReq(IpcMsgType::GetEd3kOwnSetLink);
    linkReq.append(id);
    const IpcMessage l = ask(&Ed3kOwnSetsBridge::handleRequest, std::move(linkReq));
    QVERIFY(l.fieldBool(0));
    const QString link = l.fieldMap(1).value(QStringLiteral("link")).toString();
    QVERIFY(link.contains(QStringLiteral("&s=")));
    QVERIFY(l.fieldMap(1).value(QStringLiteral("hidden")).toBool());
    // the member is shared sealed only, under its wire name
    QVERIFY(!m_own->sharesRoot(rootOf(plain)));
    const QDir sealedDir(QDir(m_own->setDir(id)).filePath(QStringLiteral(".sealed")));
    const QStringList sealed = sealedDir.entryList(QDir::Files);
    QCOMPARE(sealed.size(), 1);
    const QByteArray sealedBytes = readFile(sealedDir.filePath(sealed.at(0)));
    QCOMPARE(sealedBytes.size(), plain.size() + 12);
    QCOMPARE(QString::fromStdString(*::ed3k::sealedName(rootOf(sealedBytes))), sealed.at(0));
    QVERIFY(m_own->sharesRoot(rootOf(sealedBytes)));

    // a follower with the link opens it
    Follower d;
    QVERIFY(d.start(m_dd.path(), nodeOf(m_b)));
    QTRY_VERIFY_WITH_TIMEOUT(d.session->contactCount() > 0, 20000);
    const auto did = d.sets->follow(link, Ed3kSets::Policy::Download);
    QVERIFY2(did, qPrintable(did ? QString() : did.error()));
    QTRY_VERIFY_WITH_TIMEOUT(d.sets->info(*did)->seq == std::uint64_t(1), 90000);
    QTRY_VERIFY_WITH_TIMEOUT(readFile(QDir(d.sets->info(*did)->dir).filePath(QStringLiteral("p.bin"))) == plain, 120000);
    d.stop();
}

// subscription.md §8.1, §8.2: sold through SellEd3k kind 2 (target = the set id); members sealed
// into .sealed/, no plain root shared; a voucher holder opens one
void tst_Ed3kOwnSets::aPaidCatalogueIsSealedAndAPassHolderOpensIt()
{
    const QString folder = QDir(m_files.path()).filePath(QStringLiteral("Season"));
    const QByteArray plain = payload(150 * 1024 + 7, 9);
    QVERIFY(writeFile(QDir(folder).filePath(QStringLiteral("e.bin")), plain));
    const auto id = m_own->create(QStringLiteral("Season"), folder, {}, Ed3kOwnSets::Kind::Paid);
    QVERIFY2(id, qPrintable(id ? QString() : id.error()));
    // not published before it is sold
    QTest::qWait(200);
    QCOMPARE(m_own->info(*id)->seq, std::uint64_t(0));
    QVERIFY(!m_own->info(*id)->published);
    QVERIFY(!m_own->publishNewVersion(*id));

    IpcMessage sell(IpcMsgType::SellEd3k);
    sell.append(qint64(2));
    sell.append(*id);
    sell.append(QStringLiteral("Season"));
    sell.append(QCborMap{{QStringLiteral("price"), QStringLiteral("2 EUR")}});
    QSignalSpy done(m_own.get(), &Ed3kOwnSets::versionDone);
    const IpcMessage r = ask(&Ed3kPaidSellerBridge::handleRequest, std::move(sell));
    QVERIFY2(r.fieldBool(0), qPrintable(r.fieldMap(1).value(QStringLiteral("error")).toString()));
    const QString scope = r.fieldMap(1).value(QStringLiteral("scope")).toString();
    QCOMPARE(scope.size(), 32);
    QTRY_VERIFY_WITH_TIMEOUT(done.count() >= 1, 60000);
    QVERIFY(done.at(0).at(2).toBool());
    const auto info = m_own->info(*id);
    QCOMPARE(info->scope, scope);
    QVERIFY(info->sold);
    QCOMPARE(info->kind, Ed3kOwnSets::Kind::Paid);
    // the manifest: every member with the epoch, the offer in it (§8.2)
    const auto man = Ed3kSetManifest::parse(readBlob(QDir(QDir(stateDir(m_da.path())).filePath(QStringLiteral("own-sets"))).filePath(*id + QStringLiteral(".manifest"))));
    QVERIFY(man);
    QCOMPARE(int(man->files.size()), 1);
    QVERIFY(man->files.at(0).epoch.has_value());
    QCOMPARE(man->offer, m_seller->offerOf(scope));
    // sealed into .sealed/<wire name>; the plain file is not shared (SOURCE of the sealed one only)
    const QDir sealedDir(QDir(m_own->setDir(*id)).filePath(QStringLiteral(".sealed")));
    QCOMPARE(sealedDir.entryList(QDir::Files).size(), 1);
    const QByteArray sealedBytes = readFile(sealedDir.filePath(sealedDir.entryList(QDir::Files).at(0)));
    QCOMPARE(man->files.at(0).root, rootOf(sealedBytes));
    QVERIFY(m_own->sharesRoot(rootOf(sealedBytes)));
    QVERIFY(!m_own->sharesRoot(rootOf(plain)));

    // a stop is a newer offer: a new version carries it, the member kept (epoch, root)
    QSignalSpy again(m_own.get(), &Ed3kOwnSets::versionDone);
    QVERIFY(m_seller->stopSale(scope));
    QTRY_VERIFY_WITH_TIMEOUT(again.count() >= 1, 30000);
    QCOMPARE(m_own->info(*id)->seq, std::uint64_t(2));
    QVERIFY(m_own->info(*id)->stopped);
    const auto man2 = Ed3kSetManifest::parse(readBlob(QDir(QDir(stateDir(m_da.path())).filePath(QStringLiteral("own-sets"))).filePath(*id + QStringLiteral(".manifest"))));
    QVERIFY(man2);
    QCOMPARE(man2->files.at(0).root, man->files.at(0).root);
    QCOMPARE(man2->files.at(0).epoch, man->files.at(0).epoch);
    QCOMPARE(man2->offer, m_seller->offerOf(scope));
    QVERIFY(m_seller->resumeSale(scope));
    QTRY_VERIFY_WITH_TIMEOUT(m_own->info(*id)->seq == std::uint64_t(3), 30000);

    // E: a voucher (by hand, §9.3), then the catalogue's ordinary link
    Follower e;
    QVERIFY(e.start(m_de.path(), nodeOf(m_b)));
    QTRY_VERIFY_WITH_TIMEOUT(e.session->contactCount() > 0, 20000);
    const auto pass = m_seller->issuePass({scope}, std::nullopt, m_a.now().unixS + 7 * 86400);
    QVERIFY2(pass, qPrintable(pass ? QString() : pass.error()));
    QVERIFY(e.paid->addPassLink(*pass));
    const auto setId = e.sets->follow(info->link, Ed3kSets::Policy::Download);
    QVERIFY(setId);
    QTRY_VERIFY_WITH_TIMEOUT(e.sets->info(*setId)->seq == std::uint64_t(3), 90000);
    QCOMPARE(e.sets->info(*setId)->paidScope, scope);
    QVERIFY(e.paid->requestKeys(scope, sourceOf(m_a)));
    QTRY_VERIFY_WITH_TIMEOUT(readFile(QDir(e.sets->info(*setId)->dir).filePath(QStringLiteral("e.bin"))) == plain, 120000);
    // §8.1, hidden-set.md §4.3: no MEMBER record for a sealed member
    QCOMPARE(memberSets(*e.session, rootOf(sealedBytes)), 0);
    e.stop();
}

void tst_Ed3kOwnSets::theIpcListsPublishesAndRetitles()
{
    const QString file = QDir(m_files.path()).filePath(QStringLiteral("loose/one.bin"));
    QVERIFY(writeFile(file, payload(10 * 1024, 11)));
    IpcMessage req(IpcMsgType::PublishEd3kOwnSet);
    req.append(QString());
    req.append(QCborArray{file});
    req.append(QStringLiteral("Loose"));
    req.append(qint64(1));
    QSignalSpy done(m_own.get(), &Ed3kOwnSets::versionDone);
    const IpcMessage r = ask(&Ed3kOwnSetsBridge::handleRequest, std::move(req));
    QVERIFY(r.fieldBool(0));
    const QString id = r.fieldMap(1).value(QStringLiteral("id")).toString();
    QVERIFY(r.fieldMap(1).value(QStringLiteral("link")).toString().startsWith(QStringLiteral("ed3k:set?")));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
    IpcMessage ctl(IpcMsgType::Ed3kOwnSetControl);
    ctl.append(qint64(3));
    ctl.append(id);
    ctl.append(QStringLiteral("Loose files"));
    QVERIFY(ask(&Ed3kOwnSetsBridge::handleRequest, std::move(ctl)).fieldBool(0));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 2, 60000);
    const IpcMessage list = ask(&Ed3kOwnSetsBridge::handleRequest, IpcMessage(IpcMsgType::GetEd3kOwnSets));
    QVERIFY(list.fieldMap(1).value(QStringLiteral("running")).toBool());
    bool seen = false;
    for (const auto& v : list.fieldMap(1).value(QStringLiteral("sets")).toArray()) {
        const QCborMap m = v.toMap();
        if (m.value(QStringLiteral("id")).toString() == id) {
            seen = true;
            QCOMPARE(m.value(QStringLiteral("title")).toString(), QStringLiteral("Loose files"));
            QCOMPARE(m.value(QStringLiteral("seq")).toInteger(), 2);
            QCOMPARE(m.value(QStringLiteral("files")).toInteger(), 1);
        }
    }
    QVERIFY(seen);
    // an unknown id, an unknown action
    IpcMessage bad(IpcMsgType::Ed3kOwnSetControl);
    bad.append(qint64(9));
    bad.append(id);
    QVERIFY(!ask(&Ed3kOwnSetsBridge::handleRequest, std::move(bad)).fieldBool(0));
    IpcMessage none(IpcMsgType::GetEd3kOwnSetLink);
    none.append(QStringLiteral("00"));
    QVERIFY(!ask(&Ed3kOwnSetsBridge::handleRequest, std::move(none)).fieldBool(0));
}

// ed3k_set_unpublish: the head no longer signed, the files no longer shared; listed still
void tst_Ed3kOwnSets::unpublishEndsTheHead()
{
    const auto manifest = m_own->manifestOf(m_openId);
    QVERIFY(manifest);
    IpcMessage ctl(IpcMsgType::Ed3kOwnSetControl);
    ctl.append(qint64(2));
    ctl.append(m_openId);
    QVERIFY(ask(&Ed3kOwnSetsBridge::handleRequest, std::move(ctl)).fieldBool(0));
    QVERIFY(!m_own->info(m_openId)->published);
    QVERIFY(!m_own->manifestOf(m_openId));
    QVERIFY(!m_own->sharesRoot(*manifest));
    // the engine publishes it no more: a second unpublish finds nothing
    std::optional<::ed3k::Error> err;
    m_a.post([&](::ed3k::Engine& e, ::ed3k::Now now) {
        if (auto r = e.setUnpublish(now, *manifest); !r)
            err = r.error();
    });
    QCOMPARE(err, std::optional(::ed3k::Error::NotFound));
    // a new version brings it back
    QSignalSpy done(m_own.get(), &Ed3kOwnSets::versionDone);
    QVERIFY(m_own->publishNewVersion(m_openId));
    QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 60000);
    QVERIFY(m_own->info(m_openId)->published);
    QCOMPARE(m_own->info(m_openId)->seq, std::uint64_t(3));
}

QTEST_GUILESS_MAIN(tst_Ed3kOwnSets)
#include "tst_Ed3kOwnSets.moc"
