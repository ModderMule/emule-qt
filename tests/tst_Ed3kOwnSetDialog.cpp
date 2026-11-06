/// @file tst_Ed3kOwnSetDialog.cpp
/// @brief My ed3k Sets in the GUI (batch 15): the "Create ed3k Set…" dialog (a folder set lists
/// no files; Terms… only for a paid set) and the rows of Ed3kOwnSetsView with their menu (Sell…
/// / Change Terms…, Stop / Resume Selling only for a sold set, no sale of a hidden set, Copy Link
/// not for a paid set before it is sold). With ED3K_OWNSETS_SHOTS set, both are grabbed there.

#include "dialogs/Ed3kOwnSetDialog.h"
#include "dialogs/Ed3kSellDialogs.h"
#include "panels/Ed3kOwnSetsView.h"

#include <QAction>
#include <QCborArray>
#include <QCborMap>
#include <QComboBox>
#include <QDir>
#include <QMenu>
#include <QPushButton>
#include <QTest>
#include <QTreeWidget>

using namespace eMule;

namespace {

QCborMap row(const QString& id, const QString& title, int kind, qint64 seq, const QString& scope = {}, bool stopped = false)
{
    return QCborMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("title"), title},
        {QStringLiteral("folder"), QString()},
        {QStringLiteral("files"), 3},
        {QStringLiteral("size"), qint64(5) * 1024 * 1024},
        {QStringLiteral("seq"), seq},
        {QStringLiteral("kind"), kind},
        {QStringLiteral("published"), seq > 0},
        {QStringLiteral("building"), false},
        {QStringLiteral("scope"), scope},
        {QStringLiteral("sold"), !scope.isEmpty() && !stopped},
        {QStringLiteral("stopped"), stopped},
        {QStringLiteral("link"), kind == 2 ? QString() : QStringLiteral("ed3k:set?key=aa&id=bb")},
        {QStringLiteral("error"), QString()},
    };
}

QAction* actionNamed(QMenu& menu, const QString& text)
{
    for (QAction* a : menu.actions())
        if (a->text() == text)
            return a;
    return nullptr;
}

void shoot(QWidget* w, const QString& name)
{
    const QString dir = qEnvironmentVariable("ED3K_OWNSETS_SHOTS");
    if (dir.isEmpty())
        return;
    w->show();
    QTest::qWait(200);
    w->grab().save(QDir(dir).filePath(name));
}

} // namespace

class tst_Ed3kOwnSetDialog : public QObject {
    Q_OBJECT

private slots:
    void aFolderSetListsNoFiles();
    void theFilesDialogStartsEmptyWithoutACore();
    void rowsAndTheirMenu();
};

void tst_Ed3kOwnSetDialog::aFolderSetListsNoFiles()
{
    Ed3kOwnSetDialog dlg(nullptr, {}, QStringLiteral("/data/Holiday"));
    QCOMPARE(dlg.title(), QStringLiteral("Holiday"));
    QVERIFY(dlg.files().isEmpty());
    QCOMPARE(dlg.kind(), int(Ed3kOwnSetDialog::Open));
    auto* kind = dlg.findChild<QComboBox*>();
    QVERIFY(kind);
    QPushButton* terms = nullptr;
    for (auto* b : dlg.findChildren<QPushButton*>())
        if (b->text() == Ed3kOwnSetDialog::tr("Terms..."))
            terms = b;
    QVERIFY(terms);
    QVERIFY(!terms->isEnabled());
    if constexpr (!kEd3kSellingShown) {
        // selling hidden for now: no Paid kind, no Terms…
        QCOMPARE(kind->findData(int(Ed3kOwnSetDialog::Paid)), -1);
        QVERIFY(terms->isHidden());
        shoot(&dlg, QStringLiteral("ed3k-own-set-folder-dialog.png"));
        return;
    }
    kind->setCurrentIndex(kind->findData(int(Ed3kOwnSetDialog::Paid)));
    QCOMPARE(dlg.kind(), int(Ed3kOwnSetDialog::Paid));
    QVERIFY(terms->isEnabled());
    kind->setCurrentIndex(kind->findData(int(Ed3kOwnSetDialog::Hidden)));
    QVERIFY(!terms->isEnabled());
    shoot(&dlg, QStringLiteral("ed3k-own-set-folder-dialog.png"));
}

