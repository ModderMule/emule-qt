/// @file tst_UsenetPostPipeline.cpp
/// @brief Phase 4's exit criterion: a damaged release repaired, unpacked and published.
///
/// Everything below the queue has its own test. What only this one can cover is
/// the *cycle* phase 4 introduces: the download plan deliberately omits the PAR2
/// recovery volumes, so "every planned segment resolved" now means the download
/// phase ended, not the item. Verification then decides whether the item is
/// finished or goes back to downloading for exactly the volumes it needs.
///
/// Three things here would each be silent in production:
///
///   - **The healthy case must not fetch recovery volumes at all.** That is the
///     whole point of on-demand par2 — roughly a tenth of every release — and
///     nothing would fail if it quietly downloaded them anyway.
///   - **A repaired release must publish its payload and nothing else.** The
///     archive volumes and the recovery set are useless to an ED2K peer.
///   - **An unrepairable release must publish nothing.** Phase 3 shared short
///     files, holes and all, and no part of the system complained.

#include "FakeNntpServer.h"
#include "TestHelpers.h"

#include "decode/YencDecoder.h"
#include "queue/UsenetQueue.h"
#include "queue/UsenetQueueItem.h"

#include "prefs/Preferences.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>

#include <archive.h>
#include <archive_entry.h>

#ifdef EMULE_HAVE_PAR2
#  include <par2/libpar2.h>
#  include <sstream>
#endif

using namespace eMule;
using namespace eMule::usenet;
using eMule::testing::FakeNntpServer;

