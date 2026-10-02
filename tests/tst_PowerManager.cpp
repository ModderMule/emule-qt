/// @file tst_PowerManager.cpp
/// @brief Tests for daemon PowerManager — MFC PreventStandby gate.
///
/// The hold needs both the pref and activity (connected / transferring), is
/// re-evaluated on demand (pref change) and released on destruction. On macOS
/// this takes a real IOPM NoIdleSleep assertion, which is harmless.

#include "TestHelpers.h"
#include "PowerManager.h"
#include "prefs/Preferences.h"

#include <QTest>

#include <memory>

using namespace eMule;

class tst_PowerManager : public QObject {
    Q_OBJECT

private slots:
    void init() { m_savedPref = thePrefs.preventStandby(); }
    void cleanup() { thePrefs.setPreventStandby(m_savedPref); }

    void prefOff_neverHolds();
    void prefOn_followsActivity();
    void prefTurnedOff_releasesOnEvaluate();
    void destructor_releases();

private:
    bool m_savedPref = false;
};

void tst_PowerManager::prefOff_neverHolds()
{
    thePrefs.setPreventStandby(false);
    PowerManager pm([] { return true; });
    pm.start();
    QVERIFY(!pm.isPreventingStandby());
}

void tst_PowerManager::prefOn_followsActivity()
{
    thePrefs.setPreventStandby(true);
    bool active = false;
    PowerManager pm([&active] { return active; });
    pm.start();
    QVERIFY(!pm.isPreventingStandby());

    active = true;
    pm.evaluate();
    QVERIFY(pm.isPreventingStandby());

    active = false;
    pm.evaluate();
    QVERIFY(!pm.isPreventingStandby());
}

void tst_PowerManager::prefTurnedOff_releasesOnEvaluate()
{
    thePrefs.setPreventStandby(true);
    PowerManager pm([] { return true; });
    pm.start();
    QVERIFY(pm.isPreventingStandby());

    thePrefs.setPreventStandby(false);
    pm.evaluate();
    QVERIFY(!pm.isPreventingStandby());

    thePrefs.setPreventStandby(true);
    pm.evaluate();
    QVERIFY(pm.isPreventingStandby());
}

void tst_PowerManager::destructor_releases()
{
    thePrefs.setPreventStandby(true);
    auto pm = std::make_unique<PowerManager>([] { return true; });
    pm->start();
    QVERIFY(pm->isPreventingStandby());
    pm.reset();  // must not leak the hold; checked live via `pmset -g assertions`
}

QTEST_GUILESS_MAIN(tst_PowerManager)
#include "tst_PowerManager.moc"
