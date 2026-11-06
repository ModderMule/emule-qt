/// @file tst_Scheduler.cpp
/// @brief Tests for transfer/Scheduler.

#include "TestHelpers.h"
#include "transfer/Scheduler.h"
#include "prefs/Preferences.h"

#include <QDateTime>
#include <QSettings>
#include <QTest>

using namespace eMule;

class tst_Scheduler : public QObject {
    Q_OBJECT

private slots:
    void construction_empty();
    void addSchedule_basic();
    void removeSchedule_basic();
    void removeAll_clears();
    void scheduleEntry_resetActions();
    void updateSchedule_replaces();
    void schedule_outOfBounds();
    void saveLoad_roundTrip();
    void check_emptySchedules();
    void activateSchedule_setsPrefs();
    void clearOverrides_restoresStored();
    void check_windowEnd_restoresLimit();
    void network_pauseFiresOnceAndRestores();
    void network_noEndTimeIsPersistent();
    void network_laterWindowTakesOver();
    void network_removeAllRestores();
    void network_usenetCategoryOnce();
    void network_catStopReachesEd3k();
    void network_capsInOverlay();
    void load_dropsUnknownActions();
};

namespace {

/// Records what the scheduler asks of the other networks.
struct RecordingNetwork : ScheduleNetworkActions {
    QStringList calls;
    void usenetPause(bool paused, bool persistent) override
    {
        calls << QStringLiteral("usenet %1%2").arg(paused ? "pause" : "resume", persistent ? " saved" : "");
    }
    void usenetRestore() override { calls << QStringLiteral("usenet restore"); }
    void usenetCategory(int category, bool paused) override
    {
        calls << QStringLiteral("usenet cat %1 %2").arg(category).arg(paused ? "pause" : "resume");
    }
    void btPause(bool paused, bool persistent) override
    {
        calls << QStringLiteral("bt %1%2").arg(paused ? "pause" : "resume", persistent ? " saved" : "");
    }
    void btRestore() override { calls << QStringLiteral("bt restore"); }
    void ed3kCategory(int category, bool paused) override
    {
        calls << QStringLiteral("ed3k cat %1 %2").arg(category).arg(paused ? "pause" : "resume");
    }
};

/// An entry whose window is open now (or, with @p open false, a few hours away).
std::unique_ptr<ScheduleEntry> windowEntry(const QString& title, bool open = true)
{
    const QDateTime now = QDateTime::currentDateTime();
    auto e = std::make_unique<ScheduleEntry>();
    e->title = title;
    e->enabled = true;
    e->startTime = (open ? now.addSecs(-3600) : now.addSecs(2 * 3600)).toSecsSinceEpoch();
    e->endTime = (open ? now.addSecs(3600) : now.addSecs(3 * 3600)).toSecsSinceEpoch();
    return e;
}

void closeWindow(ScheduleEntry* e)
{
    const QDateTime now = QDateTime::currentDateTime();
    e->startTime = now.addSecs(2 * 3600).toSecsSinceEpoch();
    e->endTime = now.addSecs(3 * 3600).toSecsSinceEpoch();
}

void openWindow(ScheduleEntry* e)
{
    const QDateTime now = QDateTime::currentDateTime();
    e->startTime = now.addSecs(-3600).toSecsSinceEpoch();
    e->endTime = now.addSecs(3600).toSecsSinceEpoch();
}

} // namespace

void tst_Scheduler::construction_empty()
{
    Scheduler scheduler;
    QCOMPARE(scheduler.count(), 0);
    QVERIFY(scheduler.schedule(0) == nullptr);
    QVERIFY(scheduler.schedule(-1) == nullptr);
}

