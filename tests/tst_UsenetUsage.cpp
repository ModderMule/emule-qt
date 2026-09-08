/// @file tst_UsenetUsage.cpp
/// @brief The provider usage meter: period arithmetic, rollover and durability.
///
/// Kept apart from tst_UsenetQueue because none of it needs a socket, a thread
/// or a download — and because the two questions it answers are ones a download
/// test cannot see: does a flush that runs twice change anything, and does a
/// daemon that was switched off across four billing days reset once or four
/// times.

#include "TestHelpers.h"

#include "prefs/Preferences.h"
#include "queue/UsenetQueueStore.h"
#include "queue/UsenetUsage.h"

#include <QDir>
#include <QFile>
#include <QTest>

using namespace eMule;
using namespace eMule::usenet;

namespace {

constexpr const char* kId = "acct-1";

NewsServer meteredAccount(NntpQuotaKind kind, int resetDay = 4)
{
    NewsServer s;
    s.accountId = QString::fromLatin1(kId);
    s.name = QStringLiteral("main");
    s.host = QStringLiteral("news.example.com");
    s.port = 563;
    s.user = QStringLiteral("alice");
    s.quotaKind = kind;
    s.quotaBytes = 1000;
    s.quotaResetDay = resetDay;
    return s;
}

/// Plant a sidecar directly, which is how a test reaches a period that started
/// before the run did.
void plantUsage(const QString& periodStart, qint64 periodBytes, qint64 totalBytes,
                int kind = int(NntpQuotaKind::Monthly))
{
    QFile f(UsenetUsageTracker::usagePath());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QString yaml = QStringLiteral("version: 1\naccounts:\n  - id: %1\n").arg(QLatin1String(kId));
    yaml += QStringLiteral("    kind: %1\n").arg(kind);
    if (!periodStart.isEmpty())
        yaml += QStringLiteral("    periodStart: %1\n").arg(periodStart);
    yaml += QStringLiteral("    periodBytes: %1\n    totalBytes: %2\n")
                .arg(periodBytes).arg(totalBytes);
    f.write(yaml.toUtf8());
    f.close();
}

} // namespace

class tst_UsenetUsage : public QObject {
    Q_OBJECT

private slots:
    void init();

    void monthlyPeriodClampsToAShortMonth();
    void nextResetFollowsTheBillingDay();
    void aMissedRolloverResetsOnceNotPerPeriod();
    void aClockThatMovesBackDoesNotResurrectAPeriod();
    void aBlockAccountNeverResets();
    void flushingTwiceChangesNothing();
    void anUncleanExitLosesOnlyTheUnflushedSession();
    void aResetSurvivesTheNextFlush();
    void reapplyingTheServerListKeepsTheCounters();
    void theMeterSurvivesAPortAndUsernameEdit();

private:
    std::unique_ptr<eMule::testing::TempDir> m_tmp;
};

void tst_UsenetUsage::init()
{
    m_tmp = std::make_unique<eMule::testing::TempDir>();
    thePrefs.setConfigDir(m_tmp->path());
    QDir().mkpath(UsenetQueueStore::stateDir());
}

void tst_UsenetUsage::monthlyPeriodClampsToAShortMonth()
{
    // A plan billed on the 31st is billed on the 28th in February. Without the
    // clamp February never reaches its own billing day and the period never ends.
    QCOMPARE(nntpQuotaPeriodStart(31, QDate(2026, 3, 5)), QDate(2026, 2, 28));
    QCOMPARE(nntpQuotaPeriodStart(31, QDate(2026, 3, 31)), QDate(2026, 3, 31));
    QCOMPARE(nntpQuotaPeriodStart(31, QDate(2026, 2, 28)), QDate(2026, 2, 28));

    // The ordinary cases, so the clamp cannot be "fixed" into always applying.
    QCOMPARE(nntpQuotaPeriodStart(4, QDate(2026, 9, 8)), QDate(2026, 9, 4));
    QCOMPARE(nntpQuotaPeriodStart(4, QDate(2026, 9, 3)), QDate(2026, 8, 4));
    QCOMPARE(nntpQuotaPeriodStart(1, QDate(2026, 1, 1)), QDate(2026, 1, 1));
}

void tst_UsenetUsage::nextResetFollowsTheBillingDay()
{
    QCOMPARE(nntpQuotaNextReset(4, QDate(2026, 9, 8)), QDate(2026, 10, 4));
    QCOMPARE(nntpQuotaNextReset(4, QDate(2026, 9, 3)), QDate(2026, 9, 4));

    // Crossing a short month and coming back out: the 31st must not decay into
    // the 28th for every month after February.
    QCOMPARE(nntpQuotaNextReset(31, QDate(2026, 1, 31)), QDate(2026, 2, 28));
    QCOMPARE(nntpQuotaNextReset(31, QDate(2026, 2, 28)), QDate(2026, 3, 31));
}

void tst_UsenetUsage::aMissedRolloverResetsOnceNotPerPeriod()
{
    // The daemon was off for the best part of a year. The period is compared
    // against the one *containing today*, never counted forward, so however many
    // billing days went by the answer is one reset — and the all-time figure is
    // not part of it.
    const QDate old = nntpQuotaPeriodStart(4, QDate::currentDate()).addMonths(-11);
    plantUsage(old.toString(Qt::ISODate), 900, 5000);

    UsenetUsageTracker t;
    t.load();
    t.setAccounts({meteredAccount(NntpQuotaKind::Monthly)});
    QVERIFY(t.rollOverIfDue());

    QCOMPARE(t.periodBytes(QString::fromLatin1(kId)), 0);
    QCOMPARE(t.totalBytes(QString::fromLatin1(kId)), 5000);
    QCOMPARE(t.periodStart(QString::fromLatin1(kId)),
             nntpQuotaPeriodStart(4, QDate::currentDate()));

    // And again: idempotent, because the test is on the date and not on a flag.
    QVERIFY(!t.rollOverIfDue());
}