void tst_Ed3kOwnSetDialog::theFilesDialogStartsEmptyWithoutACore()
{
    Ed3kOwnSetDialog dlg(nullptr, {QStringLiteral("/data/a.bin")}, {});
    QVERIFY(dlg.title().startsWith(QStringLiteral("New ed3k Set-")));
    // the shared files come from the core: none without one
    QVERIFY(dlg.files().isEmpty());
    QCOMPARE(dlg.findChildren<QTreeWidget*>().size(), 2);
    shoot(&dlg, QStringLiteral("ed3k-own-set-create-dialog.png"));
}

void tst_Ed3kOwnSetDialog::rowsAndTheirMenu()
{
    Ed3kOwnSetsView view;
    const QString scope(32, QLatin1Char('a'));
    view.showRows(QCborArray{
        row(QStringLiteral("01"), QStringLiteral("Holiday"), 1, 2),
        row(QStringLiteral("02"), QStringLiteral("Private"), 2, 1),
        row(QStringLiteral("03"), QStringLiteral("Season (new)"), 3, 0),
        row(QStringLiteral("04"), QStringLiteral("Season"), 3, 4, scope, true),
    });
    auto* list = view.findChild<QTreeWidget*>();
    QVERIFY(list);
    QCOMPARE(list->topLevelItemCount(), 4);
    QCOMPARE(view.rows().size(), 4);

    if constexpr (!kEd3kSellingShown) {
        // selling hidden for now: no sell actions
        for (const QString& id : {QStringLiteral("01"), QStringLiteral("04")}) {
            QMenu m;
            view.addSetActions(&m, id);
            QVERIFY(!actionNamed(m, Ed3kOwnSetsView::tr("Sell...")));
            QVERIFY(!actionNamed(m, Ed3kOwnSetsView::tr("Change Terms...")));
            QVERIFY(!actionNamed(m, Ed3kOwnSetsView::tr("Resume Selling")));
            QVERIFY(!actionNamed(m, Ed3kOwnSetsView::tr("Stop Selling")));
        }
        return;
    }
    {
        QMenu m;
        view.addSetActions(&m, QStringLiteral("01"));
        QVERIFY(actionNamed(m, Ed3kOwnSetsView::tr("Sell..."))->isEnabled());
        QVERIFY(!actionNamed(m, Ed3kOwnSetsView::tr("Stop Selling"))->isEnabled());
        QVERIFY(actionNamed(m, Ed3kOwnSetsView::tr("Copy Link"))->isEnabled());
        QVERIFY(actionNamed(m, Ed3kOwnSetsView::tr("Unpublish..."))->isEnabled());
    }
    {
        // a hidden set is not sold (kind 3, a paid hidden set, is not offered)
        QMenu m;
        view.addSetActions(&m, QStringLiteral("02"));
        QVERIFY(!actionNamed(m, Ed3kOwnSetsView::tr("Sell..."))->isEnabled());
        QVERIFY(actionNamed(m, Ed3kOwnSetsView::tr("Copy Link"))->isEnabled());
    }
    {
        // paid, not sold yet: no version, no link
        QMenu m;
        view.addSetActions(&m, QStringLiteral("03"));
        QVERIFY(actionNamed(m, Ed3kOwnSetsView::tr("Sell..."))->isEnabled());
        QVERIFY(!actionNamed(m, Ed3kOwnSetsView::tr("Copy Link"))->isEnabled());
        QVERIFY(!actionNamed(m, Ed3kOwnSetsView::tr("Publish New Version"))->isEnabled());
    }
    {
        // sold, stopped: change the terms or resume
        QMenu m;
        view.addSetActions(&m, QStringLiteral("04"));
        QVERIFY(actionNamed(m, Ed3kOwnSetsView::tr("Change Terms...")));
        QVERIFY(actionNamed(m, Ed3kOwnSetsView::tr("Resume Selling"))->isEnabled());
    }
    view.resize(760, 220);
    shoot(&view, QStringLiteral("ed3k-own-sets-rows.png"));
}

QTEST_MAIN(tst_Ed3kOwnSetDialog)
#include "tst_Ed3kOwnSetDialog.moc"
