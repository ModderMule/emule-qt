#include "pch.h"
/// @file tst_SearchHistory.cpp
/// @brief Search name box history: Del / Ctrl+Del in the drop-down, context menu, Options reset.

#include "app/UiState.h"
#include "panels/SearchPanel.h"
#include "prefs/Preferences.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QLineEdit>
#include <QMenu>
#include <QStringListModel>
#include <QTest>
#include <QTimer>

using namespace eMule;

namespace {

QLineEdit* nameEdit(SearchPanel& panel)
{
    for (auto* edit : panel.findChildren<QLineEdit*>())
        if (edit->completer())
            return edit;
    return nullptr;
}

/// Type @p prefix and wait for the history drop-down.
QAbstractItemView* openPopup(QLineEdit* edit, const QString& prefix)
{
    edit->clear();
    edit->setFocus();
    QTest::keyClicks(edit, prefix);
    QAbstractItemView* popup = edit->completer()->popup();
    if (!QTest::qWaitFor([popup] { return popup->isVisible(); }, 2000))
        return nullptr;
    return popup;
}

/// Pick the context menu entry @p text once the menu is up.
void triggerMenuEntry(const QString& text)
{
    QTimer::singleShot(50, [text] {
        if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
            for (QAction* a : menu->actions())
                if (a->text().startsWith(text)) {
                    a->trigger();
                    break;
                }
            menu->close();
        }
    });
}

} // namespace

class tst_SearchHistory : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        thePrefs.setUseAutoCompletion(true);
        theUiState.setSearchHistory({QStringLiteral("alpha"), QStringLiteral("alpine"), QStringLiteral("beta")});
    }

    /// Del drops the highlighted suggestion only
    void delRemovesHighlighted()
    {
        SearchPanel panel;
        panel.show();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        QLineEdit* edit = nameEdit(panel);
        QVERIFY(edit);

        QAbstractItemView* popup = openPopup(edit, QStringLiteral("alp"));
        QVERIFY(popup);
        QCOMPARE(popup->model()->rowCount(), 2);
        popup->setCurrentIndex(popup->model()->index(1, 0));
        const QString removed = popup->currentIndex().data().toString();
        QTest::keyClick(popup, Qt::Key_Delete);

        QStringList expected{QStringLiteral("alpha"), QStringLiteral("alpine"), QStringLiteral("beta")};
        expected.removeAll(removed);
        QCOMPARE(theUiState.searchHistory(), expected);
        QCOMPARE(edit->text(), QStringLiteral("alp"));   // the typed text stays
    }

    /// Ctrl+Del clears the whole history, as in MFC
    void ctrlDelClearsAll()
    {
        SearchPanel panel;
        panel.show();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        QLineEdit* edit = nameEdit(panel);
        QVERIFY(edit);

        QAbstractItemView* popup = openPopup(edit, QStringLiteral("a"));
        QVERIFY(popup);
        QTest::keyClick(popup, Qt::Key_Delete, Qt::ControlModifier);
        QVERIFY(theUiState.searchHistory().isEmpty());
        QVERIFY(!popup->isVisible());
    }

    /// Options > Reset clears UiState; the box re-reads it on focus
    void resetElsewhereReachesDropDown()
    {
        SearchPanel panel;
        panel.show();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        QLineEdit* edit = nameEdit(panel);
        QVERIFY(edit);

        theUiState.setSearchHistory({});
        edit->clearFocus();
        edit->setFocus();
        QTRY_VERIFY(edit->hasFocus());
        auto* model = qobject_cast<QStringListModel*>(edit->completer()->model());
        QVERIFY(model);
        QVERIFY(model->stringList().isEmpty());
    }

    /// Context menu: remove the typed entry, then clear the rest
    void contextMenuEntries()
    {
        SearchPanel panel;
        panel.show();
        QVERIFY(QTest::qWaitForWindowActive(&panel));
        QLineEdit* edit = nameEdit(panel);
        QVERIFY(edit);

        edit->setText(QStringLiteral("beta"));
        triggerMenuEntry(QStringLiteral("Remove"));
        emit edit->customContextMenuRequested(QPoint(5, 5));
        QCOMPARE(theUiState.searchHistory(), (QStringList{QStringLiteral("alpha"), QStringLiteral("alpine")}));

        triggerMenuEntry(QStringLiteral("Clear Search History"));
        emit edit->customContextMenuRequested(QPoint(5, 5));
        QVERIFY(theUiState.searchHistory().isEmpty());
    }
};

QTEST_MAIN(tst_SearchHistory)
#include "tst_SearchHistory.moc"
