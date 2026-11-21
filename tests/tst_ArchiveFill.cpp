/// @file tst_ArchiveFill.cpp
/// @brief #224 ArchiveFill (daemon): a Usenet download of a stored archive whose entry is on
/// disk here. Once the index .par2 lands, the volume's blocks found in the entry are written
/// into its part file and their articles are not fetched; the file that comes out is whole.

#include "ArchiveFill.h"

#include "FakeNntpServer.h"
#include "TestFixtures.h"
#include "TestHelpers.h"

#include "Ed3kArchiveContents.h"
#include "Ed3kSession.h"
#include "decode/YencDecoder.h"
#include "prefs/Preferences.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#ifdef EMULE_HAVE_PAR2
#  include <par2/libpar2.h>
#  include <sstream>
#endif

using namespace eMule;
using namespace eMule::usenet;
using eMule::testing::FakeNntpServer;

namespace {

/// One article per par2 block: a range of found blocks covers whole articles.
constexpr int kPartSize = 4000;
constexpr int kHeader = 777;     ///< the volume's header before the stored entry
constexpr int kEntry = 200'000;  ///< 50 blocks: more than a connection pipelines

QByteArray noise(int size, quint32 seed)
{
    QByteArray out(size, Qt::Uninitialized);
    for (int i = 0; i < size; ++i) {
        seed = seed * 1664525u + 1013904223u;
        out[i] = char(seed >> 24);
    }
    return out;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

int partsOf(const QByteArray& whole)
{
    return int((whole.size() + kPartSize - 1) / kPartSize);
}

/// One yEnc article as a provider serves it.
QByteArray makeArticle(const QByteArray& whole, int part, const QString& name)
{
    const int offset = (part - 1) * kPartSize;
    const QByteArray chunk = whole.mid(offset, kPartSize);
    QByteArray out = QStringLiteral("=ybegin part=%1 total=%2 line=128 size=%3 name=%4\n")
                         .arg(part).arg(partsOf(whole)).arg(whole.size()).arg(name).toLatin1();
    out += QStringLiteral("=ypart begin=%1 end=%2\n").arg(offset + 1).arg(offset + chunk.size()).toLatin1();
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
            out += line + '\n';
            line.clear();
        }
    }
    if (!line.isEmpty())
        out += line + '\n';
    out += QStringLiteral("=yend size=%1 part=%2 pcrc32=%3")
               .arg(chunk.size()).arg(part).arg(yencCrc32(0, chunk), 8, 16, QLatin1Char('0')).toLatin1();
    return out;
}

QString messageId(int file, int part)
{
    return QStringLiteral("f%1p%2@example.com").arg(file).arg(part);
}

/// The NZB of @p files (name, parts), in that order.
QByteArray makeNzb(const QList<std::pair<QString, int>>& files)
{
    QByteArray xml = R"(<?xml version="1.0" encoding="iso-8859-1" ?>)"
                     "\n<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n";
    for (int f = 0; f < files.size(); ++f) {
        const auto& [name, parts] = files.at(f);
        xml += QStringLiteral("  <file poster=\"tester\" date=\"1700000000\" subject=\"&quot;%1&quot; yEnc (1/%2)\">\n")
                   .arg(name).arg(parts).toUtf8();
        xml += "    <groups><group>alt.binaries.test</group></groups>\n    <segments>\n";
        for (int p = 1; p <= parts; ++p)
            xml += QStringLiteral("      <segment bytes=\"%1\" number=\"%2\">%3</segment>\n")
                       .arg(kPartSize).arg(p).arg(messageId(f, p)).toUtf8();
        xml += "    </segments>\n  </file>\n";
    }
    xml += "</nzb>\n";
    return xml;
}

NewsServer serverConfig(quint16 port)
{
    NewsServer s;
    s.name = QStringLiteral("fake");
    s.host = QStringLiteral("127.0.0.1");
    s.port = port;
    s.tlsMode = NntpTlsMode::None;
    s.user = QStringLiteral("testuser");
    s.pass = QStringLiteral("testpass");
    s.maxConnections = 1;
    s.accountId = QStringLiteral("acct-%1").arg(port);
    return s;
}

#ifdef EMULE_HAVE_PAR2
bool createPar2Set(const QString& dir, const QString& baseName, const QStringList& files, int blockSize)
{
    std::ostringstream sout, serr;
    std::vector<std::string> extra;
    for (const QString& f : files)
        extra.push_back(QDir(dir).filePath(f).toStdString());
    QString base = QDir(dir).absolutePath();
    if (!base.endsWith(u'/'))
        base += u'/';
    return Par2::par2create(sout, serr, Par2::nlSilent, 256, base.toStdString(), 2, 1,
                            QDir(dir).filePath(baseName).toStdString(), extra, quint64(blockSize), 0,
                            Par2::scVariable, 0, 2)
           == Par2::eSuccess;
}
#endif

} // namespace

class tst_ArchiveFill : public QObject {
    Q_OBJECT

private slots:
    void aStoredVolumeIsFilledFromItsEntryOnDisk();
    void noSourcesFillsNothing();
};