void tst_Scheduler::addSchedule_basic()
{
    Scheduler scheduler;

    auto entry = std::make_unique<ScheduleEntry>();
    entry->title = QStringLiteral("Night Schedule");
    entry->enabled = true;
    entry->day = ScheduleDay::Daily;
    entry->actions[0] = ScheduleAction::SetUpload;
    entry->values[0] = QStringLiteral("50");

    int index = scheduler.addSchedule(std::move(entry));
    QCOMPARE(index, 0);
    QCOMPARE(scheduler.count(), 1);

    ScheduleEntry* stored = scheduler.schedule(0);
    QVERIFY(stored != nullptr);
    QCOMPARE(stored->title, QStringLiteral("Night Schedule"));
    QVERIFY(stored->enabled);
    QCOMPARE(stored->day, ScheduleDay::Daily);
    QCOMPARE(stored->actions[0], ScheduleAction::SetUpload);
    QCOMPARE(stored->values[0], QStringLiteral("50"));
}

void tst_Scheduler::removeSchedule_basic()
{
    Scheduler scheduler;

    auto e1 = std::make_unique<ScheduleEntry>();
    e1->title = QStringLiteral("First");
    auto e2 = std::make_unique<ScheduleEntry>();
    e2->title = QStringLiteral("Second");

    scheduler.addSchedule(std::move(e1));
    scheduler.addSchedule(std::move(e2));
    QCOMPARE(scheduler.count(), 2);

    scheduler.removeSchedule(0);
    QCOMPARE(scheduler.count(), 1);
    QCOMPARE(scheduler.schedule(0)->title, QStringLiteral("Second"));

    // Out-of-bounds removal is a no-op
    scheduler.removeSchedule(999);
    QCOMPARE(scheduler.count(), 1);
}

void tst_Scheduler::removeAll_clears()
{
    Scheduler scheduler;

    for (int i = 0; i < 5; ++i) {
        auto e = std::make_unique<ScheduleEntry>();
        e->title = QStringLiteral("Entry %1").arg(i);
        scheduler.addSchedule(std::move(e));
    }
    QCOMPARE(scheduler.count(), 5);

    scheduler.removeAll();
    QCOMPARE(scheduler.count(), 0);
    QVERIFY(scheduler.schedule(0) == nullptr);
}

void tst_Scheduler::scheduleEntry_resetActions()
{
    ScheduleEntry entry;
    entry.actions[0] = ScheduleAction::SetUpload;
    entry.values[0] = QStringLiteral("100");
    entry.actions[1] = ScheduleAction::SetDownload;
    entry.values[1] = QStringLiteral("200");

    entry.resetActions();

    for (int i = 0; i < 16; ++i) {
        QCOMPARE(entry.actions[static_cast<size_t>(i)], ScheduleAction::None);
        QVERIFY(entry.values[static_cast<size_t>(i)].isEmpty());
    }
}

void tst_Scheduler::updateSchedule_replaces()
{
    Scheduler scheduler;

    auto orig = std::make_unique<ScheduleEntry>();
    orig->title = QStringLiteral("Original");
    scheduler.addSchedule(std::move(orig));

    auto updated = std::make_unique<ScheduleEntry>();
    updated->title = QStringLiteral("Updated");
    scheduler.updateSchedule(0, std::move(updated));

    QCOMPARE(scheduler.count(), 1);
    QCOMPARE(scheduler.schedule(0)->title, QStringLiteral("Updated"));

    // Out-of-bounds update is a no-op
    auto invalid = std::make_unique<ScheduleEntry>();
    invalid->title = QStringLiteral("Invalid");
    scheduler.updateSchedule(999, std::move(invalid));
    QCOMPARE(scheduler.count(), 1);
}

void tst_Scheduler::schedule_outOfBounds()
{
    Scheduler scheduler;
    QVERIFY(scheduler.schedule(-1) == nullptr);
    QVERIFY(scheduler.schedule(0) == nullptr);
    QVERIFY(scheduler.schedule(100) == nullptr);
}

