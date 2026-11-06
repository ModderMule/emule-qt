#include "pch.h"
/// @file tst_HosterPanel.cpp
/// @brief Hoster tab: package rows built from pushed items, type icons, folder links.
///
/// No daemon: items go in as the IpcClient's push signals, as the core would send them.

#include "app/IpcClient.h"
#include "app/UiState.h"
#include "dialogs/HosterPuzzleDialog.h"
#include "panels/HosterPanel.h"
#include "utils/StringUtils.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRadioButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

using namespace eMule;
using Ipc::IpcMessage;
using Ipc::IpcMsgType;

namespace {

QCborMap item(qint64 id, const QString& name, qint64 pkg, const QString& package, int state = 5,
              qint64 size = 100, qint64 done = 50, const QString& host = QStringLiteral("a.test"))
{
    return QCborMap{{QStringLiteral("id"), id},
                    {QStringLiteral("url"), QStringLiteral("https://%1/%2").arg(host).arg(id)},
                    {QStringLiteral("host"), host},
                    {QStringLiteral("name"), name},
                    {QStringLiteral("size"), size},
                    {QStringLiteral("done"), done},
                    {QStringLiteral("speed"), qint64(10)},
                    {QStringLiteral("state"), state},
                    {QStringLiteral("packageId"), pkg},
                    {QStringLiteral("package"), package}};
}

void push(IpcClient& ipc, const QCborMap& it)
{
    IpcMessage msg(IpcMsgType::PushHosterItem, 0);
    msg.append(it);
    emit ipc.hosterItemChanged(msg);
}

void pushRemoved(IpcClient& ipc, qint64 id)
{
    IpcMessage msg(IpcMsgType::PushHosterItemRemoved, 0);
    msg.append(id);
    emit ipc.hosterItemRemoved(msg);
}

void shoot(QWidget* w, const QString& name)
{
    const QString dir = qEnvironmentVariable("EMULE_TEST_SHOTS");
    if (dir.isEmpty())
        return;
    w->resize(900, 260);
    w->show();
    QTest::qWait(50);
    w->grab().save(QDir(dir).filePath(name));
}

} // namespace

class tst_HosterPanel : public QObject {
    Q_OBJECT

private slots:
    void packagesHoldTheirItems()
    {
        IpcClient ipc;
        HosterPanel panel;
        panel.setIpcClient(&ipc);
        auto* tree = panel.findChild<QTreeWidget*>();
        QVERIFY(tree);

        push(ipc, item(1, QStringLiteral("part1.rar"), 7, QStringLiteral("Release")));
        push(ipc, item(2, QStringLiteral("part2.rar"), 7, QStringLiteral("Release"), 8, 100, 100,
                       QStringLiteral("b.test")));
        push(ipc, item(3, QStringLiteral("movie.mkv"), 0, {}));
        QCOMPARE(tree->topLevelItemCount(), 2);   // one package, one plain item

        QTreeWidgetItem* pkg = nullptr;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->childCount() > 0)
                pkg = tree->topLevelItem(i);
        QVERIFY(pkg);
        QCOMPARE(pkg->text(0), QStringLiteral("Release"));
        QCOMPARE(pkg->childCount(), 2);
        QVERIFY(!pkg->isExpanded());   // packages start collapsed
        QVERIFY(!pkg->icon(0).isNull());
        QVERIFY(!pkg->child(0)->icon(0).isNull());   // the file's type, as the Transfer window
        QCOMPARE(pkg->text(1), formatByteSize(200));
        QCOMPARE(pkg->text(2), QStringLiteral("75.0%"));
        QVERIFY(pkg->text(4).contains(QStringLiteral("a.test")));
        QVERIFY(pkg->text(4).contains(QStringLiteral("b.test")));
        QVERIFY(pkg->text(5).contains(HosterPanel::tr("%1 of %2 complete").arg(1).arg(2)));
        shoot(&panel, QStringLiteral("hoster-packages.png"));

