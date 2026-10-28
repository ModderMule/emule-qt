/// @file tst_Ed3kFilters.cpp
/// @brief Shared filter lists (filter-list.md) in the daemon (Ed3kFilters, Ed3kFiltersBridge):
/// follow by link, a FilterFetch answered by fetching the list file from another node and
/// handing it to the core, a list file judging an entry (hidden once the mode says so), own
/// verdicts kept across a restart, the IPC.

#include "Ed3kBridge.h"
#include "Ed3kFilters.h"
#include "Ed3kFiltersBridge.h"
#include "Ed3kJoinBase.h"
#include "Ed3kPlumbing.h"
#include "Ed3kSearch.h"
#include "Ed3kSession.h"
#include "Ed3kSetManifest.h"
#include "Ed3kStore.h"

#include "app/AppContext.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"

#include <QCborArray>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;
using namespace eMule::ed3k;
using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

/// Node A's side: serves blobs from memory.
class Sharer final : public Ed3kJoinBase {
public:
    Sharer(Ed3kSession& s, const QString& dir) : Ed3kJoinBase(s, dir, QStringLiteral("share"), nullptr) {}
    void start() override { beginStart(); }
    void stop() override { closeAll(); }
    std::optional<::ed3k::Root> share(const QByteArray& data) { return shareBlob(data, QStringLiteral("fakes.ed3kflt"), std::nullopt); }

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
    s.network = QStringLiteral("ed3k test");
    s.forceOpen = true;
    return s;
}

::ed3k::Source sourceOf(const Ed3kSession& s)
{
    return ::ed3k::Source{*s.nodeId(), false, ::ed3k::Address::v4(127, 0, 0, 1, s.udpPort()), std::nullopt, {}};
}

QString setLink(std::uint8_t fill, const QByteArray& id)
{
    ::ed3k::Key32 key{};
    key.fill(fill);
    return QStringLiteral("ed3k:set?key=%1&id=%2")
        .arg(base32(key), base32(std::span(reinterpret_cast<const std::uint8_t*>(id.constData()), std::size_t(id.size()))));
}

Ed3kFilters::EntryId entry(std::uint8_t b)
{
    Ed3kFilters::EntryId id{};
    id.fill(b);
    return id;
}

/// A list file judging FILE entry(5) as FAKE: node @p s's own list, written (filterOwnWrite).
QByteArray listFile(Ed3kSession& s)
{
    auto* e = s.engine();
    if (!e || !e->filterSetOwnTitle(s.now(), "fakes"))
        return {};
    if (!e->filterSetOwn(s.now(), ::ed3k::FilterSubject::File, entry(5), ::ed3k::FilterVerdict::Fake, "not the film"))
        return {};
    const auto file = e->filterOwnWrite(s.now());
    if (!file)
        return {};
    return QByteArray(reinterpret_cast<const char*>(file->bytes().data()), qsizetype(file->size()));
}

} // namespace

class tst_Ed3kFilters : public QObject {
    Q_OBJECT

private slots:
    void cleanup();
    void followByLinkAndUnfollow();
    void aFilterFetchIsFetchedAndHandedIn();
    void aListJudgesAndHidesOnlyWhenSet();
    void ownVerdictsAreKeptAndOverrule();
    void ipcFollowsAndJudges();
    // batch 13
    void theOwnListIsPublishedAndAgainOnChange();
    void showAnywayOverrulesAHidingList();
    void aMarkJudgesTheSearchRowsAgain();
    void aSetAuthorJudgesTheRow();
    void ipcPublishesAndShowsAnyway();
};

void tst_Ed3kFilters::cleanup()
{
    Ed3kFiltersBridge::setFilters(nullptr);
    Ed3kBridge::setSession(nullptr);
}