void tst_Scheduler::saveLoad_roundTrip()
{
    eMule::testing::TempDir tempDir;

    // Create and populate a scheduler
    {
        Scheduler saver;

        auto e1 = std::make_unique<ScheduleEntry>();
        e1->title = QStringLiteral("Morning");
        e1->enabled = true;
        e1->day = ScheduleDay::MonToFri;
        e1->startTime = 1000;
        e1->endTime = 2000;
        e1->actions[0] = ScheduleAction::SetUpload;
        e1->values[0] = QStringLiteral("100");
        e1->actions[1] = ScheduleAction::SetDownload;
        e1->values[1] = QStringLiteral("200");
        saver.addSchedule(std::move(e1));

        auto e2 = std::make_unique<ScheduleEntry>();
        e2->title = QStringLiteral("Night");
        e2->enabled = false;
        e2->day = ScheduleDay::SatSun;
        e2->startTime = 3000;
        e2->endTime = 4000;
        e2->actions[0] = ScheduleAction::SetConnections;
        e2->values[0] = QStringLiteral("500");
        saver.addSchedule(std::move(e2));

        saver.saveToFile(tempDir.path());
    }

    // Load into a new scheduler and verify
    {
        Scheduler loader;
        int loaded = loader.loadFromFile(tempDir.path());
        QCOMPARE(loaded, 2);
        QCOMPARE(loader.count(), 2);

        const ScheduleEntry* e1 = loader.schedule(0);
        QVERIFY(e1 != nullptr);
        QCOMPARE(e1->title, QStringLiteral("Morning"));
        QVERIFY(e1->enabled);
        QCOMPARE(e1->day, ScheduleDay::MonToFri);
        QCOMPARE(e1->startTime, time_t(1000));
        QCOMPARE(e1->endTime, time_t(2000));
        QCOMPARE(e1->actions[0], ScheduleAction::SetUpload);
        QCOMPARE(e1->values[0], QStringLiteral("100"));
        QCOMPARE(e1->actions[1], ScheduleAction::SetDownload);
        QCOMPARE(e1->values[1], QStringLiteral("200"));

        const ScheduleEntry* e2 = loader.schedule(1);
        QVERIFY(e2 != nullptr);
        QCOMPARE(e2->title, QStringLiteral("Night"));
        QVERIFY(!e2->enabled);
        QCOMPARE(e2->day, ScheduleDay::SatSun);
        QCOMPARE(e2->actions[0], ScheduleAction::SetConnections);
        QCOMPARE(e2->values[0], QStringLiteral("500"));
    }
}

void tst_Scheduler::check_emptySchedules()
{
    Scheduler scheduler;

    // With no schedules, check should return -1
    int result = scheduler.check(true);
    QCOMPARE(result, -1);
}

void tst_Scheduler::activateSchedule_setsPrefs()
{
    Scheduler scheduler;

    // Save original pref values
    uint32 origUpload = thePrefs.maxUpload();
    uint32 origDownload = thePrefs.maxDownload();

    auto entry = std::make_unique<ScheduleEntry>();
    entry->title = QStringLiteral("Test");
    entry->enabled = true;
    entry->actions[0] = ScheduleAction::SetUpload;
    entry->values[0] = QStringLiteral("42");
    entry->actions[1] = ScheduleAction::SetDownload;
    entry->values[1] = QStringLiteral("84");
    scheduler.addSchedule(std::move(entry));

    scheduler.activateSchedule(0);

    QCOMPARE(thePrefs.maxUpload(), uint32(42));
    QCOMPARE(thePrefs.maxDownload(), uint32(84));

    scheduler.clearOverrides();

    // Restore original values
    thePrefs.setMaxUpload(origUpload);
    thePrefs.setMaxDownload(origDownload);
}

