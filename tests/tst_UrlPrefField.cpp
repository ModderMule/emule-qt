/// @file tst_UrlPrefField.cpp
/// @brief The update-URL inputs (Kad nodes.dat, Servers server.met): an emptied
///        field falls back to the shipped default, a changed one reaches thePrefs.

#include "TestHelpers.h"

#include "utils/UrlPrefField.h"

#include <QLineEdit>
#include <QTest>
#include <QVBoxLayout>
#include <QWidget>

using namespace eMule;

class tst_UrlPrefField : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void showsThePreference();
    void emptiedFieldRevertsOnFocusLoss();
    void emptiedFieldRevertsOnEnter();
    void customUrlReachesThePreference();
    void valueOfAnEmptyFieldIsTheDefault();
    void refreshLeavesAFocusedFieldAlone();

private:
    void focusOther();

    QWidget*   m_window = nullptr;
    QLineEdit* m_edit   = nullptr;
    QLineEdit* m_other  = nullptr;
};

void tst_UrlPrefField::init()
{
    thePrefs.setNodesDatURL(QString());   // back to the default

    m_window = new QWidget;
    auto* layout = new QVBoxLayout(m_window);
    m_edit  = new QLineEdit;
    m_other = new QLineEdit;
    layout->addWidget(m_edit);
    layout->addWidget(m_other);

    bindUrlPrefField(m_edit, QStringLiteral("nodesDatURL"), Preferences::kDefaultNodesDatURL,
                     &Preferences::nodesDatURL, &Preferences::setNodesDatURL,
                     [] { return static_cast<IpcClient*>(nullptr); });

    m_window->show();
    m_window->activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(m_window));
    m_edit->setFocus();
    QTRY_VERIFY(m_edit->hasFocus());
}

void tst_UrlPrefField::cleanup()
{
    delete m_window;
    m_window = nullptr;
}

void tst_UrlPrefField::showsThePreference()
{
    QCOMPARE(m_edit->text(), QString(Preferences::kDefaultNodesDatURL));
    QCOMPARE(m_edit->cursorPosition(), 0);
}

void tst_UrlPrefField::emptiedFieldRevertsOnFocusLoss()
{
    m_edit->selectAll();
    QTest::keyClick(m_edit, Qt::Key_Backspace);
    QVERIFY(m_edit->text().isEmpty());

    focusOther();
    QCOMPARE(m_edit->text(), QString(Preferences::kDefaultNodesDatURL));
    QCOMPARE(thePrefs.nodesDatURL(), QString(Preferences::kDefaultNodesDatURL));
}

void tst_UrlPrefField::emptiedFieldRevertsOnEnter()
{
    m_edit->selectAll();
    QTest::keyClicks(m_edit, QStringLiteral("https://example.org/n.dat"));
    focusOther();
    QCOMPARE(thePrefs.nodesDatURL(), QStringLiteral("https://example.org/n.dat"));

    // Emptying a custom URL goes back to the default, in the field and the pref.
    m_edit->setFocus();
    m_edit->selectAll();
    QTest::keyClick(m_edit, Qt::Key_Backspace);
    QTest::keyClick(m_edit, Qt::Key_Return);
    QCOMPARE(m_edit->text(), QString(Preferences::kDefaultNodesDatURL));
    QCOMPARE(thePrefs.nodesDatURL(), QString(Preferences::kDefaultNodesDatURL));
}

void tst_UrlPrefField::customUrlReachesThePreference()
{
    m_edit->selectAll();
    QTest::keyClicks(m_edit, QStringLiteral("  https://example.org/nodes.dat "));
    focusOther();
    QCOMPARE(thePrefs.nodesDatURL(), QStringLiteral("https://example.org/nodes.dat"));
    QCOMPARE(m_edit->text(), QStringLiteral("https://example.org/nodes.dat"));
}

void tst_UrlPrefField::valueOfAnEmptyFieldIsTheDefault()
{
    m_edit->clear();
    QCOMPARE(urlPrefFieldValue(m_edit, Preferences::kDefaultNodesDatURL),
             QString(Preferences::kDefaultNodesDatURL));
    m_edit->setText(QStringLiteral(" https://example.org/x "));
    QCOMPARE(urlPrefFieldValue(m_edit, Preferences::kDefaultNodesDatURL),
             QStringLiteral("https://example.org/x"));
}

void tst_UrlPrefField::refreshLeavesAFocusedFieldAlone()
{
    m_edit->selectAll();
    QTest::keyClicks(m_edit, QStringLiteral("https://typing"));
    thePrefs.setNodesDatURL(QStringLiteral("https://example.org/daemon.dat"));

    refreshUrlPrefField(m_edit, &Preferences::nodesDatURL);
    QCOMPARE(m_edit->text(), QStringLiteral("https://typing"));

    m_other->setFocus();
    QTRY_VERIFY(m_other->hasFocus());
    refreshUrlPrefField(m_edit, &Preferences::nodesDatURL);
    // focus loss stored what was typed; the refresh shows the pref as it now is
    QCOMPARE(m_edit->text(), thePrefs.nodesDatURL());
}

void tst_UrlPrefField::focusOther()
{
    m_other->setFocus();
    QTRY_VERIFY(m_other->hasFocus());
}

QTEST_MAIN(tst_UrlPrefField)
#include "tst_UrlPrefField.moc"
