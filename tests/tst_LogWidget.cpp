#include "controls/LogWidget.h"

#include <QApplication>
#include <QTest>

using namespace eMule;

/// The tabs a log line lands in (LogWidget::routeEd3k): emule.ed3k by severity into ed3k /
/// ed3k Verbose, emule.ed3k.kad into ed3k Kad; with the ed3k tabs hidden, Log/Verbose.
class tst_LogWidget : public QObject {
    Q_OBJECT

private slots:
    void ed3kLinesLandInTheirTabs();
    void ed3kLinesFallBackWhenHidden();
    void ed3kDebugDroppedWithoutVerbose();
    void torrentDebugGoesToTorrentVerbose();
    void hosterDebugGoesToHosterVerbose();
    void leecherLinesLandInTheirTab();
    void leecherLinesFallBackWhenHidden();
};

void tst_LogWidget::ed3kLinesLandInTheirTabs()
{
    LogWidget w;
    w.setEd3kTabsVisible(true, true);
    w.routeEd3k(false, QtInfoMsg, QStringLiteral("ed3k info"));
    w.routeEd3k(false, QtWarningMsg, QStringLiteral("ed3k warning"));
    w.routeEd3k(false, QtDebugMsg, QStringLiteral("ed3k debug"));
    w.routeEd3k(true, QtInfoMsg, QStringLiteral("ed3k kad info"));
    w.routeEd3k(true, QtDebugMsg, QStringLiteral("ed3k kad debug"));

    QVERIFY(w.ed3kText().contains(QStringLiteral("ed3k info")));
    QVERIFY(w.ed3kText().contains(QStringLiteral("ed3k warning")));
    QVERIFY(!w.ed3kText().contains(QStringLiteral("debug")));
    QVERIFY(!w.ed3kText().contains(QStringLiteral("kad")));
    QVERIFY(w.ed3kVerboseText().contains(QStringLiteral("ed3k debug")));
    QVERIFY(!w.ed3kVerboseText().contains(QStringLiteral("kad")));
    QVERIFY(w.ed3kKadText().contains(QStringLiteral("ed3k kad info")));
    QVERIFY(w.ed3kKadText().contains(QStringLiteral("ed3k kad debug")));
    QVERIFY(!w.logText().contains(QStringLiteral("ed3k")));
    QVERIFY(!w.verboseText().contains(QStringLiteral("ed3k")));
}

void tst_LogWidget::ed3kLinesFallBackWhenHidden()
{
    LogWidget w;
    w.setEd3kTabsVisible(false, true);
    w.routeEd3k(false, QtInfoMsg, QStringLiteral("ed3k info"));
    w.routeEd3k(false, QtWarningMsg, QStringLiteral("ed3k warning"));
    w.routeEd3k(true, QtDebugMsg, QStringLiteral("ed3k kad debug"));

    QVERIFY(w.logText().contains(QStringLiteral("ed3k info")));
    QVERIFY(w.verboseText().contains(QStringLiteral("ed3k warning")));
    // debug lines would flood Verbose: dropped
    QVERIFY(!w.verboseText().contains(QStringLiteral("kad debug")));
    QVERIFY(w.ed3kText().isEmpty() && w.ed3kKadText().isEmpty());
}

// ed3k on, ed3kVerboseLog off: debug lines go nowhere
void tst_LogWidget::ed3kDebugDroppedWithoutVerbose()
{
    LogWidget w;
    w.setEd3kTabsVisible(true, false);
    w.routeEd3k(false, QtDebugMsg, QStringLiteral("ed3k debug"));
    w.routeEd3k(false, QtInfoMsg, QStringLiteral("ed3k info"));

    QVERIFY(w.ed3kVerboseText().isEmpty());
    QVERIFY(w.ed3kText().contains(QStringLiteral("ed3k info")));
    QVERIFY(!w.verboseText().contains(QStringLiteral("ed3k")));
}

void tst_LogWidget::torrentDebugGoesToTorrentVerbose()
{
    LogWidget w;
    w.setTorrentTabVisible(true, true);
    w.routeTorrent(QtDebugMsg, QStringLiteral("bt debug"));
    w.routeTorrent(QtWarningMsg, QStringLiteral("bt warning"));
    QVERIFY(w.torrentVerboseText().contains(QStringLiteral("bt debug")));
    QVERIFY(!w.torrentText().contains(QStringLiteral("bt debug")));
    QVERIFY(w.torrentText().contains(QStringLiteral("bt warning")));

    // switch off, or BitTorrent off: dropped, never in Verbose
    w.setTorrentTabVisible(true, false);
    w.routeTorrent(QtDebugMsg, QStringLiteral("bt second"));
    w.setTorrentTabVisible(false, true);
    w.routeTorrent(QtDebugMsg, QStringLiteral("bt third"));
    QVERIFY(!w.torrentVerboseText().contains(QStringLiteral("second")));
    QVERIFY(!w.torrentVerboseText().contains(QStringLiteral("third")));
    QVERIFY(!w.verboseText().contains(QStringLiteral("bt ")));
}

void tst_LogWidget::hosterDebugGoesToHosterVerbose()
{
    LogWidget w;
    w.setHosterTabVisible(true, true);
    w.routeHoster(QtDebugMsg, QStringLiteral("hoster debug"));
    w.routeHoster(QtInfoMsg, QStringLiteral("hoster info"));
    QVERIFY(w.hosterVerboseText().contains(QStringLiteral("hoster debug")));
    QVERIFY(!w.hosterText().contains(QStringLiteral("hoster debug")));
    QVERIFY(w.hosterText().contains(QStringLiteral("hoster info")));

    w.setHosterTabVisible(false, false);
    w.routeHoster(QtDebugMsg, QStringLiteral("hoster hidden"));
    QVERIFY(!w.verboseText().contains(QStringLiteral("hoster hidden")));
}

void tst_LogWidget::leecherLinesLandInTheirTab()
{
    LogWidget w;
    w.setLeecherTabVisible(true);
    w.routeLeecher(QStringLiteral("Banned: a leecher"));

    QVERIFY(w.leecherText().contains(QStringLiteral("Banned: a leecher")));
    QVERIFY(!w.logText().contains(QStringLiteral("leecher")));
    QVERIFY(!w.verboseText().contains(QStringLiteral("leecher")));
}

// DLP off: the tab is hidden, other bans still reach Verbose
void tst_LogWidget::leecherLinesFallBackWhenHidden()
{
    LogWidget w;
    w.setLeecherTabVisible(false);
    w.routeLeecher(QStringLiteral("Banned: Userhash changed"));

    QVERIFY(w.verboseText().contains(QStringLiteral("Userhash changed")));
    QVERIFY(w.leecherText().isEmpty());
}

QTEST_MAIN(tst_LogWidget)
#include "tst_LogWidget.moc"