void tst_Ed3kFilters::followByLinkAndUnfollow()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kFilters f(s, dir.path());
    QVERIFY(f.isRunning());
    QSignalSpy changed(&f, &Ed3kFilters::filtersChanged);
    // §3: only what the user chose; nothing followed at first
    QVERIFY(f.lists().empty());
    const auto id = f.follow(setLink(9, "fakes"), QStringLiteral("Fakes of 9"));
    QVERIFY2(id, "filterFollow refused");
    QVERIFY(changed.count() >= 1);
    const auto lists = f.lists();
    QCOMPARE(lists.size(), std::size_t(1));
    QCOMPARE(lists.front().id, *id);
    QCOMPARE(lists.front().label, QStringLiteral("Fakes of 9"));
    QVERIFY(!lists.front().byHand);
    QVERIFY(lists.front().set.has_value());
    QVERIFY(!lists.front().seq.has_value());   // no version yet
    QVERIFY(!lists.front().hide && !lists.front().refuse);   // the default is to mark
    QVERIFY(!f.follow(QStringLiteral("ed3k:set?key=nope"), {}));
    QVERIFY(f.unfollow(*id));
    QVERIFY(f.lists().empty());
    f.stop();
    s.stop();
}

void tst_Ed3kFilters::aFilterFetchIsFetchedAndHandedIn()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    const QByteArray file = listFile(a);
    QVERIFY(!file.isEmpty());
    Sharer share(a, da.path());
    share.start();
    const auto root = share.share(file);
    QVERIFY(root);

    Ed3kFilters f(b, db.path());
    f.setSearchEnabled(false);
    const auto id = f.follow(setLink(9, "fakes"), QStringLiteral("fakes"));
    QVERIFY(id);
    QSignalSpy fetched(&f, &Ed3kFilters::listFileFetched);
    // the core's ask for the list file of a version (filter-list.md §2), as it comes from a
    // manifest: answered by the host (handled), the file fetched from A
    bool handled = false;
    emit b.eventPolled(::ed3k::event::FilterFetch{*id, *root, std::uint64_t(file.size()), "fakes.ed3kflt"}, &handled);
    QVERIFY(handled);
    f.addListSource(*root, sourceOf(a));
    QTRY_COMPARE_WITH_TIMEOUT(fetched.count(), 1, 30000);
    QCOMPARE(fetched.at(0).at(0).toUInt(), *id);
    // too large (§2): not fetched
    handled = false;
    ::ed3k::Root other;
    other.bytes.fill(3);
    emit b.eventPolled(::ed3k::event::FilterFetch{*id, other, Ed3kFilters::kListFileMax + 1, "big.ed3kflt"}, &handled);
    QVERIFY(handled);
    QTest::qWait(300);
    QCOMPARE(fetched.count(), 1);
    f.stop();
    share.stop();
}

void tst_Ed3kFilters::aListJudgesAndHidesOnlyWhenSet()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    const QByteArray file = listFile(a);
    QVERIFY(!file.isEmpty());

    Ed3kFilters f(b, db.path());
    // nothing judges an entry before any list
    ::ed3k::Root root;
    std::fill(root.bytes.begin(), root.bytes.begin() + 16, std::uint8_t(5));
    QVERIFY(!f.judge(root, std::nullopt, std::nullopt));
    const auto id = f.openFile(QStringLiteral("by hand"), file);
    QVERIFY2(id, "filterOpen refused");
    QVERIFY(f.lists().front().byHand);
    // §3: marked by default, not hidden
    auto j = f.judge(root, std::nullopt, std::nullopt);
    QVERIFY(j);
    QVERIFY(j->verdict == ::ed3k::FilterVerdict::Fake);
    QVERIFY(!j->hide);
    QVERIFY(!j->own);
    QVERIFY(!j->by.isEmpty());
    // the user sets the list to hide: the row is hidden; another entry is not
    QVERIFY(f.setMode(*id, true, false));
    j = f.judge(root, std::nullopt, std::nullopt);
    QVERIFY(j && j->hide && !j->refuse);
    ::ed3k::Root clean;
    clean.bytes.fill(6);
    QVERIFY(!f.judge(clean, std::nullopt, std::nullopt));
    f.stop();
}

