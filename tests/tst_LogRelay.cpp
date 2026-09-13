/// @file tst_LogRelay.cpp
/// @brief Daemon log buffer and its main-thread forward — eMule::Ipc::LogRelay.
///
/// Qt runs a message handler on whichever thread logged. The daemon used to hand
/// each line to the IPC sockets right there, so a worker-thread line was a
/// cross-thread QTcpSocket write, and ~40 Usenet connections logging at once
/// crashed it. The relay therefore has to:
///
///   - forward only from its own thread, whoever appended;
///   - keep the id order strict and deliver each line exactly once;
///   - defer even an own-thread append, so a line logged inside a broadcast
///     cannot re-enter it;
///   - cost one wakeup per burst, not one per line;
///   - keep the newest kMaxBuffered lines for SyncLogs.

#include "LogRelay.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSignalSpy>
#include <QTest>
#include <QThread>

#include <mutex>
#include <thread>
#include <vector>

using namespace eMule::Ipc;

namespace {

/// Below kMaxBuffered in total, so the ring never drops a line the test counts.
constexpr int kThreads = 8;
constexpr int kLinesPerThread = 50;

/// Every ready() emission, recorded under a mutex: a regressed relay emitting from
/// its workers must fail the thread check, not race in here.
class Recorder {
public:
    explicit Recorder(QThread* owner) : m_owner(owner) {}

    void record(const IpcMessage& msg)
    {
        std::lock_guard lock(m_mutex);
        m_ids.push_back(msg.fieldInt(0));
        if (QThread::currentThread() != m_owner)
            ++m_offThread;
    }

    [[nodiscard]] int count() const
    {
        std::lock_guard lock(m_mutex);
        return static_cast<int>(m_ids.size());
    }

    [[nodiscard]] int offThread() const
    {
        std::lock_guard lock(m_mutex);
        return m_offThread;
    }

    [[nodiscard]] std::vector<qint64> ids() const
    {
        std::lock_guard lock(m_mutex);
        return m_ids;
    }

private:
    QThread* m_owner;
    mutable std::mutex m_mutex;
    std::vector<qint64> m_ids;
    int m_offThread = 0;
};

/// Counts queued calls delivered to the object it filters — one per posted flush.
class MetaCallCounter : public QObject {
public:
    int count = 0;

protected:
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::MetaCall)
            ++count;
        return false;
    }
};

void appendLine(LogRelay& relay, const QString& text)
{
    relay.append(QStringLiteral("emule.usenet"), QtInfoMsg, text);
}

} // namespace

class tst_LogRelay : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void workerThreads_forwardOnOwnerThreadOnly();
    void ownerThreadAppend_isDeferred();
    void burst_coalescesIntoOneWakeup();
    void ring_capsAtMaxAndKeepsNewest();
    void push_carriesWireFields();
};

void tst_LogRelay::initTestCase()
{
    // QSignalSpy stores the IpcMessage argument as a QVariant.
    qRegisterMetaType<IpcMessage>();
}

void tst_LogRelay::workerThreads_forwardOnOwnerThreadOnly()
{
    LogRelay relay;
    Recorder recorder(relay.thread());

    // Direct, so the lambda runs wherever ready() is emitted. An auto connection
    // would queue a stray worker-thread emit back to this thread and hide it.
    connect(&relay, &LogRelay::ready, &relay,
            [&recorder](const IpcMessage& msg) { recorder.record(msg); },
            Qt::DirectConnection);

    std::vector<std::jthread> workers;
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&relay, t] {
            for (int i = 0; i < kLinesPerThread; ++i)
                appendLine(relay, QStringLiteral("worker %1 line %2").arg(t).arg(i));
        });
    }

    // Spins the event loop while the workers still append, so flushes race them.
    QTRY_COMPARE(recorder.count(), kThreads * kLinesPerThread);
    workers.clear();

    QCOMPARE(recorder.offThread(), 0);

    // Each line once, in the order the ids were handed out.
    const auto ids = recorder.ids();
    for (int i = 0; i < static_cast<int>(ids.size()); ++i)
        QCOMPARE(ids[i], qint64(i + 1));
}

