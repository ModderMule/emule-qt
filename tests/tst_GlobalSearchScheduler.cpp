/// @file tst_GlobalSearchScheduler.cpp
/// @brief Tests for search/GlobalSearchScheduler — the paced ED2K global UDP sweep.
///
/// The point of this class is that it does *not* blast every server at once: MFC
/// asks one server per 750 ms and stops after a single pass. Both halves are pinned
/// here — the rotation rule as a pure function, and the pacing through the progress
/// signal, which fires once per tick whether or not a packet could be built.

#include "TestHelpers.h"
#include "app/AppContext.h"
#include "prefs/Preferences.h"
#include "search/GlobalSearchScheduler.h"
#include "protocol/Tag.h"
#include "search/SearchFile.h"
#include "search/SearchList.h"
#include "search/SearchParams.h"
#include "utils/Opcodes.h"
#include "server/Server.h"
#include "server/ServerList.h"
#include "utils/SafeFile.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

#include <cstring>
#include <memory>

using namespace eMule;

namespace {

/// Point theApp at a local server list for the duration of a case.
struct ScopedServerList {
    explicit ScopedServerList(ServerList* list) : m_saved(theApp.serverList)
    {
        theApp.serverList = list;
    }
    ~ScopedServerList() { theApp.serverList = m_saved; }
    ServerList* m_saved;
};

std::unique_ptr<Server> makeServer(uint32 ip, uint16 port, uint32 failedCount = 0)
{
    auto srv = std::make_unique<Server>(ip, port);
    srv->setFailedCount(failedCount);
    return srv;
}

/// Fill @p list with @p count public servers on distinct IPs.
/// The IP is ED2K byte order (first octet in the LSB), so vary the third octet —
/// stepping the LSB would produce a first octet of 0, which addServer rejects.
void fillServers(ServerList& list, int count)
{
    for (int i = 0; i < count; ++i)
        list.addServer(makeServer(0x08080808u + (static_cast<uint32>(i) << 16),
                                  static_cast<uint16>(4661 + i)));
    Q_ASSERT(static_cast<int>(list.serverCount()) == count);
}

} // namespace

class tst_GlobalSearchScheduler : public QObject {
    Q_OBJECT

private slots:
    // nextGlobalSearchTarget — the rotation rule
    void target_skipsTheConnectedServer();
    void target_skipsTheConnectedServerCopy();
    void target_skipsDeadServers();
    void target_stopsAfterOnePass();
    void target_allDeadTerminates();
    void target_emptyListTerminates();

    // GlobalSearchScheduler — the pacing
    void sweep_asksOneServerPerInterval();
    void sweep_cancelStopsTheTicks();
    void sweep_stopsOnSourcesNotFiles();
    void sweep_cancelSearchIgnoresOtherSearches();
    void sweep_startFromTheEndSignalSurvives();
    void sweep_emptyServerListEndsImmediately();
};

// ---------------------------------------------------------------------------
// nextGlobalSearchTarget
// ---------------------------------------------------------------------------

void tst_GlobalSearchScheduler::target_skipsTheConnectedServer()
{
    ServerList list;
    fillServers(list, 5);
    const Server* connected = list.serverAt(0);

    uint32 examined = 0;
    for (int i = 0; i < 4; ++i) {
        const Server* picked = nextGlobalSearchTarget(list, connected, 3, examined);
        QVERIFY(picked == nullptr || picked != connected);
    }
}

void tst_GlobalSearchScheduler::target_skipsTheConnectedServerCopy()
{
    // ServerConnect::currentServer() is the socket's copy, never a list pointer — the
    // skip has to go by server id or the connected server is asked over UDP too.
    ServerList list;
    fillServers(list, 5);
    const Server copy(*list.serverAt(1));

    uint32 examined = 0;
    for (int i = 0; i < 5; ++i) {
        const Server* picked = nextGlobalSearchTarget(list, &copy, 3, examined);
        QVERIFY(picked == nullptr || picked->serverId() != copy.serverId());
    }
}