void tst_Ed3kFilters::ownVerdictsAreKeptAndOverrule()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto md4 = entry(0x44);
    {
        Ed3kSession s;
        QVERIFY(s.start(loopback(dir.path())));
        Ed3kFilters f(s, dir.path());
        QVERIFY(f.setOwn(::ed3k::FilterSubject::Ed2k, md4, ::ed3k::FilterVerdict::Spam, QStringLiteral("ads")));
        QVERIFY(f.setMode(ED3K_FILTER_OWN, true, true));
        const auto j = f.judge(std::nullopt, md4, std::nullopt);
        QVERIFY(j && j->own && j->verdict == ::ed3k::FilterVerdict::Spam && j->hide && j->refuse);
        QCOMPARE(j->reason, QStringLiteral("ads"));
        f.stop();
        s.stop();   // filter.dat saved
    }
    // a restart: the mirror lists it, the core still judges it
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kFilters f(s, dir.path());
    QCOMPARE(f.own().size(), std::size_t(1));
    QCOMPARE(f.own().front().reason, QStringLiteral("ads"));
    QVERIFY(f.ownHide() && f.ownRefuse());
    QVERIFY(f.judge(std::nullopt, md4, std::nullopt));
    // taken back: none left
    QVERIFY(f.setOwn(::ed3k::FilterSubject::Ed2k, md4, ::ed3k::FilterVerdict::None, {}));
    QVERIFY(f.own().empty());
    QVERIFY(!f.judge(std::nullopt, md4, std::nullopt));
    f.stop();
    s.stop();
}

void tst_Ed3kFilters::ipcFollowsAndJudges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    IpcMessage last;
    const auto reply = [&](const IpcMessage& r) { last = r; };
    // no node: refused with error 0, GetEd3kFilters answers running false
    QVERIFY(Ed3kBridge::handleRequest(IpcMessage(IpcMsgType::GetEd3kFilters, 1), reply));
    QVERIFY(last.fieldBool(0));
    QVERIFY(!last.fieldMap(1).value(QStringLiteral("running")).toBool());

    QVERIFY(s.start(loopback(dir.path())));
    Ed3kFilters f(s, dir.path());
    Ed3kFiltersBridge::setFilters(&f);
    IpcMessage follow(IpcMsgType::FollowEd3kFilter, 2);
    follow.append(setLink(9, "fakes"));
    follow.append(QStringLiteral("nine"));
    QVERIFY(Ed3kBridge::handleRequest(follow, reply));
    QVERIFY(last.fieldBool(0));
    const qint64 id = last.fieldMap(1).value(QStringLiteral("id")).toInteger();

    IpcMessage mode(IpcMsgType::SetEd3kFilterMode, 3);
    mode.append(id);
    mode.append(true);
    mode.append(false);
    QVERIFY(Ed3kBridge::handleRequest(mode, reply));
    QVERIFY(last.fieldBool(0));

    IpcMessage own(IpcMsgType::SetEd3kFilterOwn, 4);
    own.append(qint64(1));
    own.append(QStringLiteral("07").repeated(16));
    own.append(qint64(1));
    own.append(QStringLiteral("fake"));
    QVERIFY(Ed3kBridge::handleRequest(own, reply));
    QVERIFY(last.fieldBool(0));
    IpcMessage badOwn(IpcMsgType::SetEd3kFilterOwn, 5);
    badOwn.append(qint64(9));
    badOwn.append(QStringLiteral("07"));
    badOwn.append(qint64(1));
    badOwn.append(QString());
    QVERIFY(Ed3kBridge::handleRequest(badOwn, reply));
    QVERIFY(!last.fieldBool(0));

    QVERIFY(Ed3kBridge::handleRequest(IpcMessage(IpcMsgType::GetEd3kFilters, 6), reply));
    QVERIFY(last.fieldBool(0));
    const QCborMap m = last.fieldMap(1);
    QVERIFY(m.value(QStringLiteral("running")).toBool());
    QCOMPARE(m.value(QStringLiteral("lists")).toArray().size(), 1);
    QCOMPARE(m.value(QStringLiteral("lists")).toArray().at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("nine"));
    QCOMPARE(m.value(QStringLiteral("lists")).toArray().at(0).toMap().value(QStringLiteral("mode")).toInteger(), 1);
    QCOMPARE(m.value(QStringLiteral("own")).toArray().size(), 1);

    IpcMessage judge(IpcMsgType::JudgeEd3kFilter, 7);
    judge.append(qint64(1));
    judge.append(QStringLiteral("07").repeated(16));
    QVERIFY(Ed3kBridge::handleRequest(judge, reply));
    QVERIFY(last.fieldBool(0));
    QCOMPARE(last.fieldMap(1).value(QStringLiteral("own")).toInteger(), 1);
    f.stop();
    s.stop();
}