void tst_LogRelay::ownerThreadAppend_isDeferred()
{
    LogRelay relay;
    QSignalSpy spy(&relay, &LogRelay::ready);

    // The line IpcServer logs from inside a broadcast. Forwarding it on the spot
    // would re-enter that broadcast, so even its own thread waits for the loop.
    relay.append(QStringLiteral("emule.general"), QtInfoMsg,
                 QStringLiteral("IPC client disconnected"));
    QCOMPARE(spy.count(), 0);

    QCoreApplication::sendPostedEvents(&relay, QEvent::MetaCall);
    QCOMPARE(spy.count(), 1);
}

void tst_LogRelay::burst_coalescesIntoOneWakeup()
{
    LogRelay relay;
    MetaCallCounter wakeups;
    relay.installEventFilter(&wakeups);
    QSignalSpy spy(&relay, &LogRelay::ready);

    // The add that crashed the daemon: ~40 connections logging in one millisecond.
    // This thread sits in join(), so nothing drains between lines.
    std::jthread([&relay] {
        for (int i = 0; i < 40; ++i)
            appendLine(relay, QStringLiteral("NNTP: connecting"));
    }).join();
    QCOMPARE(spy.count(), 0);

    QCoreApplication::sendPostedEvents(&relay, QEvent::MetaCall);
    QCOMPARE(wakeups.count, 1);
    QCOMPARE(spy.count(), 40);

    // The flush re-armed: the next line posts its own instead of being stranded.
    appendLine(relay, QStringLiteral("NNTP: connected"));
    QCoreApplication::sendPostedEvents(&relay, QEvent::MetaCall);
    QCOMPARE(wakeups.count, 2);
    QCOMPARE(spy.count(), 41);
}

void tst_LogRelay::ring_capsAtMaxAndKeepsNewest()
{
    LogRelay relay;
    const int total = LogRelay::kMaxBuffered + 10;
    for (int i = 1; i <= total; ++i)
        appendLine(relay, QString::number(i));

    const auto all = relay.since(0);
    QCOMPARE(static_cast<int>(all.size()), LogRelay::kMaxBuffered);
    QCOMPARE(all.front().id, qint64(11));
    QCOMPARE(all.front().message, QStringLiteral("11"));
    QCOMPARE(all.back().id, qint64(total));

    // SyncLogs asks from the GUI's checkpoint.
    QCOMPARE(static_cast<int>(relay.since(total - 3).size()), 3);
}

void tst_LogRelay::push_carriesWireFields()
{
    LogRelay relay;
    QSignalSpy spy(&relay, &LogRelay::ready);

    relay.append(QStringLiteral("emule.kad"), QtWarningMsg, QStringLiteral("Kad: firewalled"));
    QCoreApplication::sendPostedEvents(&relay, QEvent::MetaCall);
    QCOMPARE(spy.count(), 1);

    // The layout IpcClient::dispatchPushEvent and the GUI's LogWidget read.
    const auto msg = spy.at(0).at(0).value<IpcMessage>();
    QCOMPARE(msg.type(), IpcMsgType::PushLogMessage);
    QCOMPARE(msg.seqId(), 0);
    QCOMPARE(msg.fieldInt(0), qint64(1));
    QCOMPARE(msg.fieldString(1), QStringLiteral("emule.kad"));
    QCOMPARE(msg.fieldInt(2), qint64(QtWarningMsg));
    QCOMPARE(msg.fieldString(3), QStringLiteral("Kad: firewalled"));
    QVERIFY(qAbs(msg.fieldInt(4) - QDateTime::currentSecsSinceEpoch()) <= 2);
}

QTEST_GUILESS_MAIN(tst_LogRelay)
#include "tst_LogRelay.moc"
