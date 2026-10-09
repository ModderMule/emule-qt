/// @file tst_ListKeys.cpp
/// @brief MFC list accelerators on Qt views (utils/ListActivation) and F3 find-next.

#include "dialogs/FindInListDialog.h"
#include "utils/ListActivation.h"

#include <QApplication>
#include <QDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStandardItemModel>
#include <QTest>
#include <QTimer>
#include <QTreeView>

#include <algorithm>

using namespace eMule;

namespace {

/// Records the keys the view itself got, i.e. the ones the filter let through.
class RecordingView : public QTreeView {
public:
    QList<int> passed;

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        passed << event->key();
        QTreeView::keyPressEvent(event);
    }
};

QStandardItemModel* makeModel(QObject* parent, const QStringList& names)
{
    auto* model = new QStandardItemModel(parent);
    model->setHorizontalHeaderLabels({QStringLiteral("Name")});
    for (const QString& name : names)
        model->appendRow(new QStandardItem(name));
    return model;
}

/// Press @p key with the platform's binding for @p standard.
void pressStandard(QWidget* w, QKeySequence::StandardKey standard)
{
    const QKeyCombination combo = QKeySequence::keyBindings(standard).value(0)[0];
    QTest::keyClick(w, combo.key(), combo.keyboardModifiers());
}

} // namespace

class tst_ListKeys : public QObject {
    Q_OBJECT

private slots:
    void deleteAndBackspaceRemove();
    void modifiedDeleteIsIgnored();
    void commandKeysReachTheirHandlers();
    void unboundKeysTravelOn();
    void openEditorOwnsTheKeys();
    void findNextWrapsAndRepeats();
    void findIgnoresDisplayPadding();
    void windowCycleKeysExist();
    void ctrlF2IsItsOwnCommand();
    void spaceTogglesOnlyWhenTheListAnswers();
    void middleClickSelectsTheRowAndActs();
};

void tst_ListKeys::deleteAndBackspaceRemove()
{
    QTreeView view;
    view.setModel(makeModel(&view, {QStringLiteral("a")}));
    int removed = 0;
    ListKeyHandlers keys;
    keys.remove = [&] { ++removed; };
    bindListKeys(&view, std::move(keys));

    QTest::keyClick(&view, Qt::Key_Delete);
    QTest::keyClick(&view, Qt::Key_Backspace);
    QTest::keyClick(&view, Qt::Key_Delete, Qt::KeypadModifier);   // numpad Del
    QCOMPARE(removed, 3);
}

void tst_ListKeys::modifiedDeleteIsIgnored()
{
    RecordingView view;
    view.setModel(makeModel(&view, {QStringLiteral("a")}));
    int removed = 0;
    ListKeyHandlers keys;
    keys.remove = [&] { ++removed; };
    bindListKeys(&view, std::move(keys));

    QTest::keyClick(&view, Qt::Key_Delete, Qt::ShiftModifier);
    QTest::keyClick(&view, Qt::Key_Backspace, Qt::AltModifier);
    QCOMPARE(removed, 0);
}

void tst_ListKeys::commandKeysReachTheirHandlers()
{
    QTreeView view;
    view.setModel(makeModel(&view, {QStringLiteral("a")}));
    QStringList fired;
    ListKeyHandlers keys;
    keys.rename = [&] { fired << QStringLiteral("rename"); };
    keys.refresh = [&] { fired << QStringLiteral("refresh"); };
    keys.insert = [&] { fired << QStringLiteral("insert"); };
    keys.copy = [&] { fired << QStringLiteral("copy"); };
    keys.paste = [&] { fired << QStringLiteral("paste"); };
    keys.cut = [&] { fired << QStringLiteral("cut"); };
    bindListKeys(&view, std::move(keys));

    QTest::keyClick(&view, Qt::Key_F2);
    QTest::keyClick(&view, Qt::Key_F5);
    QTest::keyClick(&view, Qt::Key_Insert);
    pressStandard(&view, QKeySequence::Copy);
    pressStandard(&view, QKeySequence::Paste);
    pressStandard(&view, QKeySequence::Cut);
    QCOMPARE(fired, (QStringList{QStringLiteral("rename"), QStringLiteral("refresh"),
                                 QStringLiteral("insert"), QStringLiteral("copy"),
                                 QStringLiteral("paste"), QStringLiteral("cut")}));
}