void tst_GlobalSearchScheduler::target_skipsDeadServers()
{
    ServerList list;
    // Index 0 is well past the retry limit; every other entry is healthy.
    list.addServer(makeServer(0x08080808, 4661, /*failedCount*/ 10));
    list.addServer(makeServer(0x08090808, 4662));
    list.addServer(makeServer(0x080A0808, 4663));

    uint32 examined = 0;
    const Server* picked = nextGlobalSearchTarget(list, nullptr, /*deadServerRetries*/ 3, examined);
    QVERIFY(picked != nullptr);
    QCOMPARE(picked->failedCount(), uint32{0});

    // A disabled server is skipped whatever its count and whatever the limit.
    ServerList second;
    Server* off = second.addServer(makeServer(0x08080808, 4661));
    second.addServer(makeServer(0x08090808, 4662));
    second.addServer(makeServer(0x080A0808, 4663));
    off->setDisabled(true);
    examined = 0;
    picked = nextGlobalSearchTarget(second, nullptr, /*deadServerRetries*/ 0, examined);
    QVERIFY(picked != nullptr);
    QVERIFY(picked != off);
}

void tst_GlobalSearchScheduler::target_stopsAfterOnePass()
{
    ServerList list;
    fillServers(list, 6);

    // examined is carried across calls, so the sweep walks the list once and then
    // reports done rather than looping forever. MFC's counter is pre-incremented,
    // which is why the last server of the list is never reached.
    uint32 examined = 0;
    int asked = 0;
    for (int i = 0; i < 100; ++i) {
        if (nextGlobalSearchTarget(list, nullptr, 3, examined) == nullptr)
            break;
        ++asked;
    }
    QCOMPARE(asked, 5);
    QVERIFY(nextGlobalSearchTarget(list, nullptr, 3, examined) == nullptr);
}

void tst_GlobalSearchScheduler::target_allDeadTerminates()
{
    ServerList list;
    for (int i = 0; i < 5; ++i)
        list.addServer(makeServer(0x08080808u + (static_cast<uint32>(i) << 16),
                                  static_cast<uint16>(4661 + i), /*failedCount*/ 10));

    // Every candidate is rejected — the inner loop must run out rather than spin.
    uint32 examined = 0;
    QVERIFY(nextGlobalSearchTarget(list, nullptr, 3, examined) == nullptr);
}

void tst_GlobalSearchScheduler::target_emptyListTerminates()
{
    ServerList list;
    uint32 examined = 0;
    QVERIFY(nextGlobalSearchTarget(list, nullptr, 3, examined) == nullptr);
}

// ---------------------------------------------------------------------------
// GlobalSearchScheduler
// ---------------------------------------------------------------------------

void tst_GlobalSearchScheduler::sweep_asksOneServerPerInterval()
{
    ServerList list;
    fillServers(list, 6);
    ScopedServerList scoped(&list);

    GlobalSearchScheduler sched;
    QSignalSpy spy(&sched, &GlobalSearchScheduler::progress);
    QVERIFY(spy.isValid());

    QElapsedTimer clock;
    clock.start();
    // awaitLocalAnswer=false: no TCP request went out, so there is nothing to wait
    // for and the sweep starts at once (the Kad-only case).
    sched.start(7, QByteArrayLiteral("payload"), false, /*awaitLocalAnswer*/ false);

    // One emission for the start, then one per tick. Two ticks must take ~1.5s —
    // an unthrottled sweep would have emptied the whole list in one pass of the
    // event loop.
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 3, 4000);
    QVERIFY2(clock.elapsed() > 1200,
             qPrintable(QStringLiteral("two ticks took only %1ms").arg(clock.elapsed())));

    const auto first = spy.at(0);
    QCOMPARE(first.at(0).toUInt(), 7u);
    QCOMPARE(first.at(2).toUInt(), 6u);      // total = server count
    QVERIFY(first.at(3).toBool());           // running

    // Progress is monotonic and never exceeds the list size.
    uint32 previous = 0;
    for (const auto& emission : spy) {
        const auto asked = emission.at(1).toUInt();
        QVERIFY(asked >= previous);
        QVERIFY(asked <= 6u);
        previous = asked;
    }
}

void tst_GlobalSearchScheduler::sweep_cancelStopsTheTicks()
{
    ServerList list;
    fillServers(list, 20);
    ScopedServerList scoped(&list);

    GlobalSearchScheduler sched;
    sched.start(9, QByteArrayLiteral("payload"), false, false);
    QVERIFY(sched.isRunning());

    QSignalSpy spy(&sched, &GlobalSearchScheduler::progress);
    QVERIFY(spy.isValid());

    sched.cancel();
    QVERIFY(!sched.isRunning());

    // The closing emission reports running=false so a listener can hide its display.
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toUInt(), 9u);
    QVERIFY(!spy.at(0).at(3).toBool());

    // ...and nothing more arrives, however long we wait.
    spy.clear();
    QTest::qWait(1700);
    QCOMPARE(spy.count(), 0);
}

