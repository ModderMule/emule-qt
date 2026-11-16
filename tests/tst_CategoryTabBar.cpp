/// @file tst_CategoryTabBar.cpp
/// @brief Category tabs: drag to reorder, drop downloads on a tab.
///
/// QTabBar emits tabMoved once per tab crossed. One SetCategories per step,
/// each built from a list the daemon had already renumbered, swapped the
/// categories of two tabs' files. A drag must reach the core as one reorder.

#include "controls/CategoryTabBar.h"

#include "app/IpcClient.h"
#include "IpcConnection.h"
#include "IpcMessage.h"

#include <QCborArray>
#include <QCborMap>
#include <QDropEvent>
#include <QMimeData>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <memory>

using namespace eMule;
using namespace eMule::Ipc;

namespace {

/// A core that keeps a category list and renumbers it on SetCategories, as
/// IpcClientHandler::handleSetCategories does.
class MockCore : public QObject {
public:
    QStringList titles{QStringLiteral("All")};
    int setCount = 0;
    QList<int> lastOldIndex;

    MockCore()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                m_conn = std::make_unique<IpcConnection>(socket);
                connect(m_conn.get(), &IpcConnection::messageReceived, this, &MockCore::onMessage);
            }
        });
    }

    [[nodiscard]] bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

private:
    void push(const IpcMessage& msg)
    {
        if (m_conn)
            m_conn->sendMessage(msg);
    }

    void onMessage(const IpcMessage& msg)
    {
        switch (msg.type()) {
        case IpcMsgType::Handshake: {
            IpcMessage ok(IpcMsgType::HandshakeOk, msg.seqId());
            ok.append(QStringLiteral("1.0"));
            ok.append(QStringLiteral("mock core"));
            ok.append(QStringLiteral("session-1"));
            ok.append(false);
            ok.append(QString());
            push(ok);
            return;
        }
        case IpcMsgType::Subscribe:
            return;
        case IpcMsgType::GetCategories: {
            QCborArray rows;
            for (int i = 0; i < titles.size(); ++i)
                rows.append(QCborMap{{QStringLiteral("title"), titles.at(i)},
                                     {QStringLiteral("index"), i}});
            push(IpcMessage::makeResult(msg.seqId(), true, rows));
            return;
        }
        case IpcMsgType::SetCategories: {
            ++setCount;
            lastOldIndex.clear();
            QStringList next;
            for (const QCborValue& row : msg.fieldArray(0)) {
                const QCborMap map = row.toMap();
                next << map.value(QStringLiteral("title")).toString();
                lastOldIndex << int(map.value(QStringLiteral("oldIndex")).toInteger());
            }
            titles = next;
            push(IpcMessage::makeResult(msg.seqId(), true));
            return;
        }
        default:
            push(IpcMessage::makeResult(msg.seqId(), true));
            return;
        }
    }

    QTcpServer m_server;
    std::unique_ptr<IpcConnection> m_conn;
};

[[nodiscard]] QStringList tabTitles(const QTabBar& bar)
{
    QStringList out;
    for (int i = 0; i < bar.count(); ++i)
        out << bar.tabText(i);
    return out;
}

/// Drop @p hashes as a download drag at the centre of tab @p tab
bool dropOn(CategoryTabBar& bar, int tab, const QStringList& hashes, const char* mimeType)
{
    QMimeData mime;
    mime.setData(QLatin1String(mimeType), hashes.join(QLatin1Char('\n')).toUtf8());
    const QPointF pos = bar.tabRect(tab).center();
    QDragEnterEvent enter(pos.toPoint(), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&bar, &enter);
    QDragMoveEvent move(pos.toPoint(), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&bar, &move);
    if (!move.isAccepted())
        return false;
    QDropEvent drop(pos, Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&bar, &drop);
    return drop.isAccepted();
}

} // namespace

class tst_CategoryTabBar : public QObject {
    Q_OBJECT

private slots:
    void dragReorderSendsOnce();
    void dropDownloadsOnTab();
};

