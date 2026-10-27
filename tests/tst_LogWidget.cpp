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
};

void tst_LogWidget::ed3kLinesLandInTheirTabs()
{
    LogWidget w;
    w.setEd3kTabsVisible(true);
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
    w.setEd3kTabsVisible(false);
    w.routeEd3k(false, QtInfoMsg, QStringLiteral("ed3k info"));
    w.routeEd3k(false, QtWarningMsg, QStringLiteral("ed3k warning"));
    w.routeEd3k(true, QtDebugMsg, QStringLiteral("ed3k kad debug"));

    QVERIFY(w.logText().contains(QStringLiteral("ed3k info")));
    QVERIFY(w.verboseText().contains(QStringLiteral("ed3k warning")));
    // debug lines would flood Verbose: dropped
    QVERIFY(!w.verboseText().contains(QStringLiteral("kad debug")));
    QVERIFY(w.ed3kText().isEmpty() && w.ed3kKadText().isEmpty());
}

QTEST_MAIN(tst_LogWidget)
#include "tst_LogWidget.moc"