void tst_Scheduler::clearOverrides_restoresStored()
{
    Scheduler scheduler;

    const uint32 origUpload = thePrefs.configuredMaxUpload();
    const uint32 origDownload = thePrefs.configuredMaxDownload();
    thePrefs.setMaxUpload(100);
    thePrefs.setMaxDownload(200);

    auto entry = std::make_unique<ScheduleEntry>();
    entry->title = QStringLiteral("Override");
    entry->enabled = true;
    entry->actions[0] = ScheduleAction::SetUpload;
    entry->values[0] = QStringLiteral("10");
    entry->actions[1] = ScheduleAction::SetDownload;
    entry->values[1] = QStringLiteral("20");
    scheduler.addSchedule(std::move(entry));

    scheduler.activateSchedule(0);
    QCOMPARE(thePrefs.maxUpload(), uint32(10));
    QCOMPARE(thePrefs.maxDownload(), uint32(20));

    // Stored values untouched: save() and Options (toIpcMap) see the user's limits
    QCOMPARE(thePrefs.configuredMaxUpload(), uint32(100));
    QCOMPARE(thePrefs.configuredMaxDownload(), uint32(200));
    QCOMPARE(thePrefs.toIpcMap().value(QStringLiteral("maxUpload")).toInteger(), 100);

    scheduler.clearOverrides();
    QCOMPARE(thePrefs.maxUpload(), uint32(100));
    QCOMPARE(thePrefs.maxDownload(), uint32(200));

    thePrefs.setMaxUpload(origUpload);
    thePrefs.setMaxDownload(origDownload);
}

// Issue #47: the limit came not back when the window ended
void tst_Scheduler::check_windowEnd_restoresLimit()
{
    Scheduler scheduler;

    const uint32 origUpload = thePrefs.configuredMaxUpload();
    thePrefs.setMaxUpload(500);

    const QDateTime now = QDateTime::currentDateTime();
    auto entry = std::make_unique<ScheduleEntry>();
    entry->title = QStringLiteral("Low Speed");
    entry->enabled = true;
    entry->startTime = now.addSecs(-3600).toSecsSinceEpoch();
    entry->endTime = now.addSecs(3600).toSecsSinceEpoch();
    entry->actions[0] = ScheduleAction::SetUpload;
    entry->values[0] = QStringLiteral("50");
    scheduler.addSchedule(std::move(entry));

    scheduler.check(true);
    QCOMPARE(thePrefs.maxUpload(), uint32(50));

    // Options applied while the schedule runs: sends back the stored value
    thePrefs.setMaxUpload(static_cast<uint32>(
        thePrefs.toIpcMap().value(QStringLiteral("maxUpload")).toInteger()));
    scheduler.check(true);
    QCOMPARE(thePrefs.maxUpload(), uint32(50));
    QCOMPARE(thePrefs.configuredMaxUpload(), uint32(500));

    // Window over
    ScheduleEntry* e = scheduler.schedule(0);
    e->startTime = now.addSecs(2 * 3600).toSecsSinceEpoch();
    e->endTime = now.addSecs(3 * 3600).toSecsSinceEpoch();
    scheduler.check(true);
    QCOMPARE(thePrefs.maxUpload(), uint32(500));

    // Last entry disabled: nothing may stay behind
    e->startTime = now.addSecs(-3600).toSecsSinceEpoch();
    e->endTime = now.addSecs(3600).toSecsSinceEpoch();
    scheduler.check(true);
    QCOMPARE(thePrefs.maxUpload(), uint32(50));
    e->enabled = false;
    scheduler.check(true);
    QCOMPARE(thePrefs.maxUpload(), uint32(500));

    thePrefs.setMaxUpload(origUpload);
}

// Pause at the window's start only (a manual click sticks), undo at its end
void tst_Scheduler::network_pauseFiresOnceAndRestores()
{
    RecordingNetwork net;
    Scheduler scheduler;
    scheduler.setNetworkActions(&net);
    auto e = windowEntry(QStringLiteral("Night"));
    e->actions[0] = ScheduleAction::UsenetPause;
    e->actions[1] = ScheduleAction::BtPause;
    scheduler.addSchedule(std::move(e));

    scheduler.check(true);
    QCOMPARE(net.calls, QStringList({QStringLiteral("usenet pause"), QStringLiteral("bt pause")}));
    scheduler.check(true);
    QCOMPARE(net.calls.size(), 2);

    net.calls.clear();
    closeWindow(scheduler.schedule(0));
    scheduler.check(true);
    QCOMPARE(net.calls, QStringList({QStringLiteral("usenet restore"), QStringLiteral("bt restore")}));
    scheduler.check(true);
    QCOMPARE(net.calls.size(), 2);

    // opens again: fires again
    net.calls.clear();
    openWindow(scheduler.schedule(0));
    scheduler.check(true);
    QCOMPARE(net.calls, QStringList({QStringLiteral("usenet pause"), QStringLiteral("bt pause")}));
    scheduler.stop();
    scheduler.setNetworkActions(nullptr);
}