// §2: the own list as a signed set; the link shown; a new version on every change; kept
void tst_Ed3kFilters::theOwnListIsPublishedAndAgainOnChange()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString manifestPath = QDir(stateDir(dir.path())).filePath(QStringLiteral("filter-own.ed3kset"));
    QString link;
    {
        Ed3kSession s;
        QVERIFY(s.start(loopback(dir.path())));
        Ed3kFilters f(s, dir.path());
        QVERIFY(f.published().link.isEmpty());
        QVERIFY(f.setOwn(::ed3k::FilterSubject::File, entry(5), ::ed3k::FilterVerdict::Fake, QStringLiteral("not the film")));
        QVERIFY(!f.publishOwn(QString()));   // a title is needed
        const auto published = f.publishOwn(QStringLiteral("My fakes"));
        QVERIFY2(published, "publishOwn refused");
        link = *published;
        QVERIFY(link.startsWith(QStringLiteral("ed3k:set?key=")));
        QVERIFY(link.contains(QStringLiteral("&id=")));
        QCOMPARE(f.published().link, link);
        QCOMPARE(f.published().title, QStringLiteral("My fakes"));
        // the set holds the list file at its top level (§2), signed by the list key, not the node's
        auto m = Ed3kSetManifest::parse(readBlob(manifestPath));
        QVERIFY(m);
        QCOMPARE(m->files.size(), std::size_t(1));
        QCOMPARE(m->files.front().path, QStringLiteral("My fakes.ed3kflt"));
        QCOMPARE(m->key.size(), 32);
        const std::uint64_t seq = m->seq;
        // a change: a new version after the delay
        QVERIFY(f.setOwn(::ed3k::FilterSubject::File, entry(6), ::ed3k::FilterVerdict::Spam, {}));
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            const auto n = Ed3kSetManifest::parse(readBlob(manifestPath));
            return n && n->seq > seq;
        }(), 10000);
        f.stop();
        s.stop();
    }
    // a restart: the same link, shared and published again
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kFilters f(s, dir.path());
    QCOMPARE(f.published().link, link);
    f.unpublishOwn();
    QVERIFY(f.published().link.isEmpty());
    QVERIFY(!QFile::exists(manifestPath));
    f.stop();
    s.stop();
}

// §3: the user's local choice shows what a list hides; taken back, it hides again
void tst_Ed3kFilters::showAnywayOverrulesAHidingList()
{
    QTemporaryDir da, db;
    QVERIFY(da.isValid() && db.isValid());
    Ed3kSession a, b;
    QVERIFY(a.start(loopback(da.path())));
    QVERIFY(b.start(loopback(db.path())));
    const QByteArray file = listFile(a);
    QVERIFY(!file.isEmpty());
    Ed3kFilters f(b, db.path());
    const auto id = f.openFile(QStringLiteral("by hand"), file);
    QVERIFY(id);
    QVERIFY(f.setMode(*id, true, false));
    ::ed3k::Root root;
    std::fill(root.bytes.begin(), root.bytes.begin() + 16, std::uint8_t(5));
    auto j = f.judge(root, std::nullopt, std::nullopt);
    QVERIFY(j && j->hide && !j->shown);
    QVERIFY(f.showAnyway(root, true));
    j = f.judge(root, std::nullopt, std::nullopt);
    QVERIFY(j && !j->hide && j->shown);
    QVERIFY(j->verdict == ::ed3k::FilterVerdict::Fake);   // still marked: the user sees why
    QVERIFY(f.showAnyway(root, false));
    j = f.judge(root, std::nullopt, std::nullopt);
    QVERIFY(j && j->hide && !j->shown);
    f.stop();
}