void tst_CategoryTabBar::dragReorderSendsOnce()
{
    MockCore core;
    core.titles = {QStringLiteral("All"), QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")};
    QVERIFY(core.listen());

    IpcClient ipc;
    ipc.connectToDaemon(QHostAddress::LocalHost, core.port());
    QTRY_VERIFY(ipc.isConnected());

    CategoryTabBar bar;
    bar.setIpcClient(&ipc);
    bar.resize(600, 30);
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));
    bar.requestCategories();
    QTRY_COMPARE(bar.count(), 4);

    // grab A, drag it over B and C: QTabBar announces two moves
    const QPoint grab = bar.tabRect(1).center();
    QTest::mousePress(&bar, Qt::LeftButton, Qt::NoModifier, grab);
    QCOMPARE(bar.currentCategory(), 1);
    bar.moveTab(1, 2);
    bar.moveTab(2, 3);
    QTest::qWait(50);
    QCOMPARE(core.setCount, 0);   // nothing sent while the button is down
    QTest::mouseRelease(&bar, Qt::LeftButton, Qt::NoModifier, bar.tabRect(3).center());

    QTRY_COMPARE(core.setCount, 1);
    QCOMPARE(core.lastOldIndex, (QList<int>{0, 2, 3, 1}));
    QCOMPARE(core.titles, (QStringList{QStringLiteral("All"), QStringLiteral("B"), QStringLiteral("C"),
                                       QStringLiteral("A")}));
    QTRY_COMPARE(tabTitles(bar), (QStringList{QStringLiteral("All"), QStringLiteral("B"),
                                              QStringLiteral("C"), QStringLiteral("A")}));
    // A stays selected, now as index 3
    QCOMPARE(bar.currentCategory(), 3);

    // a second drag right after starts from the renumbered list
    QTest::mousePress(&bar, Qt::LeftButton, Qt::NoModifier, bar.tabRect(1).center());
    bar.moveTab(1, 2);
    QTest::mouseRelease(&bar, Qt::LeftButton, Qt::NoModifier, bar.tabRect(2).center());
    QTRY_COMPARE(core.setCount, 2);
    QCOMPARE(core.lastOldIndex, (QList<int>{0, 2, 1, 3}));
    QCOMPARE(core.titles, (QStringList{QStringLiteral("All"), QStringLiteral("C"), QStringLiteral("B"),
                                       QStringLiteral("A")}));

    // "All" is pinned: dragged away it comes back, nothing sent
    QTest::mousePress(&bar, Qt::LeftButton, Qt::NoModifier, bar.tabRect(0).center());
    bar.moveTab(0, 1);
    QTest::mouseRelease(&bar, Qt::LeftButton, Qt::NoModifier, bar.tabRect(1).center());
    QTRY_COMPARE(bar.tabText(0), QStringLiteral("All"));
    QTest::qWait(50);
    QCOMPARE(core.setCount, 2);
}

void tst_CategoryTabBar::dropDownloadsOnTab()
{
    CategoryTabBar bar;
    // no core: the bar has "All" only; tabs added by hand carry their category index
    const int one = bar.addTab(QStringLiteral("One"));
    bar.setTabData(one, QVariant::fromValue(int64_t{1}));
    const int two = bar.addTab(QStringLiteral("Two"));
    bar.setTabData(two, QVariant::fromValue(int64_t{2}));
    bar.resize(400, 30);
    bar.show();
    QVERIFY(QTest::qWaitForWindowExposed(&bar));

    QSignalSpy dropped(&bar, &CategoryTabBar::downloadsDropped);
    const QStringList hashes{QStringLiteral("AA"), QStringLiteral("BB")};

    // from "All" onto a category
    QVERIFY(dropOn(bar, two, hashes, CategoryTabBar::kDownloadsMime));
    QCOMPARE(dropped.count(), 1);
    QCOMPARE(dropped.at(0).at(0).toInt(), 2);
    QCOMPARE(dropped.at(0).at(1).toStringList(), hashes);

    // shown category: not onto itself, but back to "All" (unassign) and elsewhere
    bar.setCurrentIndex(one);
    QVERIFY(!dropOn(bar, one, hashes, CategoryTabBar::kDownloadsMime));
    QVERIFY(dropOn(bar, 0, hashes, CategoryTabBar::kDownloadsMime));
    QCOMPARE(dropped.last().at(0).toInt(), 0);
    QVERIFY(dropOn(bar, two, hashes, CategoryTabBar::kDownloadsMime));
    QCOMPARE(dropped.count(), 3);

    // anything else is not ours
    QVERIFY(!dropOn(bar, two, hashes, "text/plain"));
    QCOMPARE(dropped.count(), 3);
}

QTEST_MAIN(tst_CategoryTabBar)
#include "tst_CategoryTabBar.moc"