// No end time: saved, nothing to undo (MFC makeDefault)
void tst_Scheduler::network_noEndTimeIsPersistent()
{
    RecordingNetwork net;
    Scheduler scheduler;
    scheduler.setNetworkActions(&net);
    auto e = windowEntry(QStringLiteral("From now on"));
    e->endTime = 0;
    e->actions[0] = ScheduleAction::UsenetResume;
    e->actions[1] = ScheduleAction::BtResume;
    scheduler.addSchedule(std::move(e));

    scheduler.activateWindowStart(0, true);
    QCOMPARE(net.calls, QStringList({QStringLiteral("usenet resume saved"), QStringLiteral("bt resume saved")}));
    scheduler.releaseNetworks();
    QCOMPARE(net.calls.size(), 2);
    scheduler.setNetworkActions(nullptr);
}

// A window opening inside another takes over; the older one's end changes nothing
void tst_Scheduler::network_laterWindowTakesOver()
{
    RecordingNetwork net;
    Scheduler scheduler;
    scheduler.setNetworkActions(&net);
    auto a = windowEntry(QStringLiteral("Pause"));
    a->actions[0] = ScheduleAction::UsenetPause;
    scheduler.addSchedule(std::move(a));
    auto b = windowEntry(QStringLiteral("Resume"), false);
    b->actions[0] = ScheduleAction::UsenetResume;
    scheduler.addSchedule(std::move(b));

    scheduler.check(true);
    QCOMPARE(net.calls, QStringList({QStringLiteral("usenet pause")}));

    net.calls.clear();
    openWindow(scheduler.schedule(1));
    scheduler.check(true);
    QCOMPARE(net.calls, QStringList({QStringLiteral("usenet resume")}));

    net.calls.clear();
    closeWindow(scheduler.schedule(0));
    scheduler.check(true);
    QVERIFY(net.calls.isEmpty());

    closeWindow(scheduler.schedule(1));
    scheduler.check(true);
    QCOMPARE(net.calls, QStringList({QStringLiteral("usenet restore")}));
    scheduler.setNetworkActions(nullptr);
}

// Schedules replaced (SaveSchedules): the runtime pause goes, the next check re-applies
void tst_Scheduler::network_removeAllRestores()
{
    RecordingNetwork net;
    Scheduler scheduler;
    scheduler.setNetworkActions(&net);
    auto e = windowEntry(QStringLiteral("Night"));
    e->actions[0] = ScheduleAction::BtPause;
    scheduler.addSchedule(std::move(e));
    scheduler.check(true);
    scheduler.removeAll();
    QCOMPARE(net.calls, QStringList({QStringLiteral("bt pause"), QStringLiteral("bt restore")}));
    scheduler.setNetworkActions(nullptr);
}

void tst_Scheduler::network_usenetCategoryOnce()
{
    RecordingNetwork net;
    Scheduler scheduler;
    scheduler.setNetworkActions(&net);
    auto e = windowEntry(QStringLiteral("Cats"));
    e->actions[0] = ScheduleAction::UsenetCatPause;
    e->values[0] = QStringLiteral("2");
    e->actions[1] = ScheduleAction::UsenetCatResume;
    e->values[1] = QStringLiteral("-1");
    scheduler.addSchedule(std::move(e));

    scheduler.check(true);
    scheduler.check(true);
    QCOMPARE(net.calls, QStringList({QStringLiteral("usenet cat 2 pause"), QStringLiteral("usenet cat -1 resume")}));
    scheduler.stop();
    QCOMPARE(net.calls.size(), 2);   // nothing to undo
    scheduler.setNetworkActions(nullptr);
}

