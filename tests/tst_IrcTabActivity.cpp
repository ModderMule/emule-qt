/// @file tst_IrcTabActivity.cpp
/// @brief Chat tab colours: joins/leaves/notices = soft (Info), someone's message = red,
///   the same moment the toolbar "!" (unreadChanged) goes on.

#include "panels/IrcPanel.h"
#include "prefs/Preferences.h"
#include "utils/Appearance.h"

#include <QSignalSpy>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>

using namespace eMule;

class TestIrcTabActivity : public QObject {
    Q_OBJECT

private slots:
    void joinIsSoftMessageIsRed();
};

namespace {

QTabWidget* chatTabs(IrcPanel& panel)
{
    for (auto* tabs : panel.findChildren<QTabWidget*>())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i).contains(QStringLiteral("#a")))
                return tabs;
    return nullptr;
}

int tabOf(QTabWidget* tabs, const QString& name)
{
    for (int i = 0; i < tabs->count(); ++i)
        if (tabs->tabText(i).remove(QLatin1Char('&')).contains(name))
            return i;
    return -1;
}

void invoke(IrcPanel& panel, const char* slot, const QString& a, const QString& b)
{
    QVERIFY(QMetaObject::invokeMethod(&panel, slot, Q_ARG(QString, a), Q_ARG(QString, b)));
}

} // namespace

void TestIrcTabActivity::joinIsSoftMessageIsRed()
{
    thePrefs.setIrcIgnoreJoinMessages(false);   // join lines shown
    IrcPanel panel;
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QSignalSpy unread(&panel, &IrcPanel::unreadChanged);

    // own joins (no nick while offline = ours): #b ends up current
    invoke(panel, "onUserJoined", QStringLiteral("#a"), QString());
    invoke(panel, "onUserJoined", QStringLiteral("#b"), QString());
    QTabWidget* tabs = chatTabs(panel);
    QVERIFY(tabs);
    const int a = tabOf(tabs, QStringLiteral("#a"));
    const int b = tabOf(tabs, QStringLiteral("#b"));
    QVERIFY(a >= 0 && b >= 0);
    QCOMPARE(tabs->currentIndex(), b);
    QCOMPARE(tabs->tabBar()->tabTextColor(a), QColor());

    // someone joins #a: soft colour, no "!"
    invoke(panel, "onUserJoined", QStringLiteral("#a"), QStringLiteral("bob"));
    QCOMPARE(tabs->tabBar()->tabTextColor(a), roleColor(TextRole::Info));
    QCOMPARE(unread.count(), 0);

    // bob writes: red with the "!"
    QVERIFY(QMetaObject::invokeMethod(&panel, "onChannelMessage", Q_ARG(QString, QStringLiteral("#a")),
                                      Q_ARG(QString, QStringLiteral("bob")), Q_ARG(QString, QStringLiteral("hi"))));
    QCOMPARE(tabs->tabBar()->tabTextColor(a), roleColor(TextRole::Error));
    QCOMPARE(unread.count(), 1);
    QCOMPARE(unread.first().first().toBool(), true);

    // a later join keeps the red
    invoke(panel, "onUserJoined", QStringLiteral("#a"), QStringLiteral("carol"));
    QCOMPARE(tabs->tabBar()->tabTextColor(a), roleColor(TextRole::Error));

    // reading #a clears both
    tabs->setCurrentIndex(a);
    QCOMPARE(tabs->tabBar()->tabTextColor(a), QColor());
    QCOMPARE(unread.count(), 2);
    QCOMPARE(unread.last().first().toBool(), false);
}

QTEST_MAIN(TestIrcTabActivity)
#include "tst_IrcTabActivity.moc"
