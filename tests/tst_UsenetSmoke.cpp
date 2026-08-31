/// @file tst_UsenetSmoke.cpp
/// @brief Link/moc smoke test for the eMule::Usenet static library.
///
/// eMuleQt gained a fourth static library alongside eMule::Core and eMule::Ipc,
/// and a new library is wired up in six places at once: CMake, the .sln, two
/// .vcxproj files, the qmake tree and the test build. Most of those fail loudly,
/// but AUTOMOC does not — a Q_OBJECT class whose moc output never gets compiled
/// produces a "missing vtable" link error far away from the cause, and a library
/// that silently contains no objects links fine and does nothing.
///
/// So this test does the one thing that catches both: it constructs a real
/// Q_OBJECT type from the module and calls through it. It is meant to stay
/// trivial. Behaviour lives in the per-subsystem tests.

#include "UsenetSession.h"

#include <QTest>

using eMule::usenet::UsenetSession;

class tst_UsenetSmoke : public QObject {
    Q_OBJECT

private slots:
    void constructs();
    void startStopIsIdempotent();
};

void tst_UsenetSmoke::constructs()
{
    UsenetSession session;

    // Proves moc ran: metaObject() is generated code, not a header inline.
    QCOMPARE(session.metaObject()->className(), "eMule::usenet::UsenetSession");
    QVERIFY(!session.isRunning());
}

void tst_UsenetSmoke::startStopIsIdempotent()
{
    UsenetSession session;

    session.start();
    QVERIFY(session.isRunning());
    session.start();
    QVERIFY(session.isRunning());

    session.stop();
    QVERIFY(!session.isRunning());
    session.stop();
    QVERIFY(!session.isRunning());
}

QTEST_MAIN(tst_UsenetSmoke)
#include "tst_UsenetSmoke.moc"