namespace {

/// The judge DaemonApp gives Ed3kSearch, for @p f (setAuthor: none).
Ed3kSearch::Judge judgeOf(Ed3kFilters& f, std::optional<::ed3k::Key32> setAuthor = std::nullopt)
{
    return [&f, setAuthor](const ::ed3k::FoundFile& found) {
        Ed3kSearch::Verdict v;
        const auto j = f.judge(found.root, found.ed2k, found.publisher, setAuthor);
        if (!j)
            return v;
        v.shown = {int(j->verdict), false, j->own, j->shown, j->disagree, j->by, j->reason};
        if (j->hide)
            v.judged = Ed3kSearch::Judged::Hide;
        else if (j->verdict != ::ed3k::FilterVerdict::Good && j->verdict != ::ed3k::FilterVerdict::None)
            v.judged = Ed3kSearch::Judged::Mark;
        return v;
    };
}

::ed3k::FoundFile found(std::uint8_t fill)
{
    ::ed3k::FoundFile ff;
    ff.root.bytes.fill(fill);
    ff.publisher.bytes.fill(0x70);
    ff.size = 1000;
    ff.name = "film.mkv";
    ff.sources = 1;
    return ff;
}

} // namespace

// "Mark as fake" on a row: the own verdict, the rows judged again (marked, then cleared)
void tst_Ed3kFilters::aMarkJudgesTheSearchRowsAgain()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kFilters f(s, dir.path());
    Ed3kSearch search(s);
    search.setJudge(judgeOf(f));
    SearchList list;
    theApp.searchList = &list;
    struct Reset {
        ~Reset() { theApp.searchList = nullptr; }
    } reset;
    const uint32 sid = list.newSearch(QString(), SearchParams{});
    auto row = Ed3kSearch::toSearchFile(found(0x21));
    QVERIFY(row);
    QVERIFY(!Ed3kSearch::applyVerdict(*row, Ed3kSearch::Verdict{}));   // nothing judges it
    list.addMetaSearchResult(sid, row.release());
    QCOMPARE(list.resultCount(sid), 1u);
    const auto rowOf = [&] {
        const SearchFile* out = nullptr;
        list.forEachResult(sid, [&](const SearchFile* file) { out = file; });
        return out;
    };
    QVERIFY(rowOf() && !rowOf()->isConsideredSpam());
    Ed3kFilters::EntryId fileId;
    fileId.fill(0x21);
    QVERIFY(f.setOwn(::ed3k::FilterSubject::File, fileId, ::ed3k::FilterVerdict::Fake, {}));
    QCOMPARE(search.rejudge(), 1);
    QCOMPARE(rowOf()->ed3kJudged().verdict, int(::ed3k::FilterVerdict::Fake));
    QVERIFY(rowOf()->ed3kJudged().own);
    QVERIFY(!rowOf()->ed3kJudged().hidden);
    QVERIFY(rowOf()->isConsideredSpam());
    QCOMPARE(search.rejudge(), 0);   // unchanged
    // the own list set to hide: hidden, kept
    QVERIFY(f.setMode(ED3K_FILTER_OWN, true, false));
    QCOMPARE(search.rejudge(), 1);
    QVERIFY(rowOf()->ed3kJudged().hidden);
    QCOMPARE(list.resultCount(sid), 1u);
    // "clear": not judged any more
    QVERIFY(f.setOwn(::ed3k::FilterSubject::File, fileId, ::ed3k::FilterVerdict::None, {}));
    QCOMPARE(search.rejudge(), 1);
    QCOMPARE(rowOf()->ed3kJudged().verdict, 0);
    QVERIFY(!rowOf()->ed3kJudged().hidden);
    QVERIFY(!rowOf()->isConsideredSpam());
    // the publisher (AUTHOR, node ID) marks it too
    Ed3kFilters::EntryId pub;
    pub.fill(0x70);
    QVERIFY(f.setOwn(::ed3k::FilterSubject::Author, pub, ::ed3k::FilterVerdict::Spam, {}));
    QCOMPARE(search.rejudge(), 1);
    QCOMPARE(rowOf()->ed3kJudged().verdict, int(::ed3k::FilterVerdict::Spam));
    f.stop();
    s.stop();
}