// The entry of a stored (-m0) rel.rar is on disk here. The index rel.par2 comes first (the
// queue hoists it); its blocks found in the entry go into rel.rar's part file. Article 1 of
// rel.rar is held: until it lands the volume has no part size of its own (the one the queue
// would borrow is the small .par2's), so nothing is accepted before. Then the articles under
// the filled range are not asked for, and rel.rar comes out byte for byte.
void tst_ArchiveFill::aStoredVolumeIsFilledFromItsEntryOnDisk()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    eMule::testing::ScopedStatistics stats;
    QTemporaryDir work, elsewhere;
    QVERIFY(work.isValid() && elsewhere.isValid());
    const QByteArray entry = noise(kEntry, 21);
    const QByteArray volume = noise(kHeader, 22) + entry + noise(1234, 23);
    const QString setDir = QDir(work.path()).filePath(QStringLiteral("set"));
    QVERIFY(QDir().mkpath(setDir));
    QVERIFY(writeFile(QDir(setDir).filePath(QStringLiteral("rel.rar")), volume));
    QVERIFY(createPar2Set(setDir, QStringLiteral("rel"), {QStringLiteral("rel.rar")}, kPartSize));
    const QByteArray index = readFile(QDir(setDir).filePath(QStringLiteral("rel.par2")));
    QVERIFY(!index.isEmpty());
    const QString entryPath = QDir(elsewhere.path()).filePath(QStringLiteral("episode.mkv"));
    QVERIFY(writeFile(entryPath, entry));

    const int indexParts = partsOf(index);
    const int volumeParts = partsOf(volume);
    FakeNntpServer server;
    server.addGroup(QStringLiteral("alt.binaries.test"), indexParts + volumeParts, 1, indexParts + volumeParts);
    for (int p = 1; p <= indexParts; ++p)
        server.addArticle(messageId(0, p), makeArticle(index, p, QStringLiteral("rel.par2")));
    for (int p = 1; p <= volumeParts; ++p)
        server.addArticle(messageId(1, p), makeArticle(volume, p, QStringLiteral("rel.rar")));
    server.setHoldArticle(messageId(1, 1));
    const quint16 port = server.start();
    QVERIFY(port != 0);

    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});
    thePrefs.setUsenetUnrepairableAction(1);   // the default: the index is fetched first

    UsenetQueue queue;
    PostProcessingOptions post;
    post.unpack = false;   // a stored rar of noise: only the volume is compared
    queue.setPostProcessingOptions(post);
    queue.applyServers({serverConfig(port)}, 60);
    // the node is not started: the lookup finds nothing, the test gives the sources
    eMule::ed3k::Ed3kSession node;
    eMule::ed3k::Ed3kArchiveContents contents(node, tmp.filePath(QStringLiteral("ed3k")));
    ArchiveFill fill(contents, queue);
    QSignalSpy filled(&fill, &ArchiveFill::filled);
    QSignalSpy accepted(&fill, &ArchiveFill::accepted);
    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    queue.start();

    QString error;
    const QString id = queue.addNzb(makeNzb({{QStringLiteral("rel.par2"), indexParts}, {QStringLiteral("rel.rar"), volumeParts}}),
                                    QStringLiteral("rel"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));
    fill.setSources(id, {entryPath});   // before the index is there: the fill waits for it
    QCOMPARE(fill.sourcesOf(id), QStringList{entryPath});

    // blocks wholly inside the entry: from the first block at or past kHeader
    const qint64 first = (kHeader + kPartSize - 1) / kPartSize * kPartSize;
    const qint64 last = (kHeader + kEntry) / kPartSize * kPartSize;
    QTRY_COMPARE_WITH_TIMEOUT(filled.count(), 1, 30000);
    QCOMPARE(filled.at(0).at(0).toString(), id);
    QCOMPARE(filled.at(0).at(1).toLongLong(), last - first);
    // regression: no part size of rel.rar's own yet, so nothing taken as done
    QCOMPARE(accepted.count(), 0);
    const auto* item = queue.findItem(id);
    QVERIFY(item && item->files.size() == 2);
    {
        QFile part(item->files.at(1).tempPath);
        QVERIFY(part.open(QIODevice::ReadOnly));
        QVERIFY(part.seek(first));
        QCOMPARE(part.read(last - first), volume.mid(int(first), int(last - first)));
    }

    server.releaseHeld();
    QTRY_COMPARE_WITH_TIMEOUT(accepted.count(), 1, 30000);
    QCOMPARE(accepted.at(0).at(1).toInt(), 1);
    const int skipped = accepted.at(0).at(2).toInt();
    QVERIFY2(skipped >= 1, "every article of the range was in flight");
    QVERIFY(skipped <= int((last - first) / kPartSize));

    QVERIFY(finished.wait(60000));
    QVERIFY(finished.at(0).at(1).toBool());
    QString out;
    for (QDirIterator it(thePrefs.incomingDir(), {QStringLiteral("rel.rar")}, QDir::Files, QDirIterator::Subdirectories); it.hasNext();)
        out = it.next();
    QVERIFY2(!out.isEmpty(), "rel.rar not in incoming");
    QCOMPARE(readFile(out), volume);
    // the skipped articles were never asked for
    int asked = 0;
    for (int p = 1; p <= volumeParts; ++p)
        asked += int(server.receivedCommands().count(QStringLiteral("BODY <%1>").arg(messageId(1, p))));
    QCOMPARE(asked, volumeParts - skipped);
    queue.stop();
#endif
}

// No entry on disk: setSources with nothing is ignored, an unknown item too
void tst_ArchiveFill::noSourcesFillsNothing()
{
    eMule::testing::TempDir tmp;
    thePrefs.setConfigDir(tmp.path());
    UsenetQueue queue;
    eMule::ed3k::Ed3kSession node;
    eMule::ed3k::Ed3kArchiveContents contents(node, tmp.filePath(QStringLiteral("ed3k")));
    ArchiveFill fill(contents, queue);
    fill.setSources(QStringLiteral("no-such-item"), {QStringLiteral("/x")});
    QVERIFY(fill.sourcesOf(QStringLiteral("no-such-item")).isEmpty());
    fill.setSources(QStringLiteral("no-such-item"), {});
    QVERIFY(fill.sourcesOf(QStringLiteral("no-such-item")).isEmpty());
}

QTEST_MAIN(tst_ArchiveFill)
#include "tst_ArchiveFill.moc"
