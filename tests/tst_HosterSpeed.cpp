/// @file tst_HosterSpeed.cpp
/// @brief Hoster download speed over real HTTP: a loopback mock hoster (MockHosterServer,
/// own thread) behind JD's DirectHTTP plugin in the real resolver sidecar. JDownloader itself
/// reaches 50 MiB/s and more on one connection; the sidecar alone and the whole path (sidecar,
/// part file read back, ed3k hashing, the file in incoming) must keep up with that.
///
/// Needs a built resolver.zip (scripts/build-hoster-resolver.sh, or EMULE_RESOLVER_ZIP) and
/// Java 17+; skips without. No internet. EMULE_HOSTER_SPEED_MIB: file size (default 512),
/// EMULE_HOSTER_MIN_MIBS: the floor (default 50).

#include "TestHelpers.h"

#include "Ed3kHosterJoin.h"
#include "Ed3kJavaRuntime.h"
#include "Ed3kJdResolver.h"
#include "Ed3kResolver.h"
#include "Ed3kSession.h"
#include "MockHosterServer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <map>
#include <memory>
#include <optional>
#include <vector>

using namespace eMule;
using namespace eMule::ed3k;

namespace {

Ed3kSettings loopback(const QString& dir)
{
    Ed3kSettings s;
    s.configDir = dir;
    s.udpPort = 0;
    s.bindAddress = QHostAddress::LocalHost;
    s.network = QStringLiteral("ed3k test");
    s.forceOpen = true;
    return s;
}

qint64 envInt(const char* name, qint64 fallback)
{
    bool ok = false;
    const qint64 v = qEnvironmentVariable(name).toLongLong(&ok);
    return ok && v > 0 ? v : fallback;
}

double mibs(qint64 bytes, qint64 ms)
{
    return ms > 0 ? double(bytes) / (1024.0 * 1024.0) / (double(ms) / 1000.0) : 0.0;
}

/// MD5 of what the server sends
QByteArray expectedMd5(const MockHosterServer& srv)
{
    QCryptographicHash h(QCryptographicHash::Md5);
    constexpr qint64 kStep = 8 * 1024 * 1024;
    QByteArray b(kStep, Qt::Uninitialized);
    for (qint64 off = 0; off < srv.size(); off += kStep) {
        const qint64 n = std::min(kStep, srv.size() - off);
        srv.fill(b.data(), off, n);
        h.addData(QByteArrayView(b.constData(), n));
    }
    return h.result();
}

QByteArray fileMd5(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Md5);
    h.addData(&f);
    return h.result();
}

} // namespace

