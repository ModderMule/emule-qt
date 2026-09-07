/// @file tst_CategoryDialog.cpp
/// @brief The add/edit category dialog — MFC's CCatDialog.
///
/// Two things here are easy to get wrong and impossible to notice by looking:
///
///   - **Cancel must change nothing.** MFC edits the live `Category_Struct`
///     through a pointer into `catArr` and only afterwards asks whether the user
///     pressed OK (`srchybrid/CatDialog.cpp:45-52`), so a Cancel that follows a
///     failed path validation leaves the category half-edited. This dialog holds
///     a copy; the test pins that.
///   - **An empty incoming path is a valid answer**, meaning "the global
///     incoming directory". It is also the only way back after one has been set,
///     so it must not be treated as a validation failure.

#include "dialogs/CategoryDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule;

namespace {

/// The OK button, which is what the validation hangs off.
QPushButton* okButton(CategoryDialog& dlg)
{
    auto* box = dlg.findChild<QDialogButtonBox*>();
    return box ? box->button(QDialogButtonBox::Ok) : nullptr;
}

/// Line edits in construction order: title, comment, incoming, autocat, regexp.
QList<QLineEdit*> edits(CategoryDialog& dlg)
{
    return dlg.findChildren<QLineEdit*>();
}

} // namespace

class tst_CategoryDialog : public QObject {
    Q_OBJECT

private slots:
    void populatesFromTheCategory();
    void acceptWritesTheEditedCopy();
    void rejectLeavesTheCategoryAlone();
    void emptyIncomingPathIsAccepted();
    void missingFolderIsCreatedOnAccept();
    void invalidRegexpBlocksAccept();
    void untitledCategoryBlocksAccept();

private:
    QTemporaryDir m_dir;
};

void tst_CategoryDialog::populatesFromTheCategory()
{
    DownloadCategory cat;
    cat.title = QStringLiteral("Movies");
    cat.comment = QStringLiteral("films");
    cat.incomingPath = m_dir.path();
    cat.autocat = QStringLiteral("mkv|avi");
    cat.autocatIsRegexp = true;
    cat.prio = 2;

    CategoryDialog dlg(cat, m_dir.path());
    const auto fields = edits(dlg);
    QCOMPARE(fields.size(), 5);
    QCOMPARE(fields.at(0)->text(), QStringLiteral("Movies"));
    QCOMPARE(fields.at(1)->text(), QStringLiteral("films"));
    QCOMPARE(fields.at(2)->text(), m_dir.path());
    QCOMPARE(fields.at(3)->text(), QStringLiteral("mkv|avi"));

    auto* check = dlg.findChild<QCheckBox*>();
    QVERIFY(check);
    QVERIFY(check->isChecked());

    // The combo's index is the priority value, as in MFC — kPrLow/Normal/High.
    auto* combo = dlg.findChild<QComboBox*>();
    QVERIFY(combo);
    QCOMPARE(combo->currentData().toUInt(), 2U);
}

void tst_CategoryDialog::acceptWritesTheEditedCopy()
{
    const QString movies = m_dir.filePath(QStringLiteral("Movies"));
    QVERIFY(QDir().mkpath(movies));

    CategoryDialog dlg(DownloadCategory{}, m_dir.path());
    const auto fields = edits(dlg);
    fields.at(0)->setText(QStringLiteral("  Movies  ")); // trimmed on the way out
    fields.at(2)->setText(movies);
    fields.at(3)->setText(QStringLiteral("mkv|avi"));

    okButton(dlg)->click();
    QCOMPARE(dlg.result(), int(QDialog::Accepted));

    const auto cat = dlg.category();
    QCOMPARE(cat.title, QStringLiteral("Movies"));
    QCOMPARE(cat.incomingPath, QDir::cleanPath(movies));
    QCOMPARE(cat.autocat, QStringLiteral("mkv|avi"));
}

void tst_CategoryDialog::rejectLeavesTheCategoryAlone()
{
    DownloadCategory cat;
    cat.title = QStringLiteral("Movies");
    cat.incomingPath = m_dir.path();

    CategoryDialog dlg(cat, m_dir.path());
    edits(dlg).at(0)->setText(QStringLiteral("Something else"));
    edits(dlg).at(2)->setText(QStringLiteral("/nowhere/at/all"));
    dlg.reject();

    // MFC would have written both of those into catArr before finding out the
    // user meant to cancel.
    QCOMPARE(dlg.category().title, QStringLiteral("Movies"));
    QCOMPARE(dlg.category().incomingPath, m_dir.path());
}

void tst_CategoryDialog::emptyIncomingPathIsAccepted()
{
    DownloadCategory cat;
    cat.title = QStringLiteral("Movies");
    cat.incomingPath = m_dir.path();

    CategoryDialog dlg(cat, m_dir.path());
    edits(dlg).at(2)->clear();
    okButton(dlg)->click();

    QCOMPARE(dlg.result(), int(QDialog::Accepted));
    QVERIFY2(dlg.category().incomingPath.isEmpty(),
             "clearing the field is how a category goes back to the global folder");
}

void tst_CategoryDialog::missingFolderIsCreatedOnAccept()
{
    const QString fresh = m_dir.filePath(QStringLiteral("Fresh/Deeper"));
    QVERIFY(!QDir(fresh).exists());

    CategoryDialog dlg(DownloadCategory{}, m_dir.path());
    edits(dlg).at(0)->setText(QStringLiteral("Fresh"));
    edits(dlg).at(2)->setText(fresh);
    okButton(dlg)->click();

    QCOMPARE(dlg.result(), int(QDialog::Accepted));
    QVERIFY(QDir(fresh).exists());
}

void tst_CategoryDialog::invalidRegexpBlocksAccept()
{
    CategoryDialog dlg(DownloadCategory{}, m_dir.path());
    edits(dlg).at(0)->setText(QStringLiteral("Movies"));
    edits(dlg).at(3)->setText(QStringLiteral("[unterminated"));
    dlg.findChild<QCheckBox*>()->setChecked(true);

    okButton(dlg)->click();
    // Still open, as MFC's ErrorBalloon path leaves it. Accepting a pattern that
    // cannot compile would mean a category that silently never auto-assigns.
    QVERIFY(dlg.result() != int(QDialog::Accepted));
}

void tst_CategoryDialog::untitledCategoryBlocksAccept()
{
    CategoryDialog dlg(DownloadCategory{}, m_dir.path());
    edits(dlg).at(2)->setText(m_dir.path());
    okButton(dlg)->click();
    QVERIFY(dlg.result() != int(QDialog::Accepted));
}

QTEST_MAIN(tst_CategoryDialog)
#include "tst_CategoryDialog.moc"