void tst_UsenetUsage::aClockThatMovesBackDoesNotResurrectAPeriod()
{
    // NTP correction, a restored snapshot, a timezone fix. Rolling backwards
    // would zero a period already spent and charge it a second time.
    const QDate future = nntpQuotaPeriodStart(4, QDate::currentDate()).addMonths(2);
    plantUsage(future.toString(Qt::ISODate), 900, 5000);

    UsenetUsageTracker t;
    t.load();
    t.setAccounts({meteredAccount(NntpQuotaKind::Monthly)});
    QVERIFY(!t.rollOverIfDue());
    QCOMPARE(t.periodBytes(QString::fromLatin1(kId)), 900);
    QCOMPARE(t.periodStart(QString::fromLatin1(kId)), future);
}

void tst_UsenetUsage::aBlockAccountNeverResets()
{
    // Prepaid bytes do not come back on a billing day. Only a top-up refills
    // them, which is what setUsage() is for.
    plantUsage(QString(), 900, 900, int(NntpQuotaKind::Block));

    UsenetUsageTracker t;
    t.load();
    t.setAccounts({meteredAccount(NntpQuotaKind::Block)});
    QVERIFY(!t.rollOverIfDue());
    QCOMPARE(t.periodBytes(QString::fromLatin1(kId)), 900);
}

void tst_UsenetUsage::flushingTwiceChangesNothing()
{
    // The scar this whole shape exists for: the cumulative-statistics flush used
    // to write `stored += session`, which lost a session to a kill -9 and
    // double-counted the moment it ran on a timer.
    UsenetUsageTracker t;
    t.load();
    t.setAccounts({meteredAccount(NntpQuotaKind::Monthly)});
    t.add(QString::fromLatin1(kId), 100);

    QVERIFY(t.flush());
    QVERIFY(t.flush());
    QVERIFY(t.flush());

    UsenetUsageTracker reloaded;
    reloaded.load();
    QCOMPARE(reloaded.periodBytes(QString::fromLatin1(kId)), 100);
    QCOMPARE(reloaded.totalBytes(QString::fromLatin1(kId)), 100);
}

void tst_UsenetUsage::anUncleanExitLosesOnlyTheUnflushedSession()
{
    UsenetUsageTracker t;
    t.load();
    t.setAccounts({meteredAccount(NntpQuotaKind::Monthly)});
    t.add(QString::fromLatin1(kId), 100);
    QVERIFY(t.flush());
    t.add(QString::fromLatin1(kId), 50);   // and now the process dies

    UsenetUsageTracker reloaded;
    reloaded.load();
    QCOMPARE(reloaded.periodBytes(QString::fromLatin1(kId)), 100);
}

void tst_UsenetUsage::aResetSurvivesTheNextFlush()
{
    // Zeroing only the persisted half would let the next absolute write emit
    // `0 + session` and bring the old figure straight back.
    UsenetUsageTracker t;
    t.load();
    t.setAccounts({meteredAccount(NntpQuotaKind::Monthly)});
    t.add(QString::fromLatin1(kId), 100);
    QVERIFY(t.flush());

    QVERIFY(t.setUsage(QString::fromLatin1(kId), 0, -1));
    QCOMPARE(t.periodBytes(QString::fromLatin1(kId)), 0);
    QCOMPARE(t.totalBytes(QString::fromLatin1(kId)), 100);   // -1 left it alone

    QVERIFY(t.flush());
    UsenetUsageTracker reloaded;
    reloaded.load();
    QCOMPARE(reloaded.periodBytes(QString::fromLatin1(kId)), 0);
}

void tst_UsenetUsage::reapplyingTheServerListKeepsTheCounters()
{
    // applyServers() runs on every settings save. A setAccounts() that re-seeded
    // would zero every meter each time the user pressed OK in Options.
    UsenetUsageTracker t;
    t.load();
    const NewsServer s = meteredAccount(NntpQuotaKind::Monthly);
    t.setAccounts({s});
    t.add(QString::fromLatin1(kId), 100);
    t.setAccounts({s});
    t.setAccounts({s});
    QCOMPARE(t.periodBytes(QString::fromLatin1(kId)), 100);
}

void tst_UsenetUsage::theMeterSurvivesAPortAndUsernameEdit()
{
    // Why the meter is keyed by accountId and not by NewsServer::key(), which is
    // host:port/user. Switching a provider to TLS changes the port; rotating a
    // credential changes the user. Both are routine, and under key() both would
    // silently start a fresh meter — which under-counts, and therefore
    // over-spends.
    UsenetUsageTracker t;
    t.load();
    NewsServer s = meteredAccount(NntpQuotaKind::Monthly);
    t.setAccounts({s});
    t.add(s.accountId, 100);

    const QString before = s.key();
    s.port = 119;
    s.user = QStringLiteral("alice2");
    QVERIFY(s.key() != before);

    t.setAccounts({s});
    QCOMPARE(t.periodBytes(s.accountId), 100);
}

QTEST_MAIN(tst_UsenetUsage)
#include "tst_UsenetUsage.moc"
