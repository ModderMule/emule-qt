/// @file tst_UsenetQueue.cpp
/// @brief Phase 3's exit criterion: an NZB queued, downloaded and shared.
///
/// The joins this covers, all of which the layers below cannot:
///
///   - Segments are dispatched across worker threads and arrive out of order, so
///     the assembled file is only correct if every writer placed its part at the
///     absolute offset the article declared.
///   - The completed file lands in the *incoming* directory, not a subfolder:
///     the share scan does not recurse, so a subfolder would never be offered.
///   - Nothing named `.usenetpart` survives a completion.
///   - Resume state is per-segment, so a restart must not refetch what is done.

#include "FakeNntpServer.h"
#include "TestHelpers.h"

#include "decode/YencDecoder.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"
#include "queue/UsenetQueueStore.h"

#include "prefs/Preferences.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

using namespace eMule;
using namespace eMule::usenet;
using eMule::testing::FakeNntpServer;

namespace {

constexpr int kPartSize = 2500;
constexpr int kParts = 6;

QByteArray payload(int size)
{
    QByteArray data;
    data.resize(size);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 17 + (i >> 6) * 5) & 0xFF);
    return data;
}

/// One yEnc article, exactly as a provider serves it — 1-based `begin` included.
QByteArray makeArticle(const QByteArray& whole, int part, int total, const QString& name)
{
    const int offset = (part - 1) * kPartSize;
    const QByteArray chunk = whole.mid(offset, kPartSize);

    QByteArray out;
    out += QStringLiteral("=ybegin part=%1 total=%2 line=128 size=%3 name=%4")
               .arg(part).arg(total).arg(whole.size()).arg(name).toLatin1();
    out += '\n';
    out += QStringLiteral("=ypart begin=%1 end=%2")
               .arg(offset + 1).arg(offset + chunk.size()).toLatin1();
    out += '\n';

    QByteArray line;
    for (const char raw : chunk) {
        const auto enc = quint8(quint8(raw) + 42);
        if (enc == 0x00 || enc == 0x0A || enc == 0x0D || enc == '=') {
            line.append('=');
            line.append(char(quint8(enc + 64)));
        } else {
            line.append(char(enc));
        }
        if (line.size() >= 128) {
            out += line;
            out += '\n';
            line.clear();
        }
    }
    if (!line.isEmpty()) {
        out += line;
        out += '\n';
    }

    out += QStringLiteral("=yend size=%1 part=%2 pcrc32=%3")
               .arg(chunk.size()).arg(part)
               .arg(yencCrc32(0, chunk), 8, 16, QLatin1Char('0')).toLatin1();
    return out;
}

QString messageIdFor(int part) { return QStringLiteral("q%1@example.com").arg(part); }

QByteArray makeNzb(const QString& fileName, int parts)
{
    QByteArray xml;
    xml += R"(<?xml version="1.0" encoding="iso-8859-1" ?>)"
           "\n<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n";
    xml += QStringLiteral(
               "  <file poster=\"tester\" date=\"1700000000\" "
               "subject=\"&quot;%1&quot; yEnc (1/%2)\">\n")
               .arg(fileName).arg(parts).toUtf8();
    xml += "    <groups><group>alt.binaries.test</group></groups>\n";
    xml += "    <segments>\n";
    for (int p = 1; p <= parts; ++p) {
        xml += QStringLiteral("      <segment bytes=\"3500\" number=\"%1\">%2</segment>\n")
                   .arg(p).arg(messageIdFor(p)).toUtf8();
    }
    xml += "    </segments>\n  </file>\n</nzb>\n";
    return xml;
}

NewsServer serverConfig(quint16 port, int maxConnections)
{
    NewsServer s;
    s.name = QStringLiteral("fake");
    s.host = QStringLiteral("127.0.0.1");
    s.port = port;
    s.tlsMode = NntpTlsMode::None;
    s.user = QStringLiteral("testuser");
    s.pass = QStringLiteral("testpass");
    s.maxConnections = maxConnections;
    return s;
}

} // namespace

