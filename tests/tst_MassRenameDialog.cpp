/// @file tst_MassRenameDialog.cpp
/// @brief Mass Rename dialog driven like a user: typing in the mask edits every name.

#include "TestHelpers.h"

#include "dialogs/MassRenameDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTest>
#include <QTextEdit>
#include <QTimer>

using namespace eMule;
using namespace Qt::StringLiterals;

class tst_MassRenameDialog : public QObject {
    Q_OBJECT

private slots:
    void typingInMask_editsAllNames();
    void undo_restoresAndRedoes();
    void rightJustified_insertBeforeExtension();
    void insertColumn_fromMask();
    void accept_reportsOnlyChangedNames();
    void accept_rejectsDuplicates();
};

namespace {

[[nodiscard]] QList<MassRenameDialog::Item> items()
{
    return {{u"h1"_s, u"one.avi"_s, u"/d"_s},
            {u"h2"_s, u"two.avi"_s, u"/d"_s},
            {u"h3"_s, u"three.avi"_s, u"/d"_s}};
}

[[nodiscard]] QStringList newNames(MassRenameDialog& dlg)
{
    const auto edits = dlg.findChildren<QTextEdit*>();
    return edits.first()->toPlainText().split(u'\n');
}

[[nodiscard]] QLineEdit* mask(MassRenameDialog& dlg)
{
    return dlg.findChild<MassRenameMaskEdit*>();
}

} // namespace

void tst_MassRenameDialog::typingInMask_editsAllNames()
{
    MassRenameDialog dlg(items(), nullptr);
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    QLineEdit* m = mask(dlg);
    QCOMPARE(m->text(), u"one.avi"_s);

    m->setFocus();
    m->setCursorPosition(0);
    QTest::keyClicks(m, u"01 "_s);
    QCOMPARE(newNames(dlg), (QStringList{u"01 one.avi"_s, u"01 two.avi"_s, u"01 three.avi"_s}));

    QTest::keyClick(m, Qt::Key_Backspace);
    QCOMPARE(newNames(dlg), (QStringList{u"01one.avi"_s, u"01two.avi"_s, u"01three.avi"_s}));
}

void tst_MassRenameDialog::undo_restoresAndRedoes()
{
    MassRenameDialog dlg(items(), nullptr);
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    QLineEdit* m = mask(dlg);
    m->setFocus();
    m->setCursorPosition(0);
    QTest::keyClicks(m, u"AB"_s);   // one typing run = one undo step
    QCOMPARE(newNames(dlg).value(1), u"ABtwo.avi"_s);

    QTest::keySequence(m, QKeySequence::Undo);
    QCOMPARE(newNames(dlg).value(1), u"two.avi"_s);
    QCOMPARE(m->text(), u"one.avi"_s);
    QTest::keySequence(m, QKeySequence::Undo);   // MorphXT: a second undo redoes
    QCOMPARE(newNames(dlg).value(1), u"ABtwo.avi"_s);
}

void tst_MassRenameDialog::rightJustified_insertBeforeExtension()
{
    MassRenameDialog dlg(items(), nullptr);
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    const auto radios = dlg.findChildren<QRadioButton*>();
    radios.last()->click();   // right-justified
    QLineEdit* m = mask(dlg);
    m->setFocus();
    m->setCursorPosition(3);   // "one|.avi"
    QTest::keyClicks(m, u"_x"_s);
    QCOMPARE(newNames(dlg), (QStringList{u"one_x.avi"_s, u"two_x.avi"_s, u"three_x.avi"_s}));
}

void tst_MassRenameDialog::insertColumn_fromMask()
{
    MassRenameDialog dlg(items(), nullptr);
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    QLineEdit* m = mask(dlg);
    m->setFocus();
    m->setCursorPosition(0);
    QApplication::clipboard()->setText(u"1 - \n2 - \n3 - "_s);
    for (QPushButton* b : dlg.findChildren<QPushButton*>()) {
        if (b->text().contains(u"clipboard"_s))
            b->click();
    }
    QCOMPARE(newNames(dlg), (QStringList{u"1 - one.avi"_s, u"2 - two.avi"_s, u"3 - three.avi"_s}));
}

void tst_MassRenameDialog::accept_reportsOnlyChangedNames()
{
    MassRenameDialog dlg(items(), nullptr);
    auto* edit = dlg.findChildren<QTextEdit*>().first();
    edit->setPlainText(u"one.avi\n  Two.avi \nthree.avi"_s);
    dlg.accept();
    QCOMPARE(dlg.result(), int(QDialog::Accepted));
    QCOMPARE(dlg.renames().size(), 1);
    QCOMPARE(dlg.renames().first().first, u"h2"_s);
    QCOMPARE(dlg.renames().first().second, u"Two.avi"_s);
}

void tst_MassRenameDialog::accept_rejectsDuplicates()
{
    MassRenameDialog dlg(items(), nullptr);
    dlg.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dlg));
    auto* edit = dlg.findChildren<QTextEdit*>().first();
    edit->setPlainText(u"same.avi\nsame.avi\nthree.avi"_s);
    // The warning box is modal: close it as soon as it shows
    bool boxShown = false;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&] {
        QWidget* box = QApplication::activeModalWidget();
        if (box && box != &dlg) {
            boxShown = true;
            box->close();
        }
    });
    poll.start(20);
    dlg.accept();
    poll.stop();
    QVERIFY(boxShown);
    QVERIFY(dlg.isVisible());
    QVERIFY(dlg.renames().isEmpty());

    // A picture of the dialog for comparison with MorphXT's
    if (const QByteArray out = qgetenv("MASSRENAME_SHOT"); !out.isEmpty()) {
        edit->setPlainText(u"01 one.avi\n02 two.avi\n03 three.avi"_s);
        dlg.resize(820, 520);
        QTest::qWait(50);
        dlg.grab().save(QString::fromLocal8Bit(out));
    }
}

QTEST_MAIN(tst_MassRenameDialog)
#include "tst_MassRenameDialog.moc"
