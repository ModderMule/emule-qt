/// @file tst_UsenetLiveFetch.cpp
/// @brief Phase 2's exit criterion against a real provider: an .nzb downloads
///        to a byte-identical file, CRC-verified per part.
///
/// The offline counterpart (tst_UsenetArticleFetch) proves the same pipeline
/// against a fake server, which is what runs in CI. What only a real provider
/// can show is that our reading of the format matches what posters actually
/// emit: subjects we cannot parse, articles with a glued `=ypart`, parts whose
/// `=ybegin size=` disagrees with the NZB's `bytes`, and articles that have
/// simply aged out of retention.
///
/// Environment (all four are required; the cases skip otherwise):
///
///   EMULE_NNTP_HOST / EMULE_NNTP_PORT / EMULE_NNTP_TLS / EMULE_NNTP_USER /
///   EMULE_NNTP_PASS   as in tst_UsenetLiveConnect
///   EMULE_NZB_FILE    path to a .nzb to download
///   EMULE_NZB_SHA256  expected SHA-256 of the assembled first file (optional;
///                     without it the test only checks that every article
///                     arrived and verified)
///
/// Labelled "live" and built only under EMULE_LIVE_TESTS.

#include "nntp/ArticleFetcher.h"
#include "nntp/NntpSocket.h"
#include "nzb/NzbFile.h"
#include "queue/ArticleWriter.h"

#include <QCryptographicHash>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace eMule::usenet;

namespace {

QString env(const char* name)
{
    return QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(name));
}

NewsServer providerFromEnv()
{
    NewsServer s;
    s.name = QStringLiteral("live");
    s.host = env("EMULE_NNTP_HOST");
    s.user = env("EMULE_NNTP_USER");
    s.pass = env("EMULE_NNTP_PASS");

    const QString mode = env("EMULE_NNTP_TLS").toLower();
    if (mode == QLatin1String("none"))
        s.tlsMode = TlsMode::None;
    else if (mode == QLatin1String("starttls"))
        s.tlsMode = TlsMode::StartTls;
    else
        s.tlsMode = TlsMode::Implicit;

    const int port = env("EMULE_NNTP_PORT").toInt();
    s.port = port > 0 ? static_cast<quint16>(port)
                      : (s.tlsMode == TlsMode::Implicit ? kDefaultNntpTlsPort
                                                        : kDefaultNntpPort);
    return s;
}

} // namespace

class tst_UsenetLiveFetch : public QObject {
    Q_OBJECT

private slots:
    void downloadsTheFirstFileOfAnNzb();
};

void tst_UsenetLiveFetch::downloadsTheFirstFileOfAnNzb()
{
    if (env("EMULE_NNTP_HOST").isEmpty() || env("EMULE_NZB_FILE").isEmpty())
        QSKIP("Set EMULE_NNTP_HOST and EMULE_NZB_FILE to run the live fetch test");

    NzbInfo nzb;
    QString error;
    QVERIFY2(NzbFile::parseFile(env("EMULE_NZB_FILE"), nzb, error), qPrintable(error));
    QVERIFY(!nzb.files.isEmpty());

    // The first non-PAR2 file: recovery volumes are large, slow and beside the
    // point here.
    const NzbFileInfo* target = nullptr;
    for (const auto& file : nzb.files) {
        if (!file.isPar2()) {
            target = &file;
            break;
        }
    }
    QVERIFY2(target, "NZB contains nothing but PAR2 volumes");
    qInfo() << "fetching" << target->fileName << target->segments.size() << "segments";

    NntpSocket socket;
    // Real articles are ~750 KB; a default read timeout tuned for a status line
    // is not the right budget.
    socket.setResponseTimeout(120'000);
    QSignalSpy ready(&socket, &NntpSocket::ready);
    QSignalSpy failed(&socket, &NntpSocket::failed);
    socket.connectToServer(providerFromEnv());
    QVERIFY2(ready.wait(30000),
             failed.isEmpty() ? "timed out" : qPrintable(failed.first().at(1).toString()));

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(target->fileName.isEmpty()
                                          ? QStringLiteral("article.bin")
                                          : target->fileName);

    ArticleWriter writer;
    QVERIFY2(writer.open(path, error), qPrintable(error));

    int fetched = 0;
    int missing = 0;
    QString articleName;

    for (const auto& segment : target->segments) {
        ArticleFetcher fetcher;
        QSignalSpy done(&fetcher, &ArticleFetcher::finished);
        fetcher.fetch(&socket, segment, &writer);
        QVERIFY2(done.wait(180000), "article fetch timed out");

        const auto err = done.first().at(0).value<NntpError>();
        if (err == NntpError::ArticleNotFound) {
            // Aged out or taken down. Real and common; the queue would escalate
            // to the next priority level here.
            ++missing;
            continue;
        }
        QVERIFY2(err == NntpError::None,
                 qPrintable(done.first().at(1).toString()));
        ++fetched;

        if (articleName.isEmpty())
            articleName = fetcher.articleFileName();

        // The first article states the file's real size, which is the only
        // authority for it: the NZB's `bytes` are encoded sizes.
        if (fetched == 1 && fetcher.declaredFileSize() > 0)
            QVERIFY2(writer.reserve(fetcher.declaredFileSize(), error), qPrintable(error));
    }

    QVERIFY2(writer.flush(error), qPrintable(error));
    writer.close();

    qInfo() << "fetched" << fetched << "articles," << missing << "missing;"
            << "article name:" << articleName;
    QVERIFY2(fetched > 0, "every article was missing — the release has aged out");
    QVERIFY2(missing == 0, "release is incomplete on this server; PAR2 repair "
                           "would be needed (phase 4)");

    QFile out(path);
    QVERIFY(out.open(QIODevice::ReadOnly));
    const QByteArray data = out.readAll();
    QVERIFY(!data.isEmpty());

    const QString expected = env("EMULE_NZB_SHA256").trimmed().toLower();
    if (expected.isEmpty())
        QSKIP("Downloaded and CRC-verified; set EMULE_NZB_SHA256 to check the bytes");

    const QString actual = QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
    QCOMPARE(actual, expected);
}

QTEST_MAIN(tst_UsenetLiveFetch)
#include "tst_UsenetLiveFetch.moc"