class tst_UsenetQueue : public QObject {
    Q_OBJECT

private slots:
    void downloadsAnNzbByteIdentically();
    void leavesNoScratchBehind();
    void persistedStateResumesInsteadOfRefetching();
    void connectionBudgetIsDividedNotReplicated();
    void missingArticlesEscalateToTheNextLevel();
};

void tst_UsenetQueue::downloadsAnNzbByteIdentically()
{
    const QByteArray whole = payload(kPartSize * kParts);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), kParts, 1, kParts);
    for (int p = 1; p <= kParts; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, kParts,
                                                       QStringLiteral("movie.mkv")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);

    QString error;
    const QString id = queue.addNzb(makeNzb(QStringLiteral("movie.mkv"), kParts),
                                    QStringLiteral("release"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QVERIFY2(finished.wait(30000), "the download never reported a terminal outcome");
    QCOMPARE(finished.at(0).at(0).toString(), id);
    QVERIFY2(finished.at(0).at(1).toBool(), "completed with missing articles");

    // The whole point: the file must be in the incoming directory itself, because
    // the share scan does not recurse into subfolders.
    const QString out = QDir(thePrefs.incomingDir()).filePath(QStringLiteral("movie.mkv"));
    QVERIFY2(QFile::exists(out), qPrintable(QStringLiteral("missing: %1").arg(out)));

    QFile f(out);
    QVERIFY(f.open(QIODevice::ReadOnly));
    // Byte-for-byte, not a hash of the parts: an off-by-one in the 1-based
    // `=ypart begin` still passes every pcrc32, because yEnc verifies the payload
    // and not where it lands.
    QCOMPARE(f.readAll(), whole);
    f.close();

    queue.stop();
}

void tst_UsenetQueue::leavesNoScratchBehind()
{
    const QByteArray whole = payload(kPartSize * 3);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 3, 1, 3);
    for (int p = 1; p <= 3; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, 3,
                                                       QStringLiteral("small.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 2)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("small.bin"), 3),
                          QStringLiteral("small"), error).isEmpty());
    QVERIFY(finished.wait(30000));

    // A leftover .usenetpart is the one failure that is silent: the file is not
    // shared, so nothing breaks, it just accumulates forever.
    QStringList leftovers;
    QDirIterator it(tmp.path(), {QStringLiteral("*.usenetpart")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
        leftovers << it.next();
    QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(QStringLiteral(", "))));

    queue.stop();
}

void tst_UsenetQueue::persistedStateResumesInsteadOfRefetching()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    UsenetQueueItem item;
    item.id = QStringLiteral("11111111-2222-3333-4444-555555555555");
    item.name = QStringLiteral("resume me");
    item.priority = 2;
    item.status = UsenetItemStatus::Downloading;

    NzbFileInfo info;
    info.subject = QStringLiteral("\"resume.bin\" yEnc (1/4)");
    info.fileName = QStringLiteral("resume.bin");
    info.groups << QStringLiteral("alt.binaries.test");
    for (int p = 1; p <= 4; ++p)
        info.segments.append(NzbSegment{messageIdFor(p), 3500, p});
    item.nzb.files.append(info);
    item.initFileStates(thePrefs.usenetTempDir());

    // Two of four already fetched, and one of those two is a hole — a segment
    // missing on every server is also "resolved", and must not come back.
    item.files[0].done.setBit(0);
    item.files[0].done.setBit(2);
    item.files[0].missingSegments = 1;
    item.files[0].decodedBytes = 5000;

    QVERIFY(UsenetQueueStore::save(item));

    UsenetQueueItem loaded;
    QString error;
    QVERIFY2(UsenetQueueStore::load(UsenetQueueStore::statePath(item.id), loaded, error),
             qPrintable(error));

    QCOMPARE(loaded.id, item.id);
    QCOMPARE(loaded.name, item.name);
    QCOMPARE(loaded.priority, 2);
    // Downloading is not a state anything resumes *into* — nothing is in flight
    // after a restart, so it must come back as Queued.
    QCOMPARE(loaded.status, UsenetItemStatus::Queued);

    QCOMPARE(loaded.nzb.files.size(), 1);
    QCOMPARE(loaded.nzb.files.at(0).segments.size(), 4);
    QCOMPARE(loaded.nzb.files.at(0).segments.at(2).messageId, messageIdFor(3));

    QCOMPARE(loaded.files.size(), 1);
    QCOMPARE(loaded.files.at(0).done.size(), 4);
    QVERIFY(loaded.files.at(0).done.testBit(0));
    QVERIFY(!loaded.files.at(0).done.testBit(1));
    QVERIFY(loaded.files.at(0).done.testBit(2));
    QVERIFY(!loaded.files.at(0).done.testBit(3));
    QCOMPARE(loaded.files.at(0).missingSegments, 1);
    QCOMPARE(loaded.doneSegmentCount(), 2);
}

