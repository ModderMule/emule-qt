/// @file tst_Scheduler.cpp
/// @brief Tests for transfer/Scheduler.

#include "TestHelpers.h"
#include "transfer/Scheduler.h"
#include "prefs/Preferences.h"

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
    void saveRestoreOriginals();
};

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

    // Restore original values
    thePrefs.setMaxUpload(origUpload);
    thePrefs.setMaxDownload(origDownload);
}

void tst_Scheduler::saveRestoreOriginals()
{
    Scheduler scheduler;

    // Set known pref values
    uint32 origUpload = thePrefs.maxUpload();
    uint32 origDownload = thePrefs.maxDownload();

    thePrefs.setMaxUpload(100);
    thePrefs.setMaxDownload(200);

    scheduler.saveOriginals();

    // Activate a schedule that overrides
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

    // Restore originals
    scheduler.restoreOriginals();
    QCOMPARE(thePrefs.maxUpload(), uint32(100));
    QCOMPARE(thePrefs.maxDownload(), uint32(200));

    // Restore real original values
    thePrefs.setMaxUpload(origUpload);
    thePrefs.setMaxDownload(origDownload);
}

QTEST_GUILESS_MAIN(tst_Scheduler)
#include "tst_Scheduler.moc"