        // an item that leaves for top level, then the last one: the package row goes
        push(ipc, item(1, QStringLiteral("part1.rar"), 0, {}));
        QCOMPARE(pkg->childCount(), 1);
        QCOMPARE(tree->topLevelItemCount(), 3);
        pushRemoved(ipc, 2);
        QCOMPARE(tree->topLevelItemCount(), 2);
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            QCOMPARE(tree->topLevelItem(i)->childCount(), 0);
    }

    /// The status filter row hides rows of other states; a package shows while an item does
    void statusFilterHidesRows()
    {
        theUiState.setHosterFilter(0);
        theUiState.setShowHosterStatusFilter(true);
        IpcClient ipc;
        HosterPanel panel;
        panel.setIpcClient(&ipc);
        auto* tree = panel.findChild<QTreeWidget*>();
        QVERIFY(tree);
        push(ipc, item(1, QStringLiteral("a.rar"), 7, QStringLiteral("Release"), 5));    // downloading
        push(ipc, item(2, QStringLiteral("b.rar"), 7, QStringLiteral("Release"), 8));    // complete
        push(ipc, item(3, QStringLiteral("c.mkv"), 0, {}, 7));                           // paused
        push(ipc, item(4, QStringLiteral("d.mkv"), 0, {}, 12));                          // failed

        const auto button = [&panel](const QString& name) -> QToolButton* {
            for (auto* b : panel.findChildren<QToolButton*>())
                if (b->text().startsWith(name + QStringLiteral(" (")))
                    return b;
            return nullptr;
        };
        QToolButton* complete = button(HosterPanel::tr("Complete"));
        QVERIFY(complete);
        QCOMPARE(complete->text(), HosterPanel::tr("Complete") + QStringLiteral(" (1)"));
        QCOMPARE(button(HosterPanel::tr("All"))->text(), HosterPanel::tr("All") + QStringLiteral(" (4)"));

        const auto visibleNames = [tree] {
            QStringList out;
            for (QTreeWidgetItemIterator it(tree); *it; ++it)
                if (!(*it)->isHidden() && (!(*it)->parent() || !(*it)->parent()->isHidden()))
                    out << (*it)->text(0);
            out.sort();
            return out;
        };
        complete->click();
        QCOMPARE(visibleNames(), (QStringList{QStringLiteral("Release"), QStringLiteral("b.rar")}));
        QCOMPARE(theUiState.hosterFilter(), 2);
        button(HosterPanel::tr("Error"))->click();
        QCOMPARE(visibleNames(), QStringList{QStringLiteral("d.mkv")});

        // a state change moves the row in or out
        push(ipc, item(3, QStringLiteral("c.mkv"), 0, {}, 12));
        QCOMPARE(visibleNames(), (QStringList{QStringLiteral("c.mkv"), QStringLiteral("d.mkv")}));

        // the row off in Display options: everything shows, the choice is kept
        theUiState.setShowHosterStatusFilter(false);
        panel.applyDisplayOptions();
        QCOMPARE(visibleNames().size(), 5);
        QCOMPARE(theUiState.hosterFilter(), 5);
        theUiState.setShowHosterStatusFilter(true);
        panel.applyDisplayOptions();
        QCOMPARE(visibleNames().size(), 2);
        theUiState.setHosterFilter(0);
    }

    /// Esc clears the selection, as on the Transfer list; with none it travels on
    void escapeDeselects()
    {
        IpcClient ipc;
        HosterPanel panel;
        panel.setIpcClient(&ipc);
        auto* tree = panel.findChild<QTreeWidget*>();
        push(ipc, item(1, QStringLiteral("a.bin"), 0, {}));
        push(ipc, item(2, QStringLiteral("b.bin"), 0, {}));
        tree->selectAll();
        QCOMPARE(tree->selectedItems().size(), 2);
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(tree, &esc);
        QVERIFY(esc.isAccepted());
        QVERIFY(tree->selectedItems().isEmpty());
        QKeyEvent again(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(tree, &again);
        QVERIFY(tree->selectedItems().isEmpty());
    }

    /// a folder inside a package: the user picks the package or a new one
    void folderInPackageAsks()
    {
        IpcClient ipc;
        HosterPanel panel;
        panel.setIpcClient(&ipc);
        push(ipc, item(5, QStringLiteral("Folder A"), 9, QStringLiteral("Release"), 8));

        int radios = -1;
        QString first;
        QTimer::singleShot(50, [&] {
            auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dlg)
                return;
            const auto buttons = dlg->findChildren<QRadioButton*>();
            radios = int(buttons.size());
            if (!buttons.isEmpty())
                first = buttons.first()->text();
            dlg->reject();
        });
        IpcMessage msg(IpcMsgType::PushHosterLinks, 0);
        msg.append(qint64(5));
        msg.append(QCborArray{QCborMap{{QStringLiteral("url"), QStringLiteral("https://a.test/x")},
                                       {QStringLiteral("name"), QStringLiteral("x.bin")}}});
        emit ipc.hosterLinksExpanded(msg);
        QCOMPARE(radios, 2);
        QCOMPARE(first, HosterPanel::tr("Add to the package \"%1\"").arg(QStringLiteral("Release")));

        // a folder at top level: no choice, it becomes a package
        push(ipc, item(6, QStringLiteral("Folder B"), 0, {}, 8));
        radios = -1;
        QTimer::singleShot(50, [&] {
            if (auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
                radios = int(dlg->findChildren<QRadioButton*>().size());
                dlg->reject();
            }
        });
        IpcMessage top(IpcMsgType::PushHosterLinks, 0);
        top.append(qint64(6));
        top.append(QCborArray{QCborMap{{QStringLiteral("url"), QStringLiteral("https://a.test/y")}}});
        emit ipc.hosterLinksExpanded(top);
        QCOMPARE(radios, 0);
    }

    /// the bar: Add Links (the text box) and Open DLC, beside the engine's version
    void barHasAddLinksAndOpenDlc()
    {
        HosterPanel panel;
        QStringList texts;
        for (auto* b : panel.findChildren<QPushButton*>())
            texts << b->text();
        QVERIFY2(texts.contains(HosterPanel::tr("Add Links...")), qPrintable(texts.join(u'|')));
        QVERIFY2(texts.contains(HosterPanel::tr("Open DLC...")), qPrintable(texts.join(u'|')));
    }

    /// a container's links: several packages listed with their names, one package editable
    void containerLinksAsk()
    {
        IpcClient ipc;
        HosterPanel panel;
        panel.setIpcClient(&ipc);
        const auto link = [](const QString& url, const QString& name, const QString& pkg) {
            return QCborMap{{QStringLiteral("url"), url}, {QStringLiteral("name"), name}, {QStringLiteral("package"), pkg},
                            {QStringLiteral("passwords"), QCborArray{QStringLiteral("pw")}}};
        };
        int rows = -1;
        QStringList texts;
        QString packageName = QStringLiteral("<none>");
        QString title;
        const auto inspect = [&] {
            auto* dlg = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (!dlg)
                return;
            title = dlg->windowTitle();
            auto* list = dlg->findChild<QListWidget*>();
            rows = list ? list->count() : -1;
            texts.clear();
            for (int i = 0; list && i < list->count(); ++i)
                texts << list->item(i)->text();
            if (auto* edit = dlg->findChild<QLineEdit*>())
                packageName = edit->text();
            dlg->reject();
        };
        const auto send = [&ipc](const QCborArray& links) {
            IpcMessage msg(IpcMsgType::PushHosterLinks, 0);
            msg.append(qint64(0));
            msg.append(links);
            msg.append(QCborMap{{QStringLiteral("token"), qint64(1)}, {QStringLiteral("fileName"), QStringLiteral("set.dlc")}});
            emit ipc.hosterLinksExpanded(msg);
        };

        QTimer::singleShot(50, inspect);
        send({link(QStringLiteral("https://a.test/1"), QStringLiteral("one.rar"), QStringLiteral("Pkg A")),
              link(QStringLiteral("https://a.test/2"), QStringLiteral("two.rar"), QStringLiteral("Pkg B"))});
        QCOMPARE(rows, 2);
        QCOMPARE(title, HosterPanel::tr("Links of %1").arg(QStringLiteral("set.dlc")));
        QVERIFY2(texts.first().startsWith(QStringLiteral("[Pkg A] one.rar")), qPrintable(texts.first()));
        QCOMPARE(packageName, QStringLiteral("<none>"));   // several packages: their own names

        QTimer::singleShot(50, inspect);
        send({link(QStringLiteral("https://a.test/1"), QStringLiteral("one.rar"), QStringLiteral("Pkg A"))});
        QCOMPARE(rows, 1);
        QCOMPARE(texts.first(), QStringLiteral("one.rar"));
        QCOMPARE(packageName, QStringLiteral("Pkg A"));

        // no package in the container: the file's name
        packageName.clear();
        QTimer::singleShot(50, inspect);
        send({QCborMap{{QStringLiteral("url"), QStringLiteral("https://a.test/3")}}});
        QCOMPARE(packageName, QStringLiteral("set"));
    }

    /// no links: a box saying why, worded by code
    void containerFailureWarns()
    {
        IpcClient ipc;
        HosterPanel panel;
        panel.setIpcClient(&ipc);
        QString text;
        QTimer::singleShot(50, [&] {
            if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                text = box->text();
                box->reject();
            }
        });
        IpcMessage msg(IpcMsgType::PushHosterLinks, 0);
        msg.append(qint64(0));
        msg.append(QCborArray{});
        msg.append(QCborMap{{QStringLiteral("fileName"), QStringLiteral("x.dlc")}, {QStringLiteral("code"), QStringLiteral("limit")},
                            {QStringLiteral("text"), QStringLiteral("Server claims: DLC Limit reached.")}});
        emit ipc.hosterLinksExpanded(msg);
        QVERIFY2(text.startsWith(QStringLiteral("x.dlc: ")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("too many")), qPrintable(text));
    }

    /// a .dlc dragged over the list: the list takes no drops, the panel does (Torrents and
    /// Usenet take .torrent and .nzb the same way); other files are not taken
    void dropOnListReachesPanel()
    {
        HosterPanel panel;
        panel.resize(600, 300);
        panel.show();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        auto* tree = panel.findChild<QTreeWidget*>();
        QVERIFY(tree);
        QTemporaryDir tmp;
        const QString dlc = tmp.filePath(QStringLiteral("set.DLC"));
        const QString txt = tmp.filePath(QStringLiteral("notes.txt"));
        for (const QString& p : {dlc, txt}) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        const auto accepted = [&](const QString& path) {
            QMimeData mime;
            mime.setUrls({QUrl::fromLocalFile(path)});
            QDragEnterEvent ev(tree->viewport()->rect().center(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tree->viewport(), &ev);
            return ev.isAccepted();
        };
        QVERIFY(accepted(dlc));
        QVERIFY(!accepted(txt));
    }

    /// an extraction that ends wanting a password asks once per package; rows loaded wanting
    /// one do not (the menu's Set Password covers them)
    void passwordAskedOnceWhenExtractionWantsOne()
    {
        IpcClient ipc;
        HosterPanel panel;
        panel.setIpcClient(&ipc);
        const auto with = [](qint64 id, int reason) {
            QCborMap it = item(id, QStringLiteral("p%1.rar").arg(id), 7, QStringLiteral("Pack"), 8);   // Complete
            it.insert(QStringLiteral("reason"), reason);
            return it;
        };
        const auto dialogs = [] {
            QList<QInputDialog*> open;
            for (QWidget* w : QApplication::topLevelWidgets())
                if (auto* d = qobject_cast<QInputDialog*>(w); d && d->isVisible())
                    open << d;
            return open;
        };
        push(ipc, with(1, 34));   // first seen wanting one
        push(ipc, with(2, 31));
        push(ipc, with(3, 31));
        QCoreApplication::processEvents();
        QVERIFY(dialogs().isEmpty());
        push(ipc, with(2, 34));
        push(ipc, with(3, 34));   // same package: the open dialog stays the only one
        QTRY_COMPARE(dialogs().size(), 1);
        QCoreApplication::processEvents();
        QCOMPARE(dialogs().size(), 1);
        QVERIFY(dialogs().first()->labelText().contains(QStringLiteral("Pack")));
        QCOMPARE(dialogs().first()->textEchoMode(), QLineEdit::Password);
        dialogs().first()->reject();
        QTRY_VERIFY(dialogs().isEmpty());
    }

    // the box's own reject ran before the button's slot: "Skip hoster" arrived as a plain skip
    void puzzleSkipHosterSendsFlag()
    {
        for (const QString& label : {QStringLiteral("Skip hoster"), QStringLiteral("Skip")}) {
            HosterPuzzleDialog dlg({}, QStringLiteral("a.test"), QStringLiteral("f.bin"), {}, 60);
            int calls = 0;
            bool skipHoster = false;
            connect(&dlg, &HosterPuzzleDialog::answered, this, [&](const QString& text, bool flag) {
                ++calls;
                skipHoster = flag;
                QVERIFY(text.isEmpty());
            });
            QPushButton* button = nullptr;
            for (auto* b : dlg.findChildren<QPushButton*>())
                if (b->text() == label)
                    button = b;
            QVERIFY(button);
            button->click();
            QCOMPARE(calls, 1);
            QCOMPARE(skipHoster, label == QLatin1String("Skip hoster"));
        }
    }
};

QTEST_MAIN(tst_HosterPanel)
#include "tst_HosterPanel.moc"