namespace {

constexpr int kPartSize = 3000;
constexpr const char* kGroup = "alt.binaries.test";

QByteArray payload(int size, int seed)
{
    QByteArray data;
    data.resize(size);
    for (int i = 0; i < size; ++i)
        data[i] = char((i * 17 + i / 64 * 5 + seed) & 0xFF);
    return data;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

bool writeZip(const QString& path, const QString& memberName, const QByteArray& data)
{
    auto* a = archive_write_new();
    if (!a)
        return false;
    // Stored, not deflated: a compressed member would make the archive smaller
    // than the damage the test wants to inflict on it.
    archive_write_set_format_zip(a);
    archive_write_zip_set_compression_store(a);
    if (archive_write_open_filename(a, path.toUtf8().constData()) != ARCHIVE_OK) {
        archive_write_free(a);
        return false;
    }

    auto* entry = archive_entry_new();
    archive_entry_set_pathname(entry, memberName.toUtf8().constData());
    archive_entry_set_size(entry, data.size());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    bool ok = archive_write_header(a, entry) == ARCHIVE_OK
        && archive_write_data(a, data.constData(), size_t(data.size())) == data.size();
    archive_entry_free(entry);

    archive_write_close(a);
    archive_write_free(a);
    return ok;
}

/// One yEnc article as a provider serves it, `=ypart begin` 1-based.
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

QString messageIdFor(const QString& fileName, int part)
{
    return QStringLiteral("%1.%2@example.com").arg(fileName).arg(part);
}

int partCountFor(qint64 size)
{
    return int((size + kPartSize - 1) / kPartSize);
}

/// A posted release: every file in @p dir turned into articles and an NZB.
struct PostedRelease {
    QByteArray nzb;
    QStringList fileNames;
    QStringList droppedIds;   ///< articles deliberately not served
};

/// @param dropFrom  file whose articles are partly withheld, simulating a
///                  take-down or an expired article
/// @param dropParts 1-based part numbers to withhold
PostedRelease postRelease(const QString& dir, FakeNntpServer& server,
                          const QString& dropFrom = {}, const QList<int>& dropParts = {})
{
    PostedRelease out;
    QByteArray xml;
    xml += R"(<?xml version="1.0" encoding="iso-8859-1" ?>)"
           "\n<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n";

    qint64 totalArticles = 0;
    const QFileInfoList entries = QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot,
                                                          QDir::Name);
    for (const QFileInfo& fi : entries) {
        const QString name = fi.fileName();
        const QByteArray content = readFile(fi.absoluteFilePath());
        const int parts = partCountFor(content.size());
        out.fileNames.append(name);
        totalArticles += parts;

        xml += QStringLiteral(
                   "  <file poster=\"tester\" date=\"1700000000\" "
                   "subject=\"&quot;%1&quot; yEnc (1/%2)\">\n")
                   .arg(name).arg(parts).toUtf8();
        xml += QStringLiteral("    <groups><group>%1</group></groups>\n")
                   .arg(QLatin1String(kGroup)).toUtf8();
        xml += "    <segments>\n";
        for (int p = 1; p <= parts; ++p) {
            const QString id = messageIdFor(name, p);
            xml += QStringLiteral("      <segment bytes=\"4200\" number=\"%1\">%2</segment>\n")
                       .arg(p).arg(id).toUtf8();

            if (name == dropFrom && dropParts.contains(p)) {
                out.droppedIds.append(id);
                continue;   // the server simply does not have it: 430
            }
            server.addArticle(id, makeArticle(content, p, parts, name));
        }
        xml += "    </segments>\n  </file>\n";
    }
    xml += "</nzb>\n";

    server.addGroup(QLatin1String(kGroup), totalArticles, 1, totalArticles);
    out.nzb = xml;
    return out;
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

#ifdef EMULE_HAVE_PAR2
/// baseName without ".par2" — par2create appends it unconditionally.
bool createPar2Set(const QString& dir, const QString& baseName, const QStringList& files,
                   int blockSize, int recoveryBlocks)
{
    std::ostringstream sout;
    std::ostringstream serr;

    std::vector<std::string> extra;
    for (const QString& f : files)
        extra.push_back(QDir(dir).filePath(f).toStdString());

    QString base = QDir(dir).absolutePath();
    if (!base.endsWith(u'/'))
        base += u'/';

    return Par2::par2create(sout, serr, Par2::nlSilent, 256, base.toStdString(), 2, 1,
                            QDir(dir).filePath(baseName).toStdString(), extra,
                            quint64(blockSize), 0, Par2::scVariable, 0,
                            quint32(recoveryBlocks))
        == Par2::eSuccess;
}
#endif

/// Whether any recovery-volume article was ever asked for.
bool requestedAnyRecoveryVolume(const QStringList& commands)
{
    for (const QString& cmd : commands) {
        if (cmd.startsWith(QLatin1String("BODY"), Qt::CaseInsensitive)
            && cmd.contains(QLatin1String(".vol"), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QStringList namesIn(const QString& dir)
{
    QStringList names;
    for (const QFileInfo& fi : QDir(dir).entryInfoList(QDir::Files | QDir::NoDotAndDotDot))
        names.append(fi.fileName());
    names.sort();
    return names;
}

} // namespace

class tst_UsenetPostPipeline : public QObject {
    Q_OBJECT

private slots:
    void skipsRecoveryVolumesForAHealthyRelease();
    void repairsUnpacksAndPublishesOnlyThePayload();
    void refusesToShareAnUnrepairableRelease();
};

// ---------------------------------------------------------------------------

void tst_UsenetPostPipeline::skipsRecoveryVolumesForAHealthyRelease()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    eMule::testing::TempDir tmp;
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stage));

    const QByteArray movie = payload(24000, 3);
    QVERIFY(writeFile(QDir(stage).filePath(QStringLiteral("Movie.mkv")), movie));
    QVERIFY(writeZip(QDir(stage).filePath(QStringLiteral("Rel.zip")),
                     QStringLiteral("Movie.mkv"), movie));
    QVERIFY(QFile::remove(QDir(stage).filePath(QStringLiteral("Movie.mkv"))));
    QVERIFY(createPar2Set(stage, QStringLiteral("Rel"), {QStringLiteral("Rel.zip")},
                          4000, 8));

    FakeNntpServer server;
    const PostedRelease release = postRelease(stage, server);
    const quint16 port = server.start();
    QVERIFY(port != 0);

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.setPostProcessingOptions(true, true, true, true);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(release.nzb, QStringLiteral("Rel"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QVERIFY2(finished.wait(60000), "no terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(),
             qPrintable(finished.at(0).at(2).toString()));

    // The bandwidth win, and the one thing nothing else would notice: an intact
    // release must never pull its recovery volumes. They are typically a tenth
    // of the post and are discarded unread.
    QVERIFY2(!requestedAnyRecoveryVolume(server.receivedCommands()),
             "recovery volumes were fetched for an undamaged release");

    QCOMPARE(readFile(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("Movie.mkv"))),
             movie);

    queue.stop();
#endif
}

void tst_UsenetPostPipeline::repairsUnpacksAndPublishesOnlyThePayload()
{
#ifndef EMULE_HAVE_PAR2
    QSKIP("built without libpar2-turbo");
#else
    eMule::testing::TempDir tmp;
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stage));

    const QByteArray movie = payload(24000, 9);
    QVERIFY(writeFile(QDir(stage).filePath(QStringLiteral("Movie.mkv")), movie));
    QVERIFY(writeZip(QDir(stage).filePath(QStringLiteral("Rel.zip")),
                     QStringLiteral("Movie.mkv"), movie));
    QVERIFY(QFile::remove(QDir(stage).filePath(QStringLiteral("Movie.mkv"))));
    QVERIFY(createPar2Set(stage, QStringLiteral("Rel"), {QStringLiteral("Rel.zip")},
                          4000, 8));

    FakeNntpServer server;
    // Withhold one article in the middle of the archive: the file keeps its
    // length and gets a hole, which is exactly what a missing article leaves.
    const PostedRelease release =
        postRelease(stage, server, QStringLiteral("Rel.zip"), {3});
    QCOMPARE(release.droppedIds.size(), 1);

    const quint16 port = server.start();
    QVERIFY(port != 0);

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.setPostProcessingOptions(true, true, true, true);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(release.nzb, QStringLiteral("Rel"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QVERIFY2(finished.wait(120000), "no terminal outcome");
    QVERIFY2(finished.at(0).at(1).toBool(),
             qPrintable(QStringLiteral("repair failed: %1").arg(finished.at(0).at(2).toString())));

    // The cycle ran: damage was found and volumes were fetched to answer it.
    QVERIFY2(requestedAnyRecoveryVolume(server.receivedCommands()),
             "a damaged release did not go back for recovery volumes");

    // The payload, byte for byte, out of an archive that was repaired first.
    QCOMPARE(readFile(QDir(thePrefs.incomingDir()).filePath(QStringLiteral("Movie.mkv"))),
             movie);

    // And nothing else. Archive volumes and recovery data are of no use to an
    // ED2K peer, and publishing them doubles the disk cost of every release.
    const QStringList published = namesIn(thePrefs.incomingDir());
    QCOMPARE(published, QStringList{QStringLiteral("Movie.mkv")});

    const auto* item = queue.findItem(id);
    QVERIFY(item);
    QCOMPARE(item->status, UsenetItemStatus::Complete);

    queue.stop();
#endif
}

void tst_UsenetPostPipeline::refusesToShareAnUnrepairableRelease()
{
    eMule::testing::TempDir tmp;
    const QString stage = tmp.filePath(QStringLiteral("stage"));
    QVERIFY(QDir().mkpath(stage));

    // No PAR2 at all, and an article missing. There is nothing to repair with.
    const QByteArray movie = payload(15000, 4);
    QVERIFY(writeFile(QDir(stage).filePath(QStringLiteral("Movie.mkv")), movie));

    FakeNntpServer server;
    const PostedRelease release =
        postRelease(stage, server, QStringLiteral("Movie.mkv"), {2});
    QCOMPARE(release.droppedIds.size(), 1);

    const quint16 port = server.start();
    QVERIFY(port != 0);

    thePrefs.setConfigDir(tmp.path());
    thePrefs.setTempDirs({tmp.filePath(QStringLiteral("temp"))});
    thePrefs.setIncomingDir(tmp.filePath(QStringLiteral("incoming")));
    thePrefs.setSharedDirs({});

    UsenetQueue queue;
    queue.applyServers({serverConfig(port, 4)}, 60);
    queue.setPostProcessingOptions(true, true, true, true);
    queue.start();

    QSignalSpy finished(&queue, &UsenetQueue::itemFinished);
    QString error;
    const QString id = queue.addNzb(release.nzb, QStringLiteral("Rel"), error);
    QVERIFY2(!id.isEmpty(), qPrintable(error));

    QVERIFY2(finished.wait(60000), "no terminal outcome");

    // Phase 3 reported success here and published a file with a hole in it,
    // which was then offered to ED2K and Kad peers. It must fail instead.
    QVERIFY2(!finished.at(0).at(1).toBool(),
             "a release with a missing article reported success");

    const auto* item = queue.findItem(id);
    QVERIFY(item);
    QCOMPARE(item->status, UsenetItemStatus::Failed);
    QVERIFY(!item->error.isEmpty());

    // Nothing at all in incoming: not the short file, not a staging leftover.
    QVERIFY2(namesIn(thePrefs.incomingDir()).isEmpty(),
             qPrintable(namesIn(thePrefs.incomingDir()).join(u',')));

    queue.stop();
}

QTEST_MAIN(tst_UsenetPostPipeline)
#include "tst_UsenetPostPipeline.moc"