void tst_ListKeys::unboundKeysTravelOn()
{
    RecordingView view;
    view.setModel(makeModel(&view, {QStringLiteral("a")}));
    int copied = 0;
    ListKeyHandlers keys;
    keys.copy = [&] { ++copied; };
    bindListKeys(&view, std::move(keys));

    QTest::keyClick(&view, Qt::Key_Delete);   // no remove handler
    QTest::keyClick(&view, Qt::Key_F2);       // no rename handler
    pressStandard(&view, QKeySequence::Copy); // bound: swallowed
    QCOMPARE(copied, 1);
    QVERIFY(view.passed.contains(Qt::Key_Delete));
    QVERIFY(view.passed.contains(Qt::Key_F2));
    // QTest presses the modifier keys themselves too; only real keys count.
    const auto real = std::ranges::count_if(view.passed, [](int k) {
        return k != Qt::Key_Control && k != Qt::Key_Meta && k != Qt::Key_Shift
            && k != Qt::Key_Alt;
    });
    QCOMPARE(real, 2);
}

void tst_ListKeys::openEditorOwnsTheKeys()
{
    QTreeView view;
    auto* model = makeModel(&view, {QStringLiteral("a")});
    view.setModel(model);
    int removed = 0;
    ListKeyHandlers keys;
    keys.remove = [&] { ++removed; };
    bindListKeys(&view, std::move(keys));
    view.show();

    const QModelIndex idx = model->index(0, 0);
    view.setCurrentIndex(idx);
    view.edit(idx);
    auto* editor = view.findChild<QLineEdit*>();
    QVERIFY(editor);
    editor->setFocus();

    // A Del the editor ignores bubbles to the view; it must not delete the row.
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    QCOMPARE(removed, 0);
}

void tst_ListKeys::findNextWrapsAndRepeats()
{
    QTreeView view;
    auto* model = makeModel(&view, {QStringLiteral("alpha"), QStringLiteral("beta"),
                                    QStringLiteral("alphabet"), QStringLiteral("gamma")});
    view.setModel(model);
    view.show();

    // Answer the modal dialog: term "alph", first match row 0.
    QTimer::singleShot(0, this, [] {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dlg);
        dlg->findChild<QLineEdit*>()->setText(QStringLiteral("alph"));
        dlg->accept();
    });
    showFindInListDialog(&view, &view);
    QCOMPARE(view.currentIndex().row(), 0);

    findNextInList(&view, &view);
    QCOMPARE(view.currentIndex().row(), 2);
    findNextInList(&view, &view);   // wraps
    QCOMPARE(view.currentIndex().row(), 0);
    findNextInList(&view, &view, true);   // Shift+F3 wraps backwards
    QCOMPARE(view.currentIndex().row(), 2);
}

void tst_ListKeys::findIgnoresDisplayPadding()
{
    // The server list shows "ip : port"; a typed "ip:port" must still hit it.
    QTreeView view;
    view.setModel(makeModel(&view, {QStringLiteral("91.208.162.55 : 4232"),
                                    QStringLiteral("91.208.162.182 : 4232")}));
    view.show();

    QTimer::singleShot(0, this, [] {
        auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        QVERIFY(dlg);
        dlg->findChild<QLineEdit*>()->setText(QStringLiteral(" 91.208.162.182:4232 "));
        dlg->accept();
    });
    showFindInListDialog(&view, &view);
    QCOMPARE(view.currentIndex().row(), 1);
}