void tst_GlobalSearchScheduler::sweep_stopsOnSourcesNotFiles()
{
    // MFC stops once the summed availability passes MAX_RESULTS (100), however few
    // files that is — SearchResultsWnd.cpp:473.
    ServerList servers;
    fillServers(servers, 20);
    ScopedServerList scoped(&servers);

    SearchList list;
    SearchList* const savedList = theApp.searchList;
    theApp.searchList = &list;
    const uint32 id = list.newSearch({}, SearchParams{});

    const auto add = [&](uint8 hashByte, uint32 sources) {
        uint8 hash[16];
        std::memset(hash, hashByte, 16);
        SafeMemFile mem;
        mem.write(hash, 16);
        mem.writeUInt32(0x0A000001);
        mem.writeUInt16(4662);
        mem.writeUInt32(3);
        Tag(FT_FILENAME, QStringLiteral("f%1.avi").arg(hashByte)).writeNewEd2kTag(mem, UTF8Mode::Raw);
        Tag(FT_FILESIZE, uint32{10000}).writeNewEd2kTag(mem);
        Tag(FT_SOURCES, sources).writeNewEd2kTag(mem);
        const QByteArray packet = mem.takeBuffer();
        SafeMemFile data(packet);
        auto* file = new SearchFile(data, true, 0xC0A80001, 4661);
        file->setSearchID(id);
        list.addToList(file);
    };

    GlobalSearchScheduler sched;
    sched.start(id, QByteArrayLiteral("payload"), false, false);
    QVERIFY(sched.isRunning());

    add(1, 40);
    add(2, 40);
    sched.onResultCountChanged(id);
    QVERIFY(sched.isRunning());   // 80 sources

    add(3, 40);
    sched.onResultCountChanged(id);
    QVERIFY(!sched.isRunning());  // 120 sources in three files

    theApp.searchList = savedList;
}

void tst_GlobalSearchScheduler::sweep_cancelSearchIgnoresOtherSearches()
{
    ServerList list;
    fillServers(list, 10);
    ScopedServerList scoped(&list);

    GlobalSearchScheduler sched;
    sched.start(11, QByteArrayLiteral("payload"), false, false);

    // Closing some other search's tab must not kill this sweep.
    sched.cancelSearch(12);
    QVERIFY(sched.isRunning());

    sched.cancelSearch(11);
    QVERIFY(!sched.isRunning());
}

void tst_GlobalSearchScheduler::sweep_startFromTheEndSignalSurvives()
{
    ServerList list;
    fillServers(list, 10);
    ScopedServerList scoped(&list);

    // The search queue sends the next search from inside the end signal of the one
    // before. cancel() used to wipe it on the way out: its timer ran on, the sweep
    // never started and the server lane stayed taken.
    GlobalSearchScheduler sched;
    bool chained = false;
    connect(&sched, &GlobalSearchScheduler::progress, &sched,
            [&](uint32 searchID, uint32, uint32, bool running) {
                if (!running && searchID == 21 && !std::exchange(chained, true))
                    sched.start(22, QByteArrayLiteral("next"), false, /*awaitLocalAnswer*/ true);
            });

    sched.start(21, QByteArrayLiteral("payload"), false, false);
    sched.cancelSearch(21);
    QVERIFY(chained);
    QVERIFY(sched.isRunning());

    // the local answer starts its sweep, and it reports under its own id
    QSignalSpy spy(&sched, &GlobalSearchScheduler::progress);
    sched.onLocalAnswerReceived();
    QVERIFY(spy.count() >= 1);
    QCOMPARE(spy.last().at(0).toUInt(), 22u);
    QVERIFY(spy.last().at(3).toBool());

    sched.cancelSearch(22);
    QVERIFY(!sched.isRunning());
}

void tst_GlobalSearchScheduler::sweep_emptyServerListEndsImmediately()
{
    ServerList list;
    ScopedServerList scoped(&list);

    GlobalSearchScheduler sched;
    sched.start(13, QByteArrayLiteral("payload"), false, false);
    QVERIFY(!sched.isRunning());
}

QTEST_MAIN(tst_GlobalSearchScheduler)
#include "tst_GlobalSearchScheduler.moc"