// #18 (filter-list §1): a signed manifest's set author judges the row too
void tst_Ed3kFilters::aSetAuthorJudgesTheRow()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kFilters f(s, dir.path());
    ::ed3k::Key32 author{};
    author.fill(0x33);
    const auto authorId = ::ed3k::roomMemberId(author);   // H("ed3k v1 node id", key)[0..16)
    QVERIFY(authorId);
    QVERIFY(f.setOwn(::ed3k::FilterSubject::Author, *authorId, ::ed3k::FilterVerdict::Harmful, QStringLiteral("malware")));
    const auto ff = found(0x22);
    // the publisher alone: nothing
    QVERIFY(!f.judge(ff.root, std::nullopt, ff.publisher));
    // the manifest known: its author marks it
    const auto j = f.judge(ff.root, std::nullopt, ff.publisher, author);
    QVERIFY(j);
    QVERIFY(j->verdict == ::ed3k::FilterVerdict::Harmful);
    QCOMPARE(j->reason, QStringLiteral("malware"));
    f.stop();
    s.stop();
}

void tst_Ed3kFilters::ipcPublishesAndShowsAnyway()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Ed3kSession s;
    QVERIFY(s.start(loopback(dir.path())));
    Ed3kFilters f(s, dir.path());
    Ed3kFiltersBridge::setFilters(&f);
    IpcMessage last;
    const auto reply = [&](const IpcMessage& r) { last = r; };
    QVERIFY(Ed3kFiltersBridge::isFiltersRequest(IpcMsgType::PublishEd3kFilterOwn));
    QVERIFY(Ed3kFiltersBridge::isFiltersRequest(IpcMsgType::ShowEd3kFilterAnyway));
    IpcMessage pub(IpcMsgType::PublishEd3kFilterOwn, 1);
    pub.append(QStringLiteral("Mine"));
    pub.append(true);
    QVERIFY(Ed3kBridge::handleRequest(pub, reply));
    QVERIFY(last.fieldBool(0));
    const QString link = last.fieldMap(1).value(QStringLiteral("link")).toString();
    QVERIFY(link.startsWith(QStringLiteral("ed3k:set?")));
    QVERIFY(Ed3kBridge::handleRequest(IpcMessage(IpcMsgType::GetEd3kFilters, 2), reply));
    QCOMPARE(last.fieldMap(1).value(QStringLiteral("published")).toMap().value(QStringLiteral("link")).toString(), link);
    IpcMessage show(IpcMsgType::ShowEd3kFilterAnyway, 3);
    show.append(QStringLiteral("05").repeated(32));
    show.append(true);
    QVERIFY(Ed3kBridge::handleRequest(show, reply));
    QVERIFY(last.fieldBool(0));
    IpcMessage bad(IpcMsgType::ShowEd3kFilterAnyway, 4);
    bad.append(QStringLiteral("05"));
    bad.append(true);
    QVERIFY(Ed3kBridge::handleRequest(bad, reply));
    QVERIFY(!last.fieldBool(0));
    IpcMessage stop(IpcMsgType::PublishEd3kFilterOwn, 5);
    stop.append(QString());
    stop.append(false);
    QVERIFY(Ed3kBridge::handleRequest(stop, reply));
    QVERIFY(last.fieldBool(0));
    QVERIFY(f.published().link.isEmpty());
    f.stop();
    s.stop();
}

QTEST_GUILESS_MAIN(tst_Ed3kFilters)
#include "tst_Ed3kFilters.moc"
