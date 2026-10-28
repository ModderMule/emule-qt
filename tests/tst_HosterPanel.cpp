#include "pch.h"
/// @file tst_HosterPanel.cpp
/// @brief Hoster tab: package rows built from pushed items, type icons, folder links.
///
/// No daemon: items go in as the IpcClient's push signals, as the core would send them.

#include "app/IpcClient.h"
#include "panels/HosterPanel.h"
#include "utils/StringUtils.h"
#include "IpcMessage.h"
#include "IpcProtocol.h"

#include <QApplication>
#include <QCborArray>
#include <QCborMap>
#include <QDialog>
#include <QDir>
#include <QKeyEvent>
#include <QListWidget>
#include <QRadioButton>
#include <QTest>
#include <QTimer>
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
        QVERIFY(pkg->isExpanded());
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
};

QTEST_MAIN(tst_HosterPanel)
#include "tst_HosterPanel.moc"