// ed3k follows eD2K's Stop/Resume Category
void tst_Scheduler::network_catStopReachesEd3k()
{
    RecordingNetwork net;
    Scheduler scheduler;
    scheduler.setNetworkActions(&net);
    auto e = std::make_unique<ScheduleEntry>();
    e->title = QStringLiteral("Cats");
    e->enabled = true;
    e->actions[0] = ScheduleAction::CatStop;
    e->values[0] = QStringLiteral("3");
    e->actions[1] = ScheduleAction::CatResume;
    e->values[1] = QStringLiteral("-2");
    scheduler.addSchedule(std::move(e));

    scheduler.activateSchedule(0);
    QCOMPARE(net.calls, QStringList({QStringLiteral("ed3k cat 3 pause"), QStringLiteral("ed3k cat -2 resume")}));
    scheduler.clearOverrides();
    scheduler.setNetworkActions(nullptr);
}

void tst_Scheduler::network_capsInOverlay()
{
    const uint32 origBtDown = thePrefs.configuredBtMaxDownloadKb();
    const uint32 origBtUp = thePrefs.configuredBtMaxUploadKb();
    thePrefs.setBtMaxDownloadKb(1000);
    thePrefs.setBtMaxUploadKb(0);

    Scheduler scheduler;
    auto e = windowEntry(QStringLiteral("Caps"));
    e->actions[0] = ScheduleAction::UsenetDownload;
    e->values[0] = QStringLiteral("100");
    e->actions[1] = ScheduleAction::BtDownload;
    e->values[1] = QStringLiteral("200");
    e->actions[2] = ScheduleAction::BtUpload;
    e->values[2] = QStringLiteral("300");
    scheduler.addSchedule(std::move(e));

    scheduler.check(true);
    QCOMPARE(thePrefs.scheduledLimits().usenetMaxDownloadKb, std::optional<uint32>(100));
    QCOMPARE(thePrefs.btMaxDownloadKb(), uint32(200));
    QCOMPARE(thePrefs.btMaxUploadKb(), uint32(300));
    QCOMPARE(thePrefs.configuredBtMaxDownloadKb(), uint32(1000));
    QCOMPARE(thePrefs.toIpcMap().value(QStringLiteral("btMaxDownloadKb")).toInteger(), 1000);

    scheduler.stop();
    QVERIFY(!thePrefs.scheduledLimits().usenetMaxDownloadKb);
    QCOMPARE(thePrefs.btMaxDownloadKb(), uint32(1000));
    QCOMPARE(thePrefs.btMaxUploadKb(), uint32(0));

    thePrefs.setBtMaxDownloadKb(origBtDown);
    thePrefs.setBtMaxUploadKb(origBtUp);
}

// A mod's ids (MorphXT 8-16) load as nothing; the known ones close up
void tst_Scheduler::load_dropsUnknownActions()
{
    eMule::testing::TempDir tempDir;
    {
        QSettings ini(tempDir.path() + QStringLiteral("/preferences.ini"), QSettings::IniFormat);
        ini.setValue(QStringLiteral("Scheduler/Count"), 1);
        ini.setValue(QStringLiteral("Schedule#0/Title"), QStringLiteral("Mod"));
        ini.setValue(QStringLiteral("Schedule#0/Action0"), 8);
        ini.setValue(QStringLiteral("Schedule#0/Value0"), QStringLiteral("x"));
        ini.setValue(QStringLiteral("Schedule#0/Action1"), 32);
        ini.setValue(QStringLiteral("Schedule#0/Action2"), 1);
        ini.setValue(QStringLiteral("Schedule#0/Value2"), QStringLiteral("50"));
        ini.setValue(QStringLiteral("Schedule#0/Action3"), 41);
    }
    Scheduler loader;
    QCOMPARE(loader.loadFromFile(tempDir.path()), 1);
    const ScheduleEntry* e = loader.schedule(0);
    QVERIFY(e);
    QCOMPARE(e->actions[0], ScheduleAction::UsenetPause);
    QCOMPARE(e->actions[1], ScheduleAction::SetUpload);
    QCOMPARE(e->values[1], QStringLiteral("50"));
    QCOMPARE(e->actions[2], ScheduleAction::None);
}

QTEST_GUILESS_MAIN(tst_Scheduler)
#include "tst_Scheduler.moc"
