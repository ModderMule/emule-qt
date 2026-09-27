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
    void windowCycleKeysExist();
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

QTEST_MAIN(tst_ListKeys)
#include "tst_ListKeys.moc"