void tst_UsenetQueue::connectionBudgetIsDividedNotReplicated()
{
    // Exceeding a provider's connection limit gets an account throttled or
    // suspended, which is a worse failure than downloading slowly — so this is
    // asserted rather than left to review.
    const QByteArray whole = payload(kPartSize * 8);

    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), 8, 1, 8);
    for (int p = 1; p <= 8; ++p)
        server.addArticle(messageIdFor(p), makeArticle(whole, p, 8,
                                                       QStringLiteral("budget.bin")));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    constexpr int kMaxConnections = 3;

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, kMaxConnections)}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("budget.bin"), 8),
                          QStringLiteral("budget"), error).isEmpty());
    QVERIFY(finished.wait(30000));

    QVERIFY2(server.connectionCount() <= kMaxConnections,
             qPrintable(QStringLiteral("opened %1 connections, limit is %2")
                            .arg(server.connectionCount()).arg(kMaxConnections)));

    queue.stop();
}

void tst_UsenetQueue::missingArticlesEscalateToTheNextLevel()
{
    // The failover rule is the whole of Usenet fault tolerance, so it gets its
    // own test: a 430 must move *up* a level, and the level-0 account must not be
    // asked for the same article twice.
    //
    // Two servers on two ports, because NewsServer::key() is host:port/user — one
    // port cannot host two identities, and the pool would treat them as one.
    const QByteArray whole = payload(kPartSize * 4);

    FakeNntpServer level0;          // knows nothing; answers 430 to everything
    level0.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    const quint16 port0 = level0.start();
    QVERIFY(port0 != 0);

    FakeNntpServer level1;          // the fill server that actually has it
    level1.addGroup(QStringLiteral("alt.binaries.test"), 4, 1, 4);
    for (int p = 1; p <= 4; ++p)
        level1.addArticle(messageIdFor(p), makeArticle(whole, p, 4,
                                                       QStringLiteral("fill.bin")));
    const quint16 port1 = level1.start();
    QVERIFY(port1 != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));

    NewsServer main = serverConfig(port0, 2);
    main.name = QStringLiteral("main");
    main.level = 0;

    NewsServer fill = serverConfig(port1, 2);
    fill.name = QStringLiteral("fill");
    fill.level = 1;

    UsenetQueue queue;
    queue.applyServers({main, fill}, 60);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    QVERIFY(!queue.addNzb(makeNzb(QStringLiteral("fill.bin"), 4),
                          QStringLiteral("failover"), error).isEmpty());
    QVERIFY2(finished.wait(30000), "the escalation never completed");
    QVERIFY2(finished.at(0).at(1).toBool(),
             "articles present on the level-1 server were reported missing");

    const QString out = QDir(thePrefs.incomingDir()).filePath(QStringLiteral("fill.bin"));
    QVERIFY(QFile::exists(out));
    QFile f(out);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), whole);
    f.close();

    // Each article asked of the level-0 server exactly once. More would mean the
    // tried-server exclusion is not being carried across the escalation.
    int bodyRequests = 0;
    for (const QString& cmd : level0.receivedCommands()) {
        if (cmd.startsWith(QStringLiteral("BODY"), Qt::CaseInsensitive))
            ++bodyRequests;
    }
    QCOMPARE(bodyRequests, 4);

    queue.stop();
}

QTEST_MAIN(tst_UsenetQueue)
#include "tst_UsenetQueue.moc"