class tst_HosterSpeed : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        const QString zip = Ed3kJdResolver::findBundledZip();
        if (zip.isEmpty())
            QSKIP("no resolver.zip: run scripts/build-hoster-resolver.sh");
        m_java = std::make_unique<Ed3kJavaRuntime>(m_tmp.path());
        m_java->locate({});
        QTRY_VERIFY_WITH_TIMEOUT(m_java->state() != Ed3kJavaRuntime::State::Probing, 60000);
        if (!m_java->runtime())
            QSKIP("no Java 17+ on this machine");

        m_server = std::make_unique<MockHosterServer>(envInt("EMULE_HOSTER_SPEED_MIB", 512) * 1024 * 1024);
        QVERIFY(m_server->port() != 0);
        m_minMibs = double(envInt("EMULE_HOSTER_MIN_MIBS", 50));
        m_md5 = expectedMd5(*m_server);

        m_resolver = std::make_unique<Ed3kJdResolver>();
        JdResolverConfig cfg;
        cfg.java = m_java->runtime()->java;
        cfg.dataDir = m_tmp.filePath(QStringLiteral("resolver"));
        cfg.zip = zip;
        cfg.loopbackHosters = true;
        m_resolver->setConfig(cfg);
        QVERIFY2(m_resolver->available(), qPrintable(m_resolver->unavailableReason()));
        connect(m_resolver.get(), &Ed3kResolver::taskEvent, this,
                [this](Ed3kResolver::TaskId t, const ResolverEvent& e) {
                    m_events[t].push_back(e);
                    if (e.kind == ResolverEvent::Kind::Done)
                        m_lastDoneMs = m_clock.elapsed();
                });
        m_clock.start();

        // sidecar up and plugins loaded before any clock runs
        const auto t = m_resolver->match(m_server->url(QStringLiteral("warm.bin")));
        QTRY_VERIFY_WITH_TIMEOUT(final(t), 120000);
        QCOMPARE(final(t)->kind, ResolverEvent::Kind::Matched);
        if (final(t)->host.isEmpty())
            QSKIP("resolver.zip predates the loopback switch: rebuild it (scripts/build-hoster-resolver.sh)");
    }

    /// JD's own download into data.part: what the user's JDownloader would do.
    void sidecar_reachesJdSpeed()
    {
        const QString dir = m_tmp.filePath(QStringLiteral("raw"));
        QVERIFY(QDir().mkpath(dir));
        const qint64 sentBefore = m_server->bytesSent();
        QElapsedTimer clock;
        clock.start();
        const auto t = m_resolver->open(m_server->url(QStringLiteral("raw.bin")), std::nullopt, dir, 0);
        QVERIFY(runUntil([&] { return final(t) != nullptr; }, 600000));
        const qint64 ms = clock.elapsed();
        const ResolverEvent* done = final(t);
        QVERIFY2(done->kind == ResolverEvent::Kind::Done, qPrintable(done->code + QLatin1Char(' ') + done->text));
        QCOMPARE(QFileInfo(done->path).size(), m_server->size());
        // the link check's GET is dropped after a few MiB: more than the file goes out
        QVERIFY(m_server->bytesSent() - sentBefore >= m_server->size());
        const double speed = mibs(m_server->size(), ms);
        qInfo("sidecar: %lld MiB in %lld ms = %.1f MiB/s", m_server->size() >> 20, ms, speed);
        QCOMPARE(fileMd5(done->path), m_md5);
        QVERIFY2(speed >= m_minMibs, qPrintable(QStringLiteral("%1 MiB/s < %2").arg(speed, 0, 'f', 1).arg(m_minMibs)));
    }

    /// The whole path: link added, file complete in incoming, ed3k root known.
    void join_reachesJdSpeed_data()
    {
        QTest::addColumn<int>("busyMs");
        QTest::newRow("idle") << 0;
        // the daemon's thread also runs eD2K, BitTorrent and IPC: 10 ms busy every 20 ms
        QTest::newRow("busy") << 10;
    }

    void join_reachesJdSpeed()
    {
        QFETCH(int, busyMs);
        QTimer busy;
        busy.setInterval(20);
        connect(&busy, &QTimer::timeout, this, [busyMs] {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < busyMs) {
            }
        });
        if (busyMs > 0)
            busy.start();
        const QString cfgDir = m_tmp.filePath(QStringLiteral("cfg-%1").arg(busyMs));
        Ed3kSession session;
        QVERIFY(session.start(loopback(cfgDir)));
        Ed3kHosterJoin join(session, cfgDir);
        join.setResolver(m_resolver.get());
        Ed3kHosterPrefs prefs;
        prefs.enabled = true;
        join.setConfig(prefs, m_tmp.filePath(QStringLiteral("temp-%1").arg(busyMs)),
                       m_tmp.filePath(QStringLiteral("incoming-%1").arg(busyMs)));
        join.start();

        QElapsedTimer clock;
        clock.start();
        const qint64 startMs = m_clock.elapsed();
        m_lastDoneMs = -1;
        const auto ids = join.addLinks(m_server->url(QStringLiteral("join.bin")));
        QCOMPARE(ids.size(), 1);
        const auto ended = [&] {
            const auto it = join.item(ids[0]);
            return it && (it->state == Ed3kHosterJoin::State::Complete || it->state == Ed3kHosterJoin::State::Failed
                          || it->state == Ed3kHosterJoin::State::NeedsUser);
        };
        QVERIFY(runUntil(ended, 600000));
        const qint64 ms = clock.elapsed();
        busy.stop();
        const auto item = *join.item(ids[0]);
        QVERIFY2(item.state == Ed3kHosterJoin::State::Complete, qPrintable(item.reasonArg));
        QCOMPARE(QFileInfo(item.file).size(), m_server->size());
        QVERIFY(!item.rootHex.isEmpty());
        const double speed = mibs(m_server->size(), ms);
        qInfo("join: %lld MiB in %lld ms = %.1f MiB/s (sidecar done after %lld ms)", m_server->size() >> 20, ms, speed,
              m_lastDoneMs - startMs);
        QCOMPARE(fileMd5(item.file), m_md5);
        QVERIFY2(speed >= m_minMibs, qPrintable(QStringLiteral("%1 MiB/s < %2").arg(speed, 0, 'f', 1).arg(m_minMibs)));
    }

    void cleanupTestCase()
    {
        m_resolver.reset();
        m_server.reset();
    }

private:
    /// Runs a real event loop until @p done: QTRY's qWait naps 10 ms between event batches,
    /// which would add up over hundreds of reads; the daemon's loop never naps.
    template <typename F>
    static bool runUntil(F done, int timeoutMs)
    {
        QEventLoop loop;
        QTimer poll;
        poll.setInterval(20);
        QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
            if (done())
                loop.quit();
        });
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
        poll.start();
        loop.exec();
        return done();
    }

    /// The task's last event when it ended, else nullptr
    const ResolverEvent* final(Ed3kResolver::TaskId t) const
    {
        const auto it = m_events.find(t);
        if (it == m_events.end())
            return nullptr;
        for (const ResolverEvent& e : it->second)
            if (e.isFinal())
                return &e;
        return nullptr;
    }

    QTemporaryDir m_tmp;
    std::unique_ptr<Ed3kJavaRuntime> m_java;
    std::unique_ptr<MockHosterServer> m_server;
    std::unique_ptr<Ed3kJdResolver> m_resolver;
    std::map<Ed3kResolver::TaskId, std::vector<ResolverEvent>> m_events;
    QElapsedTimer m_clock;
    qint64 m_lastDoneMs = -1;
    QByteArray m_md5;
    double m_minMibs = 50;
};

QTEST_GUILESS_MAIN(tst_HosterSpeed)
#include "tst_HosterSpeed.moc"