void tst_ListKeys::windowCycleKeysExist()
{
    // MainWindow and OptionsDialog bind Ctrl+Tab via these; an empty binding list on a
    // platform would silently drop the shortcut.
    QVERIFY(!QKeySequence::keyBindings(QKeySequence::NextChild).isEmpty());
    QVERIFY(!QKeySequence::keyBindings(QKeySequence::PreviousChild).isEmpty());
    bool hasTab = false;
    for (const QKeySequence& seq : QKeySequence::keyBindings(QKeySequence::NextChild))
        hasTab = hasTab || seq[0].key() == Qt::Key_Tab;
    QVERIFY2(hasTab, qPrintable(QKeySequence::listToString(
                         QKeySequence::keyBindings(QKeySequence::NextChild))));
}

// MFC DownloadListCtrl.cpp:1394: F2 with Ctrl held is the file-name cleanup. The
// filter only knew the bare key, so Ctrl+F2 never arrived anywhere.
void tst_ListKeys::ctrlF2IsItsOwnCommand()
{
    QTreeView view;
    view.setModel(makeModel(&view, {QStringLiteral("a")}));
    int renamed = 0;
    int cleaned = 0;
    ListKeyHandlers keys;
    keys.rename = [&] { ++renamed; };
    keys.renameAll = [&] { ++cleaned; };
    bindListKeys(&view, std::move(keys));

    QTest::keyClick(&view, Qt::Key_F2);
    QCOMPARE(renamed, 1);
    QCOMPARE(cleaned, 0);
    QTest::keyClick(&view, Qt::Key_F2, Qt::ControlModifier);
    QCOMPARE(renamed, 1);
    QCOMPARE(cleaned, 1);
}

// MFC SharedFilesCtrl.cpp:1390: Space is the list's only while it shows checkboxes.
void tst_ListKeys::spaceTogglesOnlyWhenTheListAnswers()
{
    RecordingView view;
    view.setModel(makeModel(&view, {QStringLiteral("a")}));
    bool checkboxes = false;
    int toggled = 0;
    ListKeyHandlers keys;
    keys.toggle = [&] {
        if (!checkboxes)
            return false;
        ++toggled;
        return true;
    };
    bindListKeys(&view, std::move(keys));

    QTest::keyClick(&view, Qt::Key_Space);
    QCOMPARE(toggled, 0);
    QVERIFY(view.passed.contains(Qt::Key_Space));   // travelled on to Qt

    view.passed.clear();
    checkboxes = true;
    QTest::keyClick(&view, Qt::Key_Space);
    QCOMPARE(toggled, 1);
    QVERIFY(!view.passed.contains(Qt::Key_Space));
}

// MFC SharedFilesWnd.cpp:246-260, SearchResultsWnd.cpp:184-196: the row under the
// pointer becomes the selection and is acted on. Nothing listened for the button.
void tst_ListKeys::middleClickSelectsTheRowAndActs()
{
    QTreeView view;
    view.setModel(makeModel(&view, {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}));
    view.resize(200, 200);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));
    view.setCurrentIndex(view.model()->index(0, 0));

    QStringList acted;
    ListKeyHandlers keys;
    keys.middleClick = [&](const QModelIndex& index) { acted << index.data().toString(); };
    bindListKeys(&view, std::move(keys));

    const QModelIndex third = view.model()->index(2, 0);
    QTest::mouseClick(view.viewport(), Qt::MiddleButton, {}, view.visualRect(third).center());
    QCOMPARE(acted, QStringList{QStringLiteral("c")});
    QCOMPARE(view.currentIndex(), third);
    QCOMPARE(view.selectionModel()->selectedRows().size(), 1);

    // below the last row: nothing to act on
    QTest::mouseClick(view.viewport(), Qt::MiddleButton, {}, QPoint(10, 190));
    QCOMPARE(acted.size(), 1);

    // the other buttons are none of its business
    QTest::mouseClick(view.viewport(), Qt::LeftButton, {}, view.visualRect(view.model()->index(1, 0)).center());
    QCOMPARE(acted.size(), 1);
}

QTEST_MAIN(tst_ListKeys)
#include "tst_ListKeys.moc"
